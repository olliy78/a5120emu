/**
 * @file hfe_codec.cpp
 * @brief Implementierung von HfeCodec (HFE v1 ⇄ DiskMedium).
 *
 * @see core/peripherals/floppy_drive/hfe_codec.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/hfe_codec.h"
#include "core/peripherals/floppy_drive/bit_codec.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

/// Nominale Zellrate des K1520-Floppy-Stacks (Double Density) in kbit/s.
constexpr uint16_t kNominalBitrate = 250;
/// HFE-Fuellbyte fuer nicht belegte Zellen (Gap).
constexpr uint8_t  kHfeGap = 0x88;
/// Groesster geprueffter Ueberabtastfaktor (Greaseweazle-Exporte liegen bei 2..4).
constexpr uint32_t kMaxOversample = 4;
/// Ab so vielen Adressmarken gilt ein Abtastfaktor als bestaetigt (2 je Sektor).
constexpr size_t   kSichereMarken = 4;

uint16_t rd16(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}

void wr16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off]     = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

bool leseDatei(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { err = "Kann HFE-Datei nicht oeffnen: " + path; return false; }
    const auto n = static_cast<size_t>(f.tellg());
    out.resize(n);
    f.seekg(0);
    if (n && !f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n))) {
        err = "Lesefehler bei HFE-Datei: " + path;
        return false;
    }
    return true;
}

/// @brief Zaehlt echte Adressmarken einer decodierten Spur (Erkennung des Verfahrens).
size_t markenZahl(const TrackImage& t) {
    size_t n = 0;
    for (MarkType m : t.marks)
        if (m == MarkType::Id || m == MarkType::Data) ++n;
    return n;
}

/**
 * @brief Wie gut ist diese Decodierung?  Zahl der Sektoren mit **gueltiger CRC**.
 *
 * Die Markenzahl allein taugt als Urteil ueber einen Abtastfaktor NICHT: unter dem
 * falschen Faktor faellt aus einem Zellstrom reichlich Scheinsync heraus, gelegentlich
 * mehr als die echte Decodierung Marken hat.  Eine gueltige CRC entsteht dagegen
 * praktisch nie zufaellig — sie ist das belastbare Kriterium.
 *
 * Gesehen an einer EINSEITIGEN Greaseweazle-Aufnahme (`gw read --tracks c=0:h=0`):
 * dort belegt eine Spurseite den ganzen 512-B-Block statt 256, also doppelt so viele
 * Zellen je Umdrehung.  Der richtige Faktor war 4, die meisten (Schein-)Marken hatte 2.
 */
size_t gueltigeSektoren(const TrackImage& t) {
    size_t n = 0;
    for (const LogicalSector& s : TrackCodec::parseTrack(t))
        if (s.id_crc_ok && s.data_crc_ok) ++n;
    return n;
}

/**
 * @brief De-interleavt die Spurseiten-Bytes einer Spur aus dem Dateipuffer.
 *
 * HFE verschraenkt zwei Seiten zu je 256 B ([S0][S1]).  Bei EINSEITIGEN Dateien gibt
 * es ZWEI Sitten, und beide kommen vor:
 *
 *  * **verschraenkt** (@p slot 256) — Greaseweazle und HxC legen auch eine einseitige
 *    Aufnahme so ab: Seite 0 in den ersten 256 B jedes Blocks, der Rest Gap (0x88).
 *  * **kontinuierlich** (@p slot 512) — so schreibt dieses Projekt selbst.
 *
 * Wer eine verschraenkte Datei kontinuierlich liest, zieht sich alle 256 B einen
 * Schwung Gap-Bytes MITTEN in den Datenstrom: kurze ID-Felder ueberleben das meist,
 * ein 131-B-Datenfeld nie — Symptom „alle Sektoren gefunden, keine Daten-CRC gut".
 * Entschieden wird deshalb am Inhalt (@ref gueltigeSektoren), nicht am Kopf.
 */
std::vector<uint8_t> seitenBytes(const std::vector<uint8_t>& file, size_t track_start,
                                 uint32_t side_len, uint8_t num_sides, uint8_t head,
                                 uint32_t slot) {
    const size_t   head_off = (num_sides == 1) ? 0u : static_cast<size_t>(head) * 256;

    std::vector<uint8_t> side;
    side.reserve(side_len);

    uint32_t done = 0;
    for (uint32_t blk = 0; done < side_len; ++blk) {
        const size_t   blk_start = track_start + static_cast<size_t>(blk) * 512 + head_off;
        const uint32_t remaining = side_len - done;
        const uint32_t copy_len  = (remaining < slot) ? remaining : slot;
        if (blk_start + copy_len > file.size()) break;
        side.insert(side.end(),
                    file.begin() + static_cast<ptrdiff_t>(blk_start),
                    file.begin() + static_cast<ptrdiff_t>(blk_start + copy_len));
        done += copy_len;
    }
    return side;
}

}  // namespace

// ─── Signaturpruefung ────────────────────────────────────────────────────────

bool HfeCodec::isHfe(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char sig[8] = {};
    f.read(sig, sizeof(sig));
    return std::memcmp(sig, "HXCPICFE", 8) == 0;
}

// ─── Laden ───────────────────────────────────────────────────────────────────

bool HfeCodec::load(const std::string& path, DiskMedium& out, SourceInfo* info,
                    std::string& err) {
    std::vector<uint8_t> file;
    if (!leseDatei(path, file, err)) return false;
    if (file.size() < 1024) { err = "HFE-Datei zu kurz: " + path; return false; }
    if (std::memcmp(file.data(), "HXCPICFE", 8) != 0) {
        err = "Keine gueltige HFE-v1-Signatur (erwartet HXCPICFE): " + path;
        return false;
    }
    if (file[0x08] != 0) {
        err = "Nicht unterstuetzte HFE-Formatversion: " + std::to_string(file[0x08]);
        return false;
    }

    const uint8_t  num_tracks = file[0x09];
    const uint8_t  num_sides  = file[0x0A];
    const Encoding enc_hdr    = (file[0x0B] == 2) ? Encoding::FM : Encoding::MFM;
    const uint16_t bitrate    = rd16(file, 0x0C);
    const uint16_t rpm        = rd16(file, 0x0E);
    const uint16_t lut_block  = rd16(file, 0x12);

    if (num_tracks == 0 || num_sides == 0 || num_sides > 2) {
        err = "HFE-Geometrie unplausibel (" + std::to_string(num_tracks) + " Spuren, "
              + std::to_string(num_sides) + " Seiten): " + path;
        return false;
    }

    // Ueberabtastung: der K1520-Stack kennt nur 250 kbit/s Zellrate; eine hoehere
    // Rate ist dieselbe DD-Diskette mit mehrfacher Abtastung (Greaseweazle & Co.).
    //
    // Der Header-Wert `bitrate` taugt dafuer NICHT als alleinige Quelle: reale
    // Mitschnitte tragen dort auch schlicht falsche Angaben (gesehen: 311 kbit/s bei
    // tatsaechlich ~2,4-facher Abtastung).  Er dient daher nur noch als ERSTER
    // Kandidat; entschieden wird am Inhalt — genau wie beim Verfahren (FM/MFM):
    // der Faktor, unter dem ueberhaupt Adressmarken auftauchen, ist der richtige.
    //
    // Der Faktor gilt **je Spur**, nicht fuer die ganze Datei: eine Diskette kann
    // Spuren verschiedener DATENRATE tragen.  Die SCP1700-Disketten des A7100 sind
    // genau so gebaut — Spur 0 Kopf 0 in FM mit halber Datenrate (Flusszeiten 4/8 µs),
    // alle uebrigen in MFM (4/6/8 µs).  Wer den Faktor an der ersten Spur mit Marken
    // festnagelt, dekodiert danach die ganze Diskette unter dem Faktor der Bootspur:
    // 160 Spuren „unformatiert" bzw. ein Zufallssektor je Spur.
    uint32_t hdr_guess = 1;
    if (bitrate >= kNominalBitrate + kNominalBitrate / 2)
        hdr_guess = (bitrate + kNominalBitrate / 2) / kNominalBitrate;
    uint32_t letzter_ok       = 0;    // zuletzt erfolgreicher Faktor (0 = noch keiner)
    uint32_t spuren_mit_marken = 0;   // formatierte Spuren insgesamt
    uint32_t spuren_gestreckt  = 0;   // davon mit Faktor > 1

    const size_t lut_off = static_cast<size_t>(lut_block) * 512;
    if (lut_off + static_cast<size_t>(num_tracks) * 4 > file.size()) {
        err = "HFE-LUT liegt ausserhalb der Datei: " + path;
        return false;
    }

    out = DiskMedium(num_tracks, num_sides, enc_hdr);

    for (uint8_t c = 0; c < num_tracks; ++c) {
        const size_t   e            = lut_off + static_cast<size_t>(c) * 4;
        const size_t   track_start  = static_cast<size_t>(rd16(file, e)) * 512;
        const uint32_t len_bytes    = rd16(file, e + 2);
        const uint32_t side_len     = len_bytes / num_sides;
        if (side_len == 0) continue;

        for (uint8_t h = 0; h < num_sides; ++h) {
          // Bei zwei Seiten ist das Schlitzmass 256 und damit klar; bei einer Seite
          // sind beide Sitten moeglich (s. seitenBytes) — dann gewinnt die, die
          // lesbare Sektoren ergibt.
          TrackImage bestes;
          size_t     bestes_sektoren = 0, bestes_marken = 0;
          uint32_t   bestes_f = 0;

          for (uint32_t slot : {256u, 512u}) {
            if (num_sides == 2 && slot == 512) continue;
            // Verschraenkt belegt eine Seite die HAELFTE der Spurlaenge, kontinuierlich
            // das Ganze.  Das ist der eigentliche Unterschied der beiden Sitten.
            const uint32_t seite_len = (slot == 256) ? len_bytes / 2 : side_len;
            if (seite_len == 0) continue;
            const auto roh = seitenBytes(file, track_start, seite_len, num_sides, h, slot);

            // Eine Spurseite unter dem Abtastfaktor @p f decodieren (FM/MFM wird dabei
            // wie gehabt beidseitig probiert — Mischdichte-Medien).
            auto decodeMit = [&](uint32_t f) {
                auto     zellen   = roh;
                uint32_t bitcells = seite_len * 8;
                if (f > 1)
                    zellen = BitCodec::downsampleCells(zellen, bitcells, f, bitcells);
                TrackImage t = BitCodec::decode(zellen, bitcells, enc_hdr);
                if (markenZahl(t) == 0) {
                    const Encoding other = (enc_hdr == Encoding::MFM) ? Encoding::FM
                                                                      : Encoding::MFM;
                    TrackImage alt = BitCodec::decode(zellen, bitcells, other);
                    if (markenZahl(alt) > 0) t = std::move(alt);
                }
                return t;
            };

            // Kandidaten: zuerst der Faktor der Vorspur (auf einer gleichfoermigen
            // Diskette der Regelfall), dann die Header-Schaetzung, dann die uebrigen.
            // Genug Marken unter dem bewaehrten Faktor beenden die Suche sofort.
            //
            // Ein ANDERER Faktor muss sich dagegen deutlich ausweisen (@ref
            // kSichereMarken): unter dem falschen Faktor faellt aus einer MFM-Spur —
            // erst recht aus dem Rauschen einer unformatierten — durchaus mal eine
            // Scheinmarke heraus, und die duerfte den Faktor nicht umwerfen.
            TrackImage t;
            size_t     beste_sektoren = 0;   // Sektoren mit gueltiger CRC (das Urteil)
            size_t     beste_marken   = 0;   // nur der Stichentscheid
            uint32_t   bester_f       = 0;
            for (uint32_t f : {letzter_ok, hdr_guess, 1u, 2u, 3u, 4u}) {
                if (f == 0 || f > kMaxOversample) continue;
                TrackImage   probe   = decodeMit(f);
                const size_t marken  = markenZahl(probe);
                const size_t sektoren = marken ? gueltigeSektoren(probe) : 0;
                const size_t noetig  = (f == letzter_ok) ? 1 : kSichereMarken;
                const bool besser = (sektoren > beste_sektoren)
                                 || (sektoren == beste_sektoren && sektoren == 0
                                     && marken >= noetig && marken > beste_marken);
                if (besser && (sektoren > 0 || marken >= noetig)) {
                    t = std::move(probe);
                    beste_sektoren = sektoren; beste_marken = marken; bester_f = f;
                }
                // Der bewaehrte Faktor liefert lesbare Sektoren: nicht weiter suchen.
                if (f == letzter_ok && sektoren > 0) break;
            }
            if (bester_f == 0) t = decodeMit(hdr_guess);   // nichts gefunden

            const bool besser_als_bisher =
                (beste_sektoren > bestes_sektoren)
                || (beste_sektoren == bestes_sektoren && beste_marken > bestes_marken);
            if (bestes_f == 0 || besser_als_bisher) {
                bestes = std::move(t);
                bestes_sektoren = beste_sektoren;
                bestes_marken   = beste_marken;
                bestes_f        = bester_f ? bester_f : 1;
                if (bester_f) {
                    bestes.cell_factor = static_cast<uint8_t>(bester_f);
                }
            }
            if (beste_sektoren > 0) break;    // Sitte steht fest
          }

          if (bestes_marken > 0) {
              letzter_ok = bestes_f;
              ++spuren_mit_marken;
              if (bestes_f > 1) ++spuren_gestreckt;
          }
          // Markenlose Spur = unformatiert: als LEERE Spur ablegen, damit der
          // Controller gap-Flux streamt statt Rauschbytes zu liefern.
          if (markenZahl(bestes) == 0) bestes = {};
          out.setTrack(c, h, std::move(bestes));
        }
    }

    out.clearDirty();

    if (info) {
        info->write_allowed = (file[0x14] == 0xFF);
        // „Ueberabgetastet" heisst: die DATEI liegt ueber der Nominalrate — dann und
        // nur dann laesst sie sich nicht treu zurueckschreiben.  Massgeblich ist,
        // dass es KEINE Spur mit Faktor 1 gibt: eine Diskette mit gemischten Raten
        // (SCP1700: FM-Bootspur mit halber Rate, alles Uebrige nominal) ist eine
        // gewoehnliche Datei — ihr Faktor steht je Spur in TrackImage::cell_factor
        // und ueberlebt das Speichern.
        info->oversampled   = (spuren_mit_marken > 0
                               && spuren_gestreckt == spuren_mit_marken);
        info->rpm           = rpm;
    }
    return true;
}

// ─── Speichern ───────────────────────────────────────────────────────────────

bool HfeCodec::save(const std::string& path, const DiskMedium& in, std::string& err) {
    const uint8_t num_cyls  = in.numCylinders();
    const uint8_t num_heads = in.numHeads();
    if (num_cyls == 0 || num_heads == 0) { err = "Leeres Medium"; return false; }

    // 1. Zellen je Spurseite kodieren; laengste Seite bestimmt die einheitliche
    //    Spurlaenge (HFE-LUT haelt sie je Zylinder, wir nutzen ueberall dieselbe).
    //    1 Datenbyte = 16 Zellen; + Marge, aufgerundet auf 256 Byte.
    //
    //    Gerechnet wird in ZELLEN, nicht in Bytes: eine Spur mit halber Datenrate
    //    (@ref TrackImage::cell_factor) belegt je Byte doppelt so viele Zellen.  Wer
    //    hier „Bytes × 2" nimmt, gibt ihr nur die halbe Umdrehung — die letzten
    //    Sektoren fallen dann beim Kodieren hinten heraus.
    size_t max_cells = 0;
    for (uint8_t c = 0; c < num_cyls; ++c)
        for (uint8_t h = 0; h < num_heads; ++h) {
            const TrackImage& t = in.track(c, h);
            const size_t f = t.cell_factor ? t.cell_factor : 1;
            max_cells = std::max(max_cells, t.size() * 16 * f);
        }
    if (max_cells == 0) max_cells = 3125 * 16;   // leere Diskette: nominale Spurlaenge

    uint32_t side_len = static_cast<uint32_t>(max_cells / 8) + 256;
    side_len = (side_len + 255) / 256 * 256;

    const uint32_t track_len    = side_len * num_heads;
    const uint32_t track_blocks = (track_len + 511) / 512;
    const uint32_t track_pad    = track_blocks * 512;

    // 2. Header.
    std::vector<uint8_t> hdr(512, 0x00);
    std::memcpy(hdr.data(), "HXCPICFE", 8);
    hdr[0x08] = 0;                                                  // formatrevision v1
    hdr[0x09] = num_cyls;
    hdr[0x0A] = num_heads;
    hdr[0x0B] = (in.defaultEncoding() == Encoding::FM) ? 2 : 0;     // ISOIBM_FM / _MFM
    wr16(hdr, 0x0C, kNominalBitrate);
    wr16(hdr, 0x0E, 300);                                           // rpm
    hdr[0x10] = 0;                                                  // iface
    hdr[0x11] = 1;                                                  // dnu
    wr16(hdr, 0x12, 1);                                             // track_list_block
    hdr[0x14] = 0xFF;                                               // write_allowed
    hdr[0x15] = 0xFF;                                               // single_step
    for (size_t i = 0x16; i < 512; ++i) hdr[i] = 0xFF;

    // 3. Track-LUT.
    std::vector<uint8_t> lut(512, 0xFF);
    uint32_t blk = 2;                                               // Spurdaten ab Block 2
    for (uint8_t c = 0; c < num_cyls; ++c) {
        wr16(lut, static_cast<size_t>(c) * 4 + 0, static_cast<uint16_t>(blk));
        wr16(lut, static_cast<size_t>(c) * 4 + 2, static_cast<uint16_t>(track_len));
        blk += track_blocks;
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { err = "Kann HFE-Datei nicht schreiben: " + path; return false; }
    f.write(reinterpret_cast<const char*>(hdr.data()), 512);
    f.write(reinterpret_cast<const char*>(lut.data()), 512);

    // 4. Spurdaten seitenverschraenkt (einseitig: kontinuierlich).
    std::vector<uint8_t> spur(track_pad, kHfeGap);
    for (uint8_t c = 0; c < num_cyls; ++c) {
        std::fill(spur.begin(), spur.end(), kHfeGap);

        for (uint8_t h = 0; h < num_heads; ++h) {
            const TrackImage& t = in.track(c, h);
            if (t.empty()) continue;   // unformatierte Spur bleibt Gap

            // Spuren mit halber Datenrate (SCP1700-Bootspur) werden mit entsprechend
            // WENIGER Modellzellen kodiert und danach wieder gestreckt — sonst ginge
            // die Spur mit doppelter Rate in die Datei (@ref TrackImage::cell_factor).
            const uint32_t f    = t.cell_factor ? t.cell_factor : 1;
            const uint32_t ziel = side_len * 8;
            std::vector<uint8_t> cells = BitCodec::encode(t, ziel / f);
            if (f > 1) {
                uint32_t erzeugt = 0;
                cells = BitCodec::upsampleCells(cells, ziel / f, f, erzeugt);
            }
            cells.resize(side_len, kHfeGap);

            const uint32_t slot     = (num_heads == 1) ? 512u : 256u;
            const size_t   head_off = (num_heads == 1) ? 0u : static_cast<size_t>(h) * 256;
            uint32_t src = 0;
            for (uint32_t b = 0; src < side_len; ++b) {
                const size_t   dst_start = static_cast<size_t>(b) * 512 + head_off;
                const uint32_t remaining = side_len - src;
                const uint32_t copy_len  = (remaining < slot) ? remaining : slot;
                if (dst_start + copy_len > spur.size()) break;
                for (uint32_t i = 0; i < copy_len; ++i)
                    spur[dst_start + i] = (src + i < cells.size()) ? cells[src + i] : kHfeGap;
                src += copy_len;
            }
        }
        f.write(reinterpret_cast<const char*>(spur.data()),
                static_cast<std::streamsize>(spur.size()));
    }

    if (!f) { err = "Schreibfehler (unvollstaendig): " + path; return false; }
    return true;
}

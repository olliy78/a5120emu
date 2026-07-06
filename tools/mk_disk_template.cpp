// mk_disk_template – erzeugt eine GÜLTIG vorformatierte, EINSEITIGE Leerdiskette
// (HFE v1) mit dem üblichen CP/A-Systemlayout:
//   <sys_cyls> Systemspuren  = <sys_nsec>×<sys_size>   (typ. 26×128)
//   Restspuren  Datenbereich = <data_nsec>×<data_size> (z. B. 4×1024 / 5×1024 / 8×1024 / 16×256)
// Alles im selben Verfahren (FM oder MFM), Sektordaten = 0xE5 (leere CP/M-Sektoren).
//
// Warum programmatisch?  FORMAT.COM kann eine FRISCHE, gap-leere .hfe nicht direkt
// formatieren (Gap-Blank-.hfe-Hänger, docs/format.md §8.2).  Die Pipeline umgeht das,
// indem sie eine GÜLTIGE, bereits formatierte Vorlage nach B:/C: kopiert und diese neu
// formatiert.  Dieses Tool erzeugt genau solche Vorlagen für die einseitigen Formate
// (MF3200 8″-FM, MF6400 8″-MFM, K5600.10 5″-40-SS, K5600.20 5″-80-SS) — dieselbe
// Codec-Schicht (TrackCodec::buildTrack), die auch der FORMAT.COM-Schreibpfad benutzt.
//
// Verwendung:
//   mk_disk_template <out.hfe> <fm|mfm> <num_cyls> <sys_cyls> \
//                    <sys_nsec> <sys_size> <data_nsec> <data_size>
//
// Beispiele (die vier §-Formate):
//   mf3200 fmt7 :  mk_disk_template x.hfe fm  77 3 26 128 16 256
//   mf3200 fmt1 :  mk_disk_template x.hfe fm  77 3 26 128  4 1024
//   mf6400 fmt1 :  mk_disk_template x.hfe mfm 77 2 26 128  8 1024
//   k5600.10 f1 :  mk_disk_template x.hfe mfm 40 2 26 128  5 1024
//   k5600.20 f1 :  mk_disk_template x.hfe mfm 80 2 26 128  5 1024

#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

void put16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off]     = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

// Leeres, EINSEITIGES HFE-Template schreiben (num_cyls×1).  Header-Encoding steuert nur
// das Sniffing beim Öffnen (0=MFM, 2=FM); die Spuren werden ohnehin überschrieben.
// Die Spur liegt KONTINUIERLICH (volle 512 B/Block) — passend zur einseitigen Lese-/
// Schreiblogik in HfeImage (num_sides_==1, s. §8.6).
bool writeBlankHfe(const std::string& path, uint8_t num_cyls, bool fm, uint32_t side_len) {
    const uint32_t track_blocks  = (side_len + 511) / 512;
    const uint32_t track_len_pad = track_blocks * 512;

    std::vector<uint8_t> hdr(512, 0x00);
    std::memcpy(hdr.data(), "HXCPICFE", 8);
    hdr[0x08] = 0;                 // formatrevision v1
    hdr[0x09] = num_cyls;          // Spuren
    hdr[0x0A] = 1;                 // Seiten = 1 (einseitig)
    hdr[0x0B] = fm ? 2 : 0;        // track_encoding: 2=FM (IBM 3740) / 0=ISOIBM_MFM
    put16(hdr, 0x0C, 250);         // bitrate kbit/s
    put16(hdr, 0x0E, fm ? 360 : 300);
    hdr[0x10] = 0;                 // iface
    hdr[0x11] = 1;                 // dnu
    put16(hdr, 0x12, 1);           // track_list_block = 1
    hdr[0x14] = 0xFF;              // write_allowed
    hdr[0x15] = 0xFF;              // single_step
    for (size_t i = 0x16; i < 512; ++i) hdr[i] = 0xFF;

    std::vector<uint8_t> lut(512, 0xFF);
    uint32_t blk = 2;              // Spurdaten ab Block 2
    for (uint8_t c = 0; c < num_cyls; ++c) {
        put16(lut, c * 4 + 0, static_cast<uint16_t>(blk));
        put16(lut, c * 4 + 2, static_cast<uint16_t>(side_len));   // len_bytes (einseitig)
        blk += track_blocks;
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(hdr.data()), 512);
    f.write(reinterpret_cast<const char*>(lut.data()), 512);
    const std::vector<uint8_t> gap(track_len_pad, 0x88);
    for (uint8_t c = 0; c < num_cyls; ++c)
        f.write(reinterpret_cast<const char*>(gap.data()), track_len_pad);
    return static_cast<bool>(f);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 9) {
        std::fprintf(stderr,
            "usage: %s <out.hfe> <fm|mfm> <num_cyls> <sys_cyls> "
            "<sys_nsec> <sys_size> <data_nsec> <data_size>\n", argv[0]);
        return 1;
    }
    const std::string out = argv[1];
    // Verfahren: "fm" | "mfm" (uniform) ODER "<sys>/<data>" für MISCHDICHTE
    // (z. B. "fm/mfm" = FM-Systemspuren + MFM-Datenspuren, 8″-DD System-34).
    std::string enc_s = argv[2];
    auto parse_enc = [](const std::string& s, bool& fm) -> bool {
        if (s == "fm" || s == "FM")   { fm = true;  return true; }
        if (s == "mfm" || s == "MFM") { fm = false; return true; }
        return false;
    };
    bool sys_fm = false, data_fm = false;
    const size_t slash = enc_s.find('/');
    bool enc_ok;
    if (slash != std::string::npos) {
        enc_ok = parse_enc(enc_s.substr(0, slash), sys_fm)
              && parse_enc(enc_s.substr(slash + 1), data_fm);
    } else {
        enc_ok = parse_enc(enc_s, sys_fm);
        data_fm = sys_fm;
    }
    if (!enc_ok) {
        std::fprintf(stderr, "FEHLER: Verfahren muss 'fm', 'mfm' oder '<sys>/<data>' sein\n");
        return 1;
    }
    // HFE-Header-Verfahren = Datenspuren-Verfahren (dominiert); Mischspuren werden
    // beim Lesen per HfeImage-Autoerkennung korrekt decodiert.
    const bool fm = data_fm;
    const int num_cyls  = std::atoi(argv[3]);
    const int sys_cyls  = std::atoi(argv[4]);
    const int sys_nsec  = std::atoi(argv[5]);
    const int sys_size  = std::atoi(argv[6]);
    const int data_nsec = std::atoi(argv[7]);
    const int data_size = std::atoi(argv[8]);
    if (num_cyls < 1 || num_cyls > 85 || sys_cyls < 0 || sys_cyls > num_cyls) {
        std::fprintf(stderr, "FEHLER: unplausible Geometrie\n");
        return 1;
    }

    // Spuren VORAB bauen, damit die HFE-Seitenlänge dynamisch passt: ein Spur-Byte
    // belegt (FM wie MFM) 16 Bitzellen = 2 HFE-Bytes.  side_len (HFE-Bytes) muss daher
    // ≥ 2×(längste Spur) sein, sonst wird die Spur beim Kodieren abgeschnitten (z. B.
    // 8×1024-MFM ≈ 8.9 KB Spur → ~18 KB HFE).  +512 B Gap-Reserve.
    const Encoding sys_enc  = sys_fm  ? Encoding::FM : Encoding::MFM;
    const Encoding data_enc = data_fm ? Encoding::FM : Encoding::MFM;
    std::vector<TrackImage> tracks;
    tracks.reserve(static_cast<size_t>(num_cyls));
    size_t max_bytes = 0;
    for (int c = 0; c < num_cyls; ++c) {
        const bool is_sys = (c < sys_cyls);
        const int nsec = is_sys ? sys_nsec : data_nsec;
        const int size = is_sys ? sys_size : data_size;
        std::vector<LogicalSector> secs;
        secs.reserve(static_cast<size_t>(nsec));
        for (int i = 1; i <= nsec; ++i) {
            LogicalSector s;
            s.cyl  = static_cast<uint8_t>(c);
            s.head = 0;
            s.id   = static_cast<uint8_t>(i);
            s.size = static_cast<uint16_t>(size);
            s.data.assign(static_cast<size_t>(size), 0xE5);
            secs.push_back(s);
        }
        tracks.push_back(TrackCodec::buildTrack(secs, is_sys ? sys_enc : data_enc));
        max_bytes = std::max(max_bytes, tracks.back().bytes.size());
    }
    const uint32_t side_len = static_cast<uint32_t>(2 * max_bytes + 512);

    if (!writeBlankHfe(out, static_cast<uint8_t>(num_cyls), fm, side_len)) {
        std::fprintf(stderr, "FEHLER: Blank-HFE nicht schreibbar: %s\n", out.c_str());
        return 1;
    }

    auto img = DiskImage::open(out, std::nullopt, /*write_protect=*/false);
    if (!img) {
        std::fprintf(stderr, "FEHLER: erzeugtes HFE nicht öffenbar: %s\n", out.c_str());
        return 1;
    }

    for (int c = 0; c < num_cyls; ++c) {
        if (!img->writeTrack(static_cast<uint8_t>(c), 0, tracks[static_cast<size_t>(c)])) {
            std::fprintf(stderr, "FEHLER: writeTrack Spur %d\n", c);
            return 1;
        }
    }
    if (!img->flush()) {
        std::fprintf(stderr, "FEHLER: flush\n");
        return 1;
    }

    std::printf("OK: %s (%d Spuren; %s %d×%dB Sys Sp.0-%d, %s %d×%dB Daten Sp.%d-%d)\n",
                out.c_str(), num_cyls,
                sys_fm ? "FM" : "MFM", sys_nsec, sys_size, sys_cyls - 1,
                data_fm ? "FM" : "MFM", data_nsec, data_size, sys_cyls, num_cyls - 1);
    return 0;
}

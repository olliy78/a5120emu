/**
 * @file cqm_codec.cpp
 * @brief Implementierung von CqmCodec (CopyQM-Abbild, Lesen und Schreiben).
 *
 * @see core/peripherals/floppy_drive/cqm_codec.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/cqm_codec.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <utility>

namespace {

// Kopfoffsets (cqm_codec.h)
constexpr size_t kOffSecSize  = 0x03;
constexpr size_t kOffSpt      = 0x10;
constexpr size_t kOffHeads    = 0x12;
constexpr size_t kOffDescr    = 0x1C;
constexpr size_t kDescrLen    = 60;
constexpr size_t kOffBlind    = 0x58;
constexpr size_t kOffDensity  = 0x59;
constexpr size_t kOffUsedCyl  = 0x5A;
constexpr size_t kOffTotCyl   = 0x5B;
constexpr size_t kOffCrc      = 0x5C;
constexpr size_t kOffLabel    = 0x60;
constexpr size_t kLabelLen    = 11;
constexpr size_t kOffTime     = 0x6B;
constexpr size_t kOffDate     = 0x6D;
constexpr size_t kOffCmtLen   = 0x6F;
constexpr size_t kOffSecBase  = 0x71;
constexpr size_t kOffInterlv  = 0x74;
constexpr size_t kOffSkew     = 0x75;

/// Obergrenze der entpackten Nutzdaten — schuetzt vor einem kaputten Kopf, der
/// Gigabytes anfordern wuerde (die groesste echte Diskette hat 2,88 MB).
constexpr size_t kMaxRaw = 16u * 1024u * 1024u;

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// @brief NUL-terminierte Zeichenkette fester Feldlaenge.
std::string festerText(const uint8_t* p, size_t max) {
    size_t n = 0;
    while (n < max && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

/// @brief Tabelle des reflektierten CRC-32 (0xEDB88320).
const std::array<uint32_t, 256>& crcTabelle() {
    static const std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> a{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
            a[i] = c;
        }
        return a;
    }();
    return t;
}

bool leseDatei(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { err = "Kann Datei nicht oeffnen: " + path; return false; }
    const auto n = static_cast<size_t>(f.tellg());
    out.resize(n);
    f.seekg(0);
    if (n && !f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n))) {
        err = "Lesefehler: " + path;
        return false;
    }
    return true;
}

}  // namespace

// ─── Pruefwert / Erkennung ───────────────────────────────────────────────────

uint32_t CqmCodec::crc(const uint8_t* data, size_t n) {
    const auto& t = crcTabelle();
    uint32_t c = 0;
    // CopyQM-Tabellenfehler: Index nur 6 Bit breit (libdsk drvqm.c, drv_qm_update_crc).
    for (size_t i = 0; i < n; ++i) c = t[(data[i] ^ c) & 0x3F] ^ (c >> 8);
    return c;
}

bool CqmCodec::isCqm(const uint8_t* d, size_t n) {
    if (n < kHeaderSize || d[0] != 'C' || d[1] != 'Q') return false;
    uint8_t summe = 0;
    for (size_t i = 0; i < kHeaderSize; ++i) summe = static_cast<uint8_t>(summe + d[i]);
    return summe == 0;
}

bool CqmCodec::isCqm(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint8_t kopf[kHeaderSize];
    if (!f.read(reinterpret_cast<char*>(kopf), kHeaderSize)) return false;
    return isCqm(kopf, kHeaderSize);
}

// ─── Laden ───────────────────────────────────────────────────────────────────

bool CqmCodec::decodeBuffer(const std::vector<uint8_t>& d, std::vector<uint8_t>& raw,
                            SourceInfo* info, std::string& err) {
    SourceInfo si;
    if (d.size() < kHeaderSize || d[0] != 'C' || d[1] != 'Q') {
        err = "Keine CopyQM-Datei (Kennung 'CQ' fehlt)";
        return false;
    }
    if (!isCqm(d.data(), d.size())) {
        err = "CopyQM-Kopf beschaedigt (Kopfpruefbyte stimmt nicht)";
        return false;
    }

    si.sector_size     = u16(&d[kOffSecSize]);
    si.secs_per_track  = u16(&d[kOffSpt]);
    si.heads           = u16(&d[kOffHeads]);
    si.blind           = d[kOffBlind];
    si.density         = d[kOffDensity];
    si.used_cyls       = d[kOffUsedCyl];
    si.total_cyls      = d[kOffTotCyl];
    si.crc_stored      = u32(&d[kOffCrc]);
    si.dos_time        = u16(&d[kOffTime]);
    si.dos_date        = u16(&d[kOffDate]);
    si.first_sector_id = static_cast<uint8_t>(d[kOffSecBase] + 1);
    si.interleave      = d[kOffInterlv];
    si.skew            = d[kOffSkew];
    si.description     = festerText(&d[kOffDescr], kDescrLen);
    si.label           = festerText(&d[kOffLabel], kLabelLen);
    // Das Etikettfeld ist 11 Byte ohne Pflicht-NUL ("** NONE ** " mit Leerzeichen kommt vor).
    while (!si.label.empty() && si.label.back() == ' ') si.label.pop_back();
    if (si.label == "** NONE **") si.label.clear();
    const size_t cmt   = u16(&d[kOffCmtLen]);

    const uint16_t sz = si.sector_size;
    if (!(sz == 128 || sz == 256 || sz == 512 || sz == 1024 || sz == 2048 || sz == 4096)) {
        err = "CopyQM: unplausible Sektorgroesse " + std::to_string(sz);
        return false;
    }
    if (si.secs_per_track < 1 || si.secs_per_track > 64
        || si.first_sector_id + si.secs_per_track - 1 > 255) {
        err = "CopyQM: unplausible Sektorzahl je Spur " + std::to_string(si.secs_per_track);
        return false;
    }
    if (si.heads < 1 || si.heads > 2) {
        err = "CopyQM: unplausible Seitenzahl " + std::to_string(si.heads);
        return false;
    }
    if (si.used_cyls < 1) {
        err = "CopyQM: keine gespeicherten Zylinder";
        return false;
    }
    if (si.total_cyls < si.used_cyls) si.total_cyls = si.used_cyls;

    if (kHeaderSize + cmt > d.size()) {
        err = "CopyQM: Datei abgeschnitten (Kommentar reicht ueber das Dateiende)";
        return false;
    }
    si.raw_header.assign(d.begin(), d.begin() + kHeaderSize);
    si.comment_raw.assign(d.begin() + kHeaderSize, d.begin() + kHeaderSize + cmt);
    si.comment.assign(reinterpret_cast<const char*>(&d[kHeaderSize]), cmt);
    // Ein Kommentar ist meist NUL- oder ^Z-terminiert; nur den Text davor melden.
    while (!si.comment.empty() && (si.comment.back() == '\0' || si.comment.back() == 0x1A))
        si.comment.pop_back();

    const size_t erwartet = static_cast<size_t>(si.used_cyls) * si.heads
                          * si.secs_per_track * sz;
    if (erwartet > kMaxRaw) {
        err = "CopyQM: Geometrie unplausibel gross (" + std::to_string(erwartet) + " Byte)";
        return false;
    }

    // RLE-Strom: i16 n; n<0 → naechstes Byte -n-mal; n>0 → n Rohbytes; 0 → nichts.
    raw.clear();
    raw.reserve(erwartet);
    size_t p = kHeaderSize + cmt;
    while (raw.size() < erwartet) {
        if (p + 2 > d.size()) {
            err = "CopyQM: Datei abgeschnitten (Datenstrom endet nach "
                  + std::to_string(raw.size()) + " von " + std::to_string(erwartet)
                  + " Byte)";
            return false;
        }
        const int n = static_cast<int16_t>(u16(&d[p]));
        p += 2;
        if (n < 0) {
            if (p >= d.size()) {
                err = "CopyQM: Datei abgeschnitten (Wiederholungsblock ohne Byte)";
                return false;
            }
            const size_t cnt = static_cast<size_t>(-n);
            if (raw.size() + cnt > erwartet) {
                err = "CopyQM: Datenstrom laenger als die Geometrie des Kopfs";
                return false;
            }
            raw.insert(raw.end(), cnt, d[p]);
            ++p;
        } else if (n > 0) {
            const size_t cnt = static_cast<size_t>(n);
            if (p + cnt > d.size()) {
                err = "CopyQM: Datei abgeschnitten (Rohblock reicht ueber das Dateiende)";
                return false;
            }
            if (raw.size() + cnt > erwartet) {
                err = "CopyQM: Datenstrom laenger als die Geometrie des Kopfs";
                return false;
            }
            raw.insert(raw.end(), d.begin() + static_cast<std::ptrdiff_t>(p),
                       d.begin() + static_cast<std::ptrdiff_t>(p + cnt));
            p += cnt;
        }
    }
    si.trailing_bytes = d.size() - p;

    // Pruefwert 0 = alte CopyQM-Version ohne Pruefwert → nicht pruefbar.
    si.crc_computed = crc(raw.data(), raw.size());
    if (si.crc_stored != 0) {
        si.crc_checked = true;
        if (si.crc_stored != si.crc_computed) {
            err = "CopyQM: Pruefwert stimmt nicht (Datei beschaedigt)";
            if (info) *info = si;
            return false;
        }
    }

    if (info) *info = si;
    return true;
}

bool CqmCodec::loadBuffer(const std::vector<uint8_t>& datei, DiskMedium& out,
                          SourceInfo* info, std::string& err) {
    SourceInfo si;
    std::vector<uint8_t> raw;
    if (!decodeBuffer(datei, raw, &si, err)) {
        if (info) *info = si;
        return false;
    }

    const uint8_t ncyls  = si.total_cyls;
    const uint8_t nheads = static_cast<uint8_t>(si.heads);
    out = DiskMedium(ncyls, nheads, Encoding::MFM);
    // 5,25″-HD (15 x 512) dreht mit 360 U/min, alles andere hier mit 300 (siehe
    // doc/design/33_cqm.md); nur fuer die Zellenzahl einer spaeteren .hfe von Belang.
    out.setNominalRpm((si.density == 1 && si.secs_per_track == 15 && si.sector_size == 512)
                          ? 360 : 300);

    size_t off = 0;
    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            std::vector<LogicalSector> sektoren;
            sektoren.reserve(si.secs_per_track);
            const bool gespeichert = c < si.used_cyls;
            for (uint16_t i = 0; i < si.secs_per_track; ++i) {
                LogicalSector ls;
                ls.cyl  = c;
                ls.head = h;
                ls.id   = static_cast<uint8_t>(si.first_sector_id + i);
                ls.size = si.sector_size;
                if (gespeichert) {
                    ls.data.assign(raw.begin() + static_cast<std::ptrdiff_t>(off),
                                   raw.begin() + static_cast<std::ptrdiff_t>(off + si.sector_size));
                    off += si.sector_size;
                } else {
                    // Nicht gespeicherte Zylinder: leer formatiert (0xF6, wie CopyQM
                    // und libdsk sie auffuellen).
                    ls.data.assign(si.sector_size, 0xF6);
                }
                sektoren.push_back(std::move(ls));
            }
            out.setTrack(c, h, TrackCodec::buildTrack(sektoren, Encoding::MFM));
        }
    }
    out.clearDirty();
    if (info) *info = si;
    return true;
}

bool CqmCodec::load(const std::string& path, DiskMedium& out, SourceInfo* info,
                    std::string& err) {
    std::vector<uint8_t> datei;
    if (!leseDatei(path, datei, err)) return false;
    if (!loadBuffer(datei, out, info, err)) { err += ": " + path; return false; }
    return true;
}

// ─── Schreiben ───────────────────────────────────────────────────────────────

std::vector<uint8_t> CqmCodec::packRle(const std::vector<uint8_t>& raw) {
    // Ein Block traegt hoechstens 32767 (int16, positiv); eine Wiederholung kostet 3 Byte,
    // ein unterbrochenes Literal zusaetzlich 2 fuer den Kopf der Fortsetzung — ab 5
    // gleichen Bytes lohnt die Wiederholung in jedem Fall.
    constexpr size_t kMaxBlock = 32767;
    constexpr size_t kMinRun   = 5;
    std::vector<uint8_t> out;
    auto put16 = [&](int16_t n) {
        const uint16_t u = static_cast<uint16_t>(n);
        out.push_back(static_cast<uint8_t>(u));
        out.push_back(static_cast<uint8_t>(u >> 8));
    };
    size_t lit = 0;   // Beginn des noch nicht ausgegebenen Literals
    auto flushLit = [&](size_t end) {
        while (lit < end) {
            const size_t n = std::min(end - lit, kMaxBlock);
            put16(static_cast<int16_t>(n));
            out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(lit),
                       raw.begin() + static_cast<std::ptrdiff_t>(lit + n));
            lit += n;
        }
    };
    size_t i = 0;
    while (i < raw.size()) {
        size_t j = i + 1;
        while (j < raw.size() && raw[j] == raw[i]) ++j;
        if (j - i >= kMinRun) {
            flushLit(i);
            for (size_t rest = j - i; rest > 0;) {
                const size_t n = std::min(rest, kMaxBlock);
                put16(static_cast<int16_t>(-static_cast<int>(n)));
                out.push_back(raw[i]);
                rest -= n;
            }
            lit = j;
        }
        i = j;
    }
    flushLit(raw.size());
    return out;
}

namespace {

/// Geometrie und Sektorstrom eines Mediums (Ergebnis von sektorStrom()).
struct Strom {
    uint16_t size = 0, spt = 0, heads = 0;
    uint8_t  used = 0, total = 0, base = 1;
    std::vector<uint8_t> raw;
};

std::string spurName(unsigned c, unsigned h) {
    return std::to_string(c) + "/" + std::to_string(h);
}

/// @brief Medium → entpackter Sektorstrom; false mit Begruendung, wenn nicht darstellbar.
bool sektorStrom(const DiskMedium& in, Strom& st, std::string& err) {
    const std::string grund = in.rawIncompatibleReason();
    if (!grund.empty()) {
        if (grund == "unformatiert" || grund == "kein Medium")
            err = "CopyQM kann unformatierte Disketten nicht speichern (das Format kennt "
                  "keine leere Diskette). Bitte .hfe oder .dmk verwenden.";
        else
            err = "CopyQM kann die Sektorkontrollbloecke von UDOS nicht speichern: " + grund
                  + " traegt Nutzdaten hinter der Daten-CRC oder eine unformatierte Luecke, "
                    "die dabei verloren gingen. Bitte .hfe oder .dmk verwenden.";
        return false;
    }
    const unsigned ncyls = in.numCylinders(), nheads = in.numHeads();
    // Zylinder bis einschliesslich des letzten beschriebenen werden gespeichert; was
    // dahinter unformatiert ist, bleibt "nicht gespeichert" (der Leser fuellt 0xF6).
    int last = -1;
    bool seite1 = false;
    for (unsigned c = 0; c < ncyls; ++c)
        for (unsigned h = 0; h < nheads; ++h)
            if (!in.peek(static_cast<uint8_t>(c), static_cast<uint8_t>(h)).empty()) {
                last = static_cast<int>(c);
                if (h == 1) seite1 = true;
            }
    if (last < 0) { err = "CopyQM kann unformatierte Disketten nicht speichern. Bitte .hfe oder .dmk verwenden."; return false; }
    const unsigned heads = (nheads >= 2 && seite1) ? 2 : 1;
    st.used  = static_cast<uint8_t>(last + 1);
    st.total = static_cast<uint8_t>(ncyls);
    st.heads = static_cast<uint16_t>(heads);

    bool erste = true;
    for (unsigned c = 0; c < st.used; ++c)
        for (unsigned h = 0; h < heads; ++h) {
            const TrackImage& t = in.peek(static_cast<uint8_t>(c), static_cast<uint8_t>(h));
            if (t.empty()) {
                err = "CopyQM kann unformatierte Spuren nicht speichern: Spur " + spurName(c, h)
                      + " ist unformatiert, davor oder dahinter liegen aber beschriebene "
                        "Spuren. Bitte .hfe oder .dmk verwenden.";
                return false;
            }
            if (t.encoding != Encoding::MFM || t.cell_factor != 1) {
                err = "CopyQM kennt nur MFM mit voller Datenrate: Spur " + spurName(c, h)
                      + " ist FM oder hat halbe Datenrate. Bitte .hfe oder .dmk verwenden.";
                return false;
            }
            auto secs = TrackCodec::parseTrack(t);
            std::sort(secs.begin(), secs.end(),
                      [](const LogicalSector& a, const LogicalSector& b) { return a.id < b.id; });
            if (secs.empty()) { err = "CopyQM: Spur " + spurName(c, h) + " ohne Sektoren"; return false; }
            const uint16_t sz = secs.front().size;
            for (size_t i = 0; i < secs.size(); ++i) {
                if (secs[i].size != sz || secs[i].data.size() != sz) {
                    err = "CopyQM unterstuetzt keine gemischten Formate: Spur " + spurName(c, h)
                          + " mischt Sektorgroessen. Bitte .hfe oder .dmk verwenden.";
                    return false;
                }
                if (i && secs[i].id != secs[i - 1].id + 1) {
                    err = "CopyQM unterstuetzt keine gemischten Formate: die Sektor-IDs der Spur "
                          + spurName(c, h) + " sind nicht lueckenlos. Bitte .hfe oder .dmk "
                            "verwenden.";
                    return false;
                }
            }
            if (erste) {
                st.size = sz;
                st.spt  = static_cast<uint16_t>(secs.size());
                st.base = secs.front().id;
                erste   = false;
                if (!(sz == 128 || sz == 256 || sz == 512 || sz == 1024 || sz == 2048 || sz == 4096)) {
                    err = "CopyQM: Sektorgroesse " + std::to_string(sz) + " nicht darstellbar";
                    return false;
                }
                if (st.spt > 64 || st.base < 1 || st.base + st.spt - 1 > 255) {
                    err = "CopyQM: Sektorzahl/-IDs nicht darstellbar (" + std::to_string(st.spt)
                          + " Sektoren ab ID " + std::to_string(st.base) + ")";
                    return false;
                }
            } else if (sz != st.size || secs.size() != st.spt || secs.front().id != st.base) {
                err = "CopyQM unterstuetzt keine gemischten Formate: die Spuren haben "
                      "unterschiedliche Sektorgroessen oder Sektorzahlen (Spur "
                      + spurName(c, h) + " weicht von der Geometrie der ersten Spur ab). Bitte .hfe oder "
                        ".dmk verwenden.";
                return false;
            }
            for (const LogicalSector& s : secs)
                st.raw.insert(st.raw.end(), s.data.begin(), s.data.end());
        }
    return true;
}

void setU16(std::vector<uint8_t>& f, size_t o, uint32_t x) {
    f[o] = static_cast<uint8_t>(x);
    f[o + 1] = static_cast<uint8_t>(x >> 8);
}

}  // namespace

bool CqmCodec::encode(const DiskMedium& in, const SourceInfo* vorlage,
                      std::vector<uint8_t>& out, SourceInfo* geschrieben, std::string& err) {
    Strom st;
    if (!sektorStrom(in, st, err)) return false;

    // Vorlage nur, wenn sie wirklich die Kopfbytes der Quelle traegt.
    const bool hatKopf = vorlage && vorlage->raw_header.size() == kHeaderSize;
    // Bezeichnung/Dichte/Interleave & Co. gehoeren zu EINER Geometrie; aendert sie sich
    // (Medium gewachsen/verkleinert), gelten nur noch Kommentar und Etikett.
    const bool gleich = hatKopf && vorlage->sector_size == st.size
                     && vorlage->secs_per_track == st.spt && vorlage->heads == st.heads
                     && vorlage->used_cyls == st.used && vorlage->total_cyls == st.total
                     && vorlage->first_sector_id == st.base;

    std::vector<uint8_t> f(kHeaderSize, 0);
    if (gleich) {
        f = vorlage->raw_header;
    } else {
        f[0] = 'C'; f[1] = 'Q'; f[2] = 0x14;
        const unsigned kb = static_cast<unsigned>(
            static_cast<size_t>(st.total) * st.heads * st.spt * st.size / 1024);
        const std::string bez = std::to_string(kb) + "K "
                              + (st.heads == 1 ? "Single-Sided" : "Double-Sided");
        std::copy(bez.begin(), bez.end(), f.begin() + kOffDescr);
        f[kOffBlind]   = 1;   // wie alle Abbilder der Sammlung: rohe Sektorkopie
        f[kOffDensity] = (st.size == 512 && st.spt >= 30) ? 2 : (st.size == 512 && st.spt >= 15) ? 1 : 0;
        f[kOffInterlv] = 1;
        const char* nolabel = "** NONE ** ";
        std::copy(nolabel, nolabel + kLabelLen, f.begin() + kOffLabel);
        if (hatKopf) {   // Etikett der Quelle uebernehmen
            std::copy(vorlage->raw_header.begin() + kOffLabel,
                      vorlage->raw_header.begin() + kOffLabel + kLabelLen, f.begin() + kOffLabel);
        }
    }
    setU16(f, kOffSecSize, st.size);
    setU16(f, kOffSpt, st.spt);
    setU16(f, kOffHeads, st.heads);
    f[kOffUsedCyl] = st.used;
    f[kOffTotCyl]  = st.total;
    f[kOffSecBase] = static_cast<uint8_t>(st.base - 1);
    const uint32_t c = crc(st.raw.data(), st.raw.size());
    for (int i = 0; i < 4; ++i) f[kOffCrc + i] = static_cast<uint8_t>(c >> (8 * i));

    std::vector<uint8_t> kommentar;
    if (hatKopf) kommentar = vorlage->comment_raw;
    else { const std::string k = "Image file created by k1520emu"; kommentar.assign(k.begin(), k.end()); }
    if (kommentar.size() > 0xFFFF) kommentar.resize(0xFFFF);
    setU16(f, kOffCmtLen, static_cast<uint32_t>(kommentar.size()));

    uint8_t summe = 0;
    for (size_t i = 0; i < kHeaderSize - 1; ++i) summe = static_cast<uint8_t>(summe + f[i]);
    f[kHeaderSize - 1] = static_cast<uint8_t>(-summe);

    out = std::move(f);
    out.insert(out.end(), kommentar.begin(), kommentar.end());
    const std::vector<uint8_t> strom = packRle(st.raw);
    out.insert(out.end(), strom.begin(), strom.end());

    // Gegenprobe: was wir geschrieben haben, muss der Leser unveraendert zurueckgeben.
    SourceInfo si;
    std::vector<uint8_t> raw2;
    std::string e2;
    if (!decodeBuffer(out, raw2, &si, e2) || raw2 != st.raw) {
        err = "CopyQM: interner Fehler beim Kodieren (Gegenprobe fehlgeschlagen" +
              (e2.empty() ? std::string() : ": " + e2) + ")";
        return false;
    }
    if (geschrieben) *geschrieben = std::move(si);
    return true;
}

bool CqmCodec::save(const std::string& path, const DiskMedium& in, const SourceInfo* vorlage,
                    SourceInfo* geschrieben, std::string& err) {
    std::vector<uint8_t> datei;
    if (!encode(in, vorlage, datei, geschrieben, err)) return false;   // Zieldatei unberuehrt

    // Erst in eine Nachbardatei, dann umbenennen: bei vollem Datentraeger oder Abbruch
    // bleibt die alte Datei ganz, es entsteht nie ein halb geschriebenes Abbild.
    const std::string tmp = path + ".k1520tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { err = "Kann Datei nicht schreiben: " + tmp; return false; }
        f.write(reinterpret_cast<const char*>(datei.data()),
                static_cast<std::streamsize>(datei.size()));
        f.flush();
        if (!f) {
            f.close();
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            err = "Schreibfehler (unvollstaendig): " + path;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);
        err = "Kann Datei nicht ersetzen: " + path + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

bool CqmCodec::save(const std::string& path, const DiskMedium& in, std::string& err) {
    return save(path, in, nullptr, nullptr, err);
}

std::string CqmCodec::infoField(const SourceInfo& c, const std::string& k) {
    if (k == "description") return c.description;
    if (k == "label")       return c.label;
    if (k == "comment")     return c.comment;
    if (k == "interleave")  return std::to_string(c.interleave);
    if (k == "skew")        return std::to_string(c.skew);
    if (k == "cylinders")   return std::to_string(c.used_cyls) + "/" + std::to_string(c.total_cyls);
    if (k == "crc")         return !c.crc_checked ? "nicht geprueft"
                                   : c.crc_stored == c.crc_computed ? "ok" : "falsch";
    return "";
}

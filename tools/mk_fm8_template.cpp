// mk_fm8_template – erzeugt eine GÜLTIG vorformatierte 8″-SD/FM-Leerdiskette (HFE v1)
// im MF3200-Format 7 (K5601-Menü 8inchCombo/B:):
//   26×128 FM  Spuren 0–2   (Systemspuren)
//   16×256 FM  Spuren 3–76  (Datenbereich)
// Einseitig (nur Kopf 0), 77 Spuren, Sektordaten = 0xE5 (leere CP/M-Sektoren).
//
// Warum programmatisch?  FORMAT.COM kann eine FRISCHE, gap-leere .hfe nicht direkt
// formatieren (bekannter Gap-Blank-.hfe-Hänger: Vorlese-Timing-Race mit dem BIOS-
// Motor-Watchdog, docs/format.md §8.2).  Die 5¼″-Pipeline umgeht das, indem sie eine
// GÜLTIGE, bereits formatierte Vorlage nach B: kopiert und diese neu formatiert.  Für
// 8″ existierte keine solche Vorlage — dieses Tool erzeugt sie (dieselbe Codec-Schicht
// TrackCodec::buildTrack, die auch der FORMAT.COM-Schreibpfad im K5122 benutzt).  Auf
// dieser Vorlage formatiert FORMAT.COM dann fehlerfrei bis Spur 76 (verifiziert).
//
// Verwendung:  mk_fm8_template <out.hfe>
//
// Ergebnis wird als disks/empty_mf3200_296k.hfe committet (Analogon zu empty_cpa780.hfe).

#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

void put16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off]     = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

// Leeres, einseitiges FM-HFE-Template schreiben (77×1, Header-Encoding FM).
// Die Spur liegt KONTINUIERLICH (volle 512 B/Block) — passend zur einseitigen
// Lese-/Schreiblogik in HfeImage (num_sides_==1).
bool writeBlankFmHfe(const std::string& path, uint8_t num_cyls, uint32_t side_len) {
    const uint32_t track_blocks  = (side_len + 511) / 512;
    const uint32_t track_len_pad = track_blocks * 512;

    std::vector<uint8_t> hdr(512, 0x00);
    std::memcpy(hdr.data(), "HXCPICFE", 8);
    hdr[0x08] = 0;            // formatrevision v1
    hdr[0x09] = num_cyls;     // Spuren
    hdr[0x0A] = 1;            // Seiten = 1 (einseitig)
    hdr[0x0B] = 2;            // track_encoding = 2 → FM (IBM 3740)
    put16(hdr, 0x0C, 250);    // bitrate kbit/s
    put16(hdr, 0x0E, 360);    // rpm (8″)
    hdr[0x10] = 0;            // iface
    hdr[0x11] = 1;            // dnu
    put16(hdr, 0x12, 1);      // track_list_block = 1
    hdr[0x14] = 0xFF;         // write_allowed
    hdr[0x15] = 0xFF;         // single_step
    for (size_t i = 0x16; i < 512; ++i) hdr[i] = 0xFF;

    std::vector<uint8_t> lut(512, 0xFF);
    uint32_t blk = 2;         // Spurdaten ab Block 2
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
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <out.hfe>\n", argv[0]);
        return 1;
    }
    const std::string out = argv[1];

    // 77×1 FM-Blank; side_len großzügig für den größten Track (26×128 ≈ 4.8 KB, 16×256 ≈ 5 KB).
    if (!writeBlankFmHfe(out, /*num_cyls=*/77, /*side_len=*/12500)) {
        std::fprintf(stderr, "FEHLER: Blank-HFE konnte nicht geschrieben werden: %s\n", out.c_str());
        return 1;
    }

    auto img = DiskImage::open(out, std::nullopt, /*write_protect=*/false);
    if (!img) {
        std::fprintf(stderr, "FEHLER: erzeugtes HFE nicht öffenbar: %s\n", out.c_str());
        return 1;
    }

    // MF3200-Format 7: 26×128 (Spur 0–2), 16×256 (Spur 3–76), alles FM.
    for (int c = 0; c < 77; ++c) {
        const int nsec = (c <= 2) ? 26 : 16;
        const int size = (c <= 2) ? 128 : 256;
        std::vector<LogicalSector> secs;
        secs.reserve(nsec);
        for (int i = 1; i <= nsec; ++i) {
            LogicalSector s;
            s.cyl  = static_cast<uint8_t>(c);
            s.head = 0;
            s.id   = static_cast<uint8_t>(i);
            s.size = static_cast<uint16_t>(size);
            s.data.assign(static_cast<size_t>(size), 0xE5);
            secs.push_back(s);
        }
        TrackImage trk = TrackCodec::buildTrack(secs, Encoding::FM);
        if (!img->writeTrack(static_cast<uint8_t>(c), 0, trk)) {
            std::fprintf(stderr, "FEHLER: writeTrack Spur %d\n", c);
            return 1;
        }
    }
    if (!img->flush()) {
        std::fprintf(stderr, "FEHLER: flush\n");
        return 1;
    }

    std::printf("OK: %s (MF3200 fmt7: 26x128 FM Sp.0-2, 16x256 FM Sp.3-76, einseitig)\n",
                out.c_str());
    return 0;
}

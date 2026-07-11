// SCPX-Disk-Extraktor: liest ein HFE linear aus, parst das CP/M-Directory
// und schreibt jede .COM/.SYS-Datei als Datei heraus.  Nur zur Analyse.
#include "core/peripherals/floppy_drive/hfe_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const char* disk = (argc >= 2) ? argv[1] : "disks/scpx_boot.hfe";
    const char* outdir = (argc >= 3) ? argv[2] : ".";
    HfeImage img(disk, true);
    if (!img.isOpen()) { fprintf(stderr, "open fail %s\n", disk); return 1; }
    auto g = img.geometry();
    fprintf(stderr, "geom: cyl=%d heads=%d\n", g.num_cyls, g.num_heads);

    // Linearer Track/Head/Sector-Dump: [cyl][head] -> sektor-id -> 256B
    // Sammle alle Sektoren; wir brauchen sie später blockweise.
    // Struktur: sectors[cyl][head][id] = data
    std::map<int, std::map<int, std::map<int, std::vector<uint8_t>>>> sec;
    int nsec = 0, ncrc = 0;
    for (int c = 0; c < g.num_cyls; ++c)
        for (int h = 0; h < g.num_heads; ++h) {
            TrackImage t = img.readTrack((uint8_t)c, (uint8_t)h);
            auto ls = TrackCodec::parseTrack(t);
            for (auto& s : ls) {
                sec[c][h][s.id] = s.data;
                nsec++;
                if (!s.id_crc_ok || !s.data_crc_ok) ncrc++;
            }
        }
    fprintf(stderr, "sectors=%d crc_bad=%d\n", nsec, ncrc);

    // Dump linear alle Bytes (physische Reihenfolge) für String-Suche.
    {
        std::string p = std::string(outdir) + "/scpx_disk_linear.bin";
        FILE* f = fopen(p.c_str(), "wb");
        for (auto& [c, hm] : sec) for (auto& [h, im] : hm) for (auto& [id, d] : im)
            fwrite(d.data(), 1, d.size(), f);
        fclose(f);
        fprintf(stderr, "wrote %s\n", p.c_str());
    }

    // CP/M-Directory-Suche: Directory-Einträge sind 32B; user(1)+name(8)+ext(3)+
    // Ex/S1/S2/RC + 16 Allocblöcke.  Wir suchen die Spur mit "INIT    COM".
    // Scanne alle Sektoren nach 32B-Einträgen mit druckbaren Namen.
    fprintf(stderr, "\n=== Directory-Kandidaten (32B-Einträge mit user<0x10) ===\n");
    for (auto& [c, hm] : sec) for (auto& [h, im] : hm) for (auto& [id, d] : im) {
        for (size_t off = 0; off + 32 <= d.size(); off += 32) {
            const uint8_t* e = d.data() + off;
            if (e[0] > 0x0F && e[0] != 0xE5) continue;
            // Name druckbar?
            bool ok = true; int printable = 0;
            for (int i = 1; i <= 11; ++i) {
                uint8_t ch = e[i] & 0x7F;
                if (ch < 0x20 || ch > 0x7E) { ok = false; break; }
                if (ch != ' ') printable++;
            }
            if (!ok || printable < 2 || e[0] == 0xE5) continue;
            char name[13];
            for (int i = 0; i < 8; ++i) name[i] = e[1 + i] & 0x7F;
            name[8] = '.';
            for (int i = 0; i < 3; ++i) name[9 + i] = e[9 + i] & 0x7F;
            name[12] = 0;
            fprintf(stderr, "  C%d H%d S%d off%zu user%d ex%d rc%d name='%s' alloc:",
                    c, h, id, off, e[0], e[12], e[15], name);
            for (int i = 0; i < 16; ++i) fprintf(stderr, " %02X", e[16 + i]);
            fprintf(stderr, "\n");
        }
    }
    return 0;
}

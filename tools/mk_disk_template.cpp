// mk_disk_template – erzeugt eine GÜLTIG vorformatierte, EINSEITIGE Leerdiskette
// (HFE v1) mit dem üblichen CP/A-Systemlayout:
//   <sys_cyls> Systemspuren  = <sys_nsec>×<sys_size>   (typ. 26×128)
//   Restspuren  Datenbereich = <data_nsec>×<data_size> (z. B. 4×1024 / 5×1024 / 8×1024 / 16×256)
// Alles im selben Verfahren (FM oder MFM), Sektordaten = 0xE5 (leere CP/M-Sektoren).
//
// Warum programmatisch?  Die Formatier-Pipeline braucht für die einseitigen Formate
// (MF3200 8″-FM, MF6400 8″-MFM, K5600.10 5″-40-SS, K5600.20 5″-80-SS) eine bereits
// gültig formatierte Vorlage, die nach B:/C: kopiert und dort neu formatiert wird.
// Dieses Tool erzeugt sie über dieselbe Codec-Schicht (TrackCodec::buildTrack), die
// auch der FORMAT.COM-Schreibpfad benutzt, und legt sie über den HFE-Container ab.
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

#include "core/peripherals/floppy_drive/disk_medium.h"
#include "core/peripherals/floppy_drive/hfe_codec.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>


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
    // Header-Verfahren = Datenspuren-Verfahren (dominiert); Mischspuren erkennt der
    // HFE-Codec beim Lesen selbst (Dual-Decode).
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

    // Spuren in ein internes Medium bauen; der HFE-Codec bemisst die Spurlänge
    // anschließend selbst nach der längsten Spur.
    const Encoding sys_enc  = sys_fm  ? Encoding::FM : Encoding::MFM;
    const Encoding data_enc = data_fm ? Encoding::FM : Encoding::MFM;

    DiskMedium medium(static_cast<uint8_t>(num_cyls), 1,
                      fm ? Encoding::FM : Encoding::MFM);
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
        medium.setTrack(static_cast<uint8_t>(c), 0,
                        TrackCodec::buildTrack(secs, is_sys ? sys_enc : data_enc));
    }

    std::string err;
    if (!HfeCodec::save(out, medium, err)) {
        std::fprintf(stderr, "FEHLER: %s\n", err.c_str());
        return 1;
    }

    std::printf("OK: %s (%d Spuren; %s %d×%dB Sys Sp.0-%d, %s %d×%dB Daten Sp.%d-%d)\n",
                out.c_str(), num_cyls,
                sys_fm ? "FM" : "MFM", sys_nsec, sys_size, sys_cyls - 1,
                data_fm ? "FM" : "MFM", data_nsec, data_size, sys_cyls, num_cyls - 1);
    return 0;
}

// Interactive scripted driver: boot CP/A with two mounted disks, send keystrokes
// from a script, and dump the 80x24 VRAM text between steps.  Built to drive the
// interactive FORMAT.COM / FORMATB.COM utilities (drive letter / drive type /
// disk format prompts) and to actually format the disk mounted in drive B:.
//
// Usage:  format_driver <diskA> <diskB> <script>
//
// Script commands (one per line, '#' = comment, blank lines ignored):
//   boot <Mcycles>     run <Mcycles> million CPU cycles (no key)
//   run  <Mcycles>     same as boot (alias)
//   type <text>        type the literal rest-of-line, char by char
//   enter              press Return
//   key  <name>        press a named key: return esc space bs del up down left right
//   dump <label>       print the current 80x24 screen with <label>
//   ramdump <lo> <hi> <file>  dump RAM [lo,hi) (hex addrs) to a binary file
//   savestate <file>   freeze RAM+both Z80+floppy to <file> (loadable by k1520dbg/
//                      boot_trace, which cannot drive the keyboard themselves)
//   wp   <0|1>         set write-protect on drive B (1=protected)
//
// Env (debug aids for the FORMAT/FORMATB analysis):
//   FD_LOGLEVEL=info|debug|warn|trace   raise the core log level (default ERROR)
//                                       — shows the K5122 >>> READ/WRITE/FORMAT-WRITE lines
//   FD_GATE=from:to[:level]    raise the level (default TRACE) only in a cycle window
//   FD_PCGATE=lo:hi[:level]    raise the level while either CPU PC is in [lo,hi] (hex)
//   FD_PCHIST=1        per-boot-batch ZVE1/busMaster PC histogram + /BUSRQ share to stderr
//   FD_DISKC=<path>    zusätzlich Laufwerk C: (Index 2) mounten (für FORMAT auf C:).
//                      FD_DISKC_FMT=<DiskFormat> → C: via create NEU anlegen statt öffnen.
//   FD_PHYSICAL_B=<ms> B: als PHYSISCHE Diskette (TrackSync + Ersatz-Arbeitsfaden statt
//                      Datei) — <ms> = simulierte Umdrehungsdauer, Kopfweg 3 ms/Spur.
//                      Jeder Auftrag geht nach stderr ([gw t] ART cCC hH prioP Kopfweg);
//                      so wird die Auftragsfolge am Greaseweazle ohne Hardware sichtbar.
//                      FD_PHYSICAL_READAHEAD=0 schaltet das Vorauslesen ab.
//
//   NOTE: DEBUG/TRACE lines live in the CORE libraries (k5122/a5120), which build/ compiles
//   at LOG_LEVEL=3 (DEBUG/TRACE stripped).  Run build_trace/format_driver (LOG_LEVEL=5) for
//   FD_LOGLEVEL=debug/trace and the FD_GATE/FD_PCGATE windows to produce any output.
//
// Each printable char and Enter is sent one-per-run-batch (1M cycles) so the BIOS
// 5 ms keyboard poll picks it up before the next arrives — same cadence kbd_test
// uses.  Disks are mounted read/write so FORMAT writes land in the diskB file.
#include "core/machines/a5120/a5120.h"
#include "core/logger.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/bit_codec.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>

using k1520::logging::Logger;
using k1520::logging::Level;

static constexpr uint32_t QK_RETURN    = 0x01000004;
static constexpr uint32_t QK_ESCAPE    = 0x01000000;
static constexpr uint32_t QK_BACKSPACE = 0x01000003;
static constexpr uint32_t QK_DELETE    = 0x01000007;
static constexpr uint32_t QK_LEFT      = 0x01000012;
static constexpr uint32_t QK_UP        = 0x01000013;
static constexpr uint32_t QK_RIGHT     = 0x01000014;
static constexpr uint32_t QK_DOWN      = 0x01000015;


// ── FD_PHYSICAL_B: Laufwerk B: als PHYSISCHE Diskette (Ersatz-Greaseweazle) ──
// Diagnosehilfe fuer doc/design/14: statt einer Datei haengt an B: ein TrackSync,
// den ein Ersatz-Arbeitsfaden bedient — mit simuliertem Kopfweg und Umdrehung.
// Jeder Auftrag wird nach stderr protokolliert, so wird die Auftragsfolge (und
// damit der Kopfweg) sichtbar, ohne dass Hardware noetig waere.
namespace {
class Ersatzlaufwerk {
public:
    Ersatzlaufwerk(TrackSync& s, int step_ms, int rev_ms)
        : sync_(s), step_ms_(step_ms), rev_ms_(rev_ms) {
        scheibe_.resize(s.spec().num_cyls, s.spec().num_heads);
        t0_ = std::chrono::steady_clock::now();
    }
    ~Ersatzlaufwerk() { stop(); }
    void start() { faden_ = std::thread([this] { schleife(); }); }
    void stop() {
        sync_.shutdown();
        if (faden_.joinable()) faden_.join();
    }
private:
    double t() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
    }
    void warte(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
    void schleife() {
        for (;;) {
            SyncJob j;
            if (!sync_.takeJob(j, 50)) continue;
            if (j.kind == SyncJobKind::Stop) return;
            const int weg = std::abs(static_cast<int>(j.cyl) - pos_);
            const char* art = j.kind == SyncJobKind::Read ? "READ"
                            : j.kind == SyncJobKind::Write ? "WRITE" : "VERIFY";
            fprintf(stderr, "[gw %8.3f] %-6s c%02u h%u prio%u  Kopf %d->%d (%d Spuren)\n",
                    t(), art, j.cyl, j.head, static_cast<unsigned>(j.prio),
                    pos_, j.cyl, weg);
            fflush(stderr);
            warte(weg * step_ms_ + (weg ? 15 : 0));
            pos_ = j.cyl;
            if (j.kind == SyncJobKind::Write) {
                std::vector<uint8_t> zellen; uint32_t bc = 0;
                if (!sync_.fetchWrite(j.id, zellen, bc)) { sync_.failJob(j.id, "fetch"); continue; }
                warte(rev_ms_);
                scheibe_.setTrack(j.cyl, j.head, BitCodec::decodeAuto(zellen, bc, Encoding::MFM));
                sync_.completeWrite(j.id);
            } else {
                warte(2 * rev_ms_ + 100);     // 2 Umdrehungen + USB/Dekodierung
                const TrackImage& s = scheibe_.peek(j.cyl, j.head);
                const uint32_t bits = s.bitcells ? s.bitcells : sync_.nominalBitcells();
                const std::vector<uint8_t> z = BitCodec::encode(s, bits);
                sync_.completeRead(j.id, z.data(), z.size(), bits);
            }
        }
    }
    TrackSync& sync_;
    DiskMedium scheibe_;
    int pos_ = 0, step_ms_, rev_ms_;
    std::thread faden_;
    std::chrono::steady_clock::time_point t0_;
};
}  // namespace

static void dumpScreen(A5120Machine& m, const char* label) {
    printf("\n=== SCREEN: %s ===\n", label);
    for (int row = 0; row < 24; ++row) {
        char line[81];
        for (int col = 0; col < 80; ++col) {
            uint8_t c = m.memReadDebug(static_cast<uint16_t>(0xF800 + row * 80 + col));
            line[col] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ';
        }
        line[80] = 0;
        // trim trailing spaces for readability
        int e = 80; while (e > 0 && line[e-1] == ' ') line[--e] = 0;
        printf("  |%s\n", line);
    }
    fflush(stdout);
}

static void runFor(A5120Machine& m, long long cycles) {
    long long done = 0;
    static const bool pchist = std::getenv("FD_PCHIST") != nullptr;
    static std::map<uint16_t,long> h1, h2;
    static long busrq_cnt=0, samp=0;
    while (done < cycles) {
        int n = m.run(5000);
        if (n == 0) break;
        done += n;
        if (pchist) { h1[m.cpuPC()]++; h2[m.busMasterPC()]++; ++samp; if(m.isBUSRQ())++busrq_cnt; }
    }
    if (pchist && cycles >= 5'000'000) {
        auto top=[](std::map<uint16_t,long>& h,const char* tag){
            std::vector<std::pair<uint16_t,long>> v(h.begin(),h.end());
            std::sort(v.begin(),v.end(),[](auto&a,auto&b){return a.second>b.second;});
            fprintf(stderr,"[%s top]",tag);
            for(int i=0;i<8&&i<(int)v.size();++i) fprintf(stderr," %04X:%ld",v[i].first,v[i].second);
            fprintf(stderr,"\n"); h.clear();
        };
        fprintf(stderr,"[busrq %ld/%ld = %.0f%%]\n", busrq_cnt, samp, 100.0*busrq_cnt/(samp?samp:1));
        top(h1,"cpuPC(ZVE1)"); top(h2,"busMaster");
        busrq_cnt=samp=0;
    }
}

static void pressKey(A5120Machine& m, uint32_t kc) {
    m.keyPress(kc, false, false);
    runFor(m, 1'000'000);
    m.keyRelease(kc);
    runFor(m, 300'000);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr,
            "usage: %s <diskA> <diskB> <script> [createB_format]\n"
            "  createB_format: wenn angegeben, wird diskB NEU angelegt (create) statt\n"
            "                  geöffnet — .hfe = leeres Template (Format egal, z.B. '-'),\n"
            "                  .img = 0xE5-Image in der Geometrie des DiskFormat-Namens.\n",
            argv[0]);
        return 1;
    }
    const char* diskA = argv[1];
    const char* diskB = argv[2];
    const char* script = argv[3];
    const char* createB = (argc >= 5) ? argv[4] : nullptr;   // nullptr = B: öffnen

    Level lvl = Level::ERROR;
    if (const char* e = std::getenv("FD_LOGLEVEL")) {
        std::string s = e;
        if (s=="info") lvl=Level::INFO; else if (s=="debug") lvl=Level::DEBUG;
        else if (s=="warn") lvl=Level::WARN; else if (s=="trace") lvl=Level::TRACE;
    }
    Logger::instance().setBaseLevel(lvl);

    // FD_GATE="from:to[:level]"  → TRACE (o. Level) nur im Zyklusfenster.
    // Erlaubt gezieltes Instruktions-Tracing des Stall-Fensters ohne GB-Log.
    if (const char* g = std::getenv("FD_GATE")) {
        unsigned long long from = 0, to = 0; char lv[16] = "trace";
        if (sscanf(g, "%llu:%llu:%15s", &from, &to, lv) >= 2) {
            Level gl = Level::TRACE;
            std::string s = lv;
            if (s=="info") gl=Level::INFO; else if (s=="debug") gl=Level::DEBUG;
            else if (s=="warn") gl=Level::WARN; else if (s=="trace") gl=Level::TRACE;
            Logger::instance().addCycleGate(from, to, gl);
            fprintf(stderr, "[FD_GATE cyc %llu..%llu lvl=%s]\n", from, to, s.c_str());
        }
    }
    if (const char* g = std::getenv("FD_PCGATE")) {
        unsigned lo = 0, hi = 0; char lv[16] = "trace";
        if (sscanf(g, "%x:%x:%15s", &lo, &hi, lv) >= 2) {
            Level gl = Level::TRACE;
            std::string s = lv;
            if (s=="info") gl=Level::INFO; else if (s=="debug") gl=Level::DEBUG;
            else if (s=="warn") gl=Level::WARN;
            Logger::instance().addPCGate(lo, hi, gl);
            fprintf(stderr, "[FD_PCGATE pc %04X..%04X lvl=%s]\n", lo, hi, s.c_str());
        }
    }

    // Optionale Laufwerksbestückung je Slot (A,B,C,D) via FD_PROFILES="p0,p1,p2,p3".
    // Default = 4× K5601 (5,25"-MFM).  Für 8"-Combo-Tests: die B:/C:-Slots auf die
    // vom Combo-BIOS gemeldeten Fremdtypen (MF3200 / MF6400) setzen,
    // damit die PHYSISCHE Laufwerksgeometrie (Kopfzahl/Spuren/Verfahren) zum BIOS passt.
    A5120Machine::Config cfg;
    if (const char* p = std::getenv("FD_PROFILES")) {
        std::string s = p;
        size_t i = 0;
        for (int slot = 0; slot < 4 && i <= s.size(); ++slot) {
            size_t comma = s.find(',', i);
            std::string tok = s.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
            if (!tok.empty()) cfg.drive_profiles[slot] = tok;
            if (comma == std::string::npos) break;
            i = comma + 1;
        }
        fprintf(stderr, "Profiles: A=%s B=%s C=%s D=%s\n",
                cfg.drive_profiles[0].c_str(), cfg.drive_profiles[1].c_str(),
                cfg.drive_profiles[2].c_str(), cfg.drive_profiles[3].c_str());
    }
    A5120Machine machine(cfg);
    machine.powerOn();

    if (!(machine.mountDisk(0, diskA, "cpa780", false) ||
          machine.mountDisk(0, diskA, "cpa800", false))) {
        fprintf(stderr, "ERROR: mount A '%s': %s\n", diskA, machine.lastError().c_str());
        return 1;
    }
    std::unique_ptr<Ersatzlaufwerk> gw;
    if (const char* ph = std::getenv("FD_PHYSICAL_B")) {
        TrackSyncSpec spec;
        spec.num_cyls = 80; spec.num_heads = 2; spec.writable = true;
        if (const char* rd = std::getenv("FD_PHYSICAL_READAHEAD")) spec.read_ahead = atoi(rd) != 0;
        auto img = DiskImage::openPhysical(spec);
        TrackSync* sync = img->sync();
        if (!machine.mountDiskImage(1, std::move(img), false)) {
            fprintf(stderr, "ERROR: mount B physisch: %s\n", machine.lastError().c_str());
            return 1;
        }
        const int rev_ms = atoi(ph) > 0 ? atoi(ph) : 200;
        gw = std::make_unique<Ersatzlaufwerk>(*sync, 3, rev_ms);
        gw->start();
        fprintf(stderr, "B: als PHYSISCHE Diskette (Umdrehung %d ms, readahead=%d)\n",
                rev_ms, spec.read_ahead);
        // FD_PHYSICAL_PRELOAD: erst die GANZE Diskette einlesen (wie „warten, bis der
        // Vorausleser durch ist"), dann das Script fahren.
        if (std::getenv("FD_PHYSICAL_PRELOAD")) {
            fprintf(stderr, "[preload] lese alle Spuren …\n");
            const bool ok = sync->loadAll();
            fprintf(stderr, "[preload] fertig (%s)\n", ok ? "vollstaendig" : "mit Fehlern");
        }
    } else if (createB) {
        // B: NEU anlegen (create) im angegebenen Katalogformat: VORFORMATIERT
        // (echte IDAM/DATA/CRC, Nutzdaten 0xE5) — nicht die neue Leerdiskette.
        if (!machine.createDisk(1, diskB, createB, false)) {
            fprintf(stderr, "ERROR: create B '%s' (Format '%s'): %s\n",
                    diskB, createB, machine.lastError().c_str());
            return 1;
        }
        fprintf(stderr, "Created B=%s (Format '%s')\n", diskB, createB);
    } else if (!(machine.mountDisk(1, diskB, "cpa780", false) ||
                 machine.mountDisk(1, diskB, "cpa800", false))) {
        fprintf(stderr, "ERROR: mount B '%s': %s\n", diskB, machine.lastError().c_str());
        return 1;
    }
    fprintf(stderr, "Mounted A=%s  B=%s\n", diskA, diskB);

    // Optionales Laufwerk C: (Index 2) — für Menü-Capture / Format auf C:.
    if (const char* diskC = std::getenv("FD_DISKC")) {
        const char* createC = std::getenv("FD_DISKC_FMT");
        if (createC) {
            if (!machine.createDisk(2, diskC, createC, false)) {
                fprintf(stderr, "ERROR: create C '%s' (Format '%s'): %s\n",
                        diskC, createC, machine.lastError().c_str());
                return 1;
            }
            fprintf(stderr, "Created C=%s (Format '%s')\n", diskC, createC);
        } else if (!(machine.mountDisk(2, diskC, "cpa780", false) ||
                     machine.mountDisk(2, diskC, "cpa800", false))) {
            fprintf(stderr, "ERROR: mount C '%s': %s\n", diskC, machine.lastError().c_str());
            return 1;
        }
        fprintf(stderr, "Mounted C=%s\n", diskC);
    }

    std::ifstream in(script);
    if (!in) { fprintf(stderr, "ERROR: cannot open script '%s'\n", script); return 1; }

    std::string line;
    while (std::getline(in, line)) {
        // strip CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string cmd; ls >> cmd;

        if (cmd == "boot" || cmd == "run") {
            double mc = 0; ls >> mc;
            fprintf(stderr, "[run %.1fM cycles]\n", mc);
            runFor(machine, (long long)(mc * 1'000'000));
        } else if (cmd == "type") {
            std::string rest;
            std::getline(ls, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            fprintf(stderr, "[type '%s']\n", rest.c_str());
            for (char ch : rest) pressKey(machine, static_cast<uint8_t>(ch));
        } else if (cmd == "enter") {
            fprintf(stderr, "[enter]\n");
            pressKey(machine, QK_RETURN);
        } else if (cmd == "key") {
            std::string name; ls >> name;
            uint32_t kc = 0;
            if (name == "return" || name == "enter") kc = QK_RETURN;
            else if (name == "esc") kc = QK_ESCAPE;
            else if (name == "space") kc = ' ';
            else if (name == "bs") kc = QK_BACKSPACE;
            else if (name == "del") kc = QK_DELETE;
            else if (name == "up") kc = QK_UP;
            else if (name == "down") kc = QK_DOWN;
            else if (name == "left") kc = QK_LEFT;
            else if (name == "right") kc = QK_RIGHT;
            else { kc = static_cast<uint8_t>(name.empty() ? ' ' : name[0]); }
            fprintf(stderr, "[key %s]\n", name.c_str());
            pressKey(machine, kc);
        } else if (cmd == "dump") {
            std::string lbl; std::getline(ls, lbl);
            dumpScreen(machine, lbl.c_str());
        } else if (cmd == "ramdump") {
            std::string los, his, fn; ls >> los >> his >> fn;
            unsigned lo = std::stoul(los, nullptr, 16);
            unsigned hi = std::stoul(his, nullptr, 16);
            std::ofstream of(fn, std::ios::binary);
            for (unsigned a = lo; a < hi; ++a)
                of.put(static_cast<char>(machine.memReadDebug(static_cast<uint16_t>(a))));
            fprintf(stderr, "[ramdump 0x%04X..0x%04X -> %s]\n", lo, hi, fn.c_str());
        } else if (cmd == "savestate") {
            std::string fn; ls >> fn;
            bool ok = machine.saveState(fn);
            fprintf(stderr, "[savestate -> %s : %s]\n", fn.c_str(), ok ? "OK" : "FAIL");
        } else if (cmd == "wp") {
            int v = 0; ls >> v;
            machine.setDiskWriteProtect(1, v != 0);
            fprintf(stderr, "[wp B=%d]\n", v);
        } else {
            fprintf(stderr, "[unknown cmd: %s]\n", cmd.c_str());
        }
    }

    dumpScreen(machine, "final");
    return 0;
}

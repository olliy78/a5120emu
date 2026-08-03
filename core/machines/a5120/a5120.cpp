#include "a5120.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include "core/logger.h"

namespace {
// ZVE1↔ZVE2 boot handshake: ZVE2 (DMA-CPU) writes [0x03F8]=3 once it has copied
// all boot sectors into RAM (boot ROM ZVE2_DONE at 0x026B). The machine watches
// this shared RAM flag to know when to release /BUSRQ back to ZVE1.
constexpr uint16_t kZve2DoneFlagAddr = 0x03F8;
constexpr uint8_t  kZve2DoneValue    = 0x03;

// ── SCPX 1526 Laufzeit-Read: Adressen für den os-gated „gehaltenen Bus" ──────
// (doc/analyse_scpx_com_load.md §7/§9.4b).  ZVE1 erreicht den CONIN-Wartepunkt
// E079, sobald der interaktive A>-Prompt bereit ist (= „OS läuft").  Der Laufzeit-
// BIOS-Read poist eine ZVE2-Lese-Koroutine, setzt [EC0B]=E8B5 und wartet an der
// Poll-Schleife E8B5; ZVE2 signalisiert das Read-Ende, indem es [EC0B] auf einen
// Fortsetzungs-Vektor != E8B5 schreibt (ZVE1 macht dann JP (HL)).
constexpr uint16_t kScpxPromptPC     = 0xE079;  // interaktiver Prompt erreicht → os_running_
constexpr uint16_t kScpxPollWaitPC   = 0xE8B5;  // ZVE1-Poll-Wait auf [EC0B]
constexpr uint16_t kScpxContVecAddr  = 0xEC0B;  // ZVE2→ZVE1 Fortsetzungs-Vektor (16-Bit)
constexpr uint16_t kScpxPollWaitArm  = 0xE8B5;  // [EC0B]-Wert während des Wartens (== PollWaitPC)
// No-Progress-Watchdog: findet ZVE2 sein IDAM nicht (z. B. FM/MFM-Erkennung einer nicht
// passenden Diskette), re-armt der Matcher (E9C8) endlos und signalisiert nie [EC0B]≠E8B5.
// Nach so vielen gehaltenen Takten ohne Fortschritt geben wir den Bus frei, damit ZVE1 läuft
// und der Index-Interrupt (Record-not-found → [EC0B]=E998, FM/MFM-Retry) greift — wie beim Boot.
// ~3 Umdrehungen (Index-Periode ≈490000 Takte); ein erfolgreicher Sektor-Read (inkl. IDAM-Suche
// über max. 1 Umdrehung + 1024-B-Streaming) liegt deutlich darunter → Watchdog trifft nur Fehler.
constexpr long long kHeldReadWatchdogCycles = 1'500'000;
}  // namespace

// Baut das DriveProfile-Array der 4 K5122-Slots aus den Profilnamen der Config.
// Unbekannte Namen liefert builtinDriveProfile als Default-Profil (K5601).
static std::array<DriveProfile, 4> profilesFromConfig(const A5120Machine::Config& cfg) {
    return { builtinDriveProfile(cfg.drive_profiles[0]),
             builtinDriveProfile(cfg.drive_profiles[1]),
             builtinDriveProfile(cfg.drive_profiles[2]),
             builtinDriveProfile(cfg.drive_profiles[3]) };
}

// Default-Konstruktor delegiert an die Config-Variante (Default = 4× K5601).
A5120Machine::A5120Machine() : A5120Machine(Config{}) {}

A5120Machine::A5120Machine(const Config& cfg)
    : zre_(bus_)
    , ops_()
    , screen_(bus_)
    , ass_(bus_)
    // Laufwerksbestückung aus der Config; Default = A5120-Standard-Bürokonfiguration
    // (4× K5601, 5,25"-MFM). Per C-API/GUI/Config-Datei überschreibbar.
    , afs_(bus_, profilesFromConfig(cfg))
    , drive_profiles_(profilesFromConfig(cfg))
{
    // Diskettenformate aus data/formats.yaml laden (§8.6).  Fehlt die Datei oder ist
    // sie syntaktisch kaputt, kann die Maschine keine Diskette mounten/anlegen — das
    // ist ein Startabbruch mit klarer Meldung, kein stiller Weiterlauf.  Einzelne
    // FEHLERHAFTE Formatdefinitionen sind dagegen nicht fatal: sie werden übersprungen
    // und über formatCatalog().issues() gemeldet.
    {
        std::string fatal;
        disk_formats_ = FormatCatalog::loadDefault(&fatal);
        if (!fatal.empty()) throw std::runtime_error(fatal);

        // Übersprungene Definitionen zusätzlich auf stderr — eine Konfigurationspanne
        // muss sichtbar sein, auch wenn das Logging aus ist oder in eine Datei geht.
        for (const auto& issue : disk_formats_.issues()) {
            LOG_WARN("Formate", "%s", issue.c_str());
            std::fprintf(stderr, "[Formatkatalog] %s\n", issue.c_str());
        }
    }

    // ZVE1 (Haupt-CPU) lebt jetzt auf der K2526-Karte.
    // Verdrahtung mit dem Bus erfolgt im K2526-Konstruktor.
    wireBackplane();
}

void A5120Machine::wireBackplane() {
    // Register all cards on K1520 bus in slot order
    // Slot 1: OPS (K3526) — full 64KB RAM, registered first (lowest priority)
    ops_.attachToBus(bus_);

    // Slot 5: ABS (K7024) — VRAM F800H-FFFFH, overlays OPS
    screen_.attachToBus(bus_);

    // Slot 4: ZRE (K2526) — I/O ports 00H-0FH, boot ROM 0000H-03FFH
    zre_.attachToBus(bus_);

    // Slot 3: ASS (K8025) — I/O ports 50H-5FH
    bus_.registerIO(&ass_, 0x50, 16);

    // Slot 2: AFS (K5122) — I/O ports 10H-18H (9 ports)
    bus_.registerIO(&afs_, 0x10, 9);

    // Interrupt chain (physical slot order: AFS→ASS→ZRE→ABS, OPS has no IRQ)
    // ZRE BS-PIO is on the second chain via Koppelbus (lowest priority)
    bus_.setInterruptChain({&afs_, &ass_, &zre_});

    // Koppelbus wiring:
    // ZRE CTC ZC/TO outputs → Koppelbus zc_to[0..2] signals.
    // zc_to[2] is also "fest verdrahtet" back to ZRE CTC CLK/TRG[3] (channel cascade).
    // zc_to[0] feeds K8025 CTC A34 as external baud-rate clock source (W1:7 bridge).
    zre_.ctc().setZCTOCallback([this](int ch, bool lvl) {
        if (ch >= 0 && ch < 3)
            koppel_.zc_to[ch].drive(lvl);
    });

    // ZRE CTC ZC/TO[2] → ZRE CTC CLK/TRG[3] (hardwired on K2526 via Koppelbus)
    koppel_.zc_to[2].connect([this](bool lvl) {
        zre_.ctc().clkTrg(3, lvl);
    });

    // ZRE CTC ZC/TO[0] → K8025 CTC A34 external clock input (W1:7 "gezeichnete Stellung")
    koppel_.zc_to[0].connect([this](bool lvl) {
        for (int i = 0; i < 4; ++i)
            ass_.ctcA34().clkTrg(i, lvl);
    });

    // Connect keyboard to K8025 SIO A32, Channel A
    kbd_.connect(ass_.sioA32(), 0);
}

// Systemweiter /RESET des K1520-Backplane: ZVE1 + ALLE peripheren Bausteine.
//
// Nur die CPU zurückzusetzen genügt nicht.  Ein Reset aus dem laufenden Betrieb
// liess sonst den System-CTC mit den IM2-Vektoren des alten OS weiterzählen
// (`vecBase=F8`, Interrupts frei): sobald das Lade-ROM `IM 2`/`LD I,0` setzt und
// `EI` gibt, landet der erste Timer-Interrupt auf einem Fantasie-Vektor aus der
// ROM-Seite 0 → die Boot-Kette entgleist.  Dasselbe gilt für die SIOs (halb
// gesendete Tastaturbytes, IUS in der Daisy-Chain) und den K5122 (offener
// Lesetransfer + gehaltenes /BUSRQ).  Auf echter Hardware räumt die /RESET-
// Leitung des Backplane genau das ab.
void A5120Machine::resetHardware() {
    stop_.store(false);
    zre_.powerOn();     // K2526: Lade-ROM mappen, BS-PIO/CTC/Q240 zurücksetzen
    zre_.cpuReset();
    afs_.reset();       // K5122: Transfer abbrechen, /BUSRQ frei, PIOs zurück
    ass_.reset();       // K8025: Baud-CTC + beide SIOs
    kbd_.reset();       // K7637: Tastenwiederholung/LEDs/serielle Warteschlange
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.releaseWAIT();
    bus_.markIntDirty();   // Daisy-Chain nach dem Reset neu bewerten

    // Run-Loop-Koppelzustand ZVE1↔ZVE2 auf Kaltstart bringen, sonst hängt ein
    // Neustart aus laufendem Betrieb im halb offenen DMA-Handshake fest.
    busrq_active_     = false;
    dma_saw_progress_ = false;
    prev_floppy_int_  = false;
    bus_master_zve2_  = false;
    os_running_       = false;   // (SCPX) os-gated Laufzeit-Read-Gate zurücksetzen
    held_read_active_ = false;
    held_read_cycles_   = 0;
    held_read_watchdog_ = false;
    screen_.clearScreen();   // Bildschirm sichtbar löschen
    boot_trace_count_ = 0;
}

void A5120Machine::powerOn() {
    // Echter Netz-Aus/Ein: das DRAM verliert seinen Inhalt (K3526 modelliert den
    // unbestimmten Einschaltzustand als 0xFF, s. K3526-Konstruktor). Ohne dieses
    // Löschen liefe ein Power-Cycle aus dem laufenden Betrieb auf altem RAM-Inhalt
    // weiter — inklusive der Reste des vorherigen OS.
    ops_.fill(0xFF);
    resetHardware();
    LOG_INFO("A5120", "Power on: ZVE1 Reset, Lade-ROM aktiv");

#if LOG_LEVEL >= 5
    zre_.cpu().traceCallback = [](const Z80& z) {
        LOG_TRACE("ZVE1",
            "PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X",
            z.PC, z.SP, z.AF, z.BC, z.DE, z.HL, z.IX, z.IY);
    };
#endif
}

void A5120Machine::reset() {
    // Reset-Taste: wie /RESET auf echter Hardware — CPU + alle Bausteine, aber
    // der RAM-Inhalt bleibt stehen (nur Netz-Aus verliert ihn, s. powerOn()).
    resetHardware();
    LOG_INFO("A5120", "Reset: ZVE1 Reset, Lade-ROM reaktiviert");
}

// ─── Snapshot / reverse-debugging support ────────────────────────────────────
// Capture/restore is exact for CPU + 64 KB RAM (see MachineSnapshot doc in a5120.h).
// Z80 register pairs are unions, so copying the 16-bit views restores all sub-bytes.
static void saveZ80(const Z80& z, A5120Machine::MachineSnapshot::Z80Regs& r) {
    r.AF=z.AF; r.BC=z.BC; r.DE=z.DE; r.HL=z.HL; r.IX=z.IX; r.IY=z.IY;
    r.PC=z.PC; r.SP=z.SP; r.AF_=z.AF_; r.BC_=z.BC_; r.DE_=z.DE_; r.HL_=z.HL_;
    r.I=z.I; r.R=z.R; r.IM=z.IM; r.IFF1=z.IFF1; r.IFF2=z.IFF2;
    r.halted=z.halted; r.cycles=z.cycles;
}
static void loadZ80(Z80& z, const A5120Machine::MachineSnapshot::Z80Regs& r) {
    z.AF=r.AF; z.BC=r.BC; z.DE=r.DE; z.HL=r.HL; z.IX=r.IX; z.IY=r.IY;
    z.PC=r.PC; z.SP=r.SP; z.AF_=r.AF_; z.BC_=r.BC_; z.DE_=r.DE_; z.HL_=r.HL_;
    z.I=r.I; z.R=r.R; z.IM=r.IM; z.IFF1=r.IFF1; z.IFF2=r.IFF2;
    z.halted=r.halted; z.cycles=r.cycles;
}

void A5120Machine::captureState(MachineSnapshot& s) const {
    std::memcpy(s.ram.data(), ops_.rawPtr(), s.ram.size());
    saveZ80(zre_.cpu(),  s.zve1);
    saveZ80(zre_.zve2(), s.zve2);
    s.rom_enabled     = zre_.isRomEnabled();
    s.busrq_active    = busrq_active_;
    s.dma_progress    = dma_saw_progress_;
    s.bus_master_zve2 = bus_master_zve2_;
    s.total_cycles    = total_cycles_;
    // Device-internal state needed for a working keyboard after a loadstate.
    // A working keyboard needs more than the keyboard SIO itself: the OS scans
    // the keyboard from its timer ISR, so the system CTC (Q302) and its BS-PIO,
    // plus the SIO's baud CTC (A34), must resume in phase too — otherwise the
    // timer-scan vs serial-latency race re-appears and keystrokes are dropped.
    // The serialise order here MUST match the deserialise order in restoreState().
    s.device_state.clear();
    zre_.ctc().serialize(s.device_state);        // K2526 Q302 system timer
    zre_.bsPio().serialize(s.device_state);      // K2526 BS-PIO
    ass_.ctcA34().serialize(s.device_state);     // K8025 baud CTC
    ass_.sioA32().serialize(s.device_state);     // K8025 keyboard/printer SIO
    kbd_.serialize(s.device_state);              // K7637 keyboard peripheral
    // Floppy controller: both PIOs, the latched control signals and — the point
    // of this — the mechanical head position (cylinder) of each drive, so disk
    // access (dir, file reads/writes) resumes with the head on the right track.
    afs_.serialize(s.device_state);              // K5122 floppy controller
    screen_.serialize(s.device_state);           // K7024 VRAM (v4: screen survives loadstate)
}

bool A5120Machine::restoreState(const MachineSnapshot& s) {
    ops_.restore(s.ram.data());
    loadZ80(zre_.cpu(),  s.zve1);
    loadZ80(zre_.zve2(), s.zve2);
    busrq_active_    = s.busrq_active;
    dma_saw_progress_= s.dma_progress;
    bus_master_zve2_ = s.bus_master_zve2;
    total_cycles_    = s.total_cycles;
    // Reproduce the boot-ROM mapping too, so a state saved post-ROM resumes correctly
    // even into a freshly powered machine (where the ROM is still mapped at 0x0000).
    zre_.setRomMapped(s.rom_enabled);
    // Restore the keyboard subsystem (system CTC + BS-PIO + baud CTC + keyboard
    // SIO + K7637) so input works after the load. Empty for legacy (v1)
    // snapshots → the devices keep their current state. Order matches captureState().
    if (!s.device_state.empty()) {
        const uint8_t* p   = s.device_state.data();
        const uint8_t* end = p + s.device_state.size();
        zre_.ctc().deserialize(p, end);
        zre_.bsPio().deserialize(p, end);
        ass_.ctcA34().deserialize(p, end);
        ass_.sioA32().deserialize(p, end);
        kbd_.deserialize(p, end);
        afs_.deserialize(p, end);
        // v4+: K7024 VRAM. Fehlt bei v2/v3-Snapshots (p==end) → Bildschirm bleibt
        // wie er ist; deserialize prüft die Länge selbst.
        if (p < end) screen_.deserialize(p, end);
    }
    return true;
}

// On-disk state format: magic "K1520SS" + version, a regs-size guard, then the raw
// MachineSnapshot fields. Written/read by the same build → POD memcpy is sufficient.
// A length-prefixed device_state blob holds the serialised device-internal state.
// It is parsed sequentially per chip with bounds checks, so a shorter (older) blob
// loads fine — trailing chips simply keep their current state. Versions:
//   1 = no device state, 2 = + keyboard subsystem, 3 = + K5122 floppy controller,
//   4 = + K7024 VRAM (2 KB), so `screen`/framebuffer survive a loadstate.
namespace {
const char    kStateMagicPrefix[7] = {'K','1','5','2','0','S','S'};
constexpr uint8_t kStateVersion    = 4;
}

bool A5120Machine::saveState(const std::string& path) const {
    MachineSnapshot s; captureState(s);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t regsize = (uint32_t)sizeof(MachineSnapshot::Z80Regs);
    f.write(kStateMagicPrefix, sizeof kStateMagicPrefix);
    f.write(reinterpret_cast<const char*>(&kStateVersion), 1);
    f.write(reinterpret_cast<const char*>(&regsize), sizeof regsize);
    f.write(reinterpret_cast<const char*>(s.ram.data()), s.ram.size());
    f.write(reinterpret_cast<const char*>(&s.zve1), sizeof s.zve1);
    f.write(reinterpret_cast<const char*>(&s.zve2), sizeof s.zve2);
    uint8_t flags[4] = { (uint8_t)s.rom_enabled, (uint8_t)s.busrq_active,
                         (uint8_t)s.dma_progress, (uint8_t)s.bus_master_zve2 };
    f.write(reinterpret_cast<const char*>(flags), 4);
    f.write(reinterpret_cast<const char*>(&s.total_cycles), sizeof s.total_cycles);
    // v2 tail: length-prefixed device-state blob (keyboard SIO + K7637).
    uint32_t dev_len = (uint32_t)s.device_state.size();
    f.write(reinterpret_cast<const char*>(&dev_len), sizeof dev_len);
    if (dev_len) f.write(reinterpret_cast<const char*>(s.device_state.data()), dev_len);
    return (bool)f;
}

bool A5120Machine::loadState(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[7]; f.read(magic, sizeof magic);
    if (!f || std::memcmp(magic, kStateMagicPrefix, sizeof magic) != 0) return false;
    uint8_t version = 0; f.read(reinterpret_cast<char*>(&version), 1);
    if (!f || version < 1 || version > 4) return false;
    uint32_t regsize = 0; f.read(reinterpret_cast<char*>(&regsize), sizeof regsize);
    if (!f || regsize != (uint32_t)sizeof(MachineSnapshot::Z80Regs)) return false;
    MachineSnapshot s;
    f.read(reinterpret_cast<char*>(s.ram.data()), s.ram.size());
    f.read(reinterpret_cast<char*>(&s.zve1), sizeof s.zve1);
    f.read(reinterpret_cast<char*>(&s.zve2), sizeof s.zve2);
    uint8_t flags[4]; f.read(reinterpret_cast<char*>(flags), 4);
    f.read(reinterpret_cast<char*>(&s.total_cycles), sizeof s.total_cycles);
    if (!f) return false;
    if (version >= 2) {
        uint32_t dev_len = 0; f.read(reinterpret_cast<char*>(&dev_len), sizeof dev_len);
        if (!f) return false;
        s.device_state.resize(dev_len);
        if (dev_len) f.read(reinterpret_cast<char*>(s.device_state.data()), dev_len);
        if (!f) return false;
    }
    s.rom_enabled=flags[0]; s.busrq_active=flags[1]; s.dma_progress=flags[2]; s.bus_master_zve2=flags[3];
    return restoreState(s);
}

int A5120Machine::run(int max_cycles) {
    // Drain key queue
    {
        std::lock_guard<std::mutex> lk(key_mutex_);
        while (!key_queue_.empty()) {
            auto& ev = key_queue_.front();
            if (ev.is_press)
                kbd_.keyPress(ev.keycode, ev.shift, ev.ctrl);
            else
                kbd_.keyRelease(ev.keycode);
            key_queue_.pop_front();
        }
    }

    int remaining = max_cycles;
    while (remaining > 0 && !stop_.load(std::memory_order_relaxed)) {
        // Evaluate dynamic log gates for this instruction (PC range / cycle
        // window). Cheap early-out when no gates are registered. Feed both CPU
        // PCs so a PC gate can match either ZVE1 or ZVE2.
        k1520::logging::Logger::instance().update(
            total_cycles_, zre_.cpuPC(), zre_.zve2PC());

        // Zeitgetriebene Floppy-Interrupt-Flanke (K5122-Index-IRQ) erkennen: sie
        // entsteht ausserhalb von I/O innerhalb von afs_.update() und muss die
        // Interrupt-Chain dirty markieren (sonst würde updateInterruptChain sie
        // im Schnellpfad überspringen). Der Read ist billig (zwei PIO-Flags).
        {
            const bool fi = afs_.hasInterrupt();
            if (fi != prev_floppy_int_) { bus_.markIntDirty(); prev_floppy_int_ = fi; }
        }

        // Update interrupt chain (Schnellpfad: no-op solange nicht dirty)
        bus_.updateInterruptChain();

        if (bus_.isWAIT()) {
            remaining--;
            total_cycles_++;
            continue;
        }

        if (bus_.isRESET()) {
            LOG_INFO("A5120", "RESET-Leitung gesetzt, ZVE1 wird zurückgesetzt");
            zre_.cpuReset();
            bus_.markIntDirty();
        }

        // ── Os-gated „gehaltener Bus" für den SCPX-Laufzeit-Read (§9.4b) ──────────
        // Der Boot nutzt die Per-Byte-Drossel (getunt, s. project_per_byte_busrq_model);
        // Laufzeit-.COM-Reads scheitern aber daran, weil ZVE1 in den Byte-Lücken mitläuft
        // (Kopf-Divergenz, Matcher-INT-Korruption, verfrühter [0x0000]-Restore — analyse §4/5/9).
        // Sobald der interaktive Prompt (ZVE1 @E079) einmal erreicht ist, fahren wir den
        // Laufzeit-Read wie echte HW: /BUSRQ über den GANZEN Transfer halten (nur ZVE2 liest).
        // Während des Boots (os_running_==false) ist dieser Block komplett inert → keine Regression.
        if (!os_running_ && zre_.cpuPC() == kScpxPromptPC) {
            os_running_ = true;
            LOG_INFO("A5120", "SCPX: Prompt E079 erreicht → gehaltener Bus für Laufzeit-Reads aktiv");
        }
        if (os_running_) {
            const uint16_t contVec = (uint16_t)(bus_.memRead(kScpxContVecAddr) |
                                     (bus_.memRead((uint16_t)(kScpxContVecAddr + 1)) << 8));
            // Fortschritt ([EC0B]≠E8B5) hebt eine etwaige Watchdog-Sperre wieder auf.
            if (contVec != kScpxPollWaitArm) held_read_watchdog_ = false;
            if (held_read_active_) {
                if (contVec != kScpxPollWaitArm) {
                    // ZVE2 hat das Read-Ende signalisiert ([EC0B]!=E8B5) → Bus freigeben;
                    // ZVE1 fährt mit JP (HL) fort und verarbeitet die gelesenen Daten.
                    held_read_active_ = false;
    held_read_cycles_   = 0;
    held_read_watchdog_ = false;
                    afs_.releaseHeldRead();
                    LOG_DEBUG("A5120", "SCPX gehaltener Read fertig ([EC0B]=%04X): ZVE1 fährt fort", contVec);
                } else if (held_read_cycles_ > kHeldReadWatchdogCycles) {
                    // No-Progress-Watchdog: ZVE2 findet sein IDAM nicht (Endlos-Re-Arm im
                    // Matcher E9C8, z. B. FM-Probe auf MFM-Spur).  Bus freigeben und Re-Engage
                    // sperren, bis ZVE1 fortschreitet — so nimmt ZVE1 seinen Index-Interrupt
                    // (Record-not-found → [EC0B]=E998 → FM/MFM-Retry) und der Read terminiert
                    // statt einzufrieren.  Trifft nur fehlschlagende Reads (Erfolg << Schwelle).
                    held_read_active_   = false;
                    held_read_watchdog_ = true;
                    afs_.releaseHeldRead();
                    LOG_INFO("A5120", "SCPX gehaltener Read: kein Fortschritt nach %lld Takten "
                             "→ Bus frei, ZVE1 nimmt Index-Timeout (FM/MFM-Retry)", held_read_cycles_);
                } else {
                    bus_.assertBUSRQ();   // Bus gehalten → ZVE1 bleibt eingefroren (nur ZVE2)
                }
            } else if (!held_read_watchdog_ &&
                       zre_.cpuPC() == kScpxPollWaitPC &&
                       contVec == kScpxPollWaitArm &&
                       afs_.isReadTransferActive()) {
                // ZVE1 parkt read-eindeutig am Poll-Wait (Setup fertig, [EC0B]=E8B5) und ein
                // Lese-Transfer läuft → ab hier den Bus halten (nur ZVE2 bis zur Completion).
                held_read_active_ = true;
                held_read_cycles_ = 0;   // Watchdog-Fenster neu starten
                bus_.assertBUSRQ();
                // Der Verify-Read hat engagiert → das Post-Write-Gnadenfenster hat seinen
                // Zweck erfüllt und wird sofort geschlossen, damit es nicht in einen
                // späteren normalen Read leckt (sonst hinge dort transferring_ fälschlich).
                afs_.endPostWriteGrace();
                LOG_DEBUG("A5120", "SCPX gehaltener Read: ZVE1@E8B5 eingefroren, ZVE2 liest");
            }
        }

        // BUSRQ: Bus-Verriegelung ZVE1↔ZVE2 (hardware-echt, K5122-Doku §5.6.1).
        //
        // /BUSRQ entsteht in der K5122 PRO BYTE aus dem RDY des Daten-PIO: ZVE2
        // erhält den Bus, wenn ein Byte bereitliegt, holt es ab (Port 0x16) und
        // verliert den Bus wieder, bis das nächste Byte ~1 Byteperiode später
        // bereitliegt.  Während /BUSRQ aktiv ist, läuft AUSSCHLIESSLICH ZVE2; in
        // den Byte-Lücken (BUSRQ frei) läuft ZVE1.  ZVE2 verliert den Bus damit
        // automatisch, sobald es aufhört zu lesen (fertig/idle) bzw. /STR=1 zieht —
        // es braucht KEINE programm-/größenspezifische Completion-Erkennung mehr
        // ([0x03F8]=3-Watch und OUT(13H)-Hack sind entfernt).
        //
        // Ist ZVE2 in Reset oder /WAIT (Legacy-ZVE1-Byte-Poll), gibt es keine
        // zweite CPU; dmaUpdate() gibt den Bus direkt frei.
        if (bus_.isBUSRQ()) {
            if (!busrq_active_) {            // neue DMA-Runde beginnt
                busrq_active_     = true;
                dma_saw_progress_ = false;   // ein altes [0x03F8]=3 der Vorrunde ignorieren,
            }                                // bis ZVE1 es löscht (CALL 0194, 0x01B3)
            // /BUSRQ assertiert während ZVE2 noch in Reset: der 3.-Stufen-Lader
            // (0x1F36) poist ZVE2 via [0x0000]=JP-Routine + OUT(04)=0 und löst /STR
            // aus — das /BUSRQ startet ZVE2 ab PC=0 (holt das aktuelle [0x0000]),
            // bevor ZVE1 [0x0000] restauriert.
            if (zre_.isZVE2InReset()) {   // also clears any /WAIT-ZVE2
                LOG_DEBUG("A5120", "ZVE2-Start aus Reset bei /BUSRQ: PC=0 → [0x0000]");
                zre_.zve2StartFromReset();
            }
            if (!zre_.isZVE2InReset() && !zre_.isZVE2Waiting()) {
                bus_master_zve2_ = true;
                int used2 = zre_.zve2Step();
                bus_master_zve2_ = false;
                if (used2 <= 0) used2 = 1;   // Sicherung gegen Endlosschleife
                remaining     -= used2;
                total_cycles_ += used2;
                if (held_read_active_) held_read_cycles_ += used2;   // No-Progress-Watchdog
                afs_.update(used2);          // Floppy-Timer (Byte-Bereitschaft, /STR-Abtastung)
                // ZVE2-Completion-Handshake [0x03F8]=3 (Boot-ROM 0x026B; Sekundär- und
                // 3.-Stufen-Lader nutzen denselben Flag).  Dies ist KEIN sektorgrößen-
                // abhängiges Signal mehr (der OUT(13H)-Hack ist entfernt), sondern der
                // programmweite „DMA fertig"-Handshake.  Nötig, weil der Chained Loader
                // (round 2) die DMA per ZEITSCHLEIFE (0x0441 DEC C;JR NZ) abwartet; unter
                // der Per-Byte-Verschränkung verschiebt sich das ZVE1:ZVE2-Tempo, sodass
                // ZVE1s Zeitschleife vor ZVE2s DMA-Ende abläuft → /STR=1 zu früh → Retry.
                // Der Flag-Watcher gibt den Bus exakt bei ZVE2-Completion frei (Transition
                // 0→3 innerhalb der Runde; das Level allein träfe ein altes =3).
                // Der SCPX-Laufzeit-Read („gehaltener Bus") signalisiert sein Ende über [EC0B],
                // nicht über [0x03F8]; der Boot-ROM/CP-A-Watcher darf ihm nicht dazwischenfunken.
                uint8_t df = held_read_active_ ? kZve2DoneValue
                                               : bus_.memRead(kZve2DoneFlagAddr);
                if (held_read_active_) {
                    // Watcher inaktiv während des gehaltenen Reads (Completion via [EC0B] oben).
                } else if (df != kZve2DoneValue) {
                    dma_saw_progress_ = true;
                } else if (dma_saw_progress_) {
                    afs_.endDmaTransfer();
                    LOG_DEBUG("A5120", "ZVE2 DMA fertig ([0x03F8]=3): BUSRQ frei, ZVE1.PC=%04X", zre_.cpuPC());
                }
                continue;                    // Verriegelung: ZVE1 läuft NICHT während /BUSRQ
            } else {
                afs_.dmaUpdate();
                remaining--;
                total_cycles_++;
                continue;
            }
        } else {
            busrq_active_ = false;          // Bus frei → nächste Assertion ist neue Runde
        }

        // Deliver INT if CPU can accept.
        //
        // SCPX-Mini-Stack-Guard (doc/analyse_scpx_com_load.md §5/§11, „Schicht 2"): SCPX
        // ruft die IDAM-Matcher-Routine u.a. DIREKT auf ZVE1 auf (nicht nur über die
        // ZVE2-DMA-Koroutine) — z.B. aus PIP.COMs Kopier-Verify beim Laufwerkswechsel
        // Write B: → Read A:.  Deren Setup biegt SP auf den Mini-Stack `0xEC0D` und liest
        // von dort per POP den CRC-Sollwert.  Wird in diesem Fenster ein Interrupt
        // zugestellt, PUSHt der Z80 die Rücksprungadresse auf `[0xEC0D/0E]` und überschreibt
        // genau diesen Sollwert → der CRC-Vergleich schlägt ewig fehl → Endlos-Retry (Hänger).
        // Solange SP im Mini-Stack-Fenster liegt, wird die Zustellung daher verzögert (der
        // Interrupt bleibt anstehend und kommt zu, sobald SP das Fenster verlässt — kein
        // Verlust).  ZVE2 und der held-bus-Lesepfad sind unberührt (dort steht ZVE1 auf
        // seinem normalen BIOS-Stack, nie in `[0xEC00,0xEC10]`) → keine Regression.
        const uint16_t zve1_sp = zre_.cpuSP();
        const bool sp_in_scpx_mini_stack = (zve1_sp >= 0xEC00 && zve1_sp <= 0xEC10);
        if (bus_.isINT() && zre_.cpuIFF1() && !sp_in_scpx_mini_stack) {
            uint8_t vec = bus_.interruptAcknowledge();
            LOG_DEBUG("A5120", "INT zugestellt: Vektor=0x%02X PC=%04X", vec, zre_.cpuPC());
            zre_.cpuInterrupt(vec);
        }

        if (bus_.isNMI()) {
            LOG_DEBUG("A5120", "NMI zugestellt: PC=%04X", zre_.cpuPC());
            zre_.cpuNMI();
            bus_.clearNMI();
        }

        const uint16_t pc_before = zre_.cpuPC();
        int used = zre_.cpuStep();
        remaining -= used;
        total_cycles_ += used;

        // Advance floppy index pulse simulation
        afs_.update(used);

        if (boot_trace_count_ < 80) {
            LOG_DEBUG("BOOT", "step=%d PC=%04X used=%d", boot_trace_count_, pc_before, used);
            ++boot_trace_count_;
        }

        // Clock CTC on ZRE card and serial card.  Advance by the T-states the
        // instruction actually took — the CTC prescaler divides the system
        // clock, so ticking once per instruction (avg ~6 T-states) ran the
        // real-time clock and baud generators ~6x too slow.
        // clockTick meldet zurück, ob ein CTC in diesem Fenster eine ZC/TO-Flanke
        // erzeugte (→ Interruptzustand kann sich geändert haben). Nur dann muss die
        // Interrupt-Chain neu berechnet werden.
        bool ctc_fired = zre_.clockTick(used);
        ctc_fired      = ass_.clockTick(used) || ctc_fired;
        if (ctc_fired) bus_.markIntDirty();

        // Service the keyboard: advance the 9600-baud serial-transmit timing
        // (release any keyboard→host bytes whose transmission has completed) and
        // drain command bytes the BIOS sent to the K7637 (reset / LED control),
        // letting it return its type-code acknowledge.  The serial latency keeps
        // the type-code acks from appearing the same instant the command is sent
        // — otherwise the timer-ISR keyboard scan races the foreground LED
        // handshake for the ack and the loser reads an empty SIO (0xFF → CR),
        // which floods the keyboard buffer and drops real keystrokes at the CCP.
        // Tastatur-Service kann ein Empfangsbyte an den SIO zustellen (irq_rx) oder
        // ein Kommando verarbeiten (irq_tx) — beides ändert den Interruptzustand.
        if (kbd_.service(total_cycles_)) bus_.markIntDirty();
    }

    return max_cycles - remaining;
}

bool A5120Machine::mountDisk(int drive, const std::string& path,
                              const std::string& format_name, bool wp) {
    if (drive < 0 || drive > 3) { last_error_ = "Invalid drive"; return false; }
    if (!drive_profiles_[drive].present) {
        last_error_ = "Kein Laufwerk an Slot " + std::to_string(drive);
        return false;
    }

    const DiskFormat* fmt = disk_formats_.find(format_name);
    if (!fmt) {
        last_error_ = "Unbekanntes Format: " + format_name;
        return false;
    }
    // BEWUSST KEINE drives:-Prüfung beim Mounten eines VORHANDENEN Images:
    //  - bei self-describing Containern (.hfe) ist der Formatname nur ein Platzhalter,
    //    die Geometrie kommt aus der Datei (so mountet z. B. tools/format_driver alle
    //    Slots nominell als "cpa780");
    //  - der Laufwerkstyp ist auf der A5120 reine BIOS-Software, Combo-Boot-Disketten
    //    betreiben an B:/C: bewusst Fremdtypen (CLAUDE.md, docs/format.md §11).
    // Die Kompatibilität wird dort erzwungen, wo das Format die Struktur wirklich
    // bestimmt: in createDisk() und in der angebotenen Auswahl (compatibleFormats()).
    std::lock_guard<std::mutex> lk(disk_mutex_);
    if (afs_.mountDisk(drive, path, *fmt, wp)) return true;

    // Grund aus dem Laufwerk übernehmen (Geometrie-/Verfahrenskonflikt); wurde das
    // Image gar nicht erst geöffnet, ist die Laufwerks-Meldung leer → Fallback.
    const std::string drv_err = afs_.drive(drive).lastError();
    last_error_ = drv_err.empty()
                      ? ("Image konnte nicht geöffnet werden: " + path)
                      : drv_err;
    return false;
}

bool A5120Machine::createDisk(int drive, const std::string& path,
                              const std::string& format_name, bool write_protect) {
    if (drive < 0 || drive > 3) { last_error_ = "Invalid drive"; return false; }

    const DriveProfile& prof = drive_profiles_[drive];
    if (!prof.present) {
        last_error_ = "Kein Laufwerk an Slot " + std::to_string(drive);
        return false;
    }

    // Leerer Formatname → Standardformat des Laufwerkstyps (`default_for:` im Katalog).
    const DiskFormat* fmt = nullptr;
    if (format_name.empty()) {
        fmt = disk_formats_.defaultFor(prof);
        if (!fmt) {
            last_error_ = "createDisk: kein Standardformat für Laufwerk '" + prof.name
                          + "' im Katalog (default_for)";
            return false;
        }
    } else {
        fmt = disk_formats_.find(format_name);
        if (!fmt) {
            last_error_ = "createDisk: unbekanntes Format '" + format_name + "'";
            return false;
        }
        if (!fmt->supportsDrive(prof.name)) {
            last_error_ = "createDisk: Format '" + format_name + "' passt nicht zum Laufwerk '"
                          + prof.name + "'";
            return false;
        }
    }

    // Verfahren kommt jetzt aus dem FORMAT (pro Spurbereich).  Für den HFE-Header und
    // rohe .img zählt das vorherrschende Verfahren; Mischdichte trägt DiskImage::create
    // spurweise ein.
    const Encoding enc = fmt->predominantEncoding();

    auto img = DiskImage::create(path, *fmt, write_protect, enc);
    if (!img) {
        last_error_ = "createDisk fehlgeschlagen (Format '" + fmt->name + "'): " + path;
        return false;
    }

    std::lock_guard<std::mutex> lk(disk_mutex_);
    if (afs_.mountDisk(drive, std::move(img), write_protect)) return true;
    const std::string drv_err = afs_.drive(drive).lastError();
    last_error_ = drv_err.empty()
                      ? ("createDisk: Mounten fehlgeschlagen: " + path)
                      : drv_err;
    return false;
}

std::string A5120Machine::defaultFormatName(int drive) const {
    if (drive < 0 || drive > 3) return "";
    const DiskFormat* f = disk_formats_.defaultFor(drive_profiles_[drive]);
    return f ? f->name : "";
}

std::vector<std::string> A5120Machine::compatibleFormats(int drive) const {
    std::vector<std::string> out;
    if (drive < 0 || drive > 3) return out;

    // Kompatibilität ist jetzt EXPLIZIT im Katalog deklariert (`drives:`), keine
    // Geometrie-Heuristik mehr — das Standardformat des Slots steht an erster Stelle.
    for (const DiskFormat* f : disk_formats_.forDrive(drive_profiles_[drive]))
        out.push_back(f->name);
    return out;
}

std::string A5120Machine::formatDescription(const std::string& format_name) const {
    const DiskFormat* f = disk_formats_.find(format_name);
    return f ? f->description : "";
}

bool A5120Machine::unmountDisk(int drive) {
    if (drive < 0 || drive > 3) return false;
    std::lock_guard<std::mutex> lk(disk_mutex_);
    return afs_.unmountDisk(drive);
}

bool A5120Machine::isDiskActive(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isDiskActive(drive);
}

bool A5120Machine::isDiskWriteProtected(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isDiskWriteProtected(drive);
}

bool A5120Machine::isDiskLedOn(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isDriveLedOn(drive);
}

bool A5120Machine::isMotorOn(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isMotorOn(drive);
}

bool A5120Machine::isHeadLoaded() const {
    return afs_.isHeadLoaded();
}

void A5120Machine::setDiskWriteProtect(int drive, bool wp) {
    if (drive < 0 || drive > 3) return;
    afs_.setWriteProtect(drive, wp);
}

void A5120Machine::keyPress(uint32_t kc, bool shift, bool ctrl) {
    std::lock_guard<std::mutex> lk(key_mutex_);
    key_queue_.push_back({kc, shift, ctrl, true});
}

void A5120Machine::keyRelease(uint32_t kc) {
    std::lock_guard<std::mutex> lk(key_mutex_);
    key_queue_.push_back({kc, false, false, false});
}

const uint8_t* A5120Machine::framebuffer() const {
    return screen_.getFramebuffer();
}

void A5120Machine::setDFUECallback(SerialCb cb) {
    ass_.setDFUERxCallback(std::move(cb));
}

void A5120Machine::dfueSend(uint8_t byte) {
    ass_.dfueRxByte(byte);       // externer serieller Empfang → SIO irq_rx möglich
    bus_.markIntDirty();
}

void A5120Machine::setPrinterCallback(SerialCb cb) {
    // Drain printer TX in run() or via callback — store for polling
    (void)cb;  // TODO: hook into SIO A32 ch B TX callback
}

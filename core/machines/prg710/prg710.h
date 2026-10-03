/**
 * @file prg710.h
 * @brief PRG 710 / PRG 710-1 (Numerik) — dritte Maschine des Kerns.
 *
 * Stand AP-P1c (doc/design/20_prg710.md §7.1, §10): ZRE K2521 (eine CPU, ROM + CTC +
 * PIO), Speicherverwaltung E8H–EBH mit 64-KB-OPS-RAM, K7024 (VRAM F800H, nur über
 * die Speicherverwaltung sichtbar), K5122 im `/WAIT`-Betrieb mit zwei K5601 und K8025
 * (50H).  **Eine** Klasse für beide Geräte: @ref Config::Variante wählt ROM und die
 * Polarität des Marken-FF der K5122 (710 low-aktiv, 710-1 high-aktiv, §4a.1).
 * Tastatur (AP-P2a/P2b): am **710** K7609 hinter dem 8279 der ATP 590068 (C8H/C9H),
 * am **710-1** die K7672 fest an SIO A32 Kanal B der K8025 (9-poliger D-SUB statt IFSS X6,
 * §3.9) — dieselbe K8025 in beiden Geräten; am 710-1 gehört Kanal B damit nicht dem
 * `SerialHub`.  `keyPress` reiht ein, `run()` gibt im Lauffaden ab.  Host-Tastencodes:
 * K7609 (710) bzw. K7672 (710-1), siehe k7609.h/k7672.h.
 *
 * **K7024 und Speicherverwaltung:** die K7024 meldet ihr VRAM im Konstruktor am
 * Systembus an.  Die CPU der K2521 greift aber über die Speicherverwaltung zu
 * (`K2521::setSpeicherweg`), nie über den Systembus-Speicher — die Anmeldung bleibt
 * deshalb ohne Wirkung, und die Speicherverwaltung ruft `memRead/memWrite` der Karte
 * direkt (VRAM-Weg).  So bleiben K7024, A5120 und K8915 unberührt.
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/k2521/k2521.h"
#include "core/cards/prg710_speicher/prg710_speicher.h"
#include "core/cards/k7024/k7024.h"
#include "core/cards/k8025/k8025.h"
#include "core/cards/k5122/k5122.h"
#include "core/cards/atp590068/atp590068.h"
#include "core/peripherals/k7609/k7609.h"
#include "core/peripherals/k7672/k7672.h"
#include "core/machines/laufwerke.h"
#include <atomic>
#include <deque>
#include <mutex>

class Prg710Machine : public K1520Machine {
public:
    struct Config {
        enum class Variante { Prg710, Prg710_1 };
        Variante variante = Variante::Prg710;
        /** Laufwerke an der K5122 (am Gerät: 2 × K5601, §3.6). */
        std::array<std::string, 4> laufwerke = {"K5601", "K5601", "none", "none"};
        /** Tastatur angeschlossen (710: K7609 an der ATP, 710-1: K7672 an A32-B). */
        bool tastatur = true;
        /** EPROMmer der ATP 590068 (D0H–D4H + ZRE-PIO 84H Bit 0, AP-P7b).  In beiden
         *  Varianten gesteckt: `PROG`/`PROG.COM` liegen auch auf den 710-1-Disketten [?]
         *  (Plan §8.6). */
        bool eprommer = true;
    };

    /// Takt der ZRE K2521 (2,4576 MHz).
    static constexpr uint32_t CPU_HZ = 2'457'600;

    Prg710Machine();
    explicit Prg710Machine(const Config& cfg);
    ~Prg710Machine() override = default;

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn() override;   ///< Netz-Ein: RAM mit 00H, dann /RESET
    void reset() override;     ///< systemweiter /RESET, RAM bleibt
    int  run(int max_cycles) override;
    void stop() override { stop_.store(true); }
    void nmi() override { nmi_taster_.store(true, std::memory_order_relaxed); }

    // ─── Bild ────────────────────────────────────────────────────────────────
    const uint8_t* framebuffer() const override { return screen_.getFramebuffer(); }
    int  fbWidth()  const override { return screen_.fbWidth(); }
    int  fbHeight() const override { return screen_.fbHeight(); }
    bool fbDirty()  const override { return screen_.fbDirty(); }
    void fbClearDirty() override   { screen_.fbClearDirty(); }
    void setConsoleMode(bool on) override { screen_.setConsoleMode(on); }
    bool consolePoll(int& x, int& y, char& ch) override { return screen_.pollTextChange(x, y, ch); }
    /// Bildspeicher direkt von der Karte (die CPU sieht ihn nur bei E8H[F] = FFH).
    uint8_t screenChar(int col, int row) const override { return screen_.vramRead(col, row); }

    // ─── Tastatur ────────────────────────────────────────────────────────────
    /**
     * @brief Taste drücken.  Einreihen unter Sperre, Abgabe an die Tastatur am Anfang von
     *        run() im Lauffaden (wie K8915/A5120).  Tastencodes: siehe K7609 (710) bzw.
     *        K7672 (710-1) — druckbares ASCII, Qt-Sondertasten, `QK_TASTE_BASE | Position`
     *        für die physische Taste (Bildschirmtastatur).
     */    void keyPress(uint32_t k, bool shift, bool ctrl) override;
    void keyRelease(uint32_t k) override;
    void setKeyRepeatRealtime(bool an) override { k7672_.setWiederholungEchtzeit(an); }   // 710-1; der 8279 wiederholt nicht

    int machineType() const override { return 1; }   // K1520_MACHINE_PRG710
    Config::Variante variante() const { return variante_; }

    // ─── Disketten (gemeinsamer Laufwerksbaustein) ───────────────────────────
    bool mountDisk(int d, const std::string& p, const std::string& f, bool wp) override {
        return lw_.mountDisk(d, p, f, wp);
    }
    bool mountDiskImage(int d, std::unique_ptr<DiskImage> img, bool wp) override {
        return lw_.mountDiskImage(d, std::move(img), wp);
    }
    bool createDisk(int d, const std::string& p, const std::string& f, bool wp) override {
        return lw_.createDisk(d, p, f, wp);
    }
    bool saveDiskAs(int d, const std::string& p, const std::string& f) override {
        return lw_.saveDiskAs(d, p, f);
    }
    bool unmountDisk(int d) override { return lw_.unmountDisk(d); }
    bool flushDisks() override       { return lw_.flushDisks(); }

    bool isDiskRawCompatible(int d) const override { return lw_.isDiskRawCompatible(d); }
    std::string diskPath(int d) const override      { return lw_.diskPath(d); }
    std::string diskContainer(int d) const override { return lw_.diskContainer(d); }
    std::string diskNotice(int d) const override    { return lw_.diskNotice(d); }
    std::string detectedFormatName(int d) const override { return lw_.detectedFormatName(d); }
    std::string defaultFormatName(int d) const override  { return lw_.defaultFormatName(d); }
    std::vector<std::string> compatibleFormats(int d) const override { return lw_.compatibleFormats(d); }
    std::string formatDescription(const std::string& f) const override { return lw_.formatDescription(f); }
    const FormatCatalog& formatCatalog() const override { return lw_.formatCatalog(); }

    bool isDiskActive(int d) const override         { return lw_.isDiskActive(d); }
    bool isDiskWriteProtected(int d) const override { return lw_.isDiskWriteProtected(d); }
    bool isDiskLedOn(int d) const override          { return lw_.isDiskLedOn(d); }
    bool isMotorOn(int d) const override            { return lw_.isMotorOn(d); }
    bool isHeadLoaded() const override              { return lw_.isHeadLoaded(); }
    void setDiskWriteProtect(int d, bool wp) override { lw_.setDiskWriteProtect(d, wp); }

    // ─── Serielle Schnittstellen (K8025, Namen/Zuordnung je Variante: AP-P4) ──
    k1520::serial::SerialHub* serialHub() override { return &hub_; }
    std::vector<k1520::serial::SerialAnschluss*> serielleAnschluesse() override;
    /// 710-1: Tastatur K7672 fest an A32-B (D-SUB, §3.9); am 710 ist die Tastatur kein serieller Kanal.
    std::vector<std::string> festeSchnittstellen() const override {
        if (variante_ == Config::Variante::Prg710_1) return {"Tastatur K7672 (A32-B)"};
        return {};
    }
    void setDFUECallback(SerialCb cb) override { ass_.setAbnehmer(K8025::DfueV24, std::move(cb)); }
    void dfueSend(uint8_t byte) override { ass_.einspeisen(K8025::DfueV24, byte); }
    // Diagnosezugang „Drucker“ = A32-B (IFSS Hauptdrucker X6); am 710-1 liegt dort die
    // Tastatur K7672 (`printerSend` speist deren Empfänger, kein Hub-Anschluss).
    void setPrinterCallback(SerialCb cb) override { ass_.setAbnehmer(K8025::Drucker, std::move(cb)); }
    void printerSend(uint8_t byte) override { ass_.einspeisen(K8025::Drucker, byte); }

    // ─── Diagnose ────────────────────────────────────────────────────────────
    /// Speicher aus Sicht der CPU (über die Speicherverwaltung; VRAM nur bei E8H[F] = FFH).
    uint8_t memReadDebug(uint16_t addr) override { return speicher_.memRead(addr); }
    void    memWriteDebug(uint16_t addr, uint8_t d) override { speicher_.memWrite(addr, d); }
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return lw_.lastError(); }

    // ─── Prg710-eigen (Tests, Werkzeuge) ─────────────────────────────────────
    K2521&           zre()      { return zre_; }
    Prg710Speicher&  speicher() { return speicher_; }
    K7024&           screen()   { return screen_; }
    K5122&           afs()      { return afs_; }
    K8025&           ass()      { return ass_; }
    Atp590068&       atp()      { return atp_; }     ///< 8279 (nur am 710 verdrahtet)
    /// EPROMmer mit virtuellem Sockel (Config::eprommer, beide Varianten).
    Eprommer590068&  eprommer() { return atp_.eprommer(); }
    K7609&           k7609()    { return k7609_; }
    K7672&           k7672()    { return k7672_; }   ///< nur am 710-1 an A32-B
    K1520Bus&        bus()      { return bus_; }
    uint64_t         totalCycles() const { return total_cycles_; }

    void     clearStop() { stop_.store(false); }
    uint16_t cpuPC() const { return zre_.cpu().PC; }
    void setCpuTraceCallback(std::function<void(const Z80&)> cb) { zre_.cpu().traceCallback = std::move(cb); }
    /// Beobachter für JEDEN Speicher- und E/A-Zugriff der CPU (Speicher: Speicherweg der K2521,
    /// nicht der Systembus; `memReadDebug` löst ihn nicht aus).  Leer = beide aus.
    void setBusTrace(K1520Bus::BusTrace cb) { mem_trace_ = cb; bus_.setTraceCallback(std::move(cb)); }
    const K1520Bus::IntAck& lastIntAck() const { return bus_.lastIntAck(); }

private:
    void resetHardware();
    void tastenAbgeben();     ///< Warteschlange an die Tastatur (nur im Lauffaden)

    const Config::Variante variante_;
    K1520Bus        bus_;
    K2521           zre_;
    Prg710Speicher  speicher_;
    K7024           screen_;
    K5122           afs_;       // 10H–18H, /WAIT-Betrieb
    Laufwerke       lw_;
    K8025           ass_;       // 50H–5FH
    Atp590068       atp_;       // 8279 C8H/C9H (nur 710) + EPROMmer D0H–D4H (beide)
    K7609           k7609_;     // 710: Matrix hinter dem 8279
    K7672           k7672_;     // 710-1: an SIO A32-B
    /// Nach den Karten: wird zuerst zerstört (hält Verweise auf ihre Anschlüsse).
    k1520::serial::SerialHub hub_{k1520::serial::PHI_NENN};
    uint64_t  serial_naechst_ = 0;

    K1520Bus::BusTrace mem_trace_;   ///< s. setBusTrace()
    std::atomic<bool> stop_{false};
    std::atomic<bool> nmi_taster_{false};
    uint64_t          total_cycles_ = 0;
    bool              prev_afs_int_ = false;

    struct TastenEreignis { uint32_t code; bool shift, ctrl, gedrueckt; };
    std::mutex                 tasten_sperre_;
    std::deque<TastenEreignis> tasten_;
};

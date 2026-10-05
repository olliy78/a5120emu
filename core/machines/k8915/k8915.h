/**
 * @file k8915.h
 * @brief K8915 V3 (5¼″, 1989) und Generation 2 — zweite Maschine des Kerns.
 *
 * Stand Etappe 4 (doc/design/16_k8915.md §8, AP-E1…AP-E4c): ZRE 045-8762, ATS
 * K7028.30 mit Tastatur K7672, K7024 (012-6820) und K5122 im `/WAIT`-Betrieb mit
 * zwei K5601.  Die Laufwerksverwaltung ist der gemeinsame Baustein @ref Laufwerke
 * (wie beim A5120).  Die seriellen Schnittstellen Drucker/IFSS1 (X3, SIO1-B, Drucker
 * des BIOS), V.24 (X4, SIO1-A) und DFÜ/IFSS2 (X5, SIO2-A) gehen seit AP-S5 über den
 * `SerialHub` nach außen (Entwurf 19; Namen nach der Gerätebeschriftung seit AP-S12);
 * der ältere Weg `setPrinterCallback`/`printerSend` (SIO1-B) und
 * `setDFUECallback`/`dfueSend` (SIO2-A) bleibt als Test-Unterbau.  Seit AP-E4b in
 * `libk1520core` (`k1520_create(K1520_MACHINE_K8915)`).
 *
 * Steckplätze am Gerät (§6.4): 3 = K5122, 4 = ZRE, 6 = ATS, 7 = K7024.
 *
 * **Generation 2** (doc/design/24_k8915_varianten.md R1–R3, AP-V6a): dieselbe Klasse mit
 * `Config::generation = Gen2`.  Statt der ZRE 045-8762 steckt eine **K2521** (ROM 175/176/177
 * bei 0000–0BFFH, 1 KB RAM 0C00–0FFFH, CTC 80H, PIO 84H) und eine **K3528** (64 KB RAM,
 * Register A8H–ABH); die K7024 trägt den A5120-Zeichensatz v171/v172.  ATS, K7672, K5122
 * (`/WAIT`), Laufwerke, `SerialHub`, Anzeigelatch, NMI-Weg und die RAF/K6022-Haken sind
 * dieselben Objekte mit derselben Verdrahtung.  Steckplätze der Gen 2 unbekannt [?, F2];
 * die Interruptkette folgt dem V3 (K5122 → K2521 → ATS).  **Vorgabe bleibt V3.**
 * Die austauschbare CPU-Karte steht hinter privaten Weichen (`cpuRef`, `zreTakt`,
 * `memCpu`); `zre()` ist nur am V3 gültig, `k2521()`/`ops()` nur an der Gen 2.
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/zre8762/zre8762.h"
#include "core/cards/k2521/k2521.h"
#include "core/cards/k3528/k3528.h"
#include "core/cards/k7024/k7024.h"
#include "core/cards/k7028/k7028.h"
#include "core/cards/k5122/k5122.h"
#include "core/machines/laufwerke.h"
#include "core/peripherals/k7672/k7672.h"
#include <atomic>
#include <cassert>
#include <deque>
#include <memory>
#include <mutex>

class K8915Machine : public K1520Machine {
public:
    /** @brief Ausstattung, die nicht auf den Karten steht. */
    struct Config {
        /** Bauform (doc/design/24_k8915_varianten.md R1).  Gen 1 fehlt mit Absicht:
         *  ohne Urlader-Baustein für 0400H nicht startfähig (F11, R4). */
        enum class Generation : uint8_t { V3, Gen2 };
        Generation generation = Generation::V3;
        /**
         * Nur Gen 2, **nur für Tests**: ROM-Inhalt 0000–0BFFH (0x0C00 Byte) statt der
         * Vorgabe 175/176/**repariertes** 177 (F9, 2026-10-05: 0A33H = 00H, Summe stimmt).
         * Ein Test setzt hier den unveränderten Abzug (`K8915G2_ZRE_ROM`, 0A33H = 04H), um
         * den Selbsttestfehler „ROM C“ des Originals nachzustellen.  nullptr = Vorgabe.
         * Der Speicher muss die Maschine überleben.
         */
        const uint8_t* gen2_rom = nullptr;
        /**
         * Prüfstecker an SIO1-A, SIO1-B und SIO2-A (§6.10) = Einstellung **Rx/Tx-Loop**
         * der drei Schnittstellen im `SerialHub` (Entwurf 19 §6.5, AP-S5).  Ohne sie
         * scheitert der SIO-Test des ROMs mit 'G' (alle drei Kanäle ohne Echo) und
         * der Lader startet erst nach `CR`.  **Vorgabe gesteckt** — vorläufig: ob das
         * Gerät die Schleifen selbst schließt, beantwortet erst der Anwender (§6.10).
         * Das laufende BIOS braucht das Echo nicht (Wächter
         * `K8915Seriell.BiosLaeuftOhneLoopWeiter`).
         */
        bool pruefstecker = true;
        /** Tastatur K7672 angeschlossen.  Ohne sie scheitert KEY mit 'A' (Gegenprobe). */
        bool tastatur = true;
        /** Laufwerke an der K5122 (Profilnamen wie beim A5120; "none" = unbestückt).
         *  Am Gerät des Anwenders: 2 × K5601 (§6.4). */
        std::array<std::string, 4> laufwerke = {"K5601", "K5601", "none", "none"};
    };

    using Generation = Config::Generation;

    /// Takt der K8915-CPU (2,4576 MHz) — Index- und Byteperiode der K5122 daraus.
    static constexpr uint32_t CPU_HZ = 2'457'600;

    K8915Machine();
    explicit K8915Machine(const Config& cfg);
    ~K8915Machine() override = default;

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    /** @brief Netz-Ein: DRAM mit 00H (§8a AP-E1: sonst meldet der Stub bei FFE0H
     *         womöglich zufällig „System im RAM"), dann /RESET. */
    void powerOn() override;
    /** @brief Rücksetztaste: systemweiter /RESET, RAM bleibt. */
    void reset() override;
    int  run(int max_cycles) override;
    void stop() override { stop_.store(true); }
    /** @brief NMI-Taster der Frontplatte (Steckeinheit 045-8569): nur vorgemerkt,
     *         die Flanke geht am Anfang des nächsten run() im Lauffaden an den Bus
     *         — wie die Tastenwarteschlange, sonst ein Wettlauf mit der Laufschleife.
     *         A8H, SIO, CTC und K5122 bleiben, wie sie sind (kein /RESET). */
    void nmi() override { nmi_taster_.store(true, std::memory_order_relaxed); }

    // ─── Bild ────────────────────────────────────────────────────────────────
    const uint8_t* framebuffer() const override { return screen_.getFramebuffer(); }
    int  fbWidth()  const override { return screen_.fbWidth(); }
    int  fbHeight() const override { return screen_.fbHeight(); }
    bool fbDirty()  const override { return screen_.fbDirty(); }
    void fbClearDirty() override   { screen_.fbClearDirty(); }
    void setConsoleMode(bool on) override { screen_.setConsoleMode(on); }
    bool consolePoll(int& x, int& y, char& ch) override {
        return screen_.pollTextChange(x, y, ch);
    }
    /// Bildspeicher der K7024 bei 1000H direkt von der Karte — die CPU-Sicht
    /// (`memReadDebug`) sähe dort bei A8H-Bit0 = 1 das RAM der ZRE.
    uint8_t screenChar(int col, int row) const override { return screen_.vramRead(col, row); }

    // ─── Tastatur K7672 an SIO2-B der ATS ────────────────────────────────────
    /**
     * @brief Tastenereignis aus einem BELIEBIGEN Faden (Oberfläche): nur unter
     *        Sperre eingereiht, weitergereicht an die K7672 erst am Anfang von
     *        run() im Lauffaden — wie beim A5120 (`key_queue_`).  Direkt an `kbd_`
     *        wäre es ein Wettlauf mit `kbd_.service()` (AP-E4b).  Tests im Lauffaden
     *        können weiter `keyboard()` direkt benutzen.
     */
    void keyPress(uint32_t k, bool shift, bool ctrl) override;
    void keyRelease(uint32_t k) override;
    void setKeyRepeatRealtime(bool an) override { kbd_.setWiederholungEchtzeit(an); }
    /** @brief K7672-Register 21H (Bit 3 = Senden frei/XON, Bit 0 = `ESC [?13h`),
     *         Stand am Ende des letzten run() — fadensicher lesbar. */
    uint8_t keyboardLeds() const override { return leds_.load(std::memory_order_relaxed); }

    // ─── Anzeigen außerhalb des Bildes (Stand am Ende des letzten run()) ─────
    int machineType() const override { return 2; }   // K1520_MACHINE_K8915
    /** @brief Anzeigefeld, Rohbyte des Latches 61H (aktiv low, §6.2; Reset FFH). */
    uint8_t panelLamps() const override { return lampen_.load(std::memory_order_relaxed); }
    /** @brief Empfangene `BEL` der K7672 (Summer), fortlaufend seit Erzeugung. */
    uint32_t bellCount() const override { return summer_.load(std::memory_order_relaxed); }

    // ─── Disketten: K5122 (Platz 3) über den gemeinsamen Laufwerksbaustein ───
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
    std::vector<std::string> compatibleFormats(int d) const override {
        return lw_.compatibleFormats(d);
    }
    std::string formatDescription(const std::string& f) const override {
        return lw_.formatDescription(f);
    }
    const FormatCatalog& formatCatalog() const override { return lw_.formatCatalog(); }

    bool isDiskActive(int d) const override         { return lw_.isDiskActive(d); }
    bool isDiskWriteProtected(int d) const override { return lw_.isDiskWriteProtected(d); }
    bool isDiskLedOn(int d) const override          { return lw_.isDiskLedOn(d); }
    bool isMotorOn(int d) const override            { return lw_.isMotorOn(d); }
    bool isHeadLoaded() const override              { return lw_.isHeadLoaded(); }
    void setDiskWriteProtect(int d, bool wp) override { lw_.setDiskWriteProtect(d, wp); }

    // ─── Serielle Schnittstellen (Entwurf 19 §3.2, AP-S5) ─────────────────────
    k1520::serial::SerialHub* serialHub() override { return &hub_; }
    std::vector<k1520::serial::SerialAnschluss*> serielleAnschluesse() override;
    std::vector<std::string> festeSchnittstellen() const override {
        return {K7028::TASTATUR_NAME};
    }

    // Alter Unterbau (Tests, `k1520_serial_*`): Drucker = Drucker/IFSS1 (SIO1-B, vom
    // BIOS benutzt), DFÜ = DFÜ/IFSS2 (SIO2-A, IFSS).  Ein gesetzter Rückruf nimmt dem Kanal den
    // Loop (Prüfstecker ab — früher: Abnehmer ersetzt die Rückschleife); ein leerer
    // gibt ihn nach der Maschinenvorgabe zurück, sofern kein Transport aktiv ist.
    // Belegt ein Transport/Loop den Stecker, gehen Rückruf und Einspeisen ins Leere.
    void setDFUECallback(SerialCb cb) override { altRueckruf(K7028::Sio2A, std::move(cb)); }
    /// Byte von AUSSEN am DFÜ-Kanal empfangen (z. B. ein Antwortzeichen).
    void dfueSend(uint8_t byte) override { ats_.empfange(K7028::Sio2A, byte); }
    void setPrinterCallback(SerialCb cb) override { altRueckruf(K7028::Sio1B, std::move(cb)); }
    /// Byte von AUSSEN am Druckerkanal empfangen (XON/XOFF eines Druckers).
    void printerSend(uint8_t byte) override { ats_.empfange(K7028::Sio1B, byte); }

    // ─── Diagnose ────────────────────────────────────────────────────────────
    /** @brief Speicher aus Sicht der CPU (A8H-Abbildung, sonst Systembus). */
    uint8_t memReadDebug(uint16_t addr) override { return memCpu(addr); }
    void    memWriteDebug(uint16_t addr, uint8_t d) override { memCpuW(addr, d); }
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return lw_.lastError(); }

    // ─── K8915-eigen (Tests, Werkzeuge) ──────────────────────────────────────
    Generation generation() const { return generation_; }
    /// ZRE 045-8762 — **nur am V3** (Vorbedingung; alle heutigen Aufrufer sind V3).
    K8915Zre&  zre()    { assert(zre8762_); return *zre8762_; }
    /// ZRE K2521 — nur an der Gen 2.
    K2521&     k2521()  { assert(k2521_); return *k2521_; }
    /// Speicherkarte K3528 (Register A8H) — nur an der Gen 2.
    K3528&     ops()    { assert(ops_); return *ops_; }
    K7028&     ats()    { return ats_; }
    K7672&     keyboard() { return kbd_; }
    K7024&     screen() { return screen_; }
    K5122&     afs()    { return afs_; }
    K1520Bus&  bus()    { return bus_; }
    uint64_t   totalCycles() const { return total_cycles_; }

    // ─── Werkzeuge (k1520dbg, boot_trace; §8a AP-E4d) ────────────────────────
    /** @brief Einen mit stop() angeforderten Halt zurücknehmen (vor dem nächsten run()). */
    void clearStop() { stop_.store(false); }
    uint16_t cpuPC() const { return cpuRef().PC; }
    /**
     * @brief Beobachter vor JEDEM Befehl der CPU.  Zusammen mit dem im Konstruktor
     *        gesetzten `abortBeforeExecute` (= stop_) hält ein stop() aus dem Rückruf
     *        die Maschine VOR dem Befehl an (run() kehrt dann mit 0 Takten zurück) —
     *        dieselbe Einzelschrittschnittstelle wie beim A5120.
     */
    void setCpuTraceCallback(std::function<void(const Z80&)> cb) {
        cpuRef().traceCallback = std::move(cb);
    }
    /**
     * @brief Beobachter für JEDEN Speicher- und E/A-Zugriff der CPU: der Busbeobachter
     *        UND der der ZRE (deren eigener Speicher geht nicht über den Bus, s.
     *        K8915Zre::setMemTrace).  Leer = beide aus.
     */
    void setBusTrace(K1520Bus::BusTrace cb) {
        if (zre8762_) zre8762_->setMemTrace(cb);
        if (ops_)     ops_->setMemTrace(cb);
        bus_.setTraceCallback(std::move(cb));
    }
    /** @brief Letzte Interrupt-Quittung des Busses (Vektor + Quellgerät). */
    const K1520Bus::IntAck& lastIntAck() const { return bus_.lastIntAck(); }

protected:
    K1520Bus& systemBus() override { return bus_; }

private:
    void resetHardware();
    void altRueckruf(K7028::Kanal k, SerialCb cb);
    void tastenAbgeben();     ///< Warteschlange an die K7672 (nur im Lauffaden)
    void anzeigenSpiegeln();  ///< Anzeigen für fremde Fäden spiegeln (nur im Lauffaden)

    // Weichen zur CPU-Karte der jeweiligen Generation (R1, „Umbau-Muster“).
    Z80&       cpuRef()       { return zre8762_ ? zre8762_->cpu() : k2521_->cpu(); }
    const Z80& cpuRef() const { return zre8762_ ? zre8762_->cpu() : k2521_->cpu(); }
    /// CTC der ZRE weiterzählen; true bei ZC/TO-Flanke.
    bool zreTakt(int t) { return zre8762_ ? zre8762_->clockTick(t) : k2521_->clockTick(t); }
    InterruptSlave& zreInt() {
        return zre8762_ ? static_cast<InterruptSlave&>(*zre8762_)
                        : static_cast<InterruptSlave&>(*k2521_);
    }
    /// Speicher aus Sicht der CPU (V3: A8H der 045-8762; Gen 2: A8H der K3528).
    uint8_t memCpu(uint16_t a) { return zre8762_ ? zre8762_->memRead(a) : ops_->memRead(a); }
    void memCpuW(uint16_t a, uint8_t d) {
        if (zre8762_) zre8762_->memWrite(a, d); else ops_->memWrite(a, d);
    }

    const Generation generation_;
    K1520Bus  bus_;
    std::unique_ptr<K8915Zre> zre8762_;   // Platz 4 (nur V3)
    std::unique_ptr<K2521>    k2521_;     // ZRE (nur Gen 2, Platz [?, F2])
    std::unique_ptr<K3528>    ops_;       // Speicher + A8H (nur Gen 2)
    K7028     ats_;       // Platz 6, 40H–5FH + Anzeigelatch 60H–67H
    K7024     screen_;    // Platz 7, VRAM 1000H
    K7672     kbd_;       // an SIO2-B
    K5122     afs_;       // Platz 3, 10H–18H, /WAIT-Betrieb
    Laufwerke lw_;        // Laufwerksverwaltung (gemeinsam mit dem A5120)
    const bool pruefstecker_;
    /// Nach den Karten: wird zuerst zerstört (hält Verweise auf ihre Anschlüsse).
    k1520::serial::SerialHub hub_{k1520::serial::PHI_NENN};
    uint64_t  serial_naechst_ = 0;   ///< nächster Blick der Wandler (Taktzahl)

    std::atomic<bool> stop_{false};
    std::atomic<bool> nmi_taster_{false};   ///< NMI-Taster gedrückt, noch nicht zugestellt
    uint64_t          total_cycles_ = 0;
    bool              prev_afs_int_ = false;   // Flanke des K5122-Interrupts (Index, MKE)

    struct TastenEreignis { uint32_t code; bool shift, ctrl, gedrueckt; };
    std::mutex                 tasten_sperre_;
    std::deque<TastenEreignis> tasten_;

    std::atomic<uint8_t>  lampen_{0xFF};
    std::atomic<uint8_t>  leds_{0};
    std::atomic<uint32_t> summer_{0};
};

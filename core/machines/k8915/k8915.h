/**
 * @file k8915.h
 * @brief K8915 V3 (5¼″, 1989) — zweite Maschine des Kerns.
 *
 * Stand Etappe 4 (doc/design/16_k8915.md §8, AP-E1…AP-E4c): ZRE 045-8762, ATS
 * K7028.30 mit Tastatur K7672, K7024 (012-6820) und K5122 im `/WAIT`-Betrieb mit
 * zwei K5601.  Die Laufwerksverwaltung ist der gemeinsame Baustein @ref Laufwerke
 * (wie beim A5120).  Drucker (SIO1-B) und DFÜ (SIO2-A, vorläufig) gehen seit
 * AP-E4c über die ABI nach außen (`setDFUECallback`/`dfueSend`,
 * `setPrinterCallback`/`printerSend`).  Seit AP-E4b in `libk1520core`
 * (`k1520_create(K1520_MACHINE_K8915)`).
 *
 * Steckplätze am Gerät (§6.4): 3 = K5122, 4 = ZRE, 6 = ATS, 7 = K7024.
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/zre8762/zre8762.h"
#include "core/cards/k7024/k7024.h"
#include "core/cards/k7028/k7028.h"
#include "core/cards/k5122/k5122.h"
#include "core/machines/laufwerke.h"
#include "core/peripherals/k7672/k7672.h"
#include <atomic>
#include <deque>
#include <mutex>

class K8915Machine : public K1520Machine {
public:
    /** @brief Ausstattung, die nicht auf den Karten steht. */
    struct Config {
        /**
         * Prüfstecker/Rückschleife an SIO1-A, SIO1-B und SIO2-A (§6.10).  Ohne sie
         * scheitert der SIO-Test des ROMs mit 'G' (alle drei Kanäle ohne Echo) und
         * der Lader startet erst nach `CR`.  **Vorgabe gesteckt** — vorläufig: ob das
         * Gerät die Schleifen selbst schließt, beantwortet erst der Anwender (§6.10).
         */
        bool pruefstecker = true;
        /** Tastatur K7672 angeschlossen.  Ohne sie scheitert KEY mit 'A' (Gegenprobe). */
        bool tastatur = true;
        /** Laufwerke an der K5122 (Profilnamen wie beim A5120; "none" = unbestückt).
         *  Am Gerät des Anwenders: 2 × K5601 (§6.4). */
        std::array<std::string, 4> laufwerke = {"K5601", "K5601", "none", "none"};
    };

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

    // ─── DFÜ (SIO2-A) und Drucker (SIO1-B) nach außen (§8a AP-E4c) ───────────
    // Kanalzuordnung: Drucker = SIO1-B (vom BIOS benutzt, §4.4 belegt); DFÜ =
    // vorläufig SIO2-A — die IFSS-Kanalzuordnung ist eine offene Anwenderfrage
    // (§6.6 [?]).  Ein gesetzter Callback ERSETZT den Prüfstecker (Rückschleife)
    // nur auf seinem EIGENEN Kanal (`K7028::service`); die beiden übrigen Kanäle
    // bleiben zurückgeschleift, solange `Config::pruefstecker` es vorsieht — sonst
    // schlüge der ROM-Selbsttest mit einem angeschlossenen Abnehmer fehl.
    void setDFUECallback(SerialCb cb) override {
        ats_.setAbnehmer(K7028::Sio2A, std::move(cb));
    }
    /// Byte von AUSSEN am DFÜ-Kanal empfangen (z. B. ein Antwortzeichen).
    void dfueSend(uint8_t byte) override { ats_.empfange(K7028::Sio2A, byte); }
    void setPrinterCallback(SerialCb cb) override {
        ats_.setAbnehmer(K7028::Sio1B, std::move(cb));
    }
    /// Byte von AUSSEN am Druckerkanal empfangen (XON/XOFF eines Druckers).
    void printerSend(uint8_t byte) override { ats_.empfange(K7028::Sio1B, byte); }

    // ─── Diagnose ────────────────────────────────────────────────────────────
    /** @brief Speicher aus Sicht der CPU (A8H-Abbildung, sonst Systembus). */
    uint8_t memReadDebug(uint16_t addr) override { return zre_.memRead(addr); }
    void    memWriteDebug(uint16_t addr, uint8_t d) override { zre_.memWrite(addr, d); }
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return lw_.lastError(); }

    // ─── K8915-eigen (Tests, Werkzeuge) ──────────────────────────────────────
    K8915Zre&  zre()    { return zre_; }
    K7028&     ats()    { return ats_; }
    K7672&     keyboard() { return kbd_; }
    K7024&     screen() { return screen_; }
    K5122&     afs()    { return afs_; }
    K1520Bus&  bus()    { return bus_; }
    uint64_t   totalCycles() const { return total_cycles_; }

    // ─── Werkzeuge (k1520dbg, boot_trace; §8a AP-E4d) ────────────────────────
    /** @brief Einen mit stop() angeforderten Halt zurücknehmen (vor dem nächsten run()). */
    void clearStop() { stop_.store(false); }
    uint16_t cpuPC() const { return zre_.cpu().PC; }
    /**
     * @brief Beobachter vor JEDEM Befehl der CPU.  Zusammen mit dem im Konstruktor
     *        gesetzten `abortBeforeExecute` (= stop_) hält ein stop() aus dem Rückruf
     *        die Maschine VOR dem Befehl an (run() kehrt dann mit 0 Takten zurück) —
     *        dieselbe Einzelschrittschnittstelle wie beim A5120.
     */
    void setCpuTraceCallback(std::function<void(const Z80&)> cb) {
        zre_.cpu().traceCallback = std::move(cb);
    }
    /**
     * @brief Beobachter für JEDEN Speicher- und E/A-Zugriff der CPU: der Busbeobachter
     *        UND der der ZRE (deren eigener Speicher geht nicht über den Bus, s.
     *        K8915Zre::setMemTrace).  Leer = beide aus.
     */
    void setBusTrace(K1520Bus::BusTrace cb) {
        zre_.setMemTrace(cb);
        bus_.setTraceCallback(std::move(cb));
    }
    /** @brief Letzte Interrupt-Quittung des Busses (Vektor + Quellgerät). */
    const K1520Bus::IntAck& lastIntAck() const { return bus_.lastIntAck(); }

private:
    void resetHardware();
    void tastenAbgeben();     ///< Warteschlange an die K7672 (nur im Lauffaden)
    void anzeigenSpiegeln();  ///< Anzeigen für fremde Fäden spiegeln (nur im Lauffaden)

    K1520Bus  bus_;
    K8915Zre  zre_;       // Platz 4
    K7028     ats_;       // Platz 6, 40H–5FH + Anzeigelatch 60H–67H
    K7024     screen_;    // Platz 7, VRAM 1000H
    K7672     kbd_;       // an SIO2-B
    K5122     afs_;       // Platz 3, 10H–18H, /WAIT-Betrieb
    Laufwerke lw_;        // Laufwerksverwaltung (gemeinsam mit dem A5120)

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

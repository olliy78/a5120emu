/**
 * @file p8000.h
 * @brief Robotron P8000 — fünfte Maschine des Kerns.  Stand AP P7a: **nur die 8-Bit-Seite**
 *        (U880-Rechnerkarte + Floppy + Kern-Terminal an tty1); 16-Bit-Karte, Kopplung und WDC
 *        folgen (P10/P11/P13).
 *
 * Quelle: doc/design/25_p8000.md §10.1–§10.7 (Entwurf), Karten `P8000Karte8` (P5e) und
 * `P8000Floppy8` (P5f), Terminal `k1520::p8000::Terminal` (P6).
 *
 * **Zeitbasis** = Φ der 8-Bit-Karte (4 MHz, §10.2).  Laufschleife je Befehl:
 * `karte.schritt()` (DMA hält die CPU über /BUSRQ; EPROM-Wartetakte eingerechnet) →
 * `karte.takt(n)` (CTCs) → `floppy.takt(n)` (U8272, DRQ→RDY) → Terminal-Anschluss →
 * SerialHub.  Kein ZVE1/ZVE2-Mechanismus (§7 Risiko 5).
 *
 * **Serielle Kanäle (§10.6):** tty1 (SIO0-B) = Konsole, fest am Kern-Terminal
 * (`festeSchnittstellen()`); tty0/tty2/tty3 hängen am `SerialHub` — ohne Verbindung verfallen
 * ihre Zeichen in Zeichenzeit (sonst staut sich der Sender: MON8-Hardwaretest ERROR 21).
 *
 * **Ohne 16-Bit-Karte** sind die Kopplungseingänge offen (Pull-ups, §10.4 `karte16 = false`).
 *
 * **Save-State P8KS v1 (P7e, Entwurf §10.2):** `saveState/loadState` (Datei) und
 * `stateBytes/restoreStateBytes` (Speicher).  Nicht: Medieninhalt, EPROM, Hub-Verbindungen und
 * der Zustand der Hub-Wandler (Zeichen in Flug an tty0/2/3).
 *
 * Nicht hier (spätere APs): Rahmenpuffer mit dem Terminal-Zeichensatz (P16; bis dahin liefert
 * `framebuffer()` ein leeres Bild).
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/cards/p8000/floppy8.h"
#include "core/cards/p8000/karte8.h"
#include "core/machines/machine.h"
#include "core/peripherals/p8000_terminal/terminal.h"
#include "core/peripherals/p8000_terminal/terminal_anschluss.h"
#include "core/serial/hub.h"
#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class P8000Machine : public K1520Machine {
public:
    struct Config {
        using Index8 = P8000Karte8::Config::Index;
        using Mon8   = P8000Karte8::Config::Mon8;
        Index8   index8 = Index8::I3;
        Mon8     mon8   = Mon8::V3_1;
        uint32_t takt8_hz = 4'000'000;
        /// Bestückung der Floppy-Anschlüsse (X8 = 0, X9 = 1, X10 extern = 2/3).
        std::array<std::string, 4> laufwerke = {"K5601", "K5601", "none", "none"};
        uint8_t  latch_start = 0x00, adp_start = 0x00, ram_fuellwert = 0x00;
    };

    /// Kanal des Kern-Terminals (Konsole des U880-Monitors und von UDOS).
    static constexpr int KONSOLE_TTY = 1;

    P8000Machine();
    explicit P8000Machine(const Config& cfg);
    ~P8000Machine() override;

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn() override;
    /// Taste /RESP: /RES an alle Bausteine der 8-Bit-Karte, RESI = 0 (§10.2).
    void reset() override;
    int  run(int max_cycles) override;
    void stop() override { stop_.store(true); }
    /// NMI-Taste (/NMIP): Weiche nach PIO0-B7 (offen = U880), zugestellt am Anfang von run().
    void nmi() override { nmi_taster_.store(true, std::memory_order_relaxed); }

    // ─── Bild: das Kern-Terminal der Konsole ────────────────────────────────
    const uint8_t* framebuffer() const override { return fb_.data(); }
    int  fbWidth()  const override { return FB_BREITE; }
    int  fbHeight() const override { return FB_HOEHE; }
    bool fbDirty()  const override { return false; }
    void fbClearDirty() override {}
    void setConsoleMode(bool) override {}
    bool consolePoll(int&, int&, char&) override { return false; }
    /// Zeichen der Zelle (Spalte, Zeile) des Konsolen-Terminals (80 × 24); 0 außerhalb.
    uint8_t screenChar(int col, int row) const override;

    // ─── Tastatur des Terminals (fadensicher eingereiht) ────────────────────
    /// Druckbares ASCII (das Zeichen selbst), Steuerzeichen 01H–1FH, Qt-Return/Enter/Escape/
    /// Backspace/Tab/Delete.  `ctrl` macht aus einem Buchstaben das Steuerzeichen.
    void keyPress(uint32_t qt_keycode, bool shift, bool ctrl) override;
    void keyRelease(uint32_t) override {}
    void setKeyRepeatRealtime(bool) override {}

    int machineType() const override { return 4; }   // K1520_MACHINE_P8000 (Entwurf §10.9)
    /// Kein K1520-Bus: RAF und K6022 sind nicht steckbar (Entwurf §10.1).
    bool zusatzkartenSteckbar() const override { return false; }

    // ─── Disketten (zwei interne Laufwerke X8/X9, U8272-Weg) ────────────────
    bool mountDisk(int d, const std::string& p, const std::string& f, bool wp) override {
        return lw().mountDisk(d, p, f, wp);
    }
    bool mountDiskImage(int d, std::unique_ptr<DiskImage> img, bool wp) override {
        return lw().mountDiskImage(d, std::move(img), wp);
    }
    bool createDisk(int d, const std::string& p, const std::string& f, bool wp) override {
        return lw().createDisk(d, p, f, wp);
    }
    bool saveDiskAs(int d, const std::string& p, const std::string& f) override {
        return lw().saveDiskAs(d, p, f);
    }
    bool unmountDisk(int d) override { return lw().unmountDisk(d); }
    bool flushDisks() override       { return lw().flushDisks(); }

    bool isDiskRawCompatible(int d) const override { return lw().isDiskRawCompatible(d); }
    std::string diskPath(int d) const override      { return lw().diskPath(d); }
    std::string diskContainer(int d) const override { return lw().diskContainer(d); }
    std::string diskNotice(int d) const override    { return lw().diskNotice(d); }
    std::string detectedFormatName(int d) const override { return lw().detectedFormatName(d); }
    std::string defaultFormatName(int d) const override  { return lw().defaultFormatName(d); }
    std::vector<std::string> compatibleFormats(int d) const override { return lw().compatibleFormats(d); }
    std::string formatDescription(const std::string& f) const override { return lw().formatDescription(f); }
    const FormatCatalog& formatCatalog() const override { return lw().formatCatalog(); }

    bool isDiskActive(int d) const override         { return lw().isDiskActive(d); }
    bool isDiskWriteProtected(int d) const override { return lw().isDiskWriteProtected(d); }
    /// Lampe = Motor (PIO2-A0..3 bzw. Auswahl) oder Zugriff des U8272 auf das Laufwerk.
    bool isDiskLedOn(int d) const override;
    bool isMotorOn(int d) const override;
    bool isHeadLoaded() const override              { return lw().isHeadLoaded(); }
    void setDiskWriteProtect(int d, bool wp) override { lw().setDiskWriteProtect(d, wp); }

    // ─── Serielle Schnittstellen: tty0, tty2, tty3 am Hub; tty1 am Kern-Terminal ─
    k1520::serial::SerialHub* serialHub() override { return &hub_; }
    std::vector<k1520::serial::SerialAnschluss*> serielleAnschluesse() override;
    std::vector<std::string> festeSchnittstellen() const override { return {"Terminal P8000 (tty1)"}; }
    /// Alter Unterbau (Rückruf/Einspeisen) gibt es am P8000 nicht — alle tty gehen über den
    /// Hub bzw. das Kern-Terminal.
    void setDFUECallback(SerialCb) override {}
    void dfueSend(uint8_t) override {}

    // ─── Diagnose (U880-Sicht: ADP/RFF wirksam, ohne Wartetakt-Zählung) ─────
    uint8_t memReadDebug(uint16_t addr) override;
    void    memWriteDebug(uint16_t addr, uint8_t data) override;
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return lw().lastError(); }

    // ─── Save-State P8KS v1 (Entwurf 25 §10.2) ───────────────────────────────
    /// Kennung „P8KS“, dann `P8000_STAND`, dann Abschnitte `[Kennung u8][Länge u32][Bytes]`
    /// (unbekannte Abschnitte werden übersprungen).  Die Disketten werden NICHT gesichert — der
    /// Aufrufer mountet vor dem Laden dieselben Abbilder (nur die Kopfposition wird gesetzt).
    static constexpr uint8_t P8000_STAND = 1;
    std::vector<uint8_t> stateBytes() const;
    /// Nur laden, wenn die Konfiguration (Index, ROM-Satz, Takt, Laufwerke) übereinstimmt;
    /// scheitert das Laden mittendrin, wird der alte Zustand wiederhergestellt.
    bool restoreStateBytes(const std::vector<uint8_t>& b);
    bool saveState(const std::string& path) const;
    bool loadState(const std::string& path);
    /// Grund des letzten gescheiterten Ladens ("" = keiner).
    const std::string& stateError() const { return state_error_; }

    // ─── P8000-eigen (Tests, Werkzeuge) ──────────────────────────────────────
    P8000Karte8&  karte8()  { return karte_; }
    P8000Floppy8& floppy8() { return floppy_; }
    K1520Bus&     bus()     { return bus_; }
    k1520::p8000::Terminal&       terminal()       { return term_; }
    const k1520::p8000::Terminal& terminal() const { return term_; }
    /// Zeile @p z (0–23) des Konsolen-Terminals als Text (80 Zeichen).
    std::string terminalZeile(int z) const { return term_.text(z); }
    uint64_t totalCycles() const { return total_cycles_; }
    void     clearStop() { stop_.store(false); }
    uint16_t cpuPC() { return karte_.cpu().PC; }
    const Config& config() const { return cfg_; }
    void setBusTrace(K1520Bus::BusTrace cb) { bus_.setTraceCallback(std::move(cb)); }
    void setCpuTraceCallback(std::function<void(const Z80&)> cb) { karte_.cpu().traceCallback = std::move(cb); }

    static constexpr int FB_BREITE = 640, FB_HOEHE = 288;   ///< Zelle 8 × 12 (Entwurf §10.6)

protected:
    K1520Bus& systemBus() override { return bus_; }

private:
    static P8000Karte8::Config karteConfig(const Config& c);
    static P8000Floppy8::Config floppyConfig(const Config& c);
    Laufwerke&       lw()       { return floppy_.laufwerke(); }
    const Laufwerke& lw() const { return const_cast<P8000Floppy8&>(floppy_).laufwerke(); }
    void nachReset();
    void tastenAbgeben();
    bool wendeAbschnitteAn(const std::vector<uint8_t>& b);
    void configAbschnitt(std::vector<uint8_t>& out) const;

    const Config cfg_;
    K1520Bus     bus_;
    P8000Karte8  karte_;
    P8000Floppy8 floppy_;
    k1520::p8000::Terminal          term_;
    k1520::p8000::TerminalAnschluss term_anschluss_;
    k1520::serial::SerialHub        hub_;
    uint64_t serial_naechst_ = 0;

    struct Taste { uint32_t code; bool ctrl; };
    std::mutex        tasten_sperre_;
    std::deque<Taste> tasten_;

    std::vector<uint8_t> fb_ = std::vector<uint8_t>(size_t(FB_BREITE) * FB_HOEHE, 0);
    std::atomic<bool> stop_{false};
    std::atomic<bool> nmi_taster_{false};
    uint64_t total_cycles_ = 0;
    std::string state_error_;
};

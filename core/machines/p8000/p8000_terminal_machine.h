/**
 * @file p8000_terminal_machine.h
 * @brief „P8000 Terminal" — das Originalterminal Typ 2 + K7673.09 als eigenständige Maschine
 *        ohne Rechner (AP P20c/P20d, Entwurf 25 §11, Entwurf 28 §7).
 *
 * Kleinste saubere Lösung für die C-ABI: ein schlanker `K1520Machine`-Wrapper um eine
 * `P8000TerminalEinheit` (`K1520_MACHINE_P8000_TERMINAL` = 5).  Die serielle Leitung XB5 hängt
 * am **eigenen `SerialHub` der Einheit** (ein Anschluss, Index 0) — Telnet-Client/-Server,
 * RFC 2217 oder Datei wie jede Schnittstelle; Vorgabe Telnet-**Client** auf 127.0.0.1:5000
 * (ein Rechnerprozess bietet seine ttys als Server an — Arbeitsplatzbetrieb).  Mehrere Prozesse
 * = mehrere Arbeitsplätze.
 *
 * **Zeitbasis von `run()`** = interner Z8-Takt des Terminals (3 686 400 Hz).  Kein Bus, keine
 * Disketten (alle Laufwerksfunktionen verneinen), keine Zusatzkarten.
 *
 * Tasten wie am P8000 (`keyPress`: ASCII/Qt-Kodes/`0x02000000 + TerminalTaste`, getippt mit
 * Haltezeit über die Matrix; `0x04000000 | Zeile << 8 | Spalte` = Matrixtaste drücken bis
 * `keyRelease` mit demselben Kode).  Bild: `framebuffer()` = Pixelbild 640 × 312 (0 dunkel,
 * 1 normal, 2 hell), Zellen über `einheit().hw()`; `keyboardLeds()` = LEDs der K7673;
 * `bellCount()` = Klingel.  Save-State „P8TM" v1 = Konfiguration + Einheit (P8TH).
 */
#pragma once

#include "core/machines/machine.h"
#include "core/peripherals/p8000_terminal_hw/terminal_einheit.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class P8000TerminalMachine : public K1520Machine {
public:
    struct Config {
        k1520::p8000::P8000TerminalEinheitConfig einheit;
        /// Vorgabe des Hub-Anschlusses XB5 (Telnet-Client auf 127.0.0.1:5000).
        k1520::serial::SerialKonfig leitung = vorgabeLeitung();
        /// Gleich beim Anlegen verbinden (sonst über `k1520_serial_start`).
        bool verbinden = false;
        static k1520::serial::SerialKonfig vorgabeLeitung() {
            k1520::serial::SerialKonfig k;
            k.betriebsart = k1520::serial::Betriebsart::Telnet;
            k.rolle = k1520::serial::Rolle::Client;
            k.host = "127.0.0.1";
            k.port = 5000;
            return k;
        }
    };

    /// Ein Schlüssel der Terminalbestückung (`firmware`, `zeichensatz`, `teiler`) aus einem
    /// Konfigurationstext — gemeinsam für `k1520_create_p8000_terminal` und `k1520_create_p8000`
    /// (`terminal=original`).  Rückgabe 1 = erkannt, 0 = nicht mein Schlüssel, -1 = Fehler.
    static int hwSchluessel(const std::string& key, const std::string& val,
                            k1520::p8000::P8000TerminalEinheitConfig& c, std::string& fehler);
    /// Ganzer Konfigurationstext „schluessel=wert,…" (NULL/"" = Vorgabe); false + Grund.
    /// Schlüssel: firmware (5.0), zeichensatz (ezs-dzs | dzs-ezs = Bestückung ZG1-ZG2),
    /// teiler (7 | 8), art (telnet | rfc2217 | datei), rolle (client | server), host, port,
    /// datei, verbinden (0 | 1).
    static bool konfigAusText(const char* text, Config& cfg, std::string& fehler);

    P8000TerminalMachine() : P8000TerminalMachine(Config{}) {}
    explicit P8000TerminalMachine(const Config& cfg);
    ~P8000TerminalMachine() override;

    void powerOn() override;
    /// Das Terminal hat keine Reset-Taste: wie Netz aus/ein.
    void reset() override { powerOn(); }
    int  run(int max_cycles) override;
    void stop() override { stop_.store(true); }

    const uint8_t* framebuffer() const override { return einheit_.hw().pixel().data(); }
    int  fbWidth()  const override { return einheit_.hw().pixelBreite(); }
    int  fbHeight() const override { return einheit_.hw().pixelHoehe(); }
    bool fbDirty()  const override { return einheit_.hw().bilder() != bild_gesehen_; }
    void fbClearDirty() override { bild_gesehen_ = einheit_.hw().bilder(); }
    void setConsoleMode(bool) override {}
    bool consolePoll(int&, int&, char&) override { return false; }
    uint8_t screenChar(int col, int row) const override;

    void keyPress(uint32_t qt_keycode, bool shift, bool ctrl) override;
    void keyRelease(uint32_t code) override;
    void setKeyRepeatRealtime(bool) override {}
    uint8_t keyboardLeds() const override { return einheit_.tastatur().leds(); }
    int machineType() const override { return 5; }   // K1520_MACHINE_P8000_TERMINAL
    uint32_t bellCount() const override { return einheit_.hw().klingel(); }
    bool zusatzkartenSteckbar() const override { return false; }

    // Keine Laufwerke.
    bool mountDisk(int, const std::string&, const std::string&, bool) override { return false; }
    bool mountDiskImage(int, std::unique_ptr<DiskImage>, bool) override { return false; }
    bool createDisk(int, const std::string&, const std::string&, bool) override { return false; }
    bool saveDiskAs(int, const std::string&, const std::string&) override { return false; }
    bool unmountDisk(int) override { return false; }
    bool flushDisks() override { return false; }
    bool isDiskRawCompatible(int) const override { return false; }
    std::string diskPath(int) const override { return ""; }
    std::string diskContainer(int) const override { return ""; }
    std::string diskNotice(int) const override { return ""; }
    std::string detectedFormatName(int) const override { return ""; }
    std::string defaultFormatName(int) const override { return ""; }
    std::vector<std::string> compatibleFormats(int) const override { return {}; }
    std::string formatDescription(const std::string&) const override { return ""; }
    const FormatCatalog& formatCatalog() const override;
    bool isDiskActive(int) const override { return false; }
    bool isDiskWriteProtected(int) const override { return false; }
    bool isDiskLedOn(int) const override { return false; }
    bool isMotorOn(int) const override { return false; }
    bool isHeadLoaded() const override { return false; }
    void setDiskWriteProtect(int, bool) override {}

    // Serielle Leitung XB5 am eigenen Hub der Einheit.
    k1520::serial::SerialHub* serialHub() override { return &einheit_.hub(); }
    std::vector<k1520::serial::SerialAnschluss*> serielleAnschluesse() override { return {&einheit_.anschluss()}; }
    void setDFUECallback(SerialCb) override {}
    void dfueSend(uint8_t) override {}

    uint8_t memReadDebug(uint16_t a) override;
    void    memWriteDebug(uint16_t a, uint8_t d) override;
    uint8_t ioReadDebug(uint8_t) override { return 0xFF; }
    std::string lastError() const override { return ""; }

    // ── Save-State „P8TM" v1 (Konfiguration + Einheit; nicht die Hub-Verbindung) ──
    static constexpr uint8_t STAND = 1;
    std::vector<uint8_t> stateBytes() const;
    bool restoreStateBytes(const std::vector<uint8_t>& b);
    bool saveState(const std::string& path) const;
    bool loadState(const std::string& path);
    const std::string& stateError() const { return state_error_; }

    k1520::p8000::P8000TerminalEinheit&       einheit()       { return einheit_; }
    const k1520::p8000::P8000TerminalEinheit& einheit() const { return einheit_; }
    const Config& config() const { return cfg_; }
    uint64_t totalCycles() const { return einheit_.takte(); }

    static constexpr uint32_t MATRIX_KODE = 0x04000000u;
    /// Takt von `run()` (interner Z8-Takt).
    static constexpr uint64_t einheitHz() { return k1520::p8000::P8000TerminalEinheit::Z8_HZ; }

protected:
    K1520Bus& systemBus() override { return bus_; }

private:
    void tastenAbgeben();

    const Config cfg_;
    K1520Bus bus_;   ///< nur für die Grundklasse (keine Karten)
    k1520::p8000::P8000TerminalEinheit einheit_;
    struct Taste { uint32_t code; bool ctrl; };
    std::mutex tasten_sperre_;
    std::deque<Taste> tasten_;
    std::atomic<bool> stop_{false};
    mutable uint64_t bild_gesehen_ = 0;
    std::string state_error_;
};

/**
 * @file p8000.h
 * @brief EAW P8000 — fünfte Maschine des Kerns: 8-Bit-Seite (U880-Rechnerkarte + Floppy +
 *        Kern-Terminal an tty1) und — mit `Config::karte16` — die 16-Bit-Karte samt Kopplung
 *        (AP P11) und — mit `Config::wdc` — der Winchester-Disk-Controller an der PIO2 der
 *        16-Bit-Karte samt bis zu drei Platten (AP P13d).
 *
 * Quelle: doc/design/25_p8000.md §10.1–§10.7 (Entwurf), Karten `P8000Karte8` (P5e) und
 * `P8000Floppy8` (P5f), Terminal `k1520::p8000::Terminal` (P6).
 *
 * **Zeitbasis** = Φ der 8-Bit-Karte (4 MHz, §10.2).  Laufschleife je Befehl:
 * `karte.schritt()` (DMA hält die CPU über /BUSRQ; EPROM-Wartetakte eingerechnet) →
 * `karte.takt(n)` (CTCs) → `floppy.takt(n)` (U8272, DRQ→RDY) → Terminal-Anschluss →
 * SerialHub.  Kein ZVE1/ZVE2-Mechanismus (§7 Risiko 5).  Mit 16-Bit-Karte wird sie nach jedem
 * 8-Bit-Befehl bis zur selben Maschinenzeit nachgezogen (`laufeBis`, Umrechnung f16/f8, im Reset
 * ohne Schritt) — feste Reihenfolge 8 → 16, deterministisch; Kopplungsflanken wirken sofort beim
 * schreibenden Zugriff (Leitungsmodell `P8000Kopplung`).
 *
 * **Serielle Kanäle (§10.6):** tty1 (SIO0-B) = Konsole, fest am Kern-Terminal
 * (`festeSchnittstellen()`); tty0/tty2/tty3 hängen am `SerialHub` — ohne Verbindung verfallen
 * ihre Zeichen in Zeichenzeit (sonst staut sich der Sender: MON8-Hardwaretest ERROR 21).
 *
 * **Ohne 16-Bit-Karte** sind die Kopplungseingänge offen (Pull-ups, §10.4 `karte16 = false`).
 * **Mit 16-Bit-Karte** hängen tty4–tty7 am `SerialHub` (nach tty0/2/3); die Konsole des
 * U8000-Monitors läuft über die Kopplung an tty1.  Reset: 8-PIO0-B7 (Pull-up) hält den U8001 im
 * Reset (K11); die NMI-Taste geht bei B7 = 0 an den U8001 (K12).
 *
 * **Save-State P8KS v2 (P7e/P11, Entwurf §10.2):** v1 + Abschnitte 16-Bit-Karte und Kopplung;
 * ein v1-Stand lädt in eine Maschine ohne 16-Bit-Karte. `saveState/loadState` (Datei) und
 * `stateBytes/restoreStateBytes` (Speicher).  Nicht: Medieninhalt, EPROM, Hub-Verbindungen und
 * der Zustand der Hub-Wandler (Zeichen in Flug an tty0/2/3).
 *
 * **WDC (P13d):** eigene Uhr in WDC-Takten, nach der 16-Bit-Karte bis zur selben Maschinenzeit
 * nachgezogen (Reihenfolge 8 → 16 → WDC, Entwurf §10.2); Leitungen über `P8000WdcAnschluss`.
 * Platten gehören der Maschine (`hdMount/hdCreate/hdUnmount`), der WDC kennt nur Zeiger.
 * Save-State P8KS v3 = v2 + Abschnitt WDC (samt Plattenmechanik, nicht dem Medieninhalt); v4 s. u.
 *
 * **Konsole tty1 (AP P20c, Entwurf 25 §11, Entwurf 28):** `Config::terminal` wählt das
 * vereinfachte Kern-Terminal (P6, Vorgabe — Variante „P8000") oder das **Originalterminal Typ 2 +
 * K7673.09** (Variante „P8000 + P8000 Terminal": `P8000TerminalEinheit` über `TerminalHwKopplung`
 * fest an tty1, Firmware P8T 5.0 auf dem Z8).  Beide hinter `TerminalGeraet`.  Das Original wird
 * beim Netz-Ein `terminal_vorlauf_ms` VOR dem Rechner eingeschaltet (es braucht ≈ 0,5 s bis zur
 * Bereitschaft, Merkposten 29/34); was es dabei sendet (00H der Firmware, Merkposten 30), geht ins
 * Leere wie am ausgeschalteten Rechner.  Die Reset-Taste des Rechners lässt das Terminal laufen.
 * Save-State **P8KS v4** = v3 + Terminalart im Fingerabdruck; ein v3-Stand lädt nur mit Kern-Terminal.
 *
 * Rahmenpuffer: mit Originalterminal das Pixelbild 640 × 312 (0 dunkel, 1 normal, 2 hell);
 * mit Kern-Terminal leer (die Oberfläche rastert aus `k1520_term_snapshot`).
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/cards/p8000/floppy8.h"
#include "core/cards/p8000/karte16.h"
#include "core/cards/p8000/karte8.h"
#include "core/cards/p8000/kopplung.h"
#include "core/cards/p8000/wdc.h"
#include "core/cards/p8000/wdc_anschluss.h"
#include "core/peripherals/winchester/platte.h"
#include "core/machines/machine.h"
#include "core/peripherals/p8000_terminal/terminal.h"
#include "core/peripherals/p8000_terminal/terminal_anschluss.h"
#include "core/peripherals/p8000_terminal/terminal_geraet.h"
#include "core/peripherals/p8000_terminal_hw/terminal_einheit.h"
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

        // ── 16-Bit-Teil (AP P11) ──
        using Index16 = P8000Karte16::Config::Index;
        using Mon16   = P8000Karte16::Config::Mon16;
        /// 16-Bit-Karte gesteckt.  Vorgabe vorerst AUS (Entwurf §10.4 sieht EIN vor; umgestellt
        /// wird mit der Oberfläche, damit die M1-Wächter den reinen 8-Bit-Teil behalten).
        bool     karte16 = false;
        Index16  index16 = Index16::I4;   ///< nur Paare (8: 1, 16: 1) oder (3, 4) — sonst Fehler
        Mon16    mon16   = Mon16::V3_1;
        std::vector<P8000Dram16::Karte> dram = {P8000Dram16::Karte{P8000Dram16::Karte::Typ::M1, 0}};
        bool     bruecken_4xr1_5xr1 = true;   ///< nur Index16 = I1 (Kopplung [KP1])
        uint32_t takt16_hz = 4'000'000;
        /// MMU-Steuerlogik [L1]: Adresse-High = NBR wählt die Stack-MMU (Plan) bzw. die Data-MMU
        /// (WEGA `nsseg`).  Siehe `P8000Karte16::Config::nbr_gleichheit_stack`.
        bool     nbr_gleichheit_stack = true;

        // ── WDC (AP P13d) — nur mit 16-Bit-Karte ──
        /// Firmware des WDC bzw. kein WDC.  Vorgabe vorerst AUS (Entwurf §10.4: 4.2), wie `karte16`.
        enum class Wdc { Aus, V4_2, V4_0_05, V3_4_05 };
        Wdc      wdc = Wdc::Aus;
        uint32_t taktwdc_hz = 4'000'000;   ///< 40 MHz ÷ 10; 41,4-MHz-Bestückung 4 140 000
        /// Abbild an Laufwerk 0 ("" = keins); Geometrie aus `platte_typ` (Name aus
        /// `winchester::typen()`, z. B. "K5504.50"), sonst aus PAR-Sektor bzw. Dateigröße.
        std::string platte;
        std::string platte_typ;
        /// [P5] der Platte: fehlt Z0/K0/S1 ein gültiger PAR-Sektor, liefert die Spursynthese einen
        /// erzeugten (WEGA-3.1-AVR-Abbild).  AUS = die Firmware sieht den Sektor, wie er ist.
        bool        platte_par_ergaenzen = true;

        // ── Konsole tty1 (AP P20c) ──
        enum class TerminalArt { Kern, Original };
        /// Vorgabe Kern-Terminal, bis P21/P22 umstellen (M1/M2-Wächter bleiben unverändert).
        TerminalArt terminal = TerminalArt::Kern;
        /// Nur `Original`: Bestückung des Terminals (Zeichensätze, Zeichentakt-Teiler, Tastatur).
        k1520::p8000::P8000TerminalEinheitConfig terminal_hw;
        /// Nur `Original`: so lange läuft das Terminal vor dem Rechner (Netz-Ein-Reihenfolge am
        /// Arbeitsplatz).  Ohne Vorlauf fielen die ersten Zeilen des Hardwaretests in die
        /// Einschaltphase der Firmware (Tastaturwarte, Merkposten 29); die Einschaltmeldung steht
        /// nach ≈ 0,84 s, Tasten nimmt das Terminal ab ≈ 1,0 s an (Merkposten 35).
        uint32_t terminal_vorlauf_ms = 1500;
    };

    /// Kanal der Konsole (U880-Monitor, UDOS) — Kern- oder Originalterminal.
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
    const uint8_t* framebuffer() const override;
    int  fbWidth()  const override;
    int  fbHeight() const override;
    bool fbDirty()  const override { return false; }
    void fbClearDirty() override {}
    void setConsoleMode(bool) override {}
    bool consolePoll(int&, int&, char&) override { return false; }
    /// Zeichen der Zelle (Spalte, Zeile) des Konsolen-Terminals (80 × 24); 0 außerhalb.
    uint8_t screenChar(int col, int row) const override;

    // ─── Tastatur des Terminals (fadensicher eingereiht) ────────────────────
    /// Druckbares ASCII (das Zeichen selbst), Steuerzeichen 01H–1FH, Qt-Return/Enter/Escape/
    /// Backspace/Tab/Delete, die Pfeiltasten (<BS> <VT> <FF> <LF>), Pos1 und Shift+Tab (BACKTAB)
    /// sowie `0x02000000 + TerminalTaste` für alle Tasten ohne Qt-Gegenstück (Funktionstasten,
    /// BREAK, MODE, VIDEO, ON/OFF, SI/SO) und 0x02000100/0x02000101 = Caps lock an/aus.
    /// `ctrl` macht aus einem Buchstaben das Steuerzeichen.
    void keyPress(uint32_t qt_keycode, bool shift, bool ctrl) override;
    /// Nur Originalterminal: `0x04000000 | Zeile << 8 | Spalte` (Matrix der K7673) wird
    /// gedrückt gehalten, bis derselbe Kode losgelassen wird; andere Kodes ohne Wirkung.
    void keyRelease(uint32_t code) override;
    void setKeyRepeatRealtime(bool an) override { if (hwterm_) hwterm_->einheit().setWiederholungEchtzeit(an); }
    /// Kodes für Matrixtasten der K7673 (Originalterminal).
    static constexpr uint32_t MATRIX_KODE = 0x04000000u;
    static constexpr uint32_t matrixKode(int zeile, int spalte) {
        return MATRIX_KODE | (uint32_t(zeile) << 8) | uint32_t(spalte);
    }
    /// LEDs der K7673 (Bit 0 ON/OFF, 1 CAPS LOCK, 2 MODE); Kern-Terminal 0.
    uint8_t keyboardLeds() const override;
    uint32_t bellCount() const override { return konsole_->klingel(); }

    int machineType() const override { return 4; }   // K1520_MACHINE_P8000 (Entwurf §10.9)
    /// Bit 0 RUN-LED (K14), Bit 1 UNIT16 = U8001 läuft (nicht im Reset), Bit 2 Plattenzugriff
    /// (WDC); ohne 16-Bit-Karte 0.
    uint8_t panelLamps() const override;
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
    std::vector<std::string> festeSchnittstellen() const override {
        return {hwterm_ ? "Terminal P8000 Typ 2 (tty1)" : "Terminal P8000 (tty1)"};
    }
    // (mit 16-Bit-Karte zusätzlich tty4–tty7 am Hub)
    /// Alter Unterbau (Rückruf/Einspeisen) gibt es am P8000 nicht — alle tty gehen über den
    /// Hub bzw. das Kern-Terminal.
    void setDFUECallback(SerialCb) override {}
    void dfueSend(uint8_t) override {}

    // ─── Winchester (WDC, AP P13d; Laufwerk 0–2) ─────────────────────────────
    /// Abbild roh/LBA anschließen (Geometrie: `platte_typ` bzw. PAR/Dateigröße).  Ein Winchester-
    /// laufwerk kennt keinen Schreibschutz — @p wp = true wird abgewiesen.
    bool hdMount(int unit, const std::string& path, bool wp = false);
    /// Neues Abbild des Typs @p typ anlegen und anschließen.  Ohne Suffix: E5 + gültiger PAR/BTT-
    /// Sektor.  Mit Suffix `:unformatiert`: wie ein neues Laufwerk — durchgehend E5, KEIN
    /// Parametersatz (der WDC meldet „Error in PAR&BTT", der Monitor bleibt bedienbar, `sa.format`
    /// legt ihn an); die Platte wird dann OHNE Parametersatz-Ergänzung angeschlossen.
    bool hdCreate(int unit, const std::string& path, const std::string& typ);
    bool hdUnmount(int unit);
private:
    bool hdMountMit(int unit, const std::string& path, bool par_ergaenzen);
public:
    bool hdFlush();
    std::string hdPath(int unit) const;
    /// Lampe: Laufwerk gewählt und Disk-Schnittstelle in den letzten 0,1 s aktiv.
    bool hdLed(int unit) const;
    const std::string& hdError() const { return hd_fehler_; }

    // ─── Diagnose (U880-Sicht: ADP/RFF wirksam, ohne Wartetakt-Zählung) ─────
    uint8_t memReadDebug(uint16_t addr) override;
    void    memWriteDebug(uint16_t addr, uint8_t data) override;
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return lw().lastError(); }

    // ─── Save-State P8KS v4 (Entwurf 25 §10.2; v1/v2/v3 laden ohne 16-Bit-Teil/WDC/Originalterminal) ───────────────────────────────
    /// Kennung „P8KS“, dann `P8000_STAND`, dann Abschnitte `[Kennung u8][Länge u32][Bytes]`
    /// (unbekannte Abschnitte werden übersprungen).  Die Disketten werden NICHT gesichert — der
    /// Aufrufer mountet vor dem Laden dieselben Abbilder (nur die Kopfposition wird gesetzt).
    static constexpr uint8_t P8000_STAND = 4;
    std::vector<uint8_t> stateBytes() const;
    /// Nur laden, wenn die Konfiguration (Index, ROM-Satz, Takt, Laufwerke, 16-Bit-Teil) übereinstimmt;
    /// scheitert das Laden mittendrin, wird der alte Zustand wiederhergestellt.
    bool restoreStateBytes(const std::vector<uint8_t>& b);
    bool saveState(const std::string& path) const;
    bool loadState(const std::string& path);
    /// Grund des letzten gescheiterten Ladens ("" = keiner).
    const std::string& stateError() const { return state_error_; }

    // ─── P8000-eigen (Tests, Werkzeuge) ──────────────────────────────────────
    P8000Karte8&  karte8()  { return karte_; }
    P8000Floppy8& floppy8() { return floppy_; }
    /// 16-Bit-Karte bzw. Kopplung; nullptr ohne `Config::karte16`.
    P8000Karte16*  karte16()  { return k16_.get(); }
    P8000Kopplung* kopplung() { return kopplung_.get(); }
    /// WDC bzw. Platte an Laufwerk @p unit; nullptr ohne `Config::wdc` bzw. ohne Abbild.
    P8000Wdc* wdc() { return wdc_.get(); }
    k1520::winchester::Platte* platte(int unit) {
        return (unit >= 0 && unit < P8000Wdc::LAUFWERKE) ? platten_[size_t(unit)].get() : nullptr;
    }
    K1520Bus&     bus()     { return bus_; }
    /// Das Kern-Terminal (P6).  Mit Originalterminal gibt es keins ⇒ std::logic_error;
    /// maschinenneutral ist `konsole()`.
    k1520::p8000::Terminal&       terminal();
    const k1520::p8000::Terminal& terminal() const;
    /// Die Konsole an tty1 hinter der gemeinsamen Schnittstelle (Kern- oder Originalterminal).
    k1520::p8000::TerminalGeraet&       konsole()       { return *konsole_; }
    const k1520::p8000::TerminalGeraet& konsole() const { return *konsole_; }
    bool hatOriginalTerminal() const { return hwterm_ != nullptr; }
    /// Das Originalterminal (Einheit samt Tastatur); nullptr mit Kern-Terminal.
    k1520::p8000::P8000TerminalEinheit*       originalTerminal()       { return hwterm_ ? &hwterm_->einheit() : nullptr; }
    const k1520::p8000::P8000TerminalEinheit* originalTerminal() const { return hwterm_ ? &hwterm_->einheit() : nullptr; }
    /// Zeile @p z (0–23) des Konsolen-Terminals als Text (80 Zeichen).
    std::string terminalZeile(int z) const { return konsole_->text(z); }
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
    void configAbschnitt(std::vector<uint8_t>& out, uint8_t stand) const;
    static P8000Karte16::Config karte16Config(const Config& c);
    uint64_t zeit16(uint64_t t8) const;
    uint64_t zeitWdc(uint64_t t8) const;

    const Config cfg_;
    K1520Bus     bus_;
    P8000Karte8  karte_;
    P8000Floppy8 floppy_;
    std::unique_ptr<P8000Karte16>  k16_;
    std::unique_ptr<P8000Kopplung> kopplung_;
    // Reihenfolge: Platten vor dem WDC (der hält nur Zeiger), Anschluss zuletzt (zuerst abgebaut).
    std::array<std::unique_ptr<k1520::winchester::Platte>, P8000Wdc::LAUFWERKE> platten_;
    std::unique_ptr<P8000Wdc>          wdc_;
    std::unique_ptr<P8000WdcAnschluss> wdc_an_;
    uint64_t    hd_zugriff_t_ = 0;   ///< Maschinenzeit des letzten Plattenzugriffs (Lampe)
    int         hd_zugriff_lw_ = -1;  ///< Laufwerk des letzten Zugriffs (−1 = noch keiner)
    std::string hd_fehler_;
    // Konsole tty1: genau eins von beiden (konsole_ besitzt es).
    std::unique_ptr<k1520::p8000::TerminalGeraet> konsole_;
    k1520::p8000::KernTerminalGeraet* kern_ = nullptr;
    k1520::p8000::HwTerminalGeraet*   hwterm_ = nullptr;
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

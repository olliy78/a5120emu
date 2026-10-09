/**
 * @file terminal_einheit.h
 * @brief Originalterminal als Einheit (Entwurf 28 §6/§7): Terminalrechner `P8000TerminalHw` +
 *        Tastatur `TastaturK7673` + Zeitführung, dazu
 *        - `TerminalUartAnschluss`: der `SerialAnschluss` des Terminals (XB5) für einen
 *          `SerialHub` — eigenständiger Betrieb „P8000 Terminal" (Telnet/RFC 2217/Datei);
 *        - `TerminalHwKopplung`: Gegenstelle zum `SerialAnschluss` eines Rechnerkanals
 *          (Variante „P8000 + Terminal", tty1 fest verdrahtet);
 *        - `HwTerminalGeraet`: beides hinter der gemeinsamen Schnittstelle `TerminalGeraet`.
 *
 * Uhr der Einheit = interner Z8-Takt (3 686 400 Hz).  Tastaturzeit (Quarz/2) und Rechnerzeit
 * werden mit Rest umgerechnet (driftfrei).  Tasten gehen als Matrixdruck an die K7673; damit
 * die Entprellung (41 Abtastungen ≈ 40 ms bei 8 MHz) sie sieht, wird jeder Druck
 * `tasteHalteMs` gehalten und danach `tastePauseMs` gewartet; Umschalttasten (SHIFT, CTRL)
 * werden eine Runde vorher gedrückt (die Tastatur meldet je Runde in Matrixreihenfolge —
 * CTRL liegt hinter den meisten Zeichentasten).
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "core/peripherals/p8000_terminal/terminal_geraet.h"
#include "core/peripherals/p8000_terminal_hw/tastatur_k7673.h"
#include "core/peripherals/p8000_terminal_hw/terminal_hw.h"
#include "core/serial/anschluss.h"

namespace k1520::serial { class SerialHub; }

namespace k1520::p8000 {

struct P8000TerminalEinheitConfig {
    P8000TerminalHwConfig hw;
    TastaturK7673Config tastatur;
    /// Die Tastatur läuft erst so spät nach dem Terminal an [U, Beschaffung B9]: die Firmware
    /// verwirft ein Byte vor ihrem TRES (Initialisierung), ein Byte zwischen TRES und dem ersten
    /// EI bliebe im Schieberegister stecken (IRQ-Register bis EI gesperrt), und ohne erstes
    /// Tastaturbyte wartet sie hinter der Einschaltmeldung (TOUT10, erreicht nach ≈ 155 ms).
    uint32_t tastaturVerzugMs = 300;
    uint32_t tasteHalteMs = 80;     ///< Haltezeit eines Tastendrucks (≥ 2 Abtastrunden)
    uint32_t tastePauseMs = 80;     ///< nach dem Loslassen
};

/// Matrixposition der K7673 (Spalte 0–7 = P0, 8–15 = P1).
struct MatrixTaste {
    int zeile = -1, spalte = -1;
    bool gueltig() const { return zeile >= 0; }
    bool operator==(const MatrixTaste& o) const { return zeile == o.zeile && spalte == o.spalte; }
};

class P8000TerminalEinheit;

/// SerialAnschluss des Terminals (XB5 „COMPUTER") — der Z8-UART aus Sicht eines Hubs.
class TerminalUartAnschluss : public serial::SerialAnschluss {
public:
    explicit TerminalUartAnschluss(P8000TerminalEinheit& e) : e_(e) {}
    const char* name() const override { return "Terminal (XB5)"; }
    const char* stecker() const override { return "XB5"; }
    bool v24() const override { return true; }
    serial::SerialFormat format() const override;
    bool senderHatZeichen() const override;
    uint8_t senderNimm() override;
    bool empfaengerFrei() const override;
    void empfange(uint8_t byte) override;
    bool dtr() const override { return true; }   ///< DTR fest aktiv bei eingeschaltetem Terminal
    bool breakGesendet() const override;

private:
    void breaksAbholen() const;
    P8000TerminalEinheit& e_;
    mutable uint64_t breakBis_ = 0;
};

class P8000TerminalEinheit {
public:
    using Config = P8000TerminalEinheitConfig;
    static constexpr uint64_t Z8_HZ = P8000TerminalHw::Z8_HZ;

    explicit P8000TerminalEinheit(const Config& cfg = Config());
    ~P8000TerminalEinheit();

    void einschalten();
    void laufeBis(uint64_t z8Takt);
    void laufe(uint64_t z8Takte) { laufeBis(takte() + z8Takte); }
    void laufeMs(uint64_t ms) { laufe(ms * Z8_HZ / 1000); }
    uint64_t takte() const { return hw_.takte(); }

    /// Tastenwiederholung nach Wirtsuhr statt Maschinenzeit (fadensicher; Oberfläche).
    /// Vorgabe aus: Tests laufen ungebremst und müssen wiederholbar sein.
    void setWiederholungEchtzeit(bool an) { echtzeit_.store(an, std::memory_order_relaxed); }
    bool wiederholungEchtzeit() const { return echtzeit_.load(std::memory_order_relaxed); }

    P8000TerminalHw& hw() { return hw_; }
    const P8000TerminalHw& hw() const { return hw_; }
    TastaturK7673& tastatur() { return kb_; }
    const TastaturK7673& tastatur() const { return kb_; }

    // ── Tasten (zeitlicher Ablauf über die Matrix) ───────────────────────────
    /// Matrixtaste drücken, halten, loslassen; @p mods vorher drücken, danach loslassen.
    void matrixTaste(MatrixTaste t, const std::vector<MatrixTaste>& mods = {});
    /// Zeichen tippen (SHIFT/CTRL aus den Tabellen der Firmware + Tastatur).  false = keine Taste.
    bool zeichenTaste(uint8_t c, bool ctrl = false);
    bool taste(TerminalTaste t);
    /// Text tippen; false, sobald ein Zeichen keine Taste hat (davor Getipptes bleibt).
    bool tippe(const std::string& s);
    bool tastenFertig() const { return aktionen_.empty(); }
    /// Matrixtaste sofort drücken/loslassen, ohne Zeitplan (Oberfläche: gehalten, solange der
    /// Anwender hält).  Ungültige Position ⇒ false.
    bool matrixDirekt(MatrixTaste t, bool an);
    /// CAPS LOCK (Rasttaste mit LED) auf @p an bringen — gedrückt wird nur, wenn die LED anders steht.
    void setzeCapsLock(bool an);
    /// Lauf bis alle Tasten getippt sind (höchstens @p maxMs), dann noch @p nachMs.
    void tastenAbwarten(uint64_t nachMs = 50, uint64_t maxMs = 60000);
    /// Lauf, bis die Firmware ruht (`P8000TerminalHw::ruht`, 3 Proben im Abstand 1 ms) und keine
    /// Tasten mehr anstehen; höchstens @p maxMs.  Rückgabe: ruht.
    bool ruheAbwarten(uint64_t maxMs = 5000);

    /// Taste(n) für ein Zeichen: Umschalter (SHIFT/CTRL) + Taste.  Aus NORMAL_Tab/SHIFT_Tab der
    /// Terminal-Firmware (056CH/05C5H) und der Codetabelle der Tastatur (02E3H).
    bool tastenFuerZeichen(uint8_t c, bool ctrl, std::vector<MatrixTaste>& mods, MatrixTaste& taste) const;
    /// Position der Taste mit dieser Make-Folge (z. B. {E0, 48}); ungültig, wenn keine.
    MatrixTaste positionFuer(const std::vector<uint8_t>& folge) const;
    MatrixTaste positionFuer(TerminalTaste t) const;

    // ── Leitung zum Rechner / Hub ────────────────────────────────────────────
    TerminalUartAnschluss& anschluss() { return uart_; }
    /// Eigener Hub mit genau einem Anschluss (Terminal XB5) — für den eigenständigen Betrieb.
    serial::SerialHub& hub();
    bool hatHub() const { return hub_ != nullptr; }

    // ── Save-State („P8TH" v1) ───────────────────────────────────────────────
    static constexpr uint8_t SAVE_VERSION = 1;
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    struct Aktion { uint64_t warteZ8; MatrixTaste t; bool an; };
    uint64_t zuKbd(uint64_t z8) const;
    uint64_t zuZ8(uint64_t kbd) const;
    void tastaturNachziehen(uint64_t z8Bis);
    void echtzeitTicks();
    void aktionenAusfuehren();

    Config cfg_;
    P8000TerminalHw hw_;
    TastaturK7673 kb_;
    TerminalUartAnschluss uart_;
    std::unique_ptr<serial::SerialHub> hub_;
    uint64_t hubNaechst_ = 0;
    std::deque<Aktion> aktionen_;
    uint64_t aktionZeit_ = 0;      ///< Z8-Zeit der nächsten Aktion
    uint64_t kbdZ_ = 1, kbdN_ = 1; ///< kbd = (z8 − kbdStart_) · Z / N (gekürzt)
    std::atomic<bool> echtzeit_{false};
    bool     ezBezug_ = false;     ///< Wirtsuhr-Bezug gültig (nur Lauffaden)
    uint64_t ezNs_ = 0;            ///< Wirtszeit des letzten gezählten Ticks
    uint64_t kbdStart_ = 0;        ///< Z8-Zeit, zu der die Tastatur eingeschaltet wird
};

/// Kopplung an einen Rechnerkanal (Variante „P8000 + Terminal"): Zeichen des Gast-SIO im
/// Zeichentakt des Gastformats auf die Leitung P30, Zeichen des Terminals (Rahmenende) in den
/// SIO-Empfänger, sobald er frei ist.  BREAK des Terminals ⇒ `breakEmpfang` für die Rahmenzeit.
class TerminalHwKopplung {
public:
    TerminalHwKopplung(serial::SerialAnschluss& karte, P8000TerminalEinheit& term,
                       uint64_t phiNenn = 4'000'000);
    void takt(uint64_t maschinenTakte);
    /// Zeitbezug neu setzen: die Einheit lief für sich (z. B. Vorlauf vor dem Rechner,
    /// Entwurf 28 §3) — ab jetzt zählen Maschinentakte wieder ab ihrer aktuellen Zeit.
    void synchronisiere() { rest_ = 0; ziel_ = term_.takte(); rxRest_ = 0; }
    bool baudAbweichend() const;
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    uint64_t zeichenTakte() const;
    serial::SerialAnschluss& karte_;
    P8000TerminalEinheit& term_;
    uint64_t phi_;
    uint64_t rest_ = 0;             ///< Rest der Umrechnung Maschine → Z8 (Nenner phi)
    uint64_t ziel_ = 0;             ///< Z8-Zeit, bis zu der die Einheit laufen soll
    uint64_t rxRest_ = 0;           ///< Maschinentakte bis zum nächsten Gastzeichen
    uint64_t breakBis_ = 0;         ///< Z8-Zeit, bis zu der BREAK anliegt
    bool breakAktiv_ = false;
};

/// Originalterminal hinter `TerminalGeraet` (besitzt Einheit und Kopplung).
class HwTerminalGeraet : public TerminalGeraet {
public:
    HwTerminalGeraet(serial::SerialAnschluss& karte, uint64_t phiNenn = 4'000'000,
                     const P8000TerminalEinheitConfig& cfg = P8000TerminalEinheitConfig())
        : einheit_(cfg), kopplung_(karte, einheit_, phiNenn) {}
    P8000TerminalEinheit& einheit() { return einheit_; }
    const P8000TerminalEinheit& einheit() const { return einheit_; }
    TerminalHwKopplung& kopplung() { return kopplung_; }
    void takt(uint64_t n) override { kopplung_.takt(n); }
    std::string text(int z) const override { return einheit_.hw().text(z); }
    TerminalZelle zelle(int z, int s) const override { return einheit_.hw().zelle(z, s); }
    uint8_t attribut(int z, int s) const override { return einheit_.hw().wirksamesAttribut(z, s); }
    int zeile() const override { return einheit_.hw().cursorZeile(); }
    int spalte() const override { return einheit_.hw().cursorSpalte(); }
    bool zeichenTaste(uint8_t c, bool ctrl) override { return einheit_.zeichenTaste(c, ctrl); }
    bool taste(TerminalTaste t) override { return einheit_.taste(t); }
    unsigned klingel() const override { return einheit_.hw().klingel(); }
    bool baudAbweichend() const override { return kopplung_.baudAbweichend(); }
    void serialize(std::vector<uint8_t>& out) const override { einheit_.serialize(out); kopplung_.serialize(out); }
    bool deserialize(const uint8_t*& p, const uint8_t* end) override {
        return einheit_.deserialize(p, end) && kopplung_.deserialize(p, end);
    }

private:
    P8000TerminalEinheit einheit_;
    TerminalHwKopplung kopplung_;
};

}  // namespace k1520::p8000

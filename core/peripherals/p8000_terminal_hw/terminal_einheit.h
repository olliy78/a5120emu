/**
 * @file terminal_einheit.h
 * @brief Originalterminal als Einheit (Entwurf 28 §6/§7): Terminalrechner `P8000TerminalHw` +
 *        Tastatur `TastaturK7673` + Zeitfuehrung, dazu
 *        - `TerminalUartAnschluss`: der `SerialAnschluss` des Terminals (XB5) fuer einen
 *          `SerialHub` — eigenstaendiger Betrieb „P8000 Terminal" (Telnet/RFC 2217/Datei);
 *        - `TerminalHwKopplung`: Gegenstelle zum `SerialAnschluss` eines Rechnerkanals
 *          (Variante „P8000 + Terminal", tty1 fest verdrahtet);
 *        - `HwTerminalGeraet`: beides hinter der gemeinsamen Schnittstelle `TerminalGeraet`.
 *
 * Uhr der Einheit = interner Z8-Takt (3 686 400 Hz).  Tastaturzeit (Quarz/2) und Rechnerzeit
 * werden mit Rest umgerechnet (driftfrei).  Tasten gehen als Matrixdruck an die K7673; damit
 * die Entprellung (41 Abtastungen ≈ 40 ms bei 8 MHz) sie sieht, wird jeder Druck
 * `tasteHalteMs` gehalten und danach `tastePauseMs` gewartet; Umschalttasten (SHIFT, CTRL)
 * werden eine Runde vorher gedrueckt (die Tastatur meldet je Runde in Matrixreihenfolge —
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
    /// Die Tastatur laeuft erst so spaet nach dem Terminal an [U, Beschaffung B9]: die Firmware
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
class TerminalHwKopplung;

/// Wer die Einheit nur nach Bedarf laufen laesst (die Kopplung an einen Rechnerkanal, s. dort),
/// holt sie vor jedem Zugriff von aussen auf den Stand nach, den sie Befehl fuer Befehl haette.
/// `aktiv` = es steht Zeit aus oder der Aufschub ist eingeschaltet; nur dann kostet der Zugriff etwas.
struct EinheitNachzieher {
    bool aktiv = false;
    /// Zeichen des Terminals, die erst nach einem Aufschub kamen (soll 0 bleiben, s. Kopplung).
    uint64_t verspaetet = 0;
    /// @p schreibend: der Zugriff kann Zustand aendern ⇒ der Aufschub endet auch (Neubeginn nach
    /// einer Ruheprobe); nur lesend bleibt er erhalten (die Oberflaeche holt 60 Bilder je Sekunde).
    virtual void nachziehen(bool schreibend) = 0;
    virtual void setzeAufschub(bool an) = 0;
    virtual ~EinheitNachzieher() = default;
};

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
    uint64_t takte() const { vorZugriff(false); return hw_.takte(); }

    /// Aufschub der Kopplung ein/aus (Vorgabe ein).  Aus ⇒ die Einheit laeuft nach jedem Rechner-
    /// befehl, wie ohne Aufschub; fuer Gegenproben (Ergebnis bitgleich).  Ohne Kopplung ohne Wirkung.
    void setzeAufschub(bool an) { if (nz_) nz_->setzeAufschub(an); }
    uint64_t aufschubVerspaetet() const { return nz_ ? nz_->verspaetet : 0; }

    /// Tastenwiederholung nach Wirtsuhr statt Maschinenzeit (fadensicher; Oberflaeche).
    /// Vorgabe aus: Tests laufen ungebremst und muessen wiederholbar sein.
    void setWiederholungEchtzeit(bool an) { echtzeit_.store(an, std::memory_order_relaxed); }
    bool wiederholungEchtzeit() const { return echtzeit_.load(std::memory_order_relaxed); }

    P8000TerminalHw& hw() { vorZugriff(true); return hw_; }
    const P8000TerminalHw& hw() const { vorZugriff(false); return hw_; }
    TastaturK7673& tastatur() { vorZugriff(true); return kb_; }
    const TastaturK7673& tastatur() const { vorZugriff(false); return kb_; }

    // ── Tasten (zeitlicher Ablauf ueber die Matrix) ───────────────────────────
    /// Matrixtaste druecken, halten, loslassen; @p mods vorher druecken, danach loslassen.
    void matrixTaste(MatrixTaste t, const std::vector<MatrixTaste>& mods = {});
    /// Zeichen tippen (SHIFT/CTRL aus den Tabellen der Firmware + Tastatur).  false = keine Taste.
    bool zeichenTaste(uint8_t c, bool ctrl = false);
    bool taste(TerminalTaste t);
    /// Text tippen; false, sobald ein Zeichen keine Taste hat (davor Getipptes bleibt).
    bool tippe(const std::string& s);
    bool tastenFertig() const { return aktionen_.empty(); }
    /// Matrixtaste sofort druecken/loslassen, ohne Zeitplan (Oberflaeche: gehalten, solange der
    /// Anwender haelt).  Ungueltige Position ⇒ false.
    bool matrixDirekt(MatrixTaste t, bool an);
    /// CAPS LOCK (Rasttaste mit LED) auf @p an bringen — gedrueckt wird nur, wenn die LED anders steht.
    void setzeCapsLock(bool an);
    /// Lauf bis alle Tasten getippt sind (hoechstens @p maxMs), dann noch @p nachMs.
    void tastenAbwarten(uint64_t nachMs = 50, uint64_t maxMs = 60000);
    /// Lauf, bis die Firmware ruht (`P8000TerminalHw::ruht`, 3 Proben im Abstand 1 ms) und keine
    /// Tasten mehr anstehen; hoechstens @p maxMs.  Rueckgabe: ruht.
    bool ruheAbwarten(uint64_t maxMs = 5000);

    /// Taste(n) fuer ein Zeichen: Umschalter (SHIFT/CTRL) + Taste.  Aus NORMAL_Tab/SHIFT_Tab der
    /// Terminal-Firmware (056CH/05C5H) und der Codetabelle der Tastatur (02E3H).
    bool tastenFuerZeichen(uint8_t c, bool ctrl, std::vector<MatrixTaste>& mods, MatrixTaste& taste) const;
    /// Position der Taste mit dieser Make-Folge (z. B. {E0, 48}); ungueltig, wenn keine.
    MatrixTaste positionFuer(const std::vector<uint8_t>& folge) const;
    MatrixTaste positionFuer(TerminalTaste t) const;

    // ── Leitung zum Rechner / Hub ────────────────────────────────────────────
    TerminalUartAnschluss& anschluss() { vorZugriff(true); return uart_; }
    /// Eigener Hub mit genau einem Anschluss (Terminal XB5) — fuer den eigenstaendigen Betrieb.
    serial::SerialHub& hub();
    bool hatHub() const { return hub_ != nullptr; }

    // ── Save-State („P8TH" v1) ───────────────────────────────────────────────
    static constexpr uint8_t SAVE_VERSION = 1;
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    friend class TerminalHwKopplung;
    /// Zugriff von aussen: erst den Stand nachholen, den die Kopplung schuldet (s. EinheitNachzieher).
    void vorZugriff(bool schreibend) const { if (nz_ && nz_->aktiv) nz_->nachziehen(schreibend); }
    /// Wie hw()/laufeBis()/takte() ohne den Nachzieher — fuer die Kopplung selbst.
    P8000TerminalHw& hwRoh() { return hw_; }
    void laufeBisRoh(uint64_t z8Takt);
    /// Die Einheit wird sich bis zum naechsten Eingang von aussen nicht ruehren: Firmware wartet in
    /// MAIN0 (`P8000TerminalHw::ruht`), keine Taste anhaengig oder in Wiederholung, keine Flanke
    /// und kein Zeichen im Tastaturpuffer, kein Hub.  Nur dann kann auch nichts zum Rechner gehen.
    bool ruhigRoh() const {
        return hw_.ruht() && aktionen_.empty() && !hub_ && !kb_.flankenAnstehend() &&
               kb_.pufferBelegung() == 0 && !kb_.wiederholungLaeuft();
    }
    struct Aktion { uint64_t warteZ8; MatrixTaste t; bool an; };
    uint64_t zuKbd(uint64_t z8) const;
    uint64_t zuZ8(uint64_t kbd) const;
    void tastaturNachziehen(uint64_t z8Bis);
    void echtzeitTicks();
    void aktionenAusfuehren();

    EinheitNachzieher* nz_ = nullptr;   ///< gesetzt von der Kopplung (Besitzer der Zeitfuehrung)
    Config cfg_;
    P8000TerminalHw hw_;
    TastaturK7673 kb_;
    TerminalUartAnschluss uart_;
    std::unique_ptr<serial::SerialHub> hub_;
    uint64_t hubNaechst_ = 0;
    std::deque<Aktion> aktionen_;
    uint64_t aktionZeit_ = 0;      ///< Z8-Zeit der naechsten Aktion
    uint64_t kbdZ_ = 1, kbdN_ = 1; ///< kbd = (z8 − kbdStart_) · Z / N (gekuerzt)
    std::atomic<bool> echtzeit_{false};
    bool     ezBezug_ = false;     ///< Wirtsuhr-Bezug gueltig (nur Lauffaden)
    uint64_t ezNs_ = 0;            ///< Wirtszeit des letzten gezaehlten Ticks
    uint64_t kbdStart_ = 0;        ///< Z8-Zeit, zu der die Tastatur eingeschaltet wird
};

/// Kopplung an einen Rechnerkanal (Variante „P8000 + Terminal"): Zeichen des Gast-SIO im
/// Zeichentakt des Gastformats auf die Leitung P30, Zeichen des Terminals (Rahmenende) in den
/// SIO-Empfaenger, sobald er frei ist.  BREAK des Terminals ⇒ `breakEmpfang` fuer die Rahmenzeit.
///
/// **Aufschub.**  `takt` kommt je Rechnerbefehl (wenige Takte) und liess jedes Mal den Z8 des
/// Terminals nachlaufen — ein gutes Fuenftel der Laufzeit, fast ganz Aufrufaufwand.  Die Zeit der
/// Einheit haengt aber nur an der Summe der Takte (Z8-Lauf ist von der Stueckelung unabhaengig: die
/// Ereignisse Bildzeile/Watchdog/Tastaturflanke sind Haltepunkte, und die Umrechnung mit Rest
/// summiert exakt), sichtbar wird sie nur an drei Stellen:
///  - Rechner → Terminal: ein Zeichen aus dem Sender wird zu einem festen Z8-Zeitpunkt auf die
///    Leitung gelegt.  `takt` fragt deshalb weiter je Aufruf nach dem Sender; steht ein Zeichen
///    an, holt es die Einheit nach und arbeitet wie bisher Scheibe fuer Scheibe;
///  - Terminal → Rechner: Zeichen und BREAK kommen aus dem Z8-Lauf.  Sie entstehen nur als Folge
///    eines Eingangs (Zeichen vom Rechner, Taste) — wartet die Firmware ruhig in ihrer Schleife
///    (`ruhigRoh`, dreimal im Abstand von ≈ 1 ms bestaetigt), kommt nichts, bis wieder ein Eingang
///    kommt.  Nur dann wird aufgeschoben, hoechstens `HORIZONT` Takte weit; danach folgt die naechste
///    Ruheprobe.  (Kaeme doch etwas, wird es verspaetet zugestellt und in `aufschubVerspaetet()` gezaehlt.)
///  - jeder Zugriff von aussen (Bild, Tasten, Save-State, Debugger) holt vorher nach
///    (`EinheitNachzieher`).
/// Beim Nachholen entsteht derselbe Stand wie Aufruf fuer Aufruf — bis auf das letzte Bit des
/// Save-States: die Einheit fuehrt die Tastatur nur dann nach, wenn der Z8 sein Ziel noch nicht
/// erreicht hatte; deshalb werden die letzten Aufrufe einzeln nachgespielt (s. `aufholen`).
class TerminalHwKopplung : private EinheitNachzieher {
public:
    TerminalHwKopplung(serial::SerialAnschluss& karte, P8000TerminalEinheit& term,
                       uint64_t phiNenn = 4'000'000);
    ~TerminalHwKopplung();
    void takt(uint64_t maschinenTakte);
    /// Zeitbezug neu setzen: die Einheit lief fuer sich (z. B. Vorlauf vor dem Rechner,
    /// Entwurf 28 §3) — ab jetzt zaehlen Maschinentakte wieder ab ihrer aktuellen Zeit.
    void synchronisiere();
    bool baudAbweichend() const;
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);
    static constexpr uint64_t HORIZONT = 4096;   ///< Rechnertakte (≈ 1 ms) je Aufschub

private:
    void nachziehen(bool schreibend) override;
    void setzeAufschub(bool an) override;
    void falten(uint64_t takte);    ///< Rechnertakte in ziel_/rest_ rechnen (Summe wie Scheibe fuer Scheibe)
    void aufholen();                ///< alles Offene nachholen, auf dem Stand „Aufruf fuer Aufruf"
    void aufschubBeenden();         ///< aufholen, wieder Scheibe fuer Scheibe
    bool ruhigJetzt();              ///< Ruheprobe (siehe Klassenkommentar)
    void probeMerken();             ///< Ruheprobe am Ende einer Scheibe-fuer-Scheibe-Runde
    uint64_t zeichenTakte(const serial::SerialFormat& f) const;
    serial::SerialAnschluss& karte_;
    P8000TerminalEinheit& term_;
    uint64_t phi_;
    uint64_t rest_ = 0;             ///< Rest der Umrechnung Maschine → Z8 (Nenner phi)
    uint64_t ziel_ = 0;             ///< Z8-Zeit, bis zu der die Einheit laufen soll
    uint64_t rxRest_ = 0;           ///< Maschinentakte bis zum naechsten Gastzeichen
    uint64_t breakBis_ = 0;         ///< Z8-Zeit, bis zu der BREAK anliegt
    bool breakAktiv_ = false;
    // Aufschub (nicht im Save-State: nur Tempo, kein Zustand)
    bool     aufschubErlaubt_ = true;
    bool     aufgeschoben_ = false;   ///< die Einheit laeuft nur nach Bedarf
    uint64_t offen_ = 0;              ///< Rechnertakte, die noch nicht in ziel_/rest_ stehen
    static constexpr unsigned RING = 64;                  ///< Laenge der Aufrufliste (Zweierpotenz)
    static constexpr uint64_t SCHWANZ_TAKTE = 160;        ///< so viele letzte Takte werden einzeln nachgespielt
    uint32_t ring_[RING] = {};        ///< Takte der letzten aufgeschobenen Aufrufe
    unsigned ringPos_ = 0, ringN_ = 0;
    unsigned ruheFolge_ = 0;          ///< aufeinanderfolgende ruhige Proben
    uint64_t naechsteProbe_ = 0;      ///< Z8-Zeit der naechsten Probe
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

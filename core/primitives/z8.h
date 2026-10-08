/**
 * @file z8.h
 * @brief Z8-Einchiprechner (Zilog Z8601/Z8611/Z8612/Z8681, UB 8820/8821/8840/8841 M) —
 *        generische Primitive: Kern, Registerdatei, Ports 0–3 mit Handshake, Zähler T0/T1,
 *        UART, sechs Interrupts, externer Bus (P0/P1 gemultiplext), Save-State, Sicht.
 *
 * Wie `Z80`/`Z8000`: kennt keine Karte und keine Maschine.  Grundlage: Zilog UM0016
 * „Z8 CPU User Manual" (Kapitel Ports, Counters and Timers, Interrupts, Serial I/O,
 * External Interface, Instruction Set).  Befunde/Annahmen mit Datenblattstelle und Test:
 * doc/p8000/z8_abdeckung.md.  Befehlstabelle: core/primitives/z8/z8_table.h.
 *
 * Takte: alles in INTERNEN Takten (XTAL/2).  step() führt einen Befehl bzw. eine
 * Interruptannahme aus und gibt die Takte zurück (Tabellenwert + Sprung + Wartetakte).
 * Die Zähler laufen mit internem Takt/4 und werden bei jedem Zugriff auf ein Peripherie-
 * register und am Befehlsende auf die aktuelle Zeit nachgezogen — ein Registerzugriff
 * wirkt zur Zeit „Befehlsende" (Annahme [Z8-T1]).
 *
 * Speicher (Annahme der 64-poligen Entwicklungsfassung, z. B. UB 8840 M ≙ Z8612):
 *  - Programmadressen < Config::programmbus gehen an den eigenen Programmbus
 *    (`programmLesen`, im Terminal das EPROM 2732); LDC-Schreiben dorthin hat keine Wirkung.
 *  - alles andere (Programm ≥ Grenze, Datenspeicher LDE/LDEI, externer Stapel) ist ein
 *    Buszyklus auf P0/P1 (`busLesen`/`busSchreiben`) mit /DM, R/W und der Adresse, wie sie
 *    an den Pins steht (Nibbles von P0, die keine Adressleitungen sind, liefern ihren Pegel).
 *
 * @license MIT
 */
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace k1520 { struct ZAr; }

/// Fassung des Bausteins (compile-time Konfiguration der Karte).
struct Z8Config {
    /// Programmadressen darunter liegen am eigenen Programmbus/ROM (0 = ROM-los, Z8681).
    uint16_t programmbus = 0x1000;
    /// Oberstes Allzweckregister (7FH = 124 Register 04–7F).  80H–EFH: nicht vorhanden (FFH).
    uint8_t  gprOben = 0x7F;
    /// P01M nach Reset (Zilog Table 12: 4DH; ROM-lose Fassungen siehe Datenblatt).
    uint8_t  p01mReset = 0x4D;
    /// HALT (7F)/STOP (6F) der Z86-Familie; am klassischen Z8/UB88xx unbelegt.
    bool     haltStop = false;
    /// Vor der Vektorlese ein verworfenes Befehlsholen am PC (UM0016 Figure 102).
    bool     scheinholenBeiInterrupt = true;

    static Z8Config ub8840() { return Z8Config{}; }                                   ///< 4 KB Programmbus
    static Z8Config ub8820() { Z8Config c; c.programmbus = 0x0800; return c; }        ///< 2 KB
    static Z8Config z8681()  { Z8Config c; c.programmbus = 0; c.p01mReset = 0xB6; return c; }  ///< ROM-los
};

/// Art eines Buszyklus.
enum class Z8Zugriff : uint8_t {
    Holen,        ///< Opcode
    Operand,      ///< weitere Befehlsbytes
    Scheinholen,  ///< verworfenes Holen vor der Interruptannahme
    Vektor,       ///< Interruptvektor (Programmspeicher)
    Konstante,    ///< LDC/LDCI (Programmspeicher, /DM inaktiv)
    Extern,       ///< LDE/LDEI (Datenspeicher, /DM aktiv)
    Stapel,       ///< externer Stapel (Datenspeicher)
};
inline const char* z8ZugriffName(Z8Zugriff a) {
    static const char* const k[] = {"HOLEN", "OPERAND", "SCHEIN", "VEKTOR", "LDC", "LDE", "STAPEL"};
    return k[static_cast<uint8_t>(a)];
}

/// Ein Zyklus am externen Bus (P0/P1, /AS, /DS, R/W, /DM).
struct Z8BusZyklus {
    uint16_t  adresse = 0;      ///< A15–A0, wie an den Pins
    bool      lesen = true;     ///< R/W
    bool      datenspeicher = false;  ///< logisch: Datenspeicher (LDE, externer Stapel)
    bool      dm = false;       ///< Pin P34 = /DM aktiv (nur wenn P3M ihn freigibt)
    Z8Zugriff art = Z8Zugriff::Holen;
    bool      hochohmig = false;  ///< P01M.4–3 = 11: Bus abgeschaltet (Zyklus findet nicht statt)
    bool      erweitert = false;  ///< P01M.5: erweitertes Timing (+1 Takt)
    uint64_t  takt = 0;           ///< Zeit (interne Takte) beim Befehlsende
};

/// Zustand eines Zählers (für Sicht und Save-State).
struct Z8Zaehler {
    uint8_t  anfang = 0;      ///< T-Register (geschriebener Anfangswert, 0 = 256)
    uint8_t  pre = 0;         ///< PRE-Register (Bit 7–2 Teiler, Bit 1 Takt intern (nur T1), Bit 0 Dauer)
    uint16_t zaehler = 0;     ///< Abwärtszähler 1..256 (0 = steht auf 00H)
    uint8_t  vorteiler = 0;   ///< 6-Bit-Abwärtszähler 1..64
    bool     getriggert = false;  ///< T1 in Trigger-/Retriggerbetrieb gestartet
    uint64_t naechster = 0;   ///< absolute Zeit des nächsten Zählertakts (intern)
    uint64_t abgelaufen = 0;  ///< Anzahl Endwerte (Sicht)
};

/// Beobachtung für den Debugger (eine Momentaufnahme, ändert nichts).
struct Z8Sicht {
    uint16_t pc, sp;
    uint8_t  flags, rp, imr, irq, ipr, p01m, p2m, p3m, tmr, sio;
    bool     irqFreigegeben, haltZustand, stopZustand, reset;
    Z8Zaehler t[2];
    bool     tout;
    uint8_t  portAus[4];      ///< Ausgangsregister
    uint8_t  portPins[4];     ///< Pegel an den Pins (wie ein Lesen sie sähe, ohne Wirkung)
    uint8_t  rxPuffer;
    uint16_t txSchieber;      ///< 0 = Sender ruht
    uint8_t  txTeiler, rxTeiler;
    bool     rxLaeuft;
    uint16_t rxSchieber;
    Z8BusZyklus letzterZyklus;
    uint8_t  letztesDatum;
    uint64_t takte;
    uint64_t interrupts[6];   ///< angenommene Interrupts je Stufe
};

class Z8 {
public:
    using Config = Z8Config;

    // FLAGS (FCH)
    static constexpr uint8_t F_C = 0x80, F_Z = 0x40, F_S = 0x20, F_V = 0x10, F_D = 0x08, F_H = 0x04,
                             F_F2 = 0x02, F_F1 = 0x01;

    explicit Z8(const Config& cfg = Config());

    // ── Rückrufe ─────────────────────────────────────────────────────────────
    /// Programmbus unterhalb Config::programmbus (ROM/EPROM).  Ohne Rückruf: FFH.
    std::function<uint8_t(uint16_t)> programmLesen;
    /// Externer Bus (P0/P1).  Ohne Rückruf: Lesen FFH, Schreiben ohne Wirkung.
    std::function<uint8_t(const Z8BusZyklus&)>       busLesen;
    std::function<void(const Z8BusZyklus&, uint8_t)> busSchreiben;
    /// Pegel an Port 0..2 beim Lesen (optional; sonst die mit setPort() gesetzten Pegel).
    /// Bekommt die Maske der Bits, die gerade Eingänge sind.
    std::function<uint8_t(int port, uint8_t eingaenge)> portEingang;
    /// Ausgangspegel eines Ports haben sich geändert (pegel = alle Pins, treibt = Ausgangsmaske).
    std::function<void(int port, uint8_t pegel, uint8_t treibt)> portAusgang;
    /// Pegel an P30 (serieller Eingang) zur Zeit @p takt — für eine Leitung, die der Rechner
    /// zeitgenau liefert (optional; sonst Pin P30 wie gesetzt).
    std::function<bool(uint64_t takt)> p30Quelle;
    /// UART: Zeichen fertig gesendet (nach dem 2. Stoppbit) bzw. empfangen (Stoppbitmitte).
    std::function<void(uint8_t)> uartGesendet;
    std::function<void(uint8_t)> uartEmpfangen;
    /// Interrupt angenommen (Stufe, gekellerter PC, neuer PC) — Debugger/Protokoll.
    std::function<void(int irq, uint16_t alterPc, uint16_t neuerPc)> onInterrupt;
    /// Unbelegter Opcode ausgeführt (als NOP, 6 Takte).
    std::function<void(uint16_t pc, uint8_t op)> onIllegal;

    /// Aus einem Busrückruf heraus: der laufende Zyklus bekommt n Wartetakte.
    void addWaitCycles(int n) { waits_ += n; }

    // ── Pins ─────────────────────────────────────────────────────────────────
    void setResetLine(bool aktiv);   ///< /RESET; beim Loslassen Start bei 000CH
    void reset();                    ///< Resetimpuls
    /// Eingangspegel eines ganzen Ports (P3: nur Bit 0–3 sind Eingänge; Flanken → IRQ).
    void setPort(int port, uint8_t pegel);
    void setPin(int port, int bit, bool pegel);
    uint8_t pinEingang(int port) const { return pinIn_[port & 3]; }
    /// Pegel an den Pins, wie die Außenwelt sie sieht (Ausgänge getrieben, sonst 1/extern).
    uint8_t portPegel(int port) const;
    uint8_t portTreibt(int port) const;   ///< Maske der Ausgänge

    // ── Ablauf ───────────────────────────────────────────────────────────────
    int step();
    /// Schritte bis die Zeit @p takt erreicht ist (Rückgabe: Anzahl Befehle).
    uint64_t laufeBis(uint64_t takt);
    uint64_t takte = 0;              ///< interne Takte seit dem Einschalten

    // ── Register ─────────────────────────────────────────────────────────────
    uint16_t pc = 0x000C;
    uint8_t  flags = 0;
    uint8_t  rp = 0;
    uint16_t sp = 0;                 ///< SPH:SPL
    uint8_t  imr = 0, irqReg = 0, ipr = 0;
    uint8_t  reg[256] = {};          ///< Allzweckregister 04..gprOben (übrige Zellen unbenutzt)

    /// Registerdatei so, wie ein Befehl sie liest/schreibt (mit Wirkung auf Peripherie).
    uint8_t regLesen(uint8_t adr);
    void    regSchreiben(uint8_t adr, uint8_t v);
    /// Ohne Wirkung (Debugger): Ports liefern die Pins, Nur-Schreib-Register ihren Inhalt.
    uint8_t regSicht(uint8_t adr) const;
    /// Arbeitsregister n → Adresse (RP.7–4 | n).
    uint8_t arbeitsreg(unsigned n) const { return uint8_t((rp & 0xF0) | (n & 15)); }

    bool irqFreigegeben() const { return irqEin_; }
    bool angehalten() const { return halt_; }
    bool inReset() const { return resetLine_; }
    uint16_t letzterPc() const { return lastPc_; }
    const Z8BusZyklus& letzterZyklus() const { return lastCycle_; }
    uint8_t letztesDatum() const { return lastData_; }
    Z8Sicht sicht() const;
    const Config& config() const { return cfg_; }
    uint64_t illegale() const { return illegal_; }
    uint64_t romSchreibversuche() const { return romWrites_; }
    uint8_t  p01m() const { return p01m_; }
    uint8_t  p2m() const { return p2m_; }
    uint8_t  p3m() const { return p3m_; }
    uint8_t  tmr() const { return tmr_; }
    const Z8Zaehler& zaehler(int n) const { return t_[n & 1]; }
    bool     tout() const { return tout_; }
    /// Interner Stapel (P01M.2 = 1)?
    bool stapelIntern() const { return (p01m_ & 0x04) != 0; }

    // ── Save-State ───────────────────────────────────────────────────────────
    static constexpr uint8_t SAVE_VERSION = 1;
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    Config cfg_;
    // Ablauf
    bool resetLine_ = false, resetPending_ = true;
    bool halt_ = false, stop_ = false;
    bool irqEin_ = false;              ///< IRQ-Register freigegeben (erstes EI, UM0016 S. 102)
    int  waits_ = 0;
    uint64_t befehlsEnde_ = 0;         ///< Zeit, zu der Registerzugriffe des Befehls wirken
    uint64_t periTakt_ = 0;            ///< bis hierhin sind Zähler/UART nachgezogen
    uint16_t lastPc_ = 0;
    uint64_t illegal_ = 0, romWrites_ = 0;
    uint64_t irqZahl_[6] = {};
    // Ports
    uint8_t out_[4] = {0, 0, 0, 0xF0};  ///< Ausgangsregister
    uint8_t pinIn_[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t hsLatch_[3] = {};          ///< Handshake-Eingangsregister
    bool    hsGelesen_[3] = {true, true, true};
    bool    hsVoll_[3] = {};           ///< Eingang: Datum gelatcht, RDY = 0
    bool    hsDav_[3] = {true, true, true};  ///< Ausgang: DAV-Pegel
    uint16_t lastAddr_ = 0;            ///< zuletzt am Bus (P0-Adressnibbles)
    uint8_t p01m_ = 0x4D, p2m_ = 0xFF, p3m_ = 0;
    uint8_t lastOut_[4] = {}, lastDrv_[4] = {};
    // Zähler / UART
    uint8_t tmr_ = 0;
    Z8Zaehler t_[2];
    bool    lief_[2] = {false, false};   ///< Zähler lief beim letzten laufPruefen()
    bool    tout_ = true;
    uint8_t txBits_ = 0;               ///< noch auszugebende Bits (0 = Sender ruht)
    uint8_t rxBuf_ = 0;
    uint16_t txSr_ = 0;                ///< Bit 0 = aktuell ausgegebenes Bit; 0 = ruht
    uint8_t txDiv_ = 0, rxDiv_ = 0;
    bool    rxStart_ = false;          ///< Startflanke erkannt, Takt läuft
    uint16_t rxSr_ = 0;
    uint8_t rxBits_ = 0;
    bool    rxAlt_ = true;             ///< letzter abgetasteter Pegel (Flankensuche)
    uint8_t txByte_ = 0;
    // Beobachtung
    Z8BusZyklus lastCycle_;
    uint8_t lastData_ = 0;

    void visit(k1520::ZAr& a);

    // Zeit
    void sync() { syncTo(befehlsEnde_); }
    void syncTo(uint64_t t);
    void zaehlerTakt(int n, uint64_t zeit);
    void endwert(int n, uint64_t zeit);
    void uartTakt(uint64_t zeit);
    bool t1Intern() const;          ///< T1 zählt den internen Takt (je nach PRE1/TMR/TIN)
    bool laeuft(int n) const;       ///< Zähler n bekommt interne Zählertakte
    /// Laufzustand neu bestimmen; ein neu laufender (oder mit @p ankerN neu geladener)
    /// Zähler zählt das erste Mal 4 Takte nach @p jetzt.
    void laufPruefen(uint64_t jetzt, bool anker0, bool anker1);
    void t1Flanke();                ///< fallende Flanke an P31 (TIN)

    // Ports
    uint8_t portLesen(int port);
    void    portSchreiben(int port, uint8_t v);
    uint8_t p3Ausgang() const;
    void    ausgaengeMelden();
    uint8_t pinsVon(int port, uint8_t maske) const;
    void    p3Flanke(int bit, bool neu);
    int     hsRichtung(int port) const;   ///< -1 kein Handshake, 0 Eingang, 1 Ausgang

    // IRQ
    void anfordern(int n) { if (irqEin_) irqReg = uint8_t(irqReg | (1u << n)); }
    int  hoechsteAnforderung() const;
    int  interruptAnnehmen(int n);

    // Bus / Speicher
    uint16_t pinAdresse(uint16_t a) const;
    uint8_t  extLesen(uint16_t a, bool daten, Z8Zugriff art);
    void     extSchreiben(uint16_t a, bool daten, Z8Zugriff art, uint8_t v);
    uint8_t  progLesen(uint16_t a, Z8Zugriff art);
    void     progSchreiben(uint16_t a, uint8_t v);
    uint8_t  holen();
    void     pushB(uint8_t v);
    uint8_t  popB();
    void     pushW(uint16_t v) { pushB(uint8_t(v)); pushB(uint8_t(v >> 8)); }
    uint16_t popW() { uint16_t h = popB(); return uint16_t(h << 8 | popB()); }

    // Register
    uint8_t  R(uint8_t a) const { return (a & 0xF0) == 0xE0 ? arbeitsreg(a) : a; }
    uint16_t paarLesen(uint8_t a) { return uint16_t(regLesen(a) << 8 | regLesen(uint8_t(a + 1))); }
    void     paarSchreiben(uint8_t a, uint16_t v) { regSchreiben(a, uint8_t(v >> 8)); regSchreiben(uint8_t(a + 1), uint8_t(v)); }

    // Ausführung
    int  ausfuehren(uint8_t op);
    bool bedingung(unsigned cc) const;
    void setF(uint8_t f, bool an) { flags = an ? uint8_t(flags | f) : uint8_t(flags & ~f); }
    uint8_t alu(int mn, uint8_t d, uint8_t s, bool& schreiben);
    uint8_t einop(int mn, uint8_t d);
    void tuReset();
};

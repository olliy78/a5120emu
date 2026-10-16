/**
 * @file dpl.h
 * @brief Displaylogik DPL des MC80.21/.22 (AP M11): CTC F0H–F3H, PIO F4H–F7H, Zeichentakt,
 *        Tastenmatrix 7 × 8 und das „oszillografische“ Bild (8 × 32 Zeichen + Analogkurven).
 *
 * Quellen: doc/mc80/hardware_2x.md §4–§6 (AP M10), doc/mc80/bsys_2x.prn 0000H–0340H (DPI, TSI,
 * ANL, TST).  Einen Schaltplan der DPL gibt es nicht (hardware_2x.md §11) — alles, was hier über
 * die Verdrahtung gesagt wird, ist aus dem Code des Betriebssystems erschlossen und mit [?]
 * markiert, wo es eine Annahme ist.
 *
 * **Bausteine** (Reihenfolge Daten/Steuer/Daten/Steuer, = Z80PIO-Nummerierung):
 *   - F0H–F3H CTC: K0 = Zeichentakt-Interrupt (DPI), K1 = Strahlrücklauf/Zeilenanfang (TI1),
 *     K2 = Zeichenzähler → Zeilenende (TSI, ZK 21H = 33), K3 = Taste BK (BREAK, Anwender).
 *   - F4H PIO A: Zeichencode Bit 0–5, **Bit 7 = Z-Signal „analog“**; außerhalb der Bildausgabe
 *     Zeilenwahl der Tastenmatrix (40H … 01H).
 *   - F6H PIO B: Ausgabe = Y-Ablenkung (8-Bit-DAC [?]: Zeilenlage = `CPL` der BWS-Zeilenadresse,
 *     Bit 7–5; Analogwert 00H…FFH), Eingabe (Betriebsart 3) = Spalten der Tastenmatrix.
 *
 * **Zeitmodell — Zeilenautomat (zeichenweise, [?] wo nicht belegt):**
 *   - **Text:** Zeichentakt alle **64,5 µs** [HB III-7] = 158,5 Systemtakte; jeder Takt zählt
 *     CTC K0 (DPI) und K2 (CLK/TRG0, CLK/TRG2 [? K2 könnte ebenso an ZC/TO0 hängen — gleiche
 *     Wirkung]).  ZC/TO2 (33 Zeichen, TSI) beendet die Zeile.
 *   - **Strahlrücklauf:** kein Zeichentakt; K1 zählt Φ/4 [?] („Auszählen der Strahlrücklaufzeit“,
 *     ZK D0H → 832 Takte = 0,34 ms).  ZC/TO1 beendet den Rücklauf („Zeilenanfang“, TI1): bei
 *     Z-Signal „analog“ (PIO A Bit 7) beginnt eine Analogbahn, sonst die nächste Textzeile.
 *     Belege für die Annahmen: (a) der TSI samt Tastaturabfrage (~350 Takte) liegt ganz im
 *     Rücklauf — liefe der Zeichentakt weiter, käme die DPI einen Schritt zu früh und jede Zeile
 *     erschiene um eine Spalte verschoben (so im Emulator beobachtet); (b) Zeile = 33 · 158,5 +
 *     832 = 6062 Takte → Bild 19,7 ms = **50,7 Hz**, das Listing nennt für die im TSI je Bild
 *     gezählten Zähler „FREQUENZ CA. 50 HERTZ“ [BS Bl. 4]; (c) ANL wartet mit HALT auf TI1
 *     „Zeilenanfang“ — genau das Ende des Rücklaufs, der mit dem TSI begann; (d) eine Analogzeile
 *     kostet so 0,34 + 2,17 ms ≈ 2,5 ms und die Last bei vier Analogzeilen ≈ 70 % — das Handbuch
 *     nennt „bis 2,7 ms“ und „bis 72 %“ [HB VI-8].  Beim Start schaltet BSYS K2 kurz als
 *     Zeitgeber (`07H/07H`, „Impuls f. Strahlrückl., Tastatur freigeben“) — dessen ZC/TO löst
 *     ebenso einen Rücklauf aus.
 *   - **Analogbahn:** dauert T_Z = 33 Zeichentakte = 2,13 ms [?], danach Rücklauf.  Werte, die
 *     das OTIR später liefert, fallen rechts aus dem Bild (OTIR gibt 254 Werte in 2,17 ms aus →
 *     ≈ 249 sichtbar; Handbuch: 253 Abszissenwerte [?]).
 *   - Notbremse: steht der Rücklauf länger als vier Zeilen (K1 nicht programmiert), beginnt die
 *     nächste Textzeile trotzdem [?].
 *
 * **Bildmodell:** Die Karte sieht NICHT in den BWS — sie zeichnet, was an den PIO-Ausgängen
 * anliegt.  Je Zeichentakt (vor der DPI-Antwort) wird PIO A in die Zelle `Schritt` der laufenden
 * Bahn übernommen; ein Schreiben auf PIO B (Textbetrieb) beginnt eine neue Bahn auf der Zeile
 * `(~B >> 5) & 7` mit Schritt 0.  Schritt 0 ist das Leerzeichen „bei Strahlvorlauf“ (TSI), die
 * Schritte 1…32 sind die Spalten 0…31; der 33. Takt (mit dem TSI) liegt hinter dem Zeilenende
 * und wird verworfen.  Eine Analogbahn ersetzt die
 * Zeile, die auf die zuletzt gezeichnete Textzeile folgt (ANL: Zeilen 2/4/6/8).  Je der acht
 * Zeilenplätze gilt die zuletzt gezeichnete Bahn (Text oder Kurve) — so bleibt das Bild wie auf
 * dem Leuchtschirm stehen, solange das Programm es auffrischt.
 *
 * **Framebuffer** 528 × 256, ein Byte je Bildpunkt (00H/FFH), Zeile 0 oben: 33 Schritte à 16
 * Bildpunkte, Zeichenzeile k = Bildzeilen 32k…32k+31 (Y-DAC: oben = FFH), Zeichen 5 × 7 doppelt
 * groß (10 × 14) ab (3, 9) in der Zelle; Analogwert y → Bildzeile 255 − y, Abszisse
 * (t − t₀)/T_Z · 528, gezeichnet als Treppenzug (der PIO-Ausgang hält den Wert bis zum nächsten).
 *
 * **Zeichengenerator U402 [?]:** kein Abzug (hardware_2x.md §5.2).  Ersatz: der 5×7-Satz des
 * 2513 (64 Zeichen ASCII 20H–5FH, 6-Bit-Adresse wie der BWS-Code: 00–1F = @…_, 20–3F = Leer…?);
 * der U402 ist nach Code und Umfang (63/64 Zeichen, 6 Bit, 5 × 7) dessen Gegenstück.  Laut
 * Tastaturfoto [HB Bild 2.3] zeigt der U402 für `@ & ^ \` andere Bilder — hier nicht nachgebildet.
 *
 * **Tastatur:** Matrix 7 Zeilen × 8 Spalten (hardware_2x.md §6), gedrückt = 1 [ROM: TST prüft
 * `AND 3FH` ≠ 0].  Tastennummer = 8 · Zeilenindex + Bit, Zeilenindex 0 = PIO-A-Wert 40H … 6 = 01H;
 * Nummer 36H = CTR, 37H = UC.  PIO B (Betriebsart 3) liest das ODER der gewählten Zeilen.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include <array>
#include <cstdint>
#include <vector>

class Mc80Dpl : public BusDevice, public InterruptSlave {
public:
    static constexpr int ZEILEN   = 8;
    static constexpr int SPALTEN  = 32;
    static constexpr int SCHRITTE = 33;          ///< Zeichentakte je Bahn (Leerzeichen + 32)
    static constexpr int ZELLE_B  = 16;          ///< Bildpunkte je Schritt
    static constexpr int BAND_H   = 32;          ///< Bildzeilen je Zeichenzeile
    static constexpr int BREITE   = SCHRITTE * ZELLE_B;   // 528
    static constexpr int HOEHE    = 256;
    static constexpr uint8_t NIE  = 0xFF;        ///< Zelle noch nie gezeichnet

    struct Config {
        uint8_t  io_base      = 0xF0;            ///< F0H–F7H
        uint32_t takt_hz      = 2'457'600;       ///< Systemtakt [HB I-9]
        uint32_t zeichentakt_ns = 64'500;        ///< 64,5 µs [HB III-7]
        uint32_t k1_teiler    = 4;               ///< CLK/TRG1 = Φ/4 [?]
    };

    /// Ein Punkt einer Analogkurve (Bildpunkt-Abszisse 0…BREITE−1, Ordinate = PIO-B-Wert).
    struct Punkt { int x; uint8_t y; };

    Mc80Dpl();
    explicit Mc80Dpl(const Config& cfg);

    void attachToBus(K1520Bus& bus);
    /// Netz ein: Bild leer (alle Zellen @ref NIE), keine Taste, dann reset().
    void powerOn();
    /// /RESET: CTC und PIO zurück, Zeichentakt-Phase 0; das Bild bleibt stehen.
    void reset();
    /// @p ticks Systemtakte weiter.  @return true, wenn sich der Interruptzustand ändern konnte.
    bool clockTick(int ticks);

    // ─── BusDevice ───────────────────────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "MC80.2x DPL"; }

    // ─── InterruptSlave: IEI → CTC → PIO → IEO [? Reihenfolge auf der Karte] ──
    void        setIEI(bool iei) override;
    bool        getIEO() const override { return pio_.getIEO(); }
    bool        hasInterrupt() const override { return ctc_.hasInterrupt() || pio_.hasInterrupt(); }
    uint8_t     getVector() const override;
    void        onRETI() override;
    /**
     * @brief RETI nach dem Datenblatt: ist ein Kanal/Port der Karte in Bedienung, nimmt die Karte
     *        das RETI an (höchster Kanal in Bedienung, auch wenn ein höherer gerade anfordert)
     *        und meldet true.  Die Maschine gibt das RETI so nur dem obersten Gerät in Bedienung
     *        (Z80CTC::setRetiNachIus, AP M11).
     */
    bool        retiAnnehmen();
    const char* intDeviceName() const override;

    // ─── Tastatur ────────────────────────────────────────────────────────────
    /// Taste Nummer @p nr (0…37H, s. Kopf) drücken/loslassen.
    void setTaste(int nr, bool gedrueckt);
    bool taste(int nr) const;
    void alleTastenLos();
    /// Taste BK: ein Impuls an CLK/TRG3 (BREAK-Interrupt, wenn der Anwender K3 eingerichtet hat).
    void breakTaste();

    // ─── Bild ────────────────────────────────────────────────────────────────
    /// Framebuffer (neu zusammengesetzt, wenn sich das Bild geändert hat).
    const uint8_t* framebuffer() const;
    bool     dirty() const { return dirty_; }
    void     clearDirty() { dirty_ = false; }
    uint64_t revision() const { return revision_; }
    /// 6-Bit-Code der Zelle (Zeile 0…7, Spalte 0…31) aus der letzten Textbahn; @ref NIE sonst.
    uint8_t  zelle(int zeile, int spalte) const;
    /// Dasselbe als ASCII (00–1F → 40–5F); ' ' für NIE oder eine Analogzeile.
    char     zeichen(int zeile, int spalte) const;
    /// Zuletzt auf Zeilenplatz @p zeile gezeichnete Bahn war eine Analogkurve?
    bool     analogZeile(int zeile) const { return art_.at(static_cast<size_t>(zeile)) == Art::Analog; }
    const std::vector<Punkt>& kurve(int zeile) const { return kurve_.at(static_cast<size_t>(zeile)); }
    /// Zahl der Zeichentakte seit Netz ein (Diagnose, Tests).
    uint64_t zeichentakte() const { return takte_zeichen_; }
    /// Zahl der fertig gezeichneten Bahnen (Text + Analog).
    uint64_t bahnen() const { return bahnen_; }
    /// 5×7-Bild des Codes @p code (Spalte 0…4, Bit 0 = oberste Reihe) — Ersatz-U402 [?].
    static const uint8_t* glyphe(uint8_t code);
    /// Textrückgewinnung aus dem Framebuffer per Mustervergleich: Zeichen der Zelle oder 0, wenn
    /// das Bild keiner Glyphe gleicht (Analogkurve darüber, nie gezeichnet …).
    char     zeichenAusBild(int zeile, int spalte) const;

    Z80CTC& ctc() { return ctc_; }
    Z80PIO& pio() { return pio_; }
    uint8_t ausgabeA() const { return a_; }
    uint8_t ausgabeB() const { return b_; }

private:
    enum class Art : uint8_t { Leer, Text, Analog };

    void updateInternalChain();
    void zeichentakt();
    void tastaturPins();
    void textBahnBeginnen(uint8_t b);
    void ruecklauf();
    void zctoK1();
    void kurveAblegen();
    uint64_t bahnTakte() const { return (static_cast<uint64_t>(SCHRITTE) * periode_q16_) >> 16; }
    void puls(int kanal);
    void geaendert() { ++revision_; dirty_ = true; }
    void zeichne() const;

    Config cfg_;
    Z80CTC ctc_{"DPL-CTC"};
    Z80PIO pio_{"DPL-PIO"};
    bool   iei_in_ = true;

    // Zeichentakt (Q16-Systemtakte)
    uint64_t periode_q16_ = 0;
    uint64_t phase_q16_ = 0;
    uint64_t zeit_ = 0;               ///< Systemtakte seit Netz ein
    uint64_t takte_zeichen_ = 0;

    uint8_t a_ = 0, b_ = 0;           ///< zuletzt geschriebene PIO-Ausgabewerte
    std::array<uint8_t, 7> matrix_{}; ///< gedrückte Tasten je Zeilenindex

    // Zeilenautomat und Bahnen
    enum class Zustand : uint8_t { Text, Ruecklauf, Analog };
    Zustand zustand_ = Zustand::Ruecklauf;
    uint32_t rl_acc_ = 0;             ///< Takte seit dem letzten Φ/4-Impuls an K1
    uint64_t rl_seit_ = 0;            ///< Beginn des Rücklaufs (Notbremse)
    int zeile_ = 0;                   ///< Zeile der laufenden Textbahn
    int schritt_ = SCHRITTE;          ///< Schritt in der Textbahn (≥ SCHRITTE = hinter dem Ende)
    int  analog_zeile_ = 1;
    uint64_t kurve_t0_ = 0;
    uint8_t  y_akt_ = 0;
    std::vector<Punkt> neue_kurve_;
    uint64_t bahnen_ = 0;
    std::array<Art, ZEILEN> art_{};
    std::array<std::array<uint8_t, SPALTEN>, ZEILEN> zellen_{};
    std::array<std::vector<Punkt>, ZEILEN> kurve_{};

    uint64_t revision_ = 0;
    bool     dirty_ = true;
    mutable uint64_t fb_revision_ = ~0ull;
    mutable std::vector<uint8_t> fb_;
};

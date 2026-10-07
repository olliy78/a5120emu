/**
 * @file karte16.h
 * @brief 16-Bit-Rechnerkarte des P8000 (P 8000/16.1 bzw. 16.4): U8001 (Z8001-Betrieb), MMU-
 *        Steuerlogik (drei UB8010), On-Board-EPROM 16 KB + SRAM 2 KB, Hauptspeicher (DRAM-Karten),
 *        CTC0/1, SIO0/1 (tty4–tty7), PIO0–2, E/A-Dekoder FF81–FFB7, NMI-Identifier, Paritäts-FF,
 *        Single-Step-Zähler, Reset-Bildung MRESET−/PIORESET−.
 *
 * Quelle: doc/p8000/schaltplan_16bit.md (Rang 1, Index 4 Bl. 1–14), doc/p8000/hw_16bit.md,
 * doc/design/25_p8000.md §10.1–§10.6, AP P10b.  Ohne WDC (P13).  PIO0/1 und PIO2 sind nur
 * Leitungsanschlüsse (`setzePinTreiber`, `pio0()`…, `setzePioHaken`); die Kopplungsgegenseite
 * legt `P8000Kopplung` (P11) an.
 *
 * @code
 *   Speicher (über P8000MmuLogik16): Segment 0, 0000–7FFF bei SCR.0 = 0 On-Board (+1 Wartetakt):
 *     0000–3FFF EPROM, 4000–5FFF SRAM 2 KB (4 Spiegel), 6000–7FFF leer; sonst Hauptspeicher
 *     (MMU aus: SN·64K + Offset, MMU ein: UB8010).  Reset-Vektor: FCW C000 aus 0002.
 *   E/A (Status 0010, A15–A7 = 1, A0 = 1; LAD3–5 = Baustein, A1/A2 an den Baustein, KEINE Spiegel):
 *     FF81–FF87 SIO0   FF89–FF8F SIO1   FF91–FF97 PIO0   FF99–FF9F PIO1   FFA1–FFA7 PIO2
 *     FFA9–FFAF CTC0   FFB1–FFB7 CTC1   FFB9 LEDAUS · FFC1–FFF9 Register der MMU-Steuerlogik
 *     SIO/PIO: A1 = Kanal/Port B, A2 = Steuerwort;  CTC: A1/A2 = Kanal.  Gerade Adressen: offen.
 *   Spezial-E/A (Status 0011): /CS der UB8010 = AD1/AD2/AD3 (Code/Data/Stack).
 *   Interruptkette (Bl. 13): CTC0 – CTC1 – SIO0 – SIO1 – PIO0 – PIO1 – PIO2, alle an VI−.
 *   Vektor-Quittung (Status 0111): AD0–7 = Vektor, AD8–15 offen.  RETI: ED, 4D an FFE1 — und
 *   der SIO-Befehl 38H an Kanal A (MON16 `PTY_INT`).
 *   NMI-Identifier (Status 0101): Bit 0 MANUALNMI, 1 POWER FAIL (nie), 2 Paritäts-FF, 3 MSDOSNMI
 *   (Index 4, ohne Karte 0; Index 1 Masse), Bit 4–15 nicht getrieben (offener Bus).
 * @endcode
 *
 * **Takt (Bl. 6, 10):** U8001 und Peripherie 4 MHz.  BUS BAUD CLK = 9,832 MHz ÷ 8 an CLK/TRG von
 * CTC0 K0–K2 und CTC1 K0, K2 [Annahme: gleicher Takt wie X2:B10, Frage 4]; ZC/TO ÷ 2 → SIO:
 * tty4 = CTC0 K0 → SIO0-A, tty5 = CTC0 K1 → SIO0-B, tty6 = CTC0 K2 → SIO1-A, tty7 = CTC1 K0 → SIO1-B.
 * CTC1 K1 = TAPEQUIT− (fest H), CTC1 ZC/TO2 → CLK/TRG3 (Systemuhr), CTC0 CLK/TRG3 = ein Impuls je
 * Speicherzyklus mit Status 1001 (Single-Step, MON16 `p.brk.s`).
 *
 * **Reset (Bl. 6):** MRESET− = PRES− ∨ RESET (X3:B1, high-aktiv, Pull-up ⇒ Vorgabe aktiv) ∨
 * SOFTRESET (FFE9, Impuls).  MRESET setzt CPU, MMU-Logik (SCR = 0), CTCs, SIOs, DRAM-LED, Paritäts-FF;
 * die PIOs nur bei Index 1 — bei Index 4 setzt sie PIORESET− = PRES− ∧ TRESET− (X3:A1) zurück.
 *
 * **Zeitführung (Entwurf §10.2):** eigene Uhr `zeit()` in Takten des U8001; `schritt()` = ein
 * Befehl samt Peripherie, `laufeBis(t)` zieht die Karte bis t nach (im Reset ohne Schritt).
 *
 * Benannte Annahmen (zusätzlich zu [L1]–[L12] der MMU-Logik und [D1]–[D5] des DRAM):
 *  [K1] Die Kopplungs-/WDC-Eingänge sind ohne Gegenseite offen: PIO0/PIO1-Eingänge lesen über
 *       Pull-up 1 (K15), /ASTB H.  PIO2-B0/B1/B2/B7 treibt der invertierende DL540 (1D21) —
 *       ohne WDC sind seine Eingänge offen (TTL = H) ⇒ die Pins liegen auf 0 (MON16 `p.disk.s`
 *       meldet dann sofort Fehler C1 statt 16 × 65 535 Schleifen zu warten).
 *  [K2] Die NMI-Taste ist ein Impuls von `Config::nmi_impuls_takte` (6 µs = 24 Takte).  Bit 0 des
 *       Identifiers ist der PEGEL zur Quittung — ist der Impuls vorbei, steht dort 0; MON16
 *       `NMI_INT` behandelt „weder Bit 0 noch Bit 2" wie manuell, `AUTOBOOT` liest ihn nicht.
 *  [K3] /NMI des U8001 = MANUALNMI ∨ Paritäts-FF (Gatterfolge Bl. 2 F3–F5 nicht ganz verfolgbar);
 *       die CPU sieht die Flanke inaktiv → aktiv.
 *  [K4] Das Paritäts-FF übernimmt PE− am Ende eines Hauptspeicher-LESEzyklus mit SYSDS (also nicht
 *       bei unterdrücktem Zyklus), solange SCR.3 = 1; SCR.3 = 0 hält es gelöscht (/R = CLRPARITY−).
 *  [K5] /CTS tty4 = RTS (Lokalschleife 2D4/2D6), tty5–7 fest aktiv; /DCD vom Anschluss; SIO-/W/RDY
 *       unbeschaltet (Bl. 11).
 *  [K6] Refresh- und interne Zyklen (Status 0000/0001), NVI-Quittung, EPU-Transfer: offener Bus.
 *  [K7] Ein durch /SUP unterdrückter Hauptspeicher-LESEzyklus liefert den Offset der Adressphase
 *       (AD-Leitungen halten ihn, wie am A5120.16 gemessen — em.cpp `read16Io`), nicht FFFFH.
 *       Beleg MON16 3.1 `CODE_TRAP` (Testschritt 97): `add r15,#8 (im Original %0009)` — das
 *       unterdrückte Holen bei N:9000 darf R15 nicht ändern; mit FFFFH (= DJNZ R15,…) liefe der
 *       Stapel je Segment um 2 Bytes ab und der Monitor stürzte nach Testschritt 97 ab.  Mit 9000H
 *       (CPL RR0,RR0) bleibt er stehen.  Abschaltbar über `Config::unterdrueckt_liest_offset`.
 *
 * Save-State: serialize()/deserialize() (Version 1): U8001 samt Ablaufzustand, MMU-Logik (stellt
 * den /SEGT-Pegel der CPU wieder her — `Z8kRunState` trägt ihn nicht), DRAM, SRAM, CTC0/1 samt
 * Bruchtakt-Phase, SIO0/1, PIO0–2 samt Handshake, Leitungen, Uhr.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/cards/p8000/dram16.h"
#include "core/cards/p8000/mmu_logik16.h"
#include "core/primitives/z8000.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include "core/primitives/z80_sio.h"
#include "core/serial/anschluss.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class P8000Karte16 {
public:
    struct Config {
        enum class Index { I1, I4 };                ///< Leiterplattenindex
        enum class Mon16 { V3_0, V3_1, V3_3 };      ///< EPROM-Satz
        Index    index = Index::I4;
        Mon16    mon16 = Mon16::V3_1;
        P8000Dram16::Config dram;                   ///< Vorgabe: eine 1-MB-Karte, Modul 0
        uint32_t takt_hz       = 4'000'000;         ///< DL8127, 16 MHz ÷ 4
        uint32_t baudquarz_hz  = 9'832'000;         ///< BUS BAUD CLK = ÷ 8
        uint8_t  sram_fuellwert = 0x00;
        int      nmi_impuls_takte = 24;             ///< [K2]
        bool     unterdrueckt_liest_offset = true;  ///< [K7]
        // MMU-Steuerlogik (mmu_logik16.h [L1], [L5], [L6])
        bool     nbr_gleichheit_stack = true;
        bool     cs_beim_reset = true;
        bool     if1l_auch_status1100 = false;
        uint8_t  nbr_start = 0x00;
        uint8_t  leer8 = 0xFF;
        uint16_t leer16 = 0xFFFF;
    };

    static constexpr int TTY_ANZAHL = 4;   ///< tty4–tty7

    P8000Karte16() : P8000Karte16(Config{}) {}
    explicit P8000Karte16(const Config& cfg);
    ~P8000Karte16();

    // ─── serielle Kanäle tty4–tty7 (SIO0-A/B, SIO1-A/B) — Index 0…3 ──────────
    k1520::serial::SerialAnschluss& anschluss(int i);
    bool nimmSeriellGeaendert() { const bool g = seriell_geaendert_; seriell_geaendert_ = false; return g; }

    // ─── Lebenslauf und Leitungen ────────────────────────────────────────────
    void powerOn();                       ///< PRES−: MRESET− und PIORESET−; danach hält RESET
    /// X3:B1 RESET (high-aktiv; offen = Pull-up = aktiv).  Wegnehmen startet den U8001.
    void setResetEingang(bool aktiv);
    bool resetEingang() const { return reset_eingang_; }
    /// X3:A1 TRESET− (Index 4: PIORESET−); aktiv setzt die PIOs zurück.
    void setTresetEingang(bool aktiv);
    /// MANUALNMI als Pegel (Kopplung K12, P11).
    void setManualNmi(bool aktiv);
    /// NMI-Taste: MANUALNMI für `nmi_impuls_takte` Takte [K2].
    void nmiTaste();
    bool inReset() const { return mreset_; }
    bool runLed() const { return mmu_.runLed(); }
    bool paritaetsFF() const { return paritaet_ff_; }

    // ─── Zeit ────────────────────────────────────────────────────────────────
    /// Ein Befehl des U8001 (bzw. ein Wiederholungsdurchlauf, ein Ausnahmeeintritt) samt
    /// Peripherietakt; im Reset 1 Takt.  Rückgabe: Takte.
    int  schritt();
    /// Zieht die Karte bis @p ziel (Takte des U8001) nach; im Reset ohne Schritt.
    void laufeBis(uint64_t ziel);
    uint64_t zeit() const { return zeit_; }

    // ─── Bausteine ───────────────────────────────────────────────────────────
    Z8000&            cpu()   { return cpu_; }
    P8000MmuLogik16&  mmu()   { return mmu_; }
    P8000Dram16&      dram()  { return dram_; }
    Z80CTC&           ctc0()  { return ctc0_; }
    Z80CTC&           ctc1()  { return ctc1_; }
    Z80SIO&           sio0()  { return sio0_; }
    Z80SIO&           sio1()  { return sio1_; }
    Z80PIO&           pio0()  { return pio0_; }   ///< Kopplung 16 → 8 (P11)
    Z80PIO&           pio1()  { return pio1_; }   ///< Kopplung 8 → 16 (P11)
    Z80PIO&           pio2()  { return pio2_; }   ///< WDC (P13)
    K1520Bus&         peripherieBus() { return peri_; }
    const Config&     config() const { return cfg_; }
    const uint8_t*    rom() const { return rom_; }

    /// Externe Treiber an PIO-Pins (Kopplung, WDC) — Muster `P8000Karte8::setzePinTreiber`.
    /// @p pio 0..2, @p port 0 = A, 1 = B.  Vorgabe [K1].
    void setzePinTreiber(int pio, int port, uint8_t pegel, uint8_t treibMask);
    /// Nach jedem CPU-Schreiben auf eine PIO und nach jedem PIO-Reset (Kopplung, P11).
    void setzePioHaken(std::function<void(int pio)> h) { pio_haken_ = std::move(h); }

    // ─── Bus von außen (Debugger/Tests; ohne Wartetakt, mit allen Nebenwirkungen) ──
    uint16_t busLesen(const Z8kBusCycle& c);
    void     busSchreiben(const Z8kBusCycle& c, uint16_t daten);
    /// Was der Identifier in der NMI-Quittung jetzt zeigte.
    uint16_t nmiIdentifier() const;
    /// On-Board-SRAM (Byte 000–7FF), Debugger/Tests.
    uint8_t& sram(uint16_t i) { return sram_[i & 0x7FF]; }

    // ─── Debugger (P12; leer = kein Aufwand) ─────────────────────────────────
    /// Vor jedem Befehl des U8001 (nicht im Reset): `true` = nicht ausführen, `schritt()` liefert 0
    /// (Debugger-Halt, die Zeit bleibt als Guthaben stehen — Muster `EM::setStepHook`).
    std::function<bool(const Z8000&)> schrittHaken;
    /// Nach jedem Buszyklus des U8001 (Lesen/Schreiben, mit Datum) — Beobachtung, keine Wirkung.
    std::function<void(const Z8kBusCycle&, uint16_t daten, bool lesen)> zyklusHaken;

    // ─── Save-State ──────────────────────────────────────────────────────────
    void serialize(std::vector<uint8_t>& out) const;
    /// Teilweise angewandt, wenn @c false — der Aufrufer (Maschine) sichert vorher.
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    class Anschluss;
    friend class Anschluss;

    Z80PIO& pio(int i) { return i == 0 ? pio0_ : (i == 1 ? pio1_ : pio2_); }
    void pinsAnlegen(int i);
    void piosZuruecksetzen();
    void mresetBausteine();
    void mresetSetzen(bool aktiv);
    void nmiNachfuehren();
    void paritaetNachSCR();
    uint8_t ioLesenByte(uint16_t port);
    void    ioSchreibenByte(uint16_t port, uint8_t d);
    uint16_t speicherLesen(const Z8kBusCycle& c);
    void     speicherSchreiben(const Z8kBusCycle& c, uint16_t d);
    void     stapelImpuls() { ctc0_.clkTrg(3, false); ctc0_.clkTrg(3, true); }
    void     takt(int n);

    Config cfg_;
    const uint8_t* rom_ = nullptr;
    K1520Bus peri_;                      ///< nur Interruptkette/RETI (Entwurf §10.1)
    Z8000 cpu_;
    P8000MmuLogik16 mmu_;
    P8000Dram16 dram_;
    std::array<uint8_t, 2048> sram_{};
    Z80CTC  ctc0_{"P8K16-CTC0"}, ctc1_{"P8K16-CTC1"};
    Z80SIO  sio0_{"P8K16-SIO0"}, sio1_{"P8K16-SIO1"};
    Z80PIO  pio0_{"P8K16-PIO0"}, pio1_{"P8K16-PIO1"}, pio2_{"P8K16-PIO2"};

    bool reset_eingang_ = true, treset_eingang_ = false, mreset_ = true;
    bool manual_nmi_ = false, nmi_pegel_ = false, paritaet_ff_ = false;
    int  nmi_rest_ = 0;                  ///< Takte bis zum Ende des Tastenimpulses
    uint64_t zeit_ = 0;
    struct Pins { uint8_t pegel = 0, maske = 0; };
    Pins pins_[3][2];
    std::function<void(int)> pio_haken_;

    std::array<std::unique_ptr<Anschluss>, TTY_ANZAHL> anschluesse_;
    bool seriell_geaendert_ = false;
};

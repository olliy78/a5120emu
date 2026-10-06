/**
 * @file karte8.h
 * @brief 8-Bit-Rechnerkarte des P8000 (P 8000/8.1): U880, E/A-Dekoder 00H–3FH, CTC0/1, SIO0/1,
 *        PIO0/1/2, DS8282-Latches, DMA-Platzhalter, Interruptkette, RESET/RESI/NMI-Weiche.
 *
 * Quelle: doc/p8000/schaltplan_8bit.md §2–§5, §7, §8 (Rang 1), doc/design/25_p8000.md §10.1–§10.4,
 * AP P5e.  **Ohne** Floppy (FDC/DMA/PIO2-Verdrahtung: P5f) und ohne Kopplungsgegenseite (P11).
 *
 * @code
 *   E/A (nur IORQ ohne M1; A8–A15 egal)       00H–07H  Speicher8 (ADP/RES_RFF, eigenes Gerät)
 *   08H–0BH CTC0    0CH–0FH PIO0 (Kopplung)   10H–13H  Latch 1 (DS8282, nur schreibend, 4-fach)
 *   14H–17H Latch 2 (nur schreibend)          18H–1BH  PIO1 (EPROMmer, unbeschaltet)
 *   1CH–1FH PIO2 (Floppy-Port)                20H–23H  FDC (P5f; hier offen, 22H/23H spiegeln)
 *   24H–27H SIO0    28H–2BH SIO1   2CH–2FH CTC1   30H–3BH RES1..3 (nur X13, hier offen)
 *   3CH–3FH DMA (Platzhalter, 4-fach gespiegelt)  40H–FFH  nichts (E3 = NOR(A6,A7))
 *   PIO/SIO: A0 = C/D, A1 = B/A;  CTC: A0/A1 = Kanal.  Lesen unbelegter Tore = FFH [Annahme]
 *   Interruptkette: DMA – PIO2 – CTC0 – SIO0 – SIO1 – PIO0 – PIO1 – CTC1   (Schaltplan §3)
 * @endcode
 *
 * **Baudtakt (§5):** Quarz 9,832 MHz ÷ 8 = CPBAUD 1,229 MHz an CLK/TRG von CTC0 K0 und CTC1 K0–K2
 * (Bruchtakt-Phasenakkumulator `Z80CTC::setzeEingangsTakt`); ZC/TO ÷ 2 (7474) → SIO-Takt:
 * tty0 = CTC1 K0 → SIO0-A, tty1 = CTC1 K1 → SIO0-B, tty2 = CTC1 K2 → SIO1-A, tty3 = CTC0 K0 → SIO1-B.
 * CLK/TRG von CTC0 K1–K3 und CTC1 K3 sind unbeschaltet (reine Zeitgeber).
 *
 * **RESET/RESI/NMI (§4):** `powerOn()` = Netz-Ein (RESI = 1), `reset()` = Taste /RESP (RESI = 0);
 * beide /RES an CPU, CTCs, SIOs, PIOs (über /PM1), Speicher8 (RFF Q = 0).  RESI liegt an PIO2-A7.
 * NMI-Taste: `/NMI(U880) = NAND(/NMI-UM, NMIP)` mit /NMI-UM = Pinpegel von PIO0-B7 (offen = Pull-up
 * = 1); bei B7 = 0 geht die Taste statt an den U880 an die 16-Bit-Karte (`setNmiU8000Rueckruf`).
 *
 * **Annahmen** (Schaltplan §11): DS8282-Startwert `Config::latch_start` (Frage 6); /CTS tty0 =
 * RTS, übrige fest aktiv, /DCD vom Anschluss (Frage 8); SIO-/W/RDY an /WAIT nicht nachgebildet
 * (einmalige Warnung, falls ein Gast WR1 D7 setzt).
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/cards/p8000/speicher8.h"
#include "core/primitives/z80.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include "core/primitives/z80_sio.h"
#include "core/serial/anschluss.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>

class P8000Karte8 : public BusDevice {
public:
    struct Config {
        enum class Index { I1, I3 };                                   ///< Leiterplattenindex (ohne Wirkung in der Karte)
        enum class Mon8  { V3_0, V3_1, V3_1_Nur8Bit, V2_1_Nur8Bit };   ///< EPROM-Satz
        Index    index = Index::I3;
        Mon8     mon8  = Mon8::V3_1;
        uint32_t takt_hz       = 4'000'000;   ///< Φ (Quarz 16 MHz ÷ 4)
        uint32_t baudquarz_hz  = 9'832'000;   ///< Quarz C15; CPBAUD = ÷ 8
        uint8_t  latch_start   = 0x00;        ///< DS8282 nach Netz-Ein [real unbestimmt, Frage 6]
        uint8_t  adp_start     = 0x00;
        uint8_t  ram_fuellwert = 0x00;
    };

    static constexpr int TTY_ANZAHL = 4;

    explicit P8000Karte8(K1520Bus& bus) : P8000Karte8(bus, Config{}) {}
    P8000Karte8(K1520Bus& bus, const Config& cfg);
    ~P8000Karte8() override;

    // ─── serielle Kanäle tty0–tty3 (SIO0-A/B, SIO1-A/B) ──────────────────────
    k1520::serial::SerialAnschluss& anschluss(int tty);
    /// Ein Anschluss hat SIO-Zustand geändert — Interruptkette neu bewerten.  Liest und löscht.
    bool nimmSeriellGeaendert() { const bool g = seriell_geaendert_; seriell_geaendert_ = false; return g; }

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn();    ///< Netz-Ein: RAM/ADP/Latches vorbelegen, RESI = 1, dann /RES
    void reset();      ///< Taste /RESP: /RES an alle Bausteine, RESI = 0, RFF Q = 0
    void nmiTaste();   ///< NMI-Taste (/NMIP), Weiche nach PIO0-B7

    /// Ein Befehl (inkl. Interrupt-/NMI-Annahme); liefert Takte inkl. EPROM-Wartetakten.
    /// 0 = Debugger-Halt.  Die Zeitbasis (CTCs) führt anschließend `takt(n)`.
    int  schritt();
    /// Φ-Takte an die CTCs (und Interruptkette neu bewerten, wenn sich etwas geändert hat).
    void takt(int n);

    // ─── Bausteine ───────────────────────────────────────────────────────────
    Z80&              cpu()      { return cpu_; }
    P8000Speicher8&   speicher() { return sp_; }
    Z80CTC&           ctc0()     { return ctc0_; }
    Z80CTC&           ctc1()     { return ctc1_; }
    Z80SIO&           sio0()     { return sio0_; }
    Z80SIO&           sio1()     { return sio1_; }
    Z80PIO&           pio0()     { return pio0_; }   ///< Kopplung (P11)
    Z80PIO&           pio1()     { return pio1_; }   ///< EPROMmer, unbeschaltet
    Z80PIO&           pio2()     { return pio2_; }   ///< Floppy-Port (P5f)
    const Config&     config() const { return cfg_; }
    K1520Bus&         bus()      { return bus_; }

    // ─── Leitungen ───────────────────────────────────────────────────────────
    /// RESI (Q von 6D11) an PIO2-A7: 1 = Power-on, 0 = Taste.
    bool resi() const { return resi_; }
    /// Pinpegel von PIO0-B7 (/NMI-UM und /RESET_U8000): Eingang ⇒ Pull-up ⇒ 1.
    bool b7eff() const { return (pio0_.pinsB() & 0x80) != 0; }
    /// Ausgangsleitungen der DS8282 (nur Schreiben vom U880; nicht lesbar).
    uint8_t latch1() const { return latch_[0]; }   ///< 10H: D0–D7/8-16
    uint8_t latch2() const { return latch_[1]; }   ///< 14H: INT_16, V1–V6/8-16, RDY/8-16
    using LatchRueckruf = std::function<void(int latch /*0/1*/, uint8_t wert)>;
    void setLatchRueckruf(LatchRueckruf cb) { latch_cb_ = std::move(cb); }
    /// NMI-Taste bei B7eff = 0: geht an die 16-Bit-Karte (/NMI-U8000).
    void setNmiU8000Rueckruf(std::function<void()> cb) { nmi16_cb_ = std::move(cb); }

    /// Externe Treiber an den Pins eines PIO-Ports (Kopplung, Floppy-Glue); bleiben über
    /// einen PIO-Reset hinweg erhalten und werden nach jedem Steuerwort neu angelegt, weil
    /// `Z80PIO` die Eingabebits nach einem Moduswechsel nicht aus den Pins nachlädt.
    /// @p pio 0..2, @p port 0 = A, 1 = B.
    void setzePinTreiber(int pio, int port, uint8_t pegel, uint8_t treibMask);

    /// FDC-Tore 20H–23H (P5f hängt hier ein; ohne Handler: Lesen FFH, Schreiben verschluckt).
    std::function<uint8_t(uint8_t reg /*0..1*/)>       fdcLesen;
    std::function<void(uint8_t reg, uint8_t wert)>     fdcSchreiben;

    // ─── Haken für die Floppy-Seite (floppy8, P5f) ───────────────────────────
    /// DMA UA858 (3CH–3FH, 4-fach gespiegelt): Steuerport.  Ohne Handler: Lesen FFH.
    std::function<uint8_t()>        dmaLesen;
    std::function<void(uint8_t)>    dmaSchreiben;
    /// Echte DMA statt des Platzhalters in die Interruptkette (vorderstes Glied) einsetzen.
    void setzeDmaKettenglied(InterruptSlave* dma);
    /// DMA als Busmaster: fordert @p anfrage den Bus (/BUSRQ), steht die CPU still und
    /// @p schritt bewegt ein Byte (Rückgabe Takte, 0 = Bus gehalten ohne RDY ⇒ 4 Takte).
    void setzeBusmaster(std::function<bool()> anfrage, std::function<int()> schritt) {
        bm_anfrage_ = std::move(anfrage); bm_schritt_ = std::move(schritt);
    }
    /// Nach jedem /RES der Karte (Netz-Ein und Taste), nachdem die PIOs zurückgesetzt sind.
    void setzeResetHaken(std::function<void()> h) { reset_haken_ = std::move(h); }

    // ─── Save-State (P8KS, Entwurf 25 §10.2) ─────────────────────────────────
    /// U880, Speicher8, CTC0/1 (samt Bruchtakt-Phase), SIO0/1, PIO0–2 (samt Handshake/Pins),
    /// Latches, RESI, DCD-Pegel, anstehender NMI.  Nicht: EPROM, Rückrufe, Verdrahtung.
    void serialize(std::vector<uint8_t>& out) const;
    /// Teilweise angewandt, wenn @c false — der Aufrufer (Maschine) sichert vorher.
    bool deserialize(const uint8_t*& p, const uint8_t* end);

    // ─── BusDevice: alle Tore der Karte außer 00H–07H (absolute Portnummer) ──
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "P8000-Karte8"; }

private:
    class DmaPlatzhalter;
    class Anschluss;
    friend class Anschluss;

    Z80PIO& pio(int i) { return i == 0 ? pio0_ : (i == 1 ? pio1_ : pio2_); }
    void pinsAnlegen(int i);
    void resetBausteine();
    void ketteAnlegen();

    K1520Bus& bus_;
    Config    cfg_;

    P8000Speicher8 sp_;
    Z80     cpu_;
    Z80CTC  ctc0_{"P8K-CTC0"}, ctc1_{"P8K-CTC1"};
    Z80SIO  sio0_{"P8K-SIO0"}, sio1_{"P8K-SIO1"};
    Z80PIO  pio0_{"P8K-PIO0"}, pio1_{"P8K-PIO1"}, pio2_{"P8K-PIO2"};
    std::unique_ptr<DmaPlatzhalter> dma_;

    bool resi_ = true;
    std::array<uint8_t, 2> latch_{};
    LatchRueckruf latch_cb_;
    std::function<void()> nmi16_cb_;
    std::function<bool()> bm_anfrage_;
    std::function<int()>  bm_schritt_;
    std::function<void()> reset_haken_;
    struct Pins { uint8_t pegel = 0, maske = 0; };
    Pins pins_[3][2];
    bool wait_gewarnt_ = false;

    std::array<std::unique_ptr<Anschluss>, TTY_ANZAHL> anschluesse_;
    bool seriell_geaendert_ = false;
};

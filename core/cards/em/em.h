/**
 * @file em.h
 * @brief Erweiterungsmodul EM064/EM256 des A5120.16 — die 8-Bit-Seite (ohne U8001).
 *
 * Das EM besteht aus zwei K1520-Steckeinheiten: der **Steuerkarte** (062-9005:
 * U8001, PIO A32, Register A33–A36, Attributspeicher A22, Betriebsartensteuerung)
 * und der **Speicherkarte** (062-9000 EM256 = 256 KB, 062-9001 EM064 = 64 KB).
 * Diese Klasse bildet alles ab, was der **U880** davon sieht:
 *
 * - E/A-Decoder A14: 8 Tore ab MODADR (Vorgabe A8H, Brücken X10/X11).  AB8–15 werden
 *   vom Decoder NICHT ausgewertet — wohl aber vom Attributspeicher (AB12–15).
 * - PIO A32 (U855) in der IEI/IEO-Kette, Tore A8H..ABH (A-Daten, B-Daten, A-Steuer,
 *   B-Steuer).
 * - Status-8 A36 (ACH, schreiben), Vektor-8 A34 (ADH, schreiben → /VI an PIO A3),
 *   Status-16 A35 (AEH, lesen), Attributspeicher A22 (AFH, schreiben / negiert lesen).
 * - Seitenabbildung: 16 Seiten × 4 KB; je Seite PEN, WE, A14-8, A15-8.  Übernimmt
 *   die Karte einen Zugriff, zieht sie Bus-/MEMDI (MemdiDriver) — der K1520-RAM
 *   bleibt still.
 * - Betriebsartensteuerung, U880-Hälfte: RESET16, FF A29 (8/16), TRQ8, TREN.
 *
 * Belegt aus den Schaltplänen, Stand und Quellen: doc/design/17_a5120_16.md §7;
 * die beiden in S1 geklärten Fragen (MEMDI-Quelle, Leseadresse A22) stehen bei
 * @ref EM::drivesMemdi bzw. @ref EM::A22Lesart.
 *
 * **Ohne U8001** (S1): µ0 bleibt high (nach Reset erzwungen, gesetzt würde es nur
 * durch `MSET`/`MREQ` des U8001) ⇒ TREN = 0 ⇒ TRQ8 bewirkt nichts; 8/16 = RESET16.
 * Das ist kein Platzhalter: genau so verhält sich die Karte mit einem U8001, der nie
 * `MSET` ausführt (Plan §3 S1 „Einstieg").
 */
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_pio.h"

class EM : public MemdiDriver, public BusDevice, public InterruptSlave {
public:
    /// Bestückung der Speicherkarte.
    enum class Variante : uint8_t {
        EM064,   ///< 062-9001: 64 KB, SG0/SG1 gehen nicht in die Matrix ⇒ 4× gespiegelt
        EM256    ///< 062-9000: 256 KB, Zelle = SG1·128K + SG0·64K + Adresse
    };

    /**
     * @brief Welche Seite adressiert A22 beim LESEN an MODADR+7?
     *
     * **Belegt (9005/2):** A13 ist ein DL075 (≙ 7475, *transparenter* 4-Bit-Latch —
     * Anschlüsse D1=2, D2=3, C12=13, D3=6, D4=7, C34=4; benutzt die /Q-Ausgänge 1,
     * 14, 11, 8), Freigabe C = A23/02 = ¬A18/03 = /MREQ ∧ /Q(A19); A19 (7474, D=/S=H)
     * wird mit A18/03 getaktet und von ¬TAKT (A210/10 ← X1 A21) zurückgesetzt.  Der
     * Latch ist also offen, solange kein Speicherzyklus läuft, und hält die Seite nur
     * WÄHREND eines Speicherzyklus.  Das „bis zum nächsten Speicherzugriff" des
     * Handbuchs (§1.5) meint das Festhalten während des Zyklus — im E/A-Zyklus liegt
     * AB12–15 des E/A-Zyklus an (bei `IN A,(C)` also Register B).
     *
     * Messung am Gerät steht noch aus (G1, `em256adr.com`) — deshalb bleibt die
     * wörtliche Handbuchlesart als Gegenprobe wählbar.
     */
    enum class A22Lesart : uint8_t {
        ZyklusAdresse,          ///< AB12–15 des E/A-Zyklus (belegt, Vorgabe)
        LetzterSpeicherzugriff  ///< Seite des letzten U880-Speicherzugriffs (Handbuch wörtlich)
    };

    /**
     * @brief Brücken und Bestückung (Compile-Time-Konfiguration der Karte).
     */
    struct Config {
        Variante  variante = Variante::EM256;
        uint8_t   modadr   = 0xA8;   ///< X10: 1–2, 3–6, 4–5; X11: 3–9 (Handbuch §1.7.1)
        A22Lesart lesart   = A22Lesart::ZyklusAdresse;
        /// N/S des U8001 ist im Reset laut Zilog §9.7 *undefiniert*; was PIO A5 dann
        /// zeigt, ist nicht belegt.  Vorgabe: H (Normal).
        bool      ns_im_reset = true;
    };

    explicit EM(K1520Bus& bus);
    EM(K1520Bus& bus, const Config& cfg);

    /// E/A-Tore MODADR..MODADR+7 und den Vorrangspeicher am Bus anmelden.
    void attachToBus();

    /// Netz-Ein: DRAM und Attributspeicher unbestimmt, danach /RESET.
    void powerOn();
    /// System-/RESET: PIO hochohmig ⇒ Pull-ups ⇒ RESET16 = 1, /RAMEN = 1.
    /// DRAM und A22 behalten ihren Inhalt (die RAM-Floppy M: überlebt die Reset-Taste).
    void reset();

    // ─── BusDevice (E/A-Decoder A14) ────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "EM"; }

    // ─── MemdiDriver (Speicherzugriff des U880) ─────────────────────────────
    /**
     * @brief MEN für @p addr: zieht die Karte /MEMDI und übernimmt den Zyklus?
     *
     * **Belegt (9005/2):** MEN = A310/12 = ¬(8/16 ∧ RAMEN ∧ PEN) (7410, Eingänge 1 =
     * 8/16 = A29/09, 2 = RAMEN = A31/10, 13 = PEN = A22 F0).  Dieses Netz geht
     * (Abzweig bei x≈1990/y≈1473 des Scans) an A210/12 (RDY) und an A112/12; A112
     * (7400) verknüpft es mit /MDI (X2 C17, Pull-up R5), A311/10 = /MDO (X2 A17),
     * A17/11 (7403) = /MEMDI (X1 C09).  Mit offenem /MDI gilt /MEMDI = /MDO = MEN.
     * **Bus-/MEMDI hängt also an MEN, nicht an PEN allein** — das „bei PEN = 1 werden MDO
     * und MEMDI aktiv" des Handbuchs §1.5 lässt RAMEN und 8/16 nur weg.
     */
    bool    drivesMemdi(uint16_t addr) override;
    uint8_t memRead(uint16_t addr) override;
    void    memWrite(uint16_t addr, uint8_t data) override;

    // ─── InterruptSlave (PIO A32 in der IEI/IEO-Kette) ──────────────────────
    const char* intDeviceName() const override { return "EM PIO-A32"; }
    void    setIEI(bool iei) override      { pio_.setIEI(iei); }
    bool    getIEO() const override        { return pio_.getIEO(); }
    bool    hasInterrupt() const override  { return pio_.hasInterrupt(); }
    uint8_t getVector() const override     { return pio_.getVector(); }
    void    onRETI() override              { pio_.onRETI(); }

    // ─── Testhaken ──────────────────────────────────────────────────────────
    /**
     * @brief Paritätsfehler auslösen (FF auf der Speicherkarte → /PE = PIO B7 = 0).
     *
     * Paritätsbits werden nicht nachgebildet; ein Fehler entsteht nur über diesen
     * Haken.  Solange PR (PIO B6) = 1 anliegt, bleibt das FF zurückgesetzt.
     */
    void injectParityError();

    // ─── Zustand (Tests, Debugger, später Oberfläche) ───────────────────────
    const Config& config() const { return cfg_; }
    const Z80PIO& pio() const    { return pio_; }
    bool     mode8() const       { return mode8_; }       ///< 8/16 (A29 Q): 1 = 8-Bit-Mode
    bool     reset16() const     { return reset16_; }     ///< PIO B4 (Pull-up R4:2)
    bool     ramEnabled() const  { return ramen_; }       ///< RAMEN = ¬/RAMEN (PIO B2)
    bool     trq8() const        { return trq8_; }        ///< TRQ8 = ¬/TRQ8 (PIO B5)
    bool     tren() const        { return tren_; }        ///< TREN = ¬µ0 (ohne U8001: 0)
    uint8_t  segment() const     { return seg_; }         ///< SG1P:SG0P (PIO B1:B0)
    bool     parityError() const { return per_ff_; }
    bool     ledV1() const       { return ramen_; }       ///< V1 an A17/08 ← RAMEN (A31/10)
    bool     ledV2() const       { return mode8_; }       ///< V2 an A29 /Q: leuchtet im 8-Bit-Mode
    /// Gespeicherter (= geschriebener) Wert der Seite @p page; F0..F3 sind invertiert.
    uint8_t  attribute(int page) const { return attr_[page & 0x0F]; }
    uint8_t  status8() const     { return status8_; }     ///< A36 (→ U8001 AD8–15)
    uint8_t  vector8() const     { return vector8_; }     ///< A34 (→ U8001 AD0–7)
    bool     viPending() const   { return vi_pending_; }  ///< A34 INT aktiv (PIO A3 = 0)
    uint8_t  steuer16() const    { return a33_; }         ///< A33 (vom U8001; ohne ihn 0)
    uint8_t  status16() const    { return status16_; }    ///< A35 (vom U8001)
    /// MRDY: der U880-Zugriff auf @p addr wird von der Karte bestätigt (= MEN).
    bool     memRdy(uint16_t addr) const { return (men_mask_ >> (addr >> 12)) & 1; }

    /// Rohzugriff auf das DRAM (Zelle 0 .. size()-1), an Abbildung und Schutz vorbei.
    uint32_t size() const { return static_cast<uint32_t>(dram_.size()); }
    uint8_t  peek(uint32_t cell) const { return dram_[cell % dram_.size()]; }
    void     poke(uint32_t cell, uint8_t v) { dram_[cell % dram_.size()] = v; }
    /// DRAM-Zelle, die ein U880-Zugriff auf @p addr mit dem jetzigen Zustand träfe.
    uint32_t cellFor(uint16_t addr) const {
        return page_base_[addr >> 12] | (addr & 0x3FFFu);
    }

private:
    void writeA22(uint8_t data);
    uint8_t readA22() const;
    /// PIO-Ausgänge neu bewerten (nach jedem Zugriff auf die PIO und nach Reset):
    /// Pins = Ausgangslatch, wo die PIO treibt, sonst H (Pull-ups R4 / offene TTL-Eingänge).
    void updateFromPio();
    /// Betriebsartensteuerung (§7.4) und PIO-Eingänge nachziehen.
    void updateMode();
    void updatePioInputs();
    void rebuildMap();

    K1520Bus& bus_;
    Config    cfg_;
    Z80PIO    pio_{"EM PIO-A32"};
    std::vector<uint8_t> dram_;

    std::array<uint8_t, 16>  attr_{};        ///< A22, gespeicherte Werte (4 Bit)
    std::array<uint32_t, 16> page_base_{};   ///< DRAM-Basis je Seite (ohne AB13..0)
    uint16_t men_mask_  = 0;   ///< Bit n: Seite n übernimmt (MEN)
    uint16_t we_mask_   = 0;   ///< Bit n: Seite n beschreibbar (WE)
    uint8_t  last_mem_page_ = 0;  ///< für A22Lesart::LetzterSpeicherzugriff

    // PIO B (Pins)
    uint8_t seg_      = 3;
    bool    ramen_    = false;
    bool    reset16_  = true;
    bool    trq8_     = false;
    bool    pr_       = true;
    // Betriebsartensteuerung
    bool    mode8_    = true;   ///< FF A29
    bool    tren_     = false;  ///< ¬µ0; µ0 setzt nur MSET des U8001
    bool    busak_    = false;  ///< BUSAK' (U8001 gibt den Bus nie ab, solange TREN = 0)
    // Register
    uint8_t a33_      = 0;
    uint8_t status8_  = 0;
    uint8_t vector8_  = 0;
    bool    vi_pending_ = false;
    uint8_t status16_ = 0;      ///< nach Netz-Ein unbestimmt
    bool    per_ff_   = false;

    uint8_t pio_a_in_ = 0xFF;
    uint8_t pio_b_in_ = 0xFF;
};

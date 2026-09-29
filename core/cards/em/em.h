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
 * - Betriebsartensteuerung: RESET16, FF A29 (8/16), TRQ8, TREN, BUSRQ/BUSAK.
 *
 * Seit S4 sitzt der **U8001** (bzw. U8002 beim EM064) auf der Karte (@ref u8001):
 * - Segmentweiche A42 (drei Modi aus A33 Bit 5–7) + Umschalter A41 ⇒ Zelle des
 *   U8001-Zugriffs (@ref cellFor16); dieselbe Zelle wie beim U880 (big-endian, §7.5).
 * - A33/A35 über Standard-E/A (nur AD7 dekodiert, Schreiben = beide Register),
 *   Status-8 beim E/A-Lesen auf AD8–15, VI-Quittung = Vektor A34 (low) + Status-8
 *   (high) und löscht /VI, NVI-Quittung lädt den Einzelbefehlszähler A53 und hält
 *   ihn geladen (Selbsthaltekreis A54), bis A33 Bit 3 ihn freigibt.
 * - µI = /TRQ8, TREN = ¬µ0, /BUSRQ = ¬(TREN ∧ TRQ8), BUSAK → FF A29, STOP = PIO B3.
 * - A29 wird mit dem M1 des U880 getaktet (@ref onU880M1).
 * - Zeit: @ref advance zieht den U8001 befehlsweise auf die Maschinenzeit nach
 *   (U880-Takte × f16/f8; beide Takte in @ref Config).
 *
 * Belegt aus den Schaltplänen, Stand und Quellen: doc/design/17_a5120_16.md §7;
 * die beiden in S1 geklärten Fragen (MEMDI-Quelle, Leseadresse A22) stehen bei
 * @ref EM::drivesMemdi bzw. @ref EM::A22Lesart.
 *
 * **Der U880 läuft im 16-Bit-Mode weiter** (Handbuch §1.7.3: „Der U 880 arbeitet in
 * seinem Systemspeicher weiter, kann aber nicht auf den RAM des EM zugreifen").
 * BUSRQ/BUSAK ist ein Handschlag zwischen Karte und U8001, nicht mit dem U880.
 */
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_pio.h"
#include "core/primitives/z8000.h"

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
        /// N/S des U8001 ist im Reset laut Zilog §9.7 *undefiniert*.  **Am Gerät
        /// gemessen (2026-09-29, `em256adr`/`em256tst`: PIO A = 48H): L** — der Pin
        /// zeigt im Reset den Systemmodus.  Vorgabe daher L.
        bool      ns_im_reset = false;
        /// Takt des U880 (Maschinentakt des A5120 im Emulator, app/takt.py) und des U8001
        /// (A55 DS8127: 16-MHz-Quarz / 4).  Nur das Verhältnis zählt.
        uint32_t  takt_u880_hz  = 2'450'000;
        uint32_t  takt_u8001_hz = 4'000'000;
        /// Vorlast des Einzelbefehlszählers A53 (74193, rückwärts, QD → NVI).  **Nur A3
        /// ist fest H**; A0, A1 **und A2** gehen an das Brückenfeld X12 (9005/1: X12/1,
        /// 2, 3 an A0..A2, X12/4 = H, X12/5 = L; Handbuch Abb. 8).  QD fällt nach
        /// (Vorlast − 7) Stapelzugriffen.  Zweck laut Handbuch §1.12: „nach dem letzten
        /// STACK-Zugriff“ des Rücksprungs aus dem Debugger — `IRET` des U8001
        /// (segmentiert) holt 4 Worte vom Stapel ⇒ Vorlast 11 (A2 = L, A0 = A1 = H).
        /// **Am Gerät gemessen (2026-09-29, `em16abl` G3): 4 Stapelzugriffe ⇒ 11.**
        /// Werte unter 8 sind nicht beschaltbar (A3 = H) und werden zu 8 angehoben.
        uint8_t   a53_vorlast = 11;
    };

    explicit EM(K1520Bus& bus);
    EM(K1520Bus& bus, const Config& cfg);

    /// E/A-Tore MODADR..MODADR+7 und den Vorrangspeicher am Bus anmelden.
    void attachToBus();

    EM(const EM&) = delete;
    EM& operator=(const EM&) = delete;

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

    // ─── 16-Bit-Seite: U8001 und Maschinenzeit ──────────────────────────────
    /**
     * @brief Maschinenzeit ist um @p u880Takte Takte des U880 vorgerückt: den U8001
     *        befehlsweise nachziehen (Rest wird als Guthaben/Schuld mitgeführt).
     *
     * Solange der U8001 im Reset steht oder den Bus abgegeben hat (BUSAK bei
     * anliegendem BUSRQ), wird nicht Schritt für Schritt gerechnet — dort ändert sich
     * nichts, bis der U880 an der Karte etwas tut.
     */
    void advance(int u880Takte);
    /// M1-Zyklus des U880: Takt des FF A29 (A16 = NAND(/M1, /RST) = M1 ∨ RST, 9005/2).
    void onU880M1() {
        if (!mode8_ && m1_setzt_a29_) { mode8_ = true; updateMode(); }
    }
    Z8000&       u8001()       { return u8k_; }
    const Z8000& u8001() const { return u8k_; }
    /// Segmentweiche A42: 0 = Segment aus A33 Bit 5/6, 1 = INSTR × N/S, 2 = SN0/SN1.
    uint8_t  segMode() const   { return (a33_ & 0x80) ? ((a33_ & 0x40) ? 2 : 1) : 0; }
    /// DRAM-Zelle eines U8001-Speicherzyklus (Segmentweiche + Umschalter A41, §7.3/§7.5).
    uint32_t cellFor16(const Z8kBusCycle& c) const;
    uint8_t  a53() const       { return a53_; }       ///< Einzelbefehlszähler A53
    /// NVI am U8001 = ¬QD von A53 (Pegel, Handbuch Abb. 8: A53/07 direkt an NVI).
    bool     nviLine() const   { return !(a53_ & 0x08); }
    /// Selbsthaltekreis A54 (Q an A54/10 → A53 LOAD): true = Zähler freigegeben.
    bool     a54Freigabe() const { return a54_zaehlt_; }
    bool     busRq16() const   { return tren_ && trq8_; }   ///< BUSRQ am U8001
    bool     busAck16() const  { return busak_; }
    bool     stop16() const    { return stop_; }      ///< STOP am U8001 (PIO B3 = 0)

    // ─── Save-State ─────────────────────────────────────────────────────────
    /// Ganzer Kartenzustand inkl. DRAM, PIO, U8001 (Register + Ablaufzustand).
    void serialize(std::vector<uint8_t>& out) const;
    /// Gegenstück; false bei zu kurzem/falschem Block (dann ist nichts verändert).
    bool deserialize(const uint8_t*& p, const uint8_t* end);

    // ─── Debug-Anschluss (S5: k1520dbg, boot_trace) ─────────────────────────
    /// Kommunikationstransaktionen und Pegelwechsel der Karte, wie sie ein Debugger
    /// oder Trace sehen will.  `by16` = vom U8001 ausgelöst (sonst U880).
    enum class Ereignis : uint8_t {
        PioSchreiben,   ///< U880 OUT an die PIO A32 (addr = Tor 0..3 relativ, value = Byte)
        PioLesen,       ///< U880 IN von der PIO A32
        Status8,        ///< U880 OUT ACH → A36
        Vektor8,        ///< U880 OUT ADH → A34 (value = Vektor; löst VI aus)
        Status16Lesen,  ///< U880 IN AEH ← A35
        A22Schreiben,   ///< U880 OUT AFH (addr = Seite, value = Attribut)
        A22Lesen,       ///< U880 IN AFH
        A33A35,         ///< U8001 OUT (addr = Port, value = A35:A33)
        Status8Lesen,   ///< U8001 IN (value = Status-8 auf AD8–15)
        ViQuittung,     ///< U8001 VI-Quittung (value = Kennung)
        NviQuittung,    ///< U8001 NVI-Quittung (A53 neu geladen, A54 hält die Ladung)
        NmiQuittung,
        Modus,          ///< FF A29 gekippt (value 1 = 8-Bit-Mode, 0 = 16-Bit-Mode)
        Int16,          ///< A33 Bit 4 (value = neuer Pegel)
        Tren,           ///< TREN (value = neuer Pegel)
        BusRq,          ///< BUSRQ am U8001 (value = neuer Pegel)
        BusAk,          ///< BUSAK vom U8001 (value = neuer Pegel)
        Reset16,        ///< RESET16 (value = neuer Pegel)
        Nvi,            ///< NVI-Anforderung aus A53 (value = neuer Pegel)
        Stop,           ///< STOP (PIO B3, value = neuer Pegel)
    };
    struct EreignisInfo {
        Ereignis kind;
        uint16_t addr  = 0;
        uint16_t value = 0;
        bool     by16  = false;
    };
    static const char* ereignisName(Ereignis e);
    /// Ereignis-Rückruf; ohne ihn kostet jede Stelle nur einen Test.
    void setEventHook(std::function<void(const EreignisInfo&)> f) {
        on_event_ = std::move(f);
        dbg_prev_ = pegelSignatur();
    }
    /**
     * @brief Rückruf VOR jedem Schritt des U8001 (nicht im Reset/BUSAK/STOP geparkt).
     *
     * Liefert er true, wird der Schritt NICHT ausgeführt und @ref advance kehrt sofort
     * zurück; die Zeit bleibt als Guthaben stehen (Debugger: Halt vor dem Befehl).
     */
    void setStepHook(std::function<bool(const Z8000&)> f) { on_step_ = std::move(f); }
    /// Zeitguthaben des U8001 in U8001-Takten (negativ = Vorlauf).
    double guthabenTakte16() const {
        return double(guthaben_) / double(cfg_.takt_u880_hz);
    }

    // ─── Testhaken ──────────────────────────────────────────────────────────
    /**
     * @brief Paritätsfehler auslösen (FF auf der Speicherkarte → /PE = PIO B7 = 0).
     *
     * Paritätsbits werden nicht nachgebildet; ein Fehler entsteht nur über diesen
     * Haken.  Das FF A46 (9000/1) ist ein **bleibender Merker**: sein Takt A43/03 =
     * NAND(RDI, Q) sperrt sich, sobald Q = 0 (Fehler) — erst /PR (Setzeingang) löscht
     * ihn, und /PR = ¬PR · /NMI-ACK (9005/1).  Solange PR (PIO B6) = 1 anliegt, bleibt
     * das FF zurückgesetzt; eine NMI-Quittung des U8001 löscht es ebenfalls.
     * RDI = NAND(/MRD-16, /MRD-8) (9005/2 A112/08), /MRD-8 nur mit MREQ-8 und damit
     * MEN — Lesezugriffe des U880 ausserhalb des EM takten A46 nicht (G5).
     */
    void injectParityError();

    // ─── Zustand (Tests, Debugger, später Oberfläche) ───────────────────────
    const Config& config() const { return cfg_; }
    const Z80PIO& pio() const    { return pio_; }
    bool     mode8() const       { return mode8_; }       ///< 8/16 (A29 Q): 1 = 8-Bit-Mode
    bool     reset16() const     { return reset16_; }     ///< PIO B4 (Pull-up R4:2)
    bool     ramEnabled() const  { return ramen_; }       ///< RAMEN = ¬/RAMEN (PIO B2)
    bool     trq8() const        { return trq8_; }        ///< TRQ8 = ¬/TRQ8 (PIO B5)
    bool     tren() const        { return tren_; }        ///< TREN = ¬µ0 (µ0 nur nach MSET/MREQ aktiv)
    uint8_t  segment() const     { return seg_; }         ///< SG1P:SG0P (PIO B1:B0)
    bool     parityError() const { return per_ff_; }
    bool     ledV1() const       { return ramen_; }       ///< V1 an A17/08 ← RAMEN (A31/10)
    bool     ledV2() const       { return mode8_; }       ///< V2 an A29 /Q: leuchtet im 8-Bit-Mode
    /// Gespeicherter (= geschriebener) Wert der Seite @p page; F0..F3 sind invertiert.
    uint8_t  attribute(int page) const { return attr_[page & 0x0F]; }
    uint8_t  status8() const     { return status8_; }     ///< A36 (→ U8001 AD8–15)
    uint8_t  vector8() const     { return vector8_; }     ///< A34 (→ U8001 AD0–7)
    bool     viPending() const   { return vi_pending_; }  ///< A34 INT aktiv (PIO A3 = 0)
    uint8_t  steuer16() const    { return a33_; }         ///< A33 (vom U8001, RESET16 löscht)
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
    /// Pins aus dem PIO-Ausgang B ableiten (ohne Rückwirkung auf die CPU).
    void pinsFromPio();
    /// RESET16/µI/STOP/BUSRQ an den U8001 legen.
    void pinsToCpu();
    // U8001-Busrückrufe
    uint16_t read16(const Z8kBusCycle& c);
    void     write16(const Z8kBusCycle& c, uint16_t v);
    void     stapelZyklus();
    // Debug
    void emit(Ereignis e, uint16_t addr, uint16_t value, bool by16) {
        if (on_event_) on_event_(EreignisInfo{e, addr, value, by16});
    }
    uint8_t pegelSignatur() const;
    void    pegelMelden();
    std::function<void(const EreignisInfo&)> on_event_;
    std::function<bool(const Z8000&)>        on_step_;
    uint8_t dbg_prev_ = 0;

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
    bool    stop_     = false;  ///< STOP am U8001 (PIO B3 = /STOP = 0)
    // Betriebsartensteuerung
    bool    mode8_    = true;   ///< FF A29
    bool    tren_     = false;  ///< ¬µ0; µ0 setzt nur MSET des U8001
    bool    busak_    = false;  ///< BUSAK' vom U8001
    bool    m1_setzt_a29_ = false;  ///< /S und /R inaktiv ⇒ nächster M1 setzt A29
    // Register
    uint8_t a33_      = 0;
    uint8_t status8_  = 0;
    uint8_t vector8_  = 0;
    bool    vi_pending_ = false;
    uint8_t status16_ = 0;      ///< nach Netz-Ein unbestimmt
    bool    per_ff_   = false;

    uint8_t pio_a_in_ = 0xFF;
    uint8_t pio_b_in_ = 0xFF;

    // 16-Bit-Seite
    Z8000   u8k_;
    int64_t guthaben_ = 0;      ///< Zeitguthaben des U8001 in (U880-Takt × f16) − (U8001-Takt × f8)
    uint8_t a53_      = 11;     ///< Einzelbefehlszähler
    /// Selbsthaltekreis A54 (zwei NOR 7402): A33 Bit 3 = 1 gibt frei (Q10 = H ⇒ LOAD
    /// inaktiv, A53 zählt jeden Stapelzugriff), die NVI-Quittung lädt A53 und hält ihn
    /// geladen (Q10 = L), sofern Bit 3 dann 0 ist.  RESET16 wirkt auf beide nicht.
    bool    a54_zaehlt_ = false;
};

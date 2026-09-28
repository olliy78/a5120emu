/**
 * @file em.cpp
 * @brief Erweiterungsmodul EM064/EM256 — 8-Bit-Seite (U880-Sicht), ohne U8001.
 *
 * Belege: doc/design/17_a5120_16.md §7 (Scans 062-9005/9000), hier in S1 ergänzt um
 * die Quelle von /MEMDI (MEN) und die Leseadresse von A22 (em.h).
 */
#include "em.h"
#include <algorithm>
#include "core/logger.h"

namespace {
// PIO A32 Port A (alles Eingänge)
constexpr uint8_t kPaNVi   = 1u << 3;   ///< /VI  ← A34 INT
constexpr uint8_t kPaInt16 = 1u << 4;   ///< INT-16 ← A33 Bit 4
constexpr uint8_t kPaNS    = 1u << 5;   ///< N/S des U8001
constexpr uint8_t kPaM816  = 1u << 6;   ///< 8/16 (A29 Q)
constexpr uint8_t kPaTren  = 1u << 7;   ///< TREN = ¬µ0
// PIO A32 Port B
constexpr uint8_t kPbNRamen = 1u << 2;  ///< /RAMEN (Pull-up R4:1)
constexpr uint8_t kPbReset  = 1u << 4;  ///< RESET16 (Pull-up R4:2)
constexpr uint8_t kPbNTrq8  = 1u << 5;  ///< /TRQ8 (ohne Pull-up; offener TTL-Eingang = H)
constexpr uint8_t kPbPr     = 1u << 6;  ///< PR (Pull-up R4:4)
constexpr uint8_t kPbNPe    = 1u << 7;  ///< /PE (Eingang)
// Attributspeicher, gespeicherte Bits (Ausgänge F0..F3 des 74189 sind invertiert)
constexpr uint8_t kAttrNPen = 1u << 0;  ///< 0 = Seite frei (PEN = 1)
constexpr uint8_t kAttrNWe  = 1u << 1;  ///< 0 = beschreibbar (WE = 1)
constexpr uint8_t kAttrNA14 = 1u << 2;  ///< /A14-8
constexpr uint8_t kAttrNA15 = 1u << 3;  ///< /A15-8
}  // namespace

EM::EM(K1520Bus& bus) : EM(bus, Config{}) {}

EM::EM(K1520Bus& bus, const Config& cfg)
    : bus_(bus), cfg_(cfg),
      dram_(cfg.variante == Variante::EM256 ? 256u * 1024u : 64u * 1024u, 0xFF)
{
    attr_.fill(0x0F);
    reset();
}

void EM::attachToBus() {
    bus_.registerIO(this, cfg_.modadr, 8);
    bus_.addMemdiDriver(this);
    LOG_INFO("EM", "%s an MODADR %02XH–%02XH",
             cfg_.variante == Variante::EM256 ? "EM256" : "EM064",
             cfg_.modadr, cfg_.modadr + 7);
}

void EM::powerOn() {
    // DRAM wie der K3526 als 0xFF (unbestimmt).  A22 nach Netz-Ein ebenfalls
    // unbestimmt — 1111 heisst „Seite gesperrt, schreibgeschützt"; das ist nur
    // bequem, nicht belegt: bei /RAMEN = 1 (Pull-up) blendet ohnehin keine Seite ein.
    std::fill(dram_.begin(), dram_.end(), 0xFF);
    attr_.fill(0x0F);
    status16_ = 0;
    reset();
}

void EM::reset() {
    pio_.reset();          // beide Tore Eingabe ⇒ Ausgänge hochohmig
    pio_a_in_ = pio_b_in_ = 0xFF;
    per_ff_ = false;
    updateFromPio();
}

// ─── E/A-Decoder ─────────────────────────────────────────────────────────────
// MODADR+0 A-Daten, +1 B-Daten, +2 A-Steuer, +3 B-Steuer (Handbuch §1.7.1; B/A = AB0,
// C/D = AB1) — Z80PIO zählt 0 A-Daten, 1 A-Steuer, 2 B-Daten, 3 B-Steuer.
static inline uint8_t pioIndex(uint8_t rel) { return uint8_t(((rel & 1) << 1) | ((rel >> 1) & 1)); }

uint8_t EM::ioRead(uint8_t port) {
    const uint8_t rel = uint8_t(port - cfg_.modadr);
    switch (rel) {
        case 0: case 1: case 2: case 3: return pio_.ioRead(pioIndex(rel));
        case 6: return status16_;          // A35 über Treiber A11
        case 7: return readA22();
        default: return 0xFF;              // ACH/ADH sind nur beschreibbar
    }
}

void EM::ioWrite(uint8_t port, uint8_t data) {
    const uint8_t rel = uint8_t(port - cfg_.modadr);
    switch (rel) {
        case 0: case 1: case 2: case 3:
            pio_.ioWrite(pioIndex(rel), data);
            updateFromPio();
            break;
        case 4:                            // STB STATUS-8 'AC' → A36
            status8_ = data;
            break;
        case 5:                            // STB VEKTOR 'AD' → A34, INT → U8001 VI + PIO A3
            // A34 (8212) hängt mit A33 an X15/RESET OUT: solange RESET16 anliegt, ist
            // das Register gelöscht und die Anforderung abgeschaltet.
            if (!reset16_) {
                vector8_    = data;
                vi_pending_ = true;
                updatePioInputs();
            }
            break;
        case 7:
            writeA22(data);
            break;
        default:                           // AEH: nur lesbar
            break;
    }
}

// ─── Attributspeicher A22 ────────────────────────────────────────────────────
void EM::writeA22(uint8_t data) {
    // Adresse = AB12–15 des E/A-Zyklus (A13 offen, weil kein /MREQ).  WÄCHTER:
    // `OUT (n),A` legt A auf AB8–15 — dann ist die Seite A>>4, nicht irgendein B.
    const uint8_t page = uint8_t((bus_.ioAddress() >> 12) & 0x0F);
    attr_[page] = data & 0x0F;
    rebuildMap();
    LOG_DEBUG("EM", "A22[%X] <- %X (PEN=%d WE=%d A15/A14-8=%d%d)", page, data & 0x0F,
              !(data & kAttrNPen), !(data & kAttrNWe), !(data & kAttrNA15), !(data & kAttrNA14));
}

uint8_t EM::readA22() const {
    const uint8_t page = cfg_.lesart == A22Lesart::ZyklusAdresse
                             ? uint8_t((bus_.ioAddress() >> 12) & 0x0F)
                             : last_mem_page_;
    // F0..F3 des 74189 sind die invertierten gespeicherten Bits; A12 (8216) legt sie
    // nicht invertierend auf DB0–3.  DB4–7 treibt niemand (offener Bus = H).
    return uint8_t(0xF0 | (~attr_[page] & 0x0F));
}

// ─── Speicherzugriff des U880 ────────────────────────────────────────────────
bool EM::drivesMemdi(uint16_t addr) {
    last_mem_page_ = uint8_t(addr >> 12);
    return (men_mask_ >> (addr >> 12)) & 1;
}

uint8_t EM::memRead(uint16_t addr) {
    return dram_[cellFor(addr)];
}

void EM::memWrite(uint16_t addr, uint8_t data) {
    // WE = 0 unterdrückt nur WRI (A26/06); der Zyklus selbst gehört trotzdem der Karte
    // (MEMDI), der K1520-RAM wird also auch nicht beschrieben.
    if ((we_mask_ >> (addr >> 12)) & 1)
        dram_[cellFor(addr)] = data;
}

void EM::rebuildMap() {
    const bool on = mode8_ && ramen_;          // MEN = ¬(8/16 ∧ RAMEN ∧ PEN)
    men_mask_ = 0;
    we_mask_  = 0;
    for (int p = 0; p < 16; ++p) {
        const uint8_t a = attr_[p];
        uint32_t base = (a & kAttrNA15 ? 0u : 0x8000u) | (a & kAttrNA14 ? 0u : 0x4000u);
        if (cfg_.variante == Variante::EM256)
            base |= uint32_t(seg_) << 16;      // SG1·128K + SG0·64K
        page_base_[p] = base;
        if (on && !(a & kAttrNPen)) men_mask_ |= uint16_t(1u << p);
        if (!(a & kAttrNWe))        we_mask_  |= uint16_t(1u << p);
    }
}

// ─── PIO-Ausgänge → Betriebsartensteuerung ───────────────────────────────────
void EM::updateFromPio() {
    const auto st = pio_.debugState().port[1];
    const uint8_t drive = st.mode == 0 ? 0xFF : st.mode == 3 ? uint8_t(~st.dir) : 0x00;
    // Wo die PIO nicht treibt: Pull-ups R4 bzw. offene TTL-Eingänge ⇒ H.
    const uint8_t pins = uint8_t((st.out & drive) | ~drive);
    seg_     = pins & 0x03;
    ramen_   = !(pins & kPbNRamen);
    reset16_ = (pins & kPbReset) != 0;
    trq8_    = !(pins & kPbNTrq8);
    pr_      = (pins & kPbPr) != 0;
    if (pr_) per_ff_ = false;                  // PR hält das Paritäts-FF zurückgesetzt
    updateMode();
}

void EM::updateMode() {
    if (reset16_) {
        // X15: RESET OUT löscht A33 und A34; U8001 im Reset ⇒ µ0 = H (Zilog §9.7).
        a33_ = 0;
        vector8_ = 0;
        vi_pending_ = false;
        tren_  = false;
        busak_ = false;
    }
    // FF A29 (§7.4): /S = ¬RESET16, /R = ¬(¬(TREN ∧ BUSAK' ∧ TRQ8) ∧ ¬RESET16).
    // Takt = M1 des U880 bei D = H — braucht TREN ∧ BUSAK', also den U8001 (S4).
    const bool s_n = !reset16_;
    const bool r_n = !(!(tren_ && busak_ && trq8_) && !reset16_);
    if (!s_n)      mode8_ = true;              // /S hat bei /S = /R = 0 beim 7474 Q = H
    else if (!r_n) mode8_ = false;
    rebuildMap();
    updatePioInputs();
}

void EM::updatePioInputs() {
    uint8_t a = uint8_t(a33_ & 0x07);
    if (!vi_pending_)   a |= kPaNVi;
    if (a33_ & 0x10)    a |= kPaInt16;
    if (cfg_.ns_im_reset) a |= kPaNS;          // U8001 läuft in S1 nie: Wert aus dem Reset
    if (mode8_)         a |= kPaM816;
    if (tren_)          a |= kPaTren;
    const uint8_t b = uint8_t(0x7F | (per_ff_ ? 0 : kPbNPe));
    if (a != pio_a_in_) { pio_a_in_ = a; pio_.portAWrite(a); bus_.markIntDirty(); }
    if (b != pio_b_in_) { pio_b_in_ = b; pio_.portBWrite(b); bus_.markIntDirty(); }
}

void EM::injectParityError() {
    if (pr_) return;
    per_ff_ = true;
    updatePioInputs();
}

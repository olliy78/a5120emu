/**
 * @file em.cpp
 * @brief Erweiterungsmodul EM064/EM256 — U880-Seite (S1) und U8001-Seite (S4).
 *
 * Belege: doc/design/17_a5120_16.md §7 (Scans 062-9005/9000), hier in S1 ergänzt um
 * die Quelle von /MEMDI (MEN) und die Leseadresse von A22 (em.h).
 */
#include "em.h"
#include <algorithm>
#include <cstring>
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
constexpr uint8_t kPbNStop  = 1u << 3;  ///< /STOP (direkt an U8001 STOP)
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

/// Vorlast von A53: A3 fest H (9005/1), A0..A2 über X12.
static uint8_t a53Vorlast(const EM::Config& cfg) { return uint8_t((cfg.a53_vorlast & 0x0F) | 0x08); }

static Z8kConfig z8kConfigFor(const EM::Config& cfg) {
    Z8kConfig z;
    // EM256: U8001 (segmentiert); EM064: U8002 (Handbuch §1.2, Plan §6).
    z.model = cfg.variante == EM::Variante::EM256 ? Z8kModel::Z8001 : Z8kModel::Z8002;
    return z;
}

EM::EM(K1520Bus& bus, const Config& cfg)
    : bus_(bus), cfg_(cfg),
      dram_(cfg.variante == Variante::EM256 ? 256u * 1024u : 64u * 1024u, 0xFF),
      u8k_(z8kConfigFor(cfg))
{
    attr_.fill(0x0F);
    a53_ = a53Vorlast(cfg_);
    u8k_.read     = [this](const Z8kBusCycle& c) { return read16(c); };
    u8k_.write    = [this](const Z8kBusCycle& c, uint16_t v) { write16(c, v); };
    u8k_.onMO     = [this](bool) { updateMode(); };
    u8k_.onBusAck = [this](bool) { updateMode(); };
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
    // A53/A54 nach Netz-Ein unbestimmt.  Gewählt: geladen und gehalten — so zeigt es
    // die Messung (em16abl 2026-09-29: ohne A33 Bit 3 kein NVI, danach Vorlast 11).
    a53_ = a53Vorlast(cfg_);
    a54_zaehlt_ = false;
    u8k_.setNVI(nviLine());
    reset();
}

void EM::reset() {
    pio_.reset();          // beide Tore Eingabe ⇒ Ausgänge hochohmig
    pio_a_in_ = pio_b_in_ = 0xFF;
    per_ff_ = false;
    guthaben_ = 0;
    updateFromPio();
}

// ─── E/A-Decoder ─────────────────────────────────────────────────────────────
// MODADR+0 A-Daten, +1 B-Daten, +2 A-Steuer, +3 B-Steuer (Handbuch §1.7.1; B/A = AB0,
// C/D = AB1) — Z80PIO zählt 0 A-Daten, 1 A-Steuer, 2 B-Daten, 3 B-Steuer.
static inline uint8_t pioIndex(uint8_t rel) { return uint8_t(((rel & 1) << 1) | ((rel >> 1) & 1)); }

uint8_t EM::ioRead(uint8_t port) {
    const uint8_t rel = uint8_t(port - cfg_.modadr);
    uint8_t v = 0xFF;                      // ACH/ADH sind nur beschreibbar
    switch (rel) {
        case 0: case 1: case 2: case 3:
            v = pio_.ioRead(pioIndex(rel));
            emit(Ereignis::PioLesen, rel, v, false);
            break;
        case 6:                            // A35 über Treiber A11
            v = status16_;
            emit(Ereignis::Status16Lesen, port, v, false);
            break;
        case 7:
            v = readA22();
            emit(Ereignis::A22Lesen, uint16_t((bus_.ioAddress() >> 12) & 0x0F), v, false);
            break;
        default:
            break;
    }
    return v;
}

void EM::ioWrite(uint8_t port, uint8_t data) {
    const uint8_t rel = uint8_t(port - cfg_.modadr);
    switch (rel) {
        case 0: case 1: case 2: case 3:
            emit(Ereignis::PioSchreiben, rel, data, false);
            pio_.ioWrite(pioIndex(rel), data);
            updateFromPio();
            break;
        case 4:                            // STB STATUS-8 'AC' → A36
            status8_ = data;
            emit(Ereignis::Status8, port, data, false);
            break;
        case 5:                            // STB VEKTOR 'AD' → A34, INT → U8001 VI + PIO A3
            // A34 (8212) hängt mit A33 an X15/RESET OUT: solange RESET16 anliegt, ist
            // das Register gelöscht und die Anforderung abgeschaltet.
            emit(Ereignis::Vektor8, port, data, false);
            if (!reset16_) {
                vector8_    = data;
                vi_pending_ = true;
                u8k_.setVI(true);              // Netz 28 → VI (pegelgetriggert)
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
    emit(Ereignis::A22Schreiben, page, uint16_t(data & 0x0F), false);
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
    const uint8_t v = dram_[cellFor(addr)];
    if (on_access_) {
        Zugriff z; z.cell = cellFor(addr); z.value = v; z.read = true; z.addr8 = addr;
        meldeZugriff(z);
    }
    return v;
}

void EM::memWrite(uint16_t addr, uint8_t data) {
    // WE = 0 unterdrückt nur WRI (A26/06); der Zyklus selbst gehört trotzdem der Karte
    // (MEMDI), der K1520-RAM wird also auch nicht beschrieben.
    const bool we = (we_mask_ >> (addr >> 12)) & 1;
    if (we) dram_[cellFor(addr)] = data;
    if (on_access_) {
        Zugriff z; z.cell = cellFor(addr); z.value = data; z.read = false; z.addr8 = addr;
        z.wirksam = we;
        meldeZugriff(z);
    }
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
void EM::pinsFromPio() {
    const auto st = pio_.debugState().port[1];
    const uint8_t drive = st.mode == 0 ? 0xFF : st.mode == 3 ? uint8_t(~st.dir) : 0x00;
    // Wo die PIO nicht treibt: Pull-ups R4 bzw. offene TTL-Eingänge ⇒ H.
    const uint8_t pins = uint8_t((st.out & drive) | ~drive);
    seg_     = pins & 0x03;
    ramen_   = !(pins & kPbNRamen);
    stop_    = !(pins & kPbNStop);
    reset16_ = (pins & kPbReset) != 0;
    trq8_    = !(pins & kPbNTrq8);
    pr_      = (pins & kPbPr) != 0;
    if (pr_) per_ff_ = false;                  // PR hält das Paritäts-FF zurückgesetzt
}

void EM::pinsToCpu() {
    u8k_.setResetLine(reset16_);               // /RES = ¬RESET16 (A31, A55)
    u8k_.setMI(trq8_);                         // µI = /TRQ8 (Q12), aktiv = L
    u8k_.setStop(stop_);                       // STOP = PIO B3
}

void EM::updateFromPio() {
    pinsFromPio();
    pinsToCpu();
    updateMode();
}

void EM::updateMode() {
    if (reset16_) {
        // X15: RESET OUT löscht A33 und A34; U8001 im Reset ⇒ µ0 = H (Zilog §9.7).
        a33_ = 0;
        vector8_ = 0;
        vi_pending_ = false;
        u8k_.setVI(false);
        // A53 (CLR an Masse) und A54 hängen NICHT an RESET16 (9005/1, Abb. 8): Zähler-
        // stand und Selbsthaltung überstehen ihn; nur A33 Bit 3 fällt mit A33.
    }
    // TREN = ¬µ0 (A38): µ0 ist aktiv (L) nur nach MSET/MREQ des U8001; im Reset H.
    tren_  = !reset16_ && !u8k_.inReset() && u8k_.moActive();
    busak_ = !reset16_ && u8k_.busAck();
    u8k_.setBusReq(tren_ && trq8_);            // /BUSRQ = ¬(TREN ∧ TRQ8), A212/11 → Q11
    // FF A29 (§7.4): /S = ¬RESET16, /R = ¬(¬(TREN ∧ BUSAK' ∧ TRQ8) ∧ ¬RESET16),
    // D = H, C = M1 des U880 (onU880M1).
    const bool s_n = !reset16_;
    const bool r_n = !(!(tren_ && busak_ && trq8_) && !reset16_);
    if (!s_n)      mode8_ = true;              // /S hat bei /S = /R = 0 beim 7474 Q = H
    else if (!r_n) mode8_ = false;
    m1_setzt_a29_ = s_n && r_n;
    rebuildMap();
    updatePioInputs();
}

void EM::updatePioInputs() {
    uint8_t a = uint8_t(a33_ & 0x07);
    if (!vi_pending_)   a |= kPaNVi;
    if (a33_ & 0x10)    a |= kPaInt16;
    // N/S = H im Normalmodus.  Im Reset ist der Pin laut Zilog §9.7 undefiniert.
    const bool normal = u8k_.inReset() ? cfg_.ns_im_reset : !u8k_.systemMode();
    if (normal)         a |= kPaNS;
    if (mode8_)         a |= kPaM816;
    if (tren_)          a |= kPaTren;
    const uint8_t b = uint8_t(0x7F | (per_ff_ ? 0 : kPbNPe));
    if (a != pio_a_in_) { pio_a_in_ = a; pio_.portAWrite(a); bus_.markIntDirty(); }
    if (b != pio_b_in_) { pio_b_in_ = b; pio_.portBWrite(b); bus_.markIntDirty(); }
    if (on_event_) pegelMelden();
}

// ─── Debug: Pegelwechsel ─────────────────────────────────────────────────────
uint8_t EM::pegelSignatur() const {
    return uint8_t((mode8_ ? 0x01 : 0) | ((a33_ & 0x10) ? 0x02 : 0) | (tren_ ? 0x04 : 0) |
                   (busRq16() ? 0x08 : 0) | (busak_ ? 0x10 : 0) | (reset16_ ? 0x20 : 0) |
                   (nviLine() ? 0x40 : 0) | (stop_ ? 0x80 : 0));
}

void EM::pegelMelden() {
    const uint8_t neu = pegelSignatur();
    const uint8_t diff = uint8_t(neu ^ dbg_prev_);
    if (!diff) return;
    dbg_prev_ = neu;
    static const Ereignis k[8] = {Ereignis::Modus, Ereignis::Int16, Ereignis::Tren,
                                  Ereignis::BusRq, Ereignis::BusAk, Ereignis::Reset16,
                                  Ereignis::Nvi,   Ereignis::Stop};
    for (int i = 0; i < 8; ++i)
        if (diff & (1u << i)) emit(k[i], 0, (neu >> i) & 1u, false);
}

const char* EM::ereignisName(Ereignis e) {
    switch (e) {
        case Ereignis::PioSchreiben:  return "PIO-OUT";
        case Ereignis::PioLesen:      return "PIO-IN";
        case Ereignis::Status8:       return "A36-Status8";
        case Ereignis::Vektor8:       return "A34-Vektor8";
        case Ereignis::Status16Lesen: return "A35-Status16-IN";
        case Ereignis::A22Schreiben:  return "A22-OUT";
        case Ereignis::A22Lesen:      return "A22-IN";
        case Ereignis::A33A35:        return "U8001-OUT-A33/A35";
        case Ereignis::Status8Lesen:  return "U8001-IN-Status8";
        case Ereignis::ViQuittung:    return "VI-Quittung";
        case Ereignis::NviQuittung:   return "NVI-Quittung";
        case Ereignis::NmiQuittung:   return "NMI-Quittung";
        case Ereignis::Modus:         return "Moduswechsel";
        case Ereignis::Int16:         return "INT-16";
        case Ereignis::Tren:          return "TREN";
        case Ereignis::BusRq:         return "BUSRQ16";
        case Ereignis::BusAk:         return "BUSAK16";
        case Ereignis::Reset16:       return "RESET16";
        case Ereignis::Nvi:           return "NVI";
        case Ereignis::Stop:          return "STOP16";
    }
    return "?";
}

// ─── U8001: Zeit ─────────────────────────────────────────────────────────────
void EM::advance(int u880Takte) {
    guthaben_ += int64_t(u880Takte) * cfg_.takt_u8001_hz;
    const int64_t f8 = cfg_.takt_u880_hz;
    while (guthaben_ > 0) {
        // Geparkt: im Reset, Bus abgegeben (BUSAK bei anliegendem BUSRQ) oder im Stop.
        // Bis der U880 an der Karte etwas ändert, geschieht dort nichts — die Zeit
        // wird nur verbucht, statt sie in 1-Takt-Schritten abzuzählen.
        if (reset16_ || (u8k_.busAck() && tren_ && trq8_) || (u8k_.stopped() && stop_)) {
            const int64_t c = (guthaben_ + f8 - 1) / f8;
            u8k_.cycles += uint64_t(c);
            guthaben_ -= c * f8;
            break;
        }
        if (on_step_ && on_step_(u8k_)) break;   // Debugger: Halt VOR dem Schritt
        im_schritt16_ = true;                    // Ereignisse jetzt: vom U8001 ausgelöst
        int c = u8k_.step();
        im_schritt16_ = false;
        if (c <= 0) c = 1;
        guthaben_ -= int64_t(c) * f8;
    }
    updatePioInputs();                         // N/S kann sich geändert haben
}

// ─── U8001: Segmentweiche A42 + Umschalter A41 ───────────────────────────────
uint32_t EM::cellFor16(const Z8kBusCycle& c) const {
    uint32_t sg = 0;
    switch (segMode()) {
        case 0:  sg = (a33_ >> 5) & 3u;                              // SG0 = AD5*, SG1 = AD6*
                 break;
        case 1:  sg = (c.isInstructionFetch() ? 1u : 0u)              // SG0 = INSTR (Status 12/13)
                    | (c.system ? 0u : 2u);                           // SG1 = N/S (H = Normal)
                 break;
        default: sg = c.seg & 3u;                                    // SG0/SG1 = SN0/SN1
                 break;
    }
    // EM064: SG0/SG1 gehen nicht in die Matrix ⇒ Modulo 64 KB spiegelt.
    return ((sg << 16) | c.addr) % uint32_t(dram_.size());
}

void EM::stapelZyklus() {
    // A53 (74193) zählt rückwärts an T2 = ¬(DS · STATUS 9) — jeden Stapelzugriff,
    // Lesen wie Schreiben (Handbuch Abb. 8).  Gesperrt ist er nur über LOAD (A54/10):
    // nach einer NVI-Quittung bleibt er geladen, bis A33 Bit 3 A54 umlegt.
    if (!a54_zaehlt_) return;
    a53_ = uint8_t((a53_ - 1) & 0x0F);
    // NVI = ¬QD als Pegel: fällt QD (Stand 7), liegt NVI an; zählt der Zähler bis
    // 0 → 15 weiter (NVI gesperrt), lässt er es von selbst wieder los.
    u8k_.setNVI(nviLine());
    if (on_event_) pegelMelden();              // NVI-Flanke dem auslösenden Zyklus zuordnen
}

// ─── U8001: Buszyklen ────────────────────────────────────────────────────────
uint16_t EM::read16(const Z8kBusCycle& c) {
    if (c.isMemory()) {
        if (c.st == Z8kStatus::MemStack) stapelZyklus();
        const uint32_t z = cellFor16(c) & ~1u;           // gerade Adresse = oberes Byte
        const uint16_t w = uint16_t((dram_[z] << 8) | dram_[z + 1]);
        if (on_access_) {
            Zugriff a; a.by16 = true; a.read = true; a.cycle = c; a.word = c.word;
            a.cell  = c.word ? z : z + (c.addr & 1u);
            a.value = c.word ? w : uint16_t((c.addr & 1u) ? (w & 0xFF) : (w >> 8));
            meldeZugriff(a);
        }
        return w;
    }
    if (on_access_ && (c.st == Z8kStatus::Io || c.st == Z8kStatus::SpecialIo)) {
        const uint16_t v = read16Io(c);
        Zugriff a; a.by16 = true; a.io = true; a.read = true; a.cycle = c; a.word = c.word;
        a.value = c.word ? v : uint16_t((c.addr & 1u) ? (v & 0xFF) : (v >> 8));
        meldeZugriff(a);
        return v;
    }
    return read16Io(c);
}

uint16_t EM::read16Io(const Z8kBusCycle& c) {
    switch (c.st) {
        case Z8kStatus::Io:
            // /READ STATUS (A48 · A47): nur AD7 dekodiert, Status-8 auf AD8–15.  Die
            // andere Hälfte treibt niemand, und Pull-ups hat AD auf 9005/1 nicht: sie
            // behält, was der U8001 in der Adressphase darauf legte (Buskapazität) —
            // die Portadresse.  Am Gerät 2026-09-29: `INB RL0,%0081` = 81H (G5).  Ohne
            // Bit 7 treibt keiner etwas, der Wert ist die ganze Adresse.
            if (c.addr & 0x80) {
                emit(Ereignis::Status8Lesen, c.addr, status8_, true);
                return uint16_t((status8_ << 8) | (c.addr & 0x00FF));
            }
            return c.addr;
        case Z8kStatus::ViAck: {
            // READ VEKTOR: A34 auf AD0–7, Status-8 auf AD8–15; löscht INT von A34.
            const uint16_t id = uint16_t((status8_ << 8) | vector8_);
            vi_pending_ = false;
            u8k_.setVI(false);
            emit(Ereignis::ViQuittung, 0, id, true);
            updatePioInputs();
            return id;
        }
        case Z8kStatus::NviAck:
            // DS · R/W · Status 6 an A54/08 legt Q10 (= LOAD von A53) auf L: A53 lädt
            // die Vorlast (QD = H ⇒ NVI weg) und BLEIBT geladen.  Steht A33 Bit 3 noch
            // auf 1, hält es A54/13 auf L und Q10 kehrt nach dem Impuls auf H zurück —
            // der Zähler läuft weiter (Handbuch §1.12: Bit 3 vor dem RET rücksetzen).
            a53_ = a53Vorlast(cfg_);
            a54_zaehlt_ = (a33_ & 0x08) != 0;
            u8k_.setNVI(nviLine());
            emit(Ereignis::NviQuittung, 0, a53_, true);
            if (on_event_) pegelMelden();
            return 0xFFFF;
        case Z8kStatus::NmiAck:
            // /PR = ¬PR · /NMI-ACK (9005/1: A45 Y5 → A38/08 → A212/06 → A48/04; A48/06
            // → X3 A13 → Setzeingang A46/10 auf 9000/1) — die Quittung löscht den
            // Paritätsfehler wie PR (Handbuch §1.3, G5).  Kennung: niemand treibt AD,
            // und eine Adresse legt der U8001 dabei nicht an (Zilog §9.4.5) ⇒ FFFFH
            // bleibt eine Annahme.
            per_ff_ = false;
            emit(Ereignis::NmiQuittung, 0, 0, true);
            updatePioInputs();
            return 0xFFFF;
        case Z8kStatus::SpecialIo:
            return c.addr;                             // nicht dekodiert: Adresse bleibt auf AD
        default:
            return 0xFFFF;
    }
}

void EM::write16(const Z8kBusCycle& c, uint16_t v) {
    if (c.isMemory()) {
        if (c.st == Z8kStatus::MemStack) stapelZyklus();
        const uint32_t z = cellFor16(c) & ~1u;
        if (c.word) {
            dram_[z]     = uint8_t(v >> 8);
            dram_[z + 1] = uint8_t(v);
        } else if (c.addr & 1) {
            dram_[z + 1] = uint8_t(v);                  // ungerade = AD0–7
        } else {
            dram_[z] = uint8_t(v >> 8);                 // gerade = AD8–15
        }
        if (on_access_) {
            Zugriff a; a.by16 = true; a.read = false; a.cycle = c; a.word = c.word;
            a.cell  = c.word ? z : z + (c.addr & 1u);
            a.value = c.word ? v : uint16_t((c.addr & 1u) ? (v & 0xFF) : (v >> 8));
            meldeZugriff(a);
        }
        return;
    }
    if (on_access_ && (c.st == Z8kStatus::Io || c.st == Z8kStatus::SpecialIo)) {
        Zugriff a; a.by16 = true; a.io = true; a.read = false; a.cycle = c; a.word = c.word;
        a.value = c.word ? v : uint16_t((c.addr & 1u) ? (v & 0xFF) : (v >> 8));
        meldeZugriff(a);
    }
    if (c.st == Z8kStatus::Io && (c.addr & 0x80)) {
        // STB A33 + A35 = Status 2 · DS · Schreiben · AD7 (X13 3–4): BEIDE Register.
        a33_      = uint8_t(v);
        status16_ = uint8_t(v >> 8);
        // A33/10 (Bit 3) an A54/12: gibt A53 frei (Selbsthaltung bis zur NVI-Quittung).
        if (a33_ & 0x08) a54_zaehlt_ = true;
        LOG_DEBUG("EM", "U8001 OUT %04X: A33=%02X A35=%02X", c.addr, a33_, status16_);
        emit(Ereignis::A33A35, c.addr, uint16_t((status16_ << 8) | a33_), true);
        updatePioInputs();
    }
}

// ─── Save-State ──────────────────────────────────────────────────────────────
namespace {
constexpr uint8_t kEmStateVersion = 1;
template <class T> void put(std::vector<uint8_t>& o, const T& v) {
    const auto* b = reinterpret_cast<const uint8_t*>(&v);
    o.insert(o.end(), b, b + sizeof(T));
}
template <class T> bool get(const uint8_t*& p, const uint8_t* end, T& v) {
    if (size_t(end - p) < sizeof(T)) return false;
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return true;
}
}  // namespace

void EM::serialize(std::vector<uint8_t>& o) const {
    put(o, kEmStateVersion);
    put(o, uint8_t(cfg_.variante));
    put(o, uint32_t(dram_.size()));
    o.insert(o.end(), dram_.begin(), dram_.end());
    o.insert(o.end(), attr_.begin(), attr_.end());
    pio_.serialize(o);
    const uint8_t f[] = {mode8_, a33_, status8_, vector8_, vi_pending_, status16_,
                         per_ff_, last_mem_page_, pio_a_in_, pio_b_in_, a53_, a54_zaehlt_};
    o.insert(o.end(), std::begin(f), std::end(f));
    put(o, guthaben_);
    // U8001: Register + Ablaufzustand (POD, derselbe Bau liest es wieder).
    for (uint16_t r : u8k_.Rg) put(o, r);
    put(o, u8k_.R14[0]); put(o, u8k_.R14[1]);
    put(o, u8k_.R15[0]); put(o, u8k_.R15[1]);
    put(o, u8k_.fcw); put(o, u8k_.pc); put(o, u8k_.pcSeg);
    put(o, u8k_.psapSeg); put(o, u8k_.psapOff); put(o, u8k_.refresh); put(o, u8k_.cycles);
    put(o, u8k_.runState());
}

bool EM::deserialize(const uint8_t*& p, const uint8_t* end) {
    const uint8_t* q = p;
    uint8_t ver = 0, var = 0;
    uint32_t n = 0;
    if (!get(q, end, ver) || ver != kEmStateVersion) return false;
    if (!get(q, end, var) || var != uint8_t(cfg_.variante)) return false;
    if (!get(q, end, n) || n != dram_.size() || size_t(end - q) < n + attr_.size()) return false;
    const uint8_t* dram = q;
    q += n;
    const uint8_t* attr = q;
    q += attr_.size();
    Z80PIO pio("tmp");
    if (!pio.deserialize(q, end)) return false;
    uint8_t f[12];
    if (size_t(end - q) < sizeof f) return false;
    std::memcpy(f, q, sizeof f);
    q += sizeof f;
    int64_t guthaben = 0;
    Z8000 z;                                     // Zwischenablage für die Register
    if (!get(q, end, guthaben)) return false;
    for (uint16_t& r : z.Rg) if (!get(q, end, r)) return false;
    Z8kRunState rs;
    if (!get(q, end, z.R14[0]) || !get(q, end, z.R14[1]) || !get(q, end, z.R15[0]) ||
        !get(q, end, z.R15[1]) || !get(q, end, z.fcw) || !get(q, end, z.pc) ||
        !get(q, end, z.pcSeg) || !get(q, end, z.psapSeg) || !get(q, end, z.psapOff) ||
        !get(q, end, z.refresh) || !get(q, end, z.cycles) || !get(q, end, rs))
        return false;

    // Alles gelesen — jetzt übernehmen.
    std::memcpy(dram_.data(), dram, n);
    std::memcpy(attr_.data(), attr, attr_.size());
    const uint8_t* pp = p + 1 + 1 + 4 + n + attr_.size();
    pio_.deserialize(pp, end);
    mode8_ = f[0]; a33_ = f[1]; status8_ = f[2]; vector8_ = f[3]; vi_pending_ = f[4];
    status16_ = f[5]; per_ff_ = f[6]; last_mem_page_ = f[7]; pio_a_in_ = f[8];
    pio_b_in_ = f[9]; a53_ = f[10]; a54_zaehlt_ = f[11];
    guthaben_ = guthaben;
    std::copy(std::begin(z.Rg), std::end(z.Rg), std::begin(u8k_.Rg));
    u8k_.R14[0] = z.R14[0]; u8k_.R14[1] = z.R14[1];
    u8k_.R15[0] = z.R15[0]; u8k_.R15[1] = z.R15[1];
    u8k_.fcw = z.fcw; u8k_.pc = z.pc; u8k_.pcSeg = z.pcSeg;
    u8k_.psapSeg = z.psapSeg; u8k_.psapOff = z.psapOff; u8k_.refresh = z.refresh;
    u8k_.cycles = z.cycles;
    u8k_.setRunState(rs);                        // Pins der CPU wie gesichert
    pinsFromPio();                               // abgeleitete Pins (ohne Rückwirkung)
    // tren_/busak_/A29-Freigabe/Abbildung nachziehen; der Rest ist mit dem Gesicherten
    // gleich (unter RESET16 waren A33/A34 ohnehin gelöscht).
    updateMode();
    dbg_prev_ = pegelSignatur();                 // ein Laden ist kein Pegelwechsel
    p = q;
    return true;
}

void EM::injectParityError() {
    if (pr_) return;
    per_ff_ = true;
    updatePioInputs();
}

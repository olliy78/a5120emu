/**
 * @file mc80_2x.cpp
 * @brief MC80.21/.22 — Aufbau, Lebenslauf, Laufschleife, Tastatur (AP M11).
 * @see mc80_2x.h, doc/mc80/hardware_2x.md, doc/mc80/bsys_2x.prn
 */

#include "core/machines/mc80/mc80_2x.h"
#include "core/cards/mc80/rom_mc80_2x.h"
#include "core/logger.h"
#include <cctype>

namespace {

/// Qt-Tastencodes (Qt::Key), soweit die Matrix eine Taste dafuer hat.
constexpr uint32_t QT_ESCAPE = 0x01000000, QT_TAB = 0x01000001, QT_BACKSPACE = 0x01000003,
                   QT_RETURN = 0x01000004, QT_ENTER = 0x01000005, QT_DELETE = 0x01000007,
                   QT_PAUSE = 0x01000008, QT_LEFT = 0x01000012, QT_UP = 0x01000013,
                   QT_RIGHT = 0x01000014, QT_DOWN = 0x01000015;

/// Matrixnummern der Sondertasten (hardware_2x.md §6).
constexpr int T_TAB = 0x2D, T_CL = 0x2F, T_ENTER = 0x30, T_HOCH = 0x31, T_OFF = 0x32,
              T_RUNTER = 0x33, T_RECHTS = 0x34, T_LINKS = 0x35, T_CTR = 0x36, T_UC = 0x37;

/// Belegung der Zeichentasten 00H–2CH: ungeschaltet / mit UC (0 = keine), aus TTA 0305H.
constexpr char kOhne[0x2D + 1] = "0123456789+,\\;-. ABCDEFGHIJKLMNOPQRSTUVWXYZ=[";
constexpr char kMitUc[0x2D + 1] = "\"!@#$%&'()*<^:/>\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0?]";
static_assert(kOhne[0x10] == ' ' && kOhne[0x11] == 'A' && kOhne[0x2A] == 'Z' && kOhne[0x2C] == '[');
static_assert(kMitUc[0x0F] == '>' && kMitUc[0x10] == '\0' && kMitUc[0x2B] == '?' && kMitUc[0x2C] == ']');

}  // namespace

const char* Mc80_2xMachine::variantenName(Config::Variante v)
{
    return v == Config::Variante::Mc8021 ? "MC80.21" : "MC80.22";
}

K2521::Config Mc80_2xMachine::zreConfig()
{
    K2521::Config c;
    c.rom = MC80_2X_BSYS;                 // S0.1–S0.3, Fassung ver2 (bsys_2x.prn)
    c.rom_len = sizeof(MC80_2X_BSYS);
    c.ctc_base = 0x80;
    c.pio_base = 0x84;
    // Kaskaden X10–X11 am MC80.2x nicht belegt; das Betriebssystem fasst die ZRE-CTC nie an [?].
    c.kaskade_to0_clk1 = c.kaskade_to1_clk2 = c.kaskade_to2_clk3 = false;
    c.iei_quelle = K2521::IeiQuelle::System;   // ZRE nicht an der Spitze [HB III-16]
    c.pio_ab_an_a0 = true;                     // 84H A, 85H B, 86H/87H Steuer [HB III-13]
    return c;
}

Mc80_2xMachine::Mc80_2xMachine(const Config& cfg)
    : cfg_(cfg), zre_(bus_, zreConfig()), zre_speicher_(zre_),
      pfs_([] { K3820::Config c; c.startadresse = 0x2000; return c; }()),
      ops_([] { K3525::Config c; c.base_addr = 0xC000; return c; }())
{
    zre_.attachToBus(bus_);
    bus_.registerMem(&zre_speicher_, 0x0000, 0x1000);
    // PFS: Sockel 0 = mo01 (2000H), 1 leer (2400H), 2–15 = mo03…mo16 (2800H–5FFFH).
    pfs_.setChip(0, MC80_2X_MO01, sizeof(MC80_2X_MO01));
    for (int i = 0; i < 14; ++i)
        pfs_.setChip(2 + i, MC80_2X_MO03_16 + static_cast<size_t>(i) * K3820::kChipSize, K3820::kChipSize);
    pfs_.attachToBus(bus_);
    ops_.attachToBus(bus_);
    dpl_.attachToBus(bus_);
    mbi_.attachToBus(bus_);
    sys_.attachToBus(bus_);
    ppe_.attachToBus(bus_);
    bus_.setInterruptChain({&dpl_, &mbi_, &zre_});   // DPL Platz 11, MBI Platz 10 [?], ZRE
    // RETI-Verteilung nach dem Datenblatt (Z80CTC::setRetiNachIus): das RETI gehoert dem obersten
    // Geraet in Bedienung; ein hoeheres, das nur anfordert, sperrt es nicht.  Ohne das blieb der
    // Zeilenende-Kanal der DPL in Bedienung, weil der Zeichentakt-Kanal beim RETI des TSI schon
    // wieder anforderte — das Bild stand nach der ersten Zeile.
    zre_.cpu().retiCallback = [this]() {
        if (dpl_.retiAnnehmen() || mbi_.retiAnnehmen()) {
            bus_.markIntDirty();
            bus_.updateInterruptChain();
        } else {
            bus_.signalRETI();                       // ZRE und Zusatzkarten wie gewohnt
        }
    };
    powerOn();
}

Mc80_2xMachine::~Mc80_2xMachine() = default;

const FormatCatalog& Mc80_2xMachine::formatCatalog() const
{
    static const FormatCatalog leer;
    return leer;
}

void Mc80_2xMachine::powerOn()
{
    zre_.powerOn(cfg_.ram_fill);
    ops_.fill(0xFF);          // dRAM beim Einschalten: wie die K3525 (A2) FFH
    dpl_.powerOn();
    rafPowerOn();
    total_cycles_ = 0;
    {
        std::lock_guard<std::mutex> l(tasten_mtx_);
        tasten_.clear();
    }
    taste_akt_ = {-1, false, false};
    taste_unten_ = false;
    resetHardware();          // nicht reset(): das schloesse die Bestueckung ab (Fund M7)
    LOG_INFO("MC80", "%s: Netz ein", variantenName(cfg_.variante));
}

void Mc80_2xMachine::reset()
{
    bestueckungAbschliessen();
    resetHardware();
}

void Mc80_2xMachine::resetHardware()
{
    zre_.reset();             // RESET-Taste: Monoflop direkt auf den Rechner, RAM bleibt [HB (33)]
    dpl_.reset();
    mbi_.reset();
    k6022Reset();
    k5020Reset();
    rafReset();
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.markIntDirty();
}

int Mc80_2xMachine::run(int max_cycles)
{
    bestueckungAbschliessen();
    if (nmi_taster_.exchange(false, std::memory_order_relaxed)) bus_.assertNMI();
    Z80& cpu = zre_.cpu();
    int remaining = max_cycles;
    while (remaining > 0 && !stop_.load(std::memory_order_relaxed)) {
        k1520::logging::Logger::instance().update(total_cycles_, cpu.PC, 0);
        bus_.updateInterruptChain();
        if (bus_.isINT() && cpu.interruptAnnehmbar()) cpu.interrupt(bus_.interruptAcknowledge());
        if (bus_.isNMI()) {
            cpu.nmi();
            bus_.clearNMI();
        }
        const int used = cpu.step();
        if (used == 0 && stop_.load(std::memory_order_relaxed)) break;   // Debugger-Halt
        remaining     -= used;
        total_cycles_ += static_cast<uint64_t>(used);

        bool dirty = zre_.clockTick(used);
        dirty |= dpl_.clockTick(used);
        dirty |= mbi_.clockTick(used);
        tastenTakt(used);
        dirty |= k6022Takt(used);
        dirty |= k5020Takt(used);
        if (dirty) bus_.markIntDirty();
    }
    mbi_.autoFlush();
    k6022AutoFlush();
    k5020AutoFlush();
    return max_cycles - remaining;
}

// ─── Tastatur ────────────────────────────────────────────────────────────────

int Mc80_2xMachine::tasteFuer(char c, bool& uc)
{
    uc = false;
    if (c == '\r' || c == '\n') return T_ENTER;
    const char g = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (int nr = 0; nr <= 0x2C; ++nr) {
        if (kOhne[nr] == g) return nr;
    }
    for (int nr = 0; nr <= 0x2C; ++nr) {
        if (kMitUc[nr] != '\0' && kMitUc[nr] == c) {
            uc = true;
            return nr;
        }
    }
    return -1;
}

void Mc80_2xMachine::tasteEinreihen(int nr, bool uc, bool ctr)
{
    if (nr < 0 || nr > 0x37) return;
    std::lock_guard<std::mutex> l(tasten_mtx_);
    tasten_.push_back({nr, uc, ctr});
}

bool Mc80_2xMachine::zeichenEinreihen(char c)
{
    bool uc = false;
    const int nr = tasteFuer(c, uc);
    if (nr < 0) return false;
    tasteEinreihen(nr, uc, false);
    return true;
}

bool Mc80_2xMachine::tastenLeer() const
{
    std::lock_guard<std::mutex> l(tasten_mtx_);
    return tasten_.empty() && taste_akt_.nr < 0;
}

void Mc80_2xMachine::keyPress(uint32_t keycode, bool /*shift*/, bool ctrl)
{
    switch (keycode) {
        case QT_ESCAPE:    tasteEinreihen(T_OFF); return;
        case QT_TAB:       tasteEinreihen(T_TAB); return;
        case QT_BACKSPACE:
        case QT_DELETE:    tasteEinreihen(T_CL); return;
        case QT_RETURN:
        case QT_ENTER:     tasteEinreihen(T_ENTER); return;
        case QT_LEFT:      tasteEinreihen(T_LINKS); return;
        case QT_UP:        tasteEinreihen(T_HOCH); return;
        case QT_RIGHT:     tasteEinreihen(T_RECHTS); return;
        case QT_DOWN:      tasteEinreihen(T_RUNTER); return;
        case QT_PAUSE:     dpl_.breakTaste(); bus_.markIntDirty(); return;   // BK [?]
        default: break;
    }
    if (keycode >= 0x80) return;
    bool uc = false;
    const int nr = tasteFuer(static_cast<char>(keycode), uc);
    if (nr < 0) return;
    // CTR: dritte Ebene (40H–5FH → 00H–1FH, TST); nur fuer Buchstaben und @[\]^ sinnvoll.
    tasteEinreihen(nr, uc, ctrl);
}

bool Mc80_2xMachine::tastenTakt(int takte)
{
    if (taste_akt_.nr < 0) {
        std::lock_guard<std::mutex> l(tasten_mtx_);
        if (tasten_.empty()) return false;
        taste_akt_ = tasten_.front();
        tasten_.pop_front();
        dpl_.setTaste(taste_akt_.nr, true);
        if (taste_akt_.uc) dpl_.setTaste(T_UC, true);
        if (taste_akt_.ctr) dpl_.setTaste(T_CTR, true);
        taste_unten_ = true;
        taste_rest_ = TASTE_HALTEN;
        return true;
    }
    taste_rest_ -= takte;
    if (taste_rest_ > 0) return false;
    if (taste_unten_) {
        dpl_.alleTastenLos();
        taste_unten_ = false;
        taste_rest_ = TASTE_PAUSE;
    } else {
        taste_akt_.nr = -1;
    }
    return true;
}

// ─── Text ────────────────────────────────────────────────────────────────────

uint8_t Mc80_2xMachine::screenChar(int col, int row) const
{
    if (col < 0 || col >= Mc80Dpl::SPALTEN || row < 0 || row >= Mc80Dpl::ZEILEN) return 0;
    return static_cast<uint8_t>(dpl_.zeichen(row, col));
}

std::string Mc80_2xMachine::textZeile(int row) const
{
    std::string s;
    for (int c = 0; c < Mc80Dpl::SPALTEN; ++c) s += dpl_.zeichen(row, c);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

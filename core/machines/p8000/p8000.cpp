/**
 * @file p8000.cpp
 * @brief P8000 — Verdrahtung und Laufschleife der 8-Bit-Seite (AP P7a).
 * @see p8000.h, doc/design/25_p8000.md §10.2/§10.6; Vorbild core/machines/pc1715/pc1715.cpp
 */

#include "core/machines/p8000/p8000.h"
#include "core/peripherals/p8000_terminal/terminal_tasten.h"
#include "core/logger.h"
#include "core/util/zustand.h"
#include <cstring>
#include <fstream>
#include <stdexcept>

P8000Karte8::Config P8000Machine::karteConfig(const Config& c)
{
    P8000Karte8::Config k;
    k.index = c.index8;
    k.mon8 = c.mon8;
    k.takt_hz = c.takt8_hz;
    k.latch_start = c.latch_start;
    k.adp_start = c.adp_start;
    k.ram_fuellwert = c.ram_fuellwert;
    return k;
}

P8000Floppy8::Config P8000Machine::floppyConfig(const Config& c)
{
    P8000Floppy8::Config f;
    f.laufwerke = c.laufwerke;
    return f;
}

P8000Karte16::Config P8000Machine::karte16Config(const Config& c)
{
    P8000Karte16::Config k;
    k.index = c.index16;
    k.mon16 = c.mon16;
    k.dram.karten = c.dram;
    k.dram.fuellwert = c.ram_fuellwert;
    k.takt_hz = c.takt16_hz;
    k.sram_fuellwert = c.ram_fuellwert;
    k.nbr_start = c.latch_start;
    k.nbr_gleichheit_stack = c.nbr_gleichheit_stack;
    return k;
}

/// Soll-Zeit der 16-Bit-Karte zur 8-Bit-Zeit @p t8 (Entwurf §10.2, ganzzahlig ohne Überlauf).
uint64_t P8000Machine::zeit16(uint64_t t8) const
{
    const uint64_t f8 = cfg_.takt8_hz, f16 = cfg_.takt16_hz;
    if (f8 == f16) return t8;
    return (t8 / f8) * f16 + (t8 % f8) * f16 / f8;
}

uint64_t P8000Machine::zeitWdc(uint64_t t8) const
{
    const uint64_t f8 = cfg_.takt8_hz, fw = cfg_.taktwdc_hz;
    if (f8 == fw) return t8;
    return (t8 / f8) * fw + (t8 % f8) * fw / f8;
}

P8000Machine::P8000Machine() : P8000Machine(Config{}) {}
P8000Machine::~P8000Machine() = default;

P8000Machine::P8000Machine(const Config& cfg)
    : cfg_(cfg)
    , karte_(bus_, karteConfig(cfg))
    , floppy_(karte_, floppyConfig(cfg))
    , hub_(cfg.takt8_hz)
{
    if (cfg.terminal == Config::TerminalArt::Original) {
        auto t = std::make_unique<k1520::p8000::HwTerminalGeraet>(karte_.anschluss(KONSOLE_TTY), cfg.takt8_hz,
                                                                  cfg.terminal_hw);
        hwterm_ = t.get();
        konsole_ = std::move(t);
    } else {
        auto t = std::make_unique<k1520::p8000::KernTerminalGeraet>(karte_.anschluss(KONSOLE_TTY), cfg.takt8_hz);
        kern_ = t.get();
        konsole_ = std::move(t);
    }
    karte_.cpu().abortBeforeExecute = [this] { return stop_.load(std::memory_order_relaxed); };
    if (cfg.karte16) {
        // Handbuch S. 3-33/3-87: nur die Paare 8-Bit Index 1 / 16-Bit Index 1 bzw. 3 / 4.
        const bool paar1 = cfg.index8 == Config::Index8::I1 && cfg.index16 == Config::Index16::I1;
        const bool paar34 = cfg.index8 == Config::Index8::I3 && cfg.index16 == Config::Index16::I4;
        if (!paar1 && !paar34)
            throw std::invalid_argument("P8000: Leiterplattenindex 8-Bit/16-Bit nur 1/1 oder 3/4");
        if (cfg.takt8_hz == 0 || cfg.takt16_hz == 0)
            throw std::invalid_argument("P8000: Takt 0");
        k16_ = std::make_unique<P8000Karte16>(karte16Config(cfg));
        if (!k16_->dram().fehler().empty())
            throw std::invalid_argument("P8000: DRAM — " + k16_->dram().fehler());
        P8000Kopplung::Config kc;
        kc.rueckfuehrung = cfg.index16 == Config::Index16::I4 || cfg.bruecken_4xr1_5xr1;
        kopplung_ = std::make_unique<P8000Kopplung>(karte_, *k16_, kc);
    }
    if (cfg.wdc != Config::Wdc::Aus) {
        if (!k16_) throw std::invalid_argument("P8000: WDC nur mit 16-Bit-Karte (karte16=1)");
        if (cfg.taktwdc_hz == 0) throw std::invalid_argument("P8000: WDC-Takt 0");
        if (!cfg.platte_typ.empty() && !k1520::winchester::typNachName(cfg.platte_typ))
            throw std::invalid_argument("P8000: Plattentyp '" + cfg.platte_typ + "' unbekannt");
        P8000Wdc::Config wc;
        switch (cfg.wdc) {
            case Config::Wdc::V4_0_05: wc.firmware = P8000Wdc::Config::Firmware::V4_0_05; break;
            case Config::Wdc::V3_4_05: wc.firmware = P8000Wdc::Config::Firmware::V3_4_05; break;
            default:                   wc.firmware = P8000Wdc::Config::Firmware::V4_2; break;
        }
        wc.takt_hz = cfg.taktwdc_hz;
        wc.ram_fuellwert = cfg.ram_fuellwert;
        wdc_ = std::make_unique<P8000Wdc>(wc);
        wdc_an_ = std::make_unique<P8000WdcAnschluss>(*k16_, *wdc_);
        if (!cfg.platte.empty() && !hdMount(0, cfg.platte))
            throw std::invalid_argument("P8000: Platte — " + hd_fehler_);
    } else if (!cfg.platte.empty()) {
        throw std::invalid_argument("P8000: Platte ohne WDC (wdc=aus)");
    }
    // tty0, tty2, tty3 (und tty4–tty7) nach außen (Reihenfolge = C-ABI-Index); tty1 hängt am
    // Kern-Terminal.
    for (auto* a : serielleAnschluesse()) hub_.registriere(*a);
}

std::vector<k1520::serial::SerialAnschluss*> P8000Machine::serielleAnschluesse()
{
    std::vector<k1520::serial::SerialAnschluss*> v;
    for (int t = 0; t < P8000Karte8::TTY_ANZAHL; ++t)
        if (t != KONSOLE_TTY) v.push_back(&karte_.anschluss(t));
    if (k16_)
        for (int i = 0; i < P8000Karte16::TTY_ANZAHL; ++i) v.push_back(&k16_->anschluss(i));
    return v;
}

// ─── Lebenslauf ──────────────────────────────────────────────────────────────

void P8000Machine::nachReset()
{
    stop_.store(false);
    hub_.gastZurueckgesetzt();
    serial_naechst_ = 0;
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.markIntDirty();
}

void P8000Machine::powerOn()
{
    lw().flushDisks();
    karte_.powerOn();        // RAM/ADP/Latches vorbelegen, RESI = 1, /RES (Floppy über Reset-Haken)
    if (wdc_) wdc_->powerOn();   // Netz-Ein des WDC; danach hält PIO2-B5 (Pull-up) ihn im Reset
    if (k16_) {
        k16_->powerOn();     // PRES−: MRESET− und PIORESET−; RESET (K11) hält den U8001
        kopplung_->rechne();
    }
    if (wdc_an_) wdc_an_->rechne();
    hd_zugriff_lw_ = -1;
    if (hwterm_) {
        // Arbeitsplatz-Reihenfolge: erst das Terminal, dann der Rechner.  Was das Terminal in der
        // Einschaltphase sendet (00H der Firmware, Merkposten 30), trifft einen Rechner ohne Netz.
        auto& e = hwterm_->einheit();
        e.einschalten();
        e.laufeMs(cfg_.terminal_vorlauf_ms);
        while (e.hw().hatAusgabe()) (void)e.hw().holeAusgabe();
        hwterm_->kopplung().synchronisiere();
    }
    nachReset();
    LOG_INFO("P8000", "Netz ein (8-Bit-Teil)");
}

void P8000Machine::reset()
{
    bestueckungAbschliessen();
    lw().flushDisks();
    karte_.reset();          // Taste /RESP: RESI = 0
    nachReset();
    LOG_INFO("P8000", "Reset-Taste");
}

uint8_t P8000Machine::panelLamps() const
{
    if (!k16_) return 0;
    bool hd = false;
    for (int u = 0; u < P8000Wdc::LAUFWERKE; ++u) hd = hd || hdLed(u);
    return static_cast<uint8_t>((k16_->runLed() ? 1 : 0) | (k16_->inReset() ? 0 : 2) | (hd ? 4 : 0));
}

// ─── Terminal ────────────────────────────────────────────────────────────────

k1520::p8000::Terminal& P8000Machine::terminal()
{
    if (!kern_) throw std::logic_error("P8000: Originalterminal an tty1 — kein Kern-Terminal (konsole() benutzen)");
    return kern_->terminal();
}

const k1520::p8000::Terminal& P8000Machine::terminal() const
{
    if (!kern_) throw std::logic_error("P8000: Originalterminal an tty1 — kein Kern-Terminal (konsole() benutzen)");
    return kern_->terminal();
}

uint8_t P8000Machine::screenChar(int col, int row) const
{
    if (col < 0 || row < 0 || col >= k1520::p8000::Terminal::SPALTEN ||
        row >= k1520::p8000::Terminal::ZEILEN)
        return 0;
    const auto z = konsole_->zelle(row, col);
    return z.feld ? uint8_t(' ') : z.zeichen;
}

const uint8_t* P8000Machine::framebuffer() const
{
    return hwterm_ ? hwterm_->einheit().hw().pixel().data() : fb_.data();
}
int P8000Machine::fbWidth() const  { return hwterm_ ? hwterm_->einheit().hw().pixelBreite() : FB_BREITE; }
int P8000Machine::fbHeight() const { return hwterm_ ? hwterm_->einheit().hw().pixelHoehe() : FB_HOEHE; }

uint8_t P8000Machine::keyboardLeds() const
{
    return hwterm_ ? hwterm_->einheit().tastatur().leds() : 0;
}

namespace {
constexpr uint32_t LOSLASSEN = 0x00800000u;   ///< Merker im Tastenpuffer: Matrixtaste loslassen
}

void P8000Machine::keyPress(uint32_t k, bool, bool ctrl)
{
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k & ~LOSLASSEN, ctrl});
}

void P8000Machine::keyRelease(uint32_t k)
{
    if ((k & 0xFF000000u) != MATRIX_KODE) return;
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k | LOSLASSEN, false});
}

void P8000Machine::tastenAbgeben()
{
    std::deque<Taste> t;
    {
        std::lock_guard<std::mutex> lk(tasten_sperre_);
        t.swap(tasten_);
    }
    for (const Taste& e : t) {
        // Matrixtasten der K7673 (nur Originalterminal): drücken bzw. loslassen, ohne Zeitplan —
        // die Oberfläche hält sie so lange, wie der Anwender sie hält.
        if ((e.code & 0xFF000000u) == MATRIX_KODE) {
            if (hwterm_)
                hwterm_->einheit().matrixDirekt({int((e.code >> 8) & 0x7F), int(e.code & 0xFF)}, !(e.code & LOSLASSEN));
            continue;
        }
        // Caps lock an/aus (Rasttaste): Kern-Terminal setzt den Zustand, das Original drückt die Taste.
        if (e.code == 0x02000100 || e.code == 0x02000101) {
            const bool an = e.code == 0x02000100;
            if (kern_) kern_->terminal().setzeCapsLock(an);
            else hwterm_->einheit().setzeCapsLock(an);
            continue;
        }
        k1520::p8000::qtTasteAnTerminal(*konsole_, e.code, e.ctrl);
    }
}

// ─── Laufwerke ───────────────────────────────────────────────────────────────

bool P8000Machine::isMotorOn(int d) const
{
    return const_cast<P8000Floppy8&>(floppy_).motorAn(d);
}

bool P8000Machine::isDiskLedOn(int d) const
{
    auto& f = const_cast<P8000Floppy8&>(floppy_);
    return d >= 0 && d < 4 && (f.motorAn(d) || f.fdc().unitBusy(d));
}

// ─── Winchester (AP P13d) ────────────────────────────────────────────────────

bool P8000Machine::hdMount(int unit, const std::string& path, bool wp)
{
    hd_fehler_.clear();
    if (!wdc_) { hd_fehler_ = "kein WDC (wdc=aus)"; return false; }
    if (unit < 0 || unit >= P8000Wdc::LAUFWERKE) { hd_fehler_ = "Laufwerk 0–2"; return false; }
    if (wp) { hd_fehler_ = "ein Winchesterlaufwerk hat keinen Schreibschutz"; return false; }
    k1520::winchester::Platte::Config pc;
    pc.par_ergaenzen = cfg_.platte_par_ergaenzen;
    if (!cfg_.platte_typ.empty()) pc.geometrie = k1520::winchester::typNachName(cfg_.platte_typ)->g;
    auto p = std::make_unique<k1520::winchester::Platte>();
    if (!p->oeffnen(path, pc)) { hd_fehler_ = p->fehler(); return false; }
    hdUnmount(unit);
    platten_[size_t(unit)] = std::move(p);
    wdc_->anschliessen(unit, platten_[size_t(unit)].get());
    LOG_INFO("P8000", "Platte %d: %s (%u/%u/%u)", unit, path.c_str(),
             unsigned(platten_[size_t(unit)]->geometrie().zylinder), unsigned(platten_[size_t(unit)]->geometrie().koepfe),
             unsigned(platten_[size_t(unit)]->geometrie().sektoren));
    return true;
}

bool P8000Machine::hdCreate(int unit, const std::string& path, const std::string& typ)
{
    hd_fehler_.clear();
    const k1520::winchester::Typ* t = k1520::winchester::typNachName(typ.empty() ? "K5504.50" : typ);
    if (!t) { hd_fehler_ = "Plattentyp '" + typ + "' unbekannt"; return false; }
    if (!wdc_) { hd_fehler_ = "kein WDC (wdc=aus)"; return false; }
    if (!k1520::winchester::Platte::neu(path, *t, &hd_fehler_)) return false;
    return hdMount(unit, path);
}

bool P8000Machine::hdUnmount(int unit)
{
    if (unit < 0 || unit >= P8000Wdc::LAUFWERKE || !platten_[size_t(unit)]) return false;
    if (wdc_) wdc_->anschliessen(unit, nullptr);
    platten_[size_t(unit)]->schliessen();   // zerlegt und schreibt geänderte Spuren zurück
    platten_[size_t(unit)].reset();
    return true;
}

bool P8000Machine::hdFlush()
{
    bool any = false;
    for (auto& p : platten_)
        if (p) { p->flush(); any = true; }
    return any;
}

std::string P8000Machine::hdPath(int unit) const
{
    if (unit < 0 || unit >= P8000Wdc::LAUFWERKE || !platten_[size_t(unit)]) return "";
    return platten_[size_t(unit)]->pfad();
}

bool P8000Machine::hdLed(int unit) const
{
    if (!wdc_ || unit < 0 || unit >= P8000Wdc::LAUFWERKE || unit != hd_zugriff_lw_ || !platten_[size_t(unit)])
        return false;
    return total_cycles_ - hd_zugriff_t_ < cfg_.takt8_hz / 10;   // 0,1 s Nachleuchten
}

// ─── Diagnose ────────────────────────────────────────────────────────────────

uint8_t P8000Machine::memReadDebug(uint16_t a)
{
    return const_cast<P8000Karte8&>(karte_).speicher().peek(a);
}

void P8000Machine::memWriteDebug(uint16_t a, uint8_t d)
{
    karte_.speicher().memWrite(a, d);
    (void)karte_.speicher().nimmWartetakte();
}

// ─── Laufschleife (Entwurf §10.2, nur 8-Bit-Seite) ───────────────────────────

int P8000Machine::run(int max_cycles)
{
    bestueckungAbschliessen();
    tastenAbgeben();
    if (nmi_taster_.exchange(false, std::memory_order_relaxed)) karte_.nmiTaste();

    int remaining = max_cycles;
    while (remaining > 0 && !stop_.load(std::memory_order_relaxed)) {
        k1520::logging::Logger::instance().update(total_cycles_, karte_.cpu().PC, 0);
        const int n = karte_.schritt();      // DMA hält die CPU; EPROM-Wartetakte eingerechnet
        if (n == 0) break;                   // Debugger-Halt
        karte_.takt(n);
        floppy_.takt(n);
        remaining     -= n;
        total_cycles_ += static_cast<uint64_t>(n);
        if (k16_) k16_->laufeBis(zeit16(total_cycles_));   // U8001 nachziehen (im Reset ohne Schritt)
        if (wdc_) {                                          // dann der WDC (§10.2: 8 → 16 → WDC)
            wdc_->laufeBis(zeitWdc(total_cycles_));
            if (wdc_->zugriffAktiv()) { hd_zugriff_t_ = total_cycles_; hd_zugriff_lw_ = wdc_->gewaehltesLaufwerk(); }
        }

        konsole_->takt(static_cast<uint64_t>(n));
        if (total_cycles_ >= serial_naechst_) serial_naechst_ = hub_.takt(total_cycles_);
    }
    lw().autoFlush(total_cycles_);
    if (wdc_)
        for (auto& p : platten_) if (p) p->autoFlush(wdc_->takte());
    return max_cycles - remaining;
}

// ─── Save-State P8KS v4 (Entwurf §10.2) ──────────────────────────────────────

namespace {
constexpr char     MAGIC[4] = {'P', '8', 'K', 'S'};
constexpr uint8_t  ABS_CONFIG = 1, ABS_FLOPPY = 2, ABS_KARTE = 3, ABS_TERMINAL = 4, ABS_MASCHINE = 5,
                   ABS_KARTE16 = 6, ABS_KOPPLUNG = 7, ABS_WDC = 8, ABS_ANZAHL = 9;

void abschnitt(std::vector<uint8_t>& out, uint8_t id, const std::vector<uint8_t>& inhalt)
{
    out.push_back(id);
    const uint32_t n = static_cast<uint32_t>(inhalt.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((n >> (8 * i)) & 0xFF));
    out.insert(out.end(), inhalt.begin(), inhalt.end());
}
}  // namespace

/// Fingerabdruck der Bauzeit-Konfiguration: weicht er ab, wird das Laden abgelehnt.  @p stand 1
/// = Aufbau von P8KS v1 (ohne 16-Bit-Teil), sonst mit den Angaben des 16-Bit-Teils.
void P8000Machine::configAbschnitt(std::vector<uint8_t>& out, uint8_t stand) const
{
    auto a = k1520::ZAr::schreiber(out);
    uint8_t i8 = static_cast<uint8_t>(cfg_.index8), m8 = static_cast<uint8_t>(cfg_.mon8);
    uint32_t t8 = cfg_.takt8_hz;
    a.num(i8); a.num(m8); a.num(t8);
    for (const std::string& l : cfg_.laufwerke) {
        uint8_t n = static_cast<uint8_t>(l.size());
        a.num(n);
        a.raw(reinterpret_cast<uint8_t*>(const_cast<char*>(l.data())), l.size());
    }
    if (stand >= 2) {
        bool k16 = cfg_.karte16;
        a.flag(k16);
        if (k16) {
            uint8_t i16 = static_cast<uint8_t>(cfg_.index16), m16 = static_cast<uint8_t>(cfg_.mon16);
            uint32_t t16 = cfg_.takt16_hz;
            bool br = cfg_.bruecken_4xr1_5xr1;
            uint8_t nk = static_cast<uint8_t>(cfg_.dram.size());
            a.num(i16); a.num(m16); a.num(t16); a.flag(br); a.num(nk);
            for (const P8000Dram16::Karte& k : cfg_.dram) {
                uint8_t typ = static_cast<uint8_t>(k.typ), mod = k.modul;
                a.num(typ); a.num(mod);
            }
            if (stand >= 3) {
                uint8_t w = static_cast<uint8_t>(cfg_.wdc);
                uint32_t tw = cfg_.taktwdc_hz;
                a.num(w); a.num(tw);
            }
        }
    }
    if (stand >= 4) {   // P8KS v4: Terminalart der Konsole (+ Zeichentakt-Teiler des Originals) am Ende
        uint8_t art = hwterm_ ? 1 : 0;
        uint8_t teiler = hwterm_ ? static_cast<uint8_t>(cfg_.terminal_hw.hw.zeichentaktTeiler) : 0;
        a.num(art); a.num(teiler);
    }
}

std::vector<uint8_t> P8000Machine::stateBytes() const
{
    std::vector<uint8_t> out(MAGIC, MAGIC + 4);
    out.push_back(P8000_STAND);

    std::vector<uint8_t> b;
    configAbschnitt(b, P8000_STAND);
    abschnitt(out, ABS_CONFIG, b);

    b.clear(); floppy_.serialize(b);  abschnitt(out, ABS_FLOPPY, b);
    b.clear(); karte_.serialize(b);   abschnitt(out, ABS_KARTE, b);
    b.clear();
    konsole_->serialize(b);   // Kern: Terminal + Anschluss (bitgleich zu v3); Original: P8TH + Kopplung
    abschnitt(out, ABS_TERMINAL, b);
    if (k16_) {
        b.clear(); k16_->serialize(b);      abschnitt(out, ABS_KARTE16, b);
        b.clear(); kopplung_->serialize(b); abschnitt(out, ABS_KOPPLUNG, b);
    }
    if (wdc_) { b.clear(); wdc_->serialize(b); abschnitt(out, ABS_WDC, b); }

    b.clear();
    {
        auto a = k1520::ZAr::schreiber(b);
        uint64_t tc = total_cycles_, sn = serial_naechst_;
        bool nmi = nmi_taster_.load(std::memory_order_relaxed);
        a.num(tc); a.num(sn); a.flag(nmi);
        std::deque<Taste> t;
        {
            std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(tasten_sperre_));
            t = tasten_;
        }
        uint32_t n = static_cast<uint32_t>(t.size());
        a.num(n);
        for (Taste& e : t) { uint32_t c = e.code; a.num(c); a.flag(e.ctrl); }
    }
    abschnitt(out, ABS_MASCHINE, b);
    return out;
}

bool P8000Machine::wendeAbschnitteAn(const std::vector<uint8_t>& b)
{
    // Erst alle Abschnitte einsammeln und die Konfiguration prüfen, dann anwenden.
    if (b.size() < 5 || std::memcmp(b.data(), MAGIC, 4) != 0) { state_error_ = "Kein P8KS-Zustand"; return false; }
    if (b[4] < 1 || b[4] > P8000_STAND) { state_error_ = "P8KS-Version " + std::to_string(b[4]) + " unbekannt"; return false; }
    struct Teil { bool da = false; const uint8_t* p = nullptr; const uint8_t* e = nullptr; };
    Teil t[ABS_ANZAHL];
    size_t pos = 5;
    while (pos < b.size()) {
        if (b.size() - pos < 5) { state_error_ = "P8KS: Abschnittskopf abgeschnitten"; return false; }
        const uint8_t id = b[pos];
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i) n |= static_cast<uint32_t>(b[pos + 1 + i]) << (8 * i);
        pos += 5;
        if (b.size() - pos < n) { state_error_ = "P8KS: Abschnitt abgeschnitten"; return false; }
        if (id >= 1 && id < ABS_ANZAHL) t[id] = {true, b.data() + pos, b.data() + pos + n};
        pos += n;   // unbekannte Abschnitte überspringen
    }
    const uint8_t stand = b[4];
    if (stand < 2 && k16_) { state_error_ = "P8KS v1 kennt keinen 16-Bit-Teil"; return false; }
    if (stand < 3 && wdc_) { state_error_ = "P8KS v" + std::to_string(stand) + " kennt keinen WDC"; return false; }
    if (stand < 4 && hwterm_) { state_error_ = "P8KS v" + std::to_string(stand) + " kennt kein Originalterminal"; return false; }
    std::vector<uint8_t> cfg;
    configAbschnitt(cfg, stand);
    if (!t[ABS_CONFIG].da || cfg.size() != static_cast<size_t>(t[ABS_CONFIG].e - t[ABS_CONFIG].p) ||
        std::memcmp(cfg.data(), t[ABS_CONFIG].p, cfg.size()) != 0) {
        state_error_ = "P8KS: Konfiguration (Index, ROM-Satz, Takt, Laufwerke, 16-Bit-Teil) stimmt nicht überein";
        return false;
    }
    if (!t[ABS_KARTE].da) { state_error_ = "P8KS: Abschnitt Karte fehlt"; return false; }
    if (k16_ && !t[ABS_KARTE16].da) { state_error_ = "P8KS: Abschnitt 16-Bit-Karte fehlt"; return false; }
    if (wdc_ && !t[ABS_WDC].da) { state_error_ = "P8KS: Abschnitt WDC fehlt"; return false; }

    // Fehlende Abschnitte (außer der Karte) lassen den Ist-Zustand stehen.
    auto lade = [&](uint8_t id, const char* name, auto&& f) {
        if (!t[id].da) return true;
        const uint8_t* p = t[id].p;
        if (!f(p, t[id].e)) { state_error_ = std::string("P8KS: Abschnitt ") + name + " unlesbar"; return false; }
        return true;
    };
    // Reihenfolge: Floppy vor Karte (die FDC-Rückrufe schreiben in die PIO2, die die Karte danach
    // endgültig setzt).
    if (!lade(ABS_FLOPPY, "Floppy", [&](const uint8_t*& p, const uint8_t* e) { return floppy_.deserialize(p, e); })) return false;
    // Die Kopplung rechnet während des Ladens nicht: die Karten stellen ihre Pins selbst her.
    struct Ruhig {
        P8000Kopplung* k;
        explicit Ruhig(P8000Kopplung* kk) : k(kk) { if (k) k->setRuhig(true); }
        ~Ruhig() { if (k) k->setRuhig(false); }
    } ruhig(kopplung_.get());
    struct RuhigWdc {
        P8000WdcAnschluss* k;
        explicit RuhigWdc(P8000WdcAnschluss* kk) : k(kk) { if (k) k->setRuhig(true); }
        ~RuhigWdc() { if (k) k->setRuhig(false); }
    } ruhigWdc(wdc_an_.get());
    if (!lade(ABS_KARTE, "Karte", [&](const uint8_t*& p, const uint8_t* e) { return karte_.deserialize(p, e); })) return false;
    if (k16_) {
        if (!lade(ABS_KARTE16, "16-Bit-Karte", [&](const uint8_t*& p, const uint8_t* e) { return k16_->deserialize(p, e); })) return false;
        if (!lade(ABS_KOPPLUNG, "Kopplung", [&](const uint8_t*& p, const uint8_t* e) { return kopplung_->deserialize(p, e); })) return false;
    }
    // WDC samt Plattenmechanik; die Abbilder selbst mountet der Aufrufer vorher (wie Disketten).
    if (wdc_ && !lade(ABS_WDC, "WDC (Platten wie beim Speichern angeschlossen?)",
                      [&](const uint8_t*& p, const uint8_t* e) { return wdc_->deserialize(p, e); })) return false;
    if (!lade(ABS_TERMINAL, "Terminal", [&](const uint8_t*& p, const uint8_t* e) {
            return konsole_->deserialize(p, e); })) return false;
    if (!lade(ABS_MASCHINE, "Maschine", [&](const uint8_t*& p, const uint8_t* e) {
            auto a = k1520::ZAr::leser(p, e);
            uint64_t tc = 0, sn = 0; bool nmi = false; uint32_t n = 0;
            a.num(tc); a.num(sn); a.flag(nmi); a.num(n);
            if (!a.ok || n > 65536) return false;
            std::deque<Taste> q;
            for (uint32_t i = 0; i < n && a.ok; ++i) { uint32_t c = 0; bool ct = false; a.num(c); a.flag(ct); q.push_back({c, ct}); }
            if (!a.ok) return false;
            total_cycles_ = tc; serial_naechst_ = sn; nmi_taster_.store(nmi);
            std::lock_guard<std::mutex> lk(tasten_sperre_);
            tasten_ = std::move(q);
            return true; })) return false;
    hub_.gastZurueckgesetzt();
    stop_.store(false);
    bus_.markIntDirty();
    return true;
}

bool P8000Machine::restoreStateBytes(const std::vector<uint8_t>& b)
{
    state_error_.clear();
    const std::vector<uint8_t> sicherung = stateBytes();
    if (wendeAbschnitteAn(b)) return true;
    const std::string grund = state_error_;
    wendeAbschnitteAn(sicherung);      // halb angewandt ⇒ alten Zustand zurück
    state_error_ = grund;
    return false;
}

bool P8000Machine::saveState(const std::string& path) const
{
    const std::vector<uint8_t> b = stateBytes();
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(f);
}

bool P8000Machine::loadState(const std::string& path)
{
    state_error_.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) { state_error_ = "Zustandsdatei nicht lesbar: " + path; return false; }
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return restoreStateBytes(b);
}

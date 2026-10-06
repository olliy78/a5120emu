/**
 * @file p8000.cpp
 * @brief P8000 — Verdrahtung und Laufschleife der 8-Bit-Seite (AP P7a).
 * @see p8000.h, doc/design/25_p8000.md §10.2/§10.6; Vorbild core/machines/pc1715/pc1715.cpp
 */

#include "core/machines/p8000/p8000.h"
#include "core/logger.h"
#include "core/util/zustand.h"
#include <cstring>
#include <fstream>

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

P8000Machine::P8000Machine() : P8000Machine(Config{}) {}
P8000Machine::~P8000Machine() = default;

P8000Machine::P8000Machine(const Config& cfg)
    : cfg_(cfg)
    , karte_(bus_, karteConfig(cfg))
    , floppy_(karte_, floppyConfig(cfg))
    , term_anschluss_(karte_.anschluss(KONSOLE_TTY), term_, cfg.takt8_hz)
    , hub_(cfg.takt8_hz)
{
    karte_.cpu().abortBeforeExecute = [this] { return stop_.load(std::memory_order_relaxed); };
    // tty0, tty2, tty3 nach außen (Reihenfolge = C-ABI-Index); tty1 hängt am Kern-Terminal.
    for (auto* a : serielleAnschluesse()) hub_.registriere(*a);
}

std::vector<k1520::serial::SerialAnschluss*> P8000Machine::serielleAnschluesse()
{
    std::vector<k1520::serial::SerialAnschluss*> v;
    for (int t = 0; t < P8000Karte8::TTY_ANZAHL; ++t)
        if (t != KONSOLE_TTY) v.push_back(&karte_.anschluss(t));
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

// ─── Terminal ────────────────────────────────────────────────────────────────

uint8_t P8000Machine::screenChar(int col, int row) const
{
    if (col < 0 || row < 0 || col >= k1520::p8000::Terminal::SPALTEN ||
        row >= k1520::p8000::Terminal::ZEILEN)
        return 0;
    const auto& z = term_.zelle(row, col);
    return z.feld ? uint8_t(' ') : z.zeichen;
}

void P8000Machine::keyPress(uint32_t k, bool, bool ctrl)
{
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k, ctrl});
}

void P8000Machine::tastenAbgeben()
{
    std::deque<Taste> t;
    {
        std::lock_guard<std::mutex> lk(tasten_sperre_);
        t.swap(tasten_);
    }
    using k1520::p8000::TerminalTaste;
    for (const Taste& e : t) {
        switch (e.code) {
            case 0x01000004: case 0x01000005: term_.taste(TerminalTaste::CR);  continue;   // Return/Enter
            case 0x01000000: term_.taste(TerminalTaste::ESC); continue;                    // Escape
            case 0x01000003: term_.taste(TerminalTaste::BS);  continue;                    // Backspace
            case 0x01000001: term_.taste(TerminalTaste::HT);  continue;                    // Tab
            case 0x01000007: term_.taste(TerminalTaste::DEL); continue;                    // Delete
            default: break;
        }
        if (e.code == '\r' || e.code == '\n') { term_.taste(TerminalTaste::CR); continue; }
        if (e.code < 0x80) term_.zeichenTaste(static_cast<uint8_t>(e.code), e.ctrl);
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

        term_anschluss_.takt(static_cast<uint64_t>(n));
        if (total_cycles_ >= serial_naechst_) serial_naechst_ = hub_.takt(total_cycles_);
    }
    lw().autoFlush(total_cycles_);
    return max_cycles - remaining;
}

// ─── Save-State P8KS v1 (Entwurf §10.2) ──────────────────────────────────────

namespace {
constexpr char     MAGIC[4] = {'P', '8', 'K', 'S'};
constexpr uint8_t  ABS_CONFIG = 1, ABS_FLOPPY = 2, ABS_KARTE = 3, ABS_TERMINAL = 4, ABS_MASCHINE = 5;

void abschnitt(std::vector<uint8_t>& out, uint8_t id, const std::vector<uint8_t>& inhalt)
{
    out.push_back(id);
    const uint32_t n = static_cast<uint32_t>(inhalt.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((n >> (8 * i)) & 0xFF));
    out.insert(out.end(), inhalt.begin(), inhalt.end());
}
}  // namespace

/// Fingerabdruck der Bauzeit-Konfiguration: weicht er ab, wird das Laden abgelehnt.
void P8000Machine::configAbschnitt(std::vector<uint8_t>& out) const
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
}

std::vector<uint8_t> P8000Machine::stateBytes() const
{
    std::vector<uint8_t> out(MAGIC, MAGIC + 4);
    out.push_back(P8000_STAND);

    std::vector<uint8_t> b;
    configAbschnitt(b);
    abschnitt(out, ABS_CONFIG, b);

    b.clear(); floppy_.serialize(b);  abschnitt(out, ABS_FLOPPY, b);
    b.clear(); karte_.serialize(b);   abschnitt(out, ABS_KARTE, b);
    b.clear();
    term_.serialize(b);
    term_anschluss_.serialize(b);
    abschnitt(out, ABS_TERMINAL, b);

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
    Teil t[ABS_MASCHINE + 1];
    size_t pos = 5;
    while (pos < b.size()) {
        if (b.size() - pos < 5) { state_error_ = "P8KS: Abschnittskopf abgeschnitten"; return false; }
        const uint8_t id = b[pos];
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i) n |= static_cast<uint32_t>(b[pos + 1 + i]) << (8 * i);
        pos += 5;
        if (b.size() - pos < n) { state_error_ = "P8KS: Abschnitt abgeschnitten"; return false; }
        if (id >= 1 && id <= ABS_MASCHINE) t[id] = {true, b.data() + pos, b.data() + pos + n};
        pos += n;   // unbekannte Abschnitte überspringen
    }
    std::vector<uint8_t> cfg;
    configAbschnitt(cfg);
    if (!t[ABS_CONFIG].da || cfg.size() != static_cast<size_t>(t[ABS_CONFIG].e - t[ABS_CONFIG].p) ||
        std::memcmp(cfg.data(), t[ABS_CONFIG].p, cfg.size()) != 0) {
        state_error_ = "P8KS: Konfiguration (Index, ROM-Satz, Takt, Laufwerke) stimmt nicht überein";
        return false;
    }
    if (!t[ABS_KARTE].da) { state_error_ = "P8KS: Abschnitt Karte fehlt"; return false; }

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
    if (!lade(ABS_KARTE, "Karte", [&](const uint8_t*& p, const uint8_t* e) { return karte_.deserialize(p, e); })) return false;
    if (!lade(ABS_TERMINAL, "Terminal", [&](const uint8_t*& p, const uint8_t* e) {
            return term_.deserialize(p, e) && term_anschluss_.deserialize(p, e); })) return false;
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

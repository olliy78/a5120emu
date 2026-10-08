/**
 * @file p8000_terminal_machine.cpp
 * @brief „P8000 Terminal" als eigenständige Maschine (AP P20c/P20d).  @see p8000_terminal_machine.h
 */

#include "core/machines/p8000/p8000_terminal_machine.h"

#include "core/logger.h"
#include "core/peripherals/p8000_terminal/terminal_tasten.h"
#include "core/peripherals/p8000_terminal_hw/rom_p8t.h"
#include "core/util/zustand.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {
constexpr uint32_t LOSLASSEN = 0x00800000u;
constexpr char MAGIC[4] = {'P', '8', 'T', 'M'};

void trim(std::string& t) {
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    while (!t.empty() && t.back() == ' ') t.pop_back();
}
}  // namespace

int P8000TerminalMachine::hwSchluessel(const std::string& key, const std::string& val,
                                       k1520::p8000::P8000TerminalEinheitConfig& c, std::string& fehler) {
    if (key == "firmware") {
        // 6.0 gehört zu anderer Hardware (BWS 3000H, 4-KB-ZG, Befund P19a) — nur 5.0.
        if (val != "5.0") { fehler = "P8000-Terminal: firmware = " + val + " nicht moeglich (nur 5.0; 6.0 gehoert zu anderer Hardware)"; return -1; }
        c.hw.firmware = nullptr;
        c.hw.firmwareGroesse = 0;
        return 1;
    }
    if (key == "zeichensatz") {
        // Bestückung ZG1-ZG2 [U, Befund §4.3]: Vorgabe Z61 = P8TEZS (Strobe 4000H), Z62 = P8TDZS.
        if (val == "ezs-dzs") { c.hw.zg1 = P8T_ZG_EZS; c.hw.zg2 = P8T_ZG_DZS; }
        else if (val == "dzs-ezs") { c.hw.zg1 = P8T_ZG_DZS; c.hw.zg2 = P8T_ZG_EZS; }
        else { fehler = "P8000-Terminal: zeichensatz = " + val + " unbekannt (ezs-dzs | dzs-ezs)"; return -1; }
        return 1;
    }
    if (key == "teiler") {
        if (val == "7") c.hw.zeichentaktTeiler = 7;
        else if (val == "8") c.hw.zeichentaktTeiler = 8;
        else { fehler = "P8000-Terminal: teiler = " + val + " unbekannt (7 | 8)"; return -1; }
        return 1;
    }
    return 0;
}

bool P8000TerminalMachine::konfigAusText(const char* text, Config& cfg, std::string& fehler) {
    using namespace k1520::serial;
    const std::string alles = text ? text : "";
    size_t pos = 0;
    while (pos < alles.size()) {
        size_t ende = alles.find(',', pos);
        if (ende == std::string::npos) ende = alles.size();
        std::string eintrag = alles.substr(pos, ende - pos);
        pos = ende + 1;
        trim(eintrag);
        if (eintrag.empty()) continue;
        const size_t gl = eintrag.find('=');
        std::string key = eintrag.substr(0, gl), val = gl == std::string::npos ? "" : eintrag.substr(gl + 1);
        trim(key); trim(val);
        if (gl == std::string::npos || (val.empty() && key != "datei")) {
            fehler = "P8000-Terminal: '" + eintrag + "' ist kein schluessel=wert";
            return false;
        }
        const int hw = hwSchluessel(key, val, cfg.einheit, fehler);
        if (hw < 0) return false;
        if (hw > 0) continue;
        if (key == "art") {
            if (val == "telnet") cfg.leitung.betriebsart = Betriebsart::Telnet;
            else if (val == "rfc2217") cfg.leitung.betriebsart = Betriebsart::Rfc2217;
            else if (val == "datei") cfg.leitung.betriebsart = Betriebsart::Datei;
            else { fehler = "P8000-Terminal: art = " + val + " unbekannt (telnet | rfc2217 | datei)"; return false; }
        } else if (key == "rolle") {
            if (val == "client") cfg.leitung.rolle = Rolle::Client;
            else if (val == "server") cfg.leitung.rolle = Rolle::Server;
            else { fehler = "P8000-Terminal: rolle = " + val + " unbekannt (client | server)"; return false; }
        } else if (key == "host") {
            cfg.leitung.host = val;
        } else if (key == "port") {
            if (val.find_first_not_of("0123456789") != std::string::npos || val.size() > 5 || std::stoul(val) > 65535) {
                fehler = "P8000-Terminal: port = " + val + " ungueltig";
                return false;
            }
            cfg.leitung.port = static_cast<uint16_t>(std::stoul(val));
        } else if (key == "datei") {
            cfg.leitung.datei = val;
        } else if (key == "verbinden") {
            if (val == "0") cfg.verbinden = false;
            else if (val == "1") cfg.verbinden = true;
            else { fehler = "P8000-Terminal: verbinden = " + val + " unbekannt (0 | 1)"; return false; }
        } else {
            fehler = "P8000-Terminal: unbekannter Schluessel '" + key + "'";
            return false;
        }
    }
    return true;
}

P8000TerminalMachine::P8000TerminalMachine(const Config& cfg) : cfg_(cfg), einheit_(cfg.einheit) {
    auto& hub = einheit_.hub();
    if (!hub.konfigurieren(0, cfg.leitung))
        throw std::invalid_argument("P8000-Terminal: Leitungseinstellung abgewiesen (Port/Datei?)");
    if (cfg.verbinden && !hub.start(0))
        throw std::invalid_argument("P8000-Terminal: Verbindung nicht startbar — " + hub.status(0).meldung);
}

P8000TerminalMachine::~P8000TerminalMachine() {
    einheit_.hub().stopAlle();
}

const FormatCatalog& P8000TerminalMachine::formatCatalog() const {
    static const FormatCatalog leer;
    return leer;
}

void P8000TerminalMachine::powerOn() {
    {
        std::lock_guard<std::mutex> lk(tasten_sperre_);
        tasten_.clear();
    }
    einheit_.einschalten();
    einheit_.hub().gastZurueckgesetzt();
    stop_.store(false);
    LOG_INFO("P8000T", "Terminal Netz ein");
}

uint8_t P8000TerminalMachine::screenChar(int col, int row) const {
    if (col < 0 || row < 0 || col >= 80 || row >= 24) return 0;
    const auto z = einheit_.hw().zelle(row, col);
    return z.feld ? uint8_t(' ') : z.zeichen;
}

void P8000TerminalMachine::keyPress(uint32_t k, bool, bool ctrl) {
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k & ~LOSLASSEN, ctrl});
}

void P8000TerminalMachine::keyRelease(uint32_t k) {
    if ((k & 0xFF000000u) != MATRIX_KODE) return;
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k | LOSLASSEN, false});
}

void P8000TerminalMachine::tastenAbgeben() {
    std::deque<Taste> t;
    {
        std::lock_guard<std::mutex> lk(tasten_sperre_);
        t.swap(tasten_);
    }
    for (const Taste& e : t) {
        if ((e.code & 0xFF000000u) == MATRIX_KODE) {
            einheit_.matrixDirekt({int((e.code >> 8) & 0x7F), int(e.code & 0xFF)}, !(e.code & LOSLASSEN));
            continue;
        }
        if (e.code == 0x02000100 || e.code == 0x02000101) { einheit_.setzeCapsLock(e.code == 0x02000100); continue; }
        k1520::p8000::qtTasteAnTerminal(einheit_, e.code, e.ctrl);
    }
}

int P8000TerminalMachine::run(int max_cycles) {
    bestueckungAbschliessen();
    tastenAbgeben();
    if (stop_.exchange(false) || max_cycles <= 0) return 0;
    const uint64_t start = einheit_.takte();
    einheit_.laufe(static_cast<uint64_t>(max_cycles));
    return static_cast<int>(einheit_.takte() - start);
}

uint8_t P8000TerminalMachine::memReadDebug(uint16_t a) {
    // Sicht des Z8 auf den Datenspeicher: BWS 1000–17FF, sonst Programm-EPROM.
    if (a >= 0x1000 && a < 0x1800) return einheit_.hw().ram()[a - 0x1000];
    return einheit_.hw().programm(a);
}

void P8000TerminalMachine::memWriteDebug(uint16_t a, uint8_t d) {
    if (a >= 0x1000 && a < 0x1800) einheit_.hw().ram()[a - 0x1000] = d;
}

// ── Save-State „P8TM" v1 ─────────────────────────────────────────────────────

std::vector<uint8_t> P8000TerminalMachine::stateBytes() const {
    std::vector<uint8_t> out(MAGIC, MAGIC + 4);
    out.push_back(STAND);
    out.push_back(static_cast<uint8_t>(cfg_.einheit.hw.zeichentaktTeiler));
    einheit_.serialize(out);
    return out;
}

bool P8000TerminalMachine::restoreStateBytes(const std::vector<uint8_t>& b) {
    state_error_.clear();
    if (b.size() < 6 || std::memcmp(b.data(), MAGIC, 4) != 0) { state_error_ = "Kein P8TM-Zustand"; return false; }
    if (b[4] != STAND) { state_error_ = "P8TM-Version " + std::to_string(b[4]) + " unbekannt"; return false; }
    if (b[5] != static_cast<uint8_t>(cfg_.einheit.hw.zeichentaktTeiler)) {
        state_error_ = "P8TM: Konfiguration (Zeichentakt-Teiler) stimmt nicht ueberein";
        return false;
    }
    const std::vector<uint8_t> sicherung = stateBytes();
    const uint8_t* p = b.data() + 6;
    if (einheit_.deserialize(p, b.data() + b.size())) {
        einheit_.hub().gastZurueckgesetzt();
        return true;
    }
    const uint8_t* q = sicherung.data() + 6;
    einheit_.deserialize(q, sicherung.data() + sicherung.size());
    state_error_ = "P8TM: Terminalzustand unlesbar";
    return false;
}

bool P8000TerminalMachine::saveState(const std::string& path) const {
    const std::vector<uint8_t> b = stateBytes();
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(f);
}

bool P8000TerminalMachine::loadState(const std::string& path) {
    state_error_.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) { state_error_ = "Zustandsdatei nicht lesbar: " + path; return false; }
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return restoreStateBytes(b);
}

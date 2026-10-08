/**
 * @file dbg_p8000_terminal.h
 * @brief `k1520dbg --terminal`: das Originalterminal P8000 Typ 2 (Firmware P8T 5.0 + Tastatur
 *        K7673.09, Entwurf 28) als eigenständige Einheit im Debugger.  Der Kontext `cpu z8`
 *        (tools/dbg_z8.h) arbeitet auf dem Z8 des Terminals; jeder Schritt läuft durch die
 *        Einheit (Video/DMA, Tastatur, Watchdog laufen mit).
 *
 * Zusatzkommandos (help):
 *   term                 Bild (24 Zeilen), Cursor, Zeichensatz, Klingel, Watchdog
 *   crt                  8275: Parameter, Cursor, Blinkphase, DMA-Zähler, Bilder
 *   host <text>          Zeichen vom Rechner auf die Leitung (\e \r \n \t \xNN)
 *   key <text>           Text über die Tastaturmatrix tippen (gleiche Escapes)
 *   taste <name>         MODE VIDEO SI_SO ON_OFF BREAK CR ESC DEL HOME VT LF FF BS HT BACKTAB
 *                        PAGE_ERASE LINE_INSERT LINE_DELETE CHAR_INSERT CHAR_DELETE
 *   matrix <zeile> <spalte>   eine Matrixtaste drücken/loslassen
 *   lauf <ms> | ruhe     Einheit laufen lassen | bis die Firmware ruht
 *   aus                  vom Terminal gesendete Zeichen (seit dem letzten `aus`)
 *
 * @license MIT
 */
#pragma once
#include "core/peripherals/p8000_terminal_hw/terminal_einheit.h"
#include "tools/dbg_z8.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace dbgp8kt {

inline std::string escapes(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { r += s[i]; continue; }
        const char c = s[++i];
        if (c == 'e') r += '\x1b';
        else if (c == 'r') r += '\r';
        else if (c == 'n') r += '\n';
        else if (c == 't') r += '\t';
        else if (c == 's') r += ' ';
        else if (c == 'x' && i + 2 < s.size()) {
            r += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else r += c;
    }
    return r;
}

inline std::string sichtbar(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7F) r += char(c);
        else r += dbgz8::fmt("<%02X>", c);
    }
    return r;
}

/// Zusatzkommandos auf einer Einheit; @p aus sammelt die gesendeten Zeichen.
inline bool befehl(k1520::p8000::P8000TerminalEinheit& e, std::string& aus,
                   const std::vector<std::string>& t, const std::string& zeile, std::string& out) {
    using k1520::p8000::TerminalTaste;
    auto& hw = e.hw();
    auto sammeln = [&] {
        while (hw.hatAusgabe()) {
            const auto s = hw.holeAusgabe();
            aus += s.brk ? std::string("<BREAK>") : std::string(1, char(s.byte));
        }
    };
    auto rest = [&]() -> std::string {   // Text hinter dem Kommando, Leerzeichen erhalten
        const size_t p = zeile.find(t[0]);
        std::string r = p == std::string::npos ? "" : zeile.substr(p + t[0].size());
        if (!r.empty() && r[0] == ' ') r.erase(0, 1);
        return escapes(r);
    };
    const std::string& k = t[0];
    if (k == "term") {
        for (int z = 0; z < 24; ++z) {
            std::string l = hw.text(z);
            while (!l.empty() && l.back() == ' ') l.pop_back();
            out += dbgz8::fmt("%2d|%s\n", z, sichtbar(l).c_str());
        }
        out += dbgz8::fmt("Cursor %d,%d  Zeichensatz %d  Klingel %u  Watchdog-Resets %u  Bilder %llu\n",
                          hw.cursorZeile(), hw.cursorSpalte(), hw.zeichensatz2() ? 2 : 1, hw.klingel(),
                          hw.watchdogResets(), (unsigned long long)hw.bilder());
        return true;
    }
    if (k == "crt") {
        const I8275& c = hw.crt();
        out += dbgz8::fmt("8275: %s, %dx%d, %d Linien, Unterstrich %d, VRTC %d, HRTC %d, %s, Cursor %s%s\n",
                          c.configured() ? "programmiert" : "nicht programmiert", c.cols(), c.rows(), c.lineCount(),
                          c.underlineLine(), c.vrtcRows(), c.hrtcChars(), c.transparent() ? "transparent" : "nicht transparent",
                          c.cursorUnderline() ? "Unterstrich" : "Block", c.cursorBlinks() ? " blinkend" : "");
        out += dbgz8::fmt("      Anzeige %s, IE %d, Status %02X, Cursor %d,%d, Blinkphase %s\n",
                          c.displayEnabled() ? "an" : "aus", c.interruptEnabled(), c.peekStatus(), c.cursorRow(),
                          c.cursorCol(), c.charBlinkOn() ? "hell" : "dunkel");
        out += dbgz8::fmt("      Zeilen-DMA: %llu angefordert, %llu bedient; Bilder %llu\n",
                          (unsigned long long)hw.dmaAnforderungen(), (unsigned long long)hw.dmaBedient(),
                          (unsigned long long)hw.bilder());
        return true;
    }
    if (k == "host") { for (unsigned char c : rest()) hw.hostByte(c); return true; }
    if (k == "key") {
        const std::string s = rest();
        if (!e.tippe(s)) out += "  nicht jedes Zeichen hat eine Taste\n";
        e.tastenAbwarten();
        sammeln();
        return true;
    }
    if (k == "taste") {
        static const std::map<std::string, TerminalTaste> namen = {
            {"MODE", TerminalTaste::MODE}, {"VIDEO", TerminalTaste::VIDEO}, {"SI_SO", TerminalTaste::SI_SO},
            {"ON_OFF", TerminalTaste::ON_OFF}, {"BREAK", TerminalTaste::BREAK}, {"CR", TerminalTaste::CR},
            {"ESC", TerminalTaste::ESC}, {"DEL", TerminalTaste::DEL}, {"HOME", TerminalTaste::HOME},
            {"VT", TerminalTaste::VT}, {"LF", TerminalTaste::LF}, {"FF", TerminalTaste::FF}, {"BS", TerminalTaste::BS},
            {"HT", TerminalTaste::HT}, {"BACKTAB", TerminalTaste::BACKTAB}, {"PAGE_ERASE", TerminalTaste::PAGE_ERASE},
            {"LINE_INSERT", TerminalTaste::LINE_INSERT}, {"LINE_DELETE", TerminalTaste::LINE_DELETE},
            {"CHAR_INSERT", TerminalTaste::CHAR_INSERT}, {"CHAR_DELETE", TerminalTaste::CHAR_DELETE},
        };
        const auto it = t.size() > 1 ? namen.find(t[1]) : namen.end();
        if (it == namen.end() || !e.taste(it->second)) { out += "  taste MODE|VIDEO|SI_SO|ON_OFF|BREAK|CR|…\n"; return true; }
        e.tastenAbwarten();
        sammeln();
        return true;
    }
    if (k == "matrix") {
        long z = -1, s = -1;
        if (t.size() < 3 || !dbgz8::zahl(t[1], z) || !dbgz8::zahl(t[2], s) || z < 0 || z > 7 || s < 0 || s > 15) {
            out += "  matrix <zeile 0-7> <spalte 0-15>\n";
            return true;
        }
        e.matrixTaste({int(z), int(s)});
        e.tastenAbwarten();
        sammeln();
        return true;
    }
    if (k == "lauf") {
        long ms = 100;
        if (t.size() > 1) dbgz8::zahl(t[1], ms);
        e.laufeMs(uint64_t(ms));
        sammeln();
        out += dbgz8::fmt("  %llu Takte, PC=%04X\n", (unsigned long long)e.takte(), hw.z8().pc);
        return true;
    }
    if (k == "ruhe") {
        const bool ok = e.ruheAbwarten();
        sammeln();
        out += ok ? dbgz8::fmt("  ruht, PC=%04X\n", hw.z8().pc) : std::string("  ruht nicht (5 s)\n");
        return true;
    }
    if (k == "aus") {
        sammeln();
        out += "  " + sichtbar(aus) + "\n";
        aus.clear();
        return true;
    }
    return false;
}

inline const char* hilfe() {
    return "Terminal (--terminal):\n"
           "  term | crt              Bild/Cursor | 8275-Zustand\n"
           "  host <text>             Zeichen vom Rechner (\\e \\r \\n \\t \\s \\xNN)\n"
           "  key <text> | taste <n>  über die Tastatur K7673 tippen | Funktionstaste\n"
           "  matrix <z> <s>          Matrixtaste drücken/loslassen\n"
           "  lauf <ms> | ruhe        Einheit laufen lassen | bis die Firmware ruht\n"
           "  aus                     vom Terminal gesendete Zeichen\n";
}

/// `k1520dbg --terminal [-x skript]`: Einheit einschalten, Kommandos aus @p script, dann stdin.
inline int pruefstand(const char* script) {
    k1520::p8000::P8000TerminalEinheit e;
    auto& hw = e.hw();
    dbgz8::Speicher sp;
    sp.prog = [&hw](uint16_t a) { return hw.programm(a); };
    sp.daten = [&hw](uint16_t a) -> uint8_t { return (a >= 0x1000 && a < 0x1800) ? hw.ram()[a - 0x1000] : 0xFF; };
    sp.datenSchreiben = [&hw](uint16_t a, uint8_t v) { if (a >= 0x1000 && a < 0x1800) hw.ram()[a - 0x1000] = v; };
    dbgz8::Kontext k(hw.z8(), sp);
    k.schritt = [&e] { e.laufeBis(e.takte() + 1); };
    std::string aus;
    std::string aktuelleZeile;
    k.zusatz = [&](const std::vector<std::string>& t, std::string& out) { return befehl(e, aus, t, aktuelleZeile, out); };
    k.zusatzHilfe = hilfe();
    k.maxSchritte = 5'000'000;
    std::printf("P8000-Terminal Typ 2 (P8T 5.0 + K7673.09) — help für Kommandos\n");
    auto quelle = [&](FILE* in, bool prompt) {
        char zeile[1024];
        for (;;) {
            if (prompt) { std::printf("term> "); std::fflush(stdout); }
            if (!std::fgets(zeile, sizeof zeile, in)) return true;
            std::string z = zeile;
            while (!z.empty() && (z.back() == '\n' || z.back() == '\r')) z.pop_back();
            if (!z.empty() && z[0] == '#') continue;
            if (!prompt && !z.empty()) std::printf("term> %s\n", z.c_str());
            aktuelleZeile = z;
            std::string out;
            const bool weiter = k.befehl(z == "cpu z8" ? "r" : z, out);
            std::fputs(out.c_str(), stdout);
            if (!weiter) return false;
        }
    };
    if (script) {
        FILE* s = std::fopen(script, "r");
        if (!s) { std::fprintf(stderr, "Skript '%s' nicht lesbar\n", script); return 2; }
        const bool weiter = quelle(s, false);
        std::fclose(s);
        if (!weiter) return 0;
    }
    quelle(stdin, k1520::os::isTerminal(0));
    return 0;
}

}  // namespace dbgp8kt

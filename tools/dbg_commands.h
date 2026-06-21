/**
 * @file dbg_commands.h
 * @brief Kanonische Liste der k1520dbg-Kommandos + Präfix-Vervollständigung.
 *
 * Nur die Kommando-Schlüsselwörter und ein einfacher Präfix-Matcher — getrennt vom
 * großen Dispatch in k1520dbg.cpp, damit die Tab-Completion (readline) ohne Maschine
 * unit-testbar ist. Bei neuen Kommandos hier den Namen ergänzen (rein kosmetisch für
 * die Completion; die eigentliche Wirkung steckt im Dispatch).
 *
 * @license MIT
 */
#pragma once
#include <string>
#include <vector>

namespace dbgcmd {

/// Alle Kommando-Schlüsselwörter (inkl. gebräuchlicher Kurzformen).
inline const std::vector<std::string>& names() {
    static const std::vector<std::string> n = {
        // run / step / reverse
        "g","c","gu","s","s2","n","fin","rs","bs","snap","restore",
        // breakpoints + events
        "b","b2","tb","bd","bd2","be","bdis","be2","bdis2","bi","bi2","bl",
        "bint","bnmi","breti",
        // watch / log
        "wp","wpr","wb","wd","wl","iow","iob","iod","iol",
        "logpoint","lp","lpd","lpl","trace",
        // inspect
        "r","bt","d","e","u","x","list","l","set","disp","undisp","vars","dev",
        // memory / symbols / listings
        "load","save","sym","lst",
        // misc
        "mark","keys","screen","reset","alias","unalias","source","help","q","quit",
    };
    return n;
}

/// Alle Kommandonamen, die mit @p prefix beginnen (für Tab-Completion).
inline std::vector<std::string> match(const std::string& prefix) {
    std::vector<std::string> out;
    for (const auto& s : names())
        if (s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0)
            out.push_back(s);
    return out;
}

} // namespace dbgcmd

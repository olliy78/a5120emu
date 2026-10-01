/**
 * @file em_trace.h
 * @brief Eine Textzeile je Kommunikationstransaktion des Erweiterungsmoduls —
 *        gemeinsam für `boot_trace --em` und `k1520dbg emlog` (S5).
 *
 * Quelle ist @ref EM::setEventHook; hier steht nur, wie ein Ereignis aussieht.
 *
 * @license MIT
 */
#pragma once
#include <cstdio>
#include <string>
#include "core/cards/em/em.h"

namespace emtrace {

/// "A34-Vektor8  U880  OUT ADH=04  (VI angefordert)".
inline std::string text(const EM::EreignisInfo& e) {
    using E = EM::Ereignis;
    char b[128];
    const char* who = e.by16 ? "U8001" : "U880 ";
    switch (e.kind) {
        case E::PioSchreiben:
            std::snprintf(b, sizeof b, "%-18s %s OUT PIO %s=%02X", EM::ereignisName(e.kind), who,
                          e.addr == 0 ? "A-Daten" : e.addr == 1 ? "B-Daten"
                                      : e.addr == 2 ? "A-Steuer" : "B-Steuer", e.value & 0xFF);
            break;
        case E::PioLesen:
            std::snprintf(b, sizeof b, "%-18s %s IN  PIO %s=%02X", EM::ereignisName(e.kind), who,
                          e.addr == 0 ? "A-Daten" : e.addr == 1 ? "B-Daten"
                                      : e.addr == 2 ? "A-Steuer" : "B-Steuer", e.value & 0xFF);
            break;
        case E::Status8: case E::Vektor8:
            std::snprintf(b, sizeof b, "%-18s %s OUT %02XH=%02X%s", EM::ereignisName(e.kind), who,
                          e.addr & 0xFF, e.value & 0xFF,
                          e.kind == E::Vektor8 ? "  (VI an U8001)" : "");
            break;
        case E::Status16Lesen:
            std::snprintf(b, sizeof b, "%-18s %s IN  %02XH=%02X", EM::ereignisName(e.kind), who,
                          e.addr & 0xFF, e.value & 0xFF);
            break;
        case E::A22Schreiben: case E::A22Lesen:
            std::snprintf(b, sizeof b, "%-18s %s %s Seite %X=%X", EM::ereignisName(e.kind), who,
                          e.kind == E::A22Schreiben ? "OUT" : "IN ", e.addr & 0xF, e.value & 0xFF);
            break;
        case E::A33A35:
            std::snprintf(b, sizeof b, "%-18s %s OUT %%%04X  A33=%02X A35=%02X",
                          EM::ereignisName(e.kind), who, e.addr, e.value & 0xFF, e.value >> 8);
            break;
        case E::Status8Lesen:
            std::snprintf(b, sizeof b, "%-18s %s IN  %%%04X  Status-8=%02X",
                          EM::ereignisName(e.kind), who, e.addr, e.value & 0xFF);
            break;
        case E::ViQuittung:
            std::snprintf(b, sizeof b, "%-18s %s Kennung=%04X", EM::ereignisName(e.kind), who,
                          e.value);
            break;
        case E::Modus:
            std::snprintf(b, sizeof b, "%-18s %s-Bit-Mode", EM::ereignisName(e.kind),
                          e.value ? "8" : "16");
            break;
        default:
            std::snprintf(b, sizeof b, "%-18s %u", EM::ereignisName(e.kind), unsigned(e.value));
            break;
    }
    return b;
}

}  // namespace emtrace

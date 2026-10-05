/**
 * @file band_format.cpp
 * @brief Roh, Intel HEX und ASCII-Art für 8-Spur-Lochstreifen, siehe band_format.h.
 */

#include "core/peripherals/lochstreifen/band_format.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace lochstreifen {
namespace {

// ─── Zeilen ──────────────────────────────────────────────────────────────────

/// Inhalt in Zeilen zerlegen (LF; ein CR davor fällt weg).  Eine abschließende leere
/// Zeile nach dem letzten LF zählt nicht.
std::vector<std::string> zeilen(const std::vector<uint8_t>& inhalt) {
    std::vector<std::string> z;
    std::string akt;
    for (uint8_t b : inhalt) {
        if (b == '\n') {
            if (!akt.empty() && akt.back() == '\r') akt.pop_back();
            z.push_back(std::move(akt));
            akt.clear();
        } else {
            akt.push_back(static_cast<char>(b));
        }
    }
    if (!akt.empty()) {
        if (akt.back() == '\r') akt.pop_back();
        z.push_back(std::move(akt));
    }
    return z;
}

bool leer(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }

std::string trimmen(const std::string& s) {
    std::size_t a = 0, e = s.size();
    while (a < e && leer(s[a])) ++a;
    while (e > a && leer(s[e - 1])) --e;
    return s.substr(a, e - a);
}

int hexZiffer(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string zeileFehler(std::size_t nr, const std::string& was) {
    return "Zeile " + std::to_string(nr) + ": " + was;
}

void anhaengen(std::vector<uint8_t>& out, const char* s) {
    while (*s) out.push_back(static_cast<uint8_t>(*s++));
}

// ─── Intel HEX ───────────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> hexLesen(const std::vector<uint8_t>& inhalt,
                                             std::string& fehler) {
    std::vector<uint8_t> band;
    uint64_t basis = 0;                       // aus Satzart 02 (×16) bzw. 04 (×65536)
    const std::vector<std::string> z = zeilen(inhalt);
    for (std::size_t i = 0; i < z.size(); ++i) {
        const std::size_t nr = i + 1;
        const std::string s = trimmen(z[i]);
        if (s.empty()) continue;
        if (s[0] != ':') {
            fehler = zeileFehler(nr, "Intel-HEX-Satz muss mit ':' beginnen");
            return std::nullopt;
        }
        if ((s.size() - 1) % 2 != 0 || s.size() < 11) {
            fehler = zeileFehler(nr, "Satz unvollständig");
            return std::nullopt;
        }
        std::vector<uint8_t> b;
        for (std::size_t k = 1; k < s.size(); k += 2) {
            const int h = hexZiffer(s[k]), l = hexZiffer(s[k + 1]);
            if (h < 0 || l < 0) {
                fehler = zeileFehler(nr, "keine Hexziffer");
                return std::nullopt;
            }
            b.push_back(static_cast<uint8_t>(h * 16 + l));
        }
        const std::size_t n = b[0];
        if (b.size() != n + 5) {
            fehler = zeileFehler(nr, "Längenangabe " + std::to_string(n) +
                                         " passt nicht zum Satz");
            return std::nullopt;
        }
        uint8_t summe = 0;
        for (uint8_t v : b) summe = static_cast<uint8_t>(summe + v);
        if (summe != 0) {
            fehler = zeileFehler(nr, "Prüfsumme falsch");
            return std::nullopt;
        }
        const uint32_t adr = (static_cast<uint32_t>(b[1]) << 8) | b[2];
        const uint8_t art = b[3];
        switch (art) {
        case 0x00: {                                    // Daten
            const uint64_t ende = basis + adr + n;
            if (ende > kMaxBandLaenge) {
                fehler = zeileFehler(nr, "Bandposition jenseits von 16 MiB");
                return std::nullopt;
            }
            if (band.size() < ende) band.resize(static_cast<std::size_t>(ende), 0x00);
            for (std::size_t k = 0; k < n; ++k) band[static_cast<std::size_t>(basis + adr + k)] = b[4 + k];
            break;
        }
        case 0x01:                                      // Dateiende: Rest überlesen
            return band;
        case 0x02:                                      // erweiterte Segmentadresse
        case 0x04:                                      // erweiterte lineare Adresse
            if (n != 2) {
                fehler = zeileFehler(nr, "Satzart 0" + std::to_string(art) + " braucht 2 Bytes");
                return std::nullopt;
            }
            basis = ((static_cast<uint64_t>(b[4]) << 8) | b[5]) << (art == 0x02 ? 4 : 16);
            break;
        case 0x03:                                      // Startadressen: für ein Band
        case 0x05:                                      // bedeutungslos
            break;
        default: {
            char buf[48];
            std::snprintf(buf, sizeof buf, "unbekannte Satzart %02X", art);
            fehler = zeileFehler(nr, buf);
            return std::nullopt;
        }
        }
    }
    return band;   // ohne Satz 01 nachsichtig: alles Gelesene gilt
}

std::vector<uint8_t> hexSchreiben(const std::vector<uint8_t>& band) {
    std::vector<uint8_t> out;
    out.reserve(band.size() * 45 / 16 + 16);
    char buf[64];
    auto satz = [&](uint8_t art, uint16_t adr, const uint8_t* d, std::size_t n) {
        uint8_t summe = static_cast<uint8_t>(n + (adr >> 8) + (adr & 0xFF) + art);
        std::snprintf(buf, sizeof buf, ":%02X%04X%02X", static_cast<unsigned>(n), adr, art);
        anhaengen(out, buf);
        for (std::size_t k = 0; k < n; ++k) {
            summe = static_cast<uint8_t>(summe + d[k]);
            std::snprintf(buf, sizeof buf, "%02X", d[k]);
            anhaengen(out, buf);
        }
        std::snprintf(buf, sizeof buf, "%02X\n", static_cast<uint8_t>(0x100 - summe) & 0xFF);
        anhaengen(out, buf);
    };
    for (std::size_t pos = 0; pos < band.size(); pos += 16) {
        // 16 teilt 65536: ein Satz überschreitet nie eine 64-KiB-Grenze.
        if (pos != 0 && (pos & 0xFFFF) == 0) {
            const uint8_t hoch[2] = {static_cast<uint8_t>(pos >> 24), static_cast<uint8_t>(pos >> 16)};
            satz(0x04, 0, hoch, 2);
        }
        const std::size_t n = std::min<std::size_t>(16, band.size() - pos);
        satz(0x00, static_cast<uint16_t>(pos & 0xFFFF), band.data() + pos, n);
    }
    anhaengen(out, ":00000001FF\n");
    return out;
}

// ─── ASCII-Art ───────────────────────────────────────────────────────────────
//
// Feste Spalten (Spalte 0 = Zeilenanfang):
//
//   ;    8 7 6 5 4   3 2 1          ← Kopf: Spurnummern genau über den Löchern
//   ;  +-------------------+          ← Bandkante (Kommentar)
//      | . O . . . o . . O |  41  A   ← `|` in Spalte 3 und 23, Wert ab 26, Zeichen 30
//
// Zwischen den beiden `|` stehen 19 Zeichen: an den ungeraden Stellen 1, 3, … 17 die
// Spuren 8 7 6 5 4, das Transportloch, die Spuren 3 2 1; an den geraden Leerzeichen.

constexpr std::size_t kInnen = 19;
/// Stelle im Inneren (nach dem linken `|`) je Datenspur 1…8, Index = Spur − 1.
constexpr std::size_t kSpurStelle[8] = {17, 15, 13, 9, 7, 5, 3, 1};
constexpr std::size_t kTransportStelle = 11;

const char* const kKopf =
    "; K1520-Lochstreifen, 8 Spuren, ASCII-Art (doc/design/23_lochstreifen.md)\n"
    "; Eine Zeile = eine Sprosse, der Streifen laeuft von oben nach unten.\n"
    "; O = Loch, . = kein Loch, o = Transportloch; rechts Wert (hex) und Zeichen.\n"
    ";\n"
    ";    8 7 6 5 4   3 2 1\n";
const char* const kRand = ";  +-------------------+\n";

bool istLoch(char c) { return c == 'O' || c == 'X' || c == '*' || c == '#'; }
bool istKeinLoch(char c) { return c == '.' || c == ' '; }

std::vector<uint8_t> artSchreiben(const std::vector<uint8_t>& band) {
    std::vector<uint8_t> out;
    out.reserve(256 + band.size() * 34);
    anhaengen(out, kKopf);
    anhaengen(out, kRand);
    for (uint8_t v : band) {
        char innen[kInnen + 1];
        for (std::size_t k = 0; k < kInnen; ++k) innen[k] = ' ';
        innen[kInnen] = '\0';
        for (int spur = 0; spur < 8; ++spur) innen[kSpurStelle[spur]] = (v >> spur) & 1 ? 'O' : '.';
        innen[kTransportStelle] = 'o';
        char buf[64];
        if (v > 0x20 && v < 0x7F)
            std::snprintf(buf, sizeof buf, "   |%s|  %02X  %c\n", innen, v, static_cast<char>(v));
        else
            std::snprintf(buf, sizeof buf, "   |%s|  %02X\n", innen, v);
        anhaengen(out, buf);
    }
    anhaengen(out, kRand);
    return out;
}

/// Erste Zeile, die weder leer noch Kommentar ist, ist eine Lochbildzeile `|…|`?
/// Nur Kommentare (ein leeres Band, wie es @ref artSchreiben anlegt) zählen auch.
bool artErkannt(const std::vector<std::string>& z) {
    bool kommentar = false;
    for (const std::string& roh : z) {
        const std::string s = trimmen(roh);
        if (s.empty()) continue;
        if (s[0] == ';') { kommentar = true; continue; }
        return s.size() >= 2 && s[0] == '|' && s.find('|', 1) != std::string::npos;
    }
    return kommentar;
}

std::optional<std::vector<uint8_t>> artLesen(const std::vector<uint8_t>& inhalt,
                                             std::string& fehler) {
    std::vector<uint8_t> band;
    const std::vector<std::string> z = zeilen(inhalt);
    for (std::size_t i = 0; i < z.size(); ++i) {
        const std::size_t nr = i + 1;
        const std::string& s = z[i];
        std::size_t a = 0;
        while (a < s.size() && leer(s[a])) ++a;
        if (a == s.size() || s[a] == ';') continue;               // leer oder Kommentar
        if (s[a] != '|') {
            fehler = zeileFehler(nr, "weder Kommentar (;) noch Lochbild (|…|)");
            return std::nullopt;
        }
        const std::size_t e = s.find('|', a + 1);
        if (e == std::string::npos) {
            fehler = zeileFehler(nr, "rechte Begrenzung '|' fehlt");
            return std::nullopt;
        }
        const std::string innen = s.substr(a + 1, e - a - 1);
        if (innen.size() != kInnen) {
            fehler = zeileFehler(nr, "Lochbild hat " + std::to_string(innen.size()) + " statt " +
                                         std::to_string(kInnen) + " Spalten");
            return std::nullopt;
        }
        uint8_t v = 0;
        for (std::size_t k = 0; k < kInnen; ++k) {
            const char c = innen[k];
            int spur = -1;
            for (int t = 0; t < 8; ++t) if (kSpurStelle[t] == k) spur = t;
            if (spur >= 0) {
                if (istLoch(c)) v = static_cast<uint8_t>(v | (1u << spur));
                else if (!istKeinLoch(c)) {
                    fehler = zeileFehler(nr, std::string("Spur ") + std::to_string(spur + 1) +
                                                 ": unbekanntes Zeichen '" + c + "'");
                    return std::nullopt;
                }
            } else if (k == kTransportStelle) {
                if (c != 'o' && !istLoch(c) && !istKeinLoch(c)) {
                    fehler = zeileFehler(nr, std::string("Transportspur: unbekanntes Zeichen '") + c + "'");
                    return std::nullopt;
                }
            } else if (c != ' ') {
                // Ein verrutschtes Loch zwischen den Spuren würde sonst still verloren.
                fehler = zeileFehler(nr, "Zeichen zwischen den Spuren (Spalten verrutscht?)");
                return std::nullopt;
            }
        }
        // Rechts daneben: Hexwert (optional), danach das Zeichen (wird nicht gelesen).
        const std::string rest = trimmen(s.substr(e + 1));
        if (!rest.empty()) {
            std::size_t n = 0;
            int wert = 0;
            while (n < rest.size() && !leer(rest[n])) {
                const int h = hexZiffer(rest[n]);
                if (h < 0 || n >= 2) { wert = -1; break; }
                wert = wert * 16 + h;
                ++n;
            }
            if (wert < 0) {
                fehler = zeileFehler(nr, "rechts vom Lochbild steht kein Hexwert");
                return std::nullopt;
            }
            if (wert != v) {
                char buf[96];
                std::snprintf(buf, sizeof buf,
                              "Lochbild ergibt %02X, daneben steht %02X", v, wert);
                fehler = zeileFehler(nr, buf);
                return std::nullopt;
            }
        }
        band.push_back(v);
    }
    return band;
}

}  // namespace

// ─── Öffentlich ──────────────────────────────────────────────────────────────

std::vector<Format> alleFormate() { return {Format::Roh, Format::IntelHex, Format::AsciiArt}; }

std::optional<Format> formatAusZahl(int kennung) {
    switch (kennung) {
    case 0: return Format::Roh;
    case 1: return Format::IntelHex;
    case 2: return Format::AsciiArt;
    default: return std::nullopt;
    }
}

const char* formatName(Format f) {
    switch (f) {
    case Format::IntelHex: return "Intel HEX";
    case Format::AsciiArt: return "ASCII-Art";
    case Format::Roh: break;
    }
    return "Roh";
}

std::vector<std::string> formatEndungen(Format f) {
    switch (f) {
    case Format::IntelHex: return {".hex", ".ihx"};
    case Format::AsciiArt: return {".txt", ".tape"};
    case Format::Roh: break;
    }
    return {".ptp", ".bin"};
}

Format formatAusEndung(const std::string& pfad) {
    const std::size_t trenner = pfad.find_last_of("/\\");
    const std::size_t punkt = pfad.find_last_of('.');
    if (punkt == std::string::npos || (trenner != std::string::npos && punkt < trenner))
        return Format::Roh;
    std::string end = pfad.substr(punkt);
    for (char& c : end) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (Format f : alleFormate())
        for (const std::string& e : formatEndungen(f))
            if (e == end) return f;
    return Format::Roh;
}

Format erkennen(const std::vector<uint8_t>& inhalt) {
    const std::vector<std::string> z = zeilen(inhalt);
    bool hex = false;
    for (const std::string& roh : z) {
        const std::string s = trimmen(roh);
        if (s.empty()) continue;
        if (s[0] != ':') { hex = false; break; }
        hex = true;
    }
    if (hex) return Format::IntelHex;
    if (artErkannt(z)) return Format::AsciiArt;
    return Format::Roh;
}

std::optional<std::vector<uint8_t>> lesen(const std::vector<uint8_t>& inhalt, Format f,
                                          std::string& fehler) {
    switch (f) {
    case Format::IntelHex: return hexLesen(inhalt, fehler);
    case Format::AsciiArt: return artLesen(inhalt, fehler);
    case Format::Roh: break;
    }
    return inhalt;
}

std::vector<uint8_t> schreiben(const std::vector<uint8_t>& band, Format f) {
    switch (f) {
    case Format::IntelHex: return hexSchreiben(band);
    case Format::AsciiArt: return artSchreiben(band);
    case Format::Roh: break;
    }
    return band;
}

}  // namespace lochstreifen

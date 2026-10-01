/**
 * @file k8915_bedienung.h
 * @brief Bedienhelfer für die K8915-Systemtests (FORMAT.COM, DISGEN.COM): Bild lesen,
 *        auf Text warten, Felder ausfüllen — über die Tastatur K7672 (DCP-Modus), wie
 *        ein Anwender.  Nur für `tests/system/test_k8915_format*.cpp`.
 *
 * Die Menüfelder von FORMAT.COM und DISGEN.COM lesen über BDOS 6 (Direkt-Konsole) ohne
 * Umwandlung — Großbuchstaben (`Y`, `B`, `W`) müssen also groß getippt werden; die K7672
 * legt dafür selbst Umschalt um (`K7672::sendeZeichen`).
 */
#pragma once

#include <string>

#include "core/machines/k8915/k8915.h"
#include "tests/support/screen.h"

namespace k8915test {

constexpr int kSchritt = 100'000;

inline bool enthaelt(K8915Machine& m, const std::string& s) {
    return k1520test::vramText(m).find(s) != std::string::npos;
}

/// Läuft, bis @p text im Bild steht; false nach @p frist Takten.
inline bool bis(K8915Machine& m, const std::string& text, long long frist) {
    for (long long t = 0; t < frist; t += m.run(kSchritt))
        if (enthaelt(m, text)) return true;
    return enthaelt(m, text);
}

/// Läuft, bis @p text NICHT mehr im Bild steht.
inline bool bisWeg(K8915Machine& m, const std::string& text, long long frist) {
    for (long long t = 0; t < frist; t += m.run(kSchritt))
        if (!enthaelt(m, text)) return true;
    return !enthaelt(m, text);
}

/// Letzte nicht leere Bildzeile, rechts ohne Leerzeichen.
inline std::string letzteZeile(K8915Machine& m) {
    const std::string t = k1520test::vramText(m);
    std::string letzte;
    for (size_t z = 0; z + 80 <= t.size(); z += 80) {
        std::string zeile = t.substr(z, 80);
        while (!zeile.empty() && (zeile.back() == ' ' || zeile.back() == '\0')) zeile.pop_back();
        if (!zeile.empty()) letzte = zeile;
    }
    return letzte;
}

/// Tippen wie ein Mensch: Zeichen für Zeichen, jedes erst, wenn das vorige abgeholt ist
/// (K7672 fertig, Tastaturpuffer F150H leer).  FORMAT.COM liest über BDOS 6 und räumt beim
/// Aufbau eines Feldes den Puffer — vorausgetippte Zeichen gingen dabei verloren.
inline void tippe(K8915Machine& m, const std::string& text) {
    for (char c : text) {
        m.keyboard().sendeZeichen(static_cast<uint8_t>(c));
        for (long long t = 0; t < 20'000'000 && (m.keyboard().sendetNoch()
                                                  || m.memReadDebug(0xF150) != 0);
             t += m.run(kSchritt)) {}
        m.run(500'000);
    }
}

/// Bis @p text im Bild steht, dann das Programm am Eingabefeld ankommen lassen.
inline bool feld(K8915Machine& m, const std::string& text, long long frist = 20'000'000) {
    if (!bis(m, text, frist)) return false;
    m.run(2'000'000);
    return true;
}

/// Bis zum stabilen Prompt: „A>“ als letzte Zeile, Tastaturpuffer (F150H) leer.
inline bool bisPrompt(K8915Machine& m, long long frist) {
    for (long long t = 0; t < frist; t += m.run(kSchritt))
        if (letzteZeile(m) == "A>" && !m.keyboard().sendetNoch() && m.memReadDebug(0xF150) == 0)
            return true;
    return false;
}

/// CCP-Kommando: tippen, Echo abwarten, dann wieder am Prompt.
inline bool befehl(K8915Machine& m, const std::string& cmd, long long frist = 60'000'000) {
    for (char c : cmd) m.keyboard().sendeZeichen(static_cast<uint8_t>(c));
    m.keyboard().sendeZeichen(0x0D);
    bool echo = false;
    for (long long t = 0; t < frist; t += m.run(kSchritt)) {
        const bool prompt = letzteZeile(m) == "A>";
        echo = echo || !prompt;
        if (echo && prompt && !m.keyboard().sendetNoch() && m.memReadDebug(0xF150) == 0)
            return true;
    }
    return false;
}

/// Kurzer Weg zur Coldstart-Meldung: `JP` bei 0000H/0005H ⇒ das ROM überspringt den
/// Selbsttest wie nach einem Reset (doc/merkposten/k8915.md), dann `CR` und bis zum
/// Prompt hinter dem Autostart.
inline bool kaltstartBisPrompt(K8915Machine& m) {
    m.powerOn();
    m.zre().bankPoke(0, 0x0000, 0xC3);
    m.zre().bankPoke(0, 0x0005, 0xC3);
    if (!bis(m, "* Coldstart *  Disk on A: ready", 5'000'000)) return false;
    m.keyboard().sendeZeichen(0x0D);
    if (!bis(m, "SCPX 8915", 150'000'000)) return false;
    return bisPrompt(m, 150'000'000);
}

/// Parameter eines FORMAT.COM-Laufs (Felder in der Reihenfolge des Menüs).
struct FormatLauf {
    std::string verfahren;           ///< KIND OF FORMATTING, zweistellig („24“)
    char        laufwerk = 'B';      ///< DEVICE
    std::string erste    = "00";     ///< FIRST TRACK
    std::string letzte   = "79";     ///< LAST TRACK
    std::string versatz  = "01";     ///< SKEWFACTOR
};

/// FORMAT.COM am Prompt starten, alle Felder ausfüllen, `RUN` mit `Y` bestätigen und
/// bis „FUNCTION COMPLETE“ bzw. „ERROR“ laufen.  Danach `EXIT` mit `Y` ⇒ Warmstart,
/// zurück am Prompt.  Liefert das Bild vor dem Verlassen (für die Prüfung).
///
/// Befund AP-E4f (§6.14 in doc/design/16_k8915.md): vor `RUN` stehen FÜNF Felder
/// (KIND, DEVICE, FIRST, LAST, SKEWFACTOR), und das Y/N-Feld nimmt ein `Y` erst mit
/// `CR` an — wer nach vier `CR` ein `Y` tippt, landet im SKEWFACTOR-Feld.
inline std::string formatiere(K8915Machine& m, const FormatLauf& f, long long frist) {
    const long long frist_feld = 20'000'000;
    for (char c : std::string("format\r")) m.keyboard().sendeZeichen(static_cast<uint8_t>(c));
    if (!feld(m, "KIND OF FORMATTING", 60'000'000)) return "kein Menü\n" + k1520test::vramLines(m);
    tippe(m, f.verfahren + "\r");
    if (!feld(m, "DEVICE (A, B, C, D)", frist_feld)) return "kein DEVICE\n" + k1520test::vramLines(m);
    tippe(m, std::string(1, f.laufwerk) + "\r");
    if (!feld(m, "FIRST TRACK", frist_feld)) return "kein FIRST\n" + k1520test::vramLines(m);
    tippe(m, f.erste + "\r");
    if (!feld(m, "LAST TRACK", frist_feld)) return "kein LAST\n" + k1520test::vramLines(m);
    tippe(m, f.letzte + "\r");
    if (!feld(m, "SKEWFACTOR", frist_feld)) return "kein SKEW\n" + k1520test::vramLines(m);
    tippe(m, f.versatz + "\r");
    if (!feld(m, "RUN (Y=YES/N=NO)", frist_feld)) return "kein RUN\n" + k1520test::vramLines(m);
    tippe(m, "Y\r");
    // Die KROS-Verfahren (Fussnote 1) fragen NACH dem Formatieren, ob Spur 0 mit dem
    // Informationsblock (128 B) beschrieben werden soll — Vorgabe N, mit CR bestätigt;
    // FORMAT.COM geht dann ohne „FUNCTION COMPLETE“ direkt zu EXIT (0282H–02E7H).
    bool kros_gefragt = false;
    for (long long t = 0; t < frist; t += m.run(kSchritt)) {
        if (enthaelt(m, "FUNCTION COMPLETE") || enthaelt(m, "ERROR")
            || enthaelt(m, "EXIT (Y=YES/N=NO)")) break;
        if (!kros_gefragt && enthaelt(m, "TRACK 0 WITH 128 BYTE/SECTOR")) {
            kros_gefragt = true;
            tippe(m, "\r");
        }
    }
    const std::string bild = k1520test::vramLines(m);
    if (feld(m, "EXIT (Y=YES/N=NO)", frist_feld)) {
        tippe(m, "Y\r");
        bisPrompt(m, 60'000'000);
    }
    return bild;
}

}  // namespace k8915test

/**
 * @file kopplung.h
 * @brief Kopplung 8-Bit ↔ 16-Bit des P8000 (X1/X2 der 8-Bit-Karte ↔ X2/X3 der 16-Bit-Karte) als
 *        reines Leitungsmodell.
 *
 * Quelle: doc/design/25_p8000.md §10.3 (Leitungen K1–K15), doc/p8000/schaltplan_8bit.md §7,
 * doc/p8000/schaltplan_16bit.md §5 (Rang 1).  Kein Protokollwissen, kein Puffer: bei jeder
 * Änderung einer Quelle (PIO-Schreiben/-Reset beider Karten, RDY-Flanke, DS8282-Schreiben)
 * werden ALLE Leitungen aus den Pinpegeln der Quellen neu gerechnet und an die Senken gelegt.
 *
 * @code
 *   K1   D0–7/8-16   DS8282 L1 (10H)          → 16-PIO1 A-Pins
 *   K2   INT-16      L2 (14H) Bit 0           → 16-PIO1 B0
 *   K3   V1–6/8-16   L2 Bit 1–6               → 16-PIO1 B1–B6
 *   K4   RDY/8-16    L2 Bit 7                 → 16-PIO1 /ASTB   (Software-Strobe)
 *   K5   E0-8        16-PIO1 ARDY             → 8-PIO0 B6 (Eingang)
 *   K6   D0–7/16-8   16-PIO0 A-Pins           → 8-PIO0 A-Pins
 *   K7   DD-8        16-PIO0 ARDY             → 8-PIO0 /ASTB und 8-PIO0 B5
 *   K8   RDY/16-8    8-PIO0 ARDY              → 16-PIO0 /ASTB
 *   K9   INT-8       16-PIO0 B0 (Pinpegel)    → 8-PIO0 B0
 *   K10  V1–4/16-8   16-PIO0 B1–B4            → 8-PIO0 B1–B4
 *   K11  RESET       8-PIO0 B7eff             → 16-Bit X3:B1 (high-aktiv; offen = Pull-up = Reset)
 *   K12  NMIU8000−   NMI-Weiche der 8-Bit-Karte → 16-Bit MANUALNMI (Tastenimpuls)
 *   K13  TRESET−     —                         (Pull-up ⇒ inaktiv; nicht nachgebildet)
 *   K14  RUN-LED     16-Bit RS-FF             → `runLed()` (Anzeige)
 *   K15  DD-16/E0-16 Rückführung              → 16-PIO0 B5 = E0-8, B6 = DD-8  (s. [KP1]); B7 offen
 * @endcode
 *
 * **[KP1] Rückführung 16-PIO0 B5/B6.** MON16 `ENTRY_` (`p.init.s` KP1–KP9) liest B5 als
 * „DATEN DA" (0 = Byte von der 8-Bit-Seite liegt in PIO1 A) und B6 als „ENABLE OUTPUT"
 * (0 = 8-Bit-Seite hat das letzte Byte abgeholt) — das sind genau E0-8 (16-PIO1 ARDY) und
 * DD-8 (16-PIO0 ARDY).  Index 1 der 16-Bit-Karte führt sie über die Wickelbrücken 4XR1/5XR1
 * zurück (`Config::rueckfuehrung` = Brücken gesteckt); beim Paar Index 3/4 kommen DD-16/E0-16
 * über X2:A11/A9 von der 8-Bit-Karte, deren Plan fehlt — **Annahme**: dieselben Signale.  Ohne
 * Rückführung (Pull-up) liefe MON16 in die Wartezeit und nähme die Konsole an tty5 (P10c);
 * das Geräteprotokoll zeigt sie aber an tty1.
 *
 * Save-State: die zuletzt gerechneten Pegel (Anzeige/Flankenerkennung K11).  Die Senken selbst
 * stehen im Zustand der Karten (PIO-Pins, Handshake, Reset-Eingang).
 */

#pragma once
#include <cstdint>
#include <vector>

class P8000Karte8;
class P8000Karte16;

class P8000Kopplung {
public:
    struct Config {
        /// [KP1] 16-PIO0 B5/B6 lesen E0-8/DD-8 zurück (Index 1: Brücken 4XR1/5XR1; Index 3/4: X2).
        bool rueckfuehrung = true;
    };

    /// Gerechnete Leitungspegel (true = H), für Debugger/Tests (`kopp`, P12).
    struct Pegel {
        uint8_t d8_16  = 0xFF;   ///< K1
        uint8_t l2     = 0xFF;   ///< K2–K4 (Bit 0 INT-16, 1–6 V1–6, 7 RDY/8-16)
        bool    e0_8   = false;  ///< K5
        uint8_t d16_8  = 0xFF;   ///< K6
        bool    dd_8   = false;  ///< K7
        bool    rdy16_8 = false; ///< K8
        uint8_t b16_8  = 0x1F;   ///< K9/K10 (Bit 0 INT-8, 1–4 V1–4)
        bool    reset16 = true;  ///< K11
        bool    run    = true;   ///< K14
    };

    P8000Kopplung(P8000Karte8& k8, P8000Karte16& k16) : P8000Kopplung(k8, k16, Config{}) {}
    P8000Kopplung(P8000Karte8& k8, P8000Karte16& k16, const Config& cfg);
    ~P8000Kopplung();
    P8000Kopplung(const P8000Kopplung&) = delete;
    P8000Kopplung& operator=(const P8000Kopplung&) = delete;

    /// Alle Leitungen neu rechnen und an die Senken legen (Rückrufe der Karten rufen es selbst).
    void rechne();
    const Pegel& pegel() const { return pegel_; }
    const Config& config() const { return cfg_; }

    /// Während des Ladens eines Zustands nichts rechnen (die Karten stellen ihre Pins selbst her).
    void setRuhig(bool r) { ruhig_ = r; }

    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    void einmal();

    P8000Karte8&  k8_;
    P8000Karte16& k16_;
    Config cfg_;
    Pegel  pegel_;
    bool   in_rechne_ = false, nochmal_ = false, ruhig_ = false, gerechnet_ = false;
};

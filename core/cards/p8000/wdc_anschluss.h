/**
 * @file wdc_anschluss.h
 * @brief Anschluss des WDC an die PIO2 der 16-Bit-Karte (X8/X16, Bl. 13) als reines
 *        Leitungsmodell — Gegenstück zu `P8000Kopplung`.  AP P13d.
 *
 * Quelle: doc/p8000/schaltplan_16bit.md §6 (Rang 1), MON16 `p.disk.s` und WEGA `disk.s`
 * (Kopfkommentar „Anschlussbelegung"), doc/p8000/wdc_firmware.md §4.
 *
 * @code
 *   PIO2-A0…A7  ↔ D0…D7 über DS8286 (1D26); Richtung = B6 (1 = WDC → PIO)
 *   PIO2-ARDY   → WDARDY− (Byte liegt an bzw. Register leer)
 *   PIO2-/ASTB  ← ASTB− (Impuls des WDC je übertragenem Byte)
 *   PIO2-B0…B2  ← NOT ST0…ST2 (DL540, invertierend)
 *   PIO2-B5     → RST (1 = WDC im Reset; Pull-up ⇒ nach PIO-Reset im Reset)
 *   PIO2-B6     → TE− = NOT B6 (B6 = 1: Host liest) und Richtung des Datentreibers
 *   PIO2-B7     ← NOT TR− (TR aktiv ⇒ 1; von keinem Treiber gelesen)
 * @endcode
 *
 * Bei jeder Änderung einer Quelle (CPU-Schreiben/Reset der PIO2 über `setzeWdcHaken`, ARDY-Rückruf,
 * Status-/TR-Rückruf und ASTB-Impuls des WDC) werden alle Leitungen neu gerechnet.
 *
 * Annahmen:
 *  - [A1] B6 als Eingang (nach PIO-Reset) liest über den Pull-up 1 ⇒ Treiber Richtung PIO, TE
 *         aktiv.  Ohne Wirkung, solange B5 (ebenfalls Pull-up) den WDC im Reset hält.
 *  - [A2] Bei B6 = 0 treibt der DS8286 die PIO-Pins zum WDC; die PIO-A-Pins sieht der WDC nur
 *         dann, wenn die PIO sie treibt (Modus 0) — sonst offen (1).
 */
#pragma once
#include <cstdint>

class P8000Karte16;
class P8000Wdc;

class P8000WdcAnschluss {
public:
    P8000WdcAnschluss(P8000Karte16& k16, P8000Wdc& wdc);
    ~P8000WdcAnschluss();
    P8000WdcAnschluss(const P8000WdcAnschluss&) = delete;
    P8000WdcAnschluss& operator=(const P8000WdcAnschluss&) = delete;

    /// Alle Leitungen neu rechnen (die Rückrufe rufen es selbst).
    void rechne();
    /// Während des Ladens eines Zustands nichts rechnen.
    void setRuhig(bool r) { ruhig_ = r; }

private:
    void einmal();
    void astb();

    P8000Karte16& k16_;
    P8000Wdc&     wdc_;
    bool in_rechne_ = false, nochmal_ = false, ruhig_ = false;
};

/**
 * @file event_bp.h
 * @brief Ereignis-Breakpoint-Erkennung für k1520dbg (Interrupt / NMI / RETI).
 *
 * k1520dbg erkennt diese Ereignisse OHNE Core-Eingriff aus dem Per-Instruktions-
 * Trace-Callback per Zustands-Signatur (vgl. `bint`/`bnmi`/`breti`):
 *   - maskierbarer Interrupt akzeptiert: IFF1 fiel 1→0 *und* SP wurde um 2 dekrementiert
 *     (PC gepusht), Sprungziel ≠ NMI-Vektor;
 *   - NMI akzeptiert: Sprung auf 0x0066 mit SP-Push (IFF1 darf bereits 0 gewesen sein!);
 *   - RETI/RETN: Opcode `ED 4D` / `ED 45` an der nächsten Instruktion.
 *
 * Diese Klassifikation ist reine Logik → ohne Maschine unit-testbar (besonders der
 * NMI-bei-bereits-deaktivierten-Interrupts-Grenzfall, den die IFF1-Signatur nicht fängt).
 *
 * @license MIT
 */
#pragma once
#include <cstdint>

namespace eventbp {

enum class Event { None, Interrupt, NMI, RETI, RETN };

/// Zustand der *vorigen* ZVE1-Instruktion (für die Signatur-Erkennung).
struct Prev {
    bool     have = false;   ///< false vor der ersten beobachteten Instruktion
    uint16_t sp   = 0;
    bool     iff1 = false;
};

/**
 * Klassifiziert, welches *scharfgeschaltete* Ereignis an der aktuellen Instruktion feuert.
 * @param pc,sp,iff1   Zustand der aktuellen (ersten ISR-/RETI-)Instruktion.
 * @param prev         Zustand der vorigen Instruktion.
 * @param armInt/Nmi/Reti  welche Ereignis-Breakpoints aktiv sind.
 * @param op0,op1      die beiden Bytes an @p pc (nur für RETI/RETN relevant).
 * @return das feuernde Ereignis (Priorität NMI > Interrupt > RETI/RETN) oder None.
 */
inline Event classify(uint16_t pc, uint16_t sp, bool iff1, const Prev& prev,
                      bool armInt, bool armNmi, bool armReti,
                      uint8_t op0, uint8_t op1) {
    // NMI: fester Vektor 0x0066 + frischer SP-Push (unabhängig von IFF1).
    if (armNmi && pc == 0x0066 && prev.have && sp == (uint16_t)(prev.sp - 2))
        return Event::NMI;
    // Maskierbarer Interrupt: IFF1 1→0 beim PC-Push, nicht der NMI-Vektor.
    if (armInt && prev.have && prev.iff1 && !iff1
        && sp == (uint16_t)(prev.sp - 2) && pc != 0x0066)
        return Event::Interrupt;
    // RETI / RETN: ED 4D / ED 45 (vor der Ausführung).
    if (armReti && op0 == 0xED) {
        if (op1 == 0x4D) return Event::RETI;
        if (op1 == 0x45) return Event::RETN;
    }
    return Event::None;
}

} // namespace eventbp

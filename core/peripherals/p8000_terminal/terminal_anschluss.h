/**
 * @file terminal_anschluss.h
 * @brief Gegenstelle eines `SerialAnschluss`: hängt ein `Terminal` fest an einen
 *        Kanal der Karte (Entwurf 25 §10.6, wie die Tastatur am PC 1715).
 *
 * @details
 * `takt()` läuft im Emulationsfaden.  Zeichen werden im Takt des GASTFORMATS
 * (`SerialFormat::zeichen_takte`) übernommen bzw. geliefert; zum Host nur bei
 * `empfaengerFrei()` — kein Überlauf, daher kein XOFF.  Ohne gültiges Format gilt
 * 9600 Bd 8N2 (11 Bit).  BREAK-Taste ⇒ `breakEmpfang(true)` für eine Zeichenzeit.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "core/peripherals/p8000_terminal/terminal.h"
#include "core/serial/anschluss.h"

namespace k1520::p8000 {

class TerminalAnschluss {
public:
    TerminalAnschluss(serial::SerialAnschluss& karte, Terminal& term,
                      uint64_t phiNenn = 4'000'000)
        : karte_(karte), term_(term), phi_(phiNenn) {}

    /// `takte` Maschinentakte sind vergangen.
    void takt(uint64_t takte);

    /// Gastformat weicht von 9600 Bd / 8 Datenbits ab (Anzeige; Zeichen werden trotzdem gezeigt).
    bool baudAbweichend() const;

    /// Save-State (P8KS): Zeichenzeit-Reste und BREAK-Zustand.
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    uint64_t zeichenTakte() const;

    serial::SerialAnschluss& karte_;
    Terminal& term_;
    uint64_t phi_;
    uint64_t rxRest_ = 0, txRest_ = 0, breakRest_ = 0;
    bool breakAktiv_ = false;
};

}  // namespace k1520::p8000

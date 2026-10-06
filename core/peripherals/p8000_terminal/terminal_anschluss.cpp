#include "core/peripherals/p8000_terminal/terminal_anschluss.h"

namespace k1520::p8000 {

uint64_t TerminalAnschluss::zeichenTakte() const {
    const serial::SerialFormat f = karte_.format();
    if (f.gueltig && f.zeichen_takte) return f.zeichen_takte;
    return 11ull * phi_ / 9600;   // 1 Start + 8 Daten + 2 Stopp
}

bool TerminalAnschluss::baudAbweichend() const {
    const serial::SerialFormat f = karte_.format();
    return f.gueltig && (f.baud_nenn != 9600 || f.daten != 8);
}

void TerminalAnschluss::takt(uint64_t takte) {
    auto ab = [takte](uint64_t& r) { r = r > takte ? r - takte : 0; };
    ab(rxRest_); ab(txRest_); ab(breakRest_);

    // Host → Terminal: Zeichen aus dem SIO-Sender im Zeichentakt übernehmen.
    if (rxRest_ == 0 && karte_.senderHatZeichen()) {
        term_.eingabe(karte_.senderNimm());
        rxRest_ = zeichenTakte();
    }
    // Terminal → Host: nur bei freiem Empfänger.
    if (txRest_ == 0 && term_.hatAusgabe() && karte_.empfaengerFrei()) {
        karte_.empfange(term_.holeAusgabe());
        txRest_ = zeichenTakte();
    }
    // BREAK: eine Zeichenzeit lang.
    if (!breakAktiv_ && term_.breakAnstehend()) {
        term_.breakQuittieren();
        karte_.breakEmpfang(true);
        breakAktiv_ = true;
        breakRest_ = zeichenTakte();
    } else if (breakAktiv_ && breakRest_ == 0) {
        karte_.breakEmpfang(false);
        breakAktiv_ = false;
    }
}

}  // namespace k1520::p8000

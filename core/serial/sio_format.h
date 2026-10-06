/**
 * @file sio_format.h
 * @brief Hilfe für die Karten (AP-S5): `SerialFormat` aus einem `Z80SIO::Channel` und
 *        der CTC, die ihn taktet (Entwurf 19 §6.2).
 *
 * Header-only, damit `k1520_serial` die SIO-/CTC-Bibliotheken nicht bindet; wer diese
 * Kopfdatei einbindet, hat die Bausteine ohnehin.  Typischer Gebrauch in einer Karte:
 *
 * @code
 *   SerialFormat format() const override {
 *       return serialFormatAusSio(sio_.channelA(), ctc_, taktKanal_);
 *   }
 * @endcode
 */
#pragma once

#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_sio.h"
#include "core/serial/anschluss.h"

namespace k1520::serial {

/// Format eines SIO-Kanals bei bekannter CTC-Teilung (Takte je ZC/TO-Impuls, 0 = unbekannt).
inline SerialFormat serialFormatAusSio(const ::Z80SIO::Channel& kanal, uint64_t ctcTakte,
                                       uint64_t phiNenn = PHI_NENN) {
    const ::Z80SIO::Channel::Format f = kanal.format();
    return serialFormatRechnen(f.teiler, f.tx_bits, f.paritaet, f.stopp_halbe, ctcTakte,
                               phiNenn);
}

/// Dasselbe mit der taktenden CTC und ihrem Kanal (`Z80CTC::teilerTakte`, bei Kaskaden
/// rekursiv über die dort verdrahtete Eingangsquelle).
inline SerialFormat serialFormatAusSio(const ::Z80SIO::Channel& kanal, const ::Z80CTC& ctc,
                                       int ctcKanal, uint64_t phiNenn = PHI_NENN) {
    return serialFormatAusSio(kanal, ctc.teilerTakte(ctcKanal), phiNenn);
}

/// Wie oben mit gebrochener CTC-Periode (Z80CTC::teilerTakteQ16, Bruchtakt-Eingang, P5d).
inline SerialFormat serialFormatAusSioQ16(const ::Z80SIO::Channel& kanal, const ::Z80CTC& ctc,
                                          int ctcKanal, uint64_t phiNenn = PHI_NENN) {
    const ::Z80SIO::Channel::Format f = kanal.format();
    return serialFormatRechnenQ16(f.teiler, f.tx_bits, f.paritaet, f.stopp_halbe,
                                  ctc.teilerTakteQ16(ctcKanal), phiNenn);
}

}  // namespace k1520::serial

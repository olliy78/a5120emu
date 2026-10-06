#include "core/serial/anschluss.h"

#include <cmath>

namespace k1520::serial {

namespace {
// Normraten der V.24-Welt (und was die DDR-BIOSse anbieten).
constexpr uint32_t NORMRATEN[] = {50,   75,   110,   134,   150,   200,   300,   600,
                                  1200, 1800, 2400,  3600,  4800,  7200,  9600,  14400,
                                  19200, 28800, 38400, 57600, 76800, 115200};
}  // namespace

uint32_t normBaud(double baud) {
    if (!(baud > 0)) return 0;
    for (uint32_t n : NORMRATEN)
        if (std::fabs(baud - n) < 0.02 * n) return n;
    return static_cast<uint32_t>(std::lround(baud));
}

SerialFormat serialFormatRechnen(uint8_t sioTeiler, uint8_t daten, uint8_t paritaet,
                                 uint8_t stopp_halbe, uint64_t ctcTakte, uint64_t phiNenn) {
    SerialFormat f;
    f.daten       = daten;
    f.paritaet    = paritaet;
    f.stopp_halbe = stopp_halbe;
    const uint64_t bitTakte = static_cast<uint64_t>(sioTeiler) * ctcTakte;
    if (bitTakte == 0 || sioTeiler == 0) return f;          // CTC unbekannt → ungültig
    f.baud_nenn = normBaud(static_cast<double>(phiNenn) / static_cast<double>(bitTakte));
    // Synchronbetrieb (Stoppbits 0) hat keine Zeichenzeit im Sinne des Wandlers.
    if (stopp_halbe < 2 || stopp_halbe > 4 || daten < 5 || daten > 8) return f;
    // In halben Bits rechnen, damit 1½ Stoppbits exakt bleiben.
    const uint64_t halbe = 2u * (1u + daten + (paritaet ? 1u : 0u)) + stopp_halbe;
    f.zeichen_takte = halbe * bitTakte / 2;
    f.gueltig       = f.zeichen_takte > 0;
    return f;
}

SerialFormat serialFormatRechnenQ16(uint8_t sioTeiler, uint8_t daten, uint8_t paritaet,
                                    uint8_t stopp_halbe, uint64_t ctcTakteQ16,
                                    uint64_t phiNenn) {
    SerialFormat f;
    f.daten       = daten;
    f.paritaet    = paritaet;
    f.stopp_halbe = stopp_halbe;
    const uint64_t bitQ16 = static_cast<uint64_t>(sioTeiler) * ctcTakteQ16;
    if (bitQ16 == 0 || sioTeiler == 0) return f;
    f.baud_nenn = normBaud(static_cast<double>(phiNenn) * 65536.0 / static_cast<double>(bitQ16));
    if (stopp_halbe < 2 || stopp_halbe > 4 || daten < 5 || daten > 8) return f;
    const uint64_t halbe = 2u * (1u + daten + (paritaet ? 1u : 0u)) + stopp_halbe;
    f.zeichen_takte = (halbe * bitQ16 / 2 + 0x8000u) >> 16;   // gerundet
    f.gueltig       = f.zeichen_takte > 0;
    return f;
}

SerialFormat ersatzFormat(uint64_t phiNenn) {
    // 9600 Bd 8N1: 10 Bit je Zeichen.  Direkt aus φ gerechnet (nicht über einen
    // SIO-Teiler), damit es auch bei einem Nenntakt ungleich 2,4576 MHz 9600 bleibt.
    SerialFormat f;
    f.baud_nenn     = 9600;
    f.daten         = 8;
    f.paritaet      = 0;
    f.stopp_halbe   = 2;
    f.zeichen_takte = phiNenn * 10 / 9600;
    f.gueltig       = true;
    return f;
}

}  // namespace k1520::serial

/**
 * @file vis2a.cpp
 * @brief VIS 2a des MC80.3x — Bildpunkte, IN-Register, Bildkommandos, Anzeige.
 * @see vis2a.h, doc/mc80/hardware_3x.md §5
 */

#include "core/cards/mc80/vis2a.h"
#include "core/logger.h"
#include <algorithm>

Mc80Vis2a::Mc80Vis2a(K1520Bus& bus) : Mc80Vis2a(bus, Config{}) {}

Mc80Vis2a::Mc80Vis2a(K1520Bus& bus, const Config& cfg)
    : bus_(bus), cfg_(cfg),
      bild_(static_cast<size_t>(BREITE) * HOEHE, 0),
      fb_(static_cast<size_t>(BREITE) * HOEHE, 0)
{
}

void Mc80Vis2a::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.io_base, 16);
}

void Mc80Vis2a::powerOn()
{
    std::fill(bild_.begin(), bild_.end(), 0);
    roll_   = 0;
    ein_    = true;
    in_reg_ = 0xFF;
    anzeigeNeu();
}

void Mc80Vis2a::setzePunkt(int x, int y, bool hell)
{
    uint8_t& p = bild_[idx(x, y)];
    const uint8_t neu = hell ? 1 : 0;
    if (p == neu) return;
    p = neu;
    anzeigePunkt(x, y);
}

// ─── E/A ─────────────────────────────────────────────────────────────────────

uint8_t Mc80Vis2a::ioRead(uint8_t)
{
    return in_reg_;
}

void Mc80Vis2a::ioWrite(uint8_t port, uint8_t data)
{
    const uint8_t y     = static_cast<uint8_t>(bus_.ioAddress() >> 8);   // B auf A8–A15
    const int     x     = ((port & 0x08) ? 256 : 0) + data;
    const uint8_t fkt   = port & 0x07;
    switch (fkt) {
        case 0: setzePunkt(x, y, false); break;
        case 1: setzePunkt(x, y, true);  break;
        case 2: in_reg_ = static_cast<uint8_t>(0xFE | (punkt(x, y) ? 0 : 1)); break;   // [?] s. Kopf
        case 3: in_reg_ = static_cast<uint8_t>(0xFE | (punkt(x, y) ? 1 : 0)); break;
        case 4: case 5: kommando(y, (fkt & 1) != 0); break;
        default:
            LOG_DEBUG("MC80-VIS", "Funktion %u nicht belegt (Port %02XH)", fkt, port);
            break;
    }
}

void Mc80Vis2a::kommando(uint8_t op, bool hell)
{
    const uint8_t h = op >> 4, n = op & 0x0F;
    switch (h) {
        case 0:
            switch (n & 3) {
                case 0:
                    std::fill(bild_.begin(), bild_.end(), hell ? 1 : 0);
                    roll_ = 0;
                    anzeigeNeu();
                    break;
                case 1: ein_ = false; anzeigeNeu(); break;
                case 2: ein_ = true;  anzeigeNeu(); break;
                default: break;
            }
            break;
        case 1:   // nach oben: R -= n, unten erscheinen die alten obersten Zeilen -> fuellen
            if (n == 0) { roll_ = 0; anzeigeNeu(); break; }
            roll_ = static_cast<uint8_t>(roll_ - n);
            for (int d = HOEHE - n; d < HOEHE; ++d) fuelleSpeicherzeile((255 - d + roll_) & 0xFF, hell);
            anzeigeNeu();
            break;
        case 2:   // nach unten: R += n, oben neue Zeilen
            if (n == 0) { roll_ = 0; anzeigeNeu(); break; }
            roll_ = static_cast<uint8_t>(roll_ + n);
            for (int d = 0; d < n; ++d) fuelleSpeicherzeile((255 - d + roll_) & 0xFF, hell);
            anzeigeNeu();
            break;
        case 5:
            LOG_DEBUG("MC80-VIS", "Lichtstift-Initialisierung (ohne Lichtstift)");
            break;
        default:
            LOG_DEBUG("MC80-VIS", "Bildkommando %02XH ohne Wirkung", op);
            break;
    }
}

void Mc80Vis2a::fuelleSpeicherzeile(int y, bool hell)
{
    std::fill_n(bild_.begin() + static_cast<long>(idx(0, y)), BREITE, hell ? 1 : 0);
}

// ─── Anzeige ─────────────────────────────────────────────────────────────────

void Mc80Vis2a::anzeigeNeu()
{
    for (int d = 0; d < HOEHE; ++d) {
        const uint8_t* q = &bild_[idx(0, (255 - d + roll_) & 0xFF)];
        uint8_t* z = &fb_[static_cast<size_t>(d) * BREITE];
        for (int x = 0; x < BREITE; ++x) z[x] = (ein_ && q[x]) ? 0xFF : 0x00;
    }
    geaendert();
}

void Mc80Vis2a::anzeigePunkt(int x, int y)
{
    const int d = (255 - y + roll_) & 0xFF;
    fb_[static_cast<size_t>(d) * BREITE + (x & 0x1FF)] = (ein_ && bild_[idx(x, y)]) ? 0xFF : 0x00;
    geaendert();
}

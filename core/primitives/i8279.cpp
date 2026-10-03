/**
 * @file i8279.cpp
 * @brief Intel 8279 (nur Tastaturteil) — siehe i8279.h.
 */

#include "core/primitives/i8279.h"
#include "core/logger.h"

void I8279::reset()
{
    fifo_.clear();
    mode_ = 0;
    last_cmd_ = 0;
    unterlauf_ = ueberlauf_ = false;
}

void I8279::writeCommand(uint8_t cmd)
{
    last_cmd_ = cmd;
    switch (cmd >> 5) {
        case 0:   // Betriebsart: 02H = kodierte Abtastung, N-Tasten-Rollover
            mode_ = cmd;
            break;
        case 6:   // Clear (C1H = Clear All): FIFO und Zustandsbits löschen.
                  // CF (Bit 1) und CA (Bit 0) löschen beide den FIFO; das Anzeige-RAM fehlt.
            if (cmd & 0x03) {
                fifo_.clear();
                unterlauf_ = ueberlauf_ = false;
            }
            break;
        default:
            LOG_DEBUG("I8279", "Befehl %02X nicht nachgebildet (Anzeige/Takt/Fehlerbetrieb)", cmd);
            break;
    }
}

uint8_t I8279::readStatus() const
{
    uint8_t s = static_cast<uint8_t>(fifo_.size() & ST_ANZAHL);
    if (fifo_.size() >= FIFO_TIEFE) s |= ST_VOLL;
    if (unterlauf_) s |= ST_UNTERLAUF;
    if (ueberlauf_) s |= ST_UEBERLAUF;
    return s;
}

uint8_t I8279::readData()
{
    if (fifo_.empty()) {
        unterlauf_ = true;
        return 0xFF;
    }
    const uint8_t c = fifo_.front();
    fifo_.pop_front();
    return c;
}

void I8279::writeData(uint8_t d)
{
    LOG_DEBUG("I8279", "Datenschreiben %02X (Anzeige-RAM nicht nachgebildet)", d);
}

bool I8279::pushKey(uint8_t code)
{
    if (fifo_.size() >= FIFO_TIEFE) {
        ueberlauf_ = true;
        return false;
    }
    fifo_.push_back(code);
    return true;
}

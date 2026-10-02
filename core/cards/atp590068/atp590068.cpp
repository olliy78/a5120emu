/**
 * @file atp590068.cpp
 * @brief ATP 590068 (8279 + EPROMmer-Attrappe) — siehe atp590068.h.
 */

#include "core/cards/atp590068/atp590068.h"
#include "core/logger.h"

Atp590068::Atp590068(const Config& cfg) : cfg_(cfg) {}

void Atp590068::attachToBus(K1520Bus& bus)
{
    bus.registerIO(&tastatur_, cfg_.kbc_base, 2);
    bus.registerIO(&eprommer_, cfg_.eprommer_base, 4);
}

void Atp590068::Eprommer::ioWrite(uint8_t port, uint8_t d)
{
    LOG_DEBUG("ATP590068", "[?] EPROMmer D%XH := %02X (Attrappe)", port, d);
}

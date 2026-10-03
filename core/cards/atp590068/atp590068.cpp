/**
 * @file atp590068.cpp
 * @brief ATP 590068 (8279 + EPROMmer) — siehe atp590068.h.
 */

#include "core/cards/atp590068/atp590068.h"
#include "core/logger.h"

Atp590068::Atp590068(const Config& cfg)
    : cfg_(cfg), eprommer_(Eprommer590068::Config{cfg.eprommer_base, 2'457'600}) {}

void Atp590068::attachTastatur(K1520Bus& bus)
{
    bus.registerIO(&tastatur_, cfg_.kbc_base, 2);
}

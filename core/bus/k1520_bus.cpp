#include "k1520_bus.h"
#include "core/logger.h"
#include <stdexcept>
#include <algorithm>

void K1520Bus::registerIO(BusDevice* dev, uint8_t basePort, uint8_t numPorts) {
    for (int i = 0; i < numPorts; ++i) {
        uint8_t p = basePort + i;
        if (io_map_[p] && io_map_[p] != dev)
            throw std::runtime_error("K1520Bus: I/O port conflict");
        io_map_[p] = dev;
    }
    LOG_DEBUG("K1520Bus", "registerIO: %s ports 0x%02X-0x%02X (%u ports)",
              dev->deviceName(), basePort, (uint8_t)(basePort + numPorts - 1), numPorts);
}

void K1520Bus::registerMem(MemDevice* dev, uint16_t base, uint16_t size) {
    mem_regions_.push_back({base, size, dev});
    LOG_DEBUG("K1520Bus", "registerMem: Gerät @0x%04X-0x%04X (Größe=%u)",
              base, (uint16_t)(base + size - 1), size);
}

void K1520Bus::unregisterMem(MemDevice* dev) {
    mem_regions_.erase(
        std::remove_if(mem_regions_.begin(), mem_regions_.end(),
            [dev](const MemRegion& r) { return r.dev == dev; }),
        mem_regions_.end());
    LOG_DEBUG("K1520Bus", "unregisterMem: Gerät entfernt");
}

void K1520Bus::setInterruptChain(std::initializer_list<InterruptSlave*> chain) {
    int_chain_.assign(chain.begin(), chain.end());
    LOG_DEBUG("K1520Bus", "Interrupt-Daisy-Chain: %zu Geräte gesetzt", int_chain_.size());
}

uint8_t K1520Bus::memRead(uint16_t addr) {
    if (memdi_) {
        LOG_TRACE("K1520Bus", "memRead 0x%04X: /MEMDI aktiv → 0xFF", addr);
        return 0xFF;
    }
    MemDevice* hit = nullptr;
    for (auto& r : mem_regions_)
        if (addr >= r.base && addr < static_cast<uint32_t>(r.base + r.size))
            hit = r.dev;
    uint8_t val = hit ? hit->memRead(addr) : 0xFF;
    if (trace_cb_) trace_cb_(false, true, addr, val);
    LOG_TRACE("K1520Bus", "MEM RD 0x%04X => 0x%02X%s", addr, val, hit ? "" : " (kein Gerät)");
    return val;
}

void K1520Bus::memWrite(uint16_t addr, uint8_t data) {
    if (memdi_) {
        LOG_TRACE("K1520Bus", "memWrite 0x%04X=0x%02X: /MEMDI aktiv → ignoriert", addr, data);
        return;
    }
    // Suche das letzte BESCHREIBBARE Gerät für diese Adresse.
    // Damit können Schreibzugriffe in den ROM-Adressbereich (0000H-03FFH)
    // korrekt an das Hintergrund-RAM (K3526) weitergeleitet werden,
    // auch wenn das Lade-ROM für Lesezugriffe Vorrang hat.
    MemDevice* hit = nullptr;
    for (auto& r : mem_regions_)
        if (addr >= r.base && addr < static_cast<uint32_t>(r.base + r.size))
            if (r.dev->isWritable())
                hit = r.dev;
    if (hit)
        hit->memWrite(addr, data);
    if (trace_cb_) trace_cb_(false, false, addr, data);
    LOG_TRACE("K1520Bus", "MEM WR 0x%04X <= 0x%02X%s", addr, data, hit ? "" : " (kein beschreibbares Gerät)");
}

uint8_t K1520Bus::ioRead(uint8_t port) {
    if (iodi_) {
        LOG_TRACE("K1520Bus", "ioRead 0x%02X: /IODI aktiv → 0xFF", port);
        return 0xFF;
    }
    uint8_t val = io_map_[port] ? io_map_[port]->ioRead(port) : 0xFF;
    if (trace_cb_) trace_cb_(true, true, port, val);
    if (!io_map_[port])
        LOG_DEBUG("K1520Bus", "I/O RD 0x%02X => 0xFF (kein Gerät registriert)", port);
    return val;
}

void K1520Bus::ioWrite(uint8_t port, uint8_t data) {
    if (iodi_) {
        LOG_TRACE("K1520Bus", "ioWrite 0x%02X=0x%02X: /IODI aktiv → ignoriert", port, data);
        return;
    }
    if (!io_map_[port])
        LOG_DEBUG("K1520Bus", "I/O WR 0x%02X <= 0x%02X (kein Gerät registriert)", port, data);
    if (io_map_[port]) io_map_[port]->ioWrite(port, data);
    if (trace_cb_) trace_cb_(true, false, port, data);
}

void K1520Bus::updateInterruptChain() {
    bool iei = true;
    for (auto* dev : int_chain_) {
        dev->setIEI(iei);
        if (iei && dev->hasInterrupt())
            iei = false;
        iei = iei && dev->getIEO();
    }
    bool was_int = int_asserted_;
    int_asserted_ = false;
    for (auto* dev : int_chain_)
        if (dev->hasInterrupt()) { int_asserted_ = true; break; }
    if (int_asserted_ != was_int)
        LOG_DEBUG("K1520Bus", "INT: %s → %s", was_int ? "aktiv" : "inaktiv",
                  int_asserted_ ? "aktiv" : "inaktiv");
}

uint8_t K1520Bus::interruptAcknowledge() {
    for (auto* dev : int_chain_)
        if (dev->hasInterrupt()) {
            uint8_t vec = dev->getVector();
            LOG_DEBUG("K1520Bus", "INT-Quittung: Vektor=0x%02X von %s",
                      vec, dev->deviceName());
            return vec;
        }
    return 0xFF;
}

void K1520Bus::assertINT()    {
    if (!int_asserted_) LOG_DEBUG("K1520Bus", "assertINT()");
    int_asserted_ = true;
}
void K1520Bus::releaseINT()   {
    if (int_asserted_) LOG_DEBUG("K1520Bus", "releaseINT()");
    int_asserted_ = false;
}
void K1520Bus::assertNMI()    {
    LOG_DEBUG("K1520Bus", "assertNMI()");
    nmi_pending_ = true;
}
void K1520Bus::clearNMI()     {
    nmi_pending_ = false;
}
void K1520Bus::assertRESET()  {
    LOG_INFO("K1520Bus", "assertRESET()");
    reset_asserted_ = true;
}

void K1520Bus::signalRETI() {
    LOG_DEBUG("K1520Bus", "RETI-Signal: alle Geräte in Daisy-Chain benachrichtigt");
    for (auto* dev : int_chain_)
        dev->onRETI();
    updateInterruptChain();
}

void K1520Bus::assertWAIT()  {
    if (!wait_asserted_) LOG_DEBUG("K1520Bus", "assertWAIT()");
    wait_asserted_ = true;
}
void K1520Bus::releaseWAIT() {
    if (wait_asserted_) LOG_DEBUG("K1520Bus", "releaseWAIT()");
    wait_asserted_ = false;
}

void K1520Bus::assertBUSRQ() {
    if (!busrq_asserted_) LOG_DEBUG("K1520Bus", "assertBUSRQ(): ZVE2-DMA fordert Bus an");
    busrq_asserted_ = true;
}

void K1520Bus::releaseBUSRQ() {
    if (busrq_asserted_) LOG_DEBUG("K1520Bus", "releaseBUSRQ(): DMA abgeschlossen, ZVE1 übernimmt");
    busrq_asserted_ = false;
}

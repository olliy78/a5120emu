#include "k1520_bus.h"
#include <stdexcept>
#include <algorithm>

void K1520Bus::registerIO(BusDevice* dev, uint8_t basePort, uint8_t numPorts) {
    for (int i = 0; i < numPorts; ++i) {
        uint8_t p = basePort + i;
        if (io_map_[p] && io_map_[p] != dev)
            throw std::runtime_error("K1520Bus: I/O port conflict");
        io_map_[p] = dev;
    }
}

void K1520Bus::registerMem(MemDevice* dev, uint16_t base, uint16_t size) {
    mem_regions_.push_back({base, size, dev});
}

void K1520Bus::unregisterMem(MemDevice* dev) {
    mem_regions_.erase(
        std::remove_if(mem_regions_.begin(), mem_regions_.end(),
            [dev](const MemRegion& r) { return r.dev == dev; }),
        mem_regions_.end());
}

void K1520Bus::setInterruptChain(std::initializer_list<InterruptSlave*> chain) {
    int_chain_.assign(chain.begin(), chain.end());
}

uint8_t K1520Bus::memRead(uint16_t addr) {
    if (memdi_) return 0xFF;
    MemDevice* hit = nullptr;
    for (auto& r : mem_regions_)
        if (addr >= r.base && addr < static_cast<uint32_t>(r.base + r.size))
            hit = r.dev;
    uint8_t val = hit ? hit->memRead(addr) : 0xFF;
    if (trace_cb_) trace_cb_(false, true, addr, val);
    return val;
}

void K1520Bus::memWrite(uint16_t addr, uint8_t data) {
    if (memdi_) return;
    MemDevice* hit = nullptr;
    for (auto& r : mem_regions_)
        if (addr >= r.base && addr < static_cast<uint32_t>(r.base + r.size))
            hit = r.dev;
    if (hit && hit->isWritable())
        hit->memWrite(addr, data);
    if (trace_cb_) trace_cb_(false, false, addr, data);
}

uint8_t K1520Bus::ioRead(uint8_t port) {
    if (iodi_) return 0xFF;
    uint8_t val = io_map_[port] ? io_map_[port]->ioRead(port) : 0xFF;
    if (trace_cb_) trace_cb_(true, true, port, val);
    return val;
}

void K1520Bus::ioWrite(uint8_t port, uint8_t data) {
    if (iodi_) return;
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
    int_asserted_ = false;
    for (auto* dev : int_chain_)
        if (dev->hasInterrupt()) { int_asserted_ = true; break; }
}

uint8_t K1520Bus::interruptAcknowledge() {
    for (auto* dev : int_chain_)
        if (dev->hasInterrupt())
            return dev->getVector();
    return 0xFF;
}

void K1520Bus::assertINT()    { int_asserted_   = true; }
void K1520Bus::releaseINT()   { int_asserted_   = false; }
void K1520Bus::assertNMI()    { nmi_pending_    = true; }
void K1520Bus::assertRESET()  { reset_asserted_ = true; }
void K1520Bus::assertWAIT()   { wait_asserted_  = true; }
void K1520Bus::releaseWAIT()  { wait_asserted_  = false; }
// setMEMDI() is inline in header

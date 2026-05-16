#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <functional>
#include <initializer_list>

class BusDevice {
public:
    virtual ~BusDevice() = default;
    virtual uint8_t ioRead(uint8_t port)             { return 0xFF; }
    virtual void    ioWrite(uint8_t port, uint8_t d) {}
    virtual const char* deviceName() const            { return "?"; }
};

class MemDevice {
public:
    virtual ~MemDevice() = default;
    virtual uint8_t memRead(uint16_t addr)              = 0;
    virtual void    memWrite(uint16_t addr, uint8_t d)  = 0;
    virtual bool    isWritable() const { return true; }
};

class InterruptSlave {
public:
    virtual ~InterruptSlave() = default;
    virtual void    setIEI(bool)           = 0;
    virtual bool    getIEO() const         = 0;
    virtual bool    hasInterrupt() const   = 0;
    virtual uint8_t getVector() const { return 0xFF; }
};

class K1520Bus {
public:
    K1520Bus() = default;
    ~K1520Bus() = default;

    // ─── Device registration ─────────────────────────────────────────────
    void registerIO(BusDevice* dev, uint8_t basePort, uint8_t numPorts = 1);
    void registerMem(MemDevice* dev, uint16_t base, uint16_t size);
    void unregisterMem(MemDevice* dev);
    void setInterruptChain(std::initializer_list<InterruptSlave*> chain);

    // ─── Bus operations (called by Z80CPU) ──────────────────────────────
    uint8_t memRead(uint16_t addr);
    void    memWrite(uint16_t addr, uint8_t data);
    uint8_t ioRead(uint8_t port);
    void    ioWrite(uint8_t port, uint8_t data);
    uint8_t interruptAcknowledge();

    // ─── Signals ────────────────────────────────────────────────────────
    void assertINT();
    void releaseINT();
    void assertNMI();
    void assertRESET();
    void assertWAIT();
    void releaseWAIT();
    void setMEMDI(bool disabled);
    bool getMEMDI() const { return memdi_; }
    void setIODI(bool disabled) { iodi_ = disabled; }

    bool isINT()   const { return int_asserted_; }
    bool isNMI()   const { return nmi_pending_; }
    bool isWAIT()  const { return wait_asserted_; }
    bool isRESET() const { return reset_asserted_; }

    // ─── Trace / debug ──────────────────────────────────────────────────
    using BusTrace = std::function<void(bool isIO, bool isRead,
                                        uint16_t addr, uint8_t data)>;
    void setTraceCallback(BusTrace cb) { trace_cb_ = std::move(cb); }

    void updateInterruptChain();

private:
    std::array<BusDevice*, 256> io_map_{};

    struct MemRegion { uint16_t base, size; MemDevice* dev; };
    std::vector<MemRegion> mem_regions_;

    std::vector<InterruptSlave*> int_chain_;

    bool int_asserted_   = false;
    bool nmi_pending_    = false;
    bool wait_asserted_  = false;
    bool reset_asserted_ = false;
    bool memdi_          = false;
    bool iodi_           = false;

    BusTrace trace_cb_;
};

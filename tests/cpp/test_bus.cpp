#include <gtest/gtest.h>
#include "core/bus/k1520_bus.h"

// ─── Mock devices ─────────────────────────────────────────────────────────

class MockIO : public BusDevice {
public:
    uint8_t last_port_written = 0;
    uint8_t last_data_written = 0;
    uint8_t read_return       = 0xAB;
    int     write_count       = 0;

    uint8_t ioRead(uint8_t port) override { return read_return; }
    void    ioWrite(uint8_t port, uint8_t data) override {
        last_port_written = port;
        last_data_written = data;
        ++write_count;
    }
};

class MockMem : public MemDevice {
public:
    uint8_t buf[256] = {};
    uint16_t base;
    bool writable = true;

    explicit MockMem(uint16_t base) : base(base) { std::fill(buf, buf+256, 0xFF); }

    uint8_t memRead(uint16_t addr) override { return buf[addr - base]; }
    void    memWrite(uint16_t addr, uint8_t d) override { buf[addr - base] = d; }
    bool    isWritable() const override { return writable; }
};

class MockIRQ : public InterruptSlave {
public:
    bool iei_in    = false;
    bool irq_req   = false;
    uint8_t vector = 0xFF;

    void    setIEI(bool v)        override { iei_in = v; }
    bool    getIEO() const        override { return !(iei_in && irq_req); }
    bool    hasInterrupt() const  override { return iei_in && irq_req; }
    uint8_t getVector() const     override { return vector; }
};

// ─── I/O dispatch ─────────────────────────────────────────────────────────

TEST(K1520Bus, IODispatch_WriteReach) {
    K1520Bus bus;
    MockIO dev;
    bus.registerIO(&dev, 0x10, 4);

    bus.ioWrite(0x12, 0xAB);
    EXPECT_EQ(dev.last_port_written, 0x12);
    EXPECT_EQ(dev.last_data_written, 0xAB);
    EXPECT_EQ(dev.write_count, 1);
}

TEST(K1520Bus, IODispatch_Read) {
    K1520Bus bus;
    MockIO dev;
    dev.read_return = 0x55;
    bus.registerIO(&dev, 0x20, 1);

    EXPECT_EQ(bus.ioRead(0x20), 0x55);
    EXPECT_EQ(bus.ioRead(0x21), 0xFF);  // unregistered port → bus float
}

TEST(K1520Bus, IODispatch_Conflict_Throws) {
    K1520Bus bus;
    MockIO dev1, dev2;
    bus.registerIO(&dev1, 0x10, 4);
    EXPECT_THROW(bus.registerIO(&dev2, 0x12, 1), std::runtime_error);
}

TEST(K1520Bus, IODI_Blocks) {
    K1520Bus bus;
    MockIO dev;
    bus.registerIO(&dev, 0x10, 4);
    bus.setIODI(true);
    bus.ioWrite(0x10, 0x42);
    EXPECT_EQ(dev.write_count, 0);
    EXPECT_EQ(bus.ioRead(0x10), 0xFF);
}

// ─── Memory dispatch ──────────────────────────────────────────────────────

TEST(K1520Bus, MemDispatch_Write) {
    K1520Bus bus;
    MockMem mem(0x1000);
    bus.registerMem(&mem, 0x1000, 256);

    bus.memWrite(0x1010, 0xCC);
    EXPECT_EQ(mem.buf[0x10], 0xCC);
}

TEST(K1520Bus, MemDispatch_Read) {
    K1520Bus bus;
    MockMem mem(0x2000);
    mem.buf[5] = 0x77;
    bus.registerMem(&mem, 0x2000, 256);

    EXPECT_EQ(bus.memRead(0x2005), 0x77);
    EXPECT_EQ(bus.memRead(0x3000), 0xFF);  // unmapped
}

TEST(K1520Bus, MemDispatch_OverlapLaterWins) {
    K1520Bus bus;
    MockMem ram(0x0000), rom(0x0000);
    ram.buf[0]    = 0xAA;
    ram.buf[0x40] = 0xAA;  // address outside ROM range
    rom.buf[0]    = 0xBB;
    rom.writable = false;

    bus.registerMem(&ram, 0x0000, 256);
    bus.registerMem(&rom, 0x0000, 64);   // ROM overlays lower 64 bytes

    EXPECT_EQ(bus.memRead(0x0000), 0xBB);  // ROM wins
    EXPECT_EQ(bus.memRead(0x0040), 0xAA);  // RAM (ROM ends at 0x003F)
}

TEST(K1520Bus, MemDispatch_ROMIgnoresWrite) {
    K1520Bus bus;
    MockMem rom(0x0000);
    rom.buf[0] = 0xEE;
    rom.writable = false;
    bus.registerMem(&rom, 0x0000, 256);

    bus.memWrite(0x0000, 0x42);
    EXPECT_EQ(rom.buf[0], 0xEE);  // unchanged
}

TEST(K1520Bus, MEMDI_BlocksRead) {
    K1520Bus bus;
    MockMem mem(0x1000);
    mem.buf[0] = 0x42;
    bus.registerMem(&mem, 0x1000, 256);
    bus.setMEMDI(true);

    EXPECT_EQ(bus.memRead(0x1000), 0xFF);
}

TEST(K1520Bus, UnregisterMem) {
    K1520Bus bus;
    MockMem mem(0x1000);
    mem.buf[0] = 0x42;
    bus.registerMem(&mem, 0x1000, 256);
    EXPECT_EQ(bus.memRead(0x1000), 0x42);

    bus.unregisterMem(&mem);
    EXPECT_EQ(bus.memRead(0x1000), 0xFF);  // bus float after unregister
}

// ─── Interrupt daisy-chain ─────────────────────────────────────────────────

TEST(K1520Bus, InterruptChain_HigherPriorityWins) {
    K1520Bus bus;
    MockIRQ hi, lo;
    hi.vector = 0x20;
    lo.vector = 0x30;
    bus.setInterruptChain({&hi, &lo});

    hi.irq_req = true;
    lo.irq_req = true;
    bus.updateInterruptChain();

    EXPECT_TRUE(hi.iei_in);
    EXPECT_FALSE(lo.iei_in);  // blocked by hi
    EXPECT_EQ(bus.interruptAcknowledge(), 0x20);
}

TEST(K1520Bus, InterruptChain_LowerPriorityIfHigherClear) {
    K1520Bus bus;
    MockIRQ hi, lo;
    hi.vector = 0x20;
    lo.vector = 0x30;
    bus.setInterruptChain({&hi, &lo});

    hi.irq_req = false;
    lo.irq_req = true;
    bus.updateInterruptChain();

    EXPECT_EQ(bus.interruptAcknowledge(), 0x30);
}

TEST(K1520Bus, InterruptChain_NoInterrupt) {
    K1520Bus bus;
    MockIRQ hi, lo;
    bus.setInterruptChain({&hi, &lo});

    hi.irq_req = false;
    lo.irq_req = false;
    bus.updateInterruptChain();

    EXPECT_EQ(bus.interruptAcknowledge(), 0xFF);  // no interrupt
}

// ─── BUSRQ / BUSAK (DMA protocol) ─────────────────────────────────────────

TEST(K1520Bus, BUSRQ_InitiallyFalse) {
    K1520Bus bus;
    EXPECT_FALSE(bus.isBUSRQ());
}

TEST(K1520Bus, AssertBUSRQ_IsBUSRQReturnsTrue) {
    K1520Bus bus;
    bus.assertBUSRQ();
    EXPECT_TRUE(bus.isBUSRQ());
}

TEST(K1520Bus, ReleaseBUSRQ_AfterAssert_ReturnsFalse) {
    K1520Bus bus;
    bus.assertBUSRQ();
    EXPECT_TRUE(bus.isBUSRQ());
    bus.releaseBUSRQ();
    EXPECT_FALSE(bus.isBUSRQ());
}

TEST(K1520Bus, AssertBUSRQ_Idempotent) {
    K1520Bus bus;
    bus.assertBUSRQ();
    bus.assertBUSRQ();   // second assert must not crash or toggle
    EXPECT_TRUE(bus.isBUSRQ());
    bus.releaseBUSRQ();
    EXPECT_FALSE(bus.isBUSRQ());
}

TEST(K1520Bus, ReleaseBUSRQ_WithoutAssert_DoesNotCrash) {
    K1520Bus bus;
    bus.releaseBUSRQ();  // release without prior assert
    EXPECT_FALSE(bus.isBUSRQ());
}

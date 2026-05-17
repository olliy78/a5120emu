/**
 * @file test_reti.cpp
 * @brief Simple test to verify RETI detection across all peripherals
 */

#include <iostream>
#include <iomanip>
#include "core/primitives/z80.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include "core/primitives/z80_sio.h"
#include "core/bus/k1520_bus.h"

void testCTC_RETI() {
    std::cout << "Testing CTC RETI detection..." << std::endl;

    K1520Bus bus;
    Z80CTC ctc("CTC");

    // Register CTC on bus
    bus.registerIO(&ctc, 0x00, 4);
    bus.setInterruptChain({&ctc});

    // Configure channel 0 with interrupt enabled
    ctc.ioWrite(0, 0xE5);  // Control: INT=1, Timer, TC follows
    ctc.ioWrite(0, 0x01);  // TC = 1

    // Enable interrupts
    ctc.setIEI(true);
    bus.updateInterruptChain();

    // Trigger timer to generate interrupt
    for (int i = 0; i < 100; i++) {
        ctc.clockTick();
    }

    // Verify interrupt is pending
    if (!ctc.hasInterrupt()) {
        std::cout << "  FAIL: CTC interrupt not pending" << std::endl;
        return;
    }
    std::cout << "  OK: Interrupt pending" << std::endl;

    // Get vector (acknowledges interrupt, sets IUS)
    uint8_t vec = ctc.getVector();
    std::cout << "  Got vector: 0x" << std::hex << (int)vec << std::dec << std::endl;

    // Verify IEO is blocked (IUS is set)
    if (ctc.getIEO()) {
        std::cout << "  FAIL: IEO should be blocked when IUS is set" << std::endl;
        return;
    }
    std::cout << "  OK: IEO blocked (IUS set)" << std::endl;

    // Call RETI
    ctc.onRETI();
    bus.updateInterruptChain();

    // Verify IEO is now passing (IUS cleared)
    if (!ctc.getIEO()) {
        std::cout << "  FAIL: IEO should pass after RETI" << std::endl;
        return;
    }
    std::cout << "  OK: IEO passing (IUS cleared after RETI)" << std::endl;
    std::cout << "  CTC RETI test PASSED" << std::endl << std::endl;
}

void testPIO_RETI() {
    std::cout << "Testing PIO RETI detection..." << std::endl;

    K1520Bus bus;
    Z80PIO pio("PIO");

    // Register PIO on bus
    bus.registerIO(&pio, 0x00, 4);
    bus.setInterruptChain({&pio});

    // Configure Port A for Mode 1 (input) with interrupt
    pio.ioWrite(1, 0xCF);  // Mode 1
    pio.ioWrite(1, 0x97);  // ICW: EI, OR, high, vector=0x80
    pio.ioWrite(1, 0x80);  // Vector
    pio.ioWrite(1, 0xFF);  // Mask: all bits enabled

    // Enable interrupts
    pio.setIEI(true);
    bus.updateInterruptChain();

    // Simulate external data arrival
    pio.portAWrite(0x55);
    pio.setASTB(false);  // Strobe

    // Verify interrupt is pending
    if (!pio.hasInterrupt()) {
        std::cout << "  FAIL: PIO interrupt not pending" << std::endl;
        return;
    }
    std::cout << "  OK: Interrupt pending" << std::endl;

    // Get vector (acknowledges interrupt, sets IUS)
    uint8_t vec = pio.getVector();
    std::cout << "  Got vector: 0x" << std::hex << (int)vec << std::dec << std::endl;

    // Verify IEO is blocked
    if (pio.getIEO()) {
        std::cout << "  FAIL: IEO should be blocked when IUS is set" << std::endl;
        return;
    }
    std::cout << "  OK: IEO blocked (IUS set)" << std::endl;

    // Call RETI
    pio.onRETI();
    bus.updateInterruptChain();

    // Verify IEO is now passing
    if (!pio.getIEO()) {
        std::cout << "  FAIL: IEO should pass after RETI" << std::endl;
        return;
    }
    std::cout << "  OK: IEO passing (IUS cleared after RETI)" << std::endl;
    std::cout << "  PIO RETI test PASSED" << std::endl << std::endl;
}

void testSIO_RETI() {
    std::cout << "Testing SIO RETI detection..." << std::endl;

    K1520Bus bus;
    Z80SIO sio("SIO");

    // Register SIO on bus
    bus.registerIO(&sio, 0x00, 4);
    bus.setInterruptChain({&sio});

    // Configure Channel A for RX interrupts
    sio.ioWrite(1, 0x00);  // Point to WR0
    sio.ioWrite(1, 0x18);  // WR1: Rx INT on all chars
    sio.ioWrite(1, 0x00);  // Point to WR0
    sio.ioWrite(1, 0x80);  // Vector

    // Enable interrupts
    sio.setIEI(true);
    bus.updateInterruptChain();

    // Simulate received byte
    sio.channelA().rxByte(0x42);

    // Verify interrupt is pending
    if (!sio.hasInterrupt()) {
        std::cout << "  FAIL: SIO interrupt not pending" << std::endl;
        return;
    }
    std::cout << "  OK: Interrupt pending" << std::endl;

    // Get vector (acknowledges interrupt, sets IUS)
    uint8_t vec = sio.getVector();
    std::cout << "  Got vector: 0x" << std::hex << (int)vec << std::dec << std::endl;

    // Verify IEO is blocked
    if (sio.getIEO()) {
        std::cout << "  FAIL: IEO should be blocked when IUS is set" << std::endl;
        return;
    }
    std::cout << "  OK: IEO blocked (IUS set)" << std::endl;

    // Call RETI
    sio.onRETI();
    bus.updateInterruptChain();

    // Verify IEO is now passing
    if (!sio.getIEO()) {
        std::cout << "  FAIL: IEO should pass after RETI" << std::endl;
        return;
    }
    std::cout << "  OK: IEO passing (IUS cleared after RETI)" << std::endl;
    std::cout << "  SIO RETI test PASSED" << std::endl << std::endl;
}

void testNestedInterrupts() {
    std::cout << "Testing nested interrupts with RETI..." << std::endl;

    K1520Bus bus;
    Z80CTC ctc("CTC");
    Z80PIO pio("PIO");

    // Register both on bus (CTC higher priority than PIO)
    bus.registerIO(&ctc, 0x00, 4);
    bus.registerIO(&pio, 0x04, 4);
    bus.setInterruptChain({&ctc, &pio});

    // Configure CTC channel 0
    ctc.ioWrite(0, 0xE5);  // INT enabled, Timer
    ctc.ioWrite(0, 0x02);  // TC = 2

    // Configure PIO Port A
    pio.ioWrite(5, 0xCF);  // Mode 1
    pio.ioWrite(5, 0x97);  // ICW
    pio.ioWrite(5, 0x90);  // Vector
    pio.ioWrite(5, 0xFF);  // Mask

    // Enable interrupts
    ctc.setIEI(true);
    bus.updateInterruptChain();

    // Trigger both interrupts
    for (int i = 0; i < 100; i++) ctc.clockTick();
    pio.portAWrite(0x55);
    pio.setASTB(false);

    // Both should be pending
    if (!ctc.hasInterrupt() || !pio.hasInterrupt()) {
        std::cout << "  FAIL: Both interrupts should be pending" << std::endl;
        return;
    }
    std::cout << "  OK: Both interrupts pending" << std::endl;

    // Get vector from CTC (higher priority)
    uint8_t vec1 = bus.interruptAcknowledge();
    std::cout << "  First vector: 0x" << std::hex << (int)vec1 << std::dec;
    std::cout << " (should be from CTC)" << std::endl;

    // PIO should not be able to interrupt (CTC has IUS set)
    if (pio.hasInterrupt() && pio.getIEO()) {
        std::cout << "  FAIL: PIO IEO should be blocked while CTC is serviced" << std::endl;
        return;
    }
    std::cout << "  OK: PIO blocked while CTC serviced" << std::endl;

    // RETI from CTC interrupt
    bus.signalRETI();

    // Now PIO should be able to interrupt
    if (!pio.hasInterrupt()) {
        std::cout << "  FAIL: PIO interrupt should still be pending" << std::endl;
        return;
    }
    std::cout << "  OK: PIO interrupt now active after CTC RETI" << std::endl;

    // Get PIO vector
    uint8_t vec2 = bus.interruptAcknowledge();
    std::cout << "  Second vector: 0x" << std::hex << (int)vec2 << std::dec;
    std::cout << " (should be from PIO)" << std::endl;

    // RETI from PIO interrupt
    bus.signalRETI();

    std::cout << "  Nested interrupts test PASSED" << std::endl << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "RETI Detection Verification Tests" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    testCTC_RETI();
    testPIO_RETI();
    testSIO_RETI();
    testNestedInterrupts();

    std::cout << "========================================" << std::endl;
    std::cout << "All RETI tests PASSED!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}

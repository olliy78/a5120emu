// test_main.cpp - Tests for A5120 Emulator Components
#include "z80.h"
#include "memory.h"
#include "floppy.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        printf("  TEST: %-50s ", #name); \
        bool _pass = true; \
        try {

#define END_TEST \
        } catch (...) { _pass = false; } \
        if (_pass) { printf("[PASS]\n"); tests_passed++; } \
        else { printf("[FAIL]\n"); tests_failed++; } \
    } while(0)

#define CHECK(cond) do { if (!(cond)) { printf("\n    FAILED: %s (line %d)\n", #cond, __LINE__); _pass = false; } } while(0)

// =============================================================================
// Z80 CPU Tests
// =============================================================================

static void setupCPU(Z80& cpu, Memory& mem) {
    cpu.reset();
    cpu.readByte = [&mem](uint16_t addr) -> uint8_t { return mem.read(addr); };
    cpu.writeByte = [&mem](uint16_t addr, uint8_t val) { mem.write(addr, val); };
    cpu.readPort = [](uint16_t) -> uint8_t { return 0xFF; };
    cpu.writePort = [](uint16_t, uint8_t) {};
}

void testZ80Basic() {
    printf("\n=== Z80 CPU Basic Tests ===\n");
    Z80 cpu;
    Memory mem;
    setupCPU(cpu, mem);

    TEST(NOP) {
        mem.write(0, 0x00); // NOP
        cpu.step();
        CHECK(cpu.PC == 1);
        CHECK(cpu.cycles == 4);
    } END_TEST;

    TEST(LD_A_immediate) {
        cpu.reset();
        mem.write(0, 0x3E); // LD A, 42h
        mem.write(1, 0x42);
        cpu.step();
        CHECK(cpu.A == 0x42);
        CHECK(cpu.PC == 2);
    } END_TEST;

    TEST(LD_BC_immediate) {
        cpu.reset();
        mem.write(0, 0x01); // LD BC, 1234h
        mem.write(1, 0x34);
        mem.write(2, 0x12);
        cpu.step();
        CHECK(cpu.BC == 0x1234);
        CHECK(cpu.B == 0x12);
        CHECK(cpu.C == 0x34);
    } END_TEST;

    TEST(LD_SP_immediate) {
        cpu.reset();
        mem.write(0, 0x31); // LD SP, FFE0h
        mem.write(1, 0xE0);
        mem.write(2, 0xFF);
        cpu.step();
        CHECK(cpu.SP == 0xFFE0);
    } END_TEST;

    TEST(ADD_A_B) {
        cpu.reset();
        cpu.A = 0x10;
        cpu.B = 0x20;
        mem.write(0, 0x80); // ADD A, B
        cpu.step();
        CHECK(cpu.A == 0x30);
        CHECK(!(cpu.F & Z80::FLAG_Z));
        CHECK(!(cpu.F & Z80::FLAG_C));
    } END_TEST;

    TEST(ADD_A_overflow) {
        cpu.reset();
        cpu.A = 0xFF;
        cpu.B = 0x01;
        mem.write(0, 0x80); // ADD A, B
        cpu.step();
        CHECK(cpu.A == 0x00);
        CHECK(cpu.F & Z80::FLAG_Z);
        CHECK(cpu.F & Z80::FLAG_C);
    } END_TEST;

    TEST(SUB_A_B) {
        cpu.reset();
        cpu.A = 0x30;
        cpu.B = 0x10;
        mem.write(0, 0x90); // SUB B
        cpu.step();
        CHECK(cpu.A == 0x20);
        CHECK(cpu.F & Z80::FLAG_N);
        CHECK(!(cpu.F & Z80::FLAG_C));
    } END_TEST;

    TEST(AND_A) {
        cpu.reset();
        cpu.A = 0xF0;
        mem.write(0, 0xE6); // AND 0Fh
        mem.write(1, 0x0F);
        cpu.step();
        CHECK(cpu.A == 0x00);
        CHECK(cpu.F & Z80::FLAG_Z);
    } END_TEST;

    TEST(OR_A) {
        cpu.reset();
        cpu.A = 0xF0;
        mem.write(0, 0xF6); // OR 0Fh
        mem.write(1, 0x0F);
        cpu.step();
        CHECK(cpu.A == 0xFF);
        CHECK(cpu.F & Z80::FLAG_S);
    } END_TEST;

    TEST(XOR_A) {
        cpu.reset();
        cpu.A = 0xFF;
        mem.write(0, 0xAF); // XOR A
        cpu.step();
        CHECK(cpu.A == 0x00);
        CHECK(cpu.F & Z80::FLAG_Z);
    } END_TEST;

    TEST(INC_DEC) {
        cpu.reset();
        cpu.B = 0x00;
        mem.write(0, 0x04); // INC B
        cpu.step();
        CHECK(cpu.B == 0x01);

        mem.write(1, 0x05); // DEC B
        cpu.step();
        CHECK(cpu.B == 0x00);
        CHECK(cpu.F & Z80::FLAG_Z);
    } END_TEST;

    TEST(JP) {
        cpu.reset();
        mem.write(0, 0xC3); // JP 1234h
        mem.write(1, 0x34);
        mem.write(2, 0x12);
        cpu.step();
        CHECK(cpu.PC == 0x1234);
    } END_TEST;

    TEST(JR) {
        cpu.reset();
        mem.write(0, 0x18); // JR +5
        mem.write(1, 0x05);
        cpu.step();
        CHECK(cpu.PC == 7); // 2 + 5
    } END_TEST;

    TEST(JR_backward) {
        cpu.reset();
        cpu.PC = 0x10;
        mem.write(0x10, 0x18); // JR -5
        mem.write(0x11, 0xFB); // -5 in two's complement
        cpu.step();
        CHECK(cpu.PC == 0x0D); // 0x12 - 5
    } END_TEST;

    TEST(CALL_RET) {
        cpu.reset();
        cpu.SP = 0x1000;
        mem.write(0, 0xCD); // CALL 0100h
        mem.write(1, 0x00);
        mem.write(2, 0x01);
        cpu.step();
        CHECK(cpu.PC == 0x0100);
        CHECK(cpu.SP == 0x0FFE);

        mem.write(0x0100, 0xC9); // RET
        cpu.step();
        CHECK(cpu.PC == 0x0003);
        CHECK(cpu.SP == 0x1000);
    } END_TEST;

    TEST(PUSH_POP) {
        cpu.reset();
        cpu.SP = 0x1000;
        cpu.BC = 0xABCD;
        mem.write(0, 0xC5); // PUSH BC
        cpu.step();
        CHECK(cpu.SP == 0x0FFE);
        CHECK(mem.read(0x0FFE) == 0xCD);
        CHECK(mem.read(0x0FFF) == 0xAB);

        cpu.BC = 0;
        mem.write(1, 0xC1); // POP BC
        cpu.step();
        CHECK(cpu.BC == 0xABCD);
        CHECK(cpu.SP == 0x1000);
    } END_TEST;

    TEST(DJNZ) {
        cpu.reset();
        cpu.B = 3;
        mem.write(0, 0x10); // DJNZ -2 (loop to self)
        mem.write(1, 0xFE);
        cpu.step(); CHECK(cpu.B == 2); CHECK(cpu.PC == 0);
        cpu.step(); CHECK(cpu.B == 1); CHECK(cpu.PC == 0);
        cpu.step(); CHECK(cpu.B == 0); CHECK(cpu.PC == 2);
    } END_TEST;

    TEST(EX_AF) {
        cpu.reset();
        cpu.AF = 0x1234;
        cpu.AF_ = 0x5678;
        mem.write(0, 0x08); // EX AF, AF'
        cpu.step();
        CHECK(cpu.AF == 0x5678);
        CHECK(cpu.AF_ == 0x1234);
    } END_TEST;

    TEST(EXX) {
        cpu.reset();
        cpu.BC = 0x1111;
        cpu.DE = 0x2222;
        cpu.HL = 0x3333;
        cpu.BC_ = 0xAAAA;
        cpu.DE_ = 0xBBBB;
        cpu.HL_ = 0xCCCC;
        mem.write(0, 0xD9); // EXX
        cpu.step();
        CHECK(cpu.BC == 0xAAAA);
        CHECK(cpu.DE == 0xBBBB);
        CHECK(cpu.HL == 0xCCCC);
        CHECK(cpu.BC_ == 0x1111);
        CHECK(cpu.DE_ == 0x2222);
        CHECK(cpu.HL_ == 0x3333);
    } END_TEST;

    TEST(LD_HL_indirect) {
        cpu.reset();
        mem.write(0x1000, 0x78);
        mem.write(0x1001, 0x56);
        mem.write(0, 0x2A); // LD HL, (1000h)
        mem.write(1, 0x00);
        mem.write(2, 0x10);
        cpu.step();
        CHECK(cpu.HL == 0x5678);
    } END_TEST;

    TEST(LD_mem_A) {
        cpu.reset();
        cpu.A = 0x42;
        mem.write(0, 0x32); // LD (2000h), A
        mem.write(1, 0x00);
        mem.write(2, 0x20);
        cpu.step();
        CHECK(mem.read(0x2000) == 0x42);
    } END_TEST;

    TEST(CP_equal) {
        cpu.reset();
        cpu.A = 0x42;
        mem.write(0, 0xFE); // CP 42h
        mem.write(1, 0x42);
        cpu.step();
        CHECK(cpu.F & Z80::FLAG_Z);
        CHECK(cpu.A == 0x42); // CP doesn't modify A
    } END_TEST;

    TEST(CP_less) {
        cpu.reset();
        cpu.A = 0x10;
        mem.write(0, 0xFE); // CP 20h
        mem.write(1, 0x20);
        cpu.step();
        CHECK(!(cpu.F & Z80::FLAG_Z));
        CHECK(cpu.F & Z80::FLAG_C); // A < operand -> carry set
    } END_TEST;
}

void testZ80Extended() {
    printf("\n=== Z80 Extended Instruction Tests ===\n");
    Z80 cpu;
    Memory mem;
    setupCPU(cpu, mem);

    TEST(LDIR) {
        cpu.reset();
        // Source data at 0x1000
        mem.write(0x1000, 'H');
        mem.write(0x1001, 'e');
        mem.write(0x1002, 'l');
        mem.write(0x1003, 'l');
        mem.write(0x1004, 'o');
        // Set up LDIR
        cpu.HL = 0x1000; // Source
        cpu.DE = 0x2000; // Dest
        cpu.BC = 5;      // Count
        mem.write(0, 0xED);
        mem.write(1, 0xB0); // LDIR
        // Execute (LDIR loops internally)
        while (cpu.BC > 0) cpu.step();
        CHECK(mem.read(0x2000) == 'H');
        CHECK(mem.read(0x2004) == 'o');
        CHECK(cpu.BC == 0);
    } END_TEST;

    TEST(CPIR) {
        cpu.reset();
        mem.write(0x1000, 'A');
        mem.write(0x1001, 'B');
        mem.write(0x1002, 'C');
        cpu.A = 'B';
        cpu.HL = 0x1000;
        cpu.BC = 3;
        mem.write(0, 0xED);
        mem.write(1, 0xB1); // CPIR
        while (cpu.BC > 0 && !(cpu.F & Z80::FLAG_Z)) cpu.step();
        CHECK(cpu.F & Z80::FLAG_Z); // Found 'B'
        CHECK(cpu.HL == 0x1002);    // Points past found byte
    } END_TEST;

    TEST(IM2) {
        cpu.reset();
        mem.write(0, 0xED);
        mem.write(1, 0x5E); // IM 2
        cpu.step();
        CHECK(cpu.IM == 2);
    } END_TEST;

    TEST(LD_I_A) {
        cpu.reset();
        cpu.A = 0xF7;
        mem.write(0, 0xED);
        mem.write(1, 0x47); // LD I, A
        cpu.step();
        CHECK(cpu.I == 0xF7);
    } END_TEST;

    TEST(NEG) {
        cpu.reset();
        cpu.A = 0x01;
        mem.write(0, 0xED);
        mem.write(1, 0x44); // NEG
        cpu.step();
        CHECK(cpu.A == 0xFF);
        CHECK(cpu.F & Z80::FLAG_S);
        CHECK(cpu.F & Z80::FLAG_C);
    } END_TEST;

    TEST(SBC_HL_BC) {
        cpu.reset();
        cpu.HL = 0x1000;
        cpu.BC = 0x0800;
        cpu.F = 0; // No carry
        mem.write(0, 0xED);
        mem.write(1, 0x42); // SBC HL, BC
        cpu.step();
        CHECK(cpu.HL == 0x0800);
    } END_TEST;
}

void testZ80CB() {
    printf("\n=== Z80 CB Prefix Tests ===\n");
    Z80 cpu;
    Memory mem;
    setupCPU(cpu, mem);

    TEST(RLC_A) {
        cpu.reset();
        cpu.A = 0x85; // 10000101
        // B=0, so getReg8(7) = A
        mem.write(0, 0xCB);
        mem.write(1, 0x07); // RLC A
        cpu.step();
        CHECK(cpu.A == 0x0B); // 00001011
        CHECK(cpu.F & Z80::FLAG_C); // Old bit 7 was 1
    } END_TEST;

    TEST(BIT_7_A) {
        cpu.reset();
        cpu.A = 0x80;
        mem.write(0, 0xCB);
        mem.write(1, 0x7F); // BIT 7, A
        cpu.step();
        CHECK(!(cpu.F & Z80::FLAG_Z)); // Bit 7 is set
    } END_TEST;

    TEST(BIT_0_A_clear) {
        cpu.reset();
        cpu.A = 0xFE;
        mem.write(0, 0xCB);
        mem.write(1, 0x47); // BIT 0, A
        cpu.step();
        CHECK(cpu.F & Z80::FLAG_Z); // Bit 0 is clear
    } END_TEST;

    TEST(SET_3_A) {
        cpu.reset();
        cpu.A = 0x00;
        mem.write(0, 0xCB);
        mem.write(1, 0xDF); // SET 3, A
        cpu.step();
        CHECK(cpu.A == 0x08);
    } END_TEST;

    TEST(RES_7_A) {
        cpu.reset();
        cpu.A = 0xFF;
        mem.write(0, 0xCB);
        mem.write(1, 0xBF); // RES 7, A
        cpu.step();
        CHECK(cpu.A == 0x7F);
    } END_TEST;

    TEST(SRL_A) {
        cpu.reset();
        cpu.A = 0x81;
        mem.write(0, 0xCB);
        mem.write(1, 0x3F); // SRL A
        cpu.step();
        CHECK(cpu.A == 0x40); // 01000000
        CHECK(cpu.F & Z80::FLAG_C); // Old bit 0 was 1
    } END_TEST;
}

void testZ80IX() {
    printf("\n=== Z80 IX Prefix Tests ===\n");
    Z80 cpu;
    Memory mem;
    setupCPU(cpu, mem);

    TEST(LD_IX_immediate) {
        cpu.reset();
        mem.write(0, 0xDD);
        mem.write(1, 0x21); // LD IX, 1234h
        mem.write(2, 0x34);
        mem.write(3, 0x12);
        cpu.step();
        CHECK(cpu.IX == 0x1234);
    } END_TEST;

    TEST(LD_A_IX_d) {
        cpu.reset();
        cpu.IX = 0x1000;
        mem.write(0x1005, 0x42);
        mem.write(0, 0xDD);
        mem.write(1, 0x7E); // LD A, (IX+5)
        mem.write(2, 0x05);
        cpu.step();
        CHECK(cpu.A == 0x42);
    } END_TEST;

    TEST(ADD_IX_BC) {
        cpu.reset();
        cpu.IX = 0x1000;
        cpu.BC = 0x0100;
        mem.write(0, 0xDD);
        mem.write(1, 0x09); // ADD IX, BC
        cpu.step();
        CHECK(cpu.IX == 0x1100);
    } END_TEST;
}

void testZ80DI_EI() {
    printf("\n=== Z80 Interrupt Tests ===\n");
    Z80 cpu;
    Memory mem;
    setupCPU(cpu, mem);

    TEST(DI_EI) {
        cpu.reset();
        mem.write(0, 0xF3); // DI
        cpu.step();
        CHECK(!cpu.IFF1);
        CHECK(!cpu.IFF2);

        mem.write(1, 0xFB); // EI
        cpu.step();
        CHECK(cpu.IFF1);
        CHECK(cpu.IFF2);
    } END_TEST;

    TEST(HALT) {
        cpu.reset();
        mem.write(0, 0x76); // HALT
        cpu.step();
        CHECK(cpu.halted);
    } END_TEST;

    TEST(interrupt_mode2) {
        cpu.reset();
        // Set up IM 2
        cpu.IM = 2;
        cpu.I = 0x10;
        cpu.IFF1 = cpu.IFF2 = true;
        cpu.SP = 0x1000;
        cpu.PC = 0x0050;

        // Interrupt vector: at 0x1020 (I*256 + vector), points to ISR at 0x2000
        mem.write(0x1020, 0x00);
        mem.write(0x1021, 0x20);

        cpu.interrupt(0x20); // Vector 0x20
        CHECK(cpu.PC == 0x2000);
        CHECK(!cpu.IFF1);
    } END_TEST;
}

// =============================================================================
// Memory Tests
// =============================================================================

void testMemory() {
    printf("\n=== Memory Tests ===\n");
    Memory mem;

    TEST(read_write) {
        mem.write(0x0000, 0x42);
        CHECK(mem.read(0x0000) == 0x42);
        mem.write(0xFFFF, 0xAB);
        CHECK(mem.read(0xFFFF) == 0xAB);
    } END_TEST;

    TEST(clear) {
        mem.write(0x1000, 0xFF);
        mem.clear();
        CHECK(mem.read(0x1000) == 0x00);
    } END_TEST;

    TEST(fill) {
        mem.fill(0x1000, 0x10FF, 0xE5);
        CHECK(mem.read(0x1000) == 0xE5);
        CHECK(mem.read(0x10FF) == 0xE5);
        CHECK(mem.read(0x1100) != 0xE5);
    } END_TEST;

    TEST(load_bulk) {
        uint8_t data[] = {1, 2, 3, 4, 5};
        mem.load(0x2000, data, 5);
        CHECK(mem.read(0x2000) == 1);
        CHECK(mem.read(0x2004) == 5);
    } END_TEST;

    TEST(screen_buffer) {
        mem.write(Memory::SCREEN_START, 'A');
        mem.write(Memory::SCREEN_START + 1, 'B');
        CHECK(mem.getScreenChar(0, 0) == 'A');
        CHECK(mem.getScreenChar(0, 1) == 'B');
    } END_TEST;

    TEST(screen_cursor_bit7) {
        mem.write(Memory::SCREEN_START, 'C' | 0x80);
        CHECK(mem.getScreenChar(0, 0) == 'C'); // Bit 7 stripped
    } END_TEST;
}

// =============================================================================
// Floppy Disk Tests
// =============================================================================

void testFloppy() {
    printf("\n=== Floppy Disk Tests ===\n");
    FloppyDisk disk;

    TEST(create_blank) {
        bool ok = disk.createBlank("/tmp/test_a5120.img");
        CHECK(ok);
        CHECK(disk.isLoaded());
        CHECK(disk.data().size() == 819200);
    } END_TEST;

    TEST(read_write_sector) {
        uint8_t writeBuf[128];
        uint8_t readBuf[128];
        memset(writeBuf, 0x42, 128);

        bool wOk = disk.writeSector(2, 0, writeBuf); // Track 2, sector 0
        CHECK(wOk);

        bool rOk = disk.readSector(2, 0, readBuf);
        CHECK(rOk);
        CHECK(memcmp(writeBuf, readBuf, 128) == 0);
    } END_TEST;

    TEST(blank_sector_content) {
        uint8_t readBuf[128];
        disk.readSector(10, 5, readBuf);
        CHECK(readBuf[0] == 0xE5); // Blank marker
    } END_TEST;

    TEST(save_load) {
        uint8_t writeBuf[128] = {0};
        strcpy((char*)writeBuf, "Hello A5120!");
        disk.writeSector(3, 1, writeBuf);
        disk.saveImage("/tmp/test_a5120.img");

        FloppyDisk disk2;
        bool ok = disk2.loadImage("/tmp/test_a5120.img");
        CHECK(ok);

        uint8_t readBuf[128];
        disk2.readSector(3, 1, readBuf);
        CHECK(strcmp((char*)readBuf, "Hello A5120!") == 0);
    } END_TEST;

    // Cleanup
    remove("/tmp/test_a5120.img");
}

// =============================================================================
// Integration Test: Z80 + Memory executing a small program
// =============================================================================

void testIntegration() {
    printf("\n=== Integration Tests ===\n");
    Z80 cpu;
    Memory mem;
    setupCPU(cpu, mem);

    TEST(simple_program) {
        // Program: Add numbers 1 to 10
        // LD B, 10
        // LD A, 0
        // loop: ADD A, B
        // DJNZ loop
        // HALT
        uint8_t prog[] = {
            0x06, 0x0A,     // LD B, 10
            0x3E, 0x00,     // LD A, 0
            0x80,           // ADD A, B
            0x10, 0xFD,     // DJNZ -3 (back to ADD)
            0x76            // HALT
        };
        mem.load(0, prog, sizeof(prog));
        cpu.reset();

        int maxSteps = 100;
        while (!cpu.halted && maxSteps-- > 0) {
            cpu.step();
        }

        // Sum of 1..10 = 55
        CHECK(cpu.A == 55);
        CHECK(cpu.B == 0);
        CHECK(cpu.halted);
    } END_TEST;

    TEST(memory_copy_program) {
        cpu.reset();
        mem.clear();
        // Program: Copy "HELLO" from 0x100 to 0x200 using LDIR
        uint8_t prog[] = {
            0x21, 0x00, 0x01,   // LD HL, 0100h
            0x11, 0x00, 0x02,   // LD DE, 0200h
            0x01, 0x05, 0x00,   // LD BC, 5
            0xED, 0xB0,         // LDIR
            0x76                // HALT
        };
        mem.load(0, prog, sizeof(prog));
        mem.write(0x100, 'H');
        mem.write(0x101, 'E');
        mem.write(0x102, 'L');
        mem.write(0x103, 'L');
        mem.write(0x104, 'O');

        int maxSteps = 100;
        while (!cpu.halted && maxSteps-- > 0) {
            cpu.step();
        }

        CHECK(mem.read(0x200) == 'H');
        CHECK(mem.read(0x204) == 'O');
        CHECK(cpu.halted);
    } END_TEST;

    TEST(stack_operations) {
        cpu.reset();
        mem.clear();
        // Program: PUSH values, POP them in reverse
        uint8_t prog[] = {
            0x31, 0x00, 0x10,   // LD SP, 1000h
            0x01, 0x34, 0x12,   // LD BC, 1234h
            0x11, 0x78, 0x56,   // LD DE, 5678h
            0xC5,               // PUSH BC
            0xD5,               // PUSH DE
            0xC1,               // POP BC (gets 5678)
            0xD1,               // POP DE (gets 1234)
            0x76                // HALT
        };
        mem.load(0, prog, sizeof(prog));

        int maxSteps = 100;
        while (!cpu.halted && maxSteps-- > 0) {
            cpu.step();
        }

        CHECK(cpu.BC == 0x5678);
        CHECK(cpu.DE == 0x1234);
    } END_TEST;

    TEST(subroutine_call) {
        cpu.reset();
        mem.clear();
        // Main: CALL sub, HALT
        // Sub: LD A, 42h, RET
        uint8_t prog[] = {
            0x31, 0x00, 0x10,   // LD SP, 1000h
            0xCD, 0x10, 0x00,   // CALL 0010h
            0x76                // HALT
        };
        mem.load(0, prog, sizeof(prog));
        mem.write(0x10, 0x3E);  // LD A, 42h
        mem.write(0x11, 0x42);
        mem.write(0x12, 0xC9);  // RET

        int maxSteps = 100;
        while (!cpu.halted && maxSteps-- > 0) {
            cpu.step();
        }

        CHECK(cpu.A == 0x42);
        CHECK(cpu.halted);
    } END_TEST;

    TEST(conditional_jump) {
        cpu.reset();
        mem.clear();
        // If A == 0, jump to HALT, else set A=99
        uint8_t prog[] = {
            0x3E, 0x00,        // LD A, 0
            0xB7,              // OR A (set flags)
            0x28, 0x03,        // JR Z, +3 (skip to HALT)
            0x3E, 0x63,        // LD A, 99
            0x76,              // HALT
            0x76               // HALT (target of JR Z)
        };
        mem.load(0, prog, sizeof(prog));

        int maxSteps = 100;
        while (!cpu.halted && maxSteps-- > 0) {
            cpu.step();
        }

        CHECK(cpu.A == 0x00); // Jumped over LD A, 99
    } END_TEST;
}

// =============================================================================
// CP/M BDOS Call Test (simulated)
// =============================================================================

void testCPM() {
    printf("\n=== CP/M Compatibility Tests ===\n");
    Z80 cpu;
    Memory mem;
    setupCPU(cpu, mem);

    TEST(bdos_string_output_sim) {
        // Simulate a program that would call BDOS func 9 (print string)
        // Set up string at 0x200
        const char* msg = "Hello, A5120!$";
        for (int i = 0; msg[i]; i++) {
            mem.write(0x200 + i, msg[i]);
        }

        // Check string is in memory correctly
        CHECK(mem.read(0x200) == 'H');
        CHECK(mem.read(0x20D) == '$');
    } END_TEST;

    TEST(cpm_fcb_structure) {
        // Verify FCB structure placement at default DMA
        // FCB at 0x005C: drive, filename(8), ext(3), extent, s1, s2, rc
        mem.write(0x5C, 0x01);  // Drive A
        mem.write(0x5D, 'T');
        mem.write(0x5E, 'E');
        mem.write(0x5F, 'S');
        mem.write(0x60, 'T');
        mem.write(0x61, ' ');
        mem.write(0x62, ' ');
        mem.write(0x63, ' ');
        mem.write(0x64, ' ');
        mem.write(0x65, 'C');
        mem.write(0x66, 'O');
        mem.write(0x67, 'M');

        CHECK(mem.read(0x5C) == 0x01);
        CHECK(mem.read(0x5D) == 'T');
        CHECK(mem.read(0x65) == 'C');
    } END_TEST;
}

// =============================================================================

int main() {
    printf("========================================\n");
    printf("  Robotron A5120 Emulator Test Suite\n");
    printf("========================================\n");

    testZ80Basic();
    testZ80Extended();
    testZ80CB();
    testZ80IX();
    testZ80DI_EI();
    testMemory();
    testFloppy();
    testIntegration();
    testCPM();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}

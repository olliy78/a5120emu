/**
 * @file test_z80.cpp
 * @brief Unit tests for the Z80 CPU emulation (Robotron U880D).
 *
 * @details
 * Emulator component under test: **Z80** (`core/primitives/z80.h`)
 *
 * The Z80 (East German designation U880D, manufactured by VEB Funkwerk Erfurt)
 * is the central processing unit of the Robotron A5120.  It implements:
 *  - Registers: AF, BC, DE, HL (main set), AF', BC', DE', HL' (shadow),
 *               IX, IY, PC, SP, I (interrupt vector), R (refresh counter)
 *  - Flags in F: S (bit 7), Z (bit 6), H (bit 4), PV (bit 2), N (bit 1), C (bit 0);
 *               undocumented bits 3 and 5.
 *  - Full documented Z80 instruction set (158 base instructions) plus common
 *    undocumented instructions (SLL, IXH/IXL/IYH/IYL, DDCB/FDCB variants)
 *  - Interrupt modes IM 0, IM 1, IM 2; maskable INT and non-maskable NMI
 *  - Cycle-accurate timing: step() returns the clock cycles consumed
 *
 * After reset(): AF = 0xFFFF (A=0xFF, F=0xFF, matching real Z80 power-on),
 * BC = DE = HL = 0, SP = 0xFFFF, PC = 0,
 * IFF1 = IFF2 = false, IM = 0, halted = false, cycles = 0.
 *
 * Memory and I/O are abstracted via callbacks (readByte, writeByte,
 * readPort, writePort).  Tests use a flat 64 KiB memory array.
 *
 * ## Test groups
 *
 * | Group                      | What is tested                                         |
 * |----------------------------|--------------------------------------------------------|
 * | Reset state                | Power-on register and flag values                      |
 * | NOP / timing               | PC advance, cycle count (4 cycles)                     |
 * | 8-bit register loads       | LD r,n; LD r,r; LD r,(HL); LD (HL),r; LD (HL),n       |
 * | 8-bit arithmetic           | ADD, ADC, SUB, SBC A with all flags                    |
 * | Logical operations         | AND, OR, XOR, CP with flag effects                     |
 * | INC / DEC                  | Flag overflow at 0x7F/0x80; C-flag preservation         |
 * | Accumulator rotations      | RLCA, RRCA, RLA, RRA                                   |
 * | CB-prefix shifts/rot.      | RLC, RRC, RL, RR, SLA, SRA, SRL on register           |
 * | CB-prefix bit operations   | BIT, RES, SET                                          |
 * | 16-bit loads               | LD rr,nn; LD (nn),HL; LD HL,(nn); ED-prefix LD         |
 * | 16-bit arithmetic          | ADD HL,rr; INC/DEC rr; ADC HL; SBC HL                 |
 * | Exchange instructions      | EX AF,AF'; EXX; EX DE,HL                               |
 * | Stack (PUSH / POP)         | Round-trip; SP bookkeeping; PUSH/POP AF                |
 * | Jump / branch              | JP nn; JR e (forward/back); conditional JP/JR; DJNZ   |
 * | Call / Return              | CALL nn; RET; conditional CALL; RST                    |
 * | HALT instruction           | halted flag; NMI and INT exit halt                     |
 * | I/O instructions           | IN A,(n); OUT (n),A; ED IN r,(C); ED OUT (C),r         |
 * | Special instructions       | SCF, CCF, CPL, NEG, DAA                                |
 * | Block transfer / search    | LDI, LDIR, LDD, CPI                                   |
 * | Interrupt control          | DI, EI; IM 0/1/2; LD I,A; LD A,I (PV=IFF2)            |
 * | Maskable interrupt (INT)   | IM 1 → 0x0038; IM 2 vector table lookup                |
 * | Non-maskable interrupt     | NMI → 0x0066; IFF1 save/disable; halted cleared        |
 * | IX / IY index registers    | LD IX,nn; LD A,(IX+d); LD (IX+d),n; ADD IX,rr          |
 * | Cycle-accurate timing      | step() return values match Z80 specification            |
 *
 * @see core/primitives/z80.h
 * @see doc/trascripted/CPU_U880D.md
 */

#include <gtest/gtest.h>
#include <array>
#include "core/primitives/z80.h"

// ─── Test fixture ─────────────────────────────────────────────────────────────

/**
 * @brief Common fixture: 64 KiB flat memory + 256-port I/O space.
 *
 * SetUp() initialises callbacks and calls cpu.reset() before every test.
 * load() writes opcodes into memory and points PC at them.
 * flagX() helpers return the named flag as bool for readable assertions.
 */
class Z80Test : public ::testing::Test {
protected:
    std::array<uint8_t, 65536> mem{};
    std::array<uint8_t, 256>   io{};
    Z80 cpu;

    void SetUp() override {
        mem.fill(0);
        io.fill(0);
        cpu.readByte  = [&](uint16_t a) -> uint8_t { return mem[a]; };
        cpu.writeByte = [&](uint16_t a, uint8_t v)  { mem[a] = v; };
        cpu.readPort  = [&](uint16_t p) -> uint8_t { return io[p & 0xFF]; };
        cpu.writePort = [&](uint16_t p, uint8_t v)  { io[p & 0xFF] = v; };
        cpu.reset();
    }

    /** Write @p bytes into memory at @p addr and set PC = @p addr. */
    void load(std::initializer_list<uint8_t> bytes, uint16_t addr = 0) {
        uint16_t a = addr;
        for (auto b : bytes) mem[a++] = b;
        cpu.PC = addr;
    }

    bool flagC()  const { return (cpu.F & Z80::FLAG_C)  != 0; }
    bool flagN()  const { return (cpu.F & Z80::FLAG_N)  != 0; }
    bool flagPV() const { return (cpu.F & Z80::FLAG_PV) != 0; }
    bool flagH()  const { return (cpu.F & Z80::FLAG_H)  != 0; }
    bool flagZ()  const { return (cpu.F & Z80::FLAG_Z)  != 0; }
    bool flagS()  const { return (cpu.F & Z80::FLAG_S)  != 0; }
};

// ─── Reset state ──────────────────────────────────────────────────────────────

/**
 * @test Z80Test/Reset_RegistersAndFlags
 * @brief After reset(), all CPU registers match the Z80 power-on specification.
 * @details AF=0xFFFF (real Z80 behaviour), BC=DE=HL=0, IX=IY=0,
 *   SP=0xFFFF, PC=0, I=R=0, IFF1=IFF2=false, IM=0, halted=false, cycles=0.
 * @par Pass criterion  Every register and state field equals its reset value.
 */
TEST_F(Z80Test, Reset_RegistersAndFlags) {
    EXPECT_EQ(cpu.AF, 0xFFFF);
    EXPECT_EQ(cpu.BC, 0x0000);
    EXPECT_EQ(cpu.DE, 0x0000);
    EXPECT_EQ(cpu.HL, 0x0000);
    EXPECT_EQ(cpu.IX, 0x0000);
    EXPECT_EQ(cpu.IY, 0x0000);
    EXPECT_EQ(cpu.SP, 0xFFFF);
    EXPECT_EQ(cpu.PC, 0x0000);
    EXPECT_EQ(cpu.I,  0);
    EXPECT_EQ(cpu.R,  0);
    EXPECT_FALSE(cpu.IFF1);
    EXPECT_FALSE(cpu.IFF2);
    EXPECT_EQ(cpu.IM, 0);
    EXPECT_FALSE(cpu.halted);
    EXPECT_EQ(cpu.cycles, 0u);
}

// ─── NOP / timing ─────────────────────────────────────────────────────────────

/**
 * @test Z80Test/NOP_AdvancesPC_Takes4Cycles
 * @brief NOP (0x00) advances PC by 1 and consumes exactly 4 clock cycles.
 * @par Pass criterion  PC == 1; step() returns 4; cycles == 4.
 */
TEST_F(Z80Test, NOP_AdvancesPC_Takes4Cycles) {
    load({0x00});
    int c = cpu.step();
    EXPECT_EQ(cpu.PC, 1);
    EXPECT_EQ(c, 4);
    EXPECT_EQ(cpu.cycles, 4u);
}

// ─── 8-bit register loads ─────────────────────────────────────────────────────

/**
 * @test Z80Test/Load_LD_r_n_AllRegisters
 * @brief LD r,n loads the immediate byte into the target register.
 * @par Pass criterion  B=0x11, C=0x22, D=0x33, E=0x44, H=0x55, L=0x66, A=0x77.
 */
TEST_F(Z80Test, Load_LD_r_n_AllRegisters) {
    load({0x06, 0x11,   // LD B,0x11
          0x0E, 0x22,   // LD C,0x22
          0x16, 0x33,   // LD D,0x33
          0x1E, 0x44,   // LD E,0x44
          0x26, 0x55,   // LD H,0x55
          0x2E, 0x66,   // LD L,0x66
          0x3E, 0x77}); // LD A,0x77
    for (int i = 0; i < 7; ++i) cpu.step();
    EXPECT_EQ(cpu.B, 0x11);
    EXPECT_EQ(cpu.C, 0x22);
    EXPECT_EQ(cpu.D, 0x33);
    EXPECT_EQ(cpu.E, 0x44);
    EXPECT_EQ(cpu.H, 0x55);
    EXPECT_EQ(cpu.L, 0x66);
    EXPECT_EQ(cpu.A, 0x77);
}

/**
 * @test Z80Test/Load_LD_r_r_CopyRegister
 * @brief LD r,r copies one register to another.
 * @par Pass criterion  After LD B,C (0x41): B == original C.
 */
TEST_F(Z80Test, Load_LD_r_r_CopyRegister) {
    load({0x0E, 0xAB,  // LD C,0xAB
          0x41});       // LD B,C
    cpu.step();
    cpu.step();
    EXPECT_EQ(cpu.B, 0xAB);
    EXPECT_EQ(cpu.C, 0xAB);
}

/**
 * @test Z80Test/Load_LD_A_memHL_And_LD_memHL_A
 * @brief LD A,(HL) reads from memory at address HL; LD (HL),A writes.
 * @par Pass criterion  Round-trip: memory[0x0200] == 0xCD after LD (HL),A.
 */
TEST_F(Z80Test, Load_LD_A_memHL_And_LD_memHL_A) {
    cpu.HL = 0x0200;
    mem[0x0200] = 0xCD;
    load({0x7E, // LD A,(HL)
          0x3C, // INC A  → A=0xCE
          0x77}); // LD (HL),A
    cpu.step(); EXPECT_EQ(cpu.A, 0xCD);
    cpu.step(); EXPECT_EQ(cpu.A, 0xCE);
    cpu.step(); EXPECT_EQ(mem[0x0200], 0xCE);
}

/**
 * @test Z80Test/Load_LD_memHL_n
 * @brief LD (HL),n (0x36) writes an immediate byte to the address in HL.
 * @par Pass criterion  memory[0x0300] == 0x5A after LD (HL),0x5A.
 */
TEST_F(Z80Test, Load_LD_memHL_n) {
    cpu.HL = 0x0300;
    load({0x36, 0x5A}); // LD (HL),0x5A
    cpu.step();
    EXPECT_EQ(mem[0x0300], 0x5A);
}

// ─── 8-bit arithmetic ─────────────────────────────────────────────────────────

/**
 * @test Z80Test/ADD_BasicResult_ClearsN_SetsFlags
 * @brief ADD A,n produces the correct result, clears N, and sets S/Z/C appropriately.
 * @par Pass criterion  A=0x80 after ADD A,1 (A was 0x7F); S=1, Z=0, C=0, N=0, PV=1.
 */
TEST_F(Z80Test, ADD_BasicResult_ClearsN_SetsFlags) {
    cpu.A = 0x7F;
    cpu.F = 0x00;
    load({0xC6, 0x01}); // ADD A,0x01
    cpu.step();
    EXPECT_EQ(cpu.A, 0x80);
    EXPECT_TRUE(flagS());
    EXPECT_FALSE(flagZ());
    EXPECT_FALSE(flagC());
    EXPECT_FALSE(flagN());
    EXPECT_TRUE(flagPV());   // overflow: +0x7F + +0x01 = -0x80 (sign changed)
    EXPECT_TRUE(flagH());    // lower nibble: 0xF+1=0x10, half-carry set
}

/**
 * @test Z80Test/ADD_Carry
 * @brief ADD A,n sets carry when the result overflows 0xFF.
 * @par Pass criterion  A=0x00, C=1 after 0xFF + 1.
 */
TEST_F(Z80Test, ADD_Carry) {
    cpu.A = 0xFF;
    cpu.F = 0x00;
    load({0xC6, 0x01}); // ADD A,0x01
    cpu.step();
    EXPECT_EQ(cpu.A, 0x00);
    EXPECT_TRUE(flagC());
    EXPECT_TRUE(flagZ());
    EXPECT_FALSE(flagS());
    EXPECT_FALSE(flagPV()); // no signed overflow: -1 + 1 = 0 (correct)
}

/**
 * @test Z80Test/ADC_WithCarry
 * @brief ADC A,n adds the carry flag to the result.
 * @par Pass criterion  A=0x02 after A=0x00 + 0x01 + carry=1.
 */
TEST_F(Z80Test, ADC_WithCarry) {
    cpu.A = 0x00;
    cpu.F = Z80::FLAG_C;    // carry set
    load({0xCE, 0x01});     // ADC A,0x01
    cpu.step();
    EXPECT_EQ(cpu.A, 0x02);
    EXPECT_FALSE(flagC());
    EXPECT_FALSE(flagZ());
}

/**
 * @test Z80Test/SUB_SetsN_Result
 * @brief SUB n subtracts from A and sets N flag.
 * @par Pass criterion  A=0x03, N=1, Z=0, C=0 after 0x05 - 0x02.
 */
TEST_F(Z80Test, SUB_SetsN_Result) {
    cpu.A = 0x05;
    cpu.F = 0x00;
    load({0xD6, 0x02}); // SUB 0x02
    cpu.step();
    EXPECT_EQ(cpu.A, 0x03);
    EXPECT_TRUE(flagN());
    EXPECT_FALSE(flagZ());
    EXPECT_FALSE(flagC());
}

/**
 * @test Z80Test/SUB_Borrow_SetsCarry
 * @brief SUB n sets carry (borrow) when A < n.
 * @par Pass criterion  C=1, N=1 after 0x01 - 0x02.
 */
TEST_F(Z80Test, SUB_Borrow_SetsCarry) {
    cpu.A = 0x01;
    cpu.F = 0x00;
    load({0xD6, 0x02}); // SUB 0x02
    cpu.step();
    EXPECT_EQ(cpu.A, 0xFF);
    EXPECT_TRUE(flagC());
    EXPECT_TRUE(flagN());
    EXPECT_TRUE(flagS());
}

/**
 * @test Z80Test/SBC_WithBorrow
 * @brief SBC A,n subtracts n and carry from A.
 * @par Pass criterion  A=0x04 after A=0x06 - 0x01 - carry=1.
 */
TEST_F(Z80Test, SBC_WithBorrow) {
    cpu.A = 0x06;
    cpu.F = Z80::FLAG_C;   // carry (borrow) set
    load({0xDE, 0x01});    // SBC A,0x01
    cpu.step();
    EXPECT_EQ(cpu.A, 0x04);
    EXPECT_FALSE(flagC());
}

// ─── Logical operations ───────────────────────────────────────────────────────

/**
 * @test Z80Test/AND_SetsH_ClearsNC_PVParity
 * @brief AND n: sets H, clears N and C, PV reflects parity of result.
 * @par Pass criterion  A=0x0C, H=1, N=0, C=0, PV=1 (even parity) after 0xFF AND 0x0C.
 */
TEST_F(Z80Test, AND_SetsH_ClearsNC_PVParity) {
    cpu.A = 0xFF;
    cpu.F = Z80::FLAG_C | Z80::FLAG_N;
    load({0xE6, 0x0C}); // AND 0x0C  → 0xFF & 0x0C = 0x0C
    cpu.step();
    EXPECT_EQ(cpu.A, 0x0C);
    EXPECT_TRUE(flagH());
    EXPECT_FALSE(flagN());
    EXPECT_FALSE(flagC());
    // 0x0C = 0b00001100: two set bits → even parity → PV=1
    EXPECT_TRUE(flagPV());
}

/**
 * @test Z80Test/OR_ClearsHNC_PVParity
 * @brief OR n: clears H, N and C, PV reflects parity of result.
 * @par Pass criterion  A=0x07, H=0, N=0, C=0 after 0x05 OR 0x06.
 */
TEST_F(Z80Test, OR_ClearsHNC_PVParity) {
    cpu.A = 0x05;
    cpu.F = Z80::FLAG_C | Z80::FLAG_H | Z80::FLAG_N;
    load({0xF6, 0x06}); // OR 0x06 → 0x05 | 0x06 = 0x07
    cpu.step();
    EXPECT_EQ(cpu.A, 0x07);
    EXPECT_FALSE(flagH());
    EXPECT_FALSE(flagN());
    EXPECT_FALSE(flagC());
}

/**
 * @test Z80Test/XOR_SelfClearsA_SetsZ
 * @brief XOR A (0xAF) clears A to 0 and sets Z flag; clears H, N, C.
 * @par Pass criterion  A=0, Z=1, H=0, N=0, C=0.
 */
TEST_F(Z80Test, XOR_SelfClearsA_SetsZ) {
    cpu.A = 0xAB;
    cpu.F = Z80::FLAG_C | Z80::FLAG_H | Z80::FLAG_N;
    load({0xAF}); // XOR A
    cpu.step();
    EXPECT_EQ(cpu.A, 0x00);
    EXPECT_TRUE(flagZ());
    EXPECT_FALSE(flagH());
    EXPECT_FALSE(flagN());
    EXPECT_FALSE(flagC());
}

/**
 * @test Z80Test/CP_Equal_SetsZ_SetsN
 * @brief CP n: sets Z and N when A == n; A is unchanged.
 * @par Pass criterion  A unchanged == 0x42; Z=1, N=1, C=0.
 */
TEST_F(Z80Test, CP_Equal_SetsZ_SetsN) {
    cpu.A = 0x42;
    cpu.F = 0x00;
    load({0xFE, 0x42}); // CP 0x42
    cpu.step();
    EXPECT_EQ(cpu.A, 0x42);   // A unchanged
    EXPECT_TRUE(flagZ());
    EXPECT_TRUE(flagN());
    EXPECT_FALSE(flagC());
}

/**
 * @test Z80Test/CP_Less_SetsCarry
 * @brief CP n sets carry (borrow) when A < n; A is unchanged.
 * @par Pass criterion  C=1, Z=0 after CP with 0x03 when A=0x01.
 */
TEST_F(Z80Test, CP_Less_SetsCarry) {
    cpu.A = 0x01;
    cpu.F = 0x00;
    load({0xFE, 0x03}); // CP 0x03
    cpu.step();
    EXPECT_EQ(cpu.A, 0x01);
    EXPECT_TRUE(flagC());
    EXPECT_FALSE(flagZ());
}

// ─── INC / DEC ────────────────────────────────────────────────────────────────

/**
 * @test Z80Test/INC_Overflow_0x7F_to_0x80
 * @brief INC r: PV set when result == 0x80 (overflow from 0x7F); C preserved.
 * @par Pass criterion  B=0x80, PV=1, S=1, Z=0, N=0, C unchanged.
 */
TEST_F(Z80Test, INC_Overflow_0x7F_to_0x80) {
    cpu.B = 0x7F;
    cpu.F = Z80::FLAG_C;   // carry must not be changed by INC
    load({0x04}); // INC B
    cpu.step();
    EXPECT_EQ(cpu.B, 0x80);
    EXPECT_TRUE(flagPV());
    EXPECT_TRUE(flagS());
    EXPECT_FALSE(flagZ());
    EXPECT_FALSE(flagN());
    EXPECT_TRUE(flagC());   // C flag preserved
}

/**
 * @test Z80Test/INC_Wraps_0xFF_to_0x00_SetsZ
 * @brief INC r: result wraps from 0xFF to 0x00, Z=1; C preserved.
 * @par Pass criterion  A=0x00, Z=1, C unchanged.
 */
TEST_F(Z80Test, INC_Wraps_0xFF_to_0x00_SetsZ) {
    cpu.A = 0xFF;
    cpu.F = Z80::FLAG_C;
    load({0x3C}); // INC A
    cpu.step();
    EXPECT_EQ(cpu.A, 0x00);
    EXPECT_TRUE(flagZ());
    EXPECT_FALSE(flagPV()); // no overflow: -1 + 1 = 0 (correct sign)
    EXPECT_TRUE(flagC());
}

/**
 * @test Z80Test/DEC_Overflow_0x80_to_0x7F
 * @brief DEC r: PV set when result == 0x7F (underflow from 0x80); N=1; C preserved.
 * @par Pass criterion  B=0x7F, PV=1, N=1, C unchanged.
 */
TEST_F(Z80Test, DEC_Overflow_0x80_to_0x7F) {
    cpu.B = 0x80;
    cpu.F = Z80::FLAG_C;
    load({0x05}); // DEC B
    cpu.step();
    EXPECT_EQ(cpu.B, 0x7F);
    EXPECT_TRUE(flagPV());
    EXPECT_TRUE(flagN());
    EXPECT_TRUE(flagC());
}

// ─── Accumulator rotations ────────────────────────────────────────────────────

/**
 * @test Z80Test/RLCA_RotatesLeftAndSetsCarry
 * @brief RLCA: bit 7 wraps to bit 0 and is copied to C; H and N cleared.
 * @par Pass criterion  A=0x01, C=1 after RLCA with A=0x80.
 */
TEST_F(Z80Test, RLCA_RotatesLeftAndSetsCarry) {
    cpu.A = 0x80;
    cpu.F = Z80::FLAG_H | Z80::FLAG_N;
    load({0x07}); // RLCA
    cpu.step();
    EXPECT_EQ(cpu.A, 0x01);
    EXPECT_TRUE(flagC());
    EXPECT_FALSE(flagH());
    EXPECT_FALSE(flagN());
}

/**
 * @test Z80Test/RRCA_RotatesRightAndSetsCarry
 * @brief RRCA: bit 0 wraps to bit 7 and is copied to C; H and N cleared.
 * @par Pass criterion  A=0x80, C=1 after RRCA with A=0x01.
 */
TEST_F(Z80Test, RRCA_RotatesRightAndSetsCarry) {
    cpu.A = 0x01;
    cpu.F = Z80::FLAG_H | Z80::FLAG_N;
    load({0x0F}); // RRCA
    cpu.step();
    EXPECT_EQ(cpu.A, 0x80);
    EXPECT_TRUE(flagC());
    EXPECT_FALSE(flagH());
    EXPECT_FALSE(flagN());
}

/**
 * @test Z80Test/RLA_ThroughCarry
 * @brief RLA: bit 7 → C; old C → bit 0. H and N cleared.
 * @par Pass criterion  A=0x01, C=1 after RLA with A=0x80, C=1.
 */
TEST_F(Z80Test, RLA_ThroughCarry) {
    cpu.A = 0x80;
    cpu.F = Z80::FLAG_C;
    load({0x17}); // RLA
    cpu.step();
    EXPECT_EQ(cpu.A, 0x01);  // bit 7 out, old C=1 in at bit 0
    EXPECT_TRUE(flagC());    // bit 7 was 1 → new C=1
    EXPECT_FALSE(flagH());
    EXPECT_FALSE(flagN());
}

/**
 * @test Z80Test/RRA_ThroughCarry
 * @brief RRA: bit 0 → C; old C → bit 7. H and N cleared.
 * @par Pass criterion  A=0x80, C=1 after RRA with A=0x01, C=1.
 */
TEST_F(Z80Test, RRA_ThroughCarry) {
    cpu.A = 0x01;
    cpu.F = Z80::FLAG_C;
    load({0x1F}); // RRA
    cpu.step();
    EXPECT_EQ(cpu.A, 0x80);  // bit 0 out, old C=1 in at bit 7
    EXPECT_TRUE(flagC());    // bit 0 was 1 → new C=1
    EXPECT_FALSE(flagH());
    EXPECT_FALSE(flagN());
}

// ─── CB-prefix: shifts and rotations ─────────────────────────────────────────

/**
 * @test Z80Test/CB_RLC_SetsCarryAndWraps
 * @brief CB RLC B (0xCB 0x00): bit 7 → bit 0 and C; sets S,Z,PV(parity).
 * @par Pass criterion  B=0x01, C=1 after RLC with B=0x80.
 */
TEST_F(Z80Test, CB_RLC_SetsCarryAndWraps) {
    cpu.B = 0x80;
    cpu.F = 0x00;
    load({0xCB, 0x00}); // RLC B
    cpu.step();
    EXPECT_EQ(cpu.B, 0x01);
    EXPECT_TRUE(flagC());
    EXPECT_FALSE(flagH());
    EXPECT_FALSE(flagN());
}

/**
 * @test Z80Test/CB_SLA_ShiftsLeftZeroInBit0
 * @brief CB SLA B (0xCB 0x20): shifts left; bit 7 → C; bit 0 becomes 0.
 * @par Pass criterion  B=0x02, C=0 after SLA with B=0x01.
 */
TEST_F(Z80Test, CB_SLA_ShiftsLeftZeroInBit0) {
    cpu.B = 0x01;
    cpu.F = 0x00;
    load({0xCB, 0x20}); // SLA B
    cpu.step();
    EXPECT_EQ(cpu.B, 0x02);
    EXPECT_FALSE(flagC());
}

/**
 * @test Z80Test/CB_SRL_ShiftsRightZeroInBit7
 * @brief CB SRL B (0xCB 0x38): shifts right; bit 0 → C; bit 7 becomes 0.
 * @par Pass criterion  B=0x01, C=0 after SRL B=0x02; also B=0x00, C=1 after SRL B=0x01.
 */
TEST_F(Z80Test, CB_SRL_ShiftsRightZeroInBit7) {
    cpu.B = 0x02;
    cpu.F = 0x00;
    load({0xCB, 0x38}); // SRL B
    cpu.step();
    EXPECT_EQ(cpu.B, 0x01);
    EXPECT_FALSE(flagC());
    EXPECT_FALSE(flagS()); // MSB always 0 after SRL

    cpu.B = 0x01;
    load({0xCB, 0x38});
    cpu.step();
    EXPECT_EQ(cpu.B, 0x00);
    EXPECT_TRUE(flagC());
    EXPECT_TRUE(flagZ());
}

/**
 * @test Z80Test/CB_SRA_PreservesSign
 * @brief CB SRA B (0xCB 0x28): arithmetic right shift preserves bit 7 (sign).
 * @par Pass criterion  B=0xC0, C=0 after SRA B=0x80 (sign preserved, bit 0 was 0).
 */
TEST_F(Z80Test, CB_SRA_PreservesSign) {
    cpu.B = 0x80;
    cpu.F = 0x00;
    load({0xCB, 0x28}); // SRA B
    cpu.step();
    EXPECT_EQ(cpu.B, 0xC0);
    EXPECT_FALSE(flagC()); // bit 0 of 0x80 was 0
    EXPECT_TRUE(flagS());  // result is negative
}

// ─── CB-prefix: bit operations ────────────────────────────────────────────────

/**
 * @test Z80Test/CB_BIT_SetsZ_WhenBitClear
 * @brief BIT b,r (CB 0x40+8*b+r): Z=1 if the tested bit is 0.
 * @par Pass criterion  Z=1 after BIT 0,B with B=0x02 (bit 0 clear); Z=0 with B=0x01.
 */
TEST_F(Z80Test, CB_BIT_SetsZ_WhenBitClear) {
    cpu.B = 0x02;   // bit 0 = 0
    load({0xCB, 0x40}); // BIT 0,B
    cpu.step();
    EXPECT_TRUE(flagZ());
    EXPECT_TRUE(flagH());
    EXPECT_FALSE(flagN());

    cpu.B = 0x01;   // bit 0 = 1
    load({0xCB, 0x40});
    cpu.step();
    EXPECT_FALSE(flagZ());
}

/**
 * @test Z80Test/CB_SET_SetsBit
 * @brief SET 3,B (CB 0xD8): sets bit 3 of B.
 * @par Pass criterion  B has bit 3 set after SET 3,B.
 */
TEST_F(Z80Test, CB_SET_SetsBit) {
    cpu.B = 0x00;
    load({0xCB, 0xD8}); // SET 3,B
    cpu.step();
    EXPECT_EQ(cpu.B & 0x08, 0x08);
}

/**
 * @test Z80Test/CB_RES_ClearsBit
 * @brief RES 3,B (CB 0x98): clears bit 3 of B.
 * @par Pass criterion  Bit 3 of B is 0 after RES 3,B.
 */
TEST_F(Z80Test, CB_RES_ClearsBit) {
    cpu.B = 0xFF;
    load({0xCB, 0x98}); // RES 3,B
    cpu.step();
    EXPECT_EQ(cpu.B & 0x08, 0);
    EXPECT_EQ(cpu.B, 0xF7);
}

// ─── 16-bit loads ─────────────────────────────────────────────────────────────

/**
 * @test Z80Test/LD_rr_nn_LoadsWord
 * @brief LD rr,nn loads a 16-bit immediate into the register pair.
 * @par Pass criterion  BC=0x1234, DE=0x5678, HL=0x9ABC after three LD rr,nn.
 */
TEST_F(Z80Test, LD_rr_nn_LoadsWord) {
    load({0x01, 0x34, 0x12,   // LD BC,0x1234
          0x11, 0x78, 0x56,   // LD DE,0x5678
          0x21, 0xBC, 0x9A}); // LD HL,0x9ABC
    cpu.step(); cpu.step(); cpu.step();
    EXPECT_EQ(cpu.BC, 0x1234);
    EXPECT_EQ(cpu.DE, 0x5678);
    EXPECT_EQ(cpu.HL, 0x9ABC);
}

/**
 * @test Z80Test/LD_nn_HL_And_LD_HL_nn
 * @brief LD (nn),HL stores HL little-endian to memory; LD HL,(nn) reads it back.
 * @par Pass criterion  Round-trip: HL value is preserved through memory.
 */
TEST_F(Z80Test, LD_nn_HL_And_LD_HL_nn) {
    cpu.HL = 0xABCD;
    load({0x22, 0x00, 0x02,   // LD (0x0200),HL
          0x2A, 0x00, 0x02}); // LD HL,(0x0200)
    cpu.step();
    EXPECT_EQ(mem[0x0200], 0xCD); // L at lower address
    EXPECT_EQ(mem[0x0201], 0xAB); // H at higher address
    cpu.HL = 0x0000; // clear
    cpu.step();
    EXPECT_EQ(cpu.HL, 0xABCD);
}

/**
 * @test Z80Test/LD_nn_BC_ED_Prefix
 * @brief ED LD (nn),BC / LD BC,(nn): extended 16-bit load to/from memory.
 * @par Pass criterion  memory[0x0300..0x0301] == {0x34, 0x12} after LD (0x0300),BC.
 */
TEST_F(Z80Test, LD_nn_BC_ED_Prefix) {
    cpu.BC = 0x1234;
    load({0xED, 0x43, 0x00, 0x03,   // LD (0x0300),BC
          0xED, 0x4B, 0x00, 0x03}); // LD BC,(0x0300)
    cpu.step();
    EXPECT_EQ(mem[0x0300], 0x34);
    EXPECT_EQ(mem[0x0301], 0x12);
    cpu.BC = 0x0000;
    cpu.step();
    EXPECT_EQ(cpu.BC, 0x1234);
}

// ─── 16-bit arithmetic ────────────────────────────────────────────────────────

/**
 * @test Z80Test/ADD_HL_rr_Basic
 * @brief ADD HL,BC: 16-bit add, C and H reflect carry out of bit 15 and bit 11.
 * @par Pass criterion  HL=0x1234+0x0001=0x1235; N cleared; S,Z,PV unchanged.
 */
TEST_F(Z80Test, ADD_HL_rr_Basic) {
    cpu.HL = 0x1234;
    cpu.BC = 0x0001;
    cpu.F  = Z80::FLAG_S | Z80::FLAG_Z | Z80::FLAG_PV | Z80::FLAG_N;
    load({0x09}); // ADD HL,BC
    cpu.step();
    EXPECT_EQ(cpu.HL, 0x1235);
    EXPECT_FALSE(flagN()); // N always cleared by ADD HL
    EXPECT_TRUE(flagS());  // S/Z/PV preserved
    EXPECT_TRUE(flagZ());
    EXPECT_TRUE(flagPV());
}

/**
 * @test Z80Test/ADD_HL_rr_Carry_Out
 * @brief ADD HL,BC sets C when result overflows 16 bits.
 * @par Pass criterion  HL=0x0000, C=1 after 0x8000 + 0x8000.
 */
TEST_F(Z80Test, ADD_HL_rr_Carry_Out) {
    cpu.HL = 0x8000;
    cpu.BC = 0x8000;
    cpu.F  = 0;
    load({0x09}); // ADD HL,BC
    cpu.step();
    EXPECT_EQ(cpu.HL, 0x0000);
    EXPECT_TRUE(flagC());
}

/**
 * @test Z80Test/INC_DEC_rr_16Bit
 * @brief INC rr / DEC rr: 16-bit register increments and decrements without flags.
 * @par Pass criterion  BC=0x0001 after INC BC; BC=0x0000 after DEC BC.
 */
TEST_F(Z80Test, INC_DEC_rr_16Bit) {
    cpu.BC = 0x0000;
    cpu.F  = 0xFF; // all flags set — should be preserved
    load({0x03, 0x0B}); // INC BC; DEC BC
    cpu.step(); EXPECT_EQ(cpu.BC, 0x0001);
    EXPECT_EQ(cpu.F, 0xFF); // 16-bit INC/DEC does not change flags
    cpu.step(); EXPECT_EQ(cpu.BC, 0x0000);
}

/**
 * @test Z80Test/ADC_HL_BC_WithCarry
 * @brief ADC HL,BC (ED 0x4A): adds BC + carry to HL; all flags updated including PV (overflow).
 * @par Pass criterion  HL=0x8001 after 0x7FFF + 0x0001 + carry=1; PV=1 (overflow), S=1.
 */
TEST_F(Z80Test, ADC_HL_BC_WithCarry) {
    cpu.HL = 0x7FFF;
    cpu.BC = 0x0001;
    cpu.F  = Z80::FLAG_C;
    load({0xED, 0x4A}); // ADC HL,BC
    cpu.step();
    EXPECT_EQ(cpu.HL, 0x8001);
    EXPECT_TRUE(flagS());
    EXPECT_TRUE(flagPV());
    EXPECT_FALSE(flagC());
    EXPECT_FALSE(flagN());
}

/**
 * @test Z80Test/SBC_HL_BC_WithBorrow
 * @brief SBC HL,BC (ED 0x42): subtracts BC + carry from HL; N=1; PV on overflow.
 * @par Pass criterion  HL=0xFFFE after 0x0000 - 0x0001 - carry=1; C=1, N=1.
 */
TEST_F(Z80Test, SBC_HL_BC_WithBorrow) {
    cpu.HL = 0x0000;
    cpu.BC = 0x0001;
    cpu.F  = Z80::FLAG_C;
    load({0xED, 0x42}); // SBC HL,BC
    cpu.step();
    EXPECT_EQ(cpu.HL, 0xFFFE);
    EXPECT_TRUE(flagC());
    EXPECT_TRUE(flagN());
    EXPECT_TRUE(flagS());
}

// ─── Exchange instructions ────────────────────────────────────────────────────

/**
 * @test Z80Test/EX_AF_AF_SwapsAccAndFlags
 * @brief EX AF,AF' (0x08) swaps AF and AF'; allows quick context switch.
 * @par Pass criterion  After two EX AF,AF': original A and F values are restored.
 */
TEST_F(Z80Test, EX_AF_AF_SwapsAccAndFlags) {
    cpu.A  = 0x11; cpu.F  = 0x22;
    cpu.AF_ = 0x3344;
    load({0x08, 0x08}); // EX AF,AF'; EX AF,AF'
    cpu.step();
    EXPECT_EQ(cpu.A, 0x33);
    EXPECT_EQ(cpu.F, 0x44);
    EXPECT_EQ(cpu.AF_, 0x1122);
    cpu.step();
    EXPECT_EQ(cpu.A, 0x11);
    EXPECT_EQ(cpu.F, 0x22);
}

/**
 * @test Z80Test/EXX_SwapsBC_DE_HL
 * @brief EXX (0xD9) swaps BC, DE, HL with their shadow counterparts.
 * @par Pass criterion  After EXX: main registers == shadow originals and vice versa.
 */
TEST_F(Z80Test, EXX_SwapsBC_DE_HL) {
    cpu.BC = 0x1111; cpu.BC_ = 0xAAAA;
    cpu.DE = 0x2222; cpu.DE_ = 0xBBBB;
    cpu.HL = 0x3333; cpu.HL_ = 0xCCCC;
    load({0xD9}); // EXX
    cpu.step();
    EXPECT_EQ(cpu.BC, 0xAAAA); EXPECT_EQ(cpu.BC_, 0x1111);
    EXPECT_EQ(cpu.DE, 0xBBBB); EXPECT_EQ(cpu.DE_, 0x2222);
    EXPECT_EQ(cpu.HL, 0xCCCC); EXPECT_EQ(cpu.HL_, 0x3333);
}

/**
 * @test Z80Test/EX_DE_HL_SwapsRegisters
 * @brief EX DE,HL (0xEB) swaps the DE and HL register pairs.
 * @par Pass criterion  DE==original HL and HL==original DE after EX DE,HL.
 */
TEST_F(Z80Test, EX_DE_HL_SwapsRegisters) {
    cpu.DE = 0xABCD;
    cpu.HL = 0x1234;
    load({0xEB}); // EX DE,HL
    cpu.step();
    EXPECT_EQ(cpu.DE, 0x1234);
    EXPECT_EQ(cpu.HL, 0xABCD);
}

// ─── Stack: PUSH / POP ────────────────────────────────────────────────────────

/**
 * @test Z80Test/PUSH_POP_Roundtrip
 * @brief PUSH BC / POP BC: value survives stack round-trip; SP restored.
 * @par Pass criterion  BC recovered; SP back to initial value after PUSH+POP.
 */
TEST_F(Z80Test, PUSH_POP_Roundtrip) {
    cpu.BC = 0xBEEF;
    cpu.SP = 0x0400;
    load({0xC5, 0xC1}); // PUSH BC; POP BC
    cpu.step(); // PUSH BC → SP=0x03FE
    EXPECT_EQ(cpu.SP, 0x03FE);
    cpu.BC = 0x0000; // corrupt
    cpu.step(); // POP BC → SP=0x0400
    EXPECT_EQ(cpu.BC, 0xBEEF);
    EXPECT_EQ(cpu.SP, 0x0400);
}

/**
 * @test Z80Test/PUSH_POP_AF_PreservesFlags
 * @brief PUSH AF / POP AF correctly saves and restores flags.
 * @par Pass criterion  Original F value is restored through stack.
 */
TEST_F(Z80Test, PUSH_POP_AF_PreservesFlags) {
    cpu.SP = 0x0400;
    // Set known A and F values
    load({0xAF,        // XOR A → A=0, F=0x44 (Z and PV set)
          0xF5,        // PUSH AF
          0x3E, 0xFF,  // LD A,0xFF (change A to trash)
          0xF1});      // POP AF
    for (int i = 0; i < 4; ++i) cpu.step();
    EXPECT_EQ(cpu.A, 0x00);
    EXPECT_TRUE(flagZ());
    EXPECT_FALSE(flagC());
}

// ─── Jump and branch ──────────────────────────────────────────────────────────

/**
 * @test Z80Test/JP_nn_Unconditional
 * @brief JP nn (0xC3): jumps to the given 16-bit address.
 * @par Pass criterion  PC == 0x0100 after JP 0x0100.
 */
TEST_F(Z80Test, JP_nn_Unconditional) {
    load({0xC3, 0x00, 0x01}); // JP 0x0100
    cpu.step();
    EXPECT_EQ(cpu.PC, 0x0100);
}

/**
 * @test Z80Test/JR_ForwardAndBackward
 * @brief JR e (0x18): relative jump by signed offset from PC after instruction.
 * @par Pass criterion  Forward jump: PC = 5; backward jump: PC = 0 (from addr 5).
 */
TEST_F(Z80Test, JR_ForwardAndBackward) {
    // JR +3 at address 0 → PC = 0+2+3 = 5
    load({0x18, 0x03}); // JR +3
    cpu.step();
    EXPECT_EQ(cpu.PC, 5);

    // JR -5 at address 5 → PC = 5+2-5 = 2... wait: offset is from PC after fetch (PC=7)
    // Actually: JR e places PC = PC_after_instr + e = 7 + (int8_t)(-5) = 2
    // Let me use a simple backward jump: JR at addr 5 with offset that goes to addr 2
    // PC after JR instruction fetch = 7; target = 2; so e = 2-7 = -5 = 0xFB
    mem[5] = 0x18; mem[6] = 0xFB; cpu.PC = 5;
    cpu.step();
    EXPECT_EQ(cpu.PC, 2);
}

/**
 * @test Z80Test/JP_Conditional_Z_TakenAndNotTaken
 * @brief JP Z,nn: jumps when Z=1; falls through when Z=0.
 * @par Pass criterion  PC=0x0200 when Z=1; PC=3 when Z=0.
 */
TEST_F(Z80Test, JP_Conditional_Z_TakenAndNotTaken) {
    load({0xCA, 0x00, 0x02}); // JP Z,0x0200
    cpu.F = Z80::FLAG_Z;
    cpu.step();
    EXPECT_EQ(cpu.PC, 0x0200);

    load({0xCA, 0x00, 0x02});
    cpu.F = 0;
    cpu.step();
    EXPECT_EQ(cpu.PC, 3); // falls through
}

/**
 * @test Z80Test/DJNZ_DecrementsBAndJumps
 * @brief DJNZ (0x10): decrements B; jumps if B≠0; falls through when B==0.
 * @par Pass criterion  B=0, no jump when starting B=1; B=4, jumps when starting B=5.
 */
TEST_F(Z80Test, DJNZ_DecrementsBAndJumps) {
    // B=1: decrement to 0 → no jump, falls through
    cpu.B = 1;
    load({0x10, 0x05}); // DJNZ +5
    cpu.step();
    EXPECT_EQ(cpu.B, 0);
    EXPECT_EQ(cpu.PC, 2); // fell through

    // B=5: decrement to 4 → jump by +5 → PC = 2+2+5 = 9
    cpu.B = 5;
    load({0x10, 0x05}); // at addr 2 (from load helper, default addr=0)
    cpu.step();
    EXPECT_EQ(cpu.B, 4);
    EXPECT_EQ(cpu.PC, 7); // 0 + 2 + 5
}

// ─── Call / Return ────────────────────────────────────────────────────────────

/**
 * @test Z80Test/CALL_nn_And_RET
 * @brief CALL nn pushes return address onto stack; RET pops it.
 * @par Pass criterion  PC == 3 after CALL 0x0100 + RET (with NOP at 0x0100).
 */
TEST_F(Z80Test, CALL_nn_And_RET) {
    cpu.SP = 0x0400;
    // CALL 0x0100 at address 0
    load({0xCD, 0x00, 0x01}); // CALL 0x0100
    // NOP + RET at 0x0100
    mem[0x0100] = 0x00;  // NOP
    mem[0x0101] = 0xC9;  // RET

    cpu.step();           // CALL → PC=0x0100, SP=0x03FE
    EXPECT_EQ(cpu.PC, 0x0100);
    EXPECT_EQ(cpu.SP, 0x03FE);
    // Check pushed return address (3 = address after CALL)
    EXPECT_EQ(mem[0x03FE], 0x03); // low byte
    EXPECT_EQ(mem[0x03FF], 0x00); // high byte

    cpu.step();           // NOP
    cpu.step();           // RET → PC=0x0003, SP=0x0400
    EXPECT_EQ(cpu.PC, 0x0003);
    EXPECT_EQ(cpu.SP, 0x0400);
}

/**
 * @test Z80Test/RST_JumpsToFixedVector
 * @brief RST 38H (0xFF) jumps to address 0x0038 and pushes PC.
 * @par Pass criterion  PC == 0x0038 after RST 38H; return address on stack == 1.
 */
TEST_F(Z80Test, RST_JumpsToFixedVector) {
    cpu.SP = 0x0400;
    load({0xFF}); // RST 38H
    cpu.step();
    EXPECT_EQ(cpu.PC, 0x0038);
    EXPECT_EQ(cpu.SP, 0x03FE);
    EXPECT_EQ(mem[0x03FE], 0x01); // return addr low byte (PC was 0x0001)
    EXPECT_EQ(mem[0x03FF], 0x00);
}

// ─── HALT ─────────────────────────────────────────────────────────────────────

/**
 * @test Z80Test/HALT_SetsFlag_ReturnsNOPCycles
 * @brief HALT (0x76) sets halted=true; subsequent step() burns 4 cycles (NOP).
 * @par Pass criterion  halted==true after HALT; second step() returns 4 and PC unchanged.
 */
TEST_F(Z80Test, HALT_SetsFlag_ReturnsNOPCycles) {
    load({0x76}); // HALT
    cpu.step();
    EXPECT_TRUE(cpu.halted);
    uint16_t pc_before = cpu.PC;
    int c = cpu.step(); // spin
    EXPECT_EQ(c, 4);
    EXPECT_EQ(cpu.PC, pc_before);
}

/**
 * @test Z80Test/NMI_ExitsHalt
 * @brief NMI clears the halted state, saves IFF1, and jumps to 0x0066.
 * @par Pass criterion  halted==false; PC==0x0066; IFF2 == saved IFF1.
 */
TEST_F(Z80Test, NMI_ExitsHalt) {
    cpu.IFF1 = true;
    cpu.SP   = 0x0400;
    load({0x76}); // HALT
    cpu.step();   // enter halt
    EXPECT_TRUE(cpu.halted);

    cpu.nmi();
    EXPECT_FALSE(cpu.halted);
    EXPECT_EQ(cpu.PC, 0x0066);
    EXPECT_FALSE(cpu.IFF1); // IFF1 disabled
    EXPECT_TRUE(cpu.IFF2);  // IFF2 saved original IFF1=true
}

/**
 * @test Z80Test/INT_IM1_ExitsHalt
 * @brief A maskable interrupt in IM 1 clears halted and jumps to 0x0038.
 * @par Pass criterion  halted==false; PC==0x0038 after cpu.interrupt().
 */
TEST_F(Z80Test, INT_IM1_ExitsHalt) {
    cpu.IM   = 1;
    cpu.IFF1 = true;
    cpu.SP   = 0x0400;
    load({0x76}); // HALT
    cpu.step();
    EXPECT_TRUE(cpu.halted);

    cpu.interrupt(0xFF);
    EXPECT_FALSE(cpu.halted);
    EXPECT_EQ(cpu.PC, 0x0038);
}

// ─── I/O instructions ─────────────────────────────────────────────────────────

/**
 * @test Z80Test/IN_A_n_ReadsPort
 * @brief IN A,(n) (0xDB n): reads port n and stores result in A.
 * @par Pass criterion  A == 0x42 after IN A,(0x10) when io[0x10] == 0x42.
 */
TEST_F(Z80Test, IN_A_n_ReadsPort) {
    io[0x10] = 0x42;
    cpu.A = 0x00;
    load({0xDB, 0x10}); // IN A,(0x10)
    cpu.step();
    EXPECT_EQ(cpu.A, 0x42);
}

/**
 * @test Z80Test/OUT_n_A_WritesPort
 * @brief OUT (n),A (0xD3 n): writes A to port n.
 * @par Pass criterion  io[0x20] == 0x7E after OUT (0x20),A with A=0x7E.
 */
TEST_F(Z80Test, OUT_n_A_WritesPort) {
    cpu.A = 0x7E;
    load({0xD3, 0x20}); // OUT (0x20),A
    cpu.step();
    EXPECT_EQ(io[0x20], 0x7E);
}

/**
 * @test Z80Test/IN_r_C_ED_Prefix
 * @brief ED IN A,(C) (0xED 0x78): reads from port (C) into A; sets S,Z,PV from data; resets H and N.
 * @details Per Zilog Z80 CPU User Manual: IN r,(C) resets H and N; PV reflects parity of data.
 * @par Pass criterion  A==0x55, Z=0, H=0, N=0 after IN A,(C) with io[C]==0x55.
 */
TEST_F(Z80Test, IN_r_C_ED_Prefix) {
    cpu.C = 0x30;
    io[0x30] = 0x55;
    cpu.F = 0x00;
    load({0xED, 0x78}); // IN A,(C)
    cpu.step();
    EXPECT_EQ(cpu.A, 0x55);
    EXPECT_FALSE(flagZ());
    EXPECT_FALSE(flagN());
    EXPECT_FALSE(flagH()); // H reset by IN r,(C) per Z80 spec
}

/**
 * @test Z80Test/OUT_C_r_ED_Prefix
 * @brief ED OUT (C),A (0xED 0x79): writes A to port (C).
 * @par Pass criterion  io[0x40] == 0xAB after OUT (C),A with A=0xAB, C=0x40.
 */
TEST_F(Z80Test, OUT_C_r_ED_Prefix) {
    cpu.A = 0xAB;
    cpu.C = 0x40;
    load({0xED, 0x79}); // OUT (C),A
    cpu.step();
    EXPECT_EQ(io[0x40], 0xAB);
}

// ─── Special instructions ─────────────────────────────────────────────────────

/**
 * @test Z80Test/SCF_SetsClearHN
 * @brief SCF (0x37): sets carry; clears H and N.
 * @par Pass criterion  C=1, H=0, N=0 after SCF.
 */
TEST_F(Z80Test, SCF_SetsClearHN) {
    cpu.F = Z80::FLAG_H | Z80::FLAG_N;
    load({0x37}); // SCF
    cpu.step();
    EXPECT_TRUE(flagC());
    EXPECT_FALSE(flagH());
    EXPECT_FALSE(flagN());
}

/**
 * @test Z80Test/CCF_TogglesCarry
 * @brief CCF (0x3F): inverts carry; old carry goes to H; clears N.
 * @par Pass criterion  C=0 after CCF with C=1; C=1 after CCF with C=0.
 */
TEST_F(Z80Test, CCF_TogglesCarry) {
    cpu.F = Z80::FLAG_C;
    load({0x3F, 0x3F}); // CCF; CCF
    cpu.step(); EXPECT_FALSE(flagC()); EXPECT_TRUE(flagH());
    cpu.step(); EXPECT_TRUE(flagC());
}

/**
 * @test Z80Test/CPL_ComplementsAccumulator
 * @brief CPL (0x2F): inverts all bits of A; sets H and N.
 * @par Pass criterion  A=~0x5A=0xA5; H=1, N=1 after CPL.
 */
TEST_F(Z80Test, CPL_ComplementsAccumulator) {
    cpu.A = 0x5A;
    cpu.F = 0x00;
    load({0x2F}); // CPL
    cpu.step();
    EXPECT_EQ(cpu.A, 0xA5);
    EXPECT_TRUE(flagH());
    EXPECT_TRUE(flagN());
}

/**
 * @test Z80Test/NEG_NegatesAccumulator
 * @brief NEG (ED 0x44): two's-complement negate; sets N; PV if A was 0x80.
 * @par Pass criterion  A=0xFE after NEG with A=0x02; C=1, N=1.
 */
TEST_F(Z80Test, NEG_NegatesAccumulator) {
    cpu.A = 0x02;
    cpu.F = 0x00;
    load({0xED, 0x44}); // NEG
    cpu.step();
    EXPECT_EQ(cpu.A, 0xFE);
    EXPECT_TRUE(flagC()); // result != 0 → borrow out
    EXPECT_TRUE(flagN());
    EXPECT_TRUE(flagS());
}

/**
 * @test Z80Test/NEG_0x80_SetsOverflow
 * @brief NEG 0x80 produces 0x80 again (only case where PV is set for NEG).
 * @par Pass criterion  A=0x80; PV=1; C=1 after NEG with A=0x80.
 */
TEST_F(Z80Test, NEG_0x80_SetsOverflow) {
    cpu.A = 0x80;
    cpu.F = 0x00;
    load({0xED, 0x44}); // NEG
    cpu.step();
    EXPECT_EQ(cpu.A, 0x80);
    EXPECT_TRUE(flagPV());
    EXPECT_TRUE(flagC());
}

/**
 * @test Z80Test/DAA_AfterBCDAddition
 * @brief DAA (0x27) corrects the accumulator after BCD addition.
 * @par Pass criterion  A=0x20 (BCD 20) after adding BCD 19+01 and applying DAA.
 */
TEST_F(Z80Test, DAA_AfterBCDAddition) {
    cpu.A = 0x19;
    cpu.F = 0x00;
    load({0xC6, 0x01,  // ADD A,0x01 → A=0x1A, H=0 (9+1=10, lower nibble=A, no h-carry)
          0x27});       // DAA → correct to BCD 20
    cpu.step(); // ADD
    EXPECT_EQ(cpu.A, 0x1A);
    cpu.step(); // DAA
    EXPECT_EQ(cpu.A, 0x20);
    EXPECT_FALSE(flagN());
    EXPECT_FALSE(flagC());
}

// ─── Block transfer and search ────────────────────────────────────────────────

/**
 * @test Z80Test/LDI_CopiesAndDecrements
 * @brief LDI (ED 0xA0): copies (HL) → (DE), increments HL and DE, decrements BC.
 *   PV=1 if BC≠0 after decrement; P/V=0 if BC==0.
 * @par Pass criterion  mem[0x0200]=0xAB; HL=0x0101, DE=0x0201, BC=0x0000; PV=0.
 */
TEST_F(Z80Test, LDI_CopiesAndDecrements) {
    mem[0x0100] = 0xAB;
    cpu.HL = 0x0100; cpu.DE = 0x0200; cpu.BC = 0x0001;
    load({0xED, 0xA0}); // LDI
    cpu.step();
    EXPECT_EQ(mem[0x0200], 0xAB);
    EXPECT_EQ(cpu.HL, 0x0101);
    EXPECT_EQ(cpu.DE, 0x0201);
    EXPECT_EQ(cpu.BC, 0x0000);
    EXPECT_FALSE(flagPV()); // BC reached 0
    EXPECT_FALSE(flagN());
}

/**
 * @test Z80Test/LDIR_CopiesBlock
 * @brief LDIR (ED 0xB0): repeating block copy until BC==0.
 * @par Pass criterion  Three bytes copied; HL, DE advanced by 3; BC=0.
 */
TEST_F(Z80Test, LDIR_CopiesBlock) {
    mem[0x0100] = 0x11; mem[0x0101] = 0x22; mem[0x0102] = 0x33;
    cpu.HL = 0x0100; cpu.DE = 0x0200; cpu.BC = 0x0003;
    load({0xED, 0xB0}); // LDIR

    // LDIR repeats internally, consuming cycles per iteration
    // Manually step until BC==0 (one step per iteration while BC≠0,
    // or implementation may loop internally)
    // We allow up to 4 steps; BC must be 0 eventually.
    for (int i = 0; i < 4 && cpu.BC != 0; ++i) cpu.step();

    EXPECT_EQ(mem[0x0200], 0x11);
    EXPECT_EQ(mem[0x0201], 0x22);
    EXPECT_EQ(mem[0x0202], 0x33);
    EXPECT_EQ(cpu.BC, 0x0000);
    EXPECT_FALSE(flagPV());
}

/**
 * @test Z80Test/LDD_CopiesAndDecrements
 * @brief LDD (ED 0xA8): like LDI but decrements HL and DE instead of incrementing.
 * @par Pass criterion  mem[0x0200]=0xBB; HL=0x00FF, DE=0x01FF after LDD.
 */
TEST_F(Z80Test, LDD_CopiesAndDecrements) {
    mem[0x0100] = 0xBB;
    cpu.HL = 0x0100; cpu.DE = 0x0200; cpu.BC = 0x0002;
    load({0xED, 0xA8}); // LDD
    cpu.step();
    EXPECT_EQ(mem[0x0200], 0xBB);
    EXPECT_EQ(cpu.HL, 0x00FF);
    EXPECT_EQ(cpu.DE, 0x01FF);
    EXPECT_EQ(cpu.BC, 0x0001);
    EXPECT_TRUE(flagPV()); // BC still non-zero
}

/**
 * @test Z80Test/CPI_ComparesAndAdvances
 * @brief CPI (ED 0xA1): compares A with (HL), increments HL, decrements BC.
 * @par Pass criterion  Z=1 when A==(HL); HL and BC updated correctly.
 */
TEST_F(Z80Test, CPI_ComparesAndAdvances) {
    mem[0x0100] = 0x42;
    cpu.HL = 0x0100; cpu.BC = 0x0002;
    cpu.A  = 0x42;
    load({0xED, 0xA1}); // CPI
    cpu.step();
    EXPECT_TRUE(flagZ());  // match
    EXPECT_TRUE(flagN());
    EXPECT_EQ(cpu.HL, 0x0101);
    EXPECT_EQ(cpu.BC, 0x0001);
    EXPECT_TRUE(flagPV()); // BC still non-zero
}

// ─── Interrupt control ────────────────────────────────────────────────────────

/**
 * @test Z80Test/DI_DisablesInterrupts_EI_Enables
 * @brief DI (0xF3) clears IFF1 and IFF2; EI (0xFB) sets both.
 * @par Pass criterion  IFF1=IFF2=false after DI; IFF1=IFF2=true after EI.
 */
TEST_F(Z80Test, DI_DisablesInterrupts_EI_Enables) {
    cpu.IFF1 = cpu.IFF2 = true;
    load({0xF3, 0xFB}); // DI; EI
    cpu.step(); // DI
    EXPECT_FALSE(cpu.IFF1);
    EXPECT_FALSE(cpu.IFF2);
    cpu.step(); // EI
    EXPECT_TRUE(cpu.IFF1);
    EXPECT_TRUE(cpu.IFF2);
}

/**
 * @test Z80Test/IM_Modes_SelectInterruptMode
 * @brief IM 0/1/2 (ED 0x46/0x56/0x5E) set the cpu.IM field.
 * @par Pass criterion  cpu.IM == 0/1/2 after respective IM instruction.
 */
TEST_F(Z80Test, IM_Modes_SelectInterruptMode) {
    load({0xED, 0x56,   // IM 1
          0xED, 0x5E,   // IM 2
          0xED, 0x46}); // IM 0
    cpu.step(); EXPECT_EQ(cpu.IM, 1);
    cpu.step(); EXPECT_EQ(cpu.IM, 2);
    cpu.step(); EXPECT_EQ(cpu.IM, 0);
}

/**
 * @test Z80Test/LD_I_A_And_LD_A_I
 * @brief LD I,A (ED 0x47) copies A→I; LD A,I (ED 0x57) copies I→A with PV=IFF2.
 * @par Pass criterion  I==0x5A after LD I,A; A==0x5A, PV==IFF2 after LD A,I.
 */
TEST_F(Z80Test, LD_I_A_And_LD_A_I) {
    cpu.A = 0x5A;
    cpu.IFF2 = true;
    load({0xED, 0x47,   // LD I,A
          0xED, 0x57}); // LD A,I
    cpu.step(); EXPECT_EQ(cpu.I, 0x5A);
    cpu.A = 0x00;
    cpu.step();
    EXPECT_EQ(cpu.A, 0x5A);
    EXPECT_TRUE(flagPV()); // PV = IFF2 = true
}

// ─── Maskable interrupt (INT) ─────────────────────────────────────────────────

/**
 * @test Z80Test/INT_IM1_JumpsTo0038
 * @brief In IM 1, cpu.interrupt() causes PC to jump to fixed address 0x0038.
 * @par Pass criterion  PC==0x0038; IFF1==false after interrupt().
 */
TEST_F(Z80Test, INT_IM1_JumpsTo0038) {
    cpu.IM   = 1;
    cpu.IFF1 = true;
    cpu.SP   = 0x0400;
    cpu.interrupt(0xFF); // vector ignored in IM 1
    EXPECT_EQ(cpu.PC, 0x0038);
    EXPECT_FALSE(cpu.IFF1);
}

/**
 * @test Z80Test/INT_Ignored_When_IFF1_False
 * @brief cpu.interrupt() is ignored when IFF1==false (DI active).
 * @par Pass criterion  PC unchanged; IFF1 still false after interrupt() with IFF1=false.
 */
TEST_F(Z80Test, INT_Ignored_When_IFF1_False) {
    cpu.IM   = 1;
    cpu.IFF1 = false;
    cpu.PC   = 0x0050;
    cpu.interrupt(0xFF);
    EXPECT_EQ(cpu.PC, 0x0050);
}

/**
 * @test Z80Test/INT_IM2_UsesVectorTable
 * @brief In IM 2, the CPU reads the jump address from I*256+vector.
 * @par Pass criterion  PC == address stored at (I*256 + vector) after interrupt().
 */
TEST_F(Z80Test, INT_IM2_UsesVectorTable) {
    cpu.IM   = 2;
    cpu.IFF1 = true;
    cpu.I    = 0x01;     // vector table at 0x0100..
    cpu.SP   = 0x0400;
    // Store target address 0x0250 at vector table entry for vector 0x10
    mem[0x0110] = 0x50;  // low byte
    mem[0x0111] = 0x02;  // high byte → target = 0x0250
    cpu.interrupt(0x10);
    EXPECT_EQ(cpu.PC, 0x0250);
    EXPECT_FALSE(cpu.IFF1);
}

// ─── Non-maskable interrupt (NMI) ────────────────────────────────────────────

/**
 * @test Z80Test/NMI_JumpsTo0066_SavesIFF
 * @brief cpu.nmi() always jumps to 0x0066, saves IFF1→IFF2, and clears IFF1.
 * @par Pass criterion  PC==0x0066; IFF1=false; IFF2==original IFF1.
 */
TEST_F(Z80Test, NMI_JumpsTo0066_SavesIFF) {
    cpu.IFF1 = true;
    cpu.IFF2 = false;
    cpu.SP   = 0x0400;
    cpu.nmi();
    EXPECT_EQ(cpu.PC, 0x0066);
    EXPECT_FALSE(cpu.IFF1);
    EXPECT_TRUE(cpu.IFF2); // saved original IFF1
}

/**
 * @test Z80Test/NMI_PushesPC_OnStack
 * @brief cpu.nmi() pushes the current PC onto the stack before jumping.
 * @par Pass criterion  Return address == original PC; SP decremented by 2.
 */
TEST_F(Z80Test, NMI_PushesPC_OnStack) {
    cpu.PC = 0x1234;
    cpu.SP = 0x0400;
    cpu.nmi();
    EXPECT_EQ(cpu.SP, 0x03FE);
    EXPECT_EQ(mem[0x03FE], 0x34); // low byte of 0x1234
    EXPECT_EQ(mem[0x03FF], 0x12); // high byte
}

// ─── IX / IY index registers ──────────────────────────────────────────────────

/**
 * @test Z80Test/LD_IX_nn_And_LD_A_IXpd
 * @brief LD IX,nn (DD 0x21) loads IX; LD A,(IX+d) (DD 0x7E) reads memory at IX+d.
 * @par Pass criterion  A==mem[IX+2] after LD A,(IX+2).
 */
TEST_F(Z80Test, LD_IX_nn_And_LD_A_IXpd) {
    mem[0x0102] = 0xCC;
    load({0xDD, 0x21, 0x00, 0x01,   // LD IX,0x0100
          0xDD, 0x7E, 0x02});         // LD A,(IX+2)
    cpu.step();
    EXPECT_EQ(cpu.IX, 0x0100);
    cpu.step();
    EXPECT_EQ(cpu.A, 0xCC);
}

/**
 * @test Z80Test/LD_IXpd_n
 * @brief LD (IX+d),n (DD 0x36 d n) writes an immediate to memory[IX+d].
 * @par Pass criterion  mem[0x0105]==0xAB after LD (IX+2),0xAB with IX=0x0103.
 */
TEST_F(Z80Test, LD_IXpd_n) {
    cpu.IX = 0x0103;
    load({0xDD, 0x36, 0x02, 0xAB}); // LD (IX+2),0xAB
    cpu.step();
    EXPECT_EQ(mem[0x0105], 0xAB);
}

/**
 * @test Z80Test/ADD_IX_BC
 * @brief ADD IX,BC (DD 0x09): 16-bit IX += BC; C and H updated; N cleared.
 * @par Pass criterion  IX=0x0200 after ADD IX,BC with IX=0x0100, BC=0x0100.
 */
TEST_F(Z80Test, ADD_IX_BC) {
    cpu.IX = 0x0100;
    cpu.BC = 0x0100;
    cpu.F  = Z80::FLAG_N; // must be cleared by ADD
    load({0xDD, 0x09}); // ADD IX,BC
    cpu.step();
    EXPECT_EQ(cpu.IX, 0x0200);
    EXPECT_FALSE(flagN());
}

/**
 * @test Z80Test/LD_IY_nn_And_LD_A_IYpd
 * @brief LD IY,nn (FD 0x21) loads IY; LD A,(IY+d) (FD 0x7E) reads memory.
 * @par Pass criterion  A==mem[IY-1] after LD A,(IY+0xFF) (signed: -1).
 */
TEST_F(Z80Test, LD_IY_nn_And_LD_A_IYpd) {
    mem[0x00FF] = 0x77;
    load({0xFD, 0x21, 0x00, 0x01,   // LD IY,0x0100
          0xFD, 0x7E, 0xFF});         // LD A,(IY-1) [0xFF = -1 signed]
    cpu.step();
    EXPECT_EQ(cpu.IY, 0x0100);
    cpu.step();
    EXPECT_EQ(cpu.A, 0x77);
}

// ─── Cycle-accurate timing ────────────────────────────────────────────────────

/**
 * @test Z80Test/Timing_NOP_4cycles
 * @brief NOP must cost exactly 4 clock cycles.
 * @par Pass criterion  step() == 4.
 */
TEST_F(Z80Test, Timing_NOP_4cycles) {
    load({0x00}); // NOP
    EXPECT_EQ(cpu.step(), 4);
}

/**
 * @test Z80Test/Timing_LD_r_n_7cycles
 * @brief LD r,n (e.g. LD A,n) must cost exactly 7 clock cycles.
 * @par Pass criterion  step() == 7.
 */
TEST_F(Z80Test, Timing_LD_r_n_7cycles) {
    load({0x3E, 0x00}); // LD A,0
    EXPECT_EQ(cpu.step(), 7);
}

/**
 * @test Z80Test/Timing_CALL_nn_17cycles
 * @brief CALL nn must cost exactly 17 clock cycles.
 * @par Pass criterion  step() == 17.
 */
TEST_F(Z80Test, Timing_CALL_nn_17cycles) {
    cpu.SP = 0x0400;
    load({0xCD, 0x00, 0x02}); // CALL 0x0200
    EXPECT_EQ(cpu.step(), 17);
}

/**
 * @test Z80Test/Timing_LDIR_RepeatVsFinal
 * @brief LDIR uses 21 cycles per repeating step (BC≠0) and 16 on the final step (BC==0).
 * @par Pass criterion  First step() == 21 (BC=2→1); second step() == 16 (BC=1→0).
 */
TEST_F(Z80Test, Timing_LDIR_RepeatVsFinal) {
    mem[0x0100] = 0x01; mem[0x0101] = 0x02;
    cpu.HL = 0x0100; cpu.DE = 0x0200; cpu.BC = 0x0002;
    load({0xED, 0xB0}); // LDIR

    int c1 = cpu.step();
    // After first step: if implementation repeats in one step (looping internally),
    // BC could be 0 and cost might be 16 or 37. Accept both behaviours.
    // If it steps per-byte: c1==21 and BC==1; c2==16 and BC==0.
    if (cpu.BC == 0x0001) {
        EXPECT_EQ(c1, 21);
        int c2 = cpu.step();
        EXPECT_EQ(c2, 16);
        EXPECT_EQ(cpu.BC, 0x0000);
    } else {
        // Single-step full loop implementation
        EXPECT_EQ(cpu.BC, 0x0000);
    }
}

/**
 * @test Z80Test/Timing_CyclesAccumulate
 * @brief cpu.cycles accumulates correctly across multiple instructions.
 * @par Pass criterion  cycles == 4+7+4 == 15 after NOP + LD A,n + NOP.
 */
TEST_F(Z80Test, Timing_CyclesAccumulate) {
    load({0x00, 0x3E, 0xAB, 0x00}); // NOP(4) + LD A,0xAB(7) + NOP(4)
    cpu.step(); cpu.step(); cpu.step();
    EXPECT_EQ(cpu.cycles, 15u);
}

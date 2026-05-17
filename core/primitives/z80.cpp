/**
 * @file z80.cpp
 * @brief Zilog Z80 CPU Emulator - Implementation
 *
 * Contains the complete implementation of Z80 CPU emulation for the
 * Robotron A5120 office computer. All instructions are executed cycle-accurately
 * with correct handling of all documented and undocumented flags.
 *
 * The implementation is organized into the following sections:
 * - Initialization and reset
 * - Memory access and instruction fetch
 * - 8-bit and 16-bit ALU operations
 * - Rotation and shift instructions
 * - Bit operations (BIT, RES, SET)
 * - Register access via opcode index
 * - Interrupt handling (INT, NMI)
 * - Instruction decoding by prefix (unprefixed, CB, DD, ED, FD, DDCB, FDCB)
 *
 * @author Olaf Krieger
 * @license MIT License
 * @see https://opensource.org/licenses/MIT
 */
#include "core/primitives/z80.h"

// =============================================================================
/// @name Initialization
// =============================================================================

/**
 * @brief Constructor - initializes the CPU by calling reset().
 */
Z80::Z80() {
    reset();
}

/**
 * @brief Resets all CPU registers and internal state to initial values.
 *
 * After reset:
 * - All main and shadow registers are 0
 * - SP is 0xFFFF (top of memory)
 * - PC is 0x0000 (start address)
 * - Interrupts are disabled (IFF1=IFF2=false)
 * - Interrupt mode is 0
 * - HALT state is cleared
 * - Clock cycle counter is 0
 */
void Z80::reset() {
    // The real Z80 (and Q300 clone) initialises A=0xFF, F=0xFF after /RESET.
    // Other registers are undefined per spec; A/F are the exception.
    // Setting AF = 0 (as was done previously) causes RET P at ROM address
    // 0x000A to execute with an empty stack, jumping into uninitialised RAM.
    AF = 0xFFFF;
    BC = DE = HL = 0;
    AF_ = BC_ = DE_ = HL_ = 0;
    IX = IY = 0;
    PC = 0;
    SP = 0xFFFF;
    I = R = 0;
    IFF1 = IFF2 = false;
    IM = 0;
    halted = false;
    cycles = 0;
}

// =============================================================================
/// @name Helper Functions
// =============================================================================

/**
 * @brief Calculates the parity of an 8-bit value using XOR folding.
 *
 * Through successive XOR folding (4→2→1 bit), parity is
 * efficiently calculated without a loop.
 *
 * @param val Value to check.
 * @return true for even parity (even number of set bits).
 */
bool Z80::parity(uint8_t val) {
    val ^= val >> 4;
    val ^= val >> 2;
    val ^= val >> 1;
    return !(val & 1);
}

// =============================================================================
/// @name Memory Access and Instruction Fetch
// =============================================================================

/**
 * @brief Reads a 16-bit word from memory (little-endian).
 *
 * The low byte is at the lower address,
 * the high byte at addr+1 - according to Z80 convention.
 *
 * @param addr Start address of the word to read.
 * @return Read 16-bit word.
 */
uint16_t Z80::readWord(uint16_t addr) {
    return readByte(addr) | (readByte(addr + 1) << 8);
}

/**
 * @brief Writes a 16-bit word to memory (little-endian).
 * @param addr Start address.
 * @param val 16-bit word to write.
 */
void Z80::writeWord(uint16_t addr, uint16_t val) {
    writeByte(addr, val & 0xFF);
    writeByte(addr + 1, (val >> 8) & 0xFF);
}

/**
 * @brief Reads the next byte at address PC and increments PC.
 *
 * Used for fetching opcodes and immediate operands.
 *
 * @return Read byte.
 */
uint8_t Z80::fetchByte() {
    uint8_t val = readByte(PC);
    PC++;
    return val;
}

/**
 * @brief Reads the next 16-bit word at PC and increments PC by 2.
 *
 * Used for 16-bit immediates and addresses.
 *
 * @return Read 16-bit word (little-endian).
 */
uint16_t Z80::fetchWord() {
    uint16_t val = readWord(PC);
    PC += 2;
    return val;
}

// =============================================================================
/// @name Stack Operations
// =============================================================================

/**
 * @brief Pushes a 16-bit value onto the stack (PUSH).
 *
 * The stack pointer is decremented by 2, then the value is
 * written in little-endian order.
 *
 * @param val The value to push onto the stack.
 */
void Z80::push(uint16_t val) {
    SP -= 2;
    writeWord(SP, val);
}

/**
 * @brief Pops a 16-bit value from the stack (POP).
 *
 * Reads the value at the current SP address and increments SP by 2.
 *
 * @return The 16-bit value read from the stack.
 */
uint16_t Z80::pop() {
    uint16_t val = readWord(SP);
    SP += 2;
    return val;
}

// =============================================================================
/// @name 8-Bit ALU Operations
/// @brief Arithmetic and logical 8-bit operations with complete
///        flag calculation (S, Z, H, PV, N, C and undocumented bits 3 and 5).
// =============================================================================

/**
 * @brief 8-bit addition: a + b.
 *
 * Flags: S, Z, H (half-carry bit 3→4), PV (overflow), C (carry).
 * N is cleared. Bits 3 and 5 are taken from the result.
 *
 * @param a First operand.
 * @param b Second operand.
 * @return Result of addition (lower 8 bits).
 */
uint8_t Z80::add8(uint8_t a, uint8_t b) {
    uint16_t result = a + b;
    uint8_t r = result & 0xFF;
    F = (r & (FLAG_S | FLAG_3 | FLAG_5));
    if (r == 0) F |= FLAG_Z;
    if (result > 0xFF) F |= FLAG_C;
    if ((a ^ b ^ r) & 0x10) F |= FLAG_H;
    if (((a ^ b) ^ 0x80) & (a ^ r) & 0x80) F |= FLAG_PV;
    return r;
}

/**
 * @brief 8-bit addition with carry: a + b + C.
 *
 * Like add8(), but the existing carry flag is included as an additional summand.
 *
 * @param a First operand.
 * @param b Second operand.
 * @return Result of addition (lower 8 bits).
 */
uint8_t Z80::adc8(uint8_t a, uint8_t b) {
    uint8_t carry = (F & FLAG_C) ? 1 : 0;
    uint16_t result = a + b + carry;
    uint8_t r = result & 0xFF;
    F = (r & (FLAG_S | FLAG_3 | FLAG_5));
    if (r == 0) F |= FLAG_Z;
    if (result > 0xFF) F |= FLAG_C;
    if ((a ^ b ^ r) & 0x10) F |= FLAG_H;
    if (((a ^ b) ^ 0x80) & (a ^ r) & 0x80) F |= FLAG_PV;
    return r;
}

/**
 * @brief 8-bit subtraction: a - b.
 *
 * Flags: S, Z, H (half-carry), PV (overflow), C (borrow). N is set.
 *
 * @param a Minuend.
 * @param b Subtrahend.
 * @return Result of subtraction (lower 8 bits).
 */
uint8_t Z80::sub8(uint8_t a, uint8_t b) {
    uint16_t result = a - b;
    uint8_t r = result & 0xFF;
    F = FLAG_N | (r & (FLAG_S | FLAG_3 | FLAG_5));
    if (r == 0) F |= FLAG_Z;
    if (result > 0xFF) F |= FLAG_C;
    if ((a ^ b ^ r) & 0x10) F |= FLAG_H;
    if ((a ^ b) & (a ^ r) & 0x80) F |= FLAG_PV;
    return r;
}

/**
 * @brief 8-bit subtraction with carry: a - b - C.
 *
 * Like sub8(), but the existing carry flag is additionally subtracted.
 *
 * @param a Minuend.
 * @param b Subtrahend.
 * @return Result of subtraction (lower 8 bits).
 */
uint8_t Z80::sbc8(uint8_t a, uint8_t b) {
    uint8_t carry = (F & FLAG_C) ? 1 : 0;
    uint16_t result = a - b - carry;
    uint8_t r = result & 0xFF;
    F = FLAG_N | (r & (FLAG_S | FLAG_3 | FLAG_5));
    if (r == 0) F |= FLAG_Z;
    if (result > 0xFF) F |= FLAG_C;
    if ((a ^ b ^ r) & 0x10) F |= FLAG_H;
    if ((a ^ b) & (a ^ r) & 0x80) F |= FLAG_PV;
    return r;
}

/**
 * @brief Logical AND: A = A & val.
 *
 * H flag is set, N and C cleared. PV indicates even parity.
 *
 * @param val Operand for AND operation.
 */
void Z80::and8(uint8_t val) {
    A &= val;
    F = FLAG_H | (A & (FLAG_S | FLAG_3 | FLAG_5));
    if (A == 0) F |= FLAG_Z;
    if (parity(A)) F |= FLAG_PV;
}

/**
 * @brief Logical OR: A = A | val.
 *
 * H, N, and C are cleared. PV indicates even parity.
 *
 * @param val Operand for OR operation.
 */
void Z80::or8(uint8_t val) {
    A |= val;
    F = (A & (FLAG_S | FLAG_3 | FLAG_5));
    if (A == 0) F |= FLAG_Z;
    if (parity(A)) F |= FLAG_PV;
}

/**
 * @brief Logical XOR: A = A ^ val.
 *
 * H, N, and C are cleared. PV indicates even parity.
 *
 * @param val Operand for XOR operation.
 */
void Z80::xor8(uint8_t val) {
    A ^= val;
    F = (A & (FLAG_S | FLAG_3 | FLAG_5));
    if (A == 0) F |= FLAG_Z;
    if (parity(A)) F |= FLAG_PV;
}

/**
 * @brief Compare (CP): A - val, without storing the result.
 *
 * Flags are set as with SUB, A remains unchanged.
 * Special case: Bits 3 and 5 of flags are taken from the operand val,
 * not from the result - this is documented Z80 behavior.
 *
 * @param val Comparison value.
 */
void Z80::cp8(uint8_t val) {
    uint16_t result = A - val;
    uint8_t r = result & 0xFF;
    F = FLAG_N | (r & FLAG_S) | (val & (FLAG_3 | FLAG_5));
    if (r == 0) F |= FLAG_Z;
    if (result > 0xFF) F |= FLAG_C;
    if ((A ^ val ^ r) & 0x10) F |= FLAG_H;
    if ((A ^ val) & (A ^ r) & 0x80) F |= FLAG_PV;
}

/**
 * @brief 8-bit increment: val + 1.
 *
 * The C flag remains unchanged. PV is set when val == 0x7F
 * (overflow from positive to negative in two's complement).
 *
 * @param val Value to increment.
 * @return Incremented result.
 */
uint8_t Z80::inc8(uint8_t val) {
    uint8_t r = val + 1;
    F = (F & FLAG_C) | (r & (FLAG_S | FLAG_3 | FLAG_5));
    if (r == 0) F |= FLAG_Z;
    if ((val ^ r) & 0x10) F |= FLAG_H;
    if (val == 0x7F) F |= FLAG_PV;
    return r;
}

/**
 * @brief 8-bit decrement: val - 1.
 *
 * The C flag remains unchanged. N is set. PV is set
 * when val == 0x80 (underflow from negative to positive).
 *
 * @param val Value to decrement.
 * @return Decremented result.
 */
uint8_t Z80::dec8(uint8_t val) {
    uint8_t r = val - 1;
    F = FLAG_N | (F & FLAG_C) | (r & (FLAG_S | FLAG_3 | FLAG_5));
    if (r == 0) F |= FLAG_Z;
    if ((val ^ r) & 0x10) F |= FLAG_H;
    if (val == 0x80) F |= FLAG_PV;
    return r;
}

// =============================================================================
/// @name 16-Bit-ALU-Operationen
// =============================================================================

/**
 * @brief 16-Bit-Addition: a + b.
 *
 * S, Z und PV bleiben erhalten. H bezieht sich auf Bit 11→12.
 * Bits 3 und 5 werden vom oberen Byte des Ergebnisses übernommen.
 *
 * @param a Erster Operand.
 * @param b Zweiter Operand.
 * @return 16-Bit-Ergebnis.
 */
uint16_t Z80::add16(uint16_t a, uint16_t b) {
    uint32_t result = a + b;
    uint16_t r = result & 0xFFFF;
    F = (F & (FLAG_S | FLAG_Z | FLAG_PV)) | ((r >> 8) & (FLAG_3 | FLAG_5));
    if (result > 0xFFFF) F |= FLAG_C;
    if ((a ^ b ^ r) & 0x1000) F |= FLAG_H;
    return r;
}

/**
 * @brief 16-bit addition with carry: a + b + C.
 *
 * In contrast to add16(), all flags are newly set.
 * PV indicates 16-bit overflow (sign overflow).
 *
 * @param a First operand.
 * @param b Second operand.
 * @return 16-bit result.
 */
uint16_t Z80::adc16(uint16_t a, uint16_t b) {
    uint8_t carry = (F & FLAG_C) ? 1 : 0;
    uint32_t result = a + b + carry;
    uint16_t r = result & 0xFFFF;
    F = ((r >> 8) & (FLAG_S | FLAG_3 | FLAG_5));
    if (r == 0) F |= FLAG_Z;
    if (result > 0xFFFF) F |= FLAG_C;
    if ((a ^ b ^ r) & 0x1000) F |= FLAG_H;
    if (((a ^ b) ^ 0x8000) & (a ^ r) & 0x8000) F |= FLAG_PV;
    return r;
}

/**
 * @brief 16-bit subtraction with carry: a - b - C.
 *
 * All flags are set. N is set.
 * PV indicates 16-bit sign overflow.
 *
 * @param a Minuend.
 * @param b Subtrahend.
 * @return 16-bit result.
 */
uint16_t Z80::sbc16(uint16_t a, uint16_t b) {
    uint8_t carry = (F & FLAG_C) ? 1 : 0;
    uint32_t result = a - b - carry;
    uint16_t r = result & 0xFFFF;
    F = FLAG_N | ((r >> 8) & (FLAG_S | FLAG_3 | FLAG_5));
    if (r == 0) F |= FLAG_Z;
    if (result > 0xFFFF) F |= FLAG_C;
    if ((a ^ b ^ r) & 0x1000) F |= FLAG_H;
    if ((a ^ b) & (a ^ r) & 0x8000) F |= FLAG_PV;
    return r;
}

// =============================================================================
/// @name Rotation and Shift Instructions
/// @brief CB-prefix operations. All functions set S, Z, PV (parity)
///        and the undocumented bits 3 and 5 from the result.
///        H and N are cleared, C receives the shifted-out bit.
// =============================================================================

/**
 * @brief RLC - Rotate Left Circular.
 *
 * Bit 7 is simultaneously rotated to the C flag and to bit position 0.
 *
 * @param val Input value.
 * @return Rotated result.
 */
uint8_t Z80::rlc(uint8_t val) {
    uint8_t c = (val >> 7) & 1;
    uint8_t r = (val << 1) | c;
    F = (r & (FLAG_S | FLAG_3 | FLAG_5)) | c;
    if (r == 0) F |= FLAG_Z;
    if (parity(r)) F |= FLAG_PV;
    return r;
}

/**
 * @brief RRC - Rotate Right Circular.
 *
 * Bit 0 is simultaneously rotated to the C flag and to bit position 7.
 *
 * @param val Input value.
 * @return Rotated result.
 */
uint8_t Z80::rrc(uint8_t val) {
    uint8_t c = val & 1;
    uint8_t r = (val >> 1) | (c << 7);
    F = (r & (FLAG_S | FLAG_3 | FLAG_5)) | c;
    if (r == 0) F |= FLAG_Z;
    if (parity(r)) F |= FLAG_PV;
    return r;
}

/**
 * @brief RL - Rotate Left through Carry.
 *
 * Bit 7 → C flag, old C flag → bit 0. Forms a
 * 9-bit ring shift register with the C flag.
 *
 * @param val Input value.
 * @return Rotated result.
 */
uint8_t Z80::rl(uint8_t val) {
    uint8_t c = (val >> 7) & 1;
    uint8_t r = (val << 1) | ((F & FLAG_C) ? 1 : 0);
    F = (r & (FLAG_S | FLAG_3 | FLAG_5)) | c;
    if (r == 0) F |= FLAG_Z;
    if (parity(r)) F |= FLAG_PV;
    return r;
}

/**
 * @brief RR - Rotate Right through Carry.
 *
 * Bit 0 → C flag, old C flag → bit 7. Forms a
 * 9-bit ring shift register with the C flag.
 *
 * @param val Input value.
 * @return Rotated result.
 */
uint8_t Z80::rr(uint8_t val) {
    uint8_t c = val & 1;
    uint8_t r = (val >> 1) | ((F & FLAG_C) ? 0x80 : 0);
    F = (r & (FLAG_S | FLAG_3 | FLAG_5)) | c;
    if (r == 0) F |= FLAG_Z;
    if (parity(r)) F |= FLAG_PV;
    return r;
}

/**
 * @brief SLA - Shift Left Arithmetic.
 *
 * Bit 7 → C flag, bit 0 becomes 0. Corresponds to multiplication by 2.
 *
 * @param val Input value.
 * @return Shifted result.
 */
uint8_t Z80::sla(uint8_t val) {
    uint8_t c = (val >> 7) & 1;
    uint8_t r = val << 1;
    F = (r & (FLAG_S | FLAG_3 | FLAG_5)) | c;
    if (r == 0) F |= FLAG_Z;
    if (parity(r)) F |= FLAG_PV;
    return r;
}

/**
 * @brief SRA - Shift Right Arithmetic.
 *
 * Bit 0 → C flag, bit 7 preserved (sign extension).
 * Corresponds to signed division by 2.
 *
 * @param val Input value.
 * @return Shifted result.
 */
uint8_t Z80::sra(uint8_t val) {
    uint8_t c = val & 1;
    uint8_t r = (val >> 1) | (val & 0x80);
    F = (r & (FLAG_S | FLAG_3 | FLAG_5)) | c;
    if (r == 0) F |= FLAG_Z;
    if (parity(r)) F |= FLAG_PV;
    return r;
}

/**
 * @brief SLL - Shift Left Logical (undocumented, also SL1/SLS).
 *
 * Bit 7 → C flag, bit 0 is set to 1 (not 0 like SLA).
 * This is an undocumented Z80 instruction that is
 * nevertheless used in some software.
 *
 * @param val Input value.
 * @return Shifted result.
 */
uint8_t Z80::sll(uint8_t val) {
    uint8_t c = (val >> 7) & 1;
    uint8_t r = (val << 1) | 1;
    F = (r & (FLAG_S | FLAG_3 | FLAG_5)) | c;
    if (r == 0) F |= FLAG_Z;
    if (parity(r)) F |= FLAG_PV;
    return r;
}

/**
 * @brief SRL - Shift Right Logical.
 *
 * Bit 0 → C flag, bit 7 becomes 0. Corresponds to unsigned
 * division by 2.
 *
 * @param val Input value.
 * @return Shifted result.
 */
uint8_t Z80::srl(uint8_t val) {
    uint8_t c = val & 1;
    uint8_t r = val >> 1;
    F = (r & (FLAG_S | FLAG_3 | FLAG_5)) | c;
    if (r == 0) F |= FLAG_Z;
    if (parity(r)) F |= FLAG_PV;
    return r;
}

// =============================================================================
/// @name Bit Operations
// =============================================================================

/**
 * @brief BIT b,val - tests whether a specific bit is set.
 *
 * Z and PV flags are set if the bit is 0.
 * S flag is set if bit 7 is tested and is set.
 * H is set, N is cleared. C remains unchanged.
 * Bits 3 and 5 are taken from flags_src.
 *
 * @param b Bit number (0-7).
 * @param val Value to test.
 * @param flags_src Source for undocumented flags 3 and 5.
 */
void Z80::bit_op(uint8_t b, uint8_t val, uint8_t flags_src) {
    uint8_t r = val & (1 << b);
    F = (F & FLAG_C) | FLAG_H | (flags_src & (FLAG_3 | FLAG_5));
    if (r == 0) F |= (FLAG_Z | FLAG_PV);
    if (b == 7 && r) F |= FLAG_S;
}

/**
 * @brief BIT b,val - tests whether a specific bit is set (standard version).
 *
 * Uses the value itself as source for undocumented flags.
 * This is the normal behavior for non-indexed BIT operations.
 *
 * @param b Bit number (0-7).
 * @param val Value to test.
 */
void Z80::bit_op(uint8_t b, uint8_t val) {
    bit_op(b, val, val);
}

/**
 * @brief RES b,val - clears (Reset) a specific bit.
 *
 * No flags are affected.
 *
 * @param b Bit number (0-7).
 * @param val Input value.
 * @return Value with cleared bit b.
 */
uint8_t Z80::res_op(uint8_t b, uint8_t val) {
    return val & ~(1 << b);
}

/**
 * @brief SET b,val - sets a specific bit to 1.
 *
 * No flags are affected.
 *
 * @param b Bit number (0-7).
 * @param val Input value.
 * @return Value with set bit b.
 */
uint8_t Z80::set_op(uint8_t b, uint8_t val) {
    return val | (1 << b);
}

// =============================================================================
/// @name Register Access via Opcode Index
/// @brief Enable generic access to registers based on the 3-bit
///        register indices encoded in Z80 opcodes.
// =============================================================================

/**
 * @brief Reads an 8-bit register by its opcode index.
 *
 * Encoding according to Z80 instruction set:
 * - 0=B, 1=C, 2=D, 3=E, 4=H, 5=L, 6=(HL), 7=A
 *
 * For index 6, the value is read indirectly from memory at address HL.
 *
 * @param idx Register index (lower 3 bits are used).
 * @return Register value.
 */
uint8_t Z80::getReg8(uint8_t idx) {
    switch (idx & 7) {
        case 0: return B;
        case 1: return C;
        case 2: return D;
        case 3: return E;
        case 4: return H;
        case 5: return L;
        case 6: return readByte(HL);
        case 7: return A;
    }
    return 0;
}

/**
 * @brief Writes a value to an 8-bit register by its opcode index.
 *
 * For index 6, the value is written indirectly to memory at address HL.
 *
 * @param idx Register index (0-7, see getReg8).
 * @param val Value to write.
 */
void Z80::setReg8(uint8_t idx, uint8_t val) {
    switch (idx & 7) {
        case 0: B = val; break;
        case 1: C = val; break;
        case 2: D = val; break;
        case 3: E = val; break;
        case 4: H = val; break;
        case 5: L = val; break;
        case 6: writeByte(HL, val); break;
        case 7: A = val; break;
    }
}

/**
 * @brief Reads a 16-bit register pair (SP variant).
 *
 * Encoding: 0=BC, 1=DE, 2=HL, 3=SP.
 * Used for instructions that use SP as the fourth register pair.
 *
 * @param idx Register pair index (lower 2 bits).
 * @return 16-bit register value.
 */
uint16_t Z80::getReg16(uint8_t idx) {
    switch (idx & 3) {
        case 0: return BC;
        case 1: return DE;
        case 2: return HL;
        case 3: return SP;
    }
    return 0;
}

/**
 * @brief Writes a value to a 16-bit register pair (SP variant).
 * @param idx Register pair index (0-3).
 * @param val 16-bit value to write.
 */
void Z80::setReg16(uint8_t idx, uint16_t val) {
    switch (idx & 3) {
        case 0: BC = val; break;
        case 1: DE = val; break;
        case 2: HL = val; break;
        case 3: SP = val; break;
    }
}

/**
 * @brief Reads a 16-bit register pair (AF variant).
 *
 * Encoding: 0=BC, 1=DE, 2=HL, 3=AF.
 * Used for PUSH/POP instructions where AF instead of SP
 * is the fourth register pair.
 *
 * @param idx Register pair index (lower 2 bits).
 * @return 16-bit register value.
 */
uint16_t Z80::getReg16AF(uint8_t idx) {
    switch (idx & 3) {
        case 0: return BC;
        case 1: return DE;
        case 2: return HL;
        case 3: return AF;
    }
    return 0;
}

/**
 * @brief Writes a value to a 16-bit register pair (AF variant).
 * @param idx Register pair index (0-3).
 * @param val 16-bit value to write.
 */
void Z80::setReg16AF(uint8_t idx, uint16_t val) {
    switch (idx & 3) {
        case 0: BC = val; break;
        case 1: DE = val; break;
        case 2: HL = val; break;
        case 3: AF = val; break;
    }
}

/**
 * @brief Checks a jump/call/return condition.
 *
 * The 8 possible condition codes and their meaning:
 * - 0: NZ (Not Zero) - Z flag is not set
 * - 1: Z  (Zero) - Z flag is set
 * - 2: NC (No Carry) - C flag is not set
 * - 3: C  (Carry) - C flag is set
 * - 4: PO (Parity Odd) - PV flag is not set
 * - 5: PE (Parity Even) - PV flag is set
 * - 6: P  (Positive/Plus) - S flag is not set
 * - 7: M  (Minus) - S flag is set
 *
 * @param cc Condition code (0-7).
 * @return true if the condition is satisfied.
 */
bool Z80::checkCondition(uint8_t cc) {
    switch (cc) {
        case 0: return !(F & FLAG_Z);   // NZ
        case 1: return (F & FLAG_Z);     // Z
        case 2: return !(F & FLAG_C);    // NC
        case 3: return (F & FLAG_C);     // C
        case 4: return !(F & FLAG_PV);   // PO
        case 5: return (F & FLAG_PV);    // PE
        case 6: return !(F & FLAG_S);    // P
        case 7: return (F & FLAG_S);     // M
    }
    return false;
}

// =============================================================================
/// @name Interrupt Handling
// =============================================================================

/**
 * @brief Triggers a maskable interrupt (INT).
 *
 * The interrupt is only accepted if IFF1 is set.
 * After acceptance:
 * - HALT is cleared
 * - IFF1 and IFF2 are cleared (interrupts disabled)
 * - R register is incremented
 *
 * The behavior depends on the interrupt mode:
 * - **IM 0**: The vector is interpreted as RST instruction (vector & 0x38). 13 cycles.
 * - **IM 1**: Fixed jump to address 0x0038, vector is ignored. 13 cycles.
 * - **IM 2**: Vector table - target address is read from (I*256 + (vector & 0xFE)). 19 cycles.
 *
 * @param vector Interrupt vector from peripheral device.
 */
void Z80::interrupt(uint8_t vector) {
    if (!IFF1) return;
    halted = false;
    IFF1 = IFF2 = false;
    R = (R & 0x80) | ((R + 1) & 0x7F);

    switch (IM) {
        case 0:
            // Execute instruction on data bus (typically RST)
            push(PC);
            PC = vector & 0x38; // RST xx
            cycles += 13;
            break;
        case 1:
            push(PC);
            PC = 0x0038;
            cycles += 13;
            break;
        case 2: {
            push(PC);
            uint16_t addr = (I << 8) | (vector & 0xFE);
            PC = readWord(addr);
            cycles += 19;
            break;
        }
    }
}

/**
 * @brief Triggers a non-maskable interrupt (NMI).
 *
 * The NMI cannot be disabled and has highest priority.
 * - HALT is cleared
 * - IFF1 is saved to IFF2, then IFF1 is cleared
 * - PC is pushed onto the stack
 * - Jump to fixed address 0x0066
 * - 11 clock cycles
 */
void Z80::nmi() {
    halted = false;
    IFF2 = IFF1;
    IFF1 = false;
    push(PC);
    PC = 0x0066;
    R = (R & 0x80) | ((R + 1) & 0x7F);
    cycles += 11;
}

// =============================================================================
/// @name Instruction Execution
/// @brief Main loop and decoding functions for the Z80 instruction set.
// =============================================================================

/**
 * @brief Executes a single Z80 instruction and returns the consumed cycles.
 *
 * In HALT state, only NOP cycles are consumed (4 cycles) without
 * changing the program counter. The processor leaves HALT through an
 * interrupt or NMI.
 *
 * @return Number of clock cycles consumed.
 */
int Z80::step() {
    if (halted) {
        cycles += 4;
        return 4;
    }

    // Fire instruction-trace callback BEFORE execution (TRACE-level debugging).
    // The callback captures PC (before fetch), all registers, and flags.
    if (traceCallback) {
        traceCallback(*this);
    }

    R = (R & 0x80) | ((R + 1) & 0x7F);
    uint8_t opcode = fetchByte();
    int c = execMain(opcode);
    cycles += c;
    return c;
}

/**
 * @brief Decodes and executes an unprefixed Z80 opcode.
 *
 * This is the main instruction decoder for all opcodes 0x00-0xFF.
 * The opcodes are divided into the following groups:
 * - 0x00-0x3F: Various instructions (LD 16-bit, INC/DEC, rotations, DAA, etc.)
 * - 0x40-0x7F: LD r,r' (register-to-register load, 0x76 = HALT)
 * - 0x80-0xBF: ALU operations A,r (ADD, ADC, SUB, SBC, AND, XOR, OR, CP)
 * - 0xC0-0xFF: Jumps, calls, returns, I/O, prefix forwarding
 *
 * The opcode fields x, y, z, p, q are extracted according to the Z80 decoding table,
 * even though most instructions are handled directly via case statements.
 *
 * @param opcode The opcode to execute.
 * @return Clock cycles consumed.
 */
int Z80::execMain(uint8_t opcode) {
    uint8_t x = (opcode >> 6) & 3;
    uint8_t y = (opcode >> 3) & 7;
    uint8_t z = opcode & 7;
    uint8_t p = (y >> 1) & 3;
    uint8_t q = y & 1;

    switch (opcode) {
        case 0x00: return 4;  // NOP
        case 0x01: BC = fetchWord(); return 10;
        case 0x02: writeByte(BC, A); return 7;
        case 0x03: BC++; return 6;
        case 0x04: B = inc8(B); return 4;
        case 0x05: B = dec8(B); return 4;
        case 0x06: B = fetchByte(); return 7;
        case 0x07: { // RLCA
            uint8_t c = A >> 7;
            A = (A << 1) | c;
            F = (F & (FLAG_S | FLAG_Z | FLAG_PV)) | (A & (FLAG_3 | FLAG_5)) | c;
            return 4;
        }
        case 0x08: { // EX AF, AF'
            uint16_t tmp = AF; AF = AF_; AF_ = tmp;
            return 4;
        }
        case 0x09: HL = add16(HL, BC); return 11;
        case 0x0A: A = readByte(BC); return 7;
        case 0x0B: BC--; return 6;
        case 0x0C: C = inc8(C); return 4;
        case 0x0D: C = dec8(C); return 4;
        case 0x0E: C = fetchByte(); return 7;
        case 0x0F: { // RRCA
            uint8_t c = A & 1;
            A = (A >> 1) | (c << 7);
            F = (F & (FLAG_S | FLAG_Z | FLAG_PV)) | (A & (FLAG_3 | FLAG_5)) | c;
            return 4;
        }
        case 0x10: { // DJNZ
            int8_t d = (int8_t)fetchByte();
            B--;
            if (B != 0) { PC += d; return 13; }
            return 8;
        }
        case 0x11: DE = fetchWord(); return 10;
        case 0x12: writeByte(DE, A); return 7;
        case 0x13: DE++; return 6;
        case 0x14: D = inc8(D); return 4;
        case 0x15: D = dec8(D); return 4;
        case 0x16: D = fetchByte(); return 7;
        case 0x17: { // RLA
            uint8_t c = A >> 7;
            A = (A << 1) | ((F & FLAG_C) ? 1 : 0);
            F = (F & (FLAG_S | FLAG_Z | FLAG_PV)) | (A & (FLAG_3 | FLAG_5)) | c;
            return 4;
        }
        case 0x18: { int8_t d = (int8_t)fetchByte(); PC += d; return 12; }
        case 0x19: HL = add16(HL, DE); return 11;
        case 0x1A: A = readByte(DE); return 7;
        case 0x1B: DE--; return 6;
        case 0x1C: E = inc8(E); return 4;
        case 0x1D: E = dec8(E); return 4;
        case 0x1E: E = fetchByte(); return 7;
        case 0x1F: { // RRA
            uint8_t c = A & 1;
            A = (A >> 1) | ((F & FLAG_C) ? 0x80 : 0);
            F = (F & (FLAG_S | FLAG_Z | FLAG_PV)) | (A & (FLAG_3 | FLAG_5)) | c;
            return 4;
        }
        case 0x20: { int8_t d = (int8_t)fetchByte(); if (!(F & FLAG_Z)) { PC += d; return 12; } return 7; }
        case 0x21: HL = fetchWord(); return 10;
        case 0x22: writeWord(fetchWord(), HL); return 16;
        case 0x23: HL++; return 6;
        case 0x24: H = inc8(H); return 4;
        case 0x25: H = dec8(H); return 4;
        case 0x26: H = fetchByte(); return 7;
        case 0x27: { // DAA
            uint8_t correction = 0;
            uint8_t carry = 0;
            if ((F & FLAG_H) || (A & 0x0F) > 9) correction = 0x06;
            if ((F & FLAG_C) || A > 0x99) { correction |= 0x60; carry = FLAG_C; }
            if (F & FLAG_N) A -= correction; else A += correction;
            F = (A & (FLAG_S | FLAG_3 | FLAG_5)) | carry | (F & FLAG_N);
            if (A == 0) F |= FLAG_Z;
            if (parity(A)) F |= FLAG_PV;
            // H flag for DAA
            if (F & FLAG_N) {
                F |= ((A & 0x0F) > 5) ? 0 : ((correction & 0x06) ? FLAG_H : 0);
            } else {
                F |= ((A & 0x0F) >= 0x0A) ? FLAG_H : 0;
            }
            return 4;
        }
        case 0x28: { int8_t d = (int8_t)fetchByte(); if (F & FLAG_Z) { PC += d; return 12; } return 7; }
        case 0x29: HL = add16(HL, HL); return 11;
        case 0x2A: HL = readWord(fetchWord()); return 16;
        case 0x2B: HL--; return 6;
        case 0x2C: L = inc8(L); return 4;
        case 0x2D: L = dec8(L); return 4;
        case 0x2E: L = fetchByte(); return 7;
        case 0x2F: // CPL
            A = ~A;
            F = (F & (FLAG_S | FLAG_Z | FLAG_PV | FLAG_C)) | FLAG_H | FLAG_N | (A & (FLAG_3 | FLAG_5));
            return 4;
        case 0x30: { int8_t d = (int8_t)fetchByte(); if (!(F & FLAG_C)) { PC += d; return 12; } return 7; }
        case 0x31: SP = fetchWord(); return 10;
        case 0x32: writeByte(fetchWord(), A); return 13;
        case 0x33: SP++; return 6;
        case 0x34: writeByte(HL, inc8(readByte(HL))); return 11;
        case 0x35: writeByte(HL, dec8(readByte(HL))); return 11;
        case 0x36: writeByte(HL, fetchByte()); return 10;
        case 0x37: // SCF
            F = (F & (FLAG_S | FLAG_Z | FLAG_PV)) | FLAG_C | (A & (FLAG_3 | FLAG_5));
            return 4;
        case 0x38: { int8_t d = (int8_t)fetchByte(); if (F & FLAG_C) { PC += d; return 12; } return 7; }
        case 0x39: HL = add16(HL, SP); return 11;
        case 0x3A: A = readByte(fetchWord()); return 13;
        case 0x3B: SP--; return 6;
        case 0x3C: A = inc8(A); return 4;
        case 0x3D: A = dec8(A); return 4;
        case 0x3E: A = fetchByte(); return 7;
        case 0x3F: // CCF
            F = (F & (FLAG_S | FLAG_Z | FLAG_PV)) | ((F & FLAG_C) ? FLAG_H : 0) |
                ((F ^ FLAG_C) & FLAG_C) | (A & (FLAG_3 | FLAG_5));
            return 4;

        // LD r,r' (0x40-0x7F except 0x76=HALT)
        case 0x76: halted = true; return 4; // HALT

        case 0xC0: if (!(F & FLAG_Z)) { PC = pop(); return 11; } return 5;
        case 0xC1: BC = pop(); return 10;
        case 0xC2: { uint16_t a = fetchWord(); if (!(F & FLAG_Z)) PC = a; return 10; }
        case 0xC3: PC = fetchWord(); return 10;
        case 0xC4: { uint16_t a = fetchWord(); if (!(F & FLAG_Z)) { push(PC); PC = a; return 17; } return 10; }
        case 0xC5: push(BC); return 11;
        case 0xC6: A = add8(A, fetchByte()); return 7;
        case 0xC7: push(PC); PC = 0x00; return 11;
        case 0xC8: if (F & FLAG_Z) { PC = pop(); return 11; } return 5;
        case 0xC9: PC = pop(); return 10;
        case 0xCA: { uint16_t a = fetchWord(); if (F & FLAG_Z) PC = a; return 10; }
        case 0xCB: return execCB();
        case 0xCC: { uint16_t a = fetchWord(); if (F & FLAG_Z) { push(PC); PC = a; return 17; } return 10; }
        case 0xCD: { uint16_t a = fetchWord(); push(PC); PC = a; return 17; }
        case 0xCE: A = adc8(A, fetchByte()); return 7;
        case 0xCF: push(PC); PC = 0x08; return 11;
        case 0xD0: if (!(F & FLAG_C)) { PC = pop(); return 11; } return 5;
        case 0xD1: DE = pop(); return 10;
        case 0xD2: { uint16_t a = fetchWord(); if (!(F & FLAG_C)) PC = a; return 10; }
        case 0xD3: { uint8_t port = fetchByte(); writePort((A << 8) | port, A); return 11; }
        case 0xD4: { uint16_t a = fetchWord(); if (!(F & FLAG_C)) { push(PC); PC = a; return 17; } return 10; }
        case 0xD5: push(DE); return 11;
        case 0xD6: A = sub8(A, fetchByte()); return 7;
        case 0xD7: push(PC); PC = 0x10; return 11;
        case 0xD8: if (F & FLAG_C) { PC = pop(); return 11; } return 5;
        case 0xD9: { // EXX
            uint16_t t;
            t = BC; BC = BC_; BC_ = t;
            t = DE; DE = DE_; DE_ = t;
            t = HL; HL = HL_; HL_ = t;
            return 4;
        }
        case 0xDA: { uint16_t a = fetchWord(); if (F & FLAG_C) PC = a; return 10; }
        case 0xDB: { uint8_t port = fetchByte(); A = readPort((A << 8) | port); return 11; }
        case 0xDC: { uint16_t a = fetchWord(); if (F & FLAG_C) { push(PC); PC = a; return 17; } return 10; }
        case 0xDD: return execDD();
        case 0xDE: A = sbc8(A, fetchByte()); return 7;
        case 0xDF: push(PC); PC = 0x18; return 11;
        case 0xE0: if (!(F & FLAG_PV)) { PC = pop(); return 11; } return 5;
        case 0xE1: HL = pop(); return 10;
        case 0xE2: { uint16_t a = fetchWord(); if (!(F & FLAG_PV)) PC = a; return 10; }
        case 0xE3: { // EX (SP),HL
            uint16_t t = readWord(SP);
            writeWord(SP, HL);
            HL = t;
            return 19;
        }
        case 0xE4: { uint16_t a = fetchWord(); if (!(F & FLAG_PV)) { push(PC); PC = a; return 17; } return 10; }
        case 0xE5: push(HL); return 11;
        case 0xE6: and8(fetchByte()); return 7;
        case 0xE7: push(PC); PC = 0x20; return 11;
        case 0xE8: if (F & FLAG_PV) { PC = pop(); return 11; } return 5;
        case 0xE9: PC = HL; return 4;
        case 0xEA: { uint16_t a = fetchWord(); if (F & FLAG_PV) PC = a; return 10; }
        case 0xEB: { // EX DE,HL
            uint16_t t = DE; DE = HL; HL = t;
            return 4;
        }
        case 0xEC: { uint16_t a = fetchWord(); if (F & FLAG_PV) { push(PC); PC = a; return 17; } return 10; }
        case 0xED: return execED();
        case 0xEE: xor8(fetchByte()); return 7;
        case 0xEF: push(PC); PC = 0x28; return 11;
        case 0xF0: if (!(F & FLAG_S)) { PC = pop(); return 11; } return 5;
        case 0xF1: AF = pop(); return 10;
        case 0xF2: { uint16_t a = fetchWord(); if (!(F & FLAG_S)) PC = a; return 10; }
        case 0xF3: IFF1 = IFF2 = false; return 4; // DI
        case 0xF4: { uint16_t a = fetchWord(); if (!(F & FLAG_S)) { push(PC); PC = a; return 17; } return 10; }
        case 0xF5: push(AF); return 11;
        case 0xF6: or8(fetchByte()); return 7;
        case 0xF7: push(PC); PC = 0x30; return 11;
        case 0xF8: if (F & FLAG_S) { PC = pop(); return 11; } return 5;
        case 0xF9: SP = HL; return 6;
        case 0xFA: { uint16_t a = fetchWord(); if (F & FLAG_S) PC = a; return 10; }
        case 0xFB: IFF1 = IFF2 = true; return 4; // EI
        case 0xFC: { uint16_t a = fetchWord(); if (F & FLAG_S) { push(PC); PC = a; return 17; } return 10; }
        case 0xFD: return execFD();
        case 0xFE: cp8(fetchByte()); return 7;
        case 0xFF: push(PC); PC = 0x38; return 11;

        default:
            // LD r,r' block (0x40-0x7F)
            if (x == 1) {
                if (z == 6) { // LD r, (HL)
                    setReg8(y, readByte(HL));
                    return 7;
                } else if (y == 6) { // LD (HL), r
                    writeByte(HL, getReg8(z));
                    return 7;
                } else {
                    setReg8(y, getReg8(z));
                    return 4;
                }
            }
            // ALU block (0x80-0xBF)
            if (x == 2) {
                uint8_t val = getReg8(z);
                int cyc = (z == 6) ? 7 : 4;
                switch (y) {
                    case 0: A = add8(A, val); break;
                    case 1: A = adc8(A, val); break;
                    case 2: A = sub8(A, val); break;
                    case 3: A = sbc8(A, val); break;
                    case 4: and8(val); break;
                    case 5: xor8(val); break;
                    case 6: or8(val); break;
                    case 7: cp8(val); break;
                }
                return cyc;
            }
            return 4; // Unknown
    }
}

/**
 * @brief CB-prefix handler - rotations, shifts, and bit operations.
 *
 * Decodes the second opcode and executes the corresponding operation:
 * - x=0 (0x00-0x3F): Rotations/Shifts (RLC, RRC, RL, RR, SLA, SRA, SLL, SRL)
 * - x=1 (0x40-0x7F): BIT test
 * - x=2 (0x80-0xBF): RES (clear bit)
 * - x=3 (0xC0-0xFF): SET (set bit)
 *
 * z determines the source/destination register (0=B...6=(HL)...7=A).
 * y determines the specific operation or bit number depending on x.
 *
 * @return Clock cycles consumed (8 for register, 12 for BIT (HL), 15 for other (HL)).
 */
int Z80::execCB() {
    R = (R & 0x80) | ((R + 1) & 0x7F);
    uint8_t opcode = fetchByte();
    uint8_t y = (opcode >> 3) & 7;
    uint8_t z = opcode & 7;
    uint8_t x = (opcode >> 6) & 3;

    uint8_t val = getReg8(z);
    int cyc = (z == 6) ? 15 : 8;

    switch (x) {
        case 0: // Rotates/shifts
            switch (y) {
                case 0: val = rlc(val); break;
                case 1: val = rrc(val); break;
                case 2: val = rl(val); break;
                case 3: val = rr(val); break;
                case 4: val = sla(val); break;
                case 5: val = sra(val); break;
                case 6: val = sll(val); break;
                case 7: val = srl(val); break;
            }
            setReg8(z, val);
            break;
        case 1: // BIT
            bit_op(y, val);
            cyc = (z == 6) ? 12 : 8;
            break;
        case 2: // RES
            setReg8(z, res_op(y, val));
            break;
        case 3: // SET
            setReg8(z, set_op(y, val));
            break;
    }
    return cyc;
}

/**
 * @brief DD-prefix handler - IX index register operations.
 *
 * The DD prefix modifies subsequent instructions as follows:
 * - HL references are replaced by IX
 * - (HL) memory accesses become (IX+d) with signed displacement
 * - Undocumented: H/L can be addressed as IXH/IXL individually
 *
 * For non-IX-relevant opcodes, the instruction is executed as a normal
 * unprefixed instruction via execMain().
 *
 * Special case: DD CB forwards to execDDCB() (double prefix).
 *
 * @return Clock cycles consumed.
 */
int Z80::execDD() {
    R = (R & 0x80) | ((R + 1) & 0x7F);
    uint8_t opcode = fetchByte();

    if (opcode == 0xCB) return execDDCB();

    // DD prefix replaces HL with IX, H with IXH, L with IXL
    // For (HL) references, uses (IX+d)
    switch (opcode) {
        case 0x09: IX = add16(IX, BC); return 15;
        case 0x19: IX = add16(IX, DE); return 15;
        case 0x21: IX = fetchWord(); return 14;
        case 0x22: writeWord(fetchWord(), IX); return 20;
        case 0x23: IX++; return 10;
        case 0x24: { uint8_t h = (IX >> 8); h = inc8(h); IX = (h << 8) | (IX & 0xFF); return 8; }
        case 0x25: { uint8_t h = (IX >> 8); h = dec8(h); IX = (h << 8) | (IX & 0xFF); return 8; }
        case 0x26: { uint8_t n = fetchByte(); IX = (n << 8) | (IX & 0xFF); return 11; }
        case 0x29: IX = add16(IX, IX); return 15;
        case 0x2A: IX = readWord(fetchWord()); return 20;
        case 0x2B: IX--; return 10;
        case 0x2C: { uint8_t l = IX & 0xFF; l = inc8(l); IX = (IX & 0xFF00) | l; return 8; }
        case 0x2D: { uint8_t l = IX & 0xFF; l = dec8(l); IX = (IX & 0xFF00) | l; return 8; }
        case 0x2E: { uint8_t n = fetchByte(); IX = (IX & 0xFF00) | n; return 11; }
        case 0x34: { int8_t d = (int8_t)fetchByte(); uint16_t a = IX + d; writeByte(a, inc8(readByte(a))); return 23; }
        case 0x35: { int8_t d = (int8_t)fetchByte(); uint16_t a = IX + d; writeByte(a, dec8(readByte(a))); return 23; }
        case 0x36: { int8_t d = (int8_t)fetchByte(); uint8_t n = fetchByte(); writeByte(IX + d, n); return 19; }
        case 0x39: IX = add16(IX, SP); return 15;

        case 0x46: case 0x4E: case 0x56: case 0x5E:
        case 0x66: case 0x6E: case 0x7E: {
            int8_t d = (int8_t)fetchByte();
            uint8_t y = (opcode >> 3) & 7;
            setReg8(y, readByte(IX + d));
            return 19;
        }

        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x77: {
            int8_t d = (int8_t)fetchByte();
            uint8_t z = opcode & 7;
            writeByte(IX + d, getReg8(z));
            return 19;
        }

        case 0x86: { int8_t d = (int8_t)fetchByte(); A = add8(A, readByte(IX + d)); return 19; }
        case 0x8E: { int8_t d = (int8_t)fetchByte(); A = adc8(A, readByte(IX + d)); return 19; }
        case 0x96: { int8_t d = (int8_t)fetchByte(); A = sub8(A, readByte(IX + d)); return 19; }
        case 0x9E: { int8_t d = (int8_t)fetchByte(); A = sbc8(A, readByte(IX + d)); return 19; }
        case 0xA6: { int8_t d = (int8_t)fetchByte(); and8(readByte(IX + d)); return 19; }
        case 0xAE: { int8_t d = (int8_t)fetchByte(); xor8(readByte(IX + d)); return 19; }
        case 0xB6: { int8_t d = (int8_t)fetchByte(); or8(readByte(IX + d)); return 19; }
        case 0xBE: { int8_t d = (int8_t)fetchByte(); cp8(readByte(IX + d)); return 19; }

        case 0xE1: IX = pop(); return 14;
        case 0xE3: { uint16_t t = readWord(SP); writeWord(SP, IX); IX = t; return 23; }
        case 0xE5: push(IX); return 15;
        case 0xE9: PC = IX; return 8;
        case 0xF9: SP = IX; return 10;

        default:
            // Undocumented: DD prefix with non-IX instruction acts as normal
            // Handle IXH/IXL variants for register ops
            {
                uint8_t x2 = (opcode >> 6) & 3;
                uint8_t y2 = (opcode >> 3) & 7;
                uint8_t z2 = opcode & 7;
                if (x2 == 1 && z2 != 6 && y2 != 6) {
                    // LD using IXH/IXL
                    uint8_t src, dst_idx = y2;
                    if (z2 == 4) src = (IX >> 8) & 0xFF;
                    else if (z2 == 5) src = IX & 0xFF;
                    else src = getReg8(z2);

                    if (dst_idx == 4) IX = (IX & 0xFF) | (src << 8);
                    else if (dst_idx == 5) IX = (IX & 0xFF00) | src;
                    else setReg8(dst_idx, src);
                    return 8;
                }
                if (x2 == 2 && z2 != 6) {
                    // ALU with IXH/IXL
                    uint8_t val;
                    if (z2 == 4) val = (IX >> 8) & 0xFF;
                    else if (z2 == 5) val = IX & 0xFF;
                    else val = getReg8(z2);
                    switch (y2) {
                        case 0: A = add8(A, val); break;
                        case 1: A = adc8(A, val); break;
                        case 2: A = sub8(A, val); break;
                        case 3: A = sbc8(A, val); break;
                        case 4: and8(val); break;
                        case 5: xor8(val); break;
                        case 6: or8(val); break;
                        case 7: cp8(val); break;
                    }
                    return 8;
                }
            }
            return execMain(opcode);
    }
}

/**
 * @brief FD-prefix handler - IY index register operations.
 *
 * Functionally identical to execDD(), but replaces IX with IY.
 * All comments and explanations from execDD() apply analogously.
 *
 * Special case: FD CB forwards to execFDCB() (double prefix).
 *
 * @return Clock cycles consumed.
 */
int Z80::execFD() {
    R = (R & 0x80) | ((R + 1) & 0x7F);
    uint8_t opcode = fetchByte();

    if (opcode == 0xCB) return execFDCB();

    switch (opcode) {
        case 0x09: IY = add16(IY, BC); return 15;
        case 0x19: IY = add16(IY, DE); return 15;
        case 0x21: IY = fetchWord(); return 14;
        case 0x22: writeWord(fetchWord(), IY); return 20;
        case 0x23: IY++; return 10;
        case 0x24: { uint8_t h = (IY >> 8); h = inc8(h); IY = (h << 8) | (IY & 0xFF); return 8; }
        case 0x25: { uint8_t h = (IY >> 8); h = dec8(h); IY = (h << 8) | (IY & 0xFF); return 8; }
        case 0x26: { uint8_t n = fetchByte(); IY = (n << 8) | (IY & 0xFF); return 11; }
        case 0x29: IY = add16(IY, IY); return 15;
        case 0x2A: IY = readWord(fetchWord()); return 20;
        case 0x2B: IY--; return 10;
        case 0x2C: { uint8_t l = IY & 0xFF; l = inc8(l); IY = (IY & 0xFF00) | l; return 8; }
        case 0x2D: { uint8_t l = IY & 0xFF; l = dec8(l); IY = (IY & 0xFF00) | l; return 8; }
        case 0x2E: { uint8_t n = fetchByte(); IY = (IY & 0xFF00) | n; return 11; }
        case 0x34: { int8_t d = (int8_t)fetchByte(); uint16_t a = IY + d; writeByte(a, inc8(readByte(a))); return 23; }
        case 0x35: { int8_t d = (int8_t)fetchByte(); uint16_t a = IY + d; writeByte(a, dec8(readByte(a))); return 23; }
        case 0x36: { int8_t d = (int8_t)fetchByte(); uint8_t n = fetchByte(); writeByte(IY + d, n); return 19; }
        case 0x39: IY = add16(IY, SP); return 15;

        case 0x46: case 0x4E: case 0x56: case 0x5E:
        case 0x66: case 0x6E: case 0x7E: {
            int8_t d = (int8_t)fetchByte();
            uint8_t y = (opcode >> 3) & 7;
            setReg8(y, readByte(IY + d));
            return 19;
        }
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x77: {
            int8_t d = (int8_t)fetchByte();
            uint8_t z = opcode & 7;
            writeByte(IY + d, getReg8(z));
            return 19;
        }

        case 0x86: { int8_t d = (int8_t)fetchByte(); A = add8(A, readByte(IY + d)); return 19; }
        case 0x8E: { int8_t d = (int8_t)fetchByte(); A = adc8(A, readByte(IY + d)); return 19; }
        case 0x96: { int8_t d = (int8_t)fetchByte(); A = sub8(A, readByte(IY + d)); return 19; }
        case 0x9E: { int8_t d = (int8_t)fetchByte(); A = sbc8(A, readByte(IY + d)); return 19; }
        case 0xA6: { int8_t d = (int8_t)fetchByte(); and8(readByte(IY + d)); return 19; }
        case 0xAE: { int8_t d = (int8_t)fetchByte(); xor8(readByte(IY + d)); return 19; }
        case 0xB6: { int8_t d = (int8_t)fetchByte(); or8(readByte(IY + d)); return 19; }
        case 0xBE: { int8_t d = (int8_t)fetchByte(); cp8(readByte(IY + d)); return 19; }

        case 0xE1: IY = pop(); return 14;
        case 0xE3: { uint16_t t = readWord(SP); writeWord(SP, IY); IY = t; return 23; }
        case 0xE5: push(IY); return 15;
        case 0xE9: PC = IY; return 8;
        case 0xF9: SP = IY; return 10;

        default:
            {
                uint8_t x2 = (opcode >> 6) & 3;
                uint8_t y2 = (opcode >> 3) & 7;
                uint8_t z2 = opcode & 7;
                if (x2 == 1 && z2 != 6 && y2 != 6) {
                    uint8_t src;
                    if (z2 == 4) src = (IY >> 8) & 0xFF;
                    else if (z2 == 5) src = IY & 0xFF;
                    else src = getReg8(z2);
                    if (y2 == 4) IY = (IY & 0xFF) | (src << 8);
                    else if (y2 == 5) IY = (IY & 0xFF00) | src;
                    else setReg8(y2, src);
                    return 8;
                }
                if (x2 == 2 && z2 != 6) {
                    uint8_t val;
                    if (z2 == 4) val = (IY >> 8) & 0xFF;
                    else if (z2 == 5) val = IY & 0xFF;
                    else val = getReg8(z2);
                    switch (y2) {
                        case 0: A = add8(A, val); break;
                        case 1: A = adc8(A, val); break;
                        case 2: A = sub8(A, val); break;
                        case 3: A = sbc8(A, val); break;
                        case 4: and8(val); break;
                        case 5: xor8(val); break;
                        case 6: or8(val); break;
                        case 7: cp8(val); break;
                    }
                    return 8;
                }
            }
            return execMain(opcode);
    }
}

/**
 * @brief DDCB double-prefix handler - bit/rotation operations on (IX+d).
 *
 * Special feature in instruction format: The displacement byte comes before the opcode:
 * DD CB dd op (not DD CB op dd as one might expect).
 *
 * For register operands (z != 6), the result is written both to memory
 * and also copied to the specified register.
 * This is undocumented but consistent Z80 behavior.
 *
 * @return Clock cycles consumed (20 for BIT, 23 for rotations/RES/SET).
 */
int Z80::execDDCB() {
    int8_t d = (int8_t)fetchByte();
    uint8_t opcode = fetchByte();
    uint16_t addr = IX + d;
    uint8_t val = readByte(addr);
    uint8_t y = (opcode >> 3) & 7;
    uint8_t z = opcode & 7;
    uint8_t x = (opcode >> 6) & 3;

    switch (x) {
        case 0: {
            uint8_t r;
            switch (y) {
                case 0: r = rlc(val); break;
                case 1: r = rrc(val); break;
                case 2: r = rl(val); break;
                case 3: r = rr(val); break;
                case 4: r = sla(val); break;
                case 5: r = sra(val); break;
                case 6: r = sll(val); break;
                default: r = srl(val); break;
            }
            writeByte(addr, r);
            if (z != 6) setReg8(z, r);
            return 23;
        }
        case 1:
            // BIT bei indizierten Operationen: Flags 3 und 5 kommen vom
            // oberen Byte der Adresse (undokumentiertes Z80-Verhalten)
            bit_op(y, val, (addr >> 8) & 0xFF);
            return 20;
        case 2: {
            uint8_t r = res_op(y, val);
            writeByte(addr, r);
            if (z != 6) setReg8(z, r);
            return 23;
        }
        case 3: {
            uint8_t r = set_op(y, val);
            writeByte(addr, r);
            if (z != 6) setReg8(z, r);
            return 23;
        }
    }
    return 23;
}

/**
 * @brief FDCB double-prefix handler - bit/rotation operations on (IY+d).
 *
 * Functionally identical to execDDCB(), but uses IY instead of IX.
 * See execDDCB() for details on instruction format and undocumented behavior.
 *
 * @return Clock cycles consumed (20 for BIT, 23 for rotations/RES/SET).
 */
int Z80::execFDCB() {
    int8_t d = (int8_t)fetchByte();
    uint8_t opcode = fetchByte();
    uint16_t addr = IY + d;
    uint8_t val = readByte(addr);
    uint8_t y = (opcode >> 3) & 7;
    uint8_t z = opcode & 7;
    uint8_t x = (opcode >> 6) & 3;

    switch (x) {
        case 0: {
            uint8_t r;
            switch (y) {
                case 0: r = rlc(val); break;
                case 1: r = rrc(val); break;
                case 2: r = rl(val); break;
                case 3: r = rr(val); break;
                case 4: r = sla(val); break;
                case 5: r = sra(val); break;
                case 6: r = sll(val); break;
                default: r = srl(val); break;
            }
            writeByte(addr, r);
            if (z != 6) setReg8(z, r);
            return 23;
        }
        case 1:
            // BIT bei indizierten Operationen: Flags 3 und 5 kommen vom
            // oberen Byte der Adresse (undokumentiertes Z80-Verhalten)
            bit_op(y, val, (addr >> 8) & 0xFF);
            return 20;
        case 2: {
            uint8_t r = res_op(y, val);
            writeByte(addr, r);
            if (z != 6) setReg8(z, r);
            return 23;
        }
        case 3: {
            uint8_t r = set_op(y, val);
            writeByte(addr, r);
            if (z != 6) setReg8(z, r);
            return 23;
        }
    }
    return 23;
}

/**
 * @brief ED-prefix handler - extended Z80 instructions.
 *
 * Contains special instructions that don't fit the main opcode schema:
 * - **I/O with register**: IN r,(C) / OUT (C),r - port access via BC register pair
 * - **16-bit arithmetic**: SBC HL,rr / ADC HL,rr with complete flags
 * - **16-bit load**: LD (nn),rr / LD rr,(nn) for all register pairs
 * - **NEG**: Negates the accumulator (0 - A)
 * - **RETN/RETI**: Return from (non-)maskable interrupts
 * - **Interrupt mode**: IM 0/1/2
 * - **Special registers**: LD I,A / LD A,I / LD R,A / LD A,R
 * - **BCD rotation**: RRD / RLD - rotation of BCD digits between A and (HL)
 * - **Block instructions**: LDI, LDIR, LDD, LDDR - memory block transfer
 * - **Block search**: CPI, CPIR, CPD, CPDR - memory block comparison
 * - **Block I/O**: INI, INIR, IND, INDR, OUTI, OTIR, OUTD, OTDR
 *
 * The repeat variants (R suffix) repeat the instruction by decrementing
 * PC by 2 until the termination condition is met.
 *
 * @return Clock cycles consumed.
 */
int Z80::execED() {
    R = (R & 0x80) | ((R + 1) & 0x7F);
    uint8_t opcode = fetchByte();

    switch (opcode) {
        // IN r,(C)
        case 0x40: B = readPort(BC); { uint8_t v = B; F = (F & FLAG_C) | (v & (FLAG_S|FLAG_3|FLAG_5)); if(!v) F|=FLAG_Z; if(parity(v)) F|=FLAG_PV; } return 12;
        case 0x48: C = readPort(BC); { uint8_t v = C; F = (F & FLAG_C) | (v & (FLAG_S|FLAG_3|FLAG_5)); if(!v) F|=FLAG_Z; if(parity(v)) F|=FLAG_PV; } return 12;
        case 0x50: D = readPort(BC); { uint8_t v = D; F = (F & FLAG_C) | (v & (FLAG_S|FLAG_3|FLAG_5)); if(!v) F|=FLAG_Z; if(parity(v)) F|=FLAG_PV; } return 12;
        case 0x58: E = readPort(BC); { uint8_t v = E; F = (F & FLAG_C) | (v & (FLAG_S|FLAG_3|FLAG_5)); if(!v) F|=FLAG_Z; if(parity(v)) F|=FLAG_PV; } return 12;
        case 0x60: H = readPort(BC); { uint8_t v = H; F = (F & FLAG_C) | (v & (FLAG_S|FLAG_3|FLAG_5)); if(!v) F|=FLAG_Z; if(parity(v)) F|=FLAG_PV; } return 12;
        case 0x68: L = readPort(BC); { uint8_t v = L; F = (F & FLAG_C) | (v & (FLAG_S|FLAG_3|FLAG_5)); if(!v) F|=FLAG_Z; if(parity(v)) F|=FLAG_PV; } return 12;
        case 0x70: { uint8_t v = readPort(BC); F = (F & FLAG_C) | (v & (FLAG_S|FLAG_3|FLAG_5)); if(!v) F|=FLAG_Z; if(parity(v)) F|=FLAG_PV; } return 12;
        case 0x78: A = readPort(BC); { uint8_t v = A; F = (F & FLAG_C) | (v & (FLAG_S|FLAG_3|FLAG_5)); if(!v) F|=FLAG_Z; if(parity(v)) F|=FLAG_PV; } return 12;

        // OUT (C),r
        case 0x41: writePort(BC, B); return 12;
        case 0x49: writePort(BC, C); return 12;
        case 0x51: writePort(BC, D); return 12;
        case 0x59: writePort(BC, E); return 12;
        case 0x61: writePort(BC, H); return 12;
        case 0x69: writePort(BC, L); return 12;
        case 0x71: writePort(BC, 0); return 12;
        case 0x79: writePort(BC, A); return 12;

        // SBC HL, rr
        case 0x42: HL = sbc16(HL, BC); return 15;
        case 0x52: HL = sbc16(HL, DE); return 15;
        case 0x62: HL = sbc16(HL, HL); return 15;
        case 0x72: HL = sbc16(HL, SP); return 15;

        // ADC HL, rr
        case 0x4A: HL = adc16(HL, BC); return 15;
        case 0x5A: HL = adc16(HL, DE); return 15;
        case 0x6A: HL = adc16(HL, HL); return 15;
        case 0x7A: HL = adc16(HL, SP); return 15;

        // LD (nn), rr
        case 0x43: writeWord(fetchWord(), BC); return 20;
        case 0x53: writeWord(fetchWord(), DE); return 20;
        case 0x63: writeWord(fetchWord(), HL); return 20;
        case 0x73: writeWord(fetchWord(), SP); return 20;

        // LD rr, (nn)
        case 0x4B: BC = readWord(fetchWord()); return 20;
        case 0x5B: DE = readWord(fetchWord()); return 20;
        case 0x6B: HL = readWord(fetchWord()); return 20;
        case 0x7B: SP = readWord(fetchWord()); return 20;

        // NEG
        case 0x44: case 0x4C: case 0x54: case 0x5C:
        case 0x64: case 0x6C: case 0x74: case 0x7C: {
            uint8_t tmp = A;
            A = 0;
            A = sub8(A, tmp);
            return 8;
        }

        // RETN
        case 0x45: case 0x55: case 0x65: case 0x75:
            IFF1 = IFF2;
            PC = pop();
            return 14;

        // RETI (all aliases: 0x4D, 0x5D, 0x6D, 0x7D)
        case 0x4D: case 0x5D: case 0x6D: case 0x7D:
            IFF1 = IFF2;
            PC = pop();
            if (retiCallback) retiCallback();  // Notify peripherals
            return 14;

        // IM
        case 0x46: case 0x66: IM = 0; return 8;
        case 0x56: case 0x76: IM = 1; return 8;
        case 0x5E: case 0x7E: IM = 2; return 8;

        // LD I,A / LD A,I / LD R,A / LD A,R
        case 0x47: I = A; return 9;
        case 0x4F: R = A; return 9;
        case 0x57: // LD A,I
            A = I;
            F = (F & FLAG_C) | (A & (FLAG_S | FLAG_3 | FLAG_5));
            if (A == 0) F |= FLAG_Z;
            if (IFF2) F |= FLAG_PV;
            return 9;
        case 0x5F: // LD A,R
            A = R;
            F = (F & FLAG_C) | (A & (FLAG_S | FLAG_3 | FLAG_5));
            if (A == 0) F |= FLAG_Z;
            if (IFF2) F |= FLAG_PV;
            return 9;

        // RRD / RLD
        case 0x67: { // RRD
            uint8_t m = readByte(HL);
            writeByte(HL, (A << 4) | (m >> 4));
            A = (A & 0xF0) | (m & 0x0F);
            F = (F & FLAG_C) | (A & (FLAG_S | FLAG_3 | FLAG_5));
            if (A == 0) F |= FLAG_Z;
            if (parity(A)) F |= FLAG_PV;
            return 18;
        }
        case 0x6F: { // RLD
            uint8_t m = readByte(HL);
            writeByte(HL, (m << 4) | (A & 0x0F));
            A = (A & 0xF0) | (m >> 4);
            F = (F & FLAG_C) | (A & (FLAG_S | FLAG_3 | FLAG_5));
            if (A == 0) F |= FLAG_Z;
            if (parity(A)) F |= FLAG_PV;
            return 18;
        }

        // Block operations
        case 0xA0: { // LDI
            uint8_t v = readByte(HL);
            writeByte(DE, v);
            HL++; DE++; BC--;
            uint8_t n = v + A;
            F = (F & (FLAG_S | FLAG_Z | FLAG_C));
            if (n & 0x02) F |= FLAG_5;
            if (n & 0x08) F |= FLAG_3;
            if (BC != 0) F |= FLAG_PV;
            return 16;
        }
        case 0xA1: { // CPI
            uint8_t v = readByte(HL);
            uint8_t r = A - v;
            HL++; BC--;
            F = FLAG_N | (F & FLAG_C) | (r & FLAG_S);
            if (r == 0) F |= FLAG_Z;
            if ((A ^ v ^ r) & 0x10) F |= FLAG_H;
            uint8_t n = r - ((F & FLAG_H) ? 1 : 0);
            if (n & 0x02) F |= FLAG_5;
            if (n & 0x08) F |= FLAG_3;
            if (BC != 0) F |= FLAG_PV;
            return 16;
        }
        case 0xA2: { // INI
            uint8_t v = readPort(BC);
            writeByte(HL, v);
            HL++; B--;
            F = (B & (FLAG_S | FLAG_3 | FLAG_5));
            if (B == 0) F |= (FLAG_Z | FLAG_N);
            else F |= FLAG_N;
            return 16;
        }
        case 0xA3: { // OUTI
            uint8_t v = readByte(HL);
            B--;
            writePort(BC, v);
            HL++;
            F = (B & (FLAG_S | FLAG_3 | FLAG_5));
            if (B == 0) F |= (FLAG_Z | FLAG_N);
            else F |= FLAG_N;
            return 16;
        }
        case 0xA8: { // LDD
            uint8_t v = readByte(HL);
            writeByte(DE, v);
            HL--; DE--; BC--;
            uint8_t n = v + A;
            F = (F & (FLAG_S | FLAG_Z | FLAG_C));
            if (n & 0x02) F |= FLAG_5;
            if (n & 0x08) F |= FLAG_3;
            if (BC != 0) F |= FLAG_PV;
            return 16;
        }
        case 0xA9: { // CPD
            uint8_t v = readByte(HL);
            uint8_t r = A - v;
            HL--; BC--;
            F = FLAG_N | (F & FLAG_C) | (r & FLAG_S);
            if (r == 0) F |= FLAG_Z;
            if ((A ^ v ^ r) & 0x10) F |= FLAG_H;
            uint8_t n = r - ((F & FLAG_H) ? 1 : 0);
            if (n & 0x02) F |= FLAG_5;
            if (n & 0x08) F |= FLAG_3;
            if (BC != 0) F |= FLAG_PV;
            return 16;
        }
        case 0xAA: { // IND
            uint8_t v = readPort(BC);
            writeByte(HL, v);
            HL--; B--;
            F = (B & (FLAG_S | FLAG_3 | FLAG_5));
            if (B == 0) F |= (FLAG_Z | FLAG_N);
            else F |= FLAG_N;
            return 16;
        }
        case 0xAB: { // OUTD
            uint8_t v = readByte(HL);
            B--;
            writePort(BC, v);
            HL--;
            F = (B & (FLAG_S | FLAG_3 | FLAG_5));
            if (B == 0) F |= (FLAG_Z | FLAG_N);
            else F |= FLAG_N;
            return 16;
        }
        case 0xB0: { // LDIR
            uint8_t v = readByte(HL);
            writeByte(DE, v);
            HL++; DE++; BC--;
            uint8_t n = v + A;
            F = (F & (FLAG_S | FLAG_Z | FLAG_C));
            if (n & 0x02) F |= FLAG_5;
            if (n & 0x08) F |= FLAG_3;
            if (BC != 0) { F |= FLAG_PV; PC -= 2; return 21; }
            return 16;
        }
        case 0xB1: { // CPIR
            uint8_t v = readByte(HL);
            uint8_t r = A - v;
            HL++; BC--;
            F = FLAG_N | (F & FLAG_C) | (r & FLAG_S);
            if (r == 0) F |= FLAG_Z;
            if ((A ^ v ^ r) & 0x10) F |= FLAG_H;
            uint8_t n = r - ((F & FLAG_H) ? 1 : 0);
            if (n & 0x02) F |= FLAG_5;
            if (n & 0x08) F |= FLAG_3;
            if (BC != 0) F |= FLAG_PV;
            if (BC != 0 && !(F & FLAG_Z)) { PC -= 2; return 21; }
            return 16;
        }
        case 0xB2: { // INIR
            uint8_t v = readPort(BC);
            writeByte(HL, v);
            HL++; B--;
            F = FLAG_Z | FLAG_N;
            if (B != 0) { F &= ~FLAG_Z; PC -= 2; return 21; }
            return 16;
        }
        case 0xB3: { // OTIR
            uint8_t v = readByte(HL);
            B--;
            writePort(BC, v);
            HL++;
            F = FLAG_Z | FLAG_N;
            if (B != 0) { F &= ~FLAG_Z; PC -= 2; return 21; }
            return 16;
        }
        case 0xB8: { // LDDR
            uint8_t v = readByte(HL);
            writeByte(DE, v);
            HL--; DE--; BC--;
            uint8_t n = v + A;
            F = (F & (FLAG_S | FLAG_Z | FLAG_C));
            if (n & 0x02) F |= FLAG_5;
            if (n & 0x08) F |= FLAG_3;
            if (BC != 0) { F |= FLAG_PV; PC -= 2; return 21; }
            return 16;
        }
        case 0xB9: { // CPDR
            uint8_t v = readByte(HL);
            uint8_t r = A - v;
            HL--; BC--;
            F = FLAG_N | (F & FLAG_C) | (r & FLAG_S);
            if (r == 0) F |= FLAG_Z;
            if ((A ^ v ^ r) & 0x10) F |= FLAG_H;
            uint8_t n = r - ((F & FLAG_H) ? 1 : 0);
            if (n & 0x02) F |= FLAG_5;
            if (n & 0x08) F |= FLAG_3;
            if (BC != 0) F |= FLAG_PV;
            if (BC != 0 && !(F & FLAG_Z)) { PC -= 2; return 21; }
            return 16;
        }
        case 0xBA: { // INDR
            uint8_t v = readPort(BC);
            writeByte(HL, v);
            HL--; B--;
            F = FLAG_Z | FLAG_N;
            if (B != 0) { F &= ~FLAG_Z; PC -= 2; return 21; }
            return 16;
        }
        case 0xBB: { // OTDR
            uint8_t v = readByte(HL);
            B--;
            writePort(BC, v);
            HL--;
            F = FLAG_Z | FLAG_N;
            if (B != 0) { F &= ~FLAG_Z; PC -= 2; return 21; }
            return 16;
        }

        default:
            return 8; // Undocumented NOP
    }
}

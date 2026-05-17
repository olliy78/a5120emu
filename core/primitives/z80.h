/**
 * @file z80.h
 * @brief Zilog Z80 CPU Emulator - Header File
 *
 * Complete emulation of the Z80 microprocessor as used in the Robotron A5120
 * office computer (GDR). Implements the complete documented and undocumented
 * Z80 instruction set including:
 * - All 8-bit and 16-bit ALU operations with correct flag behavior
 * - Rotation and shift instructions (CB prefix)
 * - Index register operations with IX/IY (DD/FD prefix)
 * - Extended instructions (ED prefix): block operations, I/O, NEG, RETI/RETN
 * - Undocumented instructions: SLL, IXH/IXL/IYH/IYL registers, DDCB/FDCB variants
 * - All three interrupt modes (IM 0, IM 1, IM 2) and NMI
 * - Cycle-accurate timing for correct emulation
 *
 * The class uses callback functions for memory and I/O access, making it
 * platform-independent and embeddable in any emulation environment.
 *
 * @author Olaf Krieger
 * @license MIT License
 * @see https://opensource.org/licenses/MIT
 */
#pragma once
#include <cstdint>
#include <functional>

/**
 * @class Z80
 * @brief Emulates a Zilog Z80 microprocessor.
 *
 * This class implements the complete Z80 processor, including all registers,
 * flags, interrupt logic, and the entire instruction set. Memory and I/O
 * accesses are abstracted through configurable callback functions, making
 * the CPU emulation independent of the concrete hardware environment.
 *
 * Typical usage:
 * @code
 *   Z80 cpu;
 *   cpu.readByte  = [&](uint16_t addr) -> uint8_t { return memory[addr]; };
 *   cpu.writeByte = [&](uint16_t addr, uint8_t val) { memory[addr] = val; };
 *   cpu.readPort  = [&](uint16_t port) -> uint8_t { return io_read(port); };
 *   cpu.writePort = [&](uint16_t port, uint8_t val) { io_write(port, val); };
 *   cpu.reset();
 *   while (running) {
 *       int cycles = cpu.step();
 *   }
 * @endcode
 */
class Z80 {
public:
    // =========================================================================
    /// @name Main Register Set
    /// @brief The four 16-bit register pairs AF, BC, DE, HL, which can also
    ///        be accessed as individual 8-bit registers. The byte order within
    ///        the union accounts for the host platform's endianness.
    // =========================================================================
    ///@{

    /**
     * @brief Accumulator (A) and Flag register (F).
     *
     * A is the main accumulator for arithmetic and logical operations.
     * F contains the status flags (S, Z, H, PV, N, C and undocumented bits 3 and 5).
     */
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t F, A;   ///< Little-Endian: F is at lower address
            #else
            uint8_t A, F;   ///< Big-Endian: A is at lower address
            #endif
        };
        uint16_t AF;         ///< Access to the complete AF register pair
    };

    /**
     * @brief General purpose registers B and C.
     *
     * B is frequently used as a loop counter (DJNZ instruction).
     * C often serves as port number for I/O operations.
     * BC is used as 16-bit byte counter for block operations.
     */
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t C, B;
            #else
            uint8_t B, C;
            #endif
        };
        uint16_t BC;         ///< Access to the complete BC register pair
    };

    /**
     * @brief General purpose registers D and E.
     *
     * DE often serves as destination address for block transfers (LDI, LDIR, etc.).
     */
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t E, D;
            #else
            uint8_t D, E;
            #endif
        };
        uint16_t DE;         ///< Access to the complete DE register pair
    };

    /**
     * @brief General purpose registers H and L.
     *
     * HL is the primary 16-bit address register and is used for indirect
     * memory accesses (e.g., LD A,(HL)). In DD/FD-prefixed instructions,
     * HL is replaced by IX or IY respectively.
     */
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t L, H;
            #else
            uint8_t H, L;
            #endif
        };
        uint16_t HL;         ///< Access to the complete HL register pair
    };

    ///@}

    // =========================================================================
    /// @name Shadow Register Set
    /// @brief Second register set that can be exchanged with the main
    ///        register set via EX AF,AF' and EXX instructions.
    ///        Typically used in interrupt routines.
    // =========================================================================
    ///@{
    uint16_t AF_;  ///< Shadow register AF'
    uint16_t BC_;  ///< Shadow register BC'
    uint16_t DE_;  ///< Shadow register DE'
    uint16_t HL_;  ///< Shadow register HL'
    ///@}

    // =========================================================================
    /// @name Index Registers
    /// @brief Enable indexed addressing with displacement (IX+d, IY+d).
    ///        Undocumented: the upper/lower bytes (IXH, IXL, IYH, IYL)
    ///        can also be accessed individually.
    // =========================================================================
    ///@{
    uint16_t IX;   ///< Index register X
    uint16_t IY;   ///< Index register Y
    ///@}

    // =========================================================================
    /// @name Program Counter and Stack Pointer
    // =========================================================================
    ///@{
    uint16_t PC;   ///< Program Counter - points to the next instruction
    uint16_t SP;   ///< Stack Pointer - points to the top of the stack
    ///@}

    // =========================================================================
    /// @name Interrupt and Refresh Registers
    // =========================================================================
    ///@{
    uint8_t I;     ///< Interrupt vector register - upper byte of vector table in IM 2
    uint8_t R;     ///< Memory refresh register - bit 7 remains constant
    ///@}

    // =========================================================================
    /// @name Interrupt State
    // =========================================================================
    ///@{
    bool IFF1;     ///< Interrupt Flip-Flop 1 - controls acceptance of maskable interrupts
    bool IFF2;     ///< Interrupt Flip-Flop 2 - backup of IFF1 during NMI
    uint8_t IM;    ///< Interrupt mode (0, 1, or 2)
    bool halted;   ///< True when CPU is halted by HALT instruction
    ///@}

    /**
     * @brief Total count of clock cycles executed so far.
     *
     * Incremented with each step() call and during interrupt handling.
     * Can be used for timing peripheral devices and timing synchronization.
     */
    uint64_t cycles;

    // =========================================================================
    /// @name Z80 Status Flags
    /// @brief Bit masks for individual bits of flag register F.
    // =========================================================================
    ///@{
    static constexpr uint8_t FLAG_C  = 0x01;  ///< Carry flag (bit 0) - carry from addition/subtraction
    static constexpr uint8_t FLAG_N  = 0x02;  ///< Subtraction flag (bit 1) - set after subtraction, used for DAA
    static constexpr uint8_t FLAG_PV = 0x04;  ///< Parity/Overflow flag (bit 2) - parity or arithmetic overflow
    static constexpr uint8_t FLAG_3  = 0x08;  ///< Undocumented flag (bit 3) - copy of bit 3 of result
    static constexpr uint8_t FLAG_H  = 0x10;  ///< Half-carry flag (bit 4) - half-carry (BCD correction)
    static constexpr uint8_t FLAG_5  = 0x20;  ///< Undocumented flag (bit 5) - copy of bit 5 of result
    static constexpr uint8_t FLAG_Z  = 0x40;  ///< Zero flag (bit 6) - set when result is zero
    static constexpr uint8_t FLAG_S  = 0x80;  ///< Sign flag (bit 7) - sign bit (copy of bit 7 of result)
    ///@}

    // =========================================================================
    /// @name Callback Functions for Memory and I/O Access
    /// @brief Must be set before using the CPU emulation.
    ///        They provide the connection to the emulated hardware.
    // =========================================================================
    ///@{
    std::function<uint8_t(uint16_t)> readByte;           ///< Callback: Read byte from memory (address → value)
    std::function<void(uint16_t, uint8_t)> writeByte;    ///< Callback: Write byte to memory (address, value)
    std::function<uint8_t(uint16_t)> readPort;           ///< Callback: Read byte from I/O port (port address → value)
    std::function<void(uint16_t, uint8_t)> writePort;    ///< Callback: Write byte to I/O port (port address, value)

    /**
     * @brief Optional: Instruction trace callback for detailed debugging.
     *
     * If set, this function is called BEFORE each instruction execution.
     * It receives a pointer to the CPU and the CPU instance itself,
     * allowing all registers and the current PC to be read.
     * Only active if LOG_LEVEL >= 5 (TRACE), otherwise not set.
     *
     * Signature: void callback(const Z80& cpu)
     */
    std::function<void(const Z80&)> traceCallback;       ///< TRACE callback: called before each instruction (optional)

    /**
     * @brief Optional: RETI callback to notify peripherals.
     *
     * If set, this function is called when the CPU executes a
     * RETI instruction (Return from Interrupt). This allows peripheral
     * devices to reset their "Interrupt Under Service" (IUS) flags.
     *
     * Signature: void callback()
     */
    std::function<void()> retiCallback;                  ///< RETI callback: called after RETI execution (optional)
    ///@}

    // =========================================================================
    /// @name Public Methods
    // =========================================================================
    ///@{

    /** @brief Constructor - initializes the CPU by calling reset(). */
    Z80();

    /**
     * @brief Resets the CPU to its initial state.
     *
     * All registers are set to 0, SP to 0xFFFF, interrupts are
     * disabled, IM set to 0, and HALT state is cleared.
     */
    void reset();

    /**
     * @brief Executes a single Z80 instruction.
     *
     * Reads the opcode at address PC, decodes and executes the instruction.
     * In HALT state, only NOP cycles are consumed (4 clocks).
     *
     * @return Number of clock cycles consumed by this instruction.
     */
    int step();

    /**
     * @brief Triggers a maskable interrupt (INT).
     *
     * Only accepted if IFF1 is set. Behavior depends on the current
     * interrupt mode:
     * - IM 0: Executes the instruction on the data bus (typically RST).
     * - IM 1: Jumps to address 0x0038.
     * - IM 2: Reads target address from vector table (I*256 + vector).
     *
     * @param vector Interrupt vector - interpreted differently depending on IM.
     */
    void interrupt(uint8_t vector);

    /**
     * @brief Triggers a non-maskable interrupt (NMI).
     *
     * Cannot be disabled. Saves IFF1 to IFF2, disables interrupts,
     * and jumps to fixed address 0x0066.
     */
    void nmi();

    /**
     * @brief Pushes a 16-bit value onto the stack.
     *
     * Decrements SP by 2 and writes the value in little-endian order.
     *
     * @param val The value to push onto the stack.
     */
    void push(uint16_t val);

    /**
     * @brief Pops a 16-bit value from the stack.
     *
     * Reads the value in little-endian order and increments SP by 2.
     *
     * @return The 16-bit value read from the stack.
     */
    uint16_t pop();

    ///@}

private:
    // =========================================================================
    /// @name Internal Memory Access Helpers
    // =========================================================================
    ///@{

    /**
     * @brief Reads a 16-bit word from memory (little-endian).
     * @param addr Start address (low byte).
     * @return Read 16-bit word.
     */
    uint16_t readWord(uint16_t addr);

    /**
     * @brief Writes a 16-bit word to memory (little-endian).
     * @param addr Start address (low byte).
     * @param val 16-bit word to write.
     */
    void writeWord(uint16_t addr, uint16_t val);

    /**
     * @brief Reads the next byte at PC and increments the program counter.
     * @return Read byte.
     */
    uint8_t fetchByte();

    /**
     * @brief Reads the next 16-bit word at PC and increments PC by 2.
     * @return Read 16-bit word (little-endian).
     */
    uint16_t fetchWord();

    ///@}

    // =========================================================================
    /// @name 8-Bit ALU Operations
    /// @brief All arithmetic and logical 8-bit operations with correct
    ///        calculation of all flags (S, Z, H, PV, N, C and bits 3 and 5).
    // =========================================================================
    ///@{

    /**
     * @brief 8-bit addition: a + b.
     * @param a First operand.
     * @param b Second operand.
     * @return Result (8 bits). Flags: S, Z, H, PV (overflow), C.
     */
    uint8_t add8(uint8_t a, uint8_t b);

    /**
     * @brief 8-bit addition with carry: a + b + carry.
     * @param a First operand.
     * @param b Second operand.
     * @return Result (8 bits). Flags: S, Z, H, PV (overflow), C.
     */
    uint8_t adc8(uint8_t a, uint8_t b);

    /**
     * @brief 8-bit subtraction: a - b.
     * @param a First operand (minuend).
     * @param b Second operand (subtrahend).
     * @return Result (8 bits). Flags: S, Z, H, PV (overflow), N=1, C.
     */
    uint8_t sub8(uint8_t a, uint8_t b);

    /**
     * @brief 8-bit subtraction with carry: a - b - carry.
     * @param a First operand (minuend).
     * @param b Second operand (subtrahend).
     * @return Result (8 bits). Flags: S, Z, H, PV (overflow), N=1, C.
     */
    uint8_t sbc8(uint8_t a, uint8_t b);

    /**
     * @brief Logical AND: A = A & val.
     *
     * Sets H flag, clears N and C flags. PV indicates parity.
     * @param val Operand for AND operation.
     */
    void and8(uint8_t val);

    /**
     * @brief Logical OR: A = A | val.
     *
     * Clears H, N, and C flags. PV indicates parity.
     * @param val Operand for OR operation.
     */
    void or8(uint8_t val);

    /**
     * @brief Logical XOR: A = A ^ val.
     *
     * Clears H, N, and C flags. PV indicates parity.
     * @param val Operand for XOR operation.
     */
    void xor8(uint8_t val);

    /**
     * @brief Compare (CP): A - val, result is discarded.
     *
     * Sets flags like SUB, but accumulator remains unchanged.
     * Bits 3 and 5 of flags are taken from the operand (val),
     * not from the result.
     * @param val Comparison operand.
     */
    void cp8(uint8_t val);

    /**
     * @brief 8-bit increment: val + 1.
     *
     * C flag remains unchanged. PV is set on overflow from 0x7F.
     * @param val Value to increment.
     * @return Result (8 bits).
     */
    uint8_t inc8(uint8_t val);

    /**
     * @brief 8-bit decrement: val - 1.
     *
     * C flag remains unchanged. PV is set on underflow from 0x80.
     * @param val Value to decrement.
     * @return Result (8 bits).
     */
    uint8_t dec8(uint8_t val);

    ///@}

    // =========================================================================
    /// @name 16-Bit ALU Operations
    // =========================================================================
    ///@{

    /**
     * @brief 16-bit addition: a + b.
     *
     * S, Z, and PV flags are preserved. H refers to bit 11.
     * @param a First operand.
     * @param b Second operand.
     * @return Result (16 bits).
     */
    uint16_t add16(uint16_t a, uint16_t b);

    /**
     * @brief 16-bit addition with carry: a + b + carry.
     *
     * All flags are affected. PV indicates 16-bit overflow.
     * @param a First operand.
     * @param b Second operand.
     * @return Result (16 bits).
     */
    uint16_t adc16(uint16_t a, uint16_t b);

    /**
     * @brief 16-bit subtraction with carry: a - b - carry.
     *
     * All flags are affected. PV indicates 16-bit overflow.
     * @param a First operand (minuend).
     * @param b Second operand (subtrahend).
     * @return Result (16 bits).
     */
    uint16_t sbc16(uint16_t a, uint16_t b);

    ///@}

    // =========================================================================
    /// @name Rotation and Shift Instructions
    /// @brief Implementation of CB-prefix operations.
    ///        All functions set S, Z, PV (parity), bit 3, bit 5,
    ///        and C (shifted-out bit). H and N are cleared.
    // =========================================================================
    ///@{

    /**
     * @brief Rotate Left Circular - bit 7 rotates to bit 0 and C flag.
     * @param val Input value.
     * @return Rotated result.
     */
    uint8_t rlc(uint8_t val);

    /**
     * @brief Rotate Right Circular - bit 0 rotates to bit 7 and C flag.
     * @param val Input value.
     * @return Rotated result.
     */
    uint8_t rrc(uint8_t val);

    /**
     * @brief Rotate Left through Carry - bit 7 → C flag, old C flag → bit 0.
     * @param val Input value.
     * @return Rotated result.
     */
    uint8_t rl(uint8_t val);

    /**
     * @brief Rotate Right through Carry - bit 0 → C flag, old C flag → bit 7.
     * @param val Input value.
     * @return Rotated result.
     */
    uint8_t rr(uint8_t val);

    /**
     * @brief Shift Left Arithmetic - bit 7 → C flag, bit 0 becomes 0.
     * @param val Input value.
     * @return Shifted result.
     */
    uint8_t sla(uint8_t val);

    /**
     * @brief Shift Right Arithmetic - bit 0 → C flag, bit 7 preserved (sign).
     * @param val Input value.
     * @return Shifted result.
     */
    uint8_t sra(uint8_t val);

    /**
     * @brief Shift Left Logical (undocumented) - bit 7 → C flag, bit 0 becomes 1.
     *
     * This is an undocumented Z80 instruction, sometimes called SL1 or SLS.
     * @param val Input value.
     * @return Shifted result.
     */
    uint8_t sll(uint8_t val);

    /**
     * @brief Shift Right Logical - bit 0 → C flag, bit 7 becomes 0.
     * @param val Input value.
     * @return Shifted result.
     */
    uint8_t srl(uint8_t val);

    ///@}

    // =========================================================================
    /// @name Bit Operations
    /// @brief BIT, RES, and SET from the CB-prefix instruction set.
    // =========================================================================
    ///@{

    /**
     * @brief BIT b,val - tests the specified bit.
     *
     * Z flag is set if the bit is 0. H is set, N is cleared.
     * C remains unchanged.
     * @param bit Bit number (0-7).
     * @param val Value to test.
     * @param flags_src Source for undocumented flags 3 and 5.
     *                  For indexed BIT operations (IX+d, IY+d), the
     *                  upper byte of the effective address is passed here.
     */
    void bit_op(uint8_t bit, uint8_t val, uint8_t flags_src);

    /**
     * @brief BIT b,val - tests the specified bit (standard version).
     *
     * Uses the value itself as source for undocumented flags.
     * @param bit Bit number (0-7).
     * @param val Value to test.
     */
    void bit_op(uint8_t bit, uint8_t val);

    /**
     * @brief RES b,val - clears the specified bit to 0.
     * @param bit Bit number (0-7).
     * @param val Input value.
     * @return Result with cleared bit.
     */
    uint8_t res_op(uint8_t bit, uint8_t val);

    /**
     * @brief SET b,val - sets the specified bit to 1.
     * @param bit Bit number (0-7).
     * @param val Input value.
     * @return Result with set bit.
     */
    uint8_t set_op(uint8_t bit, uint8_t val);

    ///@}

    // =========================================================================
    /// @name Helper Functions
    // =========================================================================
    ///@{

    /**
     * @brief Calculates the parity of an 8-bit value.
     * @param val Value to check.
     * @return true for even parity (even number of set bits).
     */
    static bool parity(uint8_t val);

    ///@}

    // =========================================================================
    /// @name Instruction Decoding and Execution
    /// @brief Each function handles a group of Z80 opcodes,
    ///        organized by prefix bytes.
    // =========================================================================
    ///@{

    /**
     * @brief Main decoding - executes unprefixed opcodes (0x00-0xFF).
     *
     * Includes: 8-bit load/ALU/jump/call/return instructions,
     * 16-bit load and arithmetic instructions, I/O, exchange instructions,
     * and branching to prefix handlers (CB, DD, ED, FD).
     * @param opcode The opcode to execute.
     * @return Clock cycles consumed.
     */
    int execMain(uint8_t opcode);

    /**
     * @brief CB-prefix handler - rotation, shift, and bit operations.
     *
     * Decodes the second opcode after the CB prefix and executes the
     * corresponding operation on the selected register or (HL).
     * @return Clock cycles consumed (8 for register, 12/15 for (HL)).
     */
    int execCB();

    /**
     * @brief DD-prefix handler - IX index register operations.
     *
     * Replaces HL with IX in most instructions. Memory accesses via
     * (HL) become (IX+d) with signed 8-bit displacement.
     * Undocumented: IXH/IXL as individual 8-bit registers.
     * @return Clock cycles consumed.
     */
    int execDD();

    /**
     * @brief ED-prefix handler - extended instructions.
     *
     * Includes: IN/OUT with register, SBC/ADC HL, LD (nn)/rr, NEG,
     * RETN/RETI, IM modes, LD I,A / LD A,I / LD R,A / LD A,R,
     * RRD/RLD, and all block operations (LDI, LDIR, CPI, CPIR, etc.).
     * @return Clock cycles consumed.
     */
    int execED();

    /**
     * @brief FD-prefix handler - IY index register operations.
     *
     * Functionally identical to DD prefix, but uses IY instead of IX.
     * @return Clock cycles consumed.
     */
    int execFD();

    /**
     * @brief DDCB double-prefix handler - bit/rotation operations on (IX+d).
     *
     * Format: DD CB dd op - displacement first, then opcode.
     * For register operands (z != 6), the result is written both to memory
     * and also copied to the specified register (undocumented).
     * @return Clock cycles consumed (20 for BIT, 23 for others).
     */
    int execDDCB();

    /**
     * @brief FDCB double-prefix handler - bit/rotation operations on (IY+d).
     *
     * Functionally identical to DDCB, but uses IY instead of IX.
     * @return Clock cycles consumed (20 for BIT, 23 for others).
     */
    int execFDCB();

    ///@}

    // =========================================================================
    /// @name Condition Checking and Register Access
    // =========================================================================
    ///@{

    /**
     * @brief Checks a jump/call/return condition.
     *
     * @param cc Condition code (0=NZ, 1=Z, 2=NC, 3=C, 4=PO, 5=PE, 6=P, 7=M).
     * @return true if the condition is satisfied.
     */
    bool checkCondition(uint8_t cc);

    /**
     * @brief Reads an 8-bit register by its opcode index.
     *
     * Encoding: 0=B, 1=C, 2=D, 3=E, 4=H, 5=L, 6=(HL), 7=A.
     * @param idx Register index (0-7).
     * @return Register value (for idx=6, reads from memory at address HL).
     */
    uint8_t getReg8(uint8_t idx);

    /**
     * @brief Writes a value to an 8-bit register by its opcode index.
     * @param idx Register index (0-7, see getReg8).
     * @param val Value to write.
     */
    void setReg8(uint8_t idx, uint8_t val);

    /**
     * @brief Reads a 16-bit register pair by its opcode index (SP variant).
     *
     * Encoding: 0=BC, 1=DE, 2=HL, 3=SP.
     * @param idx Register pair index (0-3).
     * @return 16-bit register value.
     */
    uint16_t getReg16(uint8_t idx);

    /**
     * @brief Writes a value to a 16-bit register pair (SP variant).
     * @param idx Register pair index (0-3, see getReg16).
     * @param val Value to write.
     */
    void setReg16(uint8_t idx, uint16_t val);

    /**
     * @brief Reads a 16-bit register pair by its opcode index (AF variant).
     *
     * Encoding: 0=BC, 1=DE, 2=HL, 3=AF (instead of SP).
     * Used for PUSH/POP instructions.
     * @param idx Register pair index (0-3).
     * @return 16-bit register value.
     */
    uint16_t getReg16AF(uint8_t idx);

    /**
     * @brief Writes a value to a 16-bit register pair (AF variant).
     * @param idx Register pair index (0-3, see getReg16AF).
     * @param val Value to write.
     */
    void setReg16AF(uint8_t idx, uint16_t val);

    ///@}
};

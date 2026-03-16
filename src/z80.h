// z80.h - Z80 CPU Emulation for Robotron A5120
// Accurate Z80 instruction set implementation
#pragma once
#include <cstdint>
#include <functional>

class Z80 {
public:
    // Register set
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t F, A;
            #else
            uint8_t A, F;
            #endif
        };
        uint16_t AF;
    };
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t C, B;
            #else
            uint8_t B, C;
            #endif
        };
        uint16_t BC;
    };
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t E, D;
            #else
            uint8_t D, E;
            #endif
        };
        uint16_t DE;
    };
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t L, H;
            #else
            uint8_t H, L;
            #endif
        };
        uint16_t HL;
    };

    // Alternate register set
    uint16_t AF_, BC_, DE_, HL_;

    // Index registers
    uint16_t IX, IY;

    // Program counter & stack pointer
    uint16_t PC, SP;

    // Interrupt & refresh registers
    uint8_t I, R;

    // Interrupt state
    bool IFF1, IFF2;
    uint8_t IM; // Interrupt mode (0, 1, 2)
    bool halted;

    // Cycle counter
    uint64_t cycles;

    // Flags
    static constexpr uint8_t FLAG_C  = 0x01;
    static constexpr uint8_t FLAG_N  = 0x02;
    static constexpr uint8_t FLAG_PV = 0x04;
    static constexpr uint8_t FLAG_H  = 0x10;
    static constexpr uint8_t FLAG_Z  = 0x40;
    static constexpr uint8_t FLAG_S  = 0x80;
    // Undocumented flags (bits 3 and 5)
    static constexpr uint8_t FLAG_3  = 0x08;
    static constexpr uint8_t FLAG_5  = 0x20;

    // Callbacks for memory and I/O
    std::function<uint8_t(uint16_t)> readByte;
    std::function<void(uint16_t, uint8_t)> writeByte;
    std::function<uint8_t(uint16_t)> readPort;
    std::function<void(uint16_t, uint8_t)> writePort;

    Z80();
    void reset();
    int step();          // Execute one instruction, return cycles used
    void interrupt(uint8_t vector); // Trigger maskable interrupt
    void nmi();          // Trigger non-maskable interrupt

    void push(uint16_t val);
    uint16_t pop();

private:
    // Internal helpers
    uint16_t readWord(uint16_t addr);
    void writeWord(uint16_t addr, uint16_t val);
    uint8_t fetchByte();
    uint16_t fetchWord();

    // ALU operations
    uint8_t add8(uint8_t a, uint8_t b);
    uint8_t adc8(uint8_t a, uint8_t b);
    uint8_t sub8(uint8_t a, uint8_t b);
    uint8_t sbc8(uint8_t a, uint8_t b);
    void and8(uint8_t val);
    void or8(uint8_t val);
    void xor8(uint8_t val);
    void cp8(uint8_t val);
    uint8_t inc8(uint8_t val);
    uint8_t dec8(uint8_t val);
    uint16_t add16(uint16_t a, uint16_t b);
    uint16_t adc16(uint16_t a, uint16_t b);
    uint16_t sbc16(uint16_t a, uint16_t b);

    // Rotation/shift operations
    uint8_t rlc(uint8_t val);
    uint8_t rrc(uint8_t val);
    uint8_t rl(uint8_t val);
    uint8_t rr(uint8_t val);
    uint8_t sla(uint8_t val);
    uint8_t sra(uint8_t val);
    uint8_t sll(uint8_t val); // Undocumented
    uint8_t srl(uint8_t val);

    // Bit operations
    void bit_op(uint8_t bit, uint8_t val);
    uint8_t res_op(uint8_t bit, uint8_t val);
    uint8_t set_op(uint8_t bit, uint8_t val);

    // Parity lookup
    static bool parity(uint8_t val);

    // Instruction execution
    int execMain(uint8_t opcode);
    int execCB();
    int execDD();
    int execED();
    int execFD();
    int execDDCB();
    int execFDCB();

    // Condition check
    bool checkCondition(uint8_t cc);

    // Helper to get/set register by index
    uint8_t getReg8(uint8_t idx);
    void setReg8(uint8_t idx, uint8_t val);
    uint16_t getReg16(uint8_t idx);
    void setReg16(uint8_t idx, uint16_t val);
    uint16_t getReg16AF(uint8_t idx);
    void setReg16AF(uint8_t idx, uint16_t val);
};

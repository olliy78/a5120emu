// Hülle für MAMEs z8ops.hxx (BSD-3, unverändert heruntergeladen) als Test-ORAKEL des
// Z8-Kerns (AP P19c).  Kein MAME-Code steht hier: nur die Umgebung, die z8ops.hxx erwartet
// (Registerdatei, Speicher, Stapel), nachgebildet wie MAMEs z8.cpp sie bereitstellt
// (Registerpaar = n, n+1; Ex = Arbeitsregister; Stapel intern über SPL bzw. extern).
#pragma once
#include <cstdint>
#include <functional>

#define BIT(x, n) (((x) >> (n)) & 1)
#define LOGMASKED(...) do {} while (0)

enum endianness_t { ENDIANNESS_LITTLE, ENDIANNESS_BIG };
enum { AS_PROGRAM = 0, AS_DATA = 1, AS_IO = 2 };

struct z8_space_info { int nr; int spacenum() const { return nr; } };
struct z8_mem {
    int nr = AS_PROGRAM;
    std::function<uint8_t(uint16_t)> rd;
    std::function<void(uint16_t, uint8_t)> wr;
    z8_space_info space() const { return {nr}; }
    uint8_t read_byte(uint16_t a) { return rd(a); }
    void write_byte(uint16_t a, uint8_t v) { wr(a, v); }
};
template <int A, int W, int S, endianness_t E> struct memory_access {
    using specific = z8_mem;
    using cache = z8_mem;
};

/// Was der Prüfstand anschliesst.
struct z8_oracle_umgebung {
    std::function<uint8_t(uint8_t)> regLesen;
    std::function<void(uint8_t, uint8_t)> regSchreiben;
};

class z8_device {
public:
    z8_device();
    z8_oracle_umgebung umg;
    z8_mem m_cache, m_program, m_data;
    uint32_t m_rom_size = 0;
    uint16_t m_pc = 0, m_ppc = 0;
    union { uint16_t w; struct { uint8_t l, h; } b; } m_sp{};   // Wirt little-endian
    uint8_t m_rp = 0, m_flags = 0, m_imr = 0, m_irq = 0;
    bool m_irq_initialized = false;
    bool intern = true;          ///< P01M.2

    /// Einen Befehl ausführen; Rückgabe = MAMEs Takte (Tabelle + Zuschlag).
    int schritt();

    #define INSTRUCTION(inst) void inst(uint8_t opcode, int *cycles);
    #include "z8_decl.inc"
    #undef INSTRUCTION

    typedef void (z8_device::*z8_opcode_func)(uint8_t opcode, int *cycles);
    struct z8_opcode_map { z8_opcode_func function; int execution_cycles; int pipeline_cycles; };
    static const z8_opcode_map Z8601_OPCODE_MAP[256];

    // ── Umgebung wie MAMEs z8.cpp ──────────────────────────────────────────
    uint16_t mask_external_address(uint16_t a) { return a; }
    uint8_t fetch() { return m_cache.read_byte(m_pc++); }
    uint16_t fetch_word() { uint16_t d = uint16_t(fetch() << 8); d |= fetch(); return d; }
    uint8_t register_read(uint8_t a) { return umg.regLesen(a); }
    void register_write(uint8_t a, uint8_t v) { umg.regSchreiben(a, v); }
    uint16_t register_pair_read(uint8_t a) { return uint16_t(register_read(a) << 8 | register_read(uint8_t(a + 1))); }
    void register_pair_write(uint8_t a, uint16_t v) { register_write(a, uint8_t(v >> 8)); register_write(uint8_t(a + 1), uint8_t(v)); }
    uint8_t get_working_register(int o) const { return uint8_t((m_rp & 0xF0) | (o & 0x0F)); }
    uint8_t get_register(uint8_t o) const { return (o & 0xF0) == 0xE0 ? get_working_register(o & 0x0F) : o; }
    uint8_t get_intermediate_register(int o) { return register_read(get_register(uint8_t(o))); }
    void stack_push_byte(uint8_t v);
    void stack_push_word(uint16_t v);
    uint8_t stack_pop_byte();
    uint16_t stack_pop_word();
    void set_flag(uint8_t f, int s) { m_flags = s ? uint8_t(m_flags | f) : uint8_t(m_flags & ~f); }
    void flags_write(uint8_t d) { m_flags = d; }
    void rp_write(uint8_t d) { m_rp = d; }

    void clear(uint8_t dst);
    void load(uint8_t dst, uint8_t src);
    void load_from_memory(z8_mem& space);
    void load_to_memory(z8_mem& space);
    void load_from_memory_autoinc(z8_mem& space);
    void load_to_memory_autoinc(z8_mem& space);
    void pop(uint8_t dst);
    void push(uint8_t src);
    void add_carry(uint8_t dst, uint8_t src);
    void add(uint8_t dst, uint8_t src);
    void compare(uint8_t dst, uint8_t src);
    void decimal_adjust(uint8_t dst);
    void decrement(uint8_t dst);
    void decrement_word(uint8_t dst);
    void increment(uint8_t dst);
    void increment_word(uint8_t dst);
    void subtract_carry(uint8_t dst, uint8_t src);
    void subtract(uint8_t dst, uint8_t src);
    void _and(uint8_t dst, uint8_t src);
    void complement(uint8_t dst);
    void _or(uint8_t dst, uint8_t src);
    void _xor(uint8_t dst, uint8_t src);
    void call(uint16_t dst);
    void jump(uint16_t dst);
    bool check_condition_code(int cc);
    void test_complement_under_mask(uint8_t dst, uint8_t src);
    void test_under_mask(uint8_t dst, uint8_t src);
    void rotate_left(uint8_t dst);
    void rotate_left_carry(uint8_t dst);
    void rotate_right(uint8_t dst);
    void rotate_right_carry(uint8_t dst);
    void shift_right_arithmetic(uint8_t dst);
    void swap(uint8_t dst);
};

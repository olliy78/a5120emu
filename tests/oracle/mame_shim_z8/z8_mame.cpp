// Übersetzt MAMEs z8ops.hxx (unverändert) in der Hülle z8_shim.h; Opcodetabelle aus
// MAMEs z8.cpp (beim Konfigurieren herausgeschnitten, z8_table.inc).  Nur Prüfstand.
#include "z8_shim.h"

#define Z8_IMR_ENABLE 0x80
#define Z8_FLAGS_F1 0x01
#define Z8_FLAGS_F2 0x02
#define Z8_FLAGS_H  0x04
#define Z8_FLAGS_D  0x08
#define Z8_FLAGS_V  0x10
#define Z8_FLAGS_S  0x20
#define Z8_FLAGS_Z  0x40
#define Z8_FLAGS_C  0x80
enum { CC_F = 0, CC_LT, CC_LE, CC_ULE, CC_OV, CC_MI, CC_Z, CC_C,
       CC_T, CC_GE, CC_GT, CC_UGT, CC_NOV, CC_PL, CC_NZ, CC_NC };
#define set_flag_h(state) set_flag(Z8_FLAGS_H, state);
#define set_flag_d(state) set_flag(Z8_FLAGS_D, state);
#define set_flag_v(state) set_flag(Z8_FLAGS_V, state);
#define set_flag_s(state) set_flag(Z8_FLAGS_S, state);
#define set_flag_z(state) set_flag(Z8_FLAGS_Z, state);
#define set_flag_c(state) set_flag(Z8_FLAGS_C, state);

z8_device::z8_device() { m_program.nr = AS_PROGRAM; m_cache.nr = AS_PROGRAM; m_data.nr = AS_DATA; }

void z8_device::stack_push_byte(uint8_t v) {
    if (intern) { m_sp.b.l = uint8_t(m_sp.b.l - 1); register_write(m_sp.b.l, v); }
    else { m_sp.w = uint16_t(m_sp.w - 1); m_data.write_byte(m_sp.w, v); }
}
void z8_device::stack_push_word(uint16_t v) {   // wie MAME: Wort an SP-2 (hoch zuerst im Speicher)
    if (intern) { m_sp.b.l = uint8_t(m_sp.b.l - 2); register_pair_write(m_sp.b.l, v); }
    else { m_sp.w = uint16_t(m_sp.w - 2); m_data.write_byte(m_sp.w, uint8_t(v >> 8)); m_data.write_byte(uint16_t(m_sp.w + 1), uint8_t(v)); }
}
uint8_t z8_device::stack_pop_byte() {
    if (intern) { const uint8_t v = register_read(m_sp.b.l); m_sp.b.l = uint8_t(m_sp.b.l + 1); return v; }
    const uint8_t v = m_data.read_byte(m_sp.w); m_sp.w = uint16_t(m_sp.w + 1); return v;
}
uint16_t z8_device::stack_pop_word() {
    if (intern) { const uint16_t v = register_pair_read(m_sp.b.l); m_sp.b.l = uint8_t(m_sp.b.l + 2); return v; }
    const uint16_t v = uint16_t(m_data.read_byte(m_sp.w) << 8 | m_data.read_byte(uint16_t(m_sp.w + 1)));
    m_sp.w = uint16_t(m_sp.w + 2); return v;
}

#define INSTRUCTION(mnemonic) void z8_device::mnemonic(uint8_t opcode, int *cycles)
INSTRUCTION( illegal ) { (void)opcode; (void)cycles; }
#include "z8ops.hxx"
#include "z8_table.inc"

int z8_device::schritt() {
    m_ppc = m_pc;
    const uint8_t op = fetch();
    int c = Z8601_OPCODE_MAP[op].execution_cycles;
    (this->*(Z8601_OPCODE_MAP[op].function))(op, &c);
    return c;
}

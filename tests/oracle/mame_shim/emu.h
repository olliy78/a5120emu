// Minimaler Ersatz für MAMEs emu.h — gerade genug, damit MAMEs z8000.cpp/z8000.h
// (BSD-3, unverändert heruntergeladen) als Test-ORAKEL übersetzt werden kann.
// Kein MAME-Code steht hier; nur Namen, die jene Dateien erwarten.  Speicher und
// E/A gehen an einen `mame_oracle_bus` des Prüfstands (tests/oracle/).
#pragma once
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using u8 = uint8_t;   using u16 = uint16_t;  using u32 = uint32_t;  using u64 = uint64_t;
using s8 = int8_t;    using s16 = int16_t;   using s32 = int32_t;   using s64 = int64_t;
using offs_t = uint32_t;

#define ATTR_COLD
#define NAME(x) x, #x
#define BIT(x, n) (((x) >> (n)) & 1)

enum endianness_t { ENDIANNESS_LITTLE, ENDIANNESS_BIG };
enum { CLEAR_LINE = 0, ASSERT_LINE = 1 };
enum { INPUT_LINE_NMI = 32 };
enum { AS_PROGRAM = 0, AS_DATA = 1, AS_IO = 2, AS_OPCODES = 3 };
enum { STATE_GENPC = -1, STATE_GENPCBASE = -2, STATE_GENFLAGS = -3 };

inline u16 swapendian_int16(u16 v) { return u16((v << 8) | (v >> 8)); }

inline std::string string_format(const char* fmt, ...) {
    char b[256];
    va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    return b;
}

namespace util { class disasm_interface { public: virtual ~disasm_interface() = default; }; }

/// Anschluss des Prüfstands: Adressraum (AS_*, 4 = Stapel, 5 = Spezial-E/A), Wortadresse.
struct mame_oracle_bus {
    std::function<u16(int space, offs_t addr)> readWord;
    std::function<void(int space, offs_t addr, u16 value, u16 mask)> writeWord;
};

class address_space {
public:
    mame_oracle_bus* bus = nullptr;
    int num = 0;
    offs_t addrmask() const { return 0x7FFFFF; }
    template <class T> void cache(T& t) { t.sp = this; }
    template <class T> void specific(T& t) { t.sp = this; }
};

template <int A, int W, int S, endianness_t E>
struct memory_access {
    struct specific {
        address_space* sp = nullptr;
        address_space& space() { return *sp; }
        u16 read_word(offs_t a) { return sp->bus->readWord(sp->num, a & ~offs_t(1)); }
        u16 read_word(offs_t a, u16) { return read_word(a); }
        u8 read_byte(offs_t a) { u16 w = read_word(a); return (a & 1) ? u8(w) : u8(w >> 8); }
        void write_word(offs_t a, u16 v, u16 m = 0xFFFF) { sp->bus->writeWord(sp->num, a & ~offs_t(1), v, m); }
        void write_byte(offs_t a, u8 v) { write_word(a, u16(v << 8 | v), (a & 1) ? 0x00FF : 0xFF00); }
    };
    using cache = specific;
};

struct address_space_config {
    address_space_config(const char*, endianness_t, int, int, int) {}
};

class device_t { public: virtual ~device_t() = default; };
class machine_config {};
using device_type = int;
#define DECLARE_DEVICE_TYPE(T, C) extern device_type T;
#define DEFINE_DEVICE_TYPE(T, C, s, n) device_type T = 0;

struct devcb_read16 {
    u16 dflt = 0;
    std::function<u16(offs_t)> fn;
    devcb_read16() = default;
    devcb_read16(device_t&, u16 d) : dflt(d) {}
    u16 operator()(offs_t a = 0) { return fn ? fn(a) : dflt; }
    int bind() { return 0; }
    template <unsigned N> struct array : std::array<devcb_read16, N> {
        array(device_t&, u16 d) { for (auto& e : *this) e.dflt = d; }
    };
};

struct devcb_write_line {
    std::function<void(int)> fn;
    explicit devcb_write_line(device_t&) {}
    void operator()(int s) { if (fn) fn(s); }
    int bind() { return 0; }
};

struct device_state_entry {
    int index() const { return 0; }
};

struct state_proxy {
    template <class... T> state_proxy& mask(T&&...) { return *this; }
    state_proxy& formatstr(const char*) { return *this; }
    state_proxy& noshow() { return *this; }
};

class device_memory_interface {
public:
    using space_config_vector = std::vector<std::pair<int, const address_space_config*>>;
};

class cpu_device : public device_t, public device_memory_interface {
public:
    cpu_device(const machine_config&, device_type, const char*, device_t*, u32) {}
    mame_oracle_bus* oracleBus = nullptr;

protected:
    virtual void device_start() {}
    virtual void device_reset() {}
    virtual bool cpu_is_interruptible() const { return false; }
    virtual u32 execute_min_cycles() const noexcept { return 1; }
    virtual u32 execute_max_cycles() const noexcept { return 1; }
    virtual u32 execute_default_irq_vector(int) const noexcept { return 0; }
    virtual bool execute_input_edge_triggered(int) const noexcept { return false; }
    virtual void execute_run() {}
    virtual void execute_set_input(int, int) {}
    virtual space_config_vector memory_space_config() const = 0;
    virtual void state_string_export(const device_state_entry&, std::string&) const {}
    virtual std::unique_ptr<util::disasm_interface> create_disassembler() { return nullptr; }

    bool has_configured_map(int) const { return false; }
    bool has_space(int) const { return true; }   // alle Räume getrennt melden: Status sichtbar
    address_space& space(int n = 0) { spaces_[n].num = n; spaces_[n].bus = oracleBus; return spaces_[n]; }
    template <class T> state_proxy state_add(int, const char*, T&) { return {}; }
    template <class T> void save_item(T&&, const char*) {}
    void set_icountptr(int&) {}
    void standard_irq_callback(int, offs_t) {}
    void debugger_instruction_hook(offs_t) {}
    void debugger_wait_hook() {}
    template <class... A> void logerror(const char*, A&&...) {}

private:
    address_space spaces_[8];
};

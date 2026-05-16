#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef void* K1520Handle;

typedef enum {
    K1520_MACHINE_A5120  = 0,
    K1520_MACHINE_PRG710 = 1,
    K1520_MACHINE_K8915  = 2,
} K1520MachineType;

typedef struct {
    uint16_t pc, sp, af, bc, de, hl, ix, iy;
    uint8_t  i, r, im;
    bool     iff1, iff2, halted;
} K1520CpuState;

typedef enum {
    K1520_SERIAL_DFU     = 0,
    K1520_SERIAL_PRINTER = 1,
} K1520SerialPort;

typedef void (*K1520SerialCallback)(void* ctx, uint8_t byte);

/* ─── Lifecycle ──────────────────────────────────────────────────────────── */
K1520Handle k1520_create(K1520MachineType type);
void        k1520_destroy(K1520Handle h);
void        k1520_reset(K1520Handle h);
void        k1520_power_on(K1520Handle h);

/* ─── Execution ──────────────────────────────────────────────────────────── */
int  k1520_run(K1520Handle h, int max_cycles);
void k1520_stop(K1520Handle h);

/* ─── Framebuffer ────────────────────────────────────────────────────────── */
const uint8_t* k1520_framebuffer(K1520Handle h);
int            k1520_fb_width(K1520Handle h);
int            k1520_fb_height(K1520Handle h);
bool           k1520_fb_dirty(K1520Handle h);
void           k1520_fb_clear_dirty(K1520Handle h);

/* ─── Console (CLI) mode ─────────────────────────────────────────────────── */
void k1520_set_console_mode(K1520Handle h, bool enable);
bool k1520_console_poll(K1520Handle h, int* x, int* y, char* ch);

/* ─── Keyboard ───────────────────────────────────────────────────────────── */
void k1520_key_press(K1520Handle h, uint32_t keycode, bool shift, bool ctrl);
void k1520_key_release(K1520Handle h, uint32_t keycode);
void k1520_console_key(K1520Handle h, char c);

/* ─── Disk drives ────────────────────────────────────────────────────────── */
bool k1520_mount_disk(K1520Handle h, int drive,
                      const char* image_path,
                      const char* format_name,
                      bool write_protect);
bool k1520_unmount_disk(K1520Handle h, int drive);
bool k1520_disk_active(K1520Handle h, int drive);
bool k1520_disk_write_protected(K1520Handle h, int drive);
void k1520_set_write_protect(K1520Handle h, int drive, bool wp);

/* ─── Serial ports ───────────────────────────────────────────────────────── */
void k1520_serial_set_rx_cb(K1520Handle h, K1520SerialPort port,
                             K1520SerialCallback cb, void* ctx);
void k1520_serial_send(K1520Handle h, K1520SerialPort port, uint8_t byte);

/* ─── Debug ──────────────────────────────────────────────────────────────── */
uint8_t     k1520_mem_read(K1520Handle h, uint16_t addr);
void        k1520_mem_write(K1520Handle h, uint16_t addr, uint8_t data);
uint8_t     k1520_io_read(K1520Handle h, uint8_t port);
const char* k1520_last_error(K1520Handle h);
const char* k1520_version(void);

#ifdef __cplusplus
}
#endif

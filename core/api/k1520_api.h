#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "core/api/k1520_export.h"
#include "core/api/k1520_sync_api.h"   /* K1520_API — Ausfuhrkennzeichnung (Windows!) */

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
K1520_API K1520Handle k1520_create(K1520MachineType type);

/**
 * Create a machine with an explicit drive-bay configuration.
 *
 * @param drive0..3  DriveProfile name per K5122 slot — the real drive names:
 *                   "K5601" (5.25" DS 80 tracks, 800K, default), "K5600.10"
 *                   (5.25" SS 40 tracks, 200K), "K5600.20" (5.25" SS 80 tracks,
 *                   400K), "MF3200" (8" SS 77 tracks, FM only, 300K), "MF6400"
 *                   (8" SS 77 tracks, FM+MFM, 600K).  The special name "none"
 *                   marks an EMPTY slot (no drive wired: mounting/creating a disk
 *                   there is refused).  NULL or "" keeps the default (K5601);
 *                   unknown names fall back to K5601.  Former technical names
 *                   (e.g. "mf3200_8_ss77") still resolve as aliases.
 * @return handle, or NULL on error.  Equivalent to k1520_create() when all four
 *         names are NULL/"".
 */
K1520_API K1520Handle k1520_create_configured(K1520MachineType type,
                                              const char* drive0, const char* drive1,
                                              const char* drive2, const char* drive3);

/**
 * @brief Reason the last k1520_create*() returned NULL ("" if none).
 *
 * A startup abort (e.g. missing/broken disk format catalog `formats.yaml`) yields
 * no handle, so the message cannot be fetched via k1520_last_error(). The returned
 * pointer stays valid until the next k1520_create*() call.
 */
K1520_API const char* k1520_last_init_error(void);

K1520_API void        k1520_destroy(K1520Handle h);
K1520_API void        k1520_reset(K1520Handle h);
K1520_API void        k1520_power_on(K1520Handle h);

/* ─── Execution ──────────────────────────────────────────────────────────── */
K1520_API int  k1520_run(K1520Handle h, int max_cycles);
K1520_API void k1520_stop(K1520Handle h);

/* ─── Framebuffer ────────────────────────────────────────────────────────── */
K1520_API const uint8_t* k1520_framebuffer(K1520Handle h);
K1520_API int            k1520_fb_width(K1520Handle h);
K1520_API int            k1520_fb_height(K1520Handle h);
K1520_API bool           k1520_fb_dirty(K1520Handle h);
K1520_API void           k1520_fb_clear_dirty(K1520Handle h);

/* ─── Console (CLI) mode ─────────────────────────────────────────────────── */
K1520_API void k1520_set_console_mode(K1520Handle h, bool enable);
K1520_API bool k1520_console_poll(K1520Handle h, int* x, int* y, char* ch);

/* ─── Keyboard ───────────────────────────────────────────────────────────── */
K1520_API void k1520_key_press(K1520Handle h, uint32_t keycode, bool shift, bool ctrl);
K1520_API void k1520_key_release(K1520Handle h, uint32_t keycode);
K1520_API void k1520_console_key(K1520Handle h, char c);

/* ─── Disk drives ────────────────────────────────────────────────────────── */
/** @brief Mount a disk image into a drive slot. */
K1520_API bool k1520_mount_disk(K1520Handle h, int drive,
                                const char* image_path,
                                const char* format_name,
                                bool write_protect);
/**
 * @brief Create a NEW disk and mount it.
 *
 * @p format_name NULL or "" → a genuinely BLANK (unformatted) disk in the geometry of
 * the *drive* (K5601 80×2, K5600.10 40×1, …), ready to be formatted by the guest OS —
 * including foreign systems such as UDOS that append data behind the data CRC.  A `.img`
 * target is rejected in that case (a raw sector image cannot express "unformatted");
 * use `.hfe` or `.dmk`.
 *
 * @p format_name set → a PRE-FORMATTED disk per catalog format (real IDAM/DATA/CRC,
 * 0xE5 data); `.img` is allowed then.
 *
 * Overwrites an existing file.  Returns false on error (see k1520_last_error).
 */
K1520_API bool k1520_create_disk(K1520Handle h, int drive,
                                 const char* image_path,
                                 const char* format_name,
                                 bool write_protect);
/**
 * @brief Save the mounted disk under a new name/container and re-bind to it.
 *
 * Container follows the extension (`.img` / `.hfe` / `.dmk`).  From then on all further
 * writes go (delayed) into the new file.  @p format_name is only needed for `.img`
 * (the other containers are self-describing) and is validated against the medium.
 */
K1520_API bool k1520_save_disk_as(K1520Handle h, int drive,
                                  const char* image_path,
                                  const char* format_name);
/** @brief True if the mounted disk may be saved as a raw sector image (.img). */
K1520_API bool k1520_disk_raw_compatible(K1520Handle h, int drive);
/** @brief Currently bound image file of a slot ("" = memory only / empty drive). */
K1520_API const char* k1520_disk_path(K1520Handle h, int drive);
/** @brief Container of the bound file ("img" | "hfe" | "dmk"; "" = none). */
K1520_API const char* k1520_disk_container(K1520Handle h, int drive);
/**
 * @brief Operating notices about how the mounted disk had to be adapted to the drive.
 *
 * One line per restriction, separated by '\n'; "" = the disk fits as it is.  This is
 * NOT an error — the disk is mounted and readable, just translated (track pitch /
 * single-sided drive).  The GUI shows the lines in the drive box under the file name.
 */
K1520_API const char* k1520_disk_notice(K1520Handle h, int drive);

/**
 * @brief **Physische Diskette** aus einem echten Laufwerk am Greaseweazle anmelden.
 *
 * @p sync kommt aus @ref k1520s_create (core/api/k1520_sync_api.h) und wird von einem
 * fremden Arbeitsfaden bedient.  Es wird beim Anmelden **nichts gelesen** — Spuren
 * kommen einzeln, sobald der Gast sie anfasst.
 *
 * Ein Handle laesst sich nur EINMAL anmelden.
 *
 * @see doc/design/14_physische_diskette.md
 */
K1520_API bool k1520_mount_physical(K1520Handle h, int drive, K1520Sync sync,
                                    bool write_protect);
/** @brief Write pending changes of all drives to their files immediately. */
K1520_API bool k1520_flush_disks(K1520Handle h);
/** @brief Unmount disk image from a drive slot. */
K1520_API bool k1520_unmount_disk(K1520Handle h, int drive);

/* ─── Disk formats per drive ─────────────────────────────────────────────────
 * The built-in disk formats that geometrically fit the drive configured in a
 * slot (for the GUI format selection).  The drive-type default is index 0.
 * Returned name pointers stay valid until the next call on the same thread. */
/** @brief Number of built-in formats compatible with the drive in @p drive. */
K1520_API int         k1520_drive_format_count(K1520Handle h, int drive);
/** @brief Name of the @p index-th compatible format (NULL if out of range). */
K1520_API const char* k1520_drive_format_name(K1520Handle h, int drive, int index);
/** @brief Drive-type default format name for @p drive (what empty-create uses). */
K1520_API const char* k1520_drive_default_format(K1520Handle h, int drive);
/** @brief Human-readable description of a catalog format ("" if unknown). */
K1520_API const char* k1520_format_description(K1520Handle h, const char* name);
/** @brief Colon-separated list of the loaded formats.yaml file(s) — diagnostics. */
K1520_API const char* k1520_formats_source(K1520Handle h);
/** @brief Return true if a disk image is mounted in the drive. */
K1520_API bool k1520_disk_active(K1520Handle h, int drive);
/** @brief Return true if mounted image is write protected. */
K1520_API bool k1520_disk_write_protected(K1520Handle h, int drive);
/** @brief Return true while the drive LED should be lit (drive selected or motor on). */
K1520_API bool k1520_disk_led(K1520Handle h, int drive);
/** @brief Return true while the drive's spindle motor is running (/LCK, port 0x18). */
K1520_API bool k1520_disk_motor(K1520Handle h, int drive);
/** @brief Return true while the read/write head is loaded (/HL, ctrl port A bit6). */
K1520_API bool k1520_head_loaded(K1520Handle h);
/** @brief Update mounted image write-protect state. */
K1520_API void k1520_set_write_protect(K1520Handle h, int drive, bool wp);

/* ─── Serial ports ───────────────────────────────────────────────────────── */
K1520_API void k1520_serial_set_rx_cb(K1520Handle h, K1520SerialPort port,
                                       K1520SerialCallback cb, void* ctx);
K1520_API void k1520_serial_send(K1520Handle h, K1520SerialPort port, uint8_t byte);

/* ─── Debug ──────────────────────────────────────────────────────────────── */
/** @brief Read memory through the machine bus for diagnostics. */
K1520_API uint8_t     k1520_mem_read(K1520Handle h, uint16_t addr);
/** @brief Write memory through the machine bus for diagnostics. */
K1520_API void        k1520_mem_write(K1520Handle h, uint16_t addr, uint8_t data);
/** @brief Read I/O port through the machine bus for diagnostics. */
K1520_API uint8_t     k1520_io_read(K1520Handle h, uint8_t port);
K1520_API const char* k1520_last_error(K1520Handle h);
K1520_API const char* k1520_version(void);

#ifdef __cplusplus
}
#endif

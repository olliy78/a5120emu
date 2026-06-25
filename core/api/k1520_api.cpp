#include "k1520_api.h"
#include "core/machines/a5120/a5120.h"
#include "core/logger.h"
#include <cstring>
#include <memory>
#include <ctime>
#include <cstdio>

#define VERSION "0.1.0"

static A5120Machine* toA5120(K1520Handle h) {
    return static_cast<A5120Machine*>(h);
}

// Helper to create and set up log file on first machine creation
static void setup_logging() {
    static bool logging_initialized_ = false;
    if (logging_initialized_) return;
    logging_initialized_ = true;

#if LOG_LEVEL > 0
    // Generate log filename with timestamp: k1520_YYYYMMDD_HHMMSS.log
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    char log_path[256];
    std::strftime(log_path, sizeof(log_path), "k1520_%Y%m%d_%H%M%S.log", tm);
    
    // Try to set output file; if it fails, keep stderr
    k1520::logging::Logger::instance().setOutputFile(log_path);
#endif
}

extern "C" {

K1520Handle k1520_create(K1520MachineType type) {
    if (type != K1520_MACHINE_A5120) return nullptr;  // only A5120 for now
    
    setup_logging();
    
    try {
        return new A5120Machine();
    } catch (...) {
        return nullptr;
    }
}

K1520Handle k1520_create_configured(K1520MachineType type,
                                    const char* d0, const char* d1,
                                    const char* d2, const char* d3) {
    if (type != K1520_MACHINE_A5120) return nullptr;  // only A5120 for now

    setup_logging();

    try {
        A5120Machine::Config cfg;                      // Default = 4× K5601
        const char* names[4] = { d0, d1, d2, d3 };
        for (int i = 0; i < 4; ++i)
            if (names[i] && names[i][0]) cfg.drive_profiles[i] = names[i];
        return new A5120Machine(cfg);
    } catch (...) {
        return nullptr;
    }
}

void k1520_destroy(K1520Handle h) {
    delete toA5120(h);
}

void k1520_reset(K1520Handle h)     { toA5120(h)->reset(); }
void k1520_power_on(K1520Handle h)  { toA5120(h)->powerOn(); }

int  k1520_run(K1520Handle h, int max_cycles) {
    return toA5120(h)->run(max_cycles);
}

void k1520_stop(K1520Handle h) { toA5120(h)->stop(); }

const uint8_t* k1520_framebuffer(K1520Handle h) {
    return toA5120(h)->framebuffer();
}

int  k1520_fb_width(K1520Handle h)  { return toA5120(h)->fbWidth(); }
int  k1520_fb_height(K1520Handle h) { return toA5120(h)->fbHeight(); }

bool k1520_fb_dirty(K1520Handle h) {
    return toA5120(h)->fbDirty();
}

void k1520_fb_clear_dirty(K1520Handle h) {
    toA5120(h)->fbClearDirty();
}

void k1520_set_console_mode(K1520Handle h, bool enable) {
    toA5120(h)->setConsoleMode(enable);
}

bool k1520_console_poll(K1520Handle h, int* x, int* y, char* ch) {
    int cx, cy; char c;
    if (toA5120(h)->consolePoll(cx, cy, c)) {
        if (x)  *x  = cx;
        if (y)  *y  = cy;
        if (ch) *ch = c;
        return true;
    }
    return false;
}

void k1520_key_press(K1520Handle h, uint32_t kc, bool shift, bool ctrl) {
    toA5120(h)->keyPress(kc, shift, ctrl);
}

void k1520_key_release(K1520Handle h, uint32_t kc) {
    toA5120(h)->keyRelease(kc);
}

void k1520_console_key(K1520Handle h, char c) {
    // Inject ASCII char as if typed (keycode = ASCII value, no modifiers)
    toA5120(h)->keyPress(static_cast<uint32_t>(c), false, false);
}

bool k1520_mount_disk(K1520Handle h, int drive,
                      const char* image_path, const char* format_name,
                      bool write_protect) {
    if (!image_path || !format_name) return false;
    return toA5120(h)->mountDisk(drive, image_path, format_name, write_protect);
}

bool k1520_unmount_disk(K1520Handle h, int drive) {
    return toA5120(h)->unmountDisk(drive);
}

bool k1520_disk_active(K1520Handle h, int drive) {
    return toA5120(h)->isDiskActive(drive);
}

bool k1520_disk_write_protected(K1520Handle h, int drive) {
    return toA5120(h)->isDiskWriteProtected(drive);
}

bool k1520_disk_led(K1520Handle h, int drive) {
    return toA5120(h)->isDiskLedOn(drive);
}

void k1520_set_write_protect(K1520Handle h, int drive, bool wp) {
    toA5120(h)->setDiskWriteProtect(drive, wp);
}

void k1520_serial_set_rx_cb(K1520Handle h, K1520SerialPort port,
                              K1520SerialCallback cb, void* ctx) {
    auto m = toA5120(h);
    if (port == K1520_SERIAL_DFU) {
        m->setDFUECallback([cb, ctx](uint8_t b){ if (cb) cb(ctx, b); });
    }
    // Printer callback not yet wired
}

void k1520_serial_send(K1520Handle h, K1520SerialPort port, uint8_t byte) {
    if (port == K1520_SERIAL_DFU)
        toA5120(h)->dfueSend(byte);
}

uint8_t k1520_mem_read(K1520Handle h, uint16_t addr) {
    // Direct bus access for debugging (not thread-safe, call from run thread).
    return toA5120(h)->memReadDebug(addr);
}

void k1520_mem_write(K1520Handle h, uint16_t addr, uint8_t data) {
    toA5120(h)->memWriteDebug(addr, data);
}

uint8_t k1520_io_read(K1520Handle h, uint8_t port) {
    return toA5120(h)->ioReadDebug(port);
}

const char* k1520_last_error(K1520Handle h) {
    static thread_local std::string buf;
    buf = toA5120(h)->lastError();
    return buf.c_str();
}

const char* k1520_version(void) {
    return VERSION;
}

} // extern "C"

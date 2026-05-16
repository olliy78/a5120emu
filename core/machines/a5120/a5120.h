#pragma once
#include "core/bus/k1520_bus.h"
#include "core/bus/koppelbus.h"
#include "core/cards/k2526/k2526.h"
#include "core/cards/k3526/k3526.h"
#include "core/cards/k7024/k7024.h"
#include "core/cards/k8025/k8025.h"
#include "core/cards/k5122/k5122.h"
#include "core/peripherals/k7637/k7637.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include "src/z80.h"
#include <atomic>
#include <mutex>
#include <string>
#include <deque>
#include <functional>

class A5120Machine {
public:
    A5120Machine();
    ~A5120Machine() = default;

    // Lifecycle
    void powerOn();
    void reset();
    void stop() { stop_.store(true); }

    // Run up to max_cycles CPU cycles. Returns cycles actually executed.
    int  run(int max_cycles);

    // Disk management (thread-safe)
    bool mountDisk(int drive, const std::string& path,
                   const std::string& format_name, bool write_protect);
    bool unmountDisk(int drive);
    bool isDiskActive(int drive) const;
    bool isDiskWriteProtected(int drive) const;
    void setDiskWriteProtect(int drive, bool wp);

    // Keyboard (enqueued thread-safely, consumed in run())
    void keyPress(uint32_t qt_keycode, bool shift, bool ctrl);
    void keyRelease(uint32_t qt_keycode);

    // Framebuffer
    const uint8_t* framebuffer() const;
    int  fbWidth()  const { return 640; }
    int  fbHeight() const { return 288; }
    bool fbDirty()  const { return screen_.fbDirty(); }
    void fbClearDirty()   { screen_.fbClearDirty(); }

    // Console (CLI) mode
    void setConsoleMode(bool on) { screen_.setConsoleMode(on); }
    bool consolePoll(int& x, int& y, char& ch) {
        return screen_.pollTextChange(x, y, ch);
    }

    // Serial callbacks (DFÜ, printer)
    using SerialCb = std::function<void(uint8_t)>;
    void setDFUECallback(SerialCb cb);
    void setPrinterCallback(SerialCb cb);
    void dfueSend(uint8_t byte);

    // Debug
    std::string lastError() const { return last_error_; }

private:
    void wireBackplane();

    struct KeyEvent { uint32_t keycode; bool shift, ctrl, is_press; };

    K1520Bus      bus_;
    Koppelbus     koppel_;

    K2526         zre_;       // slot 4: CPU card
    K3526         ops_;       // slot 1: RAM
    K7024         screen_;    // slot 5: video
    K8025         ass_;       // slot 3: serial
    K5122         afs_;       // slot 2: floppy

    K7637         kbd_;

    Z80           cpu_;

    std::vector<DiskFormat> disk_formats_;

    std::atomic<bool>  stop_{false};

    mutable std::mutex disk_mutex_;
    mutable std::mutex key_mutex_;
    std::deque<KeyEvent> key_queue_;

    std::string last_error_;
};

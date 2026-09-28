/**
 * @file machine.h
 * @brief Maschinenneutrale Schnittstelle, über die die C-ABI eine Maschine bedient.
 *
 * Der Kern kennt mehr als eine K1520-Maschine (A5120, K8915 — `K1520MachineType`
 * in `core/api/k1520_api.h`).  `k1520_api.cpp` spricht ausschließlich über diese
 * Klasse; welche Maschine hinter dem Handle steht, entscheidet allein
 * `k1520_create*`.  Enthalten ist **genau** das, was die C-ABI braucht — nicht
 * mehr.  Was einer Maschine eigen ist (beim A5120 die ZVE2, der DMA-Beobachter,
 * der gehaltene Bus, Savestates), bleibt in der konkreten Klasse und wird von
 * Werkzeugen (`k1520dbg`, `boot_trace`) direkt über sie benutzt.
 *
 * @see doc/design/16_k8915.md §7.1
 */

#pragma once
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class K1520Machine {
public:
    virtual ~K1520Machine() = default;

    // ─── Lebenslauf ────────────────────────────────────────────────────────────
    virtual void powerOn() = 0;
    virtual void reset() = 0;
    /** @brief Bis zu @p max_cycles Takte ausführen; liefert die verbrauchten Takte. */
    virtual int  run(int max_cycles) = 0;
    /** @brief Anhalten anfordern (wirkt nach dem laufenden Befehl, fadensicher). */
    virtual void stop() = 0;

    // ─── Bild ──────────────────────────────────────────────────────────────────
    virtual const uint8_t* framebuffer() const = 0;
    virtual int  fbWidth()  const = 0;
    virtual int  fbHeight() const = 0;
    virtual bool fbDirty()  const = 0;
    virtual void fbClearDirty() = 0;
    virtual void setConsoleMode(bool on) = 0;
    virtual bool consolePoll(int& x, int& y, char& ch) = 0;

    // ─── Tastatur (fadensicher eingereiht, verbraucht in run()) ───────────────
    virtual void keyPress(uint32_t qt_keycode, bool shift, bool ctrl) = 0;
    virtual void keyRelease(uint32_t qt_keycode) = 0;
    /** @brief Anzeigen der Tastatur; Bitbelegung je Tastaturmodell (0 = keine). */
    virtual uint8_t keyboardLeds() const { return 0; }

    // ─── Disketten ─────────────────────────────────────────────────────────────
    virtual bool mountDisk(int drive, const std::string& path,
                           const std::string& format_name, bool write_protect) = 0;
    virtual bool mountDiskImage(int drive, std::unique_ptr<DiskImage> img,
                                bool write_protect) = 0;
    virtual bool createDisk(int drive, const std::string& path,
                            const std::string& format_name, bool write_protect) = 0;
    virtual bool saveDiskAs(int drive, const std::string& path,
                            const std::string& format_name) = 0;
    virtual bool unmountDisk(int drive) = 0;
    virtual bool flushDisks() = 0;

    virtual bool isDiskRawCompatible(int drive) const = 0;
    virtual std::string diskPath(int drive) const = 0;
    virtual std::string diskContainer(int drive) const = 0;
    virtual std::string diskNotice(int drive) const = 0;
    virtual std::string detectedFormatName(int drive) const = 0;
    virtual std::string defaultFormatName(int drive) const = 0;
    virtual std::vector<std::string> compatibleFormats(int drive) const = 0;
    virtual std::string formatDescription(const std::string& format_name) const = 0;
    virtual const FormatCatalog& formatCatalog() const = 0;

    virtual bool isDiskActive(int drive) const = 0;
    virtual bool isDiskWriteProtected(int drive) const = 0;
    virtual bool isDiskLedOn(int drive) const = 0;
    virtual bool isMotorOn(int drive) const = 0;
    virtual bool isHeadLoaded() const = 0;
    virtual void setDiskWriteProtect(int drive, bool wp) = 0;

    // ─── Serielle Schnittstelle nach außen (DFÜ) ──────────────────────────────
    using SerialCb = std::function<void(uint8_t)>;
    virtual void setDFUECallback(SerialCb cb) = 0;
    virtual void dfueSend(uint8_t byte) = 0;

    // ─── Diagnose ──────────────────────────────────────────────────────────────
    virtual uint8_t memReadDebug(uint16_t addr) = 0;
    virtual void    memWriteDebug(uint16_t addr, uint8_t data) = 0;
    virtual uint8_t ioReadDebug(uint8_t port) = 0;
    virtual std::string lastError() const = 0;
};

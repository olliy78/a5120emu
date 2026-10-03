/**
 * @file pc1715.h
 * @brief PC 1715 / PC 1715W — vierte Maschine des Kerns.
 *
 * Stand AP-1b (doc/design/21_pc1715.md §8, §10/§11): nur die ZRE (`Pc1715Zre`: U880, 64 KB,
 * ROM-Overlay S502, CTC0, SIO0, 8275 mit Rastern) und die Laufschleife.  **Eine** Klasse
 * für beide Geräte; die Variante PC 1715W (256 KB mit Bänken, U8272 + Z80-DMA, Zeichengenerator
 * im RAM) ist vorgesehen, aber noch nicht gebaut — der Konstruktor lehnt sie mit klarer
 * Meldung ab (`std::runtime_error`).
 *
 * Freie Stellen (nicht vergessen):
 *  - **Floppy (AP-2):** K5122-Konfiguration „1715" (Ports 00H–07H, 20H/21H) mit `/WAIT`;
 *    sie kommt in der Interruptkette VOR die ZRE (§3.3).  Bis dahin sind alle
 *    Disketten-Methoden Attrappen: Einlegen scheitert mit @ref lastError, Laufwerke sind leer.
 *  - **Tastatur (AP-3):** `keyPress`/`keyRelease` verwerfen die Taste.
 *  - **Schnittstellen (AP-4):** kein SerialHub, SIO0 steht ohne Gegenstelle.
 *  - Save-State: wie PRG 710 / K8915 gibt es keinen (die Grundlage wäre hier ohnehin nur
 *    CPU + RAM + Kartenregister).
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/pc1715_zre/pc1715_zre.h"
#include <atomic>
#include <stdexcept>
#include <string>

class Pc1715Machine : public K1520Machine {
public:
    struct Config {
        enum class Variante { Pc1715, Pc1715W };
        Variante variante = Variante::Pc1715;
        /// Bildschirm K7222 (80×24, Vorgabe) oder K7221 (64×16) — Hardwarevariante, Kaltstart nötig.
        Pc1715Zre::Bildschirm bild = Pc1715Zre::Bildschirm::K7222;
        /// Zeichengenerator, der bei BWS-Register DB6 = 0 gilt (S619 = A25.2, S602 = A25.1).
        Pc1715Zre::Zeichensatz zeichensatz = Pc1715Zre::Zeichensatz::S619;
    };

    static constexpr uint32_t CPU_HZ = Pc1715Zre::CPU_HZ;

    Pc1715Machine();
    explicit Pc1715Machine(const Config& cfg);   ///< @throws std::runtime_error bei Variante PC 1715W
    ~Pc1715Machine() override = default;

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn() override;
    void reset() override;
    int  run(int max_cycles) override;
    void stop() override { stop_.store(true); }
    void nmi() override { nmi_taster_.store(true, std::memory_order_relaxed); }

    // ─── Bild ────────────────────────────────────────────────────────────────
    const uint8_t* framebuffer() const override { return zre_.framebuffer(); }
    int  fbWidth()  const override { return zre_.fbWidth(); }
    int  fbHeight() const override { return zre_.fbHeight(); }
    bool fbDirty()  const override { return zre_.fbDirty(); }
    void fbClearDirty() override   { zre_.fbClearDirty(); }
    void setConsoleMode(bool) override {}
    bool consolePoll(int&, int&, char&) override { return false; }
    /// Zeichencode der Zelle des letzten Bildes (von der Karte, nie über `mem_read`).
    uint8_t screenChar(int col, int row) const override { return zre_.screenChar(col, row); }

    // ─── Tastatur (AP-3) ─────────────────────────────────────────────────────
    void keyPress(uint32_t, bool, bool) override {}
    void keyRelease(uint32_t) override {}
    void setKeyRepeatRealtime(bool) override {}

    int machineType() const override { return 3; }   // K1520_MACHINE_PC1715
    Config::Variante variante() const { return variante_; }

    // ─── Disketten (AP-2): Attrappen ─────────────────────────────────────────
    bool mountDisk(int, const std::string&, const std::string&, bool) override { return keineFloppy(); }
    bool mountDiskImage(int, std::unique_ptr<DiskImage>, bool) override { return keineFloppy(); }
    bool createDisk(int, const std::string&, const std::string&, bool) override { return keineFloppy(); }
    bool saveDiskAs(int, const std::string&, const std::string&) override { return keineFloppy(); }
    bool unmountDisk(int) override { return false; }
    bool flushDisks() override { return true; }

    bool isDiskRawCompatible(int) const override { return false; }
    std::string diskPath(int) const override { return {}; }
    std::string diskContainer(int) const override { return {}; }
    std::string diskNotice(int) const override { return {}; }
    std::string detectedFormatName(int) const override { return {}; }
    std::string defaultFormatName(int) const override { return {}; }
    std::vector<std::string> compatibleFormats(int) const override { return {}; }
    std::string formatDescription(const std::string&) const override { return {}; }
    const FormatCatalog& formatCatalog() const override { return katalog_; }

    bool isDiskActive(int) const override { return false; }
    bool isDiskWriteProtected(int) const override { return false; }
    bool isDiskLedOn(int) const override { return false; }
    bool isMotorOn(int) const override { return false; }
    bool isHeadLoaded() const override { return false; }
    void setDiskWriteProtect(int, bool) override {}

    // ─── Serielle Schnittstellen (AP-4) ──────────────────────────────────────
    void setDFUECallback(SerialCb) override {}
    void dfueSend(uint8_t) override {}

    // ─── Diagnose ────────────────────────────────────────────────────────────
    /// Speicher aus Sicht der CPU (mit ROM-Overlay).
    uint8_t memReadDebug(uint16_t addr) override { return zre_.memRead(addr); }
    void    memWriteDebug(uint16_t addr, uint8_t d) override { zre_.memWrite(addr, d); }
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return last_error_; }

    // ─── Pc1715-eigen (Tests, Werkzeuge) ─────────────────────────────────────
    Pc1715Zre&  zre() { return zre_; }
    K1520Bus&   bus() { return bus_; }
    uint64_t    totalCycles() const { return total_cycles_; }
    void        clearStop() { stop_.store(false); }
    uint16_t    cpuPC() const { return zre_.cpu().PC; }
    void setCpuTraceCallback(std::function<void(const Z80&)> cb) { zre_.cpu().traceCallback = std::move(cb); }
    void setBusTrace(K1520Bus::BusTrace cb) { bus_.setTraceCallback(std::move(cb)); }

private:
    bool keineFloppy() { last_error_ = "PC 1715: Floppy-Ansteuerung noch nicht gebaut (AP-2)"; return false; }
    void resetHardware();
    static Config pruefe(const Config& cfg);

    const Config::Variante variante_;
    K1520Bus   bus_;
    Pc1715Zre  zre_;
    FormatCatalog katalog_;   ///< leer, bis die Floppy (AP-2) kommt

    std::atomic<bool> stop_{false};
    std::atomic<bool> nmi_taster_{false};
    uint64_t    total_cycles_ = 0;
    uint64_t    bild_naechst_ = Pc1715Zre::FRAME_TAKTE;
    std::string last_error_;
};

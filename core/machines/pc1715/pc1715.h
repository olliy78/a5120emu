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
 * Floppy (AP-2): die K5122 in der Konfiguration „1715" (`K5122::Portlage::Pc1715`: Daten-PIO
 * 00H–03H, Steuer-PIO 04H–07H, SE-Register 20H, MO-Register 21H) im `/WAIT`-Betrieb, mit dem
 * gemeinsamen Baustein `Laufwerke` (Vorgabe 2 × K5601).  Interruptkette: Steuer-PIO vorn
 * (Servicehandbuch §1.2.3), dann CTC → SIO der ZRE.
 *
 * Freie Stellen (nicht vergessen):
 *  - **Tastatur (AP-3):** `keyPress`/`keyRelease` verwerfen die Taste.
 *  - **Schnittstellen (AP-4):** kein SerialHub, SIO0 steht ohne Gegenstelle.
 *  - Save-State: wie PRG 710 / K8915 gibt es keinen (die Grundlage wäre hier ohnehin nur
 *    CPU + RAM + Kartenregister).
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/pc1715_zre/pc1715_zre.h"
#include "core/cards/k5122/k5122.h"
#include "core/machines/laufwerke.h"
#include <array>
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
        /// Laufwerke an der Floppy-Ansteuerung (Vorgabe 2 × K5601 intern, AP-0a/§9).
        std::array<std::string, 4> laufwerke = {"K5601", "K5601", "none", "none"};
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

    // ─── Disketten (gemeinsamer Laufwerksbaustein) ───────────────────────────
    bool mountDisk(int d, const std::string& p, const std::string& f, bool wp) override {
        return lw_.mountDisk(d, p, f, wp);
    }
    bool mountDiskImage(int d, std::unique_ptr<DiskImage> img, bool wp) override {
        return lw_.mountDiskImage(d, std::move(img), wp);
    }
    bool createDisk(int d, const std::string& p, const std::string& f, bool wp) override {
        return lw_.createDisk(d, p, f, wp);
    }
    bool saveDiskAs(int d, const std::string& p, const std::string& f) override {
        return lw_.saveDiskAs(d, p, f);
    }
    bool unmountDisk(int d) override { return lw_.unmountDisk(d); }
    bool flushDisks() override       { return lw_.flushDisks(); }

    bool isDiskRawCompatible(int d) const override { return lw_.isDiskRawCompatible(d); }
    std::string diskPath(int d) const override      { return lw_.diskPath(d); }
    std::string diskContainer(int d) const override { return lw_.diskContainer(d); }
    std::string diskNotice(int d) const override    { return lw_.diskNotice(d); }
    std::string detectedFormatName(int d) const override { return lw_.detectedFormatName(d); }
    std::string defaultFormatName(int d) const override  { return lw_.defaultFormatName(d); }
    std::vector<std::string> compatibleFormats(int d) const override { return lw_.compatibleFormats(d); }
    std::string formatDescription(const std::string& f) const override { return lw_.formatDescription(f); }
    const FormatCatalog& formatCatalog() const override { return lw_.formatCatalog(); }

    bool isDiskActive(int d) const override         { return lw_.isDiskActive(d); }
    bool isDiskWriteProtected(int d) const override { return lw_.isDiskWriteProtected(d); }
    bool isDiskLedOn(int d) const override          { return lw_.isDiskLedOn(d); }
    bool isMotorOn(int d) const override            { return lw_.isMotorOn(d); }
    bool isHeadLoaded() const override              { return lw_.isHeadLoaded(); }
    void setDiskWriteProtect(int d, bool wp) override { lw_.setDiskWriteProtect(d, wp); }

    // ─── Serielle Schnittstellen (AP-4) ──────────────────────────────────────
    void setDFUECallback(SerialCb) override {}
    void dfueSend(uint8_t) override {}

    // ─── Diagnose ────────────────────────────────────────────────────────────
    /// Speicher aus Sicht der CPU (mit ROM-Overlay).
    uint8_t memReadDebug(uint16_t addr) override { return zre_.memRead(addr); }
    void    memWriteDebug(uint16_t addr, uint8_t d) override { zre_.memWrite(addr, d); }
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return lw_.lastError(); }

    // ─── Pc1715-eigen (Tests, Werkzeuge) ─────────────────────────────────────
    Pc1715Zre&  zre() { return zre_; }
    K5122&      afs() { return afs_; }
    K1520Bus&   bus() { return bus_; }
    uint64_t    totalCycles() const { return total_cycles_; }
    void        clearStop() { stop_.store(false); }
    uint16_t    cpuPC() const { return zre_.cpu().PC; }
    void setCpuTraceCallback(std::function<void(const Z80&)> cb) { zre_.cpu().traceCallback = std::move(cb); }
    void setBusTrace(K1520Bus::BusTrace cb) { bus_.setTraceCallback(std::move(cb)); }

private:
    void resetHardware();
    static Config pruefe(const Config& cfg);

    const Config::Variante variante_;
    K1520Bus   bus_;
    Pc1715Zre  zre_;
    K5122      afs_;       ///< Floppy-Ansteuerung 20-330-0102: Portlage „1715", /WAIT
    Laufwerke  lw_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> nmi_taster_{false};
    uint64_t    total_cycles_ = 0;
    uint64_t    bild_naechst_ = Pc1715Zre::FRAME_TAKTE;
    bool        prev_afs_int_ = false;
};

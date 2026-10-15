/**
 * @file mc80.h
 * @brief MC80 (VEB Elektronik Gera) — sechste Maschine des Kerns (K1520_MACHINE_MC80 = 6).
 *
 * Stand AP M2 (doc/design/32_… §6, Entwurf doc/design/31_mc80.md): **Gerüst** —
 * ZVE (`Mc80Zve`: U880 2,4576 MHz, EGOS-ROM 0000H–1FFFH, 16 KB RAM C000H–FFFFH, CTC 80H,
 * PIO 84H = Segmentregister über Tor B/85H, RAM-Steuerung 8CH/8DH) und SPE 2 (`Mc80Spe2`:
 * ROM 2000H–27FFH, leere Fassungen bis 3FFFH lesen 00H, RAM-Segment 4000H–BFFFH).  EGOS
 * läuft nach dem Einschalten durch RESET und die RESET-Kette bis zur Kommandoschleife; Bild
 * (VIS 2a), ASP (SIO/CTC/PIO) und Tastatur folgen in M3, Programmsegmente in M4, Kassette
 * in M7.
 *
 * **Varianten:** .30 und .31 sind im Kern bis M7 gleich (Kassettenanschluss unterscheidet
 * sie erst dort).  .33 (AKM + PK 3: Treiber-ROM nur halb vorhanden, egos.md §3) und die
 * MC80.2x (.21/.22, Etappe 3) werden abgelehnt (`std::invalid_argument`).
 *
 * Interruptkette (vorläufig): nur die ZVE (CTC → PIO) [? Reihenfolge über die Karten ist
 * offen, M3].  Keine Laufwerke (alle Diskettenfunktionen verneinen).  Kein Save-State.
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/mc80/spe.h"
#include "core/cards/mc80/zve3x.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

class Mc80Machine : public K1520Machine {
public:
    struct Config {
        enum class Variante { Mc8030, Mc8031, Mc8033, Mc8021, Mc8022 };
        Variante variante = Variante::Mc8030;
        /// Zahl der SPE 2 (1 oder 2): erste mit ROM und Segment 01H, zweite nur RAM, Segment 02H.
        int spe_karten = 1;
        /// Vorbelegung der dRAMs beim Einschalten.
        uint8_t ram_fill = 0x00;
    };

    static constexpr uint32_t CPU_HZ = Mc80Zve::CPU_HZ;
    /// Bildgröße der VIS 2a (512 × 256); bis M3 bleibt das Bild schwarz.
    static constexpr int FB_BREITE = 512;
    static constexpr int FB_HOEHE  = 256;

    Mc80Machine() : Mc80Machine(Config{}) {}
    explicit Mc80Machine(const Config& cfg);
    ~Mc80Machine() override;

    static const char* variantenName(Config::Variante v);

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn() override;
    void reset() override;
    int  run(int max_cycles) override;
    void stop() override { stop_.store(true); }
    void nmi() override { nmi_taster_.store(true, std::memory_order_relaxed); }

    // ─── Bild (M3) ───────────────────────────────────────────────────────────
    const uint8_t* framebuffer() const override { return fb_.data(); }
    int  fbWidth()  const override { return FB_BREITE; }
    int  fbHeight() const override { return FB_HOEHE; }
    bool fbDirty()  const override { return false; }
    void fbClearDirty() override {}
    void setConsoleMode(bool) override {}
    bool consolePoll(int&, int&, char&) override { return false; }
    uint8_t screenChar(int, int) const override { return 0; }

    // ─── Tastatur (M3) ───────────────────────────────────────────────────────
    void keyPress(uint32_t, bool, bool) override {}
    void keyRelease(uint32_t) override {}
    void setKeyRepeatRealtime(bool) override {}

    int machineType() const override { return 6; }   // K1520_MACHINE_MC80
    Config::Variante variante() const { return cfg_.variante; }

    // ─── Keine Laufwerke ─────────────────────────────────────────────────────
    bool mountDisk(int, const std::string&, const std::string&, bool) override { return false; }
    bool mountDiskImage(int, std::unique_ptr<DiskImage>, bool) override { return false; }
    bool createDisk(int, const std::string&, const std::string&, bool) override { return false; }
    bool saveDiskAs(int, const std::string&, const std::string&) override { return false; }
    bool unmountDisk(int) override { return false; }
    bool flushDisks() override { return false; }
    bool isDiskRawCompatible(int) const override { return false; }
    std::string diskPath(int) const override { return ""; }
    std::string diskContainer(int) const override { return ""; }
    std::string diskNotice(int) const override { return ""; }
    std::string detectedFormatName(int) const override { return ""; }
    std::string defaultFormatName(int) const override { return ""; }
    std::vector<std::string> compatibleFormats(int) const override { return {}; }
    std::string formatDescription(const std::string&) const override { return ""; }
    const FormatCatalog& formatCatalog() const override;
    bool isDiskActive(int) const override { return false; }
    bool isDiskWriteProtected(int) const override { return false; }
    bool isDiskLedOn(int) const override { return false; }
    bool isMotorOn(int) const override { return false; }
    bool isHeadLoaded() const override { return false; }
    void setDiskWriteProtect(int, bool) override {}

    // ─── Serielle Schnittstellen (ASP, M3) ───────────────────────────────────
    void setDFUECallback(SerialCb) override {}
    void dfueSend(uint8_t) override {}

    // ─── Diagnose ────────────────────────────────────────────────────────────
    uint8_t memReadDebug(uint16_t a) override { return bus_.memRead(a); }
    void    memWriteDebug(uint16_t a, uint8_t d) override { bus_.memWrite(a, d); }
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return ""; }

    Mc80Zve&  zve() { return zve_; }
    Mc80Spe2& spe(int i) { return *spe_.at(static_cast<size_t>(i)); }
    int       speKarten() const { return static_cast<int>(spe_.size()); }
    K1520Bus& bus() { return bus_; }
    uint64_t  totalCycles() const { return total_cycles_; }
    void      clearStop() { stop_.store(false); }
    uint16_t  cpuPC() const { return zve_.cpu().PC; }

protected:
    K1520Bus& systemBus() override { return bus_; }
    uint32_t  k6022TaktHz() const override { return CPU_HZ; }

private:
    static Config pruefe(const Config& cfg);

    const Config cfg_;
    K1520Bus bus_;
    Mc80Zve  zve_;
    std::vector<std::unique_ptr<Mc80Spe2>> spe_;
    Mc80Segmentfenster fenster_;
    std::vector<uint8_t> fb_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> nmi_taster_{false};
    uint64_t total_cycles_ = 0;
};

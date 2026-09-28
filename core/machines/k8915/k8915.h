/**
 * @file k8915.h
 * @brief K8915 V3 (5¼″, 1989) — zweite Maschine des Kerns.
 *
 * Stand Etappe 1 (doc/design/16_k8915.md §8, AP-E1): ZRE 045-8762 + K7024 (012-6820).
 * Das Boot-ROM läuft bis zum Selbsttest und schreibt die Statuszeile.  Noch
 * NICHT bestückt: ATS K7028.30 (40H–5FH, 61H — Etappe 2), Tastatur K7672
 * (Etappe 2/3), K5122 im `/WAIT`-Betrieb und Laufwerke (Etappe 3).  Die
 * Laufwerks- und DFÜ-Methoden der K1520Machine melden deshalb „nicht vorhanden".
 *
 * Steckplätze am Gerät (§6.4): 3 = K5122, 4 = ZRE, 6 = ATS, 7 = K7024.
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/zre8762/zre8762.h"
#include "core/cards/k7024/k7024.h"
#include <atomic>

class K8915Machine : public K1520Machine {
public:
    K8915Machine();
    ~K8915Machine() override = default;

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    /** @brief Netz-Ein: DRAM mit 00H (§8a AP-E1: sonst meldet der Stub bei FFE0H
     *         womöglich zufällig „System im RAM"), dann /RESET. */
    void powerOn() override;
    /** @brief Rücksetztaste: systemweiter /RESET, RAM bleibt. */
    void reset() override;
    int  run(int max_cycles) override;
    void stop() override { stop_.store(true); }

    // ─── Bild ────────────────────────────────────────────────────────────────
    const uint8_t* framebuffer() const override { return screen_.getFramebuffer(); }
    int  fbWidth()  const override { return screen_.fbWidth(); }
    int  fbHeight() const override { return screen_.fbHeight(); }
    bool fbDirty()  const override { return screen_.fbDirty(); }
    void fbClearDirty() override   { screen_.fbClearDirty(); }
    void setConsoleMode(bool on) override { screen_.setConsoleMode(on); }
    bool consolePoll(int& x, int& y, char& ch) override {
        return screen_.pollTextChange(x, y, ch);
    }

    // ─── Tastatur (K7672 fehlt noch — Etappe 2) ──────────────────────────────
    void keyPress(uint32_t, bool, bool) override {}
    void keyRelease(uint32_t) override {}

    // ─── Disketten (Etappe 3) ────────────────────────────────────────────────
    bool mountDisk(int, const std::string&, const std::string&, bool) override { return keinLaufwerk(); }
    bool mountDiskImage(int, std::unique_ptr<DiskImage>, bool) override { return keinLaufwerk(); }
    bool createDisk(int, const std::string&, const std::string&, bool) override { return keinLaufwerk(); }
    bool saveDiskAs(int, const std::string&, const std::string&) override { return keinLaufwerk(); }
    bool unmountDisk(int) override { return keinLaufwerk(); }
    bool flushDisks() override { return true; }

    bool isDiskRawCompatible(int) const override { return false; }
    std::string diskPath(int) const override { return {}; }
    std::string diskContainer(int) const override { return {}; }
    std::string diskNotice(int) const override { return {}; }
    std::string detectedFormatName(int) const override { return {}; }
    std::string defaultFormatName(int) const override { return {}; }
    std::vector<std::string> compatibleFormats(int) const override { return {}; }
    std::string formatDescription(const std::string&) const override { return {}; }
    const FormatCatalog& formatCatalog() const override { return formats_; }

    bool isDiskActive(int) const override { return false; }
    bool isDiskWriteProtected(int) const override { return false; }
    bool isDiskLedOn(int) const override { return false; }
    bool isMotorOn(int) const override { return false; }
    bool isHeadLoaded() const override { return false; }
    void setDiskWriteProtect(int, bool) override {}

    // ─── DFÜ (ATS fehlt noch) ────────────────────────────────────────────────
    void setDFUECallback(SerialCb) override {}
    void dfueSend(uint8_t) override {}

    // ─── Diagnose ────────────────────────────────────────────────────────────
    /** @brief Speicher aus Sicht der CPU (A8H-Abbildung, sonst Systembus). */
    uint8_t memReadDebug(uint16_t addr) override { return zre_.memRead(addr); }
    void    memWriteDebug(uint16_t addr, uint8_t d) override { zre_.memWrite(addr, d); }
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return last_error_; }

    // ─── K8915-eigen (Tests, Werkzeuge) ──────────────────────────────────────
    K8915Zre&  zre()    { return zre_; }
    K7024&     screen() { return screen_; }
    K1520Bus&  bus()    { return bus_; }
    uint64_t   totalCycles() const { return total_cycles_; }

private:
    bool keinLaufwerk() {
        last_error_ = "K8915: Diskettenlaufwerke sind noch nicht nachgebildet (Etappe 3)";
        return false;
    }
    void resetHardware();

    K1520Bus  bus_;
    K8915Zre  zre_;       // Platz 4
    K7024     screen_;    // Platz 7, VRAM 1000H

    FormatCatalog     formats_;   // leer, bis es Laufwerke gibt
    std::atomic<bool> stop_{false};
    uint64_t          total_cycles_ = 0;
    std::string       last_error_;
};

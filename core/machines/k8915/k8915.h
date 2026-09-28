/**
 * @file k8915.h
 * @brief K8915 V3 (5¼″, 1989) — zweite Maschine des Kerns.
 *
 * Stand Etappe 2 (doc/design/16_k8915.md §8, AP-E1/AP-E2): ZRE 045-8762, ATS
 * K7028.30 mit Tastatur K7672 (SCP-Modus) und K7024 (012-6820).  Der Selbsttest
 * des Boot-ROMs läuft fehlerfrei durch und endet in der Coldstart-Meldung des
 * Laders.  Noch NICHT bestückt: K5122 im `/WAIT`-Betrieb und Laufwerke
 * (Etappe 3); die Laufwerks- und DFÜ-Methoden melden „nicht vorhanden".
 *
 * Steckplätze am Gerät (§6.4): 3 = K5122, 4 = ZRE, 6 = ATS, 7 = K7024.
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/zre8762/zre8762.h"
#include "core/cards/k7024/k7024.h"
#include "core/cards/k7028/k7028.h"
#include "core/peripherals/k7672/k7672.h"
#include <atomic>

class K8915Machine : public K1520Machine {
public:
    /** @brief Ausstattung, die nicht auf den Karten steht. */
    struct Config {
        /**
         * Prüfstecker/Rückschleife an SIO1-A, SIO1-B und SIO2-A (§6.10).  Ohne sie
         * scheitert der SIO-Test des ROMs mit 'G' (alle drei Kanäle ohne Echo) und
         * der Lader startet erst nach `CR`.  **Vorgabe gesteckt** — vorläufig: ob das
         * Gerät die Schleifen selbst schließt, beantwortet erst der Anwender (§6.10).
         */
        bool pruefstecker = true;
        /** Tastatur K7672 angeschlossen.  Ohne sie scheitert KEY mit 'A' (Gegenprobe). */
        bool tastatur = true;
    };

    K8915Machine();
    explicit K8915Machine(const Config& cfg);
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

    // ─── Tastatur K7672 an SIO2-B der ATS ────────────────────────────────────
    void keyPress(uint32_t k, bool shift, bool ctrl) override { kbd_.keyPress(k, shift, ctrl); }
    void keyRelease(uint32_t k) override { kbd_.keyRelease(k); }

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

    // ─── DFÜ (V.24/IFSS nach außen: Etappe 4) ────────────────────────────────
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
    K7028&     ats()    { return ats_; }
    K7672&     keyboard() { return kbd_; }
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
    K7028     ats_;       // Platz 6, 40H–5FH + Anzeigelatch 60H–67H
    K7024     screen_;    // Platz 7, VRAM 1000H
    K7672     kbd_;       // an SIO2-B

    FormatCatalog     formats_;   // leer, bis es Laufwerke gibt
    std::atomic<bool> stop_{false};
    uint64_t          total_cycles_ = 0;
    std::string       last_error_;
};

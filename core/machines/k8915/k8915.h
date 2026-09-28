/**
 * @file k8915.h
 * @brief K8915 V3 (5¼″, 1989) — zweite Maschine des Kerns.
 *
 * Stand Etappe 3 (doc/design/16_k8915.md §8, AP-E1…AP-E3): ZRE 045-8762, ATS
 * K7028.30 mit Tastatur K7672, K7024 (012-6820) und K5122 im `/WAIT`-Betrieb mit
 * zwei K5601.  Die Laufwerksverwaltung ist der gemeinsame Baustein @ref Laufwerke
 * (wie beim A5120).  DFÜ nach außen fehlt noch (Etappe 4).
 *
 * Steckplätze am Gerät (§6.4): 3 = K5122, 4 = ZRE, 6 = ATS, 7 = K7024.
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/zre8762/zre8762.h"
#include "core/cards/k7024/k7024.h"
#include "core/cards/k7028/k7028.h"
#include "core/cards/k5122/k5122.h"
#include "core/machines/laufwerke.h"
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
        /** Laufwerke an der K5122 (Profilnamen wie beim A5120; "none" = unbestückt).
         *  Am Gerät des Anwenders: 2 × K5601 (§6.4). */
        std::array<std::string, 4> laufwerke = {"K5601", "K5601", "none", "none"};
    };

    /// Takt der K8915-CPU (2,4576 MHz) — Index- und Byteperiode der K5122 daraus.
    static constexpr uint32_t CPU_HZ = 2'457'600;

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

    // ─── Disketten: K5122 (Platz 3) über den gemeinsamen Laufwerksbaustein ───
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
    std::vector<std::string> compatibleFormats(int d) const override {
        return lw_.compatibleFormats(d);
    }
    std::string formatDescription(const std::string& f) const override {
        return lw_.formatDescription(f);
    }
    const FormatCatalog& formatCatalog() const override { return lw_.formatCatalog(); }

    bool isDiskActive(int d) const override         { return lw_.isDiskActive(d); }
    bool isDiskWriteProtected(int d) const override { return lw_.isDiskWriteProtected(d); }
    bool isDiskLedOn(int d) const override          { return lw_.isDiskLedOn(d); }
    bool isMotorOn(int d) const override            { return lw_.isMotorOn(d); }
    bool isHeadLoaded() const override              { return lw_.isHeadLoaded(); }
    void setDiskWriteProtect(int d, bool wp) override { lw_.setDiskWriteProtect(d, wp); }

    // ─── DFÜ (V.24/IFSS nach außen: Etappe 4) ────────────────────────────────
    void setDFUECallback(SerialCb) override {}
    void dfueSend(uint8_t) override {}

    // ─── Diagnose ────────────────────────────────────────────────────────────
    /** @brief Speicher aus Sicht der CPU (A8H-Abbildung, sonst Systembus). */
    uint8_t memReadDebug(uint16_t addr) override { return zre_.memRead(addr); }
    void    memWriteDebug(uint16_t addr, uint8_t d) override { zre_.memWrite(addr, d); }
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return lw_.lastError(); }

    // ─── K8915-eigen (Tests, Werkzeuge) ──────────────────────────────────────
    K8915Zre&  zre()    { return zre_; }
    K7028&     ats()    { return ats_; }
    K7672&     keyboard() { return kbd_; }
    K7024&     screen() { return screen_; }
    K5122&     afs()    { return afs_; }
    K1520Bus&  bus()    { return bus_; }
    uint64_t   totalCycles() const { return total_cycles_; }

private:
    void resetHardware();

    K1520Bus  bus_;
    K8915Zre  zre_;       // Platz 4
    K7028     ats_;       // Platz 6, 40H–5FH + Anzeigelatch 60H–67H
    K7024     screen_;    // Platz 7, VRAM 1000H
    K7672     kbd_;       // an SIO2-B
    K5122     afs_;       // Platz 3, 10H–18H, /WAIT-Betrieb
    Laufwerke lw_;        // Laufwerksverwaltung (gemeinsam mit dem A5120)

    std::atomic<bool> stop_{false};
    uint64_t          total_cycles_ = 0;
    bool              prev_afs_int_ = false;   // Flanke des K5122-Interrupts (Index, MKE)
};

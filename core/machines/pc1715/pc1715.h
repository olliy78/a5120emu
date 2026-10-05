/**
 * @file pc1715.h
 * @brief PC 1715 / PC 1715W — vierte Maschine des Kerns.
 *
 * Stand AP-1b (doc/design/21_pc1715.md §8, §10/§11): die ZRE (`Pc1715Zre`: U880, 64 KB,
 * ROM-Overlay S502, CTC0, SIO0, 8275 mit Rastern) und die Laufschleife.  **Eine** Klasse
 * für beide Geräte.
 *
 * **PC 1715W (AP-W3, doc/pc1715/pc1715w_hardware.md):** dieselbe ZRE-Karte in der Betriebsart
 * `Pc1715Zre::Config::w` (CPU 3,9936 MHz, CTC0 08H ohne Interrupt, SIO0 0CH, LT107/111), dazu
 * `Pc1715wSpeicher` (256 KB, BR 24H, 74S287, S550/ZG-RAM/Bild-RAM in Bank 0) als Speicherweg
 * der CPU und der DMA, `Pc1715wBild` (8275 18H–1BH aus dem Bild-RAM), `Z80Dma` 00H–03H,
 * CTC2 04H–07H (K1 → C/TRG2), `Upd765` 1CH–1FH (und 40H/41H nur für die DMA), KRFD 20H,
 * MOS 28H, KON 34H.  Interruptkette DMA → CTC2 → SIO0.  Die K5122 ist dort nicht am Bus; sie
 * bleibt nur als Halter der vier `FloppyDriveV2` für den gemeinsamen Baustein `Laufwerke`.
 *
 * Floppy (AP-2): die K5122 in der Konfiguration „1715" (`K5122::Portlage::Pc1715`: Daten-PIO
 * 00H–03H, Steuer-PIO 04H–07H, SE-Register 20H, MO-Register 21H) im `/WAIT`-Betrieb, mit dem
 * gemeinsamen Baustein `Laufwerke` (Vorgabe 2 × K5601).  Interruptkette: Steuer-PIO vorn
 * (Servicehandbuch §1.2.3), dann CTC → SIO der ZRE.
 *
 * Tastatur (AP-3): `Tastatur1715` (eigener U880 + S600) läuft im Takt der Maschine, ihr
 * `byteOut` füllt den Empfänger von SIO0 Kanal A (Rahmen 8N1 an der Stoppbitgrenze, kein
 * Bitmodell der SIO; der 3-Byte-FIFO fängt Statusbyte + Code im Abstand von ≈ 0,7 ms).
 *
 * Freie Stellen (nicht vergessen):
 *  - **Schnittstellen (AP-4a):** „Drucker X4" (SIO0-A Sender) und „V.24 X5" (SIO0-B) am
 *    SerialHub; der Empfänger von SIO0-A bleibt die Tastatur.  LT111 (Leitung 111) bleibt ein Latch.
 *  - Save-State: wie PRG 710 / K8915 gibt es keinen (die Grundlage wäre hier ohnehin nur
 *    CPU + RAM + Kartenregister).
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/pc1715_zre/pc1715_zre.h"
#include "core/cards/k5122/k5122.h"
#include "core/machines/laufwerke.h"
#include "core/peripherals/tastatur1715/tastatur1715.h"
#include "core/serial/hub.h"
#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>

class Pc1715wSpeicher;
class Pc1715wBild;
class Z80Dma;
class Upd765;

class Pc1715Machine : public K1520Machine {
public:
    struct Config {
        enum class Variante { Pc1715, Pc1715W };
        Variante variante = Variante::Pc1715;
        /// Bildschirm K7222 (80×24, Vorgabe) oder K7221 (64×16) — Hardwarevariante, Kaltstart nötig.
        Pc1715Zre::Bildschirm bild = Pc1715Zre::Bildschirm::K7222;
        /// Zeichengenerator, der bei BWS-Register DB6 = 0 gilt (S619 = A25.2, S602 = A25.1).
        Pc1715Zre::Zeichensatz zeichensatz = Pc1715Zre::Zeichensatz::S619;
        /// Bestückung der Zeichengenerator-EPROMs (nur PC 1715; der 1715W lädt den Satz von
        /// Diskette) — Hardwarevariante, Kaltstart nötig (AP-6).
        Pc1715Zre::ZgSatz zg_satz = Pc1715Zre::ZgSatz::Deutsch;
        /// Tastatur-ROM: S600 (QWERTY, Vorgabe) oder TAST_618 (QWERTZ) — beide Varianten.
        Tastatur1715::Rom tastatur = Tastatur1715::Rom::S600;
        /// Laufwerke an der Floppy-Ansteuerung (Vorgabe 2 × K5601 intern, AP-0a/§9).
        std::array<std::string, 4> laufwerke = {"K5601", "K5601", "none", "none"};
    };

    static constexpr uint32_t CPU_HZ = Pc1715Zre::CPU_HZ;

    /// Systemtakt des PC 1715W (15,9744 MHz / 4).
    static constexpr uint32_t CPU_HZ_W = Pc1715Zre::CPU_HZ_W;

    Pc1715Machine();
    explicit Pc1715Machine(const Config& cfg);
    ~Pc1715Machine() override;

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn() override;
    void reset() override;
    int  run(int max_cycles) override;
    void stop() override { stop_.store(true); }
    void nmi() override { nmi_taster_.store(true, std::memory_order_relaxed); }

    // ─── Bild ────────────────────────────────────────────────────────────────
    const uint8_t* framebuffer() const override;
    int  fbWidth()  const override;
    int  fbHeight() const override;
    bool fbDirty()  const override;
    void fbClearDirty() override;
    void setConsoleMode(bool) override {}
    bool consolePoll(int&, int&, char&) override { return false; }
    /// Zeichencode der Zelle des letzten Bildes (von der Karte, nie über `mem_read`).
    uint8_t screenChar(int col, int row) const override;

    // ─── Tastatur (AP-3) ─────────────────────────────────────────────────────
    /// Kennung einer physischen Taste: `QK_TASTE_BASE | (Spalte * 8 + Zeile)` (Matrix 13 × 8,
    /// wie K7672/K7609 für ihre Matrizen) — für die Bildschirmtastatur.
    static constexpr uint32_t QK_TASTE_BASE = 0x03000000;
    /**
     * @brief Taste drücken.  Codes: druckbares ASCII (das Zeichen, das die Taste erzeugen soll —
     *        die Umschaltung sucht sich der Rechner über @ref Tastatur1715::tasteFuer; `shift`
     *        wird ignoriert), Qt-Return/Enter/Escape/Backspace, `QK_TASTE_BASE | Position`.
     *        `ctrl` drückt zusätzlich CTRL (ein Code 1…26 gilt als Ctrl-Buchstabe).  Die
     *        Ereignisse werden eingereiht und im Lauffaden abgegeben; eine Umschalttaste wird
     *        EINZELN vorausgedrückt (das ROM sendet nichts, wenn zwei Tasten im selben
     *        Abfragedurchlauf neu erkannt werden).
     */
    void keyPress(uint32_t k, bool shift, bool ctrl) override;
    void keyRelease(uint32_t k) override;
    void setKeyRepeatRealtime(bool) override {}   ///< Autorepeat nur über die REP-Taste (ROM)

    int machineType() const override { return 3; }   // K1520_MACHINE_PC1715
    Config::Variante variante() const { return variante_; }
    bool istW() const { return w_ != nullptr; }
    /// Systemtakt der Variante (2,458 MHz bzw. 3,9936 MHz).
    uint32_t cpuHz() const { return variante_ == Config::Variante::Pc1715W ? CPU_HZ_W : CPU_HZ; }

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
    // 1715W: die K5122 ist nicht am Bus — Motor aus MOS 28H (Bit 4+n), Lampe = Motor oder
    // Zugriff des U8272 auf das Laufwerk (`Upd765::unitBusy`); 1715: wie bisher die K5122.
    bool isDiskLedOn(int d) const override;
    bool isMotorOn(int d) const override;
    bool isHeadLoaded() const override              { return lw_.isHeadLoaded(); }
    void setDiskWriteProtect(int d, bool wp) override { lw_.setDiskWriteProtect(d, wp); }

    // ─── Serielle Schnittstellen (AP-4a): 0 „Drucker" X4, 1 „V.24" X5 ───────
    k1520::serial::SerialHub* serialHub() override { return &hub_; }
    std::vector<k1520::serial::SerialAnschluss*> serielleAnschluesse() override;
    /// Die Tastatur hängt fest an SIO0-A (Empfänger) und geht nicht nach außen.
    std::vector<std::string> festeSchnittstellen() const override { return {"Tastatur S600 (SIO-A)"}; }
    // Alter Unterbau: DFÜ = V.24 X5, Drucker = Drucker X4 (nur Senden).
    void setDFUECallback(SerialCb cb) override { zre_.setAbnehmer(Pc1715Zre::V24, std::move(cb)); }
    void dfueSend(uint8_t b) override { zre_.einspeisen(Pc1715Zre::V24, b); }
    void setPrinterCallback(SerialCb cb) override { zre_.setAbnehmer(Pc1715Zre::Drucker, std::move(cb)); }

    // ─── Diagnose ────────────────────────────────────────────────────────────
    /// Speicher aus Sicht der CPU (1715: mit ROM-Overlay; 1715W: Lese-/Schreibbank aus BR).
    uint8_t memReadDebug(uint16_t addr) override;
    void    memWriteDebug(uint16_t addr, uint8_t d) override;
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return lw_.lastError(); }

    // ─── Pc1715-eigen (Tests, Werkzeuge) ─────────────────────────────────────
    Pc1715Zre&  zre() { return zre_; }
    K5122&      afs() { return afs_; }
    Tastatur1715& tastatur() { return kbd_; }
    K1520Bus&   bus() { return bus_; }
    uint64_t    totalCycles() const { return total_cycles_; }
    void        clearStop() { stop_.store(false); }
    uint16_t    cpuPC() const { return zre_.cpu().PC; }
    void setCpuTraceCallback(std::function<void(const Z80&)> cb) { zre_.cpu().traceCallback = std::move(cb); }
    void setBusTrace(K1520Bus::BusTrace cb) { bus_.setTraceCallback(std::move(cb)); }

    // ─── PC 1715W (nullptr am PC 1715) ───────────────────────────────────────
    Pc1715wSpeicher* speicherW();
    Pc1715wBild*     bildW();
    Z80Dma*          dmaW();
    Upd765*          fdcW();
    Z80CTC*          ctc2W();
    uint8_t          krfdW() const;   ///< KRFD 20H (zuletzt geschrieben)
    uint8_t          mosW() const;    ///< MOS 28H (zuletzt geschrieben)

protected:
    // Zusatzkarten am Systembus (RAF 88H/89H, K6022 E0H–E7H; Entwurf 22/23): wie in den
    // anderen Maschinen nach dem Anlegen, vor dem ersten Lauf gesteckt, hinten in der Kette.
    K1520Bus& systemBus() override { return bus_; }
    uint32_t  k6022TaktHz() const override { return cpuHz(); }

private:
    void resetHardware();
    void tastenAbgeben();                  ///< Oberflächen-Ereignisse in den Lauffaden holen
    void tastenVerarbeiten();              ///< fällige Ereignisse in die Matrix (nur im Lauffaden)
    static Config pruefe(const Config& cfg);
    void bauW();                           ///< Bausteine des 1715W anlegen und verdrahten
    int  runW(int max_cycles);             ///< Laufschleife des PC 1715W (DMA hält die CPU an)
    uint64_t durchlaeufe(uint64_t n) const;   ///< n Tastatur-Abfragedurchläufe in Rechnertakten
    uint32_t frameTakte() const { return cpuHz() / 50; }

    const Config::Variante variante_;
    K1520Bus   bus_;
    Pc1715Zre  zre_;
    K5122      afs_;       ///< Floppy-Ansteuerung 20-330-0102: Portlage „1715", /WAIT
    Laufwerke  lw_;
    Tastatur1715 kbd_;     ///< Tastatur mit eigenem U880 (S600); byteOut → SIO0 Kanal A

    struct TastenEreignis { uint32_t code; bool ctrl; bool gedrueckt; };
    struct Gehalten { int sp, ze; bool shift, ctrl; };
    std::mutex tasten_sperre_;
    std::deque<TastenEreignis> tasten_;    ///< vom Oberflächenfaden eingereiht
    std::deque<TastenEreignis> arbeit_;    ///< im Lauffaden abzuarbeiten
    std::map<uint32_t, Gehalten> gehalten_;///< gedrückte Zeichentasten → Matrixplatz samt Umschaltern
    bool     umschalter_gesetzt_ = false;  ///< Vorspann des vordersten Drucks ist erledigt
    uint64_t tasten_frei_ab_ = 0;          ///< frühester Takt für das nächste Ereignis
    uint64_t kbd_rest_ = 0;                ///< noch nicht an die Tastatur-CPU abgegebene Takte

    k1520::serial::SerialHub hub_;
    struct W;                              ///< Bausteine des PC 1715W (pc1715.cpp)
    std::unique_ptr<W> w_;
    uint64_t  serial_naechst_ = 0;   ///< nächster Blick der Wandler (Taktzahl)

    std::atomic<bool> stop_{false};
    std::atomic<bool> nmi_taster_{false};
    uint64_t    total_cycles_ = 0;
    uint64_t    bild_naechst_ = Pc1715Zre::FRAME_TAKTE;
    bool        prev_afs_int_ = false;
};

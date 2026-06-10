/**
 * @file k5122v2.h
 * @brief K5122v2 – neue, formatagnostische Emulation der K5122 AFS (Floppy-Controller).
 *
 * Zweite, eigenständige Implementierung der K5122 „Anschlusssteuerung Floppy-disk
 * Speicher" für das Robotron-K1520/A5120-System.  Sie ersetzt das mit den konkreten
 * Bootloader-Leseroutinen verflochtene Synthese-Modell der alten @ref K5122 durch ein
 * **Lesekopf-über-rotierender-Spur-Modell** auf Basis der zentralen @ref TrackImage
 * -Abstraktion (siehe doc/refactoring_floppy_emulator.md).
 *
 * Der Controller kennt **keine** Sektorgrößen, Sektoranzahl, CRC-Verfahren oder
 * Boot-Stadien mehr.  Er bezieht von jedem @ref FloppyDriveV2 eine fertig decodierte
 * Spur (Gaps, Sync, IDAM, DATA, echte CRCs) und streamt deren Bytes über Port 0x16 wie
 * ein echter Lesekopf; die Re-Sync-Strobes MK/MK1 rücken den Kopf auf die nächste
 * Adressmarke vor.  Das Verfahren (FM/MFM) steckt allein in der Spur — der Controller
 * ist verfahrensneutral.
 *
 * **Abgrenzung zur alten @ref K5122:** Diese Klasse existiert PARALLEL.  Die alte Karte
 * bleibt unverändert die im A5120-Boot-Pfad verdrahtete; K5122v2 wird in einer künftigen
 * Maschinenkonfiguration an ihre Stelle treten.  Damit ist der Boot-Pfad nicht betroffen.
 *
 * I/O-Port-Belegung (Basis 0x10), Port-A-/Port-B-Bitlayout: identisch zur @ref K5122
 * (siehe dort).  Side-Select = Port-A bit2 (/FR), am /STR-Strobe gelatcht; Step-Richtung
 * = bit5 (MR/SD), am /ST-Puls gesampelt.
 *
 * @see doc/refactoring_floppy_emulator.md §9
 * @see doc/design/07_k5122_afs.md
 * @see k5122.h  (alte Implementierung, unverändert)
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_pio.h"
#include "core/peripherals/floppy_drive/floppy_drive2.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include "core/peripherals/floppy_drive/track_image.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * @class K5122v2
 * @brief Streaming-basierte K5122-Emulation (Lesekopf über rotierender Spur).
 *
 * - Steuert bis zu 4 Laufwerke über zwei Z80-PIOs und ein 8212 (Port 0x18).
 * - Jeder Slot trägt ein @ref DriveProfile (physisches Laufwerk, Maschinenkonfig).
 * - Datenpfad: streamt die decodierte @ref TrackImage byteweise über Port 0x16;
 *   MK/MK1-Strobes synchronisieren auf die nächste Adressmarke.
 * - Index-Periode aus der Laufwerksdrehzahl; /HF-Statusbit aus dem Spur-Encoding.
 * - BUSRQ-Arbitrierung (dmaUpdate/endDmaTransfer) und Interrupt-Daisy-Chain wie gehabt.
 */
class K5122v2 : public BusDevice, public InterruptSlave {
public:
    /**
     * @brief Konstruiert die Karte mit je einem Laufwerksprofil pro Slot.
     * @param bus      K1520-Systembus (für BUSRQ/BUSAK)
     * @param profiles Laufwerksprofile der 4 Slots (Default: 4× mfs_525_ds80)
     * @param cpu_hz   effektive Z80-Taktfrequenz für die Index-Periode (Default 2.45 MHz)
     */
    explicit K5122v2(K1520Bus& bus,
                     std::array<DriveProfile, 4> profiles = {},
                     uint32_t cpu_hz = 2450000);

    // ─── BusDevice (Ports 0x10–0x18) ─────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "K5122v2"; }

    // ─── InterruptSlave (ctrl_pio_ → data_pio_ Daisy-Chain) ──────────────────
    void    setIEI(bool iei) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;
    void    onRETI() override;

    // ─── Disk management ─────────────────────────────────────────────────────

    /**
     * @brief Mountet ein bereits geöffnetes DiskImage auf einem Slot.
     * @param drive 0–3
     * @param img   DiskImage (Eigentum geht über)
     * @param write_protect Schreibschutz
     * @return false bei ungültigem Slot oder Inkompatibilität (siehe drive(d).lastError()).
     */
    bool mountDisk(int drive, std::unique_ptr<DiskImage> img, bool write_protect = false);

    /**
     * @brief Komfort-Overload: öffnet ein .img über @ref DiskImage::open und mountet es.
     * @param drive 0–3
     * @param path  Pfad zur Image-Datei
     * @param fmt   Geometrie (für Raw/.img)
     * @param write_protect Schreibschutz
     * @return false bei Öffnungs-/Kompatibilitätsfehler
     */
    bool mountDisk(int drive, const std::string& path, const DiskFormat& fmt,
                   bool write_protect = false);

    bool unmountDisk(int drive);
    bool isDiskActive(int drive) const;
    bool isDiskWriteProtected(int drive) const;
    void setWriteProtect(int drive, bool wp);
    bool isDriveLedOn(int drive) const;

    /// @brief Direkter Zugriff auf ein Laufwerk (Tests/C-API).
    FloppyDriveV2& drive(int idx) { return drives_[idx]; }

    // ─── DMA-Arbitrierung / Index ────────────────────────────────────────────

    /// @brief ZVE2-Fallback: führt eine ausstehende DMA-Übertragung aus und gibt /BUSRQ frei.
    void dmaUpdate();
    /// @brief Beendet einen aktiven Lese-DMA und gibt /BUSRQ frei (ZVE2-Completion).
    void endDmaTransfer();
    /// @brief Schreitet die Floppy-Simulation um @p cycles Takte fort (Index-Puls).
    void update(int cycles);

private:
    // ─── PIO-/Signal-Handler ─────────────────────────────────────────────────
    void handleCtrlPortAWrite(uint8_t data);
    void handleDataPortAWrite(uint8_t data);
    void updateStatusPortB();
    void doStep();

    /// @brief Wählt am /STR-Lese-Strobe die aktuelle Spur und armiert den Lesetransfer.
    void startReadTransfer();
    /// @brief Rückt den Lesekopf auf die nächste Adressmarke (MK/MK1-Strobe).
    void resyncToNextMark();
    /// @brief Committet einen abgeschlossenen Schreibtransfer in die gecachte Spur.
    void commitWrite();
    void markDriveAccess(int drive);

    // ─── Hardware-Objekte ────────────────────────────────────────────────────
    K1520Bus&     bus_;
    Z80PIO        ctrl_pio_{"K5122v2-CTRL"};   ///< Steuer-PIO: Ports 0x10–0x13
    Z80PIO        data_pio_{"K5122v2-DATA"};   ///< Daten-PIO:  Ports 0x14–0x17
    FloppyDriveV2 drives_[4];                  ///< bis zu 4 Laufwerke (je mit DriveProfile)
    uint32_t      cpu_hz_ = 2450000;           ///< Taktfrequenz für Index-Periode

    // ─── Controller-Zustand ──────────────────────────────────────────────────
    int     selected_drive_ = 0;
    uint8_t current_head_   = 0;               ///< am /STR gelatchte Seite (bit2/FR)
    bool    step_dir_in_    = true;            ///< Step-Richtung (bit5 MR/SD, am /ST)
    uint8_t prev_ctrl_a_    = 0xFF;            ///< vorheriger Port-A-Wert (Kantenerkennung)

    // ─── Lesekopf-Streaming-Modell (ersetzt sector_buf_/field_*) ─────────────
    const TrackImage* cur_track_    = nullptr; ///< aktive Spur (Zeiger auf robotron_track_)
    TrackImage        robotron_track_;          ///< Robotron-Layout-Track für das Streaming;
                                               ///< wird in startReadTransfer() aus dem IBM-Track
                                               ///< des Drive-Cache erzeugt (on-the-fly).
                                               ///< Der Drive-Cache bleibt immer IBM-Format,
                                               ///< damit commitWrite()/parseTrack() funktioniert.
    size_t            head_pos_     = 0;        ///< Lesekopf-Position (Byte) in der Spur
    bool              locked_       = false;    ///< Datenseparator auf eine Marke synchronisiert
    bool              transferring_ = false;    ///< Lesetransfer läuft
    bool              write_mode_   = false;    ///< Schreibtransfer läuft
    std::vector<uint8_t> write_buf_;            ///< gesammelte Schreibdaten (Port 0x14)
    uint16_t          cur_sector_size_ = 128;   ///< Sektorgröße der aktiven Spur (für die
                                                ///< Track-Ende-Arbitrierung, s. ioWrite 0x13)

    // ─── DMA-State ───────────────────────────────────────────────────────────
    bool dma_pending_  = false;
    bool dma_is_write_ = false;

    // ─── Interrupt ───────────────────────────────────────────────────────────
    bool iei_in_ = false;

    // ─── Index-Puls (Periode aus DriveProfile::rpm) ──────────────────────────
    int  index_cycle_acc_ = 0;

    // ─── LED-Simulation ──────────────────────────────────────────────────────
    std::array<std::chrono::steady_clock::time_point, 4> led_until_{};
    std::chrono::milliseconds led_hold_time_{180};
};

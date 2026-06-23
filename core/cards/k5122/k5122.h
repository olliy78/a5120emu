/**
 * @file k5122.h
 * @brief K5122 AFS – formatagnostische Emulation des Floppy-Controllers (Lesekopf-Streaming).
 *
 * Emulation der K5122 „Anschlusssteuerung Floppy-disk Speicher" für das Robotron-
 * K1520/A5120-System.  Der Datenpfad arbeitet als **Lesekopf-über-rotierender-Spur-Modell**
 * auf Basis der zentralen @ref TrackImage -Abstraktion (siehe
 * doc/refactoring_floppy_emulator.md): der Controller kennt **keine** Sektorgrößen,
 * Sektoranzahl, CRC-Verfahren oder Boot-Stadien.  Er bezieht von jedem @ref FloppyDriveV2
 * eine fertig decodierte Spur (Gaps, Sync, IDAM, DATA, echte CRCs) und streamt deren Bytes
 * über Port 0x16 wie ein echter Lesekopf; die Re-Sync-Strobes MK/MK1 rücken den Kopf auf
 * die nächste Adressmarke vor.  Das Verfahren (FM/MFM) steckt allein in der Spur — der
 * Controller ist verfahrensneutral.  Unterstützte Image-Backends (über @ref DiskImage):
 * Raw-`.img` (@ref RawSectorImage) und HFE v1 (HfeImage).
 *
 * Für die A5120-Bootdiskette erzeugt @ref startReadTransfer() on-the-fly das
 * Robotron-spezifische Spurlayout (@ref TrackCodec::buildRobotronTrack), das die
 * idiosynkratische IDAM-Suche der ZVE2-Boot-/Loader-Leseroutinen erwartet.
 *
 * I/O-Port-Belegung (Basis 0x10): zwei Z80-PIOs (Steuer 0x10–0x13, Daten 0x14–0x17) plus
 * 8212-Drive-Select (0x18).  Side-Select = Port-A bit2 (/FR), am /STR-Strobe gelatcht;
 * Step-Richtung = bit5 (MR/SD), am /ST-Puls gesampelt.
 *
 * @see doc/refactoring_floppy_emulator.md §9 / §15
 * @see doc/design/07_k5122_afs.md
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
#include "core/peripherals/floppy_drive/track_codec.h"   // LogicalSector
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * @class K5122
 * @brief Streaming-basierte K5122-Emulation (Lesekopf über rotierender Spur).
 *
 * - Steuert bis zu 4 Laufwerke über zwei Z80-PIOs und ein 8212 (Port 0x18).
 * - Jeder Slot trägt ein @ref DriveProfile (physisches Laufwerk, Maschinenkonfig).
 * - Datenpfad: streamt die decodierte @ref TrackImage byteweise über Port 0x16;
 *   MK/MK1-Strobes synchronisieren auf die nächste Adressmarke.
 * - Index-Periode aus der Laufwerksdrehzahl; /HF-Statusbit aus dem Spur-Encoding.
 * - BUSRQ-Arbitrierung (dmaUpdate/endDmaTransfer) und Interrupt-Daisy-Chain wie gehabt.
 */
class K5122 : public BusDevice, public InterruptSlave {
public:
    /**
     * @brief Konstruiert die Karte mit je einem Laufwerksprofil pro Slot.
     * @param bus      K1520-Systembus (für BUSRQ/BUSAK)
     * @param profiles Laufwerksprofile der 4 Slots (Default: 4× mfs_525_ds80)
     * @param cpu_hz   effektive Z80-Taktfrequenz für die Index-Periode (Default 2.45 MHz)
     */
    explicit K5122(K1520Bus& bus,
                     std::array<DriveProfile, 4> profiles = {},
                     uint32_t cpu_hz = 2450000);

    // ─── BusDevice (Ports 0x10–0x18) ─────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "K5122"; }

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

    /**
     * @brief Parst einen Vollspur-FORMAT-Schreibstrom (wie ZVE2 ihn über Port 0x14
     *        streamt) in logische Sektoren.
     *
     * Der Strom ist eine komplette IBM-MFM-Spur als Bytefolge: Gap (0x4E) / Sync (0x00)
     * und je Sektor `A1 A1 A1 FE <cyl> <head> <sec> <sizecode> <crc16>` (IDAM) gefolgt von
     * `A1 A1 A1 FB <datenbytes…> <crc16>` (DAM).  Sektorgröße = 128 << (sizecode & 3).
     * Statisch + frei von Controller-Zustand, damit unit-testbar.
     *
     * @param stream gesammelter Schreibstrom (write_buf_)
     * @return geparste Sektoren in Spurreihenfolge (leer, wenn keine IDAM/DAM-Paare).
     */
    static std::vector<LogicalSector> parseFormatStream(const std::vector<uint8_t>& stream);

    // ─── Snapshot-Serialisierung (savestate/loadstate) ───────────────────────
    /**
     * @brief Hängt den restaurierbaren Controller-Zustand an @p out an: beide
     *        PIOs (Steuer/Daten), die gelatchten Signale (Laufwerk/Seite/Step-
     *        Richtung) und je Laufwerk die **mechanische Kopfposition** (Zylinder).
     *
     * Gemountete Images, der Spur-Cache und ein laufender Streaming-Transfer
     * werden NICHT serialisiert: Letzterer wird beim deserialize() auf einen
     * konsistenten Idle-Zustand zurückgesetzt (Checkpoints liegen im Leerlauf;
     * der nächste /STR-Strobe streamt die Spur frisch aus dem Image). Callbacks
     * und Bus-Wiring bleiben unberührt (durch Konstruktion/Registrierung gesetzt).
     */
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

    /// @brief Momentaufnahme des Controller-Zustands für Debugger (k1520dbg `dev`).
    struct DebugState {
        int      drive;        ///< aktuell gewähltes Laufwerk (8212 /SELx)
        unsigned cylinder;     ///< physischer Zylinder des gewählten Laufwerks
        unsigned head;         ///< am /STR gelatchte Seite (bit2 /FR)
        bool     transferring; ///< Lese-Stream aktiv (ioRead 0x16 liefert Spurbytes)
        bool     writeMode;    ///< Schreib-Transfer aktiv
        size_t   headPos;      ///< Lesekopf-Position (Byte) in der Spur
        size_t   trackLen;     ///< Länge der aktuell gestreamten Spur (0 = keine)
        unsigned sectorSize;   ///< Sektorgröße der aktiven Spur
        bool     mounted;      ///< Laufwerk gemountet?
        bool     busrq;        ///< /STR-DMA ausstehend
    };
    DebugState debugState() const {
        DebugState s{};
        s.drive = selected_drive_; s.head = current_head_;
        s.mounted = drives_[selected_drive_].isMounted();
        s.cylinder = s.mounted ? drives_[selected_drive_].currentCylinder() : 0;
        s.transferring = transferring_; s.writeMode = write_mode_;
        s.headPos = head_pos_; s.trackLen = cur_track_ ? cur_track_->size() : 0;
        s.sectorSize = cur_sector_size_; s.busrq = dma_pending_;
        return s;
    }

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
    /// @brief Beendet einen Vollspur-FORMAT-Schreibtransfer am Index-Puls: parst den
    ///        gesammelten Schreibstrom (IDAM/DAM/Daten) zu Sektoren, baut die Spur und
    ///        schreibt sie ins Image; gibt /BUSRQ frei (ZVE1 wird über den Index-Int geweckt).
    void commitFormatTrack();
    /// @brief Beginnt das Sammeln eines Schreib-Datenfelds (/WE 1→0 im ZVE2-Streaming).
    ///        Ermittelt den Zielsektor aus der letzten Id-Marke vor head_pos_.
    void beginWriteField();
    /// @brief Committet ein gesammeltes Schreib-Datenfeld (/WE 0→1) in den Zielsektor:
    ///        extrahiert das Datenfeld (A1 A1 A1 DAM …) und schreibt es zurück.
    void commitWriteField();
    void markDriveAccess(int drive);

    // ─── Hardware-Objekte ────────────────────────────────────────────────────
    K1520Bus&     bus_;
    Z80PIO        ctrl_pio_{"K5122-CTRL"};   ///< Steuer-PIO: Ports 0x10–0x13
    Z80PIO        data_pio_{"K5122-DATA"};   ///< Daten-PIO:  Ports 0x14–0x17
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
    bool              write_mode_   = false;    ///< Schreibtransfer läuft (alter /STR-Schreibpfad)
    std::vector<uint8_t> write_buf_;            ///< gesammelte Schreibdaten (Port 0x14)
    uint16_t          cur_sector_size_ = 128;   ///< Sektorgröße der aktiven Spur (nur Debug-Info)

    // ─── BIOS-Schreibpfad (/WE-flankengesteuert) ──────────────────────────────
    // Der CP/A-BIOS-Schreibpfad startet den Transfer als /STR-Lesestrobe (IDAM-Suche)
    // und schaltet erst beim Datenfeld /WE (bit0) auf 0.  write_mode_ (am /STR-Start
    // gesetzt) greift hier also nicht; das Datenfeld wird stattdessen zwischen den
    // /WE-Flanken 1→0 (Feldbeginn) und 0→1 (Feldende → Commit) gesammelt.
    bool              we_writing_ = false;       ///< Datenfeld-Sammlung zwischen den /WE-Flanken
    uint8_t           wr_cyl_  = 0;              ///< Zielsektor (aus IDAM vor head_pos_)
    uint8_t           wr_head_ = 0;
    uint8_t           wr_id_   = 1;
    uint16_t          wr_size_ = 128;

    // ─── DMA-State ───────────────────────────────────────────────────────────
    bool dma_pending_  = false;
    bool dma_is_write_ = false;

    // ─── Per-Byte-/BUSRQ-Drossel (hardware-echt, K5122-Doku §5.5/§5.6.1) ──────
    // /BUSRQ entsteht pro Byte aus dem RDY des Daten-PIO: ZVE2 erhält den Bus,
    // wenn ein Byte bereitliegt, holt es über Port 0x16 ab und verliert den Bus
    // wieder, bis das nächste Byte ~1 Byteperiode später bereitliegt.  In diesen
    // Lücken läuft ZVE1.  ZVE2 verliert den Bus damit automatisch, sobald es
    // aufhört zu lesen (fertig/idle) — das ersetzt JEDE programm-/größen-
    // spezifische Completion-Erkennung ([0x03F8]=3-Watch, OUT(13H)-Hack).
    bool byte_ready_ = false;   ///< ein Byte liegt für ZVE2 bereit → /BUSRQ aktiv
    int  byte_acc_   = 0;       ///< Takte seit dem letzten Byte (bis zur Bereitschaft)
    static constexpr int kBytePeriodCycles = 150;  ///< ~1 MFM-Byte (16 Bitzellen) @ 2.45 MHz

    // ─── /STR=1-Abtastung (Latch, formatagnostisches Transfer-Ende) ──────────
    // K5122-Doku §5.5: „Beendigung der Datenübertragung … durch Abschalten von
    // /STR".  Der Datenseparator tastet /STR nur am Lesetakt ab — ein /STR=1-Puls
    // kürzer als ~1 Byteperiode wird NICHT durchgetaktet (erklärt die kurzen
    // Boot-ROM-Setup-Strobes ≤ ~18 Takte, gegenüber echten Track-Enden ≥ 30000).
    int  str_inactive_cycles_ = 0;              ///< Takte mit /STR=1 im aktiven Transfer
    static constexpr int kStrEndSampleCycles = 320;  ///< ~2 MFM-Byte-Perioden @ 2.45 MHz

    // ─── Vollspur-FORMAT: Schreib-Idle-Erkennung (Transfer-Ende) ──────────────
    int  write_idle_acc_ = 0;                   ///< Takte mit angebotenem, aber nicht
                                                ///< abgeholtem Schreib-Byte (ZVE2 idle)
    static constexpr int kWriteEndSampleCycles = 20000; ///< ZVE2 schreibt nicht mehr → letzte Spur fertig
    uint8_t fmt_cyl_  = 0;                       ///< Zyl. der gerade geschriebenen FORMAT-Spur
    uint8_t fmt_head_ = 0;                       ///< Kopf der gerade geschriebenen FORMAT-Spur

    // ─── Interrupt ───────────────────────────────────────────────────────────
    bool iei_in_ = false;

    // ─── Index-Puls (Periode aus DriveProfile::rpm) ──────────────────────────
    int  index_cycle_acc_ = 0;

    // ─── LED-Simulation ──────────────────────────────────────────────────────
    std::array<std::chrono::steady_clock::time_point, 4> led_until_{};
    std::chrono::milliseconds led_hold_time_{180};
};

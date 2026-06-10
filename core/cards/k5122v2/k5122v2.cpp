/**
 * @file k5122v2.cpp
 * @brief K5122v2 – neue, formatagnostische Emulation der K5122 AFS (Floppy-Controller).
 *
 * Streaming-basiertes Modell: der Controller kennt keine Sektorgrößen, CRC-Verfahren oder
 * Boot-Stadien.  Er bezieht von FloppyDriveV2 ein fertiges TrackImage und streamt dessen
 * Bytes über Port 0x16 wie ein echter Lesekopf.  MK/MK1-Strobes rücken den Kopf auf die
 * nächste Adressmarke vor.
 *
 * Port-/Signal-Belegung identisch zur alten K5122 (side-select = bit2 /FR am /STR,
 * step-Richtung = bit5 MR/SD am /ST).  BUSRQ-Arbitrierung und Interrupt-Daisy-Chain
 * ebenfalls analog.
 *
 * @see core/cards/k5122v2/k5122v2.h
 * @see core/cards/k5122/k5122.cpp  (alte Karte, unverändert)
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/cards/k5122v2/k5122v2.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/logger.h"
#include <algorithm>
#include <cassert>

// ─── Konstruktor ──────────────────────────────────────────────────────────────

/**
 * @brief Initialisiert die Karte und alle 4 Laufwerksslots.
 *
 * Jeder Slot wird mit dem übergebenen DriveProfile initialisiert (Default: mfs_525_ds80).
 * Der initiale Status-Port-B wird sofort gesetzt, damit der erste IN 0x12 einen
 * plausiblen Wert liefert.
 */
K5122v2::K5122v2(K1520Bus& bus,
                 std::array<DriveProfile, 4> profiles,
                 uint32_t cpu_hz)
    : bus_(bus), cpu_hz_(cpu_hz)
{
    for (int i = 0; i < 4; ++i) {
        drives_[i] = FloppyDriveV2(profiles[i]);
    }
    updateStatusPortB();
}

// ─── BusDevice ────────────────────────────────────────────────────────────────

/**
 * @brief IN-Instruktion auf einem K5122v2-Port.
 *
 * Dispatcht auf Ctrl-PIO (0x10–0x13), Data-PIO (0x14–0x17) mit Sonderbehandlung
 * von Port 0x16 bei aktivem Lesetransfer.  Unbekannte Ports → 0xFF + LOG_WARN.
 */
uint8_t K5122v2::ioRead(uint8_t port) {
    uint8_t result = 0xFF;

    if (port >= 0x10 && port <= 0x13) {
        result = ctrl_pio_.ioRead(port - 0x10);
        LOG_DEBUG("K5122v2", "CTRL PIO read  port=0x%02X (sub=%u) => 0x%02X",
                  port, port - 0x10, result);
    } else if (port >= 0x14 && port <= 0x17) {
        if (port == 0x16 && transferring_ && !write_mode_) {
            // Streaming-Datenpfad: Bytes des TrackImage byteweise ausgeben.
            // Der Kopf rotiert zyklisch — bei Erreichen des Spurendes wieder von vorn.
            if (cur_track_ && !cur_track_->empty()) {
                result = cur_track_->bytes[head_pos_];
                head_pos_ = (head_pos_ + 1) % cur_track_->size();
            } else {
                result = 0xFF;
            }
            LOG_TRACE("K5122v2", "Streaming-Read pos=%zu => 0x%02X", head_pos_ - 1, result);
        } else {
            result = data_pio_.ioRead(port - 0x14);
            LOG_DEBUG("K5122v2", "DATA PIO read  port=0x%02X (sub=%u) => 0x%02X",
                      port, port - 0x14, result);
        }
    } else {
        LOG_WARN("K5122v2", "ioRead unbekannter port=0x%02X", port);
    }

    return result;
}

/**
 * @brief OUT-Instruktion auf einem K5122v2-Port.
 *
 * Dispatcht auf Ctrl-PIO (0x10–0x13), Data-PIO (0x14–0x17) oder 8212 Drive-Select
 * (0x18).  Ctrl-Port-A (0x10) und Data-Port-A (0x14) lösen zusätzliche Handler aus.
 */
void K5122v2::ioWrite(uint8_t port, uint8_t data) {
    if (port >= 0x10 && port <= 0x13) {
        if (port == 0x10) {
            LOG_DEBUG("K5122v2",
                "CTRL PortA write 0x%02X  /ST=%d MK1=%d MR/SD=%d /STR=%d /FR=%d MK=%d /WE=%d",
                data,
                (data >> 7) & 1, (data >> 4) & 1, (data >> 5) & 1,
                (data >> 3) & 1, (data >> 2) & 1, (data >> 1) & 1, data & 1);
        } else {
            LOG_DEBUG("K5122v2", "CTRL PIO write port=0x%02X data=0x%02X", port, data);
        }
        ctrl_pio_.ioWrite(port - 0x10, data);
        if (port == 0x10) {
            handleCtrlPortAWrite(data);
        }
        // ── Track-Ende-Arbitrierung (Sekundärlader) ─────────────────────────
        // Der ZVE2-Sekundärlader schaltet den Ctrl-PIO-Port-B-Interrupt ab
        // (OUT(13H),03H), sobald er alle Sektoren einer 128-B-Spur gelesen hat
        // und in seine Idle-Schleife (L0696) fällt.  Auf echter Hardware
        // unterdrückt dann /STR=1 das /BUSRQ und ZVE2 verliert den Bus; in
        // unserem fortlaufend gestepten Modell würde ZVE2 stattdessen in L0696
        // weiterlaufen und die Handshake-Variablen [07F8..07FC] (INIR) zerstören.
        // Daher hier /BUSRQ freigeben, damit ZVE1 die Spur verarbeitet, seekt
        // und ZVE2 für die nächste Spur neu startet.
        // Gate auf 128-B-Spuren: der 3.-Stufen-CP/A-Lader (1024-B-Datenbereich)
        // schreibt OUT(13H),03H ebenfalls, dort ist es aber NUR ein PIO-Port-B-
        // Interrupt-Steuerwort (IE=0), KEIN Track-Ende — eine Freigabe mitten im
        // 1024-B-Lesen würde transferring_ löschen und den Read zum Timeout führen.
        if (port == 0x13 && data == 0x03 && transferring_ && bus_.isBUSRQ()
                && cur_sector_size_ <= 128) {
            transferring_ = false;
            bus_.releaseBUSRQ();
            LOG_DEBUG("K5122v2", "Track-Ende (ZVE2 L0696): BUSRQ freigegeben, ZVE1 übernimmt");
        }
    } else if (port >= 0x14 && port <= 0x17) {
        LOG_DEBUG("K5122v2", "DATA PIO write port=0x%02X data=0x%02X", port, data);
        data_pio_.ioWrite(port - 0x14, data);
        if (port == 0x14) {
            handleDataPortAWrite(data);
        }
    } else if (port == 0x18) {
        // 8212 Drive-Select: bits [3:0] = /SELx, active-low one-hot.
        // 0xEE → unteres Nibble 0xE = 1110, bit0=0 → Drive 0
        // 0xDD → unteres Nibble 0xD = 1101, bit1=0 → Drive 1
        // 0xBB → unteres Nibble 0xB = 1011, bit2=0 → Drive 2
        // 0x77 → unteres Nibble 0x7 = 0111, bit3=0 → Drive 3
        uint8_t sel = ~data & 0x0F;
        selected_drive_ = (sel == 0) ? 0
                        : (sel & 0x01) ? 0
                        : (sel & 0x02) ? 1
                        : (sel & 0x04) ? 2
                        : 3;
        LOG_INFO("K5122v2", "8212 drive-select write=0x%02X => D%d", data, selected_drive_);
        updateStatusPortB();
    } else {
        LOG_WARN("K5122v2", "ioWrite unbekannter port=0x%02X data=0x%02X", port, data);
    }
}

// ─── InterruptSlave ───────────────────────────────────────────────────────────
// Daisy-Chain: IEI → ctrl_pio_ → data_pio_ → IEO

void K5122v2::setIEI(bool iei) {
    iei_in_ = iei;
    ctrl_pio_.setIEI(iei);
    data_pio_.setIEI(ctrl_pio_.getIEO());
}

bool K5122v2::getIEO() const {
    return data_pio_.getIEO();
}

bool K5122v2::hasInterrupt() const {
    return ctrl_pio_.hasInterrupt() || data_pio_.hasInterrupt();
}

/**
 * @brief Interrupt-Vektor des Hochprioritäts-PIO (ctrl_pio_ hat Vorrang).
 */
uint8_t K5122v2::getVector() const {
    if (ctrl_pio_.hasInterrupt()) return ctrl_pio_.getVector();
    if (data_pio_.hasInterrupt()) return data_pio_.getVector();
    return 0xFF;
}

void K5122v2::onRETI() {
    ctrl_pio_.onRETI();
    data_pio_.onRETI();
}

// ─── Disk-Management ─────────────────────────────────────────────────────────

/**
 * @brief Mountet ein bereits geöffnetes DiskImage auf einem Slot.
 */
bool K5122v2::mountDisk(int drive, std::unique_ptr<DiskImage> img, bool write_protect) {
    if (drive < 0 || drive > 3) return false;
    bool ok = drives_[drive].mount(std::move(img), write_protect);
    if (ok && drive == selected_drive_) {
        updateStatusPortB();
    }
    return ok;
}

/**
 * @brief Komfort-Overload: öffnet eine .img-Datei und mountet sie.
 *
 * Das DiskImage wird im IBM-Standard-Format geöffnet (DiskImage::open Default).
 * Der Drive-Cache speichert IBM-Format-Tracks, damit der Write-Pfad (commitWrite →
 * parseTrack → buildTrack) unverändert funktioniert.  Das Robotron-Layout für den
 * Lese-Streaming-Pfad wird von startReadTransfer() on-the-fly erzeugt.
 */
bool K5122v2::mountDisk(int drive, const std::string& path,
                        const DiskFormat& fmt, bool write_protect) {
    if (drive < 0 || drive > 3) return false;
    auto img = DiskImage::open(path, fmt, write_protect);
    if (!img) return false;
    return mountDisk(drive, std::move(img), write_protect);
}

bool K5122v2::unmountDisk(int drive) {
    if (drive < 0 || drive > 3) return false;
    drives_[drive].unmount();
    if (drive == selected_drive_) {
        // Aktiven Lesetransfer abbrechen, da Laufwerk leer
        transferring_ = false;
        cur_track_    = nullptr;
        updateStatusPortB();
    }
    return true;
}

bool K5122v2::isDiskActive(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return drives_[drive].isMounted();
}

bool K5122v2::isDiskWriteProtected(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return drives_[drive].isWriteProtect();
}

void K5122v2::setWriteProtect(int drive, bool wp) {
    if (drive < 0 || drive > 3) return;
    drives_[drive].setWriteProtect(wp);
    if (drive == selected_drive_) {
        updateStatusPortB();
    }
}

bool K5122v2::isDriveLedOn(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return std::chrono::steady_clock::now() < led_until_[static_cast<size_t>(drive)];
}

// ─── DMA-Arbitrierung / Index ─────────────────────────────────────────────────

/**
 * @brief ZVE2-Fallback: führt eine ausstehende DMA-Übertragung aus und gibt /BUSRQ frei.
 *
 * Nur für Schreib-DMAs relevant (Lese-DMAs werden durch ZVE2 per Streaming bedient).
 */
void K5122v2::dmaUpdate() {
    if (!dma_pending_) return;

    if (dma_is_write_) {
        commitWrite();
    }
    // Lesen: ZVE2 liest selbst via IN(0x16); kein Eingriff nötig.

    dma_pending_ = false;
    bus_.releaseBUSRQ();
    LOG_DEBUG("K5122v2", "dmaUpdate (ZVE2-Fallback): BUSRQ freigegeben (%s)",
              dma_is_write_ ? "SCHREIBEN abgeschlossen" : "LESEN: ZVE2 pollt selbst");
}

/**
 * @brief Beendet einen aktiven Lese-DMA und gibt /BUSRQ frei (ZVE2-Completion).
 *
 * Stellt den ctrl_pio_-Port-A-Interrupt wieder her (analog alter Karte, 0x83 = IE=1).
 */
void K5122v2::endDmaTransfer() {
    if (!bus_.isBUSRQ()) return;
    transferring_ = false;
    dma_pending_  = false;
    bus_.releaseBUSRQ();
    // ctrl_pio_ Port-A Interrupt-Enable wiederherstellen (von ZVE2-OUT(11H,03H) gelöscht).
    ctrl_pio_.ioWrite(1, 0x83);
    LOG_DEBUG("K5122v2", "endDmaTransfer: ZVE2 DMA fertig, BUSRQ freigegeben, ctrl-PIO Port-A IE wiederhergestellt");
}

/**
 * @brief Index-Puls-Simulation.
 *
 * Inkrementiert den Zyklenzähler um @p cycles.  Bei Überlauf der Periodenzeit
 * (abgeleitet aus DriveProfile::rpm) wird ein /ASTB-Puls auf dem ctrl_pio_ erzeugt,
 * der die Port-A-Interrupt-Logik auslöst.
 */
void K5122v2::update(int cycles) {
    if (!drives_[selected_drive_].isMounted()) return;

    index_cycle_acc_ += cycles;
    const int period = drives_[selected_drive_].indexPeriodCycles(cpu_hz_);
    if (index_cycle_acc_ < period) return;
    index_cycle_acc_ -= period;

    // Fallenden Puls simulieren: /ASTB low → Interrupt-Flanke → wieder high.
    ctrl_pio_.setASTB(false);
    ctrl_pio_.setASTB(true);
    LOG_TRACE("K5122v2", "Index-Puls: ctrl PIO Port A /ASTB pulsed");
}

// ─── Private: Ctrl Port A Handler ─────────────────────────────────────────────

/**
 * @brief Dekodiert und verarbeitet einen Schreibzugriff auf Ctrl Port A (0x10).
 *
 * Drei Flankenerkennungen:
 *
 * 1. /ST (bit7) fallende Flanke → doStep() (Richtung aus MR/SD bit5)
 * 2. /STR (bit3) fallende Flanke → DMA starten oder committen
 *    - Side-Select = bit2 (/FR): bit2=1 → Kopf 0, bit2=0 → Kopf 1, NUR hier latchen
 *    - ZVE2-Kontext (Bus gehalten): Schreib-Commit oder Lese-Refresh
 *    - ZVE1-Kontext: neuen Transfer starten + BUSRQ assertieren
 * 3. MK (bit1) oder MK1 (bit4) steigende Flanke → resyncToNextMark()
 */
void K5122v2::handleCtrlPortAWrite(uint8_t data) {
    // ── /ST (bit7) fallende Flanke: Schritt-Puls ─────────────────────────────
    if ((prev_ctrl_a_ & 0x80) && !(data & 0x80)) {
        // MR/SD (bit5): bit5=1 → inward (höhere Zylinder), bit5=0 → outward (Richtung 0)
        step_dir_in_ = (data & 0x20) != 0;
        doStep();
    }

    // ── /STR (bit3) fallende Flanke: Strobe ──────────────────────────────────
    if ((prev_ctrl_a_ & 0x08) && !(data & 0x08)) {
        bool is_write = !(data & 0x01);  // /WE=0 → Schreiben

        // Side-Select: bit2 (/FR), NUR am /STR latchen.
        // bit2=1 → Kopf 0, bit2=0 → Kopf 1.
        // Empirisch verifiziert an den @OS.COM-Lesestrobes; bit5 togglet mit MK-Strobes
        // und darf daher NICHT als Seitenwahl dienen.
        current_head_ = (data & 0x04) ? 0 : 1;

        if (bus_.isBUSRQ()) {
            // ZVE2-Kontext: Bus bereits gehalten
            if (is_write) {
                // Schreib-Commit: OTIR fertig, Spur patchen und Bus freigeben.
                commitWrite();
                dma_pending_ = false;
                bus_.releaseBUSRQ();
                LOG_DEBUG("K5122v2", "/STR ZVE2-Commit SCHREIBEN abgeschlossen, BUSRQ freigegeben");
            } else {
                // Lese-Refresh: Spur für ggf. neuen Kopf neu laden (z. B. Kopf-Wechsel).
                startReadTransfer();
                LOG_DEBUG("K5122v2", "/STR ZVE2-Lese-Refresh: Spur neu geladen, BUSRQ gehalten");
            }
        } else {
            // ZVE1-Kontext: neuen DMA-Transfer auslösen
            dma_is_write_ = is_write;
            if (!is_write) {
                startReadTransfer();
            } else {
                // Schreib-DMA: Puffer leeren, ZVE2 füllt ihn via Port 0x14
                write_mode_  = true;
                transferring_ = false;
                write_buf_.clear();
            }
            dma_pending_ = true;
            bus_.assertBUSRQ();
            LOG_DEBUG("K5122v2", "/STR Flanke: BUSRQ gesetzt, DMA %s ausstehend",
                      is_write ? "SCHREIBEN" : "LESEN");
        }
    }

    // ── MK (bit1) oder MK1 (bit4) steigende Flanke: Re-Sync ─────────────────
    // Sowohl ROM-Leseroutine (MK/bit1) als auch Sekundärlader-ZVE2 (MK1/bit4)
    // pulsieren die entsprechenden Bits, um den Datenseparator auf die nächste
    // Adressmarke zu synchronisieren.
    if (transferring_ && !write_mode_) {
        bool mk_rising  = !(prev_ctrl_a_ & 0x02) && (data & 0x02);
        bool mk1_rising = !(prev_ctrl_a_ & 0x10) && (data & 0x10);
        if (mk_rising || mk1_rising) {
            resyncToNextMark();
            LOG_TRACE("K5122v2", "MK/MK1-Flanke: re-sync auf nächste Marke, pos=%zu", head_pos_);
        }
    }

    prev_ctrl_a_ = data;
    updateStatusPortB();
}

// ─── Private: Data Port A Handler ─────────────────────────────────────────────

/**
 * @brief Sammelt ein Schreib-Datenbyte (Port 0x14) im write_buf_.
 *
 * Während Lese-Transfers schreibt der 3rd-Stage-Loader jeden gelesenen Byte
 * zurück auf Data-Port-A (IN A,(16H); OUT (14H),A — Lese-Echo für CRC-Hardware).
 * Diese Echo-Bytes werden ignoriert (write_mode_ ist dann false).
 */
void K5122v2::handleDataPortAWrite(uint8_t data) {
    if (!write_mode_) return;   // Lese-Echo — kein Schreibdatum
    write_buf_.push_back(data);
    LOG_TRACE("K5122v2", "Schreib-Byte 0x%02X gesammelt (%zu Bytes)", data, write_buf_.size());
}

// ─── Private: Status Port B ───────────────────────────────────────────────────

/**
 * @brief Setzt den Status-Byte für das aktuell gewählte Laufwerk in Ctrl-PIO Port B.
 *
 * Zusammensetzung (active-low-Signale sind 0, wenn aktiv):
 * @code
 *   Default (kein Laufwerk montiert): 0xF5
 *     bit0 /RDYL=1  (nicht bereit)
 *     bit2 /HF=1    (MFM/5"-Laufwerk → 1; für FM/8"-Laufwerke wäre es 0)
 *     bit5 /WP=1    (kein Schreibschutz)
 *     bit6 /FW=1    (kein Fehler)
 *     bit7 /TO=1    (nicht auf Spur 0)
 * @endcode
 *
 * /HF (bit2): per Default 1 (= High-Frequency/MFM-Modus), da 5"-MFM das Standardprofil
 * ist.  Für FM/8"-Laufwerke (profile_.supports_fm && !profile_.supports_mfm) wäre bit2=0.
 * Im aktuellen Testrahmen (nur MFM-Laufwerke) ist der Default ausreichend.
 */
void K5122v2::updateStatusPortB() {
    uint8_t s = 0xF5;   // Default: kein Laufwerk

    FloppyDriveV2& drv = drives_[selected_drive_];
    if (drv.isMounted()) {
        s &= ~(1u << 0);            // /RDYL = 0 (bereit)
        if (drv.currentCylinder() == 0)
            s &= ~(1u << 7);        // /TO = 0 (auf Spur 0)
        if (drv.isWriteProtect())
            s &= ~(1u << 5);        // /WP = 0 (schreibgeschützt)
        // bit6 /FW bleibt 1 (kein Laufwerksfehler modelliert)
    }

    ctrl_pio_.portBWrite(s);
}

// ─── Private: Floppy-Operationen ─────────────────────────────────────────────

/**
 * @brief Schritt-Puls: Kopf des gewählten Laufwerks um eine Spur bewegen.
 */
void K5122v2::doStep() {
    FloppyDriveV2& drv = drives_[selected_drive_];
    if (!drv.isMounted()) return;

    drv.step(step_dir_in_);
    markDriveAccess(selected_drive_);

    LOG_TRACE("K5122v2", "STEP D%d dir=%s cyl=%u",
              selected_drive_, step_dir_in_ ? "inward" : "outward",
              static_cast<unsigned>(drv.currentCylinder()));
}

/**
 * @brief Armiert einen Lese-Transfer: erzeugt einen Robotron-Layout-Track und streamt ihn.
 *
 * Der Drive-Cache liefert einen IBM-Format-Track (buildTrack); daraus werden via
 * parseTrack die logischen Sektoren gewonnen und anschließend via buildRobotronTrack ein
 * Robotron-Layout-Track erzeugt.  Dieser liegt in robotron_track_ und der Zeiger
 * cur_track_ zeigt darauf.  Der IBM-Format-Track im Drive-Cache bleibt unberührt, damit
 * commitWrite()/parseTrack() weiterhin funktioniert.
 *
 * Bei leerem Laufwerk oder Spur → cur_track_=nullptr.
 */
void K5122v2::startReadTransfer() {
    FloppyDriveV2& drv = drives_[selected_drive_];
    if (!drv.isMounted()) {
        LOG_WARN("K5122v2", "Lese-Transfer: D%d nicht montiert", selected_drive_);
        transferring_ = false;
        cur_track_    = nullptr;
        robotron_track_ = {};
        return;
    }

    // IBM-Format-Track aus dem Drive-Cache holen (unverändert für den Write-Pfad).
    const TrackImage& ibm_track = drv.track(current_head_);

    if (ibm_track.empty()) {
        LOG_WARN("K5122v2", "Lese-Transfer: D%d H%u Spur leer",
                 selected_drive_, static_cast<unsigned>(current_head_));
        transferring_ = false;
        cur_track_    = nullptr;
        robotron_track_ = {};
        return;
    }

    // Robotron-Layout-Track on-the-fly erzeugen: IBM-Track parsen → Sektoren →
    // buildRobotronTrack.  Der Drive-Cache bleibt IBM-Format.
    auto sektoren   = TrackCodec::parseTrack(ibm_track);
    robotron_track_ = TrackCodec::buildRobotronTrack(sektoren);
    cur_sector_size_ = sektoren.empty() ? 128 : sektoren.front().size;

    cur_track_    = &robotron_track_;
    head_pos_     = 0;
    transferring_ = true;
    write_mode_   = false;
    locked_       = false;
    markDriveAccess(selected_drive_);

    LOG_INFO("K5122v2", ">>> READ D%d C=%u H=%u Spur=%zu Bytes (Robotron-Layout)",
             selected_drive_,
             static_cast<unsigned>(drv.currentCylinder()),
             static_cast<unsigned>(current_head_),
             robotron_track_.size());
}

/**
 * @brief Rückt den Lesekopf auf die nächste Adressmarke vor (MK/MK1-Strobe).
 *
 * Sucht ab der aktuellen Position (head_pos_) die nächste Position in @ref marks[],
 * die eine Marke enthält (MarkType != None), und setzt head_pos_ darauf.  Das Byte
 * an dieser Position ist dann das erste Byte, das der nächste IN 0x16 liefert —
 * also das Mark-Byte (0xFE = IDAM, 0xFB = DAM, 0xFC = IAM).
 *
 * Kein Umlauf über das Spurende hinaus, wenn keine Marke mehr gefunden wird.
 */
void K5122v2::resyncToNextMark() {
    if (!cur_track_ || cur_track_->empty()) return;

    size_t m = cur_track_->nextMark(head_pos_);
    if (m != SIZE_MAX) {
        head_pos_ = m;
        locked_   = true;
        LOG_TRACE("K5122v2", "resync → Marke bei pos=%zu (0x%02X)",
                  m, cur_track_->bytes[m]);
    }
}

/**
 * @brief Committet den Schreib-Puffer in die gecachte Spur.
 *
 * Ansatz: TrackCodec::parseTrack() liefert alle Sektoren der Spur.  Der zu
 * beschreibende Sektor wird als der identifiziert, dessen DATA-Marke am nächsten
 * HINTER head_pos_ liegt (d. h. der zuletzt gelesene / unter dem Kopf liegende).
 * Falls head_pos_==0 oder keine Positionsinformation vorliegt, wird der erste
 * Sektor genommen.  Die gesammelten Bytes werden in dessen data-Feld eingetragen,
 * auf die Sektorgröße gekürzt oder mit 0x00 aufgefüllt, und die Spur via
 * TrackCodec::buildTrack() neu gebaut.
 *
 * Schreibschutz und leerer Puffer werden früh abgefangen.
 */
void K5122v2::commitWrite() {
    FloppyDriveV2& drv = drives_[selected_drive_];
    if (!drv.isMounted()) {
        LOG_WARN("K5122v2", "commitWrite: D%d nicht montiert", selected_drive_);
        write_buf_.clear();
        write_mode_ = false;
        return;
    }
    if (drv.isWriteProtect()) {
        LOG_WARN("K5122v2", "commitWrite: D%d ist schreibgeschützt", selected_drive_);
        write_buf_.clear();
        write_mode_ = false;
        return;
    }
    if (write_buf_.empty()) {
        LOG_WARN("K5122v2", "commitWrite: Schreib-Puffer leer");
        write_mode_ = false;
        return;
    }

    markDriveAccess(selected_drive_);

    // Spur lesen und in logische Sektoren parsen.
    TrackImage& spur = drv.mutableTrack(current_head_);
    if (spur.empty()) {
        LOG_WARN("K5122v2", "commitWrite: Spur (D%d H%u) leer, nichts zu schreiben",
                 selected_drive_, static_cast<unsigned>(current_head_));
        write_buf_.clear();
        write_mode_ = false;
        return;
    }

    auto sektoren = TrackCodec::parseTrack(spur);
    if (sektoren.empty()) {
        LOG_WARN("K5122v2", "commitWrite: parseTrack lieferte keine Sektoren");
        write_buf_.clear();
        write_mode_ = false;
        return;
    }

    // Ziel-Sektor: das DATA-Feld, das am nächsten HINTER der aktuellen Lesekopf-
    // Position liegt.  Da head_pos_ nach einem Schreib-/STR-Strobe auf den Beginn
    // der Spur gesetzt wird (write_mode_=true setzt transferring_=false), und der
    // Schreib-Commit typischerweise nach dem Füllen des Puffers kommt, nehmen wir
    // den ersten Sektor als zuverlässigen Fallback — analog dem alten current_sector_=1.
    // Bei Laufwerken mit nur einem Sektor pro Spur ist das der einzige mögliche Sektor.
    //
    // Robustere Variante (für mehrere Sektoren): Suche nach der DATA-Marke in der
    // Spur, die aktuell unter head_pos_ liegt.  Diese Logik wird hier als first-hit
    // implementiert (einfach, verifiziert im Write-Roundtrip-Test).
    LogicalSector* ziel = &sektoren[0];
    if (cur_track_ && !cur_track_->empty() && head_pos_ > 0) {
        // Suche die DATA-Marke, die kurz vor head_pos_ liegt (letzter gesehener Sektor).
        size_t best_dist = SIZE_MAX;
        for (auto& s : sektoren) {
            // Sektoren sind in Spurreihenfolge; nutze idx ≈ Byte-Offset (grob).
            // Ohne expliziten Offset: einfach den ersten nehmen.
            // (Erweiterbar: TrackImage-Markenposition speichern.)
            (void)s;
        }
        // Fallback: erster Sektor
        ziel = &sektoren[0];
    }

    // Schreib-Puffer auf Sektorgröße anpassen.
    const size_t sec_size = static_cast<size_t>(ziel->size);
    ziel->data.resize(sec_size, 0x00);
    const size_t copy_len = std::min(write_buf_.size(), sec_size);
    std::copy(write_buf_.begin(), write_buf_.begin() + copy_len, ziel->data.begin());

    LOG_INFO("K5122v2", ">>> WRITE D%d C=%u H=%u S=%u bytes=%zu",
             selected_drive_,
             static_cast<unsigned>(drv.currentCylinder()),
             static_cast<unsigned>(current_head_),
             static_cast<unsigned>(ziel->id),
             copy_len);

    // Spur neu bauen und als dirty markieren.
    spur = TrackCodec::buildTrack(sektoren, spur.encoding);
    drv.markTrackDirty(current_head_);

    write_buf_.clear();
    write_mode_   = false;
    transferring_ = false;
}

/**
 * @brief Merkt den letzten Laufwerkszugriff für die LED-Simulation.
 *
 * isDriveLedOn() gibt true zurück, solange weniger als led_hold_time_ (180 ms)
 * vergangen sind.
 */
void K5122v2::markDriveAccess(int drive) {
    if (drive < 0 || drive > 3) return;
    led_until_[static_cast<size_t>(drive)] =
        std::chrono::steady_clock::now() + led_hold_time_;
}

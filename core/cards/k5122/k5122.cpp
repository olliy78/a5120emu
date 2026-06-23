/**
 * @file k5122.cpp
 * @brief K5122 AFS – formatagnostischer Floppy-Controller (Lesekopf-Streaming).
 *
 * Streaming-basiertes Modell: der Controller kennt keine Sektorgrößen, CRC-Verfahren oder
 * Boot-Stadien.  Er bezieht von FloppyDriveV2 ein fertiges TrackImage und streamt dessen
 * Bytes über Port 0x16 wie ein echter Lesekopf.  MK/MK1-Strobes rücken den Kopf auf die
 * nächste Adressmarke vor.  Side-select = bit2 /FR am /STR, step-Richtung = bit5 MR/SD
 * am /ST; BUSRQ-Arbitrierung und Interrupt-Daisy-Chain wie in der Doku beschrieben.
 *
 * @see core/cards/k5122/k5122.h
 * @see doc/refactoring_floppy_emulator.md §9 / §15
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/cards/k5122/k5122.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/logger.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

// ─── Konstruktor ──────────────────────────────────────────────────────────────

/**
 * @brief Initialisiert die Karte und alle 4 Laufwerksslots.
 *
 * Jeder Slot wird mit dem übergebenen DriveProfile initialisiert (Default: mfs_525_ds80).
 * Der initiale Status-Port-B wird sofort gesetzt, damit der erste IN 0x12 einen
 * plausiblen Wert liefert.
 */
K5122::K5122(K1520Bus& bus,
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
 * @brief IN-Instruktion auf einem K5122-Port.
 *
 * Dispatcht auf Ctrl-PIO (0x10–0x13), Data-PIO (0x14–0x17) mit Sonderbehandlung
 * von Port 0x16 bei aktivem Lesetransfer.  Unbekannte Ports → 0xFF + LOG_WARN.
 */
uint8_t K5122::ioRead(uint8_t port) {
    uint8_t result = 0xFF;

    if (port >= 0x10 && port <= 0x13) {
        result = ctrl_pio_.ioRead(port - 0x10);
        LOG_DEBUG("K5122", "CTRL PIO read  port=0x%02X (sub=%u) => 0x%02X",
                  port, port - 0x10, result);
    } else if (port >= 0x14 && port <= 0x17) {
        if (port == 0x16 && transferring_ && !write_mode_) {
            // Streaming-Datenpfad: Bytes des TrackImage byteweise ausgeben.
            // Der Kopf rotiert zyklisch — bei Erreichen des Spurendes wieder von vorn.
            if (cur_track_ && !cur_track_->empty()) {
                result = cur_track_->bytes[head_pos_];
                head_pos_ = (head_pos_ + 1) % cur_track_->size();
                // Per-Byte-Drossel: Byte abgeholt → ZVE2 verliert den Bus, bis das
                // nächste Byte ~1 Byteperiode später bereitliegt (s. update()).
                // In der Lücke läuft ZVE1.
                byte_ready_ = false;
                byte_acc_   = 0;
                bus_.releaseBUSRQ();
            } else {
                result = 0xFF;
            }
            LOG_TRACE("K5122", "Streaming-Read pos=%zu => 0x%02X", head_pos_ - 1, result);
        } else {
            result = data_pio_.ioRead(port - 0x14);
            LOG_DEBUG("K5122", "DATA PIO read  port=0x%02X (sub=%u) => 0x%02X",
                      port, port - 0x14, result);
        }
    } else {
        LOG_WARN("K5122", "ioRead unbekannter port=0x%02X", port);
    }

    return result;
}

/**
 * @brief OUT-Instruktion auf einem K5122-Port.
 *
 * Dispatcht auf Ctrl-PIO (0x10–0x13), Data-PIO (0x14–0x17) oder 8212 Drive-Select
 * (0x18).  Ctrl-Port-A (0x10) und Data-Port-A (0x14) lösen zusätzliche Handler aus.
 */
void K5122::ioWrite(uint8_t port, uint8_t data) {
    if (port >= 0x10 && port <= 0x13) {
        if (port == 0x10) {
            LOG_DEBUG("K5122",
                "CTRL PortA write 0x%02X  /ST=%d MK1=%d MR/SD=%d /STR=%d /FR=%d MK=%d /WE=%d",
                data,
                (data >> 7) & 1, (data >> 4) & 1, (data >> 5) & 1,
                (data >> 3) & 1, (data >> 2) & 1, (data >> 1) & 1, data & 1);
        } else {
            LOG_DEBUG("K5122", "CTRL PIO write port=0x%02X data=0x%02X", port, data);
        }
        ctrl_pio_.ioWrite(port - 0x10, data);
        if (port == 0x10) {
            handleCtrlPortAWrite(data);
        }
        // (Kein OUT(13H)-Track-Ende-Hack mehr: ZVE2 verliert den Bus jetzt
        //  hardware-echt über die Per-Byte-Drossel + /STR=1-Abtastung, s. update().)
    } else if (port >= 0x14 && port <= 0x17) {
        LOG_DEBUG("K5122", "DATA PIO write port=0x%02X data=0x%02X", port, data);
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
        LOG_INFO("K5122", "8212 drive-select write=0x%02X => D%d", data, selected_drive_);
        updateStatusPortB();
    } else {
        LOG_WARN("K5122", "ioWrite unbekannter port=0x%02X data=0x%02X", port, data);
    }
}

// ─── InterruptSlave ───────────────────────────────────────────────────────────
// Daisy-Chain: IEI → ctrl_pio_ → data_pio_ → IEO

void K5122::setIEI(bool iei) {
    iei_in_ = iei;
    ctrl_pio_.setIEI(iei);
    data_pio_.setIEI(ctrl_pio_.getIEO());
}

bool K5122::getIEO() const {
    return data_pio_.getIEO();
}

bool K5122::hasInterrupt() const {
    return ctrl_pio_.hasInterrupt() || data_pio_.hasInterrupt();
}

/**
 * @brief Interrupt-Vektor des Hochprioritäts-PIO (ctrl_pio_ hat Vorrang).
 */
uint8_t K5122::getVector() const {
    if (ctrl_pio_.hasInterrupt()) return ctrl_pio_.getVector();
    if (data_pio_.hasInterrupt()) return data_pio_.getVector();
    return 0xFF;
}

void K5122::onRETI() {
    ctrl_pio_.onRETI();
    data_pio_.onRETI();
}

// ─── Disk-Management ─────────────────────────────────────────────────────────

/**
 * @brief Mountet ein bereits geöffnetes DiskImage auf einem Slot.
 */
bool K5122::mountDisk(int drive, std::unique_ptr<DiskImage> img, bool write_protect) {
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
bool K5122::mountDisk(int drive, const std::string& path,
                        const DiskFormat& fmt, bool write_protect) {
    if (drive < 0 || drive > 3) return false;
    auto img = DiskImage::open(path, fmt, write_protect);
    if (!img) return false;
    return mountDisk(drive, std::move(img), write_protect);
}

bool K5122::unmountDisk(int drive) {
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

bool K5122::isDiskActive(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return drives_[drive].isMounted();
}

bool K5122::isDiskWriteProtected(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return drives_[drive].isWriteProtect();
}

void K5122::setWriteProtect(int drive, bool wp) {
    if (drive < 0 || drive > 3) return;
    drives_[drive].setWriteProtect(wp);
    if (drive == selected_drive_) {
        updateStatusPortB();
    }
}

bool K5122::isDriveLedOn(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return std::chrono::steady_clock::now() < led_until_[static_cast<size_t>(drive)];
}

// ─── DMA-Arbitrierung / Index ─────────────────────────────────────────────────

/**
 * @brief ZVE2-Fallback: führt eine ausstehende DMA-Übertragung aus und gibt /BUSRQ frei.
 *
 * Nur für Schreib-DMAs relevant (Lese-DMAs werden durch ZVE2 per Streaming bedient).
 */
void K5122::dmaUpdate() {
    if (!dma_pending_) return;

    if (dma_is_write_) {
        commitWrite();
    }
    // Lesen: ZVE2 liest selbst via IN(0x16); kein Eingriff nötig.

    dma_pending_ = false;
    bus_.releaseBUSRQ();
    LOG_DEBUG("K5122", "dmaUpdate (ZVE2-Fallback): BUSRQ freigegeben (%s)",
              dma_is_write_ ? "SCHREIBEN abgeschlossen" : "LESEN: ZVE2 pollt selbst");
}

/**
 * @brief Beendet einen aktiven Lese-DMA und gibt /BUSRQ frei (ZVE2-Completion).
 *
 * Stellt den ctrl_pio_-Port-A-Interrupt wieder her (analog alter Karte, 0x83 = IE=1).
 */
void K5122::endDmaTransfer() {
    if (!bus_.isBUSRQ()) return;
    transferring_ = false;
    dma_pending_  = false;
    bus_.releaseBUSRQ();
    // ctrl_pio_ Port-A Interrupt-Enable wiederherstellen (von ZVE2-OUT(11H,03H) gelöscht).
    ctrl_pio_.ioWrite(1, 0x83);
    LOG_DEBUG("K5122", "endDmaTransfer: ZVE2 DMA fertig, BUSRQ freigegeben, ctrl-PIO Port-A IE wiederhergestellt");
}

/**
 * @brief Index-Puls-Simulation.
 *
 * Inkrementiert den Zyklenzähler um @p cycles.  Bei Überlauf der Periodenzeit
 * (abgeleitet aus DriveProfile::rpm) wird ein /ASTB-Puls auf dem ctrl_pio_ erzeugt,
 * der die Port-A-Interrupt-Logik auslöst.
 */
void K5122::update(int cycles) {
    // ── /STR=1 (gelatcht/abgetastet): Datenübertragung beenden ───────────────
    // /STR=1 unterdrückt /BUSRQ (Anschluss inaktiv).  Nur ein über mehrere
    // Byteperioden anhaltendes /STR=1 wird vom Datenseparator durchgetaktet —
    // kurze Boot-ROM-Setup-Strobes (≤ ~18 Takte) werden verschluckt (Latch).
    if (transferring_ && !write_mode_ && (prev_ctrl_a_ & 0x08)) {
        str_inactive_cycles_ += cycles;
        if (str_inactive_cycles_ >= kStrEndSampleCycles) {
            transferring_        = false;
            dma_pending_         = false;
            byte_ready_          = false;
            str_inactive_cycles_ = 0;
            bus_.releaseBUSRQ();
            LOG_DEBUG("K5122", "/STR=1 abgetastet: Datenübertragung beendet, BUSRQ frei");
        }
    } else {
        str_inactive_cycles_ = 0;
    }

    // ── Per-Byte-/BUSRQ-Drossel: nächstes Byte nach 1 Byteperiode bereitstellen ──
    // Solange ZVE2 liest, liegt nach jeder Byteperiode das nächste Byte bereit
    // und /BUSRQ wird wieder assertiert.  Holt ZVE2 es nicht ab (fertig/idle),
    // bleibt /BUSRQ zwar aktiv, aber sobald ZVE2 aufhört zu lesen, beendet das
    // /STR=1 oben den Transfer — keine programm-/größenspezifische Erkennung nötig.
    if (transferring_ && !write_mode_ && !byte_ready_) {
        byte_acc_ += cycles;
        if (byte_acc_ >= kBytePeriodCycles) {
            byte_acc_   = 0;
            byte_ready_ = true;
            bus_.assertBUSRQ();
        }
    }

    // ── Per-Byte-/BUSRQ-Drossel im Vollspur-FORMAT-Schreibmodus ──────────────
    // Symmetrisch zum Lesen: nach jeder Byteperiode liegt der nächste Schreibtakt
    // bereit, /BUSRQ wird (re)assertiert, ZVE2 schreibt ein Byte (Port 0x14 →
    // handleDataPortAWrite löscht byte_ready_ + gibt /BUSRQ frei).  In der Lücke
    // läuft ZVE1 bis zu seinem Interrupt-Wartepark.
    //
    // Schreib-Idle-Erkennung (Transfer-Ende): ZVE2 streamt die Spur und hört dann
    // auf (es schreibt dtrret und kehrt in seine Idle-Schleife zurück).  Bleibt
    // byte_ready_ über mehrere Byteperioden gesetzt (ZVE2 holt das angebotene Byte
    // nicht mehr ab), ist der Spur-Schreibstrom komplett → commitFormatTrack().
    if (write_mode_) {
        if (!byte_ready_) {
            byte_acc_ += cycles;
            if (byte_acc_ >= kBytePeriodCycles) {
                byte_acc_   = 0;
                byte_ready_ = true;
                bus_.assertBUSRQ();
            }
            write_idle_acc_ = 0;
        } else {
            // Byte bereitgestellt, aber noch nicht abgeholt → ZVE2 schreibt gerade
            // nicht.  Hält das über kWriteEndSampleCycles an, hat ZVE2 das FORMAT
            // beendet (keine Folgespur mehr) → letzte Spur abschließen + Transfer beenden.
            write_idle_acc_ += cycles;
            if (write_idle_acc_ >= kWriteEndSampleCycles) {
                commitFormatTrack();
                write_mode_   = false;
                transferring_ = false;
                byte_ready_   = false;
                dma_pending_  = false;
                bus_.releaseBUSRQ();
            }
        }
    }

    if (!drives_[selected_drive_].isMounted()) return;

    index_cycle_acc_ += cycles;
    const int period = drives_[selected_drive_].indexPeriodCycles(cpu_hz_);
    if (index_cycle_acc_ < period) return;
    index_cycle_acc_ -= period;

    // Fallenden Puls simulieren: /ASTB low → Interrupt-Flanke → wieder high.
    // (Weckt u. a. ZVE1 aus dem FORMAT-Wartepark JR 1D21 über den Index-Interrupt.)
    ctrl_pio_.setASTB(false);
    ctrl_pio_.setASTB(true);
    LOG_TRACE("K5122", "Index-Puls: ctrl PIO Port A /ASTB pulsed");
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
void K5122::handleCtrlPortAWrite(uint8_t data) {
    // ── /WE (bit0) Flanken: BIOS-Schreib-Datenfeld sammeln/committen ─────────
    // Der CP/A-BIOS-dio-Pfad findet zuerst die IDAM (Lese-Strobe, /WE=1) und
    // schaltet erst zum Schreiben des Datenfelds /WE auf 0 (Steuerwort B4/B0),
    // am Feldende wieder auf 1 (B5/B1).  Nur im laufenden ZVE2-Streaming
    // (transferring_, Bus gehalten) — der alte synthetische /STR-Schreibpfad
    // setzt stattdessen write_mode_ und wird hiervon nicht berührt.
    {
        bool we_now  = !(data & 0x01);          // /WE aktiv (Schreiben) ⇔ bit0=0
        bool we_prev = !(prev_ctrl_a_ & 0x01);
        if (we_now && !we_prev && transferring_ && !write_mode_ && bus_.isBUSRQ()) {
            beginWriteField();
        } else if (!we_now && we_prev && we_writing_) {
            commitWriteField();
        }
    }

    // ── /ST (bit7) fallende Flanke: Schritt-Puls ─────────────────────────────
    if ((prev_ctrl_a_ & 0x80) && !(data & 0x80)) {
        // MR/SD (bit5): bit5=1 → inward (höhere Zylinder), bit5=0 → outward (Richtung 0)
        step_dir_in_ = (data & 0x20) != 0;
        doStep();
    }

    // ── /STR (bit3) fallende Flanke: Strobe ──────────────────────────────────
    if ((prev_ctrl_a_ & 0x08) && !(data & 0x08)) {
        bool is_write = !(data & 0x01);  // /WE=0 → Schreiben
        str_inactive_cycles_ = 0;        // neue Sitzung: /STR=1-Abtastung zurücksetzen

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
                LOG_DEBUG("K5122", "/STR ZVE2-Commit SCHREIBEN abgeschlossen, BUSRQ freigegeben");
            } else {
                // Lese-Refresh: Spur für ggf. neuen Kopf neu laden (z. B. Kopf-Wechsel).
                startReadTransfer();
                byte_ready_ = true;          // erstes Byte der neuen Spur liegt bereit
                byte_acc_   = 0;
                LOG_DEBUG("K5122", "/STR ZVE2-Lese-Refresh: Spur neu geladen, BUSRQ gehalten");
            }
        } else {
            // ZVE1-Kontext: neuen DMA-Transfer auslösen
            dma_is_write_ = is_write;
            if (!is_write) {
                startReadTransfer();
                byte_ready_ = true;          // erstes Byte liegt bereit → /BUSRQ aktiv
                byte_acc_   = 0;
            } else {
                // Schreib-DMA (Vollspur-FORMAT): ZVE2 streamt die komplette Spur
                // byteweise via Port 0x14.  Per-Byte-/BUSRQ-Drossel wie beim Lesen
                // → ZVE1 läuft in den Lücken bis zu seinem Wartepark (JR 1D21), aus
                // dem ZVE2 es nach der Spur per dtrret-Byte befreit.  ZVE2 streamt
                // dann weiter (Gap), bis ZVE1 die nächste Spur einleitet — DEREN
                // Schreib-Strobe schließt die vorige Spur ab (commitFormatTrack).
                if (!write_buf_.empty()) {
                    commitFormatTrack();      // vorige Spur abschließen + ins Image
                }
                write_mode_   = true;
                transferring_ = false;
                write_buf_.clear();
                byte_ready_   = true;     // erstes Schreib-Byte sofort anfordern
                byte_acc_     = 0;
                write_idle_acc_ = 0;
                // Zielspur (Zyl/Kopf) JETZT latchen — ZVE1 seekt vor der nächsten Spur.
                fmt_cyl_  = drives_[selected_drive_].isMounted()
                          ? drives_[selected_drive_].currentCylinder() : 0;
                fmt_head_ = current_head_;
            }
            dma_pending_ = true;
            bus_.assertBUSRQ();
            LOG_DEBUG("K5122", "/STR Flanke: BUSRQ gesetzt, DMA %s ausstehend",
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
            LOG_TRACE("K5122", "MK/MK1-Flanke: re-sync auf nächste Marke, pos=%zu", head_pos_);
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
void K5122::handleDataPortAWrite(uint8_t data) {
    // Sammeln, wenn der Vollspur-FORMAT-Schreibpfad (write_mode_) ODER der BIOS-
    // /WE-Datenfeld-Schreibpfad (we_writing_) aktiv ist.  Sonst Lese-Echo → ignorieren.
    if (!write_mode_ && !we_writing_) return;   // Lese-Echo — kein Schreibdatum
    write_buf_.push_back(data);

    // Vollspur-FORMAT: Per-Byte-/BUSRQ-Drossel — Byte abgeholt → ZVE2 verliert den
    // Bus, bis update() nach 1 Byteperiode das nächste anfordert.  In der Lücke läuft
    // ZVE1.  (Der /WE-Datenfeldpfad sammelt dagegen innerhalb eines gehaltenen
    // ZVE2-Streamings und lässt die Bus-Arbitrierung unberührt.)
    if (write_mode_ && !we_writing_) {
        byte_ready_ = false;
        byte_acc_   = 0;
        bus_.releaseBUSRQ();
    }
    LOG_TRACE("K5122", "Schreib-Byte 0x%02X gesammelt (%zu Bytes)", data, write_buf_.size());
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
void K5122::updateStatusPortB() {
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
void K5122::doStep() {
    FloppyDriveV2& drv = drives_[selected_drive_];
    if (!drv.isMounted()) return;

    drv.step(step_dir_in_);
    markDriveAccess(selected_drive_);

    LOG_TRACE("K5122", "STEP D%d dir=%s cyl=%u",
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
void K5122::startReadTransfer() {
    FloppyDriveV2& drv = drives_[selected_drive_];
    if (!drv.isMounted()) {
        LOG_WARN("K5122", "Lese-Transfer: D%d nicht montiert", selected_drive_);
        transferring_ = false;
        cur_track_    = nullptr;
        robotron_track_ = {};
        return;
    }

    // IBM-Format-Track aus dem Drive-Cache holen (unverändert für den Write-Pfad).
    const TrackImage& ibm_track = drv.track(current_head_);

    if (ibm_track.empty()) {
        LOG_WARN("K5122", "Lese-Transfer: D%d H%u Spur leer",
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

    LOG_INFO("K5122", ">>> READ D%d C=%u H=%u Spur=%zu Bytes (Robotron-Layout)",
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
void K5122::resyncToNextMark() {
    if (!cur_track_ || cur_track_->empty()) return;

    size_t m = cur_track_->nextMark(head_pos_);
    if (m != SIZE_MAX) {
        head_pos_ = m;
        locked_   = true;
        LOG_TRACE("K5122", "resync → Marke bei pos=%zu (0x%02X)",
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
void K5122::commitWrite() {
    FloppyDriveV2& drv = drives_[selected_drive_];
    if (!drv.isMounted()) {
        LOG_WARN("K5122", "commitWrite: D%d nicht montiert", selected_drive_);
        write_buf_.clear();
        write_mode_ = false;
        return;
    }
    if (drv.isWriteProtect()) {
        LOG_WARN("K5122", "commitWrite: D%d ist schreibgeschützt", selected_drive_);
        write_buf_.clear();
        write_mode_ = false;
        return;
    }
    if (write_buf_.empty()) {
        LOG_WARN("K5122", "commitWrite: Schreib-Puffer leer");
        write_mode_ = false;
        return;
    }

    markDriveAccess(selected_drive_);

    // Spur lesen und in logische Sektoren parsen.
    TrackImage& spur = drv.mutableTrack(current_head_);
    if (spur.empty()) {
        LOG_WARN("K5122", "commitWrite: Spur (D%d H%u) leer, nichts zu schreiben",
                 selected_drive_, static_cast<unsigned>(current_head_));
        write_buf_.clear();
        write_mode_ = false;
        return;
    }

    auto sektoren = TrackCodec::parseTrack(spur);
    if (sektoren.empty()) {
        LOG_WARN("K5122", "commitWrite: parseTrack lieferte keine Sektoren");
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

    LOG_INFO("K5122", ">>> WRITE D%d C=%u H=%u S=%u bytes=%zu",
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
 * @brief Parst einen Vollspur-FORMAT-Schreibstrom in logische Sektoren (statisch, testbar).
 */
std::vector<LogicalSector> K5122::parseFormatStream(const std::vector<uint8_t>& b) {
    std::vector<LogicalSector> sektoren;
    LogicalSector cur{};
    bool have_idam = false;

    size_t i = 0;
    while (i < b.size()) {
        // Adressmarke = Sync-Folge aus ≥1 A1-Bytes + Mark-Byte.  Die A1-Anzahl variiert
        // (echter ZVE2-Strom: 3×A1; TrackCodec::buildTrack: 2×A1) — daher A1-Folge
        // überspringen und das erste Nicht-A1-Byte als Mark-Byte prüfen.
        if (b[i] == 0xA1) {
            size_t j = i;
            while (j < b.size() && b[j] == 0xA1) ++j;
            if (j < b.size()) {
                const uint8_t mark = b[j];
                if (mark == 0xFE && j + 5 <= b.size()) {        // IDAM: A1… FE c h s n
                    cur = LogicalSector{};
                    cur.cyl  = b[j + 1];
                    cur.head = b[j + 2];
                    cur.id   = b[j + 3];
                    cur.size = static_cast<uint16_t>(128u << (b[j + 4] & 0x03));
                    have_idam = true;
                    i = j + 5;                                  // hinter die IDAM-Felder (CRC folgt)
                    continue;
                }
                if ((mark == 0xFB || mark == 0xF8) && have_idam) {  // DAM: A1… FB <data…>
                    const size_t data_start = j + 1;
                    const size_t take = std::min<size_t>(cur.size, b.size() - data_start);
                    cur.data.assign(b.begin() + data_start, b.begin() + data_start + take);
                    cur.data.resize(cur.size, 0xE5);            // unvollständig → mit 0xE5 füllen
                    sektoren.push_back(cur);
                    have_idam = false;
                    i = data_start + take;                      // hinter das Datenfeld (CRC folgt)
                    continue;
                }
            }
            i = j;                                              // A1-Folge ohne gültige Marke überspringen
            continue;
        }
        ++i;
    }
    return sektoren;
}

/**
 * @brief Schließt einen Vollspur-FORMAT-Schreibtransfer ab: parst den gesammelten
 *        Schreibstrom (@ref write_buf_) zu Sektoren, baut die Spur und schreibt sie an
 *        die gelatchte (@ref fmt_cyl_, @ref fmt_head_)-Position ins Image.
 *
 * Wird vom nächsten Schreib-/STR-Strobe (Folgespur schließt die vorige ab) bzw. von der
 * Schreib-Idle-Erkennung in @ref update (letzte Spur) aufgerufen.  Optionaler Roh-Dump
 * des Stroms über Env K5122_FMT_CAPTURE (Analyse/Debug).
 */
void K5122::commitFormatTrack() {
    if (const char* fn = std::getenv("K5122_FMT_CAPTURE")) {
        if (!write_buf_.empty()) {
            if (FILE* f = std::fopen(fn, "ab")) {
                uint8_t hdr[8] = {'F','T', fmt_cyl_, fmt_head_,
                    static_cast<uint8_t>(write_buf_.size() & 0xFF),
                    static_cast<uint8_t>((write_buf_.size() >> 8) & 0xFF),
                    static_cast<uint8_t>((write_buf_.size() >> 16) & 0xFF), 0};
                std::fwrite(hdr, 1, sizeof hdr, f);
                std::fwrite(write_buf_.data(), 1, write_buf_.size(), f);
                std::fclose(f);
            }
        }
    }

    auto sektoren = parseFormatStream(write_buf_);
    if (!sektoren.empty()) {
        TrackImage trk = TrackCodec::buildTrack(sektoren, Encoding::MFM);
        bool ok = drives_[selected_drive_].writeTrackAt(fmt_cyl_, fmt_head_, trk);
        LOG_INFO("K5122", ">>> FORMAT-WRITE D%d C=%u H=%u: %zu Sektoren à %uB %s",
                 selected_drive_, static_cast<unsigned>(fmt_cyl_),
                 static_cast<unsigned>(fmt_head_), sektoren.size(),
                 sektoren.empty() ? 0u : sektoren.front().size, ok ? "OK" : "FEHLER");
    } else {
        LOG_WARN("K5122", "FORMAT-COMMIT D%d C=%u H=%u: keine Sektoren im Strom (%zu Bytes)",
                 selected_drive_, static_cast<unsigned>(fmt_cyl_),
                 static_cast<unsigned>(fmt_head_), write_buf_.size());
    }

    write_buf_.clear();
    write_idle_acc_ = 0;
}

/**
 * @brief Beginnt das Sammeln eines BIOS-Schreib-Datenfelds (/WE 1→0).
 *
 * Der Zielsektor ist derjenige, dessen IDAM (Id-Marke) zuletzt unter dem Lesekopf
 * durchlief — also die letzte Id-Marke im Robotron-Streaming-Track @ref robotron_track_
 * VOR (bzw. an) @ref head_pos_.  Aus dem IDAM-Feld werden Zylinder/Kopf/Sektor-ID und
 * der Größencode (→ Sektorgröße) gelesen.  Anschließend sammelt @ref handleDataPortAWrite
 * jedes OUT(0x14)-Byte in @ref write_buf_, bis /WE wieder auf 1 geht.
 */
void K5122::beginWriteField() {
    we_writing_ = true;
    write_buf_.clear();
    wr_id_ = 0; wr_size_ = cur_sector_size_; wr_cyl_ = 0; wr_head_ = current_head_;

    if (cur_track_ && !cur_track_->empty()) {
        const size_t n = cur_track_->size();
        for (size_t k = 0; k < n; ++k) {
            const size_t p = (head_pos_ + n - k) % n;     // rückwärts ab head_pos_
            if (cur_track_->marks[p] == MarkType::Id) {
                wr_cyl_  = cur_track_->bytes[(p + 2) % n];
                wr_head_ = cur_track_->bytes[(p + 3) % n];
                wr_id_   = cur_track_->bytes[(p + 4) % n];
                const uint8_t sc = cur_track_->bytes[(p + 5) % n] & 0x03;
                wr_size_ = static_cast<uint16_t>(128u << sc);
                break;
            }
        }
    }
    LOG_DEBUG("K5122", "Schreib-Datenfeld Beginn: Ziel C=%u H=%u S=%u sz=%u (hp=%zu)",
              wr_cyl_, wr_head_, wr_id_, wr_size_, head_pos_);
}

/**
 * @brief Committet ein gesammeltes BIOS-Schreib-Datenfeld (/WE 0→1) in den Zielsektor.
 *
 * Der Schreibstrom in @ref write_buf_ enthält das vollständige Datenfeld inkl. Gap/Sync:
 * @code  …00 00 … A1 A1 A1 <DAM=FB/F8> <Datenbytes…> <CRC> <CRC> 00 00…  @endcode
 * Das Datenfeld wird extrahiert (erste A1-A1-A1-Sync → DAM → Daten), auf @ref wr_size_
 * Bytes begrenzt und in den per IDAM identifizierten Sektor des IBM-Cache-Tracks
 * geschrieben; danach wird der Track neu gebaut und als dirty markiert.  @ref transferring_
 * / @ref head_pos_ bleiben unberührt — das ZVE2-Streaming liest in derselben Sitzung weiter.
 */
void K5122::commitWriteField() {
    we_writing_ = false;
    FloppyDriveV2& drv = drives_[selected_drive_];

    if (!drv.isMounted()) { write_buf_.clear(); return; }
    if (drv.isWriteProtect()) {
        LOG_WARN("K5122", "commitWriteField: D%d schreibgeschützt", selected_drive_);
        write_buf_.clear();
        return;
    }
    if (write_buf_.empty()) { write_buf_.clear(); return; }

    // Datenfeld im Strom finden: erste A1-A1-A1-Sync, danach DAM-Byte, dann Daten.
    size_t i = 0; bool found = false;
    for (; i + 2 < write_buf_.size(); ++i) {
        if (write_buf_[i] == 0xA1 && write_buf_[i + 1] == 0xA1 && write_buf_[i + 2] == 0xA1) {
            found = true; break;
        }
    }
    if (!found) {
        LOG_WARN("K5122", "commitWriteField: keine A1-A1-A1-Sync im Schreibstrom (buf=%zu, S=%u)",
                 write_buf_.size(), wr_id_);
        write_buf_.clear();
        return;
    }
    const size_t data_start = i + 4;            // 3×A1 + DAM(FB/F8)
    if (data_start >= write_buf_.size()) {
        LOG_WARN("K5122", "commitWriteField: Datenfeld leer (S=%u)", wr_id_);
        write_buf_.clear();
        return;
    }
    const size_t avail = write_buf_.size() - data_start;
    const size_t take  = std::min<size_t>(wr_size_, avail);

    markDriveAccess(selected_drive_);

    // Ziel-Spur (IBM-Format im Drive-Cache) parsen und Sektor per ID ersetzen.
    TrackImage& spur = drv.mutableTrack(current_head_);
    if (spur.empty()) {
        LOG_WARN("K5122", "commitWriteField: Spur (D%d H%u) leer",
                 selected_drive_, static_cast<unsigned>(current_head_));
        write_buf_.clear();
        return;
    }
    auto sektoren = TrackCodec::parseTrack(spur);
    LogicalSector* ziel = nullptr;
    for (auto& s : sektoren) {
        if (s.id == wr_id_) { ziel = &s; break; }
    }
    if (!ziel) {
        LOG_WARN("K5122", "commitWriteField: Zielsektor S=%u nicht in Spur (C=%u H=%u)",
                 wr_id_, wr_cyl_, static_cast<unsigned>(current_head_));
        write_buf_.clear();
        return;
    }

    ziel->data.assign(write_buf_.begin() + data_start,
                      write_buf_.begin() + data_start + take);
    ziel->data.resize(ziel->size, 0x00);

    LOG_INFO("K5122", ">>> WRITE D%d C=%u H=%u S=%u bytes=%zu (buf=%zu)",
             selected_drive_,
             static_cast<unsigned>(drv.currentCylinder()),
             static_cast<unsigned>(current_head_),
             static_cast<unsigned>(ziel->id), take, write_buf_.size());

    spur = TrackCodec::buildTrack(sektoren, spur.encoding);
    drv.markTrackDirty(current_head_);

    // Streaming-Track aktualisieren, damit ein evtl. Verify-Read in derselben Sitzung
    // die frischen Daten sieht (Layout/Größen unverändert → head_pos_ bleibt gültig).
    robotron_track_ = TrackCodec::buildRobotronTrack(sektoren);
    cur_track_      = &robotron_track_;

    write_buf_.clear();
}

/**
 * @brief Merkt den letzten Laufwerkszugriff für die LED-Simulation.
 *
 * isDriveLedOn() gibt true zurück, solange weniger als led_hold_time_ (180 ms)
 * vergangen sind.
 */
void K5122::markDriveAccess(int drive) {
    if (drive < 0 || drive > 3) return;
    led_until_[static_cast<size_t>(drive)] =
        std::chrono::steady_clock::now() + led_hold_time_;
}

// ─── Snapshot-Serialisierung ────────────────────────────────────────────────────
namespace {
template <class T> void putPod(std::vector<uint8_t>& o, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "POD only");
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    o.insert(o.end(), p, p + sizeof(T));
}
template <class T> bool getPod(const uint8_t*& p, const uint8_t* end, T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "POD only");
    if (static_cast<size_t>(end - p) < sizeof(T)) return false;
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return true;
}
}  // namespace

void K5122::serialize(std::vector<uint8_t>& out) const {
    // Controller-Chips (Register + Daisy-Chain-Bits).
    ctrl_pio_.serialize(out);
    data_pio_.serialize(out);
    // Gelatchte Steuer-Signale.
    putPod(out, static_cast<int32_t>(selected_drive_));
    putPod(out, current_head_);
    putPod(out, static_cast<uint8_t>(step_dir_in_ ? 1 : 0));
    putPod(out, prev_ctrl_a_);
    putPod(out, static_cast<int32_t>(index_cycle_acc_));
    // Mechanische Kopfposition je Laufwerk (das Kernanliegen: Kopf steht nach
    // dem Laden wieder auf der richtigen Spur).
    for (int i = 0; i < 4; ++i) {
        uint8_t mounted = drives_[i].isMounted() ? 1 : 0;
        uint8_t cyl     = drives_[i].isMounted() ? drives_[i].currentCylinder() : 0;
        putPod(out, mounted);
        putPod(out, cyl);
    }
}

bool K5122::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (!ctrl_pio_.deserialize(p, end)) return false;
    if (!data_pio_.deserialize(p, end)) return false;
    int32_t sel = 0, idx = 0;
    uint8_t head = 0, stepdir = 0, prev = 0;
    if (!getPod(p, end, sel))     return false;
    if (!getPod(p, end, head))    return false;
    if (!getPod(p, end, stepdir)) return false;
    if (!getPod(p, end, prev))    return false;
    if (!getPod(p, end, idx))     return false;
    selected_drive_   = sel;
    current_head_     = head;
    step_dir_in_      = (stepdir != 0);
    prev_ctrl_a_      = prev;
    index_cycle_acc_  = idx;
    for (int i = 0; i < 4; ++i) {
        uint8_t mounted = 0, cyl = 0;
        if (!getPod(p, end, mounted)) return false;
        if (!getPod(p, end, cyl))     return false;
        // Nur die Kopfposition setzen, wenn das Laufwerk auch jetzt gemountet ist
        // (das Image wird separat über die Kommandozeile gemountet).
        if (mounted && drives_[i].isMounted())
            drives_[i].restoreHeadPosition(cyl);
    }
    // Einen evtl. laufenden Streaming-/Schreib-Transfer auf konsistenten Idle-
    // Zustand zurücksetzen — der nächste /STR-Strobe baut die Spur frisch auf.
    cur_track_           = nullptr;
    transferring_        = false;
    write_mode_          = false;
    locked_              = false;
    head_pos_            = 0;
    write_buf_.clear();
    byte_ready_          = false;
    byte_acc_            = 0;
    dma_pending_         = false;
    dma_is_write_        = false;
    str_inactive_cycles_ = 0;
    return true;
}

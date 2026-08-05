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
 * @see doc/design/07_k5122_afs.md
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
 * Jeder Slot wird mit dem übergebenen DriveProfile initialisiert (Default: K5601).
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
                if (!motor_on_[static_cast<size_t>(selected_drive_)]) {
                    // Motor steht (nicht selektiert/verriegelt): die Scheibe rotiert nicht →
                    // unter dem Kopf liegen keine kohärenten Daten.  Wir liefern reinen
                    // Gap-Fluss (kein Marken-Byte) und halten die Kopfposition (kein Vorlauf)
                    // — die Byte-Drossel/BUSRQ bleibt unberührt (kein Hang).  Sobald der Motor
                    // wieder läuft, läuft der Strom ab head_pos_ normal weiter.
                    // (Das Index-Gating berücksichtigt zusätzlich den Spin-up, s. update();
                    //  die kurze Anlaufphase ist für den byteweisen Lesestrom vernachlässigbar
                    //  und liegt real ohnehin lange vor dem ersten Read.)
                    result = 0x4E;
                } else {
                    const size_t pos     = head_pos_;
                    const bool   is_mark = cur_track_->marks[pos] != MarkType::None;
                    result = cur_track_->bytes[pos];
                    // Falscher Aufzeichnungsmodus (read_enc_ ≠ Spur-Codierung): der
                    // Datenseparator demoduliert die Marken-/Datenbytes als Müll → die
                    // Marke ist "ungültig" (≠ FE/FB/A1), ZVE2 findet kein IDAM.  Gaps
                    // sind weder FM noch MFM und kommen unverändert durch.  So scheitert
                    // ein Read im falschen Verfahren (z. B. ROM-FM-Probe auf MFM-Spur),
                    // das ROM läuft in den Index-Timeout und toggelt MK (FM↔MFM).
                    if (is_mark && effReadEnc() != cur_track_->encoding) {
                        result = 0x00;
                    }
                    head_pos_ = (head_pos_ + 1) % cur_track_->size();
                }
                // Byte abgeholt → einen Byte-Slot konsumieren (Spacing: die im
                // ZVE2-Fenster aufgelaufene Phase bleibt erhalten, s. consumeByteSlot()).
                consumeByteSlot();
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
                "CTRL PortA write 0x%02X  /ST=%d /HL=%d MR/SD=%d MK1=%d /STR=%d /FR=%d MK=%d /WE=%d",
                data,
                (data >> 7) & 1, (data >> 6) & 1, (data >> 5) & 1, (data >> 4) & 1,
                (data >> 3) & 1, (data >> 2) & 1, (data >> 1) & 1, data & 1);
        } else {
            LOG_DEBUG("K5122", "CTRL PIO write port=0x%02X data=0x%02X", port, data);
        }
        // ── Vollspur-FORMAT: Disketten-Index-Interrupt aktiv halten ──────────────
        // Manche FORMAT-Programme treiben den Format-Abschluss über den
        // Disketten-Index-Interrupt (ivdsk1, Vektor 0xE8): das Programm hängt eine
        // eigene ISR ein, die bei jedem Index eine ZVE2-Warteschleife per Selbst-
        // modifikation freigibt — erst nach mehreren Index-Interrupts läuft ZVE2 zu
        // Ende und weckt ZVE1.  Der BIOS-Motor-Abschalt-Watchdog (headup, 0xE3BF)
        // schreibt jedoch beim Ablauf seines Index-Zählers `OUT(11H)=0x03` (Port-A-
        // Interrupt sperren).  Auf echter Hardware ist dieser Watchdog während einer
        // laufenden Übertragung unterdrückt; da unser Vollspur-FORMAT-Write am BIOS-
        // dio vorbeiläuft, wird der Zustand nicht gesetzt und der Index würde nach dem
        // ersten Interrupt abgeschaltet → Deadlock (ZVE2 hängt in der Trailing-Gap-
        // Schleife, ZVE1 ewig im JR$-Wartepark).  Solange ein FORMAT-Write läuft,
        // ignorieren wir daher das Port-A-Interrupt-Sperrwort (Bits3-0=0011, Bit7=0).
        if (port == 0x11 && write_mode_ && (data & 0x8F) == 0x03) {
            LOG_DEBUG("K5122", "FORMAT: OUT(11H)=0x%02X (Index-INT-Sperre) ignoriert", data);
        } else {
            ctrl_pio_.ioWrite(port - 0x10, data);
        }
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
        // 8212 (A4): **high** nibble = /SE0../SE3 (Select), **low** nibble =
        // /LCK0../LCK3 (Lock = /Motor On), beide active-low (K5122-Doku §4.2).
        // 0xEE → high 1110, bit4=0 → Drive 0 selektiert (0xDD→D1, 0xBB→D2, 0x77→D3);
        // die BIOSse bilden das Byte als `LD A,77H / RLCA (LW+1)×` — dabei fällt je
        // ein /SE- UND ein /LCK-Bit, deshalb ist die Nibbelzuordnung an 0xEE/0xDD
        // NICHT ablesbar.  Entschieden wird sie von den Aufrufern, die genau EIN
        // Nibble maskieren:
        //   CP/A-Laufwerkserkennung (Bootsektor 0x021E): `LD A,0F7H` „ohne lock",
        //     RLCA je Laufwerk → 0xEF/0xDF/0xBF/0x77 und steppt dann GENAU DIESES
        //     Laufwerk (Spur-0-Signal) → das variierende High-Nibble-Bit ist /SE.
        //   UDOS-Floppytreiber (UNFLOPPY.MAC): `OR 0FH` („LW EIN") → 0xEF/0xDF/…,
        //     UDOS 4.3 auf dem A5120: `AND 0F0H` → 0xE0/0xD0/… (alle /LCK aktiv).
        // Beide Varianten variieren ausschließlich das High-Nibble ⇒ /SE.  Mit der
        // früheren (vertauschten) Zuordnung landete UDOS' 0xD0 auf „alle vier
        // selektiert" → Laufwerk 0 statt 1 (FORMAT schrieb B:, verifizierte A:).
        uint8_t sel = ~(data >> 4) & 0x0F;
        selected_drive_ = (sel == 0) ? 0
                        : (sel & 0x01) ? 0
                        : (sel & 0x02) ? 1
                        : (sel & 0x04) ? 2
                        : 3;
        // Select-/Motor-Zustand je Laufwerk ableiten (RESET → 0xFF → alles aus).  Der
        // Motor läuft, solange /LCK=0; die LED folgt „selektiert ODER Motor an".
        for (int d = 0; d < 4; ++d) {
            const size_t di  = static_cast<size_t>(d);
            const bool   mot = ((data >> d) & 1) == 0;
            // Motor-Anlaufflanke (aus→an): Spin-up armieren.  Ein bereits laufender
            // Motor (an→an) läuft weiter, kein Neu-Anlauf.
            if (mot && !motor_on_[di]) motor_spinup_cycles_[di] = motorSpinupCycles();
            drive_selected_[di] = ((data >> (4 + d)) & 1) == 0;
            motor_on_[di]       = mot;
        }
        LOG_INFO("K5122", "8212 write=0x%02X => sel D%d | SE=%d%d%d%d MotorOn=%d%d%d%d",
                 data, selected_drive_,
                 drive_selected_[0], drive_selected_[1], drive_selected_[2], drive_selected_[3],
                 motor_on_[0], motor_on_[1], motor_on_[2], motor_on_[3]);
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
 * parseTrack → buildTrack) unverändert funktioniert.  Der treue FM/MFM-Lese-Stream für den
 * Lese-Streaming-Pfad wird von startReadTransfer() on-the-fly erzeugt.
 */
bool K5122::mountDisk(int drive, const std::string& path,
                        const DiskFormat& fmt, bool write_protect) {
    if (drive < 0 || drive > 3) return false;
    // Das Verfahren steht im DiskFormat (pro Spurbereich, §8.6) — für .hfe ohnehin in
    // der Datei selbst.  Es wird nicht mehr aus dem Laufwerk abgeleitet.
    auto img = DiskImage::open(path, fmt, write_protect);
    if (!img) {
        // Öffnen/Erkennen fehlgeschlagen (unbekanntes/leeres/kaputtes Image) — für
        // eine aussagekräftige GUI-Meldung im Laufwerks-Fehler hinterlegen.
        drives_[drive].setLastError("Image konnte nicht geoeffnet werden: " + path);
        return false;
    }
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

void K5122::autoFlushDisks(uint64_t now_cycles) {
    for (auto& d : drives_) d.autoFlush(now_cycles);
}

bool K5122::flushDisks() {
    bool ok = true;
    for (auto& d : drives_) ok = d.flush() && ok;
    return ok;
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
    // Signal-treu: LED an, solange das Laufwerk selektiert (/SE) ODER sein Motor
    // (/LCK) an ist — abgeleitet aus dem letzten OUT(18H) (8212), keine Wanduhr.
    return drive_selected_[static_cast<size_t>(drive)] || motor_on_[static_cast<size_t>(drive)];
}

bool K5122::isMotorOn(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return motor_on_[static_cast<size_t>(drive)];
}

bool K5122::motorAtSpeed(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return motor_on_[static_cast<size_t>(drive)] &&
           motor_spinup_cycles_[static_cast<size_t>(drive)] <= 0;
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
void K5122::reset() {
    // Laufenden Transfer abbrechen und den Bus freigeben — sonst startet die neue
    // Boot-Kette in einen halb offenen DMA-Handshake des alten OS hinein.
    transferring_ = write_mode_ = we_writing_ = false;
    dma_pending_  = dma_is_write_ = false;
    byte_ready_   = false;
    byte_acc_     = 0;
    str_inactive_cycles_ = 0;
    write_idle_acc_      = 0;
    post_write_grace_    = 0;
    write_buf_.clear();
    cur_track_ = nullptr;
    head_pos_  = 0;
    locked_    = false;
    if (bus_.isBUSRQ()) bus_.releaseBUSRQ();

    // Beide PIOs und die gelatchten Steuersignale in den Einschaltzustand.
    ctrl_pio_.reset();
    data_pio_.reset();
    prev_ctrl_a_ = 0xFF;
    loaded_cyl_  = loaded_head_ = 0xFF;   // Lese-Spur beim nächsten /STR neu laden
    read_enc_overridden_ = false;
    head_loaded_ = false;
    index_cycle_acc_ = 0;
    LOG_INFO("K5122", "Hardware-Reset: Transfer abgebrochen, /BUSRQ frei, PIOs zurückgesetzt");
}

void K5122::endDmaTransfer() {
    if (!bus_.isBUSRQ()) return;
    transferring_ = false;
    dma_pending_  = false;
    bus_.releaseBUSRQ();
    LOG_DEBUG("K5122", "endDmaTransfer: ZVE2 DMA fertig, BUSRQ freigegeben");
}

/**
 * @brief Beendet den os-gated „gehaltenen" Laufzeit-Lese-Transfer (analyse_scpx_com_load §9.4b).
 *
 * Stoppt die Per-Byte-/BUSRQ-Drossel und gibt /BUSRQ frei, sodass ZVE1 aus seinem Poll-Wait
 * fortfährt (JP (HL)).  Anders als endDmaTransfer() wird die ctrl-PIO-Port-A-Interrupt-Freigabe
 * NICHT verändert — SCPX' ZVE2-Lese-Koroutine löscht sie nicht (kein OUT(11H,03H)), also darf
 * sie hier nicht spurios wiederhergestellt werden.
 */
void K5122::releaseHeldRead() {
    // Im Post-Write-Verify-Gnadenfenster den Streaming-Zustand NICHT abreißen: SCPX
    // ruft direkt nach dem Schreib-Commit diese Release-Kante ([EC0B]:E8B5→E929),
    // dispatcht dann aber ZVE1 zum Nachfolge-Verify-Read auf DERSELBEN Spur.  Bleibt
    // transferring_ hier erhalten, engagiert der os-Gate beim ersten E8B5-Besuch der
    // Verify-Runde sofort wieder (isReadTransferActive()==true → assertBUSRQ →
    // zve2StartFromReset), und ZVE2 setzt die Suche mit noch gültigem head_pos_/
    // cur_track_ fort (kontinuierliche Rotation, kein Rewind).  Ohne diese Ausnahme
    // bleibt ZVE2 im Reset (kein /BUSRQ-Restart), ~1 Mio Takte später kapert der
    // Index-ISR den Handshake-Vektor [EC0B]=E998 → ungeprüfter Blindscan → E975 →
    // „BAD SECTOR" (doc/analyse_scpx_com_load.md §11).  Normale Lese-Completions
    // (post_write_grace_==0) verhalten sich unverändert → keine Read-Regression.
    // Nur transferring_ (das isReadTransferActive-Signal) wird im Gnadenfenster
    // erhalten; dma_pending_/byte_ready_ werden IMMER zurückgesetzt, damit die
    // aktuelle Byte-DMA sauber terminiert (sonst streamt ZVE2 endlos weiter).
    if (post_write_grace_ <= 0) transferring_ = false;
    dma_pending_         = false;
    byte_ready_          = false;
    str_inactive_cycles_ = 0;
    if (bus_.isBUSRQ()) bus_.releaseBUSRQ();
    LOG_DEBUG("K5122", "releaseHeldRead: gehaltener Laufzeit-Read beendet, BUSRQ frei%s",
              post_write_grace_ > 0 ? " (Post-Write-Gnadenfenster: Stream bleibt engaged)" : "");
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
    if (post_write_grace_ > 0) post_write_grace_ -= cycles;
    if (transferring_ && !write_mode_ && (prev_ctrl_a_ & 0x08)) {
        str_inactive_cycles_ += cycles;
        // Im Post-Write-Verify-Fenster längere Schwelle (s. k5122.h): der Stream
        // muss die Dispatch-Lücke bis zum ZVE2-Neustart überleben.
        const int str_end_thr = post_write_grace_ > 0 ? kPostWriteStrEndCycles
                                                       : strEndSampleCycles();
        if (str_inactive_cycles_ >= str_end_thr) {
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
    if (transferring_ && !write_mode_) {
        advanceByteClock(cycles);   // freilaufendes Raster (Spacing): auch während ZVE2 läuft
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
        advanceByteClock(cycles);   // freilaufendes Raster (Spacing), wie beim Lesen
        if (byte_ready_) {
            // Byte bereitgestellt, aber (noch) nicht abgeholt → ZVE2 schreibt gerade
            // nicht.  Hält das über kWriteEndSampleCycles an, hat ZVE2 das FORMAT
            // beendet (keine Folgespur mehr) → letzte Spur abschließen + Transfer beenden.
            // (Im laufenden FORMAT holt ZVE2 jedes Byte binnen ~1 Byteperiode ab → ~0.)
            write_idle_acc_ += cycles;
            if (write_idle_acc_ >= kWriteEndSampleCycles) {
                commitFormatTrack();
                write_mode_   = false;
                transferring_ = false;
                byte_ready_   = false;
                dma_pending_  = false;
                bus_.releaseBUSRQ();
            }
        } else {
            write_idle_acc_ = 0;
        }
    }

    // ── Motor-Anlauf (Spin-up) je Laufwerk fortschreiben ─────────────────────
    // Nach dem Motor-On (/LCK) läuft die Scheibe erst nach der Spin-up-Zeit auf
    // Drehzahl.  Auch nicht selektierte Laufwerke laufen an (das BIOS spinnt sie
    // vor), damit sie beim späteren Selektieren bereits „auf Drehzahl" sind.
    for (int d = 0; d < 4; ++d) {
        int& rem = motor_spinup_cycles_[static_cast<size_t>(d)];
        if (motor_on_[static_cast<size_t>(d)] && rem > 0) {
            rem -= cycles;
            if (rem <= 0) {
                rem = 0;
                // Auf Drehzahl → /RDYL des selektierten Laufwerks nachziehen (der
                // Statusbyte-Latch wird sonst nur bei Port-Schreibzugriffen erneuert).
                if (d == selected_drive_) updateStatusPortB();
            }
        }
    }

    if (!drives_[selected_drive_].isMounted()) return;

    // ── Index-Gating: keine Rotation ⇒ kein Index ────────────────────────────
    // Steht der Motor des selektierten Laufwerks (aus oder noch im Anlauf), dreht
    // die Scheibe nicht → kein Index-Puls.  Die Phase startet nach dem Anlauf frisch
    // (index_cycle_acc_ auf 0), wie auf echter HW nach dem Motor-Neuanlauf.
    if (!motorAtSpeed(selected_drive_)) {
        index_cycle_acc_ = 0;
        return;
    }

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
    // ── /HL (bit6, active-low): Kopf-Aufsetz-Zustand latchen ─────────────────
    // 0 = Kopf aufgesetzt (Head Load), 1 = Kopf abgehoben.  Reiner Zustand für
    // Statusabfrage/GUI; das Lese-/Index-Gating hängt (noch) am Motor, nicht am /HL.
    head_loaded_ = !(data & 0x40);

    // ── Seitenwahl /FR (bit2): NUR am Pfad-/Lese-Steuerwort und am /STR-Schreib-Edge ──
    // Handbuch K5122 (Steckerbelegung A2 = /FAULT-RESET, bei Doppelkopf umgewidmet zu
    // /HS = Kopfauswahl): bit2 = 1 → Kopf 0,  bit2 = 0 → Kopf 1.  Die Seite wird NICHT
    // bei jedem Port-A-Schreiben aus bit2 übernommen — das flippte INITs (SCPX) Kopf-1-
    // Verify falsch: dessen Schleife alterniert das Pfadbyte 0x81 (bit2=0 → Kopf 1) mit dem
    // Resync-Strobe 0xB5 (bit4/5 = MK1/MR; bit2 inzidentell 1 → würde Kopf auf 0 kippen).
    // Die ECHTE Seitenwahl trägt nur das Pfad-/Lese-Steuerwort ((data&0xF9)==0x81, s.u.)
    // und der /STR-Format-Write-Edge (is_write, s.u.); die /STR-Lese-/Resync-Strobes haben
    // bit2 inzidentell und dürfen die gewählte Seite NICHT überschreiben.  Kopf-Latching
    // erfolgt daher unten via setHead() an genau diesen beiden Stellen.
    // Einseitige Laufwerke (8″-SD wie MF3200) haben physisch nur Kopf 0 → /FR wirkungslos.

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

    // ── „Lesen-Marke"-/Pfadbyte [0x03FD] (0x81/0x83/0x85/0x87): Verfahren + Seite ──
    // Das Boot-ROM/der Lader schreibt vor JEDER Sektor-Lese-Iteration ein Pfadbyte nach
    // Port 0x10 (ZVE2 @0x025F, ROM @0x01AD).  Es trägt die PERSISTENTE Wahl von
    //   bit1 = Verfahren  (0 = MFM / 1 = FM),   doc/cpa_format_detection.md §93
    //   bit2 = /FR-Seite  (1 = Kopf 0 / 0 = Kopf 1)
    // Muster: bit7=1, bit0=1, bits6..3 = 0  →  (data & 0xF9) == 0x81 (= 0x81/0x83/0x85/0x87).
    // Die /STR-/Step-Strobes (0xB5/0xBD/0xA5/0xAD/0x2D…) erfüllen es NICHT und stören daher
    // weder Verfahren noch Seite.  Wichtig für die Seitenwahl: die /STR-Strobes haben bit2
    // fest = 1 (Kopf 0); die ECHTE Seite steht nur im Pfadbyte, das ERST NACH dem /STR-Strobe
    // geschrieben wird (und unmittelbar vor dem IN(16) gilt).  Deshalb muss die Seite hier
    // (aus dem Pfadbyte) gelatcht werden, nicht am /STR-Edge (dort immer Kopf 0) — sonst ist
    // die zweiseitige Boot-Lesung (ROBOTRON-/SCPX-Lader liest Kopf 1) unerreichbar.
    if ((data & 0xF9) == 0x81) {
        read_enc_            = (data & 0x02) ? Encoding::FM : Encoding::MFM;
        read_enc_overridden_ = true;
        setHead(data);   // ECHTE Seitenwahl (bit2/FR) — nur am Pfad-/Lese-Steuerwort

        // Ein Pfad-/Lese-Steuerwort (bit0=/WE=1 → Lese-Absicht) armiert den Streaming-Read
        // DIREKT — auch OHNE vorangehenden bit3-/STR-Lese-Strobe.  Boot-ROM/BIOS fahren zwar
        // stets erst den /STR-Read-Strobe (der armiert) und dann das Pfadbyte (Seitenwahl);
        // INIT.COM (SCPX) verifiziert dagegen unmittelbar nach dem Format-Write mit
        // OUT(10H),0x85 + IN(16H) OHNE /STR-Read-Strobe.  Läge dann noch write_mode_ an,
        // fiele IN(16H) auf den 0xFF-PIO-Fallback (kein Streaming) → INIT sähe statt des
        // A1/FE-Syncs 0xFF und verwürfe JEDE Spur (alle „bad").  Daher hier (neu) armieren,
        // sofern wir nicht ohnehin schon korrekt für (Zyl,Kopf) streamen — der Boot-
        // Seitenwechsel (transferring_, nur Kopf ändert sich) verhält sich wie zuvor.
        const bool streaming_ok =
            transferring_ && !write_mode_ && cur_track_ != nullptr &&
            loaded_head_ == current_head_ &&
            loaded_cyl_  == drives_[selected_drive_].currentCylinder();
        if (!streaming_ok) {
            // Steht noch ein ungeschriebener Format-Strom an (Verify direkt nach dem
            // Write, bevor die write-idle-Erkennung committet hat), zuerst die Spur sichern
            // — sonst läse der Verify die alte (Vor-Format-)Spur.
            if (write_mode_ && !write_buf_.empty()) commitFormatTrack();
            write_mode_ = false;
            startReadTransfer();
        }
    }

    // ── /ST (bit7) fallende Flanke: Schritt-Puls ─────────────────────────────
    if ((prev_ctrl_a_ & 0x80) && !(data & 0x80)) {
        // MR/SD (bit5): bit5=1 → inward (höhere Zylinder), bit5=0 → outward (Richtung 0)
        step_dir_in_ = (data & 0x20) != 0;
        doStep();
    }

    // ── /STR (bit3) steigende Flanke DURCH ZVE2: Busbesitz sofort beenden ────
    // /STR=1 macht den Anschluss inaktiv (K5122-Doku §5.5).  Setzt ZVE2 es selbst,
    // ist seine DMA-Koroutine fertig — /BUSRQ fällt, ZVE2 friert im /WAIT ein und
    // führt KEINE weitere Instruktion aus.  Genau darauf verlässt sich UDOS: hinter
    // dem abschließenden `OUT (10H)` steht ein totes `RET`, das sich seinen
    // Rücksprung aus dem CRC-Puffer holt (die Koroutine setzt `LD SP,0E50H`) und
    // per RST-38-PUSH die CRC des ersten Sektors zerschreibt → „ERROR: C6".
    // ZVE1-Schreibzugriffe mit /STR=1 während einer laufenden DMA bleiben von der
    // Abtastung in update() abgedeckt: auf echter Hardware ist ZVE1 währenddessen
    // angehalten, sie sind ein Artefakt unserer verschränkten Ausführung.
    if (transferring_ && !write_mode_ && !(prev_ctrl_a_ & 0x08) && (data & 0x08)
        && bus_.busMasterIsZVE2() && bus_.isBUSRQ()) {
        prev_ctrl_a_ = data;
        bus_.releaseBUSRQ();
        LOG_DEBUG("K5122", "/STR=1 von ZVE2: Busbesitz beendet, ZVE2 haelt an");
        return;
    }

    // ── /STR (bit3) fallende Flanke: Strobe ──────────────────────────────────
    if ((prev_ctrl_a_ & 0x08) && !(data & 0x08)) {
        bool is_write = !(data & 0x01);  // /WE=0 → Schreiben
        str_inactive_cycles_ = 0;        // neue Sitzung: /STR=1-Abtastung zurücksetzen
        // Seitenwahl NUR am Format-Schreib-Edge aus bit2 latchen (Format-Steuerwort trägt
        // die echte Seite: 0xB4=Kopf0 / 0xB0=Kopf1).  Am /STR-LESE-Edge (Resync-Strobe)
        // NICHT latchen — dessen bit2 ist inzidentell und würde die vom Pfadbyte gewählte
        // Seite überschreiben (INITs Kopf-1-Verify).
        if (is_write) setHead(data);

        if (bus_.isBUSRQ()) {
            // ZVE2-Kontext: Bus bereits gehalten
            if (is_write) {
                // Schreib-Commit: OTIR fertig, Spur patchen und Bus freigeben.
                commitWrite();
                dma_pending_ = false;
                bus_.releaseBUSRQ();
                LOG_DEBUG("K5122", "/STR ZVE2-Commit SCHREIBEN abgeschlossen, BUSRQ freigegeben");
            } else {
                // Lese-Refresh (kontinuierliche Rotation): die Scheibe dreht weiter,
                // ein erneutes /STR auf DERSELBEN (Zyl,Kopf) setzt den Kopf NICHT zurück
                // (kein Rewind) — head_pos_ läuft weiter.  Nur bei Kopf-/Zylinderwechsel
                // (Seek, Seitenumschaltung) wird die Spur neu geladen (head_pos_=0).
                const bool same_track =
                    cur_track_ != nullptr &&
                    loaded_head_ == current_head_ &&
                    loaded_cyl_  == drives_[selected_drive_].currentCylinder();
                if (!same_track) startReadTransfer();
                byte_ready_ = true;          // nächstes Byte liegt bereit
                byte_acc_   = 0;
                LOG_DEBUG("K5122", "/STR ZVE2-Lese-Refresh: %s, BUSRQ gehalten",
                          same_track ? "weiter (kein Rewind)" : "Spur neu geladen");
            }
        } else {
            // ZVE1-Kontext: neuen DMA-Transfer auslösen
            dma_is_write_ = is_write;
            if (!is_write) {
                startReadTransfer();
                // §5.6.1: /BUSRQ entsteht NUR aus der Bereitschaft des Daten-PIO.
                // Auf einer unformatierten Spur rastet der Datenseparator nicht ein,
                // es kommt kein Byte — dann darf hier auch nichts angefordert werden
                // (startReadTransfer hat den Transfer gar nicht erst armiert).
                if (transferring_) {
                    byte_ready_ = true;      // erstes Byte liegt bereit → /BUSRQ aktiv
                    byte_acc_   = 0;
                }
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
            // Kein armierter Kanal ⇒ weder /ARDY noch /BRDY ⇒ Decoder-Ausgang 00
            // ⇒ KEIN /BUSRQ (Handbuch §5.6.1, Wahrheitstabelle).
            dma_pending_ = transferring_ || write_mode_;
            if (dma_pending_) bus_.assertBUSRQ();
            LOG_DEBUG("K5122", "/STR Flanke: BUSRQ %s, DMA %s",
                      dma_pending_ ? "gesetzt" : "NICHT gesetzt (kein Byte)",
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
        consumeByteSlot();   // Spacing: Phase des freilaufenden Byte-Rasters erhalten
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
 * /RDYL (bit0): „Laufwerk bereit" = gemountet UND Motor auf Drehzahl (motorAtSpeed).
 * Während des Spin-ups oder bei stehendem Motor meldet das Laufwerk NICHT bereit — wie
 * echte HW (MFS gibt /RDYL erst nach dem Anlauf frei).
 *
 * /HF (bit2): per Default 1 (= High-Frequency/MFM-Modus), da 5"-MFM das Standardprofil
 * ist.  Für FM/8"-Laufwerke (profile_.supports_fm && !profile_.supports_mfm) wäre bit2=0.
 * Im aktuellen Testrahmen (nur MFM-Laufwerke) ist der Default ausreichend.
 */
void K5122::updateStatusPortB() {
    uint8_t s = 0xF5;   // Default: kein Laufwerk am Slot

    FloppyDriveV2& drv = drives_[selected_drive_];

    // /TO (TRACK 00, Tor B Bit 7) ist laut Handbuch §4.1 ein **Eingang vom Laufwerk**
    // — ein mechanischer Endlagenschalter, der nichts von der eingelegten Diskette
    // weiss.  Genau daran unterscheidet Software „Slot unbestueckt" von „Laufwerk
    // ohne Diskette"; der Lade-ROM faehrt bei /TO=1 nach aussen und prueft erneut
    // (0x0110), bevor er auf Index-Pulse wartet.  War /TO an die Diskette gekoppelt,
    // blieb ein leeres Laufwerk fuer solche Suchlaeufe unsichtbar.
    if (drv.profile().present && drv.currentCylinder() == 0)
        s &= ~(1u << 7);            // /TO = 0 (auf Spur 0)

    if (drv.isMounted()) {
        if (motorAtSpeed(selected_drive_))
            s &= ~(1u << 0);        // /RDYL = 0 (bereit) — nur mit Diskette auf Drehzahl
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
    // Schrittmotor + Endlagenschalter gehoeren zum LAUFWERK: der Kopf faehrt auch
    // ohne eingelegte Diskette (sonst erreicht ein leeres Laufwerk nie /TRACK 00).
    if (!drv.profile().present) return;

    drv.step(step_dir_in_);

    LOG_TRACE("K5122", "STEP D%d dir=%s cyl=%u",
              selected_drive_, step_dir_in_ ? "inward" : "outward",
              static_cast<unsigned>(drv.currentCylinder()));
}

/**
 * @brief Armiert einen Lese-Transfer: erzeugt den treuen FM/MFM-Lese-Stream und streamt ihn.
 *
 * Der Drive-Cache liefert einen IBM-Format-Track (buildTrack); daraus werden via
 * parseTrack die logischen Sektoren gewonnen und anschließend via buildFaithfulReadTrack
 * (4×A1-Sync, Standard-CRC) der Lese-Stream erzeugt.  Dieser liegt in read_stream_track_ und
 * der Zeiger cur_track_ zeigt darauf.  Der IBM-Format-Track im Drive-Cache bleibt unberührt,
 * damit commitWrite()/parseTrack() weiterhin funktioniert.  Das Verfahren (FM/MFM) kommt aus
 * dem Steuerwort-Override bzw. dem DriveProfile-Default (eff_enc).
 *
 * Bei leerem Laufwerk oder Spur → cur_track_=nullptr.
 */
void K5122::startReadTransfer() {
    FloppyDriveV2& drv = drives_[selected_drive_];
    if (!drv.isMounted()) {
        // Kein Datentraeger → kein Fluss, kein MKE, kein Byte und damit kein /BUSRQ
        // (Handbuch §5.6.1) — dieselbe Lage wie bei einer unformatierten Spur.
        LOG_WARN("K5122", "Lese-Transfer: D%d nicht montiert", selected_drive_);
        transferring_ = false;
        write_mode_   = false;
        byte_ready_   = false;
        dma_pending_  = false;
        locked_       = false;
        cur_track_    = nullptr;
        read_stream_track_ = {};
        head_pos_     = 0;
        byte_acc_     = 0;
        loaded_cyl_   = loaded_head_ = 0xFF;
        if (bus_.isBUSRQ()) bus_.releaseBUSRQ();
        return;
    }

    // IBM-Format-Track aus dem Drive-Cache holen (unverändert für den Write-Pfad).
    const TrackImage& ibm_track = drv.track(current_head_);

    if (ibm_track.empty()) {
        // Diskette liegt ein, Spur ist aber unbeschrieben: der Datenseparator laeuft
        // frei mit und liefert weiter Bytes — nur eben ohne Adressmarke (kein MKE).
        // Genau das streamen wir (markenloser 0x4E-Fluss); die Leseroutine findet
        // kein IDAM und terminiert ueber ihren Index-Timeout.
        const Encoding eff_enc = read_enc_overridden_
                                     ? read_enc_
                                     : drv.profile().default_read_encoding;
        read_stream_track_          = {};
        read_stream_track_.bytes.assign(kUnformattedTrackBytes, 0x4E);
        read_stream_track_.marks.assign(kUnformattedTrackBytes, MarkType::None);
        read_stream_track_.encoding = eff_enc;
        cur_sector_size_ = 128;
        cur_track_       = &read_stream_track_;
        head_pos_        = 0;
        loaded_cyl_      = drv.currentCylinder();
        loaded_head_     = current_head_;
        transferring_    = true;
        write_mode_      = false;
        locked_          = false;
        LOG_INFO("K5122", ">>> READ D%d C=%u H=%u UNFORMATIERT → %zu B Gap-Flux (%s, Index-Timeout)",
                 selected_drive_, static_cast<unsigned>(drv.currentCylinder()),
                 static_cast<unsigned>(current_head_), read_stream_track_.size(),
                 eff_enc == Encoding::FM ? "FM" : "MFM");
        return;
    }

    // Aufzeichnungsverfahren wählen: Steuerwort-Override (0x85/0x87) hat Vorrang,
    // sonst der DriveProfile-Default des angeschlossenen Laufwerks (für K5601 = FM).
    const Encoding eff_enc = read_enc_overridden_
                                 ? read_enc_
                                 : drv.profile().default_read_encoding;

    // Treuer FM/MFM-Lese-Stream mit 4×A1-Sync (der gemeinsame Modus für ROM-Boot-Read und
    // SYL-Lader, s. buildFaithfulReadTrack).  Resync-Offset (markPos-4 MFM / -1 FM) und der
    // FM/MFM-Verfahrens-Match stecken in romReadResyncTarget/ioRead; Codierung aus eff_enc.
    auto sektoren    = TrackCodec::parseTrack(ibm_track);
    read_stream_track_  = TrackCodec::buildFaithfulReadTrack(sektoren, eff_enc);
    cur_sector_size_ = sektoren.empty() ? 128 : sektoren.front().size;

    cur_track_    = &read_stream_track_;
    // HW-Sync-Detektor-Modell: nach einem Read-Kommando liefert die PLL/Marken-Erkennung
    // die Bytes erst AB dem Sync/der ersten Marke — die führenden Gap-/00-Sync-Bytes werden
    // verschluckt (Handbuch §Marken-FF/MKE).  head_pos_ daher direkt auf das erste Resync-
    // Ziel (markPos-nA1 = erstes A1) setzen statt auf 0.  Nötig für INIT.COMs enge Verify-
    // Resync-Routine (überspringt nur A1, erwartet dann FE — verträgt keine 00-Sync davor);
    // deckt sich mit der ROM-Boot-Leseannahme „1 verwerfen + 3 lesen, FE bei buf[4]".
    { size_t t = TrackCodec::romReadResyncTarget(read_stream_track_, 0, eff_enc);
      head_pos_ = (t != SIZE_MAX) ? t : 0; }
    // Geladene (Zyl,Kopf) merken — der /STR-Lese-Refresh lädt nur bei Wechsel neu
    // (sonst kontinuierliche Rotation, kein Rewind).
    loaded_cyl_   = drv.currentCylinder();
    loaded_head_  = current_head_;
    transferring_ = true;
    write_mode_   = false;
    locked_       = false;

    // §1 Strukturiertes Read-Attempt-Log: WELCHE Adressmarken unter dem Kopf liegen
    // und ob ihre CRCs gültig sind — beantwortet "Kopf richtig? Sektoren gültig?" in
    // EINEM Read (statt den ZVE2-Matcher von Hand zu dekodieren).  Der Soll-Ist-
    // Vergleich mit dem vom OS gesuchten Sektor läuft CPU-seitig auf ZVE2 (die Karte
    // kennt das Soll nicht) — dort per k1520dbg `b2 <matcher>`/`hist` sichtbar.
    // INFO: Einzeiler-Health (Sektorzahl + CRC-Fehler).  DEBUG: eine Zeile je Marke
    // (per --log-level debug / --log-cycle <fenster>:debug gezielt einschaltbar).
    int crc_bad = 0;
    for (const auto& s : sektoren)
        if (!s.id_crc_ok || !s.data_crc_ok) ++crc_bad;

    LOG_INFO("K5122", ">>> READ D%d C=%u H=%u Spur=%zu Bytes (%s%s) — %zu Sekt, %d CRC-Fehler",
             selected_drive_,
             static_cast<unsigned>(drv.currentCylinder()),
             static_cast<unsigned>(current_head_),
             read_stream_track_.size(),
             eff_enc == Encoding::FM ? "FM" : "MFM",
             read_enc_overridden_ ? "/Steuerwort" : "/Laufwerk-Default",
             sektoren.size(), crc_bad);

    for (size_t i = 0; i < sektoren.size(); ++i) {
        const auto& s = sektoren[i];
        LOG_DEBUG("K5122",
                  "  RD-ID[%zu] cyl=%u head=%u sec=%u size=%u  id_crc=%s data_crc=%s",
                  i, static_cast<unsigned>(s.cyl), static_cast<unsigned>(s.head),
                  static_cast<unsigned>(s.id), static_cast<unsigned>(s.size),
                  s.id_crc_ok ? "OK" : "BAD", s.data_crc_ok ? "OK" : "BAD");
    }
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

    // Resync-Ziel aus der ROM-Lese-Kalibrierung (§10.5.1): Legacy-A1-Layout direkt auf
    // die Marke, Faithful-Layout (buildTrack) mit Offset markPos-(1+nA1) und Encoding-
    // Gate (read_enc_ vs Spur-Codierung). SIZE_MAX = kein MKE (Mismatch/keine Marke).
    size_t t = TrackCodec::romReadResyncTarget(*cur_track_, head_pos_, effReadEnc());
    if (t != SIZE_MAX) {
        head_pos_ = t;
        locked_   = true;
        LOG_TRACE("K5122", "resync → pos=%zu (0x%02X)", t, cur_track_->bytes[t]);
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
std::vector<LogicalSector> K5122::parseFormatStream(const std::vector<uint8_t>& b,
                                                    Encoding* out_enc) {
    std::vector<LogicalSector> sektoren;
    LogicalSector cur{};
    bool have_idam = false;
    Encoding enc = Encoding::MFM;   // Default; auf FM gesetzt, sobald eine 0x00-Sync-Marke fällt

    // Verarbeitet die Marke bei Offset j (Mark-Byte); liefert den neuen Lese-Index
    // hinter das verarbeitete Feld bzw. SIZE_MAX, wenn j keine gültige Marke ist.
    auto handleMark = [&](size_t j) -> size_t {
        if (j >= b.size()) return SIZE_MAX;
        const uint8_t mark = b[j];
        if (mark == 0xFE && j + 5 <= b.size()) {            // IDAM: … FE c h s n
            cur = LogicalSector{};
            cur.cyl  = b[j + 1];
            cur.head = b[j + 2];
            cur.id   = b[j + 3];
            cur.size = static_cast<uint16_t>(128u << (b[j + 4] & 0x03));
            have_idam = true;
            return j + 5;                                   // hinter die IDAM-Felder (CRC folgt)
        }
        if ((mark == 0xFB || mark == 0xF8) && have_idam) {  // DAM: … FB <data…>
            const size_t data_start = j + 1;
            const size_t take = std::min<size_t>(cur.size, b.size() - data_start);
            cur.data.assign(b.begin() + data_start, b.begin() + data_start + take);
            cur.data.resize(cur.size, 0xE5);                // unvollständig → mit 0xE5 füllen
            sektoren.push_back(cur);
            have_idam = false;
            return data_start + take;                       // hinter das Datenfeld (CRC folgt)
        }
        return SIZE_MAX;
    };

    size_t i = 0;
    while (i < b.size()) {
        // ── MFM: Adressmarke = Sync-Folge aus ≥1 A1-Bytes + Mark-Byte ──────────────
        // Die A1-Anzahl variiert (echter ZVE2-Strom: 3×A1; buildTrack: 2×A1) — daher
        // A1-Folge überspringen und das erste Nicht-A1-Byte als Mark-Byte prüfen.
        if (b[i] == 0xA1) {
            size_t j = i;
            while (j < b.size() && b[j] == 0xA1) ++j;
            size_t ni = handleMark(j);
            i = (ni == SIZE_MAX) ? j : ni;                  // A1-Folge ohne Marke: überspringen
            continue;
        }
        // ── FM: Adressmarke folgt OHNE A1 direkt auf eine 0x00-Sync-Folge ─────────
        // FM (IBM-3740) hat kein A1-Sync; die Marke (FE/FB/F8) steht direkt hinter den
        // 0x00-Sync-Bytes (typ. 6×).  Eine Mindest-Sync-Länge (≥3) verhindert Fehl-
        // treffer auf 0x00-Bytes in Daten/CRC.  FC = Indexmark → überspringen.
        if (b[i] == 0x00) {
            size_t j = i;
            while (j < b.size() && b[j] == 0x00) ++j;
            if (j - i >= 3 && j < b.size()) {
                if (b[j] == 0xFC) { i = j + 1; continue; }   // Indexmark (nur FM), ignorieren
                size_t ni = handleMark(j);
                if (ni != SIZE_MAX) { enc = Encoding::FM; i = ni; continue; }
            }
            i = j;
            continue;
        }
        ++i;
    }
    if (out_enc) *out_enc = enc;
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

    Encoding fmt_enc = Encoding::MFM;
    auto sektoren = parseFormatStream(write_buf_, &fmt_enc);
    if (!sektoren.empty()) {
        // Verfahren aus dem Schreibstrom übernehmen (FM = 0x00-Sync-Marken, MFM = A1-Sync):
        // 8″-SD-Laufwerke (MF3200) formatieren FM, 5¼″/8″-DD MFM.  So bleibt die gecachte
        // Spur codierungstreu → der anschließende Verify-Read (FM-Steuerwort) findet sie.
        TrackImage trk = TrackCodec::buildTrack(sektoren, fmt_enc);
        bool ok = drives_[selected_drive_].writeTrackAt(fmt_cyl_, fmt_head_, trk);
        LOG_INFO("K5122", ">>> FORMAT-WRITE D%d C=%u H=%u: %zu Sektoren à %uB %s %s",
                 selected_drive_, static_cast<unsigned>(fmt_cyl_),
                 static_cast<unsigned>(fmt_head_), sektoren.size(),
                 sektoren.empty() ? 0u : sektoren.front().size,
                 fmt_enc == Encoding::FM ? "FM" : "MFM", ok ? "OK" : "FEHLER");
    } else {
        LOG_WARN("K5122", "FORMAT-COMMIT D%d C=%u H=%u: keine Sektoren im Strom (%zu Bytes)",
                 selected_drive_, static_cast<unsigned>(fmt_cyl_),
                 static_cast<unsigned>(fmt_head_), write_buf_.size());
    }

    write_buf_.clear();
    write_idle_acc_ = 0;

    // ── Index-Phase an das Spur-Ende koppeln ─────────────────────────────────
    // Auf echter HW endet der Vollspur-FORMAT-Write GENAU am Disketten-Index
    // (Schreiben von Index zu Index = eine Umdrehung).  Der nächste Index-Puls
    // liegt dann eine volle Umdrehung später — genau im Fenster, in dem der
    // anschließende Verify auf ihn wartet.  Unser Index-Zähler läuft dagegen
    // frei relativ zum Byte-Takt, sodass der Index sonst mitten in INITs (SCPX)
    // Verify-Vorbereitung (Flag-Clear [12A8]@0x0EF0 → Check@0x1115) fällt und den
    // „Index-Flag muss clear sein"-Test scheitern lässt (BAD TRACKS auf Kopf 1 /
    // ungeraden Zylindern).  Durch Phasen-Reset am Commit fällt der Index wieder
    // hinter den Check in die 0x111B-Warteschleife — wie auf echter Hardware.
    index_cycle_acc_ = 0;
}

/**
 * @brief Beginnt das Sammeln eines BIOS-Schreib-Datenfelds (/WE 1→0).
 *
 * Der Zielsektor ist derjenige, dessen IDAM (Id-Marke) zuletzt unter dem Lesekopf
 * durchlief — also die letzte Id-Marke im Lese-Stream @ref read_stream_track_
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
                // Marke liegt auf dem FE-Byte; danach folgen cyl/head/id/sizecode.
                wr_cyl_  = cur_track_->bytes[(p + 1) % n];
                wr_head_ = cur_track_->bytes[(p + 2) % n];
                wr_id_   = cur_track_->bytes[(p + 3) % n];
                const uint8_t sc = cur_track_->bytes[(p + 4) % n] & 0x03;
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

    // Datenfeld-Beginn im Schreibstrom finden — verfahrensabhängig:
    //   MFM: …00 00 A1 A1 A1 <DAM=FB/F8> <Daten…>   → nach 3×A1-Sync + DAM
    //   FM : …00 00 00 00 00 00 <DAM=FB/F8> <Daten…>  → KEIN A1-Sync (FM-DAM steht
    //        allein mit Sonder-Clock); die DAM ist das erste FB/F8 nach dem 0x00-Sync.
    // Das Verfahren richtet sich nach der Zielspur (FM-Systemspuren der 8″-SD-Disk).
    const bool is_fm = drv.track(current_head_).encoding == Encoding::FM;
    size_t data_start = 0; bool found = false;
    if (is_fm) {
        // Erste DAM (FB=Daten / F8=gelöscht) — davor nur 0x00-Sync, nie FB/F8.
        for (size_t i = 0; i < write_buf_.size(); ++i) {
            if (write_buf_[i] == 0xFB || write_buf_[i] == 0xF8) {
                data_start = i + 1; found = true; break;
            }
        }
    } else {
        for (size_t i = 0; i + 2 < write_buf_.size(); ++i) {
            if (write_buf_[i] == 0xA1 && write_buf_[i + 1] == 0xA1 && write_buf_[i + 2] == 0xA1) {
                data_start = i + 4;             // 3×A1 + DAM(FB/F8)
                found = true; break;
            }
        }
    }
    if (!found) {
        LOG_WARN("K5122", "commitWriteField: kein Datenfeld-Sync im Schreibstrom "
                 "(%s, buf=%zu, S=%u)", is_fm ? "FM" : "MFM", write_buf_.size(), wr_id_);
        write_buf_.clear();
        return;
    }
    if (data_start >= write_buf_.size()) {
        LOG_WARN("K5122", "commitWriteField: Datenfeld leer (S=%u)", wr_id_);
        write_buf_.clear();
        return;
    }
    const size_t avail = write_buf_.size() - data_start;
    const size_t take  = std::min<size_t>(wr_size_, avail);

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

    // ── Sektor-Nachspann aus dem Schreibstrom uebernehmen ────────────────────
    // Der Strom eines Datenfelds lautet gemessen (UDOS, 128-B-Sektor):
    //   [12×00 Sync][A1 A1 A1][FB][128 Daten][CRC CRC][bb bb ff ff][41 FF]
    //                          └ data_start        └ +2      └ Sektorkontrollblock
    // d. h. der schreibende Treiber liefert die 2 CRC-Bytes selbst mit (der
    // Emulator rechnet sie in buildTrack ohnehin neu) und legt DAHINTER die
    // Verkettung ab.  Ohne diese Uebernahme behielte der geschriebene Sektor
    // seinen ALTEN Kontrollblock (doc/udos_bug1.md §5.1).  Standard-IBM-Formate
    // (CP/A, SCPX) schreiben dort schlicht Gap-Bytes — folgenlos.
    const size_t nach_crc = data_start + take + 2;
    if (write_buf_.size() > nach_crc) {
        const size_t ende = std::min(write_buf_.size(), nach_crc + kSectorTailBytes);
        ziel->tail.assign(write_buf_.begin() + static_cast<long>(nach_crc),
                          write_buf_.begin() + static_cast<long>(ende));
    }

    LOG_INFO("K5122", ">>> WRITE D%d C=%u H=%u S=%u bytes=%zu (buf=%zu)",
             selected_drive_,
             static_cast<unsigned>(drv.currentCylinder()),
             static_cast<unsigned>(current_head_),
             static_cast<unsigned>(ziel->id), take, write_buf_.size());

    spur = TrackCodec::buildTrack(sektoren, spur.encoding);
    drv.markTrackDirty(current_head_);

    // Streaming-Track aktualisieren, damit ein evtl. Verify-Read in derselben Sitzung
    // die frischen Daten sieht (Layout/Größen unverändert → head_pos_ bleibt gültig).
    read_stream_track_ = TrackCodec::buildFaithfulReadTrack(sektoren, spur.encoding);
    cur_track_      = &read_stream_track_;

    // Gnadenfenster für den SCPX-Nachfolge-Verify-Read öffnen: verhindert, dass die
    // /STR=1-Abtastung den Transfer in der langen ZVE1-Dispatch-Lücke vorzeitig
    // beendet (s. k5122.h / doc/analyse_scpx_com_load.md §11).
    post_write_grace_    = kPostWriteGraceCycles;
    str_inactive_cycles_ = 0;

    write_buf_.clear();
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
    // Motor-/Select-Zustand + Spin-up je Laufwerk (8212, Port 0x18) — treibt LED,
    // Motor-Abfrage und das Index-/Lese-Gating.
    for (int i = 0; i < 4; ++i) {
        putPod(out, static_cast<uint8_t>(drive_selected_[static_cast<size_t>(i)] ? 1 : 0));
        putPod(out, static_cast<uint8_t>(motor_on_[static_cast<size_t>(i)] ? 1 : 0));
        putPod(out, static_cast<int32_t>(motor_spinup_cycles_[static_cast<size_t>(i)]));
    }
    // Head-Load-Zustand (/HL, Port A Bit 6).
    putPod(out, static_cast<uint8_t>(head_loaded_ ? 1 : 0));
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
    // Motor-/Select-Zustand + Spin-up je Laufwerk (8212, Port 0x18).
    for (int i = 0; i < 4; ++i) {
        uint8_t sel_on = 0, mot_on = 0; int32_t spin = 0;
        if (!getPod(p, end, sel_on)) return false;
        if (!getPod(p, end, mot_on)) return false;
        if (!getPod(p, end, spin))   return false;
        drive_selected_[static_cast<size_t>(i)]     = (sel_on != 0);
        motor_on_[static_cast<size_t>(i)]           = (mot_on != 0);
        motor_spinup_cycles_[static_cast<size_t>(i)] = spin;
    }
    // Head-Load-Zustand (/HL, Port A Bit 6).
    uint8_t hl = 0;
    if (!getPod(p, end, hl)) return false;
    head_loaded_ = (hl != 0);
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
    post_write_grace_    = 0;
    return true;
}

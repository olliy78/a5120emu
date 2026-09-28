/**
 * @file k5122_wait.cpp
 * @brief K5122 im **`/WAIT`-Betrieb** (K8915) — drehgekoppelter Datenweg und Marken-FF.
 *
 * Im K8915 gibt es keine ZVE2.  Die Karte ist auf `/WAIT` gebrückt (am Gerät abgelesen,
 * doc/design/16_k8915.md §3.4): Boot-ROM und BIOS lesen bzw. schreiben den Datenport
 * **ausgerollt, ohne Statusabfrage** — jeder Zugriff hält die einzige CPU an, bis das
 * nächste Byte unter dem Kopf ist (Doku K5122 §5.6, Decoder-Ausgang 00).
 *
 * Das Modell dazu (doc/design/07_k5122_afs.md §7.7):
 *
 * - **Die Spur dreht sich, ob jemand liest oder nicht.**  Unter dem Kopf liegt zur Zeit T
 *   das Bytefenster `phase(T) / Byteperiode`; die Phase ist die des Indexpulses
 *   (`index_cycle_acc_`), also ist Byte 0 des Lesestroms das erste nach dem Index.  Jedes
 *   Fenster ist eine Byteperiode lang (MFM 78 Takte bei 2,4576 MHz), das letzte vor dem
 *   Index ist kürzer.  Hinter dem Ende des Lesestroms liegt Lücke (4EH).
 * - **Daten-PIO mit einem Zwischenspeicher:** ein Zugriff bekommt das zuletzt fertig
 *   gewordene Byte, wenn es noch nicht abgeholt wurde (dann ohne Warten), sonst wartet er
 *   bis zum Ende des laufenden Fensters.  Wer zu spät kommt, verliert die Bytes dazwischen
 *   (Überlauf, Handbuch §5.7).  Die Wartetakte gehen über @ref K1520Bus::addWaitCycles an
 *   die Laufschleife, die währenddessen Zeitgeber, SIO und Index weitertaktet.
 * - **Kein Warten ohne Datenfluss:** bei `/STR` = 1 oder stehender Scheibe liefert der
 *   Datenport sofort den alten Inhalt — sonst hinge die CPU für immer.
 * - **Marken-FF (MKE, Tor B Bit1)**: scharf, solange `/STR` = 0 **und** MR (Bit5) = 0
 *   (Handbuch §5.5: zurückgesetzt „durch MR oder Abschalten von /STR“).  Gesetzt, sobald
 *   nach dem Scharfmachen eine Sync-Gruppe (das erste A1 vor FE/FB, im FM das Byte vor
 *   der Marke) ganz unter dem Kopf durch ist.  Ruhepegel 0; das BIOS stellt sich beim
 *   Kaltstart auf die Polarität ein (E2B6H).  Falsches Verfahren (Steuerwort 85H/87H
 *   passt nicht zur Spur) ⇒ nie ein MKE, Markenbytes lesen sich als 00H.
 * - **Schreiben** eines Datenfelds zwischen den `/WE`-Flanken (Bit0): gesammelt wird, was
 *   an 14H geht (je Byte ein Fenster, mit Warten), übernommen beim Loslassen von `/WE`
 *   in den Sektor, dessen Kopf zuletzt unter dem Kopf durchlief (@ref commitWriteField).
 *
 * Der Lesestrom ist derselbe wie im BusRq-Weg (`TrackCodec::buildFaithfulReadTrack`,
 * 4 × A1): so bleibt dem Kopf-ISR des BIOS vom MKE bis zu seinem ersten `IN (16H)` die
 * Zeit von vier Bytes.  Vollspur-Formatieren im Wait-Betrieb ist noch nicht nachgebildet
 * (Etappe 4, FORMAT.COM des K8915).
 */

#include "core/cards/k5122/k5122.h"
#include "core/logger.h"
#include <algorithm>

void K5122::setSynchronisation(Synchronisation s)
{
    wait_betrieb_  = (s == Synchronisation::Wait);
    w_status_gilt_ = false;
    w_strom_gilt_  = false;
    w_scharf_ = w_mke_ = false;
    w_mke_time_ = UINT64_MAX;
    updateStatusPortB();
}

uint64_t K5122::waitJetzt() const
{
    return w_now_ + static_cast<uint64_t>(bus_.pendingWaitCycles());
}

bool K5122::waitDreht() const
{
    return drives_[selected_drive_].isMounted() && motorAtSpeed(selected_drive_);
}

const TrackImage& K5122::waitStrom()
{
    FloppyDriveV2& drv = drives_[selected_drive_];
    const DiskImage* img = drv.image();
    const void*    med = img ? static_cast<const void*>(&img->medium()) : nullptr;
    const uint64_t rev = img ? img->medium().revision() : 0;
    const uint8_t  zyl = drv.currentCylinder();
    if (w_strom_gilt_ && w_strom_lw_ == selected_drive_ && w_strom_zyl_ == zyl
        && w_strom_kopf_ == current_head_ && w_strom_med_ == med && w_strom_rev_ == rev)
        return w_strom_;

    w_strom_ = {};
    w_sync_.clear();
    if (img) {
        const TrackImage& ibm = drv.track(current_head_);
        if (!ibm.empty()) {
            const auto sektoren = TrackCodec::parseTrack(ibm);
            w_strom_ = TrackCodec::buildFaithfulReadTrack(sektoren, ibm.encoding);
            // Sync-Gruppen: je Marke das erste Sync-Byte (romReadResyncTarget).
            for (size_t i = 0; i < w_strom_.size(); ++i) {
                if (w_strom_.marks[i] == MarkType::None) continue;
                const size_t t = TrackCodec::romReadResyncTarget(w_strom_, i, w_strom_.encoding);
                if (t != SIZE_MAX && t <= i) w_sync_.push_back(t);
            }
            LOG_INFO("K5122", "WAIT: Spur D%d C=%u H=%u — %zu Sektoren, %zu B Lesestrom (%s)",
                     selected_drive_, static_cast<unsigned>(zyl),
                     static_cast<unsigned>(current_head_), sektoren.size(), w_strom_.size(),
                     w_strom_.encoding == Encoding::FM ? "FM" : "MFM");
        } else {
            LOG_INFO("K5122", "WAIT: Spur D%d C=%u H=%u unformatiert (nur Lücke, kein MKE)",
                     selected_drive_, static_cast<unsigned>(zyl),
                     static_cast<unsigned>(current_head_));
        }
    }
    w_strom_gilt_ = true;
    w_strom_lw_   = selected_drive_;
    w_strom_zyl_  = zyl;
    w_strom_kopf_ = current_head_;
    w_strom_med_  = med;
    w_strom_rev_  = rev;
    return w_strom_;
}

uint8_t K5122::waitByte(size_t slot)
{
    const TrackImage& s = waitStrom();
    if (slot >= s.size()) return 0x4E;            // hinter dem Lesestrom: Lücke
    // Falsches Verfahren: der Datenseparator demoduliert die Marke nicht (wie ioRead).
    if (s.marks[slot] != MarkType::None && effReadEnc() != s.encoding) return 0x00;
    return s.bytes[slot];
}

uint64_t K5122::waitFenster(uint64_t t, size_t& slot)
{
    const uint64_t P  = static_cast<uint64_t>(drives_[selected_drive_].indexPeriodCycles(cpu_hz_));
    const uint64_t p  = static_cast<uint64_t>(currentBytePeriod());
    const uint64_t ph = (static_cast<uint64_t>(index_cycle_acc_) + (t - w_now_)) % P;
    const uint64_t rs = t - ph;                    // Beginn der laufenden Umdrehung (Index)
    const uint64_t j  = ph / p;                    // Fenster unter dem Kopf
    const uint64_t fenster = (P + p - 1) / p;      // Fenster je Umdrehung

    // Das zuletzt fertig gewordene Byte (endet bei rs + j·p) liegt im Daten-PIO,
    // solange es niemand abgeholt hat — dann ohne Warten.
    const uint64_t ende_vorher = rs + j * p;
    if (ende_vorher > w_last_done_ && ende_vorher <= t) {
        slot = static_cast<size_t>(j > 0 ? j - 1 : fenster - 1);
        w_last_done_ = ende_vorher;
        return ende_vorher;
    }
    // Sonst: /WAIT bis zum Ende des laufenden Fensters.
    const uint64_t ende = rs + std::min((j + 1) * p, P);
    slot = static_cast<size_t>(j);
    w_last_done_ = ende;
    return ende;
}

uint8_t K5122::waitRead()
{
    // Kein Datenfluss (/STR = 1, keine Diskette, Motor steht): kein /WAIT, der
    // Daten-PIO gibt seinen alten Inhalt her (BIOS E902H leert ihn so).
    if ((prev_ctrl_a_ & 0x08) || !waitDreht()) return w_latch_;

    const uint64_t t = waitJetzt();
    size_t slot = 0;
    const uint64_t ende = waitFenster(t, slot);
    if (ende > t) bus_.addWaitCycles(static_cast<int>(ende - t));
    w_latch_ = waitByte(slot);
    LOG_TRACE("K5122", "WAIT-Lesen Fenster %zu => %02X (+%llu Takte)", slot, w_latch_,
              static_cast<unsigned long long>(ende > t ? ende - t : 0));
    return w_latch_;
}

void K5122::waitWrite(uint8_t data)
{
    if (!we_writing_) return;                      // kein /WE: nur der PIO-Ausgang
    if ((prev_ctrl_a_ & 0x08) || !waitDreht()) { write_buf_.push_back(data); return; }
    const uint64_t t = waitJetzt();
    size_t slot = 0;
    const uint64_t ende = waitFenster(t, slot);
    if (ende > t) bus_.addWaitCycles(static_cast<int>(ende - t));
    write_buf_.push_back(data);
}

void K5122::waitMkePlanen(bool neu_scharf)
{
    const bool scharf = !(prev_ctrl_a_ & 0x08) && !(prev_ctrl_a_ & 0x20);
    if (!scharf) {
        w_scharf_   = false;
        w_mke_      = false;
        w_mke_time_ = UINT64_MAX;
        return;
    }
    // Schon scharf, Spur unverändert, Marke gefunden oder Zeitpunkt geplant: nichts tun.
    // (Neu geplant wird beim Scharfmachen und wenn sich die Spur unter dem Kopf ändert —
    // ein beliebiger anderer Schreibzugriff auf 10H darf eine gerade durchlaufende
    // Sync-Gruppe nicht „überspringen“.)
    const bool strom_alt = w_strom_gilt_ && w_strom_lw_ == selected_drive_
        && w_strom_zyl_ == drives_[selected_drive_].currentCylinder()
        && w_strom_kopf_ == current_head_;
    if (w_scharf_ && !neu_scharf && (w_mke_ || (strom_alt && w_mke_time_ != UINT64_MAX)))
        return;

    if (!w_scharf_ || neu_scharf) w_mke_ = false;
    w_scharf_   = true;
    w_mke_time_ = UINT64_MAX;
    if (w_mke_ || !waitDreht()) return;

    const TrackImage& s = waitStrom();
    if (w_sync_.empty() || effReadEnc() != s.encoding) return;   // nie eine Marke

    const uint64_t t  = waitJetzt();
    const uint64_t P  = static_cast<uint64_t>(drives_[selected_drive_].indexPeriodCycles(cpu_hz_));
    const uint64_t p  = static_cast<uint64_t>(currentBytePeriod());
    const uint64_t ph = (static_cast<uint64_t>(index_cycle_acc_) + (t - w_now_)) % P;
    const uint64_t rs = t - ph;
    const uint64_t j0 = (ph + p - 1) / p;          // erstes Fenster, das NACH t beginnt
    for (size_t s0 : w_sync_) {
        if (s0 >= j0 && (s0 + 1) * p <= P) {
            w_mke_time_ = rs + (s0 + 1) * p;
            return;
        }
    }
    w_mke_time_ = rs + P + std::min((w_sync_.front() + 1) * p, P);   // nächste Umdrehung
}

void K5122::waitCtrlPortAWrite(uint8_t data)
{
    const uint8_t prev = prev_ctrl_a_;
    head_loaded_ = !(data & 0x40);

    // Lesesteuerwort (81H/83H/85H/87H): Verfahren (Bit1, 0 = MFM) und Seite (Bit2 /FR,
    // 1 = Kopf 0) — wie im BusRq-Weg nur an diesem Muster, nicht an jedem Schreiben.
    if ((data & 0xF9) == 0x81) {
        read_enc_            = (data & 0x02) ? Encoding::FM : Encoding::MFM;
        read_enc_overridden_ = true;
        setHead(data);
    }

    // /ST (Bit7) fallend: ein Schritt in Richtung MR/SD (Bit5).
    if ((prev & 0x80) && !(data & 0x80)) {
        step_dir_in_ = (data & 0x20) != 0;
        doStep();
    }

    // /WE (Bit0): Datenfeld schreiben.  Beginn nur bei laufendem Datenfluss.
    const bool we_now  = !(data & 0x01);
    const bool we_prev = !(prev & 0x01);
    if (we_now && !we_prev && !(data & 0x08) && waitDreht()) {
        // Ziel: der Sektor, dessen Kopf zuletzt unter dem Kopf durchlief.
        we_writing_ = true;
        write_buf_.clear();
        wr_id_ = 0; wr_size_ = 128; wr_cyl_ = 0; wr_head_ = current_head_;
        const TrackImage& s = waitStrom();
        if (!s.empty()) {
            const uint64_t P  = static_cast<uint64_t>(drives_[selected_drive_].indexPeriodCycles(cpu_hz_));
            const uint64_t p  = static_cast<uint64_t>(currentBytePeriod());
            const uint64_t ph = (static_cast<uint64_t>(index_cycle_acc_) + (waitJetzt() - w_now_)) % P;
            const size_t   n  = s.size();
            size_t pos = std::min<size_t>(static_cast<size_t>(ph / p), n - 1);
            for (size_t k = 0; k < n; ++k) {
                const size_t q = (pos + n - k) % n;
                if (s.marks[q] == MarkType::Id) {
                    wr_cyl_  = s.bytes[(q + 1) % n];
                    wr_head_ = s.bytes[(q + 2) % n];
                    wr_id_   = s.bytes[(q + 3) % n];
                    wr_size_ = static_cast<uint16_t>(128u << (s.bytes[(q + 4) % n] & 0x03));
                    break;
                }
            }
        }
        LOG_DEBUG("K5122", "WAIT: Schreiben Datenfeld C=%u H=%u S=%u (%u B)",
                  wr_cyl_, wr_head_, wr_id_, wr_size_);
    } else if (!we_now && we_prev && we_writing_) {
        commitWriteField();
        cur_track_        = nullptr;   // gehört dem BusRq-Weg
        post_write_grace_ = 0;
        w_strom_gilt_     = false;     // Spur neu aus dem Medium
    }

    prev_ctrl_a_ = data;

    // Marken-FF: scharf bei /STR = 0 und MR = 0; der Übergang dahin macht neu scharf.
    const bool scharf_jetzt = !(data & 0x08) && !(data & 0x20);
    const bool scharf_vorher = !(prev & 0x08) && !(prev & 0x20);
    waitMkePlanen(scharf_jetzt && !scharf_vorher);
    updateStatusPortB();
}

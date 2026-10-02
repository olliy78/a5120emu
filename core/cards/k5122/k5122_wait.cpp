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
 *   (`index_cycle_acc_`), also ist Byte 0 der Spur das erste nach dem Index.  Jedes
 *   Fenster ist eine Byteperiode lang (MFM 78 Takte bei 2,4576 MHz), das letzte vor dem
 *   Index ist kürzer.  Hinter dem Ende der Spur liegt Lücke (4EH).
 * - **Was unter dem Kopf liegt, ist die Spur des Mediums, Byte für Byte** (seit AP-E4f):
 *   Lücken, 3 × A1, Indexmarke, Nachlauf hinter der CRC — so, wie sie auf der Scheibe
 *   steht.  FORMAT.COM vergleicht beim Prüf-Lesen die GANZE Spur mit dem Geschriebenen,
 *   eine nachgebaute Spur (`buildFaithfulReadTrack`, 4 × A1, eigene Lücken) bestünde das
 *   nicht.  Die Byteperiode folgt dem Verfahren der Spur (bzw. MFM, wenn das Laufwerk es
 *   kann und die Spur leer ist), NICHT dem MK-Bit: MK wählt nur die Markenerkennung.
 * - **Daten-PIO mit einem Zwischenspeicher:** ein Zugriff bekommt das zuletzt fertig
 *   gewordene Byte, wenn es noch nicht abgeholt wurde (dann ohne Warten), sonst wartet er
 *   bis zum Ende des laufenden Fensters.  Wer zu spät kommt, verliert die Bytes dazwischen
 *   (Überlauf, Handbuch §5.7).  Die Wartetakte gehen über @ref K1520Bus::addWaitCycles an
 *   die Laufschleife, die währenddessen Zeitgeber, SIO und Index weitertaktet.
 * - **Kein Warten ohne Datenfluss:** bei `/STR` = 1 oder stehender Scheibe liefert der
 *   Datenport sofort den alten Inhalt — sonst hinge die CPU für immer.
 * - **Marken-FF (MKE, Tor B Bit1)**: scharf, solange `/STR` = 0 **und** MR (Bit5) = 0
 *   (Handbuch §5.5: zurückgesetzt „durch MR oder Abschalten von /STR“).  Gesetzt, sobald
 *   nach dem Scharfmachen das erste Byte einer Sync-Gruppe ganz unter dem Kopf durch ist.
 *   Welche Gruppe, entscheidet MK (Tor A Bit1; Handbuch §4.3 „/MK“, am Port invertiert):
 *   MK = 0 ⇒ MFM-Synchronisationsbyte A1 (vor Kennfeld/Datenfeld), MK = 1 ⇒
 *   MFM-Indexmarke C2 bzw. FM-Marken.  Ruhepegel 0; das BIOS stellt sich beim Kaltstart
 *   auf die Polarität ein (E2B6H).  Das erste `IN (16H)` danach liefert das erkannte
 *   Sync-Byte selbst („Datenbyte der Marke, welches als erstes Byte zur CPU übertragen
 *   wird“, Handbuch §5.3).
 * - **Schreiben** zwischen den `/WE`-Flanken (Bit0): gesammelt wird, was an 14H geht (je
 *   Byte ein Fenster, mit Warten), samt dem Fenster, in dem es auf die Scheibe ging.
 *   Übernommen wird beim Loslassen von `/WE` (oder `/STR` = 1), und der Strom entscheidet
 *   (@ref waitSchreibenUebernehmen):
 *   - **ganze Spur (FORMAT.COM, AP-E4f)** — enthält er Kennfelder (A1 A1 A1 FE …), wird
 *     jedes Byte an SEIN Fenster der Umdrehung gelegt (@ref waitSpurSchreiben) und die
 *     Spur so ins Medium geschrieben, an der beim Beginn gemerkten Kopfposition und Seite
 *     (/FR des Schreib-Steuerworts);
 *   - **Datenfeld (BIOS, DISGEN)** — sonst: der Sektor, dessen Kennfeld zuletzt unter dem
 *     Kopf durchlief, bekommt das Datenfeld an Ort und Stelle (`TrackCodec::writeSector`,
 *     Lücken und Spurlänge bleiben).
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

Encoding K5122::waitVerfahrenGemerkt() const
{
    // Die Datenrate liegt an Laufwerk und Aufzeichnung, nicht am MK-Bit (/HF, Handbuch
    // §4.3): eine beschriebene Spur dreht in ihrem Verfahren, eine leere in dem, das das
    // Laufwerk schreiben würde.
    if (w_strom_gilt_ && !w_strom_.empty()) return w_strom_.encoding;
    const DriveProfile& pr = drives_[selected_drive_].profile();
    return pr.supports_mfm ? Encoding::MFM : Encoding::FM;
}

int K5122::waitByteperiode()
{
    (void)waitStrom();
    return drives_[selected_drive_].profile().bytePeriodCycles(waitVerfahrenGemerkt(), cpu_hz_);
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
    w_sync_a1_.clear();
    w_sync_idx_.clear();
    w_sync_fm_.clear();
    if (img) {
        const TrackImage& spur = drv.track(current_head_);
        if (!spur.empty()) {
            if (spur.bitcells != 0) {
                // Die Spur selbst, Byte für Byte — keine nachgebaute (s. Dateikopf).
                // bitcells ≠ 0: sie hat eine echte Aufzeichnung (Flussabbild, oder von
                // FORMAT.COM im /WAIT-Betrieb geschrieben, waitSpurSchreiben).
                w_strom_ = spur;
            } else {
                // Aus logischen Sektoren gebaute Spur (.img, .dmk-Kopie, BusRq-Weg):
                // seit AP-F1 hat sie selbst Normlücken (TrackCodec::normGaps); bis dahin
                // Vorspann 16, Lücke 3 = 24 und bis AP-E5a Lücke 2 = 11 × 4E — zu knapp: das
                // BIOS liest nach dem Kennfeld (F780H) − 6 Lückenbytes, bevor es neu scharf
                // macht.  Unter dem Kopf liegt weiter die Spur mit den Lücken von
                // FORMAT.COM (Lücke 3 auf die Umdrehung dieses Laufwerks, §4.4 in
                // doc/design/16_k8915.md).
                w_strom_ = waitNormspur(spur);
            }
            if (w_strom_.marks.size() != w_strom_.bytes.size())
                w_strom_.marks.resize(w_strom_.bytes.size(), MarkType::None);
            // Sync-Gruppen: je Marke das ERSTE Byte der Gruppe davor.
            size_t sektoren = 0;
            for (size_t i = 0; i < w_strom_.size(); ++i) {
                const MarkType m = w_strom_.marks[i];
                if (m == MarkType::None) continue;
                if (m == MarkType::Id) ++sektoren;
                if (w_strom_.encoding == Encoding::FM) {
                    // FM: die Marke trägt ihr Taktmuster selbst; erkannt ist sie, wenn
                    // das Byte davor durch ist (wie romReadResyncTarget, markPos − 1).
                    if (i > 0) w_sync_fm_.push_back(i - 1);
                    continue;
                }
                const uint8_t sync = (m == MarkType::Index) ? 0xC2 : 0xA1;
                size_t k = i;
                while (k > 0 && w_strom_.bytes[k - 1] == sync) --k;
                auto& gruppe = (m == MarkType::Index ? w_sync_idx_ : w_sync_a1_);
                if (mke_jedes_sync_)   // jedes Sync-Byte der Gruppe (setMkeJedesSyncByte)
                    for (size_t j = k; j < i; ++j) gruppe.push_back(j);
                else
                    gruppe.push_back(k);
            }
            LOG_INFO("K5122", "WAIT: Spur D%d C=%u H=%u — %zu Kennfelder, %zu B (%s)",
                     selected_drive_, static_cast<unsigned>(zyl),
                     static_cast<unsigned>(current_head_), sektoren, w_strom_.size(),
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

TrackImage K5122::waitNormspur(const TrackImage& spur) const
{
    const auto sektoren = TrackCodec::parseTrack(spur);
    const bool mfm = spur.encoding == Encoding::MFM;
    GapParams g = TrackCodec::gapsFor(spur.encoding);
    // Normlücken nach FORMAT.COM des K8915 (MFM: 80 × 4E, IAM, 50 × 4E, Lücke 2 22 × 4E,
    // Synchron 12 × 00) bzw. IBM 3740 (FM: 40, IAM, 26, Lücke 2 11, Synchron 6).
    g.gap4a = mfm ? 80 : 40;
    g.gap1  = mfm ? 50 : 26;
    g.gap2  = mfm ? 22 : 11;
    g.with_iam = true;
    // Lücke 3 so, dass die Spur in eine Umdrehung passt (Nenndatenrate), höchstens
    // die von FORMAT.COM (116 bei 5 × 1024), mindestens der Nachlauf hinter der CRC.
    const DriveProfile& pr = drives_[selected_drive_].profile();
    const size_t umdrehung = static_cast<size_t>((mfm ? 31250u : 15625u) * 60u / pr.rpm);
    size_t fest = g.gap4a + (mfm ? 4 : 1) + g.gap1, je = 0;
    for (const auto& s : sektoren)
        je += 2u * g.sync_len + (mfm ? 8u : 2u) + 6u + g.gap2 + s.size + 2u;
    fest += je;
    size_t g3 = kSectorTailBytes;
    if (!sektoren.empty() && umdrehung > fest)
        g3 = std::max<size_t>(kSectorTailBytes, (umdrehung - fest) / sektoren.size());
    g.gap3 = static_cast<uint8_t>(std::min<size_t>(g3, mfm ? 116u : 27u));
    return TrackCodec::buildTrack(sektoren, spur.encoding, g);
}

uint8_t K5122::waitByte(size_t slot)
{
    const TrackImage& s = waitStrom();
    if (slot >= s.size())                          // hinter der Spur: Lücke
        return waitVerfahrenGemerkt() == Encoding::FM ? 0xFF : 0x4E;
    return s.bytes[slot];
}

uint64_t K5122::waitFenster(uint64_t t, size_t& slot)
{
    const uint64_t P  = static_cast<uint64_t>(drives_[selected_drive_].indexPeriodCycles(cpu_hz_));
    const uint64_t p  = static_cast<uint64_t>(waitByteperiode());
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
    if ((prev_ctrl_a_ & 0x08) || !waitDreht()) {   // kein Datenfluss: nichts erreicht die Scheibe
        write_buf_.push_back(data);
        w_schreib_fenster_.push_back(SIZE_MAX);
        return;
    }
    const uint64_t t = waitJetzt();
    size_t slot = 0;
    const uint64_t ende = waitFenster(t, slot);
    if (ende > t) bus_.addWaitCycles(static_cast<int>(ende - t));
    write_buf_.push_back(data);
    w_schreib_fenster_.push_back(slot);
}

void K5122::waitSchreibenUebernehmen()
{
    we_writing_ = false;
    // Ganze Spur oder Datenfeld?  Entschieden am Strom selbst: nur FORMAT schreibt
    // Kennfelder (A1 A1 A1 FE …); ein Datenfeld-Strom des BIOS enthält keins.
    Encoding enc = Encoding::MFM;
    const auto sektoren = parseFormatStream(write_buf_, &enc);
    FloppyDriveV2& drv = drives_[selected_drive_];
    if (!drv.isMounted() || drv.isWriteProtect()) {
        LOG_WARN("K5122", "WAIT: Schreiben D%d verworfen (%s)", selected_drive_,
                 drv.isMounted() ? "schreibgeschützt" : "keine Diskette");
    } else if (!sektoren.empty()) {
        waitSpurSchreiben(enc, sektoren.size());
    } else {
        waitFeldSchreiben();
    }
    write_buf_.clear();
    w_schreib_fenster_.clear();
    cur_track_        = nullptr;   // gehört dem BusRq-Weg
    post_write_grace_ = 0;
    w_strom_gilt_     = false;     // Spur neu aus dem Medium
}

void K5122::waitSpurSchreiben(Encoding enc, size_t kennfelder)
{
    FloppyDriveV2& drv = drives_[selected_drive_];
    // Eine Umdrehung hat so viele Bytefenster wie waitFenster zählt; jedes geschriebene
    // Byte liegt in SEINEM Fenster — was nach dem Index weitergeschrieben wurde,
    // überschreibt den Anfang der Spur (bei FORMAT.COM: Lücke über Lücke).
    const uint64_t P = static_cast<uint64_t>(drv.indexPeriodCycles(cpu_hz_));
    const uint64_t p = static_cast<uint64_t>(drv.profile().bytePeriodCycles(enc, cpu_hz_));
    const size_t   n = static_cast<size_t>((P + p - 1) / p);
    const uint8_t  luecke = (enc == Encoding::FM) ? 0xFF : 0x4E;

    TrackImage spur;
    spur.encoding = enc;
    spur.bytes.assign(n, luecke);
    spur.marks.assign(n, MarkType::None);
    // Was nicht überschrieben wird, bleibt stehen (dieselbe Seite, dasselbe Verfahren).
    const uint8_t alt_zyl = drv.currentCylinder();
    if (alt_zyl == w_schreib_zyl_) {
        const TrackImage& alt = drv.track(w_schreib_kopf_);
        if (!alt.empty() && alt.encoding == enc)
            for (size_t i = 0; i < std::min(n, alt.size()); ++i) {
                spur.bytes[i] = alt.bytes[i];
                spur.marks[i] = i < alt.marks.size() ? alt.marks[i] : MarkType::None;
            }
    }
    std::vector<bool> neu(n, false);
    size_t auf_der_scheibe = 0;
    for (size_t k = 0; k < write_buf_.size(); ++k) {
        const size_t f = w_schreib_fenster_[k];
        if (f == SIZE_MAX) continue;
        const size_t pos = f % n;
        spur.bytes[pos] = write_buf_[k];
        spur.marks[pos] = MarkType::None;
        neu[pos] = true;
        ++auf_der_scheibe;
    }
    // Marken setzen, wo der Strom sie trägt: MFM = Markenbyte hinter A1 (FE/FB/F8)
    // bzw. C2 (FC); FM = Markenbyte hinter mindestens drei 00H (wie parseFormatStream).
    for (size_t pos = 0; pos < n; ++pos) {
        if (!neu[pos]) continue;
        const uint8_t b = spur.bytes[pos];
        const auto vor = [&](size_t d) { return spur.bytes[(pos + n - d) % n]; };
        MarkType m = MarkType::None;
        if (enc == Encoding::MFM) {
            if ((b == 0xFE || b == 0xFB || b == 0xF8) && vor(1) == 0xA1)
                m = (b == 0xFE) ? MarkType::Id : MarkType::Data;
            else if (b == 0xFC && vor(1) == 0xC2)
                m = MarkType::Index;
        } else if (vor(1) == 0x00 && vor(2) == 0x00 && vor(3) == 0x00) {
            if (b == 0xFE)                     m = MarkType::Id;
            else if (b == 0xFB || b == 0xF8)   m = MarkType::Data;
            else if (b == 0xFC)                m = MarkType::Index;
        }
        spur.marks[pos] = m;
    }
    // Eine echte Aufzeichnung: 16 Zellen je Byte (Takt + Daten).  bitcells ≠ 0 heisst im
    // /WAIT-Lesen „so lesen, wie sie liegt“ (waitStrom).
    spur.bitcells = static_cast<uint32_t>(n * 16);
    const bool ok = drv.writeTrackAt(w_schreib_zyl_, w_schreib_kopf_, spur);
    ++w_spuren_geschrieben_;
    LOG_INFO("K5122", ">>> WAIT-FORMAT D%d C=%u H=%u: %zu Kennfelder %s, %zu von %zu B auf %zu Fenster, %llu Takte %s",
             selected_drive_, static_cast<unsigned>(w_schreib_zyl_),
             static_cast<unsigned>(w_schreib_kopf_), kennfelder,
             enc == Encoding::FM ? "FM" : "MFM", auf_der_scheibe, write_buf_.size(), n,
             static_cast<unsigned long long>(waitJetzt() - w_schreib_start_),
             ok ? "OK" : "FEHLER");
}

void K5122::waitFeldSchreiben()
{
    FloppyDriveV2& drv = drives_[selected_drive_];
    // Datenfeld im Strom: MFM hinter der A1-Gruppe die DAM (FB/F8), FM die erste FB/F8.
    const bool fm = waitVerfahrenGemerkt() == Encoding::FM;
    size_t daten = SIZE_MAX;
    for (size_t i = 0; i < write_buf_.size(); ++i) {
        const uint8_t b = write_buf_[i];
        if ((b == 0xFB || b == 0xF8) && (fm || (i > 0 && write_buf_[i - 1] == 0xA1))) {
            daten = i + 1;
            break;
        }
    }
    if (daten == SIZE_MAX || daten >= write_buf_.size()) {
        LOG_WARN("K5122", "WAIT: kein Datenfeld im Schreibstrom (%zu B, S=%u)",
                 write_buf_.size(), wr_id_);
        return;
    }
    const size_t take = std::min<size_t>(wr_size_, write_buf_.size() - daten);
    std::vector<uint8_t> inhalt(write_buf_.begin() + static_cast<long>(daten),
                                write_buf_.begin() + static_cast<long>(daten + take));
    inhalt.resize(wr_size_, 0x00);
    // Hinter der Daten-CRC: der Nachlauf des Schreibkopfs (DISGEN: 4 Byte, §4.4) bzw.
    // ein Sektorkontrollblock — wie im BusRq-Weg bis kSectorTailBytes übernehmen.
    std::vector<uint8_t> nachlauf;
    const size_t nach_crc = daten + take + 2;
    for (size_t i = nach_crc; i < write_buf_.size() && nachlauf.size() < kSectorTailBytes; ++i)
        nachlauf.push_back(write_buf_[i]);

    TrackImage& spur = drv.mutableTrack(current_head_);
    // An Ort und Stelle (Lücken, Spurlänge, Nachbarsektoren bleiben; die CRC rechnet
    // writeSector neu).  Stösst der Nachlauf an eine Marke, ohne ihn.
    bool ok = !spur.empty() && TrackCodec::writeSector(spur, wr_id_, inhalt, nachlauf);
    if (!ok && !spur.empty()) ok = TrackCodec::writeSector(spur, wr_id_, inhalt, {});
    if (!ok) {
        LOG_WARN("K5122", "WAIT: Datenfeld S=%u nicht in Spur D%d C=%u H=%u", wr_id_,
                 selected_drive_, static_cast<unsigned>(drv.currentCylinder()),
                 static_cast<unsigned>(current_head_));
        return;
    }
    drv.markTrackDirty(current_head_);
    LOG_INFO("K5122", ">>> WAIT-WRITE D%d C=%u H=%u S=%u bytes=%zu (buf=%zu)",
             selected_drive_, static_cast<unsigned>(drv.currentCylinder()),
             static_cast<unsigned>(current_head_), static_cast<unsigned>(wr_id_), take,
             write_buf_.size());
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
    const bool mk = (prev_ctrl_a_ & 0x02) != 0;   // Markenerkennung: 0 = A1, 1 = C2/FM
    const bool strom_alt = w_strom_gilt_ && w_strom_lw_ == selected_drive_
        && w_strom_zyl_ == drives_[selected_drive_].currentCylinder()
        && w_strom_kopf_ == current_head_;
    if (w_scharf_ && !neu_scharf && mk == w_plan_mk_
        && (w_mke_ || (strom_alt && w_mke_time_ != UINT64_MAX)))
        return;

    if (!w_scharf_ || neu_scharf) w_mke_ = false;
    w_scharf_   = true;
    w_plan_mk_  = mk;
    w_mke_time_ = UINT64_MAX;
    if (w_mke_ || !waitDreht()) return;

    const TrackImage& s = waitStrom();
    // Welche Sync-Gruppen der Markendecoder mit diesem MK erkennt (Handbuch §4.3/§5.3).
    static const std::vector<size_t> keine;
    const std::vector<size_t>& gruppen =
        (s.encoding == Encoding::MFM) ? (mk ? w_sync_idx_ : w_sync_a1_)
                                      : (mk ? w_sync_fm_ : keine);
    if (gruppen.empty()) return;                   // nie eine Marke

    const uint64_t t  = waitJetzt();
    const uint64_t P  = static_cast<uint64_t>(drives_[selected_drive_].indexPeriodCycles(cpu_hz_));
    const uint64_t p  = static_cast<uint64_t>(waitByteperiode());
    const uint64_t ph = (static_cast<uint64_t>(index_cycle_acc_) + (t - w_now_)) % P;
    const uint64_t rs = t - ph;
    // Erstes Fenster, das NACH t beginnt — bzw. mit setMkeJedesSyncByte schon das
    // zuletzt FERTIG gewordene (j0 − 1, s. u.).
    const uint64_t j_lauf = ph / p;                // Fenster, in dem t liegt
    const uint64_t j0 = mke_jedes_sync_ ? (j_lauf > 0 ? j_lauf - 1 : 0) : (ph + p - 1) / p;
    for (size_t s0 : gruppen) {
        if (s0 >= j0 && (s0 + 1) * p <= P) {
            // setMkeJedesSyncByte: ist das zuletzt fertig gewordene Byte (j_lauf − 1, es
            // liegt noch im Daten-PIO) ein Sync-Byte, gilt die Marke sofort — Ersatz für den
            // Gleichlauf-Zufall, mit dem die Abfrageschleife des PRG-710-ROMs am Gerät
            // früher oder später ein Sync-Byte im Abfragefenster erwischt
            // (doc/design/20_prg710.md AP-P1d).  Ohne ihn liefe das starre Zeitraster des
            // Emulators immer an denselben Gruppen vorbei.
            // Bis AP-P3 galt „sofort“ schon, wenn der Kopf IN einem Sync-Byte stand: beim
            // ERSTEN Byte der Gruppe lag dann noch das Byte davor (00H) im Daten-PIO, das
            // erste `IN (16H)` holte es als „Marke“ und das Datenfeld kam um die Sync-Bytes
            // verschoben an — „DISKERROR C6“ am Zweitlader des PRG 710 (AP-P3).
            w_mke_time_ = (mke_jedes_sync_ && j_lauf > 0 && s0 == j_lauf - 1)
                        ? t : rs + (s0 + 1) * p;
            return;
        }
    }
    w_mke_time_ = rs + P + std::min((gruppen.front() + 1) * p, P);   // nächste Umdrehung
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
        w_schreib_fenster_.clear();
        wr_id_ = 0; wr_size_ = 128; wr_cyl_ = 0; wr_head_ = current_head_;
        const TrackImage& s = waitStrom();
        if (!s.empty()) {
            const uint64_t P  = static_cast<uint64_t>(drives_[selected_drive_].indexPeriodCycles(cpu_hz_));
            const uint64_t p  = static_cast<uint64_t>(waitByteperiode());
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
        // Für den Fall einer ganzen Spur (FORMAT.COM): Ort und Seite JETZT festhalten.
        // Die Seite trägt das Schreib-Steuerwort selbst (/FR, Bit2: 1 = Kopf 0) — anders
        // als beim Datenfeld, dessen Seite das vorangehende Lese-Steuerwort gewählt hat.
        w_schreib_zyl_   = drives_[selected_drive_].currentCylinder();
        w_schreib_kopf_  = (drives_[selected_drive_].profile().num_heads <= 1)
                         ? 0 : ((data & 0x04) ? 0 : 1);
        w_schreib_start_ = waitJetzt();
        LOG_DEBUG("K5122", "WAIT: Schreiben beginnt (Ziel-Datenfeld C=%u H=%u S=%u, %u B)",
                  wr_cyl_, wr_head_, wr_id_, wr_size_);
    } else if (we_writing_ && ((!we_now && we_prev) || ((data & 0x08) && !(prev & 0x08)))) {
        // Ende des Schreibens: /WE wieder 1 — oder /STR = 1, dann fliesst ohnehin nichts
        // mehr zur Scheibe.  Übernommen wird SOFORT (Regel „Lese-Strobe committet
        // anstehenden Schreibstrom“, 2026-08-06): was der Kopf geschrieben hat, liegt auf
        // der Scheibe, bevor die CPU den nächsten Befehl ausführt.
        waitSchreibenUebernehmen();
    }

    prev_ctrl_a_ = data;

    // Marken-FF: scharf bei /STR = 0 und MR = 0; der Übergang dahin macht neu scharf.
    const bool scharf_jetzt = !(data & 0x08) && !(data & 0x20);
    const bool scharf_vorher = !(prev & 0x08) && !(prev & 0x20);
    waitMkePlanen(scharf_jetzt && !scharf_vorher);
    updateStatusPortB();
}

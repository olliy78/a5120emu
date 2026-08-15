/**
 * @file track_sync.cpp
 * @brief Implementierung von TrackSync (Abbild ⇄ echte Diskette, spurweise).
 *
 * @see core/peripherals/floppy_drive/track_sync.h
 * @see doc/design/14_physische_diskette.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/track_sync.h"
#include "core/peripherals/floppy_drive/bit_codec.h"

#include <algorithm>
#include <cstdlib>

namespace {
using Uhr = std::chrono::steady_clock;
}  // namespace

// ─── Aufbau / Abbau ──────────────────────────────────────────────────────────

TrackSync::TrackSync(const TrackSyncSpec& spec, DiskMedium& medium)
    : spec_(spec), medium_(&medium) {
    if (spec_.num_cyls  == 0) spec_.num_cyls  = 1;
    if (spec_.num_heads == 0) spec_.num_heads = 1;

    medium_->resize(spec_.num_cyls, spec_.num_heads);
    medium_->setDefaultEncoding(spec_.default_encoding);
    medium_->markAllUnknown();         // nichts ist gelesen — DAS ist der Unterschied
    medium_->setLoader(this);

    eintraege_.assign(static_cast<size_t>(spec_.num_cyls) * spec_.num_heads, Eintrag{});
    zaehler_.tracks_total = static_cast<uint16_t>(eintraege_.size());
}

TrackSync::~TrackSync() {
    detach();
}

uint32_t TrackSync::nominalBitcells() const {
    // Zellen je Umdrehung: Zellrate (kbit/s → Zellen/s = ×2000) mal Umdrehungsdauer.
    const uint32_t rpm = spec_.rpm ? spec_.rpm : 300;
    return static_cast<uint32_t>(spec_.cell_rate_kbps) * 2000u * 60u / rpm;
}

// ─── Vordergrund ─────────────────────────────────────────────────────────────

bool TrackSync::ensureLoaded(uint8_t cyl, uint8_t head) {
    if (!gueltig(cyl, head)) return false;

    std::unique_lock<std::mutex> sperre(m_);
    if (stop_ || !medium_) return false;
    if (medium_->state(cyl, head) != TrackState::Unknown) return true;

    Eintrag& e = eintraege_[idx(cyl, head)];
    e.failed   = false;                 // ein ausdrücklicher Zugriff versucht es erneut
    if (e.job_id == 0) {
        // Vorhandenen Vorratsauftrag heraufstufen statt einen zweiten einzustellen —
        // sonst läse man dieselbe Spur zweimal (Feinentwurf §5.2).
        if (e.prio != SyncPriority::Demand) e.seq = ++seq_zaehler_;
        e.kind = SyncJobKind::Read;
        e.prio = SyncPriority::Demand;
        cv_arbeit_.notify_all();
    }

    const auto frist = std::chrono::milliseconds(spec_.request_timeout_ms);
    cv_fertig_.wait_for(sperre, frist, [&] {
        return stop_ || !medium_ ||
               medium_->state(cyl, head) != TrackState::Unknown ||
               eintraege_[idx(cyl, head)].failed;
    });

    if (medium_ && medium_->state(cyl, head) != TrackState::Unknown) return true;
    if (medium_ && !eintraege_[idx(cyl, head)].failed && !stop_) {
        letzter_fehler_ = "Zeitüberschreitung beim Lesen von Spur " +
                          std::to_string(cyl) + "/" + std::to_string(head);
        ++zaehler_.errors;
    }
    return false;
}

void TrackSync::trackChanged(uint8_t cyl, uint8_t head) {
    if (!gueltig(cyl, head)) return;

    std::lock_guard<std::mutex> sperre(m_);
    Eintrag& e = eintraege_[idx(cyl, head)];
    ++e.changes;
    e.dirty_since   = Uhr::now();
    e.dirty_pending = spec_.writable;
    // Geweckt wird NICHT: die Spur muss erst zur Ruhe kommen (§7).  Der Arbeitsfaden
    // wacht von selbst auf, weil takeJob() seine Wartezeit auf die Ruhefrist begrenzt.
}

bool TrackSync::loadAll() {
    bool alles = true;
    for (uint8_t c = 0; c < spec_.num_cyls; ++c)
        for (uint8_t h = 0; h < spec_.num_heads; ++h) {
            {
                std::lock_guard<std::mutex> sperre(m_);
                if (stop_ || !medium_) return false;
                if (medium_->state(c, h) != TrackState::Unknown) continue;
            }
            if (!ensureLoaded(c, h)) alles = false;
        }
    return alles;
}

bool TrackSync::flushPending(int timeout_ms) {
    std::unique_lock<std::mutex> sperre(m_);
    if (!spec_.writable || stop_) return true;

    // Ruhefrist überspringen: alles, was ansteht, soll sofort hinaus.
    const auto lange_her = Uhr::now() - std::chrono::hours(1);
    bool etwas = false;
    for (size_t i = 0; i < eintraege_.size(); ++i) {
        if (!eintraege_[i].dirty_pending) continue;
        eintraege_[i].dirty_since = lange_her;
        etwas = true;
    }
    if (!etwas) return true;
    cv_arbeit_.notify_all();

    const uint32_t ms = (timeout_ms > 0) ? static_cast<uint32_t>(timeout_ms)
                                         : spec_.request_timeout_ms;
    const bool fertig = cv_fertig_.wait_for(
        sperre, std::chrono::milliseconds(ms), [&] {
            if (stop_) return true;
            for (const Eintrag& e : eintraege_)
                if (e.dirty_pending) return false;
            return true;
        });
    return fertig && !stop_;
}

// ─── Auftragsauswahl ─────────────────────────────────────────────────────────

bool TrackSync::waehleAuftrag(SyncJob& out) {
    if (!medium_) return false;
    const auto jetzt = Uhr::now();
    const auto ruhe  = std::chrono::milliseconds(spec_.write_settle_ms);

    size_t   bester      = SIZE_MAX;
    uint64_t beste_seq   = 0;
    // 1) Lesen auf Anforderung — nach Eintreffreihenfolge.
    for (size_t i = 0; i < eintraege_.size(); ++i) {
        const Eintrag& e = eintraege_[i];
        if (e.job_id != 0 || e.prio != SyncPriority::Demand) continue;
        if (bester == SIZE_MAX || e.seq < beste_seq) { bester = i; beste_seq = e.seq; }
    }
    SyncJobKind  art  = SyncJobKind::Read;
    SyncPriority prio = SyncPriority::Demand;

    // 2) Rückführung — die am längsten ruhende zuerst.
    if (bester == SIZE_MAX && spec_.writable) {
        Uhr::time_point aelteste{};
        for (size_t i = 0; i < eintraege_.size(); ++i) {
            const Eintrag& e = eintraege_[i];
            if (e.job_id != 0 || !e.dirty_pending) continue;
            if (jetzt - e.dirty_since < ruhe) continue;     // noch nicht zur Ruhe gekommen
            if (bester == SIZE_MAX || e.dirty_since < aelteste) {
                bester = i; aelteste = e.dirty_since;
            }
        }
        if (bester != SIZE_MAX) { art = SyncJobKind::Write; prio = SyncPriority::Writeback; }
    }

    // 3) Vorauslesen — unbekannte Spur mit dem kürzesten Kopfweg.
    if (bester == SIZE_MAX && spec_.read_ahead) {
        int bester_weg = 0;
        for (uint8_t c = 0; c < spec_.num_cyls; ++c)
            for (uint8_t h = 0; h < spec_.num_heads; ++h) {
                const size_t i = idx(c, h);
                const Eintrag& e = eintraege_[i];
                if (e.job_id != 0 || e.failed) continue;
                if (medium_->state(c, h) != TrackState::Unknown) continue;
                const int weg = std::abs(static_cast<int>(c) -
                                         static_cast<int>(letzter_cyl_)) * 2 + h;
                if (bester == SIZE_MAX || weg < bester_weg) { bester = i; bester_weg = weg; }
            }
        if (bester != SIZE_MAX) { art = SyncJobKind::Read; prio = SyncPriority::Readahead; }
    }

    if (bester == SIZE_MAX) return false;

    Eintrag& e = eintraege_[bester];
    e.kind   = art;
    e.prio   = prio;
    e.job_id = naechste_id_++;
    if (naechste_id_ == 0) naechste_id_ = 1;
    e.changes_at_handout = e.changes;

    out.id   = e.job_id;
    out.kind = art;
    out.cyl  = static_cast<uint8_t>(bester / spec_.num_heads);
    out.head = static_cast<uint8_t>(bester % spec_.num_heads);
    out.prio = prio;

    letzter_cyl_ = out.cyl;
    laufend_     = out;
    zaehler_.busy_kind = static_cast<uint8_t>(art);
    zaehler_.busy_cyl  = out.cyl;
    zaehler_.busy_head = out.head;
    return true;
}

TrackSync::Eintrag* TrackSync::eintragZuAuftrag(uint32_t id, uint8_t& cyl, uint8_t& head) {
    if (id == 0) return nullptr;
    for (size_t i = 0; i < eintraege_.size(); ++i) {
        if (eintraege_[i].job_id != id) continue;
        cyl  = static_cast<uint8_t>(i / spec_.num_heads);
        head = static_cast<uint8_t>(i % spec_.num_heads);
        return &eintraege_[i];
    }
    return nullptr;
}

// ─── Arbeitsfaden ────────────────────────────────────────────────────────────

bool TrackSync::takeJob(SyncJob& out, int timeout_ms) {
    std::unique_lock<std::mutex> sperre(m_);

    if (abholung_laeuft_) {          // genau ein Arbeitsfaden (Feinentwurf §9.1)
        letzter_fehler_ = "zweiter Arbeitsfaden abgewiesen";
        return false;
    }
    abholung_laeuft_ = true;
    struct Ende { bool& f; ~Ende() { f = false; } } ende{abholung_laeuft_};

    const auto frist = Uhr::now() + std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 0);
    for (;;) {
        if (stop_) {
            out = SyncJob{}; out.kind = SyncJobKind::Stop;
            return true;
        }
        if (waehleAuftrag(out)) return true;

        // Nichts zu tun: bis zur nächsten Ruhefrist schlafen, längstens bis @p frist.
        auto weckzeit = frist;
        if (spec_.writable) {
            const auto ruhe = std::chrono::milliseconds(spec_.write_settle_ms);
            for (const Eintrag& e : eintraege_)
                if (e.dirty_pending && e.job_id == 0)
                    weckzeit = std::min(weckzeit, e.dirty_since + ruhe);
        }
        zaehler_.busy_cyl = zaehler_.busy_head = 255;
        zaehler_.busy_kind = 0;

        if (weckzeit <= Uhr::now()) {
            if (frist <= Uhr::now()) return false;      // Zeit um, nichts zu tun
            continue;
        }
        cv_arbeit_.wait_until(sperre, weckzeit);
        if (!stop_ && Uhr::now() >= frist) {
            // Frist abgelaufen; ein letzter Blick, dann zurück.
            if (waehleAuftrag(out)) return true;
            return false;
        }
    }
}

bool TrackSync::fetchWrite(uint32_t id, std::vector<uint8_t>& cells, uint32_t& bitcells) {
    TrackImage kopie;
    {
        std::lock_guard<std::mutex> sperre(m_);
        if (!medium_) return false;
        uint8_t c = 0, h = 0;
        Eintrag* e = eintragZuAuftrag(id, c, h);
        if (!e || e->kind != SyncJobKind::Write) {
            letzter_fehler_ = "fetchWrite: unbekannter Auftrag";
            return false;
        }
        kopie = medium_->peek(c, h);
    }
    // Codieren außerhalb der Sperre — der Vordergrund soll nicht darauf warten.
    bitcells = kopie.bitcells ? kopie.bitcells : nominalBitcells();
    cells    = BitCodec::encode(kopie, bitcells);
    return true;
}

bool TrackSync::completeRead(uint32_t id, const uint8_t* cells, size_t len,
                             uint32_t bitcells) {
    if (!cells && len > 0) return false;
    std::vector<uint8_t> zellen(cells, cells + len);
    if (bitcells == 0 || bitcells > len * 8) bitcells = static_cast<uint32_t>(len * 8);

    // Decodieren vor dem Sperren: das ist Rechenarbeit, kein Zustand.
    Encoding vorschlag;
    {
        std::lock_guard<std::mutex> sperre(m_);
        if (!medium_) return false;
        vorschlag = medium_->defaultEncoding();
    }
    // Ueberabtastung auffangen (§8.1): liefert der Adapter eine DD-Diskette mit
    // doppelter Zellrate — weil die eingestellte Rate nicht stimmt oder er es so tut —,
    // ergibt die nominale Decodierung nichts.  Dann herunterrechnen, wie es der
    // HFE-Ladepfad bei Flux-Mitschnitten schon immer tut.  Der einmal gefundene Faktor
    // gilt fuer die ganze Diskette.
    const uint32_t bekannt = ueberabtastung_.load(std::memory_order_relaxed);
    TrackImage     spur;
    for (uint32_t f : {bekannt ? bekannt : 1u, 1u, 2u, 3u, 4u}) {
        if (f == 0) continue;
        std::vector<uint8_t> z = zellen;
        uint32_t             bc = bitcells;
        if (f > 1) z = BitCodec::downsampleCells(zellen, bitcells, f, bc);
        TrackImage probe = BitCodec::decodeAuto(z, bc, vorschlag);
        if (BitCodec::markCount(probe) > 0) {
            spur = std::move(probe);
            ueberabtastung_.store(f, std::memory_order_relaxed);
            break;
        }
    }
    // Markenlos = unformatiert: als LEERE Spur ablegen, damit der Controller Gap-Flux
    // streamt statt Rauschbytes zu liefern (doc/design/09_floppy_drive.md §7).
    if (BitCodec::markCount(spur) == 0) spur = {};

    std::lock_guard<std::mutex> sperre(m_);
    if (!medium_) return false;
    uint8_t c = 0, h = 0;
    Eintrag* e = eintragZuAuftrag(id, c, h);
    if (!e || e->kind != SyncJobKind::Read) {
        letzter_fehler_ = "completeRead: unbekannter Auftrag";
        return false;
    }
    medium_->loadTrack(c, h, std::move(spur));
    *e = Eintrag{};
    ++zaehler_.reads_done;
    zaehler_.busy_cyl = zaehler_.busy_head = 255;
    zaehler_.busy_kind = 0;
    cv_fertig_.notify_all();
    return true;
}

bool TrackSync::completeWrite(uint32_t id) {
    std::lock_guard<std::mutex> sperre(m_);
    if (!medium_) return false;
    uint8_t c = 0, h = 0;
    Eintrag* e = eintragZuAuftrag(id, c, h);
    if (!e || e->kind != SyncJobKind::Write) {
        letzter_fehler_ = "completeWrite: unbekannter Auftrag";
        return false;
    }
    // Wurde die Spur WÄHREND des Schreibens erneut geändert, bleibt sie schmutzig —
    // sonst ginge die jüngste Änderung stillschweigend verloren.
    const bool erneut = (e->changes != e->changes_at_handout);
    e->job_id = 0;
    e->kind   = SyncJobKind::None;
    e->prio   = SyncPriority::None;
    if (erneut) {
        e->dirty_since = Uhr::now();
    } else {
        e->dirty_pending = false;
        medium_->clearTrackDirty(c, h);
    }
    ++zaehler_.writes_done;
    zaehler_.busy_cyl = zaehler_.busy_head = 255;
    zaehler_.busy_kind = 0;
    cv_fertig_.notify_all();
    return true;
}

void TrackSync::failJob(uint32_t id, const std::string& msg) {
    std::lock_guard<std::mutex> sperre(m_);
    uint8_t c = 0, h = 0;
    Eintrag* e = eintragZuAuftrag(id, c, h);
    if (!e) return;

    letzter_fehler_ = msg;
    ++zaehler_.errors;
    const bool war_lesen = (e->kind == SyncJobKind::Read);
    e->job_id = 0;
    e->kind   = SyncJobKind::None;
    e->prio   = SyncPriority::None;
    if (war_lesen) {
        // Unlesbare Spur: unbekannt lassen, aber nicht endlos wiederholen.
        e->failed = true;
    } else {
        // Nicht geschriebene Änderung: erneut einstellen, sofort fällig.
        e->dirty_pending = true;
        e->dirty_since   = Uhr::now();
    }
    zaehler_.busy_cyl = zaehler_.busy_head = 255;
    zaehler_.busy_kind = 0;
    cv_fertig_.notify_all();
    cv_arbeit_.notify_all();
}

void TrackSync::shutdown() {
    {
        std::lock_guard<std::mutex> sperre(m_);
        if (stop_) return;
        stop_ = true;
    }
    cv_arbeit_.notify_all();
    cv_fertig_.notify_all();
}

void TrackSync::detach() {
    {
        std::lock_guard<std::mutex> sperre(m_);
        if (medium_) {
            medium_->setLoader(nullptr);
            medium_ = nullptr;      // ab hier scheitert jeder Zugriff sauber
        }
        stop_ = true;
    }
    cv_arbeit_.notify_all();
    cv_fertig_.notify_all();
}

bool TrackSync::stopped() const {
    std::lock_guard<std::mutex> sperre(m_);
    return stop_;
}

// ─── Steuerung / Anzeige ─────────────────────────────────────────────────────

void TrackSync::setReadAhead(bool on) {
    {
        std::lock_guard<std::mutex> sperre(m_);
        spec_.read_ahead = on;
    }
    if (on) cv_arbeit_.notify_all();
}

bool TrackSync::readAhead() const {
    std::lock_guard<std::mutex> sperre(m_);
    return spec_.read_ahead;
}

SyncStats TrackSync::stats() const {
    std::lock_guard<std::mutex> sperre(m_);
    SyncStats s = zaehler_;
    s.stopped       = stop_;
    if (!medium_) return s;
    s.tracks_known  = 0;
    s.tracks_dirty  = 0;
    s.tracks_failed = 0;
    for (uint8_t c = 0; c < spec_.num_cyls; ++c)
        for (uint8_t h = 0; h < spec_.num_heads; ++h) {
            const TrackState st = medium_->state(c, h);
            if (st != TrackState::Unknown) ++s.tracks_known;
            if (st == TrackState::Dirty)   ++s.tracks_dirty;
            if (eintraege_[idx(c, h)].failed) ++s.tracks_failed;
        }
    return s;
}

std::string TrackSync::lastError() const {
    std::lock_guard<std::mutex> sperre(m_);
    return letzter_fehler_;
}

/**
 * @file k1520_sync_api.cpp
 * @brief Implementierung der C-ABI für die physische Diskette (beide Bibliotheken).
 *
 * Die Datei ist absichtlich dünn: sie packt Handles aus und reicht an @ref TrackSync
 * weiter.  Die ganze Logik (Prioritäten, Zustände, Blockade) steht dort.
 *
 * @see core/api/k1520_sync_api.h
 * @see doc/design/14_physische_diskette.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/api/k1520_sync_internal.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

TrackSync* sync_of(K1520Sync h) {
    K1520SyncHandle* s = k1520s_handle(h);
    return s ? s->sync.get() : nullptr;
}

}  // namespace

K1520SyncHandle* k1520s_handle(K1520Sync h) {
    return static_cast<K1520SyncHandle*>(h);
}

std::unique_ptr<DiskImage> k1520s_take_image(K1520Sync h) {
    K1520SyncHandle* s = k1520s_handle(h);
    if (!s) return nullptr;
    return std::move(s->image);
}

// ─── Lebenszyklus ────────────────────────────────────────────────────────────

extern "C" K1520Sync k1520s_create(const K1520SyncSpec* spec) {
    if (!spec) return nullptr;

    TrackSyncSpec s;
    s.num_cyls       = spec->num_cyls  ? spec->num_cyls  : 80;
    s.num_heads      = spec->num_heads ? spec->num_heads : 2;
    s.cell_rate_kbps = spec->cell_rate_kbps ? spec->cell_rate_kbps : 250;
    s.rpm            = spec->rpm ? spec->rpm : 300;
    s.writable       = spec->writable;
    s.default_encoding = (spec->default_encoding == 0) ? Encoding::FM : Encoding::MFM;
    s.read_ahead     = spec->read_ahead;
    s.verify_writes  = spec->verify_writes;
    s.write_verify_retries = spec->write_verify_retries;
    if (spec->write_settle_ms)    s.write_settle_ms    = spec->write_settle_ms;
    if (spec->request_timeout_ms) s.request_timeout_ms = spec->request_timeout_ms;

    auto* h = new K1520SyncHandle();
    h->image = DiskImage::openPhysical(s);
    if (!h->image) { delete h; return nullptr; }
    // MITbesitz: der Arbeitsfaden darf das Handle noch in der Hand haben, wenn die
    // Diskette laengst abgemeldet ist.  ~DiskImage loest den Synchronisierer vom
    // Medium (detach), danach scheitern seine Aufrufe sauber statt ins Leere zu greifen.
    h->sync = h->image->syncPtr();
    return h;
}

extern "C" void k1520s_shutdown(K1520Sync h) {
    if (TrackSync* s = sync_of(h)) s->shutdown();
}

extern "C" void k1520s_destroy(K1520Sync h) {
    K1520SyncHandle* s = k1520s_handle(h);
    if (!s) return;
    if (s->sync) s->sync->shutdown();
    delete s;   // ein noch nicht angemeldetes Abbild stirbt hier mit
}

// ─── Arbeitsfaden ────────────────────────────────────────────────────────────

extern "C" bool k1520s_take_job(K1520Sync h, int timeout_ms, K1520SyncJob* out) {
    TrackSync* s = sync_of(h);
    if (!s || !out) return false;

    SyncJob j;
    if (!s->takeJob(j, timeout_ms)) return false;
    out->id   = j.id;
    out->kind = static_cast<uint8_t>(j.kind);
    out->cyl  = j.cyl;
    out->head = j.head;
    out->prio = static_cast<uint8_t>(j.prio);
    return true;
}

extern "C" int k1520s_fetch_write(K1520Sync h, uint32_t id, uint8_t* buf, int buf_len,
                                  uint32_t* bitcells) {
    TrackSync* s = sync_of(h);
    if (!s || !buf || buf_len <= 0) return -1;

    std::vector<uint8_t> zellen;
    uint32_t             cells = 0;
    if (!s->fetchWrite(id, zellen, cells)) return -1;
    if (zellen.size() > static_cast<size_t>(buf_len)) return -1;

    std::memcpy(buf, zellen.data(), zellen.size());
    if (bitcells) *bitcells = cells;
    return static_cast<int>(zellen.size());
}

extern "C" bool k1520s_complete_read(K1520Sync h, uint32_t id, const uint8_t* cells,
                                     int len, uint32_t bitcells) {
    TrackSync* s = sync_of(h);
    if (!s || !cells || len < 0) return false;
    return s->completeRead(id, cells, static_cast<size_t>(len), bitcells);
}

extern "C" bool k1520s_complete_write(K1520Sync h, uint32_t id) {
    TrackSync* s = sync_of(h);
    return s && s->completeWrite(id);
}

extern "C" void k1520s_fail_job(K1520Sync h, uint32_t id, const char* msg) {
    if (TrackSync* s = sync_of(h)) s->failJob(id, msg ? msg : "unbekannter Fehler");
}

// ─── Steuerung / Anzeige ─────────────────────────────────────────────────────

extern "C" void k1520s_set_read_ahead(K1520Sync h, bool on) {
    if (TrackSync* s = sync_of(h)) s->setReadAhead(on);
}

extern "C" bool k1520s_stats(K1520Sync h, K1520SyncStats* out) {
    TrackSync* s = sync_of(h);
    if (!s || !out) return false;

    const SyncStats st = s->stats();
    out->tracks_total  = st.tracks_total;
    out->tracks_known  = st.tracks_known;
    out->tracks_dirty  = st.tracks_dirty;
    out->tracks_failed = st.tracks_failed;
    out->tracks_defect = st.tracks_defect;
    out->reads_done    = st.reads_done;
    out->writes_done   = st.writes_done;
    out->verifies_done = st.verifies_done;
    out->verify_failed = st.verify_failed;
    out->errors        = st.errors;
    out->busy_kind     = st.busy_kind;
    out->busy_cyl      = st.busy_cyl;
    out->busy_head     = st.busy_head;
    out->stopped       = st.stopped;
    return true;
}

extern "C" const char* k1520s_last_error(K1520Sync h) {
    K1520SyncHandle* s = k1520s_handle(h);
    if (!s) return "";
    if (s->sync) s->last_error = s->sync->lastError();
    return s->last_error.c_str();
}

extern "C" bool k1520s_load_all(K1520Sync h) {
    TrackSync* s = sync_of(h);
    return s && s->loadAll();
}

extern "C" bool k1520s_flush(K1520Sync h, int timeout_ms) {
    TrackSync* s = sync_of(h);
    return s && s->flushPending(timeout_ms);
}

extern "C" int k1520s_rewrite_all(K1520Sync h) {
    TrackSync* s = sync_of(h);
    return s ? static_cast<int>(s->rewriteAll()) : 0;
}

extern "C" int k1520s_defect_tracks(K1520Sync h, char* buf, int buf_len) {
    TrackSync* s = sync_of(h);
    if (!s || !buf || buf_len <= 0) return -1;
    const std::string t = s->defectText();
    const int n = static_cast<int>(std::min<size_t>(t.size(),
                                                    static_cast<size_t>(buf_len - 1)));
    std::memcpy(buf, t.data(), static_cast<size_t>(n));
    buf[n] = 0;
    return n;
}

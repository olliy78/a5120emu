/**
 * @file k1520_disk_api.cpp
 * @brief Umsetzung der C-ABI von `libk1520disk.so`.
 *
 * Duenne Huelle um @ref DiskVolume.  Die Kataloge werden **einmal** geladen und
 * danach geteilt (sie sind unveraenderlich); jedes Handle haelt seine eigenen
 * Zeichenketten-Puffer, damit die Rueckgaben bis zum naechsten gleichartigen
 * Aufruf gueltig bleiben.
 *
 * @see core/api/k1520_disk_api.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/api/k1520_disk_api.h"

#include "core/filesystem/disk_volume.h"

#include <memory>
#include <string>
#include <vector>

namespace {

// ─── Kataloge (einmal geladen) ───────────────────────────────────────────────

struct Kataloge {
    FormatCatalog formate;
    FsCatalog     dateisysteme;
    std::string   bericht;
};

const Kataloge& kataloge() {
    static Kataloge k = [] {
        Kataloge x;
        std::string fatal;
        x.formate = FormatCatalog::loadDefault(&fatal);
        if (!fatal.empty()) x.bericht += fatal + "\n";
        for (const auto& q : x.formate.sources()) x.bericht += "Formatkatalog: " + q + "\n";
        for (const auto& i : x.formate.issues())  x.bericht += "  " + i + "\n";

        std::string fatal2;
        x.dateisysteme = FsCatalog::loadDefault(x.formate, &fatal2);
        if (!fatal2.empty()) x.bericht += fatal2 + "\n";
        for (const auto& q : x.dateisysteme.sources())
            x.bericht += "Dateisysteme:  " + q + "\n";
        for (const auto& i : x.dateisysteme.issues()) x.bericht += "  " + i + "\n";
        return x;
    }();
    return k;
}

// ─── Handle ──────────────────────────────────────────────────────────────────

struct Handle {
    std::unique_ptr<DiskVolume> vol;
    std::vector<FileEntry>      eintraege;   ///< Stand des letzten k1520d_list
    TrackView                   spur;        ///< Stand des letzten k1520d_track_scan
    // Puffer je Getter — die Rueckgabe gilt bis zum naechsten Aufruf DERSELBEN Funktion.
    std::string s_error, s_name, s_type, s_attrs, s_date, s_dir, s_label, s_created;
    std::string s_fmt, s_fs, s_alt, s_remarks, s_fit, s_check;
};

Handle* H(K1520Disk h) { return static_cast<Handle*>(h); }

std::string g_open_error;
std::string g_scratch;

/// @brief Sicherer Zugriff auf einen Listeneintrag.
const FileEntry* eintrag(K1520Disk h, int i) {
    if (!h) return nullptr;
    Handle* p = H(h);
    if (i < 0 || i >= static_cast<int>(p->eintraege.size())) return nullptr;
    return &p->eintraege[static_cast<size_t>(i)];
}

const char* halte(std::string& puffer, std::string wert) {
    puffer = std::move(wert);
    return puffer.c_str();
}

/// @brief Sicherer Zugriff auf einen Spurabschnitt des letzten @c k1520d_track_scan.
const TrackSpan* abschnitt(K1520Disk h, int i) {
    if (!h) return nullptr;
    Handle* p = H(h);
    if (i < 0 || i >= static_cast<int>(p->spur.spans.size())) return nullptr;
    return &p->spur.spans[static_cast<size_t>(i)];
}

TransferOptions optionen(K1520DMode mode, bool overwrite) {
    TransferOptions o;
    o.text      = (mode == K1520D_TEXT);
    o.overwrite = overwrite;
    return o;
}

}  // namespace

// ─── Oeffnen / Anlegen / Speichern ───────────────────────────────────────────

extern "C" K1520Disk k1520d_open(const char* path, const char* fs_name, bool read_only) {
    g_open_error.clear();
    if (!path || !*path) { g_open_error = "kein Pfad angegeben"; return nullptr; }

    auto h = std::make_unique<Handle>();
    std::string err;
    h->vol = DiskVolume::open(path, fs_name ? fs_name : "",
                              kataloge().formate, kataloge().dateisysteme, err, read_only);
    if (!h->vol) { g_open_error = err; return nullptr; }
    return h.release();
}

extern "C" K1520Disk k1520d_create(const char* path, const char* fs_name,
                                   const char* label) {
    return k1520d_create_bootable(path, fs_name, label, nullptr);
}

extern "C" K1520Disk k1520d_create_bootable(const char* path, const char* fs_name,
                                            const char* label, const char* boot_image) {
    g_open_error.clear();
    if (!path || !*path)       { g_open_error = "kein Pfad angegeben"; return nullptr; }
    if (!fs_name || !*fs_name) { g_open_error = "kein Dateisystem angegeben — beim "
                                                "Anlegen gibt es nichts zu erkennen";
                                 return nullptr; }
    auto h = std::make_unique<Handle>();
    std::string err;
    h->vol = DiskVolume::create(path, fs_name, label ? label : "",
                                kataloge().formate, kataloge().dateisysteme, err,
                                boot_image ? boot_image : "");
    if (!h->vol) { g_open_error = err; return nullptr; }
    return h.release();
}

extern "C" bool k1520d_flush(K1520Disk h) {
    return h && H(h)->vol->flush();
}

extern "C" bool k1520d_save_as(K1520Disk h, const char* path) {
    return h && path && H(h)->vol->saveAs(path);
}

extern "C" bool k1520d_export_image(K1520Disk h, const char* path) {
    return h && path && H(h)->vol->exportImage(path);
}

extern "C" bool k1520d_read_only(K1520Disk h) {
    return h ? H(h)->vol->readOnly() : true;
}

extern "C" void k1520d_set_read_only(K1520Disk h, bool ro) {
    if (h) H(h)->vol->setReadOnly(ro);
}

extern "C" void k1520d_set_backup(K1520Disk h, bool on) {
    if (h) H(h)->vol->setBackup(on);
}

extern "C" void k1520d_close(K1520Disk h) { delete H(h); }

extern "C" const char* k1520d_last_error(K1520Disk h) {
    if (!h) return g_open_error.c_str();
    return halte(H(h)->s_error, H(h)->vol->lastError());
}

extern "C" const char* k1520d_last_open_error(void) { return g_open_error.c_str(); }

// ─── Katalog und Erkennung ───────────────────────────────────────────────────

extern "C" int k1520d_fs_count(void) {
    return static_cast<int>(kataloge().dateisysteme.profiles().size());
}

extern "C" const char* k1520d_fs_name(int i) {
    const auto& p = kataloge().dateisysteme.profiles();
    if (i < 0 || i >= static_cast<int>(p.size())) return "";
    return p[static_cast<size_t>(i)].name.c_str();
}

extern "C" const char* k1520d_fs_description(const char* name) {
    const FsProfile* p = name ? kataloge().dateisysteme.find(name) : nullptr;
    return p ? p->description.c_str() : "";
}

extern "C" const char* k1520d_fs_format(const char* name) {
    const FsProfile* p = name ? kataloge().dateisysteme.find(name) : nullptr;
    return p ? p->format.c_str() : "";
}

extern "C" const char* k1520d_fs_type(const char* name) {
    const FsProfile* p = name ? kataloge().dateisysteme.find(name) : nullptr;
    return p ? fsTypeName(p->type) : "";
}

extern "C" uint64_t k1520d_fs_boot_capacity(const char* fs_name) {
    const FsProfile* p = fs_name ? kataloge().dateisysteme.find(fs_name) : nullptr;
    if (!p) return 0;
    const DiskFormat* f = kataloge().formate.find(p->format);
    return f ? DiskVolume::bootAreaCapacity(*p, *f) : 0;
}

extern "C" uint64_t k1520d_boot_area_size(K1520Disk h, int volume) {
    return h ? H(h)->vol->bootAreaSize(volume) : 0;
}

extern "C" bool k1520d_read_boot_image(K1520Disk h, int volume, const char* path) {
    return h && path && *path && H(h)->vol->readBootImageToFile(path, volume);
}

extern "C" bool k1520d_write_boot_image(K1520Disk h, int volume, const char* path) {
    return h && path && *path && H(h)->vol->writeBootImageFile(path, volume);
}

extern "C" const char* k1520d_catalog_report(void) {
    return kataloge().bericht.c_str();
}

extern "C" const char* k1520d_detect(const char* path) {
    g_scratch.clear();
    if (!path) return g_scratch.c_str();
    std::string err;
    auto v = DiskVolume::open(path, "", kataloge().formate, kataloge().dateisysteme,
                              err, /*read_only=*/true);
    if (v) g_scratch = v->detection().filesystem;
    else   g_open_error = err;
    return g_scratch.c_str();
}

extern "C" const char* k1520d_detected_format(K1520Disk h) {
    return h ? halte(H(h)->s_fmt, H(h)->vol->detection().format) : "";
}

extern "C" const char* k1520d_detected_fs(K1520Disk h) {
    return h ? halte(H(h)->s_fs, H(h)->vol->detection().filesystem) : "";
}

extern "C" bool k1520d_detection_unambiguous(K1520Disk h) {
    return h ? H(h)->vol->detection().unambiguous : false;
}

extern "C" const char* k1520d_detection_alternatives(K1520Disk h) {
    if (!h) return "";
    std::string s;
    for (const auto& a : H(h)->vol->detection().alternatives) {
        if (!s.empty()) s += ", ";
        s += a;
    }
    return halte(H(h)->s_alt, std::move(s));
}

extern "C" const char* k1520d_detection_remarks(K1520Disk h) {
    return h ? halte(H(h)->s_remarks, H(h)->vol->detection().remarks) : "";
}

// ─── Seiten ──────────────────────────────────────────────────────────────────

extern "C" int k1520d_volume_count(K1520Disk h) {
    return h ? H(h)->vol->volumeCount() : 0;
}

extern "C" const char* k1520d_volume_dir(K1520Disk h, int v) {
    return h ? halte(H(h)->s_dir, H(h)->vol->volumeDir(v)) : "";
}

extern "C" const char* k1520d_volume_label(K1520Disk h, int v) {
    return h ? halte(H(h)->s_label, H(h)->vol->volumeInfo(v).label) : "";
}

extern "C" uint64_t k1520d_volume_total(K1520Disk h, int v) {
    return h ? H(h)->vol->volumeInfo(v).total_bytes : 0;
}

extern "C" uint64_t k1520d_volume_free(K1520Disk h, int v) {
    return h ? H(h)->vol->volumeInfo(v).free_bytes : 0;
}

extern "C" uint64_t k1520d_volume_used(K1520Disk h, int v) {
    return h ? H(h)->vol->volumeInfo(v).used_bytes : 0;
}

// ─── Verzeichnis ─────────────────────────────────────────────────────────────

extern "C" int k1520d_list(K1520Disk h) {
    if (!h) return 0;
    H(h)->eintraege = H(h)->vol->list();
    return static_cast<int>(H(h)->eintraege.size());
}

extern "C" int k1520d_entry_volume(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? e->volume : 0;
}

extern "C" const char* k1520d_entry_name(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? halte(H(h)->s_name, e->qualifiedName()) : "";
}

extern "C" int k1520d_entry_user(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? e->user : 0;
}

extern "C" uint64_t k1520d_entry_size(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? e->size : 0;
}

extern "C" const char* k1520d_entry_type(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? halte(H(h)->s_type, e->type) : "";
}

extern "C" const char* k1520d_entry_attrs(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? halte(H(h)->s_attrs, e->attributes) : "";
}

extern "C" const char* k1520d_entry_date(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? halte(H(h)->s_date, e->date) : "";
}

extern "C" bool k1520d_entry_hidden(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? e->hidden : false;
}

#define K1520D_ENTRY_U16(fn, feld)                                   \
    extern "C" uint16_t fn(K1520Disk h, int i) {                     \
        const FileEntry* e = eintrag(h, i);                          \
        return e ? e->feld : 0;                                      \
    }

K1520D_ENTRY_U16(k1520d_entry_start,       entry_addr)
K1520D_ENTRY_U16(k1520d_entry_record_len,  record_len)
K1520D_ENTRY_U16(k1520d_entry_block_len,   block_len)
K1520D_ENTRY_U16(k1520d_entry_segment,     segment_start)
K1520D_ENTRY_U16(k1520d_entry_segment_len, segment_len)
K1520D_ENTRY_U16(k1520d_entry_low_addr,    low_addr)
K1520D_ENTRY_U16(k1520d_entry_high_addr,   high_addr)
K1520D_ENTRY_U16(k1520d_entry_stack_size,  stack_size)
K1520D_ENTRY_U16(k1520d_entry_bytes_in_last, bytes_in_last)
#undef K1520D_ENTRY_U16

extern "C" uint32_t k1520d_entry_extra(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? e->extra : 0;
}

extern "C" const char* k1520d_entry_created(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? halte(H(h)->s_created, e->created) : "";
}

extern "C" bool k1520d_set_udos_attrs(K1520Disk h, const char* name,
                                      const char* type, const char* props,
                                      const char* created, const char* modified,
                                      bool set_entry, uint16_t entry,
                                      bool set_block_len, uint16_t block_len,
                                      bool set_segment, uint16_t segment, uint16_t segment_len,
                                      bool set_memory, uint16_t low, uint16_t high,
                                      uint16_t stack,
                                      bool set_extra, uint32_t extra) {
    if (!h || !name) return false;
    UdosAttrs a;
    a.type       = type     ? type     : "";
    a.properties = props    ? props    : "";
    a.created    = created  ? created  : "";
    a.modified   = modified ? modified : "";
    a.set_entry     = set_entry;     a.entry     = entry;
    a.set_block_len = set_block_len; a.block_len = block_len;
    a.set_segment   = set_segment;   a.segment   = segment; a.segment_len = segment_len;
    a.set_memory    = set_memory;    a.low = low; a.high = high; a.stack = stack;
    a.set_extra     = set_extra;     a.extra     = extra;
    return H(h)->vol->setAttributes(FileRef::parse(name), a);
}

extern "C" bool k1520d_set_cpm_attrs(K1520Disk h, const char* name,
                                     bool set_read_only, bool read_only,
                                     bool set_system,    bool system,
                                     bool set_archived,  bool archived,
                                     bool set_user,      int  user) {
    if (!h || !name) return false;
    CpmAttrs a;
    a.set_read_only = set_read_only; a.read_only = read_only;
    a.set_system    = set_system;    a.system    = system;
    a.set_archived  = set_archived;  a.archived  = archived;
    a.set_user      = set_user;      a.user      = user;
    return H(h)->vol->setAttributes(FileRef::parse(name), a);
}

extern "C" bool k1520d_entry_damaged(K1520Disk h, int i) {
    const FileEntry* e = eintrag(h, i);
    return e ? e->damaged : false;
}

// ─── Sektoransicht (Diskeditor) ──────────────────────────────────────────────

extern "C" int k1520d_medium_cylinders(K1520Disk h) {
    return h ? H(h)->vol->mediumCylinders() : 0;
}

extern "C" int k1520d_medium_heads(K1520Disk h) {
    return h ? H(h)->vol->mediumHeads() : 0;
}

extern "C" int k1520d_track_scan(K1520Disk h, int cyl, int head) {
    if (!h || cyl < 0 || head < 0 || cyl > 255 || head > 255) return -1;
    H(h)->spur = H(h)->vol->trackView(static_cast<uint8_t>(cyl),
                                      static_cast<uint8_t>(head));
    return static_cast<int>(H(h)->spur.spans.size());
}

extern "C" bool k1520d_track_exists(K1520Disk h)    { return h && H(h)->spur.exists; }
extern "C" bool k1520d_track_formatted(K1520Disk h) { return h && H(h)->spur.formatted; }
extern "C" int  k1520d_track_bytes(K1520Disk h)     { return h ? static_cast<int>(H(h)->spur.bytes) : 0; }
extern "C" int  k1520d_track_sectors(K1520Disk h)   { return h ? H(h)->spur.sectors : 0; }

extern "C" const char* k1520d_track_encoding(K1520Disk h) {
    if (!h) return "";
    return H(h)->spur.encoding == Encoding::FM ? "FM" : "MFM";
}

extern "C" int k1520d_span_kind(K1520Disk h, int i) {
    const TrackSpan* s = abschnitt(h, i);
    return s ? static_cast<int>(s->kind) : 0;
}

extern "C" double k1520d_span_start(K1520Disk h, int i) {
    const TrackSpan* s = abschnitt(h, i);
    return s ? s->start : 0.0;
}

extern "C" double k1520d_span_end(K1520Disk h, int i) {
    const TrackSpan* s = abschnitt(h, i);
    return s ? s->end : 0.0;
}

#define K1520D_SPAN_INT(fn, ausdruck)                                \
    extern "C" int fn(K1520Disk h, int i) {                          \
        const TrackSpan* s = abschnitt(h, i);                        \
        return s ? static_cast<int>(ausdruck) : -1;                  \
    }
K1520D_SPAN_INT(k1520d_span_index, s->index)
K1520D_SPAN_INT(k1520d_span_id,    s->id)
K1520D_SPAN_INT(k1520d_span_cyl,   s->cyl)
K1520D_SPAN_INT(k1520d_span_head,  s->head)
K1520D_SPAN_INT(k1520d_span_size,  s->size)
#undef K1520D_SPAN_INT

extern "C" bool k1520d_span_id_crc_ok(K1520Disk h, int i) {
    const TrackSpan* s = abschnitt(h, i);
    return s && s->id_crc_ok;
}

extern "C" bool k1520d_span_data_crc_ok(K1520Disk h, int i) {
    const TrackSpan* s = abschnitt(h, i);
    return s && s->data_crc_ok;
}

extern "C" bool k1520d_span_deleted(K1520Disk h, int i) {
    const TrackSpan* s = abschnitt(h, i);
    return s && s->deleted;
}

extern "C" int k1520d_sector_read(K1520Disk h, int cyl, int head, int index,
                                  uint8_t* out, int max_len) {
    if (!h || !out || max_len < 0) return -1;
    std::vector<uint8_t> d;
    uint16_t crc = 0;
    if (!H(h)->vol->readSectorAt(static_cast<uint8_t>(cyl), static_cast<uint8_t>(head),
                                 index, d, crc))
        return -1;
    const int n = static_cast<int>(d.size());
    if (n > max_len) return -1;               // Puffer zu klein — nichts halb kopieren
    std::copy(d.begin(), d.end(), out);
    return n;
}

extern "C" int k1520d_sector_crc(K1520Disk h, int cyl, int head, int index) {
    if (!h) return -1;
    std::vector<uint8_t> d;
    uint16_t crc = 0;
    if (!H(h)->vol->readSectorAt(static_cast<uint8_t>(cyl), static_cast<uint8_t>(head),
                                 index, d, crc))
        return -1;
    return crc;
}

extern "C" int k1520d_sector_crc_for(K1520Disk h, int cyl, int head, int index,
                                     const uint8_t* data, int len) {
    if (!h || !data || len < 0) return -1;
    uint16_t crc = 0;
    if (!H(h)->vol->sectorCrcFor(static_cast<uint8_t>(cyl), static_cast<uint8_t>(head),
                                 index, std::vector<uint8_t>(data, data + len), crc))
        return -1;
    return crc;
}

extern "C" bool k1520d_sector_write(K1520Disk h, int cyl, int head, int index,
                                    const uint8_t* data, int len, int crc) {
    if (!h || !data || len < 0) return false;
    const uint16_t woertlich = static_cast<uint16_t>(crc);
    return H(h)->vol->writeSectorAt(static_cast<uint8_t>(cyl), static_cast<uint8_t>(head),
                                    index, std::vector<uint8_t>(data, data + len),
                                    crc < 0 ? nullptr : &woertlich);
}

extern "C" int k1520d_sector_tail(K1520Disk h, int cyl, int head, int index,
                                  uint8_t* out, int max_len) {
    if (!h || !out || max_len < 0) return -1;
    std::vector<uint8_t> t;
    if (!H(h)->vol->readSectorTail(static_cast<uint8_t>(cyl),
                                   static_cast<uint8_t>(head), index, t))
        return -1;
    const int n = static_cast<int>(t.size());
    if (n > max_len) return -1;
    std::copy(t.begin(), t.end(), out);
    return n;
}

namespace {
/// @brief Gemeinsame Umsetzung der Anlegeangaben in eine @ref NewSectorSpec.
TrackCodec::NewSectorSpec bauSpec(int id, int id_cyl, int id_head, int size, int gap,
                      int tail_bytes, int fill) {
    TrackCodec::NewSectorSpec s;
    s.id         = static_cast<uint8_t>(id);
    s.cyl        = static_cast<uint8_t>(id_cyl);
    s.head       = static_cast<uint8_t>(id_head);
    s.size       = static_cast<uint16_t>(size);
    s.gap_before = static_cast<uint16_t>(gap);
    s.tail_bytes = static_cast<uint16_t>(tail_bytes);
    s.fill       = static_cast<uint8_t>(fill);
    return s;
}
}  // namespace

extern "C" bool k1520d_sector_erase(K1520Disk h, int cyl, int head, int index,
                                    int tail_bytes) {
    if (!h || tail_bytes < 0) return false;
    return H(h)->vol->eraseSectorAt(static_cast<uint8_t>(cyl),
                                    static_cast<uint8_t>(head), index,
                                    static_cast<uint16_t>(tail_bytes));
}

extern "C" bool k1520d_sector_create(K1520Disk h, int cyl, int head,
                                     int id, int id_cyl, int id_head, int size,
                                     int gap, int tail_bytes, int fill, bool mfm) {
    if (!h || gap < 0 || tail_bytes < 0) return false;
    return H(h)->vol->createSector(
        static_cast<uint8_t>(cyl), static_cast<uint8_t>(head),
        bauSpec(id, id_cyl, id_head, size, gap, tail_bytes, fill), mfm);
}

extern "C" int k1520d_sector_plan_pos(K1520Disk h, int cyl, int head, int id, int gap) {
    if (!h || gap < 0) return -1;
    uint32_t von = 0, laenge = 0;
    if (!H(h)->vol->planSector(static_cast<uint8_t>(cyl), static_cast<uint8_t>(head),
                               bauSpec(id, 0, 0, 128, gap, 0, 0xE5), true,
                               von, laenge))
        return -1;
    return static_cast<int>(von);
}

extern "C" int k1520d_sector_plan_len(K1520Disk h, int cyl, int head,
                                      int size, int tail_bytes, bool mfm) {
    if (!h || tail_bytes < 0) return -1;
    uint32_t von = 0, laenge = 0;
    if (!H(h)->vol->planSector(static_cast<uint8_t>(cyl), static_cast<uint8_t>(head),
                               bauSpec(1, 0, 0, size, 0, tail_bytes, 0xE5), mfm,
                               von, laenge))
        return -1;
    return static_cast<int>(laenge);
}

// ─── Uebertragung ────────────────────────────────────────────────────────────

extern "C" bool k1520d_extract(K1520Disk h, const char* name, const char* dest,
                               K1520DMode mode) {
    if (!h || !name || !dest) return false;
    TransferOptions o = optionen(mode, /*overwrite=*/true);
    return H(h)->vol->extract(FileRef::parse(name), dest, o);
}

extern "C" bool k1520d_insert(K1520Disk h, const char* src, const char* name,
                              K1520DMode mode, bool overwrite) {
    if (!h || !src || !name) return false;
    return H(h)->vol->insert(src, FileRef::parse(name), optionen(mode, overwrite));
}

extern "C" bool k1520d_erase(K1520Disk h, const char* name) {
    if (!h || !name) return false;
    return H(h)->vol->erase(FileRef::parse(name));
}

extern "C" bool k1520d_extract_all(K1520Disk h, const char* dest_dir, K1520DMode mode) {
    if (!h || !dest_dir) return false;
    return H(h)->vol->extractAll(dest_dir, optionen(mode, /*overwrite=*/true));
}

extern "C" bool k1520d_insert_all(K1520Disk h, const char* src_dir, K1520DMode mode,
                                  bool overwrite) {
    if (!h || !src_dir) return false;
    return H(h)->vol->insertAll(src_dir, optionen(mode, overwrite));
}

extern "C" const char* k1520d_check_fit(K1520Disk h, const char* src_dir) {
    if (!h || !src_dir) return "";
    std::string bericht;
    H(h)->vol->checkFit(src_dir, bericht);
    return halte(H(h)->s_fit, std::move(bericht));
}

// ─── Zustand ─────────────────────────────────────────────────────────────────

extern "C" bool k1520d_dirty(K1520Disk h) { return h && H(h)->vol->dirty(); }

extern "C" const char* k1520d_check(K1520Disk h) {
    if (!h) return "";
    std::string b;
    DiskVolume& v = *H(h)->vol;
    b += "Abbild:      " + v.path() + "\n";
    b += "Format:      " + v.detection().format + "\n";
    b += "Dateisystem: " + v.detection().filesystem
       + (v.detection().unambiguous ? "" : "  (nicht eindeutig)") + "\n";
    if (!v.detection().remarks.empty())
        b += "Medium:      " + v.detection().remarks + "\n";

    int defekt = 0;
    for (const FileEntry& e : v.list()) if (e.damaged) ++defekt;

    for (int i = 0; i < v.volumeCount(); ++i) {
        const FsInfo info = v.volumeInfo(i);
        const std::string wo = v.volumeDir(i);
        b += (wo.empty() ? std::string("Datentraeger") : wo);
        if (!info.label.empty()) b += " '" + info.label + "'";
        b += ": " + std::to_string(info.files) + " Dateien, "
           + std::to_string(info.used_bytes / 1024) + " KB belegt, "
           + std::to_string(info.free_bytes / 1024) + " KB frei\n";
        for (const std::string& w : info.warnings) b += "  ! " + w + "\n";
    }
    if (defekt) b += "  ! " + std::to_string(defekt) + " Dateien nicht lesbar\n";
    return halte(H(h)->s_check, std::move(b));
}

extern "C" const char* k1520d_version(void) { return "k1520disk 0.1"; }

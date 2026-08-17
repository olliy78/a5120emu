/**
 * @file udos1715_fs.cpp
 * @brief Umsetzung von @ref Udos1715FileSystem.
 *
 * @see doc/udos1715_diskettenformat.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/udos/udos1715_fs.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace {

constexpr size_t kSector = kUdos1715Sector;   // 256

/// @name Offsets im Zeigersektor (Handbuch §3.2.3)
/// @{
constexpr size_t kAdrctr = 0xFA;   ///< relative Adresse der LETZTEN Eintragung
constexpr size_t kBckzgr = 0xFC;
constexpr size_t kForzgr = 0xFE;
/// @}

/// @brief Offset des Zeigers auf den ersten Zeigersektor im Descriptor (`FIRSTBL`).
constexpr size_t kFirstbl = 0x80;

/// @brief Die 9 Datenrecords der DIRECTORY einer frisch formatierten Diskette —
///        woertlich aus Handbuch §1.2.1 (Interleave 5 innerhalb der ersten 16 Sektoren).
constexpr uint8_t kDirRecords[] = {0x05, 0x0A, 0x0F, 0x04, 0x09, 0x0E, 0x03, 0x08, 0x0D};
/// @brief Zeigersektor der DIRECTORY (§1.2.1).
constexpr uint8_t kDirPointerBlock = 0x02;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void     put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

/// @brief 6-Byte-ASCII-Feld (Datum „JJMMTT" ODER Versionstext) sauber herausholen.
std::string ascii6(const uint8_t* p) {
    std::string s;
    for (int i = 0; i < 6; ++i) {
        const uint8_t c = p[i];
        s += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string heute() {
    const std::time_t jetzt = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &jetzt);
#else
    localtime_r(&jetzt, &tm);
#endif
    char puffer[8];
    std::snprintf(puffer, sizeof(puffer), "%02d%02d%02d",
                  (tm.tm_year + 1900) % 100, tm.tm_mon + 1, tm.tm_mday);
    return puffer;
}

}  // namespace

// ─── Aufsetzen ───────────────────────────────────────────────────────────────

Udos1715FileSystem::Udos1715FileSystem(SectorSpace& space, const FsProfile& prof)
    : space_(space), prof_(prof) {}

std::unique_ptr<Udos1715FileSystem> Udos1715FileSystem::mount(SectorSpace& space,
                                                              const FsProfile& prof,
                                                              std::string& err) {
    if (prof.type != FsType::Udos1715) {
        err = "Profil '" + prof.name + "' ist kein UDOS1715-Dateisystem";
        return nullptr;
    }
    if (space.sectorSize(0, 0) != kSector) {
        err = "UDOS1715 erwartet 256-B-Sektoren, gemessen "
            + std::to_string(space.sectorSize(0, 0));
        return nullptr;
    }

    std::unique_ptr<Udos1715FileSystem> fs(new Udos1715FileSystem(space, prof));
    fs->phys_spt_ = space.sectorsPerTrack(0, 0);
    // Zwei Seiten ergeben EINE Spur (§1.1) — hat die Diskette keine Rueckseite, sind
    // es eben 16 Sektoren und die Karte sagt genau das.
    const bool zweiseitig = space.trackIndexOf(0, 1) >= 0;
    fs->secs_per_track_ = static_cast<uint8_t>(fs->phys_spt_ * (zweiseitig ? 2 : 1));
    fs->tracks_         = prof.usable_tracks ? prof.usable_tracks
                                             : space.format().numCylinders();

    if (!UdosBitmap::load(space, 0, prof.bitmap_track, fs->bitmap_, err,
                          UdosMapSitte::Ndos1715))
        return nullptr;

    std::string warum;
    if (!fs->bitmap_.looksValid(fs->secs_per_track_, 0, &warum)) {
        err = "kein gueltiger UDOS1715-Diskettenbelegungsplan auf Spur "
            + std::to_string(prof.bitmap_track) + ": " + warum;
        return nullptr;
    }
    // Der Belegungsplan weiss es besser als das Profil — er stammt vom Formatierer.
    if (fs->bitmap_.trackCount() > 0) fs->tracks_ = fs->bitmap_.trackCount();
    return fs;
}

std::unique_ptr<Udos1715FileSystem> Udos1715FileSystem::format(SectorSpace& space,
                                                               const FsProfile& prof,
                                                               const std::string& label,
                                                               std::string& err) {
    if (prof.type != FsType::Udos1715) {
        err = "Profil '" + prof.name + "' ist kein UDOS1715-Dateisystem";
        return nullptr;
    }
    if (space.sectorSize(0, 0) != kSector) {
        err = "UDOS1715 erwartet 256-B-Sektoren, gemessen "
            + std::to_string(space.sectorSize(0, 0));
        return nullptr;
    }

    std::unique_ptr<Udos1715FileSystem> fs(new Udos1715FileSystem(space, prof));
    fs->phys_spt_ = space.sectorsPerTrack(0, 0);
    const bool zweiseitig = space.trackIndexOf(0, 1) >= 0;
    fs->secs_per_track_ = static_cast<uint8_t>(fs->phys_spt_ * (zweiseitig ? 2 : 1));
    fs->tracks_         = prof.usable_tracks ? prof.usable_tracks
                                             : space.format().numCylinders();
    fs->label_          = label.empty() ? "K1520.DISK" : label.substr(0, 24);

    if (!fs->mkfs()) { err = fs->lastError(); return nullptr; }
    return fs;
}

bool Udos1715FileSystem::looksLikeUdos1715(const SectorSpace& space, const FsProfile& prof,
                                           std::string* why) {
    auto sag = [&](const std::string& m) { if (why) *why = m; return false; };

    if (space.sectorSize(0, 0) != kSector) return sag("keine 256-B-Sektoren");
    const uint8_t phys = space.sectorsPerTrack(0, 0);
    const uint8_t spt  = static_cast<uint8_t>(phys * (space.trackIndexOf(0, 1) >= 0 ? 2 : 1));

    UdosBitmap b;
    std::string err;
    if (!UdosBitmap::load(space, 0, prof.bitmap_track, b, err, UdosMapSitte::Ndos1715))
        return sag(err);
    if (!b.looksValid(spt, 0, why)) return false;

    // Zweite, unabhaengige Probe: der Descriptor der Verzeichnisdatei liegt fest auf
    // Spur 16H Sektor 00 und weist sich als Typ D mit Recordlaenge 100H aus, und sein
    // FIRSTBL zeigt in die Verzeichnisspur (§2/§5).
    SectorData sec;
    if (!space.readSector(prof.directory_track, 0, 1, sec) || sec.data.size() < kSector)
        return sag("Descriptor der Verzeichnisdatei nicht lesbar");
    if ((sec.data[0x0C] & 0x40) == 0)
        return sag("Spur " + std::to_string(prof.directory_track)
                   + " Sektor 0 ist kein Verzeichnis-Descriptor");
    if (le16(sec.data.data() + 0x0F) != kSector)
        return sag("Verzeichnis-Descriptor nennt Recordlaenge "
                   + std::to_string(le16(sec.data.data() + 0x0F)) + " statt 256");
    const UdosPointer firstbl = UdosPointer::fromBytes(sec.data.data() + kFirstbl);
    if (firstbl.track != prof.directory_track || firstbl.sector_index >= spt)
        return sag("FIRSTBL des Verzeichnisses zeigt nicht in die Verzeichnisspur");
    return true;
}

// ─── Sektoren ────────────────────────────────────────────────────────────────

bool Udos1715FileSystem::readSector(UdosPointer p, std::vector<uint8_t>& data) const {
    if (p.end()) return fail("Zeiger zeigt ins Leere (FF FF)");
    if (!inRange(p))
        return fail("Zeiger auf Spur " + std::to_string(p.track) + " Sektor "
                    + std::to_string(p.sector_index) + " liegt ausserhalb der Diskette ("
                    + std::to_string(tracks_) + " Spuren à "
                    + std::to_string(secs_per_track_) + " Sektoren)");

    SectorData sec;
    if (!space_.readSector(p.track, headOf(p), idOf(p), sec)) return fail(space_.lastError());
    if (sec.data.size() < kSector)
        return fail("Sektor " + std::to_string(idOf(p)) + " auf Spur "
                    + std::to_string(p.track) + " (Kopf " + std::to_string(headOf(p))
                    + ") ist kuerzer als 256 B");
    data.assign(sec.data.begin(), sec.data.begin() + kSector);
    return true;
}

bool Udos1715FileSystem::writeSector(UdosPointer p, const std::vector<uint8_t>& data) {
    if (data.size() != kSector) return fail("Sektordaten muessen 256 B lang sein");
    if (!inRange(p)) return fail("Schreibzeiger liegt ausserhalb der Diskette");
    if (!space_.writeSector(p.track, headOf(p), idOf(p), data)) return fail(space_.lastError());
    return true;
}

// ─── Descriptor und Zeigersektoren ───────────────────────────────────────────

bool Udos1715FileSystem::readDescriptor(UdosPointer p, UdosFileHeader& out) const {
    std::vector<uint8_t> d;
    if (!readSector(p, d)) return false;

    out.directory_sector = UdosPointer::fromBytes(d.data() + 0x06);
    out.first_record     = UdosPointer::fromBytes(d.data() + 0x08);
    out.last_record      = UdosPointer::fromBytes(d.data() + 0x0A);
    out.type_byte        = d[0x0C];
    out.record_count     = le16(d.data() + 0x0D);
    out.record_len       = le16(d.data() + 0x0F);
    out.block_len        = le16(d.data() + 0x11);
    out.properties       = d[0x13];
    out.entry_addr       = le16(d.data() + 0x14);
    out.bytes_in_last    = le16(d.data() + 0x16);
    out.created          = ascii6(d.data() + 0x18);
    out.modified         = ascii6(d.data() + 0x20);
    out.segment_start    = le16(d.data() + 0x28);
    out.segment_len      = le16(d.data() + 0x2A);
    out.extra            = static_cast<uint32_t>(le16(d.data() + 0x2C))
                         | (static_cast<uint32_t>(le16(d.data() + 0x2E)) << 16);
    out.low_addr         = le16(d.data() + 0x7A);
    out.high_addr        = le16(d.data() + 0x7C);
    out.stack_size       = le16(d.data() + 0x7E);
    out.firstbl          = UdosPointer::fromBytes(d.data() + kFirstbl);

    if (out.record_len == 0 || out.record_len % 128 != 0)
        return fail("Descriptor auf Spur " + std::to_string(p.track) + " Sektor "
                    + std::to_string(p.sector_index) + ": unmoegliche Recordlaenge "
                    + std::to_string(out.record_len));
    return true;
}

bool Udos1715FileSystem::readPointerBlock(UdosPointer p, Udos1715PointerBlock& out) const {
    std::vector<uint8_t> d;
    if (!readSector(p, d)) return false;

    const uint16_t adrctr = le16(d.data() + kAdrctr);
    // ADRCTR zeigt auf das ERSTE BYTE der letzten Eintragung — die Anzahl folgt daraus
    // (§6).  Ein ungerader oder zu grosser Wert ist ein beschaedigter Zeigersektor.
    if ((adrctr & 1) || adrctr > (kUdos1715PointersPerBlock - 1) * 2)
        return fail("Zeigersektor auf Spur " + std::to_string(p.track) + " Sektor "
                    + std::to_string(p.sector_index) + ": unmoegliches ADRCTR "
                    + std::to_string(adrctr));

    out.entries.clear();
    const size_t n = static_cast<size_t>(adrctr) / 2 + 1;
    for (size_t i = 0; i < n; ++i)
        out.entries.push_back(UdosPointer::fromBytes(d.data() + 2 * i));
    out.back    = UdosPointer::fromBytes(d.data() + kBckzgr);
    out.forward = UdosPointer::fromBytes(d.data() + kForzgr);
    return true;
}

bool Udos1715FileSystem::writePointerBlock(UdosPointer p, const Udos1715PointerBlock& b) {
    if (b.entries.empty() || b.entries.size() > kUdos1715PointersPerBlock)
        return fail("Zeigersektor haette " + std::to_string(b.entries.size())
                    + " Eintragungen (erlaubt 1…125)");
    std::vector<uint8_t> d(kSector, 0x00);
    for (size_t i = 0; i < b.entries.size(); ++i) {
        d[2 * i]     = b.entries[i].sector_index;
        d[2 * i + 1] = b.entries[i].track;
    }
    put16(d.data() + kAdrctr, static_cast<uint16_t>((b.entries.size() - 1) * 2));
    d[kBckzgr]     = b.back.sector_index;    d[kBckzgr + 1] = b.back.track;
    d[kForzgr]     = b.forward.sector_index; d[kForzgr + 1] = b.forward.track;
    return writeSector(p, d);
}

bool Udos1715FileSystem::pointerBlocks(const UdosFileHeader& hdr,
                                       std::vector<UdosPointer>& adressen,
                                       std::vector<Udos1715PointerBlock>& bloecke) const {
    adressen.clear();
    bloecke.clear();
    UdosPointer p = hdr.firstbl;
    // Obergrenze gegen Zeigerschleifen auf beschaedigten Disketten.
    const size_t grenze = static_cast<size_t>(tracks_) * secs_per_track_ + 8;

    while (!p.end()) {
        if (bloecke.size() > grenze)
            return fail("Zeigersektorkette laeuft im Kreis");
        Udos1715PointerBlock b;
        if (!readPointerBlock(p, b)) return false;
        adressen.insert(adressen.end(), b.entries.begin(), b.entries.end());
        p = b.forward;
        bloecke.push_back(std::move(b));
    }
    if (bloecke.empty()) return fail("Datei hat keinen Zeigersektor (FIRSTBL ist FFFF)");
    return true;
}

bool Udos1715FileSystem::recordChain(const UdosFileHeader& hdr,
                                     std::vector<UdosPointer>& out) const {
    std::vector<Udos1715PointerBlock> bloecke;
    if (!pointerBlocks(hdr, out, bloecke)) return false;
    // Die erste Eintragung des ERSTEN Zeigersektors ist der Descriptor selbst (§6).
    if (!out.empty()) out.erase(out.begin());
    return true;
}

bool Udos1715FileSystem::readChain(const UdosFileHeader& hdr,
                                   std::vector<uint8_t>& out) const {
    out.clear();
    std::vector<UdosPointer> kette;
    if (!recordChain(hdr, kette)) return false;

    const uint32_t sek_je_rec = sectorsPerRecord(hdr.record_len);
    // Recordlaenge 80H nutzt nur die erste Haelfte jedes Sektors (§5.3).
    const size_t je_sektor = std::min<size_t>(kSector, hdr.record_len);

    out.reserve(kette.size() * hdr.record_len);
    for (const UdosPointer& rec : kette) {
        for (uint32_t k = 0; k < sek_je_rec; ++k) {
            const UdosPointer p{static_cast<uint8_t>(rec.sector_index + k), rec.track};
            if (p.sector_index >= secs_per_track_)
                return fail("Record auf Spur " + std::to_string(rec.track)
                            + " ragt ueber das Spurende hinaus (Sektor "
                            + std::to_string(p.sector_index) + ")");
            std::vector<uint8_t> d;
            if (!readSector(p, d)) return false;
            out.insert(out.end(), d.begin(), d.begin() + static_cast<long>(je_sektor));
        }
    }

    // Wie bei ZDOS reicht das Speicherabbild einer PROGRAMMdatei ueber ihr logisches
    // Dateiende hinaus (doc/udos_diskettenformat.md §14.2b) — dort steht echter Code.
    // Nur bei Typ P: bei Typ A traegt dasselbe Feld Text.
    const uint64_t laenge   = hdr.length();
    const bool     programm = (hdr.type_byte & 0x80) != 0;
    const bool     reicht_weiter = programm && hdr.segment_len > laenge;
    if (!reicht_weiter && out.size() > laenge) out.resize(static_cast<size_t>(laenge));
    return true;
}

// ─── Verzeichnis ─────────────────────────────────────────────────────────────

std::vector<UdosDirEntry> Udos1715FileSystem::directory() const {
    std::vector<UdosDirEntry> result;

    UdosFileHeader hdr;
    if (!readDescriptor(directoryDescriptor(), hdr)) return result;
    std::vector<UdosPointer> kette;
    if (!recordChain(hdr, kette)) return result;

    const uint32_t sek_je_rec = sectorsPerRecord(hdr.record_len);
    const size_t   je_sektor  = std::min<size_t>(kSector, hdr.record_len);

    for (size_t r = 0; r < kette.size(); ++r) {
        // Die Auswertung beginnt in JEDEM Satz neu bei Offset 0 (§4).
        std::vector<uint8_t> satz;
        for (uint32_t k = 0; k < sek_je_rec; ++k) {
            const UdosPointer p{static_cast<uint8_t>(kette[r].sector_index + k), kette[r].track};
            std::vector<uint8_t> d;
            if (!readSector(p, d)) return result;
            satz.insert(satz.end(), d.begin(), d.begin() + static_cast<long>(je_sektor));
        }

        size_t i = 0;
        while (i < satz.size()) {
            const uint8_t flag = satz[i];
            if (flag == 0xFF) break;            // Ende der Liste, Rest ist Altbestand
            const uint8_t n = flag & 0x7F;      // §4: Bit 0-6 sind die Namenslaenge
            if (n == 0 || i + 1 + n + 2 > satz.size()) break;

            UdosDirEntry e;
            e.name.assign(reinterpret_cast<const char*>(satz.data() + i + 1), n);
            e.secret       = (flag & 0x80) != 0;
            e.header       = UdosPointer::fromBytes(satz.data() + i + 1 + n);
            e.record       = kette[r];
            e.record_index = static_cast<uint32_t>(r);
            e.offset       = static_cast<uint32_t>(i);
            result.push_back(e);
            i += 3 + n;
        }
    }
    return result;
}

std::vector<FileEntry> Udos1715FileSystem::listNames() const {
    std::vector<FileEntry> out;
    for (const UdosDirEntry& d : directory()) {
        FileEntry e;
        e.name   = d.name;
        e.hidden = d.secret;      // Spiegel der S-Eigenschaft, steht schon hier (§4)
        e.details_loaded = false;
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(),
              [](const FileEntry& a, const FileEntry& b) { return a.name < b.name; });
    return out;
}

std::vector<FileEntry> Udos1715FileSystem::list() const {
    std::vector<FileEntry> out = listNames();
    for (FileEntry& e : out) loadDetails(e);
    return out;
}

bool Udos1715FileSystem::detailsReady(const FileEntry& e) const {
    if (e.details_loaded) return true;
    for (const UdosDirEntry& d : directory())
        if (d.name == e.name)
            return space_.trackKnown(d.header.track, headOf(d.header));
    return true;
}

bool Udos1715FileSystem::loadDetails(FileEntry& e) const {
    if (e.details_loaded) return !e.damaged;
    for (const UdosDirEntry& d : directory()) {
        if (d.name != e.name) continue;
        UdosFileHeader hdr;
        if (!readDescriptor(d.header, hdr)) {
            e.damaged = e.details_loaded = true;
            return false;
        }
        e.size          = hdr.length();
        e.type          = hdr.typeName();
        e.attributes    = hdr.propertyLetters();
        e.entry_addr    = hdr.entry_addr;
        e.record_len    = hdr.record_len;
        e.block_len     = hdr.block_len;
        e.bytes_in_last = hdr.bytes_in_last;
        e.extra         = hdr.extra;
        e.created       = hdr.created;
        e.segment_start = hdr.segment_start;
        e.segment_len   = hdr.segment_len;
        e.low_addr      = hdr.low_addr;
        e.high_addr     = hdr.high_addr;
        e.stack_size    = hdr.stack_size;
        e.date          = hdr.modified.empty() ? hdr.created : hdr.modified;
        e.details_loaded = true;
        return true;
    }
    e.damaged = e.details_loaded = true;
    return false;
}

bool Udos1715FileSystem::read(const std::string& name, std::vector<uint8_t>& out) {
    for (const UdosDirEntry& d : directory()) {
        if (d.name != name) continue;
        UdosFileHeader hdr;
        if (!readDescriptor(d.header, hdr)) return false;
        return readChain(hdr, out);
    }
    return fail("Datei '" + name + "' steht nicht im Verzeichnis");
}

// ─── Zustand ─────────────────────────────────────────────────────────────────

FsInfo Udos1715FileSystem::info() const {
    FsInfo i;
    i.label       = bitmap_.label();
    i.total_bytes = static_cast<uint64_t>(tracks_) * secs_per_track_ * kSector;
    i.free_bytes  = static_cast<uint64_t>(bitmap_.countFree()) * kSector;
    i.used_bytes  = i.total_bytes - i.free_bytes;
    i.files       = static_cast<int>(directory().size());

    // Anders als bei ZDOS sind BEIDE Zaehler echt (§3.1) — eine Abweichung ist deshalb
    // meldenswert, nicht bloss zu erwarten.
    const int frei = bitmap_.countFree();
    if (bitmap_.storedFree() != frei)
        i.warnings.push_back("Freizaehler des Belegungsplans sagt "
                             + std::to_string(bitmap_.storedFree()) + ", ausgezaehlt sind "
                             + std::to_string(frei) + " Sektoren");
    if (bitmap_.storedUsed() != bitmap_.countUsed())
        i.warnings.push_back("Belegtzaehler des Belegungsplans sagt "
                             + std::to_string(bitmap_.storedUsed()) + ", ausgezaehlt sind "
                             + std::to_string(bitmap_.countUsed()) + " Sektoren");
    return i;
}

// ─── Belegung ────────────────────────────────────────────────────────────────

bool Udos1715FileSystem::reservedTrack(uint8_t track) const {
    if (track == prof_.directory_track || track == prof_.bitmap_track) return true;
    // Spur 0 traegt auf einer Systemdiskette Urlader und BFOS (§2).  Ob das so ist,
    // sagt der Belegungsplan — er ist beim Formatieren entsprechend gesetzt worden.
    return false;
}

bool Udos1715FileSystem::allocSectors(uint32_t n, std::vector<UdosPointer>& out) {
    out.clear();
    for (uint8_t t = 0; t < tracks_ && out.size() < n; ++t) {
        if (reservedTrack(t)) continue;
        for (uint8_t s = 0; s < secs_per_track_ && out.size() < n; ++s) {
            if (bitmap_.used(t, static_cast<uint8_t>(s + 1))) continue;
            out.push_back(UdosPointer{s, t});
            bitmap_.setUsed(t, static_cast<uint8_t>(s + 1), true);
        }
    }
    if (out.size() < n) {
        freeSectors(out);
        out.clear();
        return fail("Diskette voll: " + std::to_string(n) + " Sektoren noetig");
    }
    return true;
}

bool Udos1715FileSystem::allocRecords(uint32_t records, uint32_t sec_je_record,
                                      std::vector<UdosPointer>& out) {
    out.clear();
    if (sec_je_record == 0) sec_je_record = 1;
    if (sec_je_record > secs_per_track_)
        return fail("Recordlaenge passt nicht in eine Spur ("
                    + std::to_string(sec_je_record) + " Sektoren noetig, "
                    + std::to_string(secs_per_track_) + " je Spur)");

    uint32_t gefunden = 0;
    for (uint8_t t = 0; t < tracks_ && gefunden < records; ++t) {
        if (reservedTrack(t)) continue;
        // Ein Record ueberschreitet nie die Spurgrenze (§5.3) — die KOPFgrenze
        // innerhalb des Zylinders dagegen sehr wohl, denn die Spur ist der Zylinder.
        for (uint16_t s = 0; s + sec_je_record <= secs_per_track_ && gefunden < records; ) {
            bool frei = true;
            for (uint32_t k = 0; k < sec_je_record; ++k)
                if (bitmap_.used(t, static_cast<uint8_t>(s + k + 1))) {
                    frei = false;
                    s = static_cast<uint16_t>(s + k + 1);
                    break;
                }
            if (!frei) continue;
            for (uint32_t k = 0; k < sec_je_record; ++k) {
                const uint8_t idx = static_cast<uint8_t>(s + k);
                bitmap_.setUsed(t, static_cast<uint8_t>(idx + 1), true);
                out.push_back(UdosPointer{idx, t});
            }
            ++gefunden;
            s = static_cast<uint16_t>(s + sec_je_record);
        }
    }

    if (gefunden < records) {
        freeSectors(out);
        out.clear();
        return fail("Diskette voll: " + std::to_string(records) + " Records zu je "
                    + std::to_string(sec_je_record) + " Sektoren noetig");
    }
    return true;
}

void Udos1715FileSystem::freeSectors(const std::vector<UdosPointer>& v) {
    for (const UdosPointer& p : v)
        bitmap_.setUsed(p.track, static_cast<uint8_t>(p.sector_index + 1), false);
}

bool Udos1715FileSystem::saveBitmap() {
    bitmap_.refreshCounters();
    std::string err;
    if (!bitmap_.store(space_, 0, prof_.bitmap_track, err)) return fail(err);
    return true;
}

bool Udos1715FileSystem::sectorsOfFile(UdosPointer descriptor,
                                       std::vector<UdosPointer>& out) const {
    out.clear();
    UdosFileHeader hdr;
    if (!readDescriptor(descriptor, hdr)) return false;

    std::vector<UdosPointer>          adressen;
    std::vector<Udos1715PointerBlock> bloecke;
    if (!pointerBlocks(hdr, adressen, bloecke)) return false;

    out.push_back(descriptor);
    // Die Zeigersektoren selbst.
    UdosPointer p = hdr.firstbl;
    for (const Udos1715PointerBlock& b : bloecke) {
        out.push_back(p);
        p = b.forward;
    }
    // Die Datenrecords (adressen[0] ist der Descriptor).
    const uint32_t sek_je_rec = sectorsPerRecord(hdr.record_len);
    for (size_t i = 1; i < adressen.size(); ++i)
        for (uint32_t k = 0; k < sek_je_rec; ++k)
            out.push_back(UdosPointer{
                static_cast<uint8_t>(adressen[i].sector_index + k), adressen[i].track});
    return true;
}

// ─── Verzeichnis schreiben ───────────────────────────────────────────────────

bool Udos1715FileSystem::appendToPointerChain(const UdosFileHeader& hdr,
                                              UdosPointer neue_adresse) {
    std::vector<UdosPointer>          adressen;
    std::vector<Udos1715PointerBlock> bloecke;
    if (!pointerBlocks(hdr, adressen, bloecke)) return false;

    // Adresse des letzten Zeigersektors mitfuehren — sie steht nur in der Kette.
    UdosPointer letzte = hdr.firstbl;
    for (size_t i = 0; i + 1 < bloecke.size(); ++i) letzte = bloecke[i].forward;

    Udos1715PointerBlock& letzter = bloecke.back();
    if (letzter.entries.size() < kUdos1715PointersPerBlock) {
        letzter.entries.push_back(neue_adresse);
        return writePointerBlock(letzte, letzter);
    }

    // Voll — §3.3.3: „Ist kein naechster Zeigersektor vorhanden, wird dieser
    // bereitgestellt."
    std::vector<UdosPointer> neu;
    if (!allocSectors(1, neu)) return false;
    Udos1715PointerBlock frisch;
    frisch.entries.push_back(neue_adresse);
    frisch.back    = letzte;
    frisch.forward = UdosPointer{};              // FFFF
    if (!writePointerBlock(neu.front(), frisch)) return false;

    letzter.forward = neu.front();
    return writePointerBlock(letzte, letzter);
}

bool Udos1715FileSystem::growDirectory(UdosPointer& neuer_satz) {
    UdosFileHeader hdr;
    if (!readDescriptor(directoryDescriptor(), hdr)) return false;

    std::vector<UdosPointer> neu;
    if (!allocSectors(1, neu)) return false;
    neuer_satz = neu.front();

    // Frischer Satz: nur das Endebyte.
    std::vector<uint8_t> leer(kSector, 0x00);
    leer[0] = 0xFF;
    if (!writeSector(neuer_satz, leer)) return false;

    if (!appendToPointerChain(hdr, neuer_satz)) return false;

    // Descriptor nachfuehren: letzter Record, Recordanzahl, Bytes im letzten Record.
    std::vector<uint8_t> d;
    if (!readSector(directoryDescriptor(), d)) return false;
    d[0x0A] = neuer_satz.sector_index;
    d[0x0B] = neuer_satz.track;
    put16(d.data() + 0x0D, static_cast<uint16_t>(le16(d.data() + 0x0D) + 1));
    put16(d.data() + 0x16, hdr.record_len);
    return writeSector(directoryDescriptor(), d);
}

bool Udos1715FileSystem::appendDirEntry(const std::string& name, bool secret,
                                        UdosPointer descriptor, UdosPointer& dir_record) {
    UdosFileHeader hdr;
    if (!readDescriptor(directoryDescriptor(), hdr)) return false;
    std::vector<UdosPointer> kette;
    if (!recordChain(hdr, kette)) return false;
    if (kette.empty()) return fail("Verzeichnisdatei hat keinen einzigen Record");

    const size_t noetig = 3 + name.size();      // Flagbyte + Name + Zeiger
    UdosPointer  ziel   = kette.back();

    // §4: „Passt eine Dateieintragung nicht mehr in einen DIRECTORY-Record, so erfolgt
    // die Eintragung VOLLSTAENDIG in den naechsten Record."
    auto passtIn = [&](const std::vector<uint8_t>& satz, size_t& ende) {
        ende = 0;
        while (ende < satz.size() && satz[ende] != 0xFF) {
            const uint8_t n = satz[ende] & 0x7F;
            if (n == 0) return false;
            ende += 3 + n;
        }
        if (ende >= satz.size()) return false;              // kein Endebyte gefunden
        return ende + noetig + 1 <= satz.size();            // +1 fuer das neue 0xFF
    };

    std::vector<uint8_t> satz;
    if (!readSector(ziel, satz)) return false;

    size_t ende = 0;
    if (!passtIn(satz, ende)) {
        if (!growDirectory(ziel)) return false;
        if (!readSector(ziel, satz)) return false;
        if (!passtIn(satz, ende))
            return fail("Verzeichniseintrag passt auch in einen frischen Record nicht");
    }

    satz[ende] = static_cast<uint8_t>((secret ? 0x80 : 0x00) | (name.size() & 0x7F));
    for (size_t i = 0; i < name.size(); ++i)
        satz[ende + 1 + i] = static_cast<uint8_t>(name[i]);
    satz[ende + 1 + name.size()] = descriptor.sector_index;
    satz[ende + 2 + name.size()] = descriptor.track;
    satz[ende + noetig]          = 0xFF;

    if (!writeSector(ziel, satz)) return false;
    dir_record = ziel;
    return true;
}

bool Udos1715FileSystem::removeDirEntry(const UdosDirEntry& e) {
    std::vector<uint8_t> satz;
    if (!readSector(e.record, satz)) return false;

    const size_t laenge = 3 + e.name.size();
    if (e.offset + laenge > satz.size()) return fail("Verzeichniseintrag liegt schief");

    for (size_t i = e.offset; i + laenge < satz.size(); ++i) satz[i] = satz[i + laenge];
    for (size_t i = satz.size() - laenge; i < satz.size(); ++i) satz[i] = 0xFF;
    return writeSector(e.record, satz);
}

// ─── Schreiben ───────────────────────────────────────────────────────────────

bool Udos1715FileSystem::validName(const std::string& name, std::string* why) {
    auto sag = [&](const std::string& m) { if (why) *why = m; return false; };
    if (name.empty())     return sag("Name ist leer");
    if (name.size() > 32) return sag("Name ist laenger als 32 Zeichen");
    // Handbuch §3.1: „Der Name einer Datei … muss mit einem Buchstaben beginnen."
    const char c0 = name[0];
    if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z')))
        return sag("Name muss mit einem Buchstaben beginnen");
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x21 || u > 0x7E) return sag("Name enthaelt ein Sonderzeichen oder Leerzeichen");
    }
    return true;
}

bool Udos1715FileSystem::write(const std::string& name, const std::vector<uint8_t>& data,
                               const WriteOptions& opt) {
    std::string warum;
    if (!validName(name, &warum)) return fail("'" + name + "': " + warum);
    if (data.empty()) return fail("leere Dateien kann UDOS nicht ablegen");

    for (const UdosDirEntry& d : directory()) {
        if (d.name != name) continue;
        if (!opt.overwrite)
            return fail("'" + name + "' existiert bereits — Ueberschreiben nicht erlaubt");
        if (!erase(name)) return false;
        break;
    }

    // Recordlaenge: Vorgabe 100H (die implizite von NDOS, §5.3).  Zugelassen sind
    // 80H, 100H, 200H, 400H, 800H, 1000H.
    const uint16_t reclen = opt.udos_record_len ? opt.udos_record_len
                                                : static_cast<uint16_t>(kSector);
    switch (reclen) {
        case 0x0080: case 0x0100: case 0x0200:
        case 0x0400: case 0x0800: case 0x1000: break;
        default:
            return fail("Recordlaenge " + std::to_string(reclen) + " kennt NDOS nicht "
                        "(erlaubt 128, 256, 512, 1024, 2048, 4096)");
    }
    const uint32_t sek_je_rec = sectorsPerRecord(reclen);
    const uint32_t records    = static_cast<uint32_t>((data.size() + reclen - 1) / reclen);
    const uint16_t im_letzten = static_cast<uint16_t>(data.size() % reclen);

    // Bedarf: 1 Descriptor + Zeigersektoren + Datenrecords.  Im ERSTEN Zeigersektor
    // belegt der Descriptor die Stelle 0, es bleiben 124 Adressen (§6).
    const uint32_t zeiger_bloecke =
        static_cast<uint32_t>((records + 1 + kUdos1715PointersPerBlock - 1)
                              / kUdos1715PointersPerBlock);

    std::vector<UdosPointer> desc_sekt;
    if (!allocSectors(1, desc_sekt)) return false;
    const UdosPointer descriptor = desc_sekt.front();

    std::vector<UdosPointer> zeiger;
    if (!allocSectors(zeiger_bloecke, zeiger)) { freeSectors(desc_sekt); return false; }

    std::vector<UdosPointer> sektoren;      // ALLE Sektoren der Records, in Reihenfolge
    if (!allocRecords(records, sek_je_rec, sektoren)) {
        freeSectors(desc_sekt);
        freeSectors(zeiger);
        return false;
    }
    auto recordAnfang = [&](uint32_t i) { return sektoren[static_cast<size_t>(i) * sek_je_rec]; };

    // Verzeichniseintrag zuerst — er bestimmt den Rueckzeiger des Descriptors.  Das
    // Flagbyte spiegelt SECRET (§4), sonst widersprechen sich Verzeichnis und Descriptor.
    const bool geheim = (udosPropertyByte(opt.udos_properties) & 0x10) != 0;
    UdosPointer dir_record;
    if (!appendDirEntry(name, geheim, descriptor, dir_record)) return false;

    // Datenrecords.
    const size_t je_sektor = std::min<size_t>(kSector, reclen);
    for (uint32_t i = 0; i < records; ++i) {
        const size_t ab    = static_cast<size_t>(i) * reclen;
        const size_t menge = std::min<size_t>(reclen, data.size() - ab);
        std::vector<uint8_t> block(reclen, opt.text ? 0x1A : 0x00);
        std::copy(data.begin() + static_cast<long>(ab),
                  data.begin() + static_cast<long>(ab + menge), block.begin());
        for (uint32_t k = 0; k < sek_je_rec; ++k) {
            std::vector<uint8_t> teil(kSector, 0x00);
            std::copy(block.begin() + static_cast<long>(k * je_sektor),
                      block.begin() + static_cast<long>((k + 1) * je_sektor), teil.begin());
            if (!writeSector(sektoren[static_cast<size_t>(i) * sek_je_rec + k], teil))
                return false;
        }
    }

    // Zeigersektorkette: Stelle 0 ist der Descriptor, dann die Recordadressen.
    {
        std::vector<UdosPointer> alle;
        alle.push_back(descriptor);
        for (uint32_t i = 0; i < records; ++i) alle.push_back(recordAnfang(i));

        size_t gesetzt = 0;
        for (size_t b = 0; b < zeiger.size(); ++b) {
            Udos1715PointerBlock blk;
            const size_t menge = std::min(kUdos1715PointersPerBlock, alle.size() - gesetzt);
            blk.entries.assign(alle.begin() + static_cast<long>(gesetzt),
                               alle.begin() + static_cast<long>(gesetzt + menge));
            gesetzt += menge;
            // Der Rueckwaertszeiger des ERSTEN Zeigersektors zeigt auf den Descriptor (§6).
            blk.back    = (b == 0) ? descriptor : zeiger[b - 1];
            blk.forward = (b + 1 < zeiger.size()) ? zeiger[b + 1] : UdosPointer{};
            if (!writePointerBlock(zeiger[b], blk)) return false;
        }
    }

    // Descriptor nach §5.
    std::vector<uint8_t> h(kSector, 0x00);
    h[0x06] = dir_record.sector_index;      h[0x07] = dir_record.track;
    h[0x08] = recordAnfang(0).sector_index; h[0x09] = recordAnfang(0).track;
    h[0x0A] = recordAnfang(records - 1).sector_index;
    h[0x0B] = recordAnfang(records - 1).track;
    h[0x0C] = udosTypeByte(opt.udos_type, opt.text);
    put16(h.data() + 0x0D, static_cast<uint16_t>(records));
    put16(h.data() + 0x0F, reclen);
    // Blocklaenge (11H): bei 128 und 1024 die Kopie der Recordlaenge, sonst 0 — dieselbe
    // Sitte wie bei ZDOS, am Referenzdatentraeger bestaetigt.
    const uint16_t blocklen = opt.udos_block_len_gesetzt
                            ? opt.udos_block_len
                            : ((reclen == 128 || reclen == 1024) ? reclen : 0);
    put16(h.data() + 0x11, blocklen);
    h[0x13] = udosPropertyByte(opt.udos_properties);
    put16(h.data() + 0x14, opt.udos_entry);
    const uint16_t rest = opt.udos_bytes_in_last ? opt.udos_bytes_in_last
                        : (im_letzten ? im_letzten : reclen);
    put16(h.data() + 0x16, rest);

    const std::string datum = (opt.date.size() == 6) ? opt.date : heute();
    std::string erstellt = opt.udos_created.empty() ? datum : opt.udos_created;
    erstellt.resize(6, ' ');
    for (int i = 0; i < 6; ++i) {
        h[0x18 + i] = static_cast<uint8_t>(erstellt[i]);
        h[0x20 + i] = static_cast<uint8_t>(datum[i]);
    }
    h[0x1E] = 0xFF; h[0x1F] = 0x00;
    h[0x26] = 0xFF; h[0x27] = 0x00;
    put16(h.data() + 0x28, opt.udos_segment);
    put16(h.data() + 0x2A, opt.udos_segment_len);
    put16(h.data() + 0x2C, static_cast<uint16_t>(opt.udos_extra & 0xFFFF));
    put16(h.data() + 0x2E, static_cast<uint16_t>(opt.udos_extra >> 16));
    put16(h.data() + 0x7A, opt.udos_low_addr);
    put16(h.data() + 0x7C, opt.udos_high_addr);
    put16(h.data() + 0x7E, opt.udos_stack_size);
    h[kFirstbl]     = zeiger.front().sector_index;
    h[kFirstbl + 1] = zeiger.front().track;

    if (!writeSector(descriptor, h)) return false;
    return saveBitmap();
}

bool Udos1715FileSystem::erase(const std::string& name) {
    const std::vector<UdosDirEntry> verz = directory();
    const UdosDirEntry* treffer = nullptr;
    for (const UdosDirEntry& d : verz) if (d.name == name) treffer = &d;
    if (!treffer) return fail("Datei '" + name + "' steht nicht im Verzeichnis");
    if (name == "DIRECTORY")
        return fail("Die Verzeichnisdatei selbst darf nicht geloescht werden");

    // Erst einsammeln, dann eintragen — bei einem Lesefehler bleibt die Karte heil.
    std::vector<UdosPointer> frei;
    if (!sectorsOfFile(treffer->header, frei)) return false;
    if (!removeDirEntry(*treffer)) return false;

    // §7.4: die Sektorinhalte bleiben stehen, massgeblich ist allein der Belegungsplan.
    freeSectors(frei);
    return saveBitmap();
}

bool Udos1715FileSystem::setAttributes(const std::string& name, const UdosAttrs& a) {
    const std::vector<UdosDirEntry> verz = directory();
    const UdosDirEntry* treffer = nullptr;
    for (const UdosDirEntry& d : verz) if (d.name == name) treffer = &d;
    if (!treffer) return fail("Datei '" + name + "' steht nicht im Verzeichnis");

    std::vector<uint8_t> h;
    if (!readSector(treffer->header, h)) return false;

    if (!a.type.empty())       h[0x0C] = udosTypeByte(a.type, false);
    if (!a.properties.empty()) h[0x13] = udosPropertyByte(a.properties == ";" ? ""
                                                                              : a.properties);
    if (a.set_entry)     put16(h.data() + 0x14, a.entry);
    if (a.set_block_len) put16(h.data() + 0x11, a.block_len);
    if (a.set_segment) {
        put16(h.data() + 0x28, a.segment);
        put16(h.data() + 0x2A, a.segment_len);
    }
    if (a.set_memory) {
        put16(h.data() + 0x7A, a.low);
        put16(h.data() + 0x7C, a.high);
        put16(h.data() + 0x7E, a.stack);
    }
    if (a.set_extra) {
        put16(h.data() + 0x2C, static_cast<uint16_t>(a.extra & 0xFFFF));
        put16(h.data() + 0x2E, static_cast<uint16_t>(a.extra >> 16));
    }
    for (int i = 0; i < 6; ++i) {
        if (a.created.size()  == 6) h[0x18 + i] = static_cast<uint8_t>(a.created[i]);
        if (a.modified.size() == 6) h[0x20 + i] = static_cast<uint8_t>(a.modified[i]);
    }
    if (!writeSector(treffer->header, h)) return false;

    // Das Flagbyte im Verzeichnis spiegelt SECRET (§4).
    if (!a.properties.empty()) {
        const bool geheim = (h[0x13] & 0x10) != 0;
        std::vector<uint8_t> satz;
        if (!readSector(treffer->record, satz)) return false;
        const size_t off = treffer->offset;
        if (off < satz.size()) {
            satz[off] = static_cast<uint8_t>((satz[off] & 0x7F) | (geheim ? 0x80 : 0x00));
            if (!writeSector(treffer->record, satz)) return false;
        }
    }
    return true;
}

// ─── Anlegen ─────────────────────────────────────────────────────────────────

bool Udos1715FileSystem::mkfs() {
    bitmap_ = UdosBitmap::makeEmpty(secs_per_track_, tracks_, label_, UdosMapSitte::Ndos1715);

    // Die 13 festen Sektoren aus §2 — und auf einer Systemdiskette Spur 0 dazu.
    bitmap_.setUsed(prof_.directory_track, 0 + 1, true);                  // Descriptor
    bitmap_.setUsed(prof_.directory_track, kDirPointerBlock + 1, true);   // Zeigersektor
    for (uint8_t s : kDirRecords) bitmap_.setUsed(prof_.directory_track, s + 1, true);
    bitmap_.setUsed(prof_.bitmap_track, 0 + 1, true);
    bitmap_.setUsed(prof_.bitmap_track, 1 + 1, true);
    if (prof_.system_track0)
        for (uint8_t s = 0; s < secs_per_track_; ++s) bitmap_.setUsed(0, s + 1, true);

    const UdosPointer descriptor = directoryDescriptor();
    const UdosPointer zeiger{kDirPointerBlock, prof_.directory_track};

    // Alle 9 Records leer anlegen; der erste traegt den Eintrag der Verzeichnisdatei
    // selbst — geheim, Typ D, genau wie auf dem Referenzdatentraeger.
    for (size_t i = 0; i < sizeof(kDirRecords); ++i) {
        std::vector<uint8_t> satz(kSector, 0x00);
        if (i == 0) {
            const std::string name = "DIRECTORY";
            satz[0] = static_cast<uint8_t>(0x80 | name.size());
            for (size_t k = 0; k < name.size(); ++k)
                satz[1 + k] = static_cast<uint8_t>(name[k]);
            satz[1 + name.size()] = descriptor.sector_index;
            satz[2 + name.size()] = descriptor.track;
            satz[3 + name.size()] = 0xFF;
        } else {
            satz[0] = 0xFF;
        }
        if (!writeSector(UdosPointer{kDirRecords[i], prof_.directory_track}, satz))
            return false;
    }

    // Zeigersektor der Verzeichnisdatei: Descriptor + die 9 Records.
    {
        Udos1715PointerBlock blk;
        blk.entries.push_back(descriptor);
        for (uint8_t s : kDirRecords)
            blk.entries.push_back(UdosPointer{s, prof_.directory_track});
        blk.back    = descriptor;
        blk.forward = UdosPointer{};
        if (!writePointerBlock(zeiger, blk)) return false;
    }

    // Descriptor der Verzeichnisdatei.
    std::vector<uint8_t> h(kSector, 0x00);
    const UdosPointer erster{kDirRecords[0], prof_.directory_track};
    const UdosPointer letzter{kDirRecords[sizeof(kDirRecords) - 1], prof_.directory_track};
    h[0x06] = erster.sector_index;  h[0x07] = erster.track;   // Eintrag im 1. Record
    h[0x08] = erster.sector_index;  h[0x09] = erster.track;
    h[0x0A] = letzter.sector_index; h[0x0B] = letzter.track;
    h[0x0C] = 0x40;                                           // Typ D
    put16(h.data() + 0x0D, static_cast<uint16_t>(sizeof(kDirRecords)));
    put16(h.data() + 0x0F, static_cast<uint16_t>(kSector));
    put16(h.data() + 0x11, 0);                                // wie auf der Referenz
    h[0x13] = 0xF0;                                           // WELS
    put16(h.data() + 0x16, static_cast<uint16_t>(kSector));
    const std::string datum = heute();
    for (int i = 0; i < 6; ++i) {
        h[0x18 + i] = static_cast<uint8_t>(datum[i]);
        h[0x20 + i] = static_cast<uint8_t>(datum[i]);
    }
    h[0x1E] = 0xFF; h[0x1F] = 0x00;
    h[0x26] = 0xFF; h[0x27] = 0x00;
    h[kFirstbl] = zeiger.sector_index; h[kFirstbl + 1] = zeiger.track;
    if (!writeSector(descriptor, h)) return false;

    return saveBitmap();
}

// ─── Platzpruefung ───────────────────────────────────────────────────────────

bool Udos1715FileSystem::wouldFit(const std::vector<PlannedFile>& files,
                                  FitReport& out) const {
    out = FitReport{};

    int frei = 0;
    for (uint8_t t = 0; t < tracks_; ++t) {
        if (reservedTrack(t)) continue;
        for (uint8_t s = 0; s < secs_per_track_; ++s)
            if (!bitmap_.used(t, static_cast<uint8_t>(s + 1))) ++frei;
    }

    // Ersetzte gleichnamige Dateien geben ihre Sektoren zurueck.
    const std::vector<UdosDirEntry> verz = directory();
    for (const PlannedFile& f : files) {
        for (const UdosDirEntry& d : verz) {
            if (d.name != f.name) continue;
            std::vector<UdosPointer> belegt;
            if (sectorsOfFile(d.header, belegt)) frei += static_cast<int>(belegt.size());
        }
    }

    uint32_t noetig = 0;
    for (const PlannedFile& f : files) {
        const uint32_t records = static_cast<uint32_t>((f.size + kSector - 1) / kSector);
        const uint32_t bloecke =
            static_cast<uint32_t>((records + 1 + kUdos1715PointersPerBlock - 1)
                                  / kUdos1715PointersPerBlock);
        noetig += 1 + bloecke + records;
    }

    out.needed    = static_cast<uint64_t>(noetig) * kSector;
    out.available = static_cast<uint64_t>(frei)   * kSector;
    out.fits      = static_cast<int>(noetig) <= frei;
    if (!out.fits)
        out.detail = "Es fehlen " + std::to_string(static_cast<int>(noetig) - frei)
                   + " Sektoren zu je 256 B";
    return true;
}

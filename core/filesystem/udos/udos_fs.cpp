/**
 * @file udos_fs.cpp
 * @brief Umsetzung von @ref UdosFileSystem — Lesepfad.
 *
 * @see doc/udos_diskettenformat.md §5–§8
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/udos/udos_fs.h"

#include <algorithm>

namespace {

constexpr size_t kSector = 128;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

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

}  // namespace

// ─── UdosFileHeader ──────────────────────────────────────────────────────────

uint64_t UdosFileHeader::length() const {
    if (record_count == 0) return 0;
    const uint64_t voll = static_cast<uint64_t>(record_count) * record_len;
    // §7.1: nur wenn „Bytes im letzten Satz" echt kleiner als die Satzlaenge und
    // ungleich 0 ist, wird gekuerzt.  Bei 0000 fuehrt UDOS die Datei mit vollen
    // Saetzen (nachgewiesen an NOTE.TO.SD, 16 Saetze, 0000).
    if (bytes_in_last > 0 && bytes_in_last < record_len)
        return voll - (record_len - bytes_in_last);
    return voll;
}

std::string UdosFileHeader::typeName() const {
    switch (type_byte) {
        case 0x20: return "A";
        case 0x80: return "P";
        case 0x81: return "P1";
        case 0x10: return "B";
        case 0x40: return "D";
        default:   return "";
    }
}

std::string UdosFileHeader::propertyLetters() const {
    std::string s;
    if (properties & 0x80) s += 'W';
    if (properties & 0x40) s += 'E';
    if (properties & 0x20) s += 'L';
    if (properties & 0x10) s += 'S';
    if (properties & 0x08) s += 'R';
    if (properties & 0x04) s += 'F';
    return s;
}

// ─── Aufsetzen ───────────────────────────────────────────────────────────────

UdosFileSystem::UdosFileSystem(SectorSpace& space, const FsProfile& prof, uint8_t head)
    : space_(space), prof_(prof), head_(head) {}

std::unique_ptr<UdosFileSystem> UdosFileSystem::mount(SectorSpace& space,
                                                      const FsProfile& prof,
                                                      uint8_t head, std::string& err) {
    if (prof.type != FsType::Udos) {
        err = "Profil '" + prof.name + "' ist kein UDOS-Dateisystem";
        return nullptr;
    }

    std::unique_ptr<UdosFileSystem> fs(new UdosFileSystem(space, prof, head));

    const uint16_t groesse = space.sectorSize(0, head);
    if (groesse != kSector) {
        err = "UDOS erwartet 128-B-Sektoren, gemessen " + std::to_string(groesse);
        return nullptr;
    }
    fs->secs_per_track_ = space.sectorsPerTrack(0, head);
    fs->tracks_         = prof.usable_tracks;

    if (!UdosBitmap::load(space, head, prof.bitmap_track, fs->bitmap_, err)) return nullptr;

    std::string warum;
    if (!fs->bitmap_.looksValid(fs->secs_per_track_, 0, &warum)) {
        err = "keine gueltige UDOS-Belegungskarte auf Spur "
            + std::to_string(prof.bitmap_track) + ": " + warum;
        return nullptr;
    }
    // Die Karte weiss es besser als das Profil — sie stammt vom Formatierer.
    if (fs->bitmap_.trackCount() > 0) fs->tracks_ = fs->bitmap_.trackCount();
    return fs;
}

bool UdosFileSystem::looksLikeUdos(const SectorSpace& space, const FsProfile& prof,
                                   uint8_t head, std::string* why) {
    if (space.sectorSize(0, head) != kSector) {
        if (why) *why = "keine 128-B-Sektoren";
        return false;
    }
    UdosBitmap b;
    std::string err;
    if (!UdosBitmap::load(space, head, prof.bitmap_track, b, err)) {
        if (why) *why = err;
        return false;
    }
    if (!b.looksValid(space.sectorsPerTrack(0, head), 0, why)) return false;

    // Zweite, unabhaengige Probe: der Kopfsektor der Verzeichnisdatei muss dort
    // liegen, wo UDOS ihn immer hat, und sich selbst als Typ D ausweisen.
    SectorData sec;
    if (!space.readSector(prof.directory_track, head, 1, sec) || sec.data.size() < kSector) {
        if (why) *why = "Kopfsektor der Verzeichnisdatei nicht lesbar";
        return false;
    }
    if (sec.data[12] != 0x40) {
        if (why) *why = "Spur " + std::to_string(prof.directory_track)
                      + " Sektor 1 ist kein Verzeichnis-Kopfsektor";
        return false;
    }
    return true;
}

// ─── Sektor + Kontrollblock ──────────────────────────────────────────────────

bool UdosFileSystem::readSector(UdosPointer p, std::vector<uint8_t>& data,
                                UdosPointer& back, UdosPointer& fwd) const {
    if (p.end()) return fail("Zeiger zeigt ins Leere (FF FF)");
    if (p.track >= tracks_)
        return fail("Zeiger auf Spur " + std::to_string(p.track) + " — die Diskette hat nur "
                    + std::to_string(tracks_));

    SectorData sec;
    if (!space_.readSector(p.track, head_, p.sectorId(), sec)) return fail(space_.lastError());
    if (sec.data.size() < kSector)
        return fail("Sektor " + std::to_string(p.sectorId()) + " auf Spur "
                    + std::to_string(p.track) + " ist kuerzer als 128 B");
    if (sec.tail.size() < 4)
        return fail("Sektor " + std::to_string(p.sectorId()) + " auf Spur "
                    + std::to_string(p.track) + " hat keinen Kontrollblock — das Abbild "
                    "ist kein spurbasiertes Format (.img kann UDOS nicht tragen)");

    data.assign(sec.data.begin(), sec.data.begin() + kSector);
    back = UdosPointer::fromBytes(sec.tail.data());
    fwd  = UdosPointer::fromBytes(sec.tail.data() + 2);
    return true;
}

bool UdosFileSystem::readHeader(UdosPointer p, UdosFileHeader& out) const {
    std::vector<uint8_t> d;
    UdosPointer back, fwd;
    if (!readSector(p, d, back, fwd)) return false;

    out.directory_sector = UdosPointer::fromBytes(d.data() + 6);
    out.first_record     = UdosPointer::fromBytes(d.data() + 8);
    out.last_record      = UdosPointer::fromBytes(d.data() + 10);
    out.type_byte        = d[12];
    out.record_count     = le16(d.data() + 13);
    out.record_len       = le16(d.data() + 15);
    out.properties       = d[19];
    out.entry_addr       = le16(d.data() + 20);
    out.bytes_in_last    = le16(d.data() + 22);
    out.created          = ascii6(d.data() + 24);
    out.modified         = ascii6(d.data() + 32);

    // Der Kopfsektor traegt seine Ketten-Enden auch im Kontrollblock; der ist die
    // verlaesslichere Quelle, weil UDOS ihn beim Anhaengen mitfuehrt.
    out.directory_sector = back;
    out.first_record     = fwd;

    if (out.record_len == 0 || out.record_len % kSector != 0)
        return fail("Kopfsektor auf Spur " + std::to_string(p.track) + " Sektor "
                    + std::to_string(p.sectorId()) + ": unmoegliche Satzlaenge "
                    + std::to_string(out.record_len));
    return true;
}

bool UdosFileSystem::recordChain(const UdosFileHeader& hdr,
                                 std::vector<UdosPointer>& out) const {
    out.clear();
    UdosPointer p = hdr.first_record;
    // Obergrenze gegen Zeigerschleifen auf beschaedigten Disketten.
    const size_t grenze = static_cast<size_t>(tracks_) * secs_per_track_ + 8;

    while (!p.end()) {
        if (out.size() > grenze)
            return fail("Satzkette laeuft im Kreis (mehr Glieder als Sektoren)");
        out.push_back(p);

        std::vector<uint8_t> d;
        UdosPointer back, fwd;
        if (!readSector(p, d, back, fwd)) return false;
        p = fwd;
    }
    return true;
}

bool UdosFileSystem::readChain(const UdosFileHeader& hdr, std::vector<uint8_t>& out) const {
    out.clear();
    const uint32_t sek_je_satz = hdr.record_len / kSector;

    std::vector<UdosPointer> kette;
    if (!recordChain(hdr, kette)) return false;

    out.reserve(kette.size() * hdr.record_len);
    for (const UdosPointer& satz : kette) {
        // Ein Satz belegt sek_je_satz PHYSISCH AUFEINANDERFOLGENDE Sektoren derselben
        // Spur (§7) — deshalb reicht der Zeiger auf den ersten.
        for (uint32_t k = 0; k < sek_je_satz; ++k) {
            UdosPointer p{static_cast<uint8_t>(satz.sector_index + k), satz.track};
            if (p.sectorId() > secs_per_track_)
                return fail("Satz auf Spur " + std::to_string(satz.track)
                            + " ragt ueber das Spurende hinaus (Sektor "
                            + std::to_string(p.sectorId()) + ")");
            std::vector<uint8_t> d;
            UdosPointer back, fwd;
            if (!readSector(p, d, back, fwd)) return false;
            out.insert(out.end(), d.begin(), d.end());
        }
    }

    const uint64_t laenge = hdr.length();
    if (out.size() > laenge) out.resize(static_cast<size_t>(laenge));
    return true;
}

// ─── Verzeichnis ─────────────────────────────────────────────────────────────

std::vector<UdosDirEntry> UdosFileSystem::directory() const {
    std::vector<UdosDirEntry> result;

    UdosFileHeader hdr;
    if (!readHeader(directoryHeader(), hdr)) return result;

    std::vector<UdosPointer> kette;
    if (!recordChain(hdr, kette)) return result;

    const uint32_t sek_je_satz = std::max<uint32_t>(1, hdr.record_len / kSector);

    for (size_t r = 0; r < kette.size(); ++r) {
        // Satz einlesen (die Auswertung beginnt in JEDEM Satz neu bei Offset 0, §5).
        std::vector<uint8_t> satz;
        for (uint32_t k = 0; k < sek_je_satz; ++k) {
            UdosPointer p{static_cast<uint8_t>(kette[r].sector_index + k), kette[r].track};
            std::vector<uint8_t> d;
            UdosPointer back, fwd;
            if (!readSector(p, d, back, fwd)) return result;
            satz.insert(satz.end(), d.begin(), d.end());
        }

        size_t i = 0;
        while (i < satz.size()) {
            const uint8_t flag = satz[i];
            if (flag == 0xFF) break;            // Ende der Liste, Rest ist Altbestand
            const uint8_t n = flag & 0x3F;
            if (n == 0 || i + 1 + n + 2 > satz.size()) break;

            UdosDirEntry e;
            e.name.assign(reinterpret_cast<const char*>(satz.data() + i + 1), n);
            e.secret       = (flag & 0x80) != 0;
            e.header       = UdosPointer::fromBytes(satz.data() + i + 1 + n);
            e.record_index = static_cast<uint32_t>(r);
            e.offset       = static_cast<uint32_t>(i);
            result.push_back(e);

            i += 3 + n;
        }
    }
    return result;
}

std::vector<FileEntry> UdosFileSystem::list() const {
    std::vector<FileEntry> out;
    for (const UdosDirEntry& d : directory()) {
        FileEntry e;
        e.name   = d.name;
        e.hidden = d.secret;

        UdosFileHeader hdr;
        if (readHeader(d.header, hdr)) {
            e.size       = hdr.length();
            e.type       = hdr.typeName();
            e.attributes = hdr.propertyLetters();
            e.date       = hdr.modified.empty() ? hdr.created : hdr.modified;
        } else {
            e.damaged = true;
        }
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(),
              [](const FileEntry& a, const FileEntry& b) { return a.name < b.name; });
    return out;
}

bool UdosFileSystem::read(const std::string& name, std::vector<uint8_t>& out) {
    for (const UdosDirEntry& d : directory()) {
        if (d.name != name) continue;
        UdosFileHeader hdr;
        if (!readHeader(d.header, hdr)) return false;
        return readChain(hdr, out);
    }
    return fail("Datei '" + name + "' steht nicht im Verzeichnis");
}

// ─── Zustand ─────────────────────────────────────────────────────────────────

FsInfo UdosFileSystem::info() const {
    FsInfo i;
    i.label       = bitmap_.label();
    i.total_bytes = static_cast<uint64_t>(tracks_) * secs_per_track_ * kSector;
    i.free_bytes  = static_cast<uint64_t>(bitmap_.countFree()) * kSector;
    i.used_bytes  = i.total_bytes - i.free_bytes;
    i.files       = static_cast<int>(directory().size());

    // §4.2: der gespeicherte Freizaehler ist eine Gegenprobe, nicht die Wahrheit.
    const int gezaehlt = bitmap_.countFree();
    if (bitmap_.storedFree() != gezaehlt)
        i.warnings.push_back("Freizaehler der Belegungskarte sagt "
                             + std::to_string(bitmap_.storedFree()) + ", ausgezaehlt sind "
                             + std::to_string(gezaehlt) + " Sektoren");
    return i;
}

// ─── Schreiben (Etappe 6) ────────────────────────────────────────────────────

bool UdosFileSystem::write(const std::string&, const std::vector<uint8_t>&,
                           const WriteOptions&) {
    return fail("Schreiben auf UDOS-Disketten ist noch nicht umgesetzt");
}

bool UdosFileSystem::erase(const std::string&) {
    return fail("Loeschen auf UDOS-Disketten ist noch nicht umgesetzt");
}

bool UdosFileSystem::mkfs() {
    return fail("Anlegen eines UDOS-Dateisystems ist noch nicht umgesetzt");
}

bool UdosFileSystem::wouldFit(const std::vector<PlannedFile>& files, FitReport& out) const {
    out = FitReport{};
    // Spurweise rechnen, nicht mit der Summe: ein Satz muss in EINE Spur passen (§7),
    // deshalb kann verstreuter freier Platz unbrauchbar sein.  Solange nur mit
    // Satzlaenge 128 geschrieben wird, ist jeder freie Sektor brauchbar — die
    // Feinrechnung kommt mit dem Schreibpfad in Etappe 6.
    const int frei = bitmap_.countFree();
    out.available  = static_cast<uint64_t>(frei) * kSector;

    uint64_t noetig = 0;
    for (const PlannedFile& f : files)
        noetig += kSector                                        // Kopfsektor
                + ((f.size + kSector - 1) / kSector) * kSector;  // Saetze à 128 B
    out.needed = noetig;
    out.fits   = noetig <= out.available;
    if (!out.fits)
        out.detail = std::to_string(files.size()) + " Dateien brauchen "
                   + std::to_string(noetig / kSector) + " Sektoren, frei sind "
                   + std::to_string(frei);
    return true;
}

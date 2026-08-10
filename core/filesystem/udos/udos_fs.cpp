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
#include <cstdio>
#include <ctime>

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

std::unique_ptr<UdosFileSystem> UdosFileSystem::format(SectorSpace& space,
                                                       const FsProfile& prof, uint8_t head,
                                                       const std::string& label,
                                                       std::string& err) {
    if (prof.type != FsType::Udos) {
        err = "Profil '" + prof.name + "' ist kein UDOS-Dateisystem";
        return nullptr;
    }
    if (space.sectorSize(0, head) != kSector) {
        err = "UDOS erwartet 128-B-Sektoren, gemessen "
            + std::to_string(space.sectorSize(0, head));
        return nullptr;
    }

    std::unique_ptr<UdosFileSystem> fs(new UdosFileSystem(space, prof, head));
    fs->secs_per_track_ = space.sectorsPerTrack(0, head);
    fs->tracks_         = prof.usable_tracks ? prof.usable_tracks
                                             : space.format().numCylinders();
    fs->label_          = label.empty() ? "K1520.DISK" : label.substr(0, 24);

    if (!fs->mkfs()) { err = fs->lastError(); return nullptr; }
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
            e.record       = kette[r];
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

// ─── Schreibpfad ─────────────────────────────────────────────────────────────
//
// Umsetzung von doc/udos_diskettenformat.md §8.4/§8.5.  Zwei Festlegungen, die dort
// offengelassen sind und hier bewusst getroffen werden:
//
//   * **Satzlaenge 128** (ein Sektor je Satz).  §8.4 nennt sie „immer sicher"; groessere
//     Saetze braeuchten `satzlen/128` ZUSAMMENHAENGENDE freie Sektoren in EINER Spur
//     (§7) und bringen einem Werkzeug keinen Vorteil.  Gelesen werden groessere
//     Satzlaengen selbstverstaendlich weiterhin.
//   * **Dichte Belegung** statt des Interleave 5, den der Referenzdatentraeger zeigt
//     (§7).  Der Interleave ist eine Geschwindigkeitseigenschaft echter Hardware; die
//     Verkettung steht ohnehin explizit in den Zeigern, das Ergebnis ist also fuer UDOS
//     gleichwertig.
//
// Unangetastet bleiben die Systembereiche aus §8.6: die Spuren 0, 1, 2 (Urlader und
// Nukleus) und 21–23 (Bootabbild, Verzeichnis, Belegungskarte).  Die beiden Bytes
// HINTER dem Kontrollblock (`41 F2` …, §13.5) werden nie beschrieben — sie sind als
// Gap/Schreibnaht eingeordnet, und TrackCodec::writeSector laesst sie stehen.

bool UdosFileSystem::reservedTrack(uint8_t track) const {
    return track <= 2
        || track == prof_.boot_track
        || track == prof_.directory_track
        || track == prof_.bitmap_track;
}

bool UdosFileSystem::validName(const std::string& name, std::string* why) {
    auto sag = [&](const std::string& m) { if (why) *why = m; return false; };
    if (name.empty())      return sag("Name ist leer");
    if (name.size() > 32)  return sag("Name ist laenger als 32 Zeichen");
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x21 || u > 0x7E) return sag("Name enthaelt ein Sonderzeichen oder Leerzeichen");
    }
    return true;
}

bool UdosFileSystem::writeLinked(UdosPointer p, const std::vector<uint8_t>& data128,
                                 UdosPointer back, UdosPointer fwd) {
    if (data128.size() != kSector) return fail("Sektordaten muessen 128 B lang sein");
    const std::vector<uint8_t> block = {back.sector_index, back.track,
                                        fwd.sector_index,  fwd.track};
    if (!space_.writeSector(p.track, head_, p.sectorId(), data128, block))
        return fail(space_.lastError());
    return true;
}

bool UdosFileSystem::writeData(UdosPointer p, const std::vector<uint8_t>& data128) {
    if (data128.size() != kSector) return fail("Sektordaten muessen 128 B lang sein");
    // Leeres tail-Argument: der vorhandene Kontrollblock bleibt unveraendert.
    if (!space_.writeSector(p.track, head_, p.sectorId(), data128))
        return fail(space_.lastError());
    return true;
}

bool UdosFileSystem::allocSectors(uint32_t n, std::vector<UdosPointer>& out) {
    out.clear();
    for (uint8_t t = 0; t < tracks_ && out.size() < n; ++t) {
        if (reservedTrack(t)) continue;
        for (uint8_t s = 1; s <= secs_per_track_ && out.size() < n; ++s) {
            if (bitmap_.used(t, s)) continue;
            out.push_back(UdosPointer{static_cast<uint8_t>(s - 1), t});
            bitmap_.setUsed(t, s, true);
        }
    }
    if (out.size() < n) {
        // Belegte Bits wieder freigeben — ein Fehlschlag darf nichts hinterlassen.
        for (const UdosPointer& p : out) bitmap_.setUsed(p.track, p.sectorId(), false);
        out.clear();
        return fail("Diskette voll: " + std::to_string(n) + " Sektoren noetig");
    }
    return true;
}

bool UdosFileSystem::saveBitmap() {
    bitmap_.refreshCounters();
    std::string err;
    if (!bitmap_.store(space_, head_, prof_.bitmap_track, err)) return fail(err);
    return true;
}

bool UdosFileSystem::growDirectory(UdosPointer& neuer_satz) {
    UdosFileHeader hdr;
    if (!readHeader(directoryHeader(), hdr)) return false;

    std::vector<UdosPointer> kette;
    if (!recordChain(hdr, kette)) return false;
    if (kette.empty()) return fail("Verzeichnisdatei hat keinen einzigen Satz");

    std::vector<UdosPointer> neu;
    if (!allocSectors(1, neu)) return false;
    neuer_satz = neu.front();

    // Neuer, leerer Satz: nur das Endebyte.  Rueckwaerts auf den bisher letzten Satz.
    std::vector<uint8_t> leer(kSector, 0x00);
    leer[0] = 0xFF;
    if (!writeLinked(neuer_satz, leer, kette.back(), UdosPointer{})) return false;

    // Bisher letzter Satz: Vorwaertszeiger auf den neuen (Daten unveraendert).
    {
        std::vector<uint8_t> d;
        UdosPointer back, fwd;
        if (!readSector(kette.back(), d, back, fwd)) return false;
        if (!writeLinked(kette.back(), d, back, neuer_satz)) return false;
    }

    // Kopfsektor der Verzeichnisdatei: Satzanzahl +1, „letzter Satz" nachziehen.
    // Sein Kontrollblock bleibt unangetastet (writeData).
    {
        std::vector<uint8_t> d;
        UdosPointer back, fwd;
        if (!readSector(directoryHeader(), d, back, fwd)) return false;
        d[10] = neuer_satz.sector_index;
        d[11] = neuer_satz.track;
        const uint16_t anzahl = static_cast<uint16_t>(le16(d.data() + 13) + 1);
        d[13] = static_cast<uint8_t>(anzahl & 0xFF);
        d[14] = static_cast<uint8_t>(anzahl >> 8);
        d[22] = static_cast<uint8_t>(hdr.record_len & 0xFF);   // letzter Satz voll
        d[23] = static_cast<uint8_t>(hdr.record_len >> 8);
        if (!writeData(directoryHeader(), d)) return false;
    }
    return true;
}

bool UdosFileSystem::appendDirEntry(const std::string& name, bool secret,
                                    UdosPointer header, UdosPointer& dir_record) {
    UdosFileHeader hdr;
    if (!readHeader(directoryHeader(), hdr)) return false;
    std::vector<UdosPointer> kette;
    if (!recordChain(hdr, kette)) return false;
    if (kette.empty()) return fail("Verzeichnisdatei hat keinen einzigen Satz");

    const size_t noetig = 3 + name.size();      // Flagbyte + Name + Zeiger
    UdosPointer  ziel   = kette.back();

    auto passtIn = [&](const std::vector<uint8_t>& satz, size_t& ende) {
        ende = 0;
        while (ende < satz.size() && satz[ende] != 0xFF) {
            const uint8_t n = satz[ende] & 0x3F;
            if (n == 0) return false;
            ende += 3 + n;
        }
        if (ende >= satz.size()) return false;              // kein Endebyte gefunden
        return ende + noetig + 1 <= satz.size();            // +1 fuer das neue 0xFF
    };

    std::vector<uint8_t> satz;
    UdosPointer back, fwd;
    if (!readSector(ziel, satz, back, fwd)) return false;

    size_t ende = 0;
    if (!passtIn(satz, ende)) {
        // §8.4 Schritt 5: passt er nicht mehr, waechst die Datei DIRECTORY selbst.
        if (!growDirectory(ziel)) return false;
        if (!readSector(ziel, satz, back, fwd)) return false;
        if (!passtIn(satz, ende))
            return fail("Verzeichniseintrag passt auch in einen frischen Satz nicht");
    }

    satz[ende] = static_cast<uint8_t>((secret ? 0x80 : 0x00) | (name.size() & 0x3F));
    for (size_t i = 0; i < name.size(); ++i)
        satz[ende + 1 + i] = static_cast<uint8_t>(name[i]);
    satz[ende + 1 + name.size()]     = header.sector_index;
    satz[ende + 2 + name.size()]     = header.track;
    satz[ende + noetig]              = 0xFF;

    if (!writeData(ziel, satz)) return false;
    dir_record = ziel;
    return true;
}

bool UdosFileSystem::removeDirEntry(const UdosDirEntry& e) {
    std::vector<uint8_t> satz;
    UdosPointer back, fwd;
    if (!readSector(e.record, satz, back, fwd)) return false;

    const size_t laenge = 3 + e.name.size();
    if (e.offset + laenge > satz.size()) return fail("Verzeichniseintrag liegt schief");

    // §8.5: nachfolgende Eintraege nach vorn schieben, 0xFF nachziehen.  Der Rest des
    // Sektors bleibt Altbestand — genau so haelt es UDOS selbst.
    for (size_t i = e.offset; i + laenge < satz.size(); ++i) satz[i] = satz[i + laenge];
    for (size_t i = satz.size() - laenge; i < satz.size(); ++i) satz[i] = 0xFF;

    return writeData(e.record, satz);
}

bool UdosFileSystem::write(const std::string& name, const std::vector<uint8_t>& data,
                           const WriteOptions& opt) {
    std::string warum;
    if (!validName(name, &warum)) return fail("'" + name + "': " + warum);
    if (data.empty()) return fail("leere Dateien kann UDOS nicht ablegen");

    // Vorhandene gleichnamige Datei?
    for (const UdosDirEntry& d : directory()) {
        if (d.name != name) continue;
        if (!opt.overwrite)
            return fail("'" + name + "' existiert bereits — Ueberschreiben nicht erlaubt");
        if (!erase(name)) return false;
        break;
    }

    constexpr uint16_t satzlen = kSector;        // s. Abschnittskommentar
    const uint32_t saetze  = static_cast<uint32_t>((data.size() + satzlen - 1) / satzlen);
    const uint16_t im_letzten = static_cast<uint16_t>(data.size() % satzlen);

    // Kopfsektor + Saetze belegen.  allocSectors nimmt bei Platzmangel alles zurueck.
    std::vector<UdosPointer> sektoren;
    if (!allocSectors(saetze + 1, sektoren)) return false;
    const UdosPointer kopf = sektoren.front();

    // Verzeichniseintrag zuerst — er bestimmt den Rueckwaertszeiger des Kopfsektors.
    UdosPointer dir_record;
    if (!appendDirEntry(name, /*secret=*/false, kopf, dir_record)) return false;

    // Saetze schreiben; die Kette laeuft Kopf → Satz 1 → … → Satz n → FF FF.
    for (uint32_t i = 0; i < saetze; ++i) {
        std::vector<uint8_t> block(satzlen, opt.text ? 0x1A : 0x00);
        const size_t ab    = static_cast<size_t>(i) * satzlen;
        const size_t menge = std::min<size_t>(satzlen, data.size() - ab);
        std::copy(data.begin() + static_cast<long>(ab),
                  data.begin() + static_cast<long>(ab + menge), block.begin());

        const UdosPointer selbst = sektoren[i + 1];
        const UdosPointer vor    = (i == 0) ? kopf : sektoren[i];
        const UdosPointer nach   = (i + 1 < saetze) ? sektoren[i + 2] : UdosPointer{};
        if (!writeLinked(selbst, block, vor, nach)) return false;
    }

    // Kopfsektor nach §6 fuellen.
    std::vector<uint8_t> h(kSector, 0xFF);
    std::fill(h.begin(), h.begin() + 48, static_cast<uint8_t>(0x00));
    h[6]  = dir_record.sector_index;  h[7]  = dir_record.track;
    h[8]  = sektoren[1].sector_index; h[9]  = sektoren[1].track;
    h[10] = sektoren[saetze].sector_index; h[11] = sektoren[saetze].track;
    h[12] = opt.text ? 0x20 : 0x10;                 // A = ASCII, B = BINARY
    h[13] = static_cast<uint8_t>(saetze & 0xFF);
    h[14] = static_cast<uint8_t>(saetze >> 8);
    h[15] = static_cast<uint8_t>(satzlen & 0xFF);
    h[16] = static_cast<uint8_t>(satzlen >> 8);
    h[17] = h[15];  h[18] = h[16];                  // §6: meist Kopie der Satzlaenge
    h[19] = 0x00;                                   // keine Eigenschaften
    h[20] = h[21] = 0x00;                           // ENTRY nur bei Typ P/P1 sinnvoll
    // §7.1: „Bytes im letzten Satz" = Satzlaenge, wenn er voll ist (so haelt es UDOS,
    // nachgewiesen an SD: 7 Saetze à 128 mit 0080 im Feld).
    const uint16_t rest = im_letzten ? im_letzten : satzlen;
    h[22] = static_cast<uint8_t>(rest & 0xFF);
    h[23] = static_cast<uint8_t>(rest >> 8);

    std::string datum = opt.date;
    if (datum.size() != 6) {
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
        datum = puffer;
    }
    for (int i = 0; i < 6; ++i) {
        h[24 + i] = static_cast<uint8_t>(datum[i]);   // Erstellung
        h[32 + i] = static_cast<uint8_t>(datum[i]);   // letzte Aenderung
    }
    h[30] = 0xFF; h[31] = 0x00;
    h[38] = 0xFF; h[39] = 0x00;

    if (!writeLinked(kopf, h, dir_record, sektoren[1])) return false;
    return saveBitmap();
}

bool UdosFileSystem::erase(const std::string& name) {
    const std::vector<UdosDirEntry> verz = directory();
    const UdosDirEntry* treffer = nullptr;
    for (const UdosDirEntry& d : verz) if (d.name == name) treffer = &d;
    if (!treffer) return fail("Datei '" + name + "' steht nicht im Verzeichnis");

    if (name == "DIRECTORY")
        return fail("Die Verzeichnisdatei selbst darf nicht geloescht werden");

    // Erst die Sektoren einsammeln, dann eintragen — so bleibt bei einem Lesefehler
    // die Karte unberuehrt.
    UdosFileHeader hdr;
    if (!readHeader(treffer->header, hdr)) return false;
    std::vector<UdosPointer> kette;
    if (!recordChain(hdr, kette)) return false;

    const uint32_t sek_je_satz = std::max<uint32_t>(1, hdr.record_len / kSector);
    std::vector<UdosPointer> frei = {treffer->header};
    for (const UdosPointer& satz : kette)
        for (uint32_t k = 0; k < sek_je_satz; ++k)
            frei.push_back(UdosPointer{static_cast<uint8_t>(satz.sector_index + k),
                                       satz.track});

    if (!removeDirEntry(*treffer)) return false;

    // §8.5: die Kontrollbloecke auf dem Medium bleiben stehen — massgeblich ist allein
    // die Belegungskarte.
    for (const UdosPointer& p : frei) bitmap_.setUsed(p.track, p.sectorId(), false);
    return saveBitmap();
}

bool UdosFileSystem::mkfs() {
    // Leeres UDOS-Dateisystem: Karte anlegen, Systembereiche sperren, Verzeichnisdatei
    // mit einem Satz und ihrem eigenen Eintrag erzeugen (§8.4/§10).
    bitmap_ = UdosBitmap::makeEmpty(secs_per_track_, tracks_, label_);

    // Systemspuren sperren, wie sie der Referenzdatentraeger zeigt (§3): Urlader und
    // Nukleus (0–2), Bootabbild (21), Verzeichnisspur (22) ganz, Karte (23) S1–3.
    for (uint8_t t = 0; t <= 2; ++t)
        for (uint8_t s = 1; s <= secs_per_track_; ++s) bitmap_.setUsed(t, s, true);
    for (uint8_t s = 17; s <= 22 && s <= secs_per_track_; ++s)
        bitmap_.setUsed(prof_.boot_track, s, true);
    for (uint8_t s = 1; s <= secs_per_track_; ++s)
        bitmap_.setUsed(prof_.directory_track, s, true);
    for (uint8_t s = 1; s <= 3; ++s) bitmap_.setUsed(prof_.bitmap_track, s, true);

    const UdosPointer kopf = directoryHeader();                        // Spur 22 S1
    const UdosPointer satz{5, prof_.directory_track};                  // Spur 22 S6

    // Der erste Satz traegt den Eintrag der Verzeichnisdatei selbst — geheim, Typ D,
    // genau wie auf dem Referenzdatentraeger („89 'DIRECTORY' 00 16").
    std::vector<uint8_t> erster(kSector, 0x00);
    const std::string name = "DIRECTORY";
    erster[0] = static_cast<uint8_t>(0x80 | name.size());
    for (size_t i = 0; i < name.size(); ++i)
        erster[1 + i] = static_cast<uint8_t>(name[i]);
    erster[1 + name.size()] = kopf.sector_index;
    erster[2 + name.size()] = kopf.track;
    erster[3 + name.size()] = 0xFF;
    if (!writeLinked(satz, erster, kopf, UdosPointer{})) return false;

    std::vector<uint8_t> h(kSector, 0xFF);
    std::fill(h.begin(), h.begin() + 48, static_cast<uint8_t>(0x00));
    h[6]  = satz.sector_index; h[7]  = satz.track;      // Eintrag steht im 1. Satz
    h[8]  = satz.sector_index; h[9]  = satz.track;      // erster Satz
    h[10] = satz.sector_index; h[11] = satz.track;      // letzter Satz
    h[12] = 0x40;                                       // Typ D
    h[13] = 1; h[14] = 0;                               // 1 Satz
    h[15] = static_cast<uint8_t>(kSector & 0xFF); h[16] = 0;
    h[17] = h[15]; h[18] = h[16];
    h[19] = 0xF0;                                       // Eigenschaften WELS
    h[22] = static_cast<uint8_t>(kSector & 0xFF); h[23] = 0;
    h[30] = 0xFF; h[31] = 0x00;
    h[38] = 0xFF; h[39] = 0x00;
    if (!writeLinked(kopf, h, satz, satz)) return false;

    return saveBitmap();
}

bool UdosFileSystem::wouldFit(const std::vector<PlannedFile>& files, FitReport& out) const {
    out = FitReport{};

    // Spurweise gegen die Karte rechnen, nicht mit der Summe: die Systemspuren (§8.6)
    // sind fuer ein Werkzeug tabu, auch wenn dort Bits frei sind.  Da mit Satzlaenge
    // 128 geschrieben wird, ist jeder uebrige freie Sektor brauchbar — die Aussage ist
    // damit exakt und nicht geschaetzt.
    int frei = 0;
    for (uint8_t t = 0; t < tracks_; ++t) {
        if (reservedTrack(t)) continue;
        for (uint8_t s = 1; s <= secs_per_track_; ++s)
            if (!bitmap_.used(t, s)) ++frei;
    }

    // Ersetzte gleichnamige Dateien geben ihre Sektoren zurueck.
    const std::vector<UdosDirEntry> verz = directory();
    for (const PlannedFile& f : files) {
        for (const UdosDirEntry& d : verz) {
            if (d.name != f.name) continue;
            UdosFileHeader hdr;
            if (!readHeader(d.header, hdr)) continue;
            std::vector<UdosPointer> kette;
            if (!recordChain(hdr, kette)) continue;
            frei += 1 + static_cast<int>(kette.size())
                      * std::max<uint32_t>(1, hdr.record_len / kSector);
        }
    }

    uint32_t noetig = 0;
    for (const PlannedFile& f : files)
        noetig += 1 + static_cast<uint32_t>((f.size + kSector - 1) / kSector);

    out.needed    = static_cast<uint64_t>(noetig) * kSector;
    out.available = static_cast<uint64_t>(frei)   * kSector;
    out.fits      = static_cast<int>(noetig) <= frei;
    if (!out.fits)
        out.detail = std::to_string(files.size()) + " Dateien benoetigen "
                   + std::to_string(noetig) + " Sektoren (je Datei einen Kopfsektor), "
                     "frei sind " + std::to_string(frei);
    return true;
}

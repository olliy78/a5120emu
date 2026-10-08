/**
 * @file wega_fs.cpp
 * @brief WEGA-Dateisystem (System III) — Umsetzung.  Entwurf: doc/design/27_wega_dateisystem.md
 *
 * Die Algorithmen fuer Block- und Inode-Vergabe sind die des WEGA-Kerns
 * (`uts/sys/alloc.c`: alloc/free/ialloc/ifree) und von `sa.mkfs` (bflist/iput) —
 * absichtlich nicht „verbessert": eine Diskette, die dieses Werkzeug beschrieben hat,
 * muss fuer den echten Kern aussehen wie eine, die er selbst beschrieben hat.
 *
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/wega/wega_fs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace wega;

namespace {

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16
         | static_cast<uint32_t>(p[2]) << 8 | p[3];
}
void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); }
void put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);  p[3] = static_cast<uint8_t>(v);
}

std::string festText(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && p[i]; ++i) s += static_cast<char>(p[i]);
    return s;
}

/// @brief Sekunden seit 1970 (GMT) → "JJJJ-MM-TT HH:MM" — ohne gmtime (MSVC kennt kein _r).
std::string datumText(uint32_t t) {
    if (t == 0) return {};
    const int64_t tage = t / 86400;
    const int64_t rest = t % 86400;
    // Tage → Kalenderdatum (H. Hinnant, days_from_civil umgekehrt)
    int64_t z = tage + 719468;
    const int64_t era = z / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = mp + (mp < 10 ? 3 : -9);
    if (m <= 2) ++y;
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", static_cast<int>(y),
                  static_cast<int>(m), static_cast<int>(d),
                  static_cast<int>(rest / 3600), static_cast<int>(rest % 3600 / 60));
    return b;
}

/// @brief Pfad in Bestandteile zerlegen; leere und "." werden uebergangen.
std::vector<std::string> teile(const std::string& pfad) {
    std::vector<std::string> out;
    std::string t;
    for (char c : pfad + "/") {
        if (c == '/') {
            if (!t.empty() && t != ".") out.push_back(t);
            t.clear();
        } else {
            t += c;
        }
    }
    return out;
}

std::string artVon(uint16_t mode) {
    switch (mode & IFMT) {
        case IFDIR: return "d";
        case IFREG: return "-";
        case IFCHR: return "c";
        case IFBLK: return "b";
        case IFIFO: return "p";
        case IFMPC: return "mc";
        case IFMPB: return "mb";
        default:    return "?";
    }
}

bool bekannteArt(uint16_t mode) {
    switch (mode & IFMT) {
        case IFDIR: case IFREG: case IFCHR: case IFBLK: case IFIFO: case IFMPC: case IFMPB:
            return true;
        default:
            return false;
    }
}

/// @brief Bedarf an Bloecken (Daten + indirekte) fuer @p groesse Byte.
uint64_t blockBedarf(uint64_t groesse) {
    uint64_t d = (groesse + kBlock - 1) / kBlock;
    uint64_t n = d;
    if (d <= kNDirekt) return n;
    d -= kNDirekt;
    n += 1;                                        // einfach indirekt
    if (d <= kNIndir) return n;
    d -= kNIndir;
    const uint64_t k2 = static_cast<uint64_t>(kNIndir) * kNIndir;
    const uint64_t in2 = std::min<uint64_t>(d, k2);
    n += 1 + (in2 + kNIndir - 1) / kNIndir;        // doppelt indirekt
    if (d <= k2) return n;
    d -= k2;
    n += 1 + (d + k2 - 1) / k2 + (d + kNIndir - 1) / kNIndir;   // dreifach
    return n;
}

constexpr uint64_t kMaxDatei =
    static_cast<uint64_t>(kNDirekt + kNIndir + kNIndir * kNIndir) * kBlock
    + static_cast<uint64_t>(kNIndir) * kNIndir * kNIndir * kBlock;

} // namespace

// ═══ Superblock / Inode ═════════════════════════════════════════════════════

Superblock Superblock::aus(const uint8_t* b) {
    Superblock s;
    std::memcpy(s.roh.data(), b, kBlock);
    s.isize = be16(b + 0);
    s.fsize = be32(b + 2);
    s.nfree = static_cast<int16_t>(be16(b + 6));
    for (int i = 0; i < kNicFree; ++i) s.free[static_cast<size_t>(i)] = be32(b + 8 + 4 * i);
    s.ninode = static_cast<int16_t>(be16(b + 208));
    for (int i = 0; i < kNicInod; ++i) s.inode[static_cast<size_t>(i)] = be16(b + 210 + 2 * i);
    s.flock = b[410]; s.ilock = b[411]; s.fmod = b[412]; s.ronly = b[413];
    s.time   = be32(b + 414);
    s.tfree  = be32(b + 418);
    s.tinode = be16(b + 422);
    s.m = static_cast<int16_t>(be16(b + 424));
    s.n = static_cast<int16_t>(be16(b + 426));
    s.fname = festText(b + 428, 6);
    s.fpack = festText(b + 434, 6);
    s.mach  = b[440];
    return s;
}

void Superblock::in(uint8_t* b) const {
    std::memcpy(b, roh.data(), kBlock);
    put16(b + 0, isize);
    put32(b + 2, fsize);
    put16(b + 6, static_cast<uint16_t>(nfree));
    for (int i = 0; i < kNicFree; ++i) put32(b + 8 + 4 * i, free[static_cast<size_t>(i)]);
    put16(b + 208, static_cast<uint16_t>(ninode));
    for (int i = 0; i < kNicInod; ++i) put16(b + 210 + 2 * i, inode[static_cast<size_t>(i)]);
    b[410] = flock; b[411] = ilock; b[412] = fmod; b[413] = ronly;
    put32(b + 414, time);
    put32(b + 418, tfree);
    put16(b + 422, tinode);
    put16(b + 424, static_cast<uint16_t>(m));
    put16(b + 426, static_cast<uint16_t>(n));
    std::memset(b + 428, 0, 12);
    std::memcpy(b + 428, fname.data(), std::min<size_t>(fname.size(), 6));
    std::memcpy(b + 434, fpack.data(), std::min<size_t>(fpack.size(), 6));
    b[440] = mach;
}

Dinode Dinode::aus(const uint8_t* p) {
    Dinode d;
    d.mode  = be16(p + 0);
    d.nlink = static_cast<int16_t>(be16(p + 2));
    d.uid   = static_cast<int16_t>(be16(p + 4));
    d.gid   = static_cast<int16_t>(be16(p + 6));
    d.size  = be32(p + 8);
    for (int k = 0; k < kNAddr; ++k) {
        const uint8_t* a = p + 12 + 3 * k;
        d.addr[static_cast<size_t>(k)] =
            static_cast<uint32_t>(a[0]) << 16 | static_cast<uint32_t>(a[1]) << 8 | a[2];
    }
    d.pad40 = p[51];
    d.atime = be32(p + 52);
    d.mtime = be32(p + 56);
    d.ctime = be32(p + 60);
    return d;
}

void Dinode::in(uint8_t* p) const {
    put16(p + 0, mode);
    put16(p + 2, static_cast<uint16_t>(nlink));
    put16(p + 4, static_cast<uint16_t>(uid));
    put16(p + 6, static_cast<uint16_t>(gid));
    put32(p + 8, size);
    for (int k = 0; k < kNAddr; ++k) {
        const uint32_t v = addr[static_cast<size_t>(k)];
        p[12 + 3 * k] = static_cast<uint8_t>(v >> 16);
        p[13 + 3 * k] = static_cast<uint8_t>(v >> 8);
        p[14 + 3 * k] = static_cast<uint8_t>(v);
    }
    p[51] = pad40;
    put32(p + 52, atime);
    put32(p + 56, mtime);
    put32(p + 60, ctime);
}

// ═══ Blockgeraete ═══════════════════════════════════════════════════════════

bool WegaSpaceDev::ort(uint32_t bn, FsRecoverOrt& out) const {
    const uint64_t off = static_cast<uint64_t>(bn) * kBlock;
    for (size_t i = 0; i < s_.trackCount(); ++i) {
        const SectorSpace::TrackRef t = s_.trackAt(i);
        if (off >= t.start && off < t.start + t.bytes && t.sector_size) {
            out.cyl    = t.cyl;
            out.head   = t.head;
            out.sector = t.first_id + static_cast<int>((off - t.start) / t.sector_size);
            return true;
        }
    }
    return false;
}

bool WegaSpeicherDev::readBlock(uint32_t bn, uint8_t* dst) const {
    if (bn >= n_) return false;
    const int64_t o = off_(bn);
    if (o < 0 || static_cast<uint64_t>(o) + kBlock > buf_.size()) return false;
    std::memcpy(dst, buf_.data() + o, kBlock);
    return true;
}

bool WegaSpeicherDev::writeBlock(uint32_t bn, const uint8_t* src) {
    if (bn >= n_) return false;
    const int64_t o = off_(bn);
    if (o < 0 || static_cast<uint64_t>(o) + kBlock > buf_.size()) return false;
    if (std::memcmp(buf_.data() + o, src, kBlock) == 0) return true;
    std::memcpy(buf_.data() + o, src, kBlock);
    if (geaendert_) geaendert_();
    return true;
}

// ═══ Arbeitskopie einer schreibenden Operation ══════════════════════════════

/**
 * Alle Blockaenderungen einer Operation sammeln sich hier; erst @ref commit bringt sie
 * aufs Geraet.  Scheitert die Operation (kein Platz, keine Inode), verfaellt die
 * Kopie — das Geraet hat dann nichts gesehen.
 */
class WegaFileSystem::Tx {
public:
    explicit Tx(WegaFileSystem& fs) : fs_(fs) {}

    bool start() {
        uint8_t b[kBlock];
        if (!fs_.dev_->readBlock(1, b)) return fs_.fail("Superblock nicht lesbar");
        sb = Superblock::aus(b);
        return true;
    }
    Leser leser() {
        return [this](uint32_t bn, uint8_t* d) { return lies(bn, d); };
    }
    bool lies(uint32_t bn, uint8_t* d) const {
        auto it = kopie_.find(bn);
        if (it != kopie_.end()) { std::memcpy(d, it->second.data(), kBlock); return true; }
        return fs_.dev_->readBlock(bn, d);
    }
    void schreib(uint32_t bn, const uint8_t* d) {
        std::array<uint8_t, kBlock>& z = kopie_[bn];
        std::memcpy(z.data(), d, kBlock);
    }
    bool commit() {
        sb.time = fs_.jetzt();
        sb.fmod = 0;
        uint8_t b[kBlock];
        if (!fs_.dev_->readBlock(1, b)) return fs_.fail("Superblock nicht lesbar");
        sb.in(b);
        schreib(1, b);
        for (const auto& [bn, inhalt] : kopie_)
            if (!fs_.dev_->writeBlock(bn, inhalt.data()))
                return fs_.fail("Block " + std::to_string(bn) + " nicht schreibbar");
        return true;
    }

    // ── Inodes ───────────────────────────────────────────────────────────────
    bool inode(uint32_t ino, Dinode& d) const {
        Leser l = [this](uint32_t bn, uint8_t* x) { return lies(bn, x); };
        return WegaFileSystem::inodeLesen(l, sb, ino, d);
    }
    bool setzeInode(uint32_t ino, const Dinode& d) {
        if (ino < 1 || ino > sb.inodes()) return fs_.fail("Inode ausserhalb");
        const uint32_t bn = 2 + (ino - 1) / kInodesProBlock;
        uint8_t b[kBlock];
        if (!lies(bn, b)) return fs_.fail("Inode-Block nicht lesbar");
        d.in(b + ((ino - 1) % kInodesProBlock) * kInodeGroesse);
        schreib(bn, b);
        return true;
    }

    /// @brief `alloc()` des Kerns.
    bool allocBlock(uint32_t& bno) {
        for (;;) {
            if (sb.nfree <= 0 || sb.nfree > kNicFree)
                return fs_.fail("Kein Platz mehr auf dem Datentraeger");
            bno = sb.free[static_cast<size_t>(--sb.nfree)];
            if (bno == 0) return fs_.fail("Kein Platz mehr auf dem Datentraeger");
            if (bno >= sb.isize && bno < sb.fsize) break;   // sonst: badblock, weiter
        }
        if (sb.nfree <= 0) {
            uint8_t b[kBlock];
            if (!lies(bno, b)) return fs_.fail("Freilistenblock nicht lesbar");
            const int16_t n = static_cast<int16_t>(be16(b));
            if (n <= 0 || n > kNicFree) return fs_.fail("Freiliste beschaedigt (Block "
                                                       + std::to_string(bno) + ")");
            sb.nfree = n;
            for (int i = 0; i < kNicFree; ++i) sb.free[static_cast<size_t>(i)] = be32(b + 2 + 4 * i);
        }
        uint8_t null[kBlock] = {};
        schreib(bno, null);
        if (sb.tfree) --sb.tfree;
        return true;
    }

    /// @brief `free()` des Kerns.
    void freeBlock(uint32_t bno) {
        if (bno < sb.isize || bno >= sb.fsize) return;
        if (sb.nfree <= 0) { sb.nfree = 1; sb.free[0] = 0; }
        if (sb.nfree >= kNicFree) {
            uint8_t b[kBlock] = {};
            put16(b, static_cast<uint16_t>(sb.nfree));
            for (int i = 0; i < kNicFree; ++i) put32(b + 2 + 4 * i, sb.free[static_cast<size_t>(i)]);
            schreib(bno, b);
            sb.nfree = 0;
        }
        sb.free[static_cast<size_t>(sb.nfree++)] = bno;
        ++sb.tfree;
    }

    /// @brief `ialloc()` des Kerns — liefert eine geleerte Inode.
    bool allocInode(uint32_t& ino) {
        for (int runde = 0; runde < 2; ++runde) {
            while (sb.ninode > 0) {
                ino = sb.inode[static_cast<size_t>(--sb.ninode)];
                if (ino < kRootIno || ino > sb.inodes()) continue;
                Dinode d;
                if (!inode(ino, d)) continue;
                if (d.mode != 0) continue;       // doch belegt — weitersuchen
                if (sb.tinode) --sb.tinode;
                return true;
            }
            // Zwischenspeicher leer: die Inode-Liste durchsuchen (wie der Kern).
            for (uint32_t i = 1; i <= sb.inodes() && sb.ninode < kNicInod; ++i) {
                Dinode d;
                if (!inode(i, d)) continue;
                if (d.mode == 0 && i >= kRootIno)
                    sb.inode[static_cast<size_t>(sb.ninode++)] = static_cast<uint16_t>(i);
            }
            if (sb.ninode == 0) break;
        }
        return fs_.fail("Keine freie Inode mehr (Inode-Liste voll)");
    }

    /// @brief `ifree()` des Kerns.
    void freeInode(uint32_t ino) {
        ++sb.tinode;
        if (sb.ninode >= kNicInod) return;
        sb.inode[static_cast<size_t>(sb.ninode++)] = static_cast<uint16_t>(ino);
    }

    /// @brief Alle Bloecke einer Inode freigeben und die Adressen loeschen (`itrunc`).
    bool kuerzen(Dinode& d) {
        if (!hatBloecke(d.mode)) return true;
        std::vector<uint32_t> daten, indirekt;
        std::string err;
        if (!WegaFileSystem::dateiBloecke(leser(), d, sb.fsize, daten, &indirekt, err))
            return fs_.fail(err);
        // Rueckwaerts freigeben: der Kern gibt vom Ende her frei, und die Freiliste
        // liefert danach dieselben Bloecke in Vorwaertsreihenfolge wieder aus.
        for (auto it = daten.rbegin(); it != daten.rend(); ++it) if (*it) freeBlock(*it);
        for (auto it = indirekt.rbegin(); it != indirekt.rend(); ++it) if (*it) freeBlock(*it);
        d.addr.fill(0);
        d.size = 0;
        return true;
    }

    /// @brief Inhalt einer (gekuerzten) Inode schreiben: Datenbloecke + Indirektion.
    bool fuellen(Dinode& d, const std::vector<uint8_t>& inhalt) {
        if (inhalt.size() > kMaxDatei) return fs_.fail("Datei zu gross fuer WEGA");
        const size_t n = (inhalt.size() + kBlock - 1) / kBlock;
        std::vector<uint32_t> bl(n);
        for (size_t i = 0; i < n; ++i) {
            if (!allocBlock(bl[i])) return false;
            uint8_t b[kBlock] = {};
            const size_t von = i * kBlock;
            std::memcpy(b, inhalt.data() + von, std::min<size_t>(kBlock, inhalt.size() - von));
            schreib(bl[i], b);
        }
        size_t k = 0;
        for (int i = 0; i < kNDirekt && k < n; ++i) d.addr[static_cast<size_t>(i)] = bl[k++];
        // Indirektionsstufen 1..3: ein Block mit 128 Adressen je Stufe
        for (int stufe = 1; stufe <= 3 && k < n; ++stufe) {
            uint32_t wurzel = 0;
            if (!baue(stufe, bl, k, wurzel)) return false;
            d.addr[static_cast<size_t>(kNDirekt + stufe - 1)] = wurzel;
        }
        d.size = static_cast<uint32_t>(inhalt.size());
        return true;
    }

    Superblock sb;

private:
    /// @brief Indirekten Block der Stufe @p stufe bauen, Datenbloecke ab @p k verbrauchen.
    bool baue(int stufe, const std::vector<uint32_t>& bl, size_t& k, uint32_t& out) {
        if (!allocBlock(out)) return false;
        uint8_t b[kBlock] = {};
        for (int i = 0; i < kNIndir && k < bl.size(); ++i) {
            uint32_t eintrag = 0;
            if (stufe == 1) eintrag = bl[k++];
            else if (!baue(stufe - 1, bl, k, eintrag)) return false;
            put32(b + 4 * i, eintrag);
        }
        schreib(out, b);
        return true;
    }

    WegaFileSystem& fs_;
    std::map<uint32_t, std::array<uint8_t, kBlock>> kopie_;
};

// ═══ Lesen ══════════════════════════════════════════════════════════════════

uint32_t WegaFileSystem::jetzt() const {
    if (uhr_) return uhr_();
    return static_cast<uint32_t>(std::time(nullptr));
}

WegaFileSystem::Leser WegaFileSystem::geraeteLeser() const {
    const WegaBlockDev* d = dev_.get();
    return [d](uint32_t bn, uint8_t* x) { return d->readBlock(bn, x); };
}

wega::Superblock WegaFileSystem::superblock() const {
    uint8_t b[kBlock] = {};
    dev_->readBlock(1, b);
    return Superblock::aus(b);
}

bool WegaFileSystem::inodeLesen(const Leser& l, const Superblock& sb, uint32_t ino,
                                Dinode& out) {
    if (ino < 1 || ino > sb.inodes()) return false;
    uint8_t b[kBlock];
    if (!l(2 + (ino - 1) / kInodesProBlock, b)) return false;
    out = Dinode::aus(b + ((ino - 1) % kInodesProBlock) * kInodeGroesse);
    return true;
}

bool WegaFileSystem::readInode(uint32_t ino, Dinode& out) const {
    return inodeLesen(geraeteLeser(), superblock(), ino, out);
}

bool WegaFileSystem::dateiBloecke(const Leser& l, const Dinode& di, uint32_t fsize,
                                  std::vector<uint32_t>& daten,
                                  std::vector<uint32_t>* indirekt, std::string& err) {
    daten.clear();
    if (indirekt) indirekt->clear();
    if (!hatBloecke(di.mode)) return true;
    // Gezaehlt wird bis zur Dateigroesse — dahinter stehende Adressen sind Reste.
    // Fuer die Freigabe (`indirekt != nullptr`) wird dagegen ALLES eingesammelt.
    const uint64_t noetig = (static_cast<uint64_t>(di.size) + kBlock - 1) / kBlock;
    const bool alles = indirekt != nullptr;
    for (int i = 0; i < kNDirekt; ++i) {
        if (!alles && daten.size() >= noetig) return true;
        daten.push_back(di.addr[static_cast<size_t>(i)]);
    }
    std::function<bool(uint32_t, int)> ab = [&](uint32_t bn, int stufe) -> bool {
        const uint64_t pro = stufe == 1 ? 1u : stufe == 2 ? kNIndir : kNIndir * kNIndir;
        if (bn == 0) {
            // Loch: so viele Nullbloecke, wie dieser Ast haette tragen koennen.
            if (alles) return true;
            for (uint64_t i = 0; i < kNIndir * pro && daten.size() < noetig; ++i)
                daten.push_back(0);
            return true;
        }
        if (bn >= fsize) { err = "Indirekter Block " + std::to_string(bn) + " ausserhalb"; return false; }
        if (indirekt) indirekt->push_back(bn);
        uint8_t b[kBlock];
        if (!l(bn, b)) { err = "Block " + std::to_string(bn) + " nicht lesbar"; return false; }
        for (int i = 0; i < kNIndir; ++i) {
            if (!alles && daten.size() >= noetig) return true;
            const uint32_t a = be32(b + 4 * i);
            if (stufe == 1) daten.push_back(a);
            else if (!ab(a, stufe - 1)) return false;
        }
        return true;
    };
    for (int stufe = 1; stufe <= 3; ++stufe) {
        if (!alles && daten.size() >= noetig) return true;
        if (!ab(di.addr[static_cast<size_t>(kNDirekt + stufe - 1)], stufe)) return false;
    }
    return true;
}

bool WegaFileSystem::dateiLesen(const Leser& l, const Dinode& di, uint32_t fsize,
                                std::vector<uint8_t>& out, std::string& err) {
    std::vector<uint32_t> bl;
    if (!dateiBloecke(l, di, fsize, bl, nullptr, err)) return false;
    out.assign(static_cast<size_t>(di.size), 0);
    uint8_t b[kBlock];
    for (size_t i = 0; i < bl.size(); ++i) {
        const size_t von = i * kBlock;
        if (von >= out.size()) break;
        if (bl[i] == 0) continue;                       // Loch = Nullen
        if (bl[i] >= fsize) { err = "Block " + std::to_string(bl[i]) + " ausserhalb"; return false; }
        if (!l(bl[i], b)) { err = "Block " + std::to_string(bl[i]) + " nicht lesbar"; return false; }
        std::memcpy(out.data() + von, b, std::min<size_t>(kBlock, out.size() - von));
    }
    return true;
}

bool WegaFileSystem::verzeichnisLesen(const Leser& l, const Superblock& sb, uint32_t ino,
                                      std::vector<DirEintrag>& out, std::string& err) {
    out.clear();
    Dinode d;
    if (!inodeLesen(l, sb, ino, d)) { err = "Inode " + std::to_string(ino) + " nicht lesbar"; return false; }
    if ((d.mode & IFMT) != IFDIR) { err = "kein Verzeichnis"; return false; }
    std::vector<uint8_t> roh;
    if (!dateiLesen(l, d, sb.fsize, roh, err)) return false;
    for (size_t k = 0; k + kDirEintrag <= roh.size(); k += kDirEintrag) {
        const uint16_t n = be16(roh.data() + k);
        out.push_back({n, festText(roh.data() + k + 2, kDirSiz), static_cast<uint32_t>(k / kDirEintrag)});
    }
    return true;
}

uint32_t WegaFileSystem::suche(const Leser& l, const Superblock& sb, const std::string& path,
                               uint32_t* eltern, std::string* blatt) const {
    const std::vector<std::string> t = teile(path);
    uint32_t ino = kRootIno, vor = kRootIno;
    if (blatt) *blatt = t.empty() ? std::string() : t.back();
    for (size_t i = 0; i < t.size(); ++i) {
        std::vector<DirEintrag> ein;
        std::string err;
        vor = ino;
        if (!verzeichnisLesen(l, sb, ino, ein, err)) {
            if (eltern) *eltern = 0;
            return 0;
        }
        uint32_t gefunden = 0;
        for (const DirEintrag& e : ein)
            if (e.ino != 0 && e.name == t[i]) { gefunden = e.ino; break; }
        if (gefunden == 0) {
            // Nur der LETZTE Bestandteil darf fehlen — dann kennt der Aufrufer den Elter.
            if (eltern) *eltern = (i + 1 == t.size()) ? vor : 0;
            return 0;
        }
        ino = gefunden;
    }
    if (eltern) *eltern = t.empty() ? 0 : vor;
    return ino;
}

uint32_t WegaFileSystem::lookup(const std::string& path) const {
    return suche(geraeteLeser(), superblock(), path, nullptr, nullptr);
}

std::string WegaFileSystem::modeText(uint16_t mode) {
    std::string s = artVon(mode).substr(0, 1);
    static const char* rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i) s += (mode & (0400 >> i)) ? rwx[i] : '-';
    if (mode & 04000) s[3] = (mode & 0100) ? 's' : 'S';
    if (mode & 02000) s[6] = (mode & 0010) ? 's' : 'S';
    if (mode & 01000) s[9] = (mode & 0001) ? 't' : 'T';
    return s;
}

// ═══ Erkennung und Einhaengen ═══════════════════════════════════════════════

bool WegaFileSystem::looksLikeWega(const WegaBlockDev& dev, std::string* why) {
    auto nein = [&](const std::string& w) { if (why) *why = w; return false; };
    uint8_t b[kBlock];
    if (dev.blocks() < 8 || !dev.readBlock(1, b)) return nein("Block 1 nicht lesbar");
    const Superblock sb = Superblock::aus(b);
    if (sb.isize < 3 || sb.fsize <= sb.isize)
        return nein("Superblock: s_isize " + std::to_string(sb.isize) + " / s_fsize "
                    + std::to_string(sb.fsize) + " unplausibel");
    if (sb.fsize > dev.blocks())
        return nein("Superblock: s_fsize " + std::to_string(sb.fsize) + " > "
                    + std::to_string(dev.blocks()) + " Bloecke des Datentraegers");
    if (sb.nfree < 0 || sb.nfree > kNicFree || sb.ninode < 0 || sb.ninode > kNicInod)
        return nein("Superblock: s_nfree/s_ninode ausserhalb 0…50/0…100");
    if (sb.tfree > sb.fsize - sb.isize || sb.tinode > sb.inodes())
        return nein("Superblock: Zaehler s_tfree/s_tinode zu gross");
    for (int i = 0; i < sb.nfree; ++i) {
        const uint32_t f = sb.free[static_cast<size_t>(i)];
        if (i == 0 && f == 0) continue;
        if (f < sb.isize || f >= sb.fsize)
            return nein("Superblock: s_free[" + std::to_string(i) + "] = " + std::to_string(f)
                        + " ausserhalb des Datenbereichs");
    }
    Leser l = [&dev](uint32_t bn, uint8_t* x) { return dev.readBlock(bn, x); };
    Dinode root;
    if (!inodeLesen(l, sb, kRootIno, root)) return nein("Wurzel-Inode nicht lesbar");
    if ((root.mode & IFMT) != IFDIR) return nein("Inode 2 ist kein Verzeichnis");
    if (root.size < 2 * kDirEintrag || root.size % kDirEintrag)
        return nein("Wurzelverzeichnis: Groesse " + std::to_string(root.size) + " unplausibel");
    if (root.addr[0] < sb.isize || root.addr[0] >= sb.fsize)
        return nein("Wurzelverzeichnis: erster Block ausserhalb");
    if (!l(root.addr[0], b)) return nein("Wurzelverzeichnis nicht lesbar");
    if (be16(b) != kRootIno || festText(b + 2, kDirSiz) != "."
        || be16(b + 16) != kRootIno || festText(b + 18, kDirSiz) != "..")
        return nein("Wurzelverzeichnis beginnt nicht mit '.' und '..' auf Inode 2");
    return true;
}

std::unique_ptr<WegaFileSystem> WegaFileSystem::mount(std::unique_ptr<WegaBlockDev> dev,
                                                      std::string& why) {
    if (!dev) { why = "kein Blockgeraet"; return nullptr; }
    if (!looksLikeWega(*dev, &why)) return nullptr;
    return std::unique_ptr<WegaFileSystem>(new WegaFileSystem(std::move(dev)));
}

std::unique_ptr<WegaFileSystem> WegaFileSystem::format(std::unique_ptr<WegaBlockDev> dev,
                                                       const std::string& label,
                                                       std::string& err, uint32_t bloecke,
                                                       int m, int n, uint32_t zeit) {
    if (!dev) { err = "kein Blockgeraet"; return nullptr; }
    std::unique_ptr<WegaFileSystem> fs(new WegaFileSystem(std::move(dev)));
    if (zeit) fs->setClock([zeit] { return zeit; });
    if (bloecke == 0) bloecke = fs->dev_->blocks();
    if (bloecke > fs->dev_->blocks() || bloecke < 16) {
        err = "Dateisystemgroesse " + std::to_string(bloecke) + " passt nicht zum Datentraeger";
        return nullptr;
    }
    if (n <= 0 || n >= 1000) n = 1000;
    if (m <= 0 || m > n) m = 3;
    const uint32_t t = fs->jetzt();

    Superblock sb;
    sb.fsize = bloecke;
    uint32_t ib = bloecke / 25;
    if (ib == 0) ib = 1;
    if (ib > 65500 / kInodesProBlock) ib = 65500 / kInodesProBlock;
    sb.isize = static_cast<uint16_t>(ib + 2);
    sb.m = static_cast<int16_t>(m);
    sb.n = static_cast<int16_t>(n);
    sb.fname = label.substr(0, 6);
    sb.roh.fill(0);

    uint8_t null[kBlock] = {};
    for (uint32_t b = 2; b < sb.isize; ++b)
        if (!fs->dev_->writeBlock(b, null)) { err = "Inode-Liste nicht schreibbar"; return nullptr; }
    sb.tinode = static_cast<uint16_t>(sb.inodes());

    // Arbeitskopie ohne vorhandenen Superblock: direkt mit dem neuen arbeiten.
    Tx tx(*fs);
    tx.sb = sb;

    // bflist(): Verschraenkungsmuster, dann rueckwaerts in die Freiliste.
    std::vector<int> adr(static_cast<size_t>(n));
    {
        std::vector<char> flg(static_cast<size_t>(n), 0);
        int i = 0;
        for (int j = 0; j < n; ++j) {
            while (flg[static_cast<size_t>(i)]) i = (i + 1) % n;
            adr[static_cast<size_t>(j)] = i + 1;
            flg[static_cast<size_t>(i)] = 1;
            i = (i + m) % n;
        }
    }
    // bfree(0) markiert das Ende der Kette (s_nfree = 1, s_free[0] = 0)
    tx.sb.nfree = 0;
    tx.sb.tfree = 0;
    tx.sb.free[0] = 0; tx.sb.nfree = 1; tx.sb.tfree = 1;
    int64_t d = static_cast<int64_t>(sb.fsize) - 1;
    while (d % n) ++d;
    for (; d > 0; d -= n)
        for (int i = 0; i < n; ++i) {
            const int64_t f = d - adr[static_cast<size_t>(i)];
            if (f < static_cast<int64_t>(sb.fsize) && f >= sb.isize)
                tx.freeBlock(static_cast<uint32_t>(f));
        }
    // bfree(0) zaehlt die Endmarke mit (s_tfree++ in sa.mkfs) — die Null ist kein
    // Block, aber der Kern gleicht das nie aus: auf ALLEN Lieferdisketten ist
    // s_tfree = Laenge der Freiliste + 1.  Das wird hier genauso gemacht.

    // Inode 1: Bad-Block-Datei (leer), Inode 2: Wurzel
    Dinode bad;
    bad.mode = IFREG; bad.atime = bad.mtime = bad.ctime = t;
    tx.setzeInode(1, bad);
    --tx.sb.tinode;
    Dinode root;
    root.mode = IFDIR | 0777; root.nlink = 2;
    root.atime = root.mtime = root.ctime = t;
    std::vector<uint8_t> inhalt(2 * kDirEintrag, 0);
    put16(inhalt.data(), kRootIno); inhalt[2] = '.';
    put16(inhalt.data() + 16, kRootIno); inhalt[18] = '.'; inhalt[19] = '.';
    if (!tx.fuellen(root, inhalt)) { err = fs->lastError(); return nullptr; }
    tx.setzeInode(kRootIno, root);
    --tx.sb.tinode;
    tx.sb.mach = 0;

    // Superblock erstmals schreiben, dann wie gewohnt festschreiben
    uint8_t sbb[kBlock] = {};
    tx.sb.in(sbb);
    if (!fs->dev_->writeBlock(1, sbb)) { err = "Superblock nicht schreibbar"; return nullptr; }
    if (!tx.commit()) { err = fs->lastError(); return nullptr; }
    if (zeit) fs->setClock({});
    return fs;
}

// ═══ FileSystem ═════════════════════════════════════════════════════════════

std::vector<FileEntry> WegaFileSystem::list() const {
    std::vector<FileEntry> out;
    const Superblock sb = superblock();
    const Leser l = geraeteLeser();
    std::set<uint32_t> besucht;
    std::function<void(uint32_t, const std::string&)> ab = [&](uint32_t dir, const std::string& pfad) {
        if (!besucht.insert(dir).second) return;           // Schleife im Baum
        std::vector<DirEintrag> ein;
        std::string err;
        if (!verzeichnisLesen(l, sb, dir, ein, err)) return;
        std::sort(ein.begin(), ein.end(),
                  [](const DirEintrag& a, const DirEintrag& b) { return a.name < b.name; });
        for (const DirEintrag& e : ein) {
            if (e.ino == 0 || e.name == "." || e.name == "..") continue;
            FileEntry f;
            f.name = pfad.empty() ? e.name : pfad + "/" + e.name;
            Dinode d;
            if (!inodeLesen(l, sb, e.ino, d)) { f.damaged = true; out.push_back(f); continue; }
            f.size       = hatBloecke(d.mode) ? d.size : 0;
            f.type       = artVon(d.mode);
            f.attributes = modeText(d.mode);
            f.date       = datumText(d.mtime);
            f.unix_mode  = d.mode;
            f.unix_uid   = static_cast<uint16_t>(d.uid);
            f.unix_gid   = static_cast<uint16_t>(d.gid);
            f.unix_nlink = d.nlink;
            f.unix_inode = e.ino;
            f.unix_mtime = d.mtime;
            if ((d.mode & IFMT) == IFCHR || (d.mode & IFMT) == IFBLK) {
                // Geraetedatei: im ersten Adressfeld steht major<<8 | minor
                f.created = std::to_string(d.addr[0] >> 8 & 0xFF) + ","
                          + std::to_string(d.addr[0] & 0xFF);
            }
            if (d.mode == 0) f.damaged = true;
            out.push_back(f);
            if ((d.mode & IFMT) == IFDIR) ab(e.ino, f.name);
        }
    };
    ab(kRootIno, "");
    return out;
}

bool WegaFileSystem::firstSector(const std::string& name, FsRecoverOrt& out) const {
    const uint32_t ino = lookup(name);
    if (!ino) return false;
    Dinode d;
    if (!readInode(ino, d) || !hatBloecke(d.mode) || d.addr[0] == 0) return false;
    return dev_->ort(d.addr[0], out);
}

bool WegaFileSystem::read(const std::string& name, std::vector<uint8_t>& out) {
    const Superblock sb = superblock();
    const Leser l = geraeteLeser();
    const uint32_t ino = suche(l, sb, name, nullptr, nullptr);
    if (!ino) return fail("Datei nicht gefunden: " + name);
    Dinode d;
    if (!inodeLesen(l, sb, ino, d)) return fail("Inode nicht lesbar: " + name);
    if ((d.mode & IFMT) == IFDIR) return fail(name + " ist ein Verzeichnis");
    if (!hatBloecke(d.mode))
        return fail(name + " ist eine Geraetedatei und hat keinen Inhalt");
    std::string err;
    if (!dateiLesen(l, d, sb.fsize, out, err)) return fail(name + ": " + err);
    return true;
}

namespace {
bool nameGueltig(const std::string& n, std::string& why) {
    if (n.empty() || n == "." || n == "..") { why = "ungueltiger Name '" + n + "'"; return false; }
    if (n.size() > static_cast<size_t>(kDirSiz)) {
        why = "Name '" + n + "' ist laenger als 14 Zeichen (WEGA/System III)";
        return false;
    }
    if (n.find('\0') != std::string::npos) { why = "Nullbyte im Namen"; return false; }
    return true;
}
} // namespace

bool WegaFileSystem::makeDirectory(const std::string& name) {
    Tx tx(*this);
    if (!tx.start()) return false;
    const Leser l = tx.leser();
    const std::vector<std::string> t = teile(name);
    if (t.empty()) return true;                         // Wurzel gibt es schon
    uint32_t dir = kRootIno;
    const uint32_t jetzt_ = jetzt();
    for (const std::string& teil : t) {
        std::string why;
        if (!nameGueltig(teil, why)) return fail(why);
        std::vector<DirEintrag> ein;
        std::string err;
        if (!verzeichnisLesen(l, tx.sb, dir, ein, err)) return fail(err);
        uint32_t da = 0;
        for (const DirEintrag& e : ein) if (e.ino && e.name == teil) { da = e.ino; break; }
        if (da) {
            Dinode d;
            if (!tx.inode(da, d) || (d.mode & IFMT) != IFDIR)
                return fail("'" + teil + "' gibt es schon und ist kein Verzeichnis");
            dir = da;
            continue;
        }
        // Neues Verzeichnis: Inode, Block mit . und .., Eintrag im Elter, Elter-nlink+1
        uint32_t ino = 0;
        if (!tx.allocInode(ino)) return false;
        Dinode neu;
        neu.mode = IFDIR | 0755; neu.nlink = 2;
        neu.atime = neu.mtime = neu.ctime = jetzt_;
        std::vector<uint8_t> inhalt(2 * kDirEintrag, 0);
        put16(inhalt.data(), static_cast<uint16_t>(ino)); inhalt[2] = '.';
        put16(inhalt.data() + 16, static_cast<uint16_t>(dir)); inhalt[18] = '.'; inhalt[19] = '.';
        if (!tx.fuellen(neu, inhalt) || !tx.setzeInode(ino, neu)) return false;

        Dinode elter;
        if (!tx.inode(dir, elter)) return fail("Elternverzeichnis nicht lesbar");
        std::vector<uint8_t> roh;
        if (!dateiLesen(l, elter, tx.sb.fsize, roh, err)) return fail(err);
        size_t platz = roh.size();
        for (size_t k = 0; k + kDirEintrag <= roh.size(); k += kDirEintrag)
            if (be16(roh.data() + k) == 0) { platz = k; break; }
        if (platz == roh.size()) roh.resize(roh.size() + kDirEintrag, 0);
        std::memset(roh.data() + platz, 0, kDirEintrag);
        put16(roh.data() + platz, static_cast<uint16_t>(ino));
        std::memcpy(roh.data() + platz + 2, teil.data(), teil.size());
        if (!tx.kuerzen(elter) || !tx.fuellen(elter, roh)) return false;
        ++elter.nlink;
        elter.mtime = elter.ctime = jetzt_;
        if (!tx.setzeInode(dir, elter)) return false;
        dir = ino;
    }
    return tx.commit();
}

bool WegaFileSystem::write(const std::string& name, const std::vector<uint8_t>& data,
                           const WriteOptions& opt) {
    const std::vector<std::string> t = teile(name);
    if (t.empty()) return fail("kein Dateiname");
    std::string why;
    for (const std::string& teil : t) if (!nameGueltig(teil, why)) return fail(why);
    if (data.size() > kMaxDatei) return fail("Datei zu gross fuer WEGA");

    // Fehlende Verzeichnisse vorab anlegen (eigene Transaktion — ein Fehlschlag
    // danach laesst hoechstens leere Verzeichnisse zurueck, keine halbe Datei).
    if (t.size() > 1) {
        std::string elterpfad;
        for (size_t i = 0; i + 1 < t.size(); ++i) elterpfad += (i ? "/" : "") + t[i];
        if (lookup(elterpfad) == 0 && !makeDirectory(elterpfad)) return false;
    }

    Tx tx(*this);
    if (!tx.start()) return false;
    const Leser l = tx.leser();
    uint32_t elter = 0;
    std::string blatt;
    const uint32_t vorhanden = suche(l, tx.sb, name, &elter, &blatt);
    if (!vorhanden && !elter) return fail("Verzeichnis zu '" + name + "' nicht gefunden");
    const uint32_t jetzt_ = jetzt();
    const uint32_t mzeit = opt.wega_mtime ? opt.wega_mtime : jetzt_;

    Dinode d;
    uint32_t ino = vorhanden;
    if (vorhanden) {
        if (!tx.inode(ino, d)) return fail("Inode nicht lesbar");
        if ((d.mode & IFMT) == IFDIR) return fail(name + " ist ein Verzeichnis");
        if (!opt.overwrite) return fail("Datei existiert bereits: " + name);
        if (!hatBloecke(d.mode)) return fail(name + " ist eine Geraetedatei");
        if (!tx.kuerzen(d)) return false;
        if (opt.wega_mode_gesetzt) d.mode = static_cast<uint16_t>((d.mode & IFMT) | (opt.wega_mode & 07777));
        if (opt.wega_mode_gesetzt || opt.wega_uid || opt.wega_gid) {
            d.uid = static_cast<int16_t>(opt.wega_uid);
            d.gid = static_cast<int16_t>(opt.wega_gid);
        }
    } else {
        if (!tx.allocInode(ino)) return false;
        d = Dinode{};
        d.mode  = static_cast<uint16_t>(IFREG | (opt.wega_mode_gesetzt ? (opt.wega_mode & 07777) : 0644));
        d.nlink = 1;
        d.uid   = static_cast<int16_t>(opt.wega_uid);
        d.gid   = static_cast<int16_t>(opt.wega_gid);
    }
    if (!tx.fuellen(d, data)) return false;
    d.atime = mzeit; d.mtime = mzeit; d.ctime = jetzt_;
    if (!tx.setzeInode(ino, d)) return false;

    if (!vorhanden) {
        Dinode e;
        if (!tx.inode(elter, e)) return fail("Elternverzeichnis nicht lesbar");
        std::vector<uint8_t> roh;
        std::string err;
        if (!dateiLesen(l, e, tx.sb.fsize, roh, err)) return fail(err);
        size_t platz = roh.size();
        for (size_t k = 0; k + kDirEintrag <= roh.size(); k += kDirEintrag)
            if (be16(roh.data() + k) == 0) { platz = k; break; }
        if (platz == roh.size()) roh.resize(roh.size() + kDirEintrag, 0);
        std::memset(roh.data() + platz, 0, kDirEintrag);
        put16(roh.data() + platz, static_cast<uint16_t>(ino));
        std::memcpy(roh.data() + platz + 2, blatt.data(), blatt.size());
        if (!tx.kuerzen(e) || !tx.fuellen(e, roh)) return false;
        e.mtime = e.ctime = jetzt_;
        if (!tx.setzeInode(elter, e)) return false;
    }
    return tx.commit();
}

bool WegaFileSystem::erase(const std::string& name) {
    Tx tx(*this);
    if (!tx.start()) return false;
    const Leser l = tx.leser();
    uint32_t elter = 0;
    std::string blatt;
    const uint32_t ino = suche(l, tx.sb, name, &elter, &blatt);
    if (!ino) return fail("Datei nicht gefunden: " + name);
    if (ino == kRootIno || !elter) return fail("Die Wurzel laesst sich nicht loeschen");
    Dinode d;
    if (!tx.inode(ino, d)) return fail("Inode nicht lesbar");
    const bool ist_dir = (d.mode & IFMT) == IFDIR;
    std::string err;
    if (ist_dir) {
        std::vector<DirEintrag> ein;
        if (!verzeichnisLesen(l, tx.sb, ino, ein, err)) return fail(err);
        for (const DirEintrag& e : ein)
            if (e.ino && e.name != "." && e.name != "..")
                return fail("Verzeichnis " + name + " ist nicht leer");
    }
    // Eintrag im Elter austragen (d_ino = 0, der Name bleibt stehen — wie `unlink`)
    Dinode e;
    if (!tx.inode(elter, e)) return fail("Elternverzeichnis nicht lesbar");
    std::vector<uint8_t> roh;
    if (!dateiLesen(l, e, tx.sb.fsize, roh, err)) return fail(err);
    bool weg = false;
    for (size_t k = 0; k + kDirEintrag <= roh.size(); k += kDirEintrag)
        if (be16(roh.data() + k) == ino && festText(roh.data() + k + 2, kDirSiz) == blatt) {
            put16(roh.data() + k, 0);
            weg = true;
            break;
        }
    if (!weg) return fail("Verzeichniseintrag nicht gefunden: " + name);
    // Nur den betroffenen Block zurueckschreiben — Groesse und Bloecke bleiben.
    std::vector<uint32_t> bl;
    if (!dateiBloecke(l, e, tx.sb.fsize, bl, nullptr, err)) return fail(err);
    for (size_t i = 0; i < bl.size(); ++i) {
        if (!bl[i]) continue;
        uint8_t b[kBlock] = {};
        const size_t von = i * kBlock;
        std::memcpy(b, roh.data() + von, std::min<size_t>(kBlock, roh.size() - von));
        uint8_t alt[kBlock];
        if (tx.lies(bl[i], alt) && std::memcmp(alt, b, kBlock) != 0) tx.schreib(bl[i], b);
    }
    const uint32_t jetzt_ = jetzt();
    if (ist_dir && e.nlink > 0) --e.nlink;
    e.mtime = e.ctime = jetzt_;
    if (!tx.setzeInode(elter, e)) return false;

    if (ist_dir) d.nlink = 0;
    else         --d.nlink;
    if (d.nlink <= 0) {
        if (!tx.kuerzen(d)) return false;
        tx.setzeInode(ino, Dinode{});
        tx.freeInode(ino);
    } else {
        d.ctime = jetzt_;
        tx.setzeInode(ino, d);
    }
    return tx.commit();
}

bool WegaFileSystem::wouldFit(const std::vector<PlannedFile>& files, FitReport& out) const {
    out = FitReport{};
    const Superblock sb = superblock();
    const Leser l = geraeteLeser();
    uint64_t bedarf = 0, gutschrift = 0;
    int inodes = 0;
    std::set<std::string> neue_dirs;
    for (const PlannedFile& f : files) {
        bedarf += blockBedarf(f.size);
        const uint32_t ino = suche(l, sb, f.name, nullptr, nullptr);
        if (ino) {
            Dinode d;
            if (inodeLesen(l, sb, ino, d) && hatBloecke(d.mode))
                gutschrift += blockBedarf(d.size);
        } else {
            ++inodes;
            // Zwischenverzeichnisse, die es noch nicht gibt: je eine Inode + ein Block
            const std::vector<std::string> t = teile(f.name);
            std::string p;
            for (size_t i = 0; i + 1 < t.size(); ++i) {
                p += (i ? "/" : "") + t[i];
                if (!suche(l, sb, p, nullptr, nullptr) && neue_dirs.insert(p).second) {
                    ++inodes;
                    bedarf += 1;
                }
            }
            bedarf += 1;          // Reserve: das Elternverzeichnis kann einen Block wachsen
        }
    }
    // s_tfree zaehlt die Endmarke der Freiliste mit (s. check) — vorsichtig einen
    // weniger rechnen, sonst „passt" eine Datei, deren letzter Block fehlt.
    const uint64_t frei = sb.tfree ? sb.tfree - 1u : 0u;
    out.needed    = bedarf * kBlock;
    out.available = (frei + gutschrift) * kBlock;
    out.dir_needed = inodes;
    out.dir_free   = sb.tinode;
    out.fits = bedarf <= frei + gutschrift && inodes <= static_cast<int>(sb.tinode);
    if (!out.fits) {
        out.detail = bedarf > frei + gutschrift
            ? "Es fehlen " + std::to_string((bedarf - frei - gutschrift) * kBlock / 1024)
              + " KB (" + std::to_string(bedarf) + " Bloecke noetig, "
              + std::to_string(frei + gutschrift) + " frei)"
            : "Es fehlen Inodes (" + std::to_string(inodes) + " noetig, "
              + std::to_string(sb.tinode) + " frei)";
    }
    return true;
}

bool WegaFileSystem::mkfs() {
    const Superblock alt = superblock();
    std::string err;
    auto neu = format(std::move(dev_), alt.fname, err, alt.fsize ? alt.fsize : 0,
                      alt.m > 0 ? alt.m : 1, alt.n > 0 ? alt.n : 72);
    if (!neu) return fail(err);
    dev_ = std::move(neu->dev_);
    return true;
}

FsInfo WegaFileSystem::info() const {
    FsInfo i;
    const Superblock sb = superblock();
    i.label = sb.fname;
    if (!sb.fpack.empty() && sb.fpack != sb.fname) i.label += i.label.empty() ? sb.fpack : " " + sb.fpack;
    i.total_bytes = static_cast<uint64_t>(sb.fsize - std::min<uint32_t>(sb.fsize, sb.isize)) * kBlock;
    i.free_bytes  = static_cast<uint64_t>(sb.tfree) * kBlock;
    i.used_bytes  = i.total_bytes > i.free_bytes ? i.total_bytes - i.free_bytes : 0;
    int n = 0;
    for (const FileEntry& e : list()) if (e.type != "d") ++n;
    i.files = n;
    return i;
}

// ═══ Pruefung (schreibt NIE) ════════════════════════════════════════════════

FsCheckReport WegaFileSystem::check(FsCheckLevel level, bool nachladen) const {
    (void)nachladen;
    FsCheckReport r;
    r.level = level;
    FsFindings f(r);
    const Leser l = geraeteLeser();
    auto ortVon = [&](FsFinding& x, uint32_t bn) {
        FsRecoverOrt o;
        if (dev_->ort(bn, o)) { x.cyl = o.cyl; x.head = o.head; x.sector_index = o.sector; }
    };

    r.schritt("wega.schritt.superblock", "Superblock (Block 1)");
    uint8_t b[kBlock];
    if (!dev_->readBlock(1, b)) {
        f.add("wega.super.lesen", FsSeverity::Fehler, FsLayer::Verwaltung, "Block 1",
              "Der Superblock ist nicht lesbar");
        return r;
    }
    const Superblock sb = Superblock::aus(b);
    const uint32_t n_ino = sb.inodes();
    if (sb.isize < 3 || sb.fsize <= sb.isize || sb.fsize > dev_->blocks()) {
        f.add("wega.super.groesse", FsSeverity::Fehler, FsLayer::Verwaltung, "Superblock",
              "s_isize " + std::to_string(sb.isize) + " / s_fsize " + std::to_string(sb.fsize)
              + " passen nicht zum Datentraeger (" + std::to_string(dev_->blocks()) + " Bloecke)");
        return r;
    }
    if (sb.nfree < 0 || sb.nfree > kNicFree)
        f.add("wega.super.nfree", FsSeverity::Fehler, FsLayer::Verwaltung, "Superblock",
              "s_nfree = " + std::to_string(sb.nfree) + " (erlaubt 0…50) — der Kern meldet "
              "'Bad free count' und vergibt nichts mehr");
    if (sb.ninode < 0 || sb.ninode > kNicInod)
        f.add("wega.super.ninode", FsSeverity::Fehler, FsLayer::Verwaltung, "Superblock",
              "s_ninode = " + std::to_string(sb.ninode) + " (erlaubt 0…100)");
    for (int i = 0; i < std::clamp<int>(sb.ninode, 0, kNicInod); ++i) {
        const uint16_t x = sb.inode[static_cast<size_t>(i)];
        if (x < 1 || x > n_ino)
            f.add("wega.super.inodeliste", FsSeverity::Warnung, FsLayer::Verwaltung, "Superblock",
                  "s_inode[" + std::to_string(i) + "] = " + std::to_string(x)
                  + " liegt ausserhalb der Inode-Liste (1…" + std::to_string(n_ino) + ")");
    }

    r.schritt("wega.schritt.inodes", "Inode-Liste (" + std::to_string(n_ino) + " Inodes)");
    std::vector<Dinode> inode(n_ino + 1);
    uint32_t freie_inodes = 0;
    for (uint32_t i = 1; i <= n_ino; ++i) {
        if (!inodeLesen(l, sb, i, inode[i])) {
            auto& x = f.add("wega.inode.lesen", FsSeverity::Fehler, FsLayer::Verwaltung,
                            "Inode " + std::to_string(i), "Inode-Block nicht lesbar");
            ortVon(x, 2 + (i - 1) / kInodesProBlock);
            continue;
        }
        const Dinode& d = inode[i];
        if (d.mode == 0) { ++freie_inodes; continue; }
        if (!bekannteArt(d.mode))
            f.add("wega.inode.art", FsSeverity::Fehler, FsLayer::Verwaltung,
                  "Inode " + std::to_string(i),
                  "unbekannte Dateiart (Modus 0" + [&] { char t[8]; std::snprintf(t, sizeof t, "%o", d.mode); return std::string(t); }() + ")");
    }
    if ((inode[kRootIno].mode & IFMT) != IFDIR)
        f.add("wega.super.wurzel", FsSeverity::Fehler, FsLayer::Verwaltung, "Inode 2",
              "Die Wurzel (Inode 2) ist kein Verzeichnis");
    if (freie_inodes != sb.tinode)
        f.add("wega.inode.zaehler", FsSeverity::Warnung, FsLayer::Verwaltung, "Superblock",
              "s_tinode = " + std::to_string(sb.tinode) + ", die Inode-Liste hat aber "
              + std::to_string(freie_inodes) + " freie Inodes");
    for (int i = 0; i < std::clamp<int>(sb.ninode, 0, kNicInod); ++i) {
        const uint16_t x = sb.inode[static_cast<size_t>(i)];
        // Ein belegter Eintrag im Zwischenspeicher ist harmlos — der Kern prueft
        // `i_mode == 0` und sucht weiter ("Inode was allocated after all").
        if (x >= 1 && x <= n_ino && inode[x].mode != 0)
            f.add("wega.super.inodecache", FsSeverity::Info, FsLayer::Verwaltung, "Superblock",
                  "s_inode nennt die belegte Inode " + std::to_string(x)
                  + " als frei (der Kern uebergeht sie)");
    }

    if (level == FsCheckLevel::Schnell) {
        r.schrittEntfaellt("wega.schritt.freiliste", "Freiliste", "nur bei der Vollpruefung");
        r.schrittEntfaellt("wega.schritt.baum", "Verzeichnisbaum", "nur bei der Vollpruefung");
        r.schrittEntfaellt("wega.schritt.belegung", "Blockbelegung", "nur bei der Vollpruefung");
        r.schrittEnde();
        return r;
    }

    // ── Freiliste ────────────────────────────────────────────────────────────
    r.schritt("wega.schritt.freiliste", "Freiliste (Ankerbloecke)");
    std::set<uint32_t> frei;
    bool frei_vollstaendig = true;
    {
        int16_t n = sb.nfree;
        std::array<uint32_t, kNicFree> liste = sb.free;
        std::set<uint32_t> anker;
        uint32_t woher = 1;
        for (;;) {
            if (n < 0 || n > kNicFree) {
                auto& x = f.add("wega.frei.kette", FsSeverity::Fehler, FsLayer::Dateien,
                                "Block " + std::to_string(woher),
                                "Freilistenblock mit df_nfree = " + std::to_string(n));
                ortVon(x, woher);
                frei_vollstaendig = false;
                break;
            }
            for (int i = 0; i < n; ++i) {
                const uint32_t bn = liste[static_cast<size_t>(i)];
                if (i == 0 && bn == 0) continue;               // Kettenende
                if (bn < sb.isize || bn >= sb.fsize) {
                    f.add("wega.frei.ausserhalb", FsSeverity::Fehler, FsLayer::Dateien,
                          "Block " + std::to_string(woher),
                          "Die Freiliste nennt Block " + std::to_string(bn)
                          + " ausserhalb des Datenbereichs");
                    continue;
                }
                if (!frei.insert(bn).second)
                    f.add("wega.frei.doppelt", FsSeverity::Gefahr, FsLayer::Dateien,
                          "Block " + std::to_string(bn),
                          "Block " + std::to_string(bn) + " steht zweimal in der Freiliste — "
                          "er wuerde zweimal vergeben");
            }
            const uint32_t naechster = n > 0 ? liste[0] : 0;
            if (naechster == 0) break;
            if (naechster < sb.isize || naechster >= sb.fsize) { frei_vollstaendig = false; break; }
            if (!anker.insert(naechster).second) {
                f.add("wega.frei.kette", FsSeverity::Fehler, FsLayer::Dateien,
                      "Block " + std::to_string(naechster), "Die Freiliste laeuft im Kreis");
                frei_vollstaendig = false;
                break;
            }
            uint8_t fb[kBlock];
            if (!l(naechster, fb)) {
                f.add("wega.frei.kette", FsSeverity::Fehler, FsLayer::Dateien,
                      "Block " + std::to_string(naechster), "Freilistenblock nicht lesbar");
                frei_vollstaendig = false;
                break;
            }
            woher = naechster;
            n = static_cast<int16_t>(be16(fb));
            for (int i = 0; i < kNicFree; ++i) liste[static_cast<size_t>(i)] = be32(fb + 2 + 4 * i);
        }
    }
    // s_tfree = Laenge + 1 ist der Normalfall: sa.mkfs zaehlt die Endmarke 0 mit
    // (`bfree(0)`), und der Kern gleicht das nie aus — so steht es auf jeder
    // Lieferdiskette.  Beides ist richtig; erst eine andere Zahl ist ein Befund.
    if (frei_vollstaendig && frei.size() != sb.tfree && frei.size() + 1 != sb.tfree)
        f.add("wega.frei.zaehler", FsSeverity::Warnung, FsLayer::Dateien, "Superblock",
              "s_tfree = " + std::to_string(sb.tfree) + ", die Freiliste traegt aber "
              + std::to_string(frei.size()) + " Bloecke");

    // ── Verzeichnisbaum ──────────────────────────────────────────────────────
    r.schritt("wega.schritt.baum", "Verzeichnisbaum ab der Wurzel");
    std::vector<int> verweise(n_ino + 1, 0);
    std::map<uint32_t, std::string> pfad_von;
    {
        std::set<uint32_t> besucht;
        std::function<void(uint32_t, uint32_t, const std::string&)> ab =
            [&](uint32_t dir, uint32_t elter, const std::string& pfad) {
            if (!besucht.insert(dir).second) return;
            std::vector<DirEintrag> ein;
            std::string err;
            if (!verzeichnisLesen(l, sb, dir, ein, err)) {
                f.add("wega.dir.lesen", FsSeverity::Fehler, FsLayer::Dateien,
                      pfad.empty() ? "/" : pfad, "Verzeichnis nicht lesbar: " + err);
                return;
            }
            const Dinode& dd = inode[dir];
            const std::string wo = pfad.empty() ? "/" : pfad;
            if (dd.size % kDirEintrag)
                f.add("wega.dir.groesse", FsSeverity::Warnung, FsLayer::Dateien, wo,
                      "Verzeichnisgroesse " + std::to_string(dd.size)
                      + " ist kein Vielfaches von 16");
            bool punkt = false, punktpunkt = false;
            for (const DirEintrag& e : ein) {
                if (e.ino == 0) continue;
                if (e.ino > n_ino) {
                    f.add("wega.dir.inode_ausserhalb", FsSeverity::Fehler, FsLayer::Dateien,
                          wo + "/" + e.name, "Eintrag nennt Inode " + std::to_string(e.ino)
                          + " ausserhalb der Inode-Liste");
                    continue;
                }
                ++verweise[e.ino];
                if (e.name == ".")  { punkt = (e.ino == dir); continue; }
                if (e.name == "..") { punktpunkt = (e.ino == elter); continue; }
                const std::string kind = pfad.empty() ? e.name : pfad + "/" + e.name;
                if (inode[e.ino].mode == 0) {
                    f.add("wega.dir.freier_inode", FsSeverity::Fehler, FsLayer::Dateien, kind,
                          "Eintrag verweist auf die freie Inode " + std::to_string(e.ino));
                    continue;
                }
                if (!pfad_von.count(e.ino)) pfad_von[e.ino] = kind;
                if ((inode[e.ino].mode & IFMT) == IFDIR) ab(e.ino, dir, kind);
            }
            if (!punkt || !punktpunkt)
                f.add("wega.dir.punkt", FsSeverity::Warnung, FsLayer::Dateien, wo,
                      std::string("Eintrag ") + (!punkt ? "'.'" : "'..'")
                      + " fehlt oder zeigt woandershin");
        };
        ab(kRootIno, kRootIno, "");
    }

    // ── Blockbelegung ────────────────────────────────────────────────────────
    r.schritt("wega.schritt.belegung", "Blockbelegung (Dateien gegen Freiliste)");
    std::map<uint32_t, uint32_t> besitzer;   // Block → Inode
    for (uint32_t i = 1; i <= n_ino; ++i) {
        const Dinode& d = inode[i];
        if (d.mode == 0 || !hatBloecke(d.mode)) continue;
        std::vector<uint32_t> daten, ind;
        std::string err;
        const std::string wer = pfad_von.count(i) ? pfad_von[i] : "Inode " + std::to_string(i);
        if (!dateiBloecke(l, d, sb.fsize, daten, &ind, err)) {
            f.add("wega.block.ausserhalb", FsSeverity::Fehler, FsLayer::Dateien, wer, err);
            continue;
        }
        std::vector<uint32_t> alle = ind;
        alle.insert(alle.end(), daten.begin(), daten.end());
        for (uint32_t bn : alle) {
            if (bn == 0) continue;
            if (bn < sb.isize || bn >= sb.fsize) {
                f.add("wega.block.ausserhalb", FsSeverity::Fehler, FsLayer::Dateien, wer,
                      "Block " + std::to_string(bn) + " liegt ausserhalb des Datenbereichs ("
                      + std::to_string(sb.isize) + "…" + std::to_string(sb.fsize - 1) + ")");
                continue;
            }
            auto [it, neu] = besitzer.emplace(bn, i);
            if (!neu) {
                const std::string anderer = pfad_von.count(it->second)
                    ? pfad_von[it->second] : "Inode " + std::to_string(it->second);
                auto& x = f.add("wega.block.doppelt", FsSeverity::Gefahr, FsLayer::Dateien, wer,
                                "Block " + std::to_string(bn) + " gehoert zugleich " + anderer);
                ortVon(x, bn);
            } else if (frei.count(bn)) {
                auto& x = f.add("wega.block.frei_belegt", FsSeverity::Gefahr, FsLayer::Dateien, wer,
                                "Block " + std::to_string(bn) + " gehoert der Datei und steht "
                                "zugleich in der Freiliste — der naechste Schreibvorgang "
                                "ueberschreibt ihn");
                ortVon(x, bn);
            }
        }
    }
    if (frei_vollstaendig) {
        uint32_t verloren = 0, erster = 0;
        for (uint32_t bn = sb.isize; bn < sb.fsize; ++bn)
            if (!frei.count(bn) && !besitzer.count(bn)) { if (!verloren) erster = bn; ++verloren; }
        if (verloren) {
            auto& x = f.add("wega.block.verloren", FsSeverity::Warnung, FsLayer::Dateien,
                            "Block " + std::to_string(erster),
                            std::to_string(verloren) + " Bloecke sind weder belegt noch frei "
                            "(erster: " + std::to_string(erster) + ") — Platz geht verloren");
            ortVon(x, erster);
        }
    }

    // ── Linkzaehler und verwaiste Inodes ─────────────────────────────────────
    for (uint32_t i = 1; i <= n_ino; ++i) {
        const Dinode& d = inode[i];
        if (d.mode == 0 || i == 1) continue;          // Inode 1 = Bad-Block-Datei, ohne Namen
        const std::string wer = pfad_von.count(i) ? pfad_von[i]
                              : i == kRootIno ? "/" : "Inode " + std::to_string(i);
        if (verweise[i] == 0) {
            f.add("wega.inode.verwaist", FsSeverity::Warnung, FsLayer::Dateien, wer,
                  "Inode " + std::to_string(i) + " ist belegt (" + modeText(d.mode)
                  + ", " + std::to_string(d.size) + " Byte), aber kein Verzeichnis nennt sie");
        } else if (verweise[i] != d.nlink) {
            f.add("wega.inode.links", FsSeverity::Warnung, FsLayer::Dateien, wer,
                  "Linkzaehler " + std::to_string(d.nlink) + ", tatsaechlich "
                  + std::to_string(verweise[i]) + " Verweise");
        }
    }
    r.schrittEnde();
    r.sortieren();
    r.begrenzen(20);
    return r;
}

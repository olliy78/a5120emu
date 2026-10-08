/**
 * @file wega_fs.h
 * @brief WEGA-Dateisystem (System III UNIX des P8000) — lesen, schreiben, pruefen.
 *
 * WEGA ist das UNIX des Robotron P8000 (Z8001, also **big endian**).  Sein
 * Dateisystem ist das System-III-Dateisystem mit 512-Byte-Bloecken, ohne Magic:
 *
 *   - Block 0 Boot-Block, **Block 1 Superblock** (`struct filsys`),
 *   - Bloecke 2 … s_isize−1 die Inode-Liste (8 Inodes zu 64 Byte je Block,
 *     Inode 1 = Bad-Block-Datei, **Inode 2 = Wurzel**),
 *   - ab s_isize die Datenbloecke bis s_fsize−1.
 *
 * Freispeicher ist eine **verkettete Liste**: der Superblock fuehrt bis zu 50 freie
 * Bloecke (`s_free[]`), `s_free[0]` nennt den Ankerblock, der die naechsten 50 traegt
 * (`struct fblk`).  Freie Inodes stehen zusaetzlich in einem Zwischenspeicher von bis
 * zu 100 Nummern (`s_inode[]`) — der ist NUR ein Zwischenspeicher, massgeblich ist
 * `di_mode == 0` in der Inode-Liste (Kernel `ialloc`).
 *
 * Diese Klasse arbeitet auf einem **Blockgeraet** (@ref WegaBlockDev), nicht direkt
 * auf einem @ref SectorSpace: dieselbe Klasse liest die 720-K-Disketten (Blockraum =
 * die lineare Sektorfolge) und die Partitionen eines Plattenabbilds (@ref WegaPlatte).
 *
 * Namen sind **Pfade relativ zur Wurzel** (`bin/ls`, `etc/passwd`); ein fuehrender
 * Schraegstrich wird ignoriert.  @ref list liefert den ganzen Baum, Verzeichnisse
 * eingeschlossen (Typ `d`).
 *
 * Jede schreibende Operation ist in sich eine **Transaktion**: alle Blockaenderungen
 * landen zuerst in einer Arbeitskopie und gehen erst ans Geraet, wenn die Operation
 * vollstaendig gelungen ist — eine volle Diskette hinterlaesst keine halbe Datei und
 * keine verlorenen Bloecke.
 *
 * @see doc/design/27_wega_dateisystem.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/filesystem/file_system.h"
#include "core/filesystem/sector_space.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace wega {

constexpr uint32_t kBlock     = 512;    ///< BSIZE
constexpr int      kNicFree   = 50;     ///< NICFREE — Freibloecke im Superblock
constexpr int      kNicInod   = 100;    ///< NICINOD — freie Inodes im Superblock
constexpr int      kInodesProBlock = 8; ///< INOPB
constexpr int      kInodeGroesse   = 64;
constexpr int      kNAddr     = 13;     ///< 10 direkt, einfach, doppelt, dreifach
constexpr int      kNDirekt   = 10;
constexpr int      kNIndir    = 128;    ///< Blockadressen je indirektem Block
constexpr int      kDirSiz    = 14;     ///< DIRSIZ
constexpr int      kDirEintrag = 16;
constexpr uint16_t kRootIno   = 2;

// Dateiarten (di_mode & IFMT)
constexpr uint16_t IFMT  = 0170000;
constexpr uint16_t IFDIR = 0040000;
constexpr uint16_t IFCHR = 0020000;
constexpr uint16_t IFBLK = 0060000;
constexpr uint16_t IFREG = 0100000;
constexpr uint16_t IFIFO = 0010000;
constexpr uint16_t IFMPC = 0030000;   ///< System III: gemultiplexte Zeichendatei
constexpr uint16_t IFMPB = 0070000;   ///< System III: gemultiplexte Blockdatei

/// @brief Traegt eine Inode dieser Art Blockadressen (statt einer Geraetenummer)?
inline bool hatBloecke(uint16_t mode) {
    const uint16_t t = mode & IFMT;
    return t == IFREG || t == IFDIR || t == IFIFO;
}

/**
 * @struct Superblock
 * @brief `struct filsys` — Feldlage nach dem Z8000-Compiler (Wortgrenzen, big endian).
 *
 * Offsets (am Abbild w30root1 nachgemessen): s_isize 0, s_fsize 2, s_nfree 6,
 * s_free 8, s_ninode 208, s_inode 210, s_flock 410 … s_ronly 413, s_time 414,
 * s_tfree 418, s_tinode 422, s_m 424, s_n 426, s_fname 428, s_fpack 434, s_mach 440.
 */
struct Superblock {
    uint16_t isize = 0;
    uint32_t fsize = 0;
    int16_t  nfree = 0;
    std::array<uint32_t, kNicFree> free{};
    int16_t  ninode = 0;
    std::array<uint16_t, kNicInod> inode{};
    uint8_t  flock = 0, ilock = 0, fmod = 0, ronly = 0;
    uint32_t time = 0;
    uint32_t tfree = 0;
    uint16_t tinode = 0;
    int16_t  m = 0, n = 0;
    std::string fname, fpack;   ///< je hoechstens 6 Zeichen
    uint8_t  mach = 0;

    /// @brief Rohe Felder, die beim Zurueckschreiben unveraendert bleiben (ab 441).
    std::array<uint8_t, kBlock> roh{};

    static Superblock aus(const uint8_t* b);
    void in(uint8_t* b) const;
    /// @brief Zahl der Inodes = (isize − 2) · 8.
    uint32_t inodes() const { return isize >= 2 ? (isize - 2u) * kInodesProBlock : 0; }
};

/**
 * @struct Dinode
 * @brief `struct dinode` (64 Byte): Modus, Links, uid, gid, Groesse, 13 × 3 Byte
 *        Blockadressen, Zugriffs-, Aenderungs- und Statuszeit.
 */
struct Dinode {
    uint16_t mode = 0;
    int16_t  nlink = 0;
    int16_t  uid = 0, gid = 0;
    uint32_t size = 0;
    std::array<uint32_t, kNAddr> addr{};
    uint8_t  pad40 = 0;          ///< 40. Adressbyte (unbenutzt, wird erhalten)
    uint32_t atime = 0, mtime = 0, ctime = 0;

    static Dinode aus(const uint8_t* p);
    void in(uint8_t* p) const;
};

} // namespace wega

/**
 * @class WegaBlockDev
 * @brief Ein Bereich von 512-Byte-Bloecken — Diskette oder Plattenpartition.
 */
class WegaBlockDev {
public:
    virtual ~WegaBlockDev() = default;
    /// @brief Bloecke im Bereich (obere Grenze fuer s_fsize).
    virtual uint32_t blocks() const = 0;
    virtual bool readBlock(uint32_t bn, uint8_t* dst) const = 0;
    virtual bool writeBlock(uint32_t bn, const uint8_t* src) = 0;
    /// @brief Ort eines Blocks fuer den Diskeditor (nur bei Disketten).
    virtual bool ort(uint32_t bn, FsRecoverOrt& out) const { (void)bn; (void)out; return false; }
};

/**
 * @class WegaSpaceDev
 * @brief Blockgeraet ueber einem @ref SectorSpace: Block n = Bytes n·512 der
 *        linearen Sektorfolge (Zylinder aussen, Kopf, Sektor) — so, wie der
 *        WEGA-Diskettentreiber und das `.img` die Diskette sehen.
 */
class WegaSpaceDev : public WegaBlockDev {
public:
    explicit WegaSpaceDev(SectorSpace& s) : s_(s) {}
    uint32_t blocks() const override {
        return static_cast<uint32_t>(s_.size() / wega::kBlock);
    }
    bool readBlock(uint32_t bn, uint8_t* dst) const override {
        return s_.read(static_cast<uint64_t>(bn) * wega::kBlock, dst, wega::kBlock);
    }
    bool writeBlock(uint32_t bn, const uint8_t* src) override {
        return s_.write(static_cast<uint64_t>(bn) * wega::kBlock, src, wega::kBlock);
    }
    bool ort(uint32_t bn, FsRecoverOrt& out) const override;
private:
    SectorSpace& s_;
};

/**
 * @class WegaFileSystem
 * @brief Ein WEGA-Dateisystem auf einem Blockgeraet.
 */
class WegaFileSystem : public FileSystem {
public:
    /// @brief Vorhandenes Dateisystem einhaengen (prueft die Plausibilitaet).
    static std::unique_ptr<WegaFileSystem> mount(std::unique_ptr<WegaBlockDev> dev,
                                                 std::string& why);
    /**
     * @brief Leeres Dateisystem anlegen — wie `sa.mkfs <bloecke>`.
     *
     * Inode-Liste = bloecke/25 Bloecke (+2), Freiliste in der Verschraenkung
     * @p m / @p n (die 720-K-Disketten tragen 1/72), Inode 1 = leere Bad-Block-Datei,
     * Inode 2 = Wurzel (`040777`, uid/gid 0) — genau so steht es auf den
     * Lieferdisketten.
     */
    static std::unique_ptr<WegaFileSystem> format(std::unique_ptr<WegaBlockDev> dev,
                                                  const std::string& label,
                                                  std::string& err,
                                                  uint32_t bloecke = 0,
                                                  int m = 1, int n = 72,
                                                  uint32_t zeit = 0);

    /**
     * @brief Traegt das Geraet ein WEGA-Dateisystem?  Ohne Magic — die Probe ist
     *        eine Plausibilitaetspruefung (Groessen passen zum Geraet, Listen im
     *        Wertebereich, Wurzel-Inode 2 ist ein Verzeichnis mit `.` und `..`).
     */
    static bool looksLikeWega(const WegaBlockDev& dev, std::string* why);

    // ── FileSystem ───────────────────────────────────────────────────────────
    std::vector<FileEntry> list() const override;
    bool firstSector(const std::string& name, FsRecoverOrt& out) const override;
    bool read(const std::string& name, std::vector<uint8_t>& out) override;
    bool write(const std::string& name, const std::vector<uint8_t>& data,
               const WriteOptions& opt) override;
    bool erase(const std::string& name) override;
    bool makeDirectory(const std::string& name) override;
    bool wouldFit(const std::vector<PlannedFile>& files, FitReport& out) const override;
    bool mkfs() override;
    FsInfo info() const override;
    FsCheckReport check(FsCheckLevel level, bool nachladen) const override;

    // ── WEGA-eigene Auskunft ─────────────────────────────────────────────────
    wega::Superblock superblock() const;
    const WegaBlockDev& device() const { return *dev_; }
    /// @brief Inode lesen (1-basiert).
    bool readInode(uint32_t ino, wega::Dinode& out) const;
    /// @brief Inode-Nummer eines Pfads; 0 = nicht gefunden.
    uint32_t lookup(const std::string& path) const;

    /// @brief Zeitquelle fuer neue Zeitstempel (Tests setzen sie fest).
    void setClock(std::function<uint32_t()> uhr) { uhr_ = std::move(uhr); }

    /// @brief `ls -l`-Zeichenkette fuer einen Modus ("drwxr-xr-x").
    static std::string modeText(uint16_t mode);

private:
    explicit WegaFileSystem(std::unique_ptr<WegaBlockDev> dev) : dev_(std::move(dev)) {}

    class Tx;                    // Arbeitskopie einer schreibenden Operation
    friend class Tx;

    std::unique_ptr<WegaBlockDev> dev_;
    std::function<uint32_t()> uhr_;
    uint32_t jetzt() const;

    // Lesen ueber eine Blockquelle (Geraet oder Arbeitskopie)
    using Leser = std::function<bool(uint32_t, uint8_t*)>;
    Leser geraeteLeser() const;
    static bool inodeLesen(const Leser& l, const wega::Superblock& sb, uint32_t ino,
                           wega::Dinode& out);
    /// @brief Alle Datenbloecke (0 = Loch) in Dateireihenfolge, bis @p groesse.
    static bool dateiBloecke(const Leser& l, const wega::Dinode& di, uint32_t fsize,
                             std::vector<uint32_t>& daten, std::vector<uint32_t>* indirekt,
                             std::string& err);
    static bool dateiLesen(const Leser& l, const wega::Dinode& di, uint32_t fsize,
                           std::vector<uint8_t>& out, std::string& err);
    struct DirEintrag { uint16_t ino; std::string name; uint32_t slot; };
    static bool verzeichnisLesen(const Leser& l, const wega::Superblock& sb, uint32_t ino,
                                 std::vector<DirEintrag>& out, std::string& err);
    uint32_t suche(const Leser& l, const wega::Superblock& sb, const std::string& path,
                   uint32_t* eltern, std::string* blatt) const;
};

/**
 * @class WegaSpeicherDev
 * @brief Blockgeraet ueber einem Byte-Puffer (Plattenpartition); die Blockzuordnung
 *        rechnet der Aufrufer (@ref WegaPlatte — dort sitzt die Defektspur-Verschiebung).
 */
class WegaSpeicherDev : public WegaBlockDev {
public:
    WegaSpeicherDev(std::vector<uint8_t>& buf, uint32_t bloecke,
                    std::function<int64_t(uint32_t)> offset,
                    std::function<void()> geaendert)
        : buf_(buf), n_(bloecke), off_(std::move(offset)), geaendert_(std::move(geaendert)) {}
    uint32_t blocks() const override { return n_; }
    bool readBlock(uint32_t bn, uint8_t* dst) const override;
    bool writeBlock(uint32_t bn, const uint8_t* src) override;
private:
    std::vector<uint8_t>& buf_;
    uint32_t n_;
    std::function<int64_t(uint32_t)> off_;
    std::function<void()> geaendert_;
};

/**
 * @file tar_fs.h
 * @brief Tar-Disketten — ein `tar`-Strom direkt auf der Diskette, ohne Dateisystem.
 *
 * Die P8000-Lieferdisketten tragen oft KEIN Dateisystem: jemand hat mit `tar` auf die
 * rohe Diskette geschrieben.  Der Datenstrom beginnt bei Sektor 0 (Reihenfolge wie im
 * `.img`) und laeuft ueber die ganze Diskette; mehrere Disketten eines Satzes bilden
 * EINEN Strom (`ariane.tar` = `ariane1` + `ariane2`).
 *
 * Diese Klasse liest den Strom **nur** (put/rm scheitern).  Drei Lagen kommen vor:
 *   - der Strom beginnt mit einem Kopfblock (Diskette 1 bzw. Einzeldiskette);
 *   - die Diskette beginnt MITTEN in einer Datei (Diskette 2..n): das Anfangsstueck
 *     wird als `_fortsetzung_anfang.bin` angeboten, ab dem ersten gueltigen Kopfblock
 *     (512-ausgerichtet) setzt der Strom wieder auf;
 *   - die letzte Datei ist abgeschnitten (`tar.abgeschnitten`): sie bleibt lesbar als
 *     Teilstueck, `FileEntry::damaged` und `declared_size` kennzeichnen sie.
 *
 * Erkennung ohne Magic (V7-tar hat keins): gueltige Kopfpruefsumme, druckbarer Name,
 * plausible Groesse.  Sie steht in der Erkennungsreihenfolge HINTER allen echten
 * Dateisystemen (siehe disk_volume.cpp).
 *
 * @see doc/design/33_cqm.md (Abschnitt tar-Disketten)
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/filesystem/file_system.h"
#include "core/filesystem/sector_space.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tar {

constexpr uint32_t kBlock = 512;
constexpr const char* kFortsetzungName = "_fortsetzung_anfang.bin";

/// @brief Ein gelesener Kopfblock.
struct Kopf {
    std::string name;       ///< Pfad wie im Archiv (ustar-Praefix, GNU-Langname eingerechnet)
    std::string linkname;
    uint32_t    mode = 0, uid = 0, gid = 0, mtime = 0;
    uint64_t    size = 0;   ///< Groesse laut Kopf
    char        typ = '0';  ///< '0' Datei, '1' Link, '2' Symlink, '3'/'4' Geraet, '5' Verz., '6' FIFO
};

/// @brief Ist @p b (512 Byte) ein gueltiger Kopfblock?  Prueft Summe, Name, Groesse.
bool kopfLesen(const uint8_t* b, Kopf& k, std::string* warum = nullptr);

} // namespace tar

/**
 * @class TarQuelle
 * @brief Der Bytestrom, in dem das Archiv liegt (Diskette oder Speicher).
 */
class TarQuelle {
public:
    virtual ~TarQuelle() = default;
    virtual uint64_t size() const = 0;
    virtual bool read(uint64_t off, uint8_t* dst, size_t n) const = 0;
    /// @brief Ort des Sektors, der @p off enthaelt (fuer den Diskeditor); Vorgabe: unbekannt.
    virtual bool ort(uint64_t off, FsRecoverOrt& out) const { (void)off; (void)out; return false; }
};

/// @brief Strom ueber der linearen Sektorfolge einer Diskette.
class TarSpaceQuelle : public TarQuelle {
public:
    explicit TarSpaceQuelle(SectorSpace& s) : s_(s) {}
    uint64_t size() const override { return s_.size(); }
    bool read(uint64_t off, uint8_t* dst, size_t n) const override { return s_.read(off, dst, n); }
    bool ort(uint64_t off, FsRecoverOrt& out) const override;
private:
    SectorSpace& s_;
};

/// @brief Strom im Speicher — der Mehrdisketten-Satz (die Strome hintereinander).
class TarSpeicherQuelle : public TarQuelle {
public:
    explicit TarSpeicherQuelle(std::vector<uint8_t> d) : d_(std::move(d)) {}
    uint64_t size() const override { return d_.size(); }
    bool read(uint64_t off, uint8_t* dst, size_t n) const override;
private:
    std::vector<uint8_t> d_;
};

/**
 * @class TarFileSystem
 * @brief Schreibgeschuetztes Archiv-Dateisystem ueber einem @ref TarQuelle.
 */
class TarFileSystem : public FileSystem {
public:
    static std::unique_ptr<TarFileSystem> mount(std::unique_ptr<TarQuelle> q, std::string& why);
    /// @brief Traegt der Strom ein tar-Archiv (Anfang oder Fortsetzung)?
    static bool looksLikeTar(const TarQuelle& q, std::string* why);

    std::vector<FileEntry> list() const override;
    bool firstSector(const std::string& name, FsRecoverOrt& out) const override;
    bool read(const std::string& name, std::vector<uint8_t>& out) override;
    bool write(const std::string&, const std::vector<uint8_t>&, const WriteOptions&) override;
    bool erase(const std::string&) override;
    bool wouldFit(const std::vector<PlannedFile>&, FitReport& out) const override;
    bool mkfs() override;
    FsInfo info() const override;
    FsCheckReport check(FsCheckLevel level, bool nachladen) const override;

    /// @brief Beginnt der Strom mitten in einer Datei?  Dann: Laenge des Anfangsstuecks.
    uint64_t fortsetzungAnfang() const;

private:
    explicit TarFileSystem(std::unique_ptr<TarQuelle> q) : q_(std::move(q)) {}

    struct Eintrag {
        tar::Kopf k;
        std::string name;        ///< bereinigter Pfad (ohne '/', '.', '..')
        uint64_t    kopf_off = 0;
        uint64_t    daten_off = 0;
        uint64_t    ist = 0;     ///< tatsaechlich vorhandene Datenbytes
        bool        abgeschnitten = false;
        bool        pfad_geaendert = false;   ///< absolut / '..' bereinigt
    };
    enum class Ende { Nullblock, Streamende, Abgeschnitten, Pruefsumme };
    struct Scan {
        bool        ok = false;
        uint64_t    anfang = 0;           ///< Anfangsstueck vor dem ersten Kopf (0 = Strom beginnt mit Kopf)
        std::vector<Eintrag> e;
        Ende        ende = Ende::Streamende;
        uint64_t    ende_off = 0;
        int         doppelt = 0;
    };
    static Scan scanAb(const TarQuelle& q, uint64_t start);
    static Scan scanAlles(const TarQuelle& q);
    /// @brief Wirksamer Eintrag zu @p name (der letzte gleichen Namens).
    const Eintrag* finde(const Scan& s, const std::string& name) const;
    bool lies(const Scan& s, const Eintrag& e, std::vector<uint8_t>& out, int tiefe) const;

    std::unique_ptr<TarQuelle> q_;
};

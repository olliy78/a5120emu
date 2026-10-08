/**
 * @file wega_platte.h
 * @brief P8000-Plattenabbild (WDC, K5504.50 u. a.) mit seinen WEGA-Partitionen.
 *
 * Das Abbild ist roh/LBA (`doc/merkposten/p8000.md` Nr. 17): Sektor (Z, K, S) liegt bei
 * `((Z·Köpfe + K)·Sektoren + S−1)·512`.  **Zylinder 0 gehört dem WDC** (PAR/BTT in
 * Z0/K0/S1: `"DEFEKT"` @0, `"PARMTR"` @256, Geometrie @277…280) und liegt außerhalb
 * des Blockraums: WDC-Block 0 = Zylinder 1, und jede Defektspur der BTT verschiebt die
 * Zielspur um eine Spur (`doc/p8000/wdc_firmware.md` §6/§7).
 *
 * **Die Partitionen stehen nicht auf der Platte**, sondern im WEGA-Kern
 * (`md_sizes[]` in `uts/conf/wpar.c`, Größen aus `uts/h/mdsize.h`).  Hier gilt deshalb
 * die Standardtabelle für Laufwerk 0 (md0 /usr 0/13000, md1 swap, md2 / 16000/7000,
 * md3 /tmp 23000/4000, md4 /z 27000/60732) — und es wird nur eingehängt, was dort
 * wirklich ein plausibles Dateisystem trägt.
 *
 * Schreibschutz ist die Vorgabe (wie bei physischen Datenträgern); vor dem ersten
 * Zurückschreiben entsteht `<abbild>~`.
 *
 * @see doc/design/27_wega_dateisystem.md §1.1
 */

#pragma once
#include "core/filesystem/wega/wega_fs.h"

#include <memory>
#include <string>
#include <vector>

class WegaPlatte {
public:
    /// @brief Eine Partition der Standardtabelle (`md_sizes[]`).
    struct Partition {
        int         md = 0;           ///< Minor-Nummer: md0 … md4
        uint32_t    start = 0;        ///< erster WDC-Block
        uint32_t    bloecke = 0;      ///< Groesse laut Tabelle
        std::string rolle;            ///< "/usr", "swap", "/", "/tmp", "/z"
    };
    /// @brief Die Tabelle des ausgelieferten Kerns (WEGA 3.x, Laufwerk 0).
    static const std::vector<Partition>& standardTabelle();

    /// @brief Traegt die Datei einen PAR-Sektor in Z0/K0/S1?  Liest nur 512 Byte.
    static bool istPlatte(const std::string& pfad, std::string* why = nullptr);

    /**
     * @brief Abbild laden und die Partitionen mit WEGA-Dateisystem einhaengen.
     * @return nullptr, wenn kein PAR oder KEINE Partition ein Dateisystem traegt.
     */
    static std::unique_ptr<WegaPlatte> open(const std::string& pfad, std::string& err,
                                            bool read_only = true);

    int volumeCount() const { return static_cast<int>(vols_.size()); }
    /// @brief "md2" — Praefix der Namen in der Kommandozeile.
    std::string volumeDir(int v) const { return "md" + std::to_string(vols_[size_t(v)].p.md); }
    const Partition& partition(int v) const { return vols_[size_t(v)].p; }
    WegaFileSystem&  fs(int v) { return *vols_[size_t(v)].fs; }
    const WegaFileSystem& fs(int v) const { return *vols_[size_t(v)].fs; }
    /// @brief Volume zu "md2" bzw. "2"; -1 = keins.
    int volumeFromDir(const std::string& name) const;

    // Geometrie aus dem PAR
    int zylinder() const { return zyl_; }
    int koepfe() const   { return koepfe_; }
    int sektoren() const { return sek_; }
    std::string laufwerk() const { return typ_; }
    size_t defektspuren() const { return btt_.size(); }

    /// @brief Byte-Offset eines WDC-Blocks im Abbild (mit BTT-Verschiebung); −1 = ausserhalb.
    int64_t offsetVon(uint32_t block) const;

    bool readOnly() const { return read_only_; }
    void setReadOnly(bool ro) { read_only_ = ro; }
    void setBackup(bool b) { backup_ = b; }
    bool dirty() const { return dirty_; }
    /// @brief Geaenderte Bytes zurueckschreiben (beim ersten Mal vorher `<pfad>~`).
    bool flush();
    const std::string& path() const { return pfad_; }
    const std::string& lastError() const { return err_; }

private:
    WegaPlatte() = default;
    struct Vol { Partition p; std::unique_ptr<WegaFileSystem> fs; };

    std::string pfad_;
    std::vector<uint8_t> buf_;
    int zyl_ = 0, koepfe_ = 0, sek_ = 0;
    std::string typ_;
    std::vector<uint32_t> btt_;     ///< Defektspuren als Zylinder·Koepfe+Kopf, aufsteigend
    std::vector<Vol> vols_;
    bool read_only_ = true;
    bool backup_ = true, backup_getan_ = false;
    bool dirty_ = false;
    std::string err_;
};

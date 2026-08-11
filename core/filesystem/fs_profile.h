/**
 * @file fs_profile.h
 * @brief FsProfile — die LOGISCHE Ebene einer Diskette (Dateisystem auf einer Geometrie).
 *
 * `formats:` in `data/formats.yaml` beschreibt **Physik** (welche Spur traegt wie viele
 * Sektoren welcher Groesse in welchem Verfahren).  Ob darauf ein CP/M- oder ein
 * UDOS-Dateisystem liegt und wo es beginnt, ist eine **andere** Frage — dieselbe
 * 26×128-Geometrie traegt einmal ein UDOS- und einmal ein CP/M-Dateisystem.
 *
 * Deshalb eine zweite Katalogsektion `filesystems:`, deren Eintraege per `format:` eine
 * Geometrie referenzieren (@ref FsCatalog).  Der Emulator liest sie nicht.
 *
 * Sie ist **kurz und soll es bleiben**: fuer CP/A-Disketten rechnet @ref CpaDpbRule den
 * DPB aus der Geometrie aus, wie es das BIOS beim LOGIN tut.  Ein benannter Eintrag
 * lohnt nur, wo diese Regel nicht gilt (UDOS, Fremdsysteme) oder wo ein Name gebraucht
 * wird — `create --fs NAME` kann nur aus einem benannten Profil eine Diskette anlegen.
 *
 * @see doc/design/13_k1520disktool.md §6
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include <cstdint>
#include <string>
#include <vector>

/// @brief Unterstuetzte Dateisystemfamilien.
enum class FsType : uint8_t {
    Cpm,    ///< CP/M 2.2 und Verwandte (CP/A, SCPX)
    Udos    ///< UDOS 1526 / 4.x mit dem Treiber ZDOS
};

/**
 * @struct FsProfile
 * @brief Ein benanntes Dateisystem auf einer benannten Geometrie.
 */
struct FsProfile {
    std::string name;          ///< eindeutiger Katalogname (CLI, GUI, C-API)
    std::string description;   ///< Klartext fuer die Oberflaeche
    std::string format;        ///< Name eines Eintrags aus `formats:`
    FsType      type = FsType::Cpm;

    /// @brief Erste Spur des Dateisystems.  Der Byte-Offset folgt daraus und wird
    ///        NIE gepflegt — bei gemischter Geometrie (cpa780) waere er als Spurzahl
    ///        gar nicht ausdrueckbar (doc/design/13_k1520disktool.md §6.2).
    uint8_t data_cyl  = 0;
    uint8_t data_head = 0;

    bool allow_img = true;     ///< als rohes Sektorabbild darstellbar (UDOS: nie)
    bool allow_hfe = true;
    bool allow_dmk = true;

    int detect_rank = 0;       ///< kleiner = frueher bei mehrdeutiger Erkennung

    // ── nur FsType::Cpm ──────────────────────────────────────────────────────
    uint32_t block_size  = 2048;   ///< Zuordnungseinheit
    uint16_t dir_entries = 128;    ///< Verzeichniseintraege (maxdir)
    uint8_t  skew        = 0;      ///< Sektorversatz je Spur (CP/A: 0)
    std::string os       = "cpm2.2";

    // ── nur FsType::Udos ─────────────────────────────────────────────────────
    bool    sides_separate  = true;  ///< je Seite ein eigenes Dateisystem → SideN/
    uint8_t boot_track      = 21;    ///< Bootabbild (15H)
    uint8_t directory_track = 22;    ///< Verzeichnisdatei (16H)
    uint8_t bitmap_track    = 23;    ///< Belegungskarte (17H)
    uint8_t usable_tracks   = 0;     ///< 0 = aus der Geometrie

    /// @brief Ist dieser Container erlaubt? (Endungslogik macht der Aufrufer)
    bool allowsContainer(const std::string& ext_lower) const {
        if (ext_lower == "img") return allow_img;
        if (ext_lower == "hfe") return allow_hfe;
        if (ext_lower == "dmk") return allow_dmk;
        return false;
    }
};

/// @brief "cpm" | "udos" — fuer Meldungen und die C-API.
const char* fsTypeName(FsType t);

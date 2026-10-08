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
    Cpm,       ///< CP/M 2.2 und Verwandte (CP/A, SCPX)
    Udos,      ///< UDOS 1526 / 4.x mit dem Treiber ZDOS (A5120)
    /// @brief UDOS1715 mit dem Treiber **NDOS** (PC 1715, µPD765).
    ///
    /// Dieselbe Betriebssystemfamilie, aber ein anderes Dateisystem: der µPD765 kann
    /// nichts hinter die Daten-CRC schreiben, deshalb steht die Verkettung in eigenen
    /// **Zeigersektoren** statt im Gap.  Folgen: 256-B-Sektoren, `.img` ist moeglich,
    /// und eine „Spur" umfasst BEIDE Seiten eines Zylinders (32 Sektoren).
    /// @see doc/udos1715_diskettenformat.md
    Udos1715,
    /// @brief WEGA (UNIX System III des P8000): 512-B-Bloecke, Superblock in Block 1,
    ///        Inodes ab Block 2, Verzeichnisbaum.  Kein Magic — die Erkennung ist eine
    ///        Plausibilitaetspruefung.  @see doc/design/27_wega_dateisystem.md
    Wega
};

/// @brief Gehoert der Typ zur UDOS-Familie? (gemeinsame Kopfsektorfelder, Typ/Props)
inline bool isUdosFamily(FsType t) {
    return t == FsType::Udos || t == FsType::Udos1715;
}

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
    /// @brief false = **nur auf Anforderung** (`--fs NAME`, `create --fs NAME`), nie in
    ///        der Erkennung.  Fuer ein Dateisystem, das das Medium nicht von einem
    ///        anderen unterscheidet — `scpx8915` (K8915, festes OFF 2) ist auf einer
    ///        LEEREN Diskette dasselbe wie `cpa800` (CP/A, ab Zylinder 0).  Sobald der
    ///        K8915 darauf geschrieben hat, findet es die CP/A-Regel ohnehin selbst.
    bool detect = true;

    /// @brief Bauart des **Ladekopfs** in Sektor 1 der Systemspuren, den ein Bootabbild
    ///        tragen muss; "" = keine Pruefung.  `k8915` (AP-E5c): die ersten
    ///        16 Byte mit CRC-CCITT (Startwert FFFFH, Ergebnis 0) — ein CP/A-Bootabbild
    ///        des A5120 traegt keinen und wuerde sonst still auf eine Diskette
    ///        geschrieben, die der K8915-Lader mit `C` abweist.  `pc1715` (AP-D): das
    ///        Wort `F002H`/`F003H` (Bytes `02|03 F0`), das der Urlader S502 des PC 1715
    ///        als Einziges prueft — ein A5120-Lader (`SYL`) besteht es nicht.
    std::string boot_header;

    /// @brief Nur CP/M: der **Bootbereich steht im Verzeichnis** (CP/A 1715 ohne
    ///        Systemspuren, `doc/pc1715/disketten.md` §3).  Verzeichnisplatz 0 traegt den
    ///        Bootkopf (`02|03 F0 …`), ein Platz mit Nutzerbyte `F0` unter den ersten
    ///        vier die Laufwerks-Parametersaetze; beide gehoeren dem Urlader, nicht dem
    ///        Dateisystem — `list`/`check`/`erase` uebergehen sie, `write` vergibt sie nie,
    ///        und die Erkennung dieses Profils verlangt den Bootkopf ausdruecklich
    ///        (sonst waere jede Datendiskette zugleich `cpa800` und `cpa1715`).
    bool dir_boot = false;

    // ── nur FsType::Cpm ──────────────────────────────────────────────────────
    uint32_t block_size  = 2048;   ///< Zuordnungseinheit
    uint16_t dir_entries = 128;    ///< Verzeichniseintraege (maxdir)
    uint8_t  skew        = 0;      ///< Sektorversatz je Spur (CP/A: 0)
    std::string os       = "cpm2.2";

    // ── nur die UDOS-Familie ─────────────────────────────────────────────────
    bool    sides_separate  = true;  ///< je Seite ein eigenes Dateisystem → SideN/
                                     ///< (UDOS1715: immer false — eine Diskette,
                                     ///<  ein Dateisystem, die Spur ist der Zylinder)
    uint8_t boot_track      = 21;    ///< ZDOS: Bootabbild (15H); UDOS1715: ungenutzt
    uint8_t directory_track = 22;    ///< Verzeichnisdatei (16H)
    uint8_t bitmap_track    = 23;    ///< Belegungskarte (17H)
    uint8_t usable_tracks   = 0;     ///< 0 = aus der Geometrie

    /// @brief Nur @ref FsType::Udos1715 — belegt Spur 0 der Urlader (Systemdiskette)?
    ///
    /// Auf einer Systemdiskette liegen dort Urlader und BFOS
    /// (doc/udos1715_diskettenformat.md §2).  Ob das so ist, sagt die Belegungskarte;
    /// beim ANLEGEN einer Diskette muss es aber jemand entscheiden.
    bool    system_track0   = false;

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

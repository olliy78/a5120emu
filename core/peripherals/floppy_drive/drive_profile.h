/**
 * @file drive_profile.h
 * @brief DriveProfile – beschreibt das an einem Slot angeschlossene physische Laufwerk.
 *
 * Maschinenkonfiguration (Konstruktorparameter), KEIN Runtime-Zustand — analog zu den
 * compile-time Config-Structs der Karten (CLAUDE.md-Konvention, z. B. K2526::A5120Config).
 * Jeder der 4 Laufwerks-Slots der K5122 erhält ein DriveProfile, das Zoll-Größe,
 * Spuren/Köpfe, Drehzahl und unterstützte Aufzeichnungsverfahren festlegt.
 *
 * Die K5122 ist konstruktiv verfahrensagnostisch (Marken-ROM A2.2 erkennt MFM-Sync
 * *und* FM-Marken); das Verfahren ist Eigenschaft von Laufwerk + Medium + Format.
 *
 * @see doc/refactoring_floppy_emulator.md §3.A
 * @see doc/design/09_floppy_drive.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "track_image.h"   // Encoding
#include <cstdint>
#include <string>

/**
 * @struct DriveProfile
 * @brief Physisches Laufwerk an einem K5122-Slot (Startumfang: 4 Profile).
 *
 * | Profil          | Zoll  | Spuren×Köpfe | Verfahren | U/min |
 * |-----------------|-------|--------------|-----------|-------|
 * | mfs_525_ds80    | 5,25″ | 80×2         | MFM       | 300   |
 * | ss_525_40       | 5,25″ | 40×1         | MFM       | 300   |
 * | mf3200_8_ss77   | 8″    | 77×1         | FM        | 360   |
 * | mf6400_8_ds77   | 8″    | 77×2         | MFM       | 360   |
 */
struct DriveProfile {
    std::string name        = "mfs_525_ds80";
    uint8_t  num_cyls       = 80;     ///< 40 / 77 / 80
    uint8_t  num_heads      = 2;      ///< 1 / 2
    uint16_t rpm            = 300;    ///< 300 (5,25″) / 360 (8″) → Index-Periode
    uint8_t  medium_inch    = 5;      ///< 5 / 8 (für /HF-Frequenzwahl, informativ)
    bool     supports_fm    = false;  ///< 8″-Laufwerke
    bool     supports_mfm   = true;

    /**
     * @brief Index-Periode in Z80-Takten aus der Drehzahl ableiten.
     *
     * Ersetzt die feste Konstante kIndexPeriodCycles der alten K5122.
     * Eine Umdrehung dauert 60/rpm Sekunden → @p cpu_hz * 60 / rpm Takte.
     *
     * @param cpu_hz effektive Z80-Taktfrequenz in Hz
     * @return Z80-Takte pro Umdrehung
     */
    int indexPeriodCycles(uint32_t cpu_hz) const {
        return static_cast<int>(static_cast<uint64_t>(cpu_hz) * 60 / rpm);
    }

    /// @brief Prüft, ob ein Verfahren von diesem Laufwerk unterstützt wird.
    bool supports(Encoding enc) const {
        return enc == Encoding::FM ? supports_fm : supports_mfm;
    }
};

/**
 * @brief Eingebautes Standardprofil per Name auflösen (analog FormatParser::builtinFormats()).
 *
 * Unbekannte Namen liefern das Default-Profil mfs_525_ds80.
 *
 * @param name "mfs_525_ds80" | "ss_525_40" | "mf3200_8_ss77" | "mf6400_8_ds77"
 * @return Referenz auf ein statisches DriveProfile.
 */
const DriveProfile& builtinDriveProfile(const std::string& name);

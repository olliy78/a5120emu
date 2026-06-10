/**
 * @file drive_profile.cpp
 * @brief Implementierung von builtinDriveProfile (statische Laufwerksprofile).
 *
 * @see core/peripherals/floppy_drive/drive_profile.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/drive_profile.h"

const DriveProfile& builtinDriveProfile(const std::string& name) {
    // Statische lokale Objekte — Lebensdauer bis Programmende.
    // Unbekannte Namen liefern das Standardprofil mfs_525_ds80.

    static const DriveProfile mfs_525_ds80 = {
        "mfs_525_ds80",
        /*num_cyls=*/    80,
        /*num_heads=*/    2,
        /*rpm=*/        300,
        /*medium_inch=*/  5,
        /*supports_fm=*/ false,
        /*supports_mfm=*/true
    };

    static const DriveProfile ss_525_40 = {
        "ss_525_40",
        /*num_cyls=*/    40,
        /*num_heads=*/    1,
        /*rpm=*/        300,
        /*medium_inch=*/  5,
        /*supports_fm=*/ false,
        /*supports_mfm=*/true
    };

    static const DriveProfile mf3200_8_ss77 = {
        "mf3200_8_ss77",
        /*num_cyls=*/    77,
        /*num_heads=*/    1,
        /*rpm=*/        360,
        /*medium_inch=*/  8,
        /*supports_fm=*/ true,
        /*supports_mfm=*/false
    };

    static const DriveProfile mf6400_8_ds77 = {
        "mf6400_8_ds77",
        /*num_cyls=*/    77,
        /*num_heads=*/    2,
        /*rpm=*/        360,
        /*medium_inch=*/  8,
        /*supports_fm=*/ false,
        /*supports_mfm=*/true
    };

    if (name == "ss_525_40")      return ss_525_40;
    if (name == "mf3200_8_ss77")  return mf3200_8_ss77;
    if (name == "mf6400_8_ds77")  return mf6400_8_ds77;

    // Standardprofil für unbekannte Namen und "mfs_525_ds80"
    return mfs_525_ds80;
}

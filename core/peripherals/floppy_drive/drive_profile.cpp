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

    // K5600.20 — 5,25″-Laufwerk, 80 Spuren, EINSEITIG, MFM (DPB 10580).  Wie K5601,
    // aber nur ein Kopf; die einseitigen CP/A-Formate (z. B. 26×128+5×1024, 390k)
    // adressieren nur Kopf 0.  Für das Combo-BIOS an C: (§11).  FM-lesefähig wie K5601
    // (Boot-Lesepfad startet FM), daher supports_fm=true + Default-FM.
    static const DriveProfile ss_525_80 = {
        "ss_525_80",
        /*num_cyls=*/    80,
        /*num_heads=*/    1,
        /*rpm=*/        300,
        /*medium_inch=*/  5,
        /*supports_fm=*/ true,
        /*supports_mfm=*/true
    };

    // MF6400 / K5602.10 — 8″-Laufwerk, 77 Spuren, EINSEITIG, doppelte Dichte (MFM,
    // DPB 10877).  Die 8″-DD-Formate (z. B. 26×128+8×1024, 600k) sind einseitig; das
    // Combo-BIOS meldet den Typ an C: (§5.2).  Im Unterschied zu mf6400_8_ds77 (2 Köpfe)
    // erzwingt das 1-Kopf-Profil die korrekte Kopf-0-Ablage (§8.6).  FM-lesefähiger
    // Boot-Lesepfad (Default FM, ROM schaltet auf MFM), analog K5601.
    static const DriveProfile mf6400_8_ss77 = {
        "mf6400_8_ss77",
        /*num_cyls=*/    77,
        /*num_heads=*/    1,
        /*rpm=*/        360,
        /*medium_inch=*/  8,
        /*supports_fm=*/ true,
        /*supports_mfm=*/true
    };

    // K5601 — das in der Standard-A5120-Bürokonfiguration dreifach verbaute 5,25″-
    // Laufwerk (80 Spuren, doppelseitig, 800 KB/Diskette).  Physisch MFM-fähig, aber
    // auch FM-lesefähig; der Boot-ROM-/Loader-Lesepfad ist als FM verdrahtet
    // (default_read_encoding = FM).  Das OS schaltet zur Laufzeit per Steuerwort um.
    static const DriveProfile k5601 = {
        "K5601",
        /*num_cyls=*/    80,
        /*num_heads=*/    2,
        /*rpm=*/        300,
        /*medium_inch=*/  5,
        /*supports_fm=*/ true,
        /*supports_mfm=*/true,
        /*default_read_encoding=*/ Encoding::FM
    };

    // "none" — leerer Slot (kein Laufwerk gesteckt).  Geometrie bleibt auf sinnvollen
    // Defaults (falls der Slot doch selektiert würde, keine Division/Bereichsfehler);
    // entscheidend ist present=false: mountDisk/createDisk verweigern, GUI blendet aus.
    static const DriveProfile none = {
        "none",
        /*num_cyls=*/    80,
        /*num_heads=*/    2,
        /*rpm=*/        300,
        /*medium_inch=*/  5,
        /*supports_fm=*/ true,
        /*supports_mfm=*/true,
        /*default_read_encoding=*/ Encoding::FM,
        /*present=*/     false
    };

    if (name == "none")           return none;
    if (name == "ss_525_40")      return ss_525_40;
    if (name == "ss_525_80")      return ss_525_80;
    if (name == "mf3200_8_ss77")  return mf3200_8_ss77;
    if (name == "mf6400_8_ds77")  return mf6400_8_ds77;
    if (name == "mf6400_8_ss77")  return mf6400_8_ss77;
    if (name == "K5601")          return k5601;

    // Standardprofil für unbekannte Namen und "mfs_525_ds80"
    return mfs_525_ds80;
}

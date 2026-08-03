/**
 * @file drive_profile.cpp
 * @brief Implementierung von builtinDriveProfile (statische Laufwerksprofile).
 *
 * Die Profile heißen wie die realen Laufwerke: K5601, K5600.10, K5600.20, MF3200,
 * MF6400 (+ `"none"` für den leeren Slot).  Ältere, technisch beschreibende Namen
 * (`ss_525_40`, `mf6400_8_ss77`, …) werden als Alias weiterhin aufgelöst, damit
 * gespeicherte GUI-Konfigurationen nicht still auf ein anderes Laufwerk zurückfallen.
 *
 * @see core/peripherals/floppy_drive/drive_profile.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/drive_profile.h"

const DriveProfile& builtinDriveProfile(const std::string& name) {
    // Statische lokale Objekte — Lebensdauer bis Programmende.
    // Unbekannte Namen liefern das Standardprofil K5601.

    // ── 5,25″ ────────────────────────────────────────────────────────────────

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

    // K5600.10 — 5,25″, 40 Spuren, einseitig, MFM (200 KB, DPB-Typ 200K).
    static const DriveProfile k5600_10 = {
        "K5600.10",
        /*num_cyls=*/    40,
        /*num_heads=*/    1,
        /*rpm=*/        300,
        /*medium_inch=*/  5,
        /*supports_fm=*/ false,
        /*supports_mfm=*/true
    };

    // K5600.20 — 5,25″, 80 Spuren, EINSEITIG, MFM (400 KB, DPB 10580).  Wie K5601,
    // aber nur ein Kopf; die einseitigen CP/A-Formate adressieren nur Kopf 0.
    // FM-lesefähig wie K5601 (Boot-Lesepfad startet FM), daher Default-FM.
    static const DriveProfile k5600_20 = {
        "K5600.20",
        /*num_cyls=*/    80,
        /*num_heads=*/    1,
        /*rpm=*/        300,
        /*medium_inch=*/  5,
        /*supports_fm=*/ true,
        /*supports_mfm=*/true
    };

    // ── 8″ ───────────────────────────────────────────────────────────────────
    // Beide 8″-Laufwerke sind EINSEITIG mit 77 Spuren; sie unterscheiden sich allein
    // im beherrschten Aufzeichnungsverfahren (und damit in der Kapazität).

    // MF3200 — 8″, 77 Spuren, einseitig, **nur FM** (Einfachdichte) → bis ~300 KB.
    // Das K5602 ist hierzu voll kompatibel und braucht kein eigenes Profil.
    static const DriveProfile mf3200 = {
        "MF3200",
        /*num_cyls=*/    77,
        /*num_heads=*/    1,
        /*rpm=*/        360,
        /*medium_inch=*/  8,
        /*supports_fm=*/ true,
        /*supports_mfm=*/false
    };

    // MF6400 — 8″, 77 Spuren, einseitig, **FM und MFM** (Doppeldichte) → bis ~600 KB.
    // Damit sind auch die Mischdichte-Formate (FM-Systemspur + MFM-Daten) fahrbar.
    static const DriveProfile mf6400 = {
        "MF6400",
        /*num_cyls=*/    77,
        /*num_heads=*/    1,
        /*rpm=*/        360,
        /*medium_inch=*/  8,
        /*supports_fm=*/ true,
        /*supports_mfm=*/true
    };

    // ── Leerer Slot ──────────────────────────────────────────────────────────

    // "none" — kein Laufwerk gesteckt.  Geometrie bleibt auf sinnvollen Defaults
    // (falls der Slot doch selektiert würde, keine Division/Bereichsfehler);
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

    if (name == "none")     return none;
    if (name == "K5601")    return k5601;
    if (name == "K5600.10") return k5600_10;
    if (name == "K5600.20") return k5600_20;
    if (name == "MF3200")   return mf3200;
    if (name == "MF6400")   return mf6400;

    // ── Alias: frühere technische Profilnamen ────────────────────────────────
    // Gespeicherte GUI-Konfigurationen (app/config_io.py) und ältere Skripte tragen
    // noch diese Namen; ohne Alias fielen sie still auf das Standardprofil zurück.
    // `mfs_525_ds80` war ein Duplikat des K5601 ohne FM-Lesepfad, `mf6400_8_ds77` ein
    // zweiseitiges 8″-Profil, das es als Hardware nicht gibt — beide entfallen.
    if (name == "ss_525_40")     return k5600_10;
    if (name == "ss_525_80")     return k5600_20;
    if (name == "mf3200_8_ss77") return mf3200;
    if (name == "mf6400_8_ss77") return mf6400;
    if (name == "mf6400_8_ds77") return mf6400;
    if (name == "mfs_525_ds80")  return k5601;

    // Standardprofil für unbekannte Namen
    return k5601;
}

const std::vector<std::string>& knownDriveProfileNames() {
    // Nur die REALEN Laufwerksnamen — Aliase gehören bewusst nicht dazu, damit
    // data/formats.yaml auf die aktuellen Namen festgelegt ist (Validierung V3).
    static const std::vector<std::string> names = {
        "K5601", "K5600.10", "K5600.20", "MF3200", "MF6400", "none"
    };
    return names;
}

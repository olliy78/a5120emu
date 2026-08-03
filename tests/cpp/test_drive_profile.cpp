/**
 * @file test_drive_profile.cpp
 * @brief GoogleTests für DriveProfile und builtinDriveProfile.
 *
 * Getestete Komponenten:
 *   - DriveProfile::indexPeriodCycles (Index-Periode aus Drehzahl)
 *   - builtinDriveProfile (vier eingebaute Profile, Fallback für unbekannte Namen)
 *   - DriveProfile::supports (Verfahrens-Kompatibilität)
 *
 * @see core/peripherals/floppy_drive/drive_profile.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>
#include "core/peripherals/floppy_drive/drive_profile.h"
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// indexPeriodCycles
// ─────────────────────────────────────────────────────────────────────────────

TEST(DriveProfileIndexPeriod, RPM300_2450000Hz) {
    // Erwartung: 2'450'000 * 60 / 300 = 490'000
    DriveProfile p = builtinDriveProfile("K5601");
    EXPECT_EQ(p.indexPeriodCycles(2'450'000), 490'000);
}

TEST(DriveProfileIndexPeriod, RPM360_2450000Hz) {
    // Erwartung: 2'450'000 * 60 / 360 = 408'333
    DriveProfile p = builtinDriveProfile("MF3200");
    const int erwartet = static_cast<int>(
        static_cast<uint64_t>(2'450'000) * 60 / 360);
    EXPECT_EQ(p.indexPeriodCycles(2'450'000), erwartet);
    EXPECT_EQ(erwartet, 408'333);
}

// ─────────────────────────────────────────────────────────────────────────────
// builtinDriveProfile — Felder je Name
// ─────────────────────────────────────────────────────────────────────────────

TEST(DriveProfileBuiltin, K5601_5Zoll_doppelseitig) {
    // Standardlaufwerk der A5120-Bürokonfiguration: 5,25″, 80 Spuren, 2 Köpfe, 800K.
    const DriveProfile& p = builtinDriveProfile("K5601");
    EXPECT_EQ(p.name,         "K5601");
    EXPECT_EQ(p.num_cyls,     80u);
    EXPECT_EQ(p.num_heads,     2u);
    EXPECT_EQ(p.rpm,         300u);
    EXPECT_EQ(p.medium_inch,   5u);
    EXPECT_TRUE(p.supports_fm);     // FM-lesefähig (Loader-Lesepfad als FM verdrahtet)
    EXPECT_TRUE(p.supports_mfm);
    EXPECT_EQ(p.default_read_encoding, Encoding::FM);
}

TEST(DriveProfileBuiltin, K5600_10_5Zoll_40Spuren_einseitig) {
    const DriveProfile& p = builtinDriveProfile("K5600.10");
    EXPECT_EQ(p.name,         "K5600.10");
    EXPECT_EQ(p.num_cyls,     40u);
    EXPECT_EQ(p.num_heads,     1u);
    EXPECT_EQ(p.rpm,         300u);
    EXPECT_EQ(p.medium_inch,   5u);
    EXPECT_TRUE(p.supports_fm);
    EXPECT_TRUE(p.supports_mfm);
}

TEST(DriveProfileBuiltin, K5600_20_5Zoll_80Spuren_einseitig) {
    const DriveProfile& p = builtinDriveProfile("K5600.20");
    EXPECT_EQ(p.name,         "K5600.20");
    EXPECT_EQ(p.num_cyls,     80u);
    EXPECT_EQ(p.num_heads,     1u);
    EXPECT_EQ(p.rpm,         300u);
    EXPECT_EQ(p.medium_inch,   5u);
    EXPECT_TRUE(p.supports_mfm);
}

TEST(DriveProfileBuiltin, MF3200_8Zoll_nurFM) {
    // 8″, 77 Spuren, EINSEITIG, nur Einfachdichte (FM) → ~300K.
    // Das K5602 ist hierzu voll kompatibel und hat kein eigenes Profil.
    const DriveProfile& p = builtinDriveProfile("MF3200");
    EXPECT_EQ(p.name,         "MF3200");
    EXPECT_EQ(p.num_cyls,     77u);
    EXPECT_EQ(p.num_heads,     1u);
    EXPECT_EQ(p.rpm,         360u);
    EXPECT_EQ(p.medium_inch,   8u);
    EXPECT_TRUE(p.supports_fm);
    EXPECT_FALSE(p.supports_mfm);
}

TEST(DriveProfileBuiltin, MF6400_8Zoll_FMundMFM) {
    // Gleiche Mechanik wie das MF3200 (8″, 77 Spuren, EINSEITIG), beherrscht aber
    // zusätzlich MFM — daher die doppelte Kapazität (~600K).
    const DriveProfile& p = builtinDriveProfile("MF6400");
    EXPECT_EQ(p.name,         "MF6400");
    EXPECT_EQ(p.num_cyls,     77u);
    EXPECT_EQ(p.num_heads,     1u);
    EXPECT_EQ(p.rpm,         360u);
    EXPECT_EQ(p.medium_inch,   8u);
    EXPECT_TRUE(p.supports_fm);
    EXPECT_TRUE(p.supports_mfm);
}

TEST(DriveProfileBuiltin, none_leererSlot) {
    // "none" markiert einen unbestückten Slot (present=false).
    const DriveProfile& p = builtinDriveProfile("none");
    EXPECT_EQ(p.name, "none");
    EXPECT_FALSE(p.present);
}

TEST(DriveProfileBuiltin, bestueckteProfile_present) {
    // Alle bestückten Profile sind present=true (Default), nur "none" ist leer.
    EXPECT_TRUE(builtinDriveProfile("K5601").present);
    EXPECT_TRUE(builtinDriveProfile("K5600.10").present);
    EXPECT_TRUE(builtinDriveProfile("K5600.20").present);
    EXPECT_TRUE(builtinDriveProfile("MF3200").present);
    EXPECT_TRUE(builtinDriveProfile("MF6400").present);
    EXPECT_TRUE(builtinDriveProfile("unbekannt_xyz").present);  // Fallback-Standardprofil
}

TEST(DriveProfileBuiltin, UnbekannterName_gibtStandardprofil) {
    // Unbekannter Name → K5601 (Standardprofil)
    const DriveProfile& p = builtinDriveProfile("unbekannt_xyz");
    EXPECT_EQ(p.name, "K5601");
    EXPECT_EQ(p.num_cyls, 80u);
    // Default-Lesepfad-Verfahren = FM (ROM-Bootphase)
    EXPECT_EQ(p.default_read_encoding, Encoding::FM);
}

TEST(DriveProfileBuiltin, KnownNames_NurRealeLaufwerke) {
    // Die Liste ist die Grundlage der formats.yaml-Validierung (V3) — sie darf nur
    // reale Laufwerke enthalten, keine Aliase.
    const auto& names = knownDriveProfileNames();
    EXPECT_EQ(names.size(), 6u);
    for (const char* n : {"K5601", "K5600.10", "K5600.20", "MF3200", "MF6400", "none"})
        EXPECT_NE(std::find(names.begin(), names.end(), n), names.end()) << n;

    // Jeder gelistete Name muss sich auch auf genau dieses Profil auflösen —
    // sonst ginge ein Tippfehler still als Standardprofil durch.
    for (const auto& n : names)
        EXPECT_EQ(builtinDriveProfile(n).name, n);
}

TEST(DriveProfileBuiltin, AlteProfilnamen_BleibenAlsAliasAufloesbar) {
    // Gespeicherte GUI-Konfigurationen tragen noch die früheren technischen Namen;
    // ohne Alias fielen sie still auf das Standardprofil zurück und würden damit die
    // Laufwerksbestückung des Nutzers verändern.
    EXPECT_EQ(builtinDriveProfile("ss_525_40").name,     "K5600.10");
    EXPECT_EQ(builtinDriveProfile("ss_525_80").name,     "K5600.20");
    EXPECT_EQ(builtinDriveProfile("mf3200_8_ss77").name, "MF3200");
    EXPECT_EQ(builtinDriveProfile("mf6400_8_ss77").name, "MF6400");
    // Zweiseitiges 8″-Profil: gibt es als Hardware nicht → auf das reale MF6400.
    EXPECT_EQ(builtinDriveProfile("mf6400_8_ds77").name, "MF6400");
    // War ein K5601-Duplikat ohne FM-Lesepfad.
    EXPECT_EQ(builtinDriveProfile("mfs_525_ds80").name,  "K5601");

    // Aliase gehören NICHT in die Namensliste (formats.yaml soll die aktuellen Namen nutzen).
    const auto& names = knownDriveProfileNames();
    EXPECT_EQ(std::find(names.begin(), names.end(), "ss_525_40"), names.end());
}

// ─────────────────────────────────────────────────────────────────────────────
// supports()
// ─────────────────────────────────────────────────────────────────────────────

TEST(DriveProfileSupports, MF3200_nurFM) {
    const DriveProfile& p = builtinDriveProfile("MF3200");
    EXPECT_TRUE(p.supports(Encoding::FM));
    EXPECT_FALSE(p.supports(Encoding::MFM));
}

TEST(DriveProfileSupports, MF6400_beideVerfahren) {
    // Der entscheidende Unterschied zum MF3200 — er begründet die doppelte Kapazität.
    const DriveProfile& p = builtinDriveProfile("MF6400");
    EXPECT_TRUE(p.supports(Encoding::FM));
    EXPECT_TRUE(p.supports(Encoding::MFM));
}

TEST(DriveProfileSupports, FuenfZollLaufwerke_beideVerfahren) {
    // Alle drei 5,25″-Laufwerke beherrschen FM und MFM; nur das 8″-MF3200 ist
    // auf FM beschränkt.
    for (const char* n : {"K5601", "K5600.10", "K5600.20"}) {
        const DriveProfile& p = builtinDriveProfile(n);
        EXPECT_TRUE(p.supports(Encoding::FM))  << n;
        EXPECT_TRUE(p.supports(Encoding::MFM)) << n;
    }
}

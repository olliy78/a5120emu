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

// ─────────────────────────────────────────────────────────────────────────────
// indexPeriodCycles
// ─────────────────────────────────────────────────────────────────────────────

TEST(DriveProfileIndexPeriod, RPM300_2450000Hz) {
    // Erwartung: 2'450'000 * 60 / 300 = 490'000
    DriveProfile p = builtinDriveProfile("mfs_525_ds80");
    EXPECT_EQ(p.indexPeriodCycles(2'450'000), 490'000);
}

TEST(DriveProfileIndexPeriod, RPM360_2450000Hz) {
    // Erwartung: 2'450'000 * 60 / 360 = 408'333
    DriveProfile p = builtinDriveProfile("mf3200_8_ss77");
    const int erwartet = static_cast<int>(
        static_cast<uint64_t>(2'450'000) * 60 / 360);
    EXPECT_EQ(p.indexPeriodCycles(2'450'000), erwartet);
    EXPECT_EQ(erwartet, 408'333);
}

// ─────────────────────────────────────────────────────────────────────────────
// builtinDriveProfile — Felder je Name
// ─────────────────────────────────────────────────────────────────────────────

TEST(DriveProfileBuiltin, mfs_525_ds80) {
    const DriveProfile& p = builtinDriveProfile("mfs_525_ds80");
    EXPECT_EQ(p.name,         "mfs_525_ds80");
    EXPECT_EQ(p.num_cyls,     80u);
    EXPECT_EQ(p.num_heads,     2u);
    EXPECT_EQ(p.rpm,         300u);
    EXPECT_EQ(p.medium_inch,   5u);
    EXPECT_FALSE(p.supports_fm);
    EXPECT_TRUE(p.supports_mfm);
}

TEST(DriveProfileBuiltin, ss_525_40) {
    const DriveProfile& p = builtinDriveProfile("ss_525_40");
    EXPECT_EQ(p.name,         "ss_525_40");
    EXPECT_EQ(p.num_cyls,     40u);
    EXPECT_EQ(p.num_heads,     1u);
    EXPECT_EQ(p.rpm,         300u);
    EXPECT_EQ(p.medium_inch,   5u);
    EXPECT_FALSE(p.supports_fm);
    EXPECT_TRUE(p.supports_mfm);
}

TEST(DriveProfileBuiltin, mf3200_8_ss77) {
    const DriveProfile& p = builtinDriveProfile("mf3200_8_ss77");
    EXPECT_EQ(p.name,         "mf3200_8_ss77");
    EXPECT_EQ(p.num_cyls,     77u);
    EXPECT_EQ(p.num_heads,     1u);
    EXPECT_EQ(p.rpm,         360u);
    EXPECT_EQ(p.medium_inch,   8u);
    EXPECT_TRUE(p.supports_fm);
    EXPECT_FALSE(p.supports_mfm);
}

TEST(DriveProfileBuiltin, mf6400_8_ds77) {
    const DriveProfile& p = builtinDriveProfile("mf6400_8_ds77");
    EXPECT_EQ(p.name,         "mf6400_8_ds77");
    EXPECT_EQ(p.num_cyls,     77u);
    EXPECT_EQ(p.num_heads,     2u);
    EXPECT_EQ(p.rpm,         360u);
    EXPECT_EQ(p.medium_inch,   8u);
    EXPECT_FALSE(p.supports_fm);
    EXPECT_TRUE(p.supports_mfm);
}

TEST(DriveProfileBuiltin, UnbekannterName_gibtStandardprofil) {
    // Unbekannter Name → mfs_525_ds80 (Standardprofil)
    const DriveProfile& p = builtinDriveProfile("unbekannt_xyz");
    EXPECT_EQ(p.name, "mfs_525_ds80");
    EXPECT_EQ(p.num_cyls, 80u);
}

// ─────────────────────────────────────────────────────────────────────────────
// supports()
// ─────────────────────────────────────────────────────────────────────────────

TEST(DriveProfileSupports, FM_8Zoll_nurFM) {
    const DriveProfile& p = builtinDriveProfile("mf3200_8_ss77");
    EXPECT_TRUE(p.supports(Encoding::FM));
    EXPECT_FALSE(p.supports(Encoding::MFM));
}

TEST(DriveProfileSupports, MFM_5Zoll_nurMFM) {
    const DriveProfile& p = builtinDriveProfile("mfs_525_ds80");
    EXPECT_FALSE(p.supports(Encoding::FM));
    EXPECT_TRUE(p.supports(Encoding::MFM));
}

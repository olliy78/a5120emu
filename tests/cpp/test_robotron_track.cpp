/**
 * @file test_robotron_track.cpp
 * @brief GoogleTests für TrackCodec::buildRobotronTrack.
 *
 * Prüft das Robotron-A5120-Boot-Track-Layout:
 *   - Kein IAM (0xFC) im Strom
 *   - Je Sektor genau zwei A1-Bytes (beide als Marke Id/Data, nicht FE/FB als Marke)
 *   - Marke auf dem A1-Byte; FE/FB sind markenfrei
 *   - Byte-Layout [A1/Id FE cyl head id sizecode id_gap A1/Data FB <data> CRC CRC gap]
 *   - CRC-Berechnung: 128B → crc16(data, 0xBF84); 1024B → crc16([FB]+data, 0xCDB4)
 *   - id_gap: 18 Bytes für size<=128, 27 Bytes für size>128
 *   - nextMark(0) liefert Position des ersten A1 (Id-Marke)
 *
 * @see core/peripherals/floppy_drive/track_codec.h (buildRobotronTrack, TrackLayout)
 * @see CLAUDE.md (ZVE2-Leseroutinen, Resync-Mechanismus)
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/track_image.h"

// ─── Hilfsfunktion ────────────────────────────────────────────────────────────

static LogicalSector makeRobotronSector(uint8_t cyl, uint8_t head, uint8_t id,
                                         uint16_t size, uint8_t fill = 0xE5) {
    LogicalSector s;
    s.cyl  = cyl;
    s.head = head;
    s.id   = id;
    s.size = size;
    s.data.assign(size, fill);
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 1: Basislayout 4 × 128B (cyl0, head0)
// ─────────────────────────────────────────────────────────────────────────────

/// Vier 128B-Sektoren als Eingabe: kein IAM/0xFC im Strom.
TEST(RobotronTrack, KeinIAM_4x128B) {
    std::vector<LogicalSector> sektoren;
    for (uint8_t i = 1; i <= 4; ++i)
        sektoren.push_back(makeRobotronSector(0, 0, i, 128, 0xAA));

    TrackImage t = TrackCodec::buildRobotronTrack(sektoren);
    ASSERT_FALSE(t.empty());

    // Kein 0xFC im Bytestrom (kein IAM)
    for (size_t i = 0; i < t.bytes.size(); ++i) {
        EXPECT_NE(t.bytes[i], 0xFCu)
            << "Unerwartetes IAM 0xFC bei Position " << i;
    }
    // Kein Index-Markentyp
    for (size_t i = 0; i < t.marks.size(); ++i) {
        EXPECT_NE(t.marks[i], MarkType::Index)
            << "Unerwartete Index-Marke bei Position " << i;
    }
}

/// Je Sektor genau zwei A1-Bytes (Id-Marke und Data-Marke).
TEST(RobotronTrack, GenauZweiA1JeSektor_4x128B) {
    std::vector<LogicalSector> sektoren;
    for (uint8_t i = 1; i <= 4; ++i)
        sektoren.push_back(makeRobotronSector(0, 0, i, 128, 0x55));

    TrackImage t = TrackCodec::buildRobotronTrack(sektoren);

    // Zähle A1-Bytes mit Id- und Data-Marken.
    size_t id_a1   = 0;
    size_t data_a1 = 0;
    for (size_t i = 0; i < t.bytes.size(); ++i) {
        if (t.marks[i] == MarkType::Id)   ++id_a1;
        if (t.marks[i] == MarkType::Data) ++data_a1;
    }
    // 4 Sektoren → 4 Id-Marken, 4 Data-Marken
    EXPECT_EQ(id_a1,   4u) << "Erwarte 4 Id-Marken (je Sektor eine)";
    EXPECT_EQ(data_a1, 4u) << "Erwarte 4 Data-Marken (je Sektor eine)";
}

/// Marke liegt auf dem A1-Byte (0xA1), NICHT auf 0xFE oder 0xFB.
TEST(RobotronTrack, MarkeLiegtAufA1_NichtAufFE_FB) {
    std::vector<LogicalSector> sektoren;
    for (uint8_t i = 1; i <= 4; ++i)
        sektoren.push_back(makeRobotronSector(0, 0, i, 128));

    TrackImage t = TrackCodec::buildRobotronTrack(sektoren);

    for (size_t i = 0; i < t.bytes.size(); ++i) {
        if (t.marks[i] == MarkType::Id || t.marks[i] == MarkType::Data) {
            EXPECT_EQ(t.bytes[i], 0xA1u)
                << "Marke bei Position " << i << " liegt nicht auf 0xA1 (Wert=0x"
                << std::hex << static_cast<unsigned>(t.bytes[i]) << ")";
        }
        // FE und FB dürfen KEINE Marke tragen
        if (t.bytes[i] == 0xFE || t.bytes[i] == 0xFB) {
            EXPECT_EQ(t.marks[i], MarkType::None)
                << "Byte 0x" << std::hex << static_cast<unsigned>(t.bytes[i])
                << " bei Position " << i << " trägt unerwartet eine Marke";
        }
    }
}

/// Nach jedem A1/Id folgt direkt 0xFE; nach jedem A1/Data folgt direkt 0xFB.
TEST(RobotronTrack, NachIdA1_FE_NachDataA1_FB) {
    std::vector<LogicalSector> sektoren;
    for (uint8_t i = 1; i <= 4; ++i)
        sektoren.push_back(makeRobotronSector(0, 0, i, 128));

    TrackImage t = TrackCodec::buildRobotronTrack(sektoren);

    for (size_t i = 0; i + 1 < t.bytes.size(); ++i) {
        if (t.marks[i] == MarkType::Id) {
            EXPECT_EQ(t.bytes[i + 1], 0xFEu)
                << "Nach Id-Marke bei " << i << " kein 0xFE";
        }
        if (t.marks[i] == MarkType::Data) {
            EXPECT_EQ(t.bytes[i + 1], 0xFBu)
                << "Nach Data-Marke bei " << i << " kein 0xFB";
        }
    }
}

/// Byte-Layout im IDAM-Feld: [A1/Id] [FE] [cyl] [head] [id] [sizecode].
TEST(RobotronTrack, ByteLayout_IDAM_4x128B) {
    std::vector<LogicalSector> sektoren;
    for (uint8_t i = 1; i <= 4; ++i)
        sektoren.push_back(makeRobotronSector(2, 1, i, 128));

    TrackImage t = TrackCodec::buildRobotronTrack(sektoren);

    size_t sektor_nr = 0;
    for (size_t i = 0; i + 5 < t.bytes.size(); ++i) {
        if (t.marks[i] == MarkType::Id) {
            EXPECT_EQ(t.bytes[i],     0xA1u) << "A1 erwartet bei " << i;
            EXPECT_EQ(t.bytes[i + 1], 0xFEu) << "FE erwartet bei " << i + 1;
            EXPECT_EQ(t.bytes[i + 2], 2u)    << "cyl=2 erwartet";
            EXPECT_EQ(t.bytes[i + 3], 1u)    << "head=1 erwartet";
            EXPECT_EQ(t.bytes[i + 4], sektor_nr + 1) << "id erwartet";
            EXPECT_EQ(t.bytes[i + 5], 0u)    << "sizecode=0 (128B)";
            ++sektor_nr;
        }
    }
    EXPECT_EQ(sektor_nr, 4u) << "Nicht alle 4 IDAM-Felder gefunden";
}

/// id_gap nach dem IDAM-Feld: bei 128B exakt 18 × 0x4E.
TEST(RobotronTrack, IdGap18_Bytes_128B) {
    auto sec = makeRobotronSector(0, 0, 1, 128);
    TrackImage t = TrackCodec::buildRobotronTrack({sec});

    // Suche den IDAM-A1, überspringe [A1 FE cyl head id sc] = 6 Bytes,
    // dann erwarte 18 × 0x4E.
    for (size_t i = 0; i < t.bytes.size(); ++i) {
        if (t.marks[i] == MarkType::Id) {
            // 6 Bytes Header + 18 Bytes Gap = 24 Bytes; dann A1/Data
            ASSERT_GE(t.bytes.size(), i + 6 + 18u);
            for (size_t g = 0; g < 18u; ++g) {
                EXPECT_EQ(t.bytes[i + 6 + g], 0x4Eu)
                    << "Gap-Byte " << g << " nach IDAM ist kein 0x4E";
            }
            // Das nächste Byte nach dem Gap muss die Data-Marke tragen (A1)
            EXPECT_EQ(t.marks[i + 6 + 18], MarkType::Data)
                << "Nach id_gap erwartet: A1 mit Data-Marke";
            break;
        }
    }
}

/// Daten-CRC für 128B-Sektor: crc16(data, 0xBF, 0x84).
TEST(RobotronTrack, DataCRC_128B_Seed_BF84) {
    auto sec = makeRobotronSector(0, 0, 1, 128, 0x42);
    TrackImage t = TrackCodec::buildRobotronTrack({sec});

    // Erwartete CRC
    uint16_t expected = TrackCodec::crc16(sec.data.data(), sec.data.size(), 0xBF, 0x84);

    // Im Track suchen: nach A1/Data [FB] [128 Bytes data] [CRC_hi CRC_lo]
    // Offset: [i]=A1, [i+1]=FB, [i+2..i+129]=data, [i+130]=CRC_hi, [i+131]=CRC_lo
    for (size_t i = 0; i < t.bytes.size(); ++i) {
        if (t.marks[i] == MarkType::Data) {
            ASSERT_GE(t.bytes.size(), i + 132u);
            uint8_t crc_hi = t.bytes[i + 130];
            uint8_t crc_lo = t.bytes[i + 131];
            uint16_t actual = static_cast<uint16_t>((crc_hi << 8) | crc_lo);
            EXPECT_EQ(actual, expected)
                << "Daten-CRC für 128B-Sektor stimmt nicht";
            break;
        }
    }
}

/// nextMark(0) liefert die Position des ersten A1 (Id-Marke).
TEST(RobotronTrack, NextMark0_LiefertErsteIdMarke) {
    std::vector<LogicalSector> sektoren;
    for (uint8_t i = 1; i <= 4; ++i)
        sektoren.push_back(makeRobotronSector(0, 0, i, 128));

    TrackImage t = TrackCodec::buildRobotronTrack(sektoren);
    ASSERT_FALSE(t.empty());

    size_t pos = t.nextMark(0);
    ASSERT_NE(pos, SIZE_MAX) << "Keine Marke im Track gefunden";

    EXPECT_EQ(t.bytes[pos], 0xA1u)
        << "nextMark(0) zeigt nicht auf 0xA1 (Robotron-Id-Marke)";
    EXPECT_EQ(t.marks[pos], MarkType::Id)
        << "nextMark(0) zeigt nicht auf Id-Marke";
    // Das folgende Byte muss 0xFE sein (IDAM)
    ASSERT_GT(t.bytes.size(), pos + 1);
    EXPECT_EQ(t.bytes[pos + 1], 0xFEu)
        << "Nach erstem A1/Id-Marke kein 0xFE";
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 2: 1024B-Sektor
// ─────────────────────────────────────────────────────────────────────────────

/// id_gap nach dem IDAM-Feld: bei 1024B exakt 27 × 0x4E.
TEST(RobotronTrack, IdGap27_Bytes_1024B) {
    auto sec = makeRobotronSector(1, 1, 3, 1024, 0x00);
    TrackImage t = TrackCodec::buildRobotronTrack({sec});

    for (size_t i = 0; i < t.bytes.size(); ++i) {
        if (t.marks[i] == MarkType::Id) {
            ASSERT_GE(t.bytes.size(), i + 6 + 27u);
            for (size_t g = 0; g < 27u; ++g) {
                EXPECT_EQ(t.bytes[i + 6 + g], 0x4Eu)
                    << "Gap-Byte " << g << " nach IDAM (1024B) ist kein 0x4E";
            }
            EXPECT_EQ(t.marks[i + 6 + 27], MarkType::Data)
                << "Nach id_gap (1024B) erwartet: A1 mit Data-Marke";
            break;
        }
    }
}

/// sizecode für 1024B-Sektor muss 3 sein.
TEST(RobotronTrack, SizeCode3_Fuer1024B) {
    auto sec = makeRobotronSector(0, 0, 1, 1024);
    TrackImage t = TrackCodec::buildRobotronTrack({sec});

    for (size_t i = 0; i + 5 < t.bytes.size(); ++i) {
        if (t.marks[i] == MarkType::Id) {
            EXPECT_EQ(t.bytes[i + 5], 3u)
                << "sizecode für 1024B muss 3 sein";
            break;
        }
    }
}

/// Daten-CRC für 1024B-Sektor: crc16([0xFB]+data, 0xCD, 0xB4).
TEST(RobotronTrack, DataCRC_1024B_Seed_CDB4) {
    auto sec = makeRobotronSector(1, 1, 3, 1024, 0x7F);
    TrackImage t = TrackCodec::buildRobotronTrack({sec});

    // Erwartete CRC: [FB] + data, Seed 0xCDB4
    std::vector<uint8_t> tmp;
    tmp.reserve(1 + sec.data.size());
    tmp.push_back(0xFB);
    tmp.insert(tmp.end(), sec.data.begin(), sec.data.end());
    uint16_t expected = TrackCodec::crc16(tmp.data(), tmp.size(), 0xCD, 0xB4);

    // Im Track: nach A1/Data [FB] [1024 Bytes] [CRC_hi CRC_lo]
    // Offset: [i]=A1, [i+1]=FB, [i+2..i+1025]=data, [i+1026]=CRC_hi, [i+1027]=CRC_lo
    for (size_t i = 0; i < t.bytes.size(); ++i) {
        if (t.marks[i] == MarkType::Data) {
            ASSERT_GE(t.bytes.size(), i + 1028u);
            uint8_t crc_hi = t.bytes[i + 1026];
            uint8_t crc_lo = t.bytes[i + 1027];
            uint16_t actual = static_cast<uint16_t>((crc_hi << 8) | crc_lo);
            EXPECT_EQ(actual, expected)
                << "Daten-CRC für 1024B-Sektor stimmt nicht (Seed 0xCDB4)";
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 3: Allgemeine Invarianten
// ─────────────────────────────────────────────────────────────────────────────

/// encoding des erzeugten Track ist immer MFM.
TEST(RobotronTrack, EncodingIstMFM) {
    auto sec = makeRobotronSector(0, 0, 1, 128);
    TrackImage t = TrackCodec::buildRobotronTrack({sec});
    EXPECT_EQ(t.encoding, Encoding::MFM);
}

/// bytes[] und marks[] haben immer gleiche Länge.
TEST(RobotronTrack, BytesUndMarksGleicheLaenge) {
    std::vector<LogicalSector> sektoren;
    for (uint8_t i = 1; i <= 4; ++i)
        sektoren.push_back(makeRobotronSector(0, 0, i, 128));

    TrackImage t = TrackCodec::buildRobotronTrack(sektoren);
    EXPECT_EQ(t.bytes.size(), t.marks.size());
}

/// Leere Sektor-Liste → leerer Track.
TEST(RobotronTrack, LeereSektorliste_LeerTrack) {
    TrackImage t = TrackCodec::buildRobotronTrack({});
    EXPECT_TRUE(t.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 4: Unberührtheit von buildTrack / parseTrack
// ─────────────────────────────────────────────────────────────────────────────

/// buildTrack (IBM-Standard) mit 128B-Sektor: Marke liegt weiterhin auf 0xFE.
TEST(RobotronTrack, BuildTrack_Unberuehrt_IdMarkAufFE) {
    LogicalSector sec;
    sec.cyl = 0; sec.head = 0; sec.id = 1; sec.size = 128;
    sec.data.assign(128, 0xE5);

    TrackImage t = TrackCodec::buildTrack({sec}, Encoding::MFM);

    bool gefunden = false;
    for (size_t i = 0; i < t.bytes.size(); ++i) {
        if (t.marks[i] == MarkType::Id) {
            EXPECT_EQ(t.bytes[i], 0xFEu)
                << "buildTrack: Id-Marke nicht auf 0xFE (IBM-Standard verletzt)";
            gefunden = true;
        }
    }
    EXPECT_TRUE(gefunden) << "buildTrack: keine Id-Marke gefunden";
}

/// parseTrack nach buildTrack liefert weiterhin korrekte CRCs.
TEST(RobotronTrack, ParseTrack_Unberuehrt_Roundtrip) {
    LogicalSector sec;
    sec.cyl = 3; sec.head = 1; sec.id = 2; sec.size = 128;
    sec.data.resize(128);
    for (size_t i = 0; i < 128; ++i) sec.data[i] = static_cast<uint8_t>(i);

    TrackImage t  = TrackCodec::buildTrack({sec}, Encoding::MFM);
    auto parsed   = TrackCodec::parseTrack(t);

    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_TRUE(parsed[0].id_crc_ok)   << "parseTrack: ID-CRC nach buildTrack fehlerhaft";
    EXPECT_TRUE(parsed[0].data_crc_ok) << "parseTrack: Daten-CRC nach buildTrack fehlerhaft";
    EXPECT_EQ(parsed[0].data, sec.data);
}

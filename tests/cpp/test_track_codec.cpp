/**
 * @file test_track_codec.cpp
 * @brief GoogleTests für TrackCodec (FM/MFM-Spur aufbauen/parsen) und TrackImage::nextMark.
 *
 * @details
 * Getestete Komponenten:
 *   - TrackCodec::crc16 (Robotron-CRC-Vereinheitlichung)
 *   - TrackCodec::crc16Ccitt
 *   - TrackCodec::buildTrack / parseTrack (MFM + FM, verschiedene Sektorgrößen)
 *   - TrackImage::nextMark (zyklische Markensuche)
 *
 * ERSTES Ziel: CRC-Vereinheitlichung empirisch absichern (Abschnitt unten).
 *
 * @see core/peripherals/floppy_drive/track_codec.h
 * @see core/peripherals/floppy_drive/track_image.h
 * @see doc/refactoring_floppy_emulator.md §4.1
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <vector>

#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/track_image.h"

// ─── Hilfsfunktion: einfachen LogicalSector anlegen ──────────────────────────

static LogicalSector makeSector(uint8_t cyl, uint8_t head, uint8_t id,
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
// GRUPPE 1: CRC-Vereinheitlichung
// Absicherung, dass eine einzige CRC über [A1,A1,A1,FB,...] 0xFF,0xFF die
// frühere Zwei-Seed-Logik der alten K5122 reproduziert.
// ─────────────────────────────────────────────────────────────────────────────

TEST(TrackCodecCrc, Vereinheitlichung_A1A1A1_gibt_CDB4) {
    // Zustand der CRC nach [A1,A1,A1] mit Seed 0xFF,0xFF muss 0xCDB4 ergeben.
    // Das ist der alte Seed, mit dem loaderCrc16 über [0xFB]+data gestartet wurde.
    const uint8_t in[] = {0xA1, 0xA1, 0xA1};
    EXPECT_EQ(TrackCodec::crc16(in, 3, 0xFF, 0xFF), 0xCDB4u);
}

TEST(TrackCodecCrc, Vereinheitlichung_A1A1A1FB_tatsaecklicherWert) {
    // BEFUND: crc16([A1,A1,A1,FB], 4, 0xFF,0xFF) ergibt 0xE295, NICHT 0xBF84.
    // Die Vereinheitlichung über [A1,A1,A1,FB] → 0xBF84 gilt NICHT — die
    // Präambel-CRC-Kette hat an Position 4 (nach FB) einen anderen Zustand.
    // buildTrack/parseTrack verwenden daher den Boot-kompatiblen Seed 0xBF84 direkt
    // über die reinen Datenbytes (nicht über die Präambel).
    const uint8_t in[] = {0xA1, 0xA1, 0xA1, 0xFB};
    EXPECT_EQ(TrackCodec::crc16(in, 4, 0xFF, 0xFF), 0xE295u);  // tatsächlicher Wert
}

TEST(TrackCodecCrc, CDB4_Pfad_aequivalent_zu_Seed_0xFF) {
    // GILT: loaderCrc16([FB]+data, 0xCDB4) == loaderCrc16([A1A1A1FB]+data, 0xFFFF).
    // Der CDB4-Pfad ist äquivalent zu Seed 0xFFFF über [A1,A1,A1,FB]+data.
    std::vector<uint8_t> data(128, 0xAB);

    // Alter Pfad: Seed 0xCDB4 über [FB] + data
    std::vector<uint8_t> old_input;
    old_input.push_back(0xFB);
    old_input.insert(old_input.end(), data.begin(), data.end());
    uint16_t old_crc = TrackCodec::crc16(old_input.data(), old_input.size(), 0xCD, 0xB4);

    // Äquivalenter Pfad: Seed 0xFFFF über [A1,A1,A1,FB] + data
    std::vector<uint8_t> new_input;
    new_input.push_back(0xA1); new_input.push_back(0xA1);
    new_input.push_back(0xA1); new_input.push_back(0xFB);
    new_input.insert(new_input.end(), data.begin(), data.end());
    uint16_t new_crc = TrackCodec::crc16(new_input.data(), new_input.size(), 0xFF, 0xFF);

    EXPECT_EQ(old_crc, new_crc);
}

TEST(TrackCodecCrc, BF84_NICHT_aequivalent_zu_A1A1A1FB_Seed0xFF) {
    // BEFUND: crc16(data, 0xBF,0x84) ist NICHT gleich crc16([A1A1A1FB]+data, 0xFF,0xFF).
    // Die beiden Pfade divergieren (0xBF84 ist der Seed NACH dem DAM-FB-Byte,
    // aber crc16([A1A1A1FB],4,0xFF,0xFF)==0xE295, nicht 0xBF84).
    // buildTrack/parseTrack verwenden daher den direkten Seed 0xBF84 über Datenbytes.
    std::vector<uint8_t> data(128, 0xCD);

    uint16_t bf84_crc = TrackCodec::crc16(data.data(), data.size(), 0xBF, 0x84);

    std::vector<uint8_t> new_input;
    new_input.push_back(0xA1); new_input.push_back(0xA1);
    new_input.push_back(0xA1); new_input.push_back(0xFB);
    new_input.insert(new_input.end(), data.begin(), data.end());
    uint16_t preamble_crc = TrackCodec::crc16(new_input.data(), new_input.size(), 0xFF, 0xFF);

    // Nicht äquivalent — Dokumentation des tatsächlichen Befundes
    EXPECT_NE(bf84_crc, preamble_crc);
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 2: MFM buildTrack → parseTrack Roundtrip (verschiedene Größen)
// ─────────────────────────────────────────────────────────────────────────────

class MfmRoundtrip : public ::testing::TestWithParam<uint16_t> {};

TEST_P(MfmRoundtrip, RoundtripBitgleich) {
    const uint16_t secSize = GetParam();
    // Sektor mit wiedererkennbarem Muster
    LogicalSector sec = makeSector(3, 1, 2, secSize, 0x55);
    for (size_t i = 0; i < sec.data.size(); ++i)
        sec.data[i] = static_cast<uint8_t>(i & 0xFF);

    TrackImage track = TrackCodec::buildTrack({sec}, Encoding::MFM);
    ASSERT_FALSE(track.empty());
    ASSERT_EQ(track.encoding, Encoding::MFM);

    auto parsed = TrackCodec::parseTrack(track);
    ASSERT_EQ(parsed.size(), 1u);

    const auto& ps = parsed[0];
    EXPECT_TRUE(ps.id_crc_ok)   << "ID-CRC fehlerhaft (size=" << secSize << ")";
    EXPECT_TRUE(ps.data_crc_ok) << "Daten-CRC fehlerhaft (size=" << secSize << ")";
    EXPECT_EQ(ps.cyl,  sec.cyl);
    EXPECT_EQ(ps.head, sec.head);
    EXPECT_EQ(ps.id,   sec.id);
    EXPECT_EQ(ps.size, secSize);
    ASSERT_EQ(ps.data.size(), secSize);
    EXPECT_EQ(ps.data, sec.data) << "Datenbytes weichen ab (size=" << secSize << ")";
}

INSTANTIATE_TEST_SUITE_P(
    Sekorgroessen, MfmRoundtrip,
    ::testing::Values(128u, 256u, 512u, 1024u));

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 3: Gemischte Spur (128B + 1024B Sektor), MFM
// ─────────────────────────────────────────────────────────────────────────────

TEST(TrackCodecMfm, GemischteSpur_128_und_1024) {
    LogicalSector s1 = makeSector(0, 0, 1, 128, 0xAA);
    LogicalSector s2 = makeSector(0, 0, 2, 1024, 0xBB);

    TrackImage track = TrackCodec::buildTrack({s1, s2}, Encoding::MFM);
    ASSERT_FALSE(track.empty());

    auto parsed = TrackCodec::parseTrack(track);
    ASSERT_EQ(parsed.size(), 2u);

    EXPECT_TRUE(parsed[0].id_crc_ok);
    EXPECT_TRUE(parsed[0].data_crc_ok);
    EXPECT_EQ(parsed[0].size, 128u);
    EXPECT_EQ(parsed[0].data, s1.data);

    EXPECT_TRUE(parsed[1].id_crc_ok);
    EXPECT_TRUE(parsed[1].data_crc_ok);
    EXPECT_EQ(parsed[1].size, 1024u);
    EXPECT_EQ(parsed[1].data, s2.data);
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 4: FM-Roundtrip (128B)
// ─────────────────────────────────────────────────────────────────────────────

TEST(TrackCodecFm, Roundtrip128B) {
    LogicalSector sec = makeSector(1, 0, 3, 128, 0xC3);
    TrackImage track = TrackCodec::buildTrack({sec}, Encoding::FM);
    ASSERT_FALSE(track.empty());
    ASSERT_EQ(track.encoding, Encoding::FM);

    // gap_fill = 0xFF vorhanden
    bool hatGapFF = std::find(track.bytes.begin(), track.bytes.end(), 0xFF)
                    != track.bytes.end();
    EXPECT_TRUE(hatGapFF) << "FM-Spur enthält kein Gap-Füllbyte 0xFF";

    // Kein 0xA1 vor den Marken (FM hat keinen A1-Sync)
    for (size_t i = 0; i < track.bytes.size(); ++i) {
        if (track.marks[i] == MarkType::Id || track.marks[i] == MarkType::Data) {
            // Byte direkt vor der Marke darf kein 0xA1 sein (sonst wäre es MFM-Sync)
            if (i > 0) {
                EXPECT_NE(track.bytes[i - 1], 0xA1u)
                    << "0xA1 direkt vor FM-Marke bei Position " << i;
            }
        }
    }

    auto parsed = TrackCodec::parseTrack(track);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_TRUE(parsed[0].id_crc_ok);
    EXPECT_TRUE(parsed[0].data_crc_ok);
    EXPECT_EQ(parsed[0].cyl,  sec.cyl);
    EXPECT_EQ(parsed[0].head, sec.head);
    EXPECT_EQ(parsed[0].id,   sec.id);
    EXPECT_EQ(parsed[0].size, 128u);
    EXPECT_EQ(parsed[0].data, sec.data);
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 5: marks[] an erwarteten Positionen
// ─────────────────────────────────────────────────────────────────────────────

TEST(TrackCodecMarks, MfmIdMarkAuf0xFE) {
    LogicalSector sec = makeSector(0, 0, 1, 128);
    TrackImage track = TrackCodec::buildTrack({sec}, Encoding::MFM);

    // Suche nach Id-Marke — das Byte muss 0xFE sein
    bool gefunden = false;
    for (size_t i = 0; i < track.bytes.size(); ++i) {
        if (track.marks[i] == MarkType::Id) {
            EXPECT_EQ(track.bytes[i], 0xFEu)
                << "Id-Marke nicht auf 0xFE bei Position " << i;
            gefunden = true;
        }
    }
    EXPECT_TRUE(gefunden) << "Keine Id-Marke in der Spur gefunden";
}

TEST(TrackCodecMarks, MfmDataMarkAuf0xFB) {
    LogicalSector sec = makeSector(0, 0, 1, 128);
    TrackImage track = TrackCodec::buildTrack({sec}, Encoding::MFM);

    bool gefunden = false;
    for (size_t i = 0; i < track.bytes.size(); ++i) {
        if (track.marks[i] == MarkType::Data) {
            EXPECT_EQ(track.bytes[i], 0xFBu)
                << "Data-Marke nicht auf 0xFB bei Position " << i;
            gefunden = true;
        }
    }
    EXPECT_TRUE(gefunden) << "Keine Data-Marke in der Spur gefunden";
}

TEST(TrackCodecMarks, MfmKeinMarkAufA1Sync) {
    // Die drei A1-Sync-Bytes vor dem Mark-Byte dürfen keine Marke tragen
    LogicalSector sec = makeSector(0, 0, 1, 128);
    TrackImage track = TrackCodec::buildTrack({sec}, Encoding::MFM);

    for (size_t i = 0; i < track.bytes.size(); ++i) {
        if (track.marks[i] == MarkType::Id || track.marks[i] == MarkType::Data) {
            // Direkt davor liegen zwei A1-Bytes ohne Marke
            if (i >= 2) {
                EXPECT_EQ(track.marks[i - 1], MarkType::None)
                    << "A1 vor Mark-Byte hat unerwartet eine Marke (pos=" << i - 1 << ")";
                EXPECT_EQ(track.marks[i - 2], MarkType::None)
                    << "A1 vor Mark-Byte hat unerwartet eine Marke (pos=" << i - 2 << ")";
            }
        }
    }
}

TEST(TrackCodecMarks, MfmIndexMarkVorhanden) {
    LogicalSector sec = makeSector(0, 0, 1, 128);
    GapParams g = TrackCodec::gapsFor(Encoding::MFM);
    g.with_iam = true;
    TrackImage track = TrackCodec::buildTrack({sec}, Encoding::MFM, g);

    bool gefunden = false;
    for (size_t i = 0; i < track.bytes.size(); ++i) {
        if (track.marks[i] == MarkType::Index) {
            EXPECT_EQ(track.bytes[i], 0xFCu);
            gefunden = true;
        }
    }
    EXPECT_TRUE(gefunden) << "Keine Index-Marke (IAM) gefunden";
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 6: TrackImage::nextMark
// ─────────────────────────────────────────────────────────────────────────────

TEST(TrackImageNextMark, LeereSpur_gibtSIZE_MAX) {
    TrackImage t;
    EXPECT_EQ(t.nextMark(0), SIZE_MAX);
    EXPECT_EQ(t.nextMark(0, MarkType::Id), SIZE_MAX);
}

TEST(TrackImageNextMark, KeinMarke_gibtSIZE_MAX) {
    TrackImage t;
    t.bytes = {0x4E, 0x4E, 0x4E};
    t.marks = {MarkType::None, MarkType::None, MarkType::None};
    EXPECT_EQ(t.nextMark(0), SIZE_MAX);
}

TEST(TrackImageNextMark, FindetNaechsteMarkeVonPos) {
    // Spur: [None, Id, None, Data, None]
    TrackImage t;
    t.bytes = {0x4E, 0xFE, 0x4E, 0xFB, 0x4E};
    t.marks = {MarkType::None, MarkType::Id, MarkType::None,
               MarkType::Data, MarkType::None};

    // Ab 0: nächste beliebige Marke = Position 1 (Id)
    EXPECT_EQ(t.nextMark(0), 1u);
    // Ab 1: nächste beliebige Marke = Position 1 selbst
    EXPECT_EQ(t.nextMark(1), 1u);
    // Ab 2: nächste beliebige Marke = Position 3 (Data)
    EXPECT_EQ(t.nextMark(2), 3u);
}

TEST(TrackImageNextMark, TypfilterUebersprUnpassendeMarken) {
    // Spur: [Id, Data, Id]
    TrackImage t;
    t.bytes = {0xFE, 0xFB, 0xFE};
    t.marks = {MarkType::Id, MarkType::Data, MarkType::Id};

    // Ab 0, Id: Position 0
    EXPECT_EQ(t.nextMark(0, MarkType::Id), 0u);
    // Ab 1, Id: Position 2
    EXPECT_EQ(t.nextMark(1, MarkType::Id), 2u);
    // Ab 0, Data: Position 1
    EXPECT_EQ(t.nextMark(0, MarkType::Data), 1u);
}

TEST(TrackImageNextMark, ZyklischerUmlauf) {
    // Spur: [None, None, Id] — ab Position 0 nach Data (nicht vorhanden) → SIZE_MAX
    TrackImage t;
    t.bytes = {0x4E, 0x4E, 0xFE};
    t.marks = {MarkType::None, MarkType::None, MarkType::Id};

    // Ab 0, beliebig → zyklisch zur Id bei 2
    EXPECT_EQ(t.nextMark(0), 2u);

    // Ab 0, Data → SIZE_MAX (keine Data-Marke vorhanden)
    EXPECT_EQ(t.nextMark(0, MarkType::Data), SIZE_MAX);

    // Ab 0 auf Id: 2
    EXPECT_EQ(t.nextMark(0, MarkType::Id), 2u);

    // Ab 2, Id: 2 selbst (Startposition passt)
    EXPECT_EQ(t.nextMark(2, MarkType::Id), 2u);
}

TEST(TrackImageNextMark, ZyklischNachEndeWraparound) {
    // Spur: [Id, None, None] — ab Position 1 zyklisch: 2 → 0 (Umlauf)
    TrackImage t;
    t.bytes = {0xFE, 0x4E, 0x4E};
    t.marks = {MarkType::Id, MarkType::None, MarkType::None};

    // Ab 1: muss zur 0 zurück
    EXPECT_EQ(t.nextMark(1, MarkType::Id), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 7: Mehrere Sektoren MFM — vollständiger Roundtrip
// ─────────────────────────────────────────────────────────────────────────────

TEST(TrackCodecMfm, DreiSektoren128B_AlleGruen) {
    std::vector<LogicalSector> sektoren;
    for (uint8_t i = 1; i <= 3; ++i) {
        LogicalSector s = makeSector(2, 0, i, 128);
        // Wiedererkennbares Muster je Sektor
        for (size_t j = 0; j < s.data.size(); ++j)
            s.data[j] = static_cast<uint8_t>((i * 16 + j) & 0xFF);
        sektoren.push_back(std::move(s));
    }

    TrackImage track = TrackCodec::buildTrack(sektoren, Encoding::MFM);
    auto parsed = TrackCodec::parseTrack(track);

    ASSERT_EQ(parsed.size(), 3u);
    for (size_t k = 0; k < 3; ++k) {
        EXPECT_TRUE(parsed[k].id_crc_ok)   << "Sektor " << k << ": ID-CRC fehler";
        EXPECT_TRUE(parsed[k].data_crc_ok) << "Sektor " << k << ": Daten-CRC fehler";
        EXPECT_EQ(parsed[k].data, sektoren[k].data)
            << "Sektor " << k << ": Datenbytes weichen ab";
    }
}

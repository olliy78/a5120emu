/**
 * @file test_track_codec.cpp
 * @brief GoogleTests für TrackCodec (FM/MFM-Spur aufbauen/parsen) und TrackImage::nextMark.
 *
 * @details
 * Getestete Komponenten:
 *   - TrackCodec::crc16 (Standard-IBM-CRC-16-CCITT)
 *   - TrackCodec::crc16Ccitt
 *   - TrackCodec::buildTrack / parseTrack (MFM + FM, verschiedene Sektorgrößen)
 *   - TrackImage::nextMark (zyklische Markensuche)
 *
 * ERSTES Ziel: die Standard-CCITT-CRC empirisch absichern (Abschnitt unten).
 *
 * @see core/peripherals/floppy_drive/track_codec.h
 * @see core/peripherals/floppy_drive/track_image.h
 * @see doc/design/07_k5122_afs.md
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
// GRUPPE 1: CRC-16-CCITT-Zwischenzustände
// Absicherung, dass die Standard-CCITT-crc16 die erwarteten Zwischenwerte an den
// Feldgrenzen liefert (eine CRC über [A1,A1,A1,FB,...] mit Seed 0xFF,0xFF).
// ─────────────────────────────────────────────────────────────────────────────

TEST(TrackCodecCrc, CCITT_Zustand_nach_A1A1A1_ist_CDB4) {
    // CCITT-Zustand nach der 3×A1-Sync-Präambel (Seed 0xFF,0xFF) ist 0xCDB4.
    const uint8_t in[] = {0xA1, 0xA1, 0xA1};
    EXPECT_EQ(TrackCodec::crc16(in, 3, 0xFF, 0xFF), 0xCDB4u);
}

TEST(TrackCodecCrc, MFM_DatenCrc_Seed_A1A1A1FB_ist_E295) {
    // Standard-IBM-MFM-Daten-CRC: crc16([A1,A1,A1,FB], 0xFF,0xFF) == 0xE295 —
    // Standard-CCITT über [A1,A1,A1,FB]+data.
    const uint8_t in[] = {0xA1, 0xA1, 0xA1, 0xFB};
    EXPECT_EQ(TrackCodec::crc16(in, 4, 0xFF, 0xFF), 0xE295u);
}

TEST(TrackCodecCrc, CCITT_ist_praeambelunabhaengig) {
    // Standard-CCITT: die CRC über [A1,A1,A1,FB]+data ab Seed 0xFFFF entspricht der
    // Fortführung ab dem Zwischenzustand nach [A1,A1,A1] über [FB]+data.
    std::vector<uint8_t> data(128, 0xAB);

    // Fortführung ab dem CCITT-Zwischenzustand nach [A1,A1,A1] über [FB]+data
    std::vector<uint8_t> old_input;
    old_input.push_back(0xFB);
    old_input.insert(old_input.end(), data.begin(), data.end());
    uint16_t old_crc = TrackCodec::crc16(old_input.data(), old_input.size(), 0xCD, 0xB4);

    // Volle CRC: Seed 0xFFFF über [A1,A1,A1,FB] + data
    std::vector<uint8_t> new_input;
    new_input.push_back(0xA1); new_input.push_back(0xA1);
    new_input.push_back(0xA1); new_input.push_back(0xFB);
    new_input.insert(new_input.end(), data.begin(), data.end());
    uint16_t new_crc = TrackCodec::crc16(new_input.data(), new_input.size(), 0xFF, 0xFF);

    EXPECT_EQ(old_crc, new_crc);
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

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE: Sektor-Nachspann (LogicalSector::tail) — der UDOS-Sektorkontrollblock
//
// UDOS/ZDOS legt die Dateiverkettung NICHT im Datenfeld ab, sondern in 4 Bytes
// unmittelbar hinter der Daten-CRC (doc/udos_diskettenformat.md §1.1).  Baut
// buildTrack() eine Spur aus geparsten Sektoren neu auf — das tut der K5122 bei
// JEDEM Schreibzugriff —, muss dieser Nachspann erhalten bleiben, sonst verlieren
// auch die nicht angefassten Sektoren ihre Verkettung („POINTER CHECK ERROR CA",
// doc/udos_bug1.md).
// ─────────────────────────────────────────────────────────────────────────────

TEST(TrackCodecTail, BuildParse_ErhaeltUdosKontrollblock) {
    std::vector<LogicalSector> sektoren;
    for (uint8_t i = 1; i <= 3; ++i) {
        LogicalSector s = makeSector(22, 0, i, 128, static_cast<uint8_t>(i));
        // UDOS-artiger Kontrollblock: zurueck (5,22), vor (i+4,22), dann Gap-Naht
        s.tail = {0x05, 0x16, static_cast<uint8_t>(i + 4), 0x16, 0x41, 0xFF, 0x4E, 0x4E};
        sektoren.push_back(std::move(s));
    }

    auto parsed = TrackCodec::parseTrack(TrackCodec::buildTrack(sektoren, Encoding::MFM));
    ASSERT_EQ(parsed.size(), 3u);
    for (size_t k = 0; k < 3; ++k) {
        EXPECT_TRUE(parsed[k].data_crc_ok) << "Sektor " << k;
        EXPECT_EQ(parsed[k].data, sektoren[k].data) << "Sektor " << k;
        EXPECT_EQ(parsed[k].tail, sektoren[k].tail)
            << "Sektor " << k << ": Kontrollblock hinter der Daten-CRC verloren";
    }
}

TEST(TrackCodecTail, FM_ErhaeltNachspannEbenso) {
    LogicalSector s = makeSector(3, 0, 1, 128);
    s.tail = {0x01, 0x02, 0x03, 0x04, 0xFF, 0xFF, 0xFF, 0xFF};
    auto parsed = TrackCodec::parseTrack(TrackCodec::buildTrack({s}, Encoding::FM));
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_TRUE(parsed[0].data_crc_ok);
    EXPECT_EQ(parsed[0].tail, s.tail);
}

TEST(TrackCodecTail, OhneTail_BitgleichZuReinemGap) {
    // Frisch erzeugte Sektoren (DiskImage::create, Formatierstrom) haben KEINEN
    // tail — dort muss buildTrack byteweise dasselbe liefern wie vor der
    // Nachspann-Ausgabe, sonst aendert sich jede Standard-IBM-Spur.
    std::vector<LogicalSector> ohne;
    for (uint8_t i = 1; i <= 4; ++i) ohne.push_back(makeSector(0, 0, i, 256));

    std::vector<LogicalSector> mit_gap = ohne;
    const uint8_t gap = TrackCodec::gapsFor(Encoding::MFM).gap_fill;
    for (auto& s : mit_gap) s.tail.assign(kSectorTailBytes, gap);

    const TrackImage a = TrackCodec::buildTrack(ohne, Encoding::MFM);
    const TrackImage b = TrackCodec::buildTrack(mit_gap, Encoding::MFM);
    EXPECT_EQ(a.bytes, b.bytes);
    EXPECT_EQ(a.marks, b.marks);
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE: ROM-Lese-Kalibrierung (Phase 1 FM/MFM-Umbau, doc/design/07 §10.5.1)
//
// Das ZRE-Boot-ROM liest nach einem Resync (MK/MK1-Strobe) im IDAM-Suchpfad
// (STROBE_LOOP_BODY @025A → buf[0] @0261, dann):
//   - FM  (NZ-Pfad @020B): buf[0] verworfen, buf[1] mit FE verglichen.
//   - MFM (Z-Pfad  @0205): buf[0..3] verworfen, buf[4] mit FE verglichen.
// Damit das IDAM matcht, muss der Resync NICHT auf das FE-Mark-Byte landen,
// sondern davor:  FM → markPos-1,  MFM → markPos-4 (1 Sync-Byte + 3×A1).
// Empirisch via boot_trace -z bestätigt (FM-Default: buf[0]=A1, buf[1]=FE).
// Diese Tests sichern, dass buildTrack genau dieses Layout liefert (Grundlage
// für die Resync-Offset-Logik im K5122, Phase 2).
// ─────────────────────────────────────────────────────────────────────────────

static size_t findIdMark(const TrackImage& t) {
    for (size_t i = 0; i < t.marks.size(); ++i)
        if (t.marks[i] == MarkType::Id) return i;
    return SIZE_MAX;
}

TEST(RomReadKalibrierung, FM_NZPfad_buf1_ist_FE) {
    TrackImage t = TrackCodec::buildTrack({makeSector(0, 0, 1, 128)}, Encoding::FM);
    size_t mark = findIdMark(t);
    ASSERT_NE(mark, SIZE_MAX);
    ASSERT_GE(mark, size_t{1});
    EXPECT_EQ(t.bytes[mark], 0xFE);
    // FM: kein A1; Resync landet 1 Byte vor FE → buf[0]=Sync (verworfen), buf[1]=FE.
    const size_t r = mark - 1;
    EXPECT_EQ(t.bytes[r + 0], 0x00) << "buf[0] (verworfen) = Sync-Byte";
    EXPECT_EQ(t.bytes[r + 1], 0xFE) << "buf[1] = IDAM-Marke (CP B == 0xFE im ROM)";
}

TEST(RomReadKalibrierung, MFM_ZPfad_buf4_ist_FE_nach_3xA1) {
    TrackImage t = TrackCodec::buildTrack({makeSector(0, 0, 1, 128)}, Encoding::MFM);
    size_t mark = findIdMark(t);
    ASSERT_NE(mark, SIZE_MAX);
    ASSERT_GE(mark, size_t{4});
    EXPECT_EQ(t.bytes[mark], 0xFE);
    // MFM: 3×A1 vor FE; Resync landet 4 Byte vor FE (1 Sync + 3×A1).
    const size_t r = mark - 4;
    EXPECT_EQ(t.bytes[r + 0], 0x00) << "buf[0] (verworfen) = Sync-Byte vor A1A1A1";
    EXPECT_EQ(t.bytes[r + 1], 0xA1) << "buf[1] = A1";
    EXPECT_EQ(t.bytes[r + 2], 0xA1) << "buf[2] = A1";
    EXPECT_EQ(t.bytes[r + 3], 0xA1) << "buf[3] = A1";
    EXPECT_EQ(t.bytes[r + 4], 0xFE) << "buf[4] = IDAM-Marke (CP B == 0xFE im ROM)";
}

// romReadResyncTarget: die K5122-Resync-Logik (Offset + Encoding-Gate + Legacy-A1).

TEST(RomReadKalibrierung, ResyncTarget_FM_Match_markPos_minus_1) {
    TrackImage t = TrackCodec::buildTrack({makeSector(0, 0, 1, 128)}, Encoding::FM);
    size_t mark = findIdMark(t);
    // Resync zur Id-Marke (von ihr aus gesucht; nextMark schließt pos ein).
    size_t r = TrackCodec::romReadResyncTarget(t, mark, Encoding::FM);
    ASSERT_NE(r, SIZE_MAX);
    EXPECT_EQ(r, mark - 1);
    EXPECT_EQ(t.bytes[r + 1], 0xFE);   // buf[1] = FE
}

TEST(RomReadKalibrierung, ResyncTarget_MFM_Match_markPos_minus_4) {
    TrackImage t = TrackCodec::buildTrack({makeSector(0, 0, 1, 128)}, Encoding::MFM);
    size_t mark = findIdMark(t);
    size_t r = TrackCodec::romReadResyncTarget(t, mark, Encoding::MFM);
    ASSERT_NE(r, SIZE_MAX);
    EXPECT_EQ(r, mark - 4);
    EXPECT_EQ(t.bytes[r + 4], 0xFE);   // buf[4] = FE
}

// buildFaithfulReadTrack ist der tatsächlich gestreamte Boot-Lese-Stream: 4×A1-Sync, der
// gemeinsame Modus, den ROM-Boot-Read (1 Wegwerf + 3 Reads, FE bei buf[4]) UND SYL-Lader
// (skip-A1-Schleife) gleichzeitig bedienen.

TEST(RomReadKalibrierung, FaithfulRead_MFM_4xA1_buf4_ist_FE) {
    TrackImage t = TrackCodec::buildFaithfulReadTrack({makeSector(0, 0, 1, 128)}, Encoding::MFM);
    size_t mark = findIdMark(t);
    ASSERT_NE(mark, SIZE_MAX);
    ASSERT_GE(mark, size_t{4});
    EXPECT_EQ(t.bytes[mark], 0xFE);
    // MFM: 4×A1 vor FE; Resync landet auf dem ERSTEN A1 (markPos-4).
    size_t r = TrackCodec::romReadResyncTarget(t, mark, Encoding::MFM);
    ASSERT_NE(r, SIZE_MAX);
    EXPECT_EQ(r, mark - 4);
    EXPECT_EQ(t.bytes[r + 0], 0xA1) << "buf[0] = A1 (ROM verwirft, SYL überspringt)";
    EXPECT_EQ(t.bytes[r + 1], 0xA1) << "buf[1] = A1";
    EXPECT_EQ(t.bytes[r + 2], 0xA1) << "buf[2] = A1";
    EXPECT_EQ(t.bytes[r + 3], 0xA1) << "buf[3] = A1";
    EXPECT_EQ(t.bytes[r + 4], 0xFE) << "buf[4] = IDAM-Marke (CP B == 0xFE im ROM)";
}

TEST(RomReadKalibrierung, FaithfulRead_FM_buf1_ist_FE) {
    TrackImage t = TrackCodec::buildFaithfulReadTrack({makeSector(0, 0, 1, 128)}, Encoding::FM);
    size_t mark = findIdMark(t);
    ASSERT_NE(mark, SIZE_MAX);
    ASSERT_GE(mark, size_t{1});
    EXPECT_EQ(t.bytes[mark], 0xFE);   // FM: kein A1, Marke direkt
    size_t r = TrackCodec::romReadResyncTarget(t, mark, Encoding::FM);
    ASSERT_NE(r, SIZE_MAX);
    EXPECT_EQ(r, mark - 1);
    EXPECT_EQ(t.bytes[r + 1], 0xFE) << "buf[1] = FE";
}

TEST(RomReadKalibrierung, FaithfulRead_StandardCrc_parseTrackValidiert) {
    // buildFaithfulReadTrack nutzt die Standard-IBM-CCITT-CRC →
    // parseTrack akzeptiert IDAM- und Daten-CRC.
    TrackImage t = TrackCodec::buildFaithfulReadTrack({makeSector(3, 1, 2, 1024)}, Encoding::MFM);
    auto secs = TrackCodec::parseTrack(t);
    ASSERT_EQ(secs.size(), 1u);
    EXPECT_TRUE(secs[0].id_crc_ok);
    EXPECT_TRUE(secs[0].data_crc_ok);
}

/**
 * @test TrackCodec/ParseTrack_VerfaelschtesGroessenfeldStuerztNichtAb
 * @brief Ein verfälschtes Größenfeld in der Adressmarke darf keine unmögliche
 *        Sektorgröße erzeugen.
 *
 * Das Größenfeld der IBM-IDAM ist 2 Bit breit (0..3 = 128..1024 B).  Auf einer
 * gestörten Spur (Schreibabbruch, halb formatierte Diskette) steht dort aber ein
 * beliebiges Byte.  Ohne Maske lieferte `parseTrack` daraus z. B. 4096 B, und der
 * anschließende Neuaufbau der Spur brach mit `std::invalid_argument`
 * („TrackCodec: ungültige Sektorgröße") ab — als **unbehandelte** Ausnahme, die
 * den kompletten Emulator beendete (beobachtet beim Formatieren einer real
 * eingelesenen Leerdiskette).  Ab Schiebeweiten ≥ 32 war es zusätzlich
 * undefiniertes Verhalten.
 */
TEST(TrackCodec, ParseTrack_VerfaelschtesGroessenfeldStuerztNichtAb) {
    std::vector<LogicalSector> secs;
    for (uint8_t id = 1; id <= 2; ++id) {
        LogicalSector ls;
        ls.cyl = 0; ls.head = 0; ls.id = id; ls.size = 256;
        ls.data.assign(256, static_cast<uint8_t>(0x30 + id));
        secs.push_back(std::move(ls));
    }
    TrackImage t = TrackCodec::buildTrack(secs, Encoding::MFM);

    // Größenfeld der ersten Adressmarke verfälschen (Byte 4 hinter der Id-Marke).
    const size_t idPos = t.nextMark(0, MarkType::Id);
    ASSERT_NE(idPos, SIZE_MAX);
    ASSERT_LT(idPos + 4, t.bytes.size());
    t.bytes[idPos + 4] = 0xFD;          // unmaskiert wäre das 128<<253

    std::vector<LogicalSector> parsed;
    ASSERT_NO_THROW(parsed = TrackCodec::parseTrack(t));
    ASSERT_FALSE(parsed.empty());

    // Jede gemeldete Größe muss eine echte IBM-Sektorgröße sein …
    for (const auto& s : parsed)
        EXPECT_TRUE(s.size == 128 || s.size == 256 || s.size == 512 || s.size == 1024)
            << "unmögliche Sektorgröße " << s.size;
    // … und der verfälschte Sektor faellt ueber die ID-CRC auf.
    EXPECT_FALSE(parsed.front().id_crc_ok);

    // Der Neuaufbau darf ebenfalls nicht werfen (das war der Absturzpfad).
    EXPECT_NO_THROW((void)TrackCodec::buildTrack(parsed, Encoding::MFM));
    EXPECT_NO_THROW((void)TrackCodec::buildFaithfulReadTrack(parsed, Encoding::MFM));
}

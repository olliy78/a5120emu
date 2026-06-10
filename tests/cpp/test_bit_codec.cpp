/**
 * @file test_bit_codec.cpp
 * @brief GoogleTests für BitCodec (MFM/FM-Bitzellen encode/decode).
 *
 * Getestete Eigenschaften:
 *   1. encode∘decode-Identität (MFM)
 *   2. encode∘decode-Identität (FM)
 *   3. Sync-Erkennung und Marken-Tagging
 *   4. Daten-0xA1/0xFE werden NICHT fälschlich als Marke erkannt
 *      (Kernbeweis des Cell-Level-Marken-Modells)
 *   5. bitcells/Längen-Invarianten
 *
 * @see core/peripherals/floppy_drive/bit_codec.h
 * @see core/peripherals/floppy_drive/track_codec.h
 * @see doc/refactoring_floppy_emulator.md §5
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#include "core/peripherals/floppy_drive/bit_codec.h"
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
// GRUPPE 1: encode∘decode-Identität (MFM)
// ─────────────────────────────────────────────────────────────────────────────

TEST(BitCodecMfm, EncodeDecodeIdentitaet_1Sektor128B) {
    LogicalSector sec = makeSector(0, 0, 1, 128, 0x5A);
    for (size_t i = 0; i < sec.data.size(); ++i)
        sec.data[i] = static_cast<uint8_t>(i & 0xFF);

    TrackImage track = TrackCodec::buildTrack({sec}, Encoding::MFM);
    ASSERT_FALSE(track.empty());

    // Encode: target_bitcells = bytes * 16 (exakte Spur ohne Gap-Erweiterung)
    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;
    auto cells = BitCodec::encode(track, bc);
    ASSERT_EQ(cells.size(), (bc + 7) / 8);

    auto back = BitCodec::decode(cells, bc, Encoding::MFM);
    ASSERT_FALSE(back.bytes.empty());

    // Vergleich ab erstem A1-Sync (Leading-Gap vor dem ersten Sync darf abweichen)
    size_t ref_start = SIZE_MAX;
    for (size_t i = 0; i < track.bytes.size(); ++i) {
        if (track.bytes[i] == 0xA1) { ref_start = i; break; }
    }
    size_t back_start = SIZE_MAX;
    for (size_t i = 0; i < back.bytes.size(); ++i) {
        if (back.bytes[i] == 0xA1) { back_start = i; break; }
    }
    ASSERT_NE(ref_start, SIZE_MAX) << "Kein A1 in der Quell-Spur";
    ASSERT_NE(back_start, SIZE_MAX) << "Kein A1 im decodierten Strom";

    size_t ref_len  = track.bytes.size() - ref_start;
    size_t back_len = back.bytes.size() - back_start;
    size_t cmp_len  = std::min(ref_len, back_len);
    ASSERT_GT(cmp_len, 0u);

    for (size_t j = 0; j < cmp_len; ++j) {
        EXPECT_EQ(back.bytes[back_start + j], track.bytes[ref_start + j])
            << "Byte-Abweichung an relativer Position " << j;
        EXPECT_EQ(back.marks[back_start + j], track.marks[ref_start + j])
            << "Mark-Abweichung an relativer Position " << j;
    }
}

TEST(BitCodecMfm, EncodeDecodeIdentitaet_2Sektoren128B) {
    std::vector<LogicalSector> secs;
    for (uint8_t i = 1; i <= 2; ++i) {
        LogicalSector s = makeSector(1, 0, i, 128);
        for (size_t j = 0; j < s.data.size(); ++j)
            s.data[j] = static_cast<uint8_t>((i * 17 + j) & 0xFF);
        secs.push_back(std::move(s));
    }

    TrackImage track = TrackCodec::buildTrack(secs, Encoding::MFM);
    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;
    auto cells = BitCodec::encode(track, bc);
    auto back  = BitCodec::decode(cells, bc, Encoding::MFM);

    // parseTrack auf dem decodierten Ergebnis muss 2 CRC-korrekte Sektoren liefern
    auto parsed = TrackCodec::parseTrack(back);
    ASSERT_EQ(parsed.size(), 2u);
    for (size_t k = 0; k < 2; ++k) {
        EXPECT_TRUE(parsed[k].id_crc_ok)   << "Sektor " << k << ": ID-CRC";
        EXPECT_TRUE(parsed[k].data_crc_ok) << "Sektor " << k << ": Daten-CRC";
        EXPECT_EQ(parsed[k].data, secs[k].data) << "Sektor " << k << ": Datenbytes";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 2: encode∘decode-Identität (FM)
// ─────────────────────────────────────────────────────────────────────────────

TEST(BitCodecFm, EncodeDecodeIdentitaet_1Sektor128B) {
    LogicalSector sec = makeSector(2, 0, 3, 128, 0xC3);
    for (size_t i = 0; i < sec.data.size(); ++i)
        sec.data[i] = static_cast<uint8_t>((i * 3 + 7) & 0xFF);

    TrackImage track = TrackCodec::buildTrack({sec}, Encoding::FM);
    ASSERT_FALSE(track.empty());
    ASSERT_EQ(track.encoding, Encoding::FM);

    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;
    auto cells = BitCodec::encode(track, bc);
    ASSERT_EQ(cells.size(), (bc + 7) / 8);

    auto back = BitCodec::decode(cells, bc, Encoding::FM);
    ASSERT_FALSE(back.bytes.empty());
    ASSERT_EQ(back.encoding, Encoding::FM);

    // parseTrack muss den Sektor korrekt zurückliefern
    auto parsed = TrackCodec::parseTrack(back);
    ASSERT_GE(parsed.size(), 1u);

    // Den Sektor mit passender ID finden
    const LogicalSector* found = nullptr;
    for (const auto& ps : parsed) {
        if (ps.cyl == sec.cyl && ps.head == sec.head && ps.id == sec.id) {
            found = &ps;
            break;
        }
    }
    ASSERT_NE(found, nullptr) << "Sektor nicht in parseTrack-Ergebnis";
    EXPECT_TRUE(found->id_crc_ok)   << "FM ID-CRC fehlerhaft";
    EXPECT_TRUE(found->data_crc_ok) << "FM Daten-CRC fehlerhaft";
    EXPECT_EQ(found->data, sec.data) << "FM Datenbytes weichen ab";
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 3: Sync-Erkennung und Marken-Tagging
// ─────────────────────────────────────────────────────────────────────────────

TEST(BitCodecMarks, MfmIdMarkAuf0xFE_mit_vorhergehendemA1) {
    LogicalSector sec = makeSector(0, 0, 1, 128);
    TrackImage track  = TrackCodec::buildTrack({sec}, Encoding::MFM);
    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;

    auto back = BitCodec::decode(BitCodec::encode(track, bc), bc, Encoding::MFM);

    // Im decodierten Strom muss mindestens eine Id-Marke existieren
    bool id_found = false;
    for (size_t i = 0; i < back.bytes.size(); ++i) {
        if (back.marks[i] == MarkType::Id) {
            EXPECT_EQ(back.bytes[i], 0xFEu) << "Id-Marke nicht auf 0xFE";
            id_found = true;
            // Direkt davor: mindestens ein 0xA1 (aus dem Sync)
            ASSERT_GE(i, 1u);
            EXPECT_EQ(back.bytes[i - 1], 0xA1u) << "Vor Id-Marke kein A1";
        }
    }
    EXPECT_TRUE(id_found) << "Keine Id-Marke gefunden";
}

TEST(BitCodecMarks, MfmDataMarkAuf0xFB) {
    LogicalSector sec = makeSector(0, 0, 1, 128);
    TrackImage track  = TrackCodec::buildTrack({sec}, Encoding::MFM);
    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;

    auto back = BitCodec::decode(BitCodec::encode(track, bc), bc, Encoding::MFM);

    bool data_found = false;
    for (size_t i = 0; i < back.bytes.size(); ++i) {
        if (back.marks[i] == MarkType::Data) {
            EXPECT_EQ(back.bytes[i], 0xFBu) << "Data-Marke nicht auf 0xFB";
            data_found = true;
        }
    }
    EXPECT_TRUE(data_found) << "Keine Data-Marke gefunden";
}

TEST(BitCodecMarks, MfmIndexMarkAuf0xFC) {
    LogicalSector sec = makeSector(0, 0, 1, 128);
    GapParams g       = TrackCodec::gapsFor(Encoding::MFM);
    g.with_iam        = true;
    TrackImage track  = TrackCodec::buildTrack({sec}, Encoding::MFM, g);
    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;

    auto back = BitCodec::decode(BitCodec::encode(track, bc), bc, Encoding::MFM);

    bool idx_found = false;
    for (size_t i = 0; i < back.bytes.size(); ++i) {
        if (back.marks[i] == MarkType::Index) {
            EXPECT_EQ(back.bytes[i], 0xFCu) << "Index-Marke nicht auf 0xFC";
            idx_found = true;
        }
    }
    EXPECT_TRUE(idx_found) << "Keine Index-Marke (IAM) gefunden";
}

TEST(BitCodecMarks, MfmA1SyncBytesOhneMarke) {
    // Die 0xA1-Bytes VOR dem Marken-Byte dürfen keine Marke tragen
    LogicalSector sec = makeSector(0, 0, 1, 128);
    TrackImage track  = TrackCodec::buildTrack({sec}, Encoding::MFM);
    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;

    auto back = BitCodec::decode(BitCodec::encode(track, bc), bc, Encoding::MFM);

    for (size_t i = 0; i < back.bytes.size(); ++i) {
        if (back.bytes[i] == 0xA1 && back.marks[i] == MarkType::None) {
            // 0xA1 ohne Marke ist korrekt (Sync-Byte)
        }
        if (back.bytes[i] == 0xA1 && back.marks[i] != MarkType::None) {
            ADD_FAILURE() << "0xA1 hat unerwartet Marke "
                          << static_cast<int>(back.marks[i])
                          << " an Position " << i;
        }
    }
}

TEST(BitCodecMarks, FmIdMarkKorrektGetaggt) {
    LogicalSector sec = makeSector(1, 0, 2, 128);
    TrackImage track  = TrackCodec::buildTrack({sec}, Encoding::FM);
    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;

    auto back = BitCodec::decode(BitCodec::encode(track, bc), bc, Encoding::FM);

    bool id_found = false;
    for (size_t i = 0; i < back.bytes.size(); ++i) {
        if (back.marks[i] == MarkType::Id) {
            EXPECT_EQ(back.bytes[i], 0xFEu) << "FM Id-Marke nicht auf 0xFE";
            id_found = true;
        }
    }
    EXPECT_TRUE(id_found) << "Keine FM Id-Marke gefunden";
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 4: Daten-0xA1/0xFE werden NICHT fälschlich als Marke erkannt
//
// KERNBEWEIS des Cell-Level-Marken-Modells: Datenbytes 0xA1/0xFE/0xFB in einem
// Datenfeld erscheinen nach encode→decode mit mark==None, weil sie regulär
// MFM-kodiert werden (kein Sonder-Clock, kein 0x4489-Muster).
// ─────────────────────────────────────────────────────────────────────────────

TEST(BitCodecMarkModell, Daten_A1_FE_FB_nicht_als_Marke_erkannt) {
    // Sektor mit Datenbytes 0xA1, 0xFE, 0xFB an bekannten Positionen
    LogicalSector sec = makeSector(0, 0, 1, 128, 0x00);
    sec.data[0]  = 0xA1;
    sec.data[1]  = 0xFE;
    sec.data[2]  = 0xFB;
    sec.data[10] = 0xA1;
    sec.data[11] = 0xA1;
    sec.data[12] = 0xFE;

    TrackImage track = TrackCodec::buildTrack({sec}, Encoding::MFM);

    // encode → decode
    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;
    auto cells = BitCodec::encode(track, bc);
    auto back  = BitCodec::decode(cells, bc, Encoding::MFM);

    // parseTrack muss den Sektor korrekt (CRC-grün) liefern
    auto parsed = TrackCodec::parseTrack(back);
    ASSERT_GE(parsed.size(), 1u);
    bool found = false;
    for (const auto& ps : parsed) {
        if (ps.id == sec.id) {
            found = true;
            ASSERT_GE(ps.data.size(), 13u);

            // Datenbytes müssen erhalten bleiben
            EXPECT_EQ(ps.data[0],  0xA1u) << "Datenbyte 0xA1 verfälscht";
            EXPECT_EQ(ps.data[1],  0xFEu) << "Datenbyte 0xFE verfälscht";
            EXPECT_EQ(ps.data[2],  0xFBu) << "Datenbyte 0xFB verfälscht";
            EXPECT_EQ(ps.data[10], 0xA1u) << "Datenbyte 0xA1 [10] verfälscht";
            EXPECT_EQ(ps.data[11], 0xA1u) << "Datenbyte 0xA1 [11] verfälscht";
            EXPECT_EQ(ps.data[12], 0xFEu) << "Datenbyte 0xFE [12] verfälscht";

            EXPECT_TRUE(ps.data_crc_ok) << "Daten-CRC fehlerhaft (Datenbytes verfälscht?)";
        }
    }
    EXPECT_TRUE(found) << "Sektor nicht in parseTrack gefunden";

    // Direkt: Im decodierten Strom dürfen Datenbytes 0xA1/0xFE/0xFB nach der
    // Data-Marke (im Datenbereich) keine Marke tragen.
    size_t data_mark_pos = SIZE_MAX;
    for (size_t i = 0; i < back.bytes.size(); ++i) {
        if (back.marks[i] == MarkType::Data) { data_mark_pos = i; break; }
    }
    ASSERT_NE(data_mark_pos, SIZE_MAX) << "Keine Data-Marke im decodierten Strom";

    // Die ersten 128 Bytes nach der Data-Marke sind das Datenfeld
    for (size_t j = 1; j <= 128 && data_mark_pos + j < back.bytes.size(); ++j) {
        uint8_t b = back.bytes[data_mark_pos + j];
        MarkType m = back.marks[data_mark_pos + j];
        if (b == 0xA1 || b == 0xFE || b == 0xFB) {
            EXPECT_EQ(m, MarkType::None)
                << "Datenbyte 0x" << std::hex << static_cast<int>(b)
                << " hat fälschlich Marke an Datenfeld-Position " << std::dec << j;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 5: bitcells/Längen-Invarianten
// ─────────────────────────────────────────────────────────────────────────────

TEST(BitCodecLaenge, DecodeGibtBitcellCountZurueck) {
    LogicalSector sec = makeSector(0, 0, 1, 128);
    TrackImage track  = TrackCodec::buildTrack({sec}, Encoding::MFM);
    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;

    auto cells = BitCodec::encode(track, bc);
    auto back  = BitCodec::decode(cells, bc, Encoding::MFM);

    EXPECT_EQ(back.bitcells, bc);
}

TEST(BitCodecLaenge, EncodePrueftZielGroesse) {
    LogicalSector sec = makeSector(0, 0, 1, 128);
    TrackImage track  = TrackCodec::buildTrack({sec}, Encoding::MFM);

    // Verschiedene Zielgrößen: kleiner, gleich, größer
    for (uint32_t bc : {100u, 500u, 10000u, 50000u}) {
        auto cells = BitCodec::encode(track, bc);
        uint32_t expected_bytes = (bc + 7) / 8;
        EXPECT_EQ(cells.size(), expected_bytes)
            << "Falsche Ausgabelänge für bitcells=" << bc;
    }
}

TEST(BitCodecLaenge, EncodeExakteBitcells_aufgeteilt) {
    // bitcell_count nicht vielfaches von 8 → ceil(N/8) Bytes
    for (uint32_t bc : {1u, 7u, 8u, 9u, 15u, 16u, 17u}) {
        TrackImage dummy;
        dummy.encoding = Encoding::MFM;
        dummy.bytes    = {0x4E};
        dummy.marks    = {MarkType::None};
        auto cells = BitCodec::encode(dummy, bc);
        EXPECT_EQ(cells.size(), (bc + 7) / 8)
            << "ceil(" << bc << "/8) erwartet";
    }
}

TEST(BitCodecLaenge, DecodeLeererStrom_gibtLeereSpur) {
    std::vector<uint8_t> empty;
    auto result = BitCodec::decode(empty, 0, Encoding::MFM);
    EXPECT_TRUE(result.bytes.empty());
    EXPECT_EQ(result.bitcells, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GRUPPE 6: MFM-Roundtrip über TrackCodec::parseTrack (voller Datenpfad)
// ─────────────────────────────────────────────────────────────────────────────

class MfmBitCodecRoundtrip : public ::testing::TestWithParam<uint16_t> {};

TEST_P(MfmBitCodecRoundtrip, BitCodecPlusParseTrack) {
    const uint16_t secSize = GetParam();
    LogicalSector sec = makeSector(3, 1, 2, secSize, 0x55);
    for (size_t i = 0; i < sec.data.size(); ++i)
        sec.data[i] = static_cast<uint8_t>(i & 0xFF);

    TrackImage track = TrackCodec::buildTrack({sec}, Encoding::MFM);
    ASSERT_FALSE(track.empty());

    const uint32_t bc = static_cast<uint32_t>(track.bytes.size()) * 16u;
    auto cells = BitCodec::encode(track, bc);
    auto back  = BitCodec::decode(cells, bc, Encoding::MFM);

    auto parsed = TrackCodec::parseTrack(back);
    ASSERT_EQ(parsed.size(), 1u);

    EXPECT_TRUE(parsed[0].id_crc_ok)   << "ID-CRC fehler (size=" << secSize << ")";
    EXPECT_TRUE(parsed[0].data_crc_ok) << "Daten-CRC fehler (size=" << secSize << ")";
    EXPECT_EQ(parsed[0].cyl,  sec.cyl);
    EXPECT_EQ(parsed[0].head, sec.head);
    EXPECT_EQ(parsed[0].id,   sec.id);
    EXPECT_EQ(parsed[0].size, secSize);
    EXPECT_EQ(parsed[0].data, sec.data) << "Datenbytes weichen ab (size=" << secSize << ")";
}

INSTANTIATE_TEST_SUITE_P(
    Sektorgroessen, MfmBitCodecRoundtrip,
    ::testing::Values(128u, 256u, 512u, 1024u));

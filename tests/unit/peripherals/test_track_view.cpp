/**
 * @file test_track_view.cpp
 * @brief GoogleTests für @ref scanTrack — die zeichenbare Sicht auf eine Spur.
 *
 * Geprüft wird die eine Zusage, auf der die ganze Darstellung ruht: die Abschnitte
 * decken `[0,1)` **lückenlos und überschneidungsfrei** ab.  Nur dann ist der
 * Treffertest über den Winkel eindeutig, und nur dann bleibt kein Stück Spur
 * ungezeichnet.
 *
 * @see core/peripherals/floppy_drive/track_view.h
 * @see doc/design/13_k1520disktool.md §19
 */

#include <gtest/gtest.h>

#include <vector>

#include "core/peripherals/floppy_drive/track_view.h"

namespace {

LogicalSector sektor(uint8_t id, uint16_t size, uint8_t fill = 0xE5) {
    LogicalSector s;
    s.cyl  = 0;
    s.head = 0;
    s.id   = id;
    s.size = size;
    s.data.assign(size, fill);
    return s;
}

/// @brief Die zentrale Zusage: lueckenlos, aufsteigend, ohne Ueberschneidung.
void pruefeAbdeckung(const TrackView& v) {
    ASSERT_FALSE(v.spans.empty());
    EXPECT_DOUBLE_EQ(v.spans.front().start, 0.0);
    for (size_t i = 0; i < v.spans.size(); ++i) {
        EXPECT_LT(v.spans[i].start, v.spans[i].end) << "leerer Abschnitt " << i;
        if (i > 0)
            EXPECT_DOUBLE_EQ(v.spans[i].start, v.spans[i - 1].end) << "Luecke bei " << i;
    }
    EXPECT_NEAR(v.spans.back().end, 1.0, 1e-9);
}

}  // namespace

TEST(TrackView, LeereSpurIstUnformatiert) {
    // Die Spur gibt es in dieser Geometrie nicht — ein einziger grauer Ring.
    const TrackView v = scanTrack(TrackImage{});
    EXPECT_FALSE(v.exists);
    EXPECT_FALSE(v.formatted);
    ASSERT_EQ(v.spans.size(), 1u);
    EXPECT_EQ(v.spans[0].kind, TrackSpan::Kind::Unformatted);
    pruefeAbdeckung(v);
}

TEST(TrackView, MarkenloserGapFlussIstUnformatiertUndKeinRiesigerGap) {
    // Genau das legt DiskImage::createBlank an: Bytes ja, Adressmarken nein.  Der
    // Unterschied zu einem Gap ist, ob ein Gast hier erst formatieren muss.
    TrackImage t;
    t.bytes.assign(6250, 0x4E);
    t.marks.assign(6250, MarkType::None);

    const TrackView v = scanTrack(t);
    EXPECT_TRUE(v.exists);
    EXPECT_FALSE(v.formatted);
    EXPECT_EQ(v.sectors, 0);
    ASSERT_EQ(v.spans.size(), 1u);
    EXPECT_EQ(v.spans[0].kind, TrackSpan::Kind::Unformatted);
    pruefeAbdeckung(v);
}

TEST(TrackView, SektorenUndGapsWechselnSichAb) {
    std::vector<LogicalSector> secs{sektor(1, 128), sektor(2, 128), sektor(3, 256)};
    const TrackView v = scanTrack(TrackCodec::buildTrack(secs, Encoding::MFM));

    EXPECT_TRUE(v.exists);
    EXPECT_TRUE(v.formatted);
    EXPECT_EQ(v.sectors, 3);
    EXPECT_EQ(v.encoding, Encoding::MFM);
    pruefeAbdeckung(v);

    std::vector<TrackSpan> sektoren;
    for (const TrackSpan& s : v.spans)
        if (s.kind == TrackSpan::Kind::Sector) sektoren.push_back(s);
    ASSERT_EQ(sektoren.size(), 3u);

    // Die Nummerierung ist die von parseTrack — sie ist der Zugriffsschluessel.
    for (int i = 0; i < 3; ++i) EXPECT_EQ(sektoren[static_cast<size_t>(i)].index, i);
    EXPECT_EQ(sektoren[0].id, 1);
    EXPECT_EQ(sektoren[2].size, 256);
    for (const TrackSpan& s : sektoren) {
        EXPECT_TRUE(s.ok()) << "frisch gebaut = beide CRCs gut";
        EXPECT_FALSE(s.deleted);
    }

    // Vor dem ersten Sektor liegt das Index-Gap (gap4a + IAM) — kein Sektor bei 0.
    EXPECT_EQ(v.spans.front().kind, TrackSpan::Kind::Gap);
}

TEST(TrackView, DefekteCrcSchlaegtAufDenAbschnittDurch) {
    // Das ist die Farbe in der Oberflaeche: rot statt gruen.
    TrackImage t = TrackCodec::buildTrack({sektor(1, 128), sektor(2, 128)},
                                          Encoding::MFM);
    const std::vector<LogicalSector> s = TrackCodec::parseTrack(t);
    ASSERT_EQ(s.size(), 2u);
    t.bytes[s[1].data_pos + 1] ^= 0xFF;          // ein Datenbyte verbiegen

    const TrackView v = scanTrack(t);
    pruefeAbdeckung(v);
    int gut = 0, schlecht = 0;
    for (const TrackSpan& sp : v.spans) {
        if (sp.kind != TrackSpan::Kind::Sector) continue;
        (sp.ok() ? gut : schlecht)++;
    }
    EXPECT_EQ(gut, 1);
    EXPECT_EQ(schlecht, 1);
}

TEST(TrackView, WinkelEntsprichtDerBytepositionInDerSpur) {
    // Der Kern der ganzen Darstellung: eine Spur IST eine Umdrehung, also ist der
    // Winkel Position/Spurlaenge — Drehzahl und Bitrate spielen keine Rolle.
    TrackImage t = TrackCodec::buildTrack({sektor(1, 128), sektor(2, 128)},
                                          Encoding::MFM);
    const std::vector<LogicalSector> s = TrackCodec::parseTrack(t);
    const TrackView v = scanTrack(t);
    const double n = static_cast<double>(t.bytes.size());

    for (const TrackSpan& sp : v.spans) {
        if (sp.kind != TrackSpan::Kind::Sector) continue;
        const LogicalSector& ls = s[static_cast<size_t>(sp.index)];
        EXPECT_NEAR(sp.start, static_cast<double>(ls.sync_pos) / n, 1e-12);
        EXPECT_NEAR(sp.end,   static_cast<double>(ls.end_pos)  / n, 1e-12);
    }
}

TEST(TrackView, SektorOhneDatenfeldVerschlucktNichtDenRestDerSpur) {
    // Halb formatierte Spur (Schreibabbruch): das ID-Feld steht, das Datenfeld nicht.
    // Ohne Begrenzung reichte der Abschnitt bis zum Spurende und verdeckte alles.
    TrackImage t = TrackCodec::buildTrack({sektor(1, 128), sektor(2, 128)},
                                          Encoding::MFM);
    const std::vector<LogicalSector> s = TrackCodec::parseTrack(t);
    t.marks[s[0].data_pos] = MarkType::None;     // Datenmarke wegnehmen

    const TrackView v = scanTrack(t);
    pruefeAbdeckung(v);
    for (const TrackSpan& sp : v.spans) {
        if (sp.kind != TrackSpan::Kind::Sector || sp.index != 0) continue;
        EXPECT_LT(sp.end - sp.start, 0.1) << "der Abschnitt endet hinter dem ID-Feld";
    }
    EXPECT_EQ(v.sectors, 2) << "der Sektor zaehlt weiterhin — er ist ja da";
}

// ─────────────────────────────────────────────────────────────────────────────
// Beschrieben oder nur formatiert?
// ─────────────────────────────────────────────────────────────────────────────
//
// Der Diskeditor faerbt beschriebene Sektoren dunkelgruen, formatierte ohne Inhalt
// hellgruen.  Die Unterscheidung ist „Datenfeld einfoermig" — welches Fuellbyte ein
// Format benutzt (CP/M 0xE5, andere 0x00/0xFF), ist dessen Sache.

TEST(TrackViewLeer, FormatiertOhneInhaltGiltAlsLeer) {
    for (uint8_t fuell : {uint8_t{0xE5}, uint8_t{0x00}, uint8_t{0xFF}}) {
        const std::vector<LogicalSector> secs{sektor(1, 128, fuell)};
        const TrackView v = scanTrack(TrackCodec::buildTrack(secs, Encoding::MFM));
        bool gesehen = false;
        for (const TrackSpan& sp : v.spans)
            if (sp.kind == TrackSpan::Kind::Sector) {
                EXPECT_TRUE(sp.blank) << "Fuellbyte 0x" << std::hex << int(fuell);
                gesehen = true;
            }
        EXPECT_TRUE(gesehen);
    }
}

TEST(TrackViewLeer, EinEinzigesAbweichendesByteMachtDenSektorVoll) {
    LogicalSector s = sektor(1, 128);
    s.data[64] = 0x42;                       // ein Byte genuegt
    const TrackView v = scanTrack(TrackCodec::buildTrack({s}, Encoding::MFM));
    for (const TrackSpan& sp : v.spans)
        if (sp.kind == TrackSpan::Kind::Sector)
            EXPECT_FALSE(sp.blank);
}

TEST(TrackViewLeer, DerUdosAnhangZaehltNichtMit) {
    // Bei UDOS steht hinter der Daten-CRC die Dateiverkettung — sie ist auch auf
    // einer frisch formatierten Diskette belegt.  Zaehlte sie mit, saehe dort kein
    // einziger Sektor leer aus, und die Faerbung waere wertlos.
    LogicalSector s = sektor(1, 128);
    s.tail = {0x01, 0x16, 0xFF, 0xFF};
    const TrackView v = scanTrack(TrackCodec::buildTrack({s}, Encoding::MFM));
    bool gesehen = false;
    for (const TrackSpan& sp : v.spans)
        if (sp.kind == TrackSpan::Kind::Sector) {
            EXPECT_TRUE(sp.blank) << "der Anhang wurde mitgezaehlt";
            gesehen = true;
        }
    EXPECT_TRUE(gesehen);
}

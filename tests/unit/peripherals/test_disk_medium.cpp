/**
 * @file test_disk_medium.cpp
 * @brief GoogleTests für DiskMedium — das interne, bitstrom-orientierte Diskettenabbild.
 *
 * Schwerpunkte:
 *   - Geometrie / resize (Spuren behalten ihre (cyl, head)-Position)
 *   - Dirty-Bits je Spur
 *   - formatted() (Leerdiskette vs. beschriebene Diskette)
 *   - rawCompatible(): DAS Flag, das `.img` als Speicherziel sperrt, sobald etwas
 *     auf der Spur steht, das ein rohes Sektorimage nicht ausdrücken kann —
 *     unformatierte Spuren, CRC-Fehler und **Nutzdaten hinter der Daten-CRC**
 *     (UDOS-Sektorkontrollblock).
 *
 * @see core/peripherals/floppy_drive/disk_medium.h
 * @see doc/design/09_floppy_drive.md §3, §5
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/peripherals/floppy_drive/disk_medium.h"
#include "core/peripherals/floppy_drive/track_codec.h"

namespace {

/// @brief Standard-IBM-Spur mit @p nsec Sektoren à @p size Byte.
TrackImage makeTrack(uint8_t cyl, uint8_t head, int nsec, uint16_t size,
                     Encoding enc = Encoding::MFM, uint8_t fill = 0xE5) {
    std::vector<LogicalSector> secs;
    for (int i = 1; i <= nsec; ++i) {
        LogicalSector ls;
        ls.cyl  = cyl;
        ls.head = head;
        ls.id   = static_cast<uint8_t>(i);
        ls.size = size;
        ls.data.assign(size, fill);
        secs.push_back(std::move(ls));
    }
    return TrackCodec::buildTrack(secs, enc);
}

}  // namespace

// ─── Geometrie ───────────────────────────────────────────────────────────────

TEST(DiskMedium, Konstruktor_SetztGeometrieUndLeereSpuren) {
    const DiskMedium m(80, 2, Encoding::MFM);
    EXPECT_EQ(m.numCylinders(), 80u);
    EXPECT_EQ(m.numHeads(),      2u);
    EXPECT_EQ(m.defaultEncoding(), Encoding::MFM);
    EXPECT_TRUE(m.track(0, 0).empty());
    EXPECT_TRUE(m.track(79, 1).empty());
    EXPECT_FALSE(m.formatted());
}

TEST(DiskMedium, ZugriffAusserhalbDerGeometrie_LiefertLeereSpur) {
    const DiskMedium m(2, 1, Encoding::MFM);
    EXPECT_TRUE(m.track(5, 0).empty());
    EXPECT_TRUE(m.track(0, 1).empty());
}

TEST(DiskMedium, Resize_BehaeltSpurenAnIhrerPosition) {
    DiskMedium m(2, 2, Encoding::MFM);
    m.setTrack(1, 1, makeTrack(1, 1, 4, 128));
    ASSERT_FALSE(m.track(1, 1).empty());

    m.resize(4, 2);
    EXPECT_EQ(m.numCylinders(), 4u);
    EXPECT_FALSE(m.track(1, 1).empty()) << "Spur nach resize verloren";
    EXPECT_TRUE(m.track(3, 0).empty());
}

// ─── Dirty-Bits ──────────────────────────────────────────────────────────────

TEST(DiskMedium, DirtyBits_ProSpurUndGlobal) {
    DiskMedium m(2, 1, Encoding::MFM);
    EXPECT_FALSE(m.dirty());

    m.setTrack(1, 0, makeTrack(1, 0, 2, 128));
    EXPECT_TRUE(m.dirty());
    EXPECT_TRUE(m.trackDirty(1, 0));
    EXPECT_FALSE(m.trackDirty(0, 0));

    m.clearDirty();
    EXPECT_FALSE(m.dirty());
    EXPECT_FALSE(m.trackDirty(1, 0));

    // mutableTrack markiert sofort — der Aufrufer patcht in-place.
    (void)m.mutableTrack(0, 0);
    EXPECT_TRUE(m.dirty());
    EXPECT_TRUE(m.trackDirty(0, 0));
}

// ─── formatted() ─────────────────────────────────────────────────────────────

TEST(DiskMedium, Formatted_ErstNachBeschriebenerSpur) {
    DiskMedium m(3, 1, Encoding::MFM);
    EXPECT_FALSE(m.formatted());
    m.setTrack(2, 0, makeTrack(2, 0, 2, 128));
    EXPECT_TRUE(m.formatted());
}

// ─── rawCompatible() ─────────────────────────────────────────────────────────

TEST(DiskMedium, RawCompatible_StandardSpurenSindDarstellbar) {
    DiskMedium m(2, 1, Encoding::MFM);
    for (uint8_t c = 0; c < 2; ++c) m.setTrack(c, 0, makeTrack(c, 0, 2, 128));

    EXPECT_TRUE(m.trackRawCompatible(0, 0));
    EXPECT_TRUE(m.rawCompatible());
    EXPECT_EQ(m.rawIncompatibleReason(), "");
}

/**
 * @test DiskMedium/RawCompatible_LeerdisketteIstNichtDarstellbar
 * @brief Eine komplett unformatierte Diskette kann ein `.img` nicht ausdrücken —
 *        genau der Fall „neue Leerdiskette als .img speichern" muss scheitern.
 */
TEST(DiskMedium, RawCompatible_LeerdisketteIstNichtDarstellbar) {
    const DiskMedium leer(80, 2, Encoding::MFM);
    EXPECT_FALSE(leer.rawCompatible());
    EXPECT_EQ(leer.rawIncompatibleReason(), "unformatiert");
}

/**
 * @test DiskMedium/RawCompatible_LeereZusatzspurenSindErlaubt
 * @brief Einzelne leere Spuren (viele echte Images hängen 1–3 Gap-Spuren an) sperren
 *        `.img` NICHT — sie werden dort schlicht zu Füllbytes.
 */
TEST(DiskMedium, RawCompatible_LeereZusatzspurenSindErlaubt) {
    DiskMedium m(3, 1, Encoding::MFM);
    m.setTrack(0, 0, makeTrack(0, 0, 2, 128));
    m.setTrack(1, 0, makeTrack(1, 0, 2, 128));
    // Spur 2 bleibt leer.
    EXPECT_TRUE(m.rawCompatible());
}

/**
 * @test DiskMedium/RawCompatible_AnhangHinterDatenCrcSperrtImg
 * @brief DAS Kernkriterium: UDOS schreibt je Sektor einen Kontrollblock
 *        (Verkettungszeiger + eigene CRC) direkt HINTER die Daten-CRC.  Beim
 *        Speichern als `.img` ginge er verloren → das Flag muss fallen.
 */
TEST(DiskMedium, RawCompatible_AnhangHinterDatenCrcSperrtImg) {
    DiskMedium m(2, 1, Encoding::MFM);
    m.setTrack(0, 0, makeTrack(0, 0, 2, 128));
    m.setTrack(1, 0, makeTrack(1, 0, 2, 128));
    ASSERT_TRUE(m.rawCompatible());

    TrackImage& t = m.mutableTrack(1, 0);
    const size_t dam = t.nextMark(0, MarkType::Data);
    ASSERT_NE(dam, SIZE_MAX);
    const size_t tail = dam + 1 + 128 + 2;      // Datenmarke + Nutzdaten + CRC
    ASSERT_LT(tail + 3, t.bytes.size());
    t.bytes[tail + 0] = 0x1A;                   // Rückwärtszeiger
    t.bytes[tail + 1] = 0x2B;                   // Vorwärtszeiger
    t.bytes[tail + 2] = 0x3C;                   // eigene CRC

    EXPECT_FALSE(m.trackRawCompatible(1, 0));
    EXPECT_FALSE(m.rawCompatible());
    EXPECT_EQ(m.rawIncompatibleReason(), "Spur 1/0");
}

/**
 * @test DiskMedium/RawCompatible_CrcFehlerSperrtImg
 * @brief Ein Sektor mit kaputter Daten-CRC ist im `.img` nicht abbildbar (dort gäbe
 *        es beim Zurücklesen wieder eine gültige CRC — die Information ginge verloren).
 */
TEST(DiskMedium, RawCompatible_CrcFehlerSperrtImg) {
    DiskMedium m(1, 1, Encoding::MFM);
    m.setTrack(0, 0, makeTrack(0, 0, 2, 128));
    ASSERT_TRUE(m.rawCompatible());

    TrackImage& t = m.mutableTrack(0, 0);
    const size_t dam = t.nextMark(0, MarkType::Data);
    ASSERT_NE(dam, SIZE_MAX);
    t.bytes[dam + 5] ^= 0xFF;                   // Nutzbyte kippen → CRC passt nicht mehr

    EXPECT_FALSE(m.rawCompatible());
}

/**
 * @test DiskMedium/RawCompatible_CacheWirdBeiAenderungVerworfen
 * @brief Das Ergebnis wird je Spur gecacht; eine Änderung muss es invalidieren.
 */
TEST(DiskMedium, RawCompatible_CacheWirdBeiAenderungVerworfen) {
    DiskMedium m(1, 1, Encoding::MFM);
    EXPECT_FALSE(m.trackRawCompatible(0, 0));     // leer → Ergebnis gecacht

    m.setTrack(0, 0, makeTrack(0, 0, 2, 128));
    EXPECT_TRUE(m.trackRawCompatible(0, 0)) << "Cache nicht invalidiert";
}

// ─── Mischdichte ─────────────────────────────────────────────────────────────

TEST(DiskMedium, Geometry_UniformFalschBeiMischdichte) {
    DiskMedium m(2, 1, Encoding::MFM);
    m.setTrack(0, 0, makeTrack(0, 0, 4,  128, Encoding::FM));
    m.setTrack(1, 0, makeTrack(1, 0, 2, 1024, Encoding::MFM));
    EXPECT_FALSE(m.geometry().uniform);
    EXPECT_TRUE(m.rawCompatible()) << "Mischdichte allein sperrt .img nicht";
}

// ─── Spurzustand: EIN Konzept für Datei und echtes Laufwerk ──────────────────
//
// Der Dreizustand (Unknown/Clean/Dirty) ist nicht nur für die physische Diskette
// da — er gilt für JEDES Medium.  Bei einer dateigebundenen Diskette tritt
// `Unknown` schlicht nie auf, weil der Container-Codec beim Laden alle Spuren
// füllt.  Die Aussagen `dirty()`/`trackDirty()`, mit denen der Autosave seit jeher
// arbeitet, sind dieselben Bits (doc/design/09_floppy_drive.md §12.2).

TEST(DiskMedium, ZustandGiltAuchOhneLaufwerk_UnknownKommtDortNieVor) {
    DiskMedium m(2, 2, Encoding::MFM);

    // Frisch angelegt: bekannt und sauber — nicht „unbekannt".
    for (uint8_t c = 0; c < 2; ++c)
        for (uint8_t h = 0; h < 2; ++h)
            EXPECT_EQ(m.state(c, h), TrackState::Clean) << +c << "/" << +h;
    EXPECT_TRUE(m.complete());
    EXPECT_EQ(m.unknownCount(), 0u);

    // Schreiben macht genau eine Spur schmutzig …
    m.setTrack(1, 0, makeTrack(1, 0, 2, 128));
    EXPECT_EQ(m.state(1, 0), TrackState::Dirty);
    EXPECT_EQ(m.state(0, 0), TrackState::Clean);
    EXPECT_TRUE(m.trackDirty(1, 0));       // dasselbe Bit wie eh und je
    EXPECT_TRUE(m.dirty());

    // … und der Save macht sie wieder sauber.  Nie wird daraus „unbekannt":
    // der Inhalt ist ja da, nur die Datei hinkte hinterher.
    m.clearDirty();
    EXPECT_EQ(m.state(1, 0), TrackState::Clean);
    EXPECT_TRUE(m.complete());
}

TEST(DiskMedium, EinzelneSpurSauberMelden_LaesstDieAnderenSchmutzig) {
    // Der Weg der Rueckfuehrung: eine Spur ist geschrieben, die andere noch nicht.
    // Ein pauschales clearDirty() waere hier falsch — es erklaerte auch die
    // ungeschriebene fuer erledigt.
    DiskMedium m(2, 1, Encoding::MFM);
    m.setTrack(0, 0, makeTrack(0, 0, 2, 128));
    m.setTrack(1, 0, makeTrack(1, 0, 2, 128));

    m.clearTrackDirty(0, 0);
    EXPECT_EQ(m.state(0, 0), TrackState::Clean);
    EXPECT_EQ(m.state(1, 0), TrackState::Dirty);
    EXPECT_TRUE(m.dirty()) << "es steht noch eine Spur aus";

    m.clearTrackDirty(1, 0);
    EXPECT_FALSE(m.dirty());
}

TEST(DiskMedium, GeleseneSpurIstSauber_GeschriebeneSchmutzig) {
    // loadTrack (Ladepfad) vs. setTrack (Schreibpfad) — der Unterschied entscheidet,
    // ob eine Spur zurueckgeschrieben wird.  Wer beim Laden setTrack benutzt,
    // schriebe die frisch gelesene Spur sofort wieder hinaus.
    DiskMedium m(1, 1, Encoding::MFM);
    m.loadTrack(0, 0, makeTrack(0, 0, 2, 128));
    EXPECT_EQ(m.state(0, 0), TrackState::Clean);
    EXPECT_FALSE(m.dirty());

    m.setTrack(0, 0, makeTrack(0, 0, 3, 128));
    EXPECT_EQ(m.state(0, 0), TrackState::Dirty);
}

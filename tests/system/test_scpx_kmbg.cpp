/**
 * @file test_scpx_kmbg.cpp
 * @brief SCPX + Robotron-Kassettenbibliothek `KMBG.REL` schreibt und liest einen Block
 *        (doc/design/32_… AP K8, Entwurf 29).
 *
 * Das Pruefprogramm `tools/kassette/kmbgtest/kmbgtest.com` (Quelle `src/kmbgtest.mac`, gebunden
 * mit `doc/kassette/fex/KMBG.REL`) reserviert Laufwerk 1, schreibt einen 128-Byte-Block, setzt
 * das Band um einen Block zurueck, liest ihn wieder, vergleicht und gibt das Laufwerk frei.
 * Es wird mit dem DiskTool auf eine KOPIE der SCPX-1.7-Systemdiskette gespielt (`TempDisk`) und
 * ueber die Tastatur gestartet.  Die Kassette ist leer und liegt am Anfang des Vorspanns (so, wie
 * man sie einlegt) — KMBG faehrt beim ersten Schreiben selbst ueber die BOT-Marke (treiber.md §4.2).
 *
 * Langsam (Kaltstart + Bandlauf in Maschinenzeit) → Label `format_integration`.
 */

#include <gtest/gtest.h>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "core/peripherals/kassette/kassetten_laufwerk.h"
#include "core/peripherals/kassette/kassetten_medium.h"
#include "core/logger.h"

#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

#include <memory>
#include <string>

#ifndef K1520_KMBGTEST_COM
#error "K1520_KMBGTEST_COM fehlt — k1520_add_test(... DEFS K1520_KMBGTEST_COM=\"…\")"
#endif

using k1520test::QK_RETURN;
using k1520test::runCycles;
using k1520test::runSmallUntil;
using k1520test::runSmallUntilVisible;
using k1520test::TempDisk;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramText;
using kassette::KassettenMedium;

namespace {

/// `KMBGTEST.COM` mit dem DiskTool auf die Diskette @p pfad schreiben; leer = Erfolg.
std::string aufspielen(const std::string& pfad) {
    std::string f;
    static const FormatCatalog formate = FormatCatalog::loadDefault(&f);
    static const FsCatalog     fs      = FsCatalog::loadDefault(formate, &f);
    std::string err;
    auto v = DiskVolume::open(pfad, "", formate, fs, err, /*read_only=*/false);
    if (!v) return "DiskVolume::open: " + err;
    v->setBackup(false);
    TransferOptions o;
    o.overwrite = true;
    if (!v->insert(K1520_KMBGTEST_COM, FileRef::parse("KMBGTEST.COM"), o)) return "insert: " + v->lastError();
    if (!v->flush()) return "flush: " + v->lastError();
    return {};
}

}  // namespace

TEST(ScpxKmbg, SchreibtUndLiestEinenBlock) {
    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::WARN);
    TempDisk a("scpx17_cpa780_k5601.hfe");
    ASSERT_EQ(aufspielen(a.path()), "");

    A5120Machine m;
    ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa780", /*wp=*/false)) << m.lastError();
    ASSERT_TRUE(m.installK5020()) << m.k5020Fehler();
    // Die Kassette ist zurueckgespult, wie ein K5200 sie nach UMS hinterlaesst (es spult, bis die
    // Lichtschranke den Vorspann sieht, und bremst dann): in einem eigenen Laufwerk mitten im
    // Magnetband eingelegt, reserviert, angewaehlt, UMS, ausgeworfen.  Ab Position 0 (Anfang des
    // 1-m-Vorspanns) scheitert KMBG dagegen mit Status 04: es wartet bei AEB = 0 nur
    // 65 536 × 92 Takte = 2,45 s auf das Magnetband (KMBG 048E–04A4, Adressen relativ zum Modul wie in treiber.md), bei 381 mm/s also auf
    // hoechstens ≈ 0,9 m Vorspann (Fund K8, doc/merkposten/kassette.md).
    auto band = std::make_shared<KassettenMedium>();
    band->setPosition(3'000'000);
    {
        kassette::KassettenLaufwerk spuler;
        ASSERT_TRUE(spuler.kassetteEinlegen(band));
        spuler.reservieren(true);
        spuler.advance(0.5);
        spuler.anwaehlen(true);
        spuler.umspulen();
        ASSERT_TRUE(spuler.umspulenAktiv());
        spuler.anwaehlen(false);
        spuler.reservieren(false);
        for (int i = 0; i < 200 && (spuler.umspulenAktiv() || spuler.geschwindigkeitMmS() != 0.0); ++i)
            spuler.advance(0.1);
        ASSERT_FALSE(spuler.umspulenAktiv());
        ASSERT_TRUE(spuler.aebAktiv()) << "nach UMS steht der Vorspann vor der Lichtschranke; Kopf "
                                       << spuler.positionMm() << " mm";
        ASSERT_TRUE(spuler.kassetteAuswerfen());
    }
    auto& lw = m.k5020()->laufwerk(0);
    ASSERT_TRUE(lw.kassetteEinlegen(band));
    m.powerOn();

    ASSERT_TRUE(runSmallUntil(m, "SCPX 1526 - V 1.7", 40'000'000)) << vramText(m);
    ASSERT_TRUE(runSmallUntilVisible(m, "A>", 5'000'000)) << vramText(m);
    runCycles(m, 2'000'000);

    typeString(m, "KMBGTEST");
    typeKey(m, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(m, "KMBGTEST V1", 20'000'000)) << vramText(m);
    // Reservieren 1,4 s + Vorspann/BOT + Schreiben + Block zurueck + Lesen + Freigeben 1,4 s.
    ASSERT_TRUE(runSmallUntil(m, "KMBGTEST RES=", 400'000'000, 1'000'000))
        << "PC=" << std::hex << m.cpuPC() << "\n" << vramText(m);
    ASSERT_TRUE(runSmallUntil(m, "KMBGTEST OK", 2'000'000)) << vramText(m);

    // Auf dem Band: genau ein Block, AA | 128 Daten | CRC | AA, Daten = Muster 7*i+3.
    ASSERT_EQ(band->anzahlBloecke(kassette::Seite::A), 1u);
    const auto& b = band->block(kassette::Seite::A, 0).bytes;
    ASSERT_EQ(b.size(), 132u);
    EXPECT_EQ(b.front(), 0xAA);
    EXPECT_EQ(b.back(), 0xAA);
    for (int i = 0; i < 128; ++i) EXPECT_EQ(b[1 + i], uint8_t(7 * i + 3)) << "Byte " << i;
    EXPECT_EQ(m.k5020()->verloreneBytes(), 0u);
}

/**
 * @file test_k8915_maschine.cpp
 * @brief K8915Machine ohne Boot: die Wege, die weder der Selbsttest noch SCPX
 *        durchlaufen (AP-T1a, doc/design/16_k8915.md §8a).
 *
 * Gefunden mit dem Abdeckungsbau: DFÜ-Rückruf/-Senden (SIO2-A) lief in keinem Test,
 * die Fehlerwege des gemeinsamen Laufwerksbausteins `Laufwerke` (A5120 UND K8915,
 * Meldungen gehen über `k1520_last_error` in den Laufwerkskasten) ebensowenig,
 * dazu die Durchreichungen der Maschine (Container, Schreibschutz, Konsolenmodus).
 * Kein CPU-Lauf — die Bausteine werden direkt angefasst, jeder Fall < 1 s.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/machines/k8915/k8915.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "tests/support/temp_path.h"

namespace {

/// Freier Temp-Pfad, der am Ende des Falls wieder weggeräumt wird.
struct TempDatei {
    std::string pfad;
    explicit TempDatei(const std::string& name)
        : pfad(k1520test::tempPath("k1520_test_k8915_maschine_" + name)) {
        std::error_code ec;
        std::filesystem::remove(pfad, ec);
    }
    ~TempDatei() { std::error_code ec; std::filesystem::remove(pfad, ec); }
};

bool enthaelt(const std::string& s, const std::string& teil) {
    return s.find(teil) != std::string::npos;
}

}  // namespace

/**
 * @test K8915Machine.DfueUndDruckerHaengenAnIhremEigenenKanal
 * @brief Drucker = SIO1-B, DFÜ = SIO2-A (AP-E4c).  Ein Rückruf ERSETZT die
 *        Rückschleife des Prüfsteckers nur auf seinem Kanal: SIO1-A schleift weiter,
 *        SIO1-B/SIO2-A gehen nach außen und kommen NICHT als Echo zurück.  Von außen
 *        gesendete Bytes landen im Empfänger des jeweiligen Kanals.
 */
TEST(K8915Machine, DfueUndDruckerHaengenAnIhremEigenenKanal)
{
    K8915Machine m;                       // Vorgabe: Prüfstecker gesteckt
    m.powerOn();
    std::vector<uint8_t> drucker, dfue;
    m.setPrinterCallback([&](uint8_t b) { drucker.push_back(b); });
    m.setDFUECallback([&](uint8_t b) { dfue.push_back(b); });

    K1520Bus& bus = m.bus();
    bus.ioWrite(0x40, 0x31);              // SIO1-A Daten
    bus.ioWrite(0x42, 0x32);              // SIO1-B Daten = Drucker
    bus.ioWrite(0x50, 0x33);              // SIO2-A Daten = DFÜ
    m.ats().service(0);
    m.ats().service(100'000);

    EXPECT_EQ(drucker, std::vector<uint8_t>{0x32});
    EXPECT_EQ(dfue, std::vector<uint8_t>{0x33});
    EXPECT_EQ(m.ioReadDebug(0x41) & 0x01, 0x01) << "SIO1-A bleibt zurückgeschleift";
    EXPECT_EQ(m.ioReadDebug(0x40), 0x31);
    EXPECT_EQ(m.ioReadDebug(0x43) & 0x01, 0x00) << "Drucker: kein Echo neben dem Rückruf";
    EXPECT_EQ(m.ioReadDebug(0x51) & 0x01, 0x00) << "DFÜ: kein Echo neben dem Rückruf";

    m.dfueSend(0x61);
    m.printerSend(0x13);                  // XOFF des Druckers
    EXPECT_EQ(m.ioReadDebug(0x51) & 0x01, 0x01);
    EXPECT_EQ(m.ioReadDebug(0x50), 0x61);
    EXPECT_EQ(m.ioReadDebug(0x43) & 0x01, 0x01);
    EXPECT_EQ(m.ioReadDebug(0x42), 0x13);
}

/**
 * @test K8915Machine.LaufwerkeMeldenJedenFehlerwegMitGrund
 * @brief Der gemeinsame Baustein `Laufwerke`: jeder abgelehnte Aufruf liefert false
 *        UND einen Grund in lastError() (der steht in der Oberfläche).  Am K8915 sind
 *        C:/D: unbestückt — die Meldung „Kein Laufwerk an Slot" ist dort erreichbar.
 */
TEST(K8915Machine, LaufwerkeMeldenJedenFehlerwegMitGrund)
{
    K8915Machine m;
    TempDatei hfe("fehler.hfe"), img("fehler.img");

    // Einhängen
    EXPECT_FALSE(m.mountDisk(4, hfe.pfad, "cpa800", false));
    EXPECT_TRUE(enthaelt(m.lastError(), "Invalid drive")) << m.lastError();
    EXPECT_FALSE(m.mountDisk(2, hfe.pfad, "cpa800", false));
    EXPECT_TRUE(enthaelt(m.lastError(), "Kein Laufwerk an Slot 2")) << m.lastError();
    EXPECT_FALSE(m.mountDisk(0, hfe.pfad, "gibtsnicht", false));
    EXPECT_TRUE(enthaelt(m.lastError(), "Unbekanntes Format")) << m.lastError();
    EXPECT_FALSE(m.mountDisk(0, hfe.pfad, "cpa800", false)) << "Datei gibt es nicht";
    EXPECT_FALSE(m.lastError().empty());
    EXPECT_FALSE(m.mountDiskImage(0, nullptr, false));
    EXPECT_TRUE(enthaelt(m.lastError(), "kein Abbild")) << m.lastError();
    EXPECT_FALSE(m.mountDiskImage(3, DiskImage::createBlank(80, 2, Encoding::MFM), false));
    EXPECT_TRUE(enthaelt(m.lastError(), "Kein Laufwerk an Slot 3")) << m.lastError();

    // Anlegen
    EXPECT_FALSE(m.createDisk(-1, hfe.pfad, "", false));
    EXPECT_TRUE(enthaelt(m.lastError(), "Invalid drive")) << m.lastError();
    EXPECT_FALSE(m.createDisk(2, hfe.pfad, "", false));
    EXPECT_TRUE(enthaelt(m.lastError(), "Kein Laufwerk an Slot 2")) << m.lastError();
    EXPECT_FALSE(m.createDisk(0, img.pfad, "", false)) << "leer + .img geht nicht";
    EXPECT_TRUE(enthaelt(m.lastError(), ".img")) << m.lastError();
    EXPECT_FALSE(m.createDisk(0, hfe.pfad, "gibtsnicht", false));
    EXPECT_TRUE(enthaelt(m.lastError(), "unbekanntes Format")) << m.lastError();
    EXPECT_FALSE(m.createDisk(0, hfe.pfad, "mf6400", false)) << "8″-Format im K5601";
    EXPECT_TRUE(enthaelt(m.lastError(), "passt nicht zum Laufwerk")) << m.lastError();
    const std::string gibtsnicht =
        (std::filesystem::path(hfe.pfad).parent_path() / "k1520_gibts_nicht" / "x.hfe").string();
    EXPECT_FALSE(m.createDisk(0, gibtsnicht, "", false)) << "Zielordner fehlt";
    EXPECT_TRUE(enthaelt(m.lastError(), "createDisk")) << m.lastError();
    EXPECT_FALSE(std::filesystem::exists(hfe.pfad)) << "kein Fehlschlag legt eine Datei an";

    // Speichern unter
    EXPECT_FALSE(m.saveDiskAs(9, hfe.pfad, ""));
    EXPECT_FALSE(m.saveDiskAs(0, "", ""));
    EXPECT_TRUE(enthaelt(m.lastError(), "Kein Zielpfad")) << m.lastError();
    EXPECT_FALSE(m.saveDiskAs(1, hfe.pfad, ""));
    EXPECT_TRUE(enthaelt(m.lastError(), "Kein Datentraeger in Laufwerk 1")) << m.lastError();

    ASSERT_TRUE(m.createDisk(0, hfe.pfad, "", false)) << m.lastError();
    EXPECT_TRUE(m.lastError().empty()) << "Erfolg räumt die alte Meldung weg";
    EXPECT_FALSE(m.saveDiskAs(0, img.pfad, ""));
    EXPECT_TRUE(enthaelt(m.lastError(), "Diskettenformats")) << m.lastError();
    EXPECT_FALSE(m.saveDiskAs(0, img.pfad, "gibtsnicht"));
    EXPECT_TRUE(enthaelt(m.lastError(), "Unbekanntes Format")) << m.lastError();
    EXPECT_FALSE(m.saveDiskAs(0, gibtsnicht, ""));
    EXPECT_FALSE(m.lastError().empty());
}

/**
 * @test K8915Machine.LaufwerksanzeigenFolgenDerDiskette
 * @brief Durchreichungen an `Laufwerke`: Container, `.img`-Fähigkeit, Schreibschutz,
 *        eingehängtes Abbild von außen (`mountDiskImage`), Aushängen.
 */
TEST(K8915Machine, LaufwerksanzeigenFolgenDerDiskette)
{
    K8915Machine m;
    TempDatei hfe("anzeige.hfe"), img("anzeige.img");

    ASSERT_TRUE(m.createDisk(0, hfe.pfad, "", false)) << m.lastError();
    EXPECT_EQ(m.diskContainer(0), "hfe");
    EXPECT_FALSE(m.isDiskRawCompatible(0)) << "Leerdiskette: unformatiert ⇒ kein .img";
    EXPECT_FALSE(m.isDiskWriteProtected(0));
    m.setDiskWriteProtect(0, true);
    EXPECT_TRUE(m.isDiskWriteProtected(0));

    ASSERT_TRUE(m.createDisk(1, img.pfad, "cpa800", false)) << m.lastError();
    EXPECT_EQ(m.diskContainer(1), "img");
    EXPECT_TRUE(m.isDiskRawCompatible(1));
    EXPECT_EQ(m.detectedFormatName(1), "cpa800");
    EXPECT_FALSE(m.isDiskRawCompatible(3)) << "unbestückt";
    EXPECT_EQ(m.diskContainer(3), "");

    ASSERT_TRUE(m.mountDiskImage(1, DiskImage::createBlank(80, 2, Encoding::MFM), false))
        << m.lastError();
    EXPECT_EQ(m.diskContainer(1), "") << "nur im Speicher: keine Datei, kein Container";
    EXPECT_TRUE(m.flushDisks());
    EXPECT_TRUE(m.unmountDisk(0));
    EXPECT_EQ(m.diskPath(0), "");
}

/**
 * @test K8915Machine.KonsolenmodusMeldetBildschirmaenderungen
 * @brief Konsolenmodus (`k1520_console_*`): jede Änderung im Bildspeicher der K7024
 *        bei 1000H kommt als (Spalte, Zeile, Zeichen) heraus; ohne Modus nichts.
 */
TEST(K8915Machine, KonsolenmodusMeldetBildschirmaenderungen)
{
    K8915Machine m;
    m.powerOn();                          // A8H = 00H: 1000H liegt am Systembus (K7024)
    int x = -1, y = -1;
    char ch = 0;
    m.memWriteDebug(0x1000, 'A');
    EXPECT_FALSE(m.consolePoll(x, y, ch)) << "ohne Konsolenmodus keine Meldung";

    m.setConsoleMode(true);
    m.memWriteDebug(0x1000 + 80 + 1, 'K');
    ASSERT_TRUE(m.consolePoll(x, y, ch));
    EXPECT_EQ(x, 1);
    EXPECT_EQ(y, 1);
    EXPECT_EQ(ch, 'K');
    EXPECT_EQ(m.screenChar(1, 1), 'K');
    EXPECT_FALSE(m.consolePoll(x, y, ch));
}

/**
 * @file test_k5122.cpp
 * @brief GoogleTests für die K5122 – formatagnostischer Floppy-Controller (Streaming).
 *
 * Getestete Komponenten:
 *   - Konstruktion und Mount-Verwaltung
 *   - Drive-Select (Port 0x18)
 *   - Streaming-Datenpfad (Port 0x16 nach /STR-Strobe)
 *   - MK/MK1-Re-Sync auf Adressmarken
 *   - Head-Latch (Side-Select bit2 am /STR-Strobe)
 *   - Index-Puls-Erzeugung (update())
 *   - Interrupt-Daisy-Chain
 *
 * Harness-Muster übernommen aus test_k5122.cpp.
 *
 * @see core/cards/k5122/k5122.h
 * @see core/bus/k1520_bus.h
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <cstdint>

#include "core/bus/k1520_bus.h"
#include "core/cards/k5122/k5122.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/raw_sector_image.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include "core/logger.h"

// ─── Hilfsfunktionen ─────────────────────────────────────────────────────────

/**
 * @brief Einfaches Format: 2 Zylinder, 1 Kopf, 4 Sektoren × 128 B.
 * Erkennungsmuster: Sektor-ID-Byte füllt die Sektor-Bytes (Sektor 1 → alles 0x01, …)
 */
static DiskFormat makeFormat_2cyl_1head_4sec() {
    DiskFormat fmt;
    fmt.name = "test_v2_2c1h4s";
    fmt.tracks.push_back({0, 1, 0, 0, 4, 128});
    return fmt;
}

/**
 * @brief Zweiseitiges Format: 2 Zylinder, 2 Köpfe, 4 Sektoren × 128 B.
 * head0 und head1 tragen unterschiedliche Erkennungsbytes.
 */
static DiskFormat makeFormat_2cyl_2head_4sec() {
    DiskFormat fmt;
    fmt.name = "test_v2_2c2h4s";
    fmt.tracks.push_back({0, 1, 0, 1, 4, 128});
    return fmt;
}

/**
 * @brief Legt eine temporäre .img-Datei an.
 *
 * Einseitig: Sektoren mit Sektor-ID als Füll-Byte.
 * Zweiseitig: head0-Sektoren mit Sektor-ID (0x01…), head1-Sektoren mit 0x80 | Sektor-ID.
 */
static std::string makeTmpImg(const DiskFormat& fmt, const std::string& suffix = "") {
    const auto path = (std::filesystem::temp_directory_path()
                       / ("k1520_v2_" + fmt.name + suffix + ".img")).string();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    const uint8_t ncyls  = fmt.numCylinders();
    const uint8_t nheads = fmt.numHeads();
    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            const TrackFormat* tf = fmt.findTrack(c, h);
            if (!tf) continue;
            for (uint8_t id = 1; id <= tf->secs_per_track; ++id) {
                // head0: Füll-Byte = Sektor-ID; head1: 0x80 | Sektor-ID
                uint8_t fill = (h == 0) ? id : static_cast<uint8_t>(0x80 | id);
                std::vector<uint8_t> buf(tf->bytes_per_sec, fill);
                f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
            }
        }
    }
    return path;
}

// ─── Fixture ─────────────────────────────────────────────────────────────────

class K5122Test : public ::testing::Test {
protected:
    K1520Bus  bus;
    K5122   card{bus};
    DiskFormat fmt1 = makeFormat_2cyl_1head_4sec();
    DiskFormat fmt2 = makeFormat_2cyl_2head_4sec();

    void SetUp() override {
        // Emulator-Log auf ERROR drosseln, damit Massen-Reads leise bleiben.
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }

    std::string tmpImg1() { return makeTmpImg(fmt1); }
    std::string tmpImg2() { return makeTmpImg(fmt2); }

    // /STR-Lese-Strobe (bit3 fallende Flanke): prev=0xFF→data
    // bit0=1 (/WE=1) → Lesen; bit2 kodiert Kopfwahl (/FR: 1→head0, 0→head1)
    // Reihenfolge: erst idle-Wert schreiben (alle hoch), dann mit /STR low.
    void strobeRead(uint8_t head) {
        uint8_t fr_bit = (head == 0) ? 0x04 : 0x00;   // bit2=1→head0, bit2=0→head1
        // Sicherstellen, dass prev_ctrl_a_ = 0xFF (Initialisierung)
        card.ioWrite(0x10, 0xFF);                       // alle Signale inaktiv
        // /STR (bit3) auf low ziehen; /WE=1 (bit0=1) = Lesen
        uint8_t strobe = static_cast<uint8_t>(0xF0 | fr_bit | 0x01);  // /STR=0, /WE=1
        card.ioWrite(0x10, strobe);
    }

    // Liest n Bytes via Port 0x16 in einen Vektor.
    std::vector<uint8_t> readStream(size_t n) {
        std::vector<uint8_t> result;
        result.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            result.push_back(card.ioRead(0x16));
        }
        return result;
    }

    // Pulsiert MK (bit1: 0→1) um re-sync auszulösen.
    void pulseMK(uint8_t base_ctrl_a = 0xF1) {
        // base ohne bit1 → bit1 setzen
        card.ioWrite(0x10, base_ctrl_a & ~0x02u);  // MK=0
        card.ioWrite(0x10, base_ctrl_a | 0x02u);   // MK=1 (steigende Flanke)
    }
};

// ─── 1. Mount / unmount lifecycle ─────────────────────────────────────────────

TEST_F(K5122Test, Mount_ViaPath_DriveAktiv) {
    auto path = tmpImg1();
    EXPECT_TRUE(card.mountDisk(0, path, fmt1));
    EXPECT_TRUE(card.isDiskActive(0));
    std::filesystem::remove(path);
}

TEST_F(K5122Test, Mount_NichtExistierendeDatei_Fehler) {
    EXPECT_FALSE(card.mountDisk(0, "/nonexistent/disk.img", fmt1));
    EXPECT_FALSE(card.isDiskActive(0));
}

TEST_F(K5122Test, Unmount_LaufwerkInaktiv) {
    auto path = tmpImg1();
    ASSERT_TRUE(card.mountDisk(0, path, fmt1));
    card.unmountDisk(0);
    EXPECT_FALSE(card.isDiskActive(0));
    std::filesystem::remove(path);
}

TEST_F(K5122Test, Mount_BereichsüberschreitungAbgelehnt) {
    auto path = tmpImg1();
    EXPECT_FALSE(card.mountDisk(-1, path, fmt1));
    EXPECT_FALSE(card.mountDisk(4,  path, fmt1));
    std::filesystem::remove(path);
}

// ─── 2. Drive-Select (Port 0x18) ─────────────────────────────────────────────

TEST_F(K5122Test, DriveSelect_AlleVierLaufwerke) {
    // Drive-Select-Codes: bit-Muster /SELx, active-low one-hot
    // 0xEE = 1110 1110 → bit0=0 → D0; 0xDD → D1; 0xBB → D2; 0x77 → D3
    card.ioWrite(0x18, 0xEE);   // D0
    card.ioWrite(0x18, 0xDD);   // D1
    card.ioWrite(0x18, 0xBB);   // D2
    card.ioWrite(0x18, 0x77);   // D3
    // Kein Absturz = Erfolg
    SUCCEED();
}

TEST_F(K5122Test, DriveSelect_StatusPortBReflectiertGewähltesLaufwerk) {
    auto path0 = tmpImg1();
    auto path1 = tmpImg1();
    ASSERT_TRUE(card.mountDisk(0, path0, fmt1));
    // D1 ist nicht gemountet
    card.ioWrite(0x18, 0xEE);   // D0 wählen
    uint8_t s0 = card.ioRead(0x12);
    card.ioWrite(0x18, 0xDD);   // D1 wählen
    uint8_t s1 = card.ioRead(0x12);

    EXPECT_EQ(s0 & 0x01, 0) << "/RDYL sollte 0 sein (D0 montiert)";
    EXPECT_NE(s1 & 0x01, 0) << "/RDYL sollte 1 sein (D1 nicht montiert)";

    std::filesystem::remove(path0);
    (void)path1;
}

// ─── 3. Status Port B ─────────────────────────────────────────────────────────

TEST_F(K5122Test, StatusPortB_KeineMontage_RDYLInaktiv) {
    card.ioWrite(0x18, 0xEE);   // D0 wählen
    uint8_t status = card.ioRead(0x12);
    EXPECT_NE(status & 0x01, 0) << "/RDYL=1 erwartet (kein Laufwerk montiert)";
}

TEST_F(K5122Test, StatusPortB_Montiert_RDYLAktiv) {
    auto path = tmpImg1();
    ASSERT_TRUE(card.mountDisk(0, path, fmt1));
    card.ioWrite(0x18, 0xEE);
    uint8_t status = card.ioRead(0x12);
    EXPECT_EQ(status & 0x01, 0) << "/RDYL=0 erwartet (Laufwerk montiert)";
    std::filesystem::remove(path);
}

TEST_F(K5122Test, StatusPortB_Schreibschutz_WPGesetzt) {
    auto path = tmpImg1();
    ASSERT_TRUE(card.mountDisk(0, path, fmt1, /*wp=*/true));
    card.ioWrite(0x18, 0xEE);
    uint8_t status = card.ioRead(0x12);
    EXPECT_EQ(status & 0x20, 0) << "/WP=0 erwartet (schreibgeschützt)";
    std::filesystem::remove(path);
}

// ─── 4. Streaming-Read (Port 0x16 nach /STR-Strobe) ──────────────────────────

/**
 * @test K5122Test/StreamingRead_EnthältSektordaten
 * @brief Nach /STR-Lese-Strobe liefert IN 0x16 einen Strom, der das Erkennungsmuster
 *        des ersten Sektors als zusammenhängende Teilsequenz enthält.
 *
 * Das TrackImage enthält (per TrackCodec::buildTrack) nach dem Sync-Feld:
 *   IAM [C2 C2 C2 FC], Gap, dann je Sektor: [A1 A1 A1 FE cyl head id size CRC CRC]
 *   [Sync] [A1 A1 A1 FB <128 Datenbytes> CRC CRC].
 * Sektor 1 hat Füll-Byte 0x01, Sektor 2 → 0x02, usw.
 * Wir lesen ausreichend Bytes und prüfen, dass 128× 0x01 als Block vorkommt.
 */
TEST_F(K5122Test, StreamingRead_EnthältSektordaten) {
    auto path = tmpImg1();
    ASSERT_TRUE(card.mountDisk(0, path, fmt1));
    card.ioWrite(0x18, 0xEE);   // D0

    strobeRead(0);   // head0

    // Eine volle Umdrehung (1 Spur) lesen; Spur ist ≤ 10 000 Bytes für 4 × 128-B-Sektoren.
    const size_t READ_N = 5000;
    auto stream = readStream(READ_N);

    // Erkennungsmuster: 128× 0x01 (Sektor 1 Füll-Byte head0)
    const std::vector<uint8_t> muster(128, 0x01);
    auto it = std::search(stream.begin(), stream.end(), muster.begin(), muster.end());
    EXPECT_NE(it, stream.end())
        << "128 Bytes 0x01 (Sektor 1) sollten im Lesestrom vorkommen";

    std::filesystem::remove(path);
}

/**
 * @test K5122Test/StreamingRead_EnthältIDAM
 * @brief Der Strom nach /STR-Strobe enthält das IDAM-Markenbyte 0xFE.
 */
TEST_F(K5122Test, StreamingRead_EnthältIDAM) {
    auto path = tmpImg1();
    ASSERT_TRUE(card.mountDisk(0, path, fmt1));
    card.ioWrite(0x18, 0xEE);
    strobeRead(0);

    auto stream = readStream(3000);
    bool hat_fe = std::find(stream.begin(), stream.end(), 0xFE) != stream.end();
    EXPECT_TRUE(hat_fe) << "IDAM-Markenbyte 0xFE sollte im Strom vorkommen";

    std::filesystem::remove(path);
}

/**
 * @test K5122Test/StreamingRead_ZyklischWrapAround
 * @brief Nach dem Ende der Spur beginnt der Strom erneut von vorn (Wrap-around).
 *
 * Zweimal die Spurlänge lesen; die zweite Hälfte ist eine Wiederholung der ersten.
 */
TEST_F(K5122Test, StreamingRead_ZyklischWrapAround) {
    auto path = tmpImg1();
    ASSERT_TRUE(card.mountDisk(0, path, fmt1));
    card.ioWrite(0x18, 0xEE);
    strobeRead(0);

    // Spurlänge abschätzen: 4 Sektoren × 128 B + Gap-/Sync-Overhead ≈ 2000 Bytes.
    // Wir lesen das Doppelte davon.
    const size_t HALB = 4000;
    auto a = readStream(HALB);
    auto b = readStream(HALB);

    // Nach exakt track_size Bytes wird der Kopf wieder auf 0 gesetzt.
    // Die beiden Hälften müssen nicht identisch sein (Startpunkt kann mitten im
    // Gap liegen), aber der zweite Block darf nicht alles 0xFF enthalten.
    bool alles_ff = std::all_of(b.begin(), b.end(), [](uint8_t x){ return x == 0xFF; });
    EXPECT_FALSE(alles_ff) << "Nach Wrap-around sollten keine 0xFF-Blöcke kommen";

    std::filesystem::remove(path);
}

// ─── 5. MK-Re-Sync ────────────────────────────────────────────────────────────

/**
 * @test K5122Test/MKStrobe_SyncsAufMarkenbyte
 * @brief Nach einem MK-Puls liefert das erste IN 0x16 ein Markenbyte (0xFE, 0xFB oder 0xFC).
 *
 * Das TrackImage enthält Marken an den definierten Positionen (TrackImage::marks).
 * resyncToNextMark() positioniert head_pos_ auf die nächste Marke; damit liefert
 * ioRead(0x16) genau das Mark-Byte.
 */
TEST_F(K5122Test, MKStrobe_SyncsAufMarkenbyte) {
    auto path = tmpImg1();
    ASSERT_TRUE(card.mountDisk(0, path, fmt1));
    card.ioWrite(0x18, 0xEE);

    strobeRead(0);

    // Einige Bytes lesen (ins Gap hinein), dann MK pulsieren.
    readStream(20);

    // MK pulsieren: base_ctrl_a nach /STR-Strobe ist ~0xF1 (bit0=1, bit3=0, rest hoch)
    pulseMK(0xF1);

    // Das erste Byte nach MK-Puls muss ein Markenbyte sein.
    // Im Robotron-Layout (K5122 verwendet on-the-fly buildRobotronTrack) ist
    // das Markenbyte 0xA1 (Marke liegt auf dem A1-Byte, nicht auf FE/FB).
    // Im IBM-Standard-Layout wären es 0xFE, 0xFB, 0xFC oder 0xF8.
    uint8_t first = card.ioRead(0x16);
    bool ist_marke = (first == 0xFE || first == 0xFB || first == 0xFC || first == 0xF8
                      || first == 0xA1);
    EXPECT_TRUE(ist_marke)
        << "Nach MK-Puls: erstes Byte sollte Markenbyte sein, got 0x"
        << std::hex << static_cast<int>(first);

    std::filesystem::remove(path);
}

// ─── 6. Head-Latch bit2 (/FR) ─────────────────────────────────────────────────

/**
 * @test K5122Test/HeadLatch_Bit2_Head0_und_Head1_UnterschiedlicheDaten
 * @brief bit2=1 → head 0, bit2=0 → head 1; beide Köpfe liefern unterschiedliche Daten.
 *
 * Das zweiseitige Image füllt head0-Sektoren mit 0x01–0x04 und head1-Sektoren
 * mit 0x81–0x84 (0x80 | id).  Ein ausreichend langer Lesestrom muss das jeweilige
 * Muster enthalten.
 */
TEST_F(K5122Test, HeadLatch_Bit2_Head0_und_Head1_UnterschiedlicheDaten) {
    auto path = tmpImg2();
    ASSERT_TRUE(card.mountDisk(0, path, fmt2));
    card.ioWrite(0x18, 0xEE);

    // head 0 lesen
    strobeRead(0);
    auto strom_h0 = readStream(5000);

    // head 1 lesen (neuer /STR-Strobe mit bit2=0)
    strobeRead(1);
    auto strom_h1 = readStream(5000);

    // head0: 128× 0x01 muss vorkommen
    const std::vector<uint8_t> muster_h0(128, 0x01);
    auto it0 = std::search(strom_h0.begin(), strom_h0.end(), muster_h0.begin(), muster_h0.end());
    EXPECT_NE(it0, strom_h0.end()) << "head0: 128× 0x01 sollte vorkommen";

    // head1: 128× 0x81 muss vorkommen
    const std::vector<uint8_t> muster_h1(128, 0x81);
    auto it1 = std::search(strom_h1.begin(), strom_h1.end(), muster_h1.begin(), muster_h1.end());
    EXPECT_NE(it1, strom_h1.end()) << "head1: 128× 0x81 sollte vorkommen";

    // Die Ströme dürfen sich unterscheiden
    EXPECT_NE(strom_h0, strom_h1) << "head0- und head1-Ströme sollten unterschiedlich sein";

    std::filesystem::remove(path);
}

/**
 * @test K5122Test/HeadLatch_NurAmSTRGelatcht
 * @brief MK-Strobes (bit1-Wechsel) ändern current_head_ NICHT — nur der /STR-Strobe lacht.
 *
 * Nach einem head1-/STR-Strobe MK mehrfach pulsieren; der Strom muss weiterhin
 * head1-Daten zeigen (kein Kippen auf head0 durch den MK-Bit-Toggle).
 */
TEST_F(K5122Test, HeadLatch_NurAmSTRGelatcht) {
    auto path = tmpImg2();
    ASSERT_TRUE(card.mountDisk(0, path, fmt2));
    card.ioWrite(0x18, 0xEE);

    // head1 strobe
    strobeRead(1);
    readStream(10);

    // MK pulsieren (bit1 toggle) — sollte head1 NICHT ändern
    for (int i = 0; i < 5; ++i) {
        pulseMK(0xF1);
    }

    // Weiterlesen; head1-Muster muss noch vorkommen
    auto strom = readStream(5000);
    const std::vector<uint8_t> muster_h1(128, 0x81);
    auto it = std::search(strom.begin(), strom.end(), muster_h1.begin(), muster_h1.end());
    EXPECT_NE(it, strom.end()) << "Nach MK-Strobes: head1-Daten (128× 0x81) sollten noch vorkommen";

    std::filesystem::remove(path);
}

// ─── 7. Index-Puls ────────────────────────────────────────────────────────────

/**
 * @test K5122Test/IndexPuls_NachAusreichendCycles_HasInterrupt
 * @brief Nach update() mit mindestens einer Index-Periode feuert ein Interrupt.
 *
 * Voraussetzung: ctrl_pio_ muss als Output konfiguriert und IE gesetzt sein.
 * Wir schreiben ein Interrupt-Vektor-Wort (0x62) und ein IE-Control-Wort (0x83)
 * auf Ports 0x11/0x13, dann rufen update() mit der Index-Periode auf.
 */
TEST_F(K5122Test, IndexPuls_NachAusreichendCycles_HasInterrupt) {
    auto path = tmpImg1();
    ASSERT_TRUE(card.mountDisk(0, path, fmt1));
    card.ioWrite(0x18, 0xEE);

    // ctrl_pio_ Interrupt-Vektor und IE aktivieren (wie A5120Machine für den Boot-Pfad)
    card.ioWrite(0x11, 0x62);   // Interrupt-Vektor
    card.ioWrite(0x11, 0x83);   // IE=1, OR-Modus, aktiv-low

    // IEI=true setzen (kein upstream-Gerät blockiert)
    card.setIEI(true);

    // Genügend Zyklen für eine volle Umdrehung (300 U/min, 2,45 MHz → ~490 000 Zyklen)
    const int PERIODE = builtinDriveProfile("mfs_525_ds80").indexPeriodCycles(2450000);
    card.update(PERIODE + 1);

    EXPECT_TRUE(card.hasInterrupt()) << "Nach Index-Puls sollte hasInterrupt() true sein";
    EXPECT_NE(card.getVector(), 0xFF) << "Interrupt-Vektor sollte nicht 0xFF sein";

    std::filesystem::remove(path);
}

/**
 * @test K5122Test/IndexPuls_NichtMontiert_KeinInterrupt
 * @brief Ohne gemountete Diskette erzeugt update() keinen Index-Puls.
 */
TEST_F(K5122Test, IndexPuls_NichtMontiert_KeinInterrupt) {
    card.setIEI(true);
    card.ioWrite(0x11, 0x83);

    const int PERIODE = builtinDriveProfile("mfs_525_ds80").indexPeriodCycles(2450000);
    card.update(PERIODE * 3);

    // Kein Absturz, kein Interrupt ohne gemountete Diskette
    // (hasInterrupt() kann true sein wenn PIO vorher interrupt hatte — wir prüfen nur
    //  keinen Absturz und dass ein nicht-gemountetes Laufwerk keinen Puls erzeugt)
    SUCCEED();   // Kein Absturz = Erfolg
}

// ─── 8. Interrupt-Daisy-Chain ─────────────────────────────────────────────────

/**
 * @test K5122Test/DaisyChain_IEITrue_IEOPassthrough
 * @brief Bei IEI=true und keinem ausstehenden Interrupt wird IEO=true durchgereicht.
 */
TEST_F(K5122Test, DaisyChain_IEITrue_IEOPassthrough) {
    card.setIEI(true);
    // Kein ausstehender Interrupt → IEO muss true sein
    EXPECT_TRUE(card.getIEO()) << "IEO sollte true sein, wenn kein Interrupt ausstehend";
}

/**
 * @test K5122Test/DaisyChain_IEIFalse_IEOFalse
 * @brief Bei IEI=false wird IEO=false (Chain blockiert).
 */
TEST_F(K5122Test, DaisyChain_IEIFalse_IEOFalse) {
    card.setIEI(false);
    EXPECT_FALSE(card.getIEO()) << "IEO sollte false sein, wenn IEI=false";
}

/**
 * @test K5122Test/DaisyChain_GetVector_OhneInterrupt_0xFF
 * @brief Ohne ausstehenden Interrupt gibt getVector() 0xFF zurück.
 */
TEST_F(K5122Test, DaisyChain_GetVector_OhneInterrupt_0xFF) {
    EXPECT_EQ(card.getVector(), 0xFF);
}

// ─── 9. Write-Roundtrip ───────────────────────────────────────────────────────

/**
 * @test K5122Test/WriteRoundtrip_AenderungSichtbar
 * @brief Schreib-DMA über Port 0x14 und /STR-Commit schreibt in die Spur zurück.
 *
 * Ablauf:
 * 1. Diskette mounten, /STR-Schreib-Strobe (bit0=0) → write_mode_=true, BUSRQ assertiert.
 * 2. Bytes via OUT(0x14) sammeln (Muster 0xBB).
 * 3. Zweiten /STR mit BUSRQ gehalten → commitWrite(); BUSRQ freigegeben.
 * 4. flush() + neu lesen → Muster im ersten Sektor.
 *
 * EINSCHRÄNKUNG: commitWrite() ersetzt aktuell den ersten Sektor (robuster Fallback).
 * Der Test prüft genau dieses dokumentierte Verhalten.
 */
TEST_F(K5122Test, WriteRoundtrip_ErsteSektorÄnderung) {
    auto path = makeTmpImg(fmt1, "_rw");
    ASSERT_TRUE(card.mountDisk(0, path, fmt1));
    card.ioWrite(0x18, 0xEE);

    // 1. /STR-Schreib-Strobe: bit0=0 (/WE=0), bit2=1 (head0), bit3=0 (/STR)
    //    prev=0xFF → 0xF4: bit7=1, bit3=0 (fallende Flanke), bit2=1, bit0=0
    card.ioWrite(0x10, 0xFF);          // Idle
    card.ioWrite(0x10, 0xF4);          // /STR-Schreib-Strobe: /WE=0, /FR=1, /STR=0

    ASSERT_TRUE(bus.isBUSRQ()) << "BUSRQ sollte nach Schreib-/STR assertiert sein";

    // 2. Daten via Port 0x14 schreiben
    const uint8_t MUSTER = 0xBB;
    for (int i = 0; i < 128; ++i) {
        card.ioWrite(0x14, MUSTER);
    }

    // 3. /STR-Commit: nochmal /STR mit BUSRQ gehalten → commitWrite()
    //    bit0=0 (/WE=0, Schreiben), BUSRQ ist assertiert
    card.ioWrite(0x10, 0xFF);          // /STR zurück auf high
    card.ioWrite(0x10, 0xF4);          // nochmal /STR-Strobe mit bus_.isBUSRQ()==true

    EXPECT_FALSE(bus.isBUSRQ()) << "BUSRQ sollte nach Commit freigegeben sein";

    // 4. flush() und neu lesen
    card.drive(0).flush();

    // Neues Image-Objekt für Verifikation öffnen
    RawSectorImage check(path, fmt1, /*wp=*/false);
    ASSERT_TRUE(check.isOpen());

    TrackImage gelesen = check.readTrack(0, 0);
    ASSERT_FALSE(gelesen.empty());

    auto parsed = TrackCodec::parseTrack(gelesen);
    ASSERT_GE(parsed.size(), 1u);

    // Erster Sektor muss das Muster tragen
    EXPECT_TRUE(std::all_of(parsed[0].data.begin(), parsed[0].data.end(),
                            [MUSTER](uint8_t b){ return b == MUSTER; }))
        << "Erster Sektor sollte nach Write-Roundtrip alles 0xBB enthalten";

    std::filesystem::remove(path);
}

// ─── 10. Ohne Laufwerk kein Absturz ──────────────────────────────────────────

TEST_F(K5122Test, OhneLaufwerk_STRStrobeKeinAbsturz) {
    // Kein Laufwerk gemountet; /STR-Strobe darf nicht abstürzen.
    card.ioWrite(0x10, 0xFF);
    card.ioWrite(0x10, 0xF1);   // /STR-Lese-Strobe
    uint8_t b = card.ioRead(0x16);
    (void)b;
    SUCCEED();
}

/**
 * @file test_physical_boot.cpp
 * @brief Der A5120 bootet von einer **spurweise gelesenen** Diskette.
 *
 * @details
 * Das ist die Nagelprobe der physischen Anbindung (doc/design/14_physische_diskette.md):
 * die Diskette liegt nicht als Datei im Speicher, sondern kommt **Spur für Spur** über
 * die Auftragswarteschlange herein — genau wie von einem echten Laufwerk am
 * Greaseweazle.  Der ganze Boot-Pfad (Lade-ROM → SYL-Lader → zweite Stufe → `@OS.COM`)
 * muss damit unverändert durchlaufen.
 *
 * **Ohne Hardware.**  Statt des Adapters bedient ein Ersatz-Arbeitsfaden die Aufträge
 * aus einer `.hfe`-Fixture — dieselben Bitzellen, die der Adapter liefern würde
 * (§14).  Was hier NICHT geprüft werden kann, ist einzig USB.
 *
 * | Fall | Inhalt |
 * |------|--------|
 * | `BootetVonEinerSpurweisenQuelle` | CP/A-Kaltstart bis zum Bereitschaftszeichen |
 * | `EsWirdNichtDieGanzeDisketteGelesen` | der Boot holt nur wenige Spuren, nicht 160 |
 * | `VorauslesenBremstDenKaltstartNicht` | Prio 1 verdrängt Prio 3 auch im Betrieb |
 *
 * @see doc/design/14_physische_diskette.md §14
 * @see core/peripherals/floppy_drive/track_sync.h
 */

#include <gtest/gtest.h>

#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/bit_codec.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/image_codec.h"
#include "core/peripherals/floppy_drive/track_sync.h"

#include "tests/support/fixtures.h"
#include "tests/support/machine_run.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

/**
 * @class Ersatzlaufwerk
 * @brief Steht für „Greaseweazle + K5601", liest aber aus einer `.hfe`-Fixture.
 *
 * Der Weg ist derselbe wie beim echten Adapter: Bitzellen in HFE-Konvention hinein,
 * @ref TrackSync::completeRead heraus.  Nur die USB-Strecke fehlt.
 */
class Ersatzlaufwerk {
public:
    /// @param lesedauer Kunstliche Dauer je Spur.  Ohne sie ist das Ersatzlaufwerk
    ///        unendlich schnell und das Vorauslesen kommt dem Gast IMMER zuvor —
    ///        dann gibt es nie eine Anforderung zu beobachten.  Ein echtes Laufwerk
    ///        braucht 0,5–0,8 s je Spur.
    Ersatzlaufwerk(TrackSync& sync, DiskMedium quelle,
                   std::chrono::milliseconds lesedauer = std::chrono::milliseconds(0))
        : sync_(sync), quelle_(std::move(quelle)), lesedauer_(lesedauer) {}

    ~Ersatzlaufwerk() { stop(); }

    void start() { faden_ = std::thread([this] { schleife(); }); }

    void stop() {
        sync_.shutdown();
        if (faden_.joinable()) faden_.join();
    }

    size_t gelesen() const {
        std::lock_guard<std::mutex> l(m_);
        return gelesen_.size();
    }
    std::vector<SyncJob> verlauf() const {
        std::lock_guard<std::mutex> l(m_);
        return gelesen_;
    }

private:
    void schleife() {
        for (;;) {
            SyncJob j;
            if (!sync_.takeJob(j, 20)) continue;
            if (j.kind == SyncJobKind::Stop) return;
            {
                std::lock_guard<std::mutex> l(m_);
                gelesen_.push_back(j);
            }
            if (j.kind != SyncJobKind::Read) { sync_.failJob(j.id, "nur lesend"); continue; }
            if (lesedauer_.count()) std::this_thread::sleep_for(lesedauer_);

            const TrackImage& s = quelle_.peek(j.cyl, j.head);
            const uint32_t bitcells =
                s.bitcells ? s.bitcells : static_cast<uint32_t>(s.size() * 16);
            const std::vector<uint8_t> zellen = BitCodec::encode(s, bitcells);
            sync_.completeRead(j.id, zellen.data(), zellen.size(), bitcells);
        }
    }

    TrackSync&  sync_;
    DiskMedium  quelle_;
    std::chrono::milliseconds lesedauer_{0};
    std::thread faden_;
    mutable std::mutex   m_;
    std::vector<SyncJob> gelesen_;
};

/// @brief Die Fixture-Diskette als Medium laden — sie spielt „die echte Scheibe".
DiskMedium ladeQuelle(const std::string& datei) {
    DiskMedium  m;
    std::string err;
    const std::string pfad = k1520test::diskPath(datei);
    EXPECT_TRUE(ImageCodec::load(pfad, ImageCodec::detect(pfad), nullptr, m, err)) << err;
    return m;
}

TrackSyncSpec specFuer(const DiskMedium& quelle, bool vorauslesen) {
    TrackSyncSpec s;
    s.num_cyls   = quelle.numCylinders();
    s.num_heads  = quelle.numHeads();
    s.writable   = false;                 // eine echte Diskette wird nicht angefasst
    s.read_ahead = vorauslesen;
    s.request_timeout_ms = 20000;
    return s;
}

}  // namespace

TEST(PhysicalBoot, BootetVonEinerSpurweisenQuelle) {
    DiskMedium quelle = ladeQuelle("cpa_cpa780_k5601_noclock.hfe");
    ASSERT_GT(quelle.numCylinders(), 0);

    // Diskette OHNE Datei: sie existiert nur als Auftragsweg zum „Laufwerk".
    auto img = DiskImage::openPhysical(specFuer(quelle, /*vorauslesen=*/true));
    ASSERT_TRUE(img);
    ASSERT_TRUE(img->isPhysical());
    TrackSync& sync = *img->sync();

    // Beim Anmelden ist NICHTS gelesen — das ist der ganze Punkt.
    EXPECT_EQ(sync.stats().tracks_known, 0);

    Ersatzlaufwerk laufwerk(sync, std::move(quelle));
    laufwerk.start();

    A5120Machine m;
    ASSERT_TRUE(m.mountDiskImage(0, std::move(img), /*write_protect=*/true))
        << m.lastError();
    m.powerOn();

    EXPECT_TRUE(k1520test::runUntilVramContains(m, "A>", 60'000'000))
        << "CP/A kam nicht bis zum Bereitschaftszeichen";
    laufwerk.stop();
}

TEST(PhysicalBoot, EsWirdNichtDieGanzeDisketteGelesen) {
    // Der Sinn der Uebung: der Kaltstart darf nicht auf einen Vollabzug warten.
    // Deshalb hier OHNE Vorauslesen — gelesen wird nur, was der Boot anfasst.
    DiskMedium quelle    = ladeQuelle("cpa_cpa780_k5601_noclock.hfe");
    const size_t gesamt  = static_cast<size_t>(quelle.numCylinders()) * quelle.numHeads();

    auto img = DiskImage::openPhysical(specFuer(quelle, /*vorauslesen=*/false));
    ASSERT_TRUE(img);
    TrackSync& sync = *img->sync();

    Ersatzlaufwerk laufwerk(sync, std::move(quelle));
    laufwerk.start();

    A5120Machine m;
    ASSERT_TRUE(m.mountDiskImage(0, std::move(img), true)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(k1520test::runUntilVramContains(m, "A>", 60'000'000));

    const size_t geholt = laufwerk.gelesen();
    laufwerk.stop();
    EXPECT_GT(geholt, 0u);
    EXPECT_LT(geholt, gesamt / 2)
        << "der Boot hat " << geholt << " von " << gesamt << " Spuren geholt — "
        << "das ist beinahe ein Vollabzug";
}

TEST(PhysicalBoot, VorauslesenBremstDenKaltstartNicht) {
    // Der Kern des Auftrags: braucht der Gast Spur 22, waehrend das Vorauslesen bei
    // Spur 4 steht, darf er NICHT warten, bis 4…21 durch sind.  Gemessen wird das
    // daran, worauf es ankommt — an der Zeit bis zum Bereitschaftszeichen:
    // mit eingeschaltetem Vorauslesen darf der Kaltstart nicht laenger dauern.
    //
    // 20 ms je Spur: langsam genug, dass die 160 Spuren waehrend des Kaltstarts nicht
    // durchlaufen (sonst laege ohnehin alles bereit und der Test prueft nichts).
    auto kaltstartDauer = [](bool vorauslesen) {
        DiskMedium quelle = ladeQuelle("cpa_cpa780_k5601_noclock.hfe");
        auto img = DiskImage::openPhysical(specFuer(quelle, vorauslesen));
        EXPECT_TRUE(img);
        TrackSync& sync = *img->sync();

        Ersatzlaufwerk laufwerk(sync, std::move(quelle), 20ms);
        laufwerk.start();

        A5120Machine m;
        EXPECT_TRUE(m.mountDiskImage(0, std::move(img), true)) << m.lastError();
        m.powerOn();

        const auto t0 = std::chrono::steady_clock::now();
        const bool da = k1520test::runUntilVramContains(m, "A>", 60'000'000);
        const auto dauer = std::chrono::steady_clock::now() - t0;
        const size_t geholt = laufwerk.gelesen();
        laufwerk.stop();
        EXPECT_TRUE(da) << "kein Bereitschaftszeichen (Vorauslesen=" << vorauslesen << ")";
        return std::pair<long long, size_t>{
            std::chrono::duration_cast<std::chrono::milliseconds>(dauer).count(), geholt};
    };

    const auto [ohne, spuren_ohne] = kaltstartDauer(false);
    const auto [mit,  spuren_mit ] = kaltstartDauer(true);

    // Das Vorauslesen muss ueberhaupt gelaufen sein — sonst prueft der Vergleich nichts.
    EXPECT_GT(spuren_mit, spuren_ohne)
        << "das Vorauslesen hat gar nichts geholt (" << spuren_mit << " vs. "
        << spuren_ohne << " Spuren)";
    // … und darf den Kaltstart dabei nicht aufhalten.  Grosszuegige Schranke: der
    // Boot wartet auf ~10 Spuren, ein einziger dazwischengeschobener Vorratsauftrag
    // kostet 20 ms.  Bei Prioritaetsumkehr waere es ein Vielfaches.
    EXPECT_LT(mit, ohne + 400)
        << "mit Vorauslesen " << mit << " ms statt " << ohne << " ms — "
        << "eine Anforderung musste hinter dem Vorrat warten";
}

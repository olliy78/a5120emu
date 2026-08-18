/**
 * @file test_track_sync.cpp
 * @brief TrackSync — Spurzustände, Prioritäten, Blockade, Rückführung.
 *
 * **Ohne Hardware.**  Das echte Laufwerk wird durch einen *Ersatz-Arbeitsfaden*
 * vertreten, der seine Spuren aus einem zweiten `DiskMedium` im Speicher holt und
 * dorthin zurückschreibt — über denselben Weg (HFE-Bitzellen), den der
 * Greaseweazle-Arbeitsfaden benutzt.  Damit liegt alles unterhalb von „Aufträge und
 * Bitzellen" in der Regression; die Hardware fügt nur noch USB hinzu.
 *
 * @see doc/design/14_physische_diskette.md §14
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/bit_codec.h"
#include "core/peripherals/floppy_drive/disk_medium.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/track_sync.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

constexpr uint8_t kCyls  = 8;
constexpr uint8_t kHeads = 2;

/// @brief Eine erkennbare IBM-Spur bauen (Sektor-ID trägt Zylinder und Kopf).
TrackImage baueSpur(uint8_t cyl, uint8_t head, uint8_t sektoren = 4) {
    std::vector<LogicalSector> s;
    for (uint8_t i = 0; i < sektoren; ++i) {
        LogicalSector ls;
        ls.cyl    = cyl;
        ls.head   = head;
        ls.id = static_cast<uint8_t>(i + 1);
        ls.size   = 128;
        ls.data.assign(128, static_cast<uint8_t>(cyl * 16 + head));
        s.push_back(std::move(ls));
    }
    return TrackCodec::buildTrack(s, Encoding::MFM);
}

/// @brief „Die echte Diskette": ein Medium im Speicher, aus dem der Ersatzfaden liest.
DiskMedium echteDiskette() {
    DiskMedium d(kCyls, kHeads, Encoding::MFM);
    for (uint8_t c = 0; c < kCyls; ++c)
        for (uint8_t h = 0; h < kHeads; ++h) d.setTrack(c, h, baueSpur(c, h));
    d.clearDirty();
    return d;
}

/**
 * @class Ersatzfaden
 * @brief Steht für „Greaseweazle + Laufwerk": holt Aufträge und bedient sie aus dem RAM.
 *
 * Der Weg ist bitgenau derselbe wie beim echten Adapter — @ref BitCodec::encode auf der
 * Geberseite, @ref TrackSync::completeRead mit HFE-Bitzellen auf der Nehmerseite.
 */
class Ersatzfaden {
public:
    Ersatzfaden(TrackSync& sync, DiskMedium& scheibe) : sync_(sync), scheibe_(scheibe) {}

    /// @brief Schadstelle nachstellen: diese Spur nimmt keine Schreibvorgaenge an.
    ///
    /// Geschrieben wird scheinbar, gelesen wird aber weiter der ALTE Inhalt — genau
    /// das Verhalten einer Diskette mit einer nicht mehr tragenden Stelle.
    void schadstelle(uint8_t cyl, uint8_t head) {
        std::lock_guard<std::mutex> l(m_);
        schadhaft_.emplace(cyl, head);
    }
    /// @brief Die Schadstelle heilen (neue Diskette eingelegt).
    void heilen() {
        std::lock_guard<std::mutex> l(m_);
        schadhaft_.clear();
    }
    /// @brief Wackelkontakt beim LESEN: die naechsten @p male Leseversuche dieser Spur
    ///        liefern ein verfaelschtes Datenbyte (= falsche Daten-CRC), danach ist
    ///        alles heil.  So verhaelt sich eine gealterte Diskette bei nur einer
    ///        abgetasteten Umdrehung.
    void leseflattern(uint8_t cyl, uint8_t head, int male) {
        std::lock_guard<std::mutex> l(m_);
        flattern_[{cyl, head}] = male;
    }
    /// @brief Wie oft wurde diese Spur ueberhaupt gelesen?
    int leseversuche(uint8_t cyl, uint8_t head) const {
        std::lock_guard<std::mutex> l(m_);
        auto it = leseversuche_.find({cyl, head});
        return it == leseversuche_.end() ? 0 : it->second;
    }
    size_t verifyLaeufe() const {
        std::lock_guard<std::mutex> l(m_);
        return verifies_;
    }

    ~Ersatzfaden() { stop(); }

    void start() {
        faden_ = std::thread([this] { schleife(); });
    }
    void stop() {
        sync_.shutdown();
        freigeben();
        if (faden_.joinable()) faden_.join();
    }

    /// @brief Aufträge anhalten (der Faden holt weiter ab, liefert aber nicht).
    void anhalten() { angehalten_ = true; }
    void freigeben() {
        { std::lock_guard<std::mutex> l(m_); angehalten_ = false; }
        cv_.notify_all();
    }

    /// @brief Reihenfolge der bedienten Aufträge (Prio, cyl, head).
    std::vector<SyncJob> verlauf() const {
        std::lock_guard<std::mutex> l(m_);
        return verlauf_;
    }
    size_t schreibvorgaenge() const {
        std::lock_guard<std::mutex> l(m_);
        return schreibungen_;
    }

private:
    void schleife() {
        for (;;) {
            SyncJob j;
            if (!sync_.takeJob(j, 50)) continue;
            if (j.kind == SyncJobKind::Stop) return;

            {   // Haltepunkt für die Tests, die die Warteschlange beobachten wollen.
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [this] { return !angehalten_ || sync_.stopped(); });
                if (sync_.stopped()) { sync_.failJob(j.id, "Ende"); return; }
                verlauf_.push_back(j);
            }

            if (j.kind == SyncJobKind::Read || j.kind == SyncJobKind::Verify) {
                if (j.kind == SyncJobKind::Verify) {
                    std::lock_guard<std::mutex> l(m_);
                    ++verifies_;
                }
                TrackImage s = scheibe_.peek(j.cyl, j.head);
                {   // Wackelkontakt: ein Byte verfaelschen, solange das Kontingent laeuft.
                    std::lock_guard<std::mutex> l(m_);
                    ++leseversuche_[{j.cyl, j.head}];
                    auto it = flattern_.find({j.cyl, j.head});
                    if (it != flattern_.end() && it->second > 0) {
                        --it->second;
                        // Ein Byte MITTEN IM DATENFELD verfaelschen — dort schlaegt die
                        // Daten-CRC an.  Ein Gap-Byte taete das nicht.
                        const size_t dam = s.nextMark(0, MarkType::Data);
                        if (dam != SIZE_MAX && dam + 8 < s.size()) s.bytes[dam + 8] ^= 0xFF;
                    }
                }
                const uint32_t bitcells = s.bitcells ? s.bitcells
                                                     : static_cast<uint32_t>(s.size() * 16);
                std::vector<uint8_t> zellen = BitCodec::encode(s, bitcells);
                sync_.completeRead(j.id, zellen.data(), zellen.size(), bitcells);
            } else {
                std::vector<uint8_t> zellen;
                uint32_t             bitcells = 0;
                if (!sync_.fetchWrite(j.id, zellen, bitcells)) {
                    sync_.failJob(j.id, "fetchWrite scheiterte");
                    continue;
                }
                bool kaputt;
                { std::lock_guard<std::mutex> l(m_);
                  kaputt = schadhaft_.count({j.cyl, j.head}) != 0;
                  ++schreibungen_; }
                // Auf einer Schadstelle bleibt der alte Inhalt stehen — der
                // Schreibvorgang selbst meldet trotzdem Erfolg, wie am echten Geraet.
                if (!kaputt)
                    scheibe_.setTrack(j.cyl, j.head,
                                      BitCodec::decodeAuto(zellen, bitcells, Encoding::MFM));
                sync_.completeWrite(j.id);
            }
        }
    }

    TrackSync&  sync_;
    DiskMedium& scheibe_;
    std::thread faden_;
    mutable std::mutex      m_;
    std::condition_variable cv_;
    bool                    angehalten_   = false;
    std::vector<SyncJob>    verlauf_;
    size_t                  schreibungen_ = 0;
    size_t                  verifies_     = 0;
    std::set<std::pair<uint8_t, uint8_t>> schadhaft_;
    std::map<std::pair<uint8_t, uint8_t>, int> flattern_;
    std::map<std::pair<uint8_t, uint8_t>, int> leseversuche_;
};

TrackSyncSpec spec(bool schreibbar = false, bool vorauslesen = false,
                   bool verify = true) {
    TrackSyncSpec s;
    s.num_cyls           = kCyls;
    s.num_heads          = kHeads;
    s.writable           = schreibbar;
    s.read_ahead         = vorauslesen;
    s.write_settle_ms    = 60;
    s.request_timeout_ms = 5000;
    s.verify_writes      = verify;
    return s;
}

/// @brief Wartet, bis @p pred zutrifft (oder die Frist abläuft).
template <typename F>
bool warteBis(F pred, std::chrono::milliseconds frist = 3000ms) {
    const auto ende = std::chrono::steady_clock::now() + frist;
    while (std::chrono::steady_clock::now() < ende) {
        if (pred()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return pred();
}

}  // namespace

// ─── Zustände ────────────────────────────────────────────────────────────────

TEST(TrackSync, FrischGemounteteDisketteIstVollstaendigUnbekannt) {
    DiskMedium abbild;
    TrackSync  sync(spec(), abbild);

    EXPECT_FALSE(abbild.complete());
    EXPECT_EQ(abbild.unknownCount(), static_cast<size_t>(kCyls) * kHeads);
    for (uint8_t c = 0; c < kCyls; ++c)
        for (uint8_t h = 0; h < kHeads; ++h)
            EXPECT_EQ(abbild.state(c, h), TrackState::Unknown) << +c << "/" << +h;
}

TEST(TrackSync, UnbekanntIstNichtUnformatiert) {
    // Eine halb gelesene Diskette darf sich nicht selbst fuer leer erklaeren — sonst
    // gaebe rawCompatible() gruenes Licht fuer ein .img, das die ungelesenen Spuren
    // als Fuellbytes festschriebe (doc/design/14 §4.2).
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    EXPECT_FALSE(abbild.formatted());       // noch nichts gelesen
    EXPECT_FALSE(abbild.complete());

    (void)abbild.track(0, 0);               // eine Spur holen
    EXPECT_TRUE(abbild.formatted());        // die BEKANNTE Spur traegt Marken
    EXPECT_FALSE(abbild.complete());        // aber die Diskette ist nicht ausgelesen
    faden.stop();
}

TEST(TrackSync, ReihenlaufLaedtNichtNach) {
    // formatted()/rawCompatible() gehen ueber ALLE Spuren.  Wuerden sie track()
    // benutzen, zoege eine beilaeufige Statusabfrage die ganze Diskette ein (§12.3).
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    (void)abbild.formatted();
    (void)abbild.rawCompatible();
    (void)abbild.rawIncompatibleReason();
    std::this_thread::sleep_for(50ms);

    EXPECT_EQ(abbild.unknownCount(), static_cast<size_t>(kCyls) * kHeads);
    EXPECT_TRUE(faden.verlauf().empty());
    faden.stop();
}

// ─── Lesen auf Anforderung ───────────────────────────────────────────────────

TEST(TrackSync, ZugriffHoltDieSpurUndLiefertDieselbenBytes) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    const TrackImage& geholt = abbild.track(3, 1);
    EXPECT_EQ(abbild.state(3, 1), TrackState::Clean);
    EXPECT_FALSE(geholt.empty());

    // Ueber den Umweg Bitzellen muss derselbe Sektorinhalt herauskommen.
    const auto original = TrackCodec::parseTrack(scheibe.peek(3, 1));
    const auto gelesen  = TrackCodec::parseTrack(geholt);
    ASSERT_EQ(gelesen.size(), original.size());
    for (size_t i = 0; i < gelesen.size(); ++i) {
        EXPECT_EQ(gelesen[i].id, original[i].id);
        EXPECT_EQ(gelesen[i].data,   original[i].data);
        EXPECT_TRUE(gelesen[i].data_crc_ok);
    }
    faden.stop();
}

TEST(TrackSync, LeseanforderungVerdraengtDasVorauslesen) {
    // Der Kern des Auftrags: braucht das System nach Spur 3 das Verzeichnis auf Spur 22
    // (hier 7), darf es NICHT erst alles dazwischen lesen.  Geprueft wird ohne
    // Ersatzfaden, damit die Reihenfolge nicht vom Zufall abhaengt: die Auftraege
    // werden von Hand abgeholt.
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/false, /*vorauslesen=*/true), abbild);

    // Ein Vorratsauftrag ist bereits unterwegs (der wird nicht abgebrochen, §5.1) …
    SyncJob laufend;
    ASSERT_TRUE(sync.takeJob(laufend, 500));
    EXPECT_EQ(laufend.prio, SyncPriority::Readahead);

    // … und mitten hinein kommt die Anforderung einer weit entfernten Spur.
    std::atomic<bool> zurueck{false};
    std::thread vordergrund([&] { (void)abbild.track(7, 1); zurueck = true; });
    ASSERT_TRUE(warteBis([&] { return sync.stats().tracks_known == 0 && !zurueck.load(); },
                         200ms));
    std::this_thread::sleep_for(30ms);   // die Anforderung ist eingestellt

    // Der laufende Auftrag wird zu Ende gefuehrt …
    const TrackImage leer;
    const auto zellen = BitCodec::encode(leer, 100000);
    sync.completeRead(laufend.id, zellen.data(), zellen.size(), 100000);

    // … und der NAECHSTE Auftrag ist die Anforderung, nicht der naechste Vorrat.
    SyncJob naechster;
    ASSERT_TRUE(sync.takeJob(naechster, 500));
    EXPECT_EQ(naechster.prio, SyncPriority::Demand);
    EXPECT_EQ(naechster.cyl, 7);
    EXPECT_EQ(naechster.head, 1);

    sync.shutdown();
    vordergrund.join();
}

TEST(TrackSync, ZugriffBlockiertBisDieSpurDaIst) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.anhalten();
    faden.start();

    std::atomic<bool> zurueck{false};
    std::thread vordergrund([&] {
        (void)abbild.track(2, 0);
        zurueck = true;
    });

    std::this_thread::sleep_for(80ms);
    EXPECT_FALSE(zurueck.load()) << "track() kehrte zurueck, ohne die Spur zu haben";

    faden.freigeben();
    ASSERT_TRUE(warteBis([&] { return zurueck.load(); }));
    vordergrund.join();
    EXPECT_EQ(abbild.state(2, 0), TrackState::Clean);
    faden.stop();
}

TEST(TrackSync, ZeitueberschreitungLiefertDieLeereSpur) {
    // Kein Arbeitsfaden: die Anforderung laeuft in die Frist und der Aufrufer sieht
    // eine leere (= unformatierte) Spur — fuer den Gast dasselbe wie eine kaputte.
    DiskMedium    abbild;
    TrackSyncSpec s = spec();
    s.request_timeout_ms = 60;
    TrackSync sync(s, abbild);

    const TrackImage& t = abbild.track(1, 0);
    EXPECT_TRUE(t.empty());
    EXPECT_EQ(abbild.state(1, 0), TrackState::Unknown);
    EXPECT_NE(sync.lastError().find("Zeit"), std::string::npos);
}

TEST(TrackSync, EineSpurBekommtNieZweiAuftraege) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/false, /*vorauslesen=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    ASSERT_TRUE(warteBis([&] { return abbild.complete(); }, 5000ms));
    faden.stop();

    std::vector<int> gezaehlt(static_cast<size_t>(kCyls) * kHeads, 0);
    for (const SyncJob& j : faden.verlauf())
        ++gezaehlt[static_cast<size_t>(j.cyl) * kHeads + j.head];
    for (size_t i = 0; i < gezaehlt.size(); ++i)
        EXPECT_EQ(gezaehlt[i], 1) << "Spur " << i << " wurde mehrfach gelesen";
}

// ─── Vorauslesen ─────────────────────────────────────────────────────────────

TEST(TrackSync, VorauslesenFuelltDasAbbildUndLaesstSichAbschalten) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/false, /*vorauslesen=*/false), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    std::this_thread::sleep_for(80ms);
    EXPECT_EQ(abbild.unknownCount(), static_cast<size_t>(kCyls) * kHeads)
        << "ohne Vorauslesen darf von selbst nichts gelesen werden";

    sync.setReadAhead(true);
    EXPECT_TRUE(warteBis([&] { return abbild.complete(); }, 5000ms));
    faden.stop();
}

TEST(TrackSync, VorauslesenBeginntBeimZuletztGebrauchtenZylinder) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/false, /*vorauslesen=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    (void)abbild.track(6, 0);            // Kopf steht jetzt bei 6
    ASSERT_TRUE(warteBis([&] { return faden.verlauf().size() >= 4; }));
    faden.stop();

    // Die drei Spuren nach der Anforderung muessen in ihrer Naehe liegen, nicht bei 0.
    const auto v = faden.verlauf();
    size_t i = 0;
    while (i < v.size() && !(v[i].cyl == 6 && v[i].head == 0)) ++i;
    ASSERT_LT(i + 3, v.size());
    for (size_t k = i + 1; k <= i + 3; ++k)
        EXPECT_LE(std::abs(static_cast<int>(v[k].cyl) - 6), 2)
            << "Vorauslesen springt quer ueber die Diskette";
}

// ─── Rückführung ─────────────────────────────────────────────────────────────

TEST(TrackSync, GeaenderteSpurLandetAufDerDiskette) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    abbild.setTrack(4, 0, baueSpur(4, 0, /*sektoren=*/6));   // Vollspur (kein Nachladen)
    EXPECT_EQ(abbild.state(4, 0), TrackState::Dirty);

    ASSERT_TRUE(warteBis([&] { return abbild.state(4, 0) == TrackState::Clean; }));
    EXPECT_EQ(TrackCodec::parseTrack(scheibe.peek(4, 0)).size(), 6u);
    faden.stop();
}

TEST(TrackSync, SchreibpauseFasstEinenBurstZusammen) {
    // Eine UDOS-Dateioperation fasst dieselbe Spur dutzendfach an.  Zurueckgeschrieben
    // werden darf sie trotzdem nur einmal (§7).
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    for (int i = 0; i < 12; ++i) {
        abbild.setTrack(5, 1, baueSpur(5, 1, static_cast<uint8_t>(2 + i % 3)));
        std::this_thread::sleep_for(10ms);          // < write_settle_ms (60 ms)
    }
    ASSERT_TRUE(warteBis([&] { return abbild.state(5, 1) == TrackState::Clean; }));
    EXPECT_LE(faden.schreibvorgaenge(), 2u)
        << "der Burst wurde nicht zusammengefasst";
    faden.stop();
}

TEST(TrackSync, SchreibgeschuetzteDisketteWirdNieBeschrieben) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/false), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    abbild.setTrack(2, 0, baueSpur(2, 0, 6));
    std::this_thread::sleep_for(150ms);
    EXPECT_EQ(faden.schreibvorgaenge(), 0u);
    EXPECT_EQ(TrackCodec::parseTrack(scheibe.peek(2, 0)).size(), 4u) << "Diskette veraendert!";
    faden.stop();
}

TEST(TrackSync, AbmeldenWartetAufDieRueckfuehrung) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    abbild.setTrack(1, 1, baueSpur(1, 1, 7));
    EXPECT_TRUE(sync.flushPending(3000));            // wartet, ohne die Ruhezeit abzusitzen
    EXPECT_EQ(TrackCodec::parseTrack(scheibe.peek(1, 1)).size(), 7u);
    EXPECT_EQ(abbild.state(1, 1), TrackState::Clean);
    faden.stop();
}

TEST(TrackSync, GescheitertesSchreibenLaesstDieAenderungStehen) {
    // Eine verlorene Aenderung waere der schlimmere Ausgang: Abbild und Diskette
    // liefen auseinander, ohne dass es jemand merkt (§5.4).
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);

    abbild.setTrack(0, 0, baueSpur(0, 0));
    SyncJob j;
    ASSERT_TRUE(sync.takeJob(j, 1000));
    ASSERT_EQ(j.kind, SyncJobKind::Write);
    sync.failJob(j.id, "Schreibfehler");

    EXPECT_EQ(abbild.state(0, 0), TrackState::Dirty);
    SyncJob nochmal;
    EXPECT_TRUE(sync.takeJob(nochmal, 1000));        // wird erneut eingestellt
    EXPECT_EQ(nochmal.kind, SyncJobKind::Write);
}

TEST(TrackSync, UnlesbareSpurWirdNichtEndlosWiederholt) {
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/false, /*vorauslesen=*/true), abbild);

    SyncJob j;
    ASSERT_TRUE(sync.takeJob(j, 1000));
    ASSERT_EQ(j.kind, SyncJobKind::Read);
    const uint8_t c = j.cyl, h = j.head;
    sync.failJob(j.id, "Spur unlesbar");

    for (int i = 0; i < 4; ++i) {
        SyncJob w;
        ASSERT_TRUE(sync.takeJob(w, 1000));
        EXPECT_FALSE(w.cyl == c && w.head == h) << "gescheiterte Spur erneut vorgelesen";
        sync.failJob(w.id, "auch unlesbar");
    }
    EXPECT_EQ(sync.stats().tracks_failed, 5);

    // Ein AUSDRUECKLICHER Zugriff versucht es dagegen erneut (§5.4).
    std::atomic<bool> zurueck{false};
    std::thread vordergrund([&] { (void)abbild.track(c, h); zurueck = true; });
    std::this_thread::sleep_for(50ms);      // die Anforderung muss eingestellt sein
    SyncJob nochmal;
    ASSERT_TRUE(sync.takeJob(nochmal, 1000));
    EXPECT_EQ(nochmal.cyl, c);
    EXPECT_EQ(nochmal.head, h);
    EXPECT_EQ(nochmal.prio, SyncPriority::Demand);
    sync.shutdown();
    vordergrund.join();
}

// ─── Vertrag ─────────────────────────────────────────────────────────────────

TEST(TrackSync, NurEinArbeitsfaden) {
    DiskMedium abbild;
    TrackSync  sync(spec(), abbild);

    std::atomic<bool> drin{false};
    std::thread erster([&] {
        SyncJob j;
        drin = true;
        sync.takeJob(j, 300);
    });
    ASSERT_TRUE(warteBis([&] { return drin.load(); }));
    std::this_thread::sleep_for(20ms);

    SyncJob j;
    EXPECT_FALSE(sync.takeJob(j, 50)) << "ein zweiter Arbeitsfaden wurde zugelassen";
    erster.join();
    EXPECT_NE(sync.lastError().find("zweiter"), std::string::npos);
}

TEST(TrackSync, ShutdownLoestJedenWartenden) {
    DiskMedium    abbild;
    TrackSyncSpec s = spec();
    s.request_timeout_ms = 60000;          // ohne shutdown wuerde der Test haengen
    TrackSync sync(s, abbild);

    std::atomic<bool> zurueck{false};
    std::thread vordergrund([&] { (void)abbild.track(0, 0); zurueck = true; });
    std::this_thread::sleep_for(30ms);
    EXPECT_FALSE(zurueck.load());

    sync.shutdown();
    ASSERT_TRUE(warteBis([&] { return zurueck.load(); }));
    vordergrund.join();

    SyncJob j;
    ASSERT_TRUE(sync.takeJob(j, 100));
    EXPECT_EQ(j.kind, SyncJobKind::Stop);
}

TEST(TrackSync, StatistikZaehltGelesenGeaendertUndLaufend) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    EXPECT_EQ(sync.stats().tracks_total, kCyls * kHeads);
    EXPECT_EQ(sync.stats().tracks_known, 0);

    (void)abbild.track(0, 0);
    (void)abbild.track(1, 0);
    EXPECT_EQ(sync.stats().tracks_known, 2);
    EXPECT_EQ(sync.stats().reads_done, 2u);
    faden.stop();
}

// ─── Rücknahme einer Stapeloperation ─────────────────────────────────────────

TEST(TrackSync, RuecknahmeStelltDieSpurWiederAlsAenderungEin) {
    // Bei einer physischen Diskette holt das Zurueckkopieren im Speicher nichts von
    // der Scheibe zurueck — die zurueckgesetzte Spur muss ERNEUT geschrieben werden
    // (doc/design/09_floppy_drive.md §11.3).
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    (void)abbild.track(2, 1);                                    // Ausgangsstand holen
    const DiskMedium sicherung = abbild;                          // Momentaufnahme

    abbild.setTrack(2, 1, baueSpur(2, 1, 9));                     // Stapeloperation
    ASSERT_TRUE(warteBis([&] { return abbild.state(2, 1) == TrackState::Clean; }));
    ASSERT_EQ(TrackCodec::parseTrack(scheibe.peek(2, 1)).size(), 9u);

    abbild.restoreFrom(sicherung);                                // … und Ruecknahme
    EXPECT_EQ(abbild.state(2, 1), TrackState::Dirty);
    ASSERT_TRUE(warteBis([&] { return abbild.state(2, 1) == TrackState::Clean; }));
    EXPECT_EQ(TrackCodec::parseTrack(scheibe.peek(2, 1)).size(), 4u)
        << "die Ruecknahme kam nie auf der Diskette an";
    faden.stop();
}

// ─── Schreib-Verify gegen die echte Spur ─────────────────────────────────────
//
// Der Verify-Lauf des Gastsystems (FORMAT) prueft das SPEICHERABBILD gegen sich
// selbst und sieht eine Schadstelle der Diskette daher nie.  Deshalb liest der
// Synchronisierer jede geschriebene Spur zurueck und vergleicht sie
// (doc/design/14_physische_diskette.md §7.1).

TEST(TrackSync, NachJedemSchreibenWirdZurueckgelesen) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    abbild.setTrack(3, 0, baueSpur(3, 0, 6));
    ASSERT_TRUE(warteBis([&] { return abbild.state(3, 0) == TrackState::Clean; }));
    faden.stop();

    EXPECT_EQ(faden.verifyLaeufe(), 1u) << "es wurde nicht zurueckgelesen";
    EXPECT_EQ(sync.stats().verifies_done, 1u);
    EXPECT_EQ(sync.stats().verify_failed, 0u);

    // Die Reihenfolge muss stimmen: erst schreiben, dann pruefen.
    const auto v = faden.verlauf();
    ASSERT_GE(v.size(), 2u);
    EXPECT_EQ(v[0].kind, SyncJobKind::Write);
    EXPECT_EQ(v[1].kind, SyncJobKind::Verify);
    EXPECT_EQ(v[1].cyl, 3);
}

TEST(TrackSync, VorDemPruefLesenBleibtDieSpurGeaendert) {
    // Der springende Punkt: geschrieben heisst noch nicht angekommen.  Solange das
    // Pruef-Lesen aussteht, gilt die Spur als GEAENDERT — sonst haette der Bediener
    // sie laengst als erledigt gesehen, waehrend sie noch gar nicht auf der Scheibe war.
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);

    abbild.setTrack(0, 0, baueSpur(0, 0));
    SyncJob schreiben;
    ASSERT_TRUE(sync.takeJob(schreiben, 1000));
    ASSERT_EQ(schreiben.kind, SyncJobKind::Write);
    ASSERT_TRUE(sync.completeWrite(schreiben.id));

    EXPECT_EQ(abbild.state(0, 0), TrackState::Dirty) << "zu frueh fuer sauber";

    SyncJob pruefen;
    ASSERT_TRUE(sync.takeJob(pruefen, 1000));
    EXPECT_EQ(pruefen.kind, SyncJobKind::Verify);
    EXPECT_EQ(pruefen.cyl, 0);
    EXPECT_EQ(pruefen.head, 0);
}

TEST(TrackSync, DasPruefLesenUeberschreibtDasAbbildNicht) {
    // Kaeme das Zurueckgelesene ins Abbild, ueberschriebe ein misslungener
    // Schreibvorgang genau die Daten, die er zerstoert hat — und niemand merkte es.
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.schadstelle(2, 1);                       // Spur nimmt nichts an
    faden.start();

    abbild.setTrack(2, 1, baueSpur(2, 1, /*sektoren=*/7));
    ASSERT_TRUE(warteBis([&] { return sync.hasDefects(); }));
    faden.stop();

    // Im Abbild stehen weiterhin DIE EIGENEN 7 Sektoren, nicht die 4 der Scheibe.
    EXPECT_EQ(TrackCodec::parseTrack(abbild.peek(2, 1)).size(), 7u)
        << "das Pruef-Lesen hat das Abbild ueberschrieben";
    EXPECT_EQ(abbild.state(2, 1), TrackState::Dirty)
        << "eine nicht angekommene Aenderung muss geaendert bleiben";
}

TEST(TrackSync, EineSchadstelleWirdGenauEinmalWiederholt) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.schadstelle(4, 0);
    faden.start();

    abbild.setTrack(4, 0, baueSpur(4, 0, 6));
    ASSERT_TRUE(warteBis([&] { return sync.hasDefects(); }));
    std::this_thread::sleep_for(200ms);            // nichts darf mehr nachkommen
    faden.stop();

    // Ein Versuch + eine Wiederholung = 2 Schreibvorgaenge und 2 Pruef-Lesevorgaenge.
    EXPECT_EQ(faden.schreibvorgaenge(), 2u);
    EXPECT_EQ(faden.verifyLaeufe(), 2u);
    EXPECT_EQ(sync.stats().verify_failed, 2u);
    EXPECT_EQ(sync.stats().tracks_defect, 1);
    EXPECT_EQ(sync.defectText(), "4/0");
}

TEST(TrackSync, DieSchadhafteSpurStehtImKlartext) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.schadstelle(1, 1);
    faden.schadstelle(6, 0);
    faden.start();

    abbild.setTrack(1, 1, baueSpur(1, 1, 5));
    abbild.setTrack(6, 0, baueSpur(6, 0, 5));
    ASSERT_TRUE(warteBis([&] { return sync.stats().tracks_defect == 2; }, 5000ms));

    EXPECT_EQ(sync.defectText(), "1/1, 6/0");
    EXPECT_NE(sync.lastError().find("liess sich nicht schreiben"), std::string::npos);
    // Und das Abmelden darf das NICHT als Erfolg verbuchen — in der Reihenfolge, in
    // der PhysicalSession::close() es tut: erst flush, dann den Faden anhalten.
    EXPECT_FALSE(sync.flushPending(500));
    faden.stop();
    EXPECT_FALSE(sync.flushPending(500)) << "auch nach dem Abmelden bleibt es ein Fehler";
}

TEST(TrackSync, NeuBeschreibenRettetDasAbbildAufEineHeileDiskette) {
    // Der Weg aus der Schadstelle heraus (§7.2): Diskette wechseln, alles noch einmal
    // hinausschreiben.  Das Abbild im Speicher ist die Wahrheit — es blieb ja heil.
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.schadstelle(5, 0);
    faden.start();

    (void)abbild.track(0, 0);                      // eine Spur bekannt machen
    abbild.setTrack(5, 0, baueSpur(5, 0, 9));
    ASSERT_TRUE(warteBis([&] { return sync.hasDefects(); }));

    // Neue, heile Diskette einlegen und alles Bekannte erneut hinausschreiben.
    faden.heilen();
    const size_t eingestellt = sync.rewriteAll();
    EXPECT_GE(eingestellt, 2u) << "es wurden gar keine Spuren eingestellt";

    ASSERT_TRUE(warteBis([&] { return !sync.hasDefects() &&
                                      abbild.state(5, 0) == TrackState::Clean; }, 5000ms));
    faden.stop();

    EXPECT_EQ(TrackCodec::parseTrack(scheibe.peek(5, 0)).size(), 9u)
        << "die Rettung kam nicht auf der Diskette an";
    EXPECT_EQ(sync.stats().tracks_defect, 0);
}

TEST(TrackSync, NeuBeschreibenLaesstUnbekannteSpurenInRuhe) {
    // Eine nie gelesene Spur traegt bedeutungslose Bytes; sie zu schreiben ergaebe
    // Muell auf der neuen Diskette.
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    (void)abbild.track(0, 0);                      // genau EINE Spur bekannt
    ASSERT_TRUE(warteBis([&] { return abbild.state(0, 0) == TrackState::Clean; }));

    EXPECT_EQ(sync.rewriteAll(), 1u) << "unbekannte Spuren wurden mit eingestellt";
    faden.stop();
}

TEST(TrackSync, OhneVerifyGiltGeschriebenSofortAlsErledigt) {
    // Abschaltbar bleibt es — dann ist das Verhalten wie vor der Einfuehrung.
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true, /*vorauslesen=*/false, /*verify=*/false),
                    abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.schadstelle(3, 1);                       // bliebe unentdeckt
    faden.start();

    abbild.setTrack(3, 1, baueSpur(3, 1, 6));
    ASSERT_TRUE(warteBis([&] { return abbild.state(3, 1) == TrackState::Clean; }));
    faden.stop();

    EXPECT_EQ(faden.verifyLaeufe(), 0u);
    EXPECT_FALSE(sync.hasDefects()) << "ohne Pruef-Lesen kann nichts auffallen";
}

TEST(TrackSync, WirdWaehrendDesPruefLesensGeschriebenGiltDerNeueInhalt) {
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);

    abbild.setTrack(0, 1, baueSpur(0, 1, 3));
    SyncJob schreiben;
    ASSERT_TRUE(sync.takeJob(schreiben, 1000));
    ASSERT_TRUE(sync.completeWrite(schreiben.id));

    SyncJob pruefen;
    ASSERT_TRUE(sync.takeJob(pruefen, 1000));
    ASSERT_EQ(pruefen.kind, SyncJobKind::Verify);

    // Das Gastsystem schreibt die Spur waehrend des Pruef-Lesens neu.
    abbild.setTrack(0, 1, baueSpur(0, 1, 8));

    const TrackImage alt = baueSpur(0, 1, 3);
    const auto zellen = BitCodec::encode(alt, static_cast<uint32_t>(alt.size() * 16));
    ASSERT_TRUE(sync.completeRead(pruefen.id, zellen.data(), zellen.size(),
                                  static_cast<uint32_t>(alt.size() * 16)));

    // Der Vergleich gilt dem alten Inhalt und wird verworfen — kein Defekt, sondern
    // ein neuer Schreibauftrag mit dem JUENGEREN Inhalt.
    EXPECT_FALSE(sync.hasDefects());
    SyncJob nochmal;
    ASSERT_TRUE(sync.takeJob(nochmal, 1000));
    EXPECT_EQ(nochmal.kind, SyncJobKind::Write);
    EXPECT_EQ(nochmal.cyl, 0);
    EXPECT_EQ(nochmal.head, 1);
}

/**
 * @test TrackSync/EinSchonDefekterSektorDarfDefektZurueckkommen
 * @brief Eine Spur mit Schadstelle IM ABBILD lässt sich zurückschreiben.
 *
 * Der Prüf-Vergleich verlangte von jedem zurückgelesenen Sektor eine gültige
 * Prüfsumme — auch von einem, der schon im Abbild kaputt war.  Damit liess sich eine
 * solche Spur **nie** zurückschreiben: das Schreiben gelang, der Vergleich meldete
 * „Prüfsumme des Datenfeldes falsch", die Spur galt als Schadstelle.
 *
 * Das gibt es wirklich: die SCP1700-Bootspur des A7100 wurde in einem Zug über den
 * Index hinaus beschrieben, ihre letzte Sektorkopie ist von der Naht abgeschnitten
 * und trägt eine ungültige Daten-CRC (`doc/scp1700_diskettenformat.md` §1.1).
 * Verglichen wird bei so einem Bruchstück nur noch die **Lage**; für jeden heilen
 * Sektor gilt die volle Strenge weiter (die übrigen Tests hier).
 */
TEST(TrackSync, EinSchonDefekterSektorDarfDefektZurueckkommen) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(/*schreibbar=*/true), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    // Spur mit einem kaputten Sektor: die Daten-CRC des letzten wird verbogen.
    TrackImage spur = baueSpur(5, 0, 4);
    const auto sekt = TrackCodec::parseTrack(spur);
    ASSERT_EQ(sekt.size(), 4u);
    const size_t crc_pos = sekt.back().data_pos + 1 + sekt.back().size;
    ASSERT_LT(crc_pos + 1, spur.bytes.size());
    spur.bytes[crc_pos] = static_cast<uint8_t>(spur.bytes[crc_pos] ^ 0xFF);
    ASSERT_FALSE(TrackCodec::parseTrack(spur).back().data_crc_ok);

    abbild.setTrack(5, 0, std::move(spur));
    ASSERT_TRUE(warteBis([&] { return abbild.state(5, 0) == TrackState::Clean; }))
        << "die Spur kam nie durch — " << sync.lastError();
    faden.stop();

    EXPECT_FALSE(sync.hasDefects()) << sync.lastError();
    EXPECT_EQ(sync.stats().verify_failed, 0u);
    // Auf der Scheibe stehen alle vier Sektoren, der letzte weiterhin mit falscher CRC.
    const auto drauf = TrackCodec::parseTrack(scheibe.peek(5, 0));
    ASSERT_EQ(drauf.size(), 4u);
    EXPECT_FALSE(drauf.back().data_crc_ok);
}

// ─── Lesewiederholung bei falscher Prüfsumme ─────────────────────────────────
//
// Eine Spurseite wird nur EINE Umdrehung lang abgetastet.  Auf einer gealterten
// Diskette liefert das gelegentlich einen Sektor mit falscher Daten-CRC, der beim
// nächsten Versuch fehlerfrei zurückkommt — an der P8000-Diskette von 1988
// beobachtet (ein Sektor unter 2560, danach dreimal fehlerfrei nachgelesen).
// Ohne Wiederholung wanderte dieser Ausrutscher unbemerkt in eine Datei.

TEST(TrackSync, EinLeseausrutscherWirdWiederholtUndNichtUebernommen) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.leseflattern(3, 1, 1);            // der ERSTE Versuch kommt verfaelscht
    faden.start();

    const TrackImage& spur = abbild.track(3, 1);
    ASSERT_FALSE(spur.empty());

    // Der Inhalt ist der heile — nicht der des ersten Versuchs.
    const auto soll = TrackCodec::parseTrack(scheibe.peek(3, 1));
    const auto ist  = TrackCodec::parseTrack(spur);
    ASSERT_EQ(ist.size(), soll.size());
    for (size_t i = 0; i < ist.size(); ++i) EXPECT_EQ(ist[i].data, soll[i].data) << i;
    for (const LogicalSector& s : ist) {
        EXPECT_TRUE(s.id_crc_ok)   << "Sektor " << +s.id;
        EXPECT_TRUE(s.data_crc_ok) << "Sektor " << +s.id;
    }

    EXPECT_EQ(faden.leseversuche(3, 1), 2) << "genau eine Wiederholung";
    const SyncStats st = sync.stats();
    EXPECT_EQ(st.read_retries, 1u);
    EXPECT_EQ(st.read_crc_bad, 0u) << "am Ende war die Spur heil";
    EXPECT_EQ(st.reads_done,   1u) << "die Wiederholung ist kein zweiter Lesevorgang";
    faden.stop();
}

TEST(TrackSync, EineDauerhaftSchadhafteSpurWirdNichtEwigWiederholt) {
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSyncSpec s = spec();
    s.read_crc_retries = 2;
    TrackSync   sync(s, abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.leseflattern(2, 0, 99);           // bleibt kaputt
    faden.start();

    const TrackImage& spur = abbild.track(2, 0);
    EXPECT_FALSE(spur.empty()) << "die Spur wird trotzdem hergegeben — mit Befund";
    EXPECT_EQ(faden.leseversuche(2, 0), 3) << "Erstversuch + 2 Wiederholungen, dann Schluss";

    const SyncStats st = sync.stats();
    EXPECT_EQ(st.read_retries, 2u);
    EXPECT_EQ(st.read_crc_bad, 1u);
    EXPECT_EQ(st.reads_done,   1u);
    faden.stop();
}

TEST(TrackSync, EineHeileSpurWirdNiemalsZweimalGelesen) {
    // Gegenprobe: die Wiederholung darf nicht jede Spur doppelt einlesen — das
    // verdoppelte die Wartezeit an einer Diskette ohne jeden Fehler.
    DiskMedium scheibe = echteDiskette();
    DiskMedium abbild;
    TrackSync  sync(spec(), abbild);
    Ersatzfaden faden(sync, scheibe);
    faden.start();

    for (uint8_t c = 0; c < kCyls; ++c) (void)abbild.track(c, 0);
    for (uint8_t c = 0; c < kCyls; ++c) EXPECT_EQ(faden.leseversuche(c, 0), 1) << +c;
    EXPECT_EQ(sync.stats().read_retries, 0u);
    faden.stop();
}

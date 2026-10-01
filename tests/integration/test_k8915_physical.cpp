/**
 * @file test_k8915_physical.cpp
 * @brief AP-E4k (doc/design/16_k8915.md §8a): der K8915 startet von einer **spurweise
 *        gelesenen** Diskette — Ersatzsitzung ohne Greaseweazle.
 *
 * Die Scheibe liegt nicht als Datei im Speicher, sondern kommt Spur für Spur über die
 * Auftragswarteschlange des @ref TrackSync herein; ein Ersatz-Arbeitsfaden bedient sie
 * aus `k8915scpx_boot1.hfe` und lässt sich je Spur eine künstliche Zeit (Uhrzeit, nicht
 * Maschinenzeit) — wie das echte Laufwerk (0,5–0,8 s je Spur).
 *
 * Die Frage, die dieser Satz beantwortet (Stolperstein des Entwurfs): Nachladen blockiert
 * im `/WAIT`-Betrieb den Lauffaden, also die einzige CPU.  Läuft währenddessen ein
 * Zeitgeber weiter (CTC der ZRE, K3, Frist ≈ 290 ms ⇒ Fehler `R`)?  Antwort: **nein** —
 * die Maschinenzeit ist ausschliesslich die Taktzahl von `run()`, nirgends in `core/`
 * steht eine Uhr.  Wächter dafür: `LangsamesLieferungenVerschiebenKeineMaschinenzeit`
 * (gleiche Taktzahl bis zum Prompt bei 0 und bei 320 ms je Spur — länger als die
 * 290-ms-Frist —, gleiche Auftragsfolge, kein `ERR`).
 *
 * | Fall | Inhalt |
 * |------|--------|
 * | `KaltstartBisZumPrompt` | SCPX-Kaltstart von der „physischen“ Scheibe |
 * | `KaltstartMitVorauslesenBisZumPrompt` | wie der Betrieb in der Oberfläche (Vorauslesen an) |
 * | `LangsameLieferungVerschiebtKeineMaschinenzeit` | Zeitgeber-Frage, s. o. |
 * | `SchreibenGehtMitZuruecklesenAufDieScheibe` | `save` → Write + Verify, Datei auf der Scheibe |
 * | `LaufwerksschlossHaeltDieSchreibzugriffeAuf` | Schreibschutz am Laufwerk ⇒ kein Auftrag |
 * | `SitzungsschlossHaeltDieScheibeUnberuehrt` | Sitzung nicht schreibend ⇒ Scheibe unverändert |
 */

#include <gtest/gtest.h>

#include "core/machines/k8915/k8915.h"
#include "core/peripherals/floppy_drive/bit_codec.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/image_codec.h"
#include "core/peripherals/floppy_drive/track_sync.h"

#include "tests/support/fixtures.h"
#include "tests/support/screen.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using k1520test::vramLines;
using k1520test::vramText;
using namespace std::chrono_literals;

namespace {

constexpr int kSchritt = 100'000;

/**
 * @class Ersatzlaufwerk
 * @brief „Greaseweazle + K5601“ aus einer `.hfe`-Fixture — lesend, schreibend, prüfend.
 *
 * Der Weg ist bitgenau der des echten Adapters (HFE-Bitzellen hinein/heraus); nur USB
 * fehlt.  @p dauer ist die künstliche Uhrzeit je Auftrag.
 */
class Ersatzlaufwerk {
public:
    Ersatzlaufwerk(TrackSync& sync, DiskMedium scheibe, std::chrono::milliseconds dauer)
        : sync_(sync), scheibe_(std::move(scheibe)), dauer_(dauer) {}
    ~Ersatzlaufwerk() { stop(); }

    void start() { faden_ = std::thread([this] { schleife(); }); }
    void stop() {
        sync_.shutdown();
        if (faden_.joinable()) faden_.join();
    }

    std::vector<SyncJob> verlauf() const {
        std::lock_guard<std::mutex> l(m_);
        return verlauf_;
    }
    size_t anzahl(SyncJobKind k) const {
        std::lock_guard<std::mutex> l(m_);
        return static_cast<size_t>(std::count_if(verlauf_.begin(), verlauf_.end(),
                                                 [k](const SyncJob& j) { return j.kind == k; }));
    }
    /// Nur nach stop() lesen (der Faden ist dann beendet).
    const DiskMedium& scheibe() const { return scheibe_; }

private:
    void schleife() {
        for (;;) {
            SyncJob j;
            if (!sync_.takeJob(j, 20)) continue;
            if (j.kind == SyncJobKind::Stop) return;
            { std::lock_guard<std::mutex> l(m_); verlauf_.push_back(j); }
            if (dauer_.count()) std::this_thread::sleep_for(dauer_);

            if (j.kind == SyncJobKind::Read || j.kind == SyncJobKind::Verify) {
                const TrackImage& s = scheibe_.peek(j.cyl, j.head);
                const uint32_t bitcells =
                    s.bitcells ? s.bitcells : static_cast<uint32_t>(s.size() * 16);
                const std::vector<uint8_t> zellen = BitCodec::encode(s, bitcells);
                sync_.completeRead(j.id, zellen.data(), zellen.size(), bitcells);
            } else {
                std::vector<uint8_t> zellen;
                uint32_t             bitcells = 0;
                if (!sync_.fetchWrite(j.id, zellen, bitcells)) {
                    sync_.failJob(j.id, "fetchWrite scheiterte");
                    continue;
                }
                scheibe_.setTrack(j.cyl, j.head,
                                  BitCodec::decodeAuto(zellen, bitcells, Encoding::MFM));
                sync_.completeWrite(j.id);
            }
        }
    }

    TrackSync&  sync_;
    DiskMedium  scheibe_;
    std::chrono::milliseconds dauer_;
    std::thread faden_;
    mutable std::mutex   m_;
    std::vector<SyncJob> verlauf_;
};

DiskMedium ladeScheibe() {
    DiskMedium  m;
    std::string err;
    const std::string pfad = k1520test::diskPath("k8915scpx_boot1.hfe");
    EXPECT_TRUE(ImageCodec::load(pfad, ImageCodec::detect(pfad), nullptr, m, err)) << err;
    return m;
}

/// Maschine + Ersatzlaufwerk + physisch eingelegte Diskette in A:.
struct Aufbau {
    std::unique_ptr<Ersatzlaufwerk> laufwerk;
    TrackSync*   sync = nullptr;
    K8915Machine m;

    /// @param sitzung_schreibt  darf der Arbeitsfaden auf die Scheibe schreiben?
    /// @param laufwerk_wp       Schreibschutz am Laufwerk (Haken im Laufwerkskasten)
    Aufbau(std::chrono::milliseconds dauer, bool sitzung_schreibt, bool laufwerk_wp,
           bool vorauslesen = false) {
        DiskMedium scheibe = ladeScheibe();
        TrackSyncSpec s;
        s.num_cyls   = scheibe.numCylinders();
        s.num_heads  = scheibe.numHeads();
        s.writable   = sitzung_schreibt;
        s.read_ahead = vorauslesen;
        s.write_settle_ms    = 50;
        s.request_timeout_ms = 20000;
        auto img = DiskImage::openPhysical(s);
        EXPECT_TRUE(img && img->isPhysical());
        sync = img->sync();
        EXPECT_EQ(sync->stats().tracks_known, 0);   // beim Anmelden ist NICHTS gelesen
        laufwerk = std::make_unique<Ersatzlaufwerk>(*sync, std::move(scheibe), dauer);
        laufwerk->start();
        EXPECT_TRUE(m.mountDiskImage(0, std::move(img), laufwerk_wp)) << m.lastError();
    }
    ~Aufbau() { laufwerk->stop(); }
};

bool enthaelt(K8915Machine& m, const std::string& s) {
    return vramText(m).find(s) != std::string::npos;
}

std::string letzteZeile(K8915Machine& m) {
    const std::string t = vramText(m);
    std::string letzte;
    for (size_t z = 0; z + 80 <= t.size(); z += 80) {
        std::string zeile = t.substr(z, 80);
        while (!zeile.empty() && (zeile.back() == ' ' || zeile.back() == '\0')) zeile.pop_back();
        if (!zeile.empty()) letzte = zeile;
    }
    return letzte;
}

/// Kaltstart bis zum Prompt hinter `rade`; liefert die verbrauchten Takte (-1 = nicht erreicht).
long long kaltstartBisPrompt(K8915Machine& m) {
    m.powerOn();
    long long takte = 0;
    bool coldstart = false;
    for (; takte < 60'000'000 && !coldstart; takte += m.run(kSchritt))
        coldstart = enthaelt(m, "* Coldstart *  Disk on A: ready");
    if (!coldstart) return -1;
    m.keyboard().sendeZeichen(0x0D);
    bool geladen = false;
    for (long long t = 0; t < 150'000'000 && !geladen; t += m.run(kSchritt), takte += kSchritt)
        geladen = enthaelt(m, "size: 1 kByte groups 0 ... 03FH");
    if (!geladen) return -1;
    for (long long t = 0; t < 20'000'000 && letzteZeile(m) != "A>"; t += m.run(kSchritt), takte += kSchritt) {}
    return letzteZeile(m) == "A>" ? takte : -1;
}

/// Kommando tippen und bis zum erneuten Prompt laufen (wie test_k8915_scpx.cpp).
bool befehl(K8915Machine& m, const std::string& cmd, long long frist = 60'000'000) {
    for (char c : cmd) m.keyboard().sendeZeichen(static_cast<uint8_t>(c));
    m.keyboard().sendeZeichen(0x0D);
    bool echo = false;
    for (long long t = 0; t < frist; t += m.run(kSchritt)) {
        const bool prompt = letzteZeile(m) == "A>";
        echo = echo || !prompt;
        if (echo && prompt && !m.keyboard().sendetNoch() && m.memReadDebug(0xF150) == 0)
            return true;
    }
    return false;
}

/// Steht @p text irgendwo auf der Scheibe (alle Spuren, Bytefolge)?
bool scheibeEnthaelt(const DiskMedium& d, const std::string& text) {
    for (int c = 0; c < d.numCylinders(); ++c)
        for (int h = 0; h < d.numHeads(); ++h) {
            const auto& b = d.peek(static_cast<uint8_t>(c), static_cast<uint8_t>(h)).bytes;
            if (std::search(b.begin(), b.end(), text.begin(), text.end()) != b.end()) return true;
        }
    return false;
}

constexpr const char* kEintrag = "TEST    COM";   // Verzeichniseintrag von `save 2 test.com`

}  // namespace

TEST(K8915Physical, KaltstartBisZumPrompt) {
    Aufbau x(0ms, /*sitzung_schreibt=*/false, /*laufwerk_wp=*/true);
    ASSERT_GT(kaltstartBisPrompt(x.m), 0) << vramLines(x.m);
    EXPECT_TRUE(enthaelt(x.m, "SCPX 8915  V 5.3  Anpassung:  V24  (XON/XOFF)")) << vramLines(x.m);
    EXPECT_TRUE(enthaelt(x.m, "A>rade"));
    EXPECT_FALSE(enthaelt(x.m, "ERR")) << vramLines(x.m);

    // Der Sinn der Übung: der Kaltstart holt nur, was er anfasst — nicht die ganze Scheibe.
    const size_t gesamt = static_cast<size_t>(x.sync->stats().tracks_total);
    const size_t geholt = x.laufwerk->anzahl(SyncJobKind::Read);
    EXPECT_GT(geholt, 0u);
    EXPECT_LT(geholt, gesamt / 2) << geholt << " von " << gesamt << " Spuren";
}

TEST(K8915Physical, KaltstartMitVorauslesenBisZumPrompt) {
    // Der Betrieb in der Oberflaeche: das Vorauslesen laeuft nebenher (andere Faeden,
    // andere Uhr) — der Kaltstart muss davon unberuehrt bleiben.
    Aufbau x(40ms, /*sitzung_schreibt=*/true, /*laufwerk_wp=*/true, /*vorauslesen=*/true);
    ASSERT_GT(kaltstartBisPrompt(x.m), 0) << vramLines(x.m);
    EXPECT_FALSE(enthaelt(x.m, "ERR")) << vramLines(x.m);
    EXPECT_EQ(x.laufwerk->anzahl(SyncJobKind::Write), 0u);
}

TEST(K8915Physical, LangsameLieferungVerschiebtKeineMaschinenzeit) {
    // 320 ms Uhrzeit je Spur: länger als die 290-ms-Frist des CTC-Zeitgebers (K3).
    // Liefe der Zeitgeber während des Nachladens weiter, käme `R`/ein anderer Verlauf.
    // Maschinenzeit = Taktzahl von run(); sie darf sich um nichts verschieben.
    Aufbau schnell(0ms, false, true);
    const long long t0 = kaltstartBisPrompt(schnell.m);
    ASSERT_GT(t0, 0) << vramLines(schnell.m);
    const auto verlauf0 = schnell.laufwerk->verlauf();
    const std::string bild0 = vramText(schnell.m);

    Aufbau langsam(320ms, false, true);
    const auto start = std::chrono::steady_clock::now();
    const long long t1 = kaltstartBisPrompt(langsam.m);
    const auto dauer = std::chrono::steady_clock::now() - start;
    ASSERT_GT(t1, 0) << vramLines(langsam.m);
    const auto verlauf1 = langsam.laufwerk->verlauf();

    // Die langsame Lieferung war wirklich langsam (sonst prüft der Vergleich nichts).
    EXPECT_GE(dauer, 320ms * (verlauf1.size() > 2 ? verlauf1.size() - 2 : 1) / 2);

    EXPECT_EQ(t1, t0) << "Maschinenzeit bis zum Prompt hängt an der Lieferzeit";
    ASSERT_EQ(verlauf1.size(), verlauf0.size());
    for (size_t i = 0; i < verlauf0.size(); ++i) {
        EXPECT_EQ(verlauf1[i].cyl, verlauf0[i].cyl) << i;
        EXPECT_EQ(verlauf1[i].head, verlauf0[i].head) << i;
    }
    EXPECT_EQ(vramText(langsam.m), bild0) << "anderes Bild bei langsamer Lieferung";
    EXPECT_FALSE(enthaelt(langsam.m, "ERR")) << vramLines(langsam.m);
}

TEST(K8915Physical, SchreibenGehtMitZuruecklesenAufDieScheibe) {
    Aufbau x(30ms, /*sitzung_schreibt=*/true, /*laufwerk_wp=*/false);
    ASSERT_GT(kaltstartBisPrompt(x.m), 0) << vramLines(x.m);
    ASSERT_EQ(x.laufwerk->anzahl(SyncJobKind::Write), 0u) << "Booten schreibt nichts";

    ASSERT_TRUE(befehl(x.m, "save 2 test.com")) << vramLines(x.m);
    ASSERT_TRUE(befehl(x.m, "dir")) << vramLines(x.m);
    EXPECT_TRUE(enthaelt(x.m, "TEST     COM")) << vramLines(x.m);

    ASSERT_TRUE(x.sync->flushPending(20000)) << x.sync->lastError();
    x.laufwerk->stop();

    // Zurückgeschrieben UND zurückgelesen: auf jeden Write folgt ein Verify derselben Spur.
    const auto v = x.laufwerk->verlauf();
    size_t writes = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i].kind != SyncJobKind::Write) continue;
        ++writes;
        bool verify = false;
        for (size_t k = i + 1; k < v.size() && !verify; ++k)
            verify = v[k].kind == SyncJobKind::Verify && v[k].cyl == v[i].cyl && v[k].head == v[i].head;
        EXPECT_TRUE(verify) << "Write c" << int(v[i].cyl) << "h" << int(v[i].head) << " ohne Prüf-Lesen";
    }
    EXPECT_GT(writes, 0u);
    EXPECT_TRUE(scheibeEnthaelt(x.laufwerk->scheibe(), kEintrag))
        << "TEST.COM steht nicht auf der Scheibe";
    EXPECT_EQ(x.sync->stats().tracks_dirty, 0);
}

TEST(K8915Physical, LaufwerksschlossHaeltDieSchreibzugriffeAuf) {
    // Sitzung darf schreiben, das Laufwerk (Haken „Write-Protect“) sperrt: die Maschine
    // sieht Schreibschutz, es entsteht gar keine geänderte Spur, der Adapter bekommt nichts.
    Aufbau x(0ms, /*sitzung_schreibt=*/true, /*laufwerk_wp=*/true);
    ASSERT_GT(kaltstartBisPrompt(x.m), 0) << vramLines(x.m);
    EXPECT_TRUE(x.m.isDiskWriteProtected(0));

    for (char c : std::string("save 2 test.com")) x.m.keyboard().sendeZeichen(static_cast<uint8_t>(c));
    x.m.keyboard().sendeZeichen(0x0D);
    for (long long t = 0; t < 40'000'000; t += x.m.run(kSchritt)) {}
    x.sync->flushPending(5000);
    x.laufwerk->stop();

    EXPECT_EQ(x.laufwerk->anzahl(SyncJobKind::Write), 0u);
    EXPECT_EQ(x.sync->stats().tracks_dirty, 0);
    EXPECT_FALSE(scheibeEnthaelt(x.laufwerk->scheibe(), kEintrag));
}

TEST(K8915Physical, SitzungsschlossHaeltDieScheibeUnberuehrt) {
    // Umgekehrt: das Laufwerk lässt die Maschine schreiben, die Sitzung nicht (Vorgabe im
    // DiskTool).  Die Maschine arbeitet im Speicher, zur Scheibe geht NICHTS.
    Aufbau x(0ms, /*sitzung_schreibt=*/false, /*laufwerk_wp=*/false);
    ASSERT_GT(kaltstartBisPrompt(x.m), 0) << vramLines(x.m);
    ASSERT_TRUE(befehl(x.m, "save 2 test.com")) << vramLines(x.m);
    x.sync->flushPending(2000);
    std::this_thread::sleep_for(200ms);   // länger als die Schreibpause (50 ms)
    x.laufwerk->stop();

    EXPECT_EQ(x.laufwerk->anzahl(SyncJobKind::Write), 0u);
    EXPECT_FALSE(scheibeEnthaelt(x.laufwerk->scheibe(), kEintrag));
}

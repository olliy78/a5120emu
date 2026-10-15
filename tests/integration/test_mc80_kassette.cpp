/**
 * @file test_mc80_kassette.cpp
 * @brief MC80 (AP M7): Kassette über den EGOS-Bandtreiber (1000H–1FFFH) an der K5020 (30H–37H).
 *
 * - `Mc80K5020.*`: die Karte am MC80-Bus — steckbar nach dem Anlegen (der Konstruktor fährt
 *   schon powerOn()), Tore 30H–37H, Taktquelle/Profil/Kassettenmaße je Modell, am .30 der
 *   Schreibtakt über den Koppelbus aus ASP-CTC K2.
 * - `Mc80Kassette.SpeichernUndLaden/*` (Abnahme je Modell): `KINIT 1` formatiert die Kassette,
 *   ein RAM-Programm mit Kennbyte 01 (Kommando „TST“ an 4000H, in die RAM-Kette gehängt) geht
 *   mit `WRITE 1,TST,01,4000,40FF` aufs Band, der RAM wird auf den Stand nach INIT
 *   zurückgesetzt, `READ 1,TST,R` holt es zurück („die RAM-Kettung wird auf die Anfangsadresse
 *   der Prozedur geführt“, [EGOS2 S.6]) — danach ist `TST` wieder ein Kommando und läuft.
 * - `Mc80KassettePraeambel.LiestAuchPraeambelABH`: EGOS nimmt als erstes Byte AAH oder ABH.
 *
 * Bedienung, Belege und Annahmen: doc/mc80/kassette.md.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "core/machines/mc80/mc80.h"
#include "core/peripherals/kassette/kassetten_medium.h"

using kassette::KassettenMedium;
using kassette::ProfilId;
using kassette::Seite;

namespace {

using Variante = Mc80Machine::Config::Variante;
constexpr double HZ = 2'457'600.0;

std::unique_ptr<Mc80Machine> maschine(Variante v) {
    Mc80Machine::Config c;
    c.variante = v;
    return std::make_unique<Mc80Machine>(c);
}

struct Mc80MitBand {
    std::unique_ptr<Mc80Machine> m;
    std::shared_ptr<KassettenMedium> band;
    std::vector<uint8_t> nachInit;   // 4000H–41FFH nach INIT

    explicit Mc80MitBand(Variante v) : m(maschine(v)) {
        EXPECT_TRUE(m->installK5020()) << m->k5020Fehler();
        if (!m->k5020()) return;
        // Eine neue Kassette, wie sie das Laufwerk am MC80 anlegt (Kompaktkassette ohne Marken).
        band = std::make_shared<KassettenMedium>(m->k5020()->laufwerk(0).neueKassette());
        EXPECT_TRUE(m->k5020()->laufwerk(0).kassetteEinlegen(band));
    }
    void fahre(uint64_t takte) {
        const uint64_t ziel = m->totalCycles() + takte;
        while (m->totalCycles() < ziel) m->run(5000);
    }
    void tippe(const char* text, uint64_t nach) {
        m->tastatur().tippe(text);
        while (!m->tastatur().leer()) m->run(5000);
        fahre(nach);
    }
    std::string text() const {
        std::string s;
        for (int z = 0; z < 25; ++z) {
            const std::string t = m->textZeile(z);
            if (!t.empty()) s += std::to_string(z) + ": " + t + "\n";
        }
        return s;
    }
    /// Kommando eingeben und fahren, bis EGOS fertig ist: die Eingabezeile 24 zeigt wieder nur
    /// den Prompt `>` (während der Ausführung steht dort das Kommando).  Rückgabe: Sekunden.
    double kommando(const char* kdo, uint64_t max = 800'000'000) {
        tippe(kdo, 0);
        tippe("\n", 0);
        const uint64_t start = m->totalCycles();
        while (m->totalCycles() - start < max) {
            fahre(500'000);
            if (m->textZeile(24) == ">") break;
        }
        return static_cast<double>(m->totalCycles() - start) / HZ;
    }
    /// Kaltstart, INIT (legt RAM-Kette und Directory an), Stand 4000H ff. merken.
    void boot() {
        fahre(4'000'000);
        tippe("init", 0);
        tippe("\n", 4'000'000);
        for (uint16_t a = 0x4000; a < 0x4200; ++a) nachInit.push_back(m->memReadDebug(a));
    }
    void ramWieNachInit() {
        for (uint16_t a = 0x4000; a < 0x4200; ++a) m->memWriteDebug(a, nachInit[a - 0x4000]);
    }
};

/// Das Testprogramm: Kettenglied „TST“ an 4000H (Kennbyte 01, Abstand weiter zum Directory bei
/// BAF8H wie das leere Glied `00 F5 7A FF`, das INIT dort anlegt), Code `LD A,5AH /
/// LD (4100H),A / RET`, danach ein Füllmuster mit einzelnen AAH bis 40FFH.
std::vector<uint8_t> programm() {
    std::vector<uint8_t> p = {0x01, 0xF5, 0x7A, 'T', 'S', 'T', 0xFF,
                              0x3E, 0x5A, 0x32, 0x00, 0x41, 0xC9};
    while (p.size() < 0x100) {
        const size_t i = p.size();
        // Einzelne AAH, keine zwei hintereinander: EGOS braucht für ein gelesenes AAH ~2 ms
        // (Postambelprobe 1C3A mit DI) — bei 6 000 Bit/s (.31) reicht das für EIN AAH, bei zwei
        // in Folge geht das übernächste Byte verloren (doc/mc80/kassette.md §2, Grenze des Treibers).
        p.push_back(i % 7 == 0 ? 0xAA : static_cast<uint8_t>(i * 37));
    }
    return p;
}

/// Boot, KINIT, Programm schreiben, RAM zurücksetzen.  Prüft das Band nach KINIT und WRITE.
void speichern(Mc80MitBand& r, const std::vector<uint8_t>& prog) {
    ASSERT_NE(r.m->k5020(), nullptr);
    r.boot();
    ASSERT_EQ(r.nachInit[0], 0x00);
    ASSERT_EQ(0x4003 + (r.nachInit[1] | r.nachInit[2] << 8), 0xBAF8);
    ASSERT_EQ(r.m->memReadDebug(0xBAF8), 0x04) << "Directory (Kennbyte 04) nach INIT";
    for (size_t i = 0; i < prog.size(); ++i) r.m->memWriteDebug(static_cast<uint16_t>(0x4000 + i), prog[i]);

    // FORMAT (im ROM KINIT): Bandmarke, Directory-Block 89H 'DIRECTORY', 14 leere
    // Verzeichnisblöcke, Bandmarke [P 111C].
    const double t_kinit = r.kommando("kinit 1");
    printf("[M7] KINIT %.1f s\n", t_kinit);
    ASSERT_EQ(r.m->textZeile(23), "OK") << r.text();
    ASSERT_EQ(r.band->anzahlBloecke(Seite::A), 17u) << r.text();
    EXPECT_TRUE(r.band->block(Seite::A, 0).bandmarke());
    EXPECT_EQ(r.band->block(Seite::A, 1).bytes.size(), 132u);
    EXPECT_EQ(r.band->block(Seite::A, 1).bytes[1], 0x89);
    EXPECT_TRUE(r.band->block(Seite::A, 16).bandmarke());

    const double t_write = r.kommando("write 1,tst,01,4000,40ff");
    printf("[M7] WRITE %.1f s, %zu Bloecke\n", t_write, r.band->anzahlBloecke(Seite::A));
    ASSERT_EQ(r.m->textZeile(23), "OK") << r.text();
    // Hinter dem Dateikopf (128 B) steht 4000H–40FFH als ein 256-Byte-Block.
    bool gefunden = false;
    for (size_t i = 17; i < r.band->anzahlBloecke(Seite::A); ++i) {
        const auto& b = r.band->block(Seite::A, i).bytes;
        if (b.size() == 260 && std::vector<uint8_t>(b.begin() + 1, b.begin() + 257) == prog) gefunden = true;
    }
    EXPECT_TRUE(gefunden) << "Programm nicht als 256-Byte-Block auf dem Band";

    // Stand nach INIT: „TST“ ist weg, die Kette läuft wieder direkt zum Directory.
    r.ramWieNachInit();
}

void ladenUndStarten(Mc80MitBand& r, const std::vector<uint8_t>& prog) {
    const double t_read = r.kommando("read 1,tst,r");
    printf("[M7] READ %.1f s\n", t_read);
    ASSERT_EQ(r.m->textZeile(23), "OK") << r.text();
    for (size_t i = 0; i < prog.size(); ++i)
        ASSERT_EQ(r.m->memReadDebug(static_cast<uint16_t>(0x4000 + i)), prog[i])
            << "Adresse " << std::hex << (0x4000 + i) << "\n" << r.text();
    // Das gelesene Programm ist wieder ein Kommando der RAM-Kette.
    EXPECT_NE(r.m->memReadDebug(0x4100), 0x5A);
    r.kommando("tst", 20'000'000);
    EXPECT_EQ(r.m->memReadDebug(0x4100), 0x5A) << r.text();
}

// ─── Karte am MC80-Bus ───────────────────────────────────────────────────────

TEST(Mc80K5020, SteckbarNachDemAnlegenNichtNachDemErstenLauf) {
    auto m = maschine(Variante::Mc8031);
    ASSERT_TRUE(m->installK5020()) << m->k5020Fehler();
    for (int p = 0x30; p <= 0x37; ++p) {
        const BusDevice* d = m->bus().ioOwner(static_cast<uint8_t>(p));
        ASSERT_NE(d, nullptr) << std::hex << p;
        EXPECT_EQ(std::string(d->deviceName()).rfind("K5020", 0), 0u) << d->deviceName();
    }
    EXPECT_FALSE(m->installK5020()) << "doppelt";

    auto n = maschine(Variante::Mc8030);
    n->run(1000);
    EXPECT_FALSE(n->installK5020());
    EXPECT_NE(n->k5020Fehler().find("vor dem ersten Lauf"), std::string::npos) << n->k5020Fehler();
}

TEST(Mc80K5020, ModelleUnterscheidenTaktProfilUndKassette) {
    auto m30 = maschine(Variante::Mc8030);
    auto m31 = maschine(Variante::Mc8031);
    ASSERT_TRUE(m30->installK5020());
    ASSERT_TRUE(m31->installK5020());
    const K5020& k30 = *m30->k5020();
    const K5020& k31 = *m31->k5020();
    EXPECT_EQ(k30.config().basis, 0x30);
    EXPECT_EQ(k31.config().basis, 0x30);
    EXPECT_EQ(k30.config().takt, K5020::Takt::ExternerCtc);
    EXPECT_EQ(k31.config().takt, K5020::Takt::Bustakt);
    EXPECT_EQ(k30.laufwerk(0).profil().id, ProfilId::LW1200);
    EXPECT_EQ(k31.laufwerk(0).profil().id, ProfilId::K5200_19);
    for (const K5020* k : {&k30, &k31}) {
        const auto& g = k->laufwerk(0).neueKassette();
        EXPECT_EQ(g, kassette::Geometrie::kompaktkassette());
        EXPECT_EQ(g.marke_um, 0u) << "keine BOT-/EOT-Marke";
        KassettenMedium neu(g);
        EXPECT_FALSE(neu.aebAktiv(neu.botEnde() + 1000)) << "hinter dem Vorspann kein AEB";
    }
}

TEST(Mc80K5020, Mc8030SchreibtaktKommtVonAspCtcKanal2) {
    auto m = maschine(Variante::Mc8030);
    ASSERT_TRUE(m->installK5020());
    EXPECT_FALSE(m->aspBandtakt()) << "nach dem Einschalten zählt K2 nicht";
    EXPECT_FALSE(m->k5020()->taktLiegtAn());
    m->bus().ioWrite(0xD6, 0x47);   // EGOS MB_INIT 1F96: Zähler, Zeitkonstante folgt
    EXPECT_FALSE(m->k5020()->taktLiegtAn()) << "erst mit der Zeitkonstante";
    m->bus().ioWrite(0xD6, 0x02);
    EXPECT_TRUE(m->aspBandtakt());
    EXPECT_TRUE(m->k5020()->taktLiegtAn());
    m->bus().ioWrite(0xD6, 0x03);   // Kanalreset
    EXPECT_FALSE(m->k5020()->taktLiegtAn());
    m->bus().ioWrite(0xD6, 0xA7);   // Zeitgeber statt Zähler: kein Bandtakt
    m->bus().ioWrite(0xD6, 0x10);
    EXPECT_FALSE(m->k5020()->taktLiegtAn());

    auto m31 = maschine(Variante::Mc8031);
    ASSERT_TRUE(m31->installK5020());
    EXPECT_TRUE(m31->k5020()->taktLiegtAn()) << ".31: Bustakt, ohne ASP";
}

TEST(Mc80K5020, EgosStelltDieKarteBeimStartEin) {
    auto m = maschine(Variante::Mc8030);
    ASSERT_TRUE(m->installK5020());
    while (m->totalCycles() < 4'000'000) m->run(5000);
    // MB_INIT (1F8D): Vektoren B0H/B4H (Daten-PIO A/B), B8H/B6H (Steuer-PIO A/B), ASP-CTC K2 zählt.
    EXPECT_EQ(m->k5020()->pioDaten().debugState().port[0].vector, 0xB0);
    EXPECT_EQ(m->k5020()->pioDaten().debugState().port[1].vector, 0xB4);
    EXPECT_EQ(m->k5020()->pioSteuer().debugState().port[0].vector, 0xB8);
    EXPECT_EQ(m->k5020()->pioSteuer().debugState().port[1].vector, 0xB6);
    EXPECT_TRUE(m->k5020()->taktLiegtAn());
}

// ─── Abnahme über EGOS ───────────────────────────────────────────────────────

class Mc80Kassette : public ::testing::TestWithParam<Variante> {};

TEST_P(Mc80Kassette, SpeichernUndLaden) {
    Mc80MitBand r(GetParam());
    const auto prog = programm();
    speichern(r, prog);
    if (HasFatalFailure()) return;
    ladenUndStarten(r, prog);
}

INSTANTIATE_TEST_SUITE_P(Modelle, Mc80Kassette, ::testing::Values(Variante::Mc8030, Variante::Mc8031),
                         [](const ::testing::TestParamInfo<Variante>& i) {
                             return std::string(i.param == Variante::Mc8030 ? "Mc8030" : "Mc8031");
                         });

// EGOS 1BF0–1C03: ein erstes Byte AAH läuft durch die Postambelprobe (BMS noch 1 → Datenbyte),
// ABH wird ohne sie angenommen, anderes verworfen.  Die Karte reicht Präambeln unverändert durch —
// ein Band, dessen Blöcke mit ABH beginnen, muss sich genauso laden lassen.
TEST(Mc80KassettePraeambel, LiestAuchPraeambelABH) {
    Mc80MitBand r(Variante::Mc8031);
    const auto prog = programm();
    speichern(r, prog);
    if (::testing::Test::HasFatalFailure()) return;
    size_t umgestellt = 0;
    for (size_t i = 0; i < r.band->anzahlBloecke(Seite::A); ++i) {
        auto bytes = r.band->block(Seite::A, i).bytes;
        if (bytes.size() < 5 || bytes[0] != 0xAA) continue;   // Bandmarken bleiben (AA 00 00 AA)
        bytes[0] = 0xAB;
        ASSERT_TRUE(r.band->schreibeBlock(Seite::A, r.band->blockStart(Seite::A, i), bytes));
        ++umgestellt;
    }
    ASSERT_GT(umgestellt, 20u);
    ladenUndStarten(r, prog);
}

}  // namespace

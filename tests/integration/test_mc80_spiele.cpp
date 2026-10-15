/**
 * @file test_mc80_spiele.cpp
 * @brief MC80 (AP M5): die Speicherabzüge doc/mc80/spiele/ werden wie mit dem Debugger-Befehl
 *        `load` bzw. k1520_mem_load (= memWriteDebug Byte für Byte) in den RAM geladen,
 *        über EGOS' Kommandokette gestartet und müssen laufen: Programmzähler im Spiel,
 *        Bild ändert sich.
 *
 * Kettung (egos.md §1.3): jeder Eintrag ist `Kennbyte, Abstand(2), Name, FFH, Code`; der
 * Abstand zeigt (ab Byte 3) auf den nächsten Eintrag.  PHARAO/WALL werden mit `RKET 01,4000,adr`
 * an 4000H verkettet; der Test legt dasselbe Glied (`01 <adr-4003H> FF`, leerer Name) selbst ab.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/machines/mc80/mc80.h"

#ifndef MC80_SPIELE_DIR
#error "MC80_SPIELE_DIR fehlt"
#endif

namespace {

struct Mc80Rechner {
    std::unique_ptr<Mc80Machine> m;
    Mc80Rechner() {
        Mc80Machine::Config c;
        c.variante = Mc80Machine::Config::Variante::Mc8030;
        m = std::make_unique<Mc80Machine>(c);
    }
    void fahre(uint64_t takte) {
        const uint64_t ziel = m->totalCycles() + takte;
        while (m->totalCycles() < ziel) m->run(5000);
    }
    void tippe(const char* text, uint64_t nach = 2'000'000) {
        m->tastatur().tippe(text);
        while (!m->tastatur().leer()) m->run(5000);
        fahre(nach);
    }
    /// Wie der Debugger-Befehl `load <datei> <adr>` (k1520_mem_load): Byte für Byte durch den
    /// Maschinen-Speicherpfad im gerade sichtbaren Segment.
    size_t lade(const std::string& datei, uint16_t adr) {
        std::ifstream f(std::string(MC80_SPIELE_DIR) + "/" + datei, std::ios::binary);
        EXPECT_TRUE(f.good()) << datei;
        const std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        for (size_t i = 0; i < d.size(); ++i)
            m->memWriteDebug(static_cast<uint16_t>(adr + i), static_cast<uint8_t>(d[i]));
        return d.size();
    }
    void poke(uint16_t a, std::initializer_list<uint8_t> b) {
        for (uint8_t v : b) m->memWriteDebug(a++, v);
    }
    /// Kaltstart bis zur Kommandoschleife, dann INIT (legt die RAM-Kette an).
    void boot() {
        fahre(4'000'000);
        tippe("init");
        tippe("\n", 4'000'000);
    }
    std::vector<uint8_t> bild() const {
        const uint8_t* fb = m->framebuffer();
        return {fb, fb + static_cast<size_t>(m->fbWidth()) * m->fbHeight()};
    }
    std::string text() const {
        std::string s;
        for (int z = 0; z < 25; ++z) s += std::to_string(z) + ": " + m->textZeile(z) + "\n";
        return s;
    }
};

size_t unterschied(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) n += a[i] != b[i];
    return n;
}

struct Lauf {
    int imSpiel = 0, imRom = 0, proben = 0;
    size_t bildNachStart = 0;    // Pixel anders als vor dem Kommando
    size_t bildSpaeter = 0;      // Pixel anders zwischen Mitte und Ende des Laufs
    uint16_t pcEnde = 0;
};

/// Kommando eingeben, Eingabe auslösen, dann einzeln fahren und den Programmzähler
/// einsammeln.  Das Bild wird vor dem Kommando, in der Mitte und am Ende verglichen.
Lauf starte(Mc80Rechner& r, const char* kdo, uint16_t von, uint16_t bis, int befehle = 600'000)
{
    const auto vorher = r.bild();
    r.tippe(kdo, 500'000);
    r.m->tastatur().tippe("\n");
    while (!r.m->tastatur().leer()) r.m->run(5000);
    Lauf l;
    std::vector<uint8_t> mitte;
    for (int i = 0; i < befehle; ++i) {
        r.m->run(1);
        const uint16_t pc = r.m->cpuPC();
        ++l.proben;
        if (pc >= von && pc < bis) ++l.imSpiel;
        else if (pc < 0x2000)     ++l.imRom;
        if (i == befehle / 2) mitte = r.bild();
    }
    l.pcEnde = r.m->cpuPC();
    const auto ende = r.bild();
    l.bildNachStart = unterschied(vorher, ende);
    l.bildSpaeter = unterschied(mitte, ende);
    return l;
}

/// Gemeinsame Abnahme: der Programmzähler ist nennenswert im Spiel (also keine dauerhafte
/// ROM-Warteschleife nach dem Start) und das Bild hat sich gegenüber dem Zustand vor dem
/// Kommando geändert.
void erwarteLaeuft(const Lauf& l, const Mc80Rechner& r)
{
    ::testing::Test::RecordProperty("imSpiel", l.imSpiel);
    ::testing::Test::RecordProperty("imRom", l.imRom);
    ::testing::Test::RecordProperty("bildNachStart", static_cast<int>(l.bildNachStart));
    ::testing::Test::RecordProperty("bildSpaeter", static_cast<int>(l.bildSpaeter));
    printf("[Spiel] imSpiel=%d imRom=%d von=%d pcEnde=%04X bildNachStart=%zu bildSpaeter=%zu\n",
           l.imSpiel, l.imRom, l.proben, l.pcEnde, l.bildNachStart, l.bildSpaeter);
    EXPECT_GT(l.imSpiel, l.proben / 10)
        << "Programmzähler kaum im Spiel (imSpiel=" << l.imSpiel << " imRom=" << l.imRom
        << " pcEnde=" << std::hex << l.pcEnde << ")\n" << r.text();
    EXPECT_GT(l.bildNachStart, 500u) << "Bild unverändert\n" << r.text();
    EXPECT_GT(l.bildSpaeter, 0u) << "Bild steht (Spiel lebt nicht weiter)\n" << r.text();
}

TEST(Mc80Spiele, PharaoLaeuftAus6400H) {
    Mc80Rechner r;
    r.boot();
    ASSERT_EQ(r.lade("pharao_6400-94e0.bin", 0x6400), 12513u);
    r.poke(0x4000, {0x01, 0xFD, 0x23, 0xFF});    // RKET 01,4000,6400
    const Lauf l = starte(r, "z", 0x6400, 0x94E1);
    erwarteLaeuft(l, r);
}

TEST(Mc80Spiele, WallLaeuftAus8000H) {
    Mc80Rechner r;
    r.boot();
    ASSERT_EQ(r.lade("wall_8000-93ff.bin", 0x8000), 5120u);
    r.poke(0x4000, {0x01, 0xFD, 0x3F, 0xFF});    // RKET 01,4000,8000
    const Lauf l = starte(r, "z", 0x8000, 0x9400);
    erwarteLaeuft(l, r);
}

TEST(Mc80Spiele, TetrisLaeuftAus4000H) {
    Mc80Rechner r;
    r.boot();
    ASSERT_EQ(r.lade("tetris_4000_4fff.bin", 0x4000), 4096u);
    const Lauf l = starte(r, "te", 0x4000, 0x5000);
    erwarteLaeuft(l, r);
}

TEST(Mc80Spiele, HasiLaeuftAus4000H) {
    Mc80Rechner r;
    r.boot();
    ASSERT_EQ(r.lade("hasi_4000_5fff.bin", 0x4000), 8192u);
    const Lauf l = starte(r, "hasi", 0x4000, 0x6000);
    erwarteLaeuft(l, r);
}

}  // namespace

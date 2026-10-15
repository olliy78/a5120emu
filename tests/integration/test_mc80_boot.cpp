/**
 * @file test_mc80_boot.cpp
 * @brief MC80.3x (AP M3): EGOS meldet sich auf der VIS 2a und nimmt Kommandos über die
 *        Tastatur (Zeichenebene an SIO A der ASP) an.  Der Text wird per Mustervergleich
 *        gegen den EGOS-Zeichengenerator aus dem Bild zurückgewonnen (egos_text.h).
 *        Erwartungen: doc/mc80/egos.md §1 (Einschaltmeldung, „OK“, „>“), egos.prn.
 */

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "core/machines/mc80/mc80.h"

namespace {

std::string bildText(const Mc80Machine& m)
{
    std::string s;
    for (int z = 0; z < 25; ++z) s += std::to_string(z) + ": " + m.textZeile(z) + "\n";
    return s;
}

int findeZeile(const Mc80Machine& m, const std::string& teil)
{
    for (int z = 0; z < 25; ++z)
        if (m.textZeile(z).find(teil) != std::string::npos) return z;
    return -1;
}

struct Egos {
    std::unique_ptr<Mc80Machine> m = std::make_unique<Mc80Machine>();
    void fahre(uint64_t takte) {
        const uint64_t ziel = m->totalCycles() + takte;
        while (m->totalCycles() < ziel) m->run(5000);
    }
    /// Bis die Tastatur alles abgeliefert hat, dann noch @p nach Takte.
    void tippe(const char* text, uint64_t nach = 2'000'000) {
        m->tastatur().tippe(text);
        while (!m->tastatur().leer()) m->run(5000);
        fahre(nach);
    }
};

TEST(Mc80Boot, EgosMeldetSich) {
    Egos e;
    e.fahre(4'000'000);   // ~3,7 Mio. Takte bis zur Kommandoschleife (M2)
    const auto& m = *e.m;
    EXPECT_GE(findeZeile(m, "mc 80.30. microcomputer"), 0) << bildText(m);
    EXPECT_GE(findeZeile(m, "Version 3.0/1,2"), 0) << bildText(m);
    EXPECT_NE(m.textZeile(23).find("OK"), std::string::npos) << bildText(m);
    EXPECT_EQ(m.textZeile(24).rfind(">", 0), 0u) << bildText(m);
    // dasselbe über die C-ABI-Sicht screenChar
    EXPECT_EQ(m.screenChar(0, 24), '>');
    EXPECT_EQ(m.screenChar(79, 24), ' ');
    EXPECT_EQ(m.screenChar(80, 24), 0);
    EXPECT_EQ(m.screenChar(0, 25), 0);
    // Bild: etwas ist hell, die Anzeige hat sich geändert
    EXPECT_TRUE(m.fbDirty());
    int hell = 0;
    for (int i = 0; i < m.fbWidth() * m.fbHeight(); ++i) hell += m.framebuffer()[i] != 0;
    EXPECT_GT(hell, 200);
}

TEST(Mc80Boot, KommandoInitUndCataUeberDieTastatur) {
    Egos e;
    e.fahre(4'000'000);
    // Kleinbuchstaben = Tastenkappe ohne Umschalter; EGOS tauscht Groß/Klein (FB7FH Bit 4).
    e.tippe("init");
    EXPECT_EQ(e.m->textZeile(24), ">INIT") << bildText(*e.m);
    e.tippe("\n", 4'000'000);   // INIT braucht ~2,5 Mio. Takte; Tasten davor gingen verloren
    // INIT legt die ADT an (LDIR 0C71H → FC00H, M2-Berichtigung) und meldet sich mit OK.
    EXPECT_EQ(e.m->memReadDebug(0xFC00), e.m->memReadDebug(0x0C71)) << bildText(*e.m);
    EXPECT_EQ(e.m->textZeile(24), ">") << bildText(*e.m);
    const uint32_t piep_vorher = e.m->bellCount();

    e.tippe("cata", 500'000);
    EXPECT_EQ(e.m->textZeile(24), ">CATA") << bildText(*e.m);
    e.tippe("\n", 0);
    // CATA rollt je Zeile einmal (VIS-Kommando + ~43 ms Wartezeit).  Hinter RKET läuft die
    // Kette durch die 2048 leeren 00H-Objekte bis 4000H (egos.md §1.4) — bis RKET warten.
    for (int i = 0; i < 400 && findeZeile(*e.m, "RKET") < 0; ++i) e.fahre(100'000);
    e.fahre(500'000);
    const std::string t = bildText(*e.m);
    // CATA listet die RAM-/ROM-Kette mit Kennbyte, Segment und Adresse (egos.md §1.3)
    EXPECT_GE(findeZeile(*e.m, "KLOG"), 0) << t;
    EXPECT_GE(findeZeile(*e.m, "DEFINE"), 0) << t;
    EXPECT_GE(findeZeile(*e.m, "CATA"), 0) << t;
    EXPECT_GE(findeZeile(*e.m, "%01.1000"), 0) << t;   // KLOG bei 1000H im Segment 1
    EXPECT_NE(e.m->vis().rollzaehler(), 0) << "das Bild muss gerollt haben";
    EXPECT_GE(e.m->bellCount(), piep_vorher);
    // Nach 23 Zeilen wartet CATA auf ENTER (egos.prn 26BFH: SL_TST bis 11H) — die nächste Seite
    // setzt die leeren Objekte hinter 2800H fort.
    EXPECT_LT(findeZeile(*e.m, "%01.280C"), 0) << t;
    e.tippe("\n", 0);
    for (int i = 0; i < 100 && findeZeile(*e.m, "%01.280C") < 0; ++i) e.fahre(100'000);
    EXPECT_GE(findeZeile(*e.m, "%00 %01.280C"), 0) << bildText(*e.m);
}

TEST(Mc80Boot, KommandoCatLaeuftBisZumEndeDerKette) {
    Egos e;
    e.fahre(4'000'000);
    e.tippe("init\n", 4'000'000);
    e.tippe("cat\n", 0);
    for (int i = 0; i < 400 && e.m->textZeile(24) != ">"; ++i) e.fahre(100'000);
    const std::string t = bildText(*e.m);
    EXPECT_EQ(e.m->textZeile(24), ">") << t;
    EXPECT_GE(findeZeile(*e.m, "RKET.........................%01 %01.26CF"), 0) << t;
    EXPECT_LT(findeZeile(*e.m, "KLOG"), 0) << "CAT zeigt nur Kennbyte 01\n" << t;
}

TEST(Mc80Boot, ResetLaesstDasBildStehenUndEgosMeldetSichErneut) {
    Egos e;
    e.fahre(4'000'000);
    e.m->reset();
    e.fahre(4'000'000);
    EXPECT_GE(findeZeile(*e.m, "mc 80.30. microcomputer"), 0) << bildText(*e.m);
    EXPECT_EQ(e.m->textZeile(24).rfind(">", 0), 0u) << bildText(*e.m);
}

}  // namespace

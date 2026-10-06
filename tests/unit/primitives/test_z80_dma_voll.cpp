/**
 * @test Z80DmaVoll.* — Z80-DMA / UA858 vollständig gegen das Datenblatt (P8000 AP P9c).
 *
 * Transkription und Abdeckungsliste (Datenblatt-Punkt → Test): doc/p8000/z80_dma.md.
 * Die Programmierfolgen der Gastsysteme (PC 1715W, MON8) stehen in test_z80_dma.cpp; hier
 * geht es um den Baustein: jede Basisbyte-Erkennung, jede Folgebyte-Teilmenge, jede
 * Kombination Transferart × Betriebsart × Richtung × Porttyp × Adressmodus, Zähler- und
 * Adressgrenzen, Suchen mit Maske, RDY/Force Ready, Interrupts mit Vektorbeeinflussung,
 * Impulse, Zyklen/WAIT, Befehle, Lesesequenz, Save-State und die Beobachtungsschnittstelle.
 */

#include <gtest/gtest.h>
#include "core/primitives/z80_dma.h"
#include <algorithm>
#include <bitset>
#include <sstream>
#include <vector>

namespace {

uint8_t lo(uint16_t v) { return static_cast<uint8_t>(v); }
uint8_t hi(uint16_t v) { return static_cast<uint8_t>(v >> 8); }

struct Zugriff {
    char     art;      ///< 'R' Lesen, 'W' Schreiben
    bool     ea;
    uint16_t adr;
    uint8_t  wert;
    bool operator==(const Zugriff& o) const {
        return art == o.art && ea == o.ea && adr == o.adr && wert == o.wert;
    }
};

struct Rig {
    Z80Dma dma;
    std::vector<uint8_t> mem = std::vector<uint8_t>(65536, 0);
    std::vector<uint8_t> io  = std::vector<uint8_t>(65536, 0);
    std::vector<Zugriff> log;
    std::vector<bool> ende;
    int impulse = 0;
    int wartezahl = 0;                       ///< was warteTakte je Zugriff liefert
    std::vector<Zugriff> warteLog;
    bool protokoll = true;

    Rig() {
        dma.memRead   = [this](uint16_t a) { uint8_t v = mem[a]; if (protokoll) log.push_back({'R', false, a, v}); return v; };
        dma.memWrite  = [this](uint16_t a, uint8_t d) { mem[a] = d; if (protokoll) log.push_back({'W', false, a, d}); };
        dma.portRead  = [this](uint16_t a) { uint8_t v = io[a]; if (protokoll) log.push_back({'R', true, a, v}); return v; };
        dma.portWrite = [this](uint16_t a, uint8_t d) { io[a] = d; if (protokoll) log.push_back({'W', true, a, d}); };
        dma.blockEnde = [this](bool l) { ende.push_back(l); };
        dma.impuls    = [this] { ++impulse; };
        dma.warteTakte = [this](bool ea, uint16_t a, bool s) {
            warteLog.push_back({s ? 'W' : 'R', ea, a, 0});
            return wartezahl;
        };
        dma.setIEI(true);
        dma.reset();
    }
    void w(std::initializer_list<uint8_t> b) { for (uint8_t x : b) dma.ioWrite(0, x); }
    void w(const std::vector<uint8_t>& b) { for (uint8_t x : b) dma.ioWrite(0, x); }
    uint8_t r() { return dma.ioRead(0); }

    struct Lauf { int bytes = 0, cpu = 0, takte = 0, leer = 0; };
    /// Maschinenschleife: Bus an die DMA, solange sie fordert, sonst ein CPU-Befehl.  Endet,
    /// wenn die DMA gesperrt ist und nichts fordert, oder nach @p max Runden ohne bewegtes Byte.
    Lauf fahre(int max = 1000) {
        Lauf l;
        int still = 0;
        for (;;) {
            if (dma.busRequest()) {
                int t = dma.step();
                if (t) { ++l.bytes; l.takte += t; still = 0; }
                else { ++l.leer; if (++still > max) break; }
            } else {
                if (!dma.enabled() || ++still > max) break;
                dma.cpuZyklus();
                ++l.cpu;
            }
        }
        return l;
    }

    /// Beide Ports laden — unabhängig von „fest“: LOAD je einmal mit A und B als Quelle.
    /// @p art WR0 D1D0, @p wr1/@p wr2 ohne Zeitsteuerbyte, @p modus WR4 D6D5.
    void programm(uint8_t art, bool aQuelle, uint16_t a, uint16_t b, uint16_t len,
                  uint8_t wr1, uint8_t wr2, uint8_t modus, uint8_t wr5 = 0x82) {
        w({0xC3});
        w({uint8_t(art | 0x04 | 0x78), lo(a), hi(a), lo(len), hi(len)});
        w({wr1, wr2});
        w({uint8_t(0x81 | (modus << 5) | 0x0C), lo(b), hi(b)});
        w({wr5, 0xCF});
        w({art, 0xCF});
        w({uint8_t(art | (aQuelle ? 0x04 : 0x00))});
    }
};

uint8_t wr1(bool ea, int modus) { return uint8_t(0x04 | (ea ? 0x08 : 0) | (modus << 4)); }
uint8_t wr2(bool ea, int modus) { return uint8_t(0x00 | (ea ? 0x08 : 0) | (modus << 4)); }
int schrittweite(int modus) { return modus == 0 ? -1 : modus == 1 ? 1 : 0; }

} // namespace

// ═══ 1. Basisbyte-Erkennung, Folgebytes, Sperre durch Schreiben ═════════════

/// Jedes der 256 Bytes als Basisbyte: richtige Gruppe, richtige Zahl Folgebytes, und die
/// DMA ist danach gesperrt — ausser bei 87H und WR3 mit D6 (Datenblatt Z S. 44, U §10.1).
TEST(Z80DmaVoll, AlleBasisbytesGruppeFolgebytesUndSperre) {
    for (int d = 0; d < 256; ++d) {
        SCOPED_TRACE(d);
        Rig r;
        r.w({0x87});
        ASSERT_TRUE(r.dma.enabled());
        r.w({uint8_t(d)});
        const auto v = r.dma.sicht();
        int folge = 0;
        if (!(d & 0x80) && (d & 3)) { EXPECT_EQ(v.wr0, d); folge = int(std::bitset<8>(d & 0x78).count()); }
        else if (!(d & 0x80) && (d & 7) == 4) { EXPECT_EQ(v.wr1, d); folge = (d >> 6) & 1; }
        else if (!(d & 0x80)) { EXPECT_EQ(v.wr2, d); folge = (d >> 6) & 1; }
        else if ((d & 3) == 0) { EXPECT_EQ(v.wr3, d); folge = int(std::bitset<8>(d & 0x18).count());
                                 EXPECT_EQ(v.intFreigabe, (d & 0x20) != 0); }
        else if ((d & 3) == 1) { EXPECT_EQ(v.wr4, d); folge = int(std::bitset<8>(d & 0x1C).count()); }
        else if ((d & 3) == 2) { EXPECT_EQ(v.wr5, (d & 4) ? 0 : d); }
        else                   { folge = d == 0xBB ? 1 : 0; }
        EXPECT_EQ(v.folgeOffen, folge);
        const bool frei = d == 0x87 || ((d & 0x83) == 0x80 && (d & 0x40));
        EXPECT_EQ(r.dma.enabled(), frei);
    }
}

/// WR0: alle 16 Teilmengen der Zeigerbits D3–D6, Folgebytes in fester Reihenfolge.
TEST(Z80DmaVoll, WR0AlleFolgebyteTeilmengen) {
    for (int m = 0; m < 16; ++m) {
        SCOPED_TRACE(m);
        Rig r;
        std::vector<uint8_t> b{uint8_t(0x01 | (m << 3))};
        const uint8_t werte[4] = {0x11, 0x22, 0x33, 0x44};
        for (int i = 0; i < 4; ++i) if (m & (1 << i)) b.push_back(werte[i]);
        r.w(b);
        const auto v = r.dma.sicht();
        EXPECT_EQ(v.folgeOffen, 0);
        EXPECT_EQ(lo(v.startA), (m & 1) ? 0x11 : 0);
        EXPECT_EQ(hi(v.startA), (m & 2) ? 0x22 : 0);
        EXPECT_EQ(lo(v.blocklaenge), (m & 4) ? 0x33 : 0);
        EXPECT_EQ(hi(v.blocklaenge), (m & 8) ? 0x44 : 0);
        r.w({0x87});                                  // nächstes Byte ist wieder ein Basisbyte
        EXPECT_TRUE(r.dma.enabled());
    }
}

/// WR4: alle Teilmengen von D2–D4, und hinter dem Interrupt-Steuerbyte Impuls/Vektor in allen
/// vier Kombinationen (dessen D3/D4).
TEST(Z80DmaVoll, WR4AlleFolgebyteTeilmengenSamtImpulsUndVektor) {
    for (int m = 0; m < 8; ++m)
        for (int ic = 0; ic < 4; ++ic) {
            if (!(m & 4) && ic) continue;
            SCOPED_TRACE(m * 10 + ic);
            Rig r;
            const uint8_t steuer = uint8_t(0x01 | (ic << 3));
            std::vector<uint8_t> b{uint8_t(0x81 | (m << 2))};
            if (m & 1) b.push_back(0x55);
            if (m & 2) b.push_back(0x66);
            if (m & 4) { b.push_back(steuer); if (ic & 1) b.push_back(0x77); if (ic & 2) b.push_back(0x88); }
            r.w(b);
            const auto v = r.dma.sicht();
            EXPECT_EQ(v.folgeOffen, 0);
            EXPECT_EQ(lo(v.startB), (m & 1) ? 0x55 : 0);
            EXPECT_EQ(hi(v.startB), (m & 2) ? 0x66 : 0);
            EXPECT_EQ(v.intSteuer, (m & 4) ? steuer : 0);
            EXPECT_EQ(v.impulsSteuer, (ic & 1) ? 0x77 : 0);
            EXPECT_EQ(v.vektorRoh, (ic & 2) ? 0x88 : 0);
            r.w({0x87});
            EXPECT_TRUE(r.dma.enabled());
        }
}

/// WR3: Masken-/Vergleichsbyte in allen Teilmengen; WR1/WR2: Zeitsteuerbyte.
TEST(Z80DmaVoll, WR3UndWR1WR2Folgebytes) {
    for (int m = 0; m < 4; ++m) {
        Rig r;
        std::vector<uint8_t> b{uint8_t(0x80 | (m << 3))};
        if (m & 1) b.push_back(0x0F);
        if (m & 2) b.push_back(0x5A);
        r.w(b);
        EXPECT_EQ(r.dma.sicht().maske, (m & 1) ? 0x0F : 0);
        EXPECT_EQ(r.dma.sicht().vergleich, (m & 2) ? 0x5A : 0);
        EXPECT_EQ(r.dma.sicht().folgeOffen, 0);
    }
    Rig r;
    r.w({0x54, 0xCE, 0x50, 0x01});                    // WR1 mit Zeitbyte CEH, WR2 mit 01H
    const auto v = r.dma.sicht();
    EXPECT_EQ(v.zeitA, 0xCE); EXPECT_TRUE(v.variabelA); EXPECT_EQ(v.zyklusA, 2);
    EXPECT_EQ(v.zeitB, 0x01); EXPECT_TRUE(v.variabelB); EXPECT_EQ(v.zyklusB, 3);
    EXPECT_EQ(v.wr1, 0x54); EXPECT_EQ(v.wr2, 0x50);
}

// ═══ 2. Transfer-Matrix ════════════════════════════════════════════════════

/// Transferart (Transfer/Search/beides) × Betriebsart (Byte/Continuous/Burst/11) × Richtung ×
/// Porttyp A/B (Speicher/E/A) × Adressmodus A/B (−1/+1/fest/fest) — 1536 Fälle, je 5 Bytes.
/// Geprüft: die genaue Zugriffsfolge auf dem Bus, CPU-Zwischenzyklen (nur Byte-Betrieb), Takte,
/// Adresszähler, Bytezähler, Status, Ende-Ausgang.
TEST(Z80DmaVoll, TransferMatrixArtModusRichtungPorttypAdressmodus) {
    int fehler = 0;
    for (int art = 1; art <= 3; ++art)
    for (int modus = 0; modus < 4; ++modus)
    for (int aq = 0; aq < 2; ++aq)
    for (int aIo = 0; aIo < 2; ++aIo)
    for (int bIo = 0; bIo < 2; ++bIo)
    for (int am = 0; am < 4; ++am)
    for (int bm = 0; bm < 4; ++bm) {
        if (fehler > 10) return;
        std::ostringstream fall;
        fall << "art=" << art << " modus=" << modus << " aQuelle=" << aq << " aIo=" << aIo
             << " bIo=" << bIo << " am=" << am << " bm=" << bm;
        SCOPED_TRACE(fall.str());
        Rig r;
        for (int i = 0; i < 65536; ++i) { r.mem[i] = uint8_t((i * 37 + 11) % 255); r.io[i] = uint8_t((i * 53 + 7) % 255); }
        const uint16_t a0 = 0x1234, b0 = 0x5678, n = 5;
        r.programm(uint8_t(art), aq, a0, b0, n - 1, wr1(aIo, am), wr2(bIo, bm), uint8_t(modus));
        r.w({0x98, 0x00, 0xFF});                      // Maske 00, Vergleich FFH — kommt nie vor
        r.dma.setReady(false);                        // RDY aktiv (low-aktiv)
        r.w({0x87});
        r.log.clear();
        const auto l = r.fahre(50);

        const bool aSrc = aq;
        const uint16_t s0 = aSrc ? a0 : b0, d0 = aSrc ? b0 : a0;
        const int ds = schrittweite(aSrc ? am : bm), dd = schrittweite(aSrc ? bm : am);
        const bool sIo = aSrc ? aIo : bIo, dIo = aSrc ? bIo : aIo;
        std::vector<Zugriff> soll;
        for (int i = 0; i < n; ++i) {
            const uint16_t sa = uint16_t(s0 + i * ds);
            const uint8_t v = sIo ? r.io[sa] : r.mem[sa];
            soll.push_back({'R', sIo, sa, v});
            if (art != 2) soll.push_back({'W', dIo, uint16_t(d0 + i * dd), v});
        }
        bool ok = true;
        ok &= (r.log == soll);
        EXPECT_EQ(r.log.size(), soll.size());
        for (size_t i = 0; i < std::min(r.log.size(), soll.size()); ++i)
            if (!(r.log[i] == soll[i])) { ADD_FAILURE() << "Zugriff " << i << " abweichend"; break; }
        EXPECT_EQ(l.bytes, n);
        EXPECT_EQ(l.cpu, modus == 0 ? n - 1 : 0) << "CPU-Zyklen zwischen den Bytes";
        EXPECT_EQ(l.takte, n * (art == 2 ? 4 : 8));
        const uint16_t sEnd = uint16_t(s0 + n * ds), dEnd = art == 2 ? d0 : uint16_t(d0 + n * dd);
        EXPECT_EQ(aSrc ? r.dma.adresseA() : r.dma.adresseB(), sEnd);
        EXPECT_EQ(aSrc ? r.dma.adresseB() : r.dma.adresseA(), dEnd);
        EXPECT_EQ(r.dma.bytezaehler(), n);
        EXPECT_EQ(r.dma.status() & 0x31, 0x11);       // D0 Transfer, D4 kein Treffer, D5 Blockende
        EXPECT_TRUE(r.dma.blockEndeErreicht());
        EXPECT_FALSE(r.dma.enabled());
        if (!ok || l.bytes != n) ++fehler;
    }
}

// ═══ 3. Zähler und Adressgrenzen ═══════════════════════════════════════════

/// Blocklänge 0 ⇒ 2¹⁶ + 1 Byte, FFFFH ⇒ 2¹⁶ Byte; Adresszähler laufen über FFFFH/0000H.
TEST(Z80DmaVoll, BlocklaengeNullUndFFFFUndAdressueberlauf) {
    for (uint16_t len : {uint16_t(0x0000), uint16_t(0xFFFF), uint16_t(0x0001)}) {
        SCOPED_TRACE(len);
        Rig r;
        r.protokoll = false;
        r.programm(1, true, 0xFFF0, 0x0005, len, wr1(false, 1), wr2(true, 0), 1);
        r.dma.setReady(false);
        r.w({0x87});
        const auto l = r.fahre(10);
        const int n = len == 0 ? 0x10001 : len + 1;
        EXPECT_EQ(l.bytes, n);
        EXPECT_EQ(r.dma.adresseA(), uint16_t(0xFFF0 + n));
        EXPECT_EQ(r.dma.adresseB(), uint16_t(0x0005 - n));
        EXPECT_EQ(r.dma.bytezaehler(), uint16_t(n));    // Vorgabe: bewegte Bytes (N + 1)
    }
}

/// Bytezähler: Vorgabe N + 1 nach Blockende, mit setZaehlerLiestBlocklaenge N (Tabelle 1);
/// während des Blocks in beiden Fällen die bisher bewegten Bytes.
TEST(Z80DmaVoll, BytezaehlerVorgabeUndDatenblattTabelle1) {
    for (bool opt : {false, true}) {
        Rig r;
        r.dma.setZaehlerLiestBlocklaenge(opt);
        r.programm(1, true, 0x1000, 0x2000, 0x0102, wr1(false, 1), wr2(false, 1), 0);
        r.dma.setReady(false);
        r.w({0x87, 0xBB, 0x06});                      // Lesemaske: Zähler L/H — sperrt!
        r.w({0x87});
        for (int i = 0; i < 7; ++i) { r.dma.step(); r.dma.cpuZyklus(); }
        EXPECT_EQ(r.r(), 7); EXPECT_EQ(r.r(), 0);
        r.fahre(10);
        EXPECT_EQ(r.r(), opt ? 0x02 : 0x03);
        EXPECT_EQ(r.r(), 0x01);
        EXPECT_EQ(r.dma.sicht().zaehlerGelesen, opt ? 0x0102 : 0x0103);
    }
}

// ═══ 4. RDY, Betriebsarten, Force Ready ════════════════════════════════════

TEST(Z80DmaVoll, BurstGibtBusBeiRdyInaktivAbUndFaehrtFort) {
    Rig r;
    r.programm(1, true, 0x1000, 0x40, 9, wr1(false, 1), wr2(true, 2), 2);
    r.dma.setReady(true);                             // inaktiv
    r.w({0x87});
    EXPECT_FALSE(r.dma.busRequest());
    r.dma.setReady(false);
    EXPECT_TRUE(r.dma.busRequest());
    EXPECT_EQ(r.dma.step(), 8); EXPECT_EQ(r.dma.step(), 8);
    EXPECT_TRUE(r.dma.busRequest());                  // kein Abgeben zwischen den Bytes
    r.dma.setReady(true);
    EXPECT_FALSE(r.dma.busRequest());                 // Bus zurück
    EXPECT_EQ(r.dma.step(), 0);
    r.dma.setReady(false);
    EXPECT_EQ(r.fahre(5).bytes, 8);
    EXPECT_EQ(r.dma.adresseA(), 0x100A);
}

TEST(Z80DmaVoll, ContinuousHaeltDenBusOhneRdyUndBeginntNurMitRdy) {
    Rig r;
    r.programm(1, true, 0x1000, 0x40, 9, wr1(false, 1), wr2(true, 2), 1);
    r.dma.setReady(true);
    r.w({0x87});
    EXPECT_FALSE(r.dma.busRequest());                 // vor dem Beginn: Anforderung nur mit RDY
    r.dma.setReady(false);
    EXPECT_EQ(r.dma.step(), 8);
    r.dma.setReady(true);
    EXPECT_TRUE(r.dma.busRequest());                  // Bus bleibt
    EXPECT_EQ(r.dma.step(), 0);                       // … aber es bewegt sich nichts
    r.dma.setReady(false);
    const auto l = r.fahre(5);
    EXPECT_EQ(l.bytes, 9); EXPECT_EQ(l.cpu, 0);
}

TEST(Z80DmaVoll, ByteBetriebGibtNachJedemByteAbAuchBeiAktivemRdy) {
    Rig r;
    r.programm(1, true, 0x1000, 0x2000, 3, wr1(false, 1), wr2(false, 1), 0);
    r.dma.setReady(false);
    r.w({0x87});
    EXPECT_EQ(r.dma.step(), 8);
    EXPECT_FALSE(r.dma.busRequest());
    EXPECT_TRUE(r.dma.sicht().byteFreigabe);
    EXPECT_EQ(r.dma.step(), 0);
    r.dma.cpuZyklus();
    EXPECT_TRUE(r.dma.busRequest());
}

TEST(Z80DmaVoll, RdyPolaritaetUndStatusD1) {
    for (bool hoch : {false, true}) {
        Rig r;
        r.programm(1, true, 0x1000, 0x2000, 3, wr1(false, 1), wr2(false, 1), 1, hoch ? 0x8A : 0x82);
        r.w({0x87});
        r.dma.setReady(!hoch);                        // inaktiv
        EXPECT_FALSE(r.dma.busRequest());
        EXPECT_NE(r.dma.status() & 0x02, 0);
        r.dma.setReady(hoch);                         // aktiv
        EXPECT_TRUE(r.dma.busRequest());
        EXPECT_EQ(r.dma.status() & 0x02, 0);
        r.dma.setReady(!hoch);
        r.w({0xB3, 0x87});                            // Force Ready zählt für den Bus, nicht für D1
        EXPECT_TRUE(r.dma.busRequest());
        EXPECT_NE(r.dma.status() & 0x02, 0);
    }
}

/// Force Ready fällt durch C3, CF, A3, Blockende, Treffer und jede Busfreigabe (UA858 §10.1 B3).
TEST(Z80DmaVoll, ForceReadyRuecksetzbedingungen) {
    for (uint8_t cmd : {uint8_t(0xC3), uint8_t(0xCF), uint8_t(0xA3)}) {
        Rig r;
        r.w({0xB3});
        EXPECT_TRUE(r.dma.sicht().forceReady);
        r.w({cmd});
        EXPECT_FALSE(r.dma.sicht().forceReady) << int(cmd);
    }
    for (uint8_t cmd : {uint8_t(0x83), uint8_t(0x87), uint8_t(0xAB), uint8_t(0xD3), uint8_t(0x8B)}) {
        Rig r;
        r.w({0xB3, cmd});
        EXPECT_TRUE(r.dma.sicht().forceReady) << int(cmd);
    }
    {   // Blockende (Continuous)
        Rig r;
        r.programm(1, true, 0x1000, 0x2000, 2, wr1(false, 1), wr2(false, 1), 1);
        r.dma.setReady(true);
        r.w({0xB3, 0x87});
        EXPECT_EQ(r.fahre(5).bytes, 3);
        EXPECT_FALSE(r.dma.sicht().forceReady);
    }
    {   // Busfreigabe im Byte-Betrieb: nur ein Byte
        Rig r;
        r.programm(1, true, 0x1000, 0x2000, 2, wr1(false, 1), wr2(false, 1), 0);
        r.dma.setReady(true);
        r.w({0xB3, 0x87});
        EXPECT_EQ(r.fahre(5).bytes, 1);
        EXPECT_FALSE(r.dma.sicht().forceReady);
        EXPECT_TRUE(r.dma.enabled());                 // wartet auf RDY
    }
    {   // Treffer mit Stopp
        Rig r;
        r.mem[0x1001] = 0x42;
        r.programm(3, true, 0x1000, 0x2000, 9, wr1(false, 1), wr2(false, 1), 1);
        r.dma.setReady(true);
        r.w({0x9C, 0x00, 0x42, 0xB3, 0x87});
        EXPECT_EQ(r.fahre(5).bytes, 2);
        EXPECT_FALSE(r.dma.sicht().forceReady);
    }
}

// ═══ 5. Suchen: Maske, Vergleich, Stopp, Tabelle 2 ═════════════════════════

/// Maske × Vergleichsbyte gegen die Bytes 00H…FFH: Status D4 und Trefferstelle.  Maske 0 =
/// Bit zählt, FFH = jedes Byte trifft.
TEST(Z80DmaVoll, SuchMaskeUndVergleichMatrix) {
    const uint8_t masken[] = {0x00, 0xFF, 0x0F, 0xF0, 0x55, 0xAA, 0xFE, 0x7F};
    const uint8_t vergl[] = {0x00, 0x42, 0xFF, 0x80, 0x13};
    for (uint8_t m : masken)
        for (uint8_t v : vergl) {
            SCOPED_TRACE(int(m) * 256 + v);
            int erster = -1;
            for (int i = 0; i < 256 && erster < 0; ++i) if (((i ^ v) & ~m & 0xFF) == 0) erster = i;
            // ohne Stopp: Status D4 sagt „irgendwo getroffen“, Lauf bis Blockende
            Rig r;
            for (int i = 0; i < 256; ++i) r.mem[0x3000 + i] = uint8_t(i);
            r.programm(2, true, 0x3000, 0, 255, wr1(false, 1), wr2(false, 2), 1);
            r.w({0x98, m, v, 0xB3, 0x87});
            EXPECT_EQ(r.fahre(5).bytes, 256);
            EXPECT_EQ((r.dma.status() & 0x10) == 0, erster >= 0);
            // mit Stopp, Byte-Betrieb: genau M Bytes (Tabelle 2)
            Rig s;
            for (int i = 0; i < 256; ++i) s.mem[0x3000 + i] = uint8_t(i);
            s.programm(2, true, 0x3000, 0, 255, wr1(false, 1), wr2(false, 2), 0);
            s.dma.setReady(false);
            s.w({0x9C, m, v, 0x87});
            const int bytes = s.fahre(5).bytes;
            EXPECT_EQ(bytes, erster >= 0 ? erster + 1 : 256);
        }
}

/// Tabelle 2: Stopp bei Treffer im Byte M — Transfer/Search alle Betriebsarten und Search im
/// Byte-Betrieb: M Bytes; Search Burst/Continuous: M + 1 (gepuffertes Lesen).
TEST(Z80DmaVoll, StoppBeiTrefferTabelle2) {
    const int M = 5;
    for (int art : {2, 3})
        for (int modus : {0, 1, 2}) {
            SCOPED_TRACE(art * 10 + modus);
            Rig r;
            for (int i = 0; i < 32; ++i) r.mem[0x3000 + i] = uint8_t(0x10 + i);
            r.mem[0x3000 + M - 1] = 0xE7;
            r.programm(uint8_t(art), true, 0x3000, 0x4000, 31, wr1(false, 1), wr2(false, 1), uint8_t(modus));
            r.dma.setReady(false);
            r.w({0x9C, 0x00, 0xE7, 0x87});
            const int bytes = r.fahre(5).bytes;
            const int soll = (art == 2 && modus != 0) ? M + 1 : M;
            EXPECT_EQ(bytes, soll);
            EXPECT_EQ(r.dma.adresseA(), 0x3000 + soll);
            EXPECT_EQ(r.dma.adresseB(), art == 3 ? 0x4000 + M : 0x4000);
            EXPECT_EQ(r.dma.status() & 0x10, 0);          // Treffer
            EXPECT_NE(r.dma.status() & 0x20, 0);          // kein Blockende
            EXPECT_FALSE(r.dma.enabled());
            EXPECT_FALSE(r.dma.blockEndeErreicht());
            // 8B + 87 = „Weiterlaufen nach Treffer“ (UA858 §10.1)
            r.w({0x8B, 0x87});
            EXPECT_NE(r.dma.status() & 0x10, 0);
            EXPECT_GT(r.fahre(5).bytes, 0);
            EXPECT_TRUE(r.dma.blockEndeErreicht());
        }
}

/// Search + Transfer überträgt auch das Trefferbyte und läuft ohne Stopp weiter.
TEST(Z80DmaVoll, SuchenMitTransferOhneStoppUebertraegtAlles) {
    Rig r;
    for (int i = 0; i < 8; ++i) r.mem[0x3000 + i] = uint8_t(i);
    r.programm(3, true, 0x3000, 0x4000, 7, wr1(false, 1), wr2(false, 1), 1);
    r.w({0x98, 0x00, 0x03, 0xB3, 0x87});
    EXPECT_EQ(r.fahre(5).bytes, 8);
    for (int i = 0; i < 8; ++i) EXPECT_EQ(r.mem[0x4000 + i], i);
    EXPECT_EQ(r.dma.status() & 0x30, 0);              // Treffer und Blockende
}

// ═══ 6. Interrupts ═════════════════════════════════════════════════════════

namespace {
/// Block mit 4 Bytes, Interrupt-Steuerbyte @p ic + Vektor @p v; Byte 1 = 0x77 (Trefferbyte).
Rig& interruptLauf(Rig& r, uint8_t ic, uint8_t v, int art = 1, bool stopp = false, uint8_t trefferAn = 1) {
    for (int i = 0; i < 4; ++i) r.mem[0x3000 + i] = uint8_t(0x10 + i);
    r.mem[0x3000 + trefferAn] = 0x77;
    r.programm(uint8_t(art), true, 0x3000, 0x4000, 3, wr1(false, 1), wr2(false, 1), 1);
    r.w({0xB1, uint8_t(ic | 0x10), v});               // WR4: Continuous, Int-Steuerbyte + Vektor
    r.w({uint8_t(0x98 | (stopp ? 4 : 0)), 0x00, 0x77, 0xAB, 0xB3, 0x87});
    r.fahre(5);
    return r;
}
}

TEST(Z80DmaVoll, VektorOhneUndMitStatusbeeinflussungJeGrund) {
    {   Rig r; interruptLauf(r, 0x02, 0xF0);          // Blockende, roh
        ASSERT_TRUE(r.dma.hasInterrupt()); EXPECT_EQ(r.dma.getVector(), 0xF0); }
    {   Rig r; interruptLauf(r, 0x22, 0xF0);          // Blockende → V2V1 = 10
        EXPECT_EQ(r.dma.vektor(), 0xF4); EXPECT_EQ(r.dma.getVector(), 0xF4); }
    {   Rig r; interruptLauf(r, 0x21, 0xFF, 3, true); // Treffer mit Stopp → 01
        ASSERT_TRUE(r.dma.hasInterrupt()); EXPECT_EQ(r.dma.getVector(), 0xFB); }
    {   Rig r; interruptLauf(r, 0x23, 0x00, 3, false, 3);  // Treffer im letzten Byte + Blockende → 11
        EXPECT_EQ(r.dma.getVector(), 0x06); }
    {   Rig r; interruptLauf(r, 0x23, 0x00, 3, false, 1);  // Treffer früher, Ende später → 11
        EXPECT_EQ(r.dma.getVector(), 0x06); }
    {   Rig r; interruptLauf(r, 0x01, 0x00, 1, false);     // nur Transfer: kein Vergleich, kein Treffer
        EXPECT_FALSE(r.dma.hasInterrupt()); EXPECT_NE(r.dma.status() & 0x10, 0); }
}

TEST(Z80DmaVoll, InterruptBeiRdyMitB7UndRETI) {
    Rig r;
    r.programm(1, true, 0x3000, 0x40, 2, wr1(false, 1), wr2(true, 2), 0);
    r.w({0x91, 0x70, 0xE0, 0xAB});                    // Int bei RDY, Status→Vektor, Vektor E0H
    r.dma.setReady(true);                             // inaktiv
    r.w({0x87});
    EXPECT_FALSE(r.dma.hasInterrupt());
    r.dma.setReady(false);                            // RDY aktiv → Interrupt statt Bus
    EXPECT_TRUE(r.dma.hasInterrupt());
    EXPECT_FALSE(r.dma.busRequest());
    EXPECT_EQ(r.dma.getVector(), 0xE0);               // V2V1 = 00
    r.w({0xB7, 0x87});                                // ISR: Enable after RETI, Enable DMA
    EXPECT_FALSE(r.dma.busRequest());                 // IUS und B7 halten zurück
    r.dma.onRETI();
    EXPECT_TRUE(r.dma.busRequest());
    EXPECT_EQ(r.dma.step(), 8);
    r.dma.cpuZyklus();                                // nach der Busfreigabe: wieder erst Interrupt
    EXPECT_TRUE(r.dma.hasInterrupt());
    EXPECT_FALSE(r.dma.busRequest());
}

TEST(Z80DmaVoll, IusSperrtBusanforderungBisRETIUndIeiGatetRETI) {
    Rig r;
    interruptLauf(r, 0x02, 0x20);
    ASSERT_TRUE(r.dma.hasInterrupt());
    EXPECT_FALSE(r.dma.getIEO());
    r.dma.getVector();
    EXPECT_TRUE(r.dma.sicht().ius);
    EXPECT_FALSE(r.dma.getIEO());
    r.w({0xCF, 0xB3, 0x87});                          // neuer Block, freigegeben
    EXPECT_FALSE(r.dma.busRequest());                 // IUS sperrt die Busanforderung
    r.dma.setIEI(false);
    r.dma.onRETI();                                   // RETI einer höherrangigen ISR
    EXPECT_TRUE(r.dma.sicht().ius);
    r.dma.setIEI(true);
    r.dma.onRETI();
    EXPECT_FALSE(r.dma.sicht().ius);
    EXPECT_TRUE(r.dma.getIEO());
    EXPECT_TRUE(r.dma.busRequest());
}

TEST(Z80DmaVoll, InterruptFreigabeSperreUndRuecksetzen) {
    {   // WR3 D5 ≙ AB
        Rig r;
        for (int i = 0; i < 4; ++i) r.mem[0x3000 + i] = 1;
        r.programm(1, true, 0x3000, 0x4000, 3, wr1(false, 1), wr2(false, 1), 1);
        r.w({0x91, 0x12, 0x30, 0xA0, 0xB3, 0x87});    // WR3 A0: nur Interruptfreigabe
        r.fahre(5);
        EXPECT_TRUE(r.dma.hasInterrupt());
    }
    {   // ohne Freigabe: Bedingung verworfen [A5]
        Rig r;
        r.programm(1, true, 0x3000, 0x4000, 3, wr1(false, 1), wr2(false, 1), 1);
        r.w({0x91, 0x12, 0x30, 0xB3, 0x87});
        r.fahre(5);
        EXPECT_FALSE(r.dma.hasInterrupt());
        EXPECT_NE(r.dma.status() & 0x08, 0);
        r.w({0xAB});
        EXPECT_FALSE(r.dma.hasInterrupt());
    }
    {   // AF blendet aus, AB zeigt wieder; A3 löscht IP und IUS
        Rig r;
        interruptLauf(r, 0x02, 0x30);
        r.w({0xAF});
        EXPECT_FALSE(r.dma.hasInterrupt());
        EXPECT_TRUE(r.dma.getIEO());
        EXPECT_EQ(r.dma.status() & 0x08, 0);          // IP steht noch
        r.w({0xAB});
        EXPECT_TRUE(r.dma.hasInterrupt());
        r.dma.getVector();
        r.w({0xA3});
        EXPECT_FALSE(r.dma.sicht().ius);
        EXPECT_FALSE(r.dma.sicht().intAnstehend);
        EXPECT_FALSE(r.dma.sicht().intFreigabe);
    }
    {   // AF lässt IUS stehen
        Rig r;
        interruptLauf(r, 0x02, 0x30);
        r.dma.getVector();
        r.w({0xAF});
        EXPECT_TRUE(r.dma.sicht().ius);
    }
}

TEST(Z80DmaVoll, ImpulseAlle256ByteMitVersatz) {
    for (int versatz : {0, 5, 255}) {
        Rig r;
        r.protokoll = false;
        r.programm(1, true, 0x1000, 0x8000, 599, wr1(false, 1), wr2(false, 1), 1);
        r.w({0x91, 0x0C, uint8_t(versatz), 0xB3, 0x87});   // Impulse an, Impulssteuerbyte
        r.fahre(5);
        int soll = 0;
        for (int c = 0; c < 600; ++c) if ((c & 0xFF) == versatz) ++soll;
        EXPECT_EQ(r.impulse, soll) << versatz;
        EXPECT_EQ(r.dma.impulse(), uint32_t(soll));
        EXPECT_FALSE(r.dma.hasInterrupt());           // Impuls ist kein Interrupt
    }
    Rig r;
    r.programm(1, true, 0x1000, 0x8000, 599, wr1(false, 1), wr2(false, 1), 1);
    r.w({0x91, 0x08, 0x00, 0xB3, 0x87});              // Impulssteuerbyte, aber D2 = 0
    r.fahre(5);
    EXPECT_EQ(r.impulse, 0);
}

// ═══ 7. Zyklen, /WAIT ══════════════════════════════════════════════════════

TEST(Z80DmaVoll, ZykluslaengenMatrixUndStandard) {
    const int laenge[4] = {4, 3, 2, 2};
    for (int ta = 0; ta < 4; ++ta)
        for (int tb = 0; tb < 4; ++tb) {
            Rig r;
            r.programm(1, true, 0x1000, 0x2000, 0, wr1(false, 1), wr2(false, 1), 1);
            r.w({0x54, uint8_t(ta), 0x50, uint8_t(tb), 0xB3, 0x87});
            EXPECT_EQ(r.dma.step(), laenge[ta] + laenge[tb]) << ta << tb;
        }
    // C7/CB: zurück zum Standard; Vorgabe 4 + 4, Datenblatt Speicher 3 / E/A 4
    for (bool opt : {false, true})
        for (int aIo = 0; aIo < 2; ++aIo)
            for (int bIo = 0; bIo < 2; ++bIo) {
                Rig r;
                r.dma.setStandardZyklen(opt);
                r.programm(1, true, 0x10, 0x20, 1, wr1(aIo, 1), wr2(bIo, 1), 1);
                r.w({uint8_t(0x54 | (aIo << 3)), 0x02, 0xC7, 0xCB, 0xB3, 0x87});
                const int a = opt ? (aIo ? 4 : 3) : 4, b = opt ? (bIo ? 4 : 3) : 4;
                EXPECT_EQ(r.dma.step(), a + b);
                EXPECT_FALSE(r.dma.sicht().variabelA);
            }
    {   // Search: nur der Lesezyklus
        Rig r;
        r.programm(2, true, 0x10, 0x20, 1, wr1(false, 1), wr2(false, 1), 1);
        r.w({0x54, 0x01, 0xB3, 0x87});
        EXPECT_EQ(r.dma.step(), 3);
    }
}

/// /CE/WAIT (WR5 D4): Wartetakte je Zugriff — nur bei Standardzyklus oder variabel ≥ 3
/// (Speicher) bzw. = 4 (E/A) [A9]; ohne WR5 D4 wird der Rückruf nicht gefragt.
TEST(Z80DmaVoll, WaitEingangVerlaengertZyklen) {
    struct F { bool aIo, bIo; int zeitA, zeitB; int soll; };   // zeit −1 = Standard
    const F faelle[] = {
        {false, true, -1, -1, 4 + 2 + 4 + 2},
        {false, false, 2, -1, 2 + 4 + 2},             // variabel 2 Takte: kein /WAIT
        {false, false, 1, -1, 3 + 2 + 4 + 2},         // variabel 3 (Speicher): /WAIT
        {true, false, 1, -1, 3 + 4 + 2},              // variabel 3 (E/A): kein /WAIT
        {true, false, 0, -1, 4 + 2 + 4 + 2},          // variabel 4 (E/A): /WAIT
    };
    for (const F& f : faelle) {
        Rig r;
        r.wartezahl = 2;
        r.programm(1, true, 0x10, 0x20, 1, wr1(f.aIo, 1), wr2(f.bIo, 1), 1, 0x92);
        if (f.zeitA >= 0) r.w({uint8_t(0x54 | (f.aIo << 3)), uint8_t(f.zeitA)});
        r.w({0xB3, 0x87});
        EXPECT_EQ(r.dma.step(), f.soll);
    }
    Rig r;
    r.wartezahl = 2;
    r.programm(1, true, 0x10, 0x20, 1, wr1(false, 1), wr2(false, 1), 1, 0x82);
    r.w({0xB3, 0x87});
    EXPECT_EQ(r.dma.step(), 8);
    EXPECT_TRUE(r.warteLog.empty());
}

// ═══ 8. Befehle ════════════════════════════════════════════════════════════

TEST(Z80DmaVoll, ResetC3WasErTutUndWasNicht) {
    Rig r;
    r.programm(1, true, 0x1234, 0x5678, 3, wr1(false, 1), wr2(false, 1), 2, 0xBA);  // WR5: hoch, WAIT, Restart
    r.w({0x54, 0x01, 0xD1, 0x12, 0x40, 0xAB, 0xB3, 0xBB, 0x06});
    r.r();                                            // Lesesequenz steht danach auf RR2
    r.w({0x87, 0xC3});
    const auto v = r.dma.sicht();
    EXPECT_FALSE(v.freigegeben); EXPECT_FALSE(v.intFreigabe); EXPECT_FALSE(v.forceReady);
    EXPECT_FALSE(v.autoRestart); EXPECT_FALSE(v.waitFunktion);
    EXPECT_FALSE(v.variabelA); EXPECT_EQ(v.zyklusA, 4);
    EXPECT_EQ(v.wr5 & 0x08, 0x08);                    // RDY-Polarität bleibt
    EXPECT_EQ(v.betriebsart, 2);                      // Betriebsart bleibt
    EXPECT_EQ(v.adresseA, 0x1234); EXPECT_EQ(v.adresseB, 0x5678);
    EXPECT_EQ(v.lesemaske, 0x06); EXPECT_EQ(v.lesePos, 2);   // Lesesequenz bleibt (A7 nötig)
    EXPECT_EQ(r.dma.status() & 0x31, 0x30);
}

TEST(Z80DmaVoll, LoadFesteZieladresseWirdNichtGeladen) {
    for (int bm = 0; bm < 4; ++bm) {
        Rig r;
        r.w({0x7D, 0x00, 0x10, 0x03, 0x00, wr1(false, 1), wr2(true, bm), 0x8D, 0x40, 0x00});
        r.w({0xCF});                                  // A Quelle: A immer, B nur wenn nicht fest
        EXPECT_EQ(r.dma.adresseA(), 0x1000);
        EXPECT_EQ(r.dma.adresseB(), bm >= 2 ? 0x0000 : 0x0040) << bm;
    }
    // Status: CF löscht D0 (Transfer erfolgt) und D5, nicht D4
    Rig r;
    r.mem[0x3000] = 0x77;
    r.programm(3, true, 0x3000, 0x4000, 0, wr1(false, 1), wr2(false, 1), 1);
    r.w({0x98, 0x00, 0x77, 0xB3, 0x87});
    r.fahre(5);
    EXPECT_EQ(r.dma.status() & 0x31, 0x01);
    r.w({0xCF});
    EXPECT_EQ(r.dma.status() & 0x31, 0x20);
    EXPECT_EQ(r.dma.bytezaehler(), 0);
}

TEST(Z80DmaVoll, ContinueReinitStatusDisableUnbekannt) {
    Rig r;
    r.programm(1, true, 0x1000, 0x2000, 3, wr1(false, 1), wr2(false, 1), 1);
    r.w({0xB3, 0x87});
    r.fahre(5);
    EXPECT_EQ(r.dma.status() & 0x21, 0x01);
    r.w({0xD3});                                      // Continue: Zähler 0, Adressen weiter, D5 = 1
    EXPECT_FALSE(r.dma.enabled());                    // gibt NICHT frei
    EXPECT_EQ(r.dma.bytezaehler(), 0);
    EXPECT_EQ(r.dma.adresseA(), 0x1004);
    EXPECT_NE(r.dma.status() & 0x20, 0);
    EXPECT_FALSE(r.dma.blockEndeErreicht());
    r.w({0xB3, 0x87});
    EXPECT_EQ(r.fahre(5).bytes, 4);
    EXPECT_EQ(r.dma.adresseA(), 0x1008);
    r.w({0x8B});                                      // D4/D5 zurück, D0 bleibt
    EXPECT_EQ(r.dma.status() & 0x31, 0x31);
    for (uint8_t d : {uint8_t(0x83), uint8_t(0x8F), uint8_t(0xFB), uint8_t(0xFF), uint8_t(0x86)}) {
        r.w({0x87});
        r.w({d});                                     // Disable bzw. unbekannt: nur sperren [A4]
        EXPECT_FALSE(r.dma.enabled()) << int(d);
        EXPECT_EQ(r.dma.adresseA(), 0x1008);
    }
}

/// Jedes Steuerbyte während eines laufenden Byte-Betriebs hält die DMA an; 87 setzt fort.
TEST(Z80DmaVoll, SchreibenWaehrendDesTransfersSperrt) {
    Rig r;
    r.programm(1, true, 0x1000, 0x2000, 9, wr1(false, 1), wr2(false, 1), 0);
    r.dma.setReady(false);
    r.w({0x87});
    r.dma.step(); r.dma.cpuZyklus();
    r.w({0xBB, 0x01});                                // Lesemaske setzen = Steuerbyte
    EXPECT_FALSE(r.dma.busRequest());
    r.r();                                            // Lesen sperrt nicht (und gibt nicht frei)
    EXPECT_FALSE(r.dma.enabled());
    r.w({0x87});
    EXPECT_EQ(r.fahre(5).bytes, 9);
}

// ═══ 9. Lesesequenz ════════════════════════════════════════════════════════

TEST(Z80DmaVoll, LesesequenzAlleMaskenUndA7BF) {
    Rig r;
    r.programm(1, true, 0x1234, 0x5678, 0x0102, wr1(false, 1), wr2(false, 0), 1);
    r.w({0xB3, 0x87});
    for (int i = 0; i < 3; ++i) r.dma.step();
    const uint8_t st = r.dma.status();
    const uint8_t regs[7] = {st, 0x03, 0x00, 0x37, 0x12, 0x75, 0x56};
    for (int m = 0; m < 256; ++m) {
        r.w({0xBB, uint8_t(m)});
        std::vector<uint8_t> soll;
        for (int k = 0; k < 7; ++k) if (m & (1 << k)) soll.push_back(regs[k]);
        if (soll.empty()) soll.push_back(st);         // Maske 0 [A3]
        for (size_t i = 0; i < soll.size() * 2 + 1; ++i)
            ASSERT_EQ(r.r(), soll[i % soll.size()]) << "Maske " << m << " Lesen " << i;
    }
    r.w({0xBB, 0x7E});
    EXPECT_EQ(r.r(), 0x03); EXPECT_EQ(r.r(), 0x00);
    r.w({0xA7});                                      // Initiate Read Sequence
    EXPECT_EQ(r.r(), 0x03);
    r.w({0xBF});                                      // Read Status Byte
    EXPECT_EQ(r.r(), st);
    EXPECT_EQ(r.r(), st);                             // [A3]: Maske = nur Status
}

// ═══ 10. Auto-Restart ══════════════════════════════════════════════════════

TEST(Z80DmaVoll, AutoRestartLaedtNeuUndWartetAufRdy) {
    Rig r;
    for (int i = 0; i < 16; ++i) r.mem[0x1000 + i] = uint8_t(i + 1);
    r.programm(1, true, 0x1000, 0x2000, 1, wr1(false, 1), wr2(false, 1), 0, 0xA2);
    r.dma.setReady(false);                            // RDY aktiv
    r.w({0x87});
    EXPECT_EQ(r.dma.step(), 8); r.dma.cpuZyklus();    // Byte 1 (1000H)
    // die CPU schreibt zwischen zwei Bytes eine neue Startadresse A und gibt wieder frei (UA858 §6)
    r.w({0x1D, 0x08, 0x10, 0x87});
    EXPECT_EQ(r.dma.adresseA(), 0x1001);              // Adresszähler unberührt
    EXPECT_EQ(r.dma.step(), 8);                       // Byte 2 → Blockende → Auto-Restart
    EXPECT_TRUE(r.dma.enabled());
    EXPECT_EQ(r.dma.adresseA(), 0x1008);
    EXPECT_EQ(r.dma.adresseB(), 0x2000);
    EXPECT_EQ(r.dma.bytezaehler(), 0);
    EXPECT_EQ(r.dma.status() & 0x20, 0);              // [A10]
    EXPECT_TRUE(r.dma.blockEndeErreicht());
    r.dma.cpuZyklus();
    EXPECT_EQ(r.dma.step(), 8);
    EXPECT_EQ(r.mem[0x2000], 9);                      // zweiter Block ab 1008H
    // Force Ready fällt am Blockende: ein Restart-Block mit B3 wartet danach auf RDY
    r.dma.setReady(true);                             // RDY inaktiv
    r.w({0xB3, 0x87});
    r.dma.cpuZyklus();
    EXPECT_EQ(r.dma.step(), 8);                       // letztes Byte des Blocks (Force Ready)
    r.dma.cpuZyklus();
    EXPECT_TRUE(r.dma.enabled());
    EXPECT_FALSE(r.dma.busRequest());
}

// ═══ 11. Save-State und Beobachtung ════════════════════════════════════════

TEST(Z80DmaVoll, SaveStateRundreiseMitErweiterungUndAltformat) {
    Rig r;
    for (int i = 0; i < 64; ++i) r.mem[0x3000 + i] = uint8_t(i);
    r.programm(3, true, 0x3000, 0x4000, 63, wr1(false, 1), wr2(false, 1), 2);
    r.w({0x54, 0x01, 0x91, 0x2D, 0x07, 0xA0, 0x9C, 0x0F, 0x2A, 0xAB, 0xBB, 0x1F});
    r.dma.setReady(false);
    r.w({0x87});
    for (int i = 0; i < 5; ++i) r.dma.step();
    r.r();
    std::vector<uint8_t> buf;
    r.dma.serialize(buf);

    Rig q;
    q.mem = r.mem;
    const uint8_t* p = buf.data();
    ASSERT_TRUE(q.dma.deserialize(p, buf.data() + buf.size()));
    EXPECT_EQ(p, buf.data() + buf.size());
    std::vector<uint8_t> buf2;
    q.dma.serialize(buf2);
    EXPECT_EQ(buf, buf2);
    q.dma.setReady(false);
    r.log.clear(); q.log.clear();
    r.fahre(5); q.fahre(5);
    EXPECT_EQ(r.log, q.log);
    EXPECT_EQ(r.dma.hasInterrupt(), q.dma.hasInterrupt());
    EXPECT_EQ(r.dma.vektor(), q.dma.vektor());

    // Altformat (vor P9c, ohne „DM2“-Block): lädt, Erweiterung auf Vorgaben, folgendes Byte bleibt
    auto it = std::search(buf.begin(), buf.end(), std::begin("DM2"), std::begin("DM2") + 3);
    ASSERT_NE(it, buf.end());
    std::vector<uint8_t> alt(buf.begin(), it);
    alt.push_back(0x01);                              // z. B. dma_ende_ der P8000-Floppy
    const uint8_t* pa = alt.data();
    Rig a;
    ASSERT_TRUE(a.dma.deserialize(pa, alt.data() + alt.size()));
    EXPECT_EQ(pa, alt.data() + alt.size() - 1);
    EXPECT_EQ(a.dma.sicht().wr3, 0);
    // abgeschnitten
    const uint8_t* pk = buf.data();
    EXPECT_FALSE(a.dma.deserialize(pk, buf.data() + buf.size() - 2));
}

/// Alle Abfragen für den Debugger sind ohne Seiteneffekt (Zustand bitgleich davor/danach).
TEST(Z80DmaVoll, BeobachtungOhneSeiteneffekt) {
    Rig r;
    interruptLauf(r, 0x23, 0x40, 3);
    std::vector<uint8_t> vor, nach;
    r.dma.serialize(vor);
    for (int i = 0; i < 10; ++i) {
        (void)r.dma.sicht(); (void)r.dma.status(); (void)r.dma.vektor(); (void)r.dma.busRequest();
        (void)r.dma.adresseA(); (void)r.dma.adresseB(); (void)r.dma.bytezaehler();
        (void)r.dma.blockEndeErreicht(); (void)r.dma.hasInterrupt(); (void)r.dma.getIEO();
        (void)r.dma.impulse(); (void)r.dma.enabled();
    }
    r.dma.serialize(nach);
    EXPECT_EQ(vor, nach);
    const auto v = r.dma.sicht();
    EXPECT_EQ(v.transferart, 3); EXPECT_EQ(v.betriebsart, 1); EXPECT_TRUE(v.aQuelle);
    EXPECT_EQ(v.startA, 0x3000); EXPECT_EQ(v.startB, 0x4000); EXPECT_EQ(v.blocklaenge, 3);
    EXPECT_EQ(v.vergleich, 0x77); EXPECT_EQ(v.maske, 0x00);
    EXPECT_EQ(v.intSteuer, 0x33); EXPECT_EQ(v.vektorRoh, 0x40);
    EXPECT_TRUE(v.intAnstehend); EXPECT_EQ(v.intGrund, 3);
    EXPECT_EQ(v.status, r.dma.status());
    EXPECT_FALSE(v.busAnforderung);
}

// ═══ 12. Gastfolgen bitgleich ══════════════════════════════════════════════

/// MON8 3.1 Hardwaretest 26 (P8000, 087CH ff.): Programm aus 0C0AH, Continuous Speicher→Speicher
/// 257 Bytes, dann `AND 3BH` ≠ 0 und Zähler = 0100H (mit setZaehlerLiestBlocklaenge).  Zweiter
/// Teil: AB 87 BF und Interrupt mit Vektor 40H.
TEST(Z80DmaVoll, Mon8Hardwaretest26) {
    const std::vector<uint8_t> tab{0xC3, 0x7D, 0x00, 0x21, 0x00, 0x01, 0xCF, 0x14, 0x10, 0x80, 0xBD,
                                   0x00, 0x20, 0x12, 0x40, 0x01, 0xCF, 0x05, 0x82, 0x87, 0xBB, 0x07};
    Rig r;
    r.dma.setZaehlerLiestBlocklaenge(true);
    r.dma.setReady(false);                            // P8000: RDY = DRQ (0) bei WR5 82 → aktiv
    for (int i = 0; i < 0x101; ++i) r.mem[0x2100 + i] = uint8_t(i * 3);
    // OTIR: nach 87 bekommt die DMA den Bus vor dem nächsten OUT
    for (size_t i = 0; i < tab.size(); ++i) {
        r.dma.ioWrite(0, tab[i]);
        while (r.dma.busRequest()) r.dma.step();
        r.dma.cpuZyklus();
    }
    EXPECT_NE(r.r() & 0x3B, 0);
    EXPECT_EQ(r.r(), 0x00);
    EXPECT_EQ(r.r(), 0x01);
    for (int i = 0; i < 0x101; ++i) ASSERT_EQ(r.mem[0x2000 + i], uint8_t(i * 3)) << i;
    // Teil 2: 0C0AH 13H Bytes + AB 87 BF
    std::vector<uint8_t> t2(tab.begin(), tab.begin() + 0x13);
    for (uint8_t b : {uint8_t(0xAB), uint8_t(0x87), uint8_t(0xBF)}) t2.push_back(b);
    for (uint8_t b : t2) { r.dma.ioWrite(0, b); while (r.dma.busRequest()) r.dma.step(); r.dma.cpuZyklus(); }
    ASSERT_TRUE(r.dma.hasInterrupt());
    EXPECT_EQ(r.dma.getVector(), 0x40);
    r.w({0xA3});
    r.dma.onRETI();
    EXPECT_FALSE(r.dma.hasInterrupt());
}

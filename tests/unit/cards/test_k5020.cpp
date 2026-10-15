/**
 * @file test_k5020.cpp
 * @brief AKB K5020 (Kassettenanschluss): je Leitung der Schnittstelle (RES/AWA/TRV/TRR/HGE/AUF/AEB/AZV/
 *        KSB/NIB/BMS/STA/ZF), Byteeingabe im Takt (12 und 6 kBit/s), Zeitfehler, Daisy-Chain —
 *        und die Init-Folge sowie das Bootband-Lesen des Lade-ROMs (zre.prn 027E ff.) als Gastcode
 *        auf einem echten Z80 (doc/kassette/k5020.md, treiber.md, AP K4).
 */

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

#include "core/cards/k2526/rom_data.h"
#include "core/cards/k5020/k5020.h"
#include "core/primitives/z80.h"

using namespace kassette;

namespace {

constexpr uint32_t HZ = 2'457'600;
constexpr Seite A = Seite::A;

// Portadressen der Karte (A5120: 30H)
constexpr uint8_t DA = 0x30, DB = 0x31, DAC = 0x32, DBC = 0x33;   // Daten-PIO
constexpr uint8_t SA = 0x34, SB = 0x35, SAC = 0x36, SBC = 0x37;   // Steuer-PIO

std::vector<uint8_t> daten(size_t n, uint8_t start = 0) {
    std::vector<uint8_t> d(n);
    for (size_t i = 0; i < n; ++i) d[i] = static_cast<uint8_t>(start + i);
    return d;
}

std::shared_ptr<KassettenMedium> dreiBloecke() {
    auto m = std::make_shared<KassettenMedium>();
    m->haengeAn(A, m->geometrie().initialluecke_um, blockBauen(daten(100, 0)));
    m->haengeAn(A, 20'300, blockBauen(daten(100, 100)));
    m->haengeAn(A, 20'300, blockBauen(daten(100, 200)));
    return m;
}

struct K5020Test : ::testing::Test {
    K1520Bus bus;
    std::unique_ptr<K5020> kp;
    K5020& k() { return *kp; }

    void SetUp() override { neu({}); }
    void neu(K5020::Config c) {
        bus = K1520Bus{};
        kp = std::make_unique<K5020>(c);
        k().attachToBus(bus);
        bus.setInterruptChain({&k().pioDaten(), &k().pioSteuer()});
        k().reset();
    }
    void takte(long n) {
        while (n > 0) {
            if (k().clockTick(100)) {
                bus.markIntDirty();
                bus.updateInterruptChain();
            }
            n -= 100;
        }
    }
    void sek(double s) { takte(static_cast<long>(s * HZ)); }

    /// Init der Steuer-PIO wie das Lade-ROM (zre.prn 0298–02B5): Tor A Bit 7 Eingang, Tor B nur Bit 1 Ausgang.
    void steuerInit() {
        for (uint8_t p : {SAC, SBC}) bus.ioWrite(p, 0x7F);
        bus.ioWrite(SA, 0x03);
        bus.ioWrite(SB, 0x04);
        for (uint8_t v : {0xFF, 0xFD}) bus.ioWrite(SBC, v);
        for (uint8_t v : {0xFF, 0x80}) bus.ioWrite(SAC, v);
    }
    /// Gerät 1 reservieren und anwählen, bereit abwarten.
    void geraet1Bereit(std::shared_ptr<KassettenMedium> m, double pos_mm = 30'000) {
        k().laufwerk(0).kassetteEinlegen(std::move(m));
        k().laufwerk(0).positionSetzen(static_cast<int64_t>(pos_mm * 1000));
        steuerInit();
        bus.ioWrite(SA, 0x09);
        sek(0.31);
    }
    int a() { return bus.ioRead(SA); }
    int b() { return bus.ioRead(SB); }
    int interruptQuittieren() {
        bus.updateInterruptChain();
        if (!bus.isINT()) return -1;
        const int v = bus.interruptAcknowledge();
        bus.signalRETI();
        return v;
    }
};

}  // namespace

// ─── Steuer-PIO: Ausgänge (RES / AWA / TRV / TRR / HGE / AUF) ───────────────

TEST_F(K5020Test, ResUndAwaFolgenDerFunktionstabelle) {
    steuerInit();
    auto& l1 = k().laufwerk(0);
    auto& l2 = k().laufwerk(1);
    EXPECT_FALSE(l1.reserviert());
    bus.ioWrite(SA, 0x01);   // KMBG 1 reserviert
    EXPECT_TRUE(l1.reserviert());
    EXPECT_FALSE(l2.reserviert());
    EXPECT_FALSE(l1.angewaehlt());
    bus.ioWrite(SA, 0x09);   // reserviert + angewählt
    EXPECT_TRUE(l1.reserviert());
    EXPECT_TRUE(l1.angewaehlt());
    EXPECT_EQ(k().angewaehlt(), 0);
    bus.ioWrite(SA, 0x06);   // KMBG 2 angewählt, KMBG 1 nicht reserviert
    EXPECT_FALSE(l1.reserviert());
    EXPECT_FALSE(l1.angewaehlt());
    EXPECT_TRUE(l2.angewaehlt());
    EXPECT_TRUE(l2.reserviert()) << "06: Bit 0 = 0 = RES2";
}

// AP K7: die Rückleitungen gelten auch dann, wenn sie sich seit dem Reset nicht geändert haben.
// Beim Reset steht die Steuer-PIO noch nicht im Modus 3, ihr Eingaberegister übernimmt die Pegel
// erst nach dem Steuerwort — sonst liest das Lade-ROM FFH und hält ein LEERES Gerät 1 für bereit
// (/NIB1 = 1) und Magnetband unter der Lichtschranke; es spult dort endlos statt Gerät 2 zu nehmen.
TEST_F(K5020Test, LeeresGeraetIstNachDerRomInitNichtBereit) {
    steuerInit();
    bus.ioWrite(SA, 0x09);                 // Gerät 1 reserviert + angewählt (ROM 02C9, E = 09H)
    EXPECT_EQ(b() & 0x18, 0) << "/NIB1 und /NIB2 = 0: kein Gerät hat eine Kassette";
    EXPECT_EQ(b() & 0x04, 0) << "KSB";
    bus.ioWrite(SA, 0x06);                 // Gerät 2 (E = 06H)
    EXPECT_EQ(b() & 0x18, 0);
}

TEST_F(K5020Test, Res2UndBeideReserviert) {
    steuerInit();
    bus.ioWrite(SA, 0x02);   // KMBG 2 reserviert (Bit 0 = 0), KMBG 1 nicht (Bit 1 = 1)
    EXPECT_FALSE(k().laufwerk(0).reserviert());
    EXPECT_TRUE(k().laufwerk(1).reserviert());
    bus.ioWrite(SA, 0x00);   // beide reserviert, beide Auswahlbits 0 = RSI
    EXPECT_TRUE(k().laufwerk(0).reserviert());
    EXPECT_TRUE(k().laufwerk(1).reserviert());
    EXPECT_EQ(k().angewaehlt(), -1);
    bus.ioWrite(SA, 0x03);   // inaktiv
    EXPECT_FALSE(k().laufwerk(0).reserviert());
    EXPECT_FALSE(k().laufwerk(1).reserviert());
}

TEST_F(K5020Test, TransportNurAmAngewaehltenGeraet) {
    geraet1Bereit(dreiBloecke());
    k().laufwerk(1).kassetteEinlegen(dreiBloecke());
    auto& l1 = k().laufwerk(0);
    bus.ioWrite(SA, 0x19);   // vorwärts
    sek(0.1);
    EXPECT_NEAR(l1.geschwindigkeitMmS(), 381.0, 0.5);
    EXPECT_EQ(k().laufwerk(1).geschwindigkeitMmS(), 0.0);
    bus.ioWrite(SA, 0x29);   // rückwärts (TRR)
    sek(0.3);
    EXPECT_NEAR(l1.geschwindigkeitMmS(), -381.0, 0.5);
    bus.ioWrite(SA, 0x01);   // nur reserviert: Transportbits wirken ohne Auswahl nicht
    bus.ioWrite(SA, 0x11);   // TRV ohne AWA
    sek(0.3);
    EXPECT_EQ(l1.geschwindigkeitMmS(), 0.0);
}

TEST_F(K5020Test, HgeMitTransportIstSuchlauf) {
    geraet1Bereit(dreiBloecke());
    bus.ioWrite(SA, 0x59);   // vorwärts, hohe Geschwindigkeit
    sek(0.5);
    EXPECT_NEAR(k().laufwerk(0).geschwindigkeitMmS(), 1500.0, 1.0);
    bus.ioWrite(SA, 0x69);   // rückwärts, hohe Geschwindigkeit
    sek(1.0);
    EXPECT_NEAR(k().laufwerk(0).geschwindigkeitMmS(), -1500.0, 1.0);
}

TEST_F(K5020Test, TrvPlusTrrIstUmspulenUndNibFaelltWaehrenddessen) {
    geraet1Bereit(dreiBloecke(), 5'000);
    ASSERT_TRUE(b() & 0x10) << "bereit";
    bus.ioWrite(SA, 0x39);   // UMS
    bus.ioWrite(SA, 0x09);   // ROM nimmt UMS eine Instruktion später zurück (0320/0322): das Gerät merkt es sich
    EXPECT_TRUE(k().laufwerk(0).umspulenAktiv());
    sek(0.05);
    EXPECT_FALSE(b() & 0x10) << "/NIB = 0 beim Umspulen";
    sek(8.0);
    EXPECT_FALSE(k().laufwerk(0).umspulenAktiv());
    EXPECT_TRUE(b() & 0x10);
    EXPECT_FALSE(a() & 0x80) << "am Bandanfang unter der Lichtschranke: Vorspann";
}

TEST_F(K5020Test, AufNurBeiBeschreibbarerKassetteSonstStaEins) {
    auto m = dreiBloecke();
    m->setSchreibschutz(A, true);
    geraet1Bereit(m);
    EXPECT_FALSE(b() & 0x40) << "STA = 0 ohne AUF";
    bus.ioWrite(SB, 0x02);   // AUF
    EXPECT_TRUE(b() & 0x40) << "AVE zusammen mit AUF -> STA = 1, sofort bei stehendem Band";
    EXPECT_FALSE(k().laufwerk(0).bewegt());
    bus.ioWrite(SB, 0x00);
    EXPECT_FALSE(b() & 0x40);
    // beschreibbar: kein STA, AUF kommt am Gerät an
    m->setSchreibschutz(A, false);
    bus.ioWrite(SB, 0x02);
    EXPECT_FALSE(b() & 0x40);
}

// ─── Steuer-PIO: Eingänge (AEB / AZV / KSB / NIB / BMS) ─────────────────────

TEST_F(K5020Test, AebFolgtDemAngewaehltenGeraetMitBitSiebenEinsBeiMagnetband) {
    geraet1Bereit(dreiBloecke(), 500);   // Vorspann
    auto& l = k().laufwerk(0);
    EXPECT_TRUE(l.aebAktiv());
    EXPECT_EQ(a() & 0x80, 0) << "Vorspann: Bit 7 = 0 (treiber.md §3.1)";
    bool gesehen_band = false, gesehen_vorspann = false, gesehen_marke = false;
    const int64_t bot = static_cast<int64_t>(l.medium()->geometrie().vorspann_um) +
                        l.medium()->geometrie().marken_abstand_um;
    for (int64_t um = 100'000; um < 3'000'000; um += 5'000) {
        l.positionSetzen(um);
        const bool pin = a() & 0x80;
        EXPECT_EQ(pin, !l.aebAktiv()) << "um=" << um;
        if (pin) gesehen_band = true; else gesehen_vorspann = true;
        if (!pin && um > static_cast<int64_t>(l.medium()->geometrie().vorspann_um) + 100'000 && um < bot + 100'000) gesehen_marke = true;
    }
    EXPECT_TRUE(gesehen_band);
    EXPECT_TRUE(gesehen_vorspann);
    EXPECT_TRUE(gesehen_marke) << "BOT-Marke zeigt Bit 7 = 0 mitten im Magnetband";
    // Ohne Auswahl: Bit 7 = 1
    bus.ioWrite(SA, 0x03);
    EXPECT_NE(a() & 0x80, 0);
}

TEST_F(K5020Test, AzvKsbUndNibJeGeraet) {
    K5020::Config c;
    c.profil2 = ProfilId::K5200_19;
    neu(c);
    steuerInit();
    auto m1 = dreiBloecke();
    k().laufwerk(0).kassetteEinlegen(m1);
    k().laufwerk(1).kassetteEinlegen(dreiBloecke());
    bus.ioWrite(SA, 0x09);   // Gerät 1: reserviert (Bit 1 = 0), Bit 0 = 1 -> Gerät 2 nicht
    EXPECT_EQ(b() & 0x01, 0) << "AZV 0 = 38 cm/s";
    bus.ioWrite(SA, 0x06);
    EXPECT_NE(b() & 0x01, 0) << "AZV 1 = 19 cm/s am Gerät 2";
    // NIB
    bus.ioWrite(SA, 0x00);
    EXPECT_EQ(b() & 0x18, 0) << "noch nicht angelaufen";
    sek(0.31);
    EXPECT_EQ(b() & 0x18, 0x18) << "/NIB1 und /NIB2: 1 = bereit";
    bus.ioWrite(SA, 0x02);   // nur Gerät 2 reserviert
    sek(0.01);
    EXPECT_EQ(b() & 0x18, 0x08);
    // KSB
    bus.ioWrite(SA, 0x09);
    EXPECT_EQ(b() & 0x04, 0);
    k().laufwerk(0).reservieren(false);
    EXPECT_TRUE(k().laufwerk(0).wenden());
    EXPECT_NE(b() & 0x04, 0) << "Seite B";
}

TEST_F(K5020Test, BmsFolgtDemBlockUndHaeltNochEtwa108Mikrosekunden) {
    auto m = dreiBloecke();
    geraet1Bereit(m, 0);
    k().laufwerk(0).positionSetzen(m->blockStart(A, 0) - 30'000);
    bus.ioWrite(SA, 0x19);
    bool war_an = false;
    long letzte_bms = -1, erste_aus = -1, t = 0;
    for (; t < static_cast<long>(0.2 * HZ); t += 64) {
        k().clockTick(64);
        const bool dev = k().laufwerk(0).bms();
        const bool pin = b() & 0x20;
        if (dev) letzte_bms = t;
        if (pin) war_an = true;
        if (war_an && !pin && erste_aus < 0) erste_aus = t;
        if (dev) EXPECT_TRUE(pin) << "BMS-Pin folgt dem Block sofort";
    }
    ASSERT_TRUE(war_an);
    ASSERT_GE(erste_aus, 0);
    const double halte_us = (erste_aus - letzte_bms) * 1e6 / HZ;
    EXPECT_GE(halte_us, 100.0);
    EXPECT_LE(halte_us, 180.0);
}

// ─── Byteausgabe / Zeitfehler ────────────────────────────────────────────────

namespace {
/// Aufnahmebereit: AUF zuerst, dann VOR, 60 ms Vorlücke (BD S.13).
void aufnahmeStarten(K5020Test& t) {
    t.bus.ioWrite(SB, 0x02);
    t.bus.ioWrite(SA, 0x19);
    t.sek(0.06);
}
}  // namespace

TEST_F(K5020Test, ZeitfehlerBeiZuLangsamemSchreibenZwoelfUndSechsKBit) {
    for (ProfilId p : {ProfilId::K5200_38, ProfilId::K5200_19}) {
        K5020::Config c;
        c.profil1 = p;
        neu(c);
        auto m = std::make_shared<KassettenMedium>();
        geraet1Bereit(m);
        for (uint8_t v : {0x0F}) bus.ioWrite(DAC, v);    // Betriebsart 0
        aufnahmeStarten(*this);
        const double bz = profil(p).byteZeitS();
        bus.ioWrite(DA, 0xAA);
        sek(bz * 0.8);
        EXPECT_EQ(b() & 0x80, 0) << profil(p).name << ": nach 0,8 Bytezeiten noch kein ZF";
        sek(bz * 0.4);
        EXPECT_NE(b() & 0x80, 0) << profil(p).name << ": kein neues Zeichen -> ZF";
        bus.ioWrite(DA, 0x55);   // nächste Übernahme löscht ZF
        EXPECT_EQ(b() & 0x80, 0);
    }
}

TEST_F(K5020Test, ByteausgabeImTaktEinBlockMitEchoOhneZeitfehler) {
    for (ProfilId p : {ProfilId::K5200_38, ProfilId::K5200_19}) {
        K5020::Config c;
        c.profil1 = p;
        neu(c);
        auto m = std::make_shared<KassettenMedium>();
        geraet1Bereit(m);
        bus.ioWrite(DAC, 0x0F);                          // Daten-PIO A: Ausgabe
        for (uint8_t v : {0x4F}) bus.ioWrite(DBC, v);    // Daten-PIO B: Eingabe
        bus.ioRead(DB);                                  // Blindeingabe: BRDY
        aufnahmeStarten(*this);
        const std::vector<uint8_t> blk = blockBauen(daten(40, 7));
        std::vector<uint8_t> echo;
        size_t n = 0;
        bool zf = false;
        // Das Schreiben ist an die Bytezeit gebunden: nachlegen, sobald die Karte übernommen hat (ARDY aus).
        for (long t = 0; t < static_cast<long>(0.4 * HZ) && echo.size() < blk.size(); t += 100) {
            k().clockTick(100);
            if (n < blk.size() && !k().pioDaten().ardy()) bus.ioWrite(DA, blk[n++]);
            if (!k().pioDaten().brdy()) echo.push_back(bus.ioRead(DB));
            if (n < blk.size() && (b() & 0x80)) zf = true;
        }
        EXPECT_FALSE(zf) << profil(p).name;
        EXPECT_EQ(n, blk.size());
        EXPECT_EQ(echo, blk) << profil(p).name << ": Lese-nach-Schreiben liefert Präambel bis Postambel";
        sek(0.1);
        EXPECT_EQ(k().verloreneBytes(), 0u);
        bus.ioWrite(SA, 0x09);   // VOR aus
        bus.ioWrite(SB, 0x00);
        sek(0.2);
        k().laufwerk(0).positionSetzen(0);
        ASSERT_EQ(m->anzahlBloecke(A), 1u) << profil(p).name;
        EXPECT_EQ(m->block(A, 0).bytes, blk);
    }
}

// AP M7 (MC80.30, umgebaute K5020): Schreibtakt über den Koppelbus von außen.  Ohne Takt
// übernimmt das Schieberegister kein Byte (ARDY bleibt, kein ZF, nichts aufs Band); sobald er
// anliegt, geht das wartende Byte sofort und die übrigen im Takt des Profils (LW 1200).
TEST_F(K5020Test, ExternerTaktSperrtUndGibtDieAusgabeFrei) {
    K5020::Config c;
    c.takt = K5020::Takt::ExternerCtc;
    c.profil1 = ProfilId::LW1200;
    neu(c);
    bool takt = false;
    EXPECT_TRUE(k().taktLiegtAn()) << "ohne Rückruf gilt der Takt als vorhanden";
    k().setExternerTakt([&takt] { return takt; });
    EXPECT_FALSE(k().taktLiegtAn());
    auto m = std::make_shared<KassettenMedium>();
    geraet1Bereit(m);
    bus.ioWrite(DAC, 0x0F);
    aufnahmeStarten(*this);
    const double bz = profil(ProfilId::LW1200).byteZeitS();
    bus.ioWrite(DA, 0xAA);
    sek(bz * 3);
    EXPECT_TRUE(k().pioDaten().ardy()) << "ohne Takt bleibt das Byte im Ausgaberegister";
    EXPECT_EQ(b() & 0x80, 0) << "ohne Takt kein Zeitfehler";
    takt = true;
    sek(0.001);
    EXPECT_FALSE(k().pioDaten().ardy()) << "Takt da: Byte übernommen";
    bus.ioWrite(DA, 0x55);
    sek(bz * 0.8);
    EXPECT_TRUE(k().pioDaten().ardy()) << "nächstes Byte erst nach einer Bytezeit (LW 1200)";
    sek(bz * 0.4);
    EXPECT_FALSE(k().pioDaten().ardy());
}

TEST_F(K5020Test, BustaktFragtDenExternenTaktNicht) {
    k().setExternerTakt([] { return false; });
    EXPECT_TRUE(k().taktLiegtAn());
}

TEST_F(K5020Test, HgeNachAufSperrtDasEchoOhneSuchlauf) {
    auto m = std::make_shared<KassettenMedium>();
    geraet1Bereit(m);
    bus.ioWrite(DAC, 0x0F);
    bus.ioWrite(DBC, 0x4F);
    bus.ioRead(DB);
    aufnahmeStarten(*this);
    bus.ioWrite(SA, 0x59);   // HGE nach AUF
    sek(0.05);
    EXPECT_NEAR(k().laufwerk(0).geschwindigkeitMmS(), 381.0, 0.5) << "kein Suchlauf";
    const std::vector<uint8_t> blk = blockBauen(daten(10));
    size_t n = 0;
    int echo = 0;
    for (long t = 0; t < static_cast<long>(0.2 * HZ); t += 100) {
        k().clockTick(100);
        if (n < blk.size() && !k().pioDaten().ardy()) bus.ioWrite(DA, blk[n++]);
        if (!k().pioDaten().brdy()) { bus.ioRead(DB); ++echo; }
    }
    EXPECT_EQ(n, blk.size());
    EXPECT_EQ(echo, 0) << "Aufzeichnen ohne Read-after-write (BD S.9)";
}

// ─── Byteeingabe ─────────────────────────────────────────────────────────────

TEST_F(K5020Test, EingabeMitBlindeingabeUndVerlustOhneBrdy) {
    auto m = dreiBloecke();
    geraet1Bereit(m, 0);
    bus.ioWrite(DBC, 0x4F);   // Eingabe, aber noch keine Blindeingabe: BRDY aus
    k().laufwerk(0).positionSetzen(m->blockStart(A, 0) - 30'000);
    bus.ioWrite(SA, 0x19);
    sek(0.12);
    EXPECT_GT(k().verloreneBytes(), 0u) << "ohne Blindeingabe nimmt die PIO nichts an";
    bus.ioWrite(SA, 0x09);
    sek(0.1);

    // Mit Blindeingabe: Block 1 vollständig (Präambel + 100 + CRC + Postambel)
    const uint64_t v0 = k().verloreneBytes();
    k().laufwerk(0).positionSetzen(m->blockStart(A, 1) - 30'000);
    bus.ioRead(DB);
    bus.ioWrite(SA, 0x19);
    std::vector<uint8_t> got;
    for (long t = 0; t < static_cast<long>(0.3 * HZ) && got.size() < m->block(A, 1).bytes.size(); t += 100) {
        k().clockTick(100);
        if (!k().pioDaten().brdy()) got.push_back(bus.ioRead(DB));
    }
    EXPECT_EQ(k().verloreneBytes(), v0);
    EXPECT_EQ(got.size(), m->block(A, 1).bytes.size());
    EXPECT_EQ(got, m->block(A, 1).bytes);
}

// ─── Interrupts / Daisy-Chain ────────────────────────────────────────────────

TEST_F(K5020Test, DatenPioHatVorrangVorDerSteuerPio) {
    steuerInit();
    // Steuer-PIO A: Vektor E0H, Interrupt wenn AEB (Bit 7) high wird (Vorspann verlassen)
    bus.ioWrite(SAC, 0xE0);
    for (uint8_t v : {0xFF, 0x80, 0xB7, 0x7F}) bus.ioWrite(SAC, v);
    // Daten-PIO A: Vektor F2H, Ausgabe, Interrupt ein
    for (uint8_t v : {0xF2, 0x0F, 0x83}) bus.ioWrite(DAC, v);

    auto m = dreiBloecke();
    k().laufwerk(0).kassetteEinlegen(m);
    k().laufwerk(0).positionSetzen(990'000 - 100'000);   // kurz vor Ende des Vorspanns
    bus.ioWrite(SA, 0x09);
    sek(0.31);
    ASSERT_EQ(a() & 0x80, 0);
    EXPECT_EQ(interruptQuittieren(), -1);
    bus.ioWrite(SA, 0x19);
    bool irq = false;
    for (int i = 0; i < 100 && !irq; ++i) {
        sek(0.02);
        bus.updateInterruptChain();
        irq = bus.isINT();
    }
    ASSERT_TRUE(irq) << "AEB 0 -> 1";
    // Zusätzlich fordert die Daten-PIO ein Byte an (Scheinausgabe, FEX routins.mac): sie hat Vorrang.
    bus.ioWrite(DA, 0x00);
    EXPECT_EQ(interruptQuittieren(), 0xF2);
    EXPECT_EQ(interruptQuittieren(), 0xE0);
    EXPECT_EQ(interruptQuittieren(), -1);
}

TEST_F(K5020Test, ResetLaesstDieBaenderLiegenUndLaesstAlleLeitungenFallen) {
    geraet1Bereit(dreiBloecke());
    bus.ioWrite(SA, 0x19);
    sek(0.1);
    ASSERT_TRUE(k().laufwerk(0).bewegt());
    k().reset();
    EXPECT_FALSE(k().laufwerk(0).reserviert());
    EXPECT_FALSE(k().laufwerk(0).angewaehlt());
    sek(0.3);
    EXPECT_FALSE(k().laufwerk(0).bewegt());
    EXPECT_TRUE(k().laufwerk(0).kassetteGeladen());
    EXPECT_EQ(k().angewaehlt(), -1);
}

// ─── Lade-ROM als Gastcode (zre.prn 027E ff.) ────────────────────────────────

namespace {

/// Z80 mit dem echten Boot-ROM (0000–03FF) und RAM darüber; Kassettenkarte auf 30H.
struct RomRig {
    K1520Bus bus;
    K5020 karte;
    Z80 cpu;
    std::array<uint8_t, 65536> ram{};
    uint64_t takte = 0;

    RomRig() {
        karte.attachToBus(bus);
        bus.setInterruptChain({&karte.pioDaten(), &karte.pioSteuer()});
        karte.reset();
        for (size_t i = 0; i < 1024; ++i) ram[i] = ZRE_BOOT_ROM[i];
        cpu.readByte  = [this](uint16_t ad) { return ram[ad]; };
        cpu.writeByte = [this](uint16_t ad, uint8_t d) { if (ad >= 0x03F0) ram[ad] = d; };  // darunter liegt das ROM
        cpu.readPort  = [this](uint16_t p) { return bus.ioRead(static_cast<uint8_t>(p)); };
        cpu.writePort = [this](uint16_t p, uint8_t d) { bus.ioWrite(static_cast<uint8_t>(p), d); };
        cpu.retiCallback = [this] { bus.signalRETI(); };
        cpu.reset();
        cpu.PC = 0x027E;
        cpu.SP = 0xF000;
        cpu.IM = 2;
        cpu.IFF1 = cpu.IFF2 = true;
    }
    /// Bis @p ziel_pc (erstes Erreichen) oder @p hoechstens Instruktionen; true, wenn erreicht.
    bool laufeBis(uint16_t ziel_pc, long hoechstens) {
        for (long i = 0; i < hoechstens; ++i) {
            if (cpu.PC == ziel_pc) return true;
            bus.markIntDirty();
            bus.updateInterruptChain();
            if (bus.isINT() && cpu.IFF1) cpu.interrupt(static_cast<uint8_t>(bus.interruptAcknowledge()));
            const int used = cpu.step();
            takte += static_cast<uint64_t>(used);
            if (karte.clockTick(used)) { bus.markIntDirty(); bus.updateInterruptChain(); }
        }
        return cpu.PC == ziel_pc;
    }
};

}  // namespace

TEST(K5020Rom, InitFolgeDesLadeRomsStelltDieKarteWieBeschriebenEin) {
    RomRig r;
    auto m = dreiBloecke();
    r.karte.laufwerk(0).kassetteEinlegen(m);
    r.karte.laufwerk(0).positionSetzen(900'000);   // im Vorspann nahe dem Magnetband
    ASSERT_TRUE(r.laufeBis(0x02F9, 20'000'000)) << "PC=" << std::hex << r.cpu.PC << " (03B0 = HALT-Schleife)";
    // 0298–02B5: beide Steuer-PIO-Tore in Betriebsart 3, Tor A Bit 7 Eingang, Tor B nur Bit 1 Ausgang
    const auto sa = r.karte.pioSteuer().debugState().port[0];
    const auto sb = r.karte.pioSteuer().debugState().port[1];
    EXPECT_EQ(sa.mode, 3);
    EXPECT_EQ(sa.dir, 0x80);
    EXPECT_EQ(sb.mode, 3);
    EXPECT_EQ(sb.dir, 0xFD);
    // 02BE: E = 09H -> Gerät 1 reserviert und angewählt; 02DC: /NIB1 lag an (sonst Sprung nach 0397)
    EXPECT_EQ(r.cpu.E, 0x09);
    EXPECT_TRUE(r.karte.laufwerk(0).reserviert());
    EXPECT_TRUE(r.karte.laufwerk(0).angewaehlt());
    EXPECT_FALSE(r.karte.laufwerk(1).reserviert());
    EXPECT_NE(r.cpu.PC, 0x03B0);
    // Daten-PIO ist noch unberührt (erst 0357 ff.)
    EXPECT_EQ(r.karte.pioDaten().debugState().port[1].ie, false);
}

TEST(K5020Rom, LiestEinBootbandMitVierBloeckenUndPruefSyl) {
    RomRig r;
    auto m = std::make_shared<KassettenMedium>();
    std::vector<uint8_t> bootdaten;
    for (int blk = 0; blk < 4; ++blk) {
        std::vector<uint8_t> d(128);
        for (int i = 0; i < 128; ++i) d[i] = static_cast<uint8_t>(0x10 * blk + i * 7 + 1);
        if (blk == 0) { d[0] = 'S'; d[1] = 'Y'; d[2] = 'L'; }   // Signatur 01B6: [0400..0402] = „SYL“
        bootdaten.insert(bootdaten.end(), d.begin(), d.end());
        m->haengeAn(A, blk == 0 ? m->geometrie().initialluecke_um : 20'300, blockBauen(d));
    }
    r.karte.laufwerk(0).kassetteEinlegen(m);
    r.karte.laufwerk(0).positionSetzen(900'000);
    // 0434 = Bootsektor-Code ausführen, nachdem die Signatur stimmte (0388–038D)
    ASSERT_TRUE(r.laufeBis(0x0434, 120'000'000)) << "PC=" << std::hex << r.cpu.PC;
    for (size_t i = 0; i < bootdaten.size(); ++i)
        ASSERT_EQ(r.ram[0x400 + i], bootdaten[i]) << "Byte " << i;
    EXPECT_EQ(r.karte.verloreneBytes(), 0u);
}

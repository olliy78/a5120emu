/**
 * @file test_p8000_wdc.cpp
 * @brief WDC des P8000 (AP P13c): Original-Firmware auf UA880 + CTC, Hostschnittstelle mit einem
 *        Stub-Host, Disk-Schnittstelle gegen eine TempPlatte — Start (ohne Fehler 20), Init-Fehler,
 *        Lesen/Schreiben/Formatieren (Inhalt im Abbild geprüft), Fehlercodes, RAM-Kommandos, RST,
 *        41,4-MHz-Bestückung, ältere Firmware, Save-State.  Quelle: core/cards/p8000/wdc.h,
 *        doc/p8000/wdc_firmware.md (Kommandos §4.3, Fehler §5, Init §6, PAR §7, Spur §8).
 */
#include "core/cards/p8000/rom_wdc.h"
#include "core/cards/p8000/wdc.h"
#include "tests/support/temp_platte.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>

using k1520::winchester::Platte;
using k1520test::TempPlatte;

namespace {

/// Host-Seite wie MON16 (`p.disk.s`) und der WEGA-Treiber: wartet auf den Status, schickt die
/// 9 Kommandobytes, liest nach einem Scheinbyte bzw. schreibt Daten Byte für Byte über ARDY/ASTB.
/// Zwischen zwei Bytes vergehen `takte_je_byte` WDC-Takte (INIR/OTIR des Hosts) — ein unendlich
/// schneller Host ließe der Firmware keine Zeit für den Zwischeninterrupt `isr_h1`.
struct StubHost {
    P8000Wdc& w;
    bool ardy = false;
    uint8_t latch = 0;
    int strobes = 0;
    int takte_je_byte = 21;

    explicit StubHost(P8000Wdc& wdc) : w(wdc)
    {
        w.setzeAstbRueckruf([this] {
            ++strobes;
            if (w.tr()) latch = w.datenZumHost();
            ardy = false;
            w.setzeArdy(false);
        });
    }
    void lauf(uint64_t n) { w.laufeBis(w.takte() + n); }
    bool warteBis(const std::function<bool()>& f, uint64_t max = 400'000'000)
    {
        const uint64_t ende = w.takte() + max;
        while (w.takte() < ende) {
            if (f()) return true;
            lauf(500);
        }
        return f();
    }
    /// Status @p s mit bereitgestellter Übertragung (HEN-FF gesetzt).
    bool warteStatus(uint8_t s, uint64_t max = 400'000'000)
    {
        return warteBis([&] { return w.status() == s && w.uebertragungAktiv(); }, max);
    }
    bool sende(uint8_t b)
    {
        lauf(uint64_t(takte_je_byte));
        w.setzeTe(false);
        w.setzeHostbus(b);
        ardy = true;
        w.setzeArdy(true);
        return warteBis([this] { return !ardy; }, 4'000'000);
    }
    bool sende(const std::vector<uint8_t>& d)
    {
        for (uint8_t b : d)
            if (!sende(b)) return false;
        return true;
    }
    std::optional<std::vector<uint8_t>> empfange(size_t n)
    {
        std::vector<uint8_t> d;
        w.setzeTe(true);
        ardy = true;
        w.setzeArdy(true);                       // Scheineingabe: PIO-Ready an
        for (size_t i = 0; i < n; ++i) {
            if (!warteBis([this] { return !ardy; }, 4'000'000)) return std::nullopt;
            d.push_back(latch);
            lauf(uint64_t(takte_je_byte));
            ardy = true;
            w.setzeArdy(true);                   // Host hat das Byte abgeholt
        }
        warteBis([this] { return !ardy; }, 400'000);   // Byte „zur Ready-Abschaltung"
        w.setzeTe(false);
        return d;
    }

    struct Antwort {
        int fehler = -1;                 ///< 0 = Status 1 danach, > 0 = Fehlerbyte, < 0 = Zeitüberlauf
        std::vector<uint8_t> daten;
    };
    Antwort ausfuehren(const std::vector<uint8_t>& k, size_t lesen = 0,
                       const std::vector<uint8_t>* schreiben = nullptr, uint64_t max = 400'000'000)
    {
        Antwort a;
        if (!warteStatus(1, max) || !sende(k)) return a;
        auto bereit = [&](std::initializer_list<int> st) {
            return warteBis([&] {
                if (!w.uebertragungAktiv()) return false;
                for (int s : st) if (w.status() == s) return true;
                return false;
            }, max);
        };
        if (schreiben) {
            if (!bereit({2, 7})) return a;
            if (w.status() == 2 && !sende(*schreiben)) return a;
        }
        if (!bereit({1, 3, 7})) return a;
        if (w.status() == 3) {
            auto d = empfange(lesen);
            if (!d) return a;
            a.daten = *d;
            if (!bereit({1, 7})) return a;
        }
        if (w.status() == 7) {
            auto e = empfange(1);
            a.fehler = e ? (*e)[0] : -2;
            return a;
        }
        a.fehler = 0;
        return a;
    }
};

std::vector<uint8_t> sektorKommando(uint8_t code, int lw, int zyl, int kopf, int sek, int len = 512)
{
    return {code, uint8_t(lw), uint8_t(zyl), uint8_t(zyl >> 8), uint8_t(kopf), uint8_t(sek),
            uint8_t(len), uint8_t(len >> 8), 0};
}
std::vector<uint8_t> blockKommando(uint8_t code, int lw, uint32_t block, int len = 512)
{
    return {code, uint8_t(lw), uint8_t(block), uint8_t(block >> 8), uint8_t(block >> 16), uint8_t(block >> 24),
            uint8_t(len), uint8_t(len >> 8), 0};
}
std::vector<uint8_t> ramKommando(uint8_t code, uint16_t adr, int len)
{
    return {code, uint8_t(adr), uint8_t(adr >> 8), 0, 0, 0, uint8_t(len), uint8_t(len >> 8), 0};
}

std::array<uint8_t, 512> muster(int n)
{
    std::array<uint8_t, 512> d{};
    for (int i = 0; i < 512; ++i) d[size_t(i)] = static_cast<uint8_t>(i * 13 + n * 7 + (i >> 8));
    return d;
}

/// Block b → (Zylinder, Kopf, Sektor) für 5 Köpfe × 18 Sektoren (FW Z. 1461–1506, ohne BTT).
std::array<int, 3> blockLage(uint32_t b)
{
    return {int(b / 90 + 1), int((b % 90) / 18), int(b % 18 + 1)};
}

struct Aufbau {
    TempPlatte tp;
    Platte platte;
    P8000Wdc wdc;
    StubHost host{wdc};
    explicit Aufbau(P8000Wdc::Config cfg = {}, Platte::Config pc = {},
                    const std::function<void(Platte&)>& vorStart = {})
        : tp("K5504.50", "wdc.img"), wdc(cfg)
    {
        EXPECT_TRUE(platte.oeffnen(tp, pc)) << platte.fehler();
        if (vorStart) vorStart(platte);
        wdc.anschliessen(0, &platte);
    }
};

}  // namespace

// ─── Abzüge ──────────────────────────────────────────────────────────────────

TEST(P8000WdcRom, HeaderGleichAbzug)
{
    auto lies = [](const std::string& n) {
        std::ifstream f(std::string(P8000_ABZUG_DIR) + "/WDC/" + n, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
    };
    auto paar = [&](const std::string& v) {
        auto a = lies("WDC_1_" + v), b = lies("WDC_2_" + v);
        a.insert(a.end(), b.begin(), b.end());
        return a;
    };
    EXPECT_EQ(paar("4.2"), std::vector<uint8_t>(std::begin(P8K_WDC_4_2), std::end(P8K_WDC_4_2)));
    EXPECT_EQ(paar("4.0_05"), std::vector<uint8_t>(std::begin(P8K_WDC_4_0_05), std::end(P8K_WDC_4_0_05)));
    EXPECT_EQ(lies("WDC_1_3.4_05"), std::vector<uint8_t>(std::begin(P8K_WDC_3_4_05), std::end(P8K_WDC_3_4_05)));
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(P8K_WDC_4_2) + 2, 8), "WDC_4.2 ");
}

TEST(P8000WdcRom, Firmware42PruefsummenWieCrcBr)
{
    // crc_br: CRC-CCITT, Start FFFF, über 0000–0FFF bzw. 1000–1FF7; abgelegt byteweise vertauscht
    auto crc = [](const uint8_t* d, size_t n) {
        uint16_t c = 0xFFFF;
        for (size_t i = 0; i < n; ++i) c = k1520::winchester::crcCcitt(c, d[i]);
        return static_cast<uint16_t>((c >> 8) | (c << 8));
    };
    auto wort = [](size_t o) { return uint16_t(P8K_WDC_4_2[o] | (P8K_WDC_4_2[o + 1] << 8)); };
    EXPECT_EQ(wort(0x1FF8), crc(P8K_WDC_4_2, 0x1000));
    EXPECT_EQ(wort(0x1FFC), crc(P8K_WDC_4_2 + 0x1000, 0xFF8));
    EXPECT_EQ(uint16_t(wort(0x1FF8) + wort(0x1FFA) + 1), 0);
    EXPECT_EQ(uint16_t(wort(0x1FFC) + wort(0x1FFE) + 1), 0);
}

TEST(P8000WdcRom, KommandosucheLaeuftImAbzugUeberDasTabellenende)
{
    // wdc_firmware.md §12 Frage 8: com_ts = CP (HL); JR Z; CP 0FFH; INC HL; JR NZ — der Vergleich
    // mit FFH prüft A (Kommandocode), nicht das Tabellenbyte.  Im Abzug 4.2 bei 0786H.
    const uint8_t schleife[] = {0xBE, 0x28, 0x08, 0xFE, 0xFF, 0x23, 0x20, 0xF8};
    EXPECT_EQ(std::memcmp(P8K_WDC_4_2 + 0x786, schleife, sizeof schleife), 0);
}

// ─── Start und Initialisierung ───────────────────────────────────────────────

TEST(P8000Wdc_, StartetOhneFehler20BisBereitZumKommandoempfang)
{
    Aufbau a;
    EXPECT_EQ(a.wdc.status(), 0);   // 8212-CLR: besetzt
    ASSERT_TRUE(a.host.warteStatus(1, 40'000'000)) << "Status " << int(a.wdc.status());
    // LW 0 sofort bereit, LW 1 und 2 je 2 × time2(100) ≈ 2 s Wartezeit (FW Z. 477–499)
    EXPECT_GT(a.wdc.takte(), 14'000'000u);
    EXPECT_LT(a.wdc.takte(), 22'000'000u);
    // d_rdy: LW 0 bereit; das INIT-Bit (87H) nimmt `begin` schon vor dem Warten auf das Kommando
    // zurück (die Init-Fehlerprüfung läuft vorab, gemeldet wird beim ersten Kommando)
    EXPECT_EQ(a.wdc.lesen(0x3138), 0x01);
    EXPECT_EQ(a.wdc.lesen(0x3137), 0x09);   // s_rdy: PAR + BTT von LW 0 gültig
    EXPECT_EQ(a.wdc.lesen(0x312F), 0x00);   // err_in
    EXPECT_EQ(a.platte.zylinder(), 0);
}

TEST(P8000Wdc_, ParameterblockWieSaFormatIhnLiest)
{
    Aufbau a;
    auto r = a.host.ausfuehren({0x28, 0, 0, 0, 0, 0, 50, 0, 0}, 50);
    ASSERT_EQ(r.fehler, 0);
    ASSERT_EQ(r.daten.size(), 50u);
    const auto& d = r.daten;
    EXPECT_EQ(std::string(d.begin(), d.begin() + 7), "WDC_4.2");   // rd_pb kopiert 7 Zeichen
    EXPECT_EQ(d[7], 0x81);                                           // 8. Byte = RAM-Füllwert nach Reset
    EXPECT_EQ(std::string(d.begin() + 8, d.begin() + 20), "ROB K5504.50");
    EXPECT_EQ(d[23] | (d[24] << 8), 1024);    // Zylinder (30E7)
    EXPECT_EQ(d[25], 5);                      // Köpfe
    EXPECT_EQ(d[26], 18);                     // Sektoren
    EXPECT_EQ(d[27] | (d[28] << 8), 1024);    // Vorkompensation
    EXPECT_EQ(d[29], 1);                      // rp_mod
    EXPECT_EQ(d[36], 203); EXPECT_EQ(d[37], 209);
    EXPECT_EQ(d[38], 251); EXPECT_EQ(d[39], 253); EXPECT_EQ(d[40], 241); EXPECT_EQ(d[41], 243);
    EXPECT_EQ(d[42] | (d[43] << 8) | (d[44] << 16) | (d[45] << 24), 92069);   // höchste Blocknummer
    EXPECT_EQ(d[46], 0x01);   // d_rdy (INIT-Bit nach dem ersten Kommando gelöscht)
    EXPECT_EQ(d[47], 0x09);   // s_rdy
    EXPECT_EQ(d[48], 0x00);   // err_in
    EXPECT_EQ(d[49], 1);      // Laufwerksanzahl
}

TEST(P8000Wdc_, EpromPruefsummenfehlerGibtStatus6UndHaelt)
{
    std::vector<uint8_t> rom(std::begin(P8K_WDC_4_2), std::end(P8K_WDC_4_2));
    rom[0x0800] ^= 0x01;
    P8000Wdc::Config cfg;
    cfg.rom = rom.data();
    cfg.rom_groesse = rom.size();
    Aufbau a(cfg);
    a.host.lauf(4'000'000);
    EXPECT_EQ(a.wdc.status(), 6);             // MON16 meldet dafür „Fehler 20"
    EXPECT_TRUE(a.wdc.cpu().halted);
    EXPECT_FALSE(a.wdc.uebertragungAktiv());
}

TEST(P8000Wdc_, OhneLaufwerkMeldetDasErsteKommandoFehler27)
{
    P8000Wdc w;
    StubHost h(w);
    // LW 0 bis 32 s, LW 1 und 2 je 2 s — Status 1 erst nach ≈ 36 s
    ASSERT_TRUE(h.warteStatus(1, 160'000'000));
    EXPECT_GT(w.takte(), 120'000'000u);
    EXPECT_EQ(w.lesen(0x3138), 0x40);         // ERR_IN, kein LW (INIT-Bit schon geprüft)
    auto r = h.ausfuehren(sektorKommando(0x01, 0, 0, 0, 1));
    EXPECT_EQ(r.fehler, 0x27);
    r = h.ausfuehren(sektorKommando(0x01, 0, 0, 0, 1));   // danach: LW nicht bereit
    EXPECT_EQ(r.fehler, 0x03);
}

TEST(P8000Wdc_, UngueltigerParSektorMeldetFehler39UndArbeitetVorlaeufig)
{
    Platte::Config pc;
    pc.par_ergaenzen = false;
    Aufbau a({}, pc, [](Platte& p) {
        std::array<uint8_t, 512> null{};
        ASSERT_TRUE(p.sektorSchreiben(0, 0, 1, null.data()));
    });
    auto r = a.host.ausfuehren({0x28, 0, 0, 0, 0, 0, 50, 0, 0}, 50);
    EXPECT_EQ(r.fehler, 0x39);                // 38H + LW 0: PAR/BTT fehlerhaft — Kommando nicht ausgeführt
    r = a.host.ausfuehren({0x28, 0, 0, 0, 0, 0, 50, 0, 0}, 50);
    ASSERT_EQ(r.fehler, 0);
    EXPECT_EQ(r.daten[23] | (r.daten[24] << 8), 100);   // vorläufige Parameter 100/2/18
    EXPECT_EQ(r.daten[25], 2);
    EXPECT_EQ(r.daten[47], 0x00);             // s_rdy
    EXPECT_EQ(r.daten[48], 0x08);             // err_in Bit 3 = PAR/BTT LW 0
}

TEST(P8000Wdc_, UnlesbarerParSektorMeldetFehler31)
{
    Aufbau a({}, {}, [](Platte& p) { p.setzeFormatiert(0, 0, false); });
    auto r = a.host.ausfuehren(sektorKommando(0x00, 0, 0, 0, 1));
    EXPECT_EQ(r.fehler, 0x31);                // 30H + LW 0: Z0/K0/S1 nicht lesbar
    EXPECT_EQ(a.wdc.lesen(0x31AA), 4);        // sum_zt: 4 Versuche ohne Marke (fe_n)
}

// ─── Kommandoauswertung ──────────────────────────────────────────────────────

TEST(P8000Wdc_, ReadyTestCode00UndUnbekannteCodes)
{
    Aufbau a;
    // 00 steht nicht in com_tb; com_ts sucht über das Tabellenende hinaus und findet die 00
    // im Kennfeld id_df (FE 00 00 00 01) — der Ready-Test läuft (Frage 8).
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x00, 0, 0, 0, 1)).fehler, 0);
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0xFF, 0, 0, 0, 1)).fehler, 0x01);
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x00, 1, 0, 0, 1)).fehler, 0x03);
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x00, 3, 0, 0, 1)).fehler, 0x03);
}

TEST(P8000Wdc_, FehlercodesFuerUnzulaessigeAdressen)
{
    Aufbau a;
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x01, 1, 1, 0, 1)).fehler, 0x03);    // LW 1 fehlt
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x01, 0, 1, 5, 1)).fehler, 0x02);    // Kopf 5
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x01, 0, 1024, 0, 1)).fehler, 0x04); // Zylinder 1024
    EXPECT_EQ(a.host.ausfuehren(blockKommando(0x21, 0, 92070)).fehler, 0x05);       // Block zu groß
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x01, 0, 1, 0, 19)).fehler, 0x0A);   // Sektor 19 fehlt
}

// ─── Lesen ───────────────────────────────────────────────────────────────────

TEST(P8000Wdc_, SektorLesenZ0K0S1LiefertDenParSektor)
{
    Aufbau a;
    auto r = a.host.ausfuehren(sektorKommando(0x01, 0, 0, 0, 1), 512);
    ASSERT_EQ(r.fehler, 0);
    const auto par = k1520::winchester::parSektor(*k1520::winchester::typNachName("K5504.50"));
    EXPECT_TRUE(std::equal(par.begin(), par.end(), r.daten.begin()));
}

TEST(P8000Wdc_, WdcZuHostUebertraegtEinByteMehrHostZuWdcGenauN)
{
    Aufbau a;
    ASSERT_TRUE(a.host.warteStatus(1, 40'000'000));
    const int vor = a.host.strobes;
    auto r = a.host.ausfuehren(sektorKommando(0x01, 0, 0, 0, 2), 512);
    ASSERT_EQ(r.fehler, 0);
    EXPECT_EQ(a.host.strobes - vor, 9 + 513);   // 9 Kommandobytes, 512 + 1 Datenbytes
}

TEST(P8000Wdc_, BlockLesenEinzelnUndUeberSpurUndZylindergrenze)
{
    Aufbau a;
    for (uint32_t b : {0u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 89u, 90u, 500u})
        ASSERT_TRUE(a.platte.sektorSchreiben(blockLage(b)[0], blockLage(b)[1], blockLage(b)[2], muster(int(b)).data()));
    auto pruefe = [&](uint32_t start, int n) {
        auto r = a.host.ausfuehren(blockKommando(0x21, 0, start, 512 * n), size_t(512 * n));
        ASSERT_EQ(r.fehler, 0) << "Block " << start;
        for (int i = 0; i < n; ++i) {
            const auto m = muster(int(start) + i);
            EXPECT_TRUE(std::equal(m.begin(), m.end(), r.daten.begin() + 512 * i)) << "Block " << start + i;
        }
    };
    pruefe(0, 1);                  // Block 0 = Z1/K0/S1
    pruefe(500, 1);
    pruefe(14, 8);                 // 4 KB über die Spurgrenze K0 → K1
    pruefe(89, 2);                 // Zylinderwechsel Z1/K4/S18 → Z2/K0/S1 (step1)
    EXPECT_EQ(a.platte.zylinder(), 2);
}

TEST(P8000Wdc_, KammFaehrtZuFernemZylinderUndZurueck)
{
    Aufbau a;
    ASSERT_TRUE(a.platte.sektorSchreiben(1000, 3, 7, muster(1000).data()));
    auto r = a.host.ausfuehren(sektorKommando(0x01, 0, 1000, 3, 7), 512);
    ASSERT_EQ(r.fehler, 0);
    EXPECT_EQ(a.platte.zylinder(), 1000);
    const auto m = muster(1000);
    EXPECT_TRUE(std::equal(m.begin(), m.end(), r.daten.begin()));
    r = a.host.ausfuehren(sektorKommando(0x01, 0, 0, 0, 1), 512);
    ASSERT_EQ(r.fehler, 0);
    EXPECT_EQ(a.platte.zylinder(), 0);
}

// ─── Schreiben ───────────────────────────────────────────────────────────────

TEST(P8000Wdc_, BlockSchreibenLandetImAbbild)
{
    Aufbau a;
    const auto m = muster(42);
    const std::vector<uint8_t> d(m.begin(), m.end());
    ASSERT_EQ(a.host.ausfuehren(blockKommando(0x22, 0, 3), 0, &d).fehler, 0);
    // drei Blöcke über die Spurgrenze (Block 17 = Z1/K0/S18, 18/19 = Z1/K1/S1–2)
    std::vector<uint8_t> drei;
    for (int i = 0; i < 3; ++i) { const auto x = muster(100 + i); drei.insert(drei.end(), x.begin(), x.end()); }
    ASSERT_EQ(a.host.ausfuehren(blockKommando(0x22, 0, 17, 3 * 512), 0, &drei).fehler, 0);
    a.platte.flush();
    std::array<uint8_t, 512> x{};
    ASSERT_TRUE(a.platte.sektorLesen(1, 0, 4, x.data()));
    EXPECT_EQ(x, m);
    for (int i = 0; i < 3; ++i) {
        const auto l = blockLage(17u + uint32_t(i));
        ASSERT_TRUE(a.platte.sektorLesen(l[0], l[1], l[2], x.data()));
        EXPECT_EQ(x, muster(100 + i)) << i;
    }
    ASSERT_TRUE(a.platte.sektorLesen(1, 0, 5, x.data()));   // Nachbar unberührt
    EXPECT_EQ(x[0], 0xE5);
    auto r = a.host.ausfuehren(blockKommando(0x21, 0, 17, 3 * 512), 3 * 512);   // und zurücklesen
    ASSERT_EQ(r.fehler, 0);
    EXPECT_EQ(r.daten, drei);
}

TEST(P8000Wdc_, SchreibenMitRuecklesen82UndA2)
{
    Aufbau a;
    const auto m = muster(7);
    const std::vector<uint8_t> d(m.begin(), m.end());
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x82, 0, 3, 2, 11), 0, &d).fehler, 0);
    ASSERT_EQ(a.host.ausfuehren(blockKommando(0xA2, 0, 1000), 0, &d).fehler, 0);
    a.platte.flush();
    std::array<uint8_t, 512> x{};
    ASSERT_TRUE(a.platte.sektorLesen(3, 2, 11, x.data()));
    EXPECT_EQ(x, m);
    const auto l = blockLage(1000);
    ASSERT_TRUE(a.platte.sektorLesen(l[0], l[1], l[2], x.data()));
    EXPECT_EQ(x, m);
}

// ─── Formatieren ─────────────────────────────────────────────────────────────

TEST(P8000Wdc_, FormatierenSpur04Und14Und24)
{
    Aufbau a;
    for (int s = 1; s <= 18; ++s) ASSERT_TRUE(a.platte.sektorSchreiben(5, 2, s, muster(s).data()));
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x04, 0, 5, 2, 1)).fehler, 0);
    EXPECT_EQ(a.wdc.lesen(0x3380), 203);      // Umdrehungsmessung ⇒ 40-MHz-Zeitkonstante
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x14, 0, 5, 3, 1)).fehler, 0);   // + Rücklesen
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x24, 0, 5, 4, 1)).fehler, 0);   // + Defektprüfung
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x44, 0, 5, 2, 1)).fehler, 0);   // Spur rücklesen
    a.platte.flush();
    std::array<uint8_t, 512> x{};
    for (int k = 2; k <= 4; ++k) {
        EXPECT_TRUE(a.platte.formatiert(5, k));
        for (int s = 1; s <= 18; ++s) {
            ASSERT_TRUE(a.platte.sektorLesen(5, k, s, x.data()));
            for (uint8_t b : x) ASSERT_EQ(b, 0xE5) << k << "/" << s;
        }
    }
    // BTT unverändert leer (kein Defekt): 58 liefert Zähler 0 und die Endekennung
    auto r = a.host.ausfuehren({0x58, 0, 0, 0, 0, 0, 5, 0, 0}, 5);
    ASSERT_EQ(r.fehler, 0);
    EXPECT_EQ(r.daten, (std::vector<uint8_t>{0, 0, 0xFF, 0xFF, 0xFF}));
}

TEST(P8000Wdc_, FormatierteSpurHatDieLageDerFirmware)
{
    Aufbau a;
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x04, 0, 0, 1, 1)).fehler, 0);
    const auto& s = a.platte.spur(0, 1);
    std::vector<int> id, df;
    for (int i = 0; i < Platte::BYTES_JE_SPUR; ++i) {
        if (!(s[size_t(i)] & Platte::MARKE)) continue;
        const uint16_t n = s[size_t((i + 1) % Platte::BYTES_JE_SPUR)];
        if (n == 0xFE) id.push_back(i);
        if (n == 0xFB) df.push_back(i);
    }
    ASSERT_EQ(id.size(), 18u);
    ASSERT_EQ(df.size(), 18u);
    EXPECT_LT(id[0], 60);                     // erstes Kennfeld kurz hinter dem Index
    for (size_t i = 1; i < id.size(); ++i) {   // Abstand 16·ztk + Schleife ≈ 566 Byte
        EXPECT_GE(id[i] - id[i - 1], 560);
        EXPECT_LE(id[i] - id[i - 1], 572);
    }
    for (size_t i = 0; i < 18; ++i) {          // Datenmarke ≥ 34 Byte hinter der Kennfeldmarke [W11]
        EXPECT_GE(df[i] - id[i], 34) << i;
        EXPECT_LE(df[i] - id[i], 40) << i;
        if (i + 1 < 18) EXPECT_LT(df[i] + 520, id[i + 1] - 2) << i;   // Datenfeld vor den nächsten Marken
    }
    // Kopf 1: Sektorfolge 18 1 10 … (Kopfversatz)
    EXPECT_EQ(s[size_t(id[0] + 5)], 18);
    EXPECT_EQ(s[size_t(id[1] + 5)], 1);
    EXPECT_EQ(s[size_t(id[2] + 5)], 10);
}

TEST(P8000Wdc_, UnformatierteSpurWirdDurchFormatierenLesbar)
{
    Aufbau a({}, {}, [](Platte& p) { p.setzeFormatiert(7, 1, false); });
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x01, 0, 7, 1, 3), 512).fehler, 0x0B);   // keine Marke
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x04, 0, 7, 1, 1)).fehler, 0);
    const auto m = muster(3);
    const std::vector<uint8_t> d(m.begin(), m.end());
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x02, 0, 7, 1, 3), 0, &d).fehler, 0);
    auto r = a.host.ausfuehren(sektorKommando(0x01, 0, 7, 1, 3), 512);
    ASSERT_EQ(r.fehler, 0);
    EXPECT_EQ(r.daten, d);
    a.platte.flush();
    EXPECT_TRUE(a.platte.formatiert(7, 1));
}

TEST(P8000Wdc_, SpurLoeschen84MachtUnformatiert)
{
    Aufbau a;
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x84, 0, 9, 0, 1)).fehler, 0);
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x01, 0, 9, 0, 1), 512).fehler, 0x0B);
    a.platte.flush();
    EXPECT_FALSE(a.platte.formatiert(9, 0));
    EXPECT_TRUE(a.platte.formatiert(9, 1));
}

TEST(P8000Wdc_, Bestueckung41_4MHzWaehltZtk41)
{
    P8000Wdc::Config cfg;
    cfg.takt_hz = 4'140'000;
    Aufbau a(cfg);
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x14, 0, 2, 0, 1)).fehler, 0);
    EXPECT_EQ(a.wdc.lesen(0x3380), 209);
    for (int i = 0; i < 8; ++i) {             // die 8 Messwerte liegen in zmn_41 … zmx_41
        EXPECT_GE(a.wdc.lesen(uint16_t(0x3381 + i)), 241);
        EXPECT_LE(a.wdc.lesen(uint16_t(0x3381 + i)), 243);
    }
}

// ─── RAM-Kommandos ───────────────────────────────────────────────────────────

TEST(P8000Wdc_, WdcRamSchreibenUndLesen)
{
    Aufbau a;
    const std::vector<uint8_t> d = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x55, 0xAA};
    ASSERT_EQ(a.host.ausfuehren(ramKommando(0x18, 0x2400, int(d.size())), 0, &d).fehler, 0);
    for (size_t i = 0; i < d.size(); ++i) EXPECT_EQ(a.wdc.lesen(uint16_t(0x2400 + i)), d[i]);
    auto r = a.host.ausfuehren(ramKommando(0x08, 0x2400, int(d.size())), d.size());
    ASSERT_EQ(r.fehler, 0);
    EXPECT_EQ(r.daten, d);
}

// ─── RST, ältere Firmware, Save-State ────────────────────────────────────────

TEST(P8000Wdc_, RstHaeltImResetUndStartetNeu)
{
    Aufbau a;
    ASSERT_TRUE(a.host.warteStatus(1, 40'000'000));
    a.wdc.setzeRst(true);
    EXPECT_EQ(a.wdc.status(), 0);
    EXPECT_FALSE(a.wdc.uebertragungAktiv());
    const uint64_t t = a.wdc.takte();
    a.host.lauf(1'000'000);
    EXPECT_EQ(a.wdc.takte(), t + 1'000'000);
    EXPECT_EQ(a.wdc.cpu().PC, 0);
    EXPECT_EQ(a.wdc.status(), 0);
    a.wdc.setzeRst(false);
    ASSERT_TRUE(a.host.warteStatus(1, 40'000'000));
    EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x00, 0, 0, 0, 1)).fehler, 0);
}

TEST(P8000Wdc_, Firmware4_0_05LiestMitEingebranntenParametern)
{
    P8000Wdc::Config cfg;
    cfg.firmware = P8000Wdc::Config::Firmware::V4_0_05;
    Aufbau a(cfg);
    const auto m = muster(9);
    ASSERT_TRUE(a.platte.sektorSchreiben(1, 0, 1, m.data()));
    auto r = a.host.ausfuehren(blockKommando(0x21, 0, 0), 512);
    ASSERT_EQ(r.fehler, 0);
    EXPECT_TRUE(std::equal(m.begin(), m.end(), r.daten.begin()));
    r = a.host.ausfuehren({0x28, 0, 0, 0, 0, 0, 12, 0, 0}, 12);
    ASSERT_EQ(r.fehler, 0);
    EXPECT_EQ(std::string(r.daten.begin(), r.daten.begin() + 9), "WDC_V.4.0");
}

namespace {
Platte::Config spur3xFuerStart()
{
    Platte::Config pc;
    pc.spurformat = Platte::Spurformat::V3x;
    return pc;
}
}  // namespace

TEST(P8000Wdc_, Firmware3_4_05StartetOhneZweitenEprom)
{
    // Abzug „3.4.05" (Gerät des Anwenders): nur EPROM 1, Prüfsummen bei 0FF8H, EPROM 2 unbestückt.
    // Läuft bis „bereit"; ihr Spurformat ist ein anderes als das der 4.2 (Tests `P8000Wdc3x.*`, §12 Nr. 11),
    // darum hier die 3.x-Spur — auf einer 4.2-Spur meldet das erste Kommando Fehler 05.
    P8000Wdc::Config cfg;
    cfg.firmware = P8000Wdc::Config::Firmware::V3_4_05;
    Aufbau a(cfg, spur3xFuerStart());
    ASSERT_TRUE(a.host.warteStatus(1, 200'000'000)) << "Status " << int(a.wdc.status());
    a.host.ausfuehren(sektorKommando(0x00, 0, 0, 0, 1));
    auto r = a.host.ausfuehren({0x28, 0, 0, 0, 0, 0, 12, 0, 0}, 12);
    ASSERT_EQ(r.fehler, 0);
    EXPECT_EQ(std::string(r.daten.begin(), r.daten.begin() + 12), "WDC_V.3.4.05");
}

TEST(P8000Wdc_, SaveStateRundreiseMittenImLesen)
{
    Aufbau a;
    for (uint32_t b = 0; b < 8; ++b)
        ASSERT_TRUE(a.platte.sektorSchreiben(blockLage(b)[0], blockLage(b)[1], blockLage(b)[2], muster(int(b)).data()));
    ASSERT_TRUE(a.host.warteStatus(1, 40'000'000));
    ASSERT_TRUE(a.host.sende(blockKommando(0x21, 0, 0, 8 * 512)));
    a.host.lauf(30'000);                      // mitten in Sektorsuche/DMA
    std::vector<uint8_t> st;
    a.wdc.serialize(st);

    Platte p2;
    ASSERT_TRUE(p2.oeffnen(a.tp));
    P8000Wdc w2;
    w2.anschliessen(0, &p2);
    StubHost h2(w2);
    const uint8_t* q = st.data();
    ASSERT_TRUE(w2.deserialize(q, st.data() + st.size()));
    EXPECT_EQ(q, st.data() + st.size());
    std::vector<uint8_t> st2;
    w2.serialize(st2);
    EXPECT_EQ(st, st2);

    auto weiter = [](StubHost& h) {   // beide weiter: gleiche Daten, gleicher Zustand
        EXPECT_TRUE(h.warteBis([&] { return h.w.status() == 3 && h.w.uebertragungAktiv(); }));
        return h.empfange(8 * 512);
    };
    auto d1 = weiter(a.host), d2 = weiter(h2);
    ASSERT_TRUE(d1 && d2);
    EXPECT_EQ(*d1, *d2);
    for (int i = 0; i < 8; ++i) {
        const auto m = muster(i);
        EXPECT_TRUE(std::equal(m.begin(), m.end(), d1->begin() + 512 * i)) << i;
    }
    a.host.lauf(100'000);
    h2.lauf(100'000);
    std::vector<uint8_t> e1, e2;
    a.wdc.serialize(e1);
    w2.serialize(e2);
    EXPECT_EQ(e1, e2);

    P8000Wdc w3;                              // andere Bestückung (kein Laufwerk) ⇒ abgelehnt
    q = st.data();
    EXPECT_FALSE(w3.deserialize(q, st.data() + st.size()));
}



// ─── Kartenhardware ohne Firmware (Regeln [W1]–[W11], [H1]–[H4] aus wdc.h) ───────────────────

namespace {

/// WDC mit einem Mini-ROM (DI; HALT) — die CPU steht, die Hardware wird über die E/A- und
/// Speicherrückrufe der CPU getrieben wie von einem Befehl.
struct Hw {
    std::vector<uint8_t> rom = std::vector<uint8_t>(0x2000, 0x00);
    TempPlatte tp{"K5504.50", "wdc_hw.img"};
    Platte platte;
    std::unique_ptr<P8000Wdc> w;
    explicit Hw(std::initializer_list<uint8_t> code = {0xF3, 0x76}, int wartetakte = 1)
    {
        std::copy(code.begin(), code.end(), rom.begin());
        P8000Wdc::Config cfg;
        cfg.rom = rom.data();
        cfg.rom_groesse = rom.size();
        cfg.wartetakte_m1 = wartetakte;
        w = std::make_unique<P8000Wdc>(cfg);
        EXPECT_TRUE(platte.oeffnen(tp));
        w->anschliessen(0, &platte);
        w->laufeBis(20);                 // DI; HALT ausgeführt
    }
    void out(uint8_t p, uint8_t v) { w->cpu().writePort(p, v); }
    uint8_t in(uint8_t p) { return static_cast<uint8_t>(w->cpu().readPort(p)); }
    void mem(uint16_t a, uint8_t v) { w->cpu().writeByte(a, v); }
    void lauf(uint64_t n) { w->laufeBis(w->takte() + n); }
    static uint16_t diskAdr(uint16_t rel) { return uint16_t((((rel >> 10) & 3) << 14) | 0x3C00 | (rel & 0x3FF)); }
    static uint16_t hostAdr(uint16_t rel) { return uint16_t((((rel >> 10) & 3) << 14) | 0x3800 | (rel & 0x3FF)); }
};

constexpr double UMDREHUNG = Platte::BYTES_JE_SPUR * 6.4;   // Takte bei 4 MHz

}  // namespace

TEST(P8000WdcHw, SpeicherkarteSpiegelUndAdresszaehlerLaden)
{
    Hw h;
    EXPECT_EQ(h.w->lesen(0x0000), 0xF3);
    EXPECT_EQ(h.w->lesen(0x4000), 0xF3);       // A15/A14 nicht dekodiert [W1]
    EXPECT_EQ(h.w->lesen(0xC001), 0x76);
    h.mem(0x2000, 0x11); h.mem(0x37FF, 0x22); h.mem(0x0010, 0x33);
    EXPECT_EQ(h.w->lesen(0x2000), 0x11);
    EXPECT_EQ(h.w->lesen(0xA000), 0x11);
    EXPECT_EQ(h.w->lesen(0x37FF), 0x22);
    EXPECT_EQ(h.w->lesen(0x0010), 0x00);       // EPROM nicht beschreibbar
    EXPECT_EQ(h.w->lesen(0x3800), 0xFF);       // keine Zelle
    // b_daz: Disk 1603H (kf) → 7E03H; p_h_1: Host 10B7H (cmd) → 38B7H mit HA12 aus CNTST
    h.mem(Hw::diskAdr(0x603), 0x99);
    EXPECT_EQ(h.w->diskZaehler(), 0x603);
    EXPECT_EQ(Hw::diskAdr(0x603), 0x7E03);
    h.mem(Hw::hostAdr(0x0B7), 0x99);
    EXPECT_EQ(h.w->hostZaehler(), 0x0B7);
    h.mem(Hw::diskAdr(0xFFF), 0); EXPECT_EQ(h.w->diskZaehler(), 0xFFF);
    h.mem(Hw::hostAdr(0xC00), 0); EXPECT_EQ(h.w->hostZaehler(), 0xC00);
    EXPECT_EQ(h.w->lesen(0x7E03), 0xFF);
}

TEST(P8000WdcHw, WartetaktJeM1)
{
    Hw h({0x00, 0xCB, 0x47, 0xDD, 0x21, 0x34, 0x12, 0xED, 0x56, 0x76});   // NOP; BIT 0,A; LD IX,nn; IM 1; HALT
    Hw o({0x00, 0xCB, 0x47, 0xDD, 0x21, 0x34, 0x12, 0xED, 0x56, 0x76}, 0);
    for (Hw* x : {&h, &o}) { x->w->setzeRst(true); x->w->setzeRst(false); }
    EXPECT_EQ(h.w->schritt(), 4 + 1);
    EXPECT_EQ(h.w->schritt(), 8 + 2);
    EXPECT_EQ(h.w->schritt(), 14 + 2);
    EXPECT_EQ(h.w->schritt(), 8 + 2);
    EXPECT_EQ(h.w->schritt(), 4 + 1);   // HALT
    EXPECT_EQ(h.w->schritt(), 4 + 1);   // im HALT je NOP-Zyklus
    EXPECT_EQ(o.w->schritt(), 4);
    EXPECT_EQ(o.w->schritt(), 8);
}

TEST(P8000WdcHw, StatusportOhneUndMitLaufwerkSchrittUndSeekComplete)
{
    Platte::Config pc;
    pc.seek_takte = 1000;
    Hw h;
    h.platte.oeffnen(h.tp, pc);
    EXPECT_EQ(h.in(0x88), 0xFF);               // kein Laufwerk gewählt: alles inaktiv, CRC gut
    h.out(0x58, 0x12);                         // LW 0
    EXPECT_EQ(h.in(0x88), 0xB3);               // /READY 0, /SEEKC 0, /TR0 0 (= Mitschnitt der Firmware)
    h.out(0x58, 0x22);                         // LW 1 nicht angeschlossen
    EXPECT_EQ(h.in(0x88), 0xFF);
    h.out(0x58, 0x12);
    h.out(0x48, 0x08); h.out(0x48, 0x09); h.out(0x48, 0x08);   // ein Schritt nach innen
    EXPECT_EQ(h.platte.zylinder(), 1);
    EXPECT_EQ(h.in(0x88) & 0x48, 0x48);        // nicht Spur 0, Suche läuft
    h.lauf(1100);
    EXPECT_EQ(h.in(0x88) & 0x48, 0x40);
    h.out(0x48, 0x01); h.out(0x48, 0x00);      // nach außen
    EXPECT_EQ(h.platte.zylinder(), 0);
    h.out(0x48, 0x01);                         // Pegel bleibt 1: kein weiterer Schritt
    h.out(0x48, 0x01);
    EXPECT_EQ(h.platte.zylinder(), 0);
    h.out(0x58, 0x32); h.out(0x48, 0x09); h.out(0x48, 0x08);   // Schritt an LW 2 (fehlt): LW 0 bleibt
    EXPECT_EQ(h.platte.zylinder(), 0);
    h.out(0x88, 0x00);                         // Statusport ist nur lesbar
    EXPECT_EQ(h.in(0x08), 0xFF);               // Ausgabetore lesen FFH
}

TEST(P8000WdcHw, IndeximpulsJeUmdrehungAnCtcK3NurVomGewaehltenLaufwerk)
{
    Hw h;
    h.out(0x73, 0x47); h.out(0x73, 0x00);      // Zähler, ohne Interrupt, TC 256
    h.out(0x58, 0x12);
    h.lauf(uint64_t(UMDREHUNG * 10));
    const int nach10 = h.in(0x73);
    EXPECT_TRUE(nach10 == 246 || nach10 == 247) << nach10;
    h.out(0x58, 0x02);                         // kein Laufwerk
    h.lauf(uint64_t(UMDREHUNG * 5));
    EXPECT_EQ(h.in(0x73), nach10);
    h.out(0x58, 0x12);
    h.lauf(uint64_t(UMDREHUNG * 3));
    EXPECT_EQ(h.in(0x73), nach10 - 3);
}

TEST(P8000WdcHw, LesenSynchronisiertAufDreiMarkenUndPrueftDieKennfeldCrc)
{
    Hw h;
    auto bisLage = [&](int p) {   // bis kurz vor Lage p unter dem Kopf
        for (int i = 0; i < 20000 && (h.w->plattenPosition() < p - 8 || h.w->plattenPosition() > p); ++i) h.lauf(6);
    };
    auto kennfeldLesen = [&] {
        h.mem(Hw::diskAdr(0x603), 0);          // Zähler → 3603H
        h.out(0x08, 0x0A);                     // Endadresse CRC2, Bit 2–0 = 2 ⇒ 3 Marken
        h.out(0x58, 0x15);                     // LW 0, DEN, /MEN = 0, CRCEN
        const int k0 = h.in(0x70);
        for (int i = 0; i < 20000 && h.in(0x70) == k0; ++i) h.lauf(6);
        h.lauf(uint64_t(9 * 6.4));             // FE … CRC2
        h.out(0x58, 0x12); h.out(0x68, 0);     // aus (IMPAUS)
    };
    h.out(0x70, 0x47); h.out(0x70, 0x00);      // K0 zählt MAERK
    h.out(0x71, 0x47); h.out(0x71, 0x00);      // K1 zählt /DEND
    h.out(0x38, 0xA1); h.out(0x28, 0x0A);
    h.out(0x18, 0x40);                         // Lesen, DA12 = 1
    h.mem(0x3603, 0x77);
    h.out(0x58, 0x12);
    bisLage(Platte::SLOT);                     // vor das Kennfeld von Slot 1 (Sektor 10)
    kennfeldLesen();
    EXPECT_EQ(h.in(0x70), 255);                // genau eine MAERK
    EXPECT_EQ(h.w->lesen(0x3603), 0x77);       // die Marke belegt die Adresse, wird nicht geschrieben
    const uint8_t soll[] = {0xFE, 0, 0, 0, 10};
    for (int i = 0; i < 5; ++i) EXPECT_EQ(h.w->lesen(uint16_t(0x3604 + i)), soll[i]) << i;
    EXPECT_GE(h.w->diskZaehler(), 0x60B);      // nach CRC2 (360AH) steht der Zähler dahinter
    EXPECT_EQ(h.in(0x71), 255);                // /DEND an 360AH
    EXPECT_EQ(h.in(0x88) & 0x80, 0x80);        // CRC gut

    // verfälschte Kennfeld-CRC in Slot 2: Fehler-FF an /DEND mit CRCEN, Lesen löscht es
    h.platte.schreibe(0, 2 * Platte::SLOT + 27, 0x00, 0);
    bisLage(2 * Platte::SLOT);
    kennfeldLesen();
    EXPECT_EQ(h.w->lesen(0x3608), 2);          // Slot 2 = Sektor 2
    EXPECT_EQ(h.in(0x88) & 0x80, 0x00);
    EXPECT_EQ(h.in(0x88) & 0x80, 0x80);
    // ohne CRCEN keine Prüfung
    bisLage(2 * Platte::SLOT);
    h.mem(Hw::diskAdr(0x603), 0);
    h.out(0x58, 0x11);                         // LW 0, DEN, /MEN = 0, CRCEN aus
    h.lauf(uint64_t(60 * 6.4));
    h.out(0x58, 0x12); h.out(0x68, 0);
    EXPECT_EQ(h.in(0x88) & 0x80, 0x80);
}

TEST(P8000WdcHw, MarkenzahlAusDskeaUndKeinAblegenOhneMarkensuche)
{
    Hw h;
    h.out(0x38, 0xA1);
    h.out(0x18, 0x40);
    h.mem(Hw::diskAdr(0x700), 0);
    h.out(0x08, 0xF0);                         // Bit 2–0 = 0 ⇒ eine Marke
    h.out(0x58, 0x15);
    h.lauf(uint64_t(30 * 6.4));
    EXPECT_EQ(h.w->lesen(0x3701), 0xA1);       // nach der ersten Marke folgen A1 A1 FE
    EXPECT_EQ(h.w->lesen(0x3702), 0xA1);
    EXPECT_EQ(h.w->lesen(0x3703), 0xFE);
    h.out(0x58, 0x12); h.out(0x68, 0);

    h.mem(Hw::diskAdr(0x740), 0);              // DEN ohne Markensuche: synchronisiert nie
    h.out(0x08, 0x02);
    h.out(0x58, 0x17);
    h.lauf(uint64_t(UMDREHUNG));
    EXPECT_EQ(h.w->diskZaehler(), 0x740);
    EXPECT_EQ(h.w->lesen(0x3741), 0x00);
}

TEST(P8000WdcHw, SchreibenBlendetMarkeUndCrcEinUndMitDerVerzoegerung)
{
    Hw h;
    // Puffer wie wrt_dt (dt_br = 2FF5H): FF×11, A1 (2000H), FB, 512 Daten, Platz für die CRC
    const auto d = muster(5);
    for (int i = 0; i < 11; ++i) h.mem(uint16_t(0x2FF5 + i), 0xFF);
    h.mem(0x2000, 0xA1); h.mem(0x2001, 0xFB);
    for (int i = 0; i < 512; ++i) h.mem(uint16_t(0x2002 + i), d[size_t(i)]);
    for (int i = 0; i < 12; ++i) h.mem(uint16_t(0x2202 + i), 0xFF);
    h.out(0x70, 0x47); h.out(0x70, 0);
    h.out(0x71, 0x47); h.out(0x71, 0);
    h.out(0x58, 0x12);
    h.out(0x18, 0x80);                         // RAM → Platte, DA12 = 0
    h.mem(Hw::diskAdr(0xFF5), 0);
    h.out(0x08, 0x00);                         // /DEND an 2000H: Marke, Bit 2–0 = 0 ⇒ eine
    const int start = h.w->plattenPosition();
    h.out(0x58, 0x1D);                         // DEN, /MEN = 0 (scharf), CRCEN, WG
    h.out(0x58, 0x1B);                         // /MEN zurück vor der Endadresse — bleibt scharf [W5]
    for (int i = 0; i < 2000 && (h.w->diskZaehler() < 0x1F0 || h.w->diskZaehler() >= 0xF00); ++i) h.lauf(6);
    h.out(0x08, 0x02);                         // /DEND an 2202H: CRC-Einblendung
    h.out(0x58, 0x1F);
    h.lauf(uint64_t(30 * 6.4));
    h.out(0x58, 0x12); h.out(0x68, 0);
    EXPECT_EQ(h.in(0x70), 0);                  // beim Schreiben keine MAERK
    EXPECT_EQ(h.in(0x71), 253);                // /DEND an 000H, 100H, 202H
    const auto& s = h.platte.spur(0, 0);
    int m = -1;
    for (int i = 0; i < 40; ++i)
        if (s[size_t((start + i) % Platte::BYTES_JE_SPUR)] == (0xA1 | Platte::MARKE)) { m = i; break; }
    ASSERT_GE(m, 0);
    EXPECT_GE(m, 11 + 2 - 1);                  // 11 Byte Vorspann + 2 Byte Verzögerung [W11]
    EXPECT_LE(m, 11 + 2 + 1);
    std::vector<uint8_t> feld;
    for (int i = 0; i < 516; ++i) feld.push_back(uint8_t(s[size_t((start + m + i) % Platte::BYTES_JE_SPUR)]));
    uint16_t c = 0xFFFF;
    for (uint8_t b : feld) c = k1520::winchester::crcCcitt(c, b);
    EXPECT_EQ(c, 0);                           // Marke + FB + Daten + eingeblendete CRC
    EXPECT_EQ(feld[1], 0xFB);
    EXPECT_TRUE(std::equal(d.begin(), d.end(), feld.begin() + 2));
    EXPECT_EQ(s[size_t((start + m + 516) % Platte::BYTES_JE_SPUR)], 0xFF);   // RAM-Platzhalter ersetzt
    int marken = 0;                            // im geschriebenen Fenster genau die eine Marke
    for (int i = 2; i < m + 516; ++i) marken += (s[size_t((start + i) % Platte::BYTES_JE_SPUR)] & Platte::MARKE) ? 1 : 0;
    EXPECT_EQ(marken, 1);
}

TEST(P8000WdcHw, OhneWgBleibtDiePlatteUnberuehrtDerZaehlerLaeuft)
{
    Hw h;
    const auto vorher = h.platte.spur(0, 0);
    h.out(0x58, 0x12);
    h.out(0x18, 0x80);
    h.mem(Hw::diskAdr(0x100), 0);
    h.out(0x58, 0x13);                         // DEN ohne WG
    h.lauf(uint64_t(100 * 6.4));
    EXPECT_GE(h.w->diskZaehler(), 0x160);
    EXPECT_EQ(h.platte.spur(0, 0), vorher);
    EXPECT_FALSE(h.platte.schmutzig());
    h.out(0x58, 0x16);                         // Schreiben: /MEN = 0 ohne DEN ⇒ Zähler steht [W3]
    const uint16_t z = h.w->diskZaehler();
    h.lauf(uint64_t(100 * 6.4));
    EXPECT_EQ(h.w->diskZaehler(), z);
}

TEST(P8000WdcHw, HostHandshakeHenFfUndCtcK2)
{
    Hw h;
    StubHost host(*h.w);
    host.takte_je_byte = 0;
    h.out(0x72, 0xD7); h.out(0x72, 2);         // K2: steigende HA0-Flanken, 2 ⇒ nach 3 Bytes ab gerader Adresse
    h.mem(Hw::hostAdr(0x100), 0);              // HA12 = 0 ⇒ 2100H
    EXPECT_FALSE(h.w->uebertragungAktiv());
    host.ardy = true; h.w->setzeHostbus(0x11); h.w->setzeArdy(true);   // Host schreibt vorab
    EXPECT_EQ(host.strobes, 0);                // ohne HEN nichts
    h.out(0x18, 0x09); h.out(0x18, 0x01);      // HEN-Impuls, Status 1 [H1]
    EXPECT_TRUE(h.w->uebertragungAktiv());
    EXPECT_EQ(host.strobes, 1);
    EXPECT_EQ(h.w->lesen(0x2100), 0x11);
    EXPECT_EQ(h.w->status(), 1);
    h.w->setzeTe(true);                        // TE aktiv: Host → WDC gesperrt [H3]
    h.w->setzeHostbus(0x22); host.ardy = true; h.w->setzeArdy(true);
    EXPECT_EQ(host.strobes, 1);
    h.w->setzeTe(false);
    EXPECT_EQ(host.strobes, 2);
    EXPECT_EQ(h.w->lesen(0x2101), 0x22);
    h.w->setzeHostbus(0x33); host.ardy = true; h.w->setzeArdy(true);   // 3. Byte: zweite steigende Flanke
    EXPECT_EQ(h.w->lesen(0x2102), 0x33);
    EXPECT_FALSE(h.w->uebertragungAktiv());    // Nulldurchgang bei HEN = 0 beendet
    h.w->setzeHostbus(0x44); host.ardy = true; h.w->setzeArdy(true);
    EXPECT_EQ(h.w->lesen(0x2103), 0x00);
    EXPECT_EQ(host.strobes, 3);

    // WDC → Host: TR, nur bei TE aktiv, ein Byte je ARDY-Aktivierung
    h.mem(0x3010, 0xA5); h.mem(0x3011, 0x5A);
    h.out(0x72, 0xC7); h.out(0x72, 1);         // fallende Flanke ⇒ nach 2 Bytes ab gerader Adresse
    h.mem(Hw::hostAdr(0x010), 0);
    h.out(0x18, 0x3B); h.out(0x18, 0x33);      // TR, HA12, HEN-Impuls, Status 3
    EXPECT_TRUE(h.w->tr());
    host.ardy = true; h.w->setzeArdy(true);
    EXPECT_EQ(host.strobes, 3);                // TE inaktiv
    h.w->setzeTe(true);
    EXPECT_EQ(host.strobes, 4);
    EXPECT_EQ(host.latch, 0xA5);
    host.ardy = true; h.w->setzeArdy(true);
    EXPECT_EQ(host.latch, 0x5A);
    EXPECT_FALSE(h.w->uebertragungAktiv());

    // RST löscht CNTST/DSKC2 und das FF [H4]
    h.out(0x58, 0x15);
    h.out(0x18, 0x0F);
    h.w->setzeRst(true);
    EXPECT_EQ(h.w->cntst(), 0);
    EXPECT_EQ(h.w->dskc2(), 0);
    EXPECT_FALSE(h.w->uebertragungAktiv());
    EXPECT_TRUE(h.w->imReset());
}

TEST(P8000WdcHw, EinByteJeArdyAktivierung)
{
    Hw h;
    int strobes = 0;
    h.w->setzeAstbRueckruf([&] { ++strobes; });   // eine PIO, die ARDY nicht zurücknimmt
    h.out(0x72, 0xD7); h.out(0x72, 100);
    h.mem(Hw::hostAdr(0x100), 0);
    h.out(0x18, 0x09); h.out(0x18, 0x01);
    h.w->setzeHostbus(0x42);
    h.w->setzeArdy(true);
    EXPECT_EQ(strobes, 1);
    h.w->setzeArdy(true);                       // Pegel bleibt: kein zweites Byte
    EXPECT_EQ(strobes, 1);
    h.w->setzeArdy(false);
    h.w->setzeArdy(true);
    EXPECT_EQ(strobes, 2);
    EXPECT_EQ(h.w->hostZaehler(), 0x102);
}


// ─── Firmware 3.x (P24) ──────────────────────────────────────────────────────
//
// Die 3.4.05 schreibt und erwartet eine andere Spurlage als die 4.2 (wdc_firmware.md §12 Nr. 11):
// Sektoren physisch der Reihe nach 1…18 (kein Interleave 2:1, kein Kopfversatz), Lücke hinter der
// Kennfeld-CRC FF×2 · 00×18 · FF×8, Kennfeldabstand ≈ 570 Byte; der Interleave steckt in der
// LOGISCHEN Blockreihenfolge (Tabelle `01 09 11 07 0F 05 0D 03 0B 02 0A 12 08 10 06 0E 04 0C` im Abzug).

namespace {
constexpr int BLOCK_ZU_SEKTOR_3X[18] = {1, 9, 17, 7, 15, 5, 13, 3, 11, 2, 10, 18, 8, 16, 6, 14, 4, 12};

P8000Wdc::Config firmware3x()
{
    P8000Wdc::Config cfg;
    cfg.firmware = P8000Wdc::Config::Firmware::V3_4_05;
    return cfg;
}
Platte::Config spur3x()
{
    Platte::Config pc;
    pc.spurformat = P8000Wdc::spurformat(P8000Wdc::Config::Firmware::V3_4_05);
    return pc;
}
}  // namespace

TEST(P8000Wdc3x, SpurformatJeFirmware)
{
    using F = P8000Wdc::Config::Firmware;
    EXPECT_EQ(P8000Wdc::spurformat(F::V3_4_05), Platte::Spurformat::V3x);
    EXPECT_EQ(P8000Wdc::spurformat(F::V4_0_05), Platte::Spurformat::V4_2);   // trägt die 2:1-Tabelle der 4.2
    EXPECT_EQ(P8000Wdc::spurformat(F::V4_2), Platte::Spurformat::V4_2);
}

/// Die Hochlauf-Leseprobe der 3.4.05 (BTT auf Z0/K0/S1) findet Sektor 1 nur in der 3.x-Spur; auf der
/// 4.2-Spur (Interleave 2:1, Kopfversatz) meldet das erste Kommando Fehler 05.
TEST(P8000Wdc3x, HochlaufLeseprobeBraucht3xSpur)
{
    {
        Aufbau a(firmware3x(), spur3x());
        ASSERT_TRUE(a.host.warteStatus(1, 200'000'000));
        EXPECT_EQ(a.host.ausfuehren(sektorKommando(0x00, 0, 0, 0, 1)).fehler, 0);
        auto r = a.host.ausfuehren({0x28, 0, 0, 0, 0, 0, 12, 0, 0}, 12);
        ASSERT_EQ(r.fehler, 0);
        EXPECT_EQ(std::string(r.daten.begin(), r.daten.begin() + 12), "WDC_V.3.4.05");
    }
    Aufbau b(firmware3x());                              // Gegenprobe: 4.2-Spurlage
    ASSERT_TRUE(b.host.warteStatus(1, 200'000'000));
    EXPECT_EQ(b.host.ausfuehren(sektorKommando(0x00, 0, 0, 0, 1)).fehler, 0x05);
}

TEST(P8000Wdc3x, LiestUndSchreibtBloeckeInDerLogischenReihenfolge)
{
    Aufbau a(firmware3x(), spur3x());
    for (int s = 1; s <= 18; ++s) ASSERT_TRUE(a.platte.sektorSchreiben(1, 0, s, muster(s).data()));
    // Block 0 liegt auf Zylinder 1 / Kopf 0 (Zylinder 0 ist für die BTT reserviert); Block k → Sektor Tabelle[k].
    // Je Kommando kommt genau EIN Block (512 B) — auch bei Längenfeld 1024 (anders als 4.2, nachgemessen).
    for (int k = 0; k < 8; ++k) {
        auto r = a.host.ausfuehren(blockKommando(0x21, 0, uint32_t(k)), 512, nullptr, 800'000'000);
        ASSERT_EQ(r.fehler, 0) << "Block " << k;
        const auto m = muster(BLOCK_ZU_SEKTOR_3X[k]);
        EXPECT_TRUE(std::equal(m.begin(), m.end(), r.daten.begin())) << "Block " << k;
    }
    {
        auto r = a.host.ausfuehren(blockKommando(0x21, 0, 0, 1024), 512, nullptr, 800'000'000);
        ASSERT_EQ(r.fehler, 0);
        const auto m = muster(1);
        EXPECT_TRUE(std::equal(m.begin(), m.end(), r.daten.begin()));
    }
    // Schreiben: Block 3 → Sektor 7
    const auto neu = muster(99);
    const std::vector<uint8_t> d(neu.begin(), neu.end());
    ASSERT_EQ(a.host.ausfuehren(blockKommando(0x22, 0, 3), 0, &d, 800'000'000).fehler, 0);
    a.platte.flush();
    std::array<uint8_t, 512> x{};
    ASSERT_TRUE(a.platte.sektorLesen(1, 0, 7, x.data()));
    EXPECT_EQ(x, neu);
}

/// Formatieren mit der 3.x: Sektoren 1…18 der Reihe nach auf JEDEM Kopf, Spur wird zerlegt und gilt als
/// formatiert; Kommando 04 allein (ohne Rücklesen) geht ebenfalls.
TEST(P8000Wdc3x, FormatiertSpurenMitSektorenDerReihe)
{
    Aufbau a(firmware3x(), spur3x(), [](Platte& p) { p.setzeFormatiert(7, 0, false); p.setzeFormatiert(7, 1, false); });
    ASSERT_TRUE(a.host.warteStatus(1, 200'000'000));
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x04, 0, 7, 0, 1), 0, nullptr, 800'000'000).fehler, 0);
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x14, 0, 7, 1, 1), 0, nullptr, 800'000'000).fehler, 0);
    for (int kopf : {0, 1}) {
        const auto& s = a.platte.spur(7, kopf);
        std::vector<int> folge;
        for (int i = 0; i < Platte::BYTES_JE_SPUR; ++i)
            if ((s[size_t(i)] & Platte::MARKE) && s[size_t(i + 1)] == 0xFE && !(s[size_t(i + 1)] & Platte::MARKE))
                folge.push_back(s[size_t(i + 5)] & 0xFF);
        ASSERT_EQ(folge.size(), 18u) << "Kopf " << kopf;
        for (int k = 0; k < 18; ++k) EXPECT_EQ(folge[size_t(k)], k + 1) << "Kopf " << kopf;
        const auto z = a.platte.zerlege(7, kopf, s);
        EXPECT_TRUE(z.formatiert);
        EXPECT_EQ(z.daten.size(), 18u);
    }
}

/// 4.0.05 dagegen schreibt die Spurlage der 4.2 (Interleave 2:1, Kopfversatz) — dieselbe 2:1-Tabelle im Abzug.
TEST(P8000Wdc3x, Firmware4_0_05FormatiertWieDie4_2)
{
    P8000Wdc::Config cfg;
    cfg.firmware = P8000Wdc::Config::Firmware::V4_0_05;
    Aufbau a(cfg, {}, [](Platte& p) { p.setzeFormatiert(7, 1, false); });
    ASSERT_TRUE(a.host.warteStatus(1, 200'000'000));
    ASSERT_EQ(a.host.ausfuehren(sektorKommando(0x04, 0, 7, 1, 1), 0, nullptr, 800'000'000).fehler, 0);
    const auto& s = a.platte.spur(7, 1);
    std::vector<int> folge;
    for (int i = 0; i < Platte::BYTES_JE_SPUR; ++i)
        if ((s[size_t(i)] & Platte::MARKE) && s[size_t(i + 1)] == 0xFE) folge.push_back(s[size_t(i + 5)] & 0xFF);
    ASSERT_GE(folge.size(), 18u);
    EXPECT_EQ(folge[0], 18);     // Kopf 1: 18 1 10 … (Kopfversatz)
    EXPECT_EQ(folge[1], 1);
    EXPECT_EQ(folge[2], 10);
}

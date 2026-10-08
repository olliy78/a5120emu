/**
 * @file test_z8_firmware.cpp
 * @brief Praxisprobe des Z8-Kerns (AP P19c): Original-Firmware des P8000-Terminals Typ 2
 *        (P8T_1_5.0) und der Tastatur K7673.09 auf einem Prüfstand nach
 *        doc/p8000/terminal_typ2.md §3 bzw. tastatur_k7673.md §2/§3.
 *
 * Kein Kartenmodell (das ist P20a/b) — nur so viel Umgebung, dass die Firmware ihre
 * Initialisierung bis in die Hauptschleife fährt und ihre Interruptwege benutzt:
 *  - Terminal: 2 KB RAM 1000–17FF (BWS + Zeilentabelle), 8275 an 1C00/1C01 (nur Protokoll),
 *    Schreibstrobes per LDC (2000 ZG2, 4000 ZG1, 8000 Klingel, C000 Tastatur-Rücksetzen),
 *    Bildende an P32 (IRQ0), Zeilenanforderung an P31 (IRQ2), Tastaturbyte an P2 + P33 (IRQ1),
 *    Host über P30 (UART 9600 Bd).
 *  - Tastatur: Matrix an P0/P1 (Zeile = P2.0–2), Sendefreigabe P30 = 0, P31 = 0;
 *    Takt P36 / Daten P37 (invertiert) werden an steigenden Taktflanken dekodiert.
 */
#include "tests/unit/primitives/z8_rig.h"

#include <fstream>
#include <iterator>
#include <set>

using z8test::SerielleQuelle;

namespace {

std::vector<uint8_t> abzug(const std::string& rel) {
    std::ifstream f(std::string(K1520_EPROM_DIR) + "/" + rel, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// ═════════════════════════════════════════════════════════════════════════════
// Terminal Typ 2
// ═════════════════════════════════════════════════════════════════════════════

struct Terminal {
    Z8 cpu{Z8Config::ub8840()};
    std::vector<uint8_t> rom = abzug("TERMINAL/P8T_1_5.0");
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x800, 0);
    std::vector<std::pair<uint16_t, uint8_t>> crt;    ///< 8275-Schreibzugriffe (Adresse, Wert)
    std::vector<uint16_t> strobes;                    ///< LDC-Schreiben ≥ 2000H (A15–A13)
    std::vector<uint16_t> zeilen;                     ///< Zeilenanfang je DMA (LDE in IRP31)
    std::vector<int> irqs;
    std::vector<uint8_t> gesendet;
    SerielleQuelle host{384};
    int tres = 0;

    Terminal() {
        cpu.programmLesen = [this](uint16_t a) { return a < rom.size() ? rom[a] : uint8_t(0xFF); };
        cpu.busLesen = [this](const Z8BusZyklus& c) -> uint8_t {
            if (c.dm && c.adresse >= 0x1000 && c.adresse < 0x1800) {
                if (cpu.letzterPc() == 0x00B3) zeilen.push_back(c.adresse);
                return ram[c.adresse - 0x1000];
            }
            if (c.dm && (c.adresse & 0xFFFE) == 0x1C00) return 0x00;   // 8275-Status: ruhig
            return 0xFF;
        };
        cpu.busSchreiben = [this](const Z8BusZyklus& c, uint8_t v) {
            if (c.dm && c.adresse >= 0x1000 && c.adresse < 0x1800) ram[c.adresse - 0x1000] = v;
            else if (c.dm && (c.adresse & 0xFFFE) == 0x1C00) crt.push_back({c.adresse, v});
            else if (!c.dm && c.adresse >= 0x2000) {
                strobes.push_back(uint16_t(c.adresse & 0xE000));
                if ((c.adresse & 0xE000) == 0xC000) { ++tres; cpu.setPin(3, 3, true); }   // Byte-voll weg
            }
        };
        cpu.p30Quelle = [this](uint64_t t) { return host.pegel(t); };
        cpu.onInterrupt = [this](int n, uint16_t, uint16_t) { irqs.push_back(n); };
        cpu.uartGesendet = [this](uint8_t b) { gesendet.push_back(b); };
        cpu.reset();
    }
    std::string bws() const {
        std::string s;
        for (int i = 0; i < 0x780; ++i) s += (ram[size_t(i)] >= 0x20 && ram[size_t(i)] < 0x7F) ? char(ram[size_t(i)]) : ' ';
        return s;
    }
    /// Läuft @p takte interne Takte; erzeugt Bild (62,8 Hz: Bildende + 24 Zeilenanforderungen).
    void lauf(uint64_t takte, bool video = true) {
        const uint64_t ende = cpu.takte + takte;
        constexpr uint64_t BILD = 58700, ZEILE = 2170;
        while (cpu.takte < ende) {
            const uint64_t t = cpu.takte;
            cpu.step();
            if (!video) continue;
            const uint64_t a = t % BILD, b = cpu.takte % BILD;
            auto ueber = [&](uint64_t x) { return a < x && b >= x; };
            if (ueber(100)) { cpu.setPin(3, 2, false); }            // Bildende ↓
            if (ueber(300)) { cpu.setPin(3, 2, true); }
            for (int z = 0; z < 24; ++z) {
                const uint64_t x = 3000 + uint64_t(z) * ZEILE;
                if (ueber(x)) cpu.setPin(3, 1, false);
                if (ueber(x + 50)) cpu.setPin(3, 1, true);
            }
        }
    }
    void taste(uint8_t byte) {   // Byte um eins links gedreht an P2 (FW: LD r1,P2 / RR r1), P33 ↓
        cpu.setPort(2, uint8_t(byte << 1 | byte >> 7));
        cpu.setPin(3, 3, false);
    }
};

}  // namespace

TEST(Z8Firmware, TerminalInitialisierungBisZurTastaturwarte) {
    Terminal t;
    ASSERT_EQ(t.rom.size(), 4096u);
    t.lauf(3'000'000);                                     // ≈ 0,8 s
    // Ports wie terminal_typ2.md §2
    EXPECT_EQ(t.cpu.p01m(), 0x96);
    EXPECT_EQ(t.cpu.p3m(), 0x51);
    EXPECT_EQ(t.cpu.sp & 0xFF, 0x80);
    // 8275: Reset 00, Parameter 4F 97 CC 5A, Preset E0 E0 (§4.1)
    ASSERT_GE(t.crt.size(), 7u);
    const std::vector<std::pair<uint16_t, uint8_t>> soll = {
        {0x1C01, 0x00}, {0x1C00, 0x4F}, {0x1C00, 0x97}, {0x1C00, 0xCC}, {0x1C00, 0x5A}, {0x1C01, 0xE0}, {0x1C01, 0xE0}};
    for (size_t i = 0; i < soll.size(); ++i) EXPECT_EQ(t.crt[i], soll[i]) << i;
    bool start = false;
    for (auto& w : t.crt) start |= w == std::pair<uint16_t, uint8_t>{0x1C01, 0x20};
    EXPECT_TRUE(start) << "Start Display 20H";
    // Strobes: Zeichensatz 1 (4000) und Tastatur-Rücksetzen (C000) beim Start
    ASSERT_GE(t.strobes.size(), 2u);
    EXPECT_EQ(t.strobes[0], 0x4000);
    EXPECT_EQ(t.strobes[1], 0xC000);
    // Einschaltmeldung im Bildspeicher, Firmware wartet auf das erste Tastaturbyte
    EXPECT_NE(t.bws().find("ADM31/9600 baud/Video Attr. on (c)zft/keaw"), std::string::npos) << t.bws();
    EXPECT_GE(t.cpu.pc, 0x0268); EXPECT_LE(t.cpu.pc, 0x026D);
    // Zeilentabelle 1780H: 24 Einträge (HI&0F)|LO, Zeile n bei 1000H + 50H·n
    for (int z = 0; z < 24; ++z) {
        const unsigned adr = 0x1000 + 0x50 * unsigned(z);
        EXPECT_EQ(t.ram[size_t(0x780 + z)], uint8_t(((adr >> 8) & 0x0F) | (adr & 0xF0))) << z;
    }
    // Interruptwege: Bildende (IRQ0), Zeilen-DMA (IRQ2), T1-Ende (IRQ5) — und die Zeilen in Folge
    std::set<int> arten(t.irqs.begin(), t.irqs.end());
    EXPECT_TRUE(arten.count(0)); EXPECT_TRUE(arten.count(2)); EXPECT_TRUE(arten.count(5));
    ASSERT_GE(t.zeilen.size(), 48u);
    std::vector<uint16_t> bild(t.zeilen.end() - 24, t.zeilen.end());
    std::set<uint16_t> eindeutig(bild.begin(), bild.end());
    EXPECT_EQ(eindeutig.size(), 24u);
    for (uint16_t a : bild) EXPECT_EQ((a - 0x1000) % 0x50, 0) << std::hex << a;
    // Schein-Sendebyte nach EI (MAIN: LD SIO,#00H)
    ASSERT_FALSE(t.gesendet.empty());
    EXPECT_EQ(t.gesendet[0], 0x00);
}

TEST(Z8Firmware, TerminalNachAAInDerHauptschleifeEchoUndKlingel) {
    Terminal t;
    t.lauf(3'000'000);
    t.taste(0xAA);                                         // Selbsttest gut
    t.lauf(500'000);
    EXPECT_GE(t.tres, 2);                                  // IRQ1 hat das Register zurückgesetzt
    EXPECT_EQ(t.bws().find("Error"), std::string::npos);
    // Hauptschleife 0285–029A
    EXPECT_GE(t.cpu.pc, 0x0100);
    // Host sendet "Hi" und BEL: Zeichen erscheinen im BWS, BEL ⇒ Strobe 8000H
    const uint64_t s = t.cpu.takte + 1000;
    t.host.senden(s, 'H'); t.host.senden(s + 11 * 384, 'i'); t.host.senden(s + 22 * 384, 0x07);
    t.lauf(2'000'000);
    EXPECT_NE(t.bws().find("Hi"), std::string::npos) << t.bws();
    bool klingel = false;
    for (uint16_t a : t.strobes) klingel |= a == 0x8000;
    EXPECT_TRUE(klingel);
    std::set<int> arten(t.irqs.begin(), t.irqs.end());
    EXPECT_TRUE(arten.count(1));                           // Tastatur
    EXPECT_TRUE(arten.count(3));                           // serieller Empfang
}

TEST(Z8Firmware, TerminalMeldetTastaturfehlerBeiFC) {
    Terminal t;
    t.lauf(3'000'000);
    t.taste(0xFC);
    t.lauf(500'000);
    EXPECT_NE(t.bws().find("Error Tastatur"), std::string::npos) << t.bws();
}

// ═════════════════════════════════════════════════════════════════════════════
// Tastatur K7673.09
// ═════════════════════════════════════════════════════════════════════════════

namespace {

struct Tastatur {
    Z8 cpu{Z8Config::ub8820()};                    // 2716 am Programmbus (0000–07FF)
    std::vector<uint8_t> rom = abzug("KEYBOARD/K7673.09");
    std::set<std::pair<int, int>> gedrueckt;       ///< (Zeile, Bit 0..15: P0.0–P0.7, P1.0–P1.7)
    std::vector<uint8_t> bytes;
    bool takt = false, imRahmen = false;
    int bit = 0;
    uint8_t wert = 0;

    Tastatur() {
        cpu.programmLesen = [this](uint16_t a) { return a < rom.size() ? rom[a] : uint8_t(0xFF); };
        cpu.portEingang = [this](int port, uint8_t) -> uint8_t {
            const int zeile = cpu.portPegel(2) & 7;
            uint8_t v = 0xFF;
            for (auto& k : gedrueckt)
                if (k.first == zeile && (k.second >> 3) == port) v = uint8_t(v & ~(1u << (k.second & 7)));
            return v;
        };
        cpu.portAusgang = [this](int port, uint8_t pegel, uint8_t) {
            if (port != 3) return;
            const bool neu = pegel & 0x40;
            if (neu && !takt) {                          // steigende Taktflanke: Daten stabil
                const bool d = pegel & 0x80;
                if (!imRahmen) { if (!d) { imRahmen = true; bit = 0; wert = 0; } }
                else {
                    if (!d) wert = uint8_t(wert | (1u << bit));   // Daten invertiert, LSB zuerst
                    if (++bit == 8) { bytes.push_back(wert); imRahmen = false; }
                }
            }
            takt = neu;
        };
        cpu.setPort(3, 0xF0);                            // P30 = 0 (frei), P31 = 0 (kein Neustart)
        cpu.reset();
    }
    void lauf(uint64_t takte) { cpu.laufeBis(cpu.takte + takte); }
};

}  // namespace

TEST(Z8Firmware, TastaturSendetAANachDemEinschalten) {
    Tastatur k;
    ASSERT_EQ(k.rom.size(), 2048u);
    k.lauf(200'000);
    EXPECT_EQ(k.cpu.p01m(), 0x4D);
    EXPECT_EQ(k.cpu.p2m(), 0x00);
    EXPECT_EQ(k.cpu.imr, 0x20 | 0x80);
    ASSERT_FALSE(k.bytes.empty());
    EXPECT_EQ(k.bytes[0], 0xAA);
    EXPECT_EQ(k.bytes.size(), 1u);
    EXPECT_GT(k.cpu.zaehler(1).abgelaufen, 0u);          // T1-Takt der Wiederholung läuft
}

TEST(Z8Firmware, TastaturMakeUndBreakEinerTaste) {
    Tastatur k;
    k.lauf(200'000);
    k.bytes.clear();
    k.gedrueckt.insert({0, 0});                          // Zeile 0, P0.0 = Taste „1" → 02H
    k.lauf(1'500'000);
    ASSERT_FALSE(k.bytes.empty());
    EXPECT_EQ(k.bytes[0], 0x02);
    k.gedrueckt.clear();
    const size_t n = k.bytes.size();
    k.lauf(1'500'000);
    ASSERT_GT(k.bytes.size(), n);
    EXPECT_EQ(k.bytes.back(), 0x82);
    // Zusatztaste mit E0: Zeile 0 P1.0 → E0 4D, Loslassen E0 CD
    k.bytes.clear();
    k.gedrueckt.insert({0, 8});
    k.lauf(1'500'000);
    k.gedrueckt.clear();
    k.lauf(1'500'000);
    ASSERT_GE(k.bytes.size(), 4u);
    EXPECT_EQ(k.bytes[0], 0xE0); EXPECT_EQ(k.bytes[1], 0x4D);
    EXPECT_EQ(k.bytes[k.bytes.size() - 2], 0xE0); EXPECT_EQ(k.bytes.back(), 0xCD);
}

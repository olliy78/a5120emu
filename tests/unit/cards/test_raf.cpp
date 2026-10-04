/**
 * @file test_raf.cpp
 * @brief RAM-Floppy RAF 128/512/2M: Registermodell, Sperre, Spiegelung, Lebenslauf — mit einem
 *        echten Z80, der OUT (C),r / IN r,(C) / INIR / INI / OTIR fährt (A8–A15 = Register B).
 *        doc/design/22_raf512.md §3, §5.1, AP-R1.
 */

#include <gtest/gtest.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "core/cards/raf/raf.h"
#include "core/primitives/z80.h"

namespace {

/// Bus + Karte + Z80 mit eigenem 64-KB-Speicher (der Speicher läuft NICHT über den Bus).
struct Rig {
    K1520Bus bus;
    RAF      raf;
    Z80      cpu;
    std::array<uint8_t, 65536> ram{};

    explicit Rig(RAF::Config c = {}) : raf(c) {
        raf.attachToBus(bus);
        raf.powerOn();
        cpu.readByte  = [this](uint16_t a) { return ram[a]; };
        cpu.writeByte = [this](uint16_t a, uint8_t d) { ram[a] = d; };
        cpu.readPort  = [this](uint16_t p) { return bus.ioRead(p); };
        cpu.writePort = [this](uint16_t p, uint8_t d) { bus.ioWrite(p, d); };
    }

    /// Programm bei 0100H ablegen und bis HALT fahren; BC/HL/A vorher setzen.
    void run(const std::vector<uint8_t>& code, uint16_t bc = 0, uint16_t hl = 0, uint8_t a = 0) {
        for (size_t i = 0; i < code.size(); ++i) ram[0x100 + i] = code[i];
        cpu.reset();
        cpu.PC = 0x100;
        cpu.BC = bc;
        cpu.HL = hl;
        cpu.A  = a;
        for (int i = 0; i < 100000 && !cpu.halted; ++i) cpu.step();
        ASSERT_TRUE(cpu.halted) << "Programm hat nicht angehalten";
    }

    uint8_t basis() const { return raf.config().basis; }

    /// Steuerregister laden: B = H, A = L, OUT (C),A mit C = basis+1.
    void latchSetzen(uint16_t l) {
        run({0xED, 0x79, 0x76}, uint16_t((l >> 8) << 8 | (basis() + 1)), 0, uint8_t(l));
    }
    /// OUT (basis) mit B als Bytezeiger.
    void ausgeben(uint8_t b, uint8_t wert) { run({0xED, 0x79, 0x76}, uint16_t(b << 8 | basis()), 0, wert); }
    /// IN A,(basis) mit B als Bytezeiger.
    uint8_t einlesen(uint8_t b) { run({0xED, 0x78, 0x76}, uint16_t(b << 8 | basis())); return cpu.A; }

    /// Sektor schreiben, wie das BIOS: Latch, B = 128, OTIR.
    void sektorSchreiben(uint16_t l, const std::array<uint8_t, 128>& puffer) {
        latchSetzen(l);
        std::copy(puffer.begin(), puffer.end(), ram.begin() + 0x4000);
        run({0xED, 0xB3, 0x76}, uint16_t(128 << 8 | basis()), 0x4000);
    }
    /// Sektor lesen, wie das BIOS: Latch, B = 127, INIR + ein INI.
    std::array<uint8_t, 128> sektorLesen(uint16_t l) {
        latchSetzen(l);
        run({0xED, 0xB2, 0xED, 0xA2, 0x76}, uint16_t(127 << 8 | basis()), 0x5000);
        std::array<uint8_t, 128> r;
        std::copy(ram.begin() + 0x5000, ram.begin() + 0x5080, r.begin());
        return r;
    }
};

std::array<uint8_t, 128> muster(uint8_t start) {
    std::array<uint8_t, 128> m;
    for (int i = 0; i < 128; ++i) m[i] = uint8_t(start + i * 3);
    return m;
}

}  // namespace

TEST(Raf, SektorSchreibenUndLesenMitOtirInir) {
    Rig r;
    const auto m = muster(0x11);
    r.sektorSchreiben(0x0123, m);
    EXPECT_EQ(r.sektorLesen(0x0123), m);
    // Rückwärtsreihenfolge: das erste Puffer-Byte landet auf Byte 127 des Sektors.
    EXPECT_EQ(r.raf.peek(0x123 * 128 + 127), m[0]);
    EXPECT_EQ(r.raf.peek(0x123 * 128 + 0), m[127]);
    // Nachbarsektoren unberührt.
    EXPECT_EQ(r.raf.peek(0x122 * 128 + 5), 0x00);
    EXPECT_EQ(r.raf.peek(0x124 * 128 + 5), 0x00);
}

TEST(Raf, InirLaeuftRueckwaerts) {
    Rig r;
    for (int i = 0; i < 128; ++i) r.raf.poke(0x7 * 128 + i, uint8_t(i));
    const auto g = r.sektorLesen(0x0007);
    for (int i = 0; i < 128; ++i) EXPECT_EQ(g[i], 127 - i) << i;
}

TEST(Raf, BEinhundertachtundzwanzigTrifftByteNull_KeinAutoinkrement) {
    Rig r;
    r.latchSetzen(0x0005);
    r.ausgeben(0x80, 0xA1);                 // A15 gesetzt, A8–A14 = 0 -> Byte 0
    EXPECT_EQ(r.raf.peek(5 * 128), 0xA1);
    r.ausgeben(0x00, 0xA2);                 // dasselbe Byte
    EXPECT_EQ(r.raf.peek(5 * 128), 0xA2);
    r.ausgeben(0x01, 0xA3);
    r.ausgeben(0x01, 0xA4);                 // zweimal Byte 1: kein Autoinkrement
    EXPECT_EQ(r.raf.peek(5 * 128 + 1), 0xA4);
    EXPECT_EQ(r.raf.peek(5 * 128 + 2), 0x00);
    EXPECT_EQ(r.einlesen(0x81), 0xA4);      // A15 beim Bytezeiger ohne Wirkung
    EXPECT_EQ(r.einlesen(0xFF), 0x00);      // Byte 127
}

TEST(Raf, SperreBeiFF_80_10_90_NichtBei00) {
    Rig r;
    r.raf.poke(0, 0x77);
    for (uint8_t h : {0xFF, 0x80, 0x10, 0x90}) {
        r.latchSetzen(uint16_t(h << 8));
        EXPECT_TRUE(r.raf.gesperrt()) << int(h);
        EXPECT_EQ(r.einlesen(0x00), 0xFF) << "gesperrt liest FFH, B=" << int(h);
        r.ausgeben(0x00, 0x55);
        EXPECT_EQ(r.raf.peek(0), 0x77) << "Schreiben bei Sperre verpufft, B=" << int(h);
    }
    r.latchSetzen(0x0000);
    EXPECT_FALSE(r.raf.gesperrt());
    EXPECT_EQ(r.einlesen(0x00), 0x77);
    r.ausgeben(0x00, 0x55);
    EXPECT_EQ(r.raf.peek(0), 0x55);
}

TEST(Raf, Sektor4095DaSektor4096LiestFFUndSchreibtNichtNachSektor0) {
    Rig r;
    const auto m = muster(0x40);
    r.sektorSchreiben(0x0FFF, m);
    EXPECT_EQ(r.sektorLesen(0x0FFF), m);
    EXPECT_EQ(r.raf.peek(4095 * 128 + 127), m[0]);

    const auto n0 = muster(0xC0);
    r.sektorSchreiben(0x0000, n0);
    const auto s0 = r.sektorLesen(0x0000);
    r.latchSetzen(0x1000);                  // Sektor 4096: A12 = Sperre
    EXPECT_EQ(r.einlesen(0x00), 0xFF);
    r.run({0xED, 0xB3, 0x76}, uint16_t(128 << 8 | r.basis()), 0x4000);   // OTIR dorthin
    EXPECT_EQ(r.sektorLesen(0x0000), s0);
}

TEST(Raf, Sektor8192SpiegeltAufSektor0_A13A14WerdenNichtAusgewertet) {
    Rig r;
    r.sektorSchreiben(0x0000, muster(0x01));
    EXPECT_EQ(r.sektorLesen(0x2000), muster(0x01));   // A13
    EXPECT_EQ(r.sektorLesen(0x4000), muster(0x01));   // A14
    EXPECT_EQ(r.sektorLesen(0x6000), muster(0x01));
    r.sektorSchreiben(0x2005, muster(0x99));          // Schreiben über den Spiegel
    EXPECT_EQ(r.sektorLesen(0x0005), muster(0x99));
}

TEST(Raf, ResetErhaeltInhaltPowerOnNicht) {
    Rig r;
    r.sektorSchreiben(0x0010, muster(0x21));
    EXPECT_FALSE(r.raf.gesperrt());
    r.raf.reset();
    EXPECT_TRUE(r.raf.gesperrt());
    EXPECT_EQ(r.raf.latch(), 0xFFFF);
    EXPECT_EQ(r.raf.peek(0x10 * 128 + 127), muster(0x21)[0]);
    EXPECT_EQ(r.sektorLesen(0x0010), muster(0x21));
    r.raf.powerOn();
    EXPECT_TRUE(r.raf.gesperrt());
    EXPECT_EQ(r.raf.peek(0x10 * 128 + 127), 0x00);
}

/// raftst aus RAF2X24O.MAC (Ports 88H/8AH/8CH/8EH), Byte für Byte als Z80-Code.
/// Ergebnis je Karte (Spuren zu 16 KB) ab 0200H.
TEST(Raf, RaftstFindet32SpurenAn88HUndNullAnDenLeerenPorts) {
    Rig r;
    const std::vector<uint8_t> raftst = {
        0x21, 0x00, 0x02,         // 0100 LD HL,0200H
        0x01, 0x88, 0x04,         // 0103 LD BC,0488H   (4 Karten ab 88H)
        0xC5,                     // 0106 raftst1: PUSH BC
        0xE5,                     // 0107 PUSH HL
        0x06, 0x00,               // 0108 LD B,0
        0xC5,                     // 010A raftst2: PUSH BC
        0xAF,                     // 010B XOR A
        0xCD, 0x50, 0x01,         // 010C CALL raftrs2
        0x3E, 0x5A,               // 010F LD A,5AH
        0xED, 0x68,               // 0111 IN L,(C)
        0xED, 0x79,               // 0113 OUT (C),A
        0x05,                     // 0115 DEC B
        0x2F,                     // 0116 CPL
        0xED, 0x60,               // 0117 IN H,(C)
        0xED, 0x79,               // 0119 OUT (C),A
        0x04,                     // 011B INC B
        0xED, 0x78,               // 011C IN A,(C)
        0xFE, 0x5A,               // 011E CP 5AH
        0xED, 0x69,               // 0120 OUT (C),L
        0x20, 0x0E,               // 0122 JR NZ,raftst3
        0x05,                     // 0124 DEC B
        0xED, 0x78,               // 0125 IN A,(C)
        0xFE, 0xA5,               // 0127 CP A5H
        0xED, 0x61,               // 0129 OUT (C),H
        0x20, 0x05,               // 012B JR NZ,raftst3
        0xC1,                     // 012D POP BC
        0x04,                     // 012E INC B
        0x20, 0xD9,               // 012F JR NZ,raftst2
        0xC5,                     // 0131 PUSH BC
        0xC1,                     // 0132 raftst3: POP BC
        0x78,                     // 0133 LD A,B
        0x06, 0xFF,               // 0134 LD B,0FFH   (Schreibschutz ein)
        0x0C,                     // 0136 INC C
        0xED, 0x41,               // 0137 OUT (C),B
        0xE1,                     // 0139 POP HL
        0x77,                     // 013A LD (HL),A
        0x23,                     // 013B INC HL
        0xC1,                     // 013C POP BC
        0x0C, 0x0C,               // 013D INC C / INC C
        0x10, 0xC5,               // 013F DJNZ raftst1
        0x76,                     // 0141 HALT
    };
    ASSERT_EQ(raftst.size(), 0x42u);
    // raftrs2 bei 0150H
    std::vector<uint8_t> code = raftst;
    code.resize(0x50, 0x00);
    const std::vector<uint8_t> rs2 = {
        0x87,                     // ADD A,A
        0xCB, 0x38,               // SRL B
        0x1F,                     // RRA
        0x0C,                     // INC C
        0xED, 0x79,               // OUT (C),A   (Steuerport)
        0x0D,                     // DEC C
        0x06, 0x7F,               // LD B,127
        0xAF,                     // XOR A
        0xC9,                     // RET
    };
    code.insert(code.end(), rs2.begin(), rs2.end());
    r.ram[0x0200] = r.ram[0x0201] = r.ram[0x0202] = r.ram[0x0203] = 0xEE;
    r.run(code);
    EXPECT_EQ(r.ram[0x0200], 32) << "RAF512 an 88H: 32 Spuren zu 16 KB";
    EXPECT_EQ(r.ram[0x0201], 0);
    EXPECT_EQ(r.ram[0x0202], 0);
    EXPECT_EQ(r.ram[0x0203], 0);
    EXPECT_TRUE(r.raf.gesperrt());          // der Test sperrt am Ende
    EXPECT_EQ(r.raf.peek(0), 0x00);         // Altinhalt restauriert
    EXPECT_EQ(r.raf.peek(127), 0x00);
}

TEST(Raf, Raf128Masken) {
    Rig r({RAF::Typ::RAF128, 0x88});
    EXPECT_EQ(r.raf.sektoren(), 1024u);
    EXPECT_EQ(r.raf.kapazitaet(), 128u * 1024u);
    r.sektorSchreiben(0x03FF, muster(0x31));
    EXPECT_EQ(r.sektorLesen(0x03FF), muster(0x31));
    for (uint16_t l : {0x0400, 0x8000, 0x8400, 0xFFFF}) {   // A10 bzw. A15
        r.latchSetzen(l);
        EXPECT_TRUE(r.raf.gesperrt()) << std::hex << l;
        EXPECT_EQ(r.einlesen(0x00), 0xFF);
    }
    // A11–A14 wertet RAF128 nicht aus -> Spiegel auf Sektor 0x03FF.
    EXPECT_EQ(r.sektorLesen(0x0BFF), muster(0x31));
    EXPECT_EQ(r.sektorLesen(0x7BFF), muster(0x31));
}

TEST(Raf, Raf2mMasken) {
    Rig r({RAF::Typ::RAF2M, 0x88});
    EXPECT_EQ(r.raf.sektoren(), 16384u);
    EXPECT_EQ(r.raf.kapazitaet(), 2u * 1024u * 1024u);
    r.sektorSchreiben(0x3FFF, muster(0x51));
    EXPECT_EQ(r.sektorLesen(0x3FFF), muster(0x51));
    EXPECT_EQ(r.raf.peek(16383u * 128 + 127), muster(0x51)[0]);
    for (uint16_t l : {0x4000, 0x8000, 0xC000, 0xFFFF}) {   // A14 bzw. A15
        r.latchSetzen(l);
        EXPECT_TRUE(r.raf.gesperrt()) << std::hex << l;
        EXPECT_EQ(r.einlesen(0x00), 0xFF);
    }
}

TEST(Raf, AbweichendeBasisUndPortbelegung) {
    Rig r({RAF::Typ::RAF512, 0x90});
    r.sektorSchreiben(0x0001, muster(0x61));
    EXPECT_EQ(r.sektorLesen(0x0001), muster(0x61));
    // An 88H/89H liegt nichts: der Bus liefert FFH.
    r.run({0xED, 0x78, 0x76}, 0x0088);
    EXPECT_EQ(r.cpu.A, 0xFF);
    // Doppelbelegung: ein ANDERES Gerät auf demselben Port -> Ausnahme; dieselbe Karte erneut: still.
    RAF zweite({RAF::Typ::RAF512, 0x91});
    EXPECT_THROW(zweite.attachToBus(r.bus), std::runtime_error);
    EXPECT_NO_THROW(r.raf.attachToBus(r.bus));
}

TEST(Raf, SaveStateUndRohdatei) {
    Rig r;
    r.sektorSchreiben(0x0042, muster(0x71));
    std::vector<uint8_t> blob;
    r.raf.saveState(blob);

    Rig s;
    const uint8_t* p = blob.data();
    ASSERT_TRUE(s.raf.loadState(p, blob.data() + blob.size()));
    EXPECT_EQ(p, blob.data() + blob.size());
    EXPECT_EQ(s.raf.latch(), r.raf.latch());
    EXPECT_EQ(s.sektorLesen(0x0042), muster(0x71));

    // Falscher/zu kurzer Block ändert nichts.
    Rig t;
    const uint8_t* q = blob.data();
    EXPECT_FALSE(t.raf.loadState(q, blob.data() + blob.size() - 1));
    EXPECT_EQ(t.raf.peek(0x42 * 128 + 127), 0x00);
    Rig k({RAF::Typ::RAF128, 0x88});
    q = blob.data();
    EXPECT_FALSE(k.raf.loadState(q, blob.data() + blob.size()));

    const auto pfad = (std::filesystem::temp_directory_path() / "k1520_test_raf.bin").string();
    ASSERT_TRUE(r.raf.speichereInhalt(pfad));
    EXPECT_EQ(std::filesystem::file_size(pfad), 512u * 1024u);
    Rig u;
    ASSERT_TRUE(u.raf.ladeInhalt(pfad));
    EXPECT_EQ(u.raf.peek(0x42 * 128 + 127), muster(0x71)[0]);
    // Falsche Größe: false, Inhalt unverändert.
    { std::ofstream f(pfad, std::ios::binary | std::ios::trunc); f << "zu klein"; }
    EXPECT_FALSE(u.raf.ladeInhalt(pfad));
    EXPECT_EQ(u.raf.peek(0x42 * 128 + 127), muster(0x71)[0]);
    EXPECT_FALSE(u.raf.ladeInhalt(pfad + ".fehlt"));
    std::remove(pfad.c_str());
}

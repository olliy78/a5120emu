/**
 * @file test_dbg_z8.cpp
 * @brief Debuggerkontext des Z8 (tools/dbg_z8.h): jedes Kommando auf einem kleinen Prüfstand.
 */
#include "tools/dbg_z8.h"

#include <gtest/gtest.h>

namespace {

struct Stand {
    std::vector<uint8_t> prog = std::vector<uint8_t>(65536, 0xFF);
    std::vector<uint8_t> daten = std::vector<uint8_t>(65536, 0);
    Z8 cpu;
    dbgz8::Kontext k;
    Stand() : k(cpu, speicher()) {
        cpu.programmLesen = [this](uint16_t a) { return prog[a]; };
        cpu.busLesen = [this](const Z8BusZyklus& c) { return c.datenspeicher ? daten[c.adresse] : prog[c.adresse]; };
        cpu.busSchreiben = [this](const Z8BusZyklus& c, uint8_t v) { (c.datenspeicher ? daten : prog)[c.adresse] = v; };
        const auto r = z8asm::assemble(
            " ORG 0\n DW 0, 0, 0, 0, 0, IRQ5\n"
            " ORG 0CH\n"
            " LD SPL,#80H\n"
            " LD P01M,#96H\n"
            " LD PRE1,#0FH\n LD T1,#10\n LD TMR,#0CH\n"
            " LD IMR,#0A0H\n EI\n"
            " CALL UP\n"
            "HAUPT: INC r0\n JR HAUPT\n"
            "UP: LD 40H,#42H\n RET\n"
            "IRQ5: INC r2\n IRET\n");
        EXPECT_TRUE(r.ok());
        for (auto& kv : r.bild) prog[kv.first] = kv.second;
        cpu.reset(); cpu.step();
    }
    dbgz8::Speicher speicher() {
        dbgz8::Speicher s;
        s.prog = [this](uint16_t a) { return prog[a]; };
        s.progSchreiben = [this](uint16_t a, uint8_t v) { prog[a] = v; };
        s.daten = [this](uint16_t a) { return daten[a]; };
        s.datenSchreiben = [this](uint16_t a, uint8_t v) { daten[a] = v; };
        return s;
    }
    std::string run(const std::string& z) { std::string o; EXPECT_TRUE(k.befehl(z, o)); return o; }
};

bool hat(const std::string& s, const std::string& t) { return s.find(t) != std::string::npos; }

}  // namespace

TEST(DbgZ8, RegisterSchrittUndUnassembler) {
    Stand s;
    EXPECT_TRUE(hat(s.run("r"), "PC=000C"));
    EXPECT_TRUE(hat(s.run("u 0CH 2"), "LD SPL,#80H"));
    const std::string o = s.run("s 2");
    EXPECT_TRUE(hat(o, "LD P01M,#96H"));
    EXPECT_EQ(s.cpu.pc, 0x0012);
    EXPECT_TRUE(hat(s.run("r"), "P01M=96"));
}

TEST(DbgZ8, HaltepunktLaufNextFin) {
    // Lage: 001F CALL UP, 0022 HAUPT, 0029 IRQ5
    Stand s;
    s.k.maxSchritte = 5000;
    EXPECT_TRUE(hat(s.run("u 1FH 1"), "CALL 0025H"));
    s.run("b 1FH");
    EXPECT_TRUE(hat(s.run("g"), "Haltepunkt 001F"));
    s.run("n");                                  // über CALL
    EXPECT_EQ(s.cpu.pc, 0x0022);
    EXPECT_EQ(s.cpu.reg[0x40], 0x42);
    s.run("bd all");
    EXPECT_TRUE(hat(s.run("bl"), "keine"));
    s.run("b 29H");                               // IRQ5-Routine (T1)
    EXPECT_TRUE(hat(s.run("g"), "Haltepunkt 0029"));
    s.run("bd 29H");
    EXPECT_TRUE(hat(s.run("fin"), "-> "));
    EXPECT_TRUE(s.cpu.pc == 0x0022 || s.cpu.pc == 0x0023);
    EXPECT_TRUE(hat(s.run("g 23H"), "0023"));
}

TEST(DbgZ8, PeripherieSichten) {
    Stand s;
    s.k.maxSchritte = 3000;
    s.run("g");
    EXPECT_TRUE(hat(s.run("t"), "T1: zählt"));
    EXPECT_TRUE(hat(s.run("t"), "÷3"));
    EXPECT_TRUE(hat(s.run("irq"), "Rangfolge"));
    EXPECT_TRUE(hat(s.run("irqlog"), "IRQ5"));
    EXPECT_TRUE(hat(s.run("ports"), "A12–A15"));
    EXPECT_TRUE(hat(s.run("uart"), "UART: aus"));
    EXPECT_TRUE(hat(s.run("sicht"), "letzter Buszyklus"));
    EXPECT_TRUE(hat(s.run("dr 0F0H 0FFH"), "F0:"));
    EXPECT_TRUE(hat(s.run("takte"), "interne Takte"));
}

TEST(DbgZ8, AendernAssemblierenPinsReset) {
    Stand s;
    s.run("a 100H LD r5,#77H");
    EXPECT_EQ(s.prog[0x100], 0x5C);
    EXPECT_EQ(s.prog[0x101], 0x77);
    EXPECT_TRUE(hat(s.run("a 100H FOO"), "Fehler"));
    s.run("e 200H 1 2 3");
    EXPECT_EQ(s.prog[0x202], 3);
    s.run("ee 1000H 0AAH");
    EXPECT_EQ(s.daten[0x1000], 0xAA);
    EXPECT_TRUE(hat(s.run("de 1000H 1"), "AA"));
    s.run("er 40H 12H");
    EXPECT_EQ(s.cpu.reg[0x40], 0x12);
    s.run("set pc 100H");
    EXPECT_EQ(s.cpu.pc, 0x100);
    s.run("set flags 0C0H");
    EXPECT_TRUE(hat(s.run("r"), "[CZ...."));
    s.run("port 3 0F7H");
    EXPECT_EQ(s.cpu.pinEingang(3), 0xF7);
    s.run("pin 3 3 1");
    EXPECT_EQ(s.cpu.pinEingang(3), 0xFF);
    EXPECT_TRUE(hat(s.run("reset"), "000C"));
    EXPECT_TRUE(hat(s.run("help"), "cpu z8"));
    EXPECT_TRUE(hat(s.run("xyz"), "unbekannt"));
    std::string o;
    EXPECT_FALSE(s.k.befehl("q", o));
}

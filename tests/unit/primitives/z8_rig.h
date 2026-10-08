// Prüfstand für die Z8-Primitive: Programmbus (64 KB), externer Programm- und Datenspeicher
// (je 64 KB, getrennt über /DM), Busprotokoll, Assembler tools/z8/z8_asm.h.
#pragma once
#include "core/primitives/z8.h"
#include "tools/z8/z8_asm.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace z8test {

struct BusEintrag { Z8BusZyklus c; uint8_t d; };

struct Rig {
    Z8 cpu;
    std::vector<uint8_t> prog = std::vector<uint8_t>(65536, 0xFF);     ///< Programmbus
    std::vector<uint8_t> extProg = std::vector<uint8_t>(65536, 0xFF);  ///< extern, /DM inaktiv
    std::vector<uint8_t> extDaten = std::vector<uint8_t>(65536, 0x00); ///< extern, /DM aktiv
    std::vector<BusEintrag> bus;          ///< alle externen Zyklen
    std::vector<std::pair<int, uint8_t>> portLog;   ///< (Port, Pegel) bei jeder Ausgangsänderung
    std::vector<uint8_t> gesendet, empfangen;
    int wartetakte = 0;                   ///< je externem Zyklus
    z8asm::Ergebnis asmErg;

    explicit Rig(const Z8Config& c = Z8Config::ub8840()) : cpu(c) {
        cpu.programmLesen = [this](uint16_t a) { return prog[a]; };
        cpu.busLesen = [this](const Z8BusZyklus& c) {
            const uint8_t v = c.datenspeicher ? extDaten[c.adresse] : extProg[c.adresse];
            bus.push_back({c, v});
            if (wartetakte) cpu.addWaitCycles(wartetakte);
            return v;
        };
        cpu.busSchreiben = [this](const Z8BusZyklus& c, uint8_t v) {
            (c.datenspeicher ? extDaten : extProg)[c.adresse] = v;
            bus.push_back({c, v});
            if (wartetakte) cpu.addWaitCycles(wartetakte);
        };
        cpu.portAusgang = [this](int p, uint8_t v, uint8_t) { portLog.push_back({p, v}); };
        cpu.uartGesendet = [this](uint8_t b) { gesendet.push_back(b); };
        cpu.uartEmpfangen = [this](uint8_t b) { empfangen.push_back(b); };
    }

    /// Quelltext assemblieren und ablegen (unter der Programmbusgrenze → prog, sonst extProg).
    void load(const std::string& src) {
        asmErg = z8asm::assemble(src);
        for (auto& f : asmErg.fehler) ADD_FAILURE() << "asm Zeile " << f.zeile << ": " << f.text;
        for (auto& kv : asmErg.bild) {
            if (kv.first < cpu.config().programmbus) prog[kv.first] = kv.second;
            else extProg[kv.first] = kv.second;
        }
    }
    long marke(const std::string& m) const {
        auto it = asmErg.marken.find(z8asm::detail::gross(m));
        EXPECT_NE(it, asmErg.marken.end()) << "Marke " << m;
        return it == asmErg.marken.end() ? -1 : it->second;
    }
    /// Reset ausführen (ein Schritt), Protokolle leeren.
    void boot() { cpu.reset(); cpu.step(); bus.clear(); portLog.clear(); }

    /// Programm ab 000CH (Vektoren davor frei), endet mit `ENDE: JR ENDE`.
    void prg(const std::string& body, const std::string& vorspann = "") {
        load(vorspann + "\n ORG 0CH\n" + body + "\nENDE: JR ENDE\n");
        boot();
    }
    /// Bis PC = ENDE; Rückgabe = Takte.
    uint64_t lauf(int maxSchritte = 100000) {
        const long e = marke("ENDE");
        const uint64_t t0 = cpu.takte;
        for (int i = 0; i < maxSchritte && cpu.pc != e; ++i) cpu.step();
        EXPECT_EQ(cpu.pc, e) << "ENDE nicht erreicht";
        return cpu.takte - t0;
    }
    /// Einen Befehl als Bytes an PC ablegen und ausführen; Rückgabe = Takte.
    int eins(std::initializer_list<uint8_t> bytes, uint16_t at = 0x0100) {
        uint16_t a = at;
        for (uint8_t b : bytes) prog[a++] = b;
        cpu.pc = at;
        return cpu.step();
    }
    uint8_t& r(uint8_t a) { return cpu.reg[a]; }
};

/// Leitung zum seriellen Eingang P30: Bytes als 8N1-Rahmen zeitgenau (Takte je Bit).
struct SerielleQuelle {
    struct Rahmen { uint64_t start; uint8_t byte; };
    std::vector<Rahmen> rahmen;
    uint64_t bitTakte;
    explicit SerielleQuelle(uint64_t bt) : bitTakte(bt) {}
    void senden(uint64_t ab, uint8_t b) { rahmen.push_back({ab, b}); }
    bool pegel(uint64_t t) const {
        for (auto& r : rahmen) {
            if (t < r.start) continue;
            const uint64_t bit = (t - r.start) / bitTakte;
            if (bit == 0) return false;                         // Startbit
            if (bit <= 8) return (r.byte >> (bit - 1)) & 1;     // Daten LSB zuerst
        }
        return true;                                            // Ruhe/Stopp
    }
};

} // namespace z8test

/**
 * @file dbg_machine.h
 * @brief Die Maschine hinter k1520dbg — A5120 ODER K8915 (§8a AP-E4d).
 *
 * Der Debugger ist über Jahre am A5120 gewachsen und spricht die Maschine an rund
 * 200 Stellen als `m.xxx()` an.  Statt jede Stelle zu verzweigen, steht hier EINE
 * Klasse mit genau diesen Methodennamen, die an die gewählte Maschine weiterreicht:
 *
 *  - **gemeinsam** (Lauf, Speicher in CPU-Sicht, Tastatur, Disketten, Haltepunkt-
 *    Schnittstelle): beide Maschinen haben dieselbe Semantik — Beobachter vor jedem
 *    Befehl, Halt VOR dem Befehl über `stop()` + `abortBeforeExecute`, `run()` kehrt
 *    dann mit 0 Takten zurück;
 *  - **nur A5120** (ZVE2, /BUSRQ, DMA-Beobachter, Savestates, BS-PIO, K8025): am K8915
 *    ruhige Ersatzwerte (kein /BUSRQ, „ZVE2 im Reset“), damit gemeinsame Anzeigen wie
 *    `where`/`stateLine` nicht verzweigen müssen.  Die KOMMANDOS, die es nur am A5120
 *    gibt, fängt der Debugger vorher ab und meldet „nicht vorhanden“
 *    (@ref nurA5120Kommando) — hier wird nichts davon wirklich ausgeführt;
 *  - **nur K8915**: über @ref k8915() (A8H-Speicherbild, Bänke, ATS, K7672).
 *
 * Header-only, weil nur k1520dbg sie benutzt; boot_trace fährt den K8915 in einem
 * eigenen Zweig (tools/boot_trace_k8915.cpp).
 *
 * @license MIT
 */
#pragma once
#include "core/machines/a5120/a5120.h"
#include "core/machines/k8915/k8915.h"
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace dbgm {

/** @brief Eine Interruptquelle der Daisy-Chain (maschinenneutral, für `ivt`). */
using IntSource = A5120Machine::IntSource;

/** @brief Maschinenwahl aus `--machine`; false bei unbekanntem Namen. */
inline bool parseMachine(const std::string& s, bool& k8915) {
    if (s == "a5120" || s == "A5120") { k8915 = false; return true; }
    if (s == "k8915" || s == "K8915") { k8915 = true;  return true; }
    return false;
}

/**
 * @brief Kommandos, die es am K8915 nicht gibt (ZVE2, /BUSRQ-/DMA-Ereignisse,
 *        Snapshots/Reverse/Savestates).  Unit-getestet über tests/cli.
 */
inline bool nurA5120Kommando(const std::string& cmd) {
    static const std::set<std::string> s = {
        "s2", "b2", "bd2", "be2", "bdis2", "bi2", "rj2",      // ZVE2
        "bbusrq",                                            // /BUSRQ-Flanken (bxfer geht seit AP-E4f auch am K8915)
        "snap", "restore", "rs", "bs", "rc",                 // Snapshots / Reverse
        "savestate", "loadstate",                            // Zustand auf Platte
    };
    return s.count(cmd) != 0;
}

/**
 * @brief Anzeige einer A8H-Abbildung als 16 Zeichen, je 4-KB-Seite eins:
 *        `R` Boot-ROM, `1` Bank 1, `2` Bank 2, `.` Systembus (K7024-Bildspeicher u. a.).
 */
inline std::string speicherbild(const K8915Zre& zre) {
    std::string s;
    for (int p = 0; p < 16; ++p) {
        switch (zre.ortVon((uint16_t)(p << 12)).quelle) {
            case K8915Zre::Quelle::Rom:   s += 'R'; break;
            case K8915Zre::Quelle::Bank1: s += '1'; break;
            case K8915Zre::Quelle::Bank2: s += '2'; break;
            case K8915Zre::Quelle::Bus:   s += '.'; break;
        }
    }
    return s;
}

/** @brief Die leuchtenden Lampen des Anzeigefelds 61H (aktiv low, §3.6). */
inline std::string lampen61(uint8_t v) {
    static const char* n[4] = {"Input File", "Output File", "RUN Mode", "ERROR"};
    std::string s;
    for (int b = 4; b < 8; ++b)
        if (!(v & (1u << b))) { if (!s.empty()) s += ", "; s += n[b - 4]; }
    return s.empty() ? "keine" : s;
}

class DbgMachine {
public:
    explicit DbgMachine(bool k8915) {
        if (k8915) k8_ = std::make_unique<K8915Machine>();
        else       a5_ = std::make_unique<A5120Machine>();
    }

    bool isK8915() const { return k8_ != nullptr; }
    const char* name() const { return k8_ ? "K8915" : "A5120"; }
    /// Anzeigename der (Haupt-)CPU in Meldungen: am A5120 „ZVE1“ (es gibt zwei).
    const char* cpuName() const { return k8_ ? "CPU" : "ZVE1"; }
    A5120Machine* a5120() { return a5_.get(); }
    K8915Machine* k8915() { return k8_.get(); }
    K1520Machine& base() { return k8_ ? static_cast<K1520Machine&>(*k8_) : *a5_; }

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn() { base().powerOn(); }
    void reset()   { base().reset(); }
    int  run(int n) { return base().run(n); }
    void stop()    { base().stop(); }
    void clearStop() { if (k8_) k8_->clearStop(); else a5_->clearStop(); }

    // ─── Disketten ───────────────────────────────────────────────────────────
    bool mountDisk(int d, const std::string& p, const std::string& f, bool wp) {
        return base().mountDisk(d, p, f, wp);
    }
    std::string lastError() { return base().lastError(); }
    std::string diskNotice(int d) { return base().diskNotice(d); }

    // ─── Tastatur (über die Warteschlange der Maschine) ─────────────────────
    void keyPress(uint32_t k, bool sh, bool ct) { base().keyPress(k, sh, ct); }
    void keyRelease(uint32_t k) { base().keyRelease(k); }

    // ─── Speicher/E/A in CPU-Sicht (K8915: mit A8H-Abbildung) ────────────────
    uint8_t memReadDebug(uint16_t a) { return base().memReadDebug(a); }
    void    memWriteDebug(uint16_t a, uint8_t v) { base().memWriteDebug(a, v); }

    /**
     * @brief Ein Zeichen des Textbildes (Zeile, Spalte).  A5120: Bildspeicher F800H
     *        über die CPU-Sicht (unverändert gegenüber vor AP-E4d).  K8915: direkt von
     *        der K7024 bei 1000H — über die CPU wäre er bei A8H-Bit0 = 1 vom RAM
     *        verdeckt; Bit 7 (Cursor) wird ausgeblendet, sonst verschwände das
     *        Zeichen unter dem Cursor aus jeder Suche.
     */
    uint8_t screenByte(int row, int col) {
        if (k8_) return (uint8_t)(k8_->screenChar(col, row) & 0x7F);
        return a5_->memReadDebug((uint16_t)(0xF800 + row * 80 + col));
    }

    // ─── CPU ─────────────────────────────────────────────────────────────────
    Z80& cpuDebug() { return k8_ ? k8_->zre().cpu() : a5_->cpuDebug(); }
    uint16_t cpuPC() { return cpuDebug().PC; }
    uint16_t cpuSP() { return cpuDebug().SP; }
    uint64_t cpuCycles() { return cpuDebug().cycles; }
    /// Maschinenuhr: am A5120 beide CPUs, am K8915 = CPU-Takte samt /WAIT-Takten.
    uint64_t machineCycles() { return k8_ ? k8_->totalCycles() : a5_->machineCycles(); }
    bool isRomEnabled() {
        if (k8_) return k8_->zre().ortVon(0x0000).quelle == K8915Zre::Quelle::Rom;
        return a5_->isRomEnabled();
    }

    void setCpuTraceCallback(std::function<void(const Z80&)> cb) {
        if (k8_) k8_->setCpuTraceCallback(std::move(cb));
        else     a5_->setCpuTraceCallback(std::move(cb));
    }
    void setZVE2TraceCallback(std::function<void(const Z80&)> cb) {
        if (a5_) a5_->setZVE2TraceCallback(std::move(cb));   // K8915: keine ZVE2
    }
    void setBusTrace(K1520Bus::BusTrace cb) {
        if (k8_) k8_->setBusTrace(std::move(cb));
        else     a5_->setBusTrace(std::move(cb));
    }

    // ─── Nur A5120 — am K8915 ruhige Ersatzwerte (s. Dateikopf) ──────────────
    Z80& zve2Debug() { return a5_ ? a5_->zve2Debug() : dummy_zve2_; }
    uint16_t zve2PC() { return a5_ ? a5_->zve2PC() : 0; }
    bool isBUSRQ() { return a5_ ? a5_->isBUSRQ() : false; }
    bool isZVE2InReset() { return a5_ ? a5_->isZVE2InReset() : true; }
    bool isZVE2Waiting() { return a5_ ? a5_->isZVE2Waiting() : false; }
    bool busMasterIsZVE2() { return a5_ ? a5_->busMasterIsZVE2() : false; }
    uint16_t busMasterPC() { return a5_ ? a5_->busMasterPC() : cpuPC(); }

    void captureState(A5120Machine::MachineSnapshot& s) { if (a5_) a5_->captureState(s); }
    bool restoreState(const A5120Machine::MachineSnapshot& s) { return a5_ && a5_->restoreState(s); }
    bool saveState(const std::string& p) { return a5_ && a5_->saveState(p); }
    bool loadState(const std::string& p) { return a5_ && a5_->loadState(p); }

    // ─── Karten ──────────────────────────────────────────────────────────────
    K5122::DebugState k5122State() {
        return k8_ ? k8_->afs().debugState() : a5_->k5122State();
    }
    Z80PIO::DebugState k5122CtrlPioState() {
        return k8_ ? k8_->afs().ctrlPio().debugState() : a5_->k5122CtrlPioState();
    }
    Z80PIO::DebugState k5122DataPioState() {
        return k8_ ? k8_->afs().dataPio().debugState() : a5_->k5122DataPioState();
    }
    const K1520Bus::IntAck& lastIntAck() {
        return k8_ ? k8_->lastIntAck() : a5_->lastIntAck();
    }

    /** @brief Alle Interruptquellen in Kettenfolge (K8915: K5122 → ZRE-CTC → ATS). */
    std::vector<IntSource> interruptSources() {
        if (a5_) return a5_->interruptSources();
        std::vector<IntSource> out;
        int pos = 0;
        auto addPio = [&](const Z80PIO::DebugState& st, const char* name) {
            for (int p = 0; p < 2; ++p) {
                IntSource s;
                s.device = std::string(name) + " " + (p ? "B" : "A");
                s.vector = st.port[p].vector; s.ie = st.port[p].ie;
                s.pending = st.port[p].pending; s.ius = st.port[p].ius;
                s.iei = st.port[p].iei; s.chain = pos++;
                out.push_back(std::move(s));
            }
        };
        auto addCtc = [&](Z80CTC& ctc, const char* name) {
            auto st = ctc.debugState();
            for (int c = 0; c < 4; ++c) {
                IntSource s;
                s.device = std::string(name) + " ch" + char('0' + c);
                s.vector = (uint8_t)(st.vecBase | (c << 1));
                s.ie = st.ch[c].intEn; s.pending = st.ch[c].intPending;
                s.ius = st.ch[c].ius; s.iei = st.ch[c].iei; s.chain = pos++;
                out.push_back(std::move(s));
            }
        };
        auto addSio = [&](Z80SIO& sio, const char* name) {
            auto st = sio.debugState();
            for (int c = 0; c < 2; ++c) {
                IntSource s;
                s.device = std::string(name) + " " + (c ? "B" : "A");
                s.vector = st.ch[1].wr2; s.exact = false;
                s.ie = (st.ch[c].wr1 & 0x1F) != 0;
                s.pending = st.ch[c].irqRx || st.ch[c].irqTx || st.ch[c].irqExt;
                s.ius = st.ch[c].ius; s.iei = st.ch[c].iei; s.chain = pos++;
                out.push_back(std::move(s));
            }
        };
        addPio(k8_->afs().ctrlPio().debugState(), "K5122 ctrl-PIO");
        addPio(k8_->afs().dataPio().debugState(), "K5122 data-PIO");
        addCtc(k8_->zre().ctc(), "ZRE CTC");
        addSio(k8_->ats().sio1(), "ATS SIO1");
        addSio(k8_->ats().sio2(), "ATS SIO2 (B=Tastatur)");
        addCtc(k8_->ats().ctc1(), "ATS CTC1");
        addCtc(k8_->ats().ctc2(), "ATS CTC2");
        return out;
    }

private:
    std::unique_ptr<A5120Machine> a5_;
    std::unique_ptr<K8915Machine> k8_;
    Z80 dummy_zve2_;   ///< K8915: Platzhalter, nie ausgeführt (Kommandos sind abgefangen)
};

}  // namespace dbgm

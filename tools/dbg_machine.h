/**
 * @file dbg_machine.h
 * @brief Die Maschine hinter k1520dbg — A5120, K8915 (§8a AP-E4d), PRG 710/710-1 (AP-P5c) ODER PC 1715 (AP-4b).
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
 *  - **nur K8915**: über @ref k8915() (A8H-Speicherbild, Bänke, ATS, K7672);
 *  - **nur PRG 710/710-1**: über @ref prg() (Speicherverwaltung E8H–EBH, K2521, K8025,
 *    8279/K7609 bzw. K7672 an A32-B).  Der PRG zählt wie der K8915 als „eine CPU“
 *    (@ref einCpu) — ZVE2-/Snapshot-Kommandos sind dort ebenfalls „nicht vorhanden“.
 *
 * Header-only, weil nur k1520dbg sie benutzt; boot_trace fährt den K8915 in einem
 * eigenen Zweig (tools/boot_trace_k8915.cpp).
 *
 * @license MIT
 */
#pragma once
#include "core/machines/a5120/a5120.h"
#include "core/machines/k8915/k8915.h"
#include "core/machines/prg710/prg710.h"
#include "core/machines/pc1715/pc1715.h"
#include "core/machines/p8000/p8000.h"
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace dbgm {

/** @brief Eine Interruptquelle der Daisy-Chain (maschinenneutral, für `ivt`). */
using IntSource = A5120Machine::IntSource;

/** @brief Maschinenwahl aus `--machine`; false bei unbekanntem Namen. */
enum class Art { A5120, K8915, K8915G2, Prg710, Prg710_1, Pc1715, Pc1715W, P8000, P8000_16 };
inline bool parseMachine(const std::string& s, Art& art) {
    if (s == "a5120" || s == "A5120")       { art = Art::A5120;    return true; }
    if (s == "k8915" || s == "K8915")       { art = Art::K8915;    return true; }
    if (s == "k8915-g2" || s == "K8915-G2") { art = Art::K8915G2;  return true; }   // AP-V6b
    if (s == "prg710" || s == "PRG710")     { art = Art::Prg710;   return true; }
    if (s == "prg710-1" || s == "PRG710-1") { art = Art::Prg710_1; return true; }
    if (s == "pc1715" || s == "PC1715")     { art = Art::Pc1715;   return true; }
    if (s == "pc1715w" || s == "PC1715W")   { art = Art::Pc1715W;  return true; }   // AP-W3
    if (s == "p8000" || s == "P8000")       { art = Art::P8000;    return true; }   // AP P12a: nur 8-Bit-Seite
    if (s == "p8000-16" || s == "P8000-16") { art = Art::P8000_16; return true; }   // mit 16-Bit-Karte + Kopplung
    return false;
}

/** @brief RAF-Bauart aus `--raf`; false bei unbekanntem Namen (`none` = keine Karte, @p keine). */
inline bool parseRaf(const std::string& s, RAF::Typ& typ, bool& keine) {
    keine = false;
    if (s == "none")        { keine = true; return true; }
    if (s == "raf128")      { typ = RAF::Typ::RAF128; return true; }
    if (s == "raf512")      { typ = RAF::Typ::RAF512; return true; }
    if (s == "raf2m")       { typ = RAF::Typ::RAF2M;  return true; }
    return false;
}

/** @brief Steckt die RAF @p name (leer/`none` = nichts) in @p m; false + @p err bei Fehler. */
inline bool steckeRaf(K1520Machine& m, const std::string& name, std::string& err) {
    if (name.empty()) return true;
    RAF::Typ t = RAF::Typ::RAF512; bool keine = false;
    if (!parseRaf(name, t, keine)) { err = "unbekannte Karte '" + name + "' (none|raf128|raf512|raf2m)"; return false; }
    if (keine) return true;
    if (!m.installRaf(t)) { err = m.rafFehler(); return false; }
    return true;
}

/** @brief Steckt die K6022 (`--ptape`, Entwurf 23 AP-L1) in @p m, wenn @p an; false + @p err bei Fehler. */
inline bool steckeK6022(K1520Machine& m, bool an, std::string& err) {
    if (!an) return true;
    if (!m.installK6022()) { err = m.k6022Fehler(); return false; }
    return true;
}

inline const char* rafName(RAF::Typ t) {
    return t == RAF::Typ::RAF128 ? "RAF 128" : t == RAF::Typ::RAF2M ? "RAF-2M" : "RAF 512";
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

/**
 * @brief Gen 2 (AP-V6b): je 4-KB-Seite `Z` ZRE K2521 (ROM 0000-0BFFH + 1 KB RAM), `M` RAM der
 *        K3528, `.` Systembus (K7024-Bildspeicher u. a.).
 */
inline std::string speicherbild(const K3528& ops) {
    std::string s;
    for (int p = 0; p < 16; ++p) {
        switch (ops.ortVon((uint16_t)(p << 12))) {
            case K3528::Quelle::Zre: s += 'Z'; break;
            case K3528::Quelle::Ram: s += 'M'; break;
            case K3528::Quelle::Bus: s += '.'; break;
        }
    }
    return s;
}

// ─── K8915 beider Bauformen (V3: ZRE 045-8762, Gen 2: K2521 + K3528) ─────────
// `K8915Machine::zre()` gilt nur am V3 (assert) — jede Stelle der Werkzeuge geht über diese Weichen.
inline bool gen2(const K8915Machine& k) { return k.generation() == K8915Machine::Generation::Gen2; }
inline uint8_t k8A8(K8915Machine& k)    { return gen2(k) ? k.ops().reg()    : k.zre().reg(); }
inline bool k8Memdi(K8915Machine& k)    { return gen2(k) ? k.ops().memdi()  : k.zre().memdi(); }
inline bool k8Memdi1(K8915Machine& k)   { return gen2(k) ? k.ops().memdi1() : k.zre().memdi1(); }
inline Z80& k8Cpu(K8915Machine& k)      { return gen2(k) ? k.k2521().cpu()  : k.zre().cpu(); }
inline Z80CTC& k8ZreCtc(K8915Machine& k){ return gen2(k) ? k.k2521().ctc()  : k.zre().ctc(); }
/// Antwortet an @p addr das ROM der ZRE (V3: Boot-ROM, Gen 2: K2521-Seite)?
inline bool k8RomEin(K8915Machine& k, uint16_t addr) {
    return gen2(k) ? k.ops().ortVon(addr) == K3528::Quelle::Zre
                   : k.zre().ortVon(addr).quelle == K8915Zre::Quelle::Rom;
}
inline std::string speicherbild(K8915Machine& k) {
    return gen2(k) ? speicherbild(static_cast<const K3528&>(k.ops())) : speicherbild(static_cast<const K8915Zre&>(k.zre()));
}

/**
 * @brief Speicherbild des PRG je 4-KB-Seite: `Z` ZRE, `V` Seite F mit VRAM,
 *        `0`–`F` physische OPS-Seite, `-` leer (wie boot_trace).
 */
inline std::string speicherbild(Prg710Machine& m) {
    std::string s;
    for (int n = 0; n < 16; ++n) {
        const uint16_t a = static_cast<uint16_t>((n << 12) | (n == 15 ? 0x800 : 0));
        const auto o = m.speicher().ortVon(a);
        switch (o.quelle) {
            case Prg710Speicher::Quelle::Zre:  s += 'Z'; break;
            case Prg710Speicher::Quelle::Vram: s += 'V'; break;
            case Prg710Speicher::Quelle::Ops:  s += "0123456789ABCDEF"[o.offset >> 12]; break;
            case Prg710Speicher::Quelle::Leer: s += '-'; break;
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
    /// @p cfg gilt nur für den A5120 (dort u. a. das Erweiterungsmodul des A5120.16).
    explicit DbgMachine(Art art, const A5120Machine::Config& cfg = {}) {
        if (art == Art::K8915) k8_ = std::make_unique<K8915Machine>();
        else if (art == Art::K8915G2) {
            K8915Machine::Config c;
            c.generation = K8915Machine::Generation::Gen2;
            k8_ = std::make_unique<K8915Machine>(c);
        }
        else if (art == Art::P8000 || art == Art::P8000_16) {   // AP P12a
            P8000Machine::Config c;
            c.karte16 = (art == Art::P8000_16);
            p8_ = std::make_unique<P8000Machine>(c);
        }
        else if (art == Art::Pc1715) pc_ = std::make_unique<Pc1715Machine>();
        else if (art == Art::Pc1715W) {   // dieselbe Klasse, Variante 1715W (AP-W3)
            Pc1715Machine::Config c;
            c.variante = Pc1715Machine::Config::Variante::Pc1715W;
            pc_ = std::make_unique<Pc1715Machine>(c);
        }
        else if (art == Art::Prg710 || art == Art::Prg710_1) {
            Prg710Machine::Config pc;
            pc.variante = art == Art::Prg710_1 ? Prg710Machine::Config::Variante::Prg710_1
                                               : Prg710Machine::Config::Variante::Prg710;
            p7_ = std::make_unique<Prg710Machine>(pc);
        }
        else a5_ = std::make_unique<A5120Machine>(cfg);
    }

    bool isK8915() const { return k8_ != nullptr; }
    bool isPrg() const { return p7_ != nullptr; }
    bool isPc1715() const { return pc_ != nullptr; }
    bool isP8000() const { return p8_ != nullptr; }
    /// P8000 mit 16-Bit-Karte (U8001, MMU, Kopplung).
    bool hasP8000Karte16() const { return p8_ && p8_->karte16() != nullptr; }
    bool isPrg1() const { return p7_ && p7_->variante() == Prg710Machine::Config::Variante::Prg710_1; }
    /// Eine CPU, keine ZVE2/BUSRQ/Snapshots (K8915 und PRG).
    bool einCpu() const { return a5_ == nullptr; }
    const char* name() const {
        return p8_ ? "P8000" : k8_ ? (gen2(*k8_) ? "K8915 Gen 2" : "K8915") : pc_ ? "PC 1715" : p7_ ? (isPrg1() ? "PRG 710-1" : "PRG 710") : "A5120"; }
    /// Anzeigename der (Haupt-)CPU in Meldungen: am A5120 „ZVE1“ (es gibt zwei).
    const char* cpuName() const { return a5_ ? "ZVE1" : p8_ ? "U880" : "CPU"; }
    A5120Machine* a5120() { return a5_.get(); }
    K8915Machine* k8915() { return k8_.get(); }
    Prg710Machine* prg() { return p7_.get(); }
    Pc1715Machine* pc1715() { return pc_.get(); }
    P8000Machine* p8000() { return p8_.get(); }
    /// Erweiterungsmodul (A5120.16); nullptr ohne `--em` und immer am K8915.
    EM* em() { return a5_ ? a5_->em() : nullptr; }
    K1520Machine& base() {
        if (k8_) return *k8_;
        if (p7_) return *p7_;
        if (pc_) return *pc_;
        if (p8_) return *p8_;
        return *a5_;
    }

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn() { base().powerOn(); }
    void reset()   { base().reset(); }
    int  run(int n) { return base().run(n); }
    void stop()    { base().stop(); }
    void clearStop() { if (k8_) k8_->clearStop(); else if (p7_) p7_->clearStop(); else if (pc_) pc_->clearStop(); else if (p8_) p8_->clearStop(); else a5_->clearStop(); }

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
        if (p7_) return (uint8_t)(p7_->screenChar(col, row) & 0x7F);   // VRAM nur bei E8H[F]=FFH in CPU-Sicht
        if (pc_) return (uint8_t)(pc_->screenChar(col, row) & 0x7F);   // Bild kommt per DMA, nie über mem_read
        if (p8_) return (uint8_t)(p8_->screenChar(col, row) & 0x7F);   // Kern-Terminal tty1 (80 × 24)
        return a5_->memReadDebug((uint16_t)(0xF800 + row * 80 + col));
    }

    // ─── CPU ─────────────────────────────────────────────────────────────────
    Z80& cpuDebug() {
        if (k8_) return k8Cpu(*k8_);
        if (p7_) return p7_->zre().cpu();
        if (pc_) return pc_->zre().cpu();
        if (p8_) return p8_->karte8().cpu();
        return a5_->cpuDebug();
    }
    uint16_t cpuPC() { return cpuDebug().PC; }
    uint16_t cpuSP() { return cpuDebug().SP; }
    uint64_t cpuCycles() { return cpuDebug().cycles; }
    /// Maschinenuhr: am A5120 beide CPUs, am K8915 = CPU-Takte samt /WAIT-Takten.
    uint64_t machineCycles() {
        if (k8_) return k8_->totalCycles();
        if (p7_) return p7_->totalCycles();
        if (pc_) return pc_->totalCycles();
        if (p8_) return p8_->totalCycles();
        return a5_->machineCycles();
    }
    bool isRomEnabled() {
        if (k8_) return k8RomEin(*k8_, 0x0000);
        if (p7_) return p7_->speicher().ortVon(0x0000).quelle == Prg710Speicher::Quelle::Zre;
        if (pc_) return pc_->zre().romEin();
        if (p8_) return (p8_->karte8().speicher().selekt(0x0000) & 1) != 0;   // EPROM bei 0000H selektiert
        return a5_->isRomEnabled();
    }

    void setCpuTraceCallback(std::function<void(const Z80&)> cb) {
        if (k8_) k8_->setCpuTraceCallback(std::move(cb));
        else if (p7_) p7_->setCpuTraceCallback(std::move(cb));
        else if (pc_) pc_->setCpuTraceCallback(std::move(cb));
        else if (p8_) p8_->setCpuTraceCallback(std::move(cb));
        else     a5_->setCpuTraceCallback(std::move(cb));
    }
    void setZVE2TraceCallback(std::function<void(const Z80&)> cb) {
        if (a5_) a5_->setZVE2TraceCallback(std::move(cb));   // K8915: keine ZVE2
    }
    void setBusTrace(K1520Bus::BusTrace cb) {
        if (k8_) k8_->setBusTrace(std::move(cb));
        else if (p7_) p7_->setBusTrace(std::move(cb));
        else if (pc_) pc_->setBusTrace(std::move(cb));
        else if (p8_) p8_->setBusTrace(std::move(cb));
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

    void captureState(A5120Machine::MachineSnapshot& s, bool raf_inhalt = true) {
        if (a5_) a5_->captureState(s, raf_inhalt);
    }
    bool restoreState(const A5120Machine::MachineSnapshot& s) { return a5_ && a5_->restoreState(s); }
    // Save-State: A5120 (v7) und P8000 (P8KS v2); die übrigen Maschinen haben keinen.
    bool saveState(const std::string& p) { return p8_ ? p8_->saveState(p) : (a5_ && a5_->saveState(p)); }
    bool loadState(const std::string& p) { return p8_ ? p8_->loadState(p) : (a5_ && a5_->loadState(p)); }
    std::string stateError() const { return p8_ ? p8_->stateError() : a5_ ? a5_->stateError() : std::string(); }

    // ─── Karten ──────────────────────────────────────────────────────────────
    K5122::DebugState k5122State() {
        if (k8_) return k8_->afs().debugState();
        if (p7_) return p7_->afs().debugState();
        if (pc_) return pc_->afs().debugState();
        if (p8_) return K5122::DebugState{};   // P8000: U8272, kein K5122
        return a5_->k5122State();
    }
    Z80PIO::DebugState k5122CtrlPioState() {
        if (k8_) return k8_->afs().ctrlPio().debugState();
        if (p7_) return p7_->afs().ctrlPio().debugState();
        if (pc_) return pc_->afs().ctrlPio().debugState();
        if (p8_) return Z80PIO::DebugState{};
        return a5_->k5122CtrlPioState();
    }
    Z80PIO::DebugState k5122DataPioState() {
        if (k8_) return k8_->afs().dataPio().debugState();
        if (p7_) return p7_->afs().dataPio().debugState();
        if (pc_) return pc_->afs().dataPio().debugState();
        if (p8_) return Z80PIO::DebugState{};
        return a5_->k5122DataPioState();
    }
    const K1520Bus::IntAck& lastIntAck() {
        if (k8_) return k8_->lastIntAck();
        if (p7_) return p7_->lastIntAck();
        if (pc_) return pc_->bus().lastIntAck();
        if (p8_) return p8_->bus().lastIntAck();
        return a5_->lastIntAck();
    }

    /** @brief Alle Interruptquellen in Kettenfolge (K8915: K5122 → ZRE-CTC → ATS). */
    std::vector<IntSource> interruptSources() {
        if (a5_) return a5_->interruptSources();
        std::vector<IntSource> out;
        int pos = 0;
        if (p8_) {   // Kette DMA – PIO2 – CTC0 – SIO0 – SIO1 – PIO0 – PIO1 – CTC1 (Schaltplan 8-Bit §3)
            P8000Karte8& k = p8_->karte8();
            auto addPio = [&](Z80PIO& pio, const char* name) {
                const auto st = pio.debugState();
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
            {   // UA858: vorderstes Glied
                const auto d = p8_->floppy8().dma().sicht();
                IntSource s;
                s.device = "UA858 DMA";
                s.vector = d.vektorRoh; s.ie = d.intFreigabe; s.pending = d.intAnstehend; s.ius = d.ius;
                s.iei = d.iei; s.chain = pos++;
                out.push_back(std::move(s));
            }
            addPio(k.pio2(), "PIO2 (Floppy-Port)");
            addCtc(k.ctc0(), "CTC0");
            addSio(k.sio0(), "SIO0 (A tty0, B tty1)");
            addSio(k.sio1(), "SIO1 (A tty2, B tty3)");
            addPio(k.pio0(), "PIO0 (Kopplung)");
            addPio(k.pio1(), "PIO1 (EPROMmer)");
            addCtc(k.ctc1(), "CTC1");
            return out;
        }
        if (pc_) {   // Kette Steuer-PIO → Daten-PIO → CTC0 → SIO0 (Plan §3.5, Merkposten)
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
            auto st = pc_->zre().ctc().debugState();
            addPio(pc_->afs().ctrlPio().debugState(), "FD Steuer-PIO");
            addPio(pc_->afs().dataPio().debugState(), "FD Daten-PIO");
            for (int c = 0; c < 4; ++c) {
                IntSource s;
                s.device = std::string("ZRE CTC0 ch") + char('0' + c);
                s.vector = (uint8_t)(st.vecBase | (c << 1));
                s.ie = st.ch[c].intEn; s.pending = st.ch[c].intPending;
                s.ius = st.ch[c].ius; s.iei = st.ch[c].iei; s.chain = pos++;
                out.push_back(std::move(s));
            }
            auto ss = pc_->zre().sio().debugState();
            for (int c = 0; c < 2; ++c) {
                IntSource s;
                s.device = std::string("ZRE SIO0 ") + (c ? "B" : "A (Tastatur)");
                s.vector = ss.ch[1].wr2; s.exact = false;
                s.ie = (ss.ch[c].wr1 & 0x1F) != 0;
                s.pending = ss.ch[c].irqRx || ss.ch[c].irqTx || ss.ch[c].irqExt;
                s.ius = ss.ch[c].ius; s.iei = ss.ch[c].iei; s.chain = pos++;
                out.push_back(std::move(s));
            }
            return out;
        }
        if (p7_) {   // Kette K5122 → ZRE (CTC, PIO) → K8025 (SIO A33, SIO A32)
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
            addPio(p7_->afs().ctrlPio().debugState(), "K5122 ctrl-PIO");
            addPio(p7_->afs().dataPio().debugState(), "K5122 data-PIO");
            addCtc(p7_->zre().ctc(), "ZRE CTC");
            addPio(p7_->zre().pio().debugState(), "ZRE PIO");
            addSio(p7_->ass().sioA33(), "K8025 SIO A33");
            addSio(p7_->ass().sioA32(), isPrg1() ? "K8025 SIO A32 (B=Tastatur K7672)" : "K8025 SIO A32");
            return out;
        }
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
        if (gen2(*k8_)) {   // K2521: IEI → CTC → PIO → IEO
            addCtc(k8_->k2521().ctc(), "ZRE K2521 CTC");
            addPio(k8_->k2521().pio().debugState(), "ZRE K2521 PIO");
        } else addCtc(k8_->zre().ctc(), "ZRE CTC");
        addSio(k8_->ats().sio1(), "ATS SIO1");
        addSio(k8_->ats().sio2(), "ATS SIO2 (B=Tastatur)");
        addCtc(k8_->ats().ctc1(), "ATS CTC1");
        addCtc(k8_->ats().ctc2(), "ATS CTC2");
        return out;
    }

private:
    std::unique_ptr<A5120Machine> a5_;
    std::unique_ptr<K8915Machine> k8_;
    std::unique_ptr<Prg710Machine> p7_;
    std::unique_ptr<Pc1715Machine> pc_;
    std::unique_ptr<P8000Machine> p8_;
    Z80 dummy_zve2_;   ///< K8915: Platzhalter, nie ausgeführt (Kommandos sind abgefangen)
};

}  // namespace dbgm

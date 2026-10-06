/**
 * @file z8000.h
 * @brief U8001/U8002 (≙ Zilog Z8001/Z8002) — generische CPU-Primitive.
 *
 * Wie `Z80`: kennt keine Karte, keinen Bus, keine Maschine.  Jeder Zugriff nach
 * aussen ist EIN Buszyklus mit vollem Status (ST0..3, N/S, B/W, R/W, SN0..6,
 * AD0..15) über die Rückrufe `read`/`write` — so kann eine Karte (S4: die
 * Segmentweiche der EM-Steuerkarte) aus Status und Adresse entscheiden, wohin
 * der Zugriff geht.
 *
 * Dekodierung und Takte kommen aus DER Befehlstabelle
 * (core/primitives/z8000/z8k_table.h, S2); hier steht nur die Ausführung.
 * Befunde, Annahmen und offene Punkte: core/primitives/z8000/README.md.
 *
 * Datenbus wie am Baustein (Zilog §9.4): `read` liefert die 16 Leitungen AD0..15.
 *  - Speicher, Byte:  gerade Adresse → AD8..15, ungerade → AD0..7; die CPU nimmt
 *    die Hälfte selbst.  Beim Schreiben liegt das Byte auf BEIDEN Hälften.
 *  - Speicher, Wort:  Adresse immer gerade (A0 wird von der CPU gelöscht).
 *  - E/A, Byte: Lage nach A0 wie beim Speicher (ungerade AD0..7, gerade AD8..15;
 *    zulässig sind Standard ungerade, Spezial gerade — §9.4.3).  Beim Schreiben
 *    doppelt (Config::ioByteOnBothHalves; am A5120.16 belegt, siehe README).
 *    Was auf einer Hälfte liegt, die beim Lesen niemand treibt, entscheidet die
 *    Karte (`read`): am A5120.16 die Portadresse aus der Adressphase (EM::read16).
 *
 * Takte: step() führt einen Befehl (bzw. einen Durchlauf eines Wiederholungs-
 * befehls, einen Interrupt-/Trapeintritt, einen Stop-/Halt-/Bus-Takt) aus und
 * gibt die Takte zurück — Tabellenwert + Zusatz je Einheit + WAIT-Takte, die
 * die Karte über addWaitCycles() meldet.
 *
 * @license MIT
 */
#pragma once
#include "core/primitives/z8000/z8k_codec.h"

#include <cstdint>
#include <functional>

/// Statusleitungen ST3..ST0 (Zilog Tabelle 9.1) — der Wert IST der Leitungscode.
enum class Z8kStatus : uint8_t {
    Internal      = 0x0,   ///< interne Operation (kein Datentransfer)
    Refresh       = 0x1,   ///< Speicherauffrischung (AD1..8 = Zeile)
    Io            = 0x2,   ///< Standard-E/A
    SpecialIo     = 0x3,   ///< Spezial-E/A (MMU …)
    SegTrapAck    = 0x4,   ///< Segment-Trap-Quittung (Z8001, setSEGT; am A5120.16 nie: SEGT fest H)
    NmiAck        = 0x5,   ///< NMI-Quittung
    NviAck        = 0x6,   ///< NVI-Quittung
    ViAck         = 0x7,   ///< VI-Quittung
    MemData       = 0x8,   ///< Datenspeicher
    MemStack      = 0x9,   ///< Stapelspeicher
    MemDataEpu    = 0xA,   ///< Datenspeicher (EPU)
    MemStackEpu   = 0xB,   ///< Stapelspeicher (EPU)
    MemInstr      = 0xC,   ///< Programmspeicher (weitere Befehlsworte, LDR/LDAR-Daten, PSA)
    MemInstrFirst = 0xD,   ///< Befehlsholen, erstes Wort
    EpuTransfer   = 0xE,   ///< CPU↔EPU
    Reserved      = 0xF,
};

/// Kurzname für Protokolle/Debugger ("IF1", "DATA", "STACK", "IO", …).
inline const char* z8kStatusName(Z8kStatus st) {
    static const char* const k[16] = {"INT", "REFR", "IO", "SIO", "SEGTA", "NMIA", "NVIA", "VIA",
                                      "DATA", "STACK", "EDATA", "ESTACK", "INSTR", "IF1", "EPU", "RSV"};
    return k[uint8_t(st) & 15];
}

/// Art einer Ausnahme (Zilog Kap. 7).  Reihenfolge = Rangfolge §7.7 (Reset zuerst; die
/// drei internen Traps schliessen einander aus).
enum class Z8kException : uint8_t {
    Reset, ExtendedInstruction, Privileged, SystemCall, Nmi, SegmentTrap, Vi, Nvi,
};
inline const char* z8kExceptionName(Z8kException e) {
    static const char* const k[8] = {"RESET", "EPA", "PRIV", "SC", "NMI", "SEGT", "VI", "NVI"};
    return k[uint8_t(e) & 7];
}

/// Eine angenommene Ausnahme, wie sie der Debugger protokolliert (Z8000::onException).
struct Z8kExceptionInfo {
    Z8kException kind = Z8kException::Reset;
    uint16_t id = 0;            ///< gekellerte Kennung (intern: erstes Befehlswort; extern: Quittung)
    uint8_t  atSeg = 0;         ///< Befehl, nach bzw. in dem sie angenommen wurde (lastPc)
    uint16_t atPc = 0;
    uint8_t  savedSeg = 0;      ///< gekellerter PC (Tabelle §7.6.2)
    uint16_t savedPc = 0;
    uint16_t oldFcw = 0;        ///< gekellerte FCW
    uint16_t newFcw = 0;        ///< aus der PSA geladen
    uint8_t  newSeg = 0;
    uint16_t newPc = 0;
    bool     leftRepeat = false;   ///< ein unterbrechbarer Befehl wurde verlassen (PC = der Befehl)
    uint64_t cycle = 0;         ///< `cycles` beim Eintritt
};

/// Ein Buszyklus, so wie er an den Pins erscheint.
struct Z8kBusCycle {
    Z8kStatus st = Z8kStatus::Internal;
    bool     system = true;    ///< N/S: true = Systemmodus (Pin N/S = L)
    bool     word = true;      ///< B/W: true = Wort (Pin B/W = L)
    bool     read = true;      ///< R/W: true = Lesen
    uint8_t  seg = 0;          ///< SN0..SN6 (Z8002: 0)
    uint16_t addr = 0;         ///< AD0..15 bei AS: Offset bzw. Portadresse

    bool isMemory() const { return uint8_t(st) >= 0x8 && st != Z8kStatus::EpuTransfer; }
    bool isInstructionFetch() const { return st == Z8kStatus::MemInstr || st == Z8kStatus::MemInstrFirst; }
};

enum class Z8kModel : uint8_t { Z8001, Z8002 };

struct Z8kConfig {
    Z8kModel model = Z8kModel::Z8001;
    /// Byte-OUT (Standard- wie Spezial-E/A): Byte auf beiden Bushälften.
    /// Das Handbuch sagt es nur für Speicher-Schreibzyklen (§9.4.2).  **Am A5120.16
    /// gemessen (2026-09-29): `OUTB %0081,#01` schreibt A35 (AD8..15) = 01H.**
    bool ioByteOnBothHalves = true;
    /// Refresh-Zyklen (Status 0001) als Buszyklus melden, wenn RE = 1.
    bool emitRefreshCycles = true;
    /// Vor der Interruptquittung ein Befehlsholen, das verworfen wird (Zilog §2.13).
    bool spuriousFetchBeforeAck = true;
};

/// Ablaufzustand neben den Registern — für Save-State/Snapshot (S4/S5).
struct Z8kRunState {
    bool resetLine = false, resetPending = false;
    bool nmiLine = false, nmiPending = false, vi = false, nvi = false;
    bool stopLine = false, busReq = false, mi = false, mo = false, busAck = false;
    bool halted = false, stopped = false, haveW0 = false, inRepeat = false;
    uint16_t w0 = 0;                 ///< schon geholtes erstes Wort (Stop-Zustand)
    uint16_t repWords[6] = {};       ///< laufender Wiederholungsbefehl: seine Worte
    uint8_t  repNwords = 0;
    bool     repSeg = false;
    uint16_t repNext = 0, lastPc = 0;
    uint8_t  lastPcSeg = 0;
    int      refreshAcc = 0;
};

class Z8000 {
public:
    using Model = Z8kModel;
    using Config = Z8kConfig;

    // ── FCW ─────────────────────────────────────────────────────────────────
    static constexpr uint16_t FCW_SEG  = 0x8000;
    static constexpr uint16_t FCW_SN   = 0x4000;   ///< 1 = System
    static constexpr uint16_t FCW_EPA  = 0x2000;
    static constexpr uint16_t FCW_VIE  = 0x1000;
    static constexpr uint16_t FCW_NVIE = 0x0800;
    static constexpr uint16_t F_C  = 0x0080;
    static constexpr uint16_t F_Z  = 0x0040;
    static constexpr uint16_t F_S  = 0x0020;
    static constexpr uint16_t F_PV = 0x0010;
    static constexpr uint16_t F_D  = 0x0008;
    static constexpr uint16_t F_H  = 0x0004;

    /// Byte-Offsets der Program Status Area (je Z8001 ×2).
    enum PsaEntry : uint16_t {
        PSA_EPA = 0x04, PSA_PRIV = 0x08, PSA_SC = 0x0C, PSA_SEGT = 0x10,
        PSA_NMI = 0x14, PSA_NVI = 0x18, PSA_VI = 0x1C,
    };

    explicit Z8000(const Config& cfg = Config());

    // ── Bus (Pflicht: read/write) ───────────────────────────────────────────
    std::function<uint16_t(const Z8kBusCycle&)>       read;
    std::function<void(const Z8kBusCycle&, uint16_t)> write;
    /// Pegeländerung an MO (true = aktiv = L).  MSET/MREQ/MRES, Reset.
    std::function<void(bool active)> onMO;
    /// BUSAK geht an/aus.
    std::function<void(bool active)> onBusAck;
    /// Eine Kodierung ohne Tabellenzeile wurde ausgeführt (als NOP, README §8).
    std::function<void(uint8_t seg, uint16_t pc, uint16_t w0)> onIllegal;
    /// Eine Ausnahme (Reset, Trap, Interrupt) ist angenommen und der neue Programmstatus
    /// geladen — für Debugger/Trace; ohne Rückruf kostet es nichts.
    std::function<void(const Z8kExceptionInfo&)> onException;

    /// Aus einem Busrückruf heraus: der laufende Zyklus bekommt n WAIT-Takte.
    void addWaitCycles(int n) { waits_ += n; }

    // ── Eingänge (Pins) ─────────────────────────────────────────────────────
    /// /RESET: solange aktiv, steht die CPU (step() = 1 Takt); beim Loslassen
    /// folgt die Resetsequenz (FCW aus 0002, PC aus 0004[/0006], Segment 0).
    void setResetLine(bool active);
    /// Resetimpuls: wie setResetLine(true); setResetLine(false).
    void reset();
    void setNMI(bool active);            ///< flankengetriggert (H→L)
    void setVI(bool active) { vi_ = active; }    ///< pegelgetriggert
    void setNVI(bool active) { nvi_ = active; }  ///< pegelgetriggert
    /// /SEGT (nur Z8001): pegelgetriggert, nicht maskierbar, wie VI/NVI am Befehlsende
    /// abgetastet (§9.6.1).  Die MMU hält die Anforderung bis zur Quittung (Status 0100).
    /// Am Z8002 ohne Wirkung (kein Pin).  Nicht in Z8kRunState (dessen Aufbau trägt den
    /// A5120-Save-State v7) — die Karte stellt den Pegel beim Laden selbst wieder her.
    void setSEGT(bool active) { segt_ = active; }
    void setStop(bool active) { stopLine_ = active; }
    void setBusReq(bool active) { busReq_ = active; }
    void setMI(bool active) { mi_ = active; }    ///< µI, aktiv = L

    // ── Ausgänge / Zustand ──────────────────────────────────────────────────
    bool moActive() const { return mo_; }        ///< µ0, aktiv = L (nach Reset inaktiv)
    bool busAck() const { return busAck_; }
    bool halted() const { return halted_; }
    bool stopped() const { return stopped_; }    ///< im Stop/Refresh-Zustand
    bool inReset() const { return resetLine_ || resetPending_; }
    bool inRepeat() const { return inRepeat_; }  ///< Wiederholungsbefehl läuft
    // Eingangspegel und Merker (Debugger)
    bool nmiLine() const { return nmiLine_; }
    bool nmiPending() const { return nmiPending_; }  ///< NMI-Flanke gemerkt, noch nicht angenommen
    bool segtLine() const { return segt_; }
    bool viLine() const { return vi_; }
    bool nviLine() const { return nvi_; }
    bool miLine() const { return mi_; }
    bool stopLine() const { return stopLine_; }
    bool busReqLine() const { return busReq_; }
    /// Der zuletzt ausgegebene Buszyklus (Status, N/S, B/W, R/W, SN, AD) und sein Datum.
    const Z8kBusCycle& lastCycle() const { return lastCycle_; }
    uint16_t lastCycleData() const { return lastCycleData_; }
    /// Ausgegebene Buszyklen je Statuscode seit dem Einschalten bzw. clearStatusCounts().
    uint64_t statusCount(Z8kStatus st) const { return statusCount_[uint8_t(st) & 15]; }
    void clearStatusCounts() { for (auto& n : statusCount_) n = 0; }
    /// Die zuletzt angenommene Ausnahme (kind = Reset und cycle = 0, solange keine).
    const Z8kExceptionInfo& lastException() const { return lastExc_; }
    uint64_t illegalCount() const { return illegal_; }
    Model model() const { return cfg_.model; }
    bool isZ8001() const { return cfg_.model == Model::Z8001; }
    bool segMode() const { return isZ8001() && (fcw & FCW_SEG); }
    bool systemMode() const { return (fcw & FCW_SN) != 0; }

    /// Einen Schritt ausführen; Rückgabe = Takte (auch in `cycles` aufaddiert).
    int step();

    // ── Register ────────────────────────────────────────────────────────────
    /// R0..R13 (ungebankt); R14/R15 je [0] = Normal, [1] = System.  Z8002: R14
    /// ist nicht gebankt (nur [0]).  Zugriff wie ein Befehl ihn sieht: r()/setR().
    uint16_t Rg[14] = {};
    uint16_t R14[2] = {};
    uint16_t R15[2] = {};
    uint16_t fcw = 0;
    uint16_t pc = 0;          ///< PC-Offset (Adresse des nächsten Befehls)
    uint8_t  pcSeg = 0;       ///< PC-Segment (Z8002: 0)
    uint16_t psapSeg = 0;     ///< wie LDCTL PSAPSEG: Segment in Bit 14..8
    uint16_t psapOff = 0;     ///< Bit 7..0 immer 0
    uint16_t refresh = 0;     ///< Bit 15 RE, 14..9 RATE, 8..1 ROW
    uint64_t cycles = 0;

    /// Register so, wie ein Befehl im aktuellen Modus sie sieht (Tabelle 4.1).
    uint16_t r(unsigned n) const;
    void     setR(unsigned n, uint16_t v);
    uint8_t  rb(unsigned n) const;               ///< 0..7 = RH0..7, 8..15 = RL0..7
    void     setRB(unsigned n, uint8_t v);
    uint32_t rr(unsigned n) const;               ///< RRn (Bit 0 von n wird ignoriert)
    void     setRR(unsigned n, uint32_t v);
    uint64_t rq(unsigned n) const;               ///< RQn (Bit 0/1 von n ignoriert)
    void     setRQ(unsigned n, uint64_t v);
    /// Segmentierte Adresse wie im Registerpaar: 0sss ssss 0000 0000 | offset.
    uint32_t pcLong() const { return (uint32_t(pcSeg & 0x7F) << 24) | pc; }

    /// Ablaufzustand lesen/setzen (Register sind öffentliche Member).
    Z8kRunState runState() const;
    void setRunState(const Z8kRunState& s);

    /// Beginn des laufenden/letzten Befehls (für Debugger/Trace).
    uint16_t lastPc() const { return lastPc_; }
    uint8_t  lastPcSeg() const { return lastPcSeg_; }

private:
    enum class Op : uint8_t;
    struct Ea { uint8_t seg; uint16_t off; Z8kStatus st; };

    Config cfg_;
    // Pins
    bool resetLine_ = false, resetPending_ = true;
    bool nmiLine_ = false, nmiPending_ = false;
    bool vi_ = false, nvi_ = false, segt_ = false;
    bool stopLine_ = false, busReq_ = false, mi_ = false;
    bool mo_ = false, busAck_ = false;
    // Ablauf
    bool halted_ = false, stopped_ = false, haveW0_ = false, inRepeat_ = false;
    uint16_t w0_ = 0;
    z8k::Decoded rep_;          ///< laufender Wiederholungsbefehl
    uint16_t repNext_ = 0;      ///< PC hinter dem Wiederholungsbefehl
    int waits_ = 0;
    int refreshAcc_ = 0;
    uint64_t illegal_ = 0;
    uint16_t lastPc_ = 0;
    uint8_t  lastPcSeg_ = 0;
    // Beobachtung (Debugger)
    Z8kBusCycle lastCycle_;
    uint16_t lastCycleData_ = 0;
    uint64_t statusCount_[16] = {};
    Z8kExceptionInfo lastExc_;

    uint16_t& rw(unsigned n);
    const uint16_t& rwc(unsigned n) const;
    bool z8001() const { return isZ8001(); }

    // Bus
    uint16_t bus(Z8kStatus st, uint8_t seg, uint16_t addr, bool word, bool rd, uint16_t data = 0);
    uint8_t  memB(const Ea& a);
    uint16_t memW(const Ea& a);
    uint32_t memL(const Ea& a);
    void     memWB(const Ea& a, uint8_t v);
    void     memWW(const Ea& a, uint16_t v);
    void     memWL(const Ea& a, uint32_t v);
    uint32_t memRead(const Ea& a, int w);
    void     memWrite(const Ea& a, int w, uint32_t v);
    uint16_t ioRead(bool special, uint16_t port, bool word);
    void     ioWrite(bool special, uint16_t port, bool word, uint16_t v);

    // Adressen
    uint8_t  dataSeg() const { return z8001() ? pcSeg : 0; }  // nichtsegmentiert
    Ea       ptr(unsigned reg, int32_t disp = 0) const;
    void     ptrAdd(unsigned reg, int delta);
    void     stackAdd(unsigned reg, int delta);
    Ea       ea(const z8k::Operand& o, uint16_t pcNext) const;
    unsigned spReg() const { return segMode() ? 14 : 15; }
    void     pushW(uint16_t v);
    void     pushL(uint32_t v);
    uint16_t popW();
    uint32_t popL();
    void     pushPc(uint16_t off);
    void     popPc();

    // Operanden
    uint32_t rdOp(const z8k::Operand& o, int w, uint16_t pcNext);
    void     wrOp(const z8k::Operand& o, int w, uint32_t v, uint16_t pcNext);
    uint64_t regRead(const z8k::Operand& o) const;

    // Flags
    void setFlag(uint16_t f, bool on) { fcw = on ? uint16_t(fcw | f) : uint16_t(fcw & ~f); }
    bool flag(uint16_t f) const { return (fcw & f) != 0; }
    static bool cond(unsigned cc, uint16_t flags);
    void flagsZS(uint32_t v, int w);
    void flagsLogic(uint32_t v, int w);
    uint32_t add(uint32_t a, uint32_t b, bool c, int w, bool bcd);
    uint32_t sub(uint32_t a, uint32_t b, bool c, int w, bool bcd);

    // Ablauf
    void setFcw(uint16_t v);
    void doReset();
    int  refreshCycle();
    int  finish(int c);
    int  takeException(Z8kException kind, Z8kStatus ack, uint16_t entry, uint16_t id, uint16_t savedPc,
                       bool external, bool leftRepeat = false);
    int  checkInterrupts();
    int  execute(const z8k::Decoded& d, uint16_t pcNext);
    int  blockStep(const z8k::Decoded& d, uint16_t pcNext, bool first);
    int  shiftOp(const z8k::Operand& dst, int w, int count, bool arith);
    static Op opOf(const z8k::Insn& in);
};

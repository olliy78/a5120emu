/**
 * @file callstack_tracker.h
 * @brief Exakter Z80-Aufrufstapel aus dem Instruktionsstrom (History-Backtrace).
 *
 * Statt den Stack heuristisch nach `CALL`-Vorbytes zu durchsuchen, führt dieser
 * Tracker einen *mitlaufenden* Aufrufstapel: zu jeder ausgeführten Instruktion wird
 * die VORIGE klassifiziert (CALL/RST → push, RET/RETI/RETN → pop) und der „taken"-Fall
 * exakt am tatsächlich erreichten PC erkannt. Ergebnis ist ein präziser Backtrace für
 * gewöhnliche CALL/RET-Verschachtelung.
 *
 * **Grenze:** Interrupt-Eintritte (IM2-Vektor/NMI) pushen keinen Frame (es gibt keinen
 * CALL-Opcode); der zugehörige `RETI` poppt nur, wenn sein Rücksprung zufällig einen
 * getrackten Frame trifft — er korrumpiert den Stack also nicht, ISR-Frames fehlen aber.
 *
 * Header-only & Template über den Byte-Leser → ohne Maschine unit-testbar.
 *
 * @license MIT
 */
#pragma once
#include <cstdint>
#include <vector>

namespace cstrack {

/// Ein Aufruf-Frame: Rücksprungadresse, Aufrufstelle (CALL/RST-PC), Sprungziel.
struct Frame {
    uint16_t ret;     ///< Rücksprungadresse (Adresse nach dem CALL/RST)
    uint16_t site;    ///< PC des CALL/RST-Befehls
    uint16_t target;  ///< angesprungene Unterprogramm-Adresse
};

class CallStackTracker {
public:
    /**
     * Eine ausgeführte Instruktion melden (PC = Startadresse, in Ausführungsreihenfolge).
     * @param pc  Start-PC der gerade beginnenden Instruktion.
     * @param rd  Byte-Leser `uint8_t(uint16_t addr)` für den Opcode der Vorinstruktion.
     */
    template <class ReadByte>
    void onInstruction(uint16_t pc, ReadByte rd) {
        if (have_prev_) {
            uint8_t op0 = rd(prev_pc_);
            bool isCall = (op0 == 0xCD) || ((op0 & 0xC7) == 0xC4);   // CALL nn / CALL cc nn
            bool isRst  = ((op0 & 0xC7) == 0xC7);                    // RST p
            bool isRet  = (op0 == 0xC9) || ((op0 & 0xC7) == 0xC0);   // RET / RET cc
            bool isEdRet = false;
            if (op0 == 0xED) {                                       // RETI (ED 4D) / RETN (ED 45)
                uint8_t op1 = rd((uint16_t)(prev_pc_ + 1));
                isEdRet = (op1 == 0x4D || op1 == 0x45);
            }
            if (isCall || isRst) {
                uint16_t target = isRst
                    ? (uint16_t)(op0 & 0x38)
                    : (uint16_t)(rd((uint16_t)(prev_pc_ + 1)) |
                                 (rd((uint16_t)(prev_pc_ + 2)) << 8));
                if (pc == target) {                                 // Sprung wurde genommen
                    uint16_t ret = (uint16_t)(prev_pc_ + (isRst ? 1 : 3));
                    if (stack_.size() < cap_) stack_.push_back({ret, prev_pc_, target});
                }
            } else if (isRet || isEdRet) {
                // Nur poppen, wenn der Rücksprung einen getrackten Frame trifft →
                // niemals Korruption durch Interrupt-RETIs.
                if (!stack_.empty() && pc == stack_.back().ret) stack_.pop_back();
            }
        }
        prev_pc_ = pc;
        have_prev_ = true;
    }

    /// Frames von außen (älteste) nach innen (jüngste/aktuelle).
    const std::vector<Frame>& frames() const { return stack_; }
    std::size_t depth() const { return stack_.size(); }

    /// Tracker zurücksetzen (z.B. nach restore/reverse — Stack wird neu aufgebaut).
    void clear() { stack_.clear(); have_prev_ = false; prev_pc_ = 0; }

private:
    std::vector<Frame> stack_;
    uint16_t    prev_pc_   = 0;
    bool        have_prev_ = false;
    std::size_t cap_       = 8192;   ///< Schutz gegen unbegrenztes Wachstum bei Drift
};

} // namespace cstrack

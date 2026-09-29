/**
 * @file dbg_u8000.h
 * @brief Maschinenfreie Bausteine des U8001-Kontexts in k1520dbg (S5, Plan 17 §3).
 *
 * - Adressen der 16-Bit-Seite: `<<seg>>off` (Zilog-Schreibweise des Disassemblers),
 *   `em:ZELLE` (roh ins DRAM, an der Segmentweiche vorbei) oder eine schlichte Zahl
 *   (Segment = Vorgabe, meist das PC-Segment).  Zahlen: `%1234`/`0x1234`/`1234h` hex,
 *   sonst dezimal — wie im übrigen Debugger.
 * - FCW als Text.
 * - Aufrufstapel des U8001 aus der Befehlsfolge (CALL/CALR und SP-Anstieg), das
 *   Gegenstück zu tools/callstack_tracker.h für den Z80.
 *
 * Ohne Emulator unit-getestet (tests/debugtools/test_dbg_u8000.cpp).
 *
 * @license MIT
 */
#pragma once
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace dbg16 {

/// Eine Adresse der 16-Bit-Seite.
struct Addr16 {
    enum Kind : uint8_t { Seg, Raw } kind = Seg;
    uint8_t  seg = 0;      ///< Kind::Seg: Segmentnummer (0..127)
    uint16_t off = 0;      ///< Kind::Seg: Offset
    uint32_t cell = 0;     ///< Kind::Raw: DRAM-Zelle
    bool     explicitSeg = false;   ///< `<<n>>` stand ausdrücklich da
    uint32_t key() const { return (uint32_t(seg) << 16) | off; }
};

/// Zahl im Debuggerstil: %hex, 0xhex, …h hex, sonst dezimal.  false bei Unsinn.
inline bool parseNumber(const std::string& t, long& v) {
    if (t.empty()) return false;
    char* e = nullptr;
    if (t[0] == '%') { v = std::strtol(t.c_str() + 1, &e, 16); return e && *e == 0 && t.size() > 1; }
    if (t.size() > 1 && (t.back() == 'h' || t.back() == 'H')) {
        std::string b = t.substr(0, t.size() - 1);
        v = std::strtol(b.c_str(), &e, 16);
        return e && *e == 0;
    }
    v = std::strtol(t.c_str(), &e, 0);
    return e && *e == 0;
}

/**
 * @brief `<<seg>>off` | `em:ZELLE` | Zahl | Symbol (über @p sym) → Addr16.
 * @param defSeg Segment für eine Zahl ohne `<<…>>` (PC-Segment).
 */
inline bool parseAddr(const std::string& tok, uint8_t defSeg, Addr16& a,
                      const std::function<bool(const std::string&, long&)>& sym = nullptr) {
    a = Addr16{};
    if (tok.size() > 3 && (tok.compare(0, 3, "em:") == 0 || tok.compare(0, 3, "EM:") == 0)) {
        long v;
        // Rohzellen sind immer hex (so schreibt sie auch `dev em`).
        std::string s = tok.substr(3);
        if (!s.empty() && s[0] == '%') s = s.substr(1);
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
        char* e = nullptr;
        v = std::strtol(s.c_str(), &e, 16);
        if (s.empty() || !e || *e) return false;
        a.kind = Addr16::Raw;
        a.cell = uint32_t(v);
        return true;
    }
    std::string rest = tok;
    a.seg = defSeg;
    if (tok.size() > 4 && tok.compare(0, 2, "<<") == 0) {
        size_t e = tok.find(">>");
        if (e == std::string::npos) return false;
        long s;
        std::string st = tok.substr(2, e - 2);
        // Segment: dezimal wie beim Disassembler; %/0x/h als hex erlaubt.
        if (!parseNumber(st, s) || s < 0 || s > 127) return false;
        a.seg = uint8_t(s);
        a.explicitSeg = true;
        rest = tok.substr(e + 2);
    }
    long v;
    if (parseNumber(rest, v)) { a.off = uint16_t(v); return true; }
    if (sym) {                              // Symbol (NAME[+OFF] löst der Rufer auf)
        long sv;
        if (sym(rest, sv)) {
            if (!a.explicitSeg && sv > 0xFFFF) a.seg = uint8_t((sv >> 16) & 0x7F);
            a.off = uint16_t(sv);
            return true;
        }
    }
    return false;
}

/// `<<3>>%1234` (segmentiert) bzw. `%1234`.
inline std::string addrText(uint8_t seg, uint16_t off, bool segmented) {
    char b[24];
    if (segmented) std::snprintf(b, sizeof b, "<<%u>>%%%04X", unsigned(seg), off);
    else           std::snprintf(b, sizeof b, "%%%04X", off);
    return b;
}

/// FCW als Text: "SEG S --- VIE NVIE  C Z - V - -".
inline std::string fcwText(uint16_t fcw) {
    std::string s;
    s += (fcw & 0x8000) ? "SEG " : "NONSEG ";
    s += (fcw & 0x4000) ? "SYS " : "NORM ";
    if (fcw & 0x2000) s += "EPA ";
    s += (fcw & 0x1000) ? "VIE " : "vi- ";
    s += (fcw & 0x0800) ? "NVIE " : "nvi- ";
    const char* n = "CZSVDH";
    const uint16_t bit[6] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04};
    s += "[";
    for (int i = 0; i < 6; ++i) s += (fcw & bit[i]) ? n[i] : '-';
    s += "]";
    return s;
}

/// Ein Rahmen des U8001-Aufrufstapels.
struct Frame16 {
    uint32_t site;      ///< seg<<16|off des CALL
    uint32_t target;    ///< Ziel
    uint32_t ret;       ///< Rückkehradresse
    uint16_t sp;        ///< SP-Offset NACH dem Kellern der Rückkehradresse
};

/**
 * @brief Aufrufstapel aus der Befehlsfolge.
 *
 * Vor jedem Befehl: @ref onInstruction mit dem jetzigen SP-Offset.  Ein CALL/CALR
 * wird vorgemerkt und beim nächsten Befehl als Rahmen übernommen (SP ist dann um
 * 2/4 gefallen); jeder Rahmen, über dessen SP der Stapel wieder gestiegen ist,
 * ist zurückgekehrt (RET, IRET, Stapelabbau von Hand).
 */
class CallStack16 {
public:
    void onInstruction(uint32_t pcKey, uint16_t sp) {
        while (!frames_.empty() && sp > frames_.back().sp) frames_.pop_back();
        if (pending_) {
            pending_ = false;
            if (sp < pendSp_) {                       // CALL hat wirklich gekellert
                frames_.push_back(Frame16{pendSite_, pcKey, pendRet_, sp});
                if (frames_.size() > 256) frames_.erase(frames_.begin());
            }
        }
    }
    /// Der gerade anstehende Befehl ist ein Aufruf (vor seiner Ausführung melden).
    void noteCall(uint32_t site, uint32_t ret, uint16_t sp) {
        pending_ = true; pendSite_ = site; pendRet_ = ret; pendSp_ = sp;
    }
    const std::vector<Frame16>& frames() const { return frames_; }
    void clear() { frames_.clear(); pending_ = false; }

private:
    std::vector<Frame16> frames_;
    bool     pending_ = false;
    uint32_t pendSite_ = 0, pendRet_ = 0;
    uint16_t pendSp_ = 0;
};

}  // namespace dbg16

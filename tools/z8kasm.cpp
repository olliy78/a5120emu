/**
 * @file z8kasm.cpp
 * @brief z8kasm — Assembler/Disassembler für U8001/U8002 auf der Kommandozeile.
 *
 *   z8kasm [-s] [-l] [-o abbild.bin] quelle.s        assemblieren
 *   z8kasm -d abbild.bin [-s] [--org ADR] [--len N]  disassemblieren
 *
 * Alles Inhaltliche steckt in tools/z8000/ (header-only); hier nur Ein-/Ausgabe.
 * Exit: 0 = gut, 1 = Fehler im Quelltext/Abbild, 2 = Aufruf falsch.
 *
 * @license MIT
 */
#include "tools/z8000/z8k_asm.h"
#include "tools/z8000/z8k_disasm.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void usage() {
    std::fprintf(stderr,
        "Aufruf: z8kasm [optionen] quelle.s\n"
        "        z8kasm -d abbild.bin [optionen]\n"
        "  -o DATEI      Binaerabbild schreiben (Vorgabe: quelle mit .bin)\n"
        "  -l            Listing auf die Standardausgabe\n"
        "  -s, --seg     segmentiert beginnen (U8001); Vorgabe nichtsegmentiert\n"
        "  --lax         wie z8001asm.py: @Rn/@RRn in beiden Modi, Registernummer wie\n"
        "                geschrieben (auch ungerade)\n"
        "  -d, --disasm  DATEI disassemblieren statt assemblieren\n"
        "  --org ADR     Adresse des ersten Bytes beim Disassemblieren (z. B. %%0040,\n"
        "                <<3>>%%1000); Vorgabe 0\n"
        "  --start ADR   erst ab dieser Adresse disassemblieren (davor: .WORD)\n"
        "  --len N       hoechstens N Bytes disassemblieren\n");
}

bool readFile(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

/// Adresse wie im Assembler auswerten (%1234, 0x1234, <<3>>%1234 …).
bool parseAddr(const std::string& s, int64_t& v) {
    bool undef = false; std::string err;
    z8k::asmdetail::SymFn none;
    z8k::asmdetail::Expr e(s, none, 0);
    return e.eval(v, undef, err) && !undef;
}

int disassemble(const std::string& path, bool seg, int64_t org, int64_t start, int64_t len) {
    std::string data;
    if (!readFile(path, data)) { std::fprintf(stderr, "z8kasm: %s nicht lesbar\n", path.c_str()); return 1; }
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(data.data());
    size_t n = data.size();
    if (len >= 0 && size_t(len) < n) n = size_t(len);
    uint8_t sg = uint8_t((org >> 24) & 0x7F);
    uint16_t base = uint16_t(org & 0xFFFF);
    uint16_t from = start >= 0 ? uint16_t(start & 0xFFFF) : base;
    size_t pos = 0;
    int unknown = 0;
    while (pos + 1 < n) {
        uint16_t pc = uint16_t(base + pos);
        z8k::Line l;
        bool data = uint16_t(pc - base) < uint16_t(from - base);
        if (data) {
            l.bytes = 2;
            char b[16]; std::snprintf(b, sizeof b, ".WORD %%%04X", unsigned((buf[pos] << 8) | buf[pos + 1]));
            l.text = b;
        } else {
            l = z8k::disasmBytes(buf, n, base, pc, seg, sg);
            if (!l.dec.insn) ++unknown;
        }
        char addr[24];
        if (seg) std::snprintf(addr, sizeof addr, "<<%u>>%04X", unsigned(sg), unsigned(pc));
        else     std::snprintf(addr, sizeof addr, "%04X", unsigned(pc));
        std::string hex;
        for (int i = 0; i < l.bytes && pos + size_t(i) < n; i += 2) {
            char h[8]; std::snprintf(h, sizeof h, "%02X%02X ", buf[pos + size_t(i)],
                                     pos + size_t(i) + 1 < n ? buf[pos + size_t(i) + 1] : 0);
            hex += h;
        }
        std::printf("%-11s %-25s %s\n", addr, hex.c_str(), l.text.c_str());
        pos += size_t(l.bytes);
    }
    return unknown ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string in, out, dis;
    bool seg = false, listing = false, lax = false;
    int64_t org = 0, start = -1, len = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string& v) {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            v = argv[++i];
        };
        if (a == "-o") next(out);
        else if (a == "-l") listing = true;
        else if (a == "-s" || a == "--seg") seg = true;
        else if (a == "--lax") lax = true;
        else if (a == "-d" || a == "--disasm") next(dis);
        else if (a == "--org" || a == "--start" || a == "--len") {
            std::string v; next(v); int64_t x;
            if (!parseAddr(v, x)) { std::fprintf(stderr, "z8kasm: %s: Wert ungueltig: %s\n", a.c_str(), v.c_str()); return 2; }
            (a == "--org" ? org : a == "--start" ? start : len) = x;
        }
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-') { usage(); return 2; }
        else if (in.empty()) in = a;
        else { usage(); return 2; }
    }
    if (!dis.empty()) return disassemble(dis, seg, org, start, len);
    if (in.empty()) { usage(); return 2; }

    std::string src;
    if (!readFile(in, src)) { std::fprintf(stderr, "z8kasm: %s nicht lesbar\n", in.c_str()); return 1; }
    z8k::AsmOptions opt; opt.seg = seg; opt.laxPointers = lax;
    z8k::AsmResult r = z8k::assemble(src, opt);
    for (auto& e : r.errors) std::fprintf(stderr, "%s:%d: Fehler: %s\n", in.c_str(), e.line, e.text.c_str());
    if (listing) std::fputs(z8k::formatListing(r).c_str(), stdout);
    if (!r.ok()) return 1;
    if (out.empty()) {
        out = in;
        size_t dot = out.find_last_of('.');
        size_t sl = out.find_last_of("/\\");
        if (dot != std::string::npos && (sl == std::string::npos || dot > sl)) out.resize(dot);
        out += ".bin";
    }
    uint32_t base = 0;
    std::vector<uint8_t> img = r.flat(&base);
    if (img.size() > (1u << 20)) {
        std::fprintf(stderr, "z8kasm: Abbild > 1 MB (mehrere Segmente?) — nicht geschrieben\n");
        return 1;
    }
    std::ofstream f(out, std::ios::binary);
    if (!f) { std::fprintf(stderr, "z8kasm: %s nicht schreibbar\n", out.c_str()); return 1; }
    f.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
    std::fprintf(stderr, "z8kasm: %s — %zu Bytes ab %s%04X\n", out.c_str(), img.size(),
                 (base >> 16) ? ("<<" + std::to_string(base >> 16) + ">>%").c_str() : "%",
                 unsigned(base & 0xFFFF));
    return 0;
}

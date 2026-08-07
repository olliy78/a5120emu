#include "tests/support/screen.h"

namespace k1520test {

std::string vramText(A5120Machine& m) {
    std::string s;
    s.reserve(kVramEnd - kVramBase + 1);
    for (int a = kVramBase; a <= kVramEnd; ++a) {
        uint8_t c = m.memReadDebug(static_cast<uint16_t>(a));
        s.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
    }
    return s;
}

std::string vramLines(A5120Machine& m) {
    const std::string flat = vramText(m);
    std::string out;
    out.reserve(flat.size() + kVramRows);
    for (int row = 0; row < kVramRows; ++row) {
        out.append(flat, static_cast<size_t>(row) * kVramCols, kVramCols);
        out.push_back('\n');
    }
    return out;
}

}  // namespace k1520test

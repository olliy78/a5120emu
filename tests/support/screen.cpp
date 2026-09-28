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

namespace {
std::string zeilen(const std::string& flat) {
    std::string out;
    out.reserve(flat.size() + kVramRows);
    for (int row = 0; row < kVramRows; ++row) {
        out.append(flat, static_cast<size_t>(row) * kVramCols, kVramCols);
        out.push_back('\n');
    }
    return out;
}
}  // namespace

std::string vramLines(A5120Machine& m) { return zeilen(vramText(m)); }

std::string vramText(K8915Machine& m) {
    std::string s;
    s.reserve(kVramCols * kVramRows);
    for (int row = 0; row < kVramRows; ++row)
        for (int col = 0; col < kVramCols; ++col) {
            const uint8_t c = m.screen().vramRead(col, row) & 0x7F;
            s.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
        }
    return s;
}

std::string vramLines(K8915Machine& m) { return zeilen(vramText(m)); }

}  // namespace k1520test

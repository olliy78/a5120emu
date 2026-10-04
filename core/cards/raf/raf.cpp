#include "core/cards/raf/raf.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {
constexpr uint8_t kRafStateVersion = 1;
}

RAF::RAF() : RAF(Config()) {}

RAF::RAF(const Config& cfg) : cfg_(cfg) {
    // Masken je Typ (§3.2/§5.1): Sperre = A15 ODER das typische Schutzbit,
    // Sektormaske = Sektorbits unterhalb der Sperrbits.
    switch (cfg_.typ) {
        case Typ::RAF128: sperre_ = 0x8400; sektor_ = 0x03FF; break;
        case Typ::RAF512: sperre_ = 0x9000; sektor_ = 0x0FFF; break;
        case Typ::RAF2M:  sperre_ = 0xC000; sektor_ = 0x3FFF; break;
    }
    mem_.assign(size_t(sektor_ + 1) * SEKTORLAENGE, 0x00);
}

void RAF::attachToBus(K1520Bus& bus) {
    bus_ = &bus;
    bus.registerIO(this, cfg_.basis, 2);
}

void RAF::powerOn() {
    std::fill(mem_.begin(), mem_.end(), uint8_t(0x00));
    latch_ = 0xFFFF;
}

void RAF::reset() { latch_ = 0xFFFF; }

int64_t RAF::zelle() const {
    if (!bus_) return -1;
    if (latch_ & sperre_) return -1;                  // Zugriffssperre
    // Bits oberhalb der Sektormaske, die nicht Sperrbit sind (RAF512: A13/A14),
    // wertet die Karte nicht aus -> Spiegelung (bewusst so nachgebildet, §3.2).
    const uint32_t sek  = latch_ & sektor_;
    const uint32_t byte = (bus_->ioAddress() >> 8) & 0x7F;   // A8–A14, kein Autoinkrement
    return int64_t((sek << 7) | byte);
}

uint8_t RAF::ioRead(uint8_t port) {
    if (uint8_t(port - cfg_.basis) != 0) return 0xFF;         // Steuerport: nicht lesbar
    const int64_t z = zelle();
    return z < 0 ? 0xFF : mem_[size_t(z)];                    // gesperrt: offener Bus
}

void RAF::ioWrite(uint8_t port, uint8_t d) {
    if (uint8_t(port - cfg_.basis) == 1) {
        const uint8_t hi = bus_ ? uint8_t(bus_->ioAddress() >> 8) : 0xFF;
        latch_ = uint16_t(hi << 8 | d);
        return;
    }
    const int64_t z = zelle();
    if (z >= 0) mem_[size_t(z)] = d;                          // gesperrt: verpufft
}

uint8_t RAF::peek(uint32_t adr) const { return adr < mem_.size() ? mem_[adr] : 0xFF; }
void RAF::poke(uint32_t adr, uint8_t v) { if (adr < mem_.size()) mem_[adr] = v; }

void RAF::saveState(std::vector<uint8_t>& o) const {
    o.push_back(kRafStateVersion);
    o.push_back(uint8_t(cfg_.typ));
    o.push_back(uint8_t(latch_ & 0xFF));
    o.push_back(uint8_t(latch_ >> 8));
    const uint32_t n = uint32_t(mem_.size());
    const auto* b = reinterpret_cast<const uint8_t*>(&n);
    o.insert(o.end(), b, b + sizeof n);
    o.insert(o.end(), mem_.begin(), mem_.end());
}

bool RAF::loadState(const uint8_t*& p, const uint8_t* end) {
    const uint8_t* q = p;
    if (end - q < 8) return false;
    if (q[0] != kRafStateVersion || q[1] != uint8_t(cfg_.typ)) return false;
    const uint16_t latch = uint16_t(q[2] | q[3] << 8);
    uint32_t n;
    std::memcpy(&n, q + 4, sizeof n);
    q += 8;
    if (n != mem_.size() || size_t(end - q) < n) return false;
    std::memcpy(mem_.data(), q, n);
    latch_ = latch;
    p = q + n;
    return true;
}

bool RAF::ladeInhalt(const std::string& pfad) {
    std::ifstream f(pfad, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() != mem_.size()) return false;                // Größe muss exakt passen
    mem_ = std::move(d);
    return true;
}

bool RAF::speichereInhalt(const std::string& pfad) const {
    std::ofstream f(pfad, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(mem_.data()), std::streamsize(mem_.size()));
    return bool(f);
}

/**
 * @file k7028.cpp
 * @brief ATS K7028.30 des K8915 — Dekodierung, Interruptkette, Rückschleife.
 * @see k7028.h, doc/design/16_k8915.md §3.2, §8a AP-E2
 */

#include "core/cards/k7028/k7028.h"
#include "core/logger.h"
#include <algorithm>

K7028::K7028() : K7028(Config{}) {}

K7028::K7028(const Config& cfg) : cfg_(cfg)
{
    updateInternalChain();
}

void K7028::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.io_base, 32);
    bus.registerIO(this, cfg_.latch_base, 8);
}

// ─── E/A ─────────────────────────────────────────────────────────────────────
// Relativ zu io_base: AB3/AB4 wählen den Baustein, AB2 fällt weg (Spiegel),
// AB1/AB0 gehen an den Baustein.

uint8_t K7028::ioRead(uint8_t port)
{
    const uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base);
    if (rel >= 32) return 0xFF;   // Latch: nur beschreibbar, offener Bus
    const uint8_t sub = port & 0x03;
    uint8_t r = 0xFF;
    switch ((rel >> 3) & 3) {
        case 0: r = sio1_.ioRead(sub); break;
        case 1: r = ctc1_.ioRead(sub); break;
        case 2: r = sio2_.ioRead(sub); break;
        case 3: r = ctc2_.ioRead(sub); break;
    }
    updateInternalChain();
    return r;
}

void K7028::ioWrite(uint8_t port, uint8_t data)
{
    const uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base);
    if (rel >= 32) {
        latch_ = data;
        LOG_DEBUG("K7028", "Anzeigelatch %02XH := %02X", port, data);
        return;
    }
    const uint8_t sub = port & 0x03;
    switch ((rel >> 3) & 3) {
        case 0: sio1_.ioWrite(sub, data); break;
        case 1: ctc1_.ioWrite(sub, data); break;
        case 2: sio2_.ioWrite(sub, data); break;
        case 3: ctc2_.ioWrite(sub, data); break;
    }
    updateInternalChain();
}

// ─── Interruptkette ──────────────────────────────────────────────────────────
// Reihenfolge auf der Karte aus dem Stromlaufplan nicht abgelesen [?] (§6.4).
// Im Selbsttest und im BIOS fordern nie zwei Bausteine der ATS zugleich an
// (BIOS: nur SIO 2), die Reihenfolge ist dort ohne Wirkung.

void K7028::updateInternalChain()
{
    sio1_.setIEI(iei_in_);
    sio2_.setIEI(sio1_.getIEO());
    ctc1_.setIEI(sio2_.getIEO());
    ctc2_.setIEI(ctc1_.getIEO());
}

void K7028::setIEI(bool iei)
{
    iei_in_ = iei;
    updateInternalChain();
}

bool K7028::getIEO() const { return ctc2_.getIEO(); }

bool K7028::hasInterrupt() const
{
    return sio1_.hasInterrupt() || sio2_.hasInterrupt()
        || ctc1_.hasInterrupt() || ctc2_.hasInterrupt();
}

uint8_t K7028::getVector() const
{
    // Z80SIO/Z80CTC::getVector() quittieren (IUS) — mutable im Baustein.
    auto& self = const_cast<K7028&>(*this);
    uint8_t v = 0xFF;
    if      (sio1_.hasInterrupt()) v = self.sio1_.getVector();
    else if (sio2_.hasInterrupt()) v = self.sio2_.getVector();
    else if (ctc1_.hasInterrupt()) v = self.ctc1_.getVector();
    else if (ctc2_.hasInterrupt()) v = self.ctc2_.getVector();
    self.updateInternalChain();
    return v;
}

void K7028::onRETI()
{
    // Das RETI erkennt nur der Baustein mit gesetztem IUS und freiem IEI.
    sio1_.onRETI();
    sio2_.onRETI();
    ctc1_.onRETI();
    ctc2_.onRETI();
    updateInternalChain();
}

const char* K7028::intDeviceName() const
{
    if (sio1_.hasInterrupt()) return "ATS SIO1";
    if (sio2_.hasInterrupt()) return "ATS SIO2";
    if (ctc1_.hasInterrupt()) return "ATS CTC1";
    if (ctc2_.hasInterrupt()) return "ATS CTC2";
    return "ATS K7028";
}

// ─── Lebenslauf / Takt ───────────────────────────────────────────────────────

void K7028::reset()
{
    sio1_.reset();
    sio2_.reset();
    ctc1_.reset();
    ctc2_.reset();
    latch_ = 0xFF;
    for (auto& q : schleife_) q.clear();
    frei_ab_.fill(0);
    updateInternalChain();
}

bool K7028::clockTick(int ticks)
{
    const bool a = ctc1_.clockTick(ticks);
    const bool b = ctc2_.clockTick(ticks);
    return a || b;
}

Z80SIO::Channel& K7028::kanal(Kanal k)
{
    switch (k) {
        case Sio1A: return sio1_.channelA();
        case Sio1B: return sio1_.channelB();
        default:    return sio2_.channelA();
    }
}

void K7028::empfange(Kanal k, uint8_t byte)
{
    kanal(k).rxByte(byte);
    updateInternalChain();
}

bool K7028::service(uint64_t now)
{
    bool geaendert = false;
    for (int i = 0; i < KanalAnzahl; ++i) {
        const Kanal k = static_cast<Kanal>(i);
        Z80SIO::Channel& ch = kanal(k);
        if (ch.txAvailable()) {
            const uint8_t b = ch.txGet();
            geaendert = true;
            if (cfg_.rueckschleife[i]) {
                // Zeichen für Zeichen über die Leitung: das nächste beginnt frühestens,
                // wenn das vorige angekommen ist.
                const uint64_t start = std::max(now, frei_ab_[i]);
                frei_ab_[i] = start + ZEICHEN_TAKTE;
                schleife_[i].push_back({frei_ab_[i], b});
            } else if (abnehmer_[i]) {
                abnehmer_[i](b);
            }
            // sonst: offene Leitung, das Byte geht verloren
        }
        auto& q = schleife_[i];
        while (!q.empty() && q.front().faellig <= now) {
            ch.rxByte(q.front().byte);
            q.pop_front();
            geaendert = true;
        }
    }
    if (geaendert) updateInternalChain();
    return geaendert;
}

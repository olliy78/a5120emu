/**
 * @file pc1715_zre.cpp
 * @brief ZRE des PC 1715 — Speicher mit ROM-Overlay, E/A-Dekodierung, 8275-DMA und Rastern.
 * @see pc1715_zre.h, doc/design/21_pc1715.md §3, §8.2
 */

#include "core/cards/pc1715_zre/pc1715_zre.h"
#include "core/cards/pc1715_zre/rom_s502.h"
#include "core/cards/pc1715_zre/chargen_s619.h"
#include "core/cards/pc1715_zre/chargen_s602.h"
#include "core/logger.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr uint8_t PORT_CTC   = 0x08;   // 08H–0BH
constexpr uint8_t PORT_SIO   = 0x0C;   // 0CH–0FH
constexpr uint8_t PORT_CRT   = 0x18;   // 18H–1BH
constexpr uint8_t PORT_ROMEIN = 0x24;  // 24H–27H
constexpr uint8_t PORT_ROMAUS = 0x28;  // 28H–2BH
constexpr uint8_t PORT_LT107 = 0x2C;   // 2CH–2FH (lesen 2DH/2FH = 107, schreiben 2CH/2EH = 111)
constexpr uint8_t PORT_LT111 = 0x30;   // 30H–33H
constexpr uint8_t PORT_BWS   = 0x34;   // 34H–37H

constexpr uint8_t PIXEL_NORMAL = 0xB0;   // [?] zwei Helligkeitsstufen (HLGT), Zahlenwerte frei gewählt
constexpr uint8_t PIXEL_HELL   = 0xFF;
}  // namespace

Pc1715Zre::Pc1715Zre(K1520Bus& bus) : Pc1715Zre(bus, Config{}) {}

Pc1715Zre::Pc1715Zre(K1520Bus& bus, const Config& cfg)
    : bus_(bus), cfg_(cfg), rom_(PC1715_S502_URLADER)
{
    fb_w_ = textCols() * 8;
    fb_h_ = textRows() * zeichenLinien();
    fb_.assign(static_cast<size_t>(fb_w_) * fb_h_, 0);

    // Speicherweg der CPU: die Karte selbst (64 KB + Overlay).  Eine Zusatzkarte im
    // Steckplatz mit eigenem Speicher (/MEMDI) ist nicht vorgesehen.
    cpu_.readByte  = [this](uint16_t a) { return memRead(a); };
    cpu_.writeByte = [this](uint16_t a, uint8_t d) { memWrite(a, d); };
    cpu_.readPort     = [this](uint16_t p)            { return bus_.ioRead(p); };
    cpu_.writePort    = [this](uint16_t p, uint8_t d) { bus_.ioWrite(p, d); };
    cpu_.retiCallback = [this]()                      { bus_.signalRETI(); };

    // DMA-Quelle des 8275: Basis aus BWS-Register | Adresszähler; Zähler bei VRTC auf 0,
    // 10 Bit (K7221) bzw. 11 Bit (K7222), §1.2.7.3.
    crt_.dmaRead = [this]() -> uint8_t {
        const uint16_t mask = cfg_.bild == Bildschirm::K7222 ? 0x07FF : 0x03FF;
        const uint16_t a = static_cast<uint16_t>(bildBasis() | (dma_zaehler_ & mask));
        ++dma_zaehler_;
        return ram_[a];
    };
    crt_.vrtc = [this]() { dma_zaehler_ = 0; };
}

uint16_t Pc1715Zre::bildBasis() const
{
    // Registerwert = Adresse >> 10 (DB0 = A10 … DB5 = A15); beim 80×24 liefert der Zähler A10.
    const uint8_t maske = cfg_.bild == Bildschirm::K7222 ? 0x3E : 0x3F;
    return static_cast<uint16_t>((bws_ & maske) << 10);
}

void Pc1715Zre::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, PORT_CTC, 4);
    bus.registerIO(this, PORT_SIO, 4);
    bus.registerIO(this, PORT_CRT, 4);
    bus.registerIO(this, PORT_ROMEIN, 8);   // 24H–2BH
    bus.registerIO(this, PORT_LT107, 8);    // 2CH–33H
    bus.registerIO(this, PORT_BWS, 4);
}

void Pc1715Zre::powerOn(uint8_t fill)
{
    ram_.fill(fill);
    reset();
}

void Pc1715Zre::reset()
{
    ctc_.reset();
    sio_.reset();
    crt_.reset();
    cpu_.reset();
    rom_ein_ = true;
    bws_ = 0;
    dma_zaehler_ = 0;
    lt111_[0] = lt111_[1] = false;
    std::fill(fb_.begin(), fb_.end(), 0);
    fb_dirty_ = true;
}

// ─── E/A ─────────────────────────────────────────────────────────────────────

namespace {
// SIO0: 0CH/0DH = Daten A/B, 0EH/0FH = Steuer A/B (AB0 = Kanal, AB1 = Steuer);
// Baustein: 0 = A-Daten, 1 = A-Steuer, 2 = B-Daten, 3 = B-Steuer.
inline uint8_t sioPort(uint8_t port) { return uint8_t(((port & 1) << 1) | ((port >> 1) & 1)); }
}  // namespace

uint8_t Pc1715Zre::ioRead(uint8_t port)
{
    if (uint8_t(port - PORT_CTC) < 4) return ctc_.ioRead(port & 0x03);
    if (uint8_t(port - PORT_SIO) < 4) return sio_.ioRead(sioPort(port));
    if (uint8_t(port - PORT_CRT) < 4) return crt_.read((port & 1) != 0);
    if (port == 0x2D || port == 0x2F) return 0xFF;   // 107 AUS (keine DÜE, §1.2.8: AUS = 1) [?: DB1/3–7]
    return 0xFF;                                      // unbelegt (auch BWS lesend [?])
}

void Pc1715Zre::ioWrite(uint8_t port, uint8_t data)
{
    if (uint8_t(port - PORT_CTC) < 4) { ctc_.ioWrite(port & 0x03, data); return; }
    if (uint8_t(port - PORT_SIO) < 4) { sio_.ioWrite(sioPort(port), data); return; }
    if (uint8_t(port - PORT_CRT) < 4) { crt_.write((port & 1) != 0, data); return; }
    if (uint8_t(port - PORT_ROMEIN) < 4) { rom_ein_ = true;  return; }
    if (uint8_t(port - PORT_ROMAUS) < 4) {
        if (rom_ein_) LOG_DEBUG("PC1715", "ROM-Overlay aus (OUT %02XH)", port);
        rom_ein_ = false;
        return;
    }
    if (port == 0x2C || port == 0x2E) {              // Leitung 111 setzen: DB0 = Kanal A, DB2 = Kanal B
        lt111_[0] = (data & 0x01) != 0;
        lt111_[1] = (data & 0x04) != 0;
        return;
    }
    if (uint8_t(port - PORT_LT111) < 4) {            // 30H–33H: DB1 = 111 EIN
        lt111_[0] = lt111_[1] = (data & 0x02) != 0;  // [?: Kanalzuordnung dieser Tore]
        return;
    }
    if (uint8_t(port - PORT_BWS) < 4) {
        bws_ = data;
        LOG_DEBUG("PC1715", "BWS-Register = %02X: Bildspeicher %04XH, ZG%d", data, bildBasis(),
                  bwsZg2() ? 2 : 1);
        return;
    }
}

// ─── Interruptkette: IEI → CTC → SIO → IEO ───────────────────────────────────

void Pc1715Zre::setIEI(bool iei)
{
    ctc_.setIEI(iei);
    sio_.setIEI(ctc_.getIEO());
}

uint8_t Pc1715Zre::getVector() const
{
    if (ctc_.hasInterrupt()) return ctc_.getVector();
    if (sio_.hasInterrupt()) return sio_.getVector();
    return 0xFF;
}

const char* Pc1715Zre::intDeviceName() const
{
    if (ctc_.hasInterrupt()) return "PC1715 CTC0";
    if (sio_.hasInterrupt()) return "PC1715 SIO0";
    return "PC1715 ZRE";
}

void Pc1715Zre::onRETI()
{
    ctc_.onRETI();
    sio_.onRETI();
}

// ─── Bild ────────────────────────────────────────────────────────────────────

void Pc1715Zre::frame()
{
    crt_.frame();
    rastern();
    fb_dirty_ = true;
}

uint8_t Pc1715Zre::screenChar(int col, int row) const
{
    const I8275::Cell& c = crt_.cells(row, col);
    return c.empty ? 0x20 : c.code;
}

const uint8_t* Pc1715Zre::zgRom(bool zweiter_satz) const
{
    // zweiter_satz = false: der bei DB6 = 0 gewählte Baustein
    const bool s619 = (cfg_.zg_bei_db6_low == Zeichensatz::S619) != zweiter_satz;
    return s619 ? PC1715_S619_ZG1 : PC1715_S602_ZG2;
}

void Pc1715Zre::rastern()
{
    std::fill(fb_.begin(), fb_.end(), 0);
    const int rows = std::min(crt_.rows(), textRows());
    const int cols = std::min(crt_.cols(), textCols());
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) zeichneZelle(r, c, crt_.cells(r, c));
}

void Pc1715Zre::zeichneZelle(int row, int col, const I8275::Cell& z)
{
    if (z.empty && !z.cursor) return;
    const int hoehe = zeichenLinien();
    const bool unsichtbar = z.empty || (z.blink && !crt_.charBlinkOn());   // /VSP: Dunkelsteuerung
    const uint8_t* zg = zgRom(bwsZg2() != z.gpa0);                         // DB6 XOR GPA0 [?]
    const uint8_t hell = z.hlgt ? PIXEL_HELL : PIXEL_NORMAL;
    const int ul = crt_.underlineLine();

    for (int l = 0; l < hoehe; ++l) {
        // ZG-Byte: nur Linien 0–11 belegt (bei 15 Linien sind 12–14 dunkel), MSB = linkes Pixel.
        const uint8_t bits = (l < 12 && !z.empty) ? zg[l * 0x80 + (z.code & 0x7F)] : 0;
        uint8_t* zeile = &fb_[static_cast<size_t>(row * hoehe + l) * fb_w_ + col * 8];
        for (int x = 0; x < 8; ++x) {
            bool an = ((bits >> (7 - x)) & 1) != 0;
            if (z.rvv) an = !an;                                  // Antivalenzgatter A17
            if (z.lten && l == ul) an = true;                     // /LTEN: Hellsteuerung
            if (unsichtbar) an = false;                           // /VSP gewinnt
            if (z.cursor) {
                if (crt_.cursorUnderline()) { if (l == ul) an = true; }
                else an = !an;                                    // Blockcursor: invers
            }
            zeile[x] = an ? hell : 0;
        }
    }
}

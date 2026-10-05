/**
 * @file pc1715w_bild.h
 * @brief Bildquelle des PC 1715W: 8275 (18H/19H) mit eigenem 11-Bit-Adresszähler auf das
 *        Bild-RAM der CRT-Karte, Zeichensatzwahl 1AH, Zähler-Reset 1BH, Rastern aus dem ZG-RAM.
 *
 * Quelle: doc/pc1715/pc1715w_hardware.md §1 (1AH/1BH), §3 (AP-W0), doc/design/21_pc1715.md AP-W1.
 *
 * - Der 8275 holt das Bild NICHT über die UA858-DMA, sondern über einen Zähler der CRT-Karte
 *   (je DACK +1, 11 Bit); zurückgesetzt wird er mit OUT 1BH (/ZRES, Wert gleichgültig) und
 *   [?] mit VRTC (hier: Bildanfang in @ref frame()).  Bildanfang = Bild-RAM-Offset 0 (3000H).
 * - ZG-Wahl je Zelle: **1AH Bit 4 XOR GPA0** (0 = ZG-RAM 1 bei 2000H, 1 = ZG-RAM 2 bei 2800H).
 * - Zeichen: Byte = ZG[Linie · 128 + Code], Bit 7 = linkes Pixel, Linien 0–11.
 * - Framebuffer wie @ref Pc1715Zre: 1 Byte je Pixel (0 dunkel, 0xB0 normal, 0xFF hell),
 *   80 × 24 Zeichen à 8 × 12 = 640 × 288.  Die Rasterung ist eine Kopie der des Pc1715Zre
 *   (dort Elementfunktion mit ROM-ZG); eine gemeinsame Funktion hätte die ZRE angefasst.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/cards/pc1715w_speicher/pc1715w_speicher.h"
#include "core/primitives/i8275.h"
#include <cstdint>
#include <vector>

class Pc1715wBild : public BusDevice {
public:
    static constexpr uint8_t PORT_BASIS = 0x18;   ///< 18H Parameter, 19H Befehl/Status, 1AH ZG, 1BH /ZRES
    static constexpr int COLS = 80, ROWS = 24, LINIEN = 12;
    static constexpr int FB_W = COLS * 8, FB_H = ROWS * LINIEN;

    explicit Pc1715wBild(Pc1715wSpeicher& speicher);

    // ─── BusDevice (18H–1BH, relativ 0–3) ────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "PC1715W-CRT"; }
    void attachToBus(K1520Bus& bus) { bus.registerIO(this, PORT_BASIS, 4); }

    void reset();   ///< /RESET: 8275, 1AH = 0, Zähler 0

    /// Ein Bild: Zähler zurück (VRTC), 8275-DMA aus dem Bild-RAM, Zellen füllen, rastern.
    void frame();

    I8275&       crt()       { return crt_; }
    const I8275& crt() const { return crt_; }
    bool     zg2() const     { return zg2_; }          ///< 1AH Bit 4
    uint16_t zaehler() const { return zaehler_; }      ///< aktueller 11-Bit-Adresszähler
    uint8_t  screenChar(int col, int row) const;

    const uint8_t* framebuffer() const { return fb_.data(); }
    int  fbWidth()  const { return FB_W; }
    int  fbHeight() const { return FB_H; }
    bool fbDirty()  const { return fb_dirty_; }
    void fbClearDirty()   { fb_dirty_ = false; }

private:
    void rastern();
    void zeichneZelle(int row, int col, const I8275::Cell& c);

    Pc1715wSpeicher& spk_;
    I8275    crt_;
    bool     zg2_ = false;
    uint16_t zaehler_ = 0;
    std::vector<uint8_t> fb_;
    bool     fb_dirty_ = false;
};

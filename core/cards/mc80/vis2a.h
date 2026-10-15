/**
 * @file vis2a.h
 * @brief VIS 2a (50300.3220) des MC80.3x: Vollgrafik 512 × 256 Bildpunkte, nur über E/A.
 *
 * Quellen: doc/mc80/hardware_3x.md §5 (AP M0, [BDA S.19–20]), doc/mc80/egos.md §1.8/§1.9
 * (AP M1), AP M3 in doc/design/32_….
 *
 * **Schnittstelle** (`OUT (C),r`): C = Port C0H–CFH, r = X (0…255 je Bildhälfte), B = Y
 * (0…255, auf A8–A15) bzw. Opcode.  Port-Bit 3 = Bildhälfte (0 links, 1 rechts), Bit 2–0 =
 * Funktion:
 *   - 0 Bildpunkt dunkel, 1 Bildpunkt hell,
 *   - 2 / 3 Bildpunkt ins IN-Register (lesbar mit `IN A,(C)` an jedem Port der Karte, Bit 0),
 *   - 4 / 5 Bildkommando in B (Bit 0 der Funktion = C0 = Hintergrund hell),
 *   - 6 / 7 nicht belegt — verworfen.
 *
 * **Y = 0 ist UNTEN** (egos.md §1.8: Musterbit 0 bei kleinstem Y, erste Textzeile Y = F8H).
 * **X wächst nach rechts** [Annahme; Beleg: EGOS zeichnet die Spalten eines Zeichens mit
 * steigendem X, Text ist nur so lesbar — Wächter `Mc80Boot.EgosMeldetSich`].  Die
 * Bitreihenfolge im Bildspeicher ist von außen nicht beobachtbar (der Speicher hängt nicht
 * am Systembus) und wird deshalb nicht nachgebildet: ein Byte je Bildpunkt.
 *
 * **Lesepolarität [?]:** [BDA S.19] nennt Funktion 2 „Bildpunkt → IN“ und 3 „negierter
 * Bildpunkt → IN“.  Die Segment-ROMs des MC80.31E lesen mit Funktion 3 und schreiben bei
 * Bit 0 = 1 „dunkel“, sonst „hell“ zurück — über das ganze Bild (seg_20_4000-h.bin 4C40H);
 * nach [BDA] wäre das ein wirkungsloser Durchlauf, als „Bild invertieren“ ergibt es Sinn,
 * wenn das IN-Register am Datenbus invertiert ankommt.  **Festlegung:** `IN` liefert in
 * Bit 0 nach Funktion 3 den Bildpunkt (1 = hell), nach Funktion 2 den negierten; Bit 1–7
 * lesen 1 [?].  Wächter `Mc80Vis2a.LesenFunktion3LiefertDenBildpunkt`.
 *
 * **Bildkommandos** (Opcode B = hohes/niedriges Nibble h/n, [BDA S.20]; die Tabelle dort hat
 * Schreibfehler, hardware_3x.md §5.2):
 *   - h = 0: n & 3 = 0 Bild löschen (Hintergrund nach C0), Rollzähler 0; 1 Bild aus;
 *     2 Bild ein; 3 ohne Wirkung [?],
 *   - h = 1: n > 0 Bild um n Zeilen nach OBEN rollen, die unten neu erscheinenden Zeilen
 *     nach C0 gefüllt; n = 0 Rollzähler 0 [?: „Horizontal-/Vertikalrollzähler“],
 *   - h = 2: ebenso nach UNTEN (neue Zeilen oben),
 *   - h = 5: Lichtstift-Initialisierung — ohne Wirkung (kein Lichtstift),
 *   - sonst ohne Wirkung.
 * Die Karte führt die Kommandos **sofort** aus (EGOS wartet danach ~43 ms fest, egos.md §1.8).
 *
 * **Rollen** ändert nur den Bildanfang (Rollzähler R), nicht den Speicher: Bildschirmzeile d
 * (0 = oben) zeigt Speicherzeile Y = (255 − d + R) mod 256.  Rollen nach oben um n: R −= n —
 * genau so rechnet EGOS (Rollzähler FB4AH, ZEILE_ZU_PIXEL 01E7H).
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include <array>
#include <cstdint>
#include <vector>

class Mc80Vis2a : public BusDevice {
public:
    static constexpr int BREITE = 512;
    static constexpr int HOEHE  = 256;

    struct Config {
        uint8_t io_base = 0xC0;   ///< AB4–AB7 per Wickel X6/X7, Vorgabe C0H–CFH
    };

    explicit Mc80Vis2a(K1520Bus& bus);
    Mc80Vis2a(K1520Bus& bus, const Config& cfg);

    void attachToBus(K1520Bus& bus);

    // ─── BusDevice ───────────────────────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "MC80 VIS 2a"; }

    /// Netz ein: Speicher dunkel, Rollzähler 0, Bild ein [?].  /RESET berührt die Karte nicht.
    void powerOn();

    // ─── Zustand (Tests, Bildaufbau, Textrückgewinnung) ─────────────────────
    /// Bildpunkt im Speicher: x = 0…511 (Hälfte · 256 + X), y = 0…255 (0 = unten).
    bool    punkt(int x, int y) const { return bild_[idx(x, y)] != 0; }
    void    setzePunkt(int x, int y, bool hell);
    uint8_t rollzaehler() const { return roll_; }
    bool    bildEin() const { return ein_; }
    uint8_t inRegister() const { return in_reg_; }
    /// Zähler jeder sichtbaren Änderung (Textrückgewinnung puffert danach).
    uint64_t revision() const { return revision_; }

    /// Anzeige 512 × 256, ein Byte je Bildpunkt (00H/FFH), Zeile 0 = oben.
    const uint8_t* framebuffer() const { return fb_.data(); }
    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

private:
    static size_t idx(int x, int y) { return static_cast<size_t>(y & 0xFF) * BREITE + (x & 0x1FF); }
    void kommando(uint8_t opcode, bool hell);
    void fuelleSpeicherzeile(int y, bool hell);
    void anzeigeNeu();
    void anzeigePunkt(int x, int y);
    void geaendert() { dirty_ = true; ++revision_; }

    K1520Bus& bus_;
    Config    cfg_;
    std::vector<uint8_t> bild_;   ///< Bildspeicher, Index y·512 + x, 0/1
    std::vector<uint8_t> fb_;     ///< Anzeige, Zeile 0 = oben
    uint8_t  roll_ = 0;
    bool     ein_ = true;
    uint8_t  in_reg_ = 0xFF;
    bool     dirty_ = true;
    uint64_t revision_ = 0;
};

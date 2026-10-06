/**
 * @file floppy8.h
 * @brief Floppy-Seite der 8-Bit-Rechnerkarte des P8000: U8272 (`Upd765`) + UA858 (`Z80Dma`) +
 *        PIO2-Verdrahtung + zwei interne Laufwerke (AP P5f).
 *
 * Quelle: doc/p8000/schaltplan_8bit.md §6 (maßgeblich), doc/design/25_p8000.md §10.7.
 * Hängt sich an eine @ref P8000Karte8: FDC-Tore 20H–23H, DMA 3CH–3FH, DMA als Busmaster
 * (die CPU steht, solange /BUSRQ gilt), DMA als vorderstes Kettenglied, PIO2-Pins.
 * Die Laufschleife ruft nach jedem `karte.schritt()` auch @ref takt (Zeitbasis des FDC).
 *
 * @code
 *   PIO2-A0..3  /MO0..3  Motor (0 = ein); interne Laufwerke laufen zusätzlich bei Auswahl (ML)
 *   PIO2-A4     0 = 8 MHz (8″, 500 kbit/s MFM) / 1 = 4 MHz (5¼″, 250 kbit/s) → Upd765::setDatenrate
 *   PIO2-A5     Kopf laden erzwingen (nur Anzeige)       PIO2-A6  TC per Software (∨ DMA-Blockende)
 *   PIO2-B0..3  /SE0..3  Laufwerkswahl ALLEIN (US0/US1 des U8272 unbeschaltet → Upd765::unitAuswahl)
 *   PIO2-B4     FDC-INT (Eingang, Pegel)                 PIO2-B7  FDC-RESET
 *   DRQ → (ein Takt Verzug, 7474) → DMA-RDY (aktiv high)
 *   TC  = DMA-Blockende-Pegel ∨ PIO2-A6;  DACK = Zugriff der DMA auf 20H–23H
 * @endcode
 *
 * **Annahmen** (Schaltplan §6, §11): PIO2-B7 = RESET high-aktiv (Pull-up ⇒ FDC nach Reset im
 * Reset, bis die Software B7 = 0 ausgibt); PB5/PB6 unbeschaltet (lesen 1); Index nur an den FDC
 * (der `Upd765` ohne Drehlage braucht ihn nicht); das DMA-INT/PULSE wird als „Blockende erreicht"-
 * Pegel des `Z80Dma` nachgebildet (unabhängig davon, ob der Gast den DMA-Interrupt freigibt).
 */

#pragma once
#include "core/cards/k5122/k5122.h"
#include "core/cards/p8000/karte8.h"
#include "core/machines/laufwerke.h"
#include "core/primitives/upd765.h"
#include "core/primitives/z80_dma.h"
#include <array>
#include <string>

class P8000Floppy8 {
public:
    struct Config {
        /// Bestückung der vier Anschlüsse (X8 = 0, X9 = 1, X10 extern = 2/3); Vorgabe 2 × K5601.
        std::array<std::string, 4> laufwerke = {"K5601", "K5601", "none", "none"};
        /// PIO2-B7 → FDC-RESET [Annahme high-aktiv].
        bool fdc_reset_high_aktiv = true;
    };

    explicit P8000Floppy8(P8000Karte8& karte) : P8000Floppy8(karte, Config{}) {}
    P8000Floppy8(P8000Karte8& karte, const Config& cfg);
    ~P8000Floppy8();

    /// Φ-Takte an den FDC; DRQ → DMA-RDY (ein Takt Verzug); Pin-Ausgänge der PIO2 neu bewerten.
    void takt(int n);

    // ─── Laufwerke / Medien (.img/.hfe/.dmk über den vorhandenen Stack) ──────
    bool mount(int drive, const std::string& path, const std::string& format_name, bool write_protect = false)
        { return lw_.mountDisk(drive, path, format_name, write_protect); }
    bool unmount(int drive) { return lw_.unmountDisk(drive); }
    Laufwerke& laufwerke() { return lw_; }
    FloppyDriveV2& laufwerk(int drive) { return afs_.drive(drive); }

    // ─── Bausteine / Leitungen (Tests, Debugger) ─────────────────────────────
    Upd765& fdc() { return fdc_; }
    Z80Dma& dma() { return dma_; }
    /// Angesprochenes Laufwerk nach PB0–3 (genau eine 0 ⇒ Nummer, sonst -1).
    int  gewaehlt() const;
    bool motorAn(int drive) const;
    bool tcPegel() const { return dma_ende_ || ((karte_.pio2().pinsA() & 0x40) != 0); }
    bool fdcIntPegel() const { return fdc_.irq(); }

    // ─── Save-State (P8KS): U8272, UA858, Leitungs-Flags, Kopfposition je Laufwerk ──
    // Medieninhalt NICHT (der Aufrufer mountet dieselben Abbilder neu, wie beim A5120).
    // Beim Laden zuerst DIESEN Block anwenden, danach die Karte: die Rückrufe des FDC (INT → PIO2-B4)
    // schreiben in die PIO2, die der Kartenblock anschließend endgültig setzt.
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    void pinsAuswerten();
    void reset();

    P8000Karte8& karte_;
    Config       cfg_;
    K5122        afs_;     ///< nur Halter der FloppyDriveV2 (nicht am Bus)
    Laufwerke    lw_;
    Upd765       fdc_;
    Z80Dma       dma_{"P8K-DMA"};
    bool dma_ende_ = false;
    bool drq_pin_ = false, rdy_ = false;
    bool fdc_resetet_ = false;   ///< FDC-RESET-Pin aktiv (Pegel nach Polarität)
};

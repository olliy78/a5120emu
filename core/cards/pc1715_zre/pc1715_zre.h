/**
 * @file pc1715_zre.h
 * @brief ZRE (Hauptplatine) des PC 1715: U880 2,458 MHz, 64 KB DRAM, Urlader-ROM S502 als
 *        Overlay, CTC0 + SIO0, CRT-Controller 8275 mit DMA aus dem Haupt-RAM und Rastern.
 *
 * Quellen: Servicehandbuch PC 1715 (`pc_serv.pdf`) §1.2.4–§1.2.7, doc/design/21_pc1715.md §3.
 *
 * @code
 *   CPU   U880 (Z80), 9,832 MHz / 4 = 2,458 MHz;  EINE CPU
 *   RAM   64 KB, 0000H–FFFFH; ROM-Overlay S502 2 KB auf 0000H–07FFH:
 *         lesen = ROM, schreiben = RAM darunter (§1.2.5); aus mit OUT 28H–2BH,
 *         ein mit OUT 24H–27H und bei /RESET
 *   CTC0  08H–0BH (Takt für SIO)
 *   SIO0  0CH Daten A, 0DH Daten B, 0EH Steuer A, 0FH Steuer B       [Doku §1.2.4, Tabelle
 *         „I/O-Toradressen"]: AB0 = Kanal (A = 0, B = 1), AB1 = Steuer/Daten
 *   8275  18H/1AH Parameter, 19H/1BH Befehl/Status (AB0 = Befehl)
 *   LT107 2DH/2FH lesen, LT111 2CH/2EH schreiben: Leitungen 107/111 der ZUSATZKARTE (§1.3.3,
 *         DB0 = Kanal A, DB2 = B) — nicht bestückt, 2DH liest FFH, 2CH ist nur ein Latch
 *   LT111 30H–33H schreiben: Leitung 111 am Stecker X5, DB1 = EIN (§1.2.8.5)
 *   BWS   34H–37H: Bildspeicher-Anfangsadresse (höherwertiger Teil) + DB6 = Zeichengenerator
 * @endcode
 *
 * **BWS-Register 34H (Beleg).**  Servicehandbuch §1.2.7.3: der niederwertige Teil der DMA-Adresse
 * kommt aus dem Adresszähler (10 Bit bei K7221.25, 11 Bit bei K7222.25), der höherwertige aus dem
 * Register A23.2 (OUT 34H); „bei K7221.25 wird das 11. Adressbit über Br. X12 auch von A23.2
 * übernommen".  Die Lage der Bits ergibt das CP/A-BIOS (`biopcrt.mac`, `bsanf1 = F800H`,
 * `OUT (34H), high(bsanf1 SHR 2)`): Registerwert = Adresse >> 10, also **DB0 = A10 … DB5 = A15**.
 * Beim 80×24 (11-Bit-Zähler) liefert der Zähler A10; DB0 wird dort nicht ausgewertet, die
 * Basis ist 2-KB-ausgerichtet.  **DB6** wählt den Zeichengenerator (§1.2.7.4: „DB6 = low/high";
 * BIOS `crt2zs: LD A,40H` = 2. Zeichensatz, `crt1zs: XOR A` = 1.).  DB7 ungenutzt.
 * Lesen des Registers: ungeklärt [?] — hier FFH wie ein unbelegtes Tor.
 *
 * **Zeichengenerator.**  2 × 2 KB (A25.2 = S619 = „ZG1", A25.1 = S602 = „ZG2"), Byte =
 * ROM[Linie · 80H + Code], MSB = linkes Pixel, nur Linien 0–11 belegt.  Gewählt wird je Zelle
 * mit (DB6 XOR GPA0 des Feldattributs) [?: ob die Verknüpfung ein XOR ist, geht aus dem
 * Handbuch nur als „auf den jeweils anderen ZG umschalten" hervor].  Welcher Baustein bei
 * DB6 = 0 gilt, steht in @ref Config::zg_bei_db6_low.
 *
 * **Bild.**  Kein eigener Bildspeicher: @ref frame() lässt den 8275 die Zeilenpuffer aus dem
 * Haupt-RAM füllen (Adresse = Basis | Zähler, Zähler bei VRTC auf 0) und rastert das Ergebnis
 * in einen Grauwert-Framebuffer (1 Byte je Pixel wie die K7024; 0 dunkel, 0xB0 normal,
 * 0xFF hell).  80×24: 640 × 288, 64×16: 512 × 240.  Die Maschine ruft frame() alle 20 ms
 * Maschinenzeit.  Der IRQ-Ausgang des 8275 ist im Grundgerät nicht in der Interruptkette [?].
 *
 * **Schnittstellen nach außen (AP-4a, Entwurf 19).**  Zwei `SerialAnschluss`, Namen nach der
 * Gerätebeschriftung: „Drucker X4" = SIO0-A **nur Sender** (der Empfänger von SIO-A ist die
 * Tastatur und bleibt fest verdrahtet), Sendetakt CTC0 K0, CTSA = Leitung 106; „V.24 X5" =
 * SIO0-B (Senden + Empfangen), Takt CTC0 K1, CTSB/DCDB = 106/109.  Leitung 107 (DSR) des
 * Steckers X5 liegt an `/DCDA` (§1.2.8.5, AP-4f — vorher fälschlich an 2DH); Leitung 111
 * (30H–33H) ist ein Ausgang des Gastes, den der Hub nicht kennt (`lt111X5`).  Prüfstecker
 * (Loop am Hub): die Karte bildet die Brücken von 330-042/320-032 selbst und sofort nach.
 *
 * **Nicht in dieser Karte:** Floppy (PIOs 00H–07H, KRFD 20H–23H: AP-2, vorn in der
 * Interruptkette), Tastatur (AP-3).  Unbelegte Ports lesen FFH, Schreiben wird verschluckt.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/i8275.h"
#include "core/primitives/z80.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_sio.h"
#include "core/serial/anschluss.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class Pc1715Zre : public BusDevice, public InterruptSlave {
public:
    /// Bildschirmtyp (feste Parametersätze des BIOS, §3.4): Hardwarevariante, kein Laufzeitschalter.
    enum class Bildschirm : uint8_t {
        K7222,   ///< 80 × 24, Zeichenfeld 8 × 12, 11-Bit-Adresszähler (Vorgabe)
        K7221,   ///< 64 × 16, Zeichenfeld 8 × 15, 10-Bit-Adresszähler
    };
    enum class Zeichensatz : uint8_t { S619, S602 };

    struct Config {
        Bildschirm  bild = Bildschirm::K7222;
        /// Welcher Zeichengenerator bei BWS-Register DB6 = 0 (und GPA0 = 0) gilt.
        Zeichensatz zg_bei_db6_low = Zeichensatz::S619;
        /**
         * PC 1715W (AP-W3, doc/pc1715/pc1715w_hardware.md §1): dieselbe CPU mit CTC0 08H und
         * SIO0 0CH (Tastatur, Drucker X4, V.24 X5) und LT107/LT111 — aber Takt 3,9936 MHz,
         * CTC0 **ohne Interrupt** (nur Baudtakte), und 18H–1BH (8275), 24H–2BH (BR/MOS),
         * 34H (KON) gehören anderen Bausteinen der Maschine.  Speicher, ROM-Overlay, BWS und
         * Bild der Karte sind dann unbenutzt (die Maschine biegt den Speicherweg der CPU um).
         */
        bool w = false;
    };

    /// Systemtakt: Quarz 9,832 MHz / 4 (§3.1).
    static constexpr uint32_t CPU_HZ = 2'458'000;
    /// Systemtakt des PC 1715W: 15,9744 MHz / 4 (doc/pc1715/pc1715w_hardware.md §5).
    static constexpr uint32_t CPU_HZ_W = 3'993'600;
    /// Bildwechsel alle 20 ms Maschinenzeit (50 Hz) [?: Bildfrequenz aus den Parametersätzen nicht gerechnet].
    static constexpr uint32_t FRAME_TAKTE = CPU_HZ / 50;

    explicit Pc1715Zre(K1520Bus& bus);
    Pc1715Zre(K1520Bus& bus, const Config& cfg);
    ~Pc1715Zre() override;

    /// Serielle Kanäle, die nach außen gehen; zugleich Hub-Reihenfolge (= C-ABI-Index).
    enum Kanal : int { Drucker = 0, V24 = 1, KanalAnzahl = 2 };
    k1520::serial::SerialAnschluss& anschluss(Kanal k);
    /// Ein Anschluss hat SIO-Zustand geändert — Interruptkette neu bewerten.  Liest und löscht.
    bool nimmSeriellGeaendert() { const bool g = seriell_geaendert_; seriell_geaendert_ = false; return g; }
    /// Alter Unterbau (`K1520Machine::setPrinterCallback`/`setDFUECallback`): Abnehmer der Bytes,
    /// die der Gast an Kanal @p k SENDET, solange kein Transport/Loop den Stecker belegt.
    using Abnehmer = std::function<void(uint8_t)>;
    void setAbnehmer(Kanal k, Abnehmer cb);
    /// Ein Byte von außen in den Empfänger (nur V.24; am Drucker gibt es keinen); ins Leere,
    /// wenn ein Transport/Loop den Stecker belegt.
    void einspeisen(Kanal k, uint8_t byte);
    /// Leitung 107 (DSR) des Steckers X5, wie vom Hub gemeldet (liegt an /DCDA, §1.2.8.5).
    bool lt107V24() const;

    // ─── BusDevice: alle Ports der Karte (absolute Portnummer) ───────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "ZRE PC1715"; }

    // ─── InterruptSlave: IEI → CTC → SIO → IEO ───────────────────────────────
    void        setIEI(bool iei) override;
    bool        getIEO() const override       { return sio_.getIEO(); }
    bool        hasInterrupt() const override { return ctcMitInt() || sio_.hasInterrupt(); }
    uint8_t     getVector() const override;
    void        onRETI() override;
    const char* intDeviceName() const override;

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void attachToBus(K1520Bus& bus);   ///< CTC0, SIO0, 8275, ROM-Tore, LT107/LT111, BWS
    void reset();                      ///< /RESET: CPU, CTC, SIO, 8275, ROM ein; RAM bleibt
    void powerOn(uint8_t fill = 0x00); ///< RAM vorbelegen, dann reset()
    bool clockTick(int ticks) { return ctc_.clockTick(ticks); }

    Z80&       cpu()       { return cpu_; }
    const Z80& cpu() const { return cpu_; }
    Z80CTC&    ctc()       { return ctc_; }
    Z80SIO&    sio()       { return sio_; }
    I8275&     crt()       { return crt_; }

    // ─── Speicher (Sicht der CPU) ────────────────────────────────────────────
    uint8_t memRead(uint16_t a) const { return (rom_ein_ && a < 0x0800) ? rom_[a] : ram_[a]; }
    void    memWrite(uint16_t a, uint8_t d) { ram_[a] = d; }   ///< immer ins RAM (auch unter dem ROM)
    bool    romEin() const { return rom_ein_; }
    /// RAM ohne ROM-Overlay (Tests, Debugger).
    uint8_t ramPeek(uint16_t a) const { return ram_[a]; }
    void    ramPoke(uint16_t a, uint8_t d) { ram_[a] = d; }

    // ─── Bild ────────────────────────────────────────────────────────────────
    /// Ein Bild: 8275-DMA aus dem RAM, Zellen füllen, rastern, IRQ.
    void frame();
    const uint8_t* framebuffer() const { return fb_.data(); }
    int  fbWidth()  const { return fb_w_; }
    int  fbHeight() const { return fb_h_; }
    bool fbDirty()  const { return fb_dirty_; }
    void fbClearDirty() { fb_dirty_ = false; }
    /// Zeichencode der Zelle des letzten Bildes (7 Bit); leere Zelle = 20H.
    uint8_t screenChar(int col, int row) const;
    int  textCols() const { return cfg_.bild == Bildschirm::K7222 ? 80 : 64; }
    int  textRows() const { return cfg_.bild == Bildschirm::K7222 ? 24 : 16; }
    int  zeichenLinien() const { return cfg_.bild == Bildschirm::K7222 ? 12 : 15; }
    /// Höchstens auswertbare Zeilen: Textzeilen + 1 Statuszeile.  CP/A programmiert den 8275 auf
    /// 25 bzw. 17 Zeilen (`biopcrt.mac`, `cpastz = 1`: inverse Statuszeile, doc/pc1715/cpa_bios.md
    /// §2).  screenChar() liefert sie (Zeile textRows()); der Framebuffer hat seit AP-5a
    /// maxZeilen() Zeilen (640 × 300 bzw. 512 × 255), die Statuszeile wird mitgerastert.
    int  maxZeilen() const { return textRows() + 1; }
    /// Vom 8275 programmierte Zeilenzahl, begrenzt auf maxZeilen() (vor dem Programmieren 0).
    int  bildZeilen() const { return std::min(crt_.rows(), maxZeilen()); }

    /// BWS-Register (34H): roh, Basis der DMA-Adresse und gewählter Zeichengenerator.
    uint8_t  bwsRegister() const { return bws_; }
    uint16_t bildBasis() const;
    bool     bwsZg2() const { return (bws_ & 0x40) != 0; }
    /// LT111 der Zusatzkarte (2CH/2EH, DB0 = Kanal A, DB2 = B; §1.3.3) — nur Latch.
    bool     lt111(int kanal) const { return lt111_[kanal & 1]; }
    /// Leitung 111 am Stecker X5 (/LT111CS 30H–33H, DB1; §1.2.8.5).
    bool     lt111X5() const { return lt111_x5_; }

    const Config& config() const { return cfg_; }

private:
    /// CTC0 meldet einen Interrupt (am 1715W liegt sie nicht in der Kette).
    bool ctcMitInt() const { return !cfg_.w && ctc_.hasInterrupt(); }
    void rastern();
    void zeichneZelle(int row, int col, const I8275::Cell& c);
    const uint8_t* zgRom(bool zweiter_satz) const;

    K1520Bus& bus_;
    Config    cfg_;

    Z80     cpu_;
    Z80CTC  ctc_{"PC1715-CTC0"};
    Z80SIO  sio_{"PC1715-SIO0"};
    I8275   crt_;

    std::array<uint8_t, 0x10000> ram_{};
    const uint8_t* rom_;       ///< S502 (2 KB)
    bool     rom_ein_ = true;

    uint8_t  bws_ = 0;
    uint16_t dma_zaehler_ = 0;
    bool     lt111_[2] = {false, false};
    bool     lt111_x5_ = false;
    /// Prüfstecker: Brücken der gesteckten Stecker sofort an die SIO legen.
    void     leitungenAnlegen();

    class Anschluss;   // pc1715_zre.cpp
    friend class Anschluss;
    std::array<std::unique_ptr<Anschluss>, KanalAnzahl> anschluesse_;
    bool seriell_geaendert_ = false;

    std::vector<uint8_t> fb_;
    int  fb_w_ = 0, fb_h_ = 0;
    bool fb_dirty_ = false;
};

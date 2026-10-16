/**
 * @file zve3x.h
 * @brief ZVE des MC80.3x (50300.3010 / 50302.3010): U880, 4 × 2716 (0000H–1FFFH),
 *        16 KB dRAM C000H–FFFFH, CTC 80H–83H, PIO 84H–87H, RAM-Steuerung 8CH/8DH.
 *
 * Quellen: doc/mc80/hardware_3x.md (AP M0) §2–§3, doc/mc80/egos.md (AP M1) §1,
 * Entwurf doc/design/31_mc80.md, AP M2 in doc/design/32_….
 *
 * **Segmentregister = ZVE-PIO Kanal B** (Datenport 85H, Steuerport 87H; A = Einsernummer
 * 01H, 02H, 04H … 80H) — NICHT Kanal A (Berichtigung M0 zu Entwurf 31 §3.1).  Die Karte
 * meldet jede Ausgabe von Tor B ueber @ref setSegmentCallback; wer welches Bit auswertet
 * (SPE-Karten, spaeter EPROM-Segmente), ist Sache der Maschine.  Nach /RESET steht das
 * Register auf 00H (Ausgaberegister der PIO geloescht, Tor B im Eingabebetrieb — ob die
 * offenen Leitungen am Geraet als „alle Segmente frei“ wirken, ist offen [?]); EGOS schreibt
 * erst `0FH` → 87H und dann bei jedem Objekt der RESET-Kette `OUT (85H)`.
 *
 * **PIO-Adressierung:** B/A = AB0, C/D = AB1 (84H A-Daten, 85H B-Daten, 86H A-Steuer,
 * 87H B-Steuer [BDA S.15]); der Baustein @ref Z80PIO zaehlt 0 = A-D, 1 = A-S, 2 = B-D,
 * 3 = B-S — die Karte uebersetzt wie die K2521.
 *
 * **Schreibschutz FC00H–FFFFH (8CH/8DH):** EGOS schreibt nur in der RESET-Schleife
 * 256-mal nach 8CH und beschreibt danach FC00H–FFFFH ungehindert (egos.md §1.6).  Darum:
 * nach /RESET AUS, und 8CH/8DH sind vorerst reine **Schreibsenken** (Wirkung der einzelnen
 * Ausgaben unbekannt [?]).  Der Schutz selbst ist als Zustand nachgebildet
 * (@ref setSchreibschutz) — geschuetzte Schreibzugriffe sind wirkungslos, eine NMI ist nicht
 * belegt und wird nicht ausgeloest.
 *
 * **Interruptkette in der Karte:** IEI → CTC → PIO → IEO [? — Reihenfolge innerhalb der
 * ZVE nicht belegt; wie K2521].  EGOS programmiert beide Bausteine nie (egos.md §1, Punkt 9).
 *
 * Speicher: die Karte meldet ROM (0000H–1FFFH) und RAM (C000H–FFFFH) selbst am Bus an.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include <array>
#include <cstdint>
#include <functional>

class Mc80Zve : public BusDevice, public MemDevice, public InterruptSlave {
public:
    /// Systemtakt: Quarz 9830,4 kHz ÷ 4 (hardware_3x.md §2.1).
    static constexpr uint32_t CPU_HZ = 2'457'600;

    static constexpr uint16_t ROM_BASIS = 0x0000;
    static constexpr uint16_t ROM_GROESSE = 0x2000;   ///< 4 × 2716
    static constexpr uint16_t RAM_BASIS = 0xC000;     ///< Wickel X7, Vorgabe C000H
    static constexpr uint16_t RAM_GROESSE = 0x4000;   ///< 16 KB dRAM
    static constexpr uint16_t SCHUTZ_BASIS = 0xFC00;  ///< 1-KB-Schreibschutzfenster

    struct Config {
        const uint8_t* rom = nullptr;   ///< nullptr = MAME-Satz zve_1…4 (EGOS 3.0)
        uint16_t rom_len = 0;           ///< hoechstens 2000H; unbestueckte Fassungen lesen FFH
        uint8_t  ctc_base = 0x80;       ///< Wickel X4, Vorgabe 80H
        uint8_t  pio_base = 0x84;
        uint8_t  ramst_base = 0x8C;     ///< RAM-Steuerung 8CH/8DH
    };

    explicit Mc80Zve(K1520Bus& bus);
    Mc80Zve(K1520Bus& bus, const Config& cfg);

    // ─── BusDevice: CTC, PIO, 8CH/8DH ────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "MC80 ZVE"; }

    // ─── MemDevice: ROM 0000H–1FFFH, RAM C000H–FFFFH ─────────────────────────
    uint8_t memRead(uint16_t a) override;
    void    memWrite(uint16_t a, uint8_t d) override;

    // ─── InterruptSlave ──────────────────────────────────────────────────────
    void        setIEI(bool iei) override;
    bool        getIEO() const override       { return ctc_.getIEO() && pio_.getIEO(); }
    bool        hasInterrupt() const override { return ctc_.hasInterrupt() || pio_.hasInterrupt(); }
    uint8_t     getVector() const override;
    void        onRETI() override;
    const char* intDeviceName() const override;

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void attachToBus(K1520Bus& bus);   ///< E/A + Speicher anmelden
    void reset();                      ///< /RESET: CPU, CTC, PIO, Segmentregister 0, Schutz aus; RAM bleibt
    void powerOn(uint8_t fill = 0x00); ///< RAM vorbelegen, dann reset()
    bool clockTick(int ticks) { return ctc_.clockTick(ticks); }

    Z80&       cpu()       { return cpu_; }
    const Z80& cpu() const { return cpu_; }
    Z80CTC&    ctc()       { return ctc_; }
    Z80PIO&    pio()       { return pio_; }

    /// Segmentregister (letzte Ausgabe an ZVE-PIO Tor B; 00H nach /RESET).
    uint8_t segmentRegister() const { return segment_; }
    using SegmentCallback = std::function<void(uint8_t maske)>;
    /// Rueckruf bei jeder Ausgabe an Tor B und beim /RESET (mit 00H).
    void setSegmentCallback(SegmentCallback cb) { segment_cb_ = std::move(cb); }

    bool schreibschutz() const { return schreibschutz_; }
    void setSchreibschutz(bool an) { schreibschutz_ = an; }
    /// Zahl der Ausgaben an 8CH/8DH seit dem Einschalten (Diagnose; ohne Wirkung).
    uint32_t ramSteuerAusgaben() const { return ramst_ausgaben_; }

    uint8_t ramPeek(uint16_t a) const { return ram_[(a - RAM_BASIS) & (RAM_GROESSE - 1)]; }
    void    ramPoke(uint16_t a, uint8_t v) { ram_[(a - RAM_BASIS) & (RAM_GROESSE - 1)] = v; }

private:
    uint8_t pioPort(uint8_t rel) const {
        return static_cast<uint8_t>(((rel & 1) << 1) | ((rel >> 1) & 1));
    }
    void segmentSetzen(uint8_t m);

    K1520Bus& bus_;
    Config    cfg_;
    Z80       cpu_;
    Z80CTC    ctc_{"MC80-ZVE-CTC"};
    Z80PIO    pio_{"MC80-ZVE-PIO"};
    std::array<uint8_t, RAM_GROESSE> ram_{};
    uint8_t   segment_ = 0;
    bool      schreibschutz_ = false;
    uint32_t  ramst_ausgaben_ = 0;
    SegmentCallback segment_cb_;
};

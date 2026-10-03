/**
 * @file upd765.h
 * @brief Upd765 – Floppy-Controller NEC µPD765 / Intel 8272A / U8272 (Robotron PC 1715W).
 *
 * Primitiv ohne Bus-Kenntnis, **auf Sektorebene**: der Baustein arbeitet nicht mit
 * Bitzellen, sondern sucht Sektoren über die ID-Felder der @ref TrackImage des Laufwerks
 * (@ref TrackCodec::parseTrack) und schreibt Datenfelder mit @ref TrackCodec::writeSectorAt.
 * Die Spurdichte-Übersetzung (40/80 Spuren) macht @ref FloppyDriveV2 selbst; die
 * Kopfposition bewegen SEEK/RECALIBRATE über @ref FloppyDriveV2::seek.
 *
 * ### Umfang (nur, was S550/Lader/BIOS/INIT.COM des 1715W benutzen — doc/pc1715/pc1715w_hardware.md §4.4)
 * SPECIFY 03, SENSE DRIVE STATUS 04, WRITE DATA 05, READ DATA 06, RECALIBRATE 07,
 * SENSE INTERRUPT STATUS 08, READ ID 0A, FORMAT TRACK 0D, SEEK 0F, SCAN EQUAL 11.
 * Alles andere (READ TRACK, DELETED-Befehle, SCAN LOW/HIGH, VERIFY …) → ungültiger
 * Befehl, Ergebnisbyte ST0 = 80H.  MT wird unterstützt (Seitenwechsel am EOT), SK nicht
 * (gelöschte Datenmarke: CM gesetzt, Sektor trotzdem gelesen, danach Ende).
 *
 * ### Anschluss durch die Maschine
 *  - Bus: @ref read / @ref write (A0 = 0 Hauptstatus, A0 = 1 Daten), oder die Einzelzugriffe.
 *  - DMA (SPECIFY ND = 0, Vorgabe): solange @ref drq() = 1, holt die Maschine (DACK) das
 *    Byte mit @ref dmaRead (FDC → Speicher: READ DATA) bzw. liefert es mit @ref dmaWrite
 *    (Speicher → FDC: WRITE DATA, SCAN, FORMAT TRACK).  Das **TC** gibt die Maschine mit
 *    @ref setTC(true) **NACH dem letzten Byte-Transfer** (so tut es auch das Blockende des
 *    Z80-DMA); ein TC mitten im Sektor beendet die Ausführung sofort (FORMAT TRACK
 *    ignoriert TC).  TC wird als Pegel übergeben, der Baustein hält die steigende Flanke
 *    bis zum nächsten Befehl.
 *  - Nicht-DMA (ND = 1): je Byte RQM+EXM im MSR und INT; Zugriff über das Datenregister.
 *  - @ref irq() = INT-Pin (der 1715W nutzt ihn nicht; er pollt MSR/SENSE INTERRUPT STATUS).
 *  - @ref setReset(true) = RESET-Pin (KRFD Bit 6 = 0).
 *  - READY je Laufwerk: Standard = Diskette eingelegt; die Maschine kann über @ref ready
 *    zusätzlich den Motor einrechnen.
 *
 * ### Bewusste Vereinfachungen
 *  - Kein Overrun (OR): ein Byte wartet beliebig lange auf die Maschine, der Bytetakt
 *    zählt ab der Übernahme des vorigen Bytes — das macht den Ablauf von der Granularität
 *    der @ref tick -Aufrufe unabhängig.
 *  - Keine Drehlage: der erste Sektor kommt nach einer festen Latenz, kein Warten auf
 *    den Index.  READ ID liefert die ID-Felder der Spur der Reihe nach.
 *  - Sektorgröße = 128 << N aus dem ID-Feld (DTL wird ignoriert).
 *  - SEEK/RECALIBRATE laufen auch ohne Diskette/READY (Mechanik), ohne Laufwerk → EC.
 *  - Im RESET ist das MSR 00H; nach dem Lösen 80H.
 *
 * @see doc/design/21_pc1715.md §8.2, doc/pc1715/pc1715w_hardware.md §4
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/peripherals/floppy_drive/floppy_drive2.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include <cstdint>
#include <functional>
#include <vector>

class Upd765 {
public:
    static constexpr int kUnits = 4;

    /// @brief Übersetzung in Maschinentakte (Verdrahtungsangaben).
    struct Config {
        uint32_t cpu_hz      = 4000000;  ///< Takt, in dem @ref tick zählt (1715W: 4 MHz)
        uint32_t mfm_kbit    = 250;      ///< Datenrate MFM; FM läuft mit der Hälfte
        uint32_t step_cycles = 0;        ///< Takte je Spurschritt; 0 = cpu_hz / 1000 (1 ms)
    };

    Upd765() = default;
    explicit Upd765(Config cfg) : cfg_(cfg) {}

    // ── Verdrahtung ──────────────────────────────────────────────────────────
    /// @brief Laufwerk @p unit anschließen (nullptr = kein Laufwerk).  Gehört dem Aufrufer.
    void setDrive(int unit, FloppyDriveV2* d) { if (unit >= 0 && unit < kUnits) drive_[unit] = d; }
    /// @brief Optional: READY-Leitung je Laufwerk.  Leer = „Diskette eingelegt".
    std::function<bool(int unit)> ready;
    /// @brief Optional: Flankenmeldungen der Ausgänge DRQ und INT.
    std::function<void(bool)> onDrq, onIrq;

    // ── Busseite ─────────────────────────────────────────────────────────────
    uint8_t readMsr() const;
    uint8_t readData();
    void    writeData(uint8_t b);
    uint8_t read(bool a0)               { return a0 ? readData() : readMsr(); }
    void    write(bool a0, uint8_t b)   { if (a0) writeData(b); }

    // ── DMA-Seite ────────────────────────────────────────────────────────────
    bool    drq() const { return drq_; }
    uint8_t dmaRead();            ///< Byte FDC → Speicher (READ DATA), löscht DRQ
    void    dmaWrite(uint8_t b);  ///< Byte Speicher → FDC (WRITE/SCAN/FORMAT), löscht DRQ
    void    setTC(bool level);    ///< Terminal-Count-Eingang
    bool    irq() const { return irq_; }
    void    setReset(bool level); ///< RESET-Pin; true = im Reset

    /// @brief Zeit voranbringen (@p cycles Takte nach @ref Config::cpu_hz).
    void tick(uint32_t cycles);

    // ── Beobachtung (Tests/Debugger) ─────────────────────────────────────────
    uint8_t pcn(int unit) const { return pcn_[unit & 3]; }
    bool    nonDma() const { return nd_; }
    bool    seekBusy(int unit) const { return seek_left_[unit & 3] >= 0; }

    // ── Save-State (Verdrahtung/Rückrufe nicht) ──────────────────────────────
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    enum class Phase : uint8_t { Idle, Command, Exec, Result };
    enum class Kind  : uint8_t { None, Read, Write, Scan, Format, ReadId };
    enum class Stage : uint8_t { Latency, Transfer, Tail };

    // ST1/ST2-Bits
    static constexpr uint8_t kMA = 0x01, kNW = 0x02, kND = 0x04, kDE = 0x20, kEN = 0x80;
    static constexpr uint8_t kMD = 0x01, kBC = 0x02, kSN = 0x04, kSH = 0x08, kWC = 0x10,
                             kDD = 0x20, kCM = 0x40;

    void command();
    void invalid();
    void startRw(Kind k);
    void startReadId();
    void startFormat();
    void startSeek(bool recal);
    void completeSeek(int u);
    void senseInterrupt();
    void senseDrive();

    bool prepare();                 // READY/WP-Prüfung; false = Ergebnis steht schon
    void startSector();
    void onDelay();
    void presentByte();
    void byteTaken(uint8_t b);
    void endSector();
    void commitWrite();
    void commitFormat();
    void finish(uint8_t ic);        // Ergebnisphase aus Exec-Zustand
    void toResult(std::vector<uint8_t> r, bool raiseIrq);
    void abortByTc();

    int64_t bytePeriod() const;
    FloppyDriveV2* drv() const { return drive_[unit_]; }
    bool unitReady(int u) const;
    void setDrq(bool v);
    void setIrq(bool v);
    void resetState();
    template <class A> void visit(A& a);   ///< Feldliste für Save-State (nur in upd765.cpp benutzt)

    Config cfg_;
    FloppyDriveV2* drive_[kUnits] = {nullptr, nullptr, nullptr, nullptr};

    // Phasen
    Phase phase_ = Phase::Idle;
    Kind  kind_  = Kind::None;
    Stage stage_ = Stage::Latency;
    bool reset_  = false;
    bool nd_     = false;           ///< SPECIFY ND (1 = Nicht-DMA)
    bool drq_ = false, irq_ = false, tc_ = false, tc_latch_ = false;
    bool byte_ready_ = false;       ///< Byte wartet (DMA: DRQ, Nicht-DMA: RQM)
    int64_t delay_ = -1;            ///< Takte bis zum nächsten Ereignis (-1 = wartet auf Host)

    // Befehl
    uint8_t cmd_[9] = {0};
    int cmd_len_ = 0, cmd_pos_ = 0;

    // Ausführung
    int     unit_ = 0;
    uint8_t head_ = 0;              ///< HD-Bit (Kopf an der Mechanik)
    bool    mfm_ = false, mt_ = false;
    uint8_t c_ = 0, h_ = 0, r_ = 0, n_ = 0, eot_ = 0, stp_ = 1;
    uint8_t st0_ = 0, st1_ = 0, st2_ = 0;
    std::vector<uint8_t> buf_;      ///< Sektorpuffer (Lesen: Quelle, Schreiben: Ziel)
    size_t pos_ = 0, size_ = 0;
    size_t cur_index_ = 0;          ///< Laufende Nummer des Sektors in der Spur
    bool   cur_deleted_ = false, cur_data_crc_bad_ = false;
    bool   scan_equal_ = true;
    // FORMAT TRACK
    uint8_t fmt_sc_ = 0, fmt_gpl_ = 0, fmt_fill_ = 0;
    std::vector<uint8_t> fmt_ids_;  ///< C,H,R,N je Sektor
    // Ergebnis
    std::vector<uint8_t> result_;
    size_t res_pos_ = 0;
    bool   res_irq_ = false;        ///< INT löscht sich beim ersten Ergebnisbyte

    // je Laufwerk
    uint8_t pcn_[kUnits] = {0, 0, 0, 0};
    int64_t seek_left_[kUnits] = {-1, -1, -1, -1};
    uint8_t seek_target_[kUnits] = {0, 0, 0, 0};
    bool    seek_recal_[kUnits] = {false, false, false, false};
    bool    int_pending_[kUnits] = {false, false, false, false};
    uint8_t int_st0_[kUnits] = {0, 0, 0, 0};
    uint32_t rid_pos_[kUnits] = {0, 0, 0, 0};
};

/**
 * @file z80_dma.h
 * @brief Z80-DMA (UA858 / Zilog Z8410) – Primitiv für den PC 1715W (doc/design/21_pc1715.md AP-W2a)
 *
 * Ein Kanal, zwei Ports (A/B), je Speicher oder E/A, Adressen fest/+1/-1.  Umfang: was
 * S550, Lader, SCP-3.0-BIOS und INIT.COM programmieren (doc/pc1715/pc1715w_hardware.md §4.5) —
 * Transfer Speicher↔E/A und Speicher↔Speicher, Byte-/Continuous-/Burst-Betrieb, RDY mit
 * Polarität, Force Ready, Lesesequenz (Status/Zähler/Adressen), Interrupt bei Blockende
 * mit Vektor, Auto-Restart.  NICHT abgebildet: Suchen/Match (WR3 Maske/Vergleich werden
 * nur aufgenommen), Interrupt bei RDY, Impulssteuerbyte (nur aufgenommen), /CE-/WAIT-Multiplex.
 *
 * Busanbindung über Rückrufe — Bankregeln (Lesezyklus → Lesebank, Schreibzyklus → Schreibbank)
 * legt die Maschine dahinter.  Die Maschine ruft step() auf, solange busRequest() gilt (die CPU
 * steht dann); je Aufruf wird EIN Byte bewegt.
 *
 * Blocklänge: übertragen werden Blocklänge + 1 Bytes (Datenblatt).
 * LOAD (CF): setzt den Bytezähler auf 0 und lädt die Startadresse des QUELL-Ports in dessen
 * Adresszähler.  Die Folge LOAD, WR0 (Richtung gedreht), LOAD versorgt deshalb beide Ports.
 */
#pragma once
#include "../bus/k1520_bus.h"
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

class Z80Dma : public BusDevice, public InterruptSlave {
public:
    explicit Z80Dma(const std::string& name = "DMA");

    // ─── Rückrufe zum Bus (von der Maschine zu setzen) ──────────────────────
    std::function<uint8_t(uint16_t)>          memRead;   ///< Speicherlesezyklus
    std::function<void(uint16_t, uint8_t)>    memWrite;  ///< Speicherschreibzyklus
    std::function<uint8_t(uint16_t)>          portRead;  ///< E/A-Lesezyklus (Adresse = Port-Adresse, unten 8 Bit)
    std::function<void(uint16_t, uint8_t)>    portWrite; ///< E/A-Schreibzyklus
    /// Ende-Ausgang: Pegel „Blockende erreicht“ (true bei Blockende, false bei LOAD/Reset/Continue/
    /// A3/87).  Die Maschine verknüpft ihn mit KRFD Bit 7 zu TC des U8272 [?].
    std::function<void(bool)>                 blockEnde;

    // ─── BusDevice: Steuerport (Schreiben WR0–WR6, Lesen = Lesesequenz) ────────
    uint8_t ioRead(uint8_t port) override;
    void    ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return name_.c_str(); }

    // ─── Ausführung ─────────────────────────────────────────────────────────
    /// RDY-Eingang (Pegel am Pin; die Polarität kommt aus WR5 Bit 3).
    void setReady(bool pegel);
    /// true, wenn die DMA den Bus fordert (CPU hält an): freigegeben, Block nicht zu Ende und
    /// bereit — im Continuous-Betrieb auch nach Fallen von RDY, sobald begonnen.
    bool busRequest() const;
    /// Bewegt ein Byte, wenn freigegeben und bereit.  Rückgabe: Takte (Lese- + Schreibzyklus,
    /// Standard 4 + 4), 0 = nichts bewegt.
    int  step();
    /// Hardware-Reset (/RESET): wie C3.
    void reset();

    // ─── Interrupt (Daisy-Chain wie Z80CTC) ─────────────────────────────────
    void    setIEI(bool iei) override { iei_ = iei; }
    bool    getIEO() const override { return iei_ && !s_.intPending && !s_.ius; }
    bool    hasInterrupt() const override { return iei_ && s_.intPending && !s_.ius; }
    uint8_t getVector() const override;
    void    onRETI() override;
    const char* intDeviceName() const override { return "Z80-DMA"; }

    // ─── Abfragen (Tests/Debugger) ──────────────────────────────────────────
    bool     enabled() const { return s_.enabled; }
    bool     blockEndeErreicht() const { return s_.endLevel; }
    uint8_t  status() const { return s_.status; }
    uint16_t adresseA() const { return s_.aCur; }
    uint16_t adresseB() const { return s_.bCur; }
    uint16_t bytezaehler() const { return static_cast<uint16_t>(s_.counter); }

    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    enum Folge : uint8_t {
        F_NONE, F_A_LO, F_A_HI, F_LEN_LO, F_LEN_HI, F_B_LO, F_B_HI, F_INTCTL, F_PULSE, F_VECTOR,
        F_TIMING_A, F_TIMING_B, F_MASK3, F_MATCH3, F_READMASK
    };
    struct S {                       // trivial kopierbar → Savestate = Rohkopie
        uint16_t aStart = 0, bStart = 0, aCur = 0, bCur = 0;
        uint16_t blockLen = 0;
        int32_t  counter = 0;        ///< bisher übertragene Bytes
        uint8_t  wr0 = 0, wr1 = 0, wr2 = 0, wr4 = 0, wr5 = 0, intCtl = 0, vector = 0;
        uint8_t  timingA = 4, timingB = 4;   ///< Zyklen je Zugriff
        uint8_t  readMask = 0x7F, readPos = 0, status = 0x38;
        uint8_t  folge[8] = {0}; uint8_t folgeN = 0, folgeI = 0;
        bool     enabled = false, started = false, forceReady = false, readyPin = false;
        bool     intEnable = false, intPending = false, ius = false, enableAfterReti = false;
        bool     endLevel = false;
    } s_;
    bool        iei_ = false;
    std::string name_;

    void  schreibeWR(uint8_t d);
    void  befehl(uint8_t d);
    void  nimmFolge(uint8_t d);
    void  queue(std::initializer_list<uint8_t> f);
    bool  ready() const;
    bool  quelleIstA() const { return s_.wr0 & 0x04; }
    void  load();
    void  blockEndeBehandeln();
    void  setEndLevel(bool l);
    bool  modeCont() const { return ((s_.wr4 >> 5) & 3) == 1; }
    uint16_t schritt(uint16_t a, uint8_t wr) const;
};

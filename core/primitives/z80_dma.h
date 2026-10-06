/**
 * @file z80_dma.h
 * @brief Z80-DMA (UA858 / Zilog Z8410) – vollständiges Primitiv (PC 1715W AP-W2a, P8000 AP P9c)
 *
 * Ein Kanal, zwei Ports (A/B), je Speicher oder E/A, Adressen fest/+1/-1.  Nachgebildet ist der
 * ganze Baustein laut Datenblatt (Transkription + Abdeckungsliste: doc/p8000/z80_dma.md):
 * WR0–WR6 mit allen Folgeregistern, RR0–RR6 mit Lesemaske, Transfer / Search / Search+Transfer
 * mit Maske und Vergleichsbyte, Stopp bei Treffer, Byte-/Burst-/Continuous-Betrieb, RDY mit
 * Polarität, Force Ready, Auto-Restart, alle WR6-Befehle, Interrupt bei RDY/Treffer/Blockende
 * mit statusabhängigem Vektor, Impulsausgang, IEI/IEO/RETI, Zykluslängen und /WAIT.
 *
 * Busanbindung über Rückrufe — Bankregeln (Lesezyklus → Lesebank, Schreibzyklus → Schreibbank)
 * legt die Maschine dahinter.  Die Maschine ruft step() auf, solange busRequest() gilt (die CPU
 * steht dann); je Aufruf wird EIN Byte bewegt.  Nach jedem CPU-Befehl meldet sie cpuZyklus() —
 * im Byte-Betrieb gibt die DMA den Bus nach jedem Byte ab und fordert ihn erst nach einem
 * M1-Zyklus der CPU wieder an (Datenblatt).
 *
 * Blocklänge: übertragen werden Blocklänge + 1 Bytes, Blocklänge 0 ⇒ 65537 Bytes (Datenblatt).
 * LOAD (CF): Bytezähler 0, Quell-Port-Startadresse in dessen Adresszähler, die Zielport-Adresse
 * nur, wenn der Zielport NICHT fest ist (Zilog: „only loads a fixed address to a port selected
 * as the source“) — daher die Folge LOAD, WR0 (Richtung gedreht), LOAD.
 * Jedes Steuerbyte (Basisbyte) sperrt die DMA, ausser 87H und WR3 mit D6 = 1 (Datenblatt).
 *
 * Abweichungen der VORGABE vom Datenblatt (bewusst, weil Gastsysteme darauf laufen; je eine
 * Schaltfläche stellt das Datenblattverhalten her):
 *  - Bytezähler nach Blockende liest N + 1 statt N (setZaehlerLiestBlocklaenge; P8000 an).
 *  - Standardzyklus 4 Takte je Zugriff statt 3 (Speicher) / 4 (E/A) (setStandardZyklen).
 *  - Adresszähler zählen nach jedem Byte weiter: Ziel = As ± (N+1) statt As ± N nach Tabelle 1
 *    der UA858-Beschreibung (S550 0559H setzt mit der Zieladresse fort — offen, ob die Tabelle
 *    stimmt; keine Schaltfläche).
 *
 * Annahmen bei Datenblattlücken (Abdeckungsliste doc/p8000/z80_dma.md §9):
 *  - [A1] WR4-Betriebsart 11 („nicht programmieren“) wirkt wie Burst.
 *  - [A2] Zeitsteuerbyte-Länge 11 („nicht benutzen“) wirkt wie 2 Takte; WR1/WR2 ohne D6 lassen
 *         ein früher geladenes Zeitsteuerbyte stehen (nur C3/C7/CB stellen Standard her).
 *  - [A3] BF setzt die Lesemaske auf „nur Status“ (wie MAME); Lesemaske 0 liefert den Status.
 *  - [A4] Unbekannte WR6-Codes und undefinierte Basisbytes (1xxxx110) wirken nur als
 *         Schreibzugriff (sperren die DMA), sonst nichts.
 *  - [A5] AF blendet einen anstehenden Interrupt aus (IP bleibt, /INT inaktiv); AB zeigt ihn
 *         wieder.  Eine Interruptbedingung bei gesperrten Interrupts wird verworfen.
 *  - [A6] Interrupt bei RDY: IP, sobald freigegeben ∧ RDY aktiv ∧ kein IP/IUS; die Busanforderung
 *         bleibt aus, bis B7 + RETI sie freigeben (Zilog/UA858: ISR gibt B7, 87, RETI).  Nach
 *         jeder Busfreigabe gilt wieder „erst Interrupt“.
 *  - [A7] Impuls: geprüft VOR jedem Byte mit dem Zählerstand (= bisher bewegte Bytes);
 *         Rückruf `impuls` + Zähler impulse() (das kurze /INT-Low erreicht die CPU nie).
 *  - [A8] Treffer und Blockende im selben Byte: Vektorcode 11; Search Burst/Continuous mit
 *         Stopp bei Treffer liest genau ein Byte nach (Tabelle 2), ohne es zu vergleichen.
 *  - [A9] /WAIT: Rückruf `warteTakte` liefert je Zugriff die eingeschobenen TW; wirksam nur mit
 *         WR5 D4 und bei Standardzyklus oder variabel ≥ 3 (Speicher) bzw. = 4 (E/A).
 *  - [A10] Auto-Restart: Status D5 bleibt 0 (Blockende erreicht), der Ende-Ausgang bleibt
 *         gesetzt, Force Ready ist zurück (Blockende) — der nächste Block wartet auf RDY.
 *  - [A11] RETI wirkt nur bei IEI = high (der Baustein, dessen ISR endet).
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
    /// A3/87).  Kein Pin des Bausteins — die Maschine leitet TC des U8272 daraus ab.
    std::function<void(bool)>                 blockEnde;
    /// /INT/PULSE als Impulsausgang (Interrupt-Steuerbyte D2) [A7]; optional.
    std::function<void()>                     impuls;
    /// /CE/WAIT als /WAIT (WR5 D4): eingeschobene Wartetakte je Zugriff [A9]; optional.
    std::function<int(bool ea, uint16_t adresse, bool schreiben)> warteTakte;

    // ─── BusDevice: Steuerport (Schreiben WR0–WR6, Lesen = Lesesequenz) ────────
    uint8_t ioRead(uint8_t port) override;
    void    ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return name_.c_str(); }

    // ─── Ausführung ─────────────────────────────────────────────────────────
    /// RDY-Eingang (Pegel am Pin; die Polarität kommt aus WR5 Bit 3).
    void setReady(bool pegel);
    /// true, wenn die DMA den Bus fordert (CPU hält an).  Ohne Seiteneffekt.
    bool busRequest() const;
    /// Bewegt ein Byte, wenn busRequest() und RDY (bzw. Force Ready) gelten.  Rückgabe: Takte
    /// (Lese- + Schreibzyklus, Vorgabe 4 + 4), 0 = nichts bewegt (Continuous hält den Bus).
    int  step();
    /// Die CPU hat einen Befehl (M1) ausgeführt: im Byte-Betrieb darf die DMA den Bus wieder fordern.
    void cpuZyklus();
    /// Hardware-Reset (Einschalten / Systemreset): wie C3, dazu Lesemaske 7FH, Lesesequenz auf
    /// RR0, WR5 = 0, Betriebsart Byte.
    void reset();
    /// Lesesequenz: nach Blockende meldet der Bytezähler die programmierte Blocklänge N statt
    /// N + 1 (Datenblatt-Tabelle 1; der MON8-Hardwaretest des P8000 prüft das: Zähler = 0100H
    /// nach Blocklänge 0100H).  Vorgabe aus ⇒ das bisherige Verhalten bleibt bitgleich.
    void setZaehlerLiestBlocklaenge(bool an) { zaehler_blocklaenge_ = an; }
    /// Standardzyklus nach Datenblatt: Speicher 3, E/A 4 Takte (statt 4 / 4).  Vorgabe aus.
    void setStandardZyklen(bool an) { standard_zyklen_ = an; }

    // ─── Interrupt (Daisy-Chain wie Z80CTC) ─────────────────────────────────
    void    setIEI(bool iei) override { iei_ = iei; }
    bool    getIEO() const override { return iei_ && !s_.ius && !intLeitung(); }
    bool    hasInterrupt() const override { return iei_ && intLeitung() && !s_.ius; }
    uint8_t getVector() const override;            ///< Quittung: IP → IUS, liefert den Vektor
    void    onRETI() override;
    const char* intDeviceName() const override { return "Z80-DMA"; }

    // ─── Abfragen (Tests/Debugger, alle ohne Seiteneffekt) ─────────────────
    bool     enabled() const { return s_.enabled; }
    bool     blockEndeErreicht() const { return s_.endLevel; }
    /// Statusbyte (RR0): D0 = 1 Transfer erfolgt, **D1 = 0 RDY aktiv**, D3 = 0 Interrupt
    /// anstehend, D4 = 0 Treffer, D5 = 0 Blockende.  D1/D3 werden beim Lesen aus dem Zustand
    /// gebildet (S550 0539H wertet D1 aus: „RDY noch aktiv" → FDC-Grundzustand).
    uint8_t  status() const;
    uint16_t adresseA() const { return s_.aCur; }
    uint16_t adresseB() const { return s_.bCur; }
    uint16_t bytezaehler() const { return static_cast<uint16_t>(s_.counter); }
    /// Vektor, den eine Quittung jetzt liefern würde (ohne Quittung).
    uint8_t  vektor() const;
    uint32_t impulse() const { return x_.impulse; }

    /// Vollständiger Zustand für den Debugger (P12a) — eine Kopie, ohne Seiteneffekt.
    struct Sicht {
        uint8_t  wr0, wr1, wr2, wr3, wr4, wr5;           ///< Basisregister (wie geschrieben)
        uint8_t  zeitA, zeitB;                           ///< Zeitsteuerbytes (roh)
        bool     variabelA, variabelB;                   ///< Zeitsteuerbyte wirksam (sonst Standard)
        uint8_t  zyklusA, zyklusB;                       ///< wirksame Zykluslänge je Zugriff (Takte)
        uint8_t  maske, vergleich;                       ///< WR3-Folgebytes
        uint8_t  intSteuer, impulsSteuer, vektorRoh;     ///< WR4-Folgebytes
        uint16_t startA, startB, blocklaenge;            ///< Pufferregister
        uint16_t adresseA, adresseB;                     ///< Adresszähler
        int32_t  bytesBewegt;                            ///< bisher bewegte Bytes des Blocks
        uint16_t zaehlerGelesen;                         ///< was RR1/RR2 jetzt liefern
        uint8_t  status;                                 ///< RR0
        uint8_t  lesemaske, lesePos;                     ///< RR-Maske, nächstes Register 0–6
        uint8_t  folgeOffen;                             ///< noch erwartete Folgebytes
        uint8_t  betriebsart;                            ///< 0 Byte, 1 Continuous, 2 Burst, 3 [A1]
        uint8_t  transferart;                            ///< WR0 D1D0: 1 Transfer, 2 Search, 3 beides
        bool     aQuelle;                                ///< WR0 D2
        bool     freigegeben, begonnen, forceReady, rdyPin, rdyAktiv;
        bool     intFreigabe, intAnstehend, ius, warteAufReti, rdyFreigabe, byteFreigabe;
        bool     endePegel, autoRestart, waitFunktion, stoppBeiTreffer, iei;
        uint8_t  intGrund;                               ///< V2V1 des anstehenden Interrupts
        uint32_t impulse;
        bool     busAnforderung;                         ///< = busRequest()
    };
    Sicht sicht() const;

    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    enum Folge : uint8_t {
        F_NONE, F_A_LO, F_A_HI, F_LEN_LO, F_LEN_HI, F_B_LO, F_B_HI, F_INTCTL, F_PULSE, F_VECTOR,
        F_TIMING_A, F_TIMING_B, F_MASK3, F_MATCH3, F_READMASK
    };
    struct S {                       // trivial kopierbar → Savestate = Rohkopie (Aufbau seit AP-W2a)
        uint16_t aStart = 0, bStart = 0, aCur = 0, bCur = 0;
        uint16_t blockLen = 0;
        int32_t  counter = 0;        ///< bisher übertragene Bytes
        uint8_t  wr0 = 0, wr1 = 0, wr2 = 0, wr4 = 0, wr5 = 0, intCtl = 0, vector = 0;
        uint8_t  timingA = 4, timingB = 4;   ///< Zyklen je Zugriff (bei variablem Zeitverhalten)
        uint8_t  readMask = 0x7F, readPos = 0, status = 0x38;
        uint8_t  folge[8] = {0}; uint8_t folgeN = 0, folgeI = 0;
        bool     enabled = false, started = false, forceReady = false, readyPin = false;
        bool     intEnable = false, intPending = false, ius = false;
        bool     enableAfterReti = false;    ///< B7: keine Busanforderung bis RETI
        bool     endLevel = false;
    } s_;
    struct X {                       // Erweiterung P9c — eigener Block im Savestate („DM2“)
        uint8_t  wr3 = 0, maske = 0, vergleich = 0, impulsCtl = 0;
        uint8_t  zeitA = 0, zeitB = 0;
        bool     variabelA = false, variabelB = false;
        bool     rdyFreigabe = false;    ///< Interrupt bei RDY bedient (B7 + RETI) → Bus frei
        bool     byteFreigabe = false;   ///< Byte-Betrieb: Bus abgegeben, wartet auf cpuZyklus()
        bool     nachlauf = false;       ///< Search Burst/Continuous: ein Byte nach dem Treffer
        uint8_t  intGrund = 0;           ///< V2V1
        uint32_t impulse = 0;
    } x_;
    bool        iei_ = false;
    bool        zaehler_blocklaenge_ = false;   ///< Konfiguration (nicht im Save-State)
    bool        standard_zyklen_ = false;       ///< Konfiguration (nicht im Save-State)
    std::string name_;

    void  schreibeWR(uint8_t d);
    void  befehl(uint8_t d);
    void  nimmFolge(uint8_t d);
    void  queue(std::initializer_list<uint8_t> f);
    void  befehlReset();
    bool  ready() const;
    bool  pinAktiv() const { return s_.readyPin == ((s_.wr5 & 0x08) != 0); }
    bool  quelleIstA() const { return s_.wr0 & 0x04; }
    bool  intLeitung() const { return s_.intPending && s_.intEnable; }
    bool  rdyInterruptGewaehlt() const { return s_.intEnable && (s_.intCtl & 0x40); }
    void  pruefeRdyInterrupt();
    void  load();
    int32_t zaehlerLesen() const;
    int32_t blockEndeBei() const { return s_.blockLen == 0 ? 0x10000 : s_.blockLen; }
    void  blockEndeBehandeln(bool treffer);
    void  interrupt(uint8_t grund);
    void  busFreigabe();
    void  setEndLevel(bool l);
    uint8_t modus() const { return (s_.wr4 >> 5) & 3; }
    bool  modeCont() const { return modus() == 1; }
    bool  modeByte() const { return modus() == 0; }
    uint16_t schritt(uint16_t a, uint8_t wr) const;
    int   zyklus(bool portA) const;
    int   warte(bool portA, bool ea, uint16_t adr, bool schreiben) const;
};

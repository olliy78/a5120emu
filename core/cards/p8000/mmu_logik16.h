/**
 * @file mmu_logik16.h
 * @brief P8000 16-Bit-Karte: MMU-Steuerlogik — drei UB8010 (Code/Data/Stack) samt Auswahl
 *        MCODE−/MDATA−/MSTACK−, NBR-Vergleich, On-Board-Fenster, Adressbildung MMU ein/aus,
 *        SUP-Weg, Trap-Register TRPL/IF1L und die Register der „speziellen Logik" FFC1–FFF9.
 *
 * Quelle (Rang 1): doc/p8000/schaltplan_16bit.md §1–§4 (Index 4, Bl. 2, 3, 5, 8, 9); Handbuch
 * doc/p8000/hw_16bit.md §2.6, §5; Gegenprobe WEGA `mch.s`, MON16 `p.test.s` (Testschritte 84–94).
 * AP P9b (doc/design/25_p8000.md §10.11).  Abdeckungsliste: doc/p8000/mmu_logik_abdeckung.md.
 *
 * Kennt keinen Speicher und keine CPU: die Karte (P10) reicht jeden Buszyklus des U8001 herein
 * und bekommt das ZIEL zurück (On-Board-EPROM/-SRAM/leer oder Hauptspeicher mit 24-Bit-
 * Busadresse, unterdrückt ja/nein, Wartetakte).  /SEGT geht über `onSegt` an die CPU.
 *
 * @code
 *   Auswahl (nur Speicherzyklen, nicht On-Board) — Gatter Bl. 5, wörtlich nachgebildet:
 *     System-Mode                                   → Code-MMU  (Befehl, Daten, Stack)
 *     Normal, SCR.2 = 1 (SEG USR), SN6 = 0          → Data-MMU
 *     Normal, SEG USR, SN6 = 1                      → Stack-MMU
 *     Normal, SEG USR = 0, Status 11xx (Holen)      → Code-MMU
 *     Normal, SEG USR = 0, Status 10xx, Adr-High < NBR → Data-MMU
 *     dto., Adr-High ≥ NBR                          → Stack-MMU   (Status 1001 zählt NICHT)
 *   Die gewählte MMU sieht N/S = L, die beiden anderen N/S = H (Mode %D0–%D2: MST, NMS = 0).
 *   Data-/Stack-MMU haben SN6 fest L.  /CS bei Spezial-E/A: AD1 = Code, AD2 = Data, AD3 = Stack
 *   (low-aktiv, mehrere zugleich).  MMU aus (SCR.1 = 0): Busadresse = SN·64K + Offset, A23 = 0.
 *   On-Board: SCR.0 = 0 ∧ SN = 0 ∧ Offset < 8000H — unabhängig von MMU ON und CPU-Modus;
 *   0000–3FFF EPROM, 4000–5FFF SRAM (2 KB, 4 Spiegel), 6000–7FFF leer; +1 Wartetakt.
 *
 *   E/A (Standard, ungerade):  FFC1 SCR r/w (Bit 4–7 lesen 1) · FFC9 SBR (kein Speicher) ·
 *   FFD1 NBR r/w · FFD9 LEDEIN (Idx 4) bzw. SNVR (Idx 1) · FFE1 RETI w · FFE9 SOFTRESET w ·
 *   FFF1 TRPL r · FFF9 IF1L r · FFB9 LEDAUS (Idx 4, anderer Dekoder, hier mitgeführt).
 * @endcode
 *
 * Benannte Annahmen (Lücken der Quellen; Messfragen in schaltplan_16bit.md §11):
 *  [L1] Gleichheit Adresse-High = NBR → Stack-MMU (Plan: 74S85 A>B, Kaskadenrichtung nur
 *       abgeleitet).  **WEGA rechnet umgekehrt** (`nsseg`/`getmem`: `cpb rh7,NBR; jr ugt` ⇒
 *       Gleichheit = Data) — umschaltbar über `Config::nbrGleichheitStack`.
 *  [L2] Flipflop 4D16 (/OE des NBR-Latch): Q folgt dem LAUFENDEN Zyklus (Q = SN1–SN5 alle 0);
 *       die asynchronen Eingänge (/R = Zugriff FFD1, /S = Zugriff FFC9) wirken nur während des
 *       E/A-Zyklus selbst und hinterlassen keinen Zustand.  Bei Q = 1 (Segment 0/1/64/65)
 *       schweben die Vergleichereingänge ⇒ wirksames NBR = FFH.
 *  [L3] FFC9 (SELSYSBRKREG−) speichert nichts; Lesen liefert den offenen Bus (`leer8`).  Gestützt
 *       von MON16 `p.test.s`: Testschritt 84 (SBR-Rücklesen) läuft nur auf Index-0-Hardware.
 *  [L4] SCR lesend: Bit 4–7 = 1 (Index 1 laut Handbuch fest High; Index 4 ohne Angabe).
 *  [L5] /CS während /RESET aktiv ⇒ nach MRESET MSEN = 1, TRNS = 0 in allen drei MMUs (Handbuch
 *       §2.2: „Master-Enable-Flag in den MMUs nicht auf Null"; [TB] §7.2.1).  `Config::csBeimReset`.
 *  [L6] IF1L-Stufe 1 (3D20) übernimmt LAD0–7 am ENDE jedes Zyklus mit Status 1101 (Handbuch:
 *       „erstes Befehlswort"; Plan: Y4 oder Y5 unsicher → `Config::if1lAuchStatus1100`).  Stufe 2
 *       (1D20) und TRPL takten mit der Flanke der SAMMELleitung /SEGT (inaktiv → aktiv) mitten im
 *       verletzenden Zyklus ⇒ verletzt das Holen selbst, steht in IF1L noch der Vorgänger (wie
 *       ISN/IOFF der Z8010).  Eine zweite MMU, die bei schon gezogener Leitung zieht, taktet nicht.
 *  [L7] Unbeschaltetes TRAD (MMU ON, keine MMU treibt) liest sich als 1 (TTL offen) ⇒ A8–A23 =
 *       FFFFH.  Treiben mehrere MMUs (TRNS = 0 oder MST = 0), gilt verdrahtetes UND; ebenso beim
 *       Lesen mehrerer per /CS gewählter MMUs und bei gleichen IDs in der Trap-Quittung.
 *  [L8] /SUP sperrt SYSDS (Bl. 9) für JEDEN Hauptspeicherzyklus — Schreiben wirkt nicht, Lesen
 *       liefert den offenen Bus —, auch bei MMU aus; der On-Board-Speicher hängt nicht an SYSDS.
 *  [L9] Die Register FFC1–FFF9 sind an A1/A2 nicht dekodiert (D13 ohne LAD1/2, 3D24 nur LAD3–5)
 *       ⇒ jedes erscheint 4-fach (FFC1/3/5/7 …); ebenso LEDAUS FFB9–FFBF.
 *  [L10] Schreib-Strobes (LEDEIN, LEDAUS, SOFTRESET, RETI) wirken nur beim Schreiben; Lesen aller
 *       nur schreibbaren Adressen und von SNVR (Index 1, Bl. 1 fehlt) liefert `leer8`.
 *  [L11] Fremde Busmaster (X9, DMASYNC = ZBUSACK−) laufen nicht durch diese Logik — die 16-Bit-
 *       Seite hat im Emulator keinen; Z8010-DMA-Zyklen bleiben im Primitiv getestet.
 *  [L12] RETI: die Peripherie erkennt ED 4D selbst; hier gemeldet als `onReti` beim Byte 4DH
 *       unmittelbar nach EDH an FFE1.
 *
 * Save-State: serialize()/deserialize(), Version 1 (Register, Trap-Latches, LED, /SEGT-Pegel,
 * 3 × Z8010 v1).  Zähler und „letzter Zugriff" sind Beobachtung, nicht Zustand.
 *
 * @license MIT
 */
#pragma once
#include "core/primitives/z8000.h"
#include "core/primitives/z8010.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

class P8000MmuLogik16 {
public:
    struct Config {
        enum class Index { I1, I4 };          ///< Leiterplattenindex der 16-Bit-Karte
        Index    index = Index::I4;
        bool     nbrGleichheitStack = true;   ///< [L1]
        bool     csBeimReset = true;          ///< [L5]
        bool     if1lAuchStatus1100 = false;  ///< [L6]
        uint8_t  nbrStart = 0x00;             ///< DS8282 nach Netz-Ein (real unbestimmt)
        uint8_t  leer8 = 0xFF;                ///< offener Bus (Byte)
        uint16_t leer16 = 0xFFFF;             ///< offener Bus (AD0–15)
    };

    // ── E/A-Adressen (kanonisch; Spiegel [L9]) ──────────────────────────────
    static constexpr uint16_t P_SCR = 0xFFC1, P_SBR = 0xFFC9, P_NBR = 0xFFD1, P_FFD9 = 0xFFD9,
                              P_RETI = 0xFFE1, P_SOFTRESET = 0xFFE9, P_TRPL = 0xFFF1,
                              P_IF1L = 0xFFF9, P_LEDAUS = 0xFFB9;
    // ── SCR-Bits (Handbuch Tab. 3.7-5) ──────────────────────────────────────
    static constexpr uint8_t SCR_BDMEM_AUS = 0x01;  ///< BD MEM ON−: 1 = On-Board aus
    static constexpr uint8_t SCR_MMU_ON    = 0x02;  ///< MMU ONH: Adresstreiber auf TRAD
    static constexpr uint8_t SCR_SEG_USR   = 0x04;  ///< segmentierte Anwender
    static constexpr uint8_t SCR_PARITAET  = 0x08;  ///< CLR PARITY−: 1 = Paritätsfehler annehmen

    enum class Mmu : uint8_t { Code = 0, Data = 1, Stack = 2 };
    /// Welche MMU im Zyklus N/S = L sah.
    enum class Wahl : uint8_t { Keine, Code, Data, Stack };
    enum class Ziel : uint8_t { Keins, OnBoardEprom, OnBoardSram, OnBoardLeer, Hauptspeicher };
    static const char* wahlName(Wahl w);
    static const char* zielName(Ziel z);

    /// Was ein Buszyklus bewirkt — die Karte führt den Zugriff damit aus.
    struct Zugriff {
        Ziel     ziel = Ziel::Keins;
        /// Hauptspeicher: Busadresse A0–A23.  On-Board: Byteadresse im Baustein
        /// (EPROM 0000–3FFF, SRAM 000–7FF, leer: Offset).
        uint32_t adresse = 0;
        bool     unterdrueckt = false;  ///< SYSDS gesperrt [L8] (nur Hauptspeicher)
        uint8_t  wartetakte = 0;        ///< T2WAIT (On-Board)
        Wahl     wahl = Wahl::Keine;
        bool     mmuAdresse = false;    ///< A8–A23 vom TRAD-Bus (MMU ON) statt lokal
        uint8_t  treiber = 0;           ///< Bit i: MMU i trieb A8–A23
        uint8_t  sup = 0;               ///< Bit i: MMU i zog /SUP
        bool     konflikt = false;      ///< mehrere Treiber mit verschiedener Adresse [L7]
    };

    P8000MmuLogik16();
    explicit P8000MmuLogik16(const Config& cfg);

    /// Pegel der Sammelleitung /SEGT (true = aktiv) → U8001 setSEGT.
    std::function<void(bool aktiv)> onSegt;
    /// SOFTRESET (FFE9 schreiben): die Karte bildet daraus MRESET− (und ruft reset()).
    std::function<void()> onSoftreset;
    /// RETI-Folge ED 4D an FFE1 [L12].
    std::function<void()> onReti;

    // ── Bus ─────────────────────────────────────────────────────────────────
    /// Jeder CPU-Buszyklus.  Nur Status 8–D erreicht ein Ziel; die übrigen ändern nichts.
    Zugriff zyklus(const Z8kBusCycle& c);
    /// Segmenttrap-Quittung (Status 0100): Kennwort AD0–15 [L7]; nimmt /SEGT zurück.
    uint16_t segtQuittung();
    /// Spezial-E/A (Status 0011) an die per /CS (AD1/2/3 = L) gewählten MMUs; Befehl = AD8–15.
    uint16_t spezialLesen(uint16_t port);
    void     spezialSchreiben(uint16_t port, uint16_t ad);
    /// Bitmaske der durch /CS gewählten MMUs (Bit 0 Code, 1 Data, 2 Stack).
    static uint8_t csMaske(uint16_t port) { return uint8_t((~port >> 1) & 7); }

    /// Standard-E/A dieses Blocks (Byte an ungerader Adresse = AD0–7).
    bool    istEigenerPort(uint16_t port) const;
    uint8_t ioLesen(uint16_t port);
    void    ioSchreiben(uint16_t port, uint8_t daten);

    // ── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn();
    /// MRESET−: SCR := 0, RUN-LED an (Idx 4), Reset der drei UB8010; NBR/TRPL/IF1L bleiben.
    void reset();

    // ── Reine Gatterlogik (Bl. 5), ohne Zustand ─────────────────────────────
    /// @p datenSeite = Ausgang „A>B" des NBR-Vergleichers; @p onBoard = IMEML aktiv.
    static Wahl auswahl(uint8_t status, bool system, bool segUser, bool sn6, bool datenSeite,
                        bool onBoard);
    /// On-Board-Fenster W ∧ BDMEMON ∧ Speicherzyklus.
    static bool onBoardFenster(uint8_t status, uint8_t scr, uint8_t seg, uint16_t offset);

    // ── Beobachtung (Debugger P12b, Tests) ──────────────────────────────────
    struct Probe {
        Zugriff         zugriff;
        Z8010::Probe    mmu[3];
        bool            nsHigh[3] = {true, true, true};   ///< N/S-Eingang je MMU
        uint8_t         nbrWirksam = 0;                   ///< nach [L2]
    };
    /// Was zyklus() für @p c ergäbe — ohne Zustand, Flags, REF/CHG, SEGT, Latches zu berühren.
    Probe probe(const Z8kBusCycle& c) const;

    struct SegtEreignis {
        Z8kBusCycle zyklus;
        uint8_t  mmus = 0;          ///< Bit i: MMU i zog in diesem Zyklus /SEGT
        uint8_t  trpl = 0, if1l = 0;
        Wahl     wahl = Wahl::Keine;
        uint64_t nummer = 0;        ///< laufende Nummer der Flanke (1 …)
    };
    std::function<void(const SegtEreignis&)> onSegtEreignis;

    struct Sicht {
        uint8_t  scr, nbr, trpl, if1l, if1lStufe1;
        bool     runLed, segt;
        Z8kBusCycle letzterZyklus;
        Zugriff  letzterZugriff;
        SegtEreignis letztesSegt;
        uint64_t zyklen[4];          ///< je Wahl (Keine/Code/Data/Stack), nur Speicherzyklen
        uint64_t onBoard, unterdrueckt, konflikte, segtFlanken;
    };
    Sicht sicht() const;

    uint8_t scr() const { return scr_; }
    uint8_t nbr() const { return nbr_; }
    uint8_t trpl() const { return trpl_; }
    uint8_t if1l() const { return if1l_; }
    bool    runLed() const { return led_; }
    bool    segt() const { return segtPegel_; }
    const Z8010& mmu(Mmu m) const { return mmu_[size_t(m)]; }
    Z8010&       mmu(Mmu m) { return mmu_[size_t(m)]; }
    const Config& config() const { return cfg_; }

    // ── Save-State ──────────────────────────────────────────────────────────
    void serialize(std::vector<uint8_t>& out) const;
    /// Stellt auch den /SEGT-Pegel über `onSegt` wieder her.
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    Config cfg_;
    std::array<Z8010, 3> mmu_;
    uint8_t scr_ = 0, nbr_ = 0, trpl_ = 0, if1l_ = 0, if1lStufe1_ = 0, retiVorher_ = 0;
    bool    led_ = true, segtPegel_ = false;
    // Beobachtung
    Z8kBusCycle letzterZyklus_;
    Zugriff     letzterZugriff_;
    SegtEreignis letztesSegt_;
    uint64_t zyklen_[4] = {}, onBoard_ = 0, unterdrueckt_ = 0, konflikte_ = 0, segtFlanken_ = 0;

    struct Eingang { bool onBoard; Wahl wahl; uint8_t nbrWirksam; Z8010::Zyklus mz[3]; };
    Eingang eingang(const Z8kBusCycle& c) const;
    Zugriff bilde(const Z8kBusCycle& c, const Eingang& in, const Z8010::Ergebnis e[3]) const;
    void    segtNachfuehren();
    bool    istIndex4() const { return cfg_.index == Config::Index::I4; }
};

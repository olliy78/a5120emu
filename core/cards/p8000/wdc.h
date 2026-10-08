/**
 * @file wdc.h
 * @brief Winchester-Disk-Controller (WDC) des P8000: eigener Rechner mit UA880, CTC UA857,
 *        2 × 2732 Firmware, 6 KB SRAM, Hostschnittstelle (Statusleitungen, TE/TR, ARDY/ASTB-
 *        Byte-Handshake, RST) und Disk-Schnittstellensteuerung (Adresszähler, DMA Platte↔RAM,
 *        Marken-/CRC-Hardware, Index, Laufwerks-/Kopfwahl, Schritt).  Die Original-Firmware läuft
 *        echt — keine Kommando-Nachbildung.  AP P13c, doc/p8000/wdc_firmware.md (maßgeblich),
 *        doc/design/25_p8000.md §10.1/§10.2.
 *
 * @code
 *   Speicher (A15/A14 nicht dekodiert [W1])        E/A (A2–A0 nicht dekodiert [W1])
 *   0000–1FFF  EPROM (2 × 2732)                     08H DSKEA  Disk-Endadresse, Bit 2–0 = Markenzahl − 1
 *   2000–2FFF  RAM 4 KB, Sektorpuffer (Ring)        18H CNTST  ST0–2, HEN, HA12, HR/W(TR), DA12, DR/W
 *   3000–37FF  RAM 2 KB, Arbeitszellen              28H BM_T   Taktmuster der Marke (nur gespeichert)
 *   3800–3FFF  Schreiben lädt einen Adresszähler:   38H BM_D   Datenmuster der Marke (Lesen: Vergleich)
 *              A13–A10 = 1111 Disk, 1110 Host,      48H DSKC1  STEP (Bit 0, 0→1), Richtung (Bit 3), Kopf (7–4)
 *              Wert a11 a10 = A15 A14, a9–a0 = A9–A0 58H DSKC2  DEN, /MEN, CRCEN, WG, LW (5–4), S2, FR
 *              (a12 = DA12 bzw. HA12 aus CNTST)     68H IMPAUS Disk-Schnittstelle aus (Impuls)
 *                                                    78H IMPATV Anzeigeimpuls (ohne Wirkung)
 *   CTC 70H–73H: K0 ← MAERK, K1 ← /DEND, K2 ← HA0,  88H ST_PRT /READY(2) /SEEKC(3) /WF(5) /TR0(6)
 *   K3 ← IX (Index).  IM 2, Vektor aus der Firmware.          CRC gut(7); Lesen löscht den CRC-Fehler
 * @endcode
 *
 * **Zeitführung (§10.2):** eigene Uhr in WDC-Takten; die Maschine zieht den WDC befehlsweise
 * nach (`laufeBis`).  Die Platte läuft als umlaufender Bytestrom (1 Byte = 1,6 µs = 6,4 Takte bei
 * 4 MHz, `Platte::BYTES_JE_SPUR` je Umdrehung).  Vor jedem Befehl und vor jedem E/A-Zugriff wird
 * der Strom bis „jetzt" abgearbeitet (E/A-Zeitpunkt = Befehlsbeginn + 9 Takte, Zählerladen + 6).
 * Je M1-Zyklus ein Wartetakt (Am8127, `Config::wartetakte_m1`) [W2]; EI sperrt die Annahme eines
 * Interrupts für einen Befehl (Z80-Regel; die Firmware beendet ISRs mit `EI; RETI`).
 *
 * **Disk-Schnittstelle — Verhaltensmodell aus der Benutzung durch die Firmware** (Gatterebene der
 * Pläne SP1/3, SP1/6, SP3/7 nicht ausgewertet; jede Regel ist an Firmwarestellen belegt, s.
 * wdc_firmware.md §9a):
 *  - [W3] Die Schnittstelle läuft beim Lesen, solange DEN = 1 oder /MEN = 0, beim Schreiben nur mit
 *         DEN = 1 (`isr_m2` schaltet erst auf Schreiben und dann DEN ein; liefe der Zähler schon
 *         mit /MEN = 0, käme die Datenmarke 8 Byte zu früh).  Lesen (DR/W = 0): bis zur
 *         Synchronisation wird nichts abgelegt; mit /MEN = 0 zählt die Hardware aufeinanderfolgende
 *         Marken (Wert = BM_D), bei (DSKEA & 7) + 1 Marken ist sie synchron und gibt MAERK (CTC K0);
 *         die letzte Marke belegt die Zähleradresse, wird aber NICHT ins RAM geschrieben (sonst
 *         überschriebe die Datenmarke des nächsten Sektors Byte 510 des vorigen — die Firmware rettet
 *         nur Byte 511, `sav_fb`).  Danach jedes Byte → RAM, Zähler + 1.
 *  - [W4] /DEND: Byte an der Zähleradresse mit Low-Byte = DSKEA.  Lesen: nach dem Ablegen, bei
 *         CRCEN wird die CRC (inkl. der beiden CRC-Bytes) geprüft, Rest ≠ 0 ⇒ CRC-Fehler-FF.
 *         Schreiben: das Byte an dieser Adresse wird ersetzt — durch (DSKEA & 7) + 1 Marken, wenn
 *         die Markeneinblendung scharf ist, sonst bei CRCEN durch die zwei CRC-Bytes.
 *  - [W5] Markeneinblendung wird scharf, wenn DSKC2 mit /MEN = 0 bei DR/W = 1 beschrieben wird,
 *         und bleibt es bis zur nächsten /DEND (die Firmware nimmt /MEN vor der Endadresse zurück).
 *  - [W6] CRC-Generator: CRC-CCITT, Start FFFF an der ersten Marke einer Markenfolge, über Marken
 *         und alle folgenden Bytes.
 *  - [W7] IMPAUS setzt Synchronisation, Markenzähler, Einblendungen und die Schärfung zurück.
 *  - [W8] Geschrieben wird auf die Platte nur mit WG = 1; der Adresszähler läuft auch ohne WG.
 *  - [W9] Indeximpuls und Daten nur vom gewählten, bereiten Laufwerk; Schritt nur an dieses.
 *  - [W10] ST_PRT Bit 0, 1, 4 lesen 1 (Belegung unbekannt, §12 Frage 6); WRITE FAULT nie.
 *  - [W11] Schreibverzögerung 2 Byte: ein aus dem RAM geholtes Byte liegt 2 Bytezeiten später unter
 *         dem Kopf (Puffer + Schieberegister); WG wirkt beim Holen.  Ohne Verzögerung läge die von
 *         `isr_m2` geschriebene Datenmarke (≈ 213 Takte hinter der Kennfeldmarke) VOR dem Punkt, an
 *         dem `isr_m1` beim Lesen wieder auf eine Marke wartet (≈ 217 Takte) — die Firmware könnte
 *         nicht lesen, was sie schreibt.
 *
 * **Hostschnittstelle [H1]–[H4]:** HEN = 1 setzt das Übertragungs-FF, der Nulldurchgang von CTC K2
 * setzt es bei HEN = 0 zurück (so endet die Übertragung genau nach dem letzten gezählten Byte,
 * §4.2).  Je Byte: ARDY aktiv (von der Host-PIO) ⇒ Byte zwischen Hostbus und RAM[Host-Zähler],
 * ein /ASTB-Impuls an die Host-PIO, Zähler + 1 (HA0 → CTC K2).  Host → WDC nur bei TE inaktiv,
 * WDC → Host nur bei TE aktiv (AVR-Nachbildung).  Ein Byte je ARDY-Aktivierung.  Die Status-
 * leitungen sind CNTST Bit 2–0 ohne Invertierung (die Invertierung liegt hostseitig, P13d).
 * RST = 1 hält UA880 und CTC im Reset und löscht CNTST und DSKC2 (8212-CLR) [H4].
 */
#pragma once
#include "core/bus/k1520_bus.h"
#include "core/peripherals/winchester/platte.h"
#include "core/primitives/z80.h"
#include "core/primitives/z80_ctc.h"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

class P8000Wdc {
public:
    struct Config {
        enum class Firmware { V4_2, V4_0_05, V3_4_05 };
        Firmware firmware      = Firmware::V4_2;
        uint32_t takt_hz       = 4'000'000;   ///< 40 MHz ÷ 10 (Index 3); 41,4-MHz-Bestückung = 4 140 000
        int      wartetakte_m1 = 1;           ///< [W2] Am8127-Waitgenerator, belegt durch time1/time2
        uint8_t  ram_fuellwert = 0x00;        ///< Netz-Ein [real unbestimmt]
        const uint8_t* rom = nullptr;         ///< eigener Abzug statt `firmware` (Tests)
        size_t   rom_groesse = 0;
    };
    static constexpr int LAUFWERKE = 3;

    /// Name der Fassung ("4.2", "4.0.05", "3.4.05") — wie im Konfigurationstext.
    static const char* firmwareName(Config::Firmware f);
    /**
     * Laufwerkstyp, den das EPROM FESTLEGT (P24): bis 4.0 sind Zylinder, Köpfe, Sektoren je Spur
     * und Vorkompensation als Konstanten im Abzug eingebrannt, ein anderes Laufwerk braucht ein
     * anderes EPROM (Endung des Abzugsnamens: `_01` = D5126 615/4, `_02` = D5146 615/8,
     * `_04` = VS 820/6, `_05` = K5504.50 1024/5; siehe `romLaufwerkNachEndung`).  Die
     * eingebundenen Abzüge 3.4.05 und 4.0.05 tragen beide `_05`.
     * `nullptr` = die Fassung ist laufwerksunabhängig (4.2: PAR+BTT von der Platte).
     * Hergeleitet aus den Bytes der Abzüge, nicht aus der Beschriftung — Wächter
     * `P8000WdcRomLaufwerk.*` liest sie an den Fundstellen von `eprom_diffs.txt` nach
     * (Zylinderzahl: das EPROM kennt `Zylinder − 1`; die Annahme „Sektoren je Spur = 18"
     * steht ebenfalls im Abzug, `LD E,12H`).
     */
    static const k1520::winchester::Typ* romLaufwerk(Config::Firmware f);
    /// Laufwerk zur Endung eines EPROM-Abzugs (`WDC_1_3.4_05` → 5); nullptr bei unbekannter Endung.
    static const k1520::winchester::Typ* romLaufwerkNachEndung(int endung);
    /**
     * Spurlage, die die Firmware beim Formatieren schreibt UND beim Lesen erwartet (P24): 4.2 und
     * 4.0.05 mit Interleave 2:1 und Kopfversatz (`sc_tab` im Abzug: `01 0A 02 0B …`), 3.4.05 mit
     * den Sektoren der Reihe nach, einer anderen Lücke hinter dem Kennfeld und dem Interleave in
     * der LOGISCHEN Blockreihenfolge (Tabelle `01 09 11 07 0F 05 …` im Abzug).  Die Hochlauf-Leseprobe
     * der 3.4.05 (BTT auf Z0/K0/S1) scheitert mit Fehler 05 an der 4.2-Spur.
     */
    static k1520::winchester::Platte::Spurformat spurformat(Config::Firmware f);

    P8000Wdc() : P8000Wdc(Config{}) {}
    explicit P8000Wdc(const Config& cfg);

    /// Laufwerk 0–2 anschließen (nullptr = keins).  Die Platte gehört dem Aufrufer.
    void anschliessen(int lw, k1520::winchester::Platte* p);
    k1520::winchester::Platte* platte(int lw) const { return (lw >= 0 && lw < LAUFWERKE) ? platten_[size_t(lw)] : nullptr; }

    // ─── Lebenslauf und Zeit ─────────────────────────────────────────────────
    void powerOn();
    /// Ein Befehl bzw. eine Interruptannahme; Takte inkl. Wartetakten (0 = Debugger-Halt/Reset).
    int  schritt();
    /// Bis zur WDC-Zeit @p t (Takte) laufen; im Reset nur die Zeit (und die Platte) weiter.
    void laufeBis(uint64_t t);
    uint64_t takte() const { return tw_; }

    // ─── Hostschnittstelle (Leitungen; P13d verdrahtet sie an die 16-Bit-PIO2) ─
    void    setzeRst(bool high);               ///< RST: 1 = WDC im Reset
    bool    imReset() const { return rst_; }
    void    setzeTe(bool aktiv);               ///< TE−: aktiv = Host liest (WDC → Host)
    void    setzeArdy(bool aktiv);             ///< WDARDY− der Host-PIO A
    void    setzeHostbus(uint8_t d) { hostbus_ = d; }   ///< vom Host getriebene Daten
    uint8_t datenZumHost() const { return zum_host_; }   ///< vom WDC getriebene Daten
    uint8_t status() const { return cntst_ & 0x07; }     ///< ST2–ST0 (WDC-seitig, nicht invertiert)
    bool    tr() const { return (cntst_ & 0x20) != 0; }  ///< TR−: WDC → Host
    bool    uebertragungAktiv() const { return host_aktiv_; }
    /// Ein /ASTB-Impuls an die Host-PIO (Byte übernommen bzw. Byte liegt an).
    void    setzeAstbRueckruf(std::function<void()> cb) { astb_cb_ = std::move(cb); }
    /// Rückruf, sobald sich ST2–ST0 oder TR ändern (CNTST-Schreiben, Reset) — P13d legt damit
    /// die Pegel an die Host-PIO, ohne nach jedem Befehl zu fragen.
    void    setzeStatusRueckruf(std::function<void()> cb) { status_cb_ = std::move(cb); }
    /// Disk-Schnittstelle läuft gerade an einem gewählten Laufwerk (Plattenzugriffslampe).
    bool    zugriffAktiv() const { return !rst_ && gewaehlt() != nullptr && dssLaeuft(); }
    /// Gewähltes Laufwerk 0–2 (DSKC2 Bit 5–4), −1 = keins.
    int     gewaehltesLaufwerk() const { const int lw = (dskc2_ >> 4) & 3; return lw ? lw - 1 : -1; }

    /// Debugger (P13d): nach jeder Änderung von ST2–ST0/TR mit altem und neuem Wert (CNTST & 27H).
    /// Nur Beobachtung — leer = kein Aufwand.
    std::function<void(uint8_t alt, uint8_t neu)> statusBeobachter;

    // ─── Einsicht (Tests, Debugger) ──────────────────────────────────────────
    Z80&    cpu() { return cpu_; }
    Z80CTC& ctc() { return ctc_; }
    uint8_t lesen(uint16_t a) const;                        ///< Speicherbild der WDC-Z80
    uint8_t cntst() const { return cntst_; }
    uint8_t dskc1() const { return dskc1_; }
    uint8_t dskc2() const { return dskc2_; }
    uint8_t dskea() const { return dskea_; }
    uint16_t diskZaehler() const { return dz_; }
    uint16_t hostZaehler() const { return hz_; }
    /// Lage unter dem Kopf (Byte seit Index) und abgearbeitete Plattenbytes insgesamt.
    int      plattenPosition() const { return static_cast<int>(bytes_ % Platte::BYTES_JE_SPUR); }

    // ─── Save-State (Abschnitt WDC in P8KS, P13d) ────────────────────────────
    /// Z80, CTC samt Phase, RAM, Latches, Zähler, Host-/Disk-Logik, Uhr und die Zustände der
    /// angeschlossenen Platten (Mechanik, Formatzustand, eigene Spurströme).
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    using Platte = k1520::winchester::Platte;
    void     schreiben(uint16_t a, uint8_t v);
    uint8_t  ioLesen(uint8_t port);
    void     ioSchreiben(uint8_t port, uint8_t v);
    int      m1Zyklen() const;
    void     hardwareReset();
    void     diskBis(uint64_t t);
    void     byteVerarbeiten(Platte* p, int pos, uint64_t t);
    void     ablegen(uint16_t w, bool schreiben);
    /// [W3] Lesen: DEN oder Markensuche; Schreiben: nur DEN (der Zähler steht sonst).
    bool     dssLaeuft() const { return (dskc2_ & 0x01) || (!(cntst_ & 0x80) && !(dskc2_ & 0x02)); }
    Platte*  gewaehlt() const;
    void     impuls(int kanal);
    void     hostHandshake();
    uint8_t  ramLesen(uint16_t adr) const;
    void     ramSchreiben(uint16_t adr, uint8_t v);
    static uint16_t ringAdresse(uint16_t zaehler, bool a12) {
        return static_cast<uint16_t>(0x2000 + (a12 ? 0x1000 : 0) + (zaehler & 0x0FFF));
    }

    Config cfg_;
    std::array<uint8_t, 0x2000> rom_{};
    std::array<uint8_t, 0x1800> ram_{};
    Z80      cpu_;
    Z80CTC   ctc_{"WDC-CTC"};
    K1520Bus kette_;
    std::array<Platte*, LAUFWERKE> platten_{};

    uint64_t tw_ = 0;          ///< WDC-Takte
    uint64_t bytes_ = 0;       ///< abgearbeitete Plattenbytes (absoluter Zähler, Index bei % N == 0)
    uint64_t rate_z_ = 5, rate_n_ = 32;   ///< Bytes je Takt = rate_z_/rate_n_ (625 kB/s)
    bool     ei_sperre_ = false;

    // Latches
    uint8_t cntst_ = 0, dskea_ = 0, bm_t_ = 0, bm_d_ = 0, dskc1_ = 0, dskc2_ = 0;
    uint16_t dz_ = 0, hz_ = 0;         ///< Disk-/Host-Adresszähler (12 Bit)

    // Disk-Logik
    bool     synchron_ = false, marke_scharf_ = false, letzte_marke_ = false, crc_fehler_ = false;
    int      marken_ = 0, marken_rest_ = 0, crc_rest_ = 0;
    uint16_t crc_ = 0xFFFF, crc_aus_ = 0;

    // Host
    bool    rst_ = false, te_ = false, ardy_ = false, ardy_verbraucht_ = false, host_aktiv_ = false;
    uint8_t hostbus_ = 0xFF, zum_host_ = 0xFF;
    bool    im_handshake_ = false, nochmal_ = false;
    std::function<void()> astb_cb_, status_cb_;
    uint8_t status_gemeldet_ = 0;
    void    statusMelden();
};

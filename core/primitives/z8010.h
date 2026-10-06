/**
 * @file z8010.h
 * @brief UB8010 (≙ Zilog Z8010) — Speicherverwaltungseinheit (MMU), generisches Primitiv.
 *
 * Kennt keine Karte und keine Maschine (AP P9a, doc/design/25_p8000.md §10.1/§10.11).  Wer
 * welche MMU über /CS anspricht, wie der N/S-Eingang beschaltet ist, wohin SUP wirkt und wie
 * das Kennwort der Trap-Quittung auf dem Bus zusammenkommt, entscheidet die Karte (P9b).
 * Massgeblich: doc/p8000/z8010_mmu.md (mit Belegen); Primärquelle die UB8010-Beschreibung
 * [TB] doc/trascripted/MMU_8010.md.  Wo MAME (z8010.cpp) davon abweicht, gilt [TB]
 * (z8010_mmu.md §9: SAR-Umlauf 63→0, %15/%16 setzen immer, %10 vorhanden, FATL nur aus
 * einem späteren Befehl, Flags mehrerer Verletzungen eines Befehls werden gesammelt,
 * TRNS = 0 reicht vor URS/MST durch).
 *
 * Schnittstelle:
 *  - zyklus(): JEDER Buszyklus, den die MMU an ihren Pins sieht (CPU und DMA, auch nicht
 *    übersetzte) — die MMU braucht die Statusfolge für Befehlsgrenze, ISN/IOFF und SUP.
 *    Rückgabe: Adressausgabe A8–A23 (oder Tri-State) und /SUP für diesen Zyklus.
 *  - kommandoLesen()/kommandoSchreiben(): Spezial-E/A (Status %3) bei aktivem /CS; Code =
 *    AD8–AD15 der E/A-Adresse, Daten = das hohe Byte.
 *  - segtQuittung(): Segmenttrap-Anerkennungszyklus (Status %4) — liefert das Kennwortbit.
 *  - segt()/onSegt: Pegel der /SEGT-Anforderung (true = aktiv).
 *
 * Benannte Annahmen (Lücken der Quellen, z8010_mmu.md §10):
 *  [A1] Befehlsgrenze für FATL/SWW („späterer Befehl"): jeder CPU-Zyklus mit Status %D
 *       (erstes Befehlswort, auch der unechte) und jede Trap-Quittung (%4) beginnen einen neuen
 *       Befehl.  [TB] nennt die Erkennung nicht; mit %4 als Grenze entsteht beim Statusretten
 *       die SWW, die [TB] §5.6 beschreibt.
 *  [A2] Unechter Befehlsholezyklus ([TB] A.3) = CPU-%D-Zyklus, solange die EIGENE /SEGT-
 *       Anforderung ansteht (SEGT ist nicht maskierbar, die CPU quittiert vor dem nächsten
 *       Befehl).  Er erzeugt bei Verletzung nur SUP, setzt keine Flags, ändert keinen Zustand und
 *       wird nicht als ISN/IOFF vermerkt.  Steht nur die Anforderung einer ANDEREN MMU an,
 *       kann diese MMU ihn nicht erkennen und behandelt ihn als gewöhnliches Holen.
 *  [A3] SUP „für alle folgenden Zugriffe des Befehls" ([TB] §5.3) gilt für jeden folgenden
 *       CPU-Speicherzyklus bis zur nächsten Befehlsgrenze [A1], auch wenn diese MMU ihn nicht
 *       übersetzt; DMA-Zyklen dazwischen nur bei eigener Verletzung.
 *  [A4] Mehrere Ereignisse im SELBEN Befehl: weitere Verletzungsflags werden in den VTR
 *       geodert (Beispiel [TB] §8.1.2, VTR = %05), eine Schreibwarnung nach dem ersten Ereignis
 *       setzt nichts mehr; VSN/VOFF/BCSR/ISN/IOFF bleiben beim ersten Ereignis.  Hat der Befehl
 *       schon SWW/FATL gesetzt, folgt im selben Befehl kein weiterer Wechsel ([TB] A: höchstens
 *       ein Zustandswechsel je Befehl), Verletzungen geben nur SUP.
 *  [A5] Verletzung UND Schreibwarnung im selben Zyklus (Normalzustand): beide Flags (z. B.
 *       RDV|PWW); in einem späteren Befehl zählt die Verletzung (→ FATL).
 *  [A6] Übergang in FATL (aus einfachem oder SWW-Zustand) zieht /SEGT einmal; im FATL- und
 *       SWW/FATL-Zustand gibt es kein SEGT mehr, Verletzungen nur SUP, Schreibwarnungen nichts.
 *  [A7] REF/CHG: gesetzt bei jedem übersetzten CPU-Zugriff, der NICHT unterdrückt wird (auch
 *       bei Schreibwarnung und bei bereits gesetztem VTR).  Nicht bei TRNS = 0 und nie bei DMA.
 *  [A8] ISN/IOFF: mitlaufend aus jedem CPU-%D-Zyklus bei MSEN = 1 (unabhängig von URS/MST/
 *       TRNS — am P8000 führt die Daten-MMU ISN/IOFF, obwohl sie Code nie übersetzt) und beim
 *       ersten Ereignis eingefroren; verletzt das %D-Holen selbst, bleibt der Vorgänger.
 *  [A9] VSN = SN0–SN5 (Nummer innerhalb der MMU, [TB] §8.1.2 + MAME), ISN = SN0–SN6.
 *  [A10] Befehl %10 (nur [TB]) wird ausgeführt: MR, VTR, DSCR := 0, /SEGT zurück (wie
 *       Hardware-Reset ohne das /CS-Sonderverhalten).  %11 löscht nur den VTR; eine schon
 *       anstehende /SEGT-Anforderung bleibt bis zur Quittung.
 *  [A11] Unbenutzte Befehlscodes (%12, %17–%1F, %21–%FF) und Lesen der reinen Setzbefehle:
 *       Schreiben ohne Wirkung, Lesen liefert FFH (Bus nicht getrieben).  Lesen der
 *       Statusregister per Schreibbefehl (%02–%07 schreiben): ohne Wirkung.
 *  [A12] /CS bei /RESET: reset(csAktiv) bildet [TB] §7.2.1 nach (MSEN := 1, TRNS := 0); ob
 *       die Karte /CS dabei aktiv hält, ist Sache der Karte (am P8000 offen, Messfrage A2).
 *  [A13] DSCR: %08/%0C greifen auf Feld DSCR zu und zählen; ab 2 springt der Zähler auf 0
 *       (bei %0C mit SAR+1).  Steht DSCR beim Beginn auf 2/3, wird das falsche Feld getroffen
 *       ([TB] §7.4).  Schreiben des SAR lässt den DSCR unverändert ([TB]; MAME löscht ihn).
 *  [A14] DMA-Zugriffe werden gegen RD (Schreiben), SYS (N/S = H), Länge, EXC (kein Holen) und
 *       DMAI geprüft, nicht gegen CPUI; Verletzung ⇒ nur SUP im eigenen Zyklus.
 *  [A15] Nach dem Einschalten (powerOn) sind alle Register und Deskriptoren 0; ein
 *       Hardware-Reset löscht nur MR, VTR, DSCR ([TB] §7.2.1).
 *
 * Save-State: serialize()/deserialize() — Version 1, ein Block fester Länge (der ganze Zustand).
 * Abdeckungsliste ([TB]-Inhalt → Test): doc/p8000/z8010_mmu.md §11.
 *
 * @license MIT
 */
#pragma once
#include "core/primitives/z8000.h"

#include <cstdint>
#include <functional>
#include <vector>

class Z8010 {
public:
    // ── Attributbits des Deskriptors (WEGA mmu.h, oktal) ─────────────────────
    static constexpr uint8_t ATTR_RD   = 0001;
    static constexpr uint8_t ATTR_SYS  = 0002;
    static constexpr uint8_t ATTR_CPUI = 0004;
    static constexpr uint8_t ATTR_EXC  = 0010;
    static constexpr uint8_t ATTR_DMAI = 0020;
    static constexpr uint8_t ATTR_DIRW = 0040;
    static constexpr uint8_t ATTR_CHG  = 0100;
    static constexpr uint8_t ATTR_REF  = 0200;

    // ── Modus-Register ───────────────────────────────────────────────────────
    static constexpr uint8_t MR_MSEN = 0x80;
    static constexpr uint8_t MR_TRNS = 0x40;
    static constexpr uint8_t MR_URS  = 0x20;
    static constexpr uint8_t MR_MST  = 0x10;
    static constexpr uint8_t MR_NMS  = 0x08;
    static constexpr uint8_t MR_ID   = 0x07;

    // ── Violation-Typ-Register ───────────────────────────────────────────────
    static constexpr uint8_t VTR_RDV   = 0x01;
    static constexpr uint8_t VTR_SYSV  = 0x02;
    static constexpr uint8_t VTR_SLV   = 0x04;
    static constexpr uint8_t VTR_CPUIV = 0x08;
    static constexpr uint8_t VTR_EXCV  = 0x10;
    static constexpr uint8_t VTR_PWW   = 0x20;
    static constexpr uint8_t VTR_SWW   = 0x40;
    static constexpr uint8_t VTR_FATL  = 0x80;
    static constexpr uint8_t VTR_PRIME = 0x3F;   ///< primäre Flags ([TB] A)

    // ── BCSR ─────────────────────────────────────────────────────────────────
    static constexpr uint8_t BCSR_NS = 0x20;     ///< N/S-Pegel H (Normal)
    static constexpr uint8_t BCSR_RW = 0x10;     ///< R/W-Pegel H (Lesen)

    // ── Befehlscodes (AD8–AD15 der Spezial-E/A-Adresse, [TB] Tab. 6.2) ──────
    enum Befehl : uint8_t {
        CMD_MR = 0x00, CMD_SAR = 0x01, CMD_VTR = 0x02, CMD_VSN = 0x03, CMD_VOFF = 0x04,
        CMD_BCSR = 0x05, CMD_ISN = 0x06, CMD_IOFF = 0x07,
        CMD_BASIS = 0x08, CMD_LIMIT = 0x09, CMD_ATTR = 0x0A, CMD_SDR = 0x0B,
        CMD_BASIS_INC = 0x0C, CMD_LIMIT_INC = 0x0D, CMD_ATTR_INC = 0x0E, CMD_SDR_INC = 0x0F,
        CMD_RESET = 0x10, CMD_VTR_LOESCHEN = 0x11, CMD_SWW_LOESCHEN = 0x13,
        CMD_FATL_LOESCHEN = 0x14, CMD_CPUI_SETZEN = 0x15, CMD_DMAI_SETZEN = 0x16,
        CMD_DSCR = 0x20,
    };

    /// Ein Segment-Deskriptor (Zugriffsreihenfolge BAH, BAL, LIMIT, ATTR).
    struct Deskriptor {
        uint16_t basis = 0;   ///< physische Anfangsadresse Bit 23..8
        uint8_t  limit = 0;
        uint8_t  attr = 0;
    };

    /// Ein Buszyklus, wie er an den Eingängen der MMU anliegt.
    struct Zyklus {
        uint8_t  status = 0;      ///< ST3..ST0 (Z8kStatus-Code)
        bool     nsHigh = false;  ///< Pegel am N/S-EINGANG: true = H (Normal).  Am P8000 ≠ CPU-Modus!
        bool     lesen = true;    ///< R/W = H
        uint8_t  seg = 0;         ///< SN0..SN6
        uint16_t offset = 0;      ///< AD0..AD15 bei /AS
        bool     dma = false;     ///< DMASYNC = L: Zyklus einer DMA-Einheit
    };
    /// CPU-Zyklus mit N/S-Eingang = N/S der CPU (Normalfall ohne Sonderbeschaltung).
    static Zyklus ausBuszyklus(const Z8kBusCycle& c) {
        return Zyklus{uint8_t(c.st), !c.system, c.read, uint8_t(c.seg & 0x7F), c.addr, false};
    }

    struct Ergebnis {
        bool     adresse = false;  ///< A8–A23 getrieben (sonst Tri-State)
        uint32_t phys = 0;         ///< 24-Bit-Adresse (nur bei `adresse`)
        bool     sup = false;      ///< /SUP aktiv in diesem Zyklus
    };

    /// Kennwort der Trap-Quittung: die MMU treibt AD(8+ID) (`maske`), H bei eigener Anforderung.
    struct Kennung {
        uint16_t maske = 0;
        uint16_t wert = 0;
    };

    Z8010();

    /// Pegeländerung /SEGT (true = Anforderung aktiv).
    std::function<void(bool aktiv)> onSegt;

    // ── Pins ─────────────────────────────────────────────────────────────────
    Ergebnis zyklus(const Zyklus& z);
    Kennung  segtQuittung();
    bool     segt() const { return s_.segt != 0; }

    // ── Spezial-E/A bei aktivem /CS ──────────────────────────────────────────
    uint8_t kommandoLesen(uint8_t code);
    void    kommandoSchreiben(uint8_t code, uint8_t daten);

    // ── Lebenslauf ───────────────────────────────────────────────────────────
    void powerOn();
    /// Fallende Flanke /RESET; @p csAktiv = /CS während des Reset aktiv ([TB] §7.2.1).
    void reset(bool csAktiv = false);

    // ── Einsicht ohne Nebenwirkung (Debugger, Tests) ─────────────────────────
    /// Probeweise Übersetzung: was zyklus() für @p z an Adresse und Prüfbefund ermitteln würde —
    /// ohne Zustand, Flags, REF/CHG, SEGT oder ISN zu berühren.  Der Befund ist der des
    /// Deskriptors; ob daraus SUP/SEGT würde, hängt zusätzlich am Zustandsautomaten.
    struct Probe {
        bool     adresse = false;     ///< A8–A23 würden getrieben
        bool     geprueft = false;    ///< übersetzt mit Rechteprüfung (MSEN ∧ TRNS ∧ URS ∧ MST/NMS)
        uint32_t phys = 0;
        uint8_t  verletzung = 0;      ///< VTR-Bits RDV/SYSV/SLV/CPUIV/EXCV dieses Zugriffs
        bool     dmaiVerletzung = false;
        bool     warnung = false;     ///< Schreibwarnung (DIRW, unterster Block, CPU-Schreiben)
    };
    Probe probe(const Zyklus& z) const;

    /// Die festgehaltene (erste) Verletzung — Statusregister in einem Stück.
    struct Verletzung {
        uint8_t vtr, vsn, voff, bcsr, isn, ioff;
        bool    segtAnforderung;
    };
    Verletzung letzteVerletzung() const {
        return {s_.vtr, s_.vsn, s_.voff, s_.bcsr, s_.isn, s_.ioff, s_.segt != 0};
    }
    /// Läuft gerade SUP bis zum Befehlsende ([A3])?
    bool supBisBefehlsende() const { return s_.supBisBefehlsende != 0; }

    Deskriptor deskriptor(int nr) const { return s_.sdr[nr & 63]; }
    uint8_t mr() const   { return s_.mr; }
    uint8_t sar() const  { return s_.sar; }
    uint8_t dscr() const { return s_.dscr; }
    uint8_t vtr() const  { return s_.vtr; }
    uint8_t vsn() const  { return s_.vsn; }
    uint8_t voff() const { return s_.voff; }
    uint8_t bcsr() const { return s_.bcsr; }
    uint8_t isn() const  { return s_.isn; }
    uint8_t ioff() const { return s_.ioff; }

    // ── Save-State ───────────────────────────────────────────────────────────
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    /// Wie weit der laufende Befehl die Verletzungslogik schon bewegt hat ([A4]).
    enum : uint8_t { BEFEHL_FREI = 0, BEFEHL_ERSTMELDUNG = 1, BEFEHL_WECHSEL = 2 };

    struct S {
        Deskriptor sdr[64];
        uint8_t mr, sar, dscr;
        uint8_t vtr, vsn, voff, bcsr, isn, ioff;
        uint8_t if1Seg, if1Off;   ///< mitlaufendes letztes %D-Holen [A8]
        uint8_t segt;             ///< eigene /SEGT-Anforderung steht an
        uint8_t supBisBefehlsende;///< [A3]
        uint8_t befehl;           ///< BEFEHL_*
    };
    S s_{};

    void     setzeSegt(bool an);
    void     befehlsgrenze();
    uint8_t  sdrZugriff(uint8_t code, bool schreiben, uint8_t daten);
};

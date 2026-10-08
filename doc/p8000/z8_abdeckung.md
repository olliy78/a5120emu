# Z8-Kern (UB 8840 M / Z8) — Abdeckungsliste (AP P19c)

Stand 2026-10-08.  Primitive `core/primitives/z8.{h,cpp}`, Befehlstabelle
`core/primitives/z8/z8_table.h`, Disassembler/Assembler `tools/z8/`, Debugger `tools/dbg_z8.h`
(`k1520dbg --z8 <bild>`).  Grundsatz §10.11a des Plans: **vollständig** nach Datenblatt, nicht
nur soweit das Terminal Typ 2 (`terminal_typ2.md`) oder die K7673.09 (`tastatur_k7673.md`) es
braucht.

**Quelle:** Zilog UM0016 „Z8 CPU User Manual" (UM001604-0108; lokal nicht im Repo, Abschnitte
nach Seitenzahl zitiert).  Ein Robotron-Datenblatt des UB 8840 M liegt nicht vor; UB 88xx gilt als
befehls- und registergleich zum Z8601/Z8611/Z8612 (Terminal-Firmware-Kopf „U 882", HB „UB 8840 M").
Gegenprobe: MAME `src/devices/cpu/z8/z8.cpp` (Stand 741d827a, nur Vergleich, nichts übernommen).

Spalten: **Punkt** (Datenblatt), **Umsetzung**, **Test** (GoogleTest-Name, Binary
`k1520_test_z8` bzw. `k1520_test_z8_asm`), **Annahme/Abweichung**.

## 1. Fassungen und Speicher

| Punkt | Umsetzung | Test | Annahme |
|---|---|---|---|
| Programmbus der 64-poligen Entwicklungsfassung (UB 8840 M ≙ Z8612: 0000–0FFF am eigenen Bus) | `Z8Config::programmbus` (8840: 1000H, 8820: 0800H, Z8681: 0) → `programmLesen` | `Z8Reset.FassungenProgrammbusgrenze` | [Z8-M1] Grenze wie die ROM-Größe der Maskenfassung |
| Programm ≥ Grenze, LDE/LDEI, externer Stapel: Zyklus an P0/P1 mit /AS /DS R/W /DM | `busLesen`/`busSchreiben`, `Z8BusZyklus` (Adresse wie an den Pins, Art, /DM, hochohmig, erweitert) | `Z8Laden.LdcLdeLdciLdeiMitBuszyklen`, `Z8Bus.*` | — |
| LDC-Schreiben in den Programmbus | ohne Wirkung, gezählt (`romSchreibversuche`) | `Z8Laden.LdcLdeLdciLdeiMitBuszyklen` | [Z8-M2] |
| A8–A15 nur in Adressbetrieb (P01M), sonst Pegel des Ports (8/12/16-Bit-Adressierung, UM0016 S. 132) | `pinAdresse()` | `Z8Laden.AdressnibblesVonPort0OhneAdressbetrieb` | — |
| /DM an P34 nur mit P3M.4–3 = 01/10 | `Z8BusZyklus::dm` | `Z8Laden.LdcLde…` | — |
| P01M.4–3 = 11: P0/P1, /AS, /DS, R/W hochohmig (Terminal-DMA) | Zyklus findet nicht statt, Lesen FFH, `letzterZyklus().hochohmig` | `Z8Bus.HochohmigerBusUnterdruecktZyklen` | [Z8-M3] Lesen im Tristate = FFH |
| Erweitertes Timing P01M.5 (+Tx) | +1 interner Takt je externem Zyklus | `Z8Bus.HolenExternErweitertesTimingUndWartetakte` | [Z8-M4] Tx = 2 XTAL-Perioden = 1 interner Takt |
| Wartetakte von außen | `addWaitCycles()` aus dem Busrückruf | dto. | — |
| Registerdatei: 04–7F Allzweck (124), 80–EF nicht vorhanden | Lesen FFH, Schreiben ohne Wirkung | `Z8Laden.NichtVorhandeneRegister…` | [Z8-M5] wie MAME |
| Nur-Schreib-Register F3, F5–F9 lesen FFH (UM0016 S. 81) | `regLesen` | `Z8Laden.NurSchreibbareRegisterLesenFF` | die Debuggersicht `regSicht` zeigt den Inhalt |

## 2. Befehlssatz

(Abschnitt wird mit den Tests gefüllt.)

## 3. Annahmen

| Kennung | Annahme | Begründung |
|---|---|---|
| [Z8-T1] | Registerzugriffe eines Befehls wirken zur Zeit „Befehlsende" (Zähler/UART werden davor nachgezogen) | Datenblatt nennt die Lage im Befehl nur für TMR (M2T2); Fehler ≤ 1 Befehl |
| [Z8-M1]–[Z8-M5] | siehe §1 | |

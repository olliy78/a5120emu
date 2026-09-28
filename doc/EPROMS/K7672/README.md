# Tastatur K7672.03 — Firmware und Protokoll

Stand 2026-09-28. Auswertung für den K8915 (`doc/design/16_k8915.md` §3.5, §6.7), gewonnen
aus der Firmware; nichts davon ist bisher am Gerät gemessen. Legende wie im Planungsdokument:
**[FW]** aus der Z8-Firmware abgelesen, **[SLP]** aus dem Stromlaufplan, **[Adapter]** aus einer
fremden Nachbildung (s. u.), **[?]** Vermutung.

## Dateien

| Datei | Inhalt |
|-------|--------|
| `7672.03-D2.bin` | EPROM D2 (2716), **Programm** — MD5 `f3e7c6f2c24e9f764041d57a9f67c06b` |
| `7672.03-D3.bin` | EPROM D3 (2716), **Zeichentabellen** + Leseroutinen — MD5 `4da5ab4091b9b4af4e783bb3fde8cd66` |
| `7672.03-D2.asm`, `7672.03-D3.asm` | Listings, erzeugt mit `tools/z8_disasm.py` (Aufruf unten) |

Herkunft: `tiffe.de/Robotron/K1520/Tastaturen/K7672/` (lokaler Spiegel unter
`~/projects/robotron/tiffe_de_Robotron/…`), dort auch Stromlauf- und Belegungsplan
(`Schaltplan_Tastatur_K767x.gif`, „Anlage 1 … K7670.XX, K7671.XX, K7672.XX“).

```sh
tools/z8_disasm.py doc/EPROMS/K7672/7672.03-D2.bin --entry 0x000E,0x03EC,0x03F4,0x03FD,0x043D,\
0x045C,0x0462,0x0467,0x0470,0x0479,0x047E,0x0483,0x048B,0x0493,0x04A6,0x04B1,0x04B6,0x04BB,0x04C3,\
0x04CB,0x04CF,0x04D4,0x04E8,0x04F0,0x0500,0x0503,0x0668,0x0685,0x06A6,0x06B6,0x06BB,0x06C0,0x06C5,\
0x06D4,0x06DA,0x06AF
tools/z8_disasm.py doc/EPROMS/K7672/7672.03-D3.bin --entry 0x43,0x46,0x49,0x4C,0x4F,0x52,0x55,0x58,0x5B
```

Die Zusatz-Einsprünge sind die Behandler aus der Befehlstabelle (06F8H) und der Sprungtabelle
(06DAH); der rekursive Abstieg findet sie nicht selbst, weil sie über `CALL @50H` bzw.
`JP @08H` erreicht werden.

## Hardware [SLP]

- **Prozessor: UB8820M** (Z8, 64 Anschlüsse, Programmspeicher über eigenen Adress-/Datenbus
  A0–A11/D0–D7). Port 0/1 lesen die Matrixspalten (S0–S15, Pull-ups R7), Port 2 treibt über
  D4 (CA51, 1-aus-8) und D7 (SE82-Latch) die Zeilen und die **Anzeige-LEDs L0–L8**.
- **Quarz** 9,8304 MHz über zwei LS74 (D5) geteilt ⇒ **2,4576 MHz** am Z8 (Brückenvariante
  „N1 = 9832 kHz“). Mit T0 = 2, Vorteiler 1 ergibt der Z8-UART 2 457 600 / 256 = **9600 Bd**
  [FW] — passt zur SIO-2B-Einstellung des K8915 (§3.2). 8 Datenbits, keine Parität
  (`P3M = 41H`).
- **Summer** H1 an Tout (P36), getrieben von T1 [FW: `OR TMR,#08H` / `AND TMR,#F7H`].
- PROM-Varianten laut Stromlaufplan: nur D2 / **D2 + D3** / eine 2732 auf D2. Die Wahl zwischen
  D2 und D3 läuft über eine Adressleitung, die per Brücke **E5** entweder A11 des Prozessors
  oder **P35** ist. Die Firmware benutzt P35 (s. „Offene Punkte“).

## Protokoll Rechner → Tastatur [FW]

Einzelzeichen (Empfangs-Interrupt IRQ3, 0602H):

| Zeichen | Wirkung |
|---------|---------|
| `BEL` 07H | Summer, langer Ton (0539H) |
| `DC3` 13H | Senden **sperren** (60H Bit 2), LED 21H Bit 3 aus |
| `DC1` 11H | Senden **freigeben**, LED 21H Bit 3 an, eine laufende Antwort abbrechen (2CH = 0) |
| `ESC` 1BH | Beginn einer Befehlsfolge, Vergleich gegen die Tabelle bei 06F8H |

Befehlstabelle 06F8H (je 7 Byte: bis zu 5 Zeichen, mit 8DH aufgefüllt, dann Behandleradresse).
Die ersten neun Einträge sind vollständige Folgen; `ESC [?` und `ESC [2` sind Präfixe, nach
denen die restlichen Einträge gelten (0668H schaltet den Vergleich auf den zweiten Teil):

| Folge | Behandler | Wirkung |
|-------|-----------|---------|
| `ESC c` | 000EH | **Neustart** der Tastatur (Selbsttest, danach `DC1`) |
| `ESC [2;0y` | 06D4H | Neustart **ohne** Selbsttest, danach `DC1` |
| `ESC [2;1y` | 06B6H | **Selbsttest**, danach `DC1` — das prüft das Boot-ROM des K8915 („KEY“) |
| `ESC [?2;1y` / `?2;2y` / `?2;4y` | 06C0H / 06BBH / 06C5H | weitere Testarten [?] (Code nicht im Dump, s. u.) |
| `ESC [c`, `ESC Z` | 0685H | **Kennung**: `ESC` + 6 Byte (ANSI-Modus) bzw. `ESC` + 2 Byte (VT52-Modus) |
| `ESC [5n` | 06A6H | **Statusmeldung**: `ESC` + 3 Byte |
| `ESC [?2l` | 04E8H | **VT52-Modus** (60H Bit 7) |
| `ESC <` | 0500H | zurück in den **ANSI-Modus** |
| `ESC [4h` / `ESC [3l` | 04F0H / 0503H | LED 21H Bit 2 an / aus (dieselbe LED zeigt den VT52-Modus) |
| `ESC [?11h` / `l` | 0467H / 0470H | Modus 23H Bit 6 + LED 21H Bit 7 [?] |
| `ESC [?12h` / `l` | 04C3H / 04BBH | Modus 23H Bit 1 löschen / setzen + LED Bit 4 [?] |
| `ESC [?13h` / `l` | 04B1H / 04A6H | LED 21H Bit 0 an / aus [?] |
| `ESC [?18h` / `l` | 048BH / 0483H | Modus 60H Bit 5 + LED 21H Bit 6 [?] |
| `ESC [?19h`/`20h`/`21h` | 03ECH/03F4H/03FDH | Register 2DH = 0/1/2 — Stufe, die bei gehaltener Taste als Wiederholungszahl dient (0263H) [?] |
| `ESC [?22h` | 043DH | **DCP-Modus: Scancodes statt Zeichen** (29H Bit 0); zurück nur über `ESC c` |

Antworttexte **[Adapter]**: `ESC [?1;1c` (Kennung, 6 Byte) und `ESC [0n` (Status, 3 Byte).
Die Kennung liest die Firmware ab 0739H (ANSI) bzw. 073FH (VT52), die Statusmeldung ab der
Adresse in den Registern 6AH/6BH — die nirgends beschrieben werden, also nach dem Löschen beim
Start 0000H sind [?]. Die
Längen stimmen mit der Firmware überein; die Texte selbst stehen **nicht** in den beiden Dumps
(auch nicht verschoben oder bitweise invertiert). Für den VT52-Modus sind 2 Byte vorgesehen —
nach VT52-Sitte `/Z` oder `/K` [?].

## Protokoll Tastatur → Rechner [FW]

- **`DC1` kommt nur auf Befehl**, nicht beim Einschalten. Der Selbsttest (06B6H) sendet sein
  `DC1` nur, wenn 60H Bit 4 gesetzt ist — und das setzt allein der Empfang einer Befehlsfolge
  (0651H), gelöscht wird es am Ende jedes Behandlers (04E4H). Daraus folgt:
  - **Einschalten** (Reset bei 000CH, `CLR 60H`): Selbsttest, **kein** `DC1`;
  - **`ESC c`** (springt nach 000EH, *hinter* das `CLR 60H`, Bit 4 bleibt): Selbsttest, `DC1`;
  - **`ESC [2;1y`**: Selbsttest, `DC1`;
  - **`ESC [2;0y`** (setzt 60H Bit 3, springt nach 000EH): kein Selbsttest, sofort `DC1` (004FH).
  Das Boot-ROM des K8915 schickt deshalb erst `ESC c` und dann `ESC [2;1y` und wartet auf `DC1`.
- **SCP-Modus (Vorgabe): fertige Zeichen**, ein Byte je Taste, aus den Tabellen in D3:
  0400H–05FFH und 0600H–07FFH, je 128 Byte (8 Zeilen × 16 Spalten der Matrix) für Latein
  ohne/mit Umschaltung und **Kyrillisch in KOI-8 (Bit 7 gesetzt)**, phonetisch belegt
  (Q→Я, E→Е, T→Т, U→У, O→О …). Welche der beiden Tabellenhälften wann gilt, ist nicht
  ausgewertet.
- **Funktions- und Cursortasten** senden `ESC` + bis zu 3 Byte aus Tabellen, getrennt für
  ANSI- und VT52-Modus (0360H–0443H) [?: Tabelleninhalt nicht im Dump].
- **DCP-Modus** (`ESC [?22h`): **PC/XT-Scancodes Satz 1**, Drücken = Code, Loslassen = Code
  + 80H; Umschalt- und Steuertaste werden als `2AH` / `1DH` / `9DH` einzeln gemeldet
  (0320H–035FH). **[Adapter]** bestätigt Satz 1 mit E0-Präfix (`E0 2A E0 37`, `E0 AA`).
- **Tastenklick** (052DH): kurzer Summerton bei jeder Taste, abschaltbar (23H Bit 2, eine
  Taste der Tastatur schaltet ihn um, 04CFH).
- **Flusssteuerung**: Solange `DC3` gilt (60H Bit 2), sendet die Tastatur nichts; auch die
  Antworten auf Kennung/Status unterbleiben.

## Offene Punkte

1. **Umschaltung D2 ↔ D3.** Nur D2 schaltet hin (`OR P3,#20H`, 07F1H/07F8H), nur D3 zurück
   (`AND P3,#DFH`, 0039H). Die Leseroutine 07F0H wählt über Register 4DH einen von neun
   Stummeln in D3 (0040H–005DH: `LDE rX,@rr8 / RET` bzw. `JP 0749H/0753H/0786H/07B6H`).
   Die vier `JP`-Ziele, die Funktionstasten-Folgen und die Antworttexte müssten in D3 liegen —
   dort stehen aber Zeichentabellen. Entweder fehlt ein Teil der Firmware (z. B. die andere
   Hälfte einer 2732), oder die Adressierung ist anders als hier angenommen.
   **Am Gerät klären: die EPROMs der eigenen K7672 auslesen** und mit diesen Dumps vergleichen.
2. Welche physische Taste zu welcher Matrixposition gehört — aus dem Belegungsplan
   (`Belegungsplan_Tastatur_K767x.gif`) zu übertragen, wenn das Modell die Originaltasten
   nachbilden soll. Für den Emulator reicht die Zeichenebene (Host-Taste → ASCII/KOI-8).
3. Bedeutung der Modi `?11`, `?12`, `?13`, `?18` und `?19–21` (welche LED, welche Taste).

## Folgerung für das Emulatormodell (Etappe 2)

Für den Boot des K8915 genügt ein Modell auf Protokollebene, keine Z8-Nachbildung:
Zeichen im SCP-Modus mit 9600 Bd senden, `DC1` nach `ESC c`, `ESC [2;1y` und `ESC [2;0y` (nicht beim Einschalten),
`DC1`/`DC3` als Flusssteuerung, `BEL` an den Summer der Oberfläche, Kennung und Status mit
den Adapter-Texten. Den DCP-Modus erst bauen, wenn SCPX ihn nachweislich einschaltet (Risiko
„K7672 im DCP-Modus“, §9 des Plans).

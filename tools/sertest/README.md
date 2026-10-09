# sertest — Serial Test (`SERTEST.COM`)

Z80-Programm unter CP/M 2.2 (am PC 1715W unter CP/M 3), das die seriellen Schnittstellen
eines **A5120** (ASS K8025, CP/A), eines **K8915** (ATS K7028, SCPX 8915 V5.3), eines
**PC 1715** (ZRE, CP/A 1715), eines **PC 1715W** (ZRE, SCP 3.0) und eines **PRG 710 / PRG 710-1**
(K2521-ZRE + ASS K8025, SCPX V1.5 / V1.7) prüft — am
Gerät mit Prüfstecker bzw. Nullmodemkabel, im Emulator gegen den Rx/Tx-Loop bzw.
einen zweiten Emulator. Spezifikation: `doc/design/19_serielle_schnittstellen.md`
**§14**.

> **V0.3 (2026-10-09): PC 1715W und PRG 710 / 710-1 dazu** (Schalter `/M:W`, `/M:R`, `/M:S`;
> Abschnitte [PC 1715W](#pc-1715w-ab-v03) und [PRG 710 / 710-1](#prg-710--prg-710-1-ab-v03)).
> Die Zeitbasis der Zählschleifen ist je Maschine einstellbar (1715W: 3,9936 MHz), die
> BDOS-Aufrufe sind am 1715W gegen CP/M-3-Bankwechsel geschützt. Die Fassungen V0.1/V0.2 bleiben
> als Prüflinge in `tests/fixtures/cpm/SERTEST_V01.COM` / `SERTEST_V02.COM`; **Verhalten alter
> Fassungen an den neuen Maschinen:** V0.2 hält einen 1715W für einen PC 1715 (gleiche SIO bei
> 0EH/0FH) und rechnet mit 2,4576 MHz und Zeitgeberbetrieb; V0.2 und V0.1 halten ein PRG für
> einen A5120 (K8025 bei 50H, A32 antwortet) — beides ist der Grund für die Erkennung und die
> Schalter von V0.3 (Wächter `Sertest.Pc1715W_AlteV02…`, `Sertest.Prg710_1_AlteV02…`).

>
> **V0.2 (2026-10-09): PC 1715 dazu** (Drucker X4 nur Senden, V.24 X5; Schalter `/M:P`,
> Abschnitt [PC 1715](#pc-1715-ab-v02)). V0.1 (A5120/K8915) bleibt als Prüfling
> `tests/fixtures/cpm/SERTEST_V01.COM` im Baum; sie kennt den 1715 nicht (`Rechner nicht erkannt`).
>
> **Stand, fertig (V0.1: AP-ST1 … ST7, 2026-10-01):** Kommandozeile, Maschinenerkennung,
> SIO-/CTC-Schicht (9600 8N1 für die Dauer der Prüfung, danach BIOS-Vorgabe),
> **Prüfsteckertest** (DATEN-LOOP, LEITUNGEN-LOOP) und **Test mit Gegenstelle** (LEITUNGEN,
> ECHO, FLUSS-HW, FLUSS-XON). Im Emulator vollständig grün (`Sertest.*`,
> `SertestKopplung.*`). **Offen ist allein die Geräteprüfung** — Checkliste
> [unten](#geräteprüfung-durch-den-anwender); was davon abhängt, steht in den Abschnitten
> *Annahmen* und *Kabel* als **[bestätigen]**.

**Wo es liegt:** `SERTEST.COM` steht auf den Bootdisketten in `disks/`:
`a5120_cpa_k5601_system.hfe` (A5120, CP/A), `k8915scpx_boot1.hfe` (K8915, SCPX 8915) und
`pc1715_cpa1715_system.hfe` (PC 1715, CP/A 1715; gebaut von `tools/cpa_pc1715/build.py`). Alle drei gehen als Beispieldisketten ins Paket und landen beim
ersten Start im Diskettenordner des Anwenders. Am Gerät: Diskette mit `gw write` schreiben, booten, `SERTEST`.

Inhalt: Bedienung · Prüfsteckertest · Test mit Gegenstelle · Flusssteuerung ·
Maschinenerkennung · Annahmen · Kabel · Geräteprüfung durch den Anwender · Bauen ·
Im Emulator ausprobieren.

## Bedienung

```
SERTEST                      interaktiv
SERTEST T n [/P] [/G] [/A]   Tester an Schnittstelle n (Nummer aus der Liste)
SERTEST G n                  Gegenstelle an Schnittstelle n
/P nur Prüfsteckertest, /G nur Gegenstellentest (ohne beide: beide)
/A automatisch: keine Rückfragen, kein „beliebige Taste"
/M:A, /M:K, /M:P, /M:W, /M:R bzw. /M:S   Rechner vorgeben (überstimmt die Erkennung):
   A5120, K8915, PC 1715, PC 1715W, PRG 710 bzw. PRG 710-1
```

Fehlerhafte Kommandozeile → Kurzhilfe, Ende. **Ctrl+C** beendet an jeder Stelle
(erst aufräumen, dann Warmstart).

Beim Start:

```
Serial Test V0.3  (c) 2026 Olaf Krieger
Rechner: A5120 (K8025)
Schnittstellen:
  1  DFUE/V.24      SIO A33 Kanal A   V.24
  2  DFUE/IFSS      SIO A33 Kanal B   IFSS
  3  Drucker        SIO A32 Kanal B   IFSS
  -  Tastatur K7637 SIO A32 Kanal A   (Tastatur)
Tester (Aktiv) oder Gegenstelle (Passiv)? T/G
```

Am K8915: `1 Drucker/IFSS1` (SIO1 B, 42H/43H), `2 V.24` (SIO1 A, 40H/41H),
`3 DFUE/IFSS2` (SIO2 A, 50H/51H), Tastatur K7672 an SIO2 B. Die Tastatur wird nur
angezeigt und nie geprüft — ihre SIO umzuprogrammieren nähme dem Programm die
Eingabe und damit Ctrl+C.

- **Tester:** fragt jede Schnittstelle `Test der <name>? J/N` (nur `j`/`J` wählt),
  dann je gewählter `Test mit Pruefstecker? J/N` und `Test mit Gegenstelle? J/N`.
  Am Ende eine Zusammenfassung und `SERTEST ENDE OK` bzw. `SERTEST ENDE FEHLER`.
- **Gegenstelle:** Hinweistext, dann dieselbe Abfrage; die **erste** mit `J`
  bestätigte Schnittstelle gilt. Läuft bis Ctrl+C.

**Ergebniszeilen** (Vertrag, der automatische Test liest sie vom Bildschirm):

```
SERTEST <name> <TEIL>: OK | FEHLER <grund> | ENTFAELLT
SERTEST ENDE OK | SERTEST ENDE FEHLER
SERTEST INTERRUPT OK                  (nur Gegenstelle, einmal beim ersten Empfangsinterrupt)
```

`<TEIL>` ∈ `DATEN-LOOP`, `LEITUNGEN-LOOP`, `LEITUNGEN`, `ECHO`, `FLUSS-HW`, `FLUSS-XON`, `SENDEN` (nur PC-1715-Drucker, ersetzt ECHO);
`ENTFAELLT` heißt „gilt für diese Schnittstelle nicht" (Leitungen und FLUSS-HW an IFSS).
`<name>` ist der Name aus der Liste (`DFUE/V.24`, `Drucker/IFSS1`, …). Wer an diesem
Format etwas ändert, ändert `tests/system/sertest_hilfen.h` (`SertestProtokoll`) mit.

## Prüfsteckertest

- **DATEN-LOOP:** 256 Zeichen 00H–FFH, je Zeichen Echo mit 20 ms Frist. `FEHLER KEIN ECHO
  BEI xxH` (Abbruch beim ersten fehlenden Echo — so sieht „kein Prüfstecker" aus),
  `FEHLER FALSCH nnnnH`, `FEHLER RR1 nnnnH` (Parität/Überlauf/Rahmen), `FEHLER SENDER
  BLOCKIERT`.
- **LEITUNGEN-LOOP** (nur V.24, an IFSS `ENTFAELLT`): RTS/DTR in den Kombinationen 00, 10,
  01, 11; je Kombination eine Rohzeile mit gemessenem CTS/DCD, Erwartung und RR0:

  ```
    RTS=0 DTR=1  CTS=1 DCD=1  erwartet  CTS=1 DCD=1  RR0=2CH
  ```

  Erwartung je Maschine (Prüfstecker RTS→CTS, DTR→DSR+DCD): **A5120** CTS = RTS ∧ DTR,
  DCD = DTR (K8025: V106/V109 nur mit V107); **K8915** CTS = DCD = DTR (K7028:
  CTS = V107 ∧ (¬RTS ∨ V106) — RTS ist am Prüfstecker nicht sichtbar, ein ausgefallener
  RTS-Treiber aber schon in Zeile 11). Im Fehlerfall `FEHLER RTS=r DTR=d` (erste
  abweichende Kombination).

## Test mit Gegenstelle

Zwei Rechner über ein Nullmodemkabel (IFSS: Sendeschleife an Empfangsschleife), auf dem
einen `SERTEST G n`, auf dem anderen `SERTEST T n` (bzw. `T n /G /A`) — **dieselbe
Schnittstelle** auf beiden Seiten. Zuerst die Gegenstelle starten.

**Gegenstelle** (`G n`, läuft bis Ctrl+C):

```
Gegenstelle an DFUE/V.24 bereit.
  CTS=0 DCD=0                     an V.24: die Eingänge, nur bei Änderung
Empfangsinterrupt ausgeloest      beim ersten Zeichen, einmal
SERTEST INTERRUPT OK
Abschnitt E: 1000H Bytes          Ankündigung des Testers angenommen
Abschnitt fertig, Empfangsfehler 0000H
```

- **Ruhezustand:** Empfang im Interrupt in einen Ringpuffer (256 B); an der V.24 ein
  **Leitungsspiegel**: das eigene RTS folgt dem CTS, das eigene DTR dem DCD. Über das
  Nullmodemkabel sieht der Tester so sein RTS als CTS und sein DTR als DSR + DCD wieder.
  Am K8915 hängt CTS am eigenen RTS (K7028: CTS = V107 ∧ (¬RTS ∨ V106)); dort setzt die
  Gegenstelle bei weggenommenem RTS alle ~64 ms kurz RTS, um das CTS der Gegenseite zu
  lesen („Probe").
- **Abschnitt E:** nach der Ankündigung des Testers kommt alles Empfangene aus dem
  Hauptprogramm zurück; danach ein Bericht mit den eigenen Empfangsfehlern, dann wieder
  Ruhezustand. Verstümmelte Ankündigungen werden verworfen (`Ankuendigung verworfen.`),
  3 s ohne Byte → `Zeitueberlauf, zurueck in den Ruhezustand.`

**Tester:**

- **LEITUNGEN** (nur V.24, an IFSS `ENTFAELLT`): RTS/DTR in der Folge 00, 01, 11, 01, 00
  (Grundstellung, DTR setzen, RTS setzen, RTS weg, DTR weg), ohne `/A` je Schritt nach
  einer Taste; je Schritt eine Zeile

  ```
    RTS=1 DTR=1  CTS=1 DCD=1  erwartet  CTS=1 DCD=1  Gegenstelle bestaetigt: JA
  ```

  Erwartung wie am Prüfstecker (Tabelle oben); bestätigt = drei gleiche Lesungen im
  Abstand von 10 ms innerhalb von 2 s. Fehler `FEHLER RTS=r DTR=d` (erster unbestätigter
  Schritt).
- **ECHO** (V.24 und IFSS): 4096 Bytes (Pseudozufallsfolge) senden und gleichzeitig das
  Echo Byte für Byte vergleichen, dann den Bericht der Gegenstelle auswerten. Gründe:
  `ZEITUEBERLAUF BESTAETIGUNG` (keine Gegenstelle — auch, wenn ein Prüfstecker steckt),
  `ZEITUEBERLAUF ECHO BEI nnnnH`, `FALSCH nnnnH`, `RR1 nnnnH`, `GEGENSTELLE nnnnH`,
  `ZEITUEBERLAUF BERICHT`, `BESTAETIGUNG FALSCH`, `BERICHT FALSCH`, `SENDER BLOCKIERT`.
- **FLUSS-HW** (nur V.24, an IFSS `ENTFAELLT`) und **FLUSS-XON** (V.24 und IFSS): wie ECHO,
  aber die Gegenstelle bremst — siehe *Flusssteuerung*. Zusätzlicher Grund
  `NICHT GEBREMST` (die Gegenstelle hat nie gebremst, der Test hätte nichts geprüft).

**Protokoll** (Entwurf 19 §14.6): Weckzeichen 00H + 200 ms Pause, Ankündigung
`1BH 'S' m nL nH s` (m = `E` Echo, `H` Fluss über Leitungen, `X` Fluss über XON/XOFF),
Bestätigung `1BH 'A' m s`, n Nutzbytes, Bericht `1BH 'B' 'E' ueL ueH s` bzw.
`1BH 'B' m ueL ueH bzL bzH s` bei `H`/`X` (`ue` = Empfangsfehler der Gegenstelle, `bz` =
wie oft sie gebremst hat); `s` = Summe der Bytes zwischen 1BH und s. Während der
Übertragung schreibt keine Seite auf den Bildschirm (SCPX 8915 rollt unter DI).

## Flusssteuerung

Beide FLUSS-Teile schicken 4096 Bytes wie ECHO; die **Gegenstelle erzeugt Rückstau**: vor
jedem 512. zurückgeschickten Byte hält sie 300 ms an. Läuft dabei ihr Empfangspuffer
(256 Byte, Interrupt) über **192 Byte**, bremst sie, unter **64 Byte** löst sie die Bremse.
In einem Durchgang bremst sie so siebenmal (`Gegenstelle hat 0007H mal gebremst.`).
OK = Echo fehlerfrei, kein Empfangsfehler auf beiden Seiten (RR1, Pufferüberlauf) und
mindestens einmal gebremst.

- **FLUSS-HW:** die Gegenstelle bremst, indem sie **RTS wegnimmt** (DTR bleibt); über das
  Nullmodemkabel fällt beim Tester CTS. Der Tester sendet mit **Auto Enables** (SIO WR3 D5),
  sein Sender hält also in Hardware an, solange CTS fehlt. Das eigene RTS des Testers bleibt
  gesetzt — am K8915 hängt CTS daran (CTS = V107 ∧ (¬RTS ∨ V106)). Auto Enables erst nach
  der Bestätigung, am Ende wieder aus. Während eines Abschnitts läuft an der Gegenstelle kein
  Leitungsspiegel und keine K8915-Probe; danach stellt sie ihre Leitungen von vorher her.
- **FLUSS-XON:** die Gegenstelle bremst mit **XOFF (13H)** und löst mit **XON (11H)**; der
  Tester hält sein Senden bei XOFF an (Auto Enables aus). Die Nutzdaten enthalten deshalb
  weder 11H noch 13H (das LFSR überspringt sie auf beiden Seiten). Nach dem Bericht schickt
  die Gegenstelle noch ein XON (falls ein Berichtsbyte zufällig 13H war).
- **Im Emulator:** „XON/XOFF beachten" an der Schnittstelle der Gegenstelle nur für
  FLUSS-XON einschalten — dann hält ihr Wandler den Empfang beim eigenen XOFF sofort an,
  egal wie lange das Netz braucht. Für ECHO und FLUSS-HW **aus**: dort gehen alle
  Bytewerte über die Leitung, ein zurückgeschicktes 13H hielte den Empfang an. Ohne den
  Schalter reichen die 63 Byte Reserve über der Bremsschwelle, solange das Netz flink ist.

## Maschinenerkennung

Nur lesend, bzw. schreibend nur in das Steuerregister einer SIO, die sich vorher
durch Lesen gezeigt hat (§14.4). Eine SIO gilt als gefunden, wenn RR0 von Kanal A
oder B ≠ FFH ist **und** RR2 über Kanal B (Registerzeiger 2) ≠ FFH liefert.

- **K8915:** SIO bei 40H (SIO 1) und SIO bei 50H (SIO 2).
- **A5120:** 40H–43H offener Bus (FFH), SIO bei 50H (A33), und RR0 bei 5DH/5FH
  (A32) ≠ FFH — dort wird **nur gelesen**: am K8915 ist 5CH–5FH ein Spiegel der
  CTC 2, ein Zeigerwort wäre dort ein Vektor-/Steuerwort.
- **PC 1715:** weder 40H noch 50H eine SIO, aber RR0 von Kanal A **und** B bei 0EH/0FH ≠ FFH und
  RR2 über Kanal B (0FH) ≠ FFH. Diese Prüfung läuft **erst nach** den beiden anderen: 0CH–0FH ist am
  A5120 die ZRE-CTC, ein Zeigerwort auf 0FH wäre dort ein Steuerwort. (Am 1715W liegt 40H/41H der FDC-
  Zugriff der DMA — der 1715W wird deshalb schon vorher über BDOS 12 abgefangen, s. u.)
- **PC 1715W (ab V0.3):** wird **vor** allem anderen über BDOS 12 erkannt (CP/M 3 meldet `L` = 31H,
  CP/A, SCPX und SCP 1715 melden 22H) und dann nur die SIO0 bei 0EH/0FH wie am 1715 geprüft; **40H/41H
  werden am 1715W nie angefasst** (U8272 per DMA). Gibt es dort keine SIO, bleibt es bei `nicht erkannt`.
- **PRG 710 / 710-1 (ab V0.3):** SIO bei 50H gefunden (wie A5120), aber **keine ZRE-CTC bei 0CH–0FH**
  (am A5120 antwortet sie, dort sind die vier Lesewerte nie alle FFH) und die **K2521-CTC bei 80H–83H
  antwortet** (diese Ports werden nur gelesen, wenn 0CH–0FH leer war — am A5120 nie). Danach der
  8279-Status bei C9H (nur der Status, nie das Datenregister C8H): ≠ FFH = PRG 710, sonst PRG 710-1.
  Läuft **vor** der alten A5120-Prüfung (A32 antwortet auch am PRG).
- sonst: `Rechner nicht erkannt`, Abhilfe `/M:A`, `/M:K`, `/M:P`, `/M:W`, `/M:R` bzw. `/M:S`.

**[bestätigen]** am Gerät: ob an einem A5120 in jeder Ausbaustufe 40H–43H frei ist, und
ob die nicht vom BIOS benutzte SIO 1 des K8915 einen Vektor ≠ FFH trägt (RR2 ist dort nie
programmiert — sonst `/M:K`); ob am PRG der offene Bus bei 0CH–0FH FFH liest, ob ein 710-1 bei C9H FFH
liest (sonst hielte SERTEST ihn für einen 710 — `/M:S`) und ob der 1715W-Schalter BDOS 12 = 31H hält.
Checkliste Schritt 1.

## PC 1715 (ab V0.2)

Belege: CP/A-1715-BIOS (`BIOP.MAC` „Ausgang PC1715: Printer 0c 0e CTC 08/08 nur DTR senden, V24 0d 0f CTC 09/09",
`BIOPCSIO.MAC`, `BIOPKBDC.MAC`, Vektorbelegung in `BIOPNUC.MAC`: „0c0h..0cfh frei", „0f0h..0f6 frei bei PC1715"),
`doc/merkposten/pc1715.md` (Schnittstellen AP-4a), Kern `core/cards/pc1715_zre/pc1715_zre.cpp`.

```
Serial Test V0.3  (c) 2026 Olaf Krieger
Rechner: PC 1715 (ZRE)
Schnittstellen:
  1  Drucker        SIO0 Kanal A      nur Senden (X4)
  2  V.24           SIO0 Kanal B      V.24 (X5)
  -  Tastatur S600  SIO0 Kanal A      (Empfaenger)
```

- **SIO0 bei 0CH–0FH, AB0 = Kanal, AB1 = Steuer:** Daten A/B = 0CH/0DH, Steuer A/B = 0EH/0FH (an K8025/K7028
  liegt der Steuerport 1 über dem Datenport, hier 2). Die Schnittstellentabelle trägt deshalb beide Ports
  und das Feld „Steuerport Kanal B" (RR2).
- **Drucker (SIO0 Kanal A, nur Sender):** Leitungen 102/103/106 (EFS10). Der Empfänger von Kanal A ist die
  Tastatur und bekommt Takt und Format von ihr (WR4 = x1, WR3, WR1 = 0) — SERTEST fasst sie **nie** an,
  setzt keinen Kanalreset und programmiert nur **CTC0 K0** (37H, ZK 1: 2,4576 MHz / 256 = 9600 Bd als
  Sendetakt bei SIO ×1, wie `biopcsio`) und **WR5** (EAH). Nach dem Test dieselben BIOS-Werte
  (`V_P1`); das BIOS programmiert den Drucker erst bei der ersten Benutzung genauso. Teile:
  - *Prüfstecker* (330-042: 103 → 106): **DATEN-LOOP** = 256 Zeichen 00H–FFH **nur senden** (es gibt kein
    Echo; OK heißt: der Sender läuft, Sendepuffer wird immer wieder frei; sonst `FEHLER SENDER BLOCKIERT`).
    **LEITUNGEN-LOOP** = Break aus, an, aus; CTS (RR0 D5) muss folgen (TxD „0" = EIN):
    `  Break=1  CTS=1  erwartet  CTS=1  RR0=xxH`, Fehler `FEHLER Break=b`.
  - *Gegenstelle*: **LEITUNGEN**, **FLUSS-HW**, **FLUSS-XON** `ENTFAELLT`; statt ECHO **SENDEN**:
    Weckzeichen, Ankündigung `1BH 'S' 'E' …`, 500 ms Pause, 4096 Nutzbytes. Es kommt nichts zurück —
    `OK` heißt nur „vollständig gesendet, Sender nie blockiert". **Das Ergebnis liest man an der
    Gegenstelle** (V.24 eines zweiten Rechners, `SERTEST G 2`): `Abschnitt E: 1000H Bytes` und
    `Abschnitt fertig, Empfangsfehler 0000H`.
  - `SERTEST G 1` (Drucker als Gegenstelle) gibt es nicht: `Der Drucker kann nur senden …`.
- **V.24 (SIO0 Kanal B):** Takt CTC0 K1 (09H, 17H, ZK 1 → ×16 = 9600 Bd), Leitungen 105 → RTS (`WR5 D1`),
  108 → DTR (`WR5 D7`), 106 → `/CTSB` (RR0 D5), **107 → `/DCDA`** (RR0 D3 von **Kanal A**, wird nur gelesen
  und mit dem Kommando „Reset Ext/Status" — wie es das BIOS beim Drucker-DTR-Verfahren tut), 109 → `/DCDB`.
  Als „DCD" der Prüfung gilt die **107**, weil sie dem DTR der Gegenseite folgt (am Hub/Nullmodemkabel wie am
  Prüfstecker 320-032: 108 → 107). Die 109 hängt am Prüfstecker an der Leitung 111 (Port 30H), nicht am DTR.
  Erwartung (RTS, DTR) → (CTS, DCD): 00 → 0 0, RTS → 1 0, DTR → 0 1, beide → 1 1 — **CTS = RTS**, anders als
  am A5120 (CTS = RTS ∧ DTR). Die LEITUNGEN-Schrittfolge (00, DTR, RTS+DTR, DTR, 00) und die Spiegelung der
  Gegenstelle (CTS → RTS, DCD → DTR) gehen an beiden Maschinen auf; gemischt geprüft (unten).
- **Interrupt der Gegenstelle:** eigener Vektor **F0H**, „Status affects Vector" aus (WR2 B := F0H) — BIOS-Beleg
  „0f0h..0f6 frei bei PC1715", liegt in jeder Fassung (`intvl` ≤ E8H) innerhalb der Vektorsäule (I = F7H).
  Die Tastatur bleibt unberührt (das BIOS fragt sie im 25-ms-Takt per Polling ab, WR1 A = 0).
- **Wiederherstellen:** V.24 = Kanalreset B + CTC0 K1 Reset (Zustand nach dem Kaltstart; kein Eingriff in
  UC1:-Interruptwerte, damit eine BIOS-Fassung ohne UC1: nicht auf einen leeren Vektor läuft — ein laufender
  UC1:-Treiber wird abgelöst). Drucker siehe oben.
- **Takt:** φ = 2,4576 MHz (CTC0 K0 mit Vorteiler 256 und ZK 1 ergibt genau 9600 Bd — daran ist es im BIOS
  ablesbar), die Zählschleifen gelten unverändert (Fristen sind Mindestzeiten; das 25-ms-Timer-Interrupt des BIOS
  verlängert sie nur).
- **Geprüft** im Emulator unter CP/A 1715 24.05.88 (`@OS.COM`, `tests/fixtures/disks/pc1715_cpa1715_boot_4lw.hfe`)
  und `OS0189` (03.01.89: Liste, Gegenstelle bereit). `OS2LWUHR` nicht einzeln. SCP 1715 / UDOS 1715 / 1715W
  sind nicht Gegenstand.
- **Kabel (Entwurf, [bestätigen]):** X5 V.24 (EFS26) wie üblich: Prüfstecker **320-032** (103 → 104, 105 → 106,
  108 → 107, 111 → 109 — so prüft auch PCTEST), Nullmodem 103 ↔ 104, 105 → 106, 108 → 107 (+ 109) gekreuzt,
  102 ↔ 102. X4 Drucker (EFS10, 102/103/106): Prüfstecker **330-042** (103 → 106). Für SENDEN: X4-103 an
  V.24-104 des anderen Rechners, 102 ↔ 102 (106 braucht SERTEST nicht). Pinbelegung der Buchsen: Gerätedoku
  (`pc_serv.pdf` §1.2.8), nicht in diesem Haus ausgewertet.

## PC 1715W (ab V0.3)

Belege: `doc/pc1715/pc1715w_hardware.md` (§1 E/A-Karte, §5 Interrupts), Stromlaufplan 1715W Bl. D, Kern
(`core/cards/pc1715_zre/` in der Betriebsart `Config::w`, `core/machines/pc1715/`), und das **SCP-3.0-BIOS**
(`SCP3.SYS`, gebankter Teil A600H ff., am laufenden System im Debugger gelesen): Kanalinitialisierung A89BH ff.
(Tabelle F833H ff.), Tastatur-Polling ADCDH ff., Bankumschalter F640H, Vektortabelle bei F300H.

```
Serial Test V0.3  (c) 2026 Olaf Krieger
Rechner: PC 1715W (ZRE)
Schnittstellen:                       (Liste wie am PC 1715)
  1  Drucker        SIO0 Kanal A      nur Senden (X4)
  2  V.24           SIO0 Kanal B      V.24 (X5)
  -  Tastatur S600  SIO0 Kanal A      (Empfaenger)
```

- **Dieselbe Schnittstellenlage wie der PC 1715:** SIO0 bei 0CH–0FH (AB0 = Kanal, AB1 = Steuer), CTC0 bei
  08H–0BH, Drucker X4 = Kanal A nur Sender (Empfänger = Tastatur, nie anfassen), V.24 X5 = Kanal B, 107 an
  `/DCDA`, 109 an `/DCDB`, 106 an `/CTSB`. Die Erwartung der Leitungen ist die des 1715 (CTS = RTS, 107 = DTR).
  Eine Zusatzkarte (CTC1/SIO1 bei 10H–17H, LT107/LT111 bei 2CH–2FH) kennt SERTEST nicht — nicht bestückt.
- **Takt 3,9936 MHz** (`φ = 15,9744 MHz / 4`): die Zählschleifen `N_SENDE`/`N_EMPF`/`N_WARTE` sind Variablen
  (`ZEITIN`; 3994 statt 2458 Takte je ms; `WARTE` hat zwei DJNZ-Schleifen, eine ms braucht ~300 Durchläufe).
  Fristen sind Mindestzeiten — wie bisher.
- **Baudtakt wie das BIOS:** bei 3,9936 MHz ergibt ein Zeitgeber (Vorteiler 16/256) kein ×16-9600 (153,6 kHz
  = φ / 26 — kein Vorteiler-Vielfaches). Das BIOS rechnet darum **im Zählerbetrieb**: Steuerwort `57H`,
  **ZK = 13 × Baudcode** (Baudcode 1 = 9600; A843H–A89AH), SIO ×16 (WR4 = 40H OR 04H = 44H). Daraus folgt
  **CLK/TRG0 und CLK/TRG1 = 1,9968 MHz = φ/2** (13 × 153,6 kHz) — SERTEST programmiert deshalb CTC0 K0
  (Drucker) und K1 (V.24) mit `57H`, `0DH`. **[bestätigen]** Der Emulator verdrahtet CLK/TRG am 1715W noch nicht:
  er meldet das Format der Schnittstellen als „ungültig“ und taktet mit dem Ersatzformat 9600 8N1; die Tests
  prüfen darum die CTC-Programmierung selbst (`Pc1715W_ProgrammiertDenBaudtaktWieDasBios`).
- **BIOS-Vorgabe nach dem Test** (Tabelle F833H, A89BH): CTC `57H`/13; V.24 = Kanalreset B, WR4 44H, WR3 C0H (Empfänger
  noch aus), WR5 68H (DTR/RTS aus); Drucker: nur WR5 68H und CTC0 K0 `57H`/13 (WR1/3/4 gehören der Tastatur).
  Das BIOS programmiert die Kanäle beim Öffnen des Geräts neu. **[bestätigen]**
- **Interrupt der Gegenstelle:** eigener Vektor **30H**. Die BIOS-Tabelle liegt bei `I = F3H` (F300H, in der
  gemeinsamen Speicherzone); belegt sind 08H–0EH (CTC2), 14H (DMA-Ende) und — sobald das BIOS die SIO öffnet —
  20H–2EH (WR2 B = 20H, „Status affects Vector“); ab F360H steht Code. 30H–5EH sind frei (Nullen). Am laufenden
  System ist die SIO nicht im Interrupt (Tastatur wird gepollt, WR1 = 0).
- **CP/M 3 und die Bänke — der eigentliche Unterschied:** SCP 3.0 hat die TPA in einer anderen Bank als das
  BIOS (Systembank mit dem gebankten BDOS/BIOS 7800H–BFFFH; ab C000H gemeinsamer Speicher, dort auch Vektortabelle und BIOS-ISRs). Das BIOS wechselt die
  Bank mit F640H (`DI / OUT (24H),A / EI / RET`) **und läuft danach mit EI in der Systembank** — ein SIO-Interrupt
  dort spränge auf den Tabelleneintrag der Gegenstelle, deren ISR in der TPA-Bank steht. Darum ist jeder
  BDOS-Aufruf (`BDOSW`, nur am 1715W) so geklammert: DI → WR1 des Kanals := 0 → **wartende Zeichen aus dem
  Empfänger holen** (eine schon gemeldete Anforderung geht mit WR1 := 0 *nicht* weg, wohl aber mit dem Lesen der
  Zeichen — das ist im Emulator als Zufallsabsturz aufgefallen) → BDOS → wieder holen → WR1 := 10H → EI. Die
  ISR der BIOS-eigenen Interrupts (1-Hz-Uhr F648H, DMA FB6CH) hängt dagegen an eigenem Stapel in der gemeinsamen
  Zone und ist bankunabhängig. Ohne `BDOSW` fällt `Sertest.Pc1715W_V24EmpfaengtImInterrupt` (verifiziert). Zeichen,
  die während eines BDOS-Aufrufs kommen, liegen im SIO-FIFO (3 Byte ≈ 3 ms) — ein BDOS-6-Aufruf braucht
  deutlich weniger; ein Rollen des Bildschirms mitten im Empfang würde es sprengen (die Gegenstelle schreibt
  nur im Ruhezustand).
- **Konsole:** BDOS 6 (E = FFH: Eingabe ohne Warten, sonst Ausgabe) und BDOS 0 verhalten sich unter CP/M 3
  wie unter 2.2; Strg+C kommt als 03H an (Wächter `Sertest.Pc1715W_CtrlC…`).
- **Geprüft** im Emulator mit SCP 3.0 V0003 (`pc1715w_scp30_system.hfe`): `Sertest.Pc1715W_*`,
  `SertestKopplung.Pc1715W_*` (1715W ↔ 1715W schnell; Drucker → V.24; 1715W ↔ PC 1715 mit V0.2; 1715W ↔ A5120
  mit V0.1, je beide Richtungen).
- **Kabel:** wie 1715 (X5 V.24: Prüfstecker 320-032, Nullmodem; X4 Drucker: 330-042) — der 1715W hat dieselben
  Buchsen [bestätigen, Schaltplan Bl. D: X4/X5 an A20–A23].

## PRG 710 / PRG 710-1 (ab V0.3)

Belege: `doc/merkposten/prg710.md` (K8025 am PRG, Tastaturen), `doc/design/20_prg710.md` §3.7 (`DRUCK.DOK`: V.24
X4 50H/51H CTC 5AH, IFSS-Hauptdrucker X6 5EH/5FH, ZIFSS X5 5CH/5DH, CTC 58H), Kern (`core/machines/prg710/`,
`core/cards/k8025/`) und das **SCPX-BIOS selbst** (`B152V24`/`B152IFSS` V1.5, `B17272V2`/`B17272ZI` V1.7:
Initialisierungstabellen bei E090H, E2FEH/E301H bzw. E2D8H, E30FH).

```
Serial Test V0.3  (c) 2026 Olaf Krieger          Serial Test V0.3  (c) 2026 Olaf Krieger
Rechner: PRG 710 (K2521, K8025)                  Rechner: PRG 710-1 (K2521, K8025)
  1  V.24           SIO A33 Kanal A   V.24 (X4)    1  V.24           SIO A33 Kanal A   V.24 (X4)
  2  IFSS Hauptdr.  SIO A32 Kanal B   IFSS (X6)    2  ZIFSS          SIO A32 Kanal A   IFSS (X5)
  3  ZIFSS          SIO A32 Kanal A   IFSS (X5)    -  Tastatur K7672 SIO A32 Kanal B   (Tastatur)
  -  Tastatur K7609 8279 C8H/C9H      (Tastatur)
```

**Sind 710 und 710-1 bei den seriellen Schnittstellen gleich?** Fast — nachgeprüft im Kern und an den BIOS-Fassungen:

| | PRG 710 | PRG 710-1 | Beleg |
|---|---|---|---|
| K8025, Ports 50H–5FH, V.24 an A33-A mit CTC A34 K2 (5AH) | ja | ja | `prg710.cpp` (`taktquelle = 1`), B152V24 = B17272V2 (gleiche Tabelle `03 07 01`, WR4 4CH, WR5 E8H) |
| ZIFSS A32-A (5CH/5DH), Takt CTC A34 K0 | ja | ja | `k8025.cpp` `ctcTakte()`, B17272ZI |
| IFSS-Hauptdrucker A32-B (5EH/5FH) | **ja** (X6) | **nein** — A32-B = Tastatur K7672 | `prg710.md` „K8025 am PRG“ |
| Tastatur | 8279 + K7609 (C8H/C9H, Polling) | K7672 an A32-B, 9600 8N2 | `prg710.md` „Tastaturen“ |
| CTC A34 K0 | vom BIOS erst für den IFSS programmiert (`05 01`) | beim Kaltstart auf `07 01` — **auch für die Tastatur** | B152IFSS / B17272V2 (E080H) |
| BIOS-Fassung | SCPX V1.5 (BW 15) | SCPX V1.7 (BW 17) | `doc/design/20_prg710.md` §5 |
| Takt φ, K8025-Logik (CTS = V106 ∧ V107, DCD = V109 ∧ V107) | 2,4576 MHz, wie A5120 | dito | `Prg710Machine::CPU_HZ`, K8025 |

Die Hardware der **Schnittstellen** ist also gleich, **die Belegung der A32 und der Taktkanal K0 nicht** — daher
zwei Maschinen im Programm (`/M:R` = 710, `/M:S` = 710-1; beide werden erkannt).

- **Tabu am 710-1:** A32-B (5EH/5FH) und der CTC A34 K0 (58H). SERTEST führt A32-B nicht in der Liste, schreibt
  nie nach 5EH/5FH außer beim 710 (dort ist es der IFSS) und **programmiert K0 am 710-1 nicht** (CTC-Port 0 in der
  Tabelle; das BIOS hat K0 beim Kaltstart auf 9600 gestellt). Am 710 programmiert SERTEST K0 selbst (`07H`, ZK 1).
  Die Wiederherstellung am 710-1 setzt am ZIFSS nur die SIO-Kanalwerte (A32-A), nie K0.
- **Interrupt der Gegenstelle:** eigener Vektor **D0H**. Beide BIOS setzen `I = DFH`, die Tabelle ruht auf
  BIOS-Daten (DF00H–DF8FH); belegt sind nur 90H (K5122-PIO) und E0H (ZRE-CTC), DFC0H–DFFFH ist FFH
  (= „kein Eintrag“, SERTEST springt dann nur mit RETI zurück). Das BIOS betreibt die K8025 gepollt. **[bestätigen]**
  für andere BIOS-Fassungen (nur B152V24/B152IFSS/B17272V2/B17272ZI geprüft).
- **BIOS-Vorgabe nach dem Test** (Tabellen s. Quelltext, `V_R1`–`V_R4`): V.24 = CTC K2 `03 07 01`, WR4 4CH, WR3 41H,
  WR5 E8H; IFSS (710) = K0 `05 01`, WR4 45H (7O1), WR3 41H, WR5 2AH; ZIFSS am 710-1 = WR4 45H/WR3 41H/WR5 2AH;
  ZIFSS am 710 benutzt das BIOS nicht (Kanalreset). Das BIOS programmiert beim ersten Zugriff und merkt es sich — darum
  die Werte *nach* seiner Initialisierung.
- Getestet mit den bootfähigen Fixtures (`prg710_scpx15_cpa640_sysprg.hfe`, `prg710-1_scpx17_cpa640_boot.hfe`), je
  Variante (`Sertest.Prg/SertestPrgP.*`) und gekoppelt (`SertestKopplung.Prg710*`): 710-1 ↔ 710-1 (V.24 und
  ZIFSS), 710 ↔ 710 (V.24, IFSS-Hauptdrucker ↔ ZIFSS), 710 ↔ 710-1, 710-1 ↔ PC 1715 (V0.2), 710 ↔ A5120 (V0.1).
- **Kabel:** wie A5120 (K8025-Belegung der V.24 am Stecker X4, IFSS an X5/X6; Prüfstecker/Nullmodem nach dem Abschnitt
  *Kabel*), die Pinbelegung der PRG-Buchsen selbst **[bestätigen]** — am 710-1 darf an A32-B nichts gesteckt werden, was die
  Tastatur stört.

## Annahmen

Was mit **[bestätigen]** markiert ist, prüft die Checkliste unten.

- **A5120, Brücken „gezeichnet":** DFUE/V.24 (W1:7) und DFUE/IFSS (X7–X8) werden
  beide von der **ZRE-CTC K0** (Port 0CH) getaktet; A46 auf „Asynchron, Takt vom CTC"
  (`doc/design/00_konfiguration.md` führt X7–X8 geschlossen, X9 offen). Steht eine Brücke
  anders (CTC A34 K1/K2), stimmt die Baudrate nicht. **[bestätigen]** W1:7.
- **K8915, Brücke X14** wählt am Multiplexer D13 den Takt der V.24 (RxCA/TxCA); SERTEST
  nimmt **CTC1 K0** an. **[bestätigen]** Stellung von X14.
- **Wichtig für beide:** ein falscher Baudtakt fällt am **Prüfstecker nicht auf** (Sender
  und Empfänger laufen mit demselben Takt), erst gegen die Gegenstelle (`FEHLER RR1 …`,
  `FALSCH …`, `ZEITUEBERLAUF BESTAETIGUNG`).
- **K8915-Empfänger P184 invertierend** (V.24 EIN → TTL L, offen = AUS) — angenommen,
  nicht aus einem Datenblatt. Daraus folgt die K8915-Spalte der LEITUNGEN-LOOP-Erwartung.
  **[bestätigen]** über die Rohzeilen (Checkliste Schritt 2).
- **A5120-Drucker:** Takt CTC A34 K0 = Takt der Tastatur, wird **nie** angefasst;
  nur das SIO-Format wird gesetzt.
- **PC 1715:** wie oben; zusätzlich **[bestätigen]**: CTS-Pegel am Drucker-Prüfstecker 330-042 (Break = EIN),
  RR2 B einer nie programmierten SIO ≠ FFH (sonst `/M:P`), der freie Vektor F0H in der BIOS-Fassung des Geräts,
  die Brücken für Sender-Takt (CTC0 K0 → TxCA, CTC0 K1 → RxCB/TxCB) und dass Kanal A nach WR5 = EAH den
  Drucker nicht stört (am Gerät mit Drucker einmal drucken).
- **PC 1715W (V0.3), nur ein Gerät kann es bestätigen:** (1) CLK/TRG0/1 der CTC0 = φ/2, SIO ×16 (aus der
  BIOS-Rechnung ZK = 13 × Baudcode abgeleitet, nicht aus dem Schaltplan gelesen — der Plan ist als Scan nicht
  auflösbar); (2) 107 an `/DCDA` wie am 1715 (MAME verdrahtet DSR an `/SYNCB` — Quelle ungeprüft); (3) BDOS 12 meldet
  unter SCP 3.0 immer ≥ 30H; (4) WR4 von Kanal A bleibt nach dem BIOS-Kaltstart ×16 (SERTEST fasst WR4 von A nie an,
  der Drucker-Sendetakt hängt daran); (5) der freie Vektor 30H in der BIOS-Tabelle F300H und dass das BIOS die SIO
  nicht im Interrupt betreibt, solange kein Gerät geöffnet ist; (6) ob eine bereits gemeldete Empfangsanforderung am
  echten U856 mit dem Lesen der Zeichen verschwindet (Emulator: ja) — Schritt 5 der Checkliste; (7) die Restore-Werte
  (WR3 C0H, WR5 68H, CTC `57H`/13).
- **PRG 710 / 710-1 (V0.3), [bestätigen]:** (1) offener Bus bei 0CH–0FH und am 710-1 bei C9H liest FFH (Erkennung);
  (2) die K2521-CTC liegt bei 80H–83H und antwortet; (3) der Vektor D0H ist in jeder SCPX-Fassung frei
  (`DFD0H = FFFFH`); (4) CTC A34 K0 kann am 710 frei programmiert werden; am 710-1 hält das BIOS ihn für die Tastatur;
  (5) der Takt der V.24 ist CTC A34 K2 im Zeitgeberbetrieb von φ (BIOS-Tabelle) — A46 und die ZRE-CTC-Kette
  (`taktquelle = 1`) sind nur über den Emulator-Kern, nicht am Gerät belegt.
- φ = 2,4576 MHz an A5120, K8915, PC 1715 und PRG (Zeitbasis der Zählschleifen; gemessen +7 %
  durch BIOS-Interrupts — Fristen sind Mindestzeiten); **PC 1715W 3,9936 MHz** (eigene Zähler, `ZEITIN`).
- **Interruptvektoren** (Gegenstelle): wo das BIOS die SIO nicht im Interrupt betreibt,
  ein eigener Vektor ohne „Status affects Vector" — CP/A **E4H** (`intvsy+04h`, laut
  BIOS frei), SCPX 8915 **C0H** (FFC0H). An der SIO 2 des K8915 (Tastatur) gilt der
  Vektor des BIOS (D0H, Kanal A = DCH/DEH). Ersetzt werden nur diese Einträge; beim
  Ende kommen die alten zurück. **[bestätigen]**, dass E4H bzw. FFC0H in der BIOS-Fassung
  des Geräts frei sind (geprüft: CP/A 25.09.89, SCPX 8915 V5.3 Disketten 900/904).
- **BIOS-Vorgabe nach dem Test:** A5120 DFUE/V.24 = TTY:-Werte (9600 8N1, DTR + RTS),
  Drucker = LPT:-Werte (9600 7O1), DFUE/IFSS Kanalreset; K8915 Drucker/IFSS1 = Werte
  der BIOS-Fassung „55 K" (9600 7O1 — die Fassung „V24 XON/XOFF" hätte 8N1), V.24 und
  DFUE/IFSS2 Kanalreset. Ein laufender UC1:-Treiber (CP/A, 50H) wird durch einen Test
  der DFUE/V.24 abgelöst. **[bestätigen]** mit SCPX-Fassung 901: nach einem Test von
  `Drucker/IFSS1` steht der Kanal auf 7O1 statt 8N1 (Checkliste Schritt 5).

## Kabel

Grundschaltung (so prüft SERTEST): **Prüfstecker** = 103→104, 105→106, 108→107 + 109;
**Nullmodemkabel** = 103↔104, 105→106 und 108→107 + 109 je Richtung gekreuzt, 102↔102.
IFSS: Sendeschleife (SD) der einen Seite an die Empfangsschleife (ED) der anderen, **in
jeder Schleife speist genau eine Seite** (aktiv), die andere ist passiv.

Quellen: A5120 = Kontaktbelegung der Steckverbinder in
`doc/trascripted/Anschlußsteuerung K 8025.50 und K 8025.80.md` §5 und DIL-Schalter A61
(`doc/design/00_konfiguration.md`); K8915 = Stromlaufplan 1.45.518732
(`doc/design/16_k8915.md` §3.2, am Scan gelesen). Alles Übrige ist **[bestätigen]** —
nichts davon ist erfunden; eine leere Pin-Spalte heißt „unbekannt".

### A5120 — Steckverbinder der K8025 (Kartenstecker)

Die Kabel stecken laut Gerätebeschreibung direkt auf der ASS-Karte (die Rückwand verdeckt
sie); ob dazwischen eine Gehäusebuchse liegt: **[bestätigen]**. Zählung A/B wie in der
Betriebsdokumentation (Reihe A ungerade, Reihe B gerade Kontakte).

**X6 DFÜ/V.24** (Bauform 103-13) — Schnittstelle 1:

| Kontakt | V.24 | Richtung | Prüfstecker | Nullmodem → Gegenseite | DB25 nach Norm (nur Vergleich) |
|---------|------|----------|-------------|------------------------|-------------------------------|
| A1 | 102 Betriebserde | — | — | 102 | 7 |
| A3 | 103 TxD | aus | → B4 | → 104 | 2 |
| B4 | 104 RxD | ein | ← A3 | ← 103 | 3 |
| A5 | 105 RTS | aus | → B6 | → 106 | 4 |
| B6 | 106 CTS | ein | ← A5 | ← 105 | 5 |
| A7 | 107 DSR | ein | ← B8 | ← 108 | 6 |
| B8 | 108 DTR | aus | → A7 + A9 | → 107 + 109 | 20 |
| A9 | 109 DCD | ein | ← B8 | ← 108 | 8 |
| B2 | 125 RI | ein | — | — | 22 |
| B10 / A11 / B12 / A13 | 111 / 113 / 114 / 115 | — | — | — (Synchronbetrieb) | 23 / 24 / 15 / 17 |

Die DB25-Spalte ist die Normbelegung einer DEE, **nicht** eine Angabe über das Gerät —
nur brauchbar, falls ein Adapterkabel auf DB25 führt.

**X5 DFÜ/IFSS, X3 Drucker** (Bauform 103-5, beide gleich belegt; X4 = Tastatur):

| Kontakt | Signal | Prüfstecker (X5) |
|---------|--------|------------------|
| A1 | SD− | Sendeschleife an die Empfangsschleife: SD ↔ ED, Polarität **[bestätigen]** |
| B2 | SD+ | |
| A3 | ED+ | |
| B4 | ED− | |
| A5 | „00" (so in der Dokumentation; vermutlich Bezug/Schirm **[bestätigen]**) | — |

- **X5 DFÜ/IFSS:** aktiv/passiv über DIL-Schalter A61; laut `00_konfiguration.md`
  **Empfänger aktiv, Sender passiv** (A61 1 EIN, 2 AUS, 3 EIN, 4 AUS, 5 EIN, 6 AUS)
  **[bestätigen]** am Gerät. Damit speist der Empfänger die einzige Schleife eines
  Prüfsteckers — gültig.
- **X3 Drucker:** **immer aktiv** (Sender und Empfänger über Festwiderstände gespeist,
  Gegenseite = passiver Drucker). Ein einfacher Prüfstecker schaltete zwei Quellen in eine
  Schleife — ob das geht oder ein passives Zwischenstück nötig ist: **[klären]**. Bis dahin
  den Drucker nur prüfen, wenn ein geeigneter Stecker vorhanden ist.
- Aus der Dokumentation für eine *aktive* Sendeschleife: Strom fließt aus **SD+** durch den
  Empfänger der Gegenseite zurück nach **SD−** — also SD+ → ED+ (Gegenseite),
  ED− (Gegenseite) → SD−. Für einen *passiven* Sender an einem *aktiven* Empfänger legt die
  Speiseschaltung des Empfängers die Richtung fest: **[bestätigen]**.

### K8915 — Stecker der Geräterückwand (Gerätezählung)

Die Rückwand zählt anders als der Stromlaufplan der K7028 (Entwurf 19 §3.2). Pinangaben
sind **Kartenkontakte** aus dem Plan; ob die Rückwandbuchse sie 1:1 führt, welche Bauform sie
hat und wie sie belegt ist: **[bestätigen]**.

**X4 V.24** (Karten-X3, SIO1-A) — Schnittstelle 2:

| V.24 | Richtung | Kartenkontakt | Gerätebuchse Pin | Prüfstecker | Nullmodem → Gegenseite |
|------|----------|---------------|------------------|-------------|------------------------|
| 102 Betriebserde | — | | | — | 102 |
| 103 TxD | aus | | | → 104 | → 104 |
| 104 RxD | ein | | | ← 103 | ← 103 |
| 105 RTS | aus | | | → 106 | → 106 |
| 106 CTS | ein | | | ← 105 | ← 105 |
| 107 DSR | ein | | | ← 108 | ← 108 |
| 108 DTR | aus | | | → 107 + 109 | → 107 + 109 |
| 109 DCD | ein | A09 (Plan) | | ← 108 | ← 108 |

Bekannt ist nur 109 = Kartenkontakt A09 — das passt zum Schema der K8025-X6 (A9 = 109),
ist aber für die übrigen Kontakte **nur eine Vermutung**. Auf der Karte gibt es dazu 111
(Brücke X18), 113, 114, 115, 125; SERTEST braucht sie nicht.

**X3 Drucker/IFSS1** (Karten-X4, SIO1-B) — Schnittstelle 1: laut Plan **nur 103/104 mit
V.24-Pegel** (103 = Kartenkontakt B03), keine Stromschleife auf der Karte. „IFSS1" am Gerät
setzt einen Pegelwandler außerhalb der Karte voraus oder benennt nur die Verwendung
**[bestätigen]**.

| Signal | Kartenkontakt | Gerätebuchse Pin | Prüfstecker |
|--------|---------------|------------------|-------------|
| 103 TxD | B03 (Plan) | | → 104 |
| 104 RxD | | | ← 103 |
| 102 | | | — |
| bzw. SD+/SD−/ED+/ED−, falls am Gerät eine Stromschleife | | | **[bestätigen]** |

**X5 DFÜ/IFSS2** (Karten-X5, SIO2-A) — Schnittstelle 3: 103/104 mit V.24-Pegel **und** die
einzige Stromschleife der Karte (Blatt 2, Optokoppler U1):

| Signal | Kartenkontakt (Plan) | Gerätebuchse Pin |
|--------|----------------------|------------------|
| SD+ | A01 | |
| SD− | B02 | |
| ED− | A03 | |
| ED+ | B04 | |

Achtung: dieselben Kontaktnummern tragen an der K8025-X5 die **umgekehrte** Polarität
(A1 = SD−, B2 = SD+, A3 = ED+, B4 = ED−) — ein 1:1-Kabel zwischen den Karten wäre falsch.
**Aktiv/passiv** von Sender und Empfänger: unbekannt **[bestätigen]**. Gegen die A5120-X5
(Sender passiv, Empfänger aktiv) muss der K8915 **ebenso** stehen: sein Empfänger speist die
Schleife des A5120-Senders, der A5120-Empfänger die seines Senders.

### PC 1715W und PRG

Wie PC 1715 (1715W: dieselben Stecker X4/X5, Prüfstecker 330-042/320-032) bzw. A5120 (PRG: V.24 X4 mit der K8025-Belegung
von X6 des A5120, IFSS X5/X6). Alles **[bestätigen]**. Gemischt koppelbar: 1715W/PC 1715 über die V.24 (beide Richtungen,
Tests), PRG/A5120 über die V.24, PRG-ZIFSS gegen IFSS anderer Maschinen (Drucker-IFSS der K8025 ist beidseitig aktiv —
dieselbe Einschränkung wie am A5120-Drucker, nicht ausprobiert).

### Welche Schnittstellen gegeneinander

Das Protokoll trägt keine Schnittstellennummer; es kommt nur auf die **Art** an
(V.24 ↔ V.24 für LEITUNGEN/FLUSS-HW, IFSS ↔ IFSS). Die Nummern auf beiden Seiten
unterscheiden sich deshalb:

| A5120 | K8915 | Kabel | Teile |
|-------|-------|-------|-------|
| 1 DFUE/V.24 (X6) | 2 V.24 (X4) | V.24-Nullmodem | LEITUNGEN, ECHO, FLUSS-HW, FLUSS-XON |
| 2 DFUE/IFSS (X5) | 3 DFUE/IFSS2 (X5) | IFSS gekreuzt | ECHO, FLUSS-XON |
| 3 Drucker (X3) | — | Drucker beidseitig aktiv, K8915-X3 laut Plan V.24-Pegel: **nicht direkt koppelbar** **[klären]** | nur Prüfstecker |

## Geräteprüfung durch den Anwender

Ein A5120 und ein K8915, je eine Systemdiskette mit `SERTEST.COM` (aufspielen wie unter
*Im Emulator ausprobieren*, dann auf eine echte Diskette, z. B. mit dem DiskTool am
Greaseweazle), Prüfstecker und Kabel nach *Kabel*. Zuerst die Gegenstelle starten. Diese
Liste deckt alle „am Gerät"-Punkte aus Entwurf 19 §14.10 ab; Ergebnisse dort und in
*Kabel*/*Annahmen* nachtragen (**[bestätigen]** → Befund).

**Bei jeder Abweichung notieren:** Gerät, Kartenvariante (K8025.50/.60/…), BIOS-Fassung
(CP/A-Kopfzeile; SCPX-Diskette 900/901/904), die Kommandozeile, die Rolle, **alle**
Zeilen ab dem Start (Foto des Bildschirms genügt), die Kabelbelegung (welche Kontakte
verbunden) und die Stellung der Brücken/Schalter aus Schritt 0.

| # | Wo | Tun | Erwartet | Klärt |
|---|----|-----|----------|-------|
| 0 | beide | Brücken ablesen: A5120 W1:7, X7–X8/X8–X9, A61 (1–6), A46; K8915 X14, X15, X16, IFSS-Schalter/Brücken der K7028 (falls vorhanden) | W1:7 und X7–X8 gezeichnet, A61 wie in *Kabel* | Annahmen Takt, IFSS aktiv/passiv |
| 1 | beide | `SERTEST` ohne Argumente, an `T/G` Ctrl+C, dann `DIR` | `Rechner: A5120 (K8025)` bzw. `Rechner: K8915 (K7028)`, drei Schnittstellen + Tastatur; danach bedienbar | Maschinenerkennung (40H–43H am A5120 frei? RR2 der SIO 1 am K8915 ≠ FFH?) — bei `Rechner nicht erkannt` oder falschem Rechner die Ausbaustufe notieren, weiter mit `/M:A` bzw. `/M:K` |
| 2 | A5120 | Prüfstecker an X6: `SERTEST T 1 /P` | `DATEN-LOOP: OK`, `LEITUNGEN-LOOP: OK`, vier Rohzeilen ohne `FALSCH` | Kabel/Pins X6 |
| 2a | K8915 | Prüfstecker an X4: `SERTEST T 2 /P` | wie 2; Rohzeilen: 00 → CTS=0 DCD=0, 10 → 0 0, 01 → **1** 1, 11 → 1 1 | **P184-Polarität**: alle vier Rohzeilen samt RR0 notieren. DCD umgekehrt zu DTR (etwa 00 → DCD=1) = Polarität falsch angenommen; nur Zeile 11 abweichend = RTS-Treiber oder V106-Empfänger |
| 2b | beide | `SERTEST T n /P` ohne Stecker an einer V.24 | `DATEN-LOOP: FEHLER KEIN ECHO BEI 00H` | Gegenprobe |
| 2c | beide | Prüfstecker an jeder IFSS: A5120 `T 2 /P` (X5); K8915 `T 1 /P` (X3), `T 3 /P` (X5); A5120 `T 3 /P` (X3) nur mit geeignetem Stecker (*Kabel*) | `DATEN-LOOP: OK`, `LEITUNGEN-LOOP: ENTFAELLT`; nach A5120 `T 3` Tastatur und `LIST` intakt | IFSS-Polarität, aktiv/passiv |
| 3 | V.24 | Nullmodem A5120-X6 ↔ K8915-X4. K8915 `SERTEST G 2`, A5120 `SERTEST T 1 /G` (ohne `/A`: jeden LEITUNGEN-Schritt ansehen) | fünf Zeilen `Gegenstelle bestaetigt: JA`; `ECHO: OK`; `FLUSS-HW: OK` und `FLUSS-XON: OK` mit `Gegenstelle hat 0007H mal gebremst.`; `SERTEST ENDE OK`. Gegenstelle: `SERTEST INTERRUPT OK`, `Abschnitt fertig, Empfangsfehler 0000H` | Kabel, Baudtakt K8915 (**X14**), **K8915-RTS-Probe** (Schritt 11 muss ohne spürbare Verzögerung bestätigt werden; Zeit bis `JA` notieren) |
| 3a | V.24 | umgekehrt: A5120 `SERTEST G 1`, K8915 `SERTEST T 2 /G`; danach an der Gegenstelle Ctrl+C, `DIR` | wie 3 | Vektor **E4H** (A5120-Gegenstelle mit eigenem Vektor); Baudtakt A5120 (**W1:7**) |
| 3b | V.24 | wie 3, K8915 als Gegenstelle mit Ctrl+C beenden, `DIR`, Taste tippen | K8915 bedienbar | Vektor **FFC0H** (SIO 1 des K8915) |
| 4 | IFSS | A5120-X5 ↔ K8915-X5 gekreuzt. K8915 `G 3`, A5120 `T 2 /G`; dann A5120 `G 2`, K8915 `T 3 /G` | `LEITUNGEN`/`FLUSS-HW: ENTFAELLT`, `ECHO: OK`, `FLUSS-XON: OK`, `SERTEST ENDE OK` | IFSS-Kabel, aktiv/passiv; Takt A5120-IFSS (**X7–X8**) |
| 5 | K8915 | mit **SCPX-Diskette 901** („V24 XON/XOFF") booten, Prüfstecker an X3, `SERTEST T 1 /P`; dann Drucker an X3, etwas drucken (`^P` + `DIR`) | bekannte Grenze: Druck verstümmelt (Kanal 7O1 statt 8N1), nach Kaltstart wieder richtig | Druckervorgabe SCPX-901 — bestätigt sich die Grenze, ist sie hinzunehmen oder `V_K1` je Fassung zu wählen |
| 6 | beide | in Schritt 3 mitten in ECHO auf der Tester-Seite Ctrl+C, dann `DIR`; ebenso an der Gegenstelle | beide Rechner bedienbar, Tastatur geht | Wiederherstellen am Gerät |

Fällt Schritt 3 mit `FEHLER RR1 …` oder `FALSCH …` aus, während der Prüfstecker (2/2a)
`OK` war, liegt es fast sicher am **Baudtakt** einer Seite (Brücke W1:7 bzw. X14) — der
Prüfstecker sieht den nicht. `ZEITUEBERLAUF BESTAETIGUNG` heißt: auf der anderen Seite
läuft keine Gegenstelle, das Kabel ist nicht gekreuzt oder eine Seite hat einen anderen
Takt.

### PC 1715 — Checkliste für das Gerät

| # | Tun | Erwartet |
|---|-----|----------|
| 1 | `SERTEST`, an `T/G` Ctrl+C, `DIR` | `Rechner: PC 1715 (ZRE)`, zwei Schnittstellen + Tastatur; bei `Rechner nicht erkannt` → `/M:P` und RR2-Befund notieren |
| 2 | Prüfstecker 330-042 am X4: `SERTEST T 1 /P` | `DATEN-LOOP: OK`, `LEITUNGEN-LOOP: OK` mit drei Break-Zeilen (CTS 0, 1, 0); danach Tastatur bedienbar, Drucker druckt (`^P`+`DIR`) |
| 3 | Prüfstecker 320-032 am X5: `SERTEST T 2 /P` | beide OK, vier Rohzeilen CTS = RTS, DCD = DTR; ohne Stecker `KEIN ECHO BEI 00H` |
| 4 | Nullmodem X5 ↔ X5 zweier 1715 (oder zu einem A5120-X6): `SERTEST G 2` / `SERTEST T 2 /G` | wie A5120 ↔ A5120 (Schritt 3 der Liste oben): `ECHO`, `FLUSS-HW`, `FLUSS-XON` OK |
| 5 | Drucker X4 → V.24 des anderen Rechners: dort `SERTEST G 2`, hier `SERTEST T 1 /G` | `SENDEN: OK`, an der Gegenstelle `Abschnitt fertig, Empfangsfehler 0000H` |

### PC 1715W und PRG — Checkliste für das Gerät

| # | Tun | Erwartet |
|---|-----|----------|
| 1 | 1715W: `SERTEST` (SCP 3.0), Strg+C, `DIR`; PRG: `SERTEST`, Strg+C, `DIR` | `Rechner: PC 1715W (ZRE)` bzw. `PRG 710 …` / `PRG 710-1 …` mit der passenden Liste; sonst Abhilfe `/M:W`, `/M:R`, `/M:S` und die Lesewerte von 0CH–0FH, 80H, C9H notieren |
| 2 | Prüfstecker an V.24: `SERTEST T 2 /P` (1715W) / `T 1 /P` (PRG) | beide OK; 1715W: CTS = RTS, DCD = DTR; PRG: 00 → CTS 0 DCD 0, 10 → 0 0, 01 → 0 1, 11 → 1 1 |
| 3 | 1715W-Drucker: Stecker 330-042: `T 1 /P`; danach Tastatur bedienbar | `DATEN-LOOP`/`LEITUNGEN-LOOP` OK |
| 4 | Nullmodem: `SERTEST G n` auf dem einen, `SERTEST T n /G` auf dem anderen | `ECHO`, `FLUSS-HW`, `FLUSS-XON` OK, `SERTEST INTERRUPT OK`, Empfangsfehler 0000H — am 1715W ist das der Beleg für CTC `57H`/13 und die Bankumschaltung |
| 5 | **1715W:** während der Gegenstelle sehr schnell Tasten drücken (Rollen des Bildschirms) und Strg+C; danach `DIR` | System bedienbar, kein Absturz (Interrupt/Bank) |
| 6 | **PRG 710-1:** nach Tests am ZIFSS Tastatur prüfen; **PRG 710:** IFSS-Hauptdrucker gegen ZIFSS | Tastatur geht, ENTER lädt wieder |
| 7 | Baudrate: Gegenstelle am 1715W gegen einen PC mit bekanntem 9600 8N1 | läuft; sonst Annahme (1) bzw. (5) der Liste oben |

## Bauen

```sh
python3 tools/sertest/build.py          # -> tools/sertest/sertest.com (eingecheckt)
python3 tools/sertest/build.py clean    # leert tools/sertest/build/
python3 tools/sertest/build.py --check  # Temp-Bau, bytegleich mit der eingecheckten .com?
python3 tools/sertest/build.py --out x.com   # Temp-Bau nach x.com
```

`--check` ist der ctest-Wächter `cli_sertest_com_passt_zur_quelle`; ohne Werkzeugkette
endet er mit 77 (= übersprungen). Die Emulatortests stehen in
`tests/system/test_sertest.cpp` (`tools/dev.sh test -R Sertest`; PC 1715: `Sertest.Pc1715_*`, 1715W: `Sertest.Pc1715W_*`,
PRG: `Sertest.Prg/SertestPrgP.*`) und, mit zwei gekoppelten
Maschinen, in `tests/system/test_sertest_kopplung.cpp` (`SertestKopplung.*`; zwei Fälle — A5120 und
PC 1715 — in `tools/dev.sh test`, die übrigen in `tools/dev.sh test-format`, darunter
`Pc1715_DruckerSendetAnDieGegenstelle` und `Pc1715_MitA5120UndDerAltenV01_BeideRichtungen` mit V0.1 auf
dem A5120).

M80 + LINKMT aus `~/projects/CPA_Workbench/tools` über `cparun`, Ladeadresse
0100H; Pfad überschreibbar mit `CPA_TOOLS=<pfad>`. Das Skript bricht ab, wenn M80
nicht `No Fatal error(s)` meldet (M80 selbst endet auch bei Fehlern mit 0). Die
gebaute `.com` wird nach `tools/sertest/sertest.com` kopiert und **eingecheckt** —
die CI hat die CPA_Workbench nicht.

**Nach jedem Neubau die Disketten nachziehen** — SERTEST.COM liegt auf vier Disketten (für PC 1715W und PRG kommen
Platzierungen auf SCP-3.0- bzw. SCPX-Disketten hinzu — das macht der Diskettenbau, nicht dieses Verzeichnis), die drei Bauwege
nehmen es alle aus `tools/sertest/sertest.com`:

```sh
tools/dev.sh build
python3 tools/cpa_a5120/build.py  --tool build/k1520disktool    # disks/a5120_cpa_k5601_system.hfe  (A5120, CP/A)
python3 tools/cpa_pc1715/build.py --tool build/k1520disktool    # disks/pc1715_cpa1715_system.hfe    (PC 1715, CP/A)
python3 tools/disketten_beigaben.py --tool build/k1520disktool  # k8915scpx_boot1.hfe u. a. (K8915, SCPX 8915)
```

Wächter (je mit `--check`, ctest): `cli_cpa_a5120`, `cli_cpa_pc1715` und `cli_beigaben_auf_den_disketten` schlagen an,
wenn eine Diskette SERTEST.COM nicht oder in anderer Fassung trägt. `tools/disketten_beigaben.py` bringt außerdem
ROMREAD und die A5120.16-Prüfprogramme auf ihre Disketten; gebaut wird über den gemeinsamen Teil `tools/cpm_bau.py`.

## Im Emulator ausprobieren

```sh
S=/tmp/sertest; mkdir -p $S; cp tools/sertest/sertest.com $S/SERTEST.COM
# A5120 / CP/A (Kopie! nie die Fixture direkt):
cp tests/fixtures/disks/cpa_cpa780_k5601_noclock.img $S/a.img
tools/dev.sh tool k1520disktool put $S/a.img $S/SERTEST.COM
printf 'gscreen "A>" 400000000\nkeys sertest\\r\ngscreen "T/G" 50000000\nscreen\nkeys \\x03\ng 3000000\nscreen\nq\n' > $S/a.dbg
tools/dev.sh tool k1520dbg --skip-selftest $S/a.img -x $S/a.dbg

# K8915 / SCPX 8915 (Kaltstart fragt nach <ENTER>, Autostart „rade" abwarten):
cp tests/fixtures/disks/k8915scpx_cpa800_k5601_bios55k-disk900.hfe $S/k.hfe
tools/dev.sh tool k1520disktool put $S/k.hfe $S/SERTEST.COM
printf 'gscreen "<ENTER>" 900000000\nkeys \\r\ngscreen "size:" 300000000\ng 30000000\nkeys sertest\\r\ngscreen "T/G" 80000000\nscreen\nq\n' > $S/k.dbg
tools/dev.sh tool k1520dbg --machine k8915 --skip-selftest $S/k.hfe -x $S/k.dbg
```

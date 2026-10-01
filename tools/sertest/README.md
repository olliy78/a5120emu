# sertest — Serial Test (`SERTEST.COM`)

Z80-Programm unter CP/M 2.2, das die seriellen Schnittstellen eines **A5120**
(ASS K8025, CP/A) und eines **K8915** (ATS K7028, SCPX 8915 V5.3) prüft — am
Gerät mit Prüfstecker bzw. Nullmodemkabel, im Emulator gegen den Rx/Tx-Loop bzw.
einen zweiten Emulator. Spezifikation: `doc/design/19_serielle_schnittstellen.md`
**§14**.

> **Stand V0.1, fertig (AP-ST1 … ST7, 2026-10-01):** Kommandozeile, Maschinenerkennung,
> SIO-/CTC-Schicht (9600 8N1 für die Dauer der Prüfung, danach BIOS-Vorgabe),
> **Prüfsteckertest** (DATEN-LOOP, LEITUNGEN-LOOP) und **Test mit Gegenstelle** (LEITUNGEN,
> ECHO, FLUSS-HW, FLUSS-XON). Im Emulator vollständig grün (`Sertest.*`,
> `SertestKopplung.*`). **Offen ist allein die Geräteprüfung** — Checkliste
> [unten](#geräteprüfung-durch-den-anwender); was davon abhängt, steht in den Abschnitten
> *Annahmen* und *Kabel* als **[bestätigen]**.

**Wo es liegt:** `SERTEST.COM` steht auf den Bootdisketten in `disks/` — allen
`cpa_cpa780_*` (A5120, CP/A) und `k8915scpx_boot1.hfe` (K8915, SCPX 8915). Die CP/A-Disketten
(Auswahl) und die K8915-Diskette gehen als Beispieldisketten ins Paket und landen beim
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
/M:A bzw. /M:K   Rechner A5120 bzw. K8915 vorgeben (überstimmt die Erkennung)
```

Fehlerhafte Kommandozeile → Kurzhilfe, Ende. **Ctrl+C** beendet an jeder Stelle
(erst aufräumen, dann Warmstart).

Beim Start:

```
Serial Test V0.1  (c) 2026 Olaf Krieger
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

`<TEIL>` ∈ `DATEN-LOOP`, `LEITUNGEN-LOOP`, `LEITUNGEN`, `ECHO`, `FLUSS-HW`, `FLUSS-XON`;
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
- sonst: `Rechner nicht erkannt`, Abhilfe `/M:A` bzw. `/M:K`.

**[bestätigen]** am Gerät: ob an einem A5120 in jeder Ausbaustufe 40H–43H frei ist, und
ob die nicht vom BIOS benutzte SIO 1 des K8915 einen Vektor ≠ FFH trägt (RR2 ist dort nie
programmiert — sonst `/M:K`). Checkliste Schritt 1.

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
- φ = 2,4576 MHz an beiden Maschinen (Zeitbasis der Zählschleifen; gemessen +7 %
  durch BIOS-Interrupts — Fristen sind Mindestzeiten).
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

## Bauen

```sh
python3 tools/sertest/build.py          # -> tools/sertest/sertest.com (eingecheckt)
python3 tools/sertest/build.py clean    # leert tools/sertest/build/
python3 tools/sertest/build.py --check  # Temp-Bau, bytegleich mit der eingecheckten .com?
python3 tools/sertest/build.py --out x.com   # Temp-Bau nach x.com
```

`--check` ist der ctest-Wächter `cli_sertest_com_passt_zur_quelle`; ohne Werkzeugkette
endet er mit 77 (= übersprungen). Die Emulatortests stehen in
`tests/system/test_sertest.cpp` (`tools/dev.sh test -R Sertest`) und, mit zwei gekoppelten
Maschinen, in `tests/system/test_sertest_kopplung.cpp` (`SertestKopplung.*`; ein Fall in
`tools/dev.sh test`, die übrigen in `tools/dev.sh test-format`).

M80 + LINKMT aus `~/projects/CPA_Workbench/tools` über `cparun`, Ladeadresse
0100H; Pfad überschreibbar mit `CPA_TOOLS=<pfad>`. Das Skript bricht ab, wenn M80
nicht `No Fatal error(s)` meldet (M80 selbst endet auch bei Fehlern mit 0). Die
gebaute `.com` wird nach `tools/sertest/sertest.com` kopiert und **eingecheckt** —
die CI hat die CPA_Workbench nicht.

**Nach jedem Neubau die Disketten nachziehen:**

```sh
tools/dev.sh build
python3 tools/sertest/disketten.py --tool build/k1520disktool          # aufspielen
python3 tools/sertest/disketten.py --tool build/k1520disktool --check  # = Wächter
```

Der Wächter `cli_sertest_auf_den_disketten` schlägt an, wenn eine der Disketten
(Liste `DISKETTEN` im Skript) SERTEST.COM nicht oder in einer anderen Fassung trägt.

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

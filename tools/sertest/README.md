# sertest — Serial Test (`SERTEST.COM`)

Z80-Programm unter CP/M 2.2, das die seriellen Schnittstellen eines **A5120**
(ASS K8025, CP/A) und eines **K8915** (ATS K7028, SCPX 8915 V5.3) prüft — am
Gerät mit Prüfstecker bzw. Nullmodemkabel, im Emulator gegen den Rx/Tx-Loop bzw.
einen zweiten Emulator. Spezifikation: `doc/design/19_serielle_schnittstellen.md`
**§14**.

> **Stand V0.1 (AP-ST5):** Gerüst (Kopfzeile, Kommandozeile, Maschinenerkennung,
> Schnittstellenliste, Rollenwahl, J/N-Abfragen, Ctrl+C), SIO-/CTC-Schicht (9600 8N1 für
> die Dauer der Prüfung, danach BIOS-Vorgabe), **Prüfsteckertest** (DATEN-LOOP,
> LEITUNGEN-LOOP) und **Test mit Gegenstelle** (LEITUNGEN, ECHO; Gegenstelle mit
> Leitungsspiegel und Echo). FLUSS-HW/FLUSS-XON folgen in AP-ST6; bis dahin melden sie
> `FEHLER NICHT EINGEBAUT` (ein Test, der nicht läuft, meldet nie `OK`).

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
```

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

**Protokoll** (Entwurf 19 §14.6): Weckzeichen 00H + 200 ms Pause, Ankündigung
`1BH 'S' m nL nH s`, Bestätigung `1BH 'A' m s`, n Nutzbytes, Bericht `1BH 'B' m ueL ueH s`;
`s` = Summe der Bytes zwischen 1BH und s. Während der Übertragung schreibt keine Seite
auf den Bildschirm (SCPX 8915 rollt unter DI).

## Maschinenerkennung

Nur lesend, bzw. schreibend nur in das Steuerregister einer SIO, die sich vorher
durch Lesen gezeigt hat (§14.4). Eine SIO gilt als gefunden, wenn RR0 von Kanal A
oder B ≠ FFH ist **und** RR2 über Kanal B (Registerzeiger 2) ≠ FFH liefert.

- **K8915:** SIO bei 40H (SIO 1) und SIO bei 50H (SIO 2).
- **A5120:** 40H–43H offener Bus (FFH), SIO bei 50H (A33), und RR0 bei 5DH/5FH
  (A32) ≠ FFH — dort wird **nur gelesen**: am K8915 ist 5CH–5FH ein Spiegel der
  CTC 2, ein Zeigerwort wäre dort ein Vektor-/Steuerwort.
- sonst: `Rechner nicht erkannt`, Abhilfe `/M:A` bzw. `/M:K`.

Am Gerät offen: ob an einem A5120 in jeder Ausbaustufe 40H–43H frei ist, und ob
die nicht vom BIOS benutzte SIO 1 des K8915 einen Vektor ≠ FFH trägt (sonst
`/M:K`).

## Annahmen

- **A5120, Brücken „gezeichnet":** DFUE/V.24 (W1:7) und DFUE/IFSS (X7–X8) werden
  beide von der **ZRE-CTC K0** (Port 0CH) getaktet. Steht eine Brücke anders (CTC
  A34 K1/K2), stimmt die Baudrate nicht.
- **A5120-Drucker:** Takt CTC A34 K0 = Takt der Tastatur, wird **nie** angefasst;
  nur das SIO-Format wird gesetzt.
- φ = 2,4576 MHz an beiden Maschinen (Zeitbasis der Zählschleifen; gemessen +7 %
  durch BIOS-Interrupts — Fristen sind Mindestzeiten).
- **Interruptvektoren** (Gegenstelle): wo das BIOS die SIO nicht im Interrupt betreibt,
  ein eigener Vektor ohne „Status affects Vector" — CP/A **E4H** (`intvsy+04h`, laut
  BIOS frei), SCPX 8915 **C0H** (FFC0H). An der SIO 2 des K8915 (Tastatur) gilt der
  Vektor des BIOS (D0H, Kanal A = DCH/DEH). Ersetzt werden nur diese Einträge; beim
  Ende kommen die alten zurück.
- **BIOS-Vorgabe nach dem Test:** A5120 DFUE/V.24 = TTY:-Werte (9600 8N1, DTR + RTS),
  Drucker = LPT:-Werte (9600 7O1), DFUE/IFSS Kanalreset; K8915 Drucker/IFSS1 = Werte
  der BIOS-Fassung „55 K" (9600 7O1 — die Fassung „V24 XON/XOFF" hätte 8N1), V.24 und
  DFUE/IFSS2 Kanalreset. Ein laufender UC1:-Treiber (CP/A, 50H) wird durch einen Test
  der DFUE/V.24 abgelöst.

## Kabel

**[offen — liefert der Anwender, AP-ST7]** Belegung des Prüfsteckers und des
Nullmodemkabels je Stecker (A5120 X3/X5/X6, K8915 X3/X4/X5), IFSS aktiv/passiv.

Grundschaltung: Prüfstecker = TxD→RxD, RTS→CTS, DTR→DSR+DCD; Nullmodem = TxD↔RxD,
RTS→CTS, DTR→DSR+DCD jeweils gekreuzt, Masse.

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

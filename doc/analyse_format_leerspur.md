# `Fehler 'U' SPUR DEFEKT` beim Formatieren leerer Spuren — GELÖST

**Status:** ✅ gelöst (2026-08-06).  **Ursache:** verwaister FORMAT-Schreibstrom im K5122 —
die frisch geschriebene Spur wurde erst beim *nächsten* Schreib-Strobe ins Medium
materialisiert, sodass FORMAT.COMs Vergleichs-Lesen sie je nach Anlaufphase noch als
unformatiert vorfand.  **Fix:** ein Lese-`/STR`-Strobe aus ZVE1-Kontext committet einen
anstehenden Schreibstrom, bevor er den Lesetransfer armiert
(`K5122::handleCtrlPortAWrite`, `core/cards/k5122/k5122.cpp`).

---

## 1. Symptom (Ausgangslage)

Formatiert man unter **CP/A** mit `FORMAT.COM` (V19.05.89) eine **komplett unformatierte**
Diskette **mit Vergleichs-Lesen**, meldet das Programm an zufälligen Stellen
`Fehler 'U' SPUR DEFEKT!`.  Der Lauf bricht nicht ab, die Diskette wird fertig
formatiert — die betroffenen Spuren gelten aber als defekt.  Auf einer bereits
formatierten Diskette und auf echter Hardware tritt es nicht auf.

`'U'` ist der BIOS-Ergebniskode **„keine Marke gefunden"** (`fl.to1`, `E32F`:
`ld a,'U' ; time-out`), erreicht über `pret4` nach 5 Umdrehungen ohne Adressmarke.

### Referenzmessung (jeweils voller Lauf, Format 1, Spur 0–159, mit Verify)

| Anlaufphase (`boot`-Wert vor der Uhrzeit) | vor dem Fix | nach dem Fix |
|---|---|---|
| 76 | 2× `'U'` (Spur 71, 78) | **0×** |
| 78 | 2× `'U'` (Spur 5, 123)  | **0×** |
| 80 | 0×                      | **0×** |
| 82 | 1× `'U'` (Spur 24)      | **0×** |

Ergebnisdiskette (Phase 78): `disk verify` → *160 Spuren, 863 Sektoren, 0 CRC-Fehler,
0 Problem-Spuren, 0 leer* (= 3×26 + 157×5, genau die `cpa780`-Geometrie).

---

## 2. Reproduktion

```sh
S=/tmp/fmt && mkdir -p $S
cp disks/cpadisk_autofs_noclk_noautoexec.img $S/A.img

cat > $S/full.script <<'EOF'
boot 78
type 12:00:00
enter
boot 5
type FORMAT
enter
boot 30
enter
boot 6
type B
enter
boot 10
enter
boot 8
type 1
boot 6
enter
boot 6
enter
boot 6
type j
boot 8000
EOF

rm -f $S/B.hfe
./build/format_driver $S/A.img $S/B.hfe $S/full.script ""     # "" = LEERE Diskette anlegen
```

- Der leere vierte Parameter legt B: als **unformatierte Leerdiskette** an
  (`A5120Machine::createDisk` mit leerem Formatnamen); ein Katalogname (z. B. `cpa800`)
  legt vorformatiert an — das ist die Gegenprobe.
- Andere Anlaufphase = erste Zeile ändern (76/78/80/82).  `boot 8000` ist das Taktbudget
  nach dem Start; der Lauf dauert ~2 min.
- `FD_LOGLEVEL=info` schaltet die K5122-Meldungen (`>>> READ`, `>>> FORMAT-WRITE`) zu.

**Wichtig:** ein kurzer Spurbereich reicht **nicht** — mit `bis Spur 4` oder `20` lief auch
der fehlerhafte Fall durch.

---

## 3. Die Ursache

### 3.1 Was FORMAT.COM pro Spur tut

Die geladene Routine `0x1CEC–0x1D38` (RAM-Disassemblat, s. §5) ist der generische
„ZVE2-Auftrag starten"-Rahmen — identisch zum BIOS-Original `diortn` (`E45A`,
`disks/cpadisk_autofs_noclk_noautoexec.prn`):

```
1CEC  LD A,FE / LD (1D22H),A   ; Spin-Falle 1D21 (JR $) schaerfen
1CF1  LD HL,1D50 / LD DE,1DE5  ; zwei weitere Fallen im ZVE2-Code
1CF7  LD (HL),18H / LD (DE),18H ;   (Opcode -> JR zurueck)
1CFD  XOR A / OUT (14H),A      ; Daten-PIO Tor A laden
1D00  OUT (04H),A              ; ZVE2 in Reset
1D02  DEC A / OUT (17H),A ×2   ; Daten-PIO Tor B: Mode 3, alles Eingang
1D07  LD A,B9H / OUT (10H),A   ; /STR = 1
1D0B  LD A,A5H / OUT (10H),A   ; /STR = 0, /WE = 1  -> Lese-Strobe
1D0F  LD A,7FH / OUT (17H),A   ; Tor B -> Mode 1 (Byte-Eingabe)
1D13  IN A,(16H)               ; Scheineingabe  == "CPU2 starten (ueber BUSRQ)"
1D15  LD (0001H),BC            ; ZVE2-Einsprung
1D19  LD A,B4H / OUT (10H),A   ; /WE = 0  -> Schreiben
1D1D  LD A,83H / OUT (11H),A   ; Index-Interrupt scharf
1D21  JR $                     ; Wartepark, bis die Index-ISR [1D22] patcht
```

ZVE2 läuft ab `1D39`: Gap-Bytes `4E` in der gepatchten Falle `1D4E/1D50` schreiben, bis der
Index kommt (das ist die **Spurlängen-Messung** — „Laenge in Bytes: 6282", Sollwert 6250
±2 %), danach die eigentliche Spur (`C2 C2 C2 FC`-IAM, `A1 A1 A1 FE`-IDAMs, `OTIR` der
ID-Felder, Datenfelder).  Anschließend liest FORMAT die Spur zum **Vergleich** zurück.

### 3.2 Der Fehler im Emulator

Der K5122 sammelt den Vollspur-Schreibstrom in `write_buf_` und materialisiert ihn erst in
`commitFormatTrack()`.  Ausgelöst wurde der Commit nur an zwei Stellen:

1. am **nächsten Schreib-`/STR`-Strobe** (`handleCtrlPortAWrite`), und
2. über die **Schreib-Idle-Erkennung** in `update()` — die aber ausschließlich im
   `write_mode_` läuft.

`startReadTransfer()` setzt `write_mode_ = false`.  Ein Lese-Strobe (das Vergleichs-Lesen)
löschte also `write_mode_`, **ohne** den Strom zu committen: der fertig geschriebene
Spurinhalt blieb verwaist in `write_buf_` liegen, die Schreib-Idle-Erkennung war
abgeschaltet, und die Spur galt im Medium weiter als **unformatiert** — bis irgendwann der
Schreib-Strobe der *nächsten* Spur den Puffer abschloss.

Ob das auffiel, war ein reines Wettrennen um die Reihenfolge:

```
gesunde Spur (C=2 H=0)                     defekte Spur (C=2 H=1)
  /STR B4  wr=1  -> Commit (Vorspur)         /STR B0  wr=1 -> Commit (Vorspur)
  ... ZVE2 schreibt (2,0) ...                ... ZVE2 schreibt (2,1) ...
  /STR B4  wr=1  -> Commit (2,0)  <<<        /STR A1  wr=0 -> READ (2,1) UNFORMATIERT
  /STR A5  wr=0  -> READ (2,0) 5 Sekt  OK    /STR A1  wr=0 -> READ (2,1) UNFORMATIERT
                                             ... 5 Umdrehungen ohne Marke -> 'U'
                                             (Commit erst 1 s spaeter, Kopf schon auf C=3)
```

Gemessen mit temporären `DIAG`-Logs auf `/STR`-Flanken und `commitFormatTrack()`;
im Fehlerfall lag zwischen Schreib-Strobe und Commit von `(2,1)` gut **eine Sekunde
Maschinenzeit**, in der zwei Vergleichs-Lesungen auf die noch leere Spur trafen.

Auf echter Hardware gibt es dieses Fenster nicht: die Bytes liegen in dem Moment auf der
Scheibe, in dem der Kopf sie schreibt.

### 3.3 Der Fix

`core/cards/k5122/k5122.cpp`, `handleCtrlPortAWrite`, ZVE1-Kontext des
`/STR`-Lese-Strobes:

```cpp
if (write_mode_ && !write_buf_.empty()) commitFormatTrack();
startReadTransfer();
```

Ein Lese-Strobe aus **ZVE1**-Kontext bedeutet zwangsläufig, dass ZVE1 aus seinem
Format-Wartepark (`JR 1D21`) zurück ist, ZVE2 die Spur also fertig geschrieben hat —
der Strom ist vollständig und gehört auf die Diskette.  (Die `/STR`-Strobes *während* des
Schreibens kommen von ZVE2 und laufen über den `bus_.isBUSRQ()`-Zweig, werden also nicht
berührt.)

Guards:
- `K5122Test.FormatWrite_LeseStrobeCommittetSpurSofort` (Unit, `tests/cpp/test_k5122.cpp`) —
  schlägt ohne den Fix fehl (0 statt 4 Sektoren).
- `format_blank_disk_with_verify` (Anlaufphase 80) **und neu**
  `format_blank_disk_with_verify_p78` (Anlaufphase 78, traf den Fehler reproduzierbar).
  Phase 80 allein war als Wächter untauglich — sie lief auch MIT dem Fehler durch.

---

## 4. Belegte Grundlagen (weiterhin gültig)

### 4.1 K5122-Handbuch (`doc/trascripted/Floppy Anschlußsteuerung K 5122.md`)

- **§4.1**: Steuer-PIO **Tor A = Mode 0 (Ausgabe)**, Tor B = Mode 3; `/ASTB` von Tor A ist
  der **Index-Puls**.  Bitlage Tor A: `A₀ /WE`, `A₁ MK`, `A₂ /FR`, `A₃ /STR`, `A₄ MK1`,
  `A₅ MR/SD`, `A₆ /HL`, `A₇ /ST`.  Tor B: `B₇ = /TRACK 00` ist ein **Laufwerks**signal.
- **§5.5**: der Lesedatenpfad ist vom **Marken-FF** getaktet — erst nach erkannter Marke
  (MKE = 1) entsteht `/BSTB` und damit die Übernahme ins Daten-PIO.
- **§5.6.1**: „BUSRQ wird gebildet, wenn der Daten-PIO zum Datenaustausch mit dem Bus
  bereit ist … BUSRQ wird durch /STR unterdrückt."  1-aus-8-Decoder A3.3 über
  `/ARDY`/`/BRDY`; die vierte Eingangskombination ist in der Wahrheitstabelle **nicht**
  aufgeführt (Ausgang 03 → weder `/BUSRQ` noch `/WAIT`).
- **§5.7**: der Separator wartet nicht auf den Abholer — das nächste Byte kommt in den PIO,
  bevor die CPU das alte geholt hat (`/FA`, Überlauf).

### 4.2 CP/A-BIOS (`disks/cpadisk_autofs_noclk_noautoexec.prn`)

| Adresse | Label | Bedeutung |
|---|---|---|
| `E32F` | `fl.to1` | `ld a,'U'` — **genau die Fehlermeldung**, „keine Sektor-Marke gefunden" |
| `E392` | `fl.sto` | setzt `fl.zto` (Zähler für Indexpunkte) aus Register A |
| `E39A` | `itimeo` | **Index-ISR**: `dec a / ld (fl.zto),a`; bei 0 → `pretx` |
| `E3AA` | `pretx` | `jr pret3`, während eines Transfers auf `jr pret4` gepatcht |
| `E3AC` | `pret4` | Timeout **während** Transfer → CPU 2 stoppen, `dtrret` = „keine Marke gefunden" |
| `E3BD` | `headup` | `OUT (11H),3` = Index-Interrupt sperren; `OUT (18H),0FFH` = Motor aus |
| `E45A` | `diortn` | **Original des FORMAT-Rahmens `1CEC`** (s. §3.1) |
| `E47F` | — | `IN A,(fldabd)` mit dem Kommentar **„CPU2 starten (ueber BUSRQ)"** |
| `E2C9` | — | vor dem Transfer: `fl.sto(5)` „Überwachung auf fehlende Marke" |
| `E2D6` | `fehret` | Operationsende: `pretx+1 = 0x0B`, `fl.sto(20)` = 4-s-Leerlauf-Motorstopp |

Dazu die Warnung des BIOS-Autors bei `E481`:
> „Achtung!! Zur Synchronisation CPU1 und CPU2 keine 2-Byte-Werte (Adressen) verwenden,
> da dazwischen BUSRQ auftreten kann und dann nur ein Byte gesetzt wurde!!"

---

## 5. Werkzeuge / Messwege dieser Analyse

- RAM-Disassemblat der geladenen FORMAT-Routine:
  `format_driver … ramdump 0100 2200 tpa.bin` (Skript-Kommando), dann
  `python3 tools/z80_disasm2.py --org 0x0100 --entry 0x1CEC --entry 0x1D39 tpa.bin`.
- Zustandsverlauf: temporäre `LOG_INFO`-Zeilen (env-geschaltet) auf `/STR`-Flanken,
  `commitFormatTrack()` und `OUT(10H)` — zeigten Reihenfolge und Puffergröße pro Spur.
  (Nicht committet; das Muster ist bei Bedarf schnell wiederhergestellt.)
- Ergebnisprüfung: `k1520dbg -b <disk.hfe>` → `disk verify b`.

---

## 6. Widerlegte Hypothesen — bitte nicht wiederholen

| # | Hypothese | Widerlegung |
|---|---|---|
| 1 | Index-Puls kommt zu oft / falsche Rate | `indexPeriodCycles = 2 450 000·60/300 = 490 000` Takte = 0,2 s = genau ein Puls pro Umdrehung.  Gerechnet und im Code verifiziert. |
| 2 | Index-Phase wird durch Laufwerkswechsel zerstört | Zähler der Resets im `!motorAtSpeed`-Zweig während eines Formatierlaufs: **0**. |
| 3 | Motor-Spin-up kostet Umdrehungen | `K5122_MOTOR_SPINUP_MS = 2`. Vernachlässigbar. |
| 4 | Unsere CPU-Taktbilanz ist zu langsam | Die BIOS-eigene kalibrierte 50-ms-Schleife (`phrw`, Soll 122 500 Takte) läuft in **128 078** Takten = +4,6 %. 94 Messungen. |
| 5 | Das Füllbyte des Leerstroms (`0x4E`) ist falsch | Auf `0xAA` (echter Wert einer leeren Spur, §7.2) umgestellt: 2/2/0/1 defekte Spuren — **identisch** zur `0x4E`-Referenz. |
| 6 | §5.5 wörtlich umsetzen (markenlos ⇒ gar kein Byte, kein `/BUSRQ`) | Vier Phasen gemessen: **4 von 4 frieren ein**.  Deutlich schlechter als der damalige Stand. |
| 7 | Verfallsfrist der unabgeholten Anforderung tunen | 2 Byteperioden funktionierte; entkoppelter Zähler mit Schwelle 2 → Einfrieren bei Spur 0, Schwelle 16 → 20 defekte Spuren.  Nicht monoton, reines Symptomtuning. |
| 8 | SCPX-Mini-Stack-Guard verzögert Interrupts | Instrumentiert: **null Treffer** während des Formatierlaufs. |
| 9 | Zu wenig Index-Pulse ⇒ BIOS-Wachhund verhungert | Pro Spur 6–11 Index-Pulse; der markenlose Abbruch läuft über `pret4` korrekt ab. |
| 10 | Der ZVE1↔ZVE2-Arbitrierungspunkt („`continue;` — ZVE1 läuft nicht während `/BUSRQ`") ist die Ursache | Widerlegt durch die Messung in §3.2: die Reihenfolge Commit ↔ Vergleichs-Lesen entscheidet, nicht die Busvergabe.  Mit dem Commit-Fix sind alle vier Anlaufphasen sauber, an der Arbitrierung wurde nichts geändert. |

---

## 7. Weitere gemessene Fakten (Kontext, unverändert gültig)

### 7.1 Zeitbudget pro Spur

Index-Pulse pro Spur, nach Kartenzustand aufgeschlüsselt:

| Fall | idle | markenlos | Verify | Schreiben | Summe |
|---|---|---|---|---|---|
| leere Diskette | 0–2 | 5–9 | 0 | 2 | 7–13 |
| vorformatiert, 1. Durchlauf | 1–5 | 0 | 0–1 | 2 | 3–8 |
| vorformatiert, 2. Durchlauf | 5 | 0 | 0–1 | 2 | **7** |

Zeitanteile im Formatierfenster (PC-Histogramm): **idle 87 %**, Lesen 4,5 %, Schreiben 8,4 %.
Heißeste Leerlaufstellen: `E37D–E389` (Software-CRC) ~4,6 %, `E6CC/E6CD` (`iodis1`) ~3,1 %,
`E230` (Kopfstabilisierung) ~1 %.  Kein dominanter Wartepunkt.

### 7.2 Inhalt einer echten Leerspur

Aus `disks/leer_scp.dmk` (Greaseweazle-Mitschnitt einer echten leeren Diskette,
80 Spuren × 2 Seiten, `track_len = 7668`):

```
Spur 0 / 1 / 40:  IDAM-Eintraege = 0,  7540 Rohbytes,  davon 7540 × AA
```

Eine echte leere Spur liefert also **einen durchgehenden Bytestrom ohne jede Adressmarke** —
das stützt das Modell in `startReadTransfer()` (Bytes ja, Marken nein) und spricht gegen die
wörtliche §5.5-Lesart (Hypothese 6).

> Die zugehörige `disks/leer_scp.hfe` meldet im Header `bitrate = 311` kbit/s, hat aber
> 15 097 Bytes je Spurseite = 120 776 Zellen pro Umdrehung (nominal 50 000) — ~2,4-fach
> überabgetastet und **kein ganzzahliger Faktor**.  Seit `4740b98` wird die Überabtastung am
> Inhalt bestimmt (Kandidaten 1–4); bei dieser markenlosen Diskette greift das nicht und sie
> gilt korrekt als unformatiert.  Für einen *formatierten* Mitschnitt mit demselben Setup
> wäre ein nicht-ganzzahliger Faktor weiterhin ein Problem.

### 7.3 Ereignisfolge des BIOS-Wachhunds (gemessen)

```
drvsel    pretx=00  fl.sto(255)      ; Laufwerkswahl: Motorabschaltung gesperrt
fl.sto(5)                            ; "Ueberwachung auf fehlende Marke"
itimeo ×5   zto 5->1                 ; fuenf Umdrehungen ohne Marke
pret4                                ; KORREKTER Abbruch "keine Marke gefunden" -> 'U'
fehret    pretx=0B  fl.sto(20)       ; Operationsende -> 4-s-Leerlauf-Motorstopp scharf
```

Die 5 Umdrehungen sind **das Budget des OS selbst** und kein Emulatorfehler.  Genau dieser
Pfad lieferte das `'U'` — er lief korrekt, nur war die Spur zu diesem Zeitpunkt eben (im
Modell) noch leer.

---

## 8. Offen geblieben (getrennt vom obigen Fehler)

**`/BUSRQ` wird nicht aus dem Daten-PIO-Handshake gebildet.**  Heute assertiert der K5122
`/BUSRQ` direkt aus `byte_ready_`/`transferring_`; auf echter Hardware entsteht es aus
`/ARDY`/`/BRDY` des Daten-PIO (§4.1/§5.6.1).  Der BIOS-Kommentar bei `E47F`
(„`IN A,(16H)` = CPU2 starten (ueber BUSRQ)") und die Sequenz *Tor B → Mode 3 → Mode 1 →
Scheineingabe* belegen, dass die Karte **vor** dieser Scheineingabe gar kein `/BUSRQ`
bilden kann — sonst käme ZVE1 nie dazu, den ZVE2-Einsprung bei `1D15`/`E461` zu setzen.
Unser Modell braucht deshalb den künstlichen Verfall der unabgeholten Anforderung nach
2 Byteperioden (`advanceByteClock`, Commit `69551f0`).

Das ist eine echte Modellierungslücke, aber **nicht** die Ursache des hier gelösten
Fehlers.  Wer sie angeht: ein „armiert"-Flag am Daten-PIO Tor B (Steuerwort → entwaffnen,
Datenlesung → armieren) als `/BUSRQ`-Gate ist der aussichtsreichste Weg; er ersetzt die
Verfallsheuristik.  ⚠ Ein strikter Umbau des „gehaltenen Bus" ist laut Notiz
`project_per_byte_busrq_model` bereits einmal als **verifizierte Sackgasse** markiert
worden — vorher dort nachlesen.

---

## 9. Absicherung beim Weiterarbeiten

- `ctest --test-dir build -j8` — **796/796** grün, inklusive der 15
  `format_integration`-Tests.  `./build/a5120emu_test` — 58/58.
- Direkte Wächter dieses Themas: `K5122Test.FormatWrite_LeseStrobeCommittetSpurSofort`
  (schnell) sowie `format_blank_disk_with_verify` + `format_blank_disk_with_verify_p78`
  (je ~130 s, laufen parallel).
- Boot-kritisch und besonders im Auge zu behalten: `test_boot_integration` (CP/A- und
  UDOS-Kaltstart, Boot von B:/C:), `ScpxInit.InitFormatsDriveAWithNoBadTracks`,
  `UdosFormat.*`.

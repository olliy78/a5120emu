# `Fehler 'U' SPUR DEFEKT` beim Formatieren leerer Spuren — Analysestand

**Status:** offen.  **Stand:** 2026-08-06, HEAD = `4740b98`.
**Zweck:** Übergabe an eine neue Sitzung.  Jede Aussage hier ist entweder **gemessen**
(mit Zahl und Reproduktionsweg), **belegt** (Handbuch- oder Quelltextstelle) oder
ausdrücklich als **Vermutung** markiert.  Widerlegte Hypothesen stehen in §6 — bitte
nicht erneut durchlaufen.

---

## 1. Symptom

Formatiert man unter **CP/A** mit `FORMAT.COM` (V19.05.89) eine **komplett unformatierte**
Diskette **mit Vergleichs-Lesen**, meldet das Programm an zufälligen Stellen
`Fehler 'U' SPUR DEFEKT!`.  Der Lauf bricht nicht ab, die Diskette wird fertig
formatiert — die betroffenen Spuren gelten aber als defekt.

`'U'` ist laut `docs/format.md` ein **Timeout** (dort in §9 bei den bekannten
Format-Ausfällen: „`5` (16×256 mit ph. Sektorversatz) → `Fehler 'U'` (Timeout)").

Auf einer bereits formatierten Diskette tritt es nicht auf — auch nicht, wenn das
vorhandene Format ein *anderes* ist (vom Anwender geprüft mit 5×1024, 16×256, 9×512;
9 Durchläufe ohne Fehler).  **Auf echter Hardware tritt der Fehler nicht auf.**

### Referenzmessung auf HEAD

Voller Lauf, Format 1, Spur 0–159, mit Verify, B: = frisch angelegte Leerdiskette:

| Anlaufphase (`boot`-Wert vor der Uhrzeit) | Ergebnis | betroffene Spuren |
|---|---|---|
| 76 | `FORMATIEREN beendet`, 2× `'U'` | 71, 78 |
| 78 | `FORMATIEREN beendet`, 2× `'U'` | 5, 123 |
| 80 | `FORMATIEREN beendet`, 0× | — |
| 82 | `FORMATIEREN beendet`, 1× `'U'` | 24 |
| dieselbe Phase, B: **vorformatiert** (cpa800) | `FORMATIEREN beendet`, **0×** | — |

Die Fehlerstellen hängen allein an der Anlaufphase — es ist ein Wettlauf, keine feste
Spur.

---

## 2. Reproduktion

```sh
S=/tmp/fmt && mkdir -p $S
cp disks/cpadisk_autofs_noclk_noautoexec.img $S/A.img

cat > $S/full.script <<'EOF'
boot 80
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
  (`A5120Machine::createDisk` mit leerem Formatnamen).  Ein Katalogname
  (z. B. `cpa800`) legt stattdessen vorformatiert an — das ist die Gegenprobe.
- Andere Anlaufphase = erste Zeile `boot 80` auf 76/78/82 ändern.
- `boot 8000` ist das Taktbudget nach dem Start; der Lauf dauert ~2–4 min.
- `FD_LOGLEVEL=info` schaltet die K5122-Meldungen (`>>> READ`, `>>> FORMAT-WRITE`,
  `8212 write=`) zu.

**Wichtig:** ein kurzer Spurbereich reicht **nicht** — mit `bis Spur 4` oder `20` läuft
auch der fehlerhafte Fall durch.  Der Guard `format_blank_disk_with_verify`
(CMakeLists, Label `format_integration`) deckt deshalb alle 160 Spuren ab.

---

## 3. Was HEAD tut (Ausgangslage des Codes)

`K5122::startReadTransfer()` (`core/cards/k5122/k5122.cpp`, Zweig `if (ibm_track.empty())`)
behandelt eine unbeschriebene Spur so:

- Es wird ein Strom aus `kUnformattedTrackBytes` (= 6250) Bytes `0x4E` **ohne Marken**
  armiert, `transferring_ = true`, `stream_has_marks_ = false`.
- An der `/STR`-Flanke (`handleCtrlPortAWrite`) wird wie bei jeder Leseanforderung
  `byte_ready_` gesetzt und `/BUSRQ` assertiert.
- **`advanceByteClock()` (`k5122.h`) lässt die Anforderung nach 2 Byteperioden
  verfallen**, wenn niemand sie abholt: `byte_ready_ = false`, `/BUSRQ` frei,
  `head_pos_` um ein Byte weiter.  Nur für Lesen ohne Marken (`!write_mode_ &&
  !stream_has_marks_`).

Netto liefert eine markenlose Spur damit Bytes **nur jede zweite Byteperiode**, und
`/BUSRQ` pendelt.  Das ist der Kompromiss aus Commit `69551f0` — er verhindert den
Totalhänger (§5), erzeugt aber genau die halbe Byte-Rate, die als Timeout-Ursache in
Frage kommt (§7).

---

## 4. Belegte Grundlagen

### 4.1 K5122-Handbuch (`doc/trascripted/Floppy Anschlußsteuerung K 5122.md`)

- **§5.5 „Der Ablauf des Lesens"**: der Lesedatenpfad ist vom **Marken-FF** getaktet.
  Erst wenn eine Marke erkannt ist (MKE = 1), wird der Bitzähler A13 gesetzt, es
  entsteht `/BSTB` und „die Übernahme des Datenbytes […] in den Daten-PIO" erfolgt.
  Ende der Übertragung durch Rücksetzen des Marken-FF (`/MR`) oder Abschalten von `/STR`.
- **§5.6.1**: „BUSRQ wird gebildet, wenn der Daten-PIO zum Datenaustausch mit dem Bus
  bereit ist."  Wahrheitstabelle über `/ARDY`/`/BRDY` (1-aus-8-Decoder A3.3): bei
  `/ARDY = 0 ∧ /BRDY = 0` (Ausgang 00) entsteht **kein** `/BUSRQ`.
- **§5.7**: „Das nächste Byte wird in den Daten-PIO übernommen, **bevor** die CPU die
  Daten abgefordert hat" — Überlauf, gemeldet über `/FA`.  Der Separator wartet also
  nicht auf den Abholer.
- **§4.1, Steuer-PIO Tor B**: `B₇ = /TRACK 00 = /TO`, **Eingang vom Laufwerk**
  (mechanischer Endlagenschalter, unabhängig von der Diskette).

### 4.2 CP/A-BIOS (`disks/cpadisk_autofs_noclk_noautoexec.prn`)

| Adresse | Label | Bedeutung |
|---|---|---|
| `E392` | `fl.sto` | setzt `fl.zto` (Zähler für Indexpunkte) aus Register A |
| `E3A3` | `fl.zto` | der Zähler selbst (selbstmodifizierender Operand) |
| `E39A` | `itimeo` | **Index-ISR**: `dec a / ld (fl.zto),a`; bei 0 → `pretx` |
| `E3AA` | `pretx` | `jr pret3`, während eines Transfers auf `jr pret4` gepatcht |
| `E3AB` | `pretx+1` | Sprungdistanz: `0x00` = Transfer läuft, `0x0B` = Leerlauf |
| `E3AC` | `pret4` | Timeout **während** Transfer → CPU 2 stoppen, `dtrret` = „keine Marke gefunden" |
| `E3B7` | `pret3` | → `headup` |
| `E3BD` | `headup` | `OUT (11H),3` = Index-Interrupt **sperren**; `OUT (18H),0FFH` = Motor aus |
| `E12D` | `drv0a` | Laufwerkswahl: `pretx+1 = 0`, `fl.sto(255)` |
| `E2C9` | — | vor dem Transfer: `fl.sto(5)` „Überwachung auf fehlende Marke" |
| `E2D6` | `fehret` | Operationsende: `pretx+1 = 0x0B`, `fl.sto(20)` = 4-s-Leerlauf-Motorstopp |
| `E22C` | `phrw` | Kopfstabilisierung, 50 ms, kalibriert auf `189·13 = 2457 Takte/ms` |

### 4.3 FORMAT.COM (aus dem RAM disassembliert, Ladeadresse 0x1D00)

```
ZVE1  1D0B  LD A,A5H / OUT (10H),A   ; Lesen armieren (/STR aktiv, /WE=1)
      1D0F  LD A,7FH / OUT (17H),A   ; Daten-PIO Tor B auf Eingabe
      1D13  IN A,(16H)               ; genau EIN Byte — wird von LD A,B4H verworfen
      1D19  LD A,B4H / OUT (10H),A   ; danach Schreiben armieren (/WE=0)
      1D1F  LD A,83H / OUT (11H),A   ; Index-Interrupt scharf
      1D21  JR 1D21                  ; Wartepark
ZVE2  1D44  LD A,(1C45H) / LD D,A    ; Gap-Füllbyte
      1D48  LD E,4EH / LD A,BDH / OUT (10H),A
      1D4E  OUT (C),E / JR 1D4E      ; Gap schreiben (C = 14H = Daten-PIO Tor A)
```

Das Byte bei `1D13` ist ein **Dummy-Read**; sein Wert wird nicht ausgewertet.

---

## 5. Vorgeschichte (bereits behobene, verwandte Fehler)

Diese Fixes sind committet und dürfen nicht rückgängig gemacht werden:

| Commit | Inhalt |
|---|---|
| `1c22af7` | `/TO` ist ein Laufwerkssignal (`DriveProfile::present`), nicht an die Diskette gekoppelt; kein `/BUSRQ` ohne Datenträger.  Behob: Diskette zur Laufzeit einlegen und unter UDOS formatieren (`ERROR C2`). |
| `22e2bbc` | Marken-Gatung eingeführt (`stream_has_marks_`).  Behob: Totalhänger bei ZVE1=`1D0F` / ZVE2=`1D4E`, `/BUSRQ` gehalten. |
| `69551f0` | Verfall der unabgeholten Anforderung nach 2 Byteperioden (§5.7).  Behob: Hänger bei ZVE1=`1D21`, Motor aus. |
| `3fd6414` | `parseTrack` maskiert das 2-Bit-Größenfeld → kein `std::invalid_argument`-Abbruch mehr. |
| `4740b98` | HFE-Überabtastung am Inhalt statt an der Header-Bitrate erkennen. |

---

## 6. Widerlegte Hypothesen — bitte nicht wiederholen

| # | Hypothese | Widerlegung |
|---|---|---|
| 1 | Index-Puls kommt zu oft / falsche Rate | `indexPeriodCycles = 2 450 000·60/300 = 490 000` Takte = 0,2 s = **genau ein Puls pro Umdrehung**.  Gerechnet und im Code verifiziert. |
| 2 | Index-Phase wird durch Laufwerkswechsel zerstört | Zähler der Resets in `!motorAtSpeed`-Zweig während eines Formatierlaufs: **0**. |
| 3 | Motor-Spin-up kostet Umdrehungen | `K5122_MOTOR_SPINUP_MS = 2` (k5122.h). Vernachlässigbar. |
| 4 | Unsere CPU-Taktbilanz ist zu langsam | Die BIOS-eigene kalibrierte 50-ms-Schleife (`phrw`, Soll 122 500 Takte) läuft in **128 078** Takten = **+4,6 %**, erklärt durch die mitlaufenden ISRs.  94 Messungen. |
| 5 | Das Füllbyte des Leerstroms (`0x4E`) ist falsch | Auf `0xAA` umgestellt (= echter Wert, s. §7.2) und vier Phasen gemessen: **2/2/0/1 defekte Spuren — identisch** zur `0x4E`-Referenz. |
| 6 | §5.5 wörtlich umsetzen (markenlos ⇒ gar kein Byte, kein `/BUSRQ`) | Vier Phasen gemessen: **4 von 4 frieren ein** (Spur 124/18/96/92), kein Lauf kommt durch.  Deutlich schlechter als HEAD. |
| 7 | Verfallsfrist der Anforderung tunen | 2 Byteperioden (HEAD, gemessen ab `byte_ready_`): funktioniert.  Entkoppelter Zähler mit Schwelle 2 → **Einfrieren schon bei Spur 0**; Schwelle 16 → **20 defekte Spuren** pro Lauf.  Nicht monoton, reines Symptomtuning. |
| 8 | Der SCPX-Mini-Stack-Guard verzögert Interrupts (`SP ∈ [0xEC00,0xEC10]`, a5120.cpp) | Instrumentiert: **null Treffer** während des Formatierlaufs. |
| 9 | Zu wenig Index-Pulse ⇒ BIOS-Wachhund verhungert | Pro Spur werden **6–11** Index-Pulse erzeugt; der markenlose Abbruch läuft über `pret4` **korrekt** ab (Ereignis-Trace §7.3). |

---

## 7. Gemessene Fakten

### 7.1 Zeitbudget pro Spur

Index-Pulse pro Spur, nach Kartenzustand aufgeschlüsselt (gezählt an
`commitFormatTrack`; Phase = `write_mode_` / `transferring_ && stream_has_marks_` /
`transferring_ && !stream_has_marks_` / sonst):

| Fall | idle | markenlos | Verify | Schreiben | Summe |
|---|---|---|---|---|---|
| leere Diskette | 0–2 | 5–9 | 0 | 2 | 7–13 |
| vorformatiert, 1. Durchlauf | 1–5 | 0 | 0–1 | 2 | 3–8 |
| vorformatiert, 2. Durchlauf | 5 | 0 | 0–1 | 2 | **7** |

Auf der **leeren** Diskette dazu gemessen: 3–11 `startReadTransfer`-Aufrufe und
3,0–5,5 Mio. Takte pro Spur.

**Wichtig:** auch der *funktionierende* Fall braucht 7 Umdrehungen pro Spur.  Zum
Vergleich hat der Anwender UDOS und SCPX gemessen: dort ~2 Umdrehungen pro Spur, auch
auf leeren Disketten.  Für CP/A liegt **keine** Messung an echter Hardware vor.

Zeitanteile im Formatierfenster (PC-Histogramm, jede 1024. Instruktion, nach
Kartenzustand): **idle 87 %**, Lesen 4,5 %, Schreiben 8,4 %.  Heißeste Stellen im
Leerlauf: `E37D–E389` (Software-CRC des BIOS) ~4,6 %, `E6CC/E6CD`
(`iodis1: rrca/djnz`, I/O-Byte-Dispatch der Konsole) ~3,1 %, `E230` (Kopfstabilisierung)
~1 %.  Kein einzelner dominanter Wartepunkt.

> **Vermutung (nicht belegt):** die 7 Umdrehungen könnten für CP/A echt sein, weil es
> pro Spur mehr tut als UDOS/SCPX (Software-CRC über jeden Sektor beim Vergleichs-Lesen,
> Fortschrittsausgabe).  Das ließe sich nur mit einer Stoppuhr an echter Hardware
> klären — z. B. Sekunden für „Spur 0 bis Spur 20 mit Verify".

### 7.2 Inhalt einer echten Leerspur

Aus `disks/leer_scp.dmk` (mit Greaseweazle von einer echten leeren Diskette eingelesen,
80 Spuren × 2 Seiten, `track_len = 7668`):

```
Spur 0 / 1 / 40:  IDAM-Einträge = 0,  7540 Rohbytes,  davon 7540 × AA
```

Eine echte leere Spur liefert also **einen durchgehenden Bytestrom ohne jede
Adressmarke**.  Das stützt das HEAD-Modell (Bytes ja, Marken nein) und spricht gegen
die wörtliche §5.5-Lesart (Hypothese 6).

> Die zugehörige `disks/leer_scp.hfe` meldet im Header `bitrate = 311` kbit/s, hat aber
> 15 097 Bytes je Spurseite = 120 776 Zellen pro Umdrehung (nominal 50 000) — also ~2,4-fach
> überabgetastet und **kein ganzzahliger Faktor**.  Seit `4740b98` wird die Überabtastung
> am Inhalt bestimmt (Kandidaten 1–4); bei dieser markenlosen Diskette greift das nicht
> und sie gilt korrekt als unformatiert.  Für einen *formatierten* Mitschnitt mit
> demselben Setup wäre ein nicht-ganzzahliger Faktor weiterhin ein Problem.

### 7.3 Ereignisfolge des BIOS-Wachhunds

Trace der BIOS-Stellen aus §4.2 pro Spur (gemessen, `pretx`/`fl.zto` mitgelesen):

```
drvsel    pretx=00  fl.sto(255)      ; Laufwerkswahl: Motorabschaltung gesperrt
fl.sto(5)                            ; „Überwachung auf fehlende Marke"
itimeo ×5   zto 5→1                  ; fünf Umdrehungen ohne Marke
pret4                                ; KORREKTER Abbruch „keine Marke gefunden"
fehret    pretx=0B  fl.sto(20)       ; Operationsende → 4-s-Leerlauf-Motorstopp scharf
itimeo    zto 20→19→18→17…           ; Leerlauf-Countdown
drvsel    pretx=00                   ; nächste Operation → wieder gesperrt
```

Die 5 Umdrehungen des markenlosen Lesens sind also **das Budget des OS selbst** und
kein Emulatorfehler.  Verteilung der `fl.sto`-Argumente über einen Lauf auf der leeren Diskette: `5`, `20`
und `255` — je 798 Aufrufe.

### 7.4 Signatur des (behobenen) Totalhängers

Nur zur Einordnung — auf HEAD tritt das **nicht** mehr auf.  Gemessen mit der
§5.5-Variante (Hypothese 6):

```
ZVE1 PC=1D21   ZVE2 PC=1D4E   /BUSRQ frei
pretx+1[E3AB] = 0x0B   (Leerlauf)      fl.zto[E3A3] = 0x00
```

`fl.zto` lief im **Leerlaufzustand** ab → `pret3` → `headup` → Index-Interrupt gesperrt
und Motor aus → ZVE1 wartete bei `1D21` auf einen Puls, den es nicht mehr gab.

---

## 8. Offener Kern

Alles Obige zusammen zeigt auf **eine** Stelle:

`A5120Machine::run` (`core/machines/a5120/a5120.cpp`, Zeile ~562) enthält

```cpp
continue;   // Verriegelung: ZVE1 läuft NICHT während /BUSRQ
```

FORMAT.COM armiert bei `1D0D` einen Lesevorgang und will das Byte bei `1D13`
**selbst** abholen — währenddessen sitzt ZVE2 in seiner Gap-Schreibschleife (`1D4E`)
und ist gar nicht der Abholer.  Unsere Arbitrierung gibt den Bus bei `/BUSRQ` aber
immer an ZVE2.  Genau deshalb braucht HEAD den künstlichen Verfall der Anforderung —
und der halbiert die Byte-Rate auf markenlosen Spuren, was als Timeout-Ursache
naheliegt.

**Zu klären ist:** wie kommt ZVE1 auf echter Hardware in diesem Fenster an sein Byte?
Drei Möglichkeiten, keine davon geprüft:

1. `/BUSRQ` entsteht dort gar nicht, weil beide Richtungen des Daten-PIO armiert sind
   (ZVE2 schreibt auf Tor A, der Lesekanal liegt auf Tor B).  Der 1-aus-8-Decoder A3.3
   hat für „beide bereit" keinen `/BUSRQ`-Ausgang (§5.6.1) — die Wahrheitstabelle listet
   nur 00/01/02.  **Das ist der aussichtsreichste Kandidat**, erfordert aber ein Modell
   von `/ARDY`/`/BRDY` statt der heutigen `transferring_`/`write_mode_`-Flags.
2. Die Verriegelung ist zu streng: der Z80 gibt den Bus erst nach dem laufenden
   Maschinenzyklus frei; FORMAT könnte darauf bauen.  ⚠ Ein strikter Umbau des
   „gehaltenen Bus" ist laut Notiz `project_per_byte_busrq_model` bereits einmal als
   **verifizierte Sackgasse** markiert worden — vorher dort nachlesen.
3. Unsere ZVE1/ZVE2-Verschränkung lässt ZVE2 zu früh in die Gap-Schleife laufen.

---

## 9. Absicherung beim Weiterarbeiten

- `ctest --test-dir build -j8` — **795/795** müssen grün bleiben, inklusive der 14
  `format_integration`-Tests.  `./build/a5120emu_test` — 58/58.
- `format_blank_disk_with_verify` (Label `format_integration`, ~150 s) ist der direkte
  Guard für dieses Thema: Leerdiskette, alle 160 Spuren, mit Verify, erwartet
  `FORMATIEREN beendet` **ohne** `SPUR DEFEKT`.  **Er ist heute grün, obwohl der Fehler
  auftritt** — weil `SPUR DEFEKT` in 1 von 4 Phasen ausbleibt.  Wer hier arbeitet,
  sollte die vier Phasen aus §1 einzeln fahren, nicht nur den Guard.
- Boot-kritisch und daher besonders im Auge zu behalten: `test_boot_integration`
  (CP/A- und UDOS-Kaltstart, Boot von B:/C:), `ScpxInit.InitFormatsDriveAWithNoBadTracks`,
  `UdosFormat.*`.
- Werkzeuge: `tools/format_driver` (Tastatur-Skript, siehe §2), `tools/k1520dbg`,
  `tools/boot_trace`.  Für Messungen an BIOS-Adressen eignet sich ein
  `machine.setCpuTraceCallback(...)` in `format_driver` (temporär, nicht committen).

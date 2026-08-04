# UDOS-Bug 1: Schreibzugriffe zerstören alle Sektorkontrollblöcke der Spur

**Gefunden:** 2026-08-04 · **Status: ✅ BEHOBEN 2026-08-04** · **Schwere:** hoch (jeder
Schreibzugriff auf eine UDOS-Diskette machte Dateien unlesbar) · **Betroffen:** nur
Fremdformate mit Daten hinter der Daten-CRC — **UDOS**. CP/A und SCPX waren nicht betroffen.

> **Behoben wie in §6 vorgeschlagen** — beide Teile:
> `TrackCodec::buildTrack()` gibt jetzt `LogicalSector::tail` aus, und
> `K5122::commitWriteField()` übernimmt den frisch geschriebenen Kontrollblock aus dem
> Schreibstrom. Der gemessene Offset (§6.2, damals offen) ist bestätigt: **Datenfeld +
> 2 CRC-Bytes**, dahinter die 4 Zeigerbytes.  Nachweis, Regressionstests und die
> Restunsicherheit stehen in [§10](#10-stand-nach-dem-fix).

Dies ist das **Spiegelbild** des in `doc/analyse_udos.md` §13.4 behobenen Lesefehlers:
damals ging der Sektorkontrollblock im *Lese*-Stream verloren, jetzt geht er im
*Schreib*-Pfad verloren. Der Hintergrund zum Format steht in
`doc/udos_diskettenformat.md` §1.1.

---

## 1. Was UDOS erwartet

UDOS/ZDOS legt die Verkettung seiner Dateien **nicht im Sektordatenfeld** ab, sondern in
**4 Bytes unmittelbar hinter der Daten-CRC**, also im Gap:

```
A1 A1 A1 FB │ 128 Datenbytes │ CRC CRC │ bb bb ff ff │ Gap…
                                          └ zurück ┘└ vor ┘     FF FF = Dateiende
```

Ein Zeiger ist `(Sektorindex 0-basiert, Spur)`. Ohne diese vier Bytes ist eine Datei
nicht mehr auffindbar — es gibt keine FAT, aus der man sie rekonstruieren könnte.

---

## 2. Symptom

Ein **einziger** Schreibzugriff macht die betroffene Datei unlesbar:

```
%SET PROPERTIES OF CODE TO R
%CAT CODE P=& F=L
                              RECORD                 DATE OF
 FILENAME            D  T  COUNT LENG. PROP ENTRY CREAT.  MOD.

CODE                 0  P     2  0080       4000  870413 900808
CODE                 4                 ***   OPEN ERROR CA
%ERROR CA
CA: POINTER CHECK ERROR
```

`CODE` liegt auf beiden Seiten; geschrieben wurde die Fassung auf Laufwerk 4 (Seite 1),
und genau die ist danach kaputt. Auch `COPY` scheitert:
`I/O ERROR CA ON UNIT 06 / PROGRAM ABORT`.

Die Fehlerkennung stammt aus UDOS selbst (`ERROR`-Kommando der Diskette):
**`CA` = POINTER CHECK ERROR**.

---

## 3. Reproduktion

```sh
cp disks/udos_boot_scp.hfe /tmp/udos_rw.hfe

cat > /tmp/wr.dbg <<'EOF'
gscreen "Neues Datum" 60000000
g 2000000
keys 150388
g 8000000
logpoint 0x0BEB A
keys cat\scode\sp=&\sf=l\r
g 40000000
keys set\sproperties\sof\scode\sto\sr\r
g 40000000
keys cat\scode\sp=&\sf=l\r
g 40000000
q
EOF

tools/dev.sh tool k1520dbg /tmp/udos_rw.hfe --rw -x /tmp/wr.dbg \
  | rg -o 'A=([0-9]+)\(' -r '$1' \
  | python3 -c "import sys;print(''.join(chr(int(l)) for l in sys.stdin.read().split()).replace('\r','\n'))"
```

Die zweite `CAT`-Ausgabe zeigt `OPEN ERROR CA`.

> ⚠ **Die Tastatur ist invertiert** — Kommandos *klein* tippen, UDOS macht daraus
> Großschrift (`doc/analyse_udos.md` §14.2).

---

## 4. Beweis am Medium

Vergleich der Abbilder vorher/nachher (Sektoren samt der Bytes hinter der Daten-CRC).
UDOS hat zwei Spuren beschrieben — Seite 1, Spur 17 (der Kopfsektor von `CODE`) und
Seite 1, Spur 23 (die Belegungskarte). Auf **beiden** sind **alle 26** Kontrollblöcke
zu Gap-Füllbytes geworden, nicht nur der eine geschriebene Sektor:

```
vorher                                        nachher
C17 h1 S01 tail=16 11 0E 11 41 F1 09 09       C17 h1 S01 tail=4E 4E 4E 4E 4E 4E 4E 4E
C17 h1 S02 tail=18 11 0F 11 41 F0 E4 E4       C17 h1 S02 tail=4E 4E 4E 4E 4E 4E 4E 4E
C17 h1 S03 tail=19 11 10 11 41 F4 E4 E4       C17 h1 S03 tail=4E 4E 4E 4E 4E 4E 4E 4E
…  (alle 26 Sektoren, ebenso Spur 23)
```

**Die Nutzdaten selbst sind korrekt.** Der Kopfsektor von `CODE` (Seite 1, Spur 17,
Sektor 8) hat sich exakt so geändert, wie er soll:

```
vorher : … 80 02 00 80 00 80 00 00 00 40 80 00 38 37 30 34 31 33 FF 00 …
nachher: … 80 02 00 80 00 80 00 08 00 40 80 00 38 37 30 34 31 33 FF 00 …
                                  ↑ +19 Eigenschaften: R-Bit (0x08) gesetzt
```

Es ist also **ausschließlich** der Kontrollblock betroffen.

---

## 5. Ursache

`K5122::commitWriteField()` — `core/cards/k5122/k5122.cpp:1258 ff.`:

```cpp
auto sektoren = TrackCodec::parseTrack(spur);      // füllt sec.tail korrekt (8 Bytes)
…
ziel->data.assign(write_buf_.begin() + data_start,
                  write_buf_.begin() + data_start + take);
ziel->data.resize(ziel->size, 0x00);
…
spur = TrackCodec::buildTrack(sektoren, spur.encoding);   // ← hier gehen alle tails verloren
```

`TrackCodec::buildTrack()` — `core/peripherals/floppy_drive/track_codec.cpp:234` —
schreibt hinter die Daten-CRC unbesehen den Gap:

```cpp
        for (uint8_t b : sec.data) push(b);
        push(static_cast<uint8_t>(dataCrc >> 8));
        push(static_cast<uint8_t>(dataCrc & 0xFF));
    }

    fill(gaps.gap_fill, gaps.gap3);      // ← sec.tail wird nie ausgegeben
```

`buildFaithfulReadTrack()` wurde bei §13.4 nachgezogen und macht es richtig
(`track_codec.cpp:456-462`) — `buildTrack()` nicht. Da `commitWriteField()` die **ganze
Spur** neu aufbaut, verlieren auch die 25 nicht angefassten Sektoren ihren Kontrollblock.

### 5.1 Zweiter, unabhängiger Teil des Fehlers

Selbst mit korrigiertem `buildTrack()` behielte der geschriebene Sektor seinen **alten**
Kontrollblock, denn `commitWriteField()` verwirft alles jenseits der Nutzdaten
(`k5122.cpp:1247`):

```cpp
const size_t avail = write_buf_.size() - data_start;
const size_t take  = std::min<size_t>(wr_size_, avail);   // schneidet den Rest ab
```

Der neue Kontrollblock **liegt im Puffer** — gemessen mit `K1520DBG_LOGLEVEL=info`:

```
>>> WRITE D0 C=17 H=1 S=8 bytes=128 (buf=152)
>>> WRITE D0 C=23 H=1 S=1 bytes=128 (buf=152)
```

152 Bytes im Puffer, 128 davon Nutzdaten. Bei `data_start = 16`
(12×`00`-Sync + 3×`A1` + `FB`) bleiben **8 Bytes hinter dem Datenfeld** — Platz für
2 CRC-Bytes + den 4-Byte-Kontrollblock + 2 weitere. Die genaue Lage ist mit einem
einmaligen Hexdump von `write_buf_` in `commitWriteField()` in einer Minute geklärt.

---

## 6. Vorgeschlagener Fix

### 6.1 `buildTrack()` gibt `sec.tail` aus

Symmetrisch zu `buildFaithfulReadTrack()` (`track_codec.cpp:456-462`), das genau so
schon funktioniert:

```cpp
        // Nachspann wie auf dem Medium (LogicalSector::tail, kSectorTailBytes = 8).
        // Standard-IBM: 8× Gap 0x4E — bitgleich zum bisherigen fill().
        for (size_t i = 0; i < kSectorTailBytes; ++i)
            push(i < sec.tail.size() ? sec.tail[i] : gaps.gap_fill);
    }
    fill(gaps.gap_fill, gaps.gap3 - kSectorTailBytes);   // Gap 3 entsprechend kürzen
```

⚠ `gap3` ist per Default **24** (`track_codec.h:64`), `kSectorTailBytes` ist **8** — die
Kürzung geht auf, aber ein `assert`/Klemmen auf `>= 0` ist angebracht, falls jemand
`GapParams` mit kleinerem `gap3` benutzt.

**Warum das CP/A und SCPX nicht berührt:** Bei einer Standard-IBM-Spur ist `sec.tail`
genau 8× `0x4E` — die erzeugten Bytes sind **bitgleich** zum heutigen
`fill(0x4E, 24)`. Das ist dasselbe Argument, mit dem der Lesepfad in §13.4 abgesichert
wurde. Vorsicht ist nur dort geboten, wo `buildTrack()` auf Sektoren mit **leerem**
`tail` läuft (frisch erzeugte `LogicalSector`, z. B. `DiskImage::create`,
`mk_disk_template`, Formatier-Pfad) — deshalb der Fallback auf `gaps.gap_fill`, der das
alte Verhalten exakt reproduziert.

### 6.2 `commitWriteField()` übernimmt den neuen Kontrollblock

```cpp
ziel->data.assign(…);
ziel->data.resize(ziel->size, 0x00);

// Bytes hinter dem Datenfeld: <2 CRC> <4 Kontrollblock> … — der schreibende OS-Treiber
// legt hier den Sektorkontrollblock ab (UDOS). Unverändert nach ziel->tail übernehmen,
// sonst behält der Sektor seine alte Verkettung.
const size_t after = data_start + take + 2 /* CRC */;
if (write_buf_.size() > after) {
    ziel->tail.assign(write_buf_.begin() + after,
                      write_buf_.begin() + std::min(write_buf_.size(),
                                                    after + kSectorTailBytes));
}
```

Ob die `+ 2` für die CRC stimmt (schreibt UDOS die CRC selbst oder erzeugt sie der
Emulator?), ist vor dem Übernehmen **zu messen** — s. §5.1. Solange der Offset unsicher
ist, ist es besser, den Fix in 6.1 allein zu machen: dann bleibt wenigstens die
Verkettung der 25 unbeteiligten Sektoren erhalten.

---

## 7. Prüfen, ob der Fix wirkt

**Positivtest** — die Reproduktion aus §3 muss danach zweimal dieselbe Zeile liefern:

```
CODE                 4  P     2  0080  R    4000  870413 880315
```
(`R` gesetzt, Änderungsdatum auf das Systemdatum aktualisiert — beides bereits als
korrekt nachgewiesen, s. §4.)

**Medientest** — die Kontrollblöcke der nicht angefassten Sektoren müssen unverändert
bleiben:

```sh
# vorher/nachher je ein Sektorabbild ziehen und die tails vergleichen
# (HfeImage::readTrack + TrackCodec::parseTrack, s. doc/udos_diskettenformat.md §11.1)
```

**Regressionsnetz** — `buildTrack()` ist breit benutzt, deshalb die volle Suite:

```sh
tools/dev.sh test          # 728 ctest + 58 Legacy-Harness
tools/dev.sh test-format   # 8 langsame format_integration-Tests
```

Besonders im Auge behalten: `test_track_codec`, `test_disk_image_raw`,
`test_hfe_image`, `test_boot_integration`, `ScpxInit.*`,
`FormatParser*` und die `format_integration`-Tests — sie alle laufen über
`buildTrack()`.

**Neuer Guard** — sinnvoll wäre ein Test in `tests/cpp/test_track_codec.cpp`:
`parseTrack(buildTrack(sektoren)) == sektoren` **einschließlich `tail`**, einmal mit
Standard-IBM-Tails (8×`0x4E`) und einmal mit einem UDOS-artigen Kontrollblock.

---

## 8. Bis dahin

**UDOS-Disketten im Emulator nur lesen.** Booten, `CAT`, `EXTRACT`, `STATUS`, Programme
starten — alles funktioniert einwandfrei. Nur Kommandos, die auf die Diskette schreiben
(`SET …`, `COPY`, `MOVE`, `DELETE`, `FORMAT`), beschädigen sie.

Für Werkzeuge, die UDOS-Abbilder **außerhalb** des Emulators erzeugen, ist der Fehler
irrelevant — solche Abbilder lassen sich normal booten und lesen.

---

## 9. Querverweise

* `doc/udos_diskettenformat.md` §1.1 (Kontrollblock), §12 (dieselbe Sache in Kurzform)
* `doc/analyse_udos.md` §13.4 — der bereits behobene Lesepfad-Zwilling, inklusive der
  Begründung, warum das Durchreichen der Tail-Bytes CP/A und SCPX nicht verändert
* `core/peripherals/floppy_drive/track_codec.h:47-53` — `LogicalSector::tail` und
  `kSectorTailBytes`, dort ist der UDOS-Fall bereits im Kommentar beschrieben


---

## 10. Stand nach dem Fix

**Code** (beide Teile aus §6, unverändert übernommen):

* `core/peripherals/floppy_drive/track_codec.cpp` — `buildTrack()` gibt hinter der
  Daten-CRC `min(kSectorTailBytes, gap3)` Bytes aus `sec.tail` aus und kürzt Gap 3
  entsprechend; fehlende Bytes fallen auf `gaps.gap_fill` zurück. Für Sektoren **ohne**
  `tail` (frisch erzeugt: `DiskImage::create`, `parseFormatStream`) ist das Ergebnis
  bitgleich zu vorher — deshalb ändert sich für CP/A, SCPX und alle Formatier-Pfade nichts.
* `core/cards/k5122/k5122.cpp` — `commitWriteField()` übernimmt
  `write_buf_[data_start+take+2 …]` nach `ziel->tail`.

**Der Schreibstrom, gemessen** (UDOS 4.3, 128-B-Sektor, `buf=152`, `data_start=16`):

```
[12×00 Sync][A1 A1 A1][FB][128 Daten][CRC CRC][bb bb ff ff][41 FF]
                           └ +16          └ +144    └ +146 Kontrollblock
```

Der schreibende Treiber liefert die 2 CRC-Bytes selbst mit (der Emulator rechnet sie in
`buildTrack` ohnehin neu); die 4 Zeigerbytes stehen unmittelbar dahinter. Die `41 FF`
sind die Schreibnaht (§13.5 „nicht als CRC eingeordnet").

**Positivtest §7 — erfüllt.** Die Reproduktion aus §3 liefert jetzt:

```
CODE                 0  P     2  0080       4000  870413 900808
CODE                 4  P     2  0080  R    4000  870413 880315
    69 FILES EXAMINED
```

`R` gesetzt, Änderungsdatum auf das Systemdatum, **kein** `OPEN ERROR CA`, und alle
69 Dateien sind weiterhin auffindbar.

**Medientest §7 — erfüllt.** Spur 17 / Seite 1 des Referenzdatenträgers nach dem
`SET`: von 26 Sektoren hat sich **genau einer** geändert — der Kopfsektor von `CODE`
(Sektor 8), und dessen Kontrollblock ist inhaltlich unverändert `0A 16 08 11`:

```
vorher   S08  tail=0A 16 08 11 41 F2 12 12
nachher  S08  tail=0A 16 08 11 41 FF 4E 4E
         S01..S07, S09..S26  byteidentisch
```

Die Bytes 5…8 sind Gap/Schreibnaht hinter dem 4-Byte-Kontrollblock (§13.5); UDOS liest
nur die ersten vier (`LD BC,0416H / INIR`).

**Regressionsnetz.** `tools/dev.sh test` 730/730 + 58 Legacy-Harness grün,
`tools/dev.sh test-format` unverändert (der eine rote Test
`ScpxInit.Builds5x1024SystemViaInitModfSyspAndBoots` war vorher schon rot — gegen den
Baseline-Stand geprüft). Neue Guards:

| Test | sichert |
|---|---|
| `TrackCodecTail.BuildParse_ErhaeltUdosKontrollblock` | `parseTrack(buildTrack(…))` erhält `tail` (MFM) |
| `TrackCodecTail.FM_ErhaeltNachspannEbenso` | dasselbe für FM |
| `TrackCodecTail.OhneTail_BitgleichZuReinemGap` | ohne `tail` **bitgleich** zum alten Gap-Verhalten |
| `K5122Test.WriteField_UebernimmtSektorkontrollblock` | Schreibstrom → `tail`, und der **zweite** Schreibzugriff auf dieselbe Spur lässt den Kontrollblock des ersten Sektors stehen (`.hfe`, weil `.img` den Nachspann prinzipbedingt verliert) |

Beide Guards wurden per Mutation geprüft (Fix einzeln zurückgedreht → Test rot).

### 10.1 Was dieser Fix NICHT war

Der zweite UDOS-Schreibfehler lag **nicht** hier, sondern in der Laufwerksauswahl:
Port 18H war im K5122-Modell nibbelvertauscht, sodass UDOS' `AND 0F0H`-Anwahlbyte
(`0xD0` für Laufwerk 1) auf Laufwerk 0 zeigte. Details:
`doc/design/07_k5122_afs.md` §8. Erst beide Korrekturen zusammen machen `FORMAT`
und Schreibzugriffe auf Laufwerk 1 nutzbar.

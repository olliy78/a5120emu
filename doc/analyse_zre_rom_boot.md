# ZRE/K2526 Boot-ROM (Lade-ROM A26) — Funktionsweise & Emulator-Anforderungen

**Stand:** 2026-06-25 (Neuschrieb). Ersetzt die alte, von Entwicklungs-Bug-Historie
geprägte Fassung.

Dieses Dokument beschreibt **wie das ZRE-Boot-ROM funktioniert** und **was der Emulator
leisten muss**, um es treu auszuführen — speziell die **FM/MFM-Umschaltung** und die
**Unterstützung von FM-8"-Laufwerken**.

> **Zeilengenaue Annotation steht in `doc/EPROMS/zre.prn`** (kommentiertes MACRO-80-Listing,
> jede Code-Zeile mit `[ZVE1]`/`[ZVE2]`-Tag und deutschem Kommentar). In `k1520dbg`/`boot_trace`
> via `-l doc/EPROMS/zre.prn` ladbar — dann annotiert sich jede Trace-/Disasm-Zeile selbst.
> Dieses Dokument verweist auf ROM-Adressen; den jeweiligen Opcode + Kommentar liefert die `.prn`.
> Generator: `tools/gen_zre_prn.py` (aus `zre.rom` + `zre_rom.asm`).

---

## 1. Das ROM

- **Lade-ROM A26** der ZRE K2526: **1 KB EPROM, Adressen `0000H–03FFH`** (K2526-Handbuch §6.4,
  `doc/trascripted/Zentrale Recheneinheit K2526 K2527.md`).
- Quelle im Repo: `doc/EPROMS/zre.rom` (in `core/cards/k2526/rom_data.h` einkompiliert).
  Die Code-Bytes sind **byte-bestätigt gegen den realen Chip** (der EPROM-Dump
  `A5120_ZRE_rom.bin` enthält denselben Hauptteil; er ist allerdings selbst um +0x100
  verschoben/korrupt und wird **nicht** verwendet — siehe Erinnerung `project_real_zre_rom_dump`).
- Reset-Einsprung ist `0x0000`. Das ROM lädt die ersten Bootsektoren von Diskette nach
  `0x0400+`, prüft eine Signatur und übergibt an den geladenen Lader.

### Speicher während des Boots

| Phase | `0000–03FF` | Rest |
|-------|-------------|------|
| Power-on | ROM (read); Schreibzugriffe gehen ins dahinterliegende RAM | K3526-RAM |
| nach `LDIR`-Spiegelung (`@00BE`) | ROM kopiert `00BA–03FF` ins RAM | RAM |
| nach ROM-Disable (`OUT(0A) bit0=0 @00D4`) | reines RAM (von beiden CPUs geteilt) | RAM |

---

## 2. Zwei CPUs auf der K2526

Die K2526 trägt **zwei Z80**:

- **ZVE1** — Haupt-CPU. Führt Reset/Init, Laufwerks-Seek, den Handshake und die OS-Übergabe aus.
- **ZVE2** — DMA-CPU. Lädt die Bootsektoren von der Floppy. Startet bei `PC=0x0000`; nach
  ROM-Disable steht dort `RAM[0000]=C3 DD 01 = JP 01DD` → ZVE2 springt sofort in die
  STROBE_LOOP-Leseroutine. ZVE2 läuft nur, solange `/BUSRQ` gehalten wird.

**CPU-Zuordnung der ROM-Adressen** (empirisch via `boot_trace --coverage cpu,pc` bestätigt;
jede `.prn`-Zeile trägt das Tag):

| CPU | ROM-Bereich | Inhalt |
|-----|-------------|--------|
| **ZVE1** | `0000–01DC` + `027E–03FF` | Reset/Init, Drive-Detect/Seek, Index-Wait, Drive-Init, **IM2-Index-ISR `01C7`**, Signatur-Check `01B6`, OS-Übergabe, Alt-Boot, RAM-Vars |
| **ZVE2** | `01DD–027D` | die *eine* STROBE_LOOP-Sektorladeroutine (IDAM-Suche, INI/INIR-Transfer, DONE, Mehrsektor-Reentry) |

Beide koordinieren sich ausschließlich über gemeinsame RAM-Variablen.

### Handshake-RAM-Variablen

| Adresse | Bedeutung | Init |
|---------|-----------|------|
| `03F0/03F1` | Ladeadresse Sektordaten | `0x0400` |
| `03F3` | erwarteter Zylinder | `0` |
| `03F4` | erwarteter Kopf | `0` |
| `03F5` | erwartete Sektor-ID (**1-basiert!**) | `1` |
| `03F6` | erwarteter Size-Code (0=128B) | `0` |
| `03F7` | Index-Puls-/ISR-Timeout-Zähler | `4` bzw. `16` |
| `03F8` | **Done-Flag**: 0=läuft, 1=Timeout, 3=fertig | `0` |
| `03FA` | Seek-/Retry-Zähler | `5` |
| `03FC` | Laufwerks-Select-/Port-0x10-Ctrl (rotiert beim Retry) | `0xEE` |
| `03FD` | **FM/MFM-Pfadbyte** (siehe §4) | `0x87` (FM) |
| `07F2` | Ziel-Sektorzahl (Bootspur) | `4` |

---

## 3. Boot-Ablauf (Überblick)

1. **Reset/Init** (`0000–00C5`): 2 KB RAM löschen, `SP=07E0`, `IM 2`, BS-PIO + K5122-PIOs
   konfigurieren, IM2-ISR-Vektoren in RAM ablegen (`0xB8→007A`, `0xBA→01C7`).
2. **ROM→RAM-Spiegel + ROM-Disable** (`00BE–00D4`).
3. **Laufwerk wählen & Spur 0 suchen** (`0101–0126`): `DRIVE_DETECT_LOOP` pollt Port `0x12`
   (Status); Step-Pulse bis „bereit + Spur 0". `[03FC]` = Laufwerks-Select.
4. **4 Index-Pulse abwarten** (`0128–013E`): jeder Indexpuls → ISR `01C7` → `[03F7]--`.
   Bei Erfolg wird das FM/MFM-Default `0x87` in `[03FD]` abgelegt (`@0137`/`@0153`).
5. **Erwartetes IDAM setzen** (`015C–0162`): Zyl=0, Kopf=0, **Sektor-ID=1**, Size=0.
6. **Drive-Init + ZVE2 starten** (`CALL 0194 @0165`): `[03F7]=16`, `OUT(04h)` gibt ZVE2 aus
   Reset frei, `/STR`-Flanke. `[03F8]=0`.
7. **ZVE1 wartet** (`0168–016A`) auf `[03F8]`.
8. **ZVE2 liest** die Bootspur (STROBE_LOOP `01DD`, §5): sucht je Sektor das IDAM, vergleicht
   Zyl/Kopf/Sektor/Size, überträgt die Daten per INI/INIR nach `[03F0]`, zählt Sektor-ID hoch
   bis `[07F2]=4`. Danach `[03F8]=3` (`@0267`).
9. **ZVE1 prüft** (`016C`→`01B6`): bei `[03F8]=1` (Timeout) → Retry (§4); bei `[03F8]=3` →
   Signatur `RAM[0400]="SYL"` prüfen → **OS-Übergabe** `CALL 0437` (`@0174`).
   Schlägt alles fehl und `[03FC]` ist durchrotiert (=`0x77`) → **alternativer Boot-Pfad** `027E`.

Die Index-Puls-Periode kommt aus der Laufwerksdrehzahl (`DriveProfile::indexPeriodCycles`, rpm),
nicht aus einer Konstante.

---

## 4. FM/MFM-Umschaltung (Kernthema)

Auf realer Hardware unterscheiden sich FM- (single density) und MFM-Spuren (double density)
in **zwei** Dingen:

| | FM | MFM |
|---|---|---|
| **Adressmarke / Präambel** | keine A1-Präambel; Marke = `FE` direkt (FM-Taktmuster) | **3× `A1`**-Sync (fehlendes Taktbit), dann `FE` |
| **Modulation / Datenrate** | Leitungscode FM | Leitungscode MFM, doppelte Bit-Dichte |

Im ROM werden **zwei getrennte Signale** verwendet (K5122-Tor A/B, vgl.
`doc/trascripted/Floppy Anschlußsteuerung K 5122.md`):

| Signal | Port/Bit | Funktion | Im ROM |
|--------|----------|----------|--------|
| **MK** | Port `0x10` (Tor A) **bit1** | wählt im K5122-Markenerkennungs-ROM: FM-Marke vs. MFM-A1-Sync | **variabel** (Trial-and-Error) |
| **/HF** | Port `0x12` (Tor B) **bit2** | Datenrate/Frequenz | **fest** `/HF=1` (`@00DD`, Tor B=`0x7F`) |

### 4.1 MK — die FM/MFM-Markencodierung (wird durchprobiert)

Das **Pfadbyte `[03FD]`** wird `@01AD` nach Port `0x10` geschrieben → Bit1 = Signal **MK**:

- **`0x87` = FM** (bit1=1). Mit der `MK→/MK`-Inversion (offene-Kollektor-Treiber) wählt der
  K5122-Marken-ROM die **FM-Marke**. *Default* (vorgeladen `@0137`).
- **`0x85` = MFM** (bit1=0). Wählt die **MFM-A1-Sync-Erkennung**.
- (Bit7 unterdrückt einen Step-Puls; `0x00` hätte bit7=0 → ungewollter Step.)

Bestätigt durch Robotrons eigene Quelle `K2526ROM.MAC` („85/81 MFM, 87/83 FM").

**Wann umgeschaltet wird — reines Trial-and-Error, NICHT A1-Header-Erkennung:**

```
Default FM (0x87 @0137)
  → CALL 0194 startet ZVE2-Read → ZVE1 wartet @0168 auf [03F8]
     → ZVE2 findet in 4 Indexpulsen KEIN gültiges IDAM
        → Index-ISR setzt [03F8]=1 (Timeout)
           → ZVE1 @016D JR Z 014B → DEC E (init 6)
              → E≠0: @0151 XOR 02h  (MK kippen: FM↔MFM)  → Re-Read
              → E=0:  @0140 BOOT_FAIL → Laufwerks-Select [03FC] rotieren → nächstes Laufwerk
```

Das ROM stellt den K5122 also **blind** auf eine Markenart, lässt ZVE2 lesen, und kippt bei
Misserfolg auf die andere. Die Disk *bestimmt* die Aufzeichnungsart implizit über
Erfolg/Misserfolg, nicht über Header-Auswertung.

Im ZVE2-Lesepfad äußert sich MK so (`.prn` `0205`/`020B`/`025A`):
- **FM (NZ-Pfad)**: Marke `FE` liegt direkt → wenige Bytes bis `FE`.
- **MFM (Z-Pfad)**: erst `A1`(`A1A1`)-Sync → mehr Bytes bis `FE`.

### 4.2 /HF — die Datenrate (fest)

`/HF=1` (niedrige Frequenz) deckt laut K5122-Doku **FM-8" (MF3200/6400) UND MFM-5,25" (MFS)** ab
— beide ≈250 kbit/s. `/HF=0` (hohe Frequenz) = **MFM-8"** (500 kbit/s). Das ROM setzt `/HF`
**fest auf 1** und toggelt es nie. Folge: an dieser ZRE laufen 8"-FM- und 5,25"-MFM-Laufwerke
ohne Umkonfiguration (gleiche Datenrate, Encoding auto-erkannt); ein **MFM-8"-Laufwerk
(bräuchte `/HF=0`) wird von diesem ROM nicht unterstützt**.

### 4.3 CRC — das ROM prüft KEINE

ZVE2 validiert einen Sektor **nur über das IDAM-Feld** (Vergleich Zyl/Kopf/Sektor/Size) und ZVE1
später über die **Boot-Signatur** `@01B6`. Die nachlaufenden INI `@0245/0247` **lesen die
Daten-CRC-Bytes nur ein** (in den Scratch `0x0700`), prüfen sie aber nicht. Die eigentliche
CRC-Verifikation (`sub_0407`, Dual-Konvention 0xBF84 FM / 0xE295 MFM, gewählt über `[03FD]` bit1)
liegt erst im **geladenen Lader** (RAM `0x04xx`), nicht im ROM. Details:
Erinnerung `project_crc_128b_dual_convention`.

---

## 5. ZVE2 STROBE_LOOP (Sektorladen, `01DD–027D`)

Reine ZVE2-Routine. Ablauf je Sektor (Adressen → `.prn`):

1. **IDAM-Parameter laden** (`01DD–01F6`): `HL=[03F0]` Ladeadresse; Alt-Register `B'=0xFE`,
   `C'=0xA1`, `D'=Kopf`, `E'=Zyl`, `H'=Size`, `L'=Sektor-ID`.
2. **`/STR` assertieren** (`01F8–01FC`): `OUT(10h,B5)` (Bit3 fallend) → `doReadSector()` →
   springe zu STROBE_LOOP_BODY.
3. **FM/MFM-Pfad wählen** (`025A`): `[03FD]` bit1 → NZ-Pfad (FM) bzw. Z-Pfad (MFM).
4. **IDAM suchen & vergleichen** (`020B–0222`): `FE`? dann Zyl, Kopf, Sektor-ID, Size jeweils per
   `IN(16h)` + `CP`. Mismatch IDAM-Marke/Zyl/Kopf/Sektor → neuer `/STR`-Strobe (`JR 01F8`);
   Size-Mismatch → `01FE` (Sektor-Nummern-Fehler).
5. **Daten übertragen** (`0224–0247`): `INI`×3 + `INIR` (Nutzdaten nach `[03F0]`), dann 2× `INI`
   für die Daten-CRC-Bytes (nur gelesen, §4.3).
6. **Schleife/Abschluss** (`0253–026B`): Sektor-ID hochzählen bis `[07F2]=4`; danach `[03F8]=3`
   → ZVE1 wird geweckt.

---

## 6. Alternativer Boot-Pfad (`027E–03AF`)

Wird erreicht, wenn alle Floppy-Laufwerks-Selects (`[03FC]` bis `0x77`) erfolglos durchprobiert
sind. Schaltet auf einen **Sekundär-Controller** (Ports `0x30–0x37`, CTC-basiert) um und lädt von
dort. Im A5120-Standardbetrieb i.d.R. nicht relevant; Detail-Annotation in der `.prn`
(`027E+`, alle `[ZVE1]`).

---

## 7. Emulator-Anforderung: treue MK-Umschaltung

> **Status: aktuell vereinfacht — der Mismatch wird NICHT modelliert.**
> Designziel in Erinnerung `project_fm_mfm_faithful_readpath`.

### 7.1 Aktueller Stand

`K5122::startReadTransfer()` (`core/cards/k5122/k5122.cpp:605–619`) **berechnet** die effektive
Codierung `eff_enc` (Steuerwort-Override `0x85`/`0x87` via `read_enc_`, sonst
`DriveProfile::default_read_encoding`), **benutzt sie aber nur in der Log-Zeile**. Der Lese-Stream
entsteht über `TrackCodec::buildRobotronTrack()` und liefert für FM **und** MFM **dasselbe**
boot-kompatible **Single-A1-Layout** (Marke auf dem A1, Seeds 0xBF84/0xCDB4) — unabhängig von MK
*und* von der echten Codierung des `.hfe`/`.img`. Folge: das ROM liest mit dem FM-Default sofort
erfolgreich, **die MFM-Umschaltung wird nie ausgelöst**; ein Modus-Mismatch erzeugt nie ungültige
Daten.

### 7.2 So MUSS es funktionieren

Damit die ROM-Trial-and-Error-Logik (§4.1) echt durchläuft, muss der Lesepfad einen **Mismatch**
erzeugen:

1. **Spur-Synthese gemäß echter Codierung** (aus `.hfe`-Header bzw. `.img`-Beschreibung bekannt):
   - **FM**: Marke (MKE) direkt auf dem **`FE`**, keine A1-Präambel.
   - **MFM**: **3× `A1`**-Sync, Marke auf dem (3.) `A1`, danach `FE`.
2. **K5122 liefert nur bei Übereinstimmung gültige Bytes:** Nur wenn `read_enc_` (= ROM-MK,
   `0x87` FM / `0x85` MFM) zur Spur-Codierung passt, präsentiert der K5122 IDAM-findbare Bytes.
   Bei Mismatch liefert er, was die Markenerkennung der *falschen* Betriebsart sieht — also
   **kein gültiges `FE`** an der erwarteten Stelle.
3. **Folge im ROM**: ZVE2 findet kein IDAM → `/STR`-Re-Strobes → Index-Timeout `[03F8]=1` → ZVE1
   toggelt MK (`XOR 02h @0151`) → bei passender Betriebsart gültige Daten → Boot konvergiert.

### 7.3 Knackpunkt (warum bisher offen)

Das ROM erwartet die **Marken-Quittung (MKE) auf dem `A1`** (MFM) bzw. auf dem `FE` (FM); der
generische `TrackCodec::buildTrack` setzt die Marke auf das **`FE`**. Ein naiver Umstieg von
`buildRobotronTrack` (single-A1, Marke auf A1) auf `buildTrack` bricht daher die **2-Byte-IDAM-
Suche** des ROM (`READ_IDAM_NZ`, `020B`) → Boot-Regress. Die MFM-Synthese muss die Marke aufs
**3. A1** legen, die FM-Synthese direkt aufs **FE**. (Vgl. Erinnerung `project_fm_mfm_drive_property`.)

---

## 8. Emulator-Anforderung: FM-8"-Laufwerke unterstützen

> **Status: aktuell nicht möglich — verifiziert 2026-06-25.** Auf echter HW bootet FM-8"
> problemlos (das funktionierende A:-Laufwerk); die Lücke ist rein emulatorseitig (5,25"-zentriert).

### 8.1 Befund (Experiment)

Die Maschine ist fest mit **4× K5601** (5,25", rpm=300) verdrahtet (`a5120.cpp:23`). Test: temporär
4× `mf3200_8_ss77` (FM-8", rpm=360) + vorhandene 5,25"-Disk → **Boot hängt in der Seek/Status-
Schleife** (ZVE1 PC `0x0121`, `DRIVE_DETECT_LOOP`), **ZVE2 startet nie**, `[03F8]=0`.

**Es liegt also NICHT an der Datenrate** (ZVE2 kommt gar nicht zum Lesen). Die Byte-Periode
`kBytePeriodCycles=150` ist fix und entspricht der `/HF=1`-Rate, die FM-8" und MFM-5,25" teilen.
Blocker ist **früher**: `updateStatusPortB()` (`k5122.cpp:537`) bildet den Status nur aus
`isMounted()` + `currentCylinder()` und **verdrahtet das /HF-Statusbit hart auf 1** (Kommentar:
„für FM/8" wäre bit2=0; nur MFM-Testrahmen"). Das Laufwerk meldet nie „bereit + Spur 0" → Seek
dreht endlos. Ursache: 5,25"-Image nicht in 8"-Geometrie nutzbar + unvollständiges 8"-Statusmodell.

### 8.2 Was nötig ist

1. **Laufwerksbestückung konfigurierbar** machen (statt 4× K5601 hartcodiert ein 8"-Profil
   `mf3200_8_ss77` zulassen).
2. **FM-8"-formatiertes CP/A-Boot-Image** bereitstellen (passende Geometrie, 77 Spuren, FM).
3. **8"-Drive-Status-/Seek-Modell vervollständigen**: `/HF`-Statusbit (Port 0x12 bit2)
   profilabhängig setzen (8"-FM → 0); sicherstellen, dass das Image im 8"-Profil mountet und
   „bereit + Spur 0" meldet.
4. *Optional (Treue):* **Byte-Periode an `/HF` koppeln** — `150` Takte für `/HF=1`
   (FM-8"/MFM-5,25"), ~`75` für `/HF=0` (MFM-8"). Für FM-8" selbst nicht erforderlich, da es sich
   `/HF=1` mit dem schon bootenden MFM-5,25" teilt.

Danach trägt der Datenpfad (single-A1-Stream + treue FM/MFM-Synthese aus §7) auch für FM-8".

---

## 9. Verweise

- **Zeilengenaues Listing:** `doc/EPROMS/zre.prn` (`-l`-ladbar) — Generator `tools/gen_zre_prn.py`,
  Quelle `doc/EPROMS/zre_rom.asm` (+ `zre.rom`).
- **Hardware:** `doc/trascripted/Zentrale Recheneinheit K2526 K2527.md` (ROM-Mapping, §6.4),
  `doc/trascripted/Floppy Anschlußsteuerung K 5122.md` (Tor A/B-Signale, Markenerkennung §5.3).
- **Emulator-Modell:** `core/cards/k5122/`, `core/cards/k2526/`, `core/machines/a5120/a5120.cpp`,
  `core/peripherals/floppy_drive/` (DriveProfile, TrackCodec).
- **Erinnerungen:** `project_crc_128b_dual_convention` (FM/MFM + CRC-Mechanismus),
  `project_fm_mfm_faithful_readpath` (treuer Lesepfad-Umbau), `project_fm_mfm_drive_property`
  (Emulator-FM/MFM-Modell), `project_real_zre_rom_dump` (ROM-Image-Herkunft).
- **Debug-Workflow:** `tools/how_to_debug_and_trace.md`, `tools/boot_trace.md`, `tools/k1520dbg.md`.

# Refactoring: Floppy-Emulation (K5122 + Disketten-Images)

**Status:** Entwurf, ausprogrammierbar
**Datum:** 2026-06-09
**Betrifft:** neuer K1520-Core (`core/`), nicht den Legacy-Emulator (`src/`)
**Module:** `core/cards/k5122/`, `core/peripherals/floppy_drive/`, `core/api/`, `app/`
**Referenzen:** `doc/design/07_k5122_afs.md`, `doc/design/09_floppy_drive.md`,
`doc/K1520_architecture.md` §8 / §14.5 / §14.5b / §14.6, `doc/analyse_bootloader.md`

---

## 1. Motivation und Ziel

### 1.1 Ist-Zustand

Heute liegt ein Diskettenbild als **`.img` (reine Sektor-Nutzdaten) + Geometrie-Beschreibung**
(`DiskFormat`/`TrackFormat`, aus `FormatParser::builtinFormats()` oder `.cfg`) vor. Beim Lesen
baut die K5122 in `K5122::doReadSector()` / `buildField()` aus den Sektordaten *on the fly* einen
MFM-ähnlichen Spur-Bytestrom zusammen (IDAM `A1 FE …`, DATA `A1 FB … CRC`, Gap `0x4E`) und streamt
ihn über Port `0x16`. Beim Schreiben läuft es umgekehrt: Port-`0x14`-Bytes werden gesammelt und als
reine Nutzdaten zurückgeschrieben.

### 1.2 Problem

Die Synthese ist **mit der konkreten Lese-Routine des jeweiligen Bootloader-Stadiums verflochten**.
Im Code (`core/cards/k5122/k5122.cpp`) stecken zahlreiche fragile Sonderbehandlungen:

- feste Blockgröße `sector_block_ = 138`, `sector_data_len_`, manueller IDAM/CRC-Blockaufbau
  (`doReadSector`, Z. 585–648);
- `stream_continuous_`-Flag mit **zwei** verschiedenen `buildField()`-Pfaden (Z. 757–821);
- **zwei** CRC-Startwerte `0xBF84` und `0xCDB4` für dieselbe Größe, abhängig vom Lesepfad
  (`loaderCrc16`, Z. 732–754);
- die `OUT(13H),03H`-Track-Ende-Heuristik mit `!stream_continuous_`-Gate (Z. 178–183);
- die Read-Echo-Ausnahme in `handleDataPortAWrite()` (Z. 487–491);
- Auto-Advance-auf-Überlesen und Gap-Padding in `ioRead(0x16)` (Z. 94–114).

Jeder neue Zugriffspattern eines Betriebssystems (andere Sektorgröße, anderer Strobe-Rhythmus,
gemischte Spuren) erzwang bisher einen neuen Spezialfall. Das ist nicht erweiterbar und nicht
hardware-getreu.

### 1.3 Ziel

1. **Eine zentrale, formatagnostische Track-Bytestrom-Schicht** (`TrackImage`) als *einziges*
   Bindeglied zwischen Controller und Dateiformat. Die K5122 streamt diesen Bytestrom wie ein
   echter Lesekopf über die rotierende Spur — **ohne** Wissen über Sektorgröße, Sektoranzahl,
   CRC-Verfahren oder Boot-Stadium.
2. **Austauschbare Image-Backends** hinter einer `DiskImage`-Schnittstelle:
   - `RawSectorImage` — bestehendes `.img` + Geometriebeschreibung (synthetisiert den Track-Bytestrom);
   - `HfeImage` — `.hfe` (HxC) mit **echtem MFM-Bitstrom pro Spur**, **lesend und schreibend**.
   - erweiterbar (IMD/DMK/EDSK) ohne Controller-Änderung.
3. **Wegfall der fragilen Spezialfälle** auf der Datenpfad-Seite: ein einziges Streaming-Modell,
   echte Gaps/Marken/CRCs im materialisierten Track, eine einzige CRC-Routine.
4. **Vollständige Agnostik** gegenüber gemischten Formaten (Bootspur 128 B + Datenspur 1024 B,
   asymmetrische Seiten — siehe `cpa780`), beliebigen Sektorgrößen und Mischbelegungen.
5. **Mehrere Laufwerkstypen und Aufzeichnungsverfahren** an einer Karte: das aktuelle
   doppelseitige 5,25″-MFM-Laufwerk (80×2), ein einseitiges 5,25″-MFM-Laufwerk (40×1) **und** ein
   8″-FM-Laufwerk (77×1). Das schließt **FM-Aufzeichnung** neben MFM ein (siehe §3.A). Die K5122 ist
   gegenüber Verfahren und Geometrie agnostisch — wie die echte Hardware (Marken-ROM A2.2 erkennt
   MFM-Sync *und* FM-Marken; /HF, /MK, /MK1 wählen das Verfahren).

> **Abgrenzung (wichtig):** Der Refactor adressiert den **Datenpfad** (Track-Synthese/-Streaming).
> Die **Bus-Arbitrierung** des ZVE1↔ZVE2-Handshakes (BUSRQ-Übergabe, Track-Ende) ist eine *separate*
> Controller-Verantwortung und wird bewusst erhalten und gegen die Boot-Regression revalidiert
> (siehe §12, §13). Die bestehende Boot-Kette (`disks/cpadisk01.img` → Banner → `@OS.COM`) darf
> nicht regredieren.

---

## 2. Architekturüberblick

```
┌──────────────────────────────────────────────────────────────────────┐
│ K5122 (Controller-Karte) — core/cards/k5122/                          │
│   PIO-Ports 0x10–0x18, /STR /ST MK MK1 Kantenlogik, BUSRQ-Arbitrierung │
│   Modell: Lesekopf über rotierender Spur (head_pos, wrap = Umdrehung)  │
│   kennt KEINE Sektoren/CRC/Boot-Stadien                                │
└───────────────▲───────────────────────────────────────────────────────┘
                │ fordert/committet  TrackImage (cyl, head)
┌───────────────┴───────────────────────────────────────────────────────┐
│ FloppyDrive — core/peripherals/floppy_drive/                           │
│   hält das gemountete DiskImage, akt. Zylinder, Track-Cache + dirty    │
└───────────────▲───────────────────────────────────────────────────────┘
                │  readTrack / writeTrack / geometry
┌───────────────┴───────────────────────────────────────────────────────┐
│ DiskImage (abstrakte Schnittstelle)        meldet Encoding (FM/MFM)     │
│   ├── RawSectorImage  (.img + DiskFormat)  ──uses──► TrackCodec         │
│   └── HfeImage        (.hfe)               ──uses──► BitCodec (MFM/FM)   │
│   (künftig: ImdImage, DmkImage, EdskImage …)                           │
└───────────────▲───────────────────────────────────────────────────────┘
                │ liefert/nimmt
┌───────────────┴───────────────────────────────────────────────────────┐
│ TrackImage  — ZENTRALE ABSTRAKTION (Track-Bytestrom + Markenflags)     │
│              + Encoding-Tag (FM/MFM) — sonst verfahrensneutral          │
│ TrackCodec    — IBM-Track (FM ODER MFM) bauen/parsen, CRC-16-Primitive  │
│ BitCodec (MFM + FM)      — Bitzellen ⇆ Bytes je Verfahren (für HFE)     │
└────────────────────────────────────────────────────────────────────────┘

  Querschnitt-Konfiguration:  DriveProfile[4]  (pro Slot: Zoll/Spuren/Köpfe/
  U-min/unterstützte Verfahren) — Maschinenkonfiguration, kein Runtime-State.
```

Schichtregel wie im übrigen Core: jede Schicht kennt nur die darunterliegende. Die K5122 hängt
nur noch an `FloppyDrive` und `TrackImage` — **nicht** mehr an `DiskFormat`/Sektorlogik. Das
Aufzeichnungsverfahren (FM/MFM) ist in der **Codec-Schicht** gekapselt; `TrackImage` und Controller
bleiben verfahrensneutral (§3.A).

---

## 3. Zentrale Abstraktion: `TrackImage` (Track-Bytestrom)

Eine `TrackImage` ist der **decodierte Spurinhalt einer (cyl, head)-Spur**: die Bytefolge, die der
Datenseparator hinter dem Lesekopf liefert — inklusive Gaps, Sync-/Adressmarken, ID-Feldern,
Datenfeldern und **echten CRCs**. Sie ist die einzige Repräsentation, die der Controller sieht.

```cpp
// core/peripherals/floppy_drive/track_image.h
#pragma once
#include <cstdint>
#include <vector>

/// Aufzeichnungsverfahren der Spur. Bestimmt NUR die Codec-Schicht, nicht den Controller.
enum class Encoding : uint8_t { FM, MFM };

/// Typ einer Adressmarke an einer Zellposition (aus dem fehlenden Clock-Bit abgeleitet).
/// Gilt für BEIDE Verfahren; nur das Sync-/Clock-Muster unterscheidet sich:
///   MFM: Sync A1A1A1 + Mark-Byte (FE/FB/F8/FC) bzw. C2C2C2+FC für Index
///   FM : Mark-Byte allein mit Sonder-Clock (FE/C7, FB/C7, F8/C7, FC/D7), KEIN A1-Sync
enum class MarkType : uint8_t {
    None = 0,   ///< normales Daten-/Gap-Byte
    Index,      ///< IAM  (MFM C2C2C2 FC / FM FC)
    Id,         ///< IDAM (MFM A1A1A1 FE / FM FE) — ID-Feld folgt
    Data        ///< DAM  (MFM A1A1A1 FB|F8 / FM FB|F8) — Datenfeld folgt (F8 = gelöscht)
};

/// Eine decodierte Spur als Byte-Strom mit Markenflags.
/// Die Bytes sind GENAU das, was der Lesekopf sieht (inkl. Gaps/Sync/CRC) —
/// keine Synthese im Controller, keine Sonderfälle.
struct TrackImage {
    std::vector<uint8_t>  bytes;   ///< decodierte Spur-Bytes (eine Umdrehung)
    std::vector<MarkType> marks;   ///< parallel zu bytes; markiert das Mark-Byte (das letzte
                                   ///< Sync-A1/C2 bzw. das Mark-Byte FE/FB/FC der Marke)
    Encoding encoding = Encoding::MFM;  ///< Verfahren der Spur (Re-Encode + Index-/HF-Logik)
    // Optionale Metadaten für formattreues Zurückschreiben (v.a. HFE):
    uint32_t bitcells = 0;         ///< urspr. Bitzellen-Länge der Spur (0 = unbekannt/Raw)

    bool empty() const { return bytes.empty(); }
    size_t size() const { return bytes.size(); }

    /// Index der nächsten Marke ab Position pos (zyklisch); type==None => beliebige Marke.
    /// Liefert SIZE_MAX, wenn keine Marke existiert.
    size_t nextMark(size_t pos, MarkType type = MarkType::None) const;
};
```

**Design-Begründung — `marks[]` statt Suche nach `0xA1`/`0xFE`:** In echtem MFM unterscheidet sich
das Sync-Byte `A1` von einem Daten-`A1` durch einen **fehlenden Clock-Bit** (Pattern `0x4489`).
Datenbytes dürfen also `0xA1`/`0xFE`/`0xFB` enthalten, ohne als Marke missverstanden zu werden. Die
Markenpositionen werden vom Backend gesetzt (Raw: beim Bauen; HFE: beim Decodieren aus dem
Clock-Muster) und machen die Re-Sync-Logik des Controllers **robust und formatagnostisch**.

**Byte- statt Bit-Ebene:** Die zentrale Schicht arbeitet auf **decodierten Bytes**, weil der K5122
hinter dem Datenseparator ebenfalls byteweise arbeitet (Ports 0x14/0x16). Die Bitebene (MFM *oder*
FM) lebt ausschließlich in den Bit-Codecs des HFE-Backends. (Eine spätere Flux-Ebene — SCP/IPF —
würde die zentrale Schicht auf Bitzellen heben; das ist hier *nicht* nötig und bewusst ausgeklammert.)

---

## 3.A Laufwerksprofile, Aufzeichnungsverfahren (FM/MFM) und Geräte-Agnostik

Die Betriebsdokumentation (`doc/trascripted/Floppy Anschlußsteuerung K 5122.md`, §4.1/§4.3/§5.3/§5.5)
zeigt: die K5122 ist **konstruktiv verfahrensagnostisch**. Der Marken-ROM A2.2 erkennt sowohl die
MFM-Synchronisationsbytes (`A1`, `C2`) als auch die FM-Marken (`F8`, `FB`, `FC`, `FE`); das
Mikroprogramm wählt FM vs. MFM und die Datenrate über die Steuersignale:

- **/HF** (Steuer-PIO Tor B, B₂, Ausgang): Lese-/Schreibtaktfrequenz.
  `HF=0` → 1 MHz (MFM, 8″ MF6400); `HF=1` → 500 kHz (FM 8″ / **MFM 5,25″** MFS).
- **/MK** (Tor A, A₁): `/MK=0` Markenerkennung FM bzw. MFM-Index; `/MK=1` MFM-Sync-Byte `A1`.
- **/MK1** (Tor A, A₄): FM-Marken-/MFM-Sync-Schreiben vs. Dateninformation.

Das Verfahren ist also Eigenschaft von **Laufwerk + Medium + Format**, nicht der Karte. Die Emulation
spiegelt das exakt:

1. `TrackImage` bleibt ein reiner decodierter Byte-+Marken-Strom — **identisch** für FM und MFM. Nur
   das `encoding`-Tag sagt dem Rückschreiber/Index-Generator, welches Verfahren gilt.
2. Der Verfahrensunterschied lebt **vollständig in der Codec-Schicht** (`TrackCodec`, Bit-Codecs).
3. Jeder der 4 Laufwerks-Slots erhält ein `DriveProfile` (physisches Laufwerk).

### 3.A.1 Unterstützte Laufwerke (Startumfang)

| Profil          | Zoll  | Spuren×Köpfe | Verfahren | U/min | typ. Format               |
|-----------------|-------|--------------|-----------|-------|---------------------------|
| `mfs_525_ds80`  | 5,25″ | 80×2         | MFM       | 300   | cpa780 / cpa800 (aktuell) |
| `ss_525_40`     | 5,25″ | 40×1         | MFM       | 300   | cpa200-artig, 40 Spuren   |
| `mf3200_8_ss77` | 8″    | 77×1         | FM        | 360   | IBM-3740 SSSD 26×128 B    |
| `mf6400_8_ds77` | 8″    | 77×2         | MFM       | 360   | optional (8″ DSDD)        |

```cpp
// core/peripherals/floppy_drive/drive_profile.h
#pragma once
#include "track_image.h"   // Encoding
#include <cstdint>
#include <string>

/// Beschreibt das an einem Slot angeschlossene physische Laufwerk.
/// Maschinenkonfiguration (Konstruktorparameter), KEIN Runtime-Zustand —
/// analog zu den compile-time Config-Structs der Karten (CLAUDE.md-Konvention).
struct DriveProfile {
    std::string name        = "mfs_525_ds80";
    uint8_t  num_cyls       = 80;     ///< 40 / 77 / 80
    uint8_t  num_heads      = 2;      ///< 1 / 2
    uint16_t rpm            = 300;    ///< 300 (5,25″) / 360 (8″) → Index-Periode
    uint8_t  medium_inch    = 5;      ///< 5 / 8 (für /HF-Frequenzwahl, informativ)
    bool     supports_fm    = false;  ///< 8″-Laufwerke
    bool     supports_mfm   = true;

    /// Index-Periode in Z80-Takten aus rpm ableiten (ersetzt die feste Konstante).
    int indexPeriodCycles(uint32_t cpu_hz) const {
        return static_cast<int>(static_cast<uint64_t>(cpu_hz) * 60 / rpm);
    }
};

/// Eingebaute Standardprofile (per Name auflösbar, analog FormatParser::builtinFormats()).
const DriveProfile& builtinDriveProfile(const std::string& name);
```

### 3.A.2 Woher kommt das Encoding?

- **HFE**: aus dem Header-Feld `track_encoding` (0 = ISOIBM_MFM, 2 = ISOIBM_FM). HfeImage meldet es
  pro Spur über `TrackImage::encoding`. Self-describing.
- **Raw `.img`**: `DiskFormat`/`TrackFormat` bekommt ein Feld `Encoding encoding` (Default `MFM`),
  pro Track-Bereich überschreibbar → erlaubt sogar **gemischte Verfahren** (z. B. FM-Indexspur +
  MFM-Daten), falls je nötig. `RawSectorImage` reicht es an `TrackCodec` weiter.
- Der Controller setzt **/HF** passend zum Encoding der aktiven Spur und nimmt **`rpm`** des
  Profils für die Index-Periode.

### 3.A.3 Kompatibilitätsprüfung beim Mounten

Beim Mounten wird `DiskImage::geometry()` + Encoding gegen das `DriveProfile` des Slots geprüft:
Spurzahl ≤ `num_cyls`, Köpfe ≤ `num_heads`, Encoding ∈ {`supports_fm`?FM, `supports_mfm`?MFM}.
Inkompatibel → `mountDisk` schlägt fehl und setzt `last_error_` (z. B. „FM-Medium an MFM-Laufwerk").

---

## 4. `TrackCodec` — IBM-Track (FM/MFM) bauen/parsen + CRC-Vereinheitlichung

Erzeugt aus logischen Sektoren einen normgerechten Track-Bytestrom (**FM oder MFM**) und parst ihn
zurück. Wird vom `RawSectorImage`-Backend benutzt (und in Tests). **Hier wird die CRC zentralisiert.**

```cpp
// core/peripherals/floppy_drive/track_codec.h
#pragma once
#include "track_image.h"        // Encoding, TrackImage, MarkType
#include "format_parser.h"      // TrackFormat
#include <cstdint>
#include <vector>

/// Ein logischer Sektor, wie ihn das Raw-Backend liefert/erwartet.
struct LogicalSector {
    uint8_t  cyl  = 0;
    uint8_t  head = 0;
    uint8_t  id   = 1;       ///< 1-basierte Sektor-ID
    uint16_t size = 128;     ///< Bytes/Sektor (128/256/512/1024)
    std::vector<uint8_t> data;
};

/// Gap-Parameter (Default je Verfahren via gapsFor(); pro Format überschreibbar).
/// MFM: Sync 12×00 + 3×A1, Gap-Füller 0x4E.  FM: Sync 6×00, kein A1, Gap-Füller 0xFF.
struct GapParams {
    uint8_t  gap1 = 16;   ///< nach IAM
    uint8_t  gap2 = 11;   ///< zwischen IDAM-Feld und DAM
    uint8_t  gap3 = 24;   ///< nach Datenfeld (vor nächstem IDAM)
    uint16_t gap4a = 16;  ///< Vor-Index-Gap
    uint8_t  sync_len = 12; ///< 00-Sync vor jeder Markengruppe (MFM 12, FM 6)
    uint8_t  gap_fill = 0x4E; ///< MFM 0x4E, FM 0xFF
    bool     with_iam = true;
};

namespace TrackCodec {

/// Verfahrensabhängige Default-Gaps (MFM/FM).
GapParams gapsFor(Encoding enc);

/// Baut eine vollständige Spur (gap4a, IAM, gap1, [IDAM gap2 DAM data gap3]×n, gap4b)
/// mit ECHTEN Marken (marks[]) und ECHTEN CRCs, im gewählten Verfahren. Das Ergebnis-
/// TrackImage trägt enc als encoding-Tag. Sektorgröße/-anzahl frei gemischt.
///   MFM: Marken = 3×A1 + Mark-Byte (FE/FB) bzw. C2+FC; Gap 0x4E.
///   FM : Marken = Mark-Byte allein mit Sonder-Clock; KEIN A1-Sync; Gap 0xFF.
TrackImage buildTrack(const std::vector<LogicalSector>& sectors,
                      Encoding enc,
                      const GapParams& gaps /* = gapsFor(enc) */);

/// Parst eine Spur (FM oder MFM, aus track.encoding) zurück in logische Sektoren
/// (für Zurückschreiben in .img und Tests). Verifiziert ID- und Daten-CRC; setzt bei
/// CRC-Fehler das jeweilige Flag im Sektor.
std::vector<LogicalSector> parseTrack(const TrackImage& track);

/// CRC-Primitive (siehe §4.1/§4.2). seed_hi/seed_lo = Startzustand (B,C).
/// Robotron-MFM-Routine (= verifizierte loaderCrc16). Für FM/IBM-3740 ggf. crc16Ccitt().
uint16_t crc16(const uint8_t* data, size_t n, uint8_t seed_hi, uint8_t seed_lo);
uint16_t crc16Ccitt(const uint8_t* data, size_t n, uint16_t seed = 0xFFFF);

}  // namespace TrackCodec
```

### 4.1 CRC-Vereinheitlichung (entfernt die zwei „Seeds")

Die heutige `loaderCrc16` (k5122.cpp Z. 732–754) ist die **verifizierte Referenz** — eine
byte-genaue Übersetzung der Z80-Routinen `sub_0407` (Sekundärlader) und `sub_1E44` (3. Stufe).
Sie wird **unverändert** zu `TrackCodec::crc16` (umziehen, nicht neu erfinden).

Die zwei „Startwerte" `0xBF84` (über 128 B Daten) und `0xCDB4` (über `[FB]+Daten`) sind **kein**
zweites Verfahren, sondern **dieselbe** CRC, nur an verschiedenen Stellen der Präambel angesetzt.
Es gibt genau **eine** kanonische CRC:

```
crc = crc16(Bytes ab erstem A1 bis Feldende, seed = 0xFF, 0xFF)
      über  [A1 A1 A1 <Mark> <Feld>]
```

**Verifikationsschritt (zu Beginn der Umsetzung als Unit-Test absichern):**

```cpp
// Beweist die Vereinheitlichung. Wenn beide Assertions halten, fallen ALLE
// CRC-Sonderfälle weg und buildTrack bäckt genau eine korrekte CRC ein.
const uint8_t A1 = 0xA1, FB = 0xFB;
EXPECT_EQ(crc16((uint8_t[]){A1,A1,A1},        3, 0xFF, 0xFF), 0xCDB4);  // Zustand vor Mark
EXPECT_EQ(crc16((uint8_t[]){A1,A1,A1,FB},     4, 0xFF, 0xFF), 0xBF84);  // Zustand nach DAM
```

Halten die Assertions, berechnet `buildTrack` jede CRC als
`crc16([A1,A1,A1,Mark, …Feld…], 0xFF,0xFF)` und schreibt die zwei Bytes (B=high, C=low) hinter das
Feld. Beide Leseroutinen (Sekundärlader ab Daten, 3. Stufe ab `[FB]`) verifizieren dann dieselbe
CRC — ohne Wissen des Controllers. Sollte eine Assertion *nicht* halten, ist der Startwert vor dem
ersten `A1` ≠ `0xFFFF`; dann den tatsächlichen Reset-Zustand aus `sub_0407`/`sub_1E44`
(`doc/analyse_bootloader.md`) übernehmen — das Prinzip „eine CRC, in den Track eingebacken" bleibt.

> Die ID-Feld-CRC (`A1 A1 A1 FE cyl head sec size`) wird analog eingebacken. Falls der Robotron-Lader
> die IDAM-CRC gar nicht prüft (heute zwei `0x00`-Bytes, bootet trotzdem), sind korrekte Werte
> dennoch harmlos und formattreu — also einbacken.

### 4.2 FM-Track-Aufbau (8″-Laufwerk, Single Density)

FM unterscheidet sich von MFM nur in **Marken, Sync und Gap-Füller** — die Feld-/CRC-Struktur ist
analog, `buildTrack(sectors, Encoding::FM, …)` erzeugt dasselbe `TrackImage`-Schema:

- **Kein** `A1`/`C2`-Sync-Tripel. Die Marke ist ein **einzelnes Byte mit Sonder-Clock**:
  IDAM `FE` (Clock `C7`), DAM `FB` bzw. gelöscht `F8` (Clock `C7`), IAM `FC` (Clock `D7`).
  `marks[]` wird auf dem Mark-Byte gesetzt; den Sonder-Clock kodiert erst der FM-Pfad des `BitCodec` (§5).
- **Gap-Füller `0xFF`** statt `0x4E`; **Sync 6×00** statt 12×00 (`gapsFor(Encoding::FM)`).
- **CRC**: IBM-3740-FM verwendet **Standard-CRC-16-CCITT** (Poly `0x1021`, Init `0xFFFF`) über
  `[Mark-Byte … Feld]` — also `crc16Ccitt()`. (Die Robotron-`crc16` ist für den MFM-Boot-Pfad
  verifiziert; ob sie mit CCITT identisch ist, klärt der §4.1-Test. FM-Images nutzen sicherheits-
  halber `crc16Ccitt`, bis Gleichheit belegt ist — siehe §13.)

Der Marken-ROM A2.2 der echten Karte erkennt genau diese Marken (`F8/FB/FC/FE` → AM, `A1`/`C2` →
MFM-Sync; Doku §5.3, Gruppentabelle). Das Modell bildet das 1:1 über `MarkType` ab — verfahrensneutral.

---

## 5. Bit-Codecs — Bitzellen ⇆ Bytes (für HFE: MFM **und** FM)

Nur das HFE-Backend braucht die Bitebene. Gekapselt, damit der Rest byteorientiert bleibt.
**Ein** Dispatcher, **zwei** Implementierungen (MFM/FM) — gewählt über `Encoding`:

```cpp
// core/peripherals/floppy_drive/bit_codec.h
#pragma once
#include "track_image.h"        // Encoding, TrackImage
#include <cstdint>
#include <vector>

namespace BitCodec {

/// Decodiert einen Bitzellen-Strom (LSB-first je Byte, wie in HFE gespeichert) in eine
/// TrackImage: decodierte Datenbytes + Markenflags. enc wählt MFM- oder FM-Decoder; das
/// Ergebnis trägt enc als TrackImage::encoding.
/// MFM: Sync am fehlenden Clock-Bit (0x4489 = A1, 0x5224 = C2).
/// FM : Marke am Sonder-Clock-Muster der Mark-Bytes (FE/FB/F8 Clock C7, FC Clock D7).
TrackImage decode(const std::vector<uint8_t>& cells, uint32_t bitcell_count, Encoding enc);

/// Encodiert eine TrackImage (Verfahren aus track.encoding) zurück in einen Bitzellen-Strom
/// (LSB-first). Marken (marks[i] != None) werden mit dem verfahrenstypischen Sonder-Clock
/// kodiert, übrige Bytes regulär. Füllt/trimmt auf target_bitcells (urspr. Spurlänge) mit Gap.
std::vector<uint8_t> encode(const TrackImage& track, uint32_t target_bitcells);

}  // namespace BitCodec
```

**MFM-Grundregeln:**
- Datenbit `d_i` → Zelle `c_i d_i`, Clock `c_i = NOT(d_{i-1} OR d_i)`.
- A1-Sync: Datenmuster `0xA1` mit unterdrücktem Clock → Zellmuster `0x4489`.
- C2-Sync (IAM): Zellmuster `0x5224`.

**FM-Grundregeln (8″ Single Density):**
- Datenbit `d_i` → Zelle `1 d_i` (Clock-Bit **immer 1** zwischen Datenbits).
- Mark-Bytes mit unterdrückten Clock-Bits: `FE`/`FB`/`F8` mit Clock `C7`, `FC` mit Clock `D7`
  (siehe Marken-ROM A2.2, Doku §5.3) → daran erkennt `decode` die Marke und setzt `marks[]`.
- Gap/Sync sind reguläres FM (`0xFF`/`0x00`).

**Gemeinsam:** Bitreihenfolge in HFE = **LSB zuerst** je Byte; `bitrate`/`floppy_rpm` aus dem
HFE-Header bzw. dem `DriveProfile` bestimmen die Zellzeit (für Index-Timing, §9).

---

## 6. `DiskImage`-Schnittstelle und Backends

```cpp
// core/peripherals/floppy_drive/disk_image.h
#pragma once
#include "track_image.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

/// Geometrieabfrage (für UI/Validierung); Backends, die self-describing sind (HFE),
/// füllen das aus der Datei, RawSectorImage aus dem DiskFormat.
struct DiskGeometry {
    uint8_t  num_cyls  = 0;
    uint8_t  num_heads = 0;
    bool     uniform   = false;   ///< true = alle Spuren gleich (Info für UI)
};

/// Abstraktes Disketten-Image. Liefert/nimmt fertige TrackImages pro (cyl, head).
/// Der Controller sieht NUR TrackImage — nie Sektoren, Offsets oder Dateiformat.
class DiskImage {
public:
    virtual ~DiskImage() = default;

    virtual DiskGeometry geometry() const = 0;
    virtual bool writable() const = 0;

    /// Decodierte Spur (cyl, head). Leeres TrackImage, wenn die Spur nicht existiert.
    virtual TrackImage readTrack(uint8_t cyl, uint8_t head) = 0;

    /// Geänderte Spur zurückschreiben. Raw: Track parsen → Sektoren → .img;
    /// HFE: Track → MFM-Bitstrom → Datei. false bei WP/Fehler.
    virtual bool writeTrack(uint8_t cyl, uint8_t head, const TrackImage& track) = 0;

    /// Persistiert ausstehende Änderungen (No-op, wenn synchron geschrieben wird).
    virtual bool flush() = 0;

    /// Fabrik mit Format-Sniffing. fmt nur für nicht-self-describing Backends (Raw).
    /// Erkennt HFE an der Signatur "HXCPICFE"/"HXCHFEV3", sonst .img/Raw (braucht fmt).
    static std::unique_ptr<DiskImage> open(const std::string& path,
                                           std::optional<DiskFormat> fmt,
                                           bool write_protect);
};
```

### 6.1 `RawSectorImage` (`.img` + `DiskFormat`)

- `readTrack(cyl, head)`: `findTrack(cyl,head)` → Sektordaten per `sectorOffset()` einlesen →
  `TrackCodec::buildTrack(sectors, enc, …)` mit `enc` aus `TrackFormat::encoding` (Default MFM) →
  echte Marken + CRCs. Übernimmt das heutige Offset-/Interleave-Modell (`floppy_drive.cpp`
  `sectorOffset`, asymmetrische Geometrie cpa780 bleibt gültig).
- `writeTrack(cyl, head, track)`: `TrackCodec::parseTrack(track)` → logische Sektoren → an die
  passenden `.img`-Offsets schreiben. Sektoren, die der Track nicht enthält, bleiben unangetastet.
- `geometry()` aus `DiskFormat`; Encoding aus `TrackFormat::encoding`; `writable()` = !write_protect.
- **Verhalten 1:1 wie heute** (MFM-Default), nur sauber gekapselt — das ist der risikoarme Teil.

### 6.2 `HfeImage` (`.hfe`, HxC) — lesend und schreibend

- `readTrack(cyl, head)`: Track-LUT-Eintrag lesen → seitenweise de-interleaven (§7) →
  `BitCodec::decode(cells, bits, enc)` mit `enc` aus Header-`track_encoding` (0=MFM, 2=FM) →
  `TrackImage` (inkl. `encoding` + `bitcells` = Originallänge fürs Zurückschreiben).
- `writeTrack(cyl, head, track)`: `BitCodec::encode(track, bitcells)` → re-interleaven →
  an LUT-Offset zurückschreiben (in-place, da Spurlänge erhalten bleibt).
- `geometry()` + Encoding aus dem HFE-Header (Spur-/Seiten-/Verfahren); self-describing,
  **kein `DiskFormat` nötig**.
- Cache der decodierten Spur in `FloppyDrive` (§8), damit nicht jeder Port-Read neu decodiert.

---

## 7. HFE-Dateiformat (v1 „HXCPICFE", ISOIBM_MFM + ISOIBM_FM)

Implementiert wird **HFE v1** mit **Encoding ISOIBM_MFM (0) und ISOIBM_FM (2)** — MFM für die
5,25″-Laufwerke, FM für das 8″-Laufwerk. HFE v3 („HXCHFEV3", Opcode-basiert) ist als spätere
Erweiterung vorgesehen, aber out of scope.

**Header (512 Byte, am Dateianfang):**

| Offset | Typ      | Feld                  | Wert/Bedeutung                                  |
|--------|----------|-----------------------|-------------------------------------------------|
| 0x00   | char[8]  | signature             | `"HXCPICFE"`                                     |
| 0x08   | u8       | formatrevision        | 0                                                |
| 0x09   | u8       | number_of_tracks      | Zylinderzahl                                     |
| 0x0A   | u8       | number_of_sides       | 1 oder 2                                         |
| 0x0B   | u8       | track_encoding        | 0 = ISOIBM_MFM, 2 = ISOIBM_FM (beide unterstützt)|
| 0x0C   | u16 (LE) | bitrate (kbit/s)      | z. B. 250                                        |
| 0x0E   | u16 (LE) | floppy_rpm            | z. B. 300                                        |
| 0x10   | u8       | floppy_iface_mode     | (Metadatum, erhalten)                            |
| 0x11   | u8       | reserved              |                                                  |
| 0x12   | u16 (LE) | track_list_offset     | in 512-Byte-Blöcken (i. d. R. 1 → 0x200)         |
| 0x14   | u8       | write_allowed         | 0xFF = ja                                        |
| 0x15   | u8       | single_step           | 0xFF = single-step                               |
| …      |          | (Rest reserviert)     | beim Schreiben unverändert erhalten              |

**Track-LUT** ab `track_list_offset*512`, ein Eintrag pro Spur:

| Typ      | Feld       | Bedeutung                                   |
|----------|------------|---------------------------------------------|
| u16 (LE) | offset     | Startblock der Spurdaten (×512 Byte)        |
| u16 (LE) | track_len  | Länge der Spurdaten **beider Seiten** (Byte)|

**Spurdaten:** ab `offset*512`, in **256-Byte-Blöcken seitenverschränkt**:
`[256 B Seite 0][256 B Seite 1][256 B Seite 0]…`. Jeder Seitenblock enthält den **MFM-Bitzellenstrom
LSB-first** (Daten- + Clock-Bits bereits verschränkt). Die Bitzellenzahl je Seite ≈
`(track_len/2) * 8`.

**Schreiben:** Da `writeTrack` die Bitzellenlänge erhält (`TrackImage::bitcells`), bleibt `track_len`
konstant → reines In-place-Überschreiben der betroffenen Blöcke; Header und LUT unverändert.

Referenz: HxC „HFE file format" (öffentlich dokumentiert; Felder oben sind die benötigte Teilmenge).

---

## 8. `FloppyDrive`-Umbau

`FloppyDrive` hält künftig ein `std::unique_ptr<DiskImage>` statt Pfad+`DiskFormat`+Inline-IO,
cached die aktuelle Spur und **kennt sein `DriveProfile`** (welches physische Laufwerk am Slot hängt).

```cpp
// core/peripherals/floppy_drive/floppy_drive.h (Auszug, NEU)
class FloppyDrive {
public:
    /// Profil = physisches Laufwerk am Slot (Maschinenkonfiguration, §3.A).
    explicit FloppyDrive(DriveProfile profile = {});

    /// Mountet ein Image. Prüft Geometrie + Encoding gegen das DriveProfile;
    /// liefert false (und Grund über lastError()) bei Inkompatibilität.
    bool mount(std::unique_ptr<DiskImage> img, bool write_protect = false);
    void unmount();                       // flush() + Reset
    bool isMounted()      const;
    bool isWriteProtect() const;
    void setWriteProtect(bool wp);
    const char* lastError() const;

    bool    step(bool inward);            // begrenzt durch profile_.num_cyls
    uint8_t currentCylinder() const;

    const DriveProfile& profile() const { return profile_; }
    /// Index-Periode in Z80-Takten aus profile_.rpm (5,25″ 300 / 8″ 360 U/min).
    int indexPeriodCycles(uint32_t cpu_hz) const { return profile_.indexPeriodCycles(cpu_hz); }

    /// Aktuelle Spur (cyl, head) als Track-Bytestrom (inkl. encoding). Cached; lazy decodiert.
    const TrackImage& track(uint8_t head);

    /// Markiert die gecachte Spur als verändert (vom Controller nach Schreibzugriff).
    void markTrackDirty(uint8_t head);

    /// Schreibt dirty-Spur(en) via DiskImage::writeTrack zurück.
    bool flush();

    DiskGeometry geometry() const;

private:
    DriveProfile               profile_;
    std::unique_ptr<DiskImage> image_;
    uint8_t cur_cyl_ = 0;
    // einfacher 1-Spur-Cache je Kopf reicht (Lesekopf liest jeweils eine Spur):
    struct CachedTrack { TrackImage img; uint8_t cyl=0xFF; bool dirty=false; bool valid=false; };
    CachedTrack cache_[2];
};
```

Beim Zylinderwechsel (`step`) oder `unmount` werden dirty-Spuren via `writeTrack` zurückgeschrieben.
Die alten `readSector`/`writeSector`/`sectorOffset` entfallen aus `FloppyDrive` (wandern in
`RawSectorImage`). Der Step-Bereich wird durch `profile_.num_cyls` begrenzt (40/77/80).

---

## 9. `K5122`-Controller-Umbau

### 9.1 Was WEGFÄLLT (Datenpfad-Sonderfälle)

Aus `k5122.cpp`/`.h` entfernen: `doReadSector` (Block-/IDAM-Synthese), `buildField`/`advanceField`,
`loaderCrc16` (→ `TrackCodec`), `sector_block_`, `sector_data_len_`, `stream_continuous_`,
`field_*`, `kGapByte`, die `!stream_continuous_`-Gates und das Gap-Padding/Auto-Advance in
`ioRead(0x16)`.

### 9.2 Was BLEIBT (Controller-/Arbitrierungslogik)

PIO-Objekte, Port-Dispatch (0x10–0x18), Drive-Select (8212), `updateStatusPortB`,
Step-/Strobe-Kantenerkennung, Head-Latch über `/STR`-bit2, BUSRQ-Arbitrierung
(`dmaUpdate`/`endDmaTransfer`), Index-Puls (`update`), Interrupt-Daisy-Chain, LED-Sim.

### 9.3 Neues Lesekopf-Streaming-Modell (das Herzstück)

Der Controller modelliert einen **Lesekopf über der rotierenden Spur**:

```cpp
// K5122 private state (NEU, ersetzt sector_buf_/field_* etc.)
const TrackImage* cur_track_ = nullptr;  ///< aktive Spur (von FloppyDrive::track)
size_t head_pos_  = 0;                   ///< Position des Lesekopfs (Byte)
bool   locked_    = false;               ///< Datenseparator auf eine Marke synchronisiert
bool   transferring_ = false;
bool   write_mode_   = false;
```

**Track wählen (`/STR`-Lese-Flanke):** Head/Seite aus bit2 latchen (wie heute), dann
`cur_track_ = &drives_[sel].track(current_head_); transferring_ = true; locked_ = false;`
Kein Synthese-Aufruf mehr — die Spur *ist* schon fertig.

**Byte lesen (`ioRead(0x16)` während Lesetransfer):**
```cpp
uint8_t b = cur_track_->bytes[head_pos_];
head_pos_ = (head_pos_ + 1) % cur_track_->size();   // Rotation
return b;
```
Gaps, Sync, IDAM, DATA, CRC sind **reale Bytes** der Spur → kein Padding, keine Sonderfälle.

**Re-Sync-Strobe (MK bit1 ODER MK1 bit4, steigende Flanke):** Beide Strobes bedeuten dasselbe —
„Datenseparator auf die nächste Adressmarke synchronisieren":
```cpp
head_pos_ = cur_track_->nextMark(head_pos_);   // zur nächsten Marke vorrücken
locked_   = true;
```
Dies vereint das frühere diskrete (MK/bit1) und das kontinuierliche (MK1/bit4) Modell zu **einer**
Regel. Der kontinuierliche Leser (3. Stufe) strobt nur am Sektoranfang und liest per INIR über
IDAM→gap2→DAM→Daten→CRC durch — alles reale, physisch benachbarte Bytes. Der diskrete Leser
(Sekundärlader) strobt zwischen den Feldern und springt jeweils zur nächsten Marke.

> **Validierungs-Knopf (vorsorglich eingebaut):** Sollte sich zeigen, dass der diskrete Leser eine
> *typgenaue* Synchronisation braucht (IDAM↔DATA-Wechsel), ist `MarkType` bereits granular —
> `nextMark(pos, MarkType::Id|Data)` erlaubt es, MK/MK1 auf „nächste Id-" bzw. „nächste Data-Marke"
> abzubilden, **ohne Redesign**. Erst auf `MarkType::None` (beliebige Marke) implementieren, nur bei
> Boot-Regression auf Typgenauigkeit verfeinern.

**Schreiben:** Port-`0x14`-Bytes nur bei armiertem Schreibtransfer (`dma_is_write_`) aufnehmen
(bestehende, korrekte Regel — kein „Read-Echo"-Hack mehr nötig, da sie als normale Controller-Regel
formuliert ist). Die geschriebenen Datenbytes werden in das **Datenfeld der gecachten Spur** an
`head_pos_` eingetragen, die CRC neu berechnet (`TrackCodec::crc16`), und
`drives_[sel].markTrackDirty(head)` gesetzt; `FloppyDrive::flush()` schreibt beim Spur-/Mountwechsel
via `DiskImage::writeTrack` zurück. Für reine Sektor-Writes genügt es, das adressierte Datenfeld zu
ersetzen und die CRC zu erneuern.

### 9.4 BUSRQ-Arbitrierung / Track-Ende (separat, erhalten)

Die ZVE1↔ZVE2-Übergabe (`dmaUpdate`, `endDmaTransfer`, BUSRQ assert/release) bleibt funktional
erhalten. Die heutige `OUT(13H),03H`-Track-Ende-Heuristik mit `!stream_continuous_`-Gate verliert
ihre Datenpfad-Kopplung; sie muss als **reine Arbitrierungsregel** neu formuliert werden:

- **Primär (Refactor-Scope):** Verhalten 1:1 portieren, aber das Gate von `stream_continuous_` auf
  ein arbitrierungsnahes Kriterium umstellen (z. B. „Track vollständig gestreamt / Lesekopf hat seit
  letztem `/STR` ≥ eine Umdrehung geliefert"), damit es nicht mehr an der Sektorgröße hängt.
- **Empfohlene Folgearbeit (separater Schritt, §13):** `/BUSRQ` korrekt aus `/STR` ableiten (echtes
  Hardwareverhalten: `/STR=1` unterdrückt `/BUSRQ`), wodurch die `OUT(13H),03H`-Heuristik **ganz**
  entfallen kann. Nicht im ersten Refactor-Schritt umsetzen (höheres Boot-Regressionsrisiko).

### 9.5 Verfahren (/HF), Drehzahl und Encoding-Agnostik

Das Streaming-Modell (§9.3) ist **identisch für FM und MFM** — es streamt nur Bytes und springt zu
Marken; ob die Spur FM oder MFM ist, steckt allein im `TrackImage` (vom Backend gebaut/decodiert).
Der Controller muss daher *nichts* am Datenpfad verfahrensspezifisch tun. Er pflegt nur zwei
hardwarenahe Nebeneffekte:

- **/HF (Steuer-PIO Tor B, B₂):** Der Controller setzt das /HF-Statusbit passend zum Encoding der
  aktiven Spur (MFM 5,25″ → HF=1 / 500 kHz; MFM 8″ → HF=0 / 1 MHz; FM 8″ → HF=1). Reines
  Status-/Anzeigeverhalten; den Bytestrom beeinflusst es **nicht** (Daten sind bereits decodiert).
  Quelle des Encodings: `drives_[sel].track(head).encoding`.
- **Index-Periode:** statt der festen `kIndexPeriodCycles` nun `drives_[sel].indexPeriodCycles(cpu_hz)`
  aus der Laufwerksdrehzahl (5,25″ 300 / 8″ 360 U/min). Damit stimmt das Index-Timing je Laufwerkstyp.
  **Achtung Boot-Timing:** für das bestehende 5,25″-MFM-Laufwerk denselben effektiven Wert wie heute
  beibehalten (≈490 000 Takte) — die Boot-Kette ist timing-empfindlich (§13).

Die `/MK`/`/MK1`-Strobes wirken weiterhin nur als Re-Sync-Auslöser (§9.3), unabhängig vom Verfahren.

---

## 10. Mount-Pfad, Factory, C-API, GUI

### 10.1 `A5120Machine` / `K5122::mountDisk`

`A5120Machine::mountDisk(drive, path, format_name, wp)` (a5120.cpp Z. 221–233):
- HFE/self-describing erkannt (Signatur) → `DiskImage::open(path, std::nullopt, wp)`;
- sonst `format_name` in `disk_formats_` auflösen → `DiskImage::open(path, *fmt, wp)`.
- Ergebnis an `K5122::mountDisk(drive, std::unique_ptr<DiskImage>, wp)` → `FloppyDrive::mount(...)`,
  das die Geometrie/Encoding gegen das **`DriveProfile` des Slots** prüft (§3.A.3).

`format_name` darf für HFE leer/`""` sein. `DiskFormat`-Auflösung bleibt nur für Raw nötig.

**Laufwerksprofile pro Slot** sind Maschinenkonfiguration: Die 4 `FloppyDrive` werden in `K5122`
mit ihrem `DriveProfile` konstruiert. Default = `mfs_525_ds80` (heutiges Verhalten, alle 4 Slots).
Abweichende Bestückung (z. B. Slot 2 = `mf3200_8_ss77`) wird beim Bau der Maschine gesetzt — analog
zu den compile-time Config-Structs der übrigen Karten. Optional zur Laufzeit über die C-API (§10.2),
solange kein Image gemountet ist.

### 10.2 C-API (`core/api/k1520_api.{h,cpp}`)

`k1520_mount_disk(h, drive, image_path, format_name, write_protect)` bleibt **ABI-stabil**.
Semantik erweitert: `format_name` wird bei HFE ignoriert (darf `NULL`/`""` sein). Optional ergänzen
(neue Symbole, additiv, ABI-kompatibel):
```c
/** @brief Geometrie eines gemounteten Images abfragen (für UI). */
bool k1520_disk_geometry(K1520Handle h, int drive,
                         uint8_t* num_cyls, uint8_t* num_heads);
/** @brief Schreibänderungen persistieren (für HFE/Raw gleich). */
bool k1520_disk_flush(K1520Handle h, int drive);
/** @brief Laufwerksprofil eines Slots per Name setzen (nur ohne gemountetes Image).
 *  Namen: "mfs_525_ds80", "ss_525_40", "mf3200_8_ss77", "mf6400_8_ds77". */
bool k1520_set_drive_profile(K1520Handle h, int drive, const char* profile_name);
```

### 10.3 GUI (`app/`)

Datei-Dialog um `*.hfe` erweitern; Format-Auswahl nur anzeigen/erzwingen, wenn das Image **nicht**
self-describing ist (also für `.img`). Bei HFE Geometrie/Verfahren aus `k1520_disk_geometry` anzeigen.
Beim Auswerfen/Schließen `k1520_disk_flush` aufrufen. Pro Laufwerks-Slot optional eine
Profil-Auswahl (`k1520_set_drive_profile`, nur bei leerem Slot), damit der Nutzer ein 8″-FM- oder
40-Spur-Laufwerk konfigurieren kann.

---

## 11. Migrationsplan (Phasen mit Regressions-Gates)

Jede Phase ist für sich baubar und testbar. **Gate nach jeder Phase:** `ctest` grün gegenüber
Baseline (bekannte pre-existing Failures beachten) **und** Boot-Regression (§12.3) unverändert.

1. **TrackImage + TrackCodec (MFM) + CRC-Vereinheitlichung.** Neue Dateien, reine Unit-Tests
   (§4.1-Assertions, Build/Parse-Roundtrip). Noch nicht in K5122 verdrahtet. *Risiko: gering.*
2. **DiskImage-Schnittstelle + RawSectorImage (MFM).** Kapselt das heutige `.img`-Verhalten.
   Unit-Test: `RawSectorImage::readTrack` erzeugt eine Spur, deren geparste Sektoren bitgleich zu
   `FloppyDrive::readSector` (alt) sind. *Risiko: gering.*
3. **DriveProfile + FloppyDrive auf DiskImage umstellen** (alte Sektor-IO entfernen; Profil/Index aus
   `rpm`). Default `mfs_525_ds80` → kein Verhaltenswechsel. *Risiko: mittel.*
4. **K5122 auf Lesekopf-Streaming umstellen** (§9.3). Datenpfad-Sonderfälle entfernen.
   **Hier liegt das Boot-Risiko** — Boot-Regression (§12.3) ist das harte Gate; bei Mis-Sync den
   `MarkType`-Knopf (§9.3) ziehen. Index-Timing des 5,25″-Profils unverändert lassen. *Risiko: hoch.*
5. **BitCodec (MFM) + HfeImage (MFM)** (lesen, dann schreiben). Unit-Tests gegen Referenz-`.hfe`.
   Factory-Sniffing in `DiskImage::open`. *Risiko: mittel (isoliert vom Boot).*
6. **Mount/C-API/GUI** für HFE + Laufwerksprofil-Auswahl. *Risiko: gering.*
7. **Schreibpfad** (`writeTrack`) für beide Backends + Write-Roundtrip-Tests. *Risiko: mittel.*
8. **FM-Erweiterung.** `TrackCodec`/`BitCodec` um FM (§4.2/§5), Profile `mf3200_8_ss77`/
   `mf6400_8_ds77`, `crc16Ccitt`. Unit-Tests + FM-HFE-Lese/Schreib-Roundtrip + 8″-IBM-3740-Smoke.
   Voll vom Boot-Pfad isoliert (Boot ist MFM). *Risiko: mittel.*
9. **(Optional, separat) `/BUSRQ`-aus-`/STR`** (§9.4), Entfernen der `OUT(13H),03H`-Heuristik.

---

## 12. Testplan

Tests liegen unter `tests/cpp/` (GoogleTest, via ctest). Vorhandene betroffen:
`test_k5122.cpp`, `test_floppy.cpp`, `test_format_parser.cpp`, `test_boot_integration.cpp`.

### 12.1 Neue Unit-Tests
- **`test_track_codec.cpp`** — CRC-Vereinheitlichung (§4.1-Assertions); `buildTrack`/`parseTrack`
  Roundtrip für 128/256/512/1024-B-Sektoren und **gemischte** Tracks, je für **MFM und FM**; Marken
  an erwarteten Positionen; FM nutzt 0xFF-Gap und Einzel-Mark-Bytes (kein A1).
- **`test_bit_codec.cpp`** — `encode∘decode == identity` für eine Beispielspur in MFM **und** FM;
  A1/C2-Sync (MFM) bzw. Sonder-Clock-Marken (FM) werden erkannt; Datenbytes `0xA1`/`0xFE` werden
  **nicht** fälschlich als Marke erkannt.
- **`test_hfe_image.cpp`** — `readTrack` einer Referenz-`.hfe` (MFM **und** eine FM-`.hfe`) liefert
  dieselben Sektoren wie das äquivalente `.img`+Format; `writeTrack`→`readTrack`-Roundtrip;
  Header/LUT nach Schreiben intakt; `track_encoding` korrekt interpretiert.
- **`test_disk_image_raw.cpp`** — `RawSectorImage` reproduziert altes `readSector`-Verhalten bitgleich.
- **`test_drive_profile.cpp`** — `indexPeriodCycles` für 300/360 U/min; Mount-Kompatibilität
  (FM-Image an MFM-Laufwerk → Fehler; 80-Spur-Image an 40-Spur-Laufwerk → Fehler).

### 12.2 Geänderte Tests
`test_k5122.cpp`/`test_floppy.cpp` auf die neue Streaming-/`DiskImage`-API umstellen
(Mock-`DiskImage` mit fester `TrackImage` erleichtert Controller-Tests stark).

### 12.3 Boot-Regression (hartes Gate für Phase 4)
- Per-Stage-Integrationstests in `test_boot_integration.cpp` müssen grün bleiben.
- `boot_trace -L /dev/null disks/cpadisk01.img` muss weiterhin **Banner** zeigen und
- `boot_trace -L /dev/null -c 40000000 -p 38000000 disks/cpadisk01.img` muss `@OS.COM` weiterhin
  bis zum heutigen Stand laden (siehe `doc/K1520_architecture.md` §14.6b — der Rest-Timeout `'U'`
  ist Vorzustand, **keine** neue Regression).
- Testdaten: dieselbe Diskette zusätzlich als `.hfe` bereitstellen (via HxC-Tooling oder einen
  kleinen Konverter `.img`→`.hfe`) und einen Boot-Smoke-Test damit fahren — beweist die Agnostik.

---

## 13. Risiken und offene Punkte

- **Boot-Handshake (höchstes Risiko).** Die ZVE1↔ZVE2-Arbitrierung ist empfindlich (siehe die
  Fix-Historie in CLAUDE.md/§14.6). Der Datenpfad-Refactor darf sie nicht verändern; Phase 4 wird
  ausschließlich über die Boot-Regression abgesichert. Der granulare `MarkType` ist der Fallback,
  falls die „nächste-Marke"-Regel mis-synct.
- **`OUT(13H),03H`-Heuristik.** Bleibt zunächst als Arbitrierungsregel erhalten (entkoppelt von der
  Sektorgröße). Sauberer Endzustand = `/BUSRQ` aus `/STR` (Phase 8, separat).
- **CRC-Annahme (MFM).** §4.1 ist die erwartete, aber numerisch noch zu bestätigende
  Vereinheitlichung; der dortige Unit-Test ist die erste umzusetzende Aufgabe und entscheidet den
  CRC-Teil. **FM (§4.2):** IBM-3740 nutzt Standard-CRC-16-CCITT; bis belegt ist, dass die
  Robotron-`crc16` damit identisch ist, verwenden FM-Tracks `crc16Ccitt` getrennt. Der MFM-Boot-Pfad
  bleibt davon unberührt.
- **HFE-Schreiben.** Setzt voraus, dass die Bitzellenlänge erhalten bleibt (`TrackImage::bitcells`);
  Formatierung/Neulayout einer Spur (variable Länge) ist Folgearbeit. Gilt für MFM und FM gleich.
- **Index-Puls / Drehzahl.** Die Index-Periode kommt künftig aus `DriveProfile::rpm`
  (5,25″ 300 / 8″ 360 U/min). Für das bestehende 5,25″-MFM-Laufwerk den **effektiven Wert wie heute**
  beibehalten (≈490 000 Takte) — die Boot-Kette ist timing-empfindlich. Optional `update()` an den
  `head_pos_`-Wrap koppeln (eine Umdrehung = ein Track-Durchlauf); zunächst unverändert lassen.
- **FM-Pfad ist boot-unkritisch.** Der A5120-Boot läuft ausschließlich über MFM (5,25″). Die gesamte
  FM-/8″-Unterstützung (Phase 8) ist davon isoliert und kann ohne Boot-Regressionsrisiko entwickelt
  werden — Voraussetzung: das 5,25″-MFM-Default-Profil bleibt Standard und unverändert.

---

## 14. Datei-Übersicht

**Neu:**
```
core/peripherals/floppy_drive/track_image.h / .cpp        (TrackImage + Encoding + MarkType)
core/peripherals/floppy_drive/drive_profile.h / .cpp      (DriveProfile + builtinDriveProfile)
core/peripherals/floppy_drive/track_codec.h / .cpp        (FM/MFM Track bauen/parsen + CRC)
core/peripherals/floppy_drive/bit_codec.h / .cpp          (MFM- + FM-Bitzellen ⇆ Bytes)
core/peripherals/floppy_drive/disk_image.h / .cpp         (Factory + open/Sniffing)
core/peripherals/floppy_drive/raw_sector_image.h / .cpp
core/peripherals/floppy_drive/hfe_image.h / .cpp
tests/cpp/test_track_codec.cpp
tests/cpp/test_bit_codec.cpp
tests/cpp/test_hfe_image.cpp
tests/cpp/test_disk_image_raw.cpp
tests/cpp/test_drive_profile.cpp
```

**Geändert:**
```
core/cards/k5122/k5122.h / .cpp          (Streaming-Kopf statt Synthese; Sonderfälle raus;
                                          DriveProfile pro Slot; /HF + Index aus rpm)
core/peripherals/floppy_drive/floppy_drive.h / .cpp   (DiskImage statt Inline-IO; DriveProfile)
core/peripherals/floppy_drive/format_parser.h / .cpp  (TrackFormat/DiskFormat: Feld Encoding, Default MFM)
core/machines/a5120/a5120.cpp            (mountDisk: Factory + Sniffing; DriveProfile-Bestückung)
core/api/k1520_api.h / .cpp              (additiv: geometry/flush/set_drive_profile; format_name optional)
app/…                                    (Datei-Dialog *.hfe, Geometrie/Verfahren, Profil-Auswahl, flush)
tests/cpp/test_k5122.cpp, test_floppy.cpp (auf neue API)
tests/cpp/test_format_parser.cpp         (Encoding-Feld)
CMakeLists.txt                            (neue Quellen + Test-Targets)
doc/design/07_k5122_afs.md, 09_floppy_drive.md  (Modell + FM/Laufwerksprofile aktualisieren)
```

**Unverändert (bewusst):** Legacy-`src/`, `cparun/`.

---

## 15. Implementierungsstand (2026-06-10)

> **Konsolidierung (Endstand):** Die unten als „K5122v2" entwickelte Karte ist nach
> erfolgreichem vollständigem CP/A-Boot zur **alleinigen `K5122`** (`core/cards/k5122/`,
> Klasse `K5122`, Lib `k1520_k5122`, Test `test_k5122`) umbenannt; die alte monolithische
> Synthese-K5122 wurde **entfernt**.  Die `-DUSE_LEGACY_K5122`-Weiche in `a5120.h` ist damit
> ebenfalls entfallen.  Die folgenden Abschnitte nennen aus historischen Gründen weiter
> „K5122v2" — gemeint ist die heutige `K5122`.

Umgesetzt wurde die neue Datenpfad-Architektur als **eigenständige, parallele Karte
`K5122v2`** — *nicht* als In-place-Umbau der alten `K5122`.  Begründung: Die alte Karte
ist eng mit der timing-empfindlichen ZVE1↔ZVE2-Boot-Arbitrierung verflochten (Phase 4 =
höchstes Boot-Regressionsrisiko, §13).  Indem die neue Karte **parallel** existiert und der
A5120-Boot-Pfad (`a5120.cpp`) weiter die alte `K5122` verdrahtet, ist das Boot-Risiko
**vollständig vermieden**: die Boot-Integrationstests laufen unverändert grün.  Die neue
Karte tritt in einer künftigen Maschinenkonfiguration an die Stelle der alten (Phase 6/
„ersetzt später") — diese Verdrahtung ist bewusst noch nicht erfolgt.

### 15.1 Tatsächlich angelegte Dateien (getestet)

```
core/peripherals/floppy_drive/track_image.h / .cpp     TrackImage + Encoding + MarkType + nextMark
core/peripherals/floppy_drive/drive_profile.h / .cpp   DriveProfile + builtinDriveProfile (4 Profile)
core/peripherals/floppy_drive/track_codec.h / .cpp     buildTrack/parseTrack (MFM+FM) + crc16/crc16Ccitt
core/peripherals/floppy_drive/disk_image.h / .cpp      DiskImage-Interface + open()/Sniffing (Raw+HFE)
core/peripherals/floppy_drive/raw_sector_image.h / .cpp  RawSectorImage (.img + DiskFormat)
core/peripherals/floppy_drive/bit_codec.h / .cpp       BitCodec: Bitzellen ⇆ Bytes (MFM+FM, HFE)
core/peripherals/floppy_drive/hfe_image.h / .cpp       HfeImage: HFE v1 (HXCPICFE) lesen+schreiben
core/peripherals/floppy_drive/floppy_drive2.h / .cpp   FloppyDriveV2 (DiskImage + Profil + Track-Cache)
core/cards/k5122v2/k5122v2.h / .cpp                    K5122v2 (Lesekopf-Streaming-Controller)
tools/img_to_hfe.py                                    eigenständiger .img→HFE-v1-MFM-Konverter (Fixtures)
tests/fixtures/cpa_mini.img / cpa_mini.hfe             Test-Fixture (2 Zyl × 2 Köpfe × 4×128B)
tests/cpp/test_track_codec.cpp        (21 Tests)
tests/cpp/test_drive_profile.cpp      ( 9 Tests)
tests/cpp/test_disk_image_raw.cpp     ( 8 Tests, inkl. Bitgleichheit vs. alte FloppyDrive)
tests/cpp/test_floppy_drive2.cpp      (13 Tests)
tests/cpp/test_bit_codec.cpp          (17 Tests, MFM+FM encode∘decode-Identität)
tests/cpp/test_hfe_image.cpp          ( 9 Tests, inkl. unabhängiger Cross-Check HFE↔.img)
tests/cpp/test_k5122v2.cpp            (22 Tests)
```

CMake: neue Libs `k1520_floppy2` (Peripherie, inkl. BitCodec/HfeImage) und `k1520_k5122v2`
(Karte) + die sieben Test-Targets.  Voller `ctest`-Lauf: alle neuen Tests grün; nur die
vorbekannten Baseline-Failures (FormatParser/CPA780, K3526, K7024) rot — **keine neue
Regression**, `test_boot_integration` und `test_k5122` unverändert grün.

**HFE-Backend (Recherche-gestützt, Greaseweazle/HxC).** `HfeImage` liest/schreibt HFE v1
(„HXCPICFE", MFM=0/FM=2), `BitCodec` kapselt die Bitzellen-Ebene (16 Zellen/Byte,
HFE-LSB-first ↔ intern MSB-first via bytereverse, A1-Sync = Zellwort `0x4489`, MFM-Clock
`c_i=¬(d_{i-1}∨d_i)`, FM-Sondertakt C7/D7).  `DiskImage::open` öffnet HFE jetzt via `HfeImage`
(HXCHFEV3/v3 weiterhin out-of-scope → nullptr).  Validierung: ein **eigenständig** aus der
Spec geschriebener Python-Konverter (`tools/img_to_hfe.py`) erzeugt `cpa_mini.hfe`; der
Cross-Check-Test beweist, dass `HfeImage::readTrack`→`BitCodec::decode`→`parseTrack`
**dieselben Sektoren mit gültigen ID-/Daten-CRCs** liefert wie `RawSectorImage` aus der
äquivalenten `.img`.  Nicht-offensichtlicher Fund: ein Daten-`0xC2`/Sync-Kollisionsfall an
nicht-byte-alignierter Bitposition (`mfm_cell_word(0xC2)`=`0x52A4` kann ein spurioses
`0x5224` bilden) — gelöst, indem C2-Syncs als Zellwort `0x5224` statt regulär kodiert werden.

### 15.2 Abweichungen vom Entwurf

- **Standalone-Karte `K5122v2`** statt Umbau von `K5122` (s. o.).  Die alte
  `floppy_drive.{h,cpp}` und `format_parser.{h,cpp}` bleiben **unangetastet** — die neue
  Schicht liegt vollständig daneben.  `RawSectorImage` nimmt das Encoding als
  Konstruktorparameter (Default MFM), statt `TrackFormat` um ein Encoding-Feld zu erweitern,
  damit der gemeinsam genutzte `format_parser`-Header nicht angefasst werden muss.
- **CRC-Vereinheitlichung nur halb bestätigt (wichtig).** Der §4.1-Verifikationstest wurde
  umgesetzt und empirisch geprüft:
  - `crc16([A1,A1,A1], 0xFF,0xFF) == 0xCDB4` **hält** → der 3.-Stufe-Pfad (Seed 0xCDB4 über
    `[FB]+Daten`) ist äquivalent zu einer CRC über `[A1,A1,A1,FB,Daten]` ab 0xFFFF.
  - `crc16([A1,A1,A1,FB], 0xFF,0xFF) == 0xBF84` **hält NICHT** — ergibt `0xE295` (genau der in
    der alten K5122 dokumentierte „alternate path, bit1=0").  Die beiden Boot-Stadien
    erwarten also physisch **unterschiedliche** Daten-CRC-Bytes; eine einzige eingebackene
    CRC kann nicht beide gleichzeitig befriedigen — das war der reale Grund für die alte
    `stream_continuous_`-Verzweigung, kein Synthese-Artefakt.
  - `TrackCodec`/`buildTrack` verwendet daher für die MFM-Daten-CRC den verifizierten
    boot-kompatiblen Seed `crc16(Daten, 0xBF, 0x84)` über die reinen Datenbytes; der
    `buildTrack`↔`parseTrack`-Roundtrip ist damit byte-genau und konsistent.
  - **Im Boot-Pfad gelöst (§15.4):** `TrackCodec::buildRobotronTrack` wählt den Daten-CRC
    pro Sektorgröße — 128 B → `crc16(Daten, 0xBF84)` (Sekundärlader), ≥1024 B →
    `crc16([FB]+Daten, 0xCDB4)` (3. Stufe).  Damit verifizieren beide OS-Stadien korrekt.

### 15.3 Bewusst noch offen (dokumentierte Folgearbeit)

- **HFE v3 / EMU-FM**: nur HFE v1 (HXCPICFE, ISOIBM_MFM/FM) ist implementiert; HXCHFEV3
  (opcode-basiert) bleibt out of scope (`open` → nullptr).
- **FM-HFE-Fixture / 8″-Smoke** (§4.2/§5): der FM-Pfad ist in `BitCodec` und `HfeImage`
  vollständig (encode∘decode-Identität getestet), aber `tools/img_to_hfe.py` erzeugt bislang
  nur MFM; eine FM-Fixture + 8″-IBM-3740-Smoke fehlt noch.  `TrackCodec` baut/parst FM bereits
  (mit `crc16Ccitt`).
- **Schreibpfad** (`commitWrite`/`writeTrack`): funktionsfähiger Roundtrip vorhanden; die
  Mehr-Sektor-Lokalisierung beim Schreiben ist vereinfacht (committet aktuell Sektor 0 bzw.
  den unter dem Kopf liegenden Sektor) — für reale OS-Writes zu verfeinern.
- **Maschinen-Verdrahtung**: erledigt (§15.4) — `a5120.cpp` nutzt jetzt standardmäßig `K5122v2`.
- **C-API + GUI** (§10): `k1520_api.*` und `app/` sind unverändert; die Profil-/Geometrie-/
  HFE-API ist noch nicht angebunden (HFE-Boot-Smoke via GUI offen).
- **BUSRQ aus /STR** (§9.4/Phase 9): unverändert offen (die `OUT(13H),03H`-Track-Ende-Regel
  ist als Arbitrierungsregel erhalten, s. §15.4).

### 15.4 Maschinen-Verdrahtung & vollständiger CP/A-Boot (2026-06-10)

`A5120Machine` verwendet jetzt **standardmäßig** die neue Karte `K5122v2` als Slot-2-Floppy-
Controller (`core/machines/a5120/a5120.h`, `using`/`#if`-Weiche — Rückschalten auf die alte
Karte mit `-DUSE_LEGACY_K5122`).  Mit dieser Verdrahtung **bootet die A5120 die echte Diskette
`disks/cpadisk01.img` vollständig in CP/A** — alle 8 `test_boot_integration`-Subtests grün, der
Bildschirm zeigt `CP/A, Version 25.09.89, TPA 100H-0C205H …`.

Damit der Robotron-Boot-ROM-/Loader-Leser (ZVE2) den von `K5122v2` gestreamten Track akzeptiert,
waren — über das HFE-/Codec-Werk hinaus — **zwei boot-spezifische Anpassungen** nötig (per
ZVE2-Instruktionstrace diagnostiziert):

1. **Robotron-Track-Layout** (`TrackCodec::buildRobotronTrack`, von `K5122v2::startReadTransfer`
   on-the-fly aus dem IBM-Cache-Track erzeugt).  Der generische IBM-Track (3×A1-Sync, führendes
   gap4a + IAM `C2C2C2 FC`, Marke auf dem Mark-Byte) ist mit dem ZVE2-Leser **inkompatibel**:
   dessen IDAM-Suche re-strobt /STR (Kopf an Position 0), pulst MK → `resyncToNextMark` springt
   auf die **erste Marke** = den IAM `FC`, dann liest ZVE2 `buf[0]=FC`, `buf[1]=Gap` und vergleicht
   `buf[1]` mit `0xFE` → Endlosschleife.  Das Robotron-Layout hat **kein IAM, genau ein A1 je Feld
   und die Marke auf dem A1**, sodass nach dem Resync `buf[0]=A1`, `buf[1]=0xFE` ist → Treffer.
   Datenfeld analog (`A1 FB …`); CRC pro Sektorgröße (s. §15.2).  Das generische IBM-/HFE-Layout
   (`buildTrack`/`parseTrack`, Marke auf FE/FB) bleibt davon **unberührt**.
2. **Track-Ende-/BUSRQ-Arbitrierung** (`K5122v2::ioWrite`, Port 0x13).  Der ZVE2-Sekundärlader
   schaltet nach einer vollständig gelesenen 128-B-Spur den Ctrl-PIO-Port-B-Interrupt ab
   (`OUT(13H),03H`) und fällt in seine Idle-Schleife `L0696`.  Ohne Bus-Freigabe würde ZVE2 dort
   weiterlaufen und die Handshake-Variablen `[07F8..07FC]` zerstören; ZVE1 (Wartezustand
   `0x052A–0x0538`) übernimmt nie → Stall direkt nach dem Banner.  Die Regel gibt `/BUSRQ` auf
   `OUT(13H),03H` frei, **gegated auf 128-B-Spuren** (`cur_sector_size_ ≤ 128`) — der 3.-Stufen-
   1024-B-Lader schreibt `OUT(13H),03H` ebenfalls, dort ist es aber nur ein PIO-Steuerwort und
   **kein** Track-Ende (entspricht dem alten `!stream_continuous_`-Gate).

Die übrige Streaming-/Resync-/Index-/Interrupt-Mechanik von `K5122v2` (MK/MK1 → `nextMark`,
Head-Latch bit2, Index aus `rpm`, Daisy-Chain) trug ohne weitere Boot-Sonderfälle.  Der
ZVE1↔ZVE2-Handshake im Run-Loop (`a5120.cpp`, `[0x03F8]`-Beobachtung, `dmaUpdate`/
`endDmaTransfer`) blieb unverändert und funktioniert mit `K5122v2` identisch.

### 15.5 Zweites Image-Paar (`cpadisk_02.img` + `.hfe`) — Boot-Verifikation und offener Post-Boot-Stall

Zwei Encodings derselben Diskette (CP/A-System **ohne Uhr**) dienen als Integrationsnachweis,
dass `K5122v2` **beide Formate** durch alle Stadien bootet:
`disks/cpadisk_02.img` (Raw cpa780) und `disks/cpadisk_02.hfe` (echtes HxC-HFE-v1-Tooling,
`track_encoding=0xFF` → als MFM behandelt).  Integrationstests:
`tests/cpp/test_boot_integration.cpp` → `BootIntegrationCpa02.{Img,Hfe}BootsIntoRunningCpaOs`.

**Ergebnis (beide grün):** Raw-Image **und** HFE booten **byte-äquivalent** bis ins laufende
CP/A (`CP/A, Version 25.09.89, TPA 100H-0C405H` + BIOS-Kaltstart `… TPA ist OK!`).  Damit ist
der **HFE-/BitCodec-/HfeImage-Pfad end-to-end auf einem echten (nicht synthetischen) Image**
belegt — der Controller streamt aus dem HFE-Bitstrom denselben Robotron-Track wie aus der `.img`.

**Offener Punkt — Auto-`dir` + Prompt wird (noch) nicht erreicht.** Direkt nach `TPA ist OK!`
dreht das OS in einer 16-Bit-Divisionsroutine (`sub_C800` @`0xC800`, Schleife `0xC7A3`):
`DE += [0xD1BE]` je Iteration, Abbruch bei `DE > BC` — aber `[0xD1BE] == 0`, also wächst `DE`
nie → Endlosschleife (Division durch 0).  **Diagnose (per ZVE2-/ZVE1-Trace + RAM-Dump):**
- `.img` und `.hfe` hängen am **identischen** PC mit **identischem** Bildschirm → das ist
  **kein** Datenpfad-Unterschied der beiden Formate und **kein** K5122v2-Lese-Fehler, sondern
  ein **OS-Laufzeit-/Timing-Problem** (passend zu „ohne Uhr"): der Divisor `[0xD1BE]` ist ein
  zur Laufzeit zu setzender Wert (Timer-/Format-Erkennungs-Parameter), der 0 bleibt.
- Liegt damit **außerhalb** des Floppy-Refactor-Scopes (Datenpfad/Boot-Kette), gehört zum
  Themenkreis der bekannten Post-Boot-Issues (vgl. CP/A-Kaltstart-Timing).  Folgearbeit:
  Quelle von `[0xD1BE]` bestimmen (Timer-ISR/CTC vs. automatische Format-Erkennung) und
  bereitstellen; danach die Tests auf `dir`-Ausgabe + Prompt verschärfen.

> **Bestätigung (2026-06-10):** `disks/cpadisk_mitUhr_01.{img,hfe}` — dieselbe Diskette **mit
> aktiver Uhr** — bootet **vollständig** bis zum Zeit-Eingabe-Prompt `Bitte Uhrzeit eingeben!`
> und hängt **nicht** in der `0xC7A3`-Schleife.  Damit ist belegt, dass der Stall von
> `cpadisk_02` **die fehlende Uhr** ist (`[0xD1BE]` wird vom Uhren-/Timer-Pfad gesetzt) und
> **kein** Floppy-/Boot-Problem.

### 15.6 Boot von Laufwerk B: und C: (Drive-Select-Verifikation)

Um Loader **und** K5122v2-Laufwerksauswahl jenseits von A: zu prüfen, wird die bootfähige
Diskette `cpadisk_mitUhr_01` **nicht** in A:, sondern in **B: (Drive 1)** bzw. **C: (Drive 2)**
gemountet, die niedrigeren Laufwerke bleiben **leer**.  Integrationstests:
`tests/cpp/test_boot_integration.cpp` → `BootIntegrationDriveBC.{ClockImg,ClockHfe}_FromDrive{B,C}`
(4 Tests, **alle grün**).

**Mechanik (per ZVE2-/ROM-Trace bestätigt):** Die Laufwerks-Suchschleife des ZRE-Boot-ROM
(`DRIVE_DETECT_LOOP` `0x0110`) liest den Drive-Status (`IN 0x12`), prüft `/TO` (bit7) und
rotiert bei „nicht bereit" nach ~80 Retries (Schritt-Puls-Delay) über `L0140` das
8212-Select-Byte `0xEE → 0xDD → 0xBB → 0x77` (A:→B:→C:→D:, `RLCA`), bis ein Laufwerk eine
Diskette bei Spur 0 meldet.  Eine **leere** K5122v2-Drive liefert Status `0xF5` (`/RDYL=1`,
`/TO=1`) → der ROM überspringt sie und bootet vom ersten bestückten Laufwerk.

**Ergebnis:** `.img` **und** `.hfe` booten aus **B:** und **C:** vollständig bis zum
Zeit-Prompt; K5122v2 bedient Drive 1 und Drive 2 korrekt.  C: erreicht den Prompt einige
Millionen Takte später als B:, weil der ROM erst die leeren Laufwerke A: und B: mit je ~80
Retries durchsucht (deterministisch; das Test-Budget deckt den langsamsten Fall ab).

Hilfsmittel: `boot_trace` hat eine neue Option **`--drive N`** (mountet auf Laufwerk N, die
niedrigeren bleiben leer), z. B. `boot_trace --drive 2 -p 100000000 disks/cpadisk_mitUhr_01.hfe`.

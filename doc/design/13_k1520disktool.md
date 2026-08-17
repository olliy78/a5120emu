# Feinentwurf: k1520DiskTool — Dateiaustausch mit K1520-Disketten

**Module (neu):** `core/filesystem/`, `core/api/k1520_disk_api.*`, `tools/k1520disktool.cpp`,
`app/disktool/`, `app/core_binding/k1520disk.py`
**Wiederverwendet:** `core/peripherals/floppy_drive/` (Container-Codecs, Medium, Spur-Codec),
`core/util/yaml_lite.*`, `data/formats.yaml`
**Verwandt:** `doc/design/09_floppy_drive.md` (Medium/Container), `doc/udos_diskettenformat.md`
(UDOS-Dateisystem — die maßgebliche Spezifikation), `doc/K1520_architecture.md` §8.6 (Formatkatalog)

**Stand:** Entwurf, 2026-08-09 · Branch `create_disktool`

---

## 1. Aufgabe

**k1520DiskTool** tauscht Dateien zwischen einem Linux-Verzeichnis und einem
K1520-Diskettenabbild aus — in beide Richtungen, für **CP/M-artige** (CP/A, SCPX) und für
**UDOS/ZDOS**-Disketten, auf `.img`, `.hfe` und `.dmk`.

Es löst die beiden Tkinter-Werkzeuge `readDiskUI.py` / `writeDiskUI.py` des Projekts
*CPA_Workbench* ab, die dafür die Fremdprogramme `cpmls`/`cpmcp` aus `cpmtools` als
Unterprozesse aufrufen und deren `diskdefs` als zweiten, unabhängigen Formatkatalog pflegen.

**Funktionsumfang** (entschieden, s. §2):

| | |
|---|---|
| **Auflisten** | Verzeichnis der Diskette mit Name, Größe, Typ, Eigenschaften, Datum |
| **Extrahieren** | einzelne Dateien oder alles in ein Zielverzeichnis |
| **Einfügen** | einzelne Dateien oder ein ganzes Verzeichnis auf die Diskette |
| **Löschen** | Datei von der Diskette entfernen |
| **Leerdiskette** | neues Abbild mit **initialisiertem Dateisystem** anlegen (CP/M und UDOS) |
| **Prüfen** | Medien-/Dateisystemzustand berichten (CRC-Fehler, Kettenbrüche, Belegungswidersprüche) |

Vier Verhaltensregeln sind Vorgabe und durchziehen den ganzen Entwurf:

- **Eine beidseitige UDOS-Diskette ist EIN Datenträger.** Beide Seiten werden gemeinsam
  geöffnet und gemeinsam angezeigt; beim Extrahieren landen sie in den Unterverzeichnissen
  `Side0/` und `Side1/`, beim Einfügen muss der Quellordner genau diese Unterverzeichnisse
  haben — sonst Fehlermeldung (§9.1).
- **Der Inhalt ist sofort und immer aktuell sichtbar**: nach dem Laden und nach *jeder*
  schreibenden Aktion wird die Ansicht aus dem Medium **neu gelesen** (§9.3).
- **Passt es nicht, wird gar nicht erst geschrieben**: Platzbedarf wird vor der ersten
  Änderung geprüft, ein Fehlschlag nimmt die ganze Stapeloperation zurück (§9.2).
- **`.hfe`/`.dmk` erkennen ihr Format selbst**; passt ein Abbild zu keinem Eintrag in
  `formats.yaml`, bricht das Werkzeug mit einer Meldung ab, die die gemessene Geometrie nennt
  (§12).

**Nicht im Umfang:** Attribute ändern, Umbenennen, Datenträgername setzen, Reparatur
(fsck-artig), physische Laufwerke. Für Letzteres wird die Schnittstelle vorgesehen (§13),
nicht die Umsetzung.

---

## 2. Entscheidungen

| # | Frage | Entscheidung |
|---|-------|--------------|
| E1 | Bibliotheksschnitt | **Eigene `libk1520disk.so`** mit eigener C-API (`core/api/k1520_disk_api.h`). Sie bindet dieselben **statischen** Bausteine wie der Emulator (`k1520_floppy2`, `k1520_util`), aber **keinen** Z80-/Karten-Code. Die Emulator-ABI bleibt unverändert. |
| E2 | Oberfläche | **Eigenständige PySide6-Anwendung** unter `app/disktool/`, Start über `run_disktool.sh`. Teilt sich mit der Emulator-GUI die Hilfsmodule (`config_io`, Stil, Symbol), nicht den Prozess. |
| E3 | Schreibumfang | Kopieren rein/raus **+ Löschen + Leerdiskette anlegen (mkfs)**. |
| E5 | Schreibsicherheit | **Schreibgeschützt öffnen statt atomar schreiben** (§14). Beim Lesen soll nichts kaputtgehen können; Schreiben verlangt einen bewussten Schritt. |
| E4 | Hardware (Greaseweazle) | **Später.** Der Entwurf sieht eine Quellen-Abstraktion (Datei / Gerät) vor, damit `gw read/write` ohne Umbau andocken kann; die erste Ausbaustufe kennt nur Dateien. |

**Getroffene Annahmen** (nicht rückgefragt, weil ein anderer Weg die Arbeit nicht wesentlich
ändert — jederzeit umstellbar):

- **A1** Beim Öffnen werden Geometrie und Dateisystem **automatisch erkannt** (§12); der
  Anwender kann manuell übersteuern. Eine unsichere Erkennung darf niemals stillschweigend
  schreiben.
- **A2** Übertragung standardmäßig **binär**; Textkonvertierung (CR LF ↔ LF, `0x1A`-Ende,
  UDOS: CR ↔ LF) ist ein **explizit wählbarer** Modus, mit Vorschlag anhand der Endung
  bzw. des UDOS-Dateityps `A`.
- **A3** CP/M-**Nutzerbereiche** (User 0…15) werden unterstützt und angezeigt; Vorgabe ist
  User 0. Namensform `3:NAME.TYP` wie bei `cpmls`.
- **A4** Bei UDOS ist **jede Diskettenseite technisch ein eigenes Dateisystem**
  (`doc/udos_diskettenformat.md` §2) — für den Anwender aber **eine Diskette**. Das Werkzeug
  mountet daher beide Seiten zusammen und zeigt sie in *einer* Liste; die Trennung wird in der
  Ordnerstruktur (`Side0/`, `Side1/`) abgebildet, nicht durch Umschalten (§9.1). Die Zahl der
  Seiten bestimmt die Ordnerform: **ein** Dateisystem → flacher Ordner, **mehrere** → `SideN/`.
  CP/M-Disketten haben immer genau ein Dateisystem, auch beidseitige — dort gibt es also keine
  Unterverzeichnisse.
- **A5** **UDOS auf `.img` ist unmöglich** und wird abgelehnt — die Verkettung steht hinter der
  Daten-CRC und existiert in einem rohen Sektorabbild nicht (`rawCompatible()`,
  `doc/udos_diskettenformat.md` Vorspann). Das Werkzeug sagt das im Klartext, statt eine
  halbe Diskette zu erzeugen.
- **A6** Das Werkzeug schreibt **nie in eine Datei, die gerade im Emulator gemountet ist** —
  es kann das nicht erkennen; deshalb Vorgabe „Sicherungskopie anlegen“ (§14).

---

## 3. Einordnung in den Baum

```
core/
  peripherals/floppy_drive/     ← unverändert, WIRD BENUTZT
                                  (+ TrackCodec::writeSector, §4.1) ✅
  filesystem/                   ← NEU: alles Logische
      sector_space.{h,cpp}          Sektorraum über ein DiskMedium (§5)          ✅
      geometry_probe.{h,cpp}        Geometrie messen + Katalogabgleich (§12.1)   ✅
      fs_profile.h                  Datenmodell der `filesystems:`-Sektion (§6)  ✅
      fs_catalog.{h,cpp}            lädt sie aus formats.yaml (yaml_lite)        ✅
      file_system.h                 Fassade eines Volumes (§9)                   ✅
      disk_volume.{h,cpp}           die Diskette als Ganzes (§9.1–9.3, §12)      ✅
      cpm/
          cpm_fs.{h,cpp}            CP/M-2.2-Dateisystem (§7)                    ✅
      udos/
          udos_bitmap.{h,cpp}       Belegungskarte Spur 23 (§8)                  ✅
          udos_fs.{h,cpp}           UDOS/ZDOS-Dateisystem (§8) — lesen+schreiben ✅
  api/
      k1520_disk_api.{h,cpp}    ← NEU: C-ABI von libk1520disk.so (§10)          ✅

tools/
  k1520disktool.cpp             ← NEU: CLI (§11.1)                              ✅
  k1520disktool.md              ← NEU: Referenz, Stil wie k1520dbg.md

app/
  disktool/                     ← NEU: PySide6-Anwendung (§11.2)
      main.py, ui/…                                                            ✅
  core_binding/
      k1520disk.py              ← NEU: ctypes-Bindung an libk1520disk.so        ✅

data/formats.yaml               ← ERWEITERT: `filesystems:` + UDOS-Geometrien (§6)
run_disktool.sh                 ← NEU (Analogon zu run_gui.sh)                  ✅
```

> **Warum unter `core/`?** `core/` ist im Projekt die C++-Seite mit Include-Wurzel
> Projektwurzel; `core/filesystem/` steht **neben**, nicht *unter* der Hardware-Emulation und
> hängt nur von `core/peripherals/floppy_drive/` ab (nie umgekehrt). Ein eigener Wurzel-Ordner
> `disktool/` wäre die Alternative, verdoppelte aber Include-Pfade und CMake-Konventionen ohne
> Gewinn.

---

## 4. Wiederverwendung — was der Floppy-Stack schon liefert

Das ist der eigentliche Grund, das Werkzeug **in** diesem Projekt zu bauen: die untere Hälfte
existiert bereits, ist im Bootpfad des Emulators täglich in Benutzung und durch das Testsystem
abgesichert.

```
   Datei (.img/.hfe/.dmk)
        │  ImageCodec::detect + load          ← vorhanden
        ▼
   DiskMedium  (alle Spuren als TrackImage)   ← vorhanden
        │  TrackCodec::parseTrack             ← vorhanden, liefert data + tail
        ▼
   SectorSpace (§5)                           ← NEU, dünn
        │
        ▼
   CpmFileSystem / UdosFileSystem (§7/§8)     ← NEU, der eigentliche Inhalt
```

| Baustein | Zustand |
|----------|---------|
| `DiskImage::open` / `saveAs` / `flush` | **fertig** — erkennt `.img`/`.hfe`/`.dmk`, bindet die Datei, schreibt zurück |
| `DiskImage::createBlank` / `create` | **fertig** — unformatiert bzw. gültig formatiert (echte IDAM/DATA/CRC) |
| `DiskMedium`, `TrackImage`, Marken | **fertig** |
| `TrackCodec::parseTrack` | **fertig** — inkl. `LogicalSector::tail` (die 4 UDOS-Kontrollbytes) |
| `TrackCodec::buildTrack`, CRC | **fertig** |
| `FormatCatalog` + `data/formats.yaml` | **fertig** für die Geometrie; die logische Ebene fehlt (§6) |
| `yaml_lite` | **fertig** — der Katalog-Parser des Kerns |
| **Sektor an Ort und Stelle schreiben** | **fehlt** — s. §4.1 |

### 4.1 Die eine nötige Ergänzung im Kern: `TrackCodec::writeSector`

Zum Schreiben brauchen wir „ersetze das Datenfeld von Sektor *id* in dieser Spur, rechne die
Daten-CRC neu, lasse alles andere **byteweise unangetastet**“. Diese Funktion gibt es nicht:

- `buildTrack()` baut eine Spur **komplett neu** aus logischen Sektoren — dabei gehen die
  `tail`-Bytes verloren, also bei UDOS **das gesamte Dateisystem**;
- der Schreibpfad des `K5122` patcht die Spur zwar an Ort und Stelle, aber gebunden an die
  Kopfposition und den Portstrom des Gastsystems — nicht als aufrufbare Funktion.

Vorschlag (additiv, in `track_codec.h`, ohne jede Änderung an vorhandenem Verhalten):

```cpp
/// @brief Datenfeld (und optional die Bytes hinter der CRC) eines Sektors ersetzen.
/// Findet den Sektor über seine ID, schreibt @p data, rechnet die Daten-CRC neu und
/// lässt Gaps, Marken, ID-Feld und Spurlänge unverändert.
/// @param tail  optional; leer = die vorhandenen Bytes hinter der CRC bleiben stehen.
/// @return false, wenn die ID nicht gefunden wurde oder die Länge nicht zur ID passt.
bool writeSector(TrackImage& track, uint8_t sector_id,
                 const std::vector<uint8_t>& data,
                 const std::vector<uint8_t>& tail = {});
```

Guards: Roundtrip `parseTrack ∘ writeSector` byte-identisch außer im ersetzten Feld; CRC gültig;
`tail` bleibt bei leerem Argument erhalten (der UDOS-kritische Fall); FM **und** MFM.

---

## 5. Der Sektorraum — die Brücke zwischen Medium und Dateisystem

`SectorSpace` ist die einzige Sicht, die die Dateisysteme auf das Medium haben. Sie
verbirgt Container, Encoding und Spurgeometrie und bietet **zwei** Adressierungen, weil die
beiden Dateisysteme grundverschieden adressieren:

```cpp
class SectorSpace {
public:
    SectorSpace(DiskMedium& medium, const DiskFormat& fmt, uint8_t head_filter = kAllHeads);

    // ── physisch: (Zylinder, Kopf, Sektor-ID) ───────────────── UDOS denkt so
    bool readSector (uint8_t cyl, uint8_t head, uint8_t id, Sector& out) const;
    bool writeSector(uint8_t cyl, uint8_t head, uint8_t id, const Sector& in);

    // ── linear: fortlaufender Byte-/Satzstrom in Layout-Reihenfolge ─ CP/M denkt so
    uint64_t size() const;                       // Nutzbytes des ganzen Datenträgers
    bool read (uint64_t offset, uint8_t* dst, size_t n) const;
    bool write(uint64_t offset, const uint8_t* src, size_t n);

    struct Sector { std::vector<uint8_t> data, tail; bool id_crc_ok, data_crc_ok; };
};
```

**Layout-Reihenfolge** ist exakt die des `.img`-Codecs (`img_codec.cpp`): Zylinder außen,
Kopf innen, Sektoren nach aufsteigender ID —
`c0h0, c0h1, c1h0, c1h1, …`. Damit gilt für jede `.img`-fähige Diskette
`SectorSpace::read(off,…) ≡ Datei-Offset off`, und die Byte-Offsets aus `cpmtools`' `diskdefs`
sind unmittelbar übertragbar.

**Schreiben ist immer gepuffert-und-zurückgeschrieben**: `writeSector` liest die Spur, patcht
sie über `TrackCodec::writeSector` (§4.1) und legt sie per `DiskMedium::setTrack` zurück; die
Datei wird erst durch `DiskImage::flush()` angefasst. Ein abgebrochener Kopiervorgang
hinterlässt also **keine** halb geschriebene Datei (§14).

**`head_filter`** bedient §2 des UDOS-Dokuments: Seite 0 und Seite 1 sind getrennte
Dateisysteme, also bekommt jedes seinen eigenen `SectorSpace` über *eine* Kopfnummer.

**Fehlerhafte Sektoren** werden nicht verschwiegen: `Sector::id_crc_ok/data_crc_ok` reichen
bis in die Oberfläche durch (Datei wird mit Warnung und mit den gelesenen Bytes extrahiert,
nie stillschweigend).

---

## 6. Erweiterung von `data/formats.yaml`

### 6.1 Warum eine neue Sektion und keine neuen Felder

`formats:` beschreibt **Physik** (welche Spur trägt wie viele Sektoren welcher Größe in welchem
Verfahren) und wird vom Emulator zum Formatieren/Mounten benutzt. Ob auf dieser Geometrie ein
CP/M- oder ein UDOS-Dateisystem liegt und wo es beginnt, ist **eine andere Frage** — und sie hat
**mehrere Antworten pro Geometrie**: dieselbe 80×2×5×1024-Diskette trägt als `cpa800` ein
Dateisystem ab Spur 0 und als SCPX-Variante eines ab Spur 2. Deshalb:

> **`formats:` bleibt unangetastet. Neu ist eine zweite Top-Level-Sektion `filesystems:`,
> deren Einträge per `format:` eine Geometrie referenzieren.** n Dateisysteme je Geometrie sind
> damit ausdrückbar, und der Emulator ignoriert die Sektion (er liest nur `formats:`).

> **Nachtrag 2026-08-11:** die Sektion ist **kurz und soll es bleiben.** Seit §6.4 rechnet
> das Werkzeug den DPB einer CP/A-Diskette selbst aus; ein benannter Eintrag lohnt nur
> noch, wo diese Regel nicht gilt (UDOS, Fremdsysteme) oder wo ein Name gebraucht wird —
> `create --fs NAME` kann nur aus einem benannten Profil eine Diskette anlegen, und in der
> Oberfläche ist „cpa780“ die bessere Auskunft als „cpa_auto“. Das ursprüngliche Beispiel
> für „mehrere Antworten pro Geometrie“ (`cpa640` ab Spur 0 neben `scpx640` ab Spur 2) war
> übrigens **falsch** und wurde entfernt: für 256-B-Sektoren trägt `dtrsl1` ein *festes*
> Offset von 4 logischen Spuren — CP/A kann eine 16×256-Diskette ohne Systemspuren gar
> nicht erzeugen. Der Eintrag bewirkte nur, dass jede solche Diskette „nicht eindeutig“
> gemeldet wurde. Die Aussage selbst bleibt richtig: dieselbe 26×128-Geometrie trägt
> einmal UDOS und einmal CP/M.

Der `FsCatalog` benutzt denselben `yaml_lite`-Parser und dieselbe Pfadsuche wie der
`FormatCatalog` (`K1520_FORMATS_DEFAULT` → `./data/formats.yaml` → `~/.config/…` → `$K1520_FORMATS`),
d.h. eigene Formate legt der Anwender weiterhin an genau einer Stelle ab.

### 6.2 Der Beginn des Dateisystems ist *keine* Spurzahl

`cpmtools` drückt den Systembereich als `boottrk` (Anzahl Spuren) **oder** `offset` (Bytes) aus.
Bei den gemischten CP/A-Geometrien geht nur Letzteres — und der Grund steht schon in `formats.yaml`:

```
cpa780:  c0h0 26×128   c0h1 26×128   c1h0 26×128   c1h1 5×1024   c2…c79 5×1024
         └────────── 3 × 3328 = 9984 B ──────────┘└─ 5120 B ─┘   └ Dateisystem ab hier
```

`9984 + 5120 = 15104` — und genau das steht in `cpmtools`' `diskdefs` als
`diskdef A5120_160 … offset 15104`. Als Spurzahl ist das nicht ausdrückbar, weil die „Spuren“
verschieden groß sind. (Die Angabe `boottrk 3` bei `seclen 1024` in der `diskdefs` der
CPA_Workbench ergibt 15360 und ist eine Näherung, die nur funktioniert, solange man das Abbild
als reines 1024-B-Bild betrachtet.)

**Primitive im neuen Schema ist deshalb die Spur, an der das Dateisystem beginnt** — eindeutig,
geometrieunabhängig, und der Byte-Offset folgt daraus:

```yaml
data_start: { cyl: 2, head: 0 }     # → SectorSpace-Offset 15104, ausgerechnet, nicht gepflegt
```

Der Lader validiert: `data_start` muss auf einer Spurgrenze liegen und innerhalb der Geometrie.

### 6.3 Schema `filesystems:`

```yaml
filesystems:

  # ── gemeinsame Felder ──────────────────────────────────────────────────────
  #  name         (Pflicht)  eindeutiger Name — CLI, GUI, C-API
  #  description  (optional) Klartext für die Oberfläche
  #  format       (Pflicht)  Name eines Eintrags aus `formats:` (Geometrie)
  #  type         (Pflicht)  cpm | udos
  #  data_start   (optional) erste Spur des Dateisystems (Default cyl 0/head 0)
  #  containers   (optional) img | hfe | dmk — was zulässig ist (Default: alle; udos: nie img)
  #  detect_rank  (optional) Reihenfolge bei mehrdeutiger Autoerkennung (§12)

  # ── nur type: cpm ──────────────────────────────────────────────────────────
  #  block_size   (Pflicht)  Zuordnungseinheit in Byte (1024 | 2048 | 4096 | 8192 | 16384)
  #  dir_entries  (Pflicht)  Verzeichniseinträge (maxdir)
  #  skew         (optional) Sektorversatz je Spur (Default 0 — CP/A benutzt keinen)
  #  first_record (optional) Byte-Offset innerhalb data_start (Default 0)
  #  os           (optional) cpm2.2 | p2dos | cpm3 — Zeitstempel/Nutzernummern (Default cpm2.2)

  # ── nur type: udos ─────────────────────────────────────────────────────────
  #  sides_separate  (optional) jede Seite ein eigenes Dateisystem → n Volumes,
  #                            Ordnerform SideN/ (Default true; §9.1)
  #  boot_track      (optional) Spur des Bootabbilds (Default 21)
  #  directory_track (optional) Spur der Verzeichnisdatei (Default 22)
  #  bitmap_track    (optional) Spur der Belegungskarte (Default 23)
  #  usable_tracks   (optional) Anzahl nutzbarer Spuren (Default: aus der Geometrie)

  - name:        cpa800
    description: "CP/A 800K Datendiskette (K5601)"
    format:      cpa800
    type:        cpm
    block_size:  2048
    dir_entries: 128

  - name:        cpa780
    description: "CP/A 780K Bootdiskette — Dateisystem ab Zylinder 2"
    format:      cpa780
    type:        cpm
    data_start:  { cyl: 2, head: 0 }
    block_size:  2048
    dir_entries: 128

  - name:        scpx780
    description: "SCPX 780K — 3 Systemspuren mit 26×128"
    format:      scpx780
    type:        cpm
    data_start:  { cyl: 3, head: 0 }
    block_size:  2048
    dir_entries: 128

  - name:        udos_41
    description: "UDOS 1526 / 4.x, Laufwerkstyp 41 — 5,25″, 26×128, Seiten getrennt"
    format:      udos_26x128_77
    type:        udos
    containers:  [hfe, dmk]
```

Dazu die **fehlenden UDOS-Geometrien** in `formats:` — 77 Spuren à 26×128 MFM, beidseitig
belegt, aber logisch getrennt. Die Kandidaten stehen als Referenzabbilder bereits im Baum
(`disks/udos_boot_scp.hfe`, `udos_boot_k5600_10.hfe`, `udos_boot_k5600_20.hfe`,
`udos_boot_mf6400.hfe`) und werden bei der Umsetzung daraus **verifiziert**, nicht geraten
(vgl. die Laufwerkstyp-Matrix in `doc/udos_diskettenformat.md` §12.3).

### 6.4 Der Rückfall: CP/A rechnet den DPB selbst aus (`cpa_auto`)

**Stand 2026-08-11.** Für die Handvoll Disketten, die man ständig in der Hand hat, ist ein
benanntes Profil genau richtig. Für die 59 Geometrien des Katalogs wäre es Handarbeit mit
Rateanteil — und jedes neu vermessene Format bräuchte wieder einen Eintrag. Genau daran hing
die Mountbarkeit: von 117 im Emulator erzeugten Abbildern ließen sich **12** öffnen.

Das ist unnötig, denn **CP/A rät nicht, CP/A rechnet**. Sein BIOS leitet den DPB beim LOGIN
aus dem ab, was auf der Diskette steht (`biosdsk.mac`, Marke `drdfrm`; analysiert in
[`doc/cpa_format_detection.md`](../cpa_format_detection.md)):

1. **Sektorlängencode der Datenspur** — Zylinder 3, Kopf 0 (`dlgint`, „größte Anzahl
   SS‑Systemspuren"); ab dort muss die Diskette einheitlich sein.
2. **Zeile** in einer von vier Tabellen `dtrsl0..3` (eine je Sektorlänge), gewählt nach
   40/80 Spuren, ein-/beidseitig bzw. 8″ FM/MFM.
3. Die Zeile liefert **Verzeichnisplätze**, **Systemspuren** (in logischen Spuren, `2·N`
   plus Flag „fest") und die **Blockgröße**.
4. Ist die Systemspurzahl *nicht* fest, entscheidet Spur 0: ein Lader (Byte ≥ 0x20) heißt
   „Standardanzahl", ein Verzeichniseintrag heißt „0 Systemspuren", und eine durchgehend
   leere Spur 0 lässt an der ersten möglichen Datenspur nachsehen.
5. Eine Diskette **mit** Systemspuren wird von 192 auf 128 Plätze gekürzt (`selddr`).
6. 128‑B‑Sektoren bekommen den Sektorversatz der Tabelle `xlt` (1,7,13,… = Versatz 6).

`core/filesystem/cpm/cpa_dpb.{h,cpp}` (@ref CpaDpbRule) bildet das nach. Ein benanntes
Profil aus dem Katalog **gewinnt immer**; die Ableitung greift nur, wenn keines passt, und
heißt dann `cpa_auto`. Mit `--fs cpa_auto` lässt sie sich erzwingen.

Gegenprobe statt Selbstbestätigung: die Regel reproduziert die von Hand nachgemessenen
Profile `cpa780` und `scpx798` **exakt** (`CpaDpb.ReproduziertNachgemessenesProfil*`), und
sie korrigierte dabei einen geratenen Katalogwert — `cpa800` hat **192** Verzeichnisplätze,
nicht 128 (FORMAT.COM nennt sie im Menü selbst, doc/format.md §3.1 Wahl 0). Bewiesen hat
das erst das laufende CP/A: `DiskToolNeueDisketten.CpaFindetDateiJenseitsVonPlatz128`.

Ergebnis: **104 der 117** Abbilder sind mountbar. Die restlichen dreizehn sind es aus
Gründen, die kein Profil heilt — drei MS-DOS-Disketten (§12.3) und zehn fehlgeschlagene
Formatierläufe, die keine einzige gültige Spur hinterlassen haben.

> **Was die Regel NICHT ist:** ein Profil für Fremdsysteme. Eine KAYPRO- oder
> VORTEX-formatierte Diskette (FORMAT.COM kann beide anlegen) bekommt hier den DPB, den
> *CP/A* darauf sähe — für eine leere Diskette ist das richtig, für eine beschriebene
> nicht unbedingt. Davor schützt die Positivprobe aus §12.2: ein fremdes Verzeichnis
> fällt durch, und die Diskette gilt als nicht erkannt.

### 6.5 Rückwärtskompatibilität

Ein `formats.yaml` ohne `filesystems:` ist weiterhin gültig — der Emulator merkt nichts, das
DiskTool meldet dann schlicht „kein Dateisystemprofil bekannt“. Umgekehrt ignoriert der
`FormatCatalog` die neue Sektion. Guard: `test_format_catalog` muss mit der erweiterten Datei
unverändert grün bleiben (insbesondere `BootKritischeGeometrien_Unveraendert`).

---

## 7. CP/M-Dateisystem (`core/filesystem/cpm/`)

Zielbild ist die Teilmenge von `cpmtools`, die wir wirklich brauchen — bewusst **ohne**
Passwörter, Datenträgeretiketten, CP/M-3-Zeitstempel und `libdsk`.

**Modell.** Ab `data_start` zerfällt der Sektorraum in Blöcke à `block_size`. Die ersten
`dir_entries × 32 / block_size` Blöcke sind das Verzeichnis. Ein Eintrag (32 Byte):

```
US NAME(8) TYP(3) EX S1 S2 RC  AL(16)
│  │       │      │  │  │  │   └─ Blocknummern: 16×8 Bit, oder 8×16 Bit wenn Blöcke ≥ 256
│  │       │      │  │  │  └──── Sätze im letzten Extent (à 128 B)
│  │       │      └──┴─────────── Extent-Nummer (EX + S2×32)
│  │       └── Hochbit von TYP[0..2] = R/O, SYS, ARCHIV
│  └── Name, mit Leerzeichen aufgefüllt
└── Nutzerbereich 0…15, 0xE5 = frei
```

**Auflisten**: Verzeichnis lesen, Einträge je (User, Name) zu Dateien gruppieren, Größe aus
höchstem Extent + `RC` (bei `os: cpm3` zusätzlich das „exakte Länge“-Byte), Attribute aus den
Hochbits.

**Lesen**: Extents nach `EX` sortieren, Blockliste verketten, je Block `block_size` Bytes aus
dem Sektorraum ziehen, am Ende auf `(extents−1)×extent_size + RC×128` kürzen. Im Textmodus bis
zum ersten `0x1A` abschneiden und CR LF → LF wandeln.

**Schreiben**: freie Blöcke aus der Allokationskarte (aus allen Verzeichniseinträgen
rekonstruiert, nicht aus einem gespeicherten Feld!) belegen, Daten auf `128` aufrunden und mit
`0x1A`/`0x00` auffüllen, Extents in freie Verzeichnisplätze schreiben. Vorhandener gleicher
Name → nach Rückfrage überschreiben (CLI: `--force`).

**Löschen**: alle Extents auf `US = 0xE5` setzen. Blöcke werden dadurch frei (Karte wird
ohnehin bei jedem Öffnen neu aufgebaut) — kein Nachführen einer FAT nötig.

**mkfs**: Verzeichnisblöcke mit `0xE5` füllen, Rest des Datenbereichs unangetastet lassen.

**Skew**: Übersetzungstabelle wie in `cpmtools` (`skewtab`), angewandt auf die Sektorreihenfolge
innerhalb einer Spur. Für alle heute bekannten CP/A-/SCPX-Formate ist `skew 0`; die Tabelle
existiert trotzdem, damit Fremdformate nachrüstbar sind.

**Bewusst nicht unterstützt** (mit klarer Meldung statt falscher Ergebnisse): Passwörter,
Datenträgeretikett, Datestamper, mehrere Verzeichnisspuren an nicht zusammenhängender Lage.

---

## 8. UDOS-/ZDOS-Dateisystem (`core/filesystem/udos/`)

Vollständig spezifiziert in **`doc/udos_diskettenformat.md`** — das Dokument ist an einer echten,
fehlerfrei eingelesenen Diskette gemessen und enthält in §8 bereits die Algorithmen. Der Entwurf
übernimmt sie unverändert; hier nur die Bezüge zur Architektur.

| Struktur | Ort | Umsetzung |
|---|---|---|
| Sektorkontrollblock (Rück-/Vorwärtszeiger, je 2 B) | **hinter der Daten-CRC** | `Sector::tail[0..3]`; genau deshalb `.hfe`/`.dmk` und nie `.img` (A5) |
| Belegungskarte | Spur 23, Sektoren 1–3 (384 B) | `UdosBitmap` — **Bits sind die Wahrheit**, Zähler werden nachgeführt, aber nie geglaubt (§4.2 des Dokuments) |
| Verzeichnis | gewöhnliche Datei `DIRECTORY`, Kopfsektor Spur 22 Sektor 1 | `UdosDirectory`: Einträge variabler Länge, Ende `0xFF`, Rest des Sektors ist Altbestand |
| Dateikopfsektor | 1 Sektor je Datei | `UdosFileHeader`: Typ, Satzanzahl, Satzlänge, Bytes im letzten Satz, PROPS, Datum, Segmente |
| Satz (Record) | `satzlen/128` **aufeinanderfolgende** Sektoren **einer** Spur | Zuteilungseinheit — Sätze überschreiten nie eine Spurgrenze |

**Auflisten**: Verzeichnisdatei über die Kontrollblöcke durchlaufen; je Eintrag Namenslänge
(Bits 0–5), SECRET (Bit 7) und Kopfzeiger; Metadaten aus dem Kopfsektor.

**Lesen**: Kette ab Vorwärtszeiger des Kopfsektors, je Satz `satzlen/128` Sektoren, Ende bei
`FF FF`, letzten Satz auf „Bytes im letzten Satz“ kürzen.

**Schreiben**: Satzlänge 128 als sichere Vorgabe; Platz spurweise aus der Karte suchen
(zusammenhängender Block je Satz); Kette rückwärts/vorwärts konsistent setzen; Kopfsektor nach
§6 füllen; Eintrag vor dem `0xFF` des letzten Verzeichnissatzes einfügen — passt er nicht mehr,
wächst die Datei `DIRECTORY` um einen Satz; Karte und **beide** Zähler nachführen.

**Löschen**: Eintrag ausschneiden, Bits zurücksetzen. **Die Kontrollblöcke auf dem Medium
bleiben stehen** — daraus darf nie auf „belegt“ geschlossen werden (§8.5 des Dokuments: auf dem
Referenzträger sehen 364 freie Sektoren belegt aus).

**mkfs**: Karte anlegen (Datenträgername, Spureinträge, konstanter Nachlauf `11×0x33 / 0xF7 /
27×0x77`, Zähler), Datei `DIRECTORY` mit einem leeren Satz und Kopfsektor anlegen, Systemspuren
0/1/2 und 21 **frei lassen und sperren**. Eine so erzeugte Diskette ist **nicht bootfähig** —
das bleibt dem Emulator vorbehalten (`UdosFormat.BuildsBootableSystemDiskAndBootsFromIt`), und
das Werkzeug sagt es beim Anlegen dazu.

**Unantastbar** (§8.6 des Dokuments): Spuren 0, 1, 2 und 21–23. Der Belegungsprüfer des
Werkzeugs behandelt sie als reserviert, egal was die Karte sagt.

---

## 9. Die gemeinsame Fassade

Zwei Ebenen, weil eine UDOS-Diskette **zwei Dateisysteme** trägt, aber **eine Diskette** ist:

```
Volume     = ein Dateisystem (CP/M: die ganze Diskette · UDOS: eine Seite)
DiskVolume = die Diskette als Ganzes: 1..n Volumes + Dateibindung   ← was Anwender/API sehen
```

```cpp
struct FileEntry {
    int         volume;        // 0..n-1 — bei UDOS die Seite, sonst immer 0
    std::string name;          // CP/M "3:NAME.TYP" · UDOS "HELP.DAT.00"
    uint64_t    size;          // Nutzbytes
    std::string type;          // CP/M "" · UDOS "A"/"P"/"P1"/"B"/"D"
    std::string attributes;    // CP/M "RO SYS ARC" · UDOS "WELS"
    std::string date;          // "" wenn das Dateisystem keins führt
    bool        hidden;        // CP/M SYS · UDOS SECRET
    bool        damaged;       // CRC-Fehler oder Kettenbruch beim Lesen
};

class FileSystem {                       // ein Volume; Basisklasse, keine Ausnahmen
public:
    virtual std::vector<FileEntry> list() const = 0;
    virtual bool read (const std::string& name, std::vector<uint8_t>& out) = 0;
    virtual bool write(const std::string& name, const std::vector<uint8_t>& in,
                       const WriteOptions&) = 0;
    virtual bool erase(const std::string& name) = 0;
    /// @brief Was WÜRDE das Einfügen kosten — inkl. Blockrundung, Verzeichniseinträgen
    ///        und (UDOS) Kopfsektor + Wachstum der Datei DIRECTORY.  Schreibt nichts.
    virtual bool wouldFit(const std::vector<PlannedFile>&, FitReport& out) const = 0;
    virtual FsInfo info() const = 0;      // Datenträgername, frei/belegt, Warnungen
};

class DiskVolume {                       // die Diskette — das ist die Arbeitsschnittstelle
public:
    static std::unique_ptr<DiskVolume> open(const std::string& path,
                                            const std::string& fs_name /* "" = erkennen */);
    static std::unique_ptr<DiskVolume> create(const std::string& path,
                                              const std::string& fs_name);

    int  volumeCount() const;                     // CP/M 1 · UDOS beidseitig 2
    const std::string& volumeDir(int v) const;    // "" bei 1 Volume, sonst "Side0"/"Side1"

    std::vector<FileEntry> list() const;          // ALLE Volumes, immer frisch (§9.3)

    bool extractAll(const std::string& dest_dir, const TransferOptions&);
    bool insertAll (const std::string& src_dir,  const TransferOptions&);
    bool extract(const FileRef&, const std::string& dest_path, const TransferOptions&);
    bool insert (const std::string& src_path, const FileRef&, const TransferOptions&);
    bool erase  (const FileRef&);

    bool dirty() const;   bool flush();   bool saveAs(const std::string& path);
    const std::string& lastError() const;
};
```

`FileRef` ist `{volume, name}` — in Textform `Side1/HELP.DAT.00` bzw. bei einem Volume
schlicht `HELP.DAT.00`. Damit ist jede Datei über die ganze Diskette eindeutig bezeichnet,
auch wenn beide Seiten denselben Namen tragen (auf `disks/udos_boot_scp.hfe` ist das der
Normalfall: beide Seiten heißen `UDOS.SYS.4.3` und teilen sich viele Dateinamen).

**Fehlerstil wie im Kern**: Rückgabe `bool` + `lastError()` in Klartext-Deutsch, keine
Ausnahmen über die Modulgrenze — passt zu C-ABI und zu `DiskImage`.

**Namenskonvertierung** (Linux ↔ Diskette) liegt in der Fassade, nicht in der Oberfläche:
CP/M `8.3`, Großschrift, verbotene Zeichen; UDOS bis 32 Zeichen, Punkt ist normales
Namenszeichen, Groß-/Kleinschreibung signifikant. Kollisionen werden gemeldet, nicht geraten.

### 9.1 Zusammengesetzter Datenträger: `Side0/` und `Side1/`

Die beiden Seiten einer UDOS-Diskette sind getrennte Dateisysteme mit **getrennter
Belegungskarte, getrenntem Verzeichnis und getrenntem Datenträgernamen** — Platz auf Seite 1
hilft einer vollen Seite 0 nicht, und ein Zeiger kann die Seite nicht wechseln
(`doc/udos_diskettenformat.md` §1.2/§2). Genau deshalb wird die Trennung **im Dateisystem des
Anwenders** abgebildet statt weggemogelt:

```
Extrahieren                              Einfügen
~/udos_extrakt/                          ~/udos_neu/
├── Side0/   ← Volume 0 (UDOS-Laufwerk 0) ├── Side0/   → Volume 0
│   ├── ZDOS                              │   └── PROG.COM
│   └── HELP.DAT.00                       └── Side1/   → Volume 1
└── Side1/   ← Volume 1 (UDOS-Laufwerk 4)     └── TEXT.DAT
    └── NOTE.TO.UDOS.4.3
```

**Regeln** (`DiskVolume::extractAll` / `insertAll`):

| Lage | Verhalten |
|---|---|
| 1 Volume (CP/M, einseitiges UDOS) | flacher Ordner, **keine** `SideN`-Unterverzeichnisse — weder erzeugt noch verlangt |
| n Volumes, Extrahieren | `SideN/` wird angelegt, auch wenn eine Seite leer ist (die leere Seite als leerer Ordner ist die ehrliche Auskunft) |
| n Volumes, Einfügen, Ordner hat **alle** `SideN/` | normal — je Unterverzeichnis auf das zugehörige Volume |
| n Volumes, Einfügen, `SideN/` fehlt | **Fehler**, keine Änderung: *„Der Ordner ~/x muss die Unterverzeichnisse Side0/ und Side1/ enthalten (die Diskette hat 2 Seiten). Gefunden: Side0/."* |
| n Volumes, Einfügen, Ordner enthält zusätzlich lose Dateien | **Fehler** mit Nennung der Dateien — nicht stillschweigend auf Seite 0 legen |
| leeres `SideN/` | zulässig — diese Seite bleibt unverändert |

Ein Unterverzeichnis **unterhalb** von `SideN/` ist ein Fehler: weder CP/M noch UDOS kennen
Unterverzeichnisse. Die Namensvergabe der Ordner (`Side0`, `Side1`) ist fest und
groß-/kleinschreibungstolerant beim Lesen (`side0` wird akzeptiert), aber beim Anlegen immer
`Side0`.

### 9.2 Stapeloperationen sind Transaktionen — Platzprüfung vorab

„Passt nicht“ darf **nie** eine halb beschriebene Diskette hinterlassen. Jede Stapeloperation
läuft deshalb in drei Schritten:

1. **Planen.** Alle Quelldateien einlesen, je Volume den Bedarf ausrechnen — inklusive des
   Verwaltungsaufwands, denn der ist erheblich:
   - **CP/M**: Aufrundung auf `block_size` je Datei, plus ein Verzeichniseintrag je
     angefangenem Extent (`extent_size = block_size × Blockzeiger`), plus vorhandene
     gleichnamige Dateien, die ersetzt und damit frei werden.
   - **UDOS**: 1 Kopfsektor je Datei, Aufrundung auf ganze **Sätze**, dazu der Zwang, dass ein
     Satz in *eine* Spur passen muss (Zerstückelung kann Platz unbrauchbar machen), plus
     mögliches Wachstum der Verzeichnisdatei `DIRECTORY` um Sätze.
2. **Urteilen.** Reicht der Platz auf **jedem** betroffenen Volume? Sind alle Namen zulässig
   und kollisionsfrei? Ist die Diskette schreibbar? Nein → **Abbruch vor der ersten Änderung**,
   mit einer Meldung, die Zahlen nennt:
   *„Seite 1: 12 Dateien (184 KB) benötigen 1472 Sektoren, frei sind 1310. Es wurde nichts
   geschrieben.“*
3. **Ausführen.** Erst jetzt wird geschrieben — im Speicher (§5). Tritt trotzdem ein Fehler auf
   (CRC-defekte Zielspur, unerwarteter Kettenbruch), wird die **vorher genommene Momentaufnahme
   des `DiskMedium` zurückgerollt** (≈1 MB je Diskette, also billig) und nichts geht in die
   Datei. Erst `flush()` schreibt.

Der Prüfschritt ist als `FileSystem::wouldFit()` auch einzeln aufrufbar — die GUI kann damit
schon beim Ablegen per Ziehen anzeigen, dass es nicht passen wird.

> **Rest­risiko, ehrlich benannt:** Die UDOS-Vorausrechnung kann in Ausnahmefällen zu
> optimistisch sein, wenn die freien Sektoren so über die Spuren verstreut liegen, dass kein
> zusammenhängender Block für einen Satz mehr frei ist. Deshalb rechnet der Planer nicht mit
> „Summe freier Sektoren“, sondern **spurweise** gegen die Belegungskarte — dieselbe Suche, die
> das Schreiben später benutzt. Damit ist die Aussage exakt, nicht geschätzt.

### 9.3 Die Ansicht ist immer frisch

`DiskVolume::list()` liest jedes Mal Verzeichnis (und bei UDOS Belegungskarte) aus dem
`DiskMedium` neu; es gibt **keinen zwischengespeicherten Verzeichnisstand**. Damit gilt ohne
Zutun der Oberfläche:

- nach dem Öffnen ist der Inhalt sofort da (die GUI ruft `list()` direkt nach `open()`),
- nach Einfügen, Löschen und `mkfs` zeigt die Ansicht den **tatsächlichen** Zustand des Mediums —
  auch dann, wenn eine Operation teilweise fehlschlug,
- Belegungsanzeige (frei/belegt je Seite) stammt aus derselben Quelle wie die Liste und kann
  gar nicht auseinanderlaufen.

Das ist bewusst „teuer“ (Verzeichnis erneut parsen: einige zehn Sektoren aus dem RAM) und dafür
nicht falsifizierbar. Ein Cache käme erst in Frage, wenn es messbar stört.

---

## 10. C-API (`libk1520disk.so`)

Stil wie `k1520_api.h`: opakes Handle, Index-plus-Getter statt Strukturen über die Grenze,
`bool` + Fehlertext.

```c
typedef void* K1520Disk;

/* Öffnen / Anlegen / Speichern */
K1520Disk   k1520d_open(const char* path, const char* fs_name /* NULL = erkennen */);
K1520Disk   k1520d_create(const char* path, const char* fs_name);   /* Leerdiskette + mkfs */
bool        k1520d_flush(K1520Disk);
bool        k1520d_save_as(K1520Disk, const char* path);
void        k1520d_close(K1520Disk);
const char* k1520d_last_error(K1520Disk);
const char* k1520d_last_open_error(void);

/* Katalog + Erkennung */
int         k1520d_fs_count(void);
const char* k1520d_fs_name(int i);
const char* k1520d_fs_description(const char* name);
const char* k1520d_detect(const char* path);        /* erkannter Name, "" = unbekannt */

/* Seiten/Volumes — es wird NICHT umgeschaltet, sie sind alle gleichzeitig sichtbar */
int         k1520d_volume_count(K1520Disk);            /* CP/M 1 · UDOS beidseitig 2      */
const char* k1520d_volume_dir(K1520Disk, int v);       /* "" | "Side0" | "Side1"          */
const char* k1520d_volume_label(K1520Disk, int v);     /* Datenträgername                 */
uint64_t    k1520d_volume_free (K1520Disk, int v);
uint64_t    k1520d_volume_used (K1520Disk, int v);

/* Verzeichnis — IMMER frisch aus dem Medium (§9.3); enthält alle Volumes */
int         k1520d_list(K1520Disk);                 /* Anzahl; füllt den internen Puffer */
int         k1520d_entry_volume(K1520Disk, int i);
const char* k1520d_entry_name(K1520Disk, int i);     /* ohne SideN-Präfix                */
uint64_t    k1520d_entry_size(K1520Disk, int i);
const char* k1520d_entry_type(K1520Disk, int i);
const char* k1520d_entry_attrs(K1520Disk, int i);
const char* k1520d_entry_date(K1520Disk, int i);
bool        k1520d_entry_damaged(K1520Disk, int i);

/* Übertragung  (mode: 0 = binär, 1 = Text) — `name` darf "Side1/NAME" sein */
bool        k1520d_extract(K1520Disk, const char* name, const char* dest, int mode);
bool        k1520d_insert (K1520Disk, const char* src,  const char* name, int mode, bool force);
bool        k1520d_erase  (K1520Disk, const char* name);

/* Stapel: legt bei mehreren Volumes SideN/ an bzw. verlangt sie (§9.1) */
bool        k1520d_extract_all(K1520Disk, const char* dest_dir, int mode);
bool        k1520d_insert_all (K1520Disk, const char* src_dir,  int mode, bool force);

/* Vorabprüfung ohne jede Änderung (§9.2) — "" = passt, sonst der Grund im Klartext */
const char* k1520d_check_fit(K1520Disk, const char* src_dir);

/* Zustand */
bool        k1520d_dirty(K1520Disk);   /* ungespeicherte Änderungen im Speicher */
const char* k1520d_check(K1520Disk);   /* mehrzeiliger Prüfbericht, "" = ohne Befund */
const char* k1520d_version(void);
```

Der **Dreiklang-Test** aus `tests/python/test_c_api.py` (Header ↔ `.so`-Symbole ↔ ctypes-
Deklarationen) wird auf diese Schnittstelle mitgezogen — sonst bricht eine Signaturänderung
auch hier stillschweigend.

---

## 11. Anwenderseite

### 11.1 CLI — `k1520disktool`

```sh
k1520disktool ls     <image> [--fs NAME] [-l]
k1520disktool get    <image> <muster…> --to <verzeichnis> [--text|--binary]
k1520disktool put    <image> <datei…>  [--as NAME] [--text|--binary] [--force]
k1520disktool rm     <image> <muster…>
k1520disktool create <image> --fs NAME
k1520disktool info   <image> [--fs NAME]
k1520disktool check  <image>
```

`ls` zeigt **beide Seiten in einer Liste** mit Seitenspalte; `get … --to DIR` legt bei
mehreren Seiten `DIR/Side0` und `DIR/Side1` an; `put DIR` (Verzeichnis statt Dateien) verlangt
sie umgekehrt (§9.1). Muster und Namen dürfen das Präfix tragen — `get 'Side1/*.DAT'`,
`put text.dat --as Side1/TEXT.DAT`; ohne Präfix wählt `--volume N` (Vorgabe 0).

```
$ k1520disktool ls disks/udos_boot_scp.hfe
Dateisystem: udos_41 (erkannt) · 2 Seiten · UDOS.SYS.4.3 / UDOS.SYS.4.3
Seite Name                 Typ  Größe  Eigensch.  Geändert
0     ZDOS                 P1    3072  WELS       900517
0     HELP.DAT.00          A     8192             900808
1     NOTE.TO.UDOS.4.3     A      512             900808
39 Dateien · Seite 0: 850 Sektoren frei · Seite 1: 1310 frei
```

Gemeinsam: `--fs` übersteuert die Erkennung, `--json` gibt maschinenlesbar aus (für Tests und
Skripte), `--dry-run` führt **nur** die Planungs-/Prüfphase aus (§9.2) und meldet, ob es passt.
Exit-Codes: `0` ok, `1` Fehler, `2` Format/Dateisystem nicht erkannt, `3` passt nicht (kein
Platz), `4` Ordnerstruktur falsch (fehlendes `SideN/`).

Aufruf im Projekt konsequent über `tools/dev.sh tool k1520disktool …` (nie direkt aus `build/` —
zwei Build-Verzeichnisse, gleiche Namen; CLAUDE.md „Build & test“).

### 11.2 GUI — `app/disktool/`

PySide6, **Zwei-Fenster-Ansicht** (links Diskette, rechts Linux-Verzeichnis), weil das der
Arbeitsablauf ist, den die beiden alten Werkzeuge künstlich auf zwei Programme aufteilten:

```
┌─ Diskettenabbild ──────────────────────┐   ┌─ Ordner ─────────────────────┐
│ [Datei…]  udos_boot_scp.hfe            │   │ [Ordner…]  ~/udos_extrakt    │
│ Format:      udos_26x128_77  (erkannt) │   │                              │
│ Dateisystem: [udos_41 ▾]     (erkannt) │   │ ▾ Side0/                     │
├────────────────────────────────────────┤   │     ZDOS                     │
│ ▾ Side 0   UDOS.SYS.4.3   850 frei     │ → │     HELP.DAT.00              │
│     ZDOS             P1  3 KB  WELS    │ ← │ ▾ Side1/                     │
│     HELP.DAT.00      A   8 KB          │   │     NOTE.TO.UDOS.4.3         │
│ ▾ Side 1   UDOS.SYS.4.3  1310 frei     │   │                              │
│     NOTE.TO.UDOS.4.3 A    512 B        │   └──────────────────────────────┘
├────────────────────────────────────────┤
│ ● geändert · 69 Dateien                │
└────────────────────────────────────────┘
[Alles extrahieren] [Alles einfügen] [Löschen] [Neue Diskette…] [Speichern]
```

- **Beide Seiten in einer Liste**, nach Seite gruppiert (bei CP/M entfällt die Gruppierung
  ersatzlos) — kein Umschalten, keine halbe Sicht auf die Diskette.
- Die Liste wird **direkt nach dem Laden** gefüllt und nach jeder schreibenden Aktion aus dem
  Medium **neu gelesen** (§9.3); die Freiplatzanzeige je Seite kommt aus derselben Abfrage.
- Erkennung von Format und Dateisystem wird angezeigt („erkannt“ / „gewählt“); scheitert die
  Erkennung, bleibt die Liste leer und die Meldung aus §12 steht sichtbar im Fenster —
  die Schaltflächen zum Schreiben sind dann gesperrt.
- Ziehen und Ablegen in beide Richtungen, auch aus dem Dateimanager. Beim Ablegen auf eine
  Seitengruppe ist das Ziel-Volume damit bestimmt; ein Ordner mit `Side0/`/`Side1/` wird als
  Ganzes übernommen.
- **Vor** jedem Einfügen läuft die Prüfung aus §9.2; passt es nicht, erscheint der Fehler mit
  Zahlen und es wird nichts geändert.
- Schreibende Aktionen sind bis zum **[Speichern]** rein im Speicher (§5) — mit sichtbarem
  „geändert“-Zeichen; Schließen mit ungespeicherten Änderungen fragt nach.
- **Rechtsklick auf eine Datei** (oder Doppelklick): *Eigenschaften…*, *In den Ordner
  holen*, *Löschen*. Der Eigenschaften-Dialog zeigt und ändert, was weder in den Bytes
  der Datei noch in einem Linux-Dateisystem steht — bei UDOS den ganzen Kopfsektor, bei
  CP/M Nutzerbereich und Attributbits (§13c). Auf einer Gruppenzeile oder im Leeren
  erscheint kein Menü statt eines mit toten Einträgen.
- Textmodus als Umschalter mit Vorschlag aus Endung/UDOS-Typ.
- Fortschritt + Protokollbereich für lange Läufe (ganze Diskette extrahieren).

Start: `run_disktool.sh` (setzt `LD_LIBRARY_PATH=build`, ruft `app/disktool/main.py`), analog
zu `run_gui.sh`.

---

## 12. Autoerkennung — und wann sie abbricht

Zwei Stufen, weil zwei verschiedene Dinge erkannt werden: die **Geometrie** (welcher
`formats:`-Eintrag beschreibt dieses Abbild?) und das **Dateisystem** (welcher
`filesystems:`-Eintrag liegt darauf?).

### 12.1 Stufe 1 — Geometrie aus dem Abbild messen

`.hfe` und `.dmk` sind selbstbeschreibend: nach `DiskImage::open` liegt das ganze Medium vor,
und `TrackCodec::parseTrack` liefert je Spur die **tatsächlichen** Sektor-IDs, Sektorgrößen und
das Verfahren. Daraus entsteht ein gemessener Spurbereichsplan — exakt die Struktur, die ein
`formats:`-Eintrag beschreibt:

```
gemessen:  c0h0-c0h1 26×128 MFM │ c1h0 26×128 MFM │ c1h1 5×1024 MFM │ c2-c79 5×1024 MFM
formats:   cpa780                                                     ✔ deckungsgleich
```

Verglichen wird gegen **jeden** Eintrag in `formats:`; ein Treffer verlangt Übereinstimmung in
Zylinder-/Kopfzahl, Sektoranzahl, Sektorgröße, erster Sektor-ID und Verfahren je Spurbereich.

Toleranzen, damit echte Abbilder nicht durchfallen:
- **leere Spuren am Ende** (viele echte `.hfe` tragen ein bis drei unformatierte Zusatzspuren)
  werden ignoriert, wie schon im `.img`-Codec;
- **einzelne CRC-defekte Spuren** disqualifizieren nicht, sie werden gemeldet;
- eine Spur, die zu **gar keinem** Bereich passt, disqualifiziert den Eintrag.

Bei `.img` gibt es nichts zu messen (rohe Sektorbytes ohne Struktur): dort bleiben alle Formate
Kandidaten, deren `totalBytes()` zur Dateigröße passt; die Entscheidung fällt in Stufe 2.

> **Wichtig für die Erwartungshaltung:** *das* Format eines Abbilds ist nicht immer eindeutig
> bestimmbar — `formats.yaml` enthält bewusst geometrisch **identische** Einträge (`cpa640` und
> `k5601_16x256`, `scpx780` und `scpx780_b`). Das ist kein Mangel: was der Anwender wirklich
> braucht, ist das **Dateisystem**, und das entscheidet Stufe 2. Bleiben danach mehrere gleich
> gute Kandidaten, wird der bestplatzierte genommen (`detect_rank`), das Ergebnis als
> **„nicht eindeutig“** markiert und die Alternativen in der Auswahlliste angeboten.

### 12.2 Stufe 2 — Dateisystem positiv nachweisen

Kandidaten sind alle `filesystems:`-Einträge, deren `format:` in Stufe 1 getroffen wurde. Je
Kandidat eine **Positivprobe** (billig, wenige Sektoren):

- **UDOS**: Belegungskarte lesen (Spur `bitmap_track`, Sektoren 1–3) — 24 Byte druckbarer
  Datenträgername mit `0x0D`-Füllung, Byte +378 = Sektoren/Spur, +379 = Spurzahl passend zur
  gemessenen Geometrie, Nachlauf `11×0x33 / 0xF7 / 27×0x77`. Zusätzlich: Kopfsektor der Datei
  `DIRECTORY` auf `directory_track` Sektor 1. Das ist sehr trennscharf — Zufallstreffer
  praktisch ausgeschlossen.
- **CP/M**: erste Verzeichnisblöcke ab `data_start` lesen — Anteil plausibler Einträge
  (`US ≤ 15` oder `0xE5`, Name druckbar-großgeschrieben, Blocknummern innerhalb der Kapazität,
  `RC ≤ 0x80`) über einem Schwellwert.

Bei UDOS wird die Probe **je Seite** gefahren; besteht nur Seite 0, ist es eine einseitig
beschriebene UDOS-Diskette (1 Volume, flacher Ordner — §9.1).

### 12.3 Kein Treffer → Abbruch mit Diagnose

Findet Stufe 1 keinen passenden `formats:`-Eintrag, wird **nicht geraten**. Das Werkzeug bricht
ab und gibt aus, was es gemessen hat — damit ist die Meldung zugleich die Vorlage für den
fehlenden Katalogeintrag:

```
Fehler: Das Abbild passt zu keinem Format in data/formats.yaml.
Gemessen:
  Zylinder 0-79, Köpfe 0-1, MFM
  c0h0..c0h1   : 26 Sektoren à 128 B, IDs 1-26
  c1h0..c79h1  :  9 Sektoren à 512 B, IDs 1-9
Nächstliegender Eintrag: k5601_9x512 (weicht ab: Spuren c0h0..c0h1 sind dort 9×512)
Geprüfter Katalog: /home/…/data/formats.yaml
```

Besteht Stufe 2 keine Probe, lautet die Meldung entsprechend „Geometrie erkannt (`cpa800`), aber
kein bekanntes Dateisystem gefunden“ — mit dem Angebot, ein Profil manuell zu wählen (Lesen ist
dann auf eigene Gefahr möglich, **Schreiben bleibt gesperrt**, bis die Wahl bestätigt ist).
Seit §6.4 kommt vor dieser Meldung noch die abgeleitete CP/A-Regel zum Zuge; die Meldung
nennt jetzt auch, **woran** die Probe scheiterte („Verzeichnisplatz 0 trägt Nutzerbereich
0x53 — das Verzeichnis ist nicht angelegt“).

Zwei Sonderfälle, die sonst als „unbekannt“ durchgingen und dem Bediener nichts sagten:

* **Formatiert, aber nie eingerichtet.** Trägt der ganze Verzeichnisbereich EIN Füllbyte
  (0xF6 oder das Prüfmuster 0x53 von FORMAT.COM), gilt die Diskette als leer statt als
  unlesbar — allerdings nur beim *abgeleiteten* Profil, denn nur dort steht durch die Regel
  fest, wo das Verzeichnis liegt. `list()` überspringt solche Plätze ohnehin
  (Nutzerbereich > 15), die Diskette erscheint also leer, was sie auch ist. Die Anzeige
  sagt es: „Verzeichnis nicht angelegt (Füllbyte 0xF6)“.
* **MS-DOS.** FORMAT.COM legt auf Wunsch DOS-Disketten an (die Menüpunkte mit `{MSDOS}`,
  doc/format.md §3.3/§3.4). Ein BPB in Spur 0 Sektor 1 wird erkannt und benannt: „die
  Diskette trägt ein MS-DOS-Dateisystem (FAT), Kennung 'CP/A1188' — dieses Werkzeug liest
  CP/M und UDOS“. Gelesen wird sie nicht; dafür gibt es das Wirtssystem.

### 12.4 …und wenn auch die Geometrie unbekannt ist: vermessen und **nur lesen**

**Stand 2026-08-11.** Bis dahin war ein fehlender `formats:`-Eintrag das Ende: eine fremde
Diskette ließ sich nicht einmal *ansehen*, ohne dass jemand vorher den Katalog erweiterte.
Das war eine unnötig hohe Hürde für den häufigsten Fall — „was ist das überhaupt für eine
Diskette?".

Findet Stufe 1 nichts, baut `GeometryProbe::synthesize()` deshalb aus der Messung ein
namenloses `DiskFormat` (`detection().format == "(gemessen)"`). Darauf läuft dann die
gewohnte Stufe 2 samt CP/A-Regel (§6.4). Die Spurbereiche entstehen als echte
**Rechtecke** — erst Zylinder mit gleichem Kopf-Muster zusammenfassen, dann darin die
Köpfe; sonst bekäme eine gemischte Geometrie wie `cpa780` (c0h0/c0h1/c1h0 = 128 B, c1h1
schon 1024 B) einen Bereich, den es gar nicht gibt. Ein erkanntes Lückenmuster wird als
`step: 2` ausgedrückt.

> **Ein so gelesener Datenträger ist unaufhebbar schreibgeschützt.** `setReadOnly(false)`
> verweigert und sagt warum. Die Geometrie ist gemessen, nicht belegt — beim geringsten
> Irrtum landete ein Schreibvorgang an der falschen Stelle, und fremde Abbilder sind in
> aller Regel Einzelstücke. Wer schreiben will, legt den Katalogeintrag an; die Ausgabe
> von `measure` taugt als Vorlage.

Abgewiesen wird weiterhin, was sich nicht als zusammenhängendes Dateisystem lesen ließe:
kein einziger formatierter Sektor, uneinheitliche Sektorgrößen **innerhalb** einer Spur,
oder ein Loch mitten im beschriebenen Bereich, das kein Doppelschritt ist. Und wenn die
Geometrie zwar vermessen wurde, aber nichts Lesbares trägt, nennt die Meldung wieder die
**Messung** — nicht den nichtssagenden Namen `(gemessen)`.

Nebenbei fiel dabei eine alte Schwäche auf: „zu wenige Sektoren" galt als *Schaden* ohne
Obergrenze, sodass eine 7×512-Diskette als „`k5601_ss40_9x512` mit 40 defekten Spuren"
durchging. Jetzt gilt: mehr als ein Viertel abweichender Spuren ist kein Schaden, sondern
ein anderes Format (`GeometryProbe`, Regel 4b).

---

## 13. Quellen-Abstraktion für spätere Hardware (E4)

`DiskImage` bleibt die Quelle. Für physische Laufwerke kommt später eine dünne Schicht davor:

```
ImageSource ─┬─ FileSource     (heute: DiskImage::open / flush)
             └─ DeviceSource   (später: gw read → temporäres .hfe → gw write)
```

Konkret heißt „vorgesehen“: die C-API kennt nur `path`, das CLI nur `<image>`, und beides
akzeptiert später ein Gerätekürzel (`gw:0`) statt eines Dateinamens — ohne Änderung an
Dateisystem, Sektorraum oder Oberfläche. Mehr wird jetzt nicht gebaut.

---

## 13a. Bootabbild — die Systemspuren (2026-08-12)

Das Werkzeug konnte leere Disketten anlegen; **bootfähig** wird eine Diskette aber erst
durch die Spuren *vor* dem Dateisystem. Das Lade-ROM liest Spur 0 blind ein, lange bevor
es irgendein Dateisystem gibt (`doc/K1520_architecture.md` §14.5) — diese Spuren gehören
keiner Datei und werden vom Dateisystem nie angefasst.

**Entscheidung: ein Byteband, keine Struktur.** Das Bootabbild ist eine rohe `.bin`,
linear ab dem ersten Sektor des Systembereichs geschrieben. Der Systembereich ist je
Dateisystemfamilie fest umrissen:

| Familie | Systembereich | `cpa780` / `udos_ds77` |
|---------|---------------|------------------------|
| CP/M (CP/A, SCPX) | alles vor `data_cyl`/`data_head` | 15104 B |
| UDOS | Spuren 0–2 (Urlader + Nukleus) **+ Bootspur 21** | 13312 B je Seite |

Dass die UDOS-Bootspur dazugehört, ist am laufenden System gemessen: ohne sie bricht der
Kaltstart mit `ERROR: 45` ab (der Urlader liest sie, sichtbar im K5122-Leseprotokoll als
`READ D0 C=21 H=0`). Sie liegt **hinter** den Dateispuren, ist also kein durchgehendes
Band — im Abbild folgt sie den Spuren 0–2 hinten an. Die Reihenfolge der Stücke ist Teil
des Dateiformats und darf sich nicht ändern.

Drei Festlegungen:

1. **Geprüft wird vor dem Formatieren.** `DiskVolume::create` liest das Abbild und
   vergleicht es mit dem Fassungsvermögen, bevor die erste Spur entsteht — ein zu grosses
   Abbild hinterlässt keine halbe Diskette. Die Meldung nennt beide Zahlen.
2. **Kürzer ist erlaubt, länger nie.** Der Rest bleibt formatierte Leerspur (0xE5); ein
   längeres Abbild würde in das Dateisystem hineinschreiben.
3. **Ohne Systemspuren keine Bootfähigkeit.** `cpa800` beginnt auf Zylinder 0;
   `bootAreaCapacity` liefert dort 0, und die Oberfläche fragt gar nicht erst nach einem
   Abbild. Das ist keine Einschränkung des Werkzeugs, sondern die Diskette.

Belegt ist der Weg für alle drei Systeme: `test_disktool_bootdiskette` baut die Diskette
mit dem Werkzeug und **bootet sie im Emulator** — CP/A und SCPX bis zum `A>`-Prompt,
UDOS bis zum `%`-Prompt (dort braucht es zusätzlich zwei Dateien, §13b).

Schnittstellen: `DiskVolume::{bootAreaCapacity,bootAreaSize,readBootImage,writeBootImage}`,
`k1520d_create_bootable` / `k1520d_{fs_boot_capacity,boot_area_size,read,write}_boot_image`,
CLI `create --boot` / `boot-get` / `boot-put`, in der Oberfläche die Rückfrage bei „Neue
Diskette" und die Schaltfläche „Bootabbild sichern…".

---

## 13b. UDOS-Kopfsektorangaben — was eine Datei ausser ihren Bytes hat (2026-08-12)

Der Versuch, aus Systemspuren + kopierten Dateien eine bootfähige **UDOS**-Diskette zu
bauen, endete zunächst im UDOS-Debugger (`BREAK 2600`). Die Ursache liegt nicht in den
Spuren, sondern im **Kopfsektor** jeder Datei (§6 von `doc/udos_diskettenformat.md`):
er trägt Angaben, die eine Linux-Datei nicht mitbringt und die UDOS zum *Laden* braucht.

| Feld | Offset | `ZDOS` | Wozu |
|------|--------|--------|------|
| Typ | 12 | `81` = P1 | Programm vs. ASCII vs. Binär |
| Eigenschaften | 19 | `90` = W S | Schreib-/Löschschutz, SECRET |
| Satzlänge | 15 | 1024 | **Zuteilungseinheit**: ein Satz belegt `Satzlänge/128` aufeinanderfolgende Sektoren EINER Spur (§7) |
| Startadresse | 20 | `2600` | wohin gesprungen wird |
| **Ladeadresse** | **40** | `2600` | wohin das Speicherabbild geladen wird |
| **Abbildlänge** | **42** | 5521 | wie viel davon — **nicht** die Dateigröße (`OS`: 5504 Byte groß, 5632 Byte Abbild) |

Die beiden letzten Felder waren im Datenformat bis dahin nicht beschrieben; sie sind an
`OS`, `ZDOS`, `ACTIVATE`, `CAT`, `DO` und `SD` abgelesen (Ladeadresse = Startadresse bei
allen sechs, Länge stets ≤ Dateigröße) und **am laufenden System belegt**: erst mit
ihnen bootet eine neu geschriebene `ZDOS`.

Drei Entscheidungen:

1. **Die Angaben gehören in die Schreiboptionen, nicht in eine Heuristik.**
   `WriteOptions::udos_*` (bis `TransferOptions` und in die CLI-Schalter
   `--type/--props/--entry/--record-len/--load/--image-len`). Ohne Angabe bleibt es
   beim bisherigen Verhalten (A/B, 128er Sätze) — das ist für Nutzdateien richtig.
2. **Der Rundlauf trägt sie selbst**: `extractAll` legt neben die Dateien ein Beiblatt
   `udos-dateiangaben.txt`, `insertAll` liest es. Auch der Einzel-`insert` schaut nach
   (Ordner der Quelldatei und dessen Elternordner), damit eine aus der Oberfläche
   herübergezogene Systemdatei ihren Kopfsektor behält. Das Beiblatt zählt bei der
   Strukturprüfung **nicht** als „lose Datei" neben den `SideN/`-Ordnern.
3. **Das Flagbyte im Verzeichnis spiegelt SECRET** (§5) — sonst widersprächen sich
   Verzeichnis und Kopfsektor.

Dazu kam die **variable Satzlänge** im Schreibpfad (`allocRecords`): Sätze > 128 Byte
belegen mehrere aufeinanderfolgende Sektoren derselben Spur, alle mit demselben
Kontrollblock, und ein Satz überschreitet nie eine Spurgrenze.

**Kleinstmögliche UDOS-Bootdiskette**: Systemspuren + `OS` + `ZDOS`. Sie startet bis zum
`%`-Prompt (`MinimaleUdosDisketteBootetInsSystem`).

> **Offen:** der Selbststart über `OS.INIT` (Banner, `DATE`, `TAST`) läuft dort nicht.
> Sobald `DO` — der Interpreter für Kommandodateien — auf der Diskette liegt, meldet
> UDOS `MEMORY PROTECT VIOLATION` und überspringt die Startdatei; das System kommt
> trotzdem hoch. Eingegrenzt ist es auf `DO` allein: schreibt man auf einer sonst
> unveränderten Originaldiskette **nur** `DO` neu, tritt der Fehler auch dort auf,
> obwohl Kopfsektor und Inhalt byte-gleich sind und nur die Sektorlage sich ändert;
> `CAT`, `DATE` oder `ZDOS` neu zu schreiben stört den Start nicht. Bei der Störung ist
> `DO` **nicht geladen** (RAM ab E000H bleibt `FF`) — UDOS weist es also schon vor dem
> Laden ab. Naheliegender nächster Schritt: im Nukleus verfolgen, woher die Prüfung ihre
> Erwartung nimmt (Verdacht: eine im Bootabbild der Spur 21 mitgeführte Lage der
> aktivierten Module).

---

## 13c. Dateiangaben sehen und ändern — der Eigenschaften-Dialog (2026-08-13)

§13b hat die UDOS-Kopfsektorangaben in die Schreiboptionen und ins Beiblatt gebracht.
Was fehlte, war die **Sicht des Anwenders** darauf: sie waren nur über die CLI
(`attr`) erreichbar, und für CP/M gab es sie überhaupt nicht — `FileSystem::
setAttributes` kannte allein `UdosAttrs`.

Vier Entscheidungen:

1. **Zwei Attributstrukturen, nicht eine.** Neben `UdosAttrs` steht jetzt `CpmAttrs`
   (R/O, SYS, ARCHIV, Nutzerbereich) als **zweite Überladung** von
   `FileSystem::setAttributes`. Eine gemeinsame Struktur hätte für jedes Dateisystem
   die Felder des anderen mitgeschleppt; die Familien haben fachlich nichts gemeinsam
   ausser der Absicht. Die C-ABI bekommt entsprechend `k1520d_set_cpm_attrs` neben
   `k1520d_set_udos_attrs` — kein Umbau der bestehenden Signatur.
2. **Der Nutzerbereich ist Identität, kein Attribut.** Ihn zu ändern verschiebt die
   Datei nach `3:NAME.TYP`. `CpmFileSystem::setAttributes` prüft deshalb **vor** dem
   ersten Schreiben, ob im Ziel schon eine Datei gleichen Namens liegt, und lehnt
   sonst ab — zwei gleichnamige Dateien im selben Bereich fände CP/M beim Lesen
   nicht mehr auseinander. Geändert werden **alle Extents**: CP/M trägt die
   Attributbits in jedem Verzeichnisplatz erneut.
3. **Satzlänge und „Bytes im letzten Satz“ bleiben unveränderlich.** Beide bestimmen
   die Sektorlage der Daten; sie zu ändern hiesse die Datei neu zu schreiben. Der
   Dialog zeigt sie an und sagt im Tooltip, warum sie gesperrt sind — der Weg dahin
   ist `get` + `put --record-len …`. Damit gilt für den ganzen Dialog: **er fasst
   den Inhalt einer Datei nie an.**
4. **Geschrieben wird nur, was sich unterscheidet.** `PropertiesDialog.aenderungen()`
   vergleicht gegen den geladenen Eintrag und liefert ein Wörterbuch, das direkt an
   `set_udos_attrs`/`set_cpm_attrs` geht. Ein Dialog, den man nur ansieht und wieder
   schliesst, schreibt nichts — auch nicht dieselben Werte zurück (das würde bei UDOS
   das Änderungsdatum bewegen).

Dazu ein **CP/M-Beiblatt** `cpm-dateiangaben.txt` nach demselben Muster wie
`udos-dateiangaben.txt`: ohne es überlebten Nutzerbereich und Attributbits den
Rundlauf `extractAll` → `insertAll` nicht (aus `3:PIP.COM` wurde die Linux-Datei
`3_PIP.COM` und daraus beim Zurückschreiben eine gewöhnliche Datei im Bereich 0).
Eine Zeile entsteht nur, wenn es etwas zu sagen gibt; der Name auf der Diskette wird
beim Einfügen aus dem Beiblatt geholt (`zielName()`, benutzt von `checkFit` **und**
`insertAll`, damit Platzprüfung und Ausführung denselben Namen sehen).

Das **Archiv** (`app/disktool/archive.py`) druckt seitdem alle Angaben in einer
zweiten Tabelle „DATEIANGABEN IM EINZELNEN“ samt Spaltenlegende. Der Zweck ist
ausdrücklich die Wiederherstellbarkeit **von Hand**: Dateien zurückschreiben, Angaben
im Dialog einstellen. Maschinell ist es nicht nötig — die beiden Beiblätter liegen im
selben Archiv und werden beim Einfügen von selbst gelesen.

Wächter: `CpmFileSystemAttrs.*` (vier Fälle inkl. Kollision und aller Extents),
`DiskVolume.CpmBeiblatt*`, `py_disktool_gui` (Dialog für beide Familien, ungültige
Eingabe, Schreibschutz, Archivtabellen).

---

## 14. Sicherheit beim Schreiben

> **Entscheidung 2026-08-10 (E5): kein atomares Schreiben.** Der Entwurf sah ursprünglich
> `.tmp` + `rename()` vor (§14.3 unten, gestrichen). Der Bauherr hat das verworfen, und
> zwar mit dem besseren Argument: dass eine Datei kaputtgeht, wenn ein Programm mitten
> im Schreiben abstürzt, ist ein *normales* Risiko jeder Software — dann legt man die
> Diskette eben neu an. Was **nicht** passieren darf, ist eine Diskette zu verlieren,
> die man nur **ansehen** wollte. Genau dort setzt der Schutz jetzt an:
>
> * **Schreibgeschützt ist die Vorgabe.** `DiskVolume::open` öffnet schreibgeschützt;
>   der Schutz reicht bis ins `DiskImage` (dessen `flush()` schreibt dann nichts, auch
>   nicht aus dem Destruktor). Die Oberfläche zeigt das als
>   *Diskette ▸ Schreibschutz* (Strg+R), gesetzt beim Öffnen; das CLI öffnet für `ls`/`get`/`info`/`check`/`save-as`
>   schreibgeschützt und nur für `put`/`rm` schreibend.
> * **Schreiben ist ein bewusster Schritt.** Wer den Schutz aufhebt, weiß in diesem
>   Moment, dass er die Diskette verändern kann — und legt bei einem unersetzlichen
>   Stück vorher mit **„Speichern unter…"** eine Arbeitskopie an.
> * Die **Sicherungskopie `<name>~`** beim ersten Zurückschreiben bleibt: sie schützt
>   gegen den *logischen* Irrtum, nicht gegen den abgebrochenen Schreibvorgang.

Ein Werkzeug, das fremde, teils einmalige Datenträgerabbilder verändert, muss vorsichtiger sein
als der Emulator (der auf einer Arbeitskopie läuft):

1. **Alles im Speicher**: Änderungen gehen ins `DiskMedium`, die Datei wird erst beim
   ausdrücklichen Speichern angefasst (§5). Abbruch = Datei unverändert.
2. **Sicherungskopie**: beim ersten Schreiben auf eine bestehende Datei standardmäßig
   `name.hfe~` anlegen (CLI: `--no-backup`).
3. ~~**Atomar**: in `name.hfe.tmp` schreiben, dann `rename()`.~~ — verworfen, s. o.
4. **Schreibschutz** der Datei und `DiskImage::bindingWritable()` werden respektiert.
5. **`.img` + UDOS** wird abgelehnt, ebenso Speichern eines nicht `rawCompatible()`-Mediums als
   `.img` (`DiskImage::saveAs` meldet das bereits mit Grund).
6. **Kein Zugriff auf gemountete Abbilder** ist erzwingbar (A6) — deshalb warnt die GUI, wenn die
   Datei in der letzten Emulator-Sitzung als Laufwerk konfiguriert war (`app/config`).

---

## 15. Teststrategie

Eingeordnet in das bestehende System (`tests/README.md`, `doc/design/12_testing.md`) — eine Zeile
je Test über `k1520_add_test()`, Ebene = Verzeichnis = ctest-Label:

| Ebene | Tests |
|-------|-------|
| `unit/filesystem/` | `test_sector_space` (Layout-Reihenfolge ≡ `.img`-Offset, Kopf-Filter, Schreib-Roundtrip), `test_fs_catalog` (Schema, Validierung, Fehlermeldungen), `test_cpm_dir` (Extents, Blocklisten 8/16 Bit, Attribute, Nutzerbereiche), `test_udos_bitmap` (Bitlage MSB-first, Zähler), `test_udos_dir` (Einträge variabler Länge, `0xFF`-Ende, Altbestand), `test_udos_chain` (Satzverkettung, Längenrechnung), **`test_fs_detect`** (§12: jedes Referenzabbild wird erkannt; ein absichtlich abweichendes Abbild wird **abgelehnt** und die Meldung nennt die gemessene Geometrie; leere Diskette → Ablehnung), **`test_fit_planner`** (§9.2: Bedarf inkl. Blockrundung/Extents bzw. Kopfsektor/Satzrundung, spurweise UDOS-Suche) |
| `unit/peripherals/` | `test_track_codec` **erweitert** um `writeSector` (§4.1): CRC neu, `tail` erhalten, FM+MFM |
| `integration/` | **Roundtrip** je Dateisystem: Leerdiskette anlegen → n Dateien einfügen → auflisten → extrahieren → byte-identisch; **Lesen der echten Referenzabbilder** (`disks/cpa_*.hfe`, `disks/udos_boot_*.hfe`) gegen die im UDOS-Dokument §10/§11 dokumentierten Sollwerte (69 Dateien, 850 freie Sektoren, `UDOS.SYS.4.3`); Datei löschen und Platz wiederverwenden. **Beidseitiges UDOS (§9.1):** `extractAll` erzeugt `Side0/`+`Side1/` mit der richtigen Aufteilung; `insertAll` auf einen Ordner **ohne** `Side1/` schlägt fehl **und lässt die Diskette unverändert** (Medium byte-identisch); gleichnamige Dateien auf beiden Seiten bleiben getrennt. **Volllauf (§9.2):** zu viele Dateien → Fehler mit Zahlen, Medium unverändert; Platz nur auf Seite 1 frei → Einfügen auf Seite 0 scheitert trotz „genug Platz auf der Diskette“ |
| `cli/` | `k1520disktool ls/get/put/rm/create` über `tests/cli/run_case.py`, Ausgabe-Wortlaut und **Exit-Codes 2/3/4** (nicht erkannt / passt nicht / Ordnerstruktur); ein `all_commands_smoke`-Analogon, damit kein Kommando aus der Dispatch-Kette fällt |
| `python/` | `test_disk_c_api.py` (Header ↔ `.so` ↔ ctypes, mechanisch wie `test_c_api.py`), `test_disktool_gui.py` (PySide6 headless: Liste ist nach `open()` gefüllt, wird nach jedem Schreiben neu geladen, Schaltflächen bei fehlgeschlagener Erkennung gesperrt) |
| `system/` (Label `format_integration`, langsam) | **Die schärfste Probe:** DiskTool schreibt eine Datei auf eine Diskette → der **Emulator bootet sie** und liest sie mit `TYPE`/`DIR` bzw. UDOS `CAT`/`EXTRACT`. Und umgekehrt: im Emulator mit `PIP` erzeugte Datei wird vom DiskTool extrahiert. Das prüft beide Dateisysteme gegen die *echten* Betriebssysteme, nicht gegen unsere eigene Lesart. |

**Kreuzprobe gegen `cpmtools`** (optional, nur wenn `cpmls`/`cpmcp` im Pfad): gleiche Diskette,
gleiche Liste, gleiche extrahierten Bytes. Kein harter Testabhängigkeit — als Werkzeug in
`tests/support/` für die Entwicklungsphase, so wie `tools/disasm_difftest.py` beim Disassembler.

Die Regressionsläufe bleiben schnell: alles außer der `system/`-Ebene gehört in
`tools/dev.sh test` (Pre-Push-Hook), die Emulator-Kreuzproben laufen unter
`tools/dev.sh test-format`.

---

## 16. Build-Integration

```cmake
# Dateisystem-Schicht (rein logisch, kein Z80/Karten-Code)
add_library(k1520_fs STATIC
    core/filesystem/sector_space.cpp  core/filesystem/fs_profile.cpp
    core/filesystem/fs_catalog.cpp    core/filesystem/fs_detect.cpp
    core/filesystem/cpm/cpm_fs.cpp    core/filesystem/cpm/cpm_dir.cpp
    core/filesystem/udos/udos_fs.cpp  core/filesystem/udos/udos_dir.cpp
    core/filesystem/udos/udos_bitmap.cpp)
target_link_libraries(k1520_fs PUBLIC k1520_floppy2 k1520_util k1520_logger)
target_include_directories(k1520_fs PUBLIC ${CMAKE_SOURCE_DIR})

# libk1520disk.so — stabile C-ABI für Python
add_library(k1520disk SHARED core/api/k1520_disk_api.cpp)
target_link_libraries(k1520disk PRIVATE k1520_fs)

# CLI
add_executable(k1520disktool tools/k1520disktool.cpp)
target_link_libraries(k1520disktool PRIVATE k1520_fs)
```

`k1520_fs` erbt `K1520_FORMATS_DEFAULT` über `k1520_floppy2` — der Katalogpfad bleibt eine
einzige Wahrheit. Beide neuen Ziele landen in `build/` bzw. `build_trace/` und werden über
`tools/dev.sh` gebaut wie alles andere.

---

## 17. Umsetzung in Etappen

Jede Etappe ist für sich lauffähig und testbar; die Reihenfolge minimiert das Risiko, weil das
Schwierigste (UDOS-Schreiben) auf einem dann schon geprüften Unterbau steht.

> **Stand 2026-08-10 (Branch `create_disktool`): ALLE Etappen sind umgesetzt und grün.**
> Die gesamte Dateisystemschicht steht: Sektorraum, Geometriemessung, Katalog,
> **CP/M lesen und schreiben**, **UDOS lesen und schreiben** (inkl. `mkfs`),
> `DiskVolume` mit `Side0/Side1`, Transaktionen und Erkennung.
> Verifiziert an den echten Disketten des Projekts — jeweils gegen eine *unabhängige*
> Instanz, nicht gegen uns selbst:
> CP/M-Extraktion byteweise gegen `cpmtools`; CP/M-Schreiben gegen das **laufende CP/A**
> (`TYPE`/`DIR`); UDOS-Lesen gegen die am laufenden UDOS gemessenen Sollwerte
> (69 Dateien, 39 sichtbar, 850/1310 freie Sektoren, `EXTRACT SD`); UDOS-**Schreiben**
> gegen das **laufende UDOS** (`CAT` listet, `PRINT` gibt aus, `STATUS` bestätigt den
> freien Platz auf den Sektor genau).
> Dazu **C-API `libk1520disk.so`**, **CLI `k1520disktool`**, die **ctypes-Bindung**
> `app/core_binding/k1520disk.py` (mit dem Dreiklang-Test Header ↔ `.so` ↔ ctypes wie
> beim Emulator) und die **PySide6-Oberfläche** `app/disktool/` samt
> `run_disktool.sh` und Werkzeugreferenz `tools/k1520disktool.md`.
> Abweichungen von der Dateiliste oben: `CpmFileSystem` deckt Etappe 4 mit ab;
> `udos_dir.*` und `fs_detect.*` sind in `udos_fs.*` bzw.
> `disk_volume.*`/`geometry_probe.*` aufgegangen.

| # | Inhalt | Ergebnis |
|---|--------|----------|
| 1 | `TrackCodec::writeSector` (§4.1) + `SectorSpace` (§5) + Tests | Sektoren lesen/schreiben über alle drei Container, `tail` bleibt erhalten |
| 2 | `filesystems:`-Schema, `FsProfile`/`FsCatalog`, CP/A- und SCPX-Profile, UDOS-Geometrien aus den Referenzabbildern verifiziert; **Erkennung Stufe 1 + 2 (§12)** inkl. Ablehnungsmeldung | Katalog kennt die logische Ebene; jedes Abbild im Baum wird erkannt oder sauber abgelehnt |
| 3 | **CP/M lesen** + `DiskVolume` mit 1 Volume + `k1520disktool ls/get/info` | ersetzt `cpmls`/`cpmcp -f` für das Auslesen — der häufigste Anwendungsfall |
| 4 | **CP/M schreiben** (`put`, `rm`, `create`), Planer + Rücknahme (§9.2) + Roundtrip- und Emulator-Kreuzprobe | ersetzt `writeDiskUI.py`; „passt nicht“ ist ab hier eine geprüfte Zusage |
| 5 | **UDOS lesen** (Karte, Verzeichnis, Kette) + **mehrere Volumes** in `DiskVolume` + `Side0/Side1`-Extraktion — gegen `disks/udos_boot_scp.hfe` und die Sollwerte aus §10/§11 des UDOS-Dokuments | erstmals überhaupt UDOS-Dateien unter Linux, beide Seiten in einem Zug |
| 6 | **UDOS schreiben** + `mkfs` + `Side0/Side1`-Einfügen mit Strukturprüfung + Emulator-Kreuzprobe (`CAT`/`EXTRACT`) | Schreibpfad, den bisher nur UDOS selbst beherrscht |
| 7 | C-API + ctypes-Bindung + Dreiklang-Test | Python sieht die Bibliothek |
| 8 | **PySide6-Oberfläche** + `run_disktool.sh` + GUI-Smoke | das eigentliche Anwenderprogramm |
| 9 | `tools/k1520disktool.md`, `APP_README.md`/`README.md`-Verweise, `doc/udos_diskettenformat.md` §13 nachziehen | Dokumentation auf dem Ist-Stand |

Etappe 3 ist der erste echte Nutzen; ab Etappe 4 ist die `cpmtools`-Abhängigkeit der alten
Werkzeuge vollständig ersetzt.

---

## 18. Offene Punkte

1. **UDOS-Geometrien**: Spurzahl, Seitenzahl und Laufwerkszuordnung der vier Referenzabbilder
   sind aus den Dateien zu bestätigen, bevor die `formats:`-Einträge geschrieben werden
   (`doc/udos_diskettenformat.md` §12.3 nennt die Laufwerkstypen, nicht die Abbilder).
2. **CP/A-Verzeichnisse mit `os: cpm3`**: ob im Bestand Disketten mit CP/M-3-Zeitstempeln
   liegen, ist ungeprüft; das Schema sieht das Feld vor, die Umsetzung folgt bei Bedarf.
3. **Kopfsektor-Felder `HIGH ADDRESS` / `STACK SIZE`** (UDOS §6) sind nicht eindeutig zugeordnet.
   Beim Einfügen einer Datei vom Typ `P`/`P1` werden sie deshalb vorerst nicht gesetzt — für
   reine Datendateien ohne Belang, für ausführbare Dateien ein bekannter Vorbehalt.
4. **Satzlänge beim UDOS-Einfügen**: 128 ist immer sicher, größere Sätze sparen Verwaltung.
   Ob das Werkzeug die Satzlänge anbieten soll oder fest bei 128 bleibt, wird nach Etappe 5
   entschieden (dann kennen wir die Streuung im Bestand).
5. **Interleave beim Schreiben — entschieden (Etappe 6): dicht packen.** UDOS legt Sätze
   typisch mit Versatz 5 ab (`doc/udos_diskettenformat.md` §7), aber die beobachtete
   Folge (1, 6, 11, 16, 21, 26, **2**, 7, …) folgt keiner der geprüften Versatzregeln —
   sie ist offenbar Ablagegeschichte, keine Vorschrift. Der Versatz ist ohnehin nur eine
   Geschwindigkeitseigenschaft echter Hardware; die Verkettung steht explizit in den
   Zeigern. Das laufende UDOS liest dicht gepackte Dateien nachweislich einwandfrei
   (`PRINT`, Systemtest). **Ebenfalls entschieden: Satzlänge 128** beim Schreiben (§8.4
   nennt sie „immer sicher"); größere Satzlängen werden weiterhin gelesen.
6. **Einseitig beschriebene UDOS-Diskette**: Ob es die im Bestand gibt (Seite 1 unformatiert
   oder ohne gültige Belegungskarte), ist ungeprüft. Der Entwurf behandelt sie als
   1-Volume-Diskette mit flachem Ordner (§9.1) — das ist erst nach Etappe 5 an den vier
   `disks/udos_boot_*.hfe` zu bestätigen. Sollte sich zeigen, dass UDOS-Disketten **immer**
   beide Seiten führen, entfällt der Sonderfall ersatzlos.
7. **Feste Ordnernamen `Side0`/`Side1`**: bewusst nicht konfigurierbar, damit ein extrahierter
   Ordner ohne Zusatzwissen wieder einfügbar ist. Falls sich später zeigt, dass Anwender die
   UDOS-Laufwerksnummern (Seite 0 = Laufwerk 0, Seite 1 = Laufwerk 4) erwarten, wäre
   `Drive0`/`Drive4` die Alternative — dann aber projektweit einheitlich, nicht als Option.
8. **Doppelschritt-Disketten sind nicht katalogisierbar.** Die Austauschformate
   zwischen Laufwerken verschiedener Spurdichte (96 tpi schreibt jede zweite Spur,
   damit ein 48-tpi-Laufwerk lesen kann — `doc/format.md`, „Wozu einseitig und
   Doppelschritt gut sind") belegen nur jeden zweiten Zylinder. `tracks:` beschreibt
   aber zusammenhängende Bereiche. Die Erkennung lehnt sie deshalb ausdrücklich ab
   (Kriterium `gap_tracks`), statt ein 80-Spur-Format darauf zu ziehen und Datenmüll
   zu lesen. Abhilfe wäre ein Attribut `step: 2` am Format; zu ändern wären
   `TrackFormat`/`DiskFormat`, die Spurabbildung im `SectorSpace` und das
   Lückenkriterium im `GeometryProbe`. Betroffen: 13 der erzeugten Abbilder
   (CP/A-Geometrien `T`/`U`). Ausgearbeitet als Feature-Request:
   `doc/feature_requests/doppelschritt_disketten.md`.
9. **Geometrisch mehrdeutige Formate** (`cpa640` ≡ `k5601_16x256`, `scpx780` ≡ `scpx780_b`):
   Stufe 1 der Erkennung kann sie prinzipiell nicht trennen (§12.1). Solange sich die zugehörigen
   Dateisystemprofile unterscheiden, entscheidet Stufe 2; wo auch die identisch sind, ist die
   Unterscheidung für das Werkzeug ohne Folge. `detect_rank` ist die Notbremse, falls doch eine
   Reihenfolge nötig wird.

---

## 19. Diskeditor — die Diskette als Scheibe (2026-08-13)

Alles Bisherige sieht die Diskette **durch das Dateisystem**. Wo keines mehr
greift — eine Diskette mit CRC-Fehlern, ein halb formatierter Datenträger, ein
Verzeichnis, das ins Leere zeigt —, war das Werkzeug blind. Der Diskeditor ist die
Ebene darunter: Spuren, Sektoren, Gaps, CRCs.

### 19.1 Die Darstellung

Zwei Scheiben nebeneinander, links Seite 0, rechts Seite 1. **Spur 0 außen**,
**Sektor 0 bei 12 Uhr**; Seite 0 zählt im Uhrzeigersinn, Seite 1 dagegen — so, wie
man die Diskette sähe, wenn man sie umdreht. Sektor mit gültigen CRCs grün —
**hellgrün, wenn er zwar formatiert, aber nie beschrieben wurde** (das Datenfeld
trägt nur das Füllbyte des Formats; welches, ist dessen Sache, gezählt wird die
Einförmigkeit).  Der **UDOS-Anhang hinter der Daten-CRC zählt dabei nicht mit**:
er trägt die Dateiverkettung und ist auch auf einer frisch formatierten Diskette
belegt — mitgezählt sähe dort kein Sektor leer aus.  Sektor mit CRC-Fehler rot,
Gap orange, unformatiert grau, noch nicht gelesen schwarz (nur physisch, §11.2c
des Greaseweazle-Entwurfs).

**Die Winkel sind echt, und sie kosten nichts.** Eine `TrackImage` *ist* genau eine
Umdrehung; der Winkel eines Bytes ist damit `Position ÷ Spurlänge`. Bitrate und
Drehzahl aus dem HFE-Kopf (`hfe_codec.cpp`, Offset `0x0E`) werden dafür **nicht**
gebraucht — sie skalieren nur die Zeitachse und wären zudem bei krummer Drehzahl
eine Fehlerquelle. Sichtbarer Beweis am realen Abbild: die Schreibnaht wandert von
Spur zu Spur und ergibt eine Spirale, keine Speiche. Bei `.img` gibt es keine
Winkelinformation im Container; dort liegen die Sektoren gleichmäßig, und das sieht
man dem Bild dann auch an.

### 19.2 Vier Entscheidungen

1. **`scanTrack` liefert eine LÜCKENLOSE Abschnittsfolge** über `[0,1)`
   (`core/peripherals/floppy_drive/track_view.h`). Nur dann muss die Darstellung
   nichts raten und der Treffertest über den Winkel ist eindeutig. Ein Sektor, der
   über den Index läuft, wird an der Naht in zwei Abschnitte mit **derselben
   Nummer** geteilt — sonst wäre `start < end` nicht zu halten. Wächter:
   `TrackView.*` prüft die Abdeckung in jedem Fall mit.
2. **Gap und „unformatiert“ sind zwei Dinge.** Keine einzige Adressmarke =
   unformatiert (grau) — das ist der Zustand, den `DiskImage::createBlank` anlegt
   und den ein Gast erst formatieren muss. Alles zwischen den Sektorfeldern einer
   formatierten Spur = Gap (orange). Der Unterschied ist keine Kosmetik: er sagt,
   ob dort etwas fehlt oder ob dort nichts hingehört.
3. **Geschrieben wird über die LAUFENDE NUMMER, nicht über die Sektor-ID**
   (`TrackCodec::writeSectorAt`). Eine Spur darf dieselbe ID zweimal tragen
   (fehlerhaft formatiert, Kopierschutz); der Editor muss genau den Sektor treffen,
   den der Anwender angeklickt hat. `writeSector(id)` bleibt für den
   Dateisystempfad.
4. **Die CRC ist mitschreibbar.** `writeSectorAt` nimmt optional einen wörtlichen
   CRC-Wert; ohne ihn wird gerechnet. In der Oberfläche heißt das: das CRC-Feld ist
   editierbar, *Save Sektor* schreibt genau das, was dort steht, *Fix CRC* trägt den
   berechneten Wert ein. Erst damit lässt sich eine **schadhafte Diskette
   originalgetreu nachbilden** — ohne diesen Weg wäre jeder geschriebene Sektor
   zwangsläufig gültig. Der Preis ist ein Knopf mehr; er ist es wert.

Dazu, rein additiv: `parseTrack` liefert jetzt die **Byte-Offsets** (`sync_pos`,
`id_pos`, `data_pos`, `end_pos`), die gespeicherten CRCs und das Kennzeichen
`deleted` (Datenmarke `0xF8`). Ohne die Offsets ließe sich nicht zeichnen, *wo* ein
Sektor liegt.

### 19.3 Oberfläche

`app/disktool/ui/disk_editor.py`, geöffnet über den Knopf **Diskeditor** im
Hauptfenster (nicht modal, genau eines je Fenster, mit Maximieren-Knopf — ein
QDialog bekommt den vom Fensterverwalter sonst nicht). Oben die Scheiben, unten
die Wählerzeile `[−] Seite: [0] [+]  [−] Spur: [25] [+]  [−] Sektor: [3] [+]`,
CRC-Feld, Hexfeld (**32 Byte je Zeile** — ein 1024-B-Sektor ergibt damit 32 Zeilen)
und die drei Knöpfe *Reload Sektor* / *Fix CRC* / *Save Sektor*.

**Die Wähler sind kein Zierrat**: einen bestimmten Sektor auf der Grafik zu treffen
ist Sucharbeit. Wer weiß, dass er Spur 25 will, tippt sie; wer *sucht*, schaltet mit
den Knöpfen durch (gedrückt halten blättert weiter). `[+]`/`[−]` beim Sektor geht
**in Spurreihenfolge**, nicht zur nächsten ID — die IDs liegen wegen Sektorversatz
weder lückenlos noch aufsteigend. Eine unmögliche Eingabe wird zurückgesetzt und
begründet, statt die Anzeige wegspringen zu lassen.

**`Save Sektor` schreibt bis in die Datei** (`sector_write` + `flush`). Der Entwurf
sah zuerst nur das Medium im Speicher vor, wie bei jeder anderen Änderung — für
einen *Sektor*editor ist das aber eine Falle: man glaubt gespeichert zu haben. Die
Sicherungskopie `…~` entsteht dabei wie immer beim ersten Schreiben. **Ausnahme mit
Ansage:** ein `.img` ist ein reines Sektorabbild und führt gar kein CRC-Feld — eine
absichtlich falsche CRC ist dort nicht darstellbar, `flush` verweigert, und der
Editor sagt es samt Ausweg („Speichern unter…“ als `.hfe`/`.dmk`). Die Änderung
bleibt dann im Speicher stehen. Wächter:
`test_disk_editor_says_when_img_cannot_hold_a_broken_crc`.

Drei Umsetzungsdetails, die man nicht aufweichen sollte:

* **Der Treffertest ist analytisch** (Polarkoordinaten → Ring → Winkel → Abschnitt),
  kein Szenengraph. Bei 80 Spuren × 26 Sektoren × 2 Seiten wären es sonst ~4000
  Objekte, durch die jede Mausbewegung liefe. Wächter:
  `test_disk_editor_hit_test_finds_the_drawn_sector` rechnet für **jeden** Sektor
  den gezeichneten Mittelpunkt zurück — das ist die einzige Prüfung, die
  Winkelrichtung, Ringlage und Nullpunkt zwischen Zeichnen und Treffen wirklich
  zusammenhält.
* **Das Bild liegt als Pixmap vor** und wird nur bei Größen- oder Datenänderung neu
  gezeichnet; die Auswahl kommt obenauf.
* **Trennlinien erst ab 5 px Ringhöhe.** Bei 80 Spuren sind 80 schwarze Ringlinien
  plus 26 Speichen je Ring mehr Linie als Farbe.
* **Metadaten und Nutzdaten sind getrennte Felder.** Die Sektorangaben stehen als
  Beschriftung über dem Hexfeld, nicht darin: nur so ist beim Zurücklesen eindeutig,
  was Inhalt ist und was Anzeige. Gelesen wird ohnehin **positionsgenau** nur die
  Hexspalte — ein `.` in der ASCII-Deutung kann keine Hexziffer werden, und wer den
  Offset verbiegt, ändert nichts.
* **Das Hexfeld überschreibt, es fügt nicht ein** (`setOverwriteMode`), und nach jeder
  Änderung wird der Dump neu erzeugt, damit die **ASCII-Spalte mitläuft** (Schreibmarke
  bleibt stehen). Im Einfügemodus verschöbe jede getippte Ziffer den Rest der Zeile
  und die feste Spaltenlage — die Grundlage des positionsgenauen Zurücklesens — wäre
  dahin. Ein halb getippter Bytewert lässt alles unverändert stehen.
* **Die Beschriftung „Seite 0/1" steht im leeren Eck oben links**, nicht über der
  Scheibe: ein Kreis füllt sein Quadrat nicht aus. Das sind 26 Pixel Höhe, die sonst
  verschenkt wären.

### 19.4 Sektoren anlegen und löschen (2026-08-13)

Wer eine schadhafte Diskette *nachbauen* oder eine Lücke schliessen will, muss eine
Spur von Hand zusammensetzen können. Dafür zwei Knöpfe und ein Dialog.

**Die ID bestimmt die Lage.** `TrackCodec::newSectorPosition` setzt den neuen Sektor
hinter den vorhandenen mit der **nächstkleineren ID**, um den eingestellten Gap
versetzt; gibt es keinen kleineren, hinter den Index (12 Uhr). Daraus folgt ein
Verhalten, das gewollt ist und in der Bedienung sichtbar sein muss: legt man 0, 1
und dann 5 an, sitzt die 5 physisch an dritter Stelle — und ein danach angelegter
Sektor 2 landet *ebenfalls* hinter der 1 und überschreibt sie. Wer Platz für 2, 3
und 4 lassen will, gibt beim Anlegen der 5 einen entsprechend grossen Gap an. Die
Alternative — automatisch die erste passende Lücke suchen — wäre bequemer und
verschleierte genau das, was man beim Nachbau einer Spur kontrollieren will.

**Die Spurlänge ist fest.** Eine Umdrehung hat so viele Bytes, wie sie hat;
geschrieben wird über vorhandenes Gap und, wenn der Gap zu knapp ist, über den
Nachbarn. `TrackImage::bitcells` bleibt damit gültig, das HFE-Rückschreiben
unverändert. Was nicht mehr vor das Spurende passt, wird abgelehnt — ohne etwas zu
ändern. Vor dem Überschreiben eines vorhandenen Sektors **fragt** die Oberfläche
(das Modell erlaubt es, unbeabsichtigt soll es trotzdem nicht passieren); der
Dialog nennt Zielposition, Länge und die betroffenen Sektoren schon *vor* dem
Bestätigen (`planSector`).

**FM und MFM sind nicht mischbar** — das Verfahren hängt am Bit-Codec der ganzen
Spur (`TrackImage::encoding`). Auf einer Spur mit Sektoren ist die Auswahl deshalb
gesperrt und zeigt nur an, was gilt; auf einer markenlosen Spur legt der erste
Sektor sie fest.

**Der Gap-Vorschlag wird gemessen**, nicht geraten: der Median der Gap-Längen
*dieser* Spur. Ein nachgelegter Sektor sieht damit aus wie seine Nachbarn, auch auf
einer fremd formatierten Spur.

**Löschen** (`eraseSectorAt`) überschreibt den Bereich von der Sync-Gruppe bis
hinter die Daten-CRC (plus den UDOS-Kontrollblock) mit dem Gap-Füllbyte und nimmt
die Marken weg. Die Nachbarn bleiben unangetastet, die Spurlänge bleibt.

Dazu eine Korrektur an `parseTrack`: **`sync_pos` zeigt jetzt auf den Anfang der
Sync-Gruppe** (die 00-Bytes vor den A1), nicht auf das erste A1. Sonst wichen „wo
fängt der Sektor an" (Anzeige) und „wo setzt ein neuer auf" (`newSectorPosition`)
um die Sync-Länge voneinander ab, und die Überschneidungswarnung urteilte über eine
andere Stelle, als sie beschrieb. Rückwärts gelaufen wird höchstens eine doppelte
Sync-Länge, damit ein auf Nullbytes endendes Datenfeld nicht verschluckt wird.

### 19.6 Ganze Spuren löschen und einfügen (2026-08-17)

Zwei Knöpfe neben „Neuer Sektor" — eine Ebene darüber: sie ändern die **Geometrie**
des Abbilds, nicht seinen Inhalt.

* **Spur löschen** wirft den gewählten Zylinder (beide Seiten) heraus; alles dahinter
  rückt auf.  Für Abbilder mit mehr Spuren, als hineingehören (82 statt 80), und zum
  Zurechtstutzen auf eine Zielgeometrie — 77 Spuren, damit es auf eine 8″-Diskette
  passt.
* **Spur einfügen** fragt in einem Dialog nach **Stelle** und **Verfahren** und
  setzt dort einen **unformatierten** Zylinder ein; alles ab dort rückt nach hinten.
  Sektoren legt man danach einzeln an.

  Beides muss wählbar sein, und zwar wegen der **gemischten Formate** der K1520-Welt:
  * die **Stelle** ist die Nummer, die die neue Spur bekommt — zulässig ist auch
    **0** (vor allen bestehenden, für eine FM-Systemspur vor MFM-Daten) und das Ende
    (anhängen).  Aus der jetzigen 42 wird dabei die 43.
  * das **Verfahren** folgt bewusst NICHT dem Nachbarn — gerade der Wechsel ist der
    Zweck.  Vorbelegt ist das des künftigen Vorgängers, denn das ist der übliche Fall.

  **Die Länge hängt am Verfahren.**  Zellen je Umdrehung sind eine Eigenschaft der
  Scheibe, die Bytezahl nicht: MFM braucht 16 Zellen je Byte, FM deren 32.  Eine
  FM-Spur trägt also **halb so viele Bytes** wie ihr MFM-Nachbar — und passt genau so
  in dieselbe Umdrehung (gemessen: 3136 gegen 6209 Byte).  Der Dialog sagt vorher,
  was geschieht.

Beides verlangt ein **vollständiges** Abbild, ist bei **Schreibschutz gesperrt** (es
ändert die Geometrie — erst recht nichts für „nur lesen") und **löst vom Laufwerk**
(wie die Schnitte in §12.6 des Greaseweazle-Entwurfs): die Spurnummer stimmt danach
nicht mehr mit der Kopfposition überein.

> **Unformatiert heisst nicht leer.**  Die eingefügte Spur trägt **Gap-Füllbytes** in
> Länge und Verfahren ihres Nachbarn — so wie eine gelöschte echte Spur Fluss trägt,
> nur ohne Marken.  Eine Spur *ohne Bytes* gibt es in dieser Geometrie dagegen gar
> nicht (`TrackView::exists == false`), und in eine solche lässt sich kein Sektor
> legen: das Anlegen landete dann auf der nächsten formatierten Spur.  Mit Gap-Fluss
> lässt sich die neue Spur **von Hand formatieren** — Sektor für Sektor, mit `Neuer
> Sektor`.  Sie ist trotzdem kein Abklatsch des Nachbarn: dessen Sektor-IDs stünden
> sonst auf der neuen Spur.

Wächter: `test_disk_editor_loescht_und_fuegt_ganze_spuren_ein`,
`test_eine_eingefuegte_spur_laesst_sich_von_hand_formatieren`,
`test_disk_editor_sperrt_spuraenderungen_bei_schreibschutz`.

### 19.5 UDOS-Anhang in der Sektorzeile

Bei UDOS hängen hinter der Daten-CRC **4 Byte Sektorkontrollblock** (Rückwärts- und
Vorwärtszeiger, `doc/udos_diskettenformat.md` §1.1) — der Sektor ist also grösser,
als sein ID-Feld sagt. Die Sektorzeile nennt deshalb `IBM-MFM + UDOS-Erweiterung`,
rechnet die Grösse als `128+4 Byte` (Nutzdaten + Kontrollblock — die Daten-CRC
zählt hier so wenig mit wie bei CP/M, sie hat ihr eigenes Feld) und
zeigt beide Zeiger im Klartext (`zurück: Spur 22/Sektor 6   vor: …`, `FF FF` =
Kettenende).  **Alles in EINER Zeile** — Format, Grösse, Rohbytes, Deutung: jede
zusätzliche Zeile im unteren Teil fehlt oben der Scheibe.

**Ob es den Anhang gibt, sagen SEKTOR und Dateisystem gemeinsam** — und beides wird
gebraucht.  Anfangs hing es an `filesystem_type == "udos"` — und damit
sah man auf einer **gemischten oder gar nicht erkannten** Diskette ihre
UDOS-Sektoren ohne Verkettung, obwohl der Kontrollblock danebenstand.  Gerade dort
will man ihn aber sehen; die Erkennung ist ja gescheitert.  Entschieden wird am
Inhalt: hinter dem Datenfeld steht auf einer gewöhnlichen IBM-Spur das
**Gap-Füllbyte** (MFM `4E`, FM `FF`) — weicht eines der ersten vier Bytes davon ab,
gilt der Anhang als belegt (`TrackSpan::tail_bytes`).

Das allein reicht aber nicht: auf einer **frisch formatierten** UDOS-Diskette lautet
der Kontrollblock nie beschriebener Sektoren `4E 4E 4E 4E` (`doc/udos_diskettenformat.md`
§1.1) — vom Gap nicht zu unterscheiden.  Wo UDOS **erkannt** ist, wissen wir es
trotzdem besser.  Deshalb: **Inhalt ODER Dateisystem.**  Nur der Inhalt war zu streng
(frisch angelegte Disketten zeigten nichts), nur das Dateisystem war es auch
(gemischte Disketten zeigten nichts).  Bei CP/M spricht keines von beiden dafür, dort
bleiben die Angaben weg.  Wächter: `TrackViewAnhang.*`,
`test_disk_editor_zeigt_den_udos_anhang_auch_ohne_erkanntes_udos`,
`test_frisch_angelegte_udos_diskette_zeigt_ihre_anhaenge`.

Der Anhang ist **änderbar**: die vier Rohbytes stehen in einem eigenen Eingabefeld
(gesperrt, solange die Diskette schreibgeschützt ist), die Deutung daneben. Zwei
Gründe für das eigene Feld statt einer Zeile im Hexdump: die Verkettung zu ändern
ist etwas anderes, als die Nutzdaten zu ändern, und der Schreibweg ist ein anderer —
`DiskVolume::writeSectorTail` schreibt **nur** den Nachspann und übernimmt die
vorhandene Daten-CRC wörtlich, damit ein absichtlich defekter Sektor defekt bleibt.
Geschrieben wird mit demselben *Save Sektor*. Wächter:
`DiskVolume.NachspannSchreibenLaesstDatenUndCrcInRuhe`,
`test_udos_tail_is_saved_without_touching_data_or_crc`.

**Grenze:** der Editor hängt an einer *geöffneten* Diskette, und geöffnet wird nur,
was die Erkennung (§12) durchlässt. Eine Diskette ganz ohne brauchbares Dateisystem
— gerade der Fall, für den ein Sektoreditor gemacht ist — lässt sich damit heute
nicht ansehen. Ein „roh öffnen" (nur Geometrie, kein Dateisystem) wäre die
Fortsetzung; `GeometryProbe::synthesize` liefert die Geometrie dafür bereits.

## 20. Oberfläche als gewöhnliche Anwendung (2026-08-14)

Bis hierher wuchs das Fenster mit den Fähigkeiten mit: jede neue Fähigkeit bekam
eine Schaltfläche, und die landete in einer der **zwei Knopfleisten auf halber
Höhe** — oben Öffnen/Neu/Dateisystem/Nur-lesen, unten acht weitere. Am Ende
klemmten sie den Inhalt ein, die Menüleiste blieb leer (obwohl `QMainWindow`
schon benutzt wurde), und **Meldungen erschienen an vier Orten ohne Rollen**:
Protokollkasten unten, Statuszeile, Hinweistext über der Dateiliste, dazu ein
selbstgemachtes `●` im Fenstertitel. Dieselbe Meldung stand oft an dreien
gleichzeitig, ein *Zustand* („schreibgeschützt") dagegen im Protokoll, wo er
wegscrollt.

Der Umbau macht daraus ein Fenster nach den Gepflogenheiten der Werkzeugkiste:
**Menüleiste + ausblendbare Symbolleiste + Kopfbereich + Meldungsstreifen +
Statuszeile + Protokoll-Dock**. Die Fach-Logik ist unangetastet geblieben — alle
Methoden (`open_image`, `insert_all`, `save_as`, …) sind weiter dialogfrei
aufrufbar, und genau das machte den Umbau billig.

```
Titelleiste      udos_boot_scp.hfe — k1520DiskTool     ← Identität + Änderungsmarke
Menüleiste       Datei · Bearbeiten · Diskette · Übertragung · Ansicht · Hilfe
Symbolleiste     [Öffnen][Neu] │ [Speichern][Unter…][Archiv] │ [Diskeditor][Bootabbild]
                 │ [Holen][Schreiben][Löschen] │ [Schutz]      ← ausblendbar
Kopfbereich      💿 Pfad / Format · Dateisystem · Seiten        [Dateisystem ▾]
Meldungsstreifen ⚠ Medium: … (nur bei Bedarf, wegklickbar)
Inhalt           Diskette │ →→| →| |← |←← │ Ordner   (beide Hälften gleich breit)
Protokoll-Dock   (unten, standardmäßig zu, F8)
Statuszeile      letzte Aktion            │ 69 Dateien · 106 KB frei │ binär │ 🔓 R/W
```

### 20.1 Warum überhaupt Menü statt Knöpfe

Nicht wegen des Aussehens, sondern wegen der **Vollständigkeit**: eine Knopfleiste
ist immer eine Auswahl, ein Menü ist eine Landkarte. Seitdem lässt sich sagen
*„jede Aktion steht in der Menüleiste"* — und das ist prüfbar
(`test_every_action_is_reachable_from_the_menu_bar` sucht jede `act_*` des
Fensters in der rekursiv durchlaufenen Menüleiste). Eine künftig ergänzte Aktion,
die nur in die Symbolleiste gehängt wird, fällt sofort auf; sie wäre nach dem
Ausblenden der Leiste unerreichbar.

**Beide Hälften sind gleich gebaut und gleich breit**: Überschrift + Liste, sonst
nichts. Die frühere Fusszeile unter dem Ordner („12 Dateien") ist entfallen —
Zahlen stehen in der Statuszeile —, und der Teiler startet mittig mit gleichen
Dehnungsfaktoren. Ungleiche Spalten lesen sich wie eine Aussage darüber, welche
Seite die wichtigere sei; das ist keine. Guards:
`test_both_panes_are_the_same_width`, `test_the_folder_pane_has_no_footer_line`.

### 20.2 Kopfbereich statt Kopfleiste

Was **dauerhaft** über die geöffnete Diskette gilt, steht in zwei Zeilen über
beiden Hälften (`ui/disk_header.py`): Pfad, Format, Dateisystem samt Urteil der
Erkennung, Seitenzahl. Rechts daneben das Auswahlfeld, mit dem sich die Erkennung
übersteuern lässt — es steht dort und nicht in einer Knopfleiste, weil es sich auf
die Angabe daneben bezieht.

Dieselbe Wahl gibt es zusätzlich als Radiogruppe unter *Diskette ▸ Dateisystem
übersteuern*. Damit beide nicht auseinanderlaufen, kommt die Liste aus **einer**
Funktion (`disk_header.auswahlliste()`) und gesetzt wird sie an **einer** Stelle
(`MainWindow._fs_setzen`). In der Liste steht neben den Katalogprofilen auch
`cpa_auto` — die abgeleitete Erkennung (§6.4) hat keinen Katalogeintrag, ist aber
ein zulässiger Zwang; ohne sie liesse sich eine profillose Diskette nicht öffnen.
Guard: `test_filesystem_choice_stays_in_step_between_header_and_menu`.

Alles, was man nur gelegentlich nachschlägt — Geometrie, Alternativen der
Erkennung, Auffälligkeiten, Belegung je Seite —, steht seitdem unter *Diskette ▸
Diskettenangaben…* (`ui/disk_info_dialog.py`), nicht im Kopf.

### 20.3 Eine Aktion, drei Orte — und ein Ort zum Sperren

Alle Aktionen entstehen an einer Stelle (`ui/actions.py`, Tabelle `_SPEC`) mit
Beschriftung, Symbol, Kürzel und Statustext und werden als `fenster.act_<name>`
abgelegt; Menü, Symbolleiste, die beiden Kontextmenüs und die Mittelspalte
(→/←) zeigen **dasselbe Objekt**. Wer eine Aktion sperrt, sperrt sie damit
überall — vorher musste jede Sperre in einer Schleife über Schaltflächen
wiederholt werden.

Gesperrt und freigegeben wird ausschließlich in `_aktionen_pruefen()`, in drei
Stufen:

| Stufe | Bedingung | Betrifft |
|-------|-----------|----------|
| offen | eine erkannte Diskette liegt vor | Speichern unter, Archivieren, Diskeditor, Alles extrahieren, Angaben, Schließen |
| schreibbar | zusätzlich: Schreibschutz fort | Speichern, Alles einfügen |
| ausgewählt | zusätzlich: in der *zuständigen* Liste ist etwas markiert | Holen, Schreiben, Löschen, Eigenschaften |

Zwei Aktionen fallen aus diesem Raster, beide vom physischen Laufwerk
(`14_physische_diskette.md` §12.2, hereingekommen 2026-08-16): *Datei ▸ Physisches
Laufwerk…* hängt an gar keiner Diskette — sie öffnet ja erst eine — und wird
stattdessen **einmal beim Aufbau** danach gesperrt, ob die Greaseweazle-Hosttools
überhaupt da sind. Und *Diskette ▸ Diskette neu beschreiben* ist **unsichtbar**
statt gesperrt, solange keine physische Sitzung läuft: an einer Datei gibt es
keine Schadstelle, die es heilen könnte, und ein dauerhaft grauer Menüpunkt
behauptete, es gäbe sie.

Die dritte Stufe ist neu und lässt den früheren Hinweis *„Keine Datei
ausgewählt"* verschwinden: eine Aktion, die gerade nichts zu tun hätte, ist
gesperrt statt gesprächig. Über Kontextmenü und Ziehen kann der Fall trotzdem
eintreten — dann sagt es die Statuszeile, kein Meldungsfenster. Guards:
`test_selection_gates_the_transfer_actions`,
`test_context_menus_use_the_same_actions_as_the_menu_bar`.

Die **Mittelspalte** ist geblieben. Räumlich ist „von hier nach dort" eindeutiger
als jeder Menüpunkt; die Knöpfe sind aber nur noch Anzeigen derselben Aktion
(`QToolButton.setDefaultAction`), keine eigene Verdrahtung. Sie trägt **vier**
Knöpfe — aussen die Stapel, innen die Auswahl, und die Pfeillänge sagt, wie viel
wandert:

```
  →→|   Alles extrahieren     (ganze Diskette → Ordner)
   →|   Auswahl holen
   |←   Auswahl schreiben
  |←←   Alles einfügen        (ganzer Ordner → Diskette)
```

Guard: `test_the_middle_column_offers_selection_and_everything` (Reihenfolge,
Symbol und Menü-Erreichbarkeit aller vier).

**Der Schreibschutzknopf zeigt seinen Zustand**, statt ihn nur zu schalten: ein
rastender Knopf ist von aussen schwer zu lesen („ist er gedrückt?"), deshalb
wechseln Symbol **und** Beschriftung mit — geschlossenes Schloss + `R/O` gegen
offenes Schloss + `R/W` (`MainWindow._schutz_anzeigen`, aus `_aktionen_pruefen()`
heraus). Dasselbe Bild trägt der Menüpunkt, dieselbe Aussage steht rechts in der
Statuszeile. Guard: `test_the_protection_button_shows_which_state_it_is_in`.

Die Beschriftung im Menü ist ausführlich („In den Ordner holen"), die in der
Leiste kurz („Holen") — über `QAction.setIconText` (Tabelle `KURZ`). Ohne das
kippt die Leiste schon bei 1150 px Fensterbreite in den Überlauf (»), und die
letzten Knöpfe sind unsichtbar, obwohl die Leiste eingeblendet ist.

### 20.4 Die sechs Meldungsorte und ihre Rollen

Das eigentliche Aufräumen. Jede Sorte Meldung hat **genau einen** Ort:

| Ort | Rolle | Lebensdauer |
|-----|-------|-------------|
| Fenstertitel | Identität + Änderungsmarke | ganze Sitzung |
| Kopfbereich | dauerhafte **Eigenschaften** der Diskette | solange offen |
| Meldungsstreifen (`ui/info_bar.py`) | dauerhafte **Einschränkungen**, wegklickbar | bis geschlossen / nächste Diskette |
| Statuszeile links | Ergebnis der **letzten Aktion** | 8 s |
| Statuszeile rechts | **Zustand** als Widget, nie als Fließtext | solange offen |
| Protokoll-Dock (`ui/log_dock.py`) | **alles**, mit Uhrzeit | ganze Sitzung |
| Meldungsfenster | nur **Abbruch** oder Rückfrage | modal |

Fünf Festlegungen, die dabei nicht aufzuweichen sind:

1. **Alles geht ins Protokoll**, ausnahmslos — es ist die Historie, nicht die
   Anzeige. Deshalb darf es zu sein, und ist es beim Start auch (F8 holt es
   hervor); ein verborgenes Dock nimmt trotzdem Text an, die Historie ist also
   auch dann vollständig, wenn sie nie aufgeklappt wurde. Guard:
   `test_log_dock_is_closed_at_start_but_keeps_the_history`.
2. **Die Statuszeile ist flüchtig, der Kopfbereich ist es nicht.** Der
   Schreibschutz wurde früher ins Protokoll geloggt — falsch, er ist ein Zustand:
   jetzt Schlossanzeige rechts in der Statuszeile plus rastender Knopf in der
   Leiste. Guard: `test_status_bar_shows_the_write_protection_as_a_state`.
3. **Die Änderungsmarke ist Qts eigene**: `[*]` im Titelmuster +
   `setWindowModified()`, statt eines selbstgemalten `●`. Damit trägt jede
   Plattform ihr übliches Zeichen. Der Titel zeigt nur noch den **Dateinamen**;
   der volle Pfad steht im Kopf (vorher stand er in beiden).
4. **Modal nur bei Abbruch.** Erfolg wird nie mit einer Box gemeldet; „nichts
   ausgewählt" ebenso wenig (s. §20.3).
5. **Der Streifen liegt über beiden Hälften**, nicht in der Diskettenspalte —
   auch das Einfügen aus dem Ordner kann etwas zu melden haben.
6. **Die Statuszeile ist nicht abschaltbar.** Sie trägt den Zustand der
   geöffneten Diskette (Schreibschutz, freier Platz, Übertragungsart); wer sie
   ausblenden könnte, arbeitete blind. Symbolleiste und Protokoll dürfen weg,
   sie nicht. Das Schloss darin ist ein **Bild**, kein Emoji — 🔒 und 🔓 sehen in
   vielen Schriften gleich aus, und dann sagt die Anzeige nichts. Guards:
   `test_the_status_bar_cannot_be_switched_off`,
   `test_status_bar_shows_the_write_protection_as_a_state`.

### 20.5 Fensterzustand — und warum die Tests ihn nicht anfassen

Größe, Lage, Sichtbarkeit von Leiste und Dock, Leistenstil, Übertragungsart und
die zuletzt geöffneten Abbilder überleben das Beenden (`saveGeometry`/`saveState`
in `QSettings`). Der Haken daran: Testläufe würden dabei in die Einstellungen des
Anwenders schreiben — und, schlimmer, dessen ausgeblendete Symbolleiste erben und
daran scheitern.

Deshalb die Regel: **gespeichert wird nur, wenn sich die Anwendung benannt hat.**
`app/disktool/main.py` setzt `setOrganizationName`/`setApplicationName`; die
Testläufe legen eine namenlose `QApplication` an, und `_einstellungen()` gibt dann
`None` zurück. Guard:
`test_a_nameless_application_does_not_touch_the_users_settings`.

### 20.6 Symbole liegen im Baum, nicht im Systemthema

`QIcon.fromTheme()` liefert unter Windows **nichts** — ein Paket, dessen
Symbolleiste auf einer Plattform leer bleibt, ist keins (§13 der
Verteilungsentwürfe). Die Zeichnungen liegen deshalb als einfarbige SVG unter
`app/icons/` (17 Stück) und werden beim Laden eingefärbt: `ui/icons.py` ersetzt das Wort
`currentColor` durch die Textfarbe der laufenden Palette und rastert je Größe neu
(16/22/32/48). So passt ein Satz zu hellem wie dunklem Thema, und `app/` wandert
ohnehin als Ganzes in die Payload. Guards:
`test_every_bundled_icon_can_be_rendered`,
`test_toolbar_shows_actions_that_all_exist_in_the_menu` (jede Leistenaktion trägt
ein Symbol).

### 20.7 Das Handbuch — eine `.md`, von Qt selbst gesetzt

Die Hilfe war eine HTML-Zeichenkette im Quelltext. Das trägt für fünf Absätze und
nicht weiter. Sie ist jetzt eine gewöhnliche Markdown-Datei,
`app/disktool/help/handbuch.md`, angezeigt in einem eigenen Fenster
(`ui/help_window.py`, F1).

**Warum kein Bauschritt.** `QTextBrowser` versteht Markdown seit Qt 5.14
(`QTextDocument::setMarkdown`, GitHub-Erweiterungen inbegriffen). Nachgemessen an
einer Probe: Überschriften, Auszeichnung, geordnete und ungeordnete Listen,
Ankreuzlisten, **Tabellen** und Verweise kommen sauber durch; Zitatblöcke werden
nur eingerückt, Codeblöcke bekommen keinen Hintergrund. Das reicht für Text und
Überschriften — und es kostet **nichts**: keine Abhängigkeit, kein erzeugtes
Artefakt, keine Zeile in `release.yml` oder im MSVC-Lauf.

Die Alternativen und warum sie hier nicht lohnen:

| Weg | Urteil |
|-----|--------|
| Markdown → HTML beim Bauen (python-markdown, cmark, pandoc) | Der Gewinn wäre klein: angezeigt wird weiter mit `QTextBrowser` und dessen CSS-Ausschnitt. Lohnt erst mit Bildern oder Syntaxfärbung — dann bleibt die `.md` die Quelle und es kommt nur ein Umwandler davor. |
| `QtWebEngine` | Volles CSS, aber 100–150 MB im Paket. Die Nutzlast ist ~2 MB. |
| Qt-Hilfe (`.qhp`/`.qch`, `QHelpEngine`) | Der offizielle Weg mit Volltextindex; braucht `qhelpgenerator` und das QtHelp-Modul. Für ein Kurzhandbuch überdimensioniert, ab ~50 Seiten die richtige Antwort. |
| Systembrowser (`QDesktopServices`) | Der Anwender verlässt das Programm, und im Paket müsste trotzdem HTML liegen. |

Vier Festlegungen:

1. **Die Datei liegt unter `app/`**, nicht in `doc/`. `packaging/build_payload.sh`
   schiebt den `app/`-Baum als Ganzes in die Nutzlast; damit ist das Handbuch ohne
   eine einzige Zeile Paketierungsänderung dabei. `doc/` ist **nicht** im Paket.
   Guard: `test_help_manual_ships_inside_the_app_tree`.
2. **Das Inhaltsverzeichnis kommt aus den Textblöcken, nicht aus Ankern.** Qts
   Markdown vergibt keine Anker, `[…](#abschnitt)` funktioniert also nicht.
   Gesprungen wird auf die Blöcke mit `headingLevel() == 2` — genauer als eine
   Textsuche, weil eine Überschrift, deren Wortlaut auch im Fließtext vorkommt,
   nicht in die Irre führt. Guard:
   `test_help_window_lists_every_section_and_jumps_to_it`.
3. **Typografie über den HTML-Umweg**: ein Formatvorlagenblatt wirkt nur auf HTML,
   nicht auf importiertes Markdown — deshalb `setMarkdown` → `toHtml` → `setHtml`
   mit gesetztem `defaultStyleSheet` (Schriftgrade, Tabellenabstände).
4. **Das Handbuch darf nicht von der Oberfläche wegdriften.** Deshalb prüfen zwei
   Tests die Tabelle „Tastenkürzel" **in beide Richtungen** gegen die wirklich
   verdrahteten `QAction`s: `test_every_shortcut_in_the_manual_really_exists` und
   `test_every_shortcut_of_the_window_is_in_the_manual`. Ein neues Kürzel ohne
   Eintrag im Handbuch fällt damit sofort auf — und ein im Handbuch versprochenes,
   das es nicht gibt, ebenso. Das ist der eigentliche Grund, die Hilfe im Baum zu
   halten statt auf einer Webseite.

### 20.8 Arbeitsverzeichnisse — dieselben wie beim Emulator (2026-08-15)

Alle Dateidialoge des DiskTool gingen mit leerem Startpfad auf. Für Qt heißt das
*Arbeitsverzeichnis*, und das ist beim installierten Programm der
**Installationsordner**: dort suchte der Anwender seine Abbilder, dorthin wollte
das Werkzeug seine Dateien schreiben. Der Emulator macht es längst richtig
(`app/ui/drive_widget.py` → `paths.default_disk_dir()`); das DiskTool importierte
`app.paths` überhaupt nicht.

Beide Programme lösen jetzt über **dieselbe** Stelle auf und zeigen damit auf
dieselben Ordner:

| Dialog | Startpunkt |
|--------|-----------|
| Abbild öffnen · Neue Diskette · Bootabbild auswählen | `paths.default_disk_dir()` — identisch zum Laufwerksfeld des Emulators |
| Speichern unter… | neben der **geöffneten** Diskette; ohne Diskette der Diskettenordner |
| Archivieren · Bootabbild sichern | unverändert neben der geöffneten Diskette (Vorschlag mit Namen) |
| Ordner wählen · Zielordner wählen | der bereits gewählte Ordner, sonst `paths.default_folder_dir()` |

**Der Dateiordner ist neu** (`paths.user_files_dir()` → `<Datenordner>/Dateien`).
Er ist das Gegenstück zu `Disketten` für die Ordnerseite und liegt aus demselben
Grund außerhalb der Installation: dort wird geschrieben, und ein Update darf das
nicht überbügeln. Die Trennung ist inhaltlich — hier einzelne Dateien und die
Beiblätter, dort Abbilder ganzer Datenträger; der Kommentar an `DISKS_DIRNAME`
sah den Platz „daneben" von Anfang an vor.

Vier Festlegungen:

1. **`K1520_DISKS` verschiebt den Dateiordner NICHT.** Es ist die Angabe für die
   *Abbilder*; wer beides verschieben will, setzt `K1520_DATA`. Guard:
   `test_env_disks_verschiebt_den_dateiordner_nicht`.
2. **Angelegt wird nur in einer Installation.** `ensure_user_files_dir()` ist das
   Gegenstück zu `seed_user_disks()` und trägt dieselbe Bedingung: im Quellbaum
   geschieht nichts — dort soll kein Ordner im Heimatverzeichnis entstehen, bloß
   weil jemand das Werkzeug einmal gestartet hat. Beides ruft
   `app/disktool/main.py` beim Start auf, wie `app/main.py` es für den Emulator
   tut. `--purge` des Installers räumt es mit ab, weil es `$DATADIR` als Ganzes
   löscht. Guard: `test_dateiordner_entsteht_nur_in_einer_installation`.
3. **`default_folder_dir()` zeigt nie in die Installation** — gibt es den
   Dateiordner noch nicht, wird nach *oben* ausgewichen (Datenordner →
   Dokumentenordner → Heimatverzeichnis), niemals ins Programm. Bei den
   **Abbildern** gibt es dagegen eine gewollte Ausnahme: passt kein
   Benutzerordner, bietet `default_disk_dir()` die *mitgelieferten* Beispiele an,
   und die liegen naturgemäß im Programmordner — der Emulator macht es ebenso.
   Guards: `test_startordner_weicht_nach_oben_aus_statt_in_die_installation`,
   `test_folder_side_never_starts_in_the_installation`.
4. **Kein Dialog ohne Startpunkt.** Das ist der eigentliche Wächter
   (`test_every_file_dialog_gets_a_start_directory`): er ruft jeden Dateidialog
   einmal auf und prüft, dass ein Pfad übergeben wurde. Ein leerer Startpfad ist
   genau der Fehler, um den es hier ging — und er fällt beim Ansehen nicht auf,
   weil im Quellbaum das Arbeitsverzeichnis zufällig der richtige Ort ist.

`python3 app/disktool/main.py --paths` gibt die ganze Auflösung aus (wie beim
Emulator, und wie dort **vor** den Qt-Importen — die Auskunft muss auch dann
kommen, wenn genau das fehlt, wonach gefragt wird). `describe()` nennt seitdem
beide Arbeitsverzeichnisse; das ist die erste Frage, wenn ein Dialog am falschen
Ort aufgeht.

---

## 21. UDOS1715 / NDOS — die zweite UDOS-Ausprägung (2026-08-17)

Das Werkzeug liest und schreibt seit dem 2026-08-17 auch die Disketten des
**PC 1715**. Sie tragen dasselbe Betriebssystem (UDOS), aber ein anderes
Dateisystem: **NDOS** statt ZDOS. Vollständige Spezifikation:
**`doc/udos1715_diskettenformat.md`**; die maßgebliche Quelle ist das
Systemhandbuch, das auf der Diskette selbst liegt
(`doc/original_docs/UDOS1715_Systemhandbuch.txt`, extrahiert als Datei
`UDOS.TEXT`).

### 21.1 Warum es überhaupt ein zweites Dateisystem gibt

Der PC 1715 hat einen **µPD765**-Floppycontroller. Der liest und schreibt ganze
IBM-Sektoren und kommt an die vier Bytes hinter der Daten-CRC nicht heran, in
denen ZDOS auf dem A5120 seine Dateiverkettung führt. NDOS legt die Verkettung
deshalb in **eigene Sektoren** — Zeigersektoren mit je bis zu 125 Adressen,
untereinander verkettet, adressiert über `FIRSTBL` im Descriptor.

Daraus folgt alles Weitere:

| | UDOS/ZDOS (A5120) | UDOS1715/NDOS (PC 1715) |
|---|---|---|
| Sektor | 128 B + 4 B hinter der CRC | **256 B, reines Standard-IBM** |
| Verkettung | Kontrollblock im Gap | **Zeigersektoren** |
| `.img` | unmöglich | **möglich und richtig** |
| Seiten | zwei getrennte Datenträger (`Side0`/`Side1`) | **eine Diskette, eine Spur = ein Zylinder** |
| Namen | beliebiges druckbares Zeichen | müssen mit einem **Buchstaben** beginnen |

### 21.2 Was geteilt wird und was nicht

Geteilt (eine Umsetzung, kein Doppel):

* **Descriptor und Verzeichniseintrag.** Die ersten 128 Byte des Descriptors sind
  bitgleich; Typbyte, Eigenschaftsbyte, Datumsfelder, Segment, LOW/HIGH/STACK
  liegen an denselben Offsets. `UdosFileHeader`, `UdosPointer` und `UdosDirEntry`
  sind deshalb gemeinsam, ebenso `udosTypeByte`/`udosTypeName`/
  `udosPropertyByte`/`udosPropertyLetters` (`udos_fs.h`). Dabei fiel auf, dass das
  Typbyte ein **Bitfeld aus Typ und Subtyp** ist — das erklärt nebenbei das
  ZDOS-„P1" (`81H` = P, Subtyp 1), das vorher als Sonderwert in einer Tabelle stand.
* **Belegungskarte.** Die Feldoffsets sind identisch (Name 0…23, Einträge ab 24,
  Zähler bei 375/378/379/380). `UdosBitmap` bekam deshalb eine
  @ref UdosMapSitte statt einer zweiten Klasse. Drei Unterschiede stecken darin:
  80 statt 78 Spureinträge, `00`-Füllung statt des ZDOS-Nachlaufs
  `11×33H · F7H · 27×77H`, und — der einzige inhaltliche — **der „belegt"-Zähler
  ist bei NDOS echt**, nicht `2464 − frei`.

Nicht geteilt: die Klasse selbst. `Udos1715FileSystem` ist eine eigene
`FileSystem`-Umsetzung, weil Kettenauswertung, Belegung und Adressumrechnung
nichts gemeinsam haben. Ein gemeinsamer Rumpf mit Fallunterscheidungen wäre
länger als beide Umsetzungen zusammen.

### 21.3 Die eine Stelle, an der die Adressen umgerechnet werden

> „Für die Arbeit mit UDOS werden die Sektornummern intern nach der Beziehung
> **(Sektornummer − 1) + Kopfnummer · 16** gebildet." (Handbuch §2.6.2)

Eine „Spur" ist der ganze **Zylinder**, 32 Sektoren über beide Seiten. Das
geschieht in `headOf()`/`idOf()` — zwei Zeilen, durch die jeder Zugriff geht. Wer
sie umgeht, bekommt eine Diskette, deren zweite Hälfte fehlt.

Zwei Folgen, die man nicht aufweichen darf:

1. **`sides_separate` gibt es hier nicht.** Der Katalogparser setzt es für
   `type: udos1715` hart auf `false` und meldet es, wenn jemand es setzt. Ein
   Volume, ein Ordner, keine `SideN/`.
2. **Ein Record darf die KOPFgrenze überschreiten**, die Spurgrenze nicht. Auf
   dem Referenzdatenträger tut das z. B. `CAT` (1024-B-Record ab Sektor 0DH über
   10H hinweg). Eine Belegung, die auf `Recordlänge/256` ausrichtet, findet solche
   Dateien nicht wieder.

### 21.4 Erkennung: ein leeres CP/M sieht aus wie eine leere UDOS1715-Diskette

`cpa640` und `k5601_16x256` sind **dieselbe** Rohgeometrie unter zwei
Katalognamen. Eine frisch angelegte UDOS1715-Diskette ist außerhalb ihrer beiden
Systemspuren voller `0xE5` — und damit zugleich ein völlig plausibles *leeres*
CP/M. Bis dahin gewann in Stufe 2 einfach die zuerst gemessene Geometrie, also
`cpa640`/`scpx640`.

Die Auflösung: `detect_rank` gilt jetzt **über Geometriegrenzen hinweg**. Die
Schleife sucht über die erste treffende Geometrie hinaus weiter, solange eine
spätere ein Profil mit *kleinerem* Rang anbieten kann. Bei lauter gleichen Rängen
— dem Normalfall — ist das Verhalten unverändert; nur die drei
`udos1715`-Profile stehen mit `detect_rank: -10` davor. Die Begründung ist
inhaltlich: ein positiv nachgewiesener Datenträger (Belegungsplan mit passenden
Zählern **und** ein Verzeichnis-Descriptor an der festen Stelle) schlägt ein
Verzeichnis, das bloß nicht widerspricht.

### 21.5 Systemdiskette: Spur 0 muss beim Anlegen gesperrt werden

Handbuch §1.2.2: auf einer Systemdiskette liegen Urlader und BFOS auf Spur 0
Sektor 00…0F (also Kopf 0). Das Katalogprofil beschreibt die **Anwender**diskette;
wird beim Anlegen ein Bootabbild mitgegeben, leitet `DiskVolume::create` ein
Profil mit `system_track0: true` ab, bevor `mkfs()` läuft. Ohne das vergäbe die
erste geschriebene Datei genau die Sektoren, die den Urlader tragen.

### 21.6 Wächter

| Test | Was er festhält |
|---|---|
| `Udos1715Belegung.KarteUndDateienStimmenSektorgenau` | **der schärfste**: die Belegung aus allen 67 Dateien (Descriptoren, Zeigersektoren, Datenrecords) gegen den Belegungsplan, in beide Richtungen ohne Rest |
| `Udos1715.VerzeichnisLiegtWoDasHandbuchEsSagt` | die 13 festen Sektoren aus §1.2.1, wörtlich |
| `Udos1715.ErsteEintragungDesErstenZeigersektorsIstDerDescriptor` | die Kettenregel aus §3.2.3, für alle 67 Dateien |
| `Udos1715.MehrAls124RecordsBrauchenZweiZeigersektoren` | 206 Records = 124 + 82 (`UDOS.TEXT`) |
| `Udos1715.RecordUeberDieKopfgrenzeWirdRichtigZusammengesetzt` | die Spur ist der Zylinder |
| `Udos1715.HandbuchLaesstSichLesen` | der Lesepfad holt genau das Dokument zurück, aus dem die Sollwerte stammen |
| `Udos1715.EineZdosDisketteWirdNichtVerwechselt` | Gegenprobe gegen die A5120-Diskette |
| `Udos1715Schreiben.*` | Anlegen, Rundlauf, Verzeichniswachstum, Recordlängen, Namensregel, Systemspuren |
| `FsCatalog.Udos1715ProfileSindImgFaehigUndEinseitigGezaehlt` | `.img` erlaubt, `sides_separate` false, 256-B-Geometrie |

Fixture: `tests/fixtures/disks/udos1715_640k_pc1715_system.img` (640 KB — das
`.img` genügt, weil bei NDOS nichts außerhalb der Sektoren steht); die
spurbasierte Aufnahme derselben Diskette liegt unter
`disks/udos1715_640k_pc1715_system.hfe`.

Am echten Laufwerk gegengeprüft (Greaseweazle F1 + MFS-1.6-Diskette): der
Physisch-Pfad des DiskTool öffnet die eingelegte Diskette, erkennt `udos1715` und
listet alle 67 Dateien
(`K1520_GW_HARDWARE=1 … -k disktool_oeffnet`).

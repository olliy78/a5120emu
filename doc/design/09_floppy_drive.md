# Feinentwurf: Diskettenabbild — internes Medium + Container-Codecs

**Modul:** `core/peripherals/floppy_drive/`
**Dateien:** `track_image.*`, `track_codec.*`, `bit_codec.*`, `track_view.*`, `disk_medium.*`,
`image_codec.*`, `img_codec.*`, `hfe_codec.*`, `dmk_codec.*`, `disk_image.*`, `track_sync.*`,
`floppy_drive2.*`, `drive_profile.*`, `disk_format.*`, `format_catalog.*`
**Verwandt:** `doc/design/07_k5122_afs.md` (Controller), `doc/design/13_k1520disktool.md`
(zweites Programm auf derselben Schicht), `doc/design/14_physische_diskette.md`
(echte Diskette am Greaseweazle), `doc/K1520_architecture.md` §8.5/§8.7/§8.8

> **Historie.** Dieses Dokument beschrieb ursprünglich die monolithische `FloppyDrive`
> (Inline-Sektor-IO über `.img`, Klasse längst entfallen), danach die
> `DiskImage`-Backend-Schicht (je Dateiformat eine Unterklasse, die *auf der Datei
> arbeitete*).  Seit dem **Medium-Umbau (2026-08-05)** gibt es nur noch **ein
> internes Diskettenabbild**; die Dateiformate sind reine **Container-Codecs**
> davor.  Der frühere Aufbau ist in §9 als Abgrenzung dokumentiert.
>
> **Seither zweierlei dazugekommen:** das **k1520DiskTool** benutzt dieselbe Schicht
> ohne Emulator und hat sie um eine **Schreibseite** erweitert (§11); die **physische
> Diskette** am Greaseweazle kommt als zweite Art von Bindung neben die Datei (§12).

---

## 1. Aufgabe und Leitidee

Der Emulator hält eine gemountete Diskette **vollständig als internes,
bitstrom-orientiertes Abbild im Speicher** (`DiskMedium`).  Eine Image-Datei ist
nur noch **Ein-/Ausgabeformat**:

```
Datei (.img | .hfe | .dmk)  ──load──►  DiskMedium (intern, bitstrom-orientiert)  ──►  K5122
                            ◄─save──                    ▲
                                                        └── Schreibzugriffe des Gastsystems
```

Daraus folgt alles Weitere:

| Anforderung | Umsetzung |
|-------------|-----------|
| Einheitliche interne Darstellung | `DiskMedium` = Feld von `TrackImage` (Vollumdrehungs-Byte-/Markenstrom, FM **und** MFM) |
| Datei folgt dem Abbild | Autosave: schmutzige Spuren werden verzögert in die **gebundene** Datei zurückgeschrieben |
| Format nachträglich wechseln | `saveAs(pfad, container)` schreibt das Medium neu **und bindet um** — ab da geht der Autosave in die neue Datei |
| `.dmk` zusätzlich | dritter Container-Codec, self-describing wie `.hfe` |
| Echte Leerdiskette | `DiskMedium` mit **unformatierten** Spuren (keine Marken) — Geometrie aus dem **DriveProfile** |
| Leerdiskette ist nicht `.img`-fähig | Flag `rawCompatible()`; Speichern als `.img` scheitert mit Fehlermeldung, GUI bietet `.img` gar nicht erst an |
| Formatname nur für `.img` | `.hfe`/`.dmk` sind self-describing; `DiskFormat` wird **ausschließlich** vom `.img`-Codec verlangt |

**Warum „bitstrom-orientiert“, obwohl `TrackImage` Bytes hält:** `TrackImage` ist der
*decodierte* Vollumdrehungsstrom — Gaps, Sync, Adressmarken (als `marks[]`, also aus dem
fehlenden Clock-Bit, nicht aus dem Bytewert), Datenfelder, **echte CRCs** und alles, was
sonst noch auf der Spur steht.  Er ist damit verlustfrei in FM-/MFM-Bitzellen
rückcodierbar (`BitCodec`) und trägt insbesondere die Bytes, die **kein** Sektorimage
kennt: den UDOS-Sektorkontrollblock hinter der Daten-CRC, fremde Gap-Inhalte,
CRC-Fehler, unformatierte Spuren.  Eine Bitzellen-Speicherung wäre gleichwertig, aber
teurer (jede Leseanforderung müsste decodieren) und würde die vom Controller ohnehin
benötigte Markeninformation wegwerfen.

---

## 2. Schichtung

```
K5122 (Controller)                    DiskVolume / SectorSpace (DiskTool)
   │  fordert TrackImage(cyl, head)      │  medium().track(cyl,head), Sektor-IO
   │  an, streamt es byteweise           │                      core/filesystem/
   ▼                                     │
FloppyDriveV2 — physisches Laufwerk: DriveProfile + Kopfposition   floppy_drive2.*
   │  track(head) / mutableTrack(head) / writeTrackAt(cyl,head,t)  │
   ▼                                                               ▼
DiskImage — gemountete Diskette: Medium + Bindung + Autosave       disk_image.*
   ├── DiskMedium — DAS interne Abbild (alle Spuren, Zustände)     disk_medium.*
   ├── ImageCodec — Container laden/speichern (Fabrik + Sniffing)  image_codec.*
   │     ├── ImgCodec  (.img, braucht DiskFormat)                  img_codec.*
   │     ├── HfeCodec  (.hfe, HFE v1, self-describing)             hfe_codec.*
   │     └── DmkCodec  (.dmk, David Keil, self-describing)         dmk_codec.*
   └── TrackSync — Bindung an eine ECHTE Diskette (§12)            track_sync.*
   ▼
TrackImage / TrackCodec / BitCodec — Spurstrom, IBM-Synthese, Bitzellen
TrackView — dieselbe Spur als zeichenbare Abschnitte (Diskeditor)  track_view.*
DriveProfile[4] / DiskFormat / FormatCatalog — Laufwerke bzw. Sektorlayout
```

Die **Namensgebung**: `DiskMedium` ist der Datenträger, `DiskImage` die *gemountete*
Diskette (Medium **plus** Bindung, Schreibschutz, Fehlertext).  `ImageCodec` ist
die Container-Schicht.  Es gibt **keine** Datei-Backend-Unterklassen mehr.

**Die Schicht trägt zwei Programme.**  Der Emulator kommt von oben links (K5122), das
k1520DiskTool von oben rechts (`DiskVolume`) — und zwar **ohne Z80 und ohne Karten**:
`libk1520disk.so` übersetzt diese Schicht plus `core/filesystem/` allein
(`doc/design/13_k1520disktool.md`).  Das ist der Grund, warum unterhalb von `DiskImage`
nichts über die Maschine wissen darf: kein Maschinentakt in einer Signatur, keine
`A5120Machine`, kein `Logger`-Aufruf, der eine Karte voraussetzt.  Die einzige Ausnahme
ist die Maschinenuhr des Autosave — und sie steht als Parameter in `autoFlush(now)`,
nicht als Abhängigkeit.

---

## 3. `DiskMedium` — das interne Abbild

```cpp
class DiskMedium {
public:
    DiskMedium() = default;
    DiskMedium(uint8_t num_cyls, uint8_t num_heads, Encoding default_enc);

    void resize(uint8_t num_cyls, uint8_t num_heads);   ///< Spuren bleiben erhalten

    uint8_t  numCylinders() const;
    uint8_t  numHeads()     const;
    Encoding defaultEncoding() const;                   ///< vorherrschendes Verfahren
    void     setDefaultEncoding(Encoding e);
    DiskGeometry geometry() const;

    const TrackImage& track(uint8_t cyl, uint8_t head) const;    ///< lädt ggf. nach (§12)
    const TrackImage& peek (uint8_t cyl, uint8_t head) const;    ///< NIE nachladen (§12.3)
    TrackImage&       mutableTrack(uint8_t cyl, uint8_t head);   ///< markiert dirty
    void              setTrack(uint8_t cyl, uint8_t head, TrackImage t);
    void              markDirty(uint8_t cyl, uint8_t head);

    bool     dirty() const;                   ///< irgendeine Spur seit dem letzten Save geändert
    bool     trackDirty(uint8_t cyl, uint8_t head) const;
    void     clearDirty();
    uint64_t revision() const;                ///< +1 je Spuränderung (Autosave-Ruhepause, §6.1)

    TrackState state(uint8_t cyl, uint8_t head) const;   ///< Unknown | Clean | Dirty (§12.2)
    bool       complete() const;              ///< keine Spur mehr unbekannt

    bool        formatted()     const;        ///< mind. eine Spur trägt Adressmarken
    bool        rawCompatible() const;        ///< als .img darstellbar (§5)
    bool        trackRawCompatible(uint8_t cyl, uint8_t head) const;
    std::string rawIncompatibleReason() const;   ///< "" oder z. B. "Spur 12/1" (Fehlertext)
};
```

* **Speicherung:** `std::vector<TrackImage>`, Index `cyl * num_heads + head`.
  80×2 Spuren × ~6,3 KB ≈ 1 MB je Laufwerk — unkritisch.
* **Dirty-Bits pro Spur**, damit der Autosave nur geänderte Spuren neu codiert.
* **`raw_ok_`-Cache pro Spur** (tri-state), wird bei jeder Änderung dieser Spur
  invalidiert und bei Bedarf neu bestimmt (§5).
* **Nicht existierende Spuren** (jenseits der Geometrie) liefern ein leeres
  `TrackImage`; **unformatierte** Spuren sind ebenfalls leer (`bytes.empty()`).
  Der Controller macht daraus gap-Flux ohne Marken (§7).

### 3.1 Leerdiskette — Geometrie kommt vom Laufwerk

Eine neue, leere Diskette hat **kein** Sektorlayout, also auch kein `DiskFormat`.
Ihre Geometrie ist die **physische Erreichbarkeit des Laufwerks**, das sie aufnimmt:

| DriveProfile | Zylinder × Köpfe | Default-Verfahren des Mediums |
|--------------|------------------|-------------------------------|
| `K5601`      | 80 × 2           | MFM |
| `K5600.10`   | 40 × 1           | MFM |
| `K5600.20`   | 80 × 1           | MFM |
| `MF3200`     | 77 × 1           | FM  |
| `MF6400`     | 77 × 1           | MFM |

`DiskMedium::defaultEncoding()` ist nur ein **Vorschlagswert** für Container-Header und
für die Geometrieanzeige; das tatsächliche Verfahren steht **pro Spur** in
`TrackImage::encoding` und wird vom Formatiervorgang des Gastsystems gesetzt.  Ein
Medium darf FM- und MFM-Spuren mischen (8″-System-34, CP/A-Mischdichte).

---

## 4. Container-Codecs (`ImageCodec`)

```cpp
enum class ContainerType { Img, Hfe, Dmk };

namespace ImageCodec {
    /// Endung → Typ (ohne Datei-Zugriff); Default Img.
    ContainerType fromExtension(const std::string& path);
    /// Signatur der Datei → Typ; fällt auf die Endung zurück.
    ContainerType detect(const std::string& path);

    const char* name(ContainerType);                 ///< "img" | "hfe" | "dmk"
    const char* extension(ContainerType);            ///< ".img" | ".hfe" | ".dmk"
    bool        selfDescribing(ContainerType);       ///< Hfe/Dmk = true
    bool        needsDiskFormat(ContainerType);      ///< nur Img = true

    bool load(const std::string& path, ContainerType,
              const DiskFormat* fmt, DiskMedium& out, std::string& err);
    bool save(const std::string& path, ContainerType,
              const DiskFormat* fmt, const DiskMedium& in, std::string& err);
}
```

**Vertrag:** `load()` füllt das Medium **vollständig** (alle Spuren, Geometrie,
Default-Verfahren), `save()` schreibt die Datei **komplett neu** aus dem Medium.
Kein Codec hält einen Dateizustand über den Aufruf hinaus — genau das erlaubt den
Formatwechsel per `saveAs()`.

### 4.1 `.img` — rohes Sektorimage (`ImgCodec`)

* **Nicht** self-describing → `DiskFormat` ist Pflicht (Geometrie, Sektorzahl/-größe,
  Sektor-IDs, Verfahren pro Spurbereich, Spur-Reihenfolge im File).
* `load`: je Spur die Sektor-Nutzdaten am errechneten Offset lesen und über
  `TrackCodec::buildTrack` eine normgerechte IBM-Spur (Marken, Gaps, echte CRCs)
  **synthetisieren**.
* `save`: je Spur `TrackCodec::parseTrack`, Nutzdaten an ihre Offsets schreiben.
  **Alles außerhalb der Datenfelder geht verloren** — deshalb §5.
* Spur-Reihenfolge im File: verschränkt `(0,0) (0,1) (1,0) (1,1) …`.

### 4.2 `.hfe` — HXC Floppy Emulator v1 (`HfeCodec`)

* Self-describing: Signatur `HXCPICFE`, Zylinder-/Seitenzahl, Verfahren, Bitrate, RPM
  stehen im 512-B-Header; Track-LUT ab Block 1; Spurdaten als seitenverschränkte
  256-B-Blöcke, Zellen **LSB-first**.
* `load`: LUT → Seitenbytes de-interleaven → `BitCodec::decode`.  Bringt das
  Header-Verfahren keine Marken, wird **das andere Verfahren probiert** (Mischdichte).
  Überabgetastete Aufnahmen (Bitrate ≥ 375 kbit/s, typ. Greaseweazle 500) werden über
  `BitCodec::downsampleCells` auf die Nominalrate quantisiert.
* `save`: Header + LUT werden **neu berechnet** — die Spurlänge ergibt sich aus der
  längsten Spur des Mediums.  Damit kann ein aus `.img` geladenes oder frisch
  formatiertes Medium ohne Vorlage als `.hfe` geschrieben werden.
  Unformatierte Spuren werden als Gap-Zellen (`0x88`) abgelegt.

### 4.3 `.dmk` — David Keil's Disk Image (`DmkCodec`) — **neu**

DMK speichert je Spur den **rohen Byte-Strom** plus eine Tabelle der
Adressmarken-Positionen — also fast exakt unser `TrackImage`.

```
Datei-Header (16 B)
  0      Schreibschutz: 0xFF = geschützt, 0x00 = frei
  1      Anzahl Zylinder
  2..3   Spurlänge in Bytes (little-endian), INKLUSIVE der 128-B-IDAM-Tabelle
  4      Optionen:  Bit4 = 1 → einseitig
                    Bit6 = 1 → reine SD-Diskette (FM-Bytes NICHT verdoppelt)
                    Bit7 = 1 → Dichte-Flags ignorieren
  5..11  reserviert (0)
  12..15 0x00000000 = Datei-Image (0x12345678 = echter Laufwerkszugriff, n.u.)

Spuren in der Reihenfolge (0,0) (0,1) (1,0) (1,1) …, je Spur:
  128 B  IDAM-Tabelle: 64 × u16 little-endian
           Bit15 = 1 → Sektor in MFM (Double Density), 0 → FM
           Bit14 = reserviert
           Bit0..13 = Offset des 0xFE-Bytes, gezählt AB SPURANFANG (also ≥ 0x80)
           0x0000 = unbenutzter Eintrag; Einträge aufsteigend sortiert
  Rest   Spur-Bytes, wie vom Datenseparator gelesen.
         Bei FM-Spuren ist jedes Byte VERDOPPELT (außer Header-Bit6 ist gesetzt).
```

* `load`: IDAM-Tabelle → `marks[MarkType::Id]`.  Die zugehörige Datenmarke wird
  hinter jeder IDAM gesucht (nächstes `F8..FB`, im MFM hinter der `A1 A1 A1`-Sync);
  IAM (`FC`) wird erkannt, wenn vorhanden.  FM-Verdopplung wird beim Laden entfernt.
  Spuren ohne IDAM-Einträge sind **unformatiert** → leeres `TrackImage`.
* `save`: Spurlänge = `128 + max(Spurbytes)`, aufgerundet auf 256; Verdopplung für
  FM-Spuren; `Bit4` aus `numHeads()`, `Bit6` gesetzt, wenn **alle** Spuren FM sind
  und dann ohne Verdopplung geschrieben wird.
* Damit ist DMK — wie HFE — verlustfrei für Gap-Inhalte, Sektorkontrollblöcke,
  Mischdichte und unformatierte Spuren.

### 4.4 Erkennung beim Öffnen

| Signatur / Endung | Container |
|-------------------|-----------|
| `HXCPICFE`        | HFE v1    |
| `HXCHFEV3`        | abgelehnt (nicht implementiert) |
| Endung `.dmk` **und** plausibler DMK-Header | DMK |
| sonst             | Img (verlangt `DiskFormat`) |

DMK hat keine Magic-Zahl; erkannt wird an der Endung **plus** Plausibilitätsprüfung
(Byte 0 ∈ {0x00, 0xFF}, `1 ≤ n_tracks ≤ 96`, `0x80 < track_len ≤ 0x4000`,
Dateigröße passt zu `16 + n_tracks * sides * track_len`).

---

## 5. `.img`-Tauglichkeit (`rawCompatible`)

Ein rohes Sektorimage kann **nur Datenfeld-Nutzbytes** speichern.  Sobald auf dem
Medium etwas steht, das dabei verloren ginge, darf nicht als `.img` gespeichert werden.

Eine **Spur** ist `.img`-tauglich, wenn sie

1. mindestens einen Sektor enthält (`TrackCodec::parseTrack` liefert ≥ 1 Sektor),
2. bei allen Sektoren **ID- und Daten-CRC gültig** sind,
3. und hinter jeder Daten-CRC **nur Gap-Füllbytes** stehen
   (`0x4E`, `0xFF`, `0x00`; geprüft über `LogicalSector::tail`).

Punkt 3 ist der eigentliche Auslöser: **UDOS** schreibt je Sektor einen
Sektorkontrollblock (Rückwärts-/Vorwärtszeiger + eigene CRC) direkt hinter die
Daten-CRC.  Genau dieser Anhang macht ein UDOS-Dateisystem in `.img` unmöglich
(vgl. `doc/udos_diskettenformat.md`) — und war der Grund, warum eine leere `.img`
unter UDOS nicht formatierbar war.

Das **Medium** ist `.img`-tauglich, wenn es überhaupt formatiert ist (`formatted()`)
**und jede nicht-leere Spur** tauglich ist.  Leere Spuren sind erlaubt — sie werden im
`.img` zu Füllbytes, und viele echte Images tragen ein bis drei leere Zusatzspuren am
Ende.  Eine **komplett** unformatierte Diskette ist nie `.img`-fähig: ein rohes
Sektorimage kann „nicht formatiert“ nicht ausdrücken.

> **Konsequenz für die Bedienung:** Das Flag entsteht **beim Schreiben** und ist damit
> das vom Auftrag geforderte „Flag, das gesetzt wird, sobald ein Sektor geschrieben
> wird, der nicht in ein `.img`-Image überführt werden kann“.  Es wird nicht
> zurückgesetzt, solange die betreffende Spur so bleibt — ein anschließendes
> Neuformatieren im IBM-Layout macht die Spur wieder tauglich.

Beim Speichern als `.img` kommt eine **zweite** Prüfung dazu: das Medium muss zum
gewählten `DiskFormat` passen (Sektorzahl, -größe und -IDs je Spurbereich).  Schlägt
sie fehl, nennt die Fehlermeldung die erste abweichende Spur.

---

## 6. `DiskImage` — gemountete Diskette (Medium + Dateibindung)

```cpp
class DiskImage {
public:
    // ── Fabriken ───────────────────────────────────────────────────────────
    /// Vorhandene Datei laden und binden (Container per Sniffing).
    static std::unique_ptr<DiskImage> open(const std::string& path,
                                           std::optional<DiskFormat> fmt,
                                           bool write_protect);
    /// LEERE, unformatierte Diskette in Laufwerksgeometrie — ohne Dateibindung.
    static std::unique_ptr<DiskImage> createBlank(uint8_t num_cyls, uint8_t num_heads,
                                                  Encoding default_enc);
    /// Vorformatierte Leerdiskette (echte IDAM/DATA/CRC, Nutzdaten 0xE5) + Datei anlegen.
    static std::unique_ptr<DiskImage> create(const std::string& path,
                                             std::optional<DiskFormat> fmt,
                                             bool write_protect,
                                             Encoding enc = Encoding::MFM);

    // ── Medium ─────────────────────────────────────────────────────────────
    DiskMedium&       medium();
    const DiskMedium& medium() const;
    DiskGeometry geometry() const;
    bool writable()      const;      ///< Medium beschreibbar (kein WP, keine Nur-Lese-Quelle)
    bool rawCompatible() const;      ///< → medium().rawCompatible()

    const TrackImage& readTrack(uint8_t cyl, uint8_t head) const;
    bool  writeTrack(uint8_t cyl, uint8_t head, const TrackImage& t);

    // ── Dateibindung ───────────────────────────────────────────────────────
    bool                 hasFile()   const;
    const std::string&   path()      const;
    ContainerType        container() const;
    const DiskFormat*    diskFormat() const;   ///< nur bei .img gesetzt

    /// Schmutzige Spuren in die gebundene Datei schreiben (No-op ohne Bindung/dirty).
    bool flush();
    /// Verzögerter Autosave: flush(), wenn seit der letzten Änderung genug Zeit verging.
    bool autoFlush(uint64_t now_cycles);

    /// Unter neuem Namen/Container speichern und **umbinden**.
    /// @p fmt nur für .img nötig; bei .img zusätzlich rawCompatible()-Prüfung.
    bool saveAs(const std::string& path, std::optional<DiskFormat> fmt);
    /// Wie saveAs, aber **ohne umzubinden** und ohne den Dirty-Zustand anzutasten —
    /// für Exporte, die den Arbeitsstand nicht bewegen sollen (Archiv, Formatansicht).
    bool exportTo(const std::string& path, std::optional<DiskFormat> fmt);

    /// false, wenn die Quelle nicht zurückgeschrieben werden darf (§6.2).
    bool bindingWritable() const;

    const char* lastError() const;
};
```

### 6.1 Autosave

`writeTrack()` markiert die Spur im Medium schmutzig und erhöht dessen
`revision()`-Zähler.  `A5120Machine::run()` ruft alle `kDiskFlushCheckInterval`
(100 000) Takte `autoFlush(total_cycles_)` für alle Laufwerke.  Geschrieben wird erst
nach einer **Schreibpause**: solange sich die Revision zwischen zwei Prüfungen ändert,
wird die Uhr neu gestellt; erst wenn `kAutoFlushDelayCycles` (≈ 0,5 s Maschinenzeit)
lang **nichts mehr** geschrieben wurde, geht die ganze Datei einmal hinaus.

* Warum auf die Pause und nicht auf eine feste Frist: ein Formatier- oder Kopierlauf
  schreibt hunderte Spuren am Stück (UDOS `FORMAT` + `MOVE`: ~2 · 10⁹ Takte).  Mit einer
  festen Frist wäre die komplette Image-Datei dabei dutzende Male neu codiert worden;
  mit der Pausenerkennung genau einmal, kurz nachdem das Gastsystem fertig ist.
* Der Zeitversatz bleibt derselbe: die Datei entspricht dem Abbild **kurz nach** dem
  letzten Zugriff.
* `unmount()`, `saveAs()`, der Destruktor und `A5120Machine::reset()/powerOn()`
  flushen **sofort**.
* Ohne Dateibindung (Leerdiskette, noch nie gespeichert) ist der Autosave ein
  No-op — das Medium lebt bis zum ersten `saveAs()` nur im Speicher.

### 6.2 Nur-Lese-Quellen

Ein überabgetastetes HFE (Flux-Mitschnitt einer echten Diskette) wird beim Laden auf
die Nominalrate quantisiert.  Ein Rückschreiben in die Originalrate gibt es nicht,
also wird die **Bindung** als nur-lesend markiert: Schreibzugriffe des Gastsystems
landen im Medium, der Autosave schweigt, `saveAs()` in eine neue Datei funktioniert
normal.  Ebenso bei einer schreibgeschützten Datei.

---

## 7. Unformatierte Spuren im Lesepfad

Eine leere Spur (`TrackImage::empty()`) ist auf echter Hardware reiner Gap-Flux ohne
Adressmarken.  `K5122::startReadTransfer()` streamt genau das
(`kUnformattedTrackBytes` × `0x4E`, keine Marken): die Leseroutine findet kein IDAM
und terminiert über den Index-Timeout („record not found“) — statt in der
ZVE2-Lese-Koroutine zu verklemmen.  **Das ist die Voraussetzung dafür, dass eine echte
Leerdiskette überhaupt gemountet und dann vom Gastsystem formatiert werden kann.**

Konsequenz für das Mounten: die frühere Ablehnung markenloser Images
(`hasFormattedData()`) **entfällt** — eine unformatierte Diskette ist ab jetzt ein
gültiger, gewollter Zustand.

### 7.1 /TRACK 00 gehört zum Laufwerk, nicht zur Diskette

`/TO` (Steuer-PIO Tor B, Bit 7) ist laut K5122-Handbuch §4.1 ein **Eingang vom
Laufwerk** — ein mechanischer Endlagenschalter.  Er gilt daher, sobald der Slot
bestückt ist (`DriveProfile::present`), unabhängig von einer eingelegten Diskette;
ebenso fährt der Schrittmotor ohne Diskette.  Nur `/RDYL` (und `/WP`) hängen am
Medium.

Das ist die Grenze, an der Software „Slot unbestückt" von „Laufwerk ohne Diskette"
unterscheidet: der Lade-ROM fährt bei `/TO=1` nach außen und prüft erneut (0x0110)
und wartet erst danach — mit Software-Timeout — auf Index-Pulse.  Solange `/TO` an
die Diskette gekoppelt war, blieb ein beim Kaltstart leeres Laufwerk für solche
Suchläufe unsichtbar; UDOS strich es dauerhaft aus seiner Gerätetabelle, sodass eine
später eingelegte Diskette bis zum Kaltstart unbenutzbar blieb (`FORMAT` → `ERROR C2`).
Guard: `UdosFormat.FormatsDisketteInsertedAtRuntime`.

### 7.2 Spurdichte — Diskette und Laufwerk müssen nicht zusammenpassen

5,25″ kennt zwei Spurdichten: **48 tpi** (40 Spuren über den ganzen Radius) und
**96 tpi** (80 Spuren).  Welche eine Diskette trägt, verrät ihre Spurzahl; welche das
Laufwerk abfährt, steht im `DriveProfile`.  Stimmen sie nicht überein, ist das **kein
Grund zur Ablehnung**, sondern ein Übersetzungsverhältnis zwischen Kopfposition und
Diskettenspur — genau das, was ein 96-tpi-Laufwerk an einer 40-Spur-Diskette seit jeher
tut.  Dasselbe gilt für die Seitenzahl.

| Diskette | Laufwerk | `TrackPitch` | Kopfposition → Spur | Hinweis |
|----------|----------|--------------|---------------------|---------|
| 40 Spuren | 80 Zyl. (K5601, K5600.20) | `DoubleStep` | 2n → n, dazwischen nichts | „Double Step aktiviert" |
| 80 Spuren | 40 Zyl. (K5600.10) | `HalfStep` | n → 2n | „Laufwerk liest nur jede zweite Spur" |
| zweiseitig | einseitig | (`side0Only`) | Kopf 1 existiert nicht | „Nur Seite 0 verwendbar" |

Beide Achsen sind unabhängig und treten auch gemeinsam auf.  Die Zuordnung geschieht
**einmal beim Mount** aus der Mediengeometrie; ausgerechnet wird sie an **einer**
Stelle, `FloppyDriveV2::mediumCylinder()`, durch die jeder Spurzugriff läuft
(`track`, `mutableTrack`, `markTrackDirty`, `writeTrackAt` — letzteres bekommt vom
Vollspur-FORMAT ebenfalls eine *Kopfposition*).

Vier Festlegungen, die dabei tragen:

* **Die Spurzahl entscheidet, nicht das Katalogformat.** 48 tpi heißt 35–45 Zylinder
  (die Toleranz nach oben fängt die leeren Gap-Spuren vieler HFE-Abbilder ab, die
  Untergrenze verhindert, dass ein Bruchstück-Abbild zur 48-tpi-Diskette erklärt wird);
  96 tpi heißt ab 70.  8″ hat nur eine Dichte und bleibt unberührt.
* **Dass nur die äußere Hälfte einer Diskette beschrieben ist, gibt es nicht** — so
  beschreibt kein Laufwerk eine Diskette.  Ein 40-Zylinder-Abbild ist deshalb immer
  eine 48-tpi-Diskette.  Wer die (durch eine Fehlkonfiguration im Gast durchaus
  erzeugbare) halb beschriebene 96-tpi-Diskette abbilden will, braucht ein
  **80-Zylinder**-Abbild mit unformatierter Innenhälfte — `.hfe`/`.dmk` können das,
  `.img` nicht.
* **Schreibzugriffe ohne Ziel werden verworfen** (mit Warnung im Log), nicht auf die
  Nachbarspur umgeleitet: unter einer ungeraden Kopfposition liegt bei `DoubleStep`
  keine Spur, und Seite 1 hat in einem einseitigen Laufwerk keinen Kopf.  Ein richtig
  eingestellter Gast schreibt nie dorthin; ein falsch eingestellter scheitert an
  seinem eigenen Vergleichs-Lesen — wie am echten Laufwerk, aber ohne die Diskette
  zu beschädigen.
* **Abgewiesen wird weiterhin**, was das Laufwerk wirklich nicht kann: ein Verfahren
  außerhalb seines Könnens und Spuren mit Daten jenseits seiner Reichweite (die sich
  mit der Übersetzung mitbewegt — schrittverdoppelt sind es 40 Diskettenspuren, halb
  geschrittet 80).

Der entscheidende Wächter ist
`FloppyDriveV2.Doppelschritt_IstDieselbeDisketteWieEinDoppelschrittAbbild`: dieselbe
physische Diskette einmal als 80-Zylinder-Abbild mit formatierten geraden Zylindern
(`step: 2` im Katalog) und einmal als 40-Zylinder-Abbild muss unter dem Lesekopf Byte
für Byte dasselbe liefern — der Gast darf die beiden Darstellungen nicht unterscheiden
können.  Am laufenden CP/A nachgeprüft: mit Geometrie U (40 Spuren Doppelschritt)
formatiert FORMAT.COM ein 40-Zylinder-Abbild im K5601 vollständig und liest es
fehlerfrei zurück.

---

## 8. Bedienschnittstelle

### 8.1 Maschinen-API (`A5120Machine`)

```cpp
bool mountDisk (int drive, const std::string& path,
                const std::string& format_name, bool wp);
/// format_name LEER  → echte Leerdiskette (unformatiert, Geometrie aus dem DriveProfile);
///                     Ziel muss .hfe/.dmk sein (bzw. gar keine Datei).
/// format_name GESETZT → vorformatierte Diskette nach Katalogformat (auch .img).
bool createDisk(int drive, const std::string& path,
                const std::string& format_name, bool wp);
/// Speichert die gemountete Diskette unter neuem Namen/Container und bindet um.
/// format_name wird NUR bei Ziel .img gebraucht (und dort geprüft).
bool saveDiskAs(int drive, const std::string& path, const std::string& format_name);
bool isDiskRawCompatible(int drive) const;   ///< darf als .img gespeichert werden
std::string diskPath(int drive) const;       ///< aktuell gebundene Datei ("" = nur im Speicher)
/// Wie die Diskette ans Laufwerk angepasst werden musste (§7.2); je Einschränkung
/// eine Zeile, "" = sie passt.  KEIN Fehler — sie ist gemountet und lesbar.
std::string diskNotice(int drive) const;
```

### 8.2 C-API

```c
bool  k1520_save_disk_as     (K1520Handle, int drive, const char* path, const char* format_name);
bool  k1520_disk_raw_compatible(K1520Handle, int drive);
int   k1520_disk_path        (K1520Handle, int drive, char* buf, int buf_len);
const char* k1520_disk_notice(K1520Handle, int drive);   /* Anpassungs-Hinweise, §7.2 */
```

`k1520_create_disk(h, drive, path, format_name, wp)` behält seine Signatur; der
leere/`NULL`-Formatname bedeutet jetzt **Leerdiskette** statt „Standardformat des
Laufwerks“.

### 8.3 GUI

* Dateifilter für Öffnen/Speichern: `*.img *.hfe *.dmk`.
* **Neu anlegen** erzeugt standardmäßig eine Leerdiskette (`.hfe`); das
  Format-Auswahlfeld ist dabei **deaktiviert**.
* **Speichern unter…** blendet `.img` aus, solange `k1520_disk_raw_compatible()`
  falsch ist, und verlangt nur bei `.img` einen Formatnamen.
* Die **Anpassungs-Hinweise** aus §7.2 stehen im Laufwerkskasten unter dem Dateinamen
  (je Einschränkung eine Zeile, Erläuterung als Tooltip) — kein Meldungsfenster, denn
  die Diskette ist gemountet und benutzbar.

---

## 9. Abgrenzung zum vorigen Aufbau

| vorher | jetzt |
|--------|-------|
| `DiskImage` abstrakt, je Dateiformat eine Unterklasse (`RawSectorImage`, `HfeImage`) | `DiskImage` konkret = `DiskMedium` + Bindung; Dateiformate sind Codecs |
| Backend arbeitete **auf der Datei** (Raw: seek/write je Sektor; HFE: In-place-Blöcke) | Alles läuft im **Medium**; Datei wird komplett neu geschrieben |
| Spur-Cache (1 Spur je Kopf) in `FloppyDriveV2`, Rückschreiben bei Spurwechsel | kein Cache mehr — `FloppyDriveV2` referenziert das Medium direkt |
| Formatwechsel unmöglich (Backend an Dateityp gebunden) | `saveAs()` in jeden Container |
| `.hfe`-Neuanlage musste **vorformatiert** sein (gap-leere Datei ließ den Controller hängen) | echte Leerdiskette möglich; der Hänger ist im Controller behoben (§7) |
| `DiskImage::open` lehnte markenlose Images ab | markenlose (= unformatierte) Medien sind gültig |
| `.img`-Verlust von Gap-Anhängen fiel nicht auf | `rawCompatible()` verhindert ihn |

---

## 10. Testbarkeit

| Test (ctest-Suite) | Inhalt |
|--------------------|--------|
| `DiskMedium.*` (`test_disk_medium`) | Geometrie/Resize, Dirty-Bits + `revision()`, `formatted()`, `rawCompatible()` inkl. UDOS-Anhang, CRC-Fehler, Leerdiskette, Cache-Invalidierung |
| `ImgCodec.*` / `DiskImageOpen.*` / `DiskImageCreate.*` (`test_img_codec`) | `.img` ⇄ Medium, Offset-/Interleave-Modell als Ground-Truth, Mischdichte, `first_sector_id`, Ablehnung nicht darstellbarer Medien |
| `HfeCodec.*` (`test_hfe_codec`) | `.hfe` ⇄ Medium, Cross-Check gegen `.img`, Neuanlage ohne Vorlage, Leerdiskette, Mischdichte-Erkennung, Überabtastung |
| `DmkCodec.*` (`test_dmk_codec`) | `.dmk`-Header, IDAM-Tabelle, FM-Verdopplung (mit und ohne SD-Flag), Round-Trip inkl. Gap-Anhang und unformatierter Spur, Erkennung |
| `DiskImageBlank/SaveAs/AutoFlush.*` (`test_disk_image`) | Leerdiskette ohne Datei, Autosave-Verzögerung + Destruktor, `saveAs()` durch alle drei Container, `.img`-Ablehnung |
| `FloppyDriveV2.*` (`test_floppy_drive2`) | Kopfposition, Medium-Referenz, `writeTrackAt`, Geometrie-/Verfahrensprüfung beim Mounten, Leerdiskette in jedem Laufwerk |
| `A5120DiskApi.*` (`test_a5120_disk_api`) | `createDisk` (leer vs. vorformatiert), `saveDiskAs`, `isDiskRawCompatible` |
| `CreateDiskBlank/Formatted.*`, `BootIntegrationCpa02.DmkBootsIntoRunningCpaOs` | Maschinen-API + **CP/A bootet von einem `.dmk`-Konvertat** |
| `UdosFormat.FormatsBrandNewBlankDiskette` | **Der Zweck des Umbaus:** frische Leerdiskette unter UDOS formatieren, `STATUS` bestätigt 1988 freie Sektoren, Abbild bleibt `.img`-untauglich |
| `UdosFormat.BuildsBootableSystemDiskAndBootsFromIt` | Vollkette auf einer **emulator-erzeugten Leerdiskette als `.dmk`**: beidseitig formatieren (Laufwerk 1 + 5), `MOVE`, `CAT`, dann **Kaltstart von genau dieser `.dmk`** |
| `test_k5122`, `test_boot_integration` | Regression: Boot-Pfad, FORMAT-Schreibpfad, unformatierte Spur |
| `TrackView.*`, `TrackCodecWriteSectorAt.*` (`test_track_view`, `test_track_codec`) | Schreibseite und Ansicht der Spur (§11) — lückenlose Abschnittsfolge, Sektor anlegen/löschen/schreiben, CRC wörtlich |
| `TrackSync.*` (`test_track_sync`), `DiskMedium.Unbekannt*` | Spurzustände, Prioritäten, Blockade, Rückführung (§12) — mit **Ersatz-Arbeitsfaden**, ohne Hardware |

> **Anmerkung zur Nummerierung:** §11 und §12 sind später angehängt, damit die in
> `CLAUDE.md` und den Nachbardokumenten zitierten Nummern (§5, §6.1, §7.2 …) stehen
> bleiben.  Inhaltlich gehört §11 zu §4/§6 und §12 zu §3/§6.

---

## 11. Was das DiskTool auf dieser Schicht ergänzt hat — 2026-08-13

Das k1520DiskTool (`doc/design/13_k1520disktool.md`) benutzt `DiskMedium` und
`TrackCodec` **ohne Emulator**.  Dabei kam heraus, dass die Schicht bis dahin fast nur
lesen und ganze Spuren schreiben konnte.  Ergänzt wurden drei Dinge; sie gehören
hierher, weil der Emulator sie mitträgt.

### 11.1 Schreiben in eine vorhandene Spur

`buildTrack()` taugt zum Ändern **nicht**: es baut die Spur neu und verlöre alles hinter
der Daten-CRC — bei UDOS die gesamte Dateiverkettung.  Es gibt daher eine Schreibseite,
die an Ort und Stelle arbeitet:

| Funktion | Zweck |
|----------|-------|
| `writeSector(track, id, daten)` | Datenfeld ersetzen, CRC neu rechnen — über die **Sektor-ID** |
| `writeSectorAt(track, index, daten, crc_woertlich?)` | dasselbe über die **laufende Nummer**; IDs dürfen doppelt vorkommen. Die CRC ist optional **wörtlich** setzbar, sonst ließe sich eine schadhafte Diskette nicht originalgetreu nachbilden |
| `writeSectorTail(track, index, tail)` | die Bytes **hinter** der Daten-CRC (UDOS-Sektorkontrollblock) — fasst Nutzdaten und CRC nicht an |
| `sectorDataCrc(track, index, …)` | gespeicherte **und** gerechnete CRC nebeneinander |
| `createSector` / `eraseSectorAt` / `newSectorPosition` / `newSectorLength` | Sektoren anlegen und löschen; **die ID bestimmt die Lage** (hinter den vorhandenen mit der nächstkleineren ID). Die Spurlänge bleibt fest — angelegt wird in den Gap hinein |

`parseTrack()` liefert seither zusätzlich **Byte-Offsets, gespeicherte CRCs und
`deleted`**; ohne die Offsets könnte ein Editor nicht sagen, *wo* etwas steht.
Die Festlegungen im Einzelnen (warum die ID die Lage bestimmt, warum ein später
angelegter Sektor einen vorhandenen überschreiben darf) stehen in
`doc/design/13_k1520disktool.md` §19.4.

### 11.2 `TrackView` — dieselbe Spur zum Ansehen

`track_view.{h,cpp}` (`scanTrack`) zerlegt eine `TrackImage` in eine **lückenlose**
Folge von Abschnitten über `[0,1)` einer Umdrehung: Sektor, Gap, unformatiert.  Der
Winkel eines Bytes ist `Position ÷ Spurlänge` — eine `TrackImage` **ist** eine
Umdrehung, Bitrate und Drehzahl skalieren nur die Zeitachse und werden nicht gebraucht.
Zwei Unterscheidungen tragen das: **Gap ≠ unformatiert** (keine einzige Adressmarke =
unformatiert), und `sync_pos` zeigt auf den **Anfang der Sync-Gruppe**, nicht auf die
Adressmarke — sonst weichen Anzeige und `newSectorPosition` um die Sync-Länge ab.

### 11.3 Das Medium als Transaktionsgegenstand

Stapeloperationen des DiskTools sind Transaktionen: erst planen und urteilen, dann
schreiben, bei einem Fehler alles zurück.  Die Rücknahme ist eine **Kopie des ganzen
`DiskMedium`** (`const DiskMedium sicherung = disk_->medium();`, ~1 MB) — billiger als
jede Buchführung über Einzeländerungen und immun gegen vergessene Fälle.

> **Bei einer physischen Diskette (§12) genügt das Zurückkopieren nicht:** was schon auf
> der echten Diskette steht, holt keine Kopie zurück.  Zurückgesetzte Spuren müssen
> daher **erneut als geändert** gelten, damit die Rückführung sie richtigstellt — dafür
> gibt es `restoreFrom(snapshot)` statt einer bloßen Zuweisung.

---

## 12. Physische Diskette als zweite Bindung — 2026-08-15

**Voller Entwurf: `doc/design/14_physische_diskette.md`.**  Hier nur, was am *Medium*
anders wird; alles über Greaseweazle, Arbeitsfaden, C-ABI und Python steht dort.

Bisher hat eine gemountete Diskette genau eine Bindung: eine **Datei**, die beim Öffnen
vollständig gelesen und beim Autosave vollständig geschrieben wird.  Daneben tritt die
**echte Diskette in einem echten Laufwerk**, und die verhält sich in einem Punkt
grundsätzlich anders: sie wird **spurweise nach Bedarf** gelesen, nicht am Stück.

### 12.1 Der Zwischenschritt über eine Datei entfällt

```
bisher:  echte Diskette ──(2 min)──► .hfe ──► DiskMedium ──► arbeiten ──► .hfe ──(2 min)──► Diskette
jetzt:   echte Diskette ◄──spurweise, nach Bedarf──► DiskMedium ──► arbeiten
```

### 12.2 Je Spur ein Zustand

Das Dirty-Bit wird zu einem Dreizustand:

| Zustand | Bedeutung | Inhalt gültig? |
|---------|-----------|----------------|
| `Unknown` | noch nie von der Diskette gelesen | **nein** |
| `Clean` | gelesen, seither nicht geändert | ja |
| `Dirty` | im Abbild geändert, noch nicht zurückgeschrieben | ja, neuer als die Diskette |

**Ein Konzept, nicht zwei:** der Zustand gilt für *jedes* Medium.  Bei einer
dateigebundenen Diskette tritt `Unknown` nur nie auf — der Codec füllt beim Laden
alles —, und `Dirty` ist genau das Bit, mit dem der Autosave (§6.1) seit jeher
arbeitet.  Der Unterschied, an dem alles hängt: **`loadTrack` (gelesen) macht sauber,
`setTrack` (geschrieben) macht schmutzig**.  Wächter: `DiskMedium.ZustandGiltAuchOhne\
Laufwerk_UnknownKommtDortNieVor`, `…EinzelneSpurSauberMelden_LaesstDieAnderenSchmutzig`,
`…GeleseneSpurIstSauber_GeschriebeneSchmutzig`.

**`Unknown` ist nicht dasselbe wie „unformatiert“.**  Unformatiert (§7) ist eine
belegte Aussage über die Diskette; unbekannt ist gar keine.  Deshalb melden
`formatted()` und `rawCompatible()` zusätzlich `complete()` — sonst erklärte sich eine
halb gelesene Diskette für unformatiert, und ein `.img` schriebe die ungelesenen Spuren
als Füllbytes fest.

### 12.3 Nachgeladen wird in `track()` — und nur dort

`DiskMedium::track()` ist die einzige Stelle, durch die **jeder** Verbraucher geht
(Controller über `FloppyDriveV2`, DiskTool über `DiskVolume`, Erkennung über
`GeometryProbe`).  Dort sitzt das Nachladen; es blockiert den rufenden Faden, bis die
Spur da ist (≈ 0,5–0,8 s).

Die medienweiten Reihenläufe dürfen das **nicht** auslösen — `formatted()`,
`rawCompatible()`, die Codecs und jede Zustandsanzeige benutzen `peek()`.  Sonst zöge
eine beiläufige Statusabfrage der Oberfläche die ganze Diskette ein.
`mutableTrack()` lädt nach (Sektorschreiben ist Lesen-Ändern-Schreiben), `setTrack()`
nicht (Vollspur-FORMAT ersetzt die Spur ohnehin) — daran hängt, dass eine Leerdiskette
im echten Laufwerk formatiert werden kann, ohne vorher gelesen zu werden.

### 12.4 Wer die Spuren holt

`TrackSync` hält die Aufträge, hat aber **keinen eigenen Faden**: ein fremder
Arbeitsfaden (Python + Greaseweazle) holt sie sich ab.  Drei Prioritäten —
**1** Lesen auf Anforderung (jemand wartet), **2** geänderte Spuren zurückschreiben,
**3** unbekannte Spuren vorausschauend lesen.  Der Kern kennt dabei weder USB noch
Greaseweazle; ausgetauscht werden **HFE-Bitzellen**, die durch denselben
`BitCodec::decode` laufen wie eine HFE-Datei.

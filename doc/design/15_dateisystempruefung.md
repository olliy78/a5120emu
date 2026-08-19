# Dateisystemprüfung, Reparatur und Wiederherstellung (`fsck`)

> **Stand:** 2026-08-19 · **Etappen 1–7 umgesetzt** (Modell, CP/M-, ZDOS- und
> NDOS-Prüfung, Automatik beim Öffnen, `check --full`, Anzeige; Reparatur in Kern,
> C-ABI, CLI und Oberfläche samt Sprung in den Diskeditor; **Ebene 0** — warum wurde
> nichts erkannt; **Rettung gelöschter Dateien** für CP/M, ZDOS und NDOS in Kern,
> C-ABI, CLI und Oberfläche).  Offen ist nur noch die Gegenprobe der Alternativprofile
> aus Etappe 7 — s. §19 und §20
> **Gehört zu:** `doc/design/13_k1520disktool.md` (das Werkzeug), `doc/udos_diskettenformat.md`,
> `doc/udos1715_diskettenformat.md`, `doc/design/14_physische_diskette.md`,
> `doc/design/09_floppy_drive.md`
> **Betrifft:** `core/filesystem/`, `core/api/k1520_disk_api.*`, `tools/k1520disktool.cpp`,
> `app/disktool/`

---

## 0. Wo weitermachen

Damit eine neue Sitzung nicht das ganze Dokument lesen muss, hier der Stand in
Kürze. Die Begründungen dahinter stehen in den genannten Abschnitten.

**Steht** (Etappen 1–4, §19):

| | |
|---|---|
| Modell | `core/filesystem/check/fs_check.{h,cpp}` — `FsFinding` · `FsSeverity` · `FsLayer` · `FsRepair` · `FsCheckReport` |
| Prüfer | `cpm_check.cpp` (CP/A, SCPX, SCP1700) · `udos_check.cpp` (ZDOS) · `udos1715_check.cpp` (NDOS) |
| Haken | `FileSystem::check(level, nachladen)` und `FileSystem::repair(const FsRepair&)` — beide überall umgesetzt |
| Bündelung | `DiskVolume::check()` / `checkReport()`, Automatik am Ende von `oeffnenMit`; `DiskVolume::applyRepairs()` = Rangfolge + Transaktion + Nachprüfung (§12) |
| C-ABI | `k1520d_check` · `k1520d_check_complete` · `k1520d_check_tracks_*` · `k1520d_check_summary` · `k1520d_finding_*` · `k1520d_repair_*` · `k1520d_apply_repairs` (der frühere Textbericht heißt jetzt `k1520d_check_report`) |
| CLI | `check [--full] [--json]` · `fsck [--full] [--repair[=alle\|sicher\|<kennung>,…]] [--dry-run] [--json]` |
| Oberfläche | Statuszeile · Meldungsstreifen (mit Rangfolge) · Protokoll · Diskettenangaben inkl. Schaltfläche *Vollprüfung* · **Reparaturdialog** `ui/fsck_dialog.py` (Aktion `act_reparieren`, Strg+F) mit Sprung in den Diskeditor (`DiskEditorWindow.zeige_ort`) |
| Ebene 0 | `DiskVolume::merkeAblehnung` sammelt (Kandidat, Grund), `DiskVolume::ebene0` macht Befunde daraus (`erkennung.abgelehnt`, `erkennung.ohne_kandidat`, Ebene `FsLayer::Erkennung`); `check`/`fsck` öffnen roh, das Protokoll der Oberfläche führt die Gründe auf, der Prüfdialog geht auch ohne Dateisystem (§11) |
| Rettung | `core/filesystem/check/fs_recover.{h,cpp}` (Modell + die geteilten Helfer `fsRecoverEinordnung`/`fsRecoverFuellmuster`/`fsRecoverBelege`) · `check/cpm_recover.cpp` (Suche, Lesen, Wiedereintragen) · `check/udos_recover.cpp` und `check/udos1715_recover.cpp` (Signatursuche, Kettenverfolgung, Rohbereiche — **nur lesend**, §13.3a) · `FileSystem::recoverScan/recoverRead/recoverRestore/recoverEntry` · `DiskVolume::recoverScan/recoverRead/recoverExtract/recoverRestore` (bei der UDOS-Familie schreibt `recoverExtract` das Beiblatt `udos-dateiangaben.txt`) · `k1520d_recover_*` · `recover` in der CLI · `ui/recover_dialog.py` (*Diskette ▸ Gelöschte Dateien suchen…*) |
| Tests | `tests/unit/filesystem/test_fs_check.cpp` (47, davon 12 `FsCheckReparatur.*`) · `test_fs_recover.cpp` (19: 11 CP/M, 7 UDOS, 1 NDOS) · `cli_dt_check_*` (6) · `cli_dt_fsck_*` (4) · `cli_dt_recover_*` (5) · `py_disk_c_api` · `py_disktool_gui` (16 Prüf-/Reparatur-/Ebene-0-/Rettungsfälle) |

**Fehlt noch.** Die Etappen 1–7 sind abgeschlossen; offen ist allein die Gegenprobe der
Alternativprofile aus Etappe 7 — mit Grund, s. §20.

**Etappe 5 — Rettung CP/M ist fertig** (2026-08-19).  Sie ist die einzige Familie, bei
der auch **auf der Diskette** wiederhergestellt wird — dort ist es ein Byte, und der
Name stimmt (§13.3a).  Der Suchlauf findet
zweierlei: gelöschte **Verzeichnisplätze** (Nutzerbyte `0xE5`, Rest unversehrt — Name,
`RC` und alle Blockzeiger stehen noch da) und, nur in der Oberflächensuche, **freie
Blöcke mit Inhalt** als Rohbereiche ohne Namen.  Fünf Dinge, die man wissen will:

* **Zwei Suchtiefen, und die billige fasst keine Datenspur an** (bei UDOS geht das
  nicht — §20).
  `FsRecoverLevel::Verzeichnis` liest nur den Verzeichnisbereich — auch die
  Prüfsummenkontrolle der Fundblöcke läuft erst in der Oberflächensuche, sonst zöge
  eine „billige" Suche an einer physischen Diskette die ganze Scheibe ein.
* **Verloren geht bei CP/M genau eines: der Nutzerbereich.**  Er stand in ebendem
  Byte, das `0xE5` geworden ist; wiederhergestellt wird nach Bereich 0.
* **Zusammengefasst wird über den NAMEN**, nicht über (Nutzer, Name) — der
  Nutzerbereich ist ja fort.  Zwei nacheinander gelöschte Dateien gleichen Namens
  werden dadurch ein Fund; auseinanderhalten ließen sie sich ohnehin nicht.
* **`restorable` ist nicht „rettbar".**  Herausholen lässt sich jeder Fund, auch
  schreibgeschützt (E7).  Auf der Diskette eintragen nur, wenn kein Block inzwischen
  einer lebenden Datei gehört und kein gleichnamiger Eintrag existiert — sonst
  entstünde genau der `cpm.block.doppelt`, den die Prüfung als **Gefahr** meldet.
  Nachgeprüft wird unmittelbar vor dem Schreiben noch einmal.
* **Zwei Abweichungen von diesem Entwurf**, beide bewusst: Rohbereiche heißen
  `fragment_c12h0_b40-b47.bin` (Ort **und** Blockspanne — die Sektorspanne aus §13.3
  wäre falsch, sobald ein Lauf über eine Spurgrenze geht), und ein Fund mit Vorbehalt
  bekommt sein Beiblatt als `<datei>.rettung.txt` **neben** der geretteten Datei.
* Der Befund **`cpm.medium.frei_beschrieben`** (§8-Tabelle) bleibt **unvergeben**: die
  freien Blöcke mit Inhalt sind kein Schaden, sondern ein Fund — sie erscheinen in der
  Suche, nicht in der Prüfung.

**Etappe 6 — Rettung UDOS/NDOS ist fertig** (2026-08-19, §13.1/§13.3a).  Sie fiel
**rein lesend** aus: gefunden, angezeigt und in den Linux-Ordner geschrieben wird, auf
die Diskette zurückgeschrieben nicht.  Sechs Dinge, die man wissen will:

* **Verloren ist allein der Name.**  `erase` schneidet den Verzeichniseintrag heraus
  und löscht die Kartenbits; der Kopfsektor bleibt mit Typ, Eigenschaften, ENTRY,
  Satzlänge, Blocklänge, allen Speichersegmenten, LOW/HIGH/STACK und beiden
  Datumsvermerken stehen.  Deshalb ist der Weg zurück *retten → benennen → `put`*, und
  das Beiblatt `udos-dateiangaben.txt` trägt die Angaben hinüber (§13.3a).
* **Der Name im Dialog ist ein Vorschlag, nie eine Tatsache.**  `FsRecoverFind::name`
  bleibt bei UDOS **immer leer**; `vorschlag` ist ein Namensrest aus dem Verzeichnis
  (wenn einer überlebt hat) oder `GERETTET.001`.
* **Die Systemspuren werden nicht übersprungen** — `NOTE.TO.SD` der Referenzdiskette
  hat ihren Kopfsektor auf Spur 21.  Ausgeschlossen wird nur, was die Karte im
  Bootbereich als belegt führt (§13.1, Kasten).  Das war der einzige echte Fehlgriff
  beim Bauen, und er wäre still geblieben.
* **Bei NDOS trägt die Bytesignatur nicht** — die `FF 00`-Marken sind eine
  A5120-Sitte.  Getragen wird die Erkennung dort strukturell über `FIRSTBL` (§13.1).
* **Rohbereiche enden an der Spurgrenze**, sonst nennt der Dateiname eine Sektornummer
  auf einer anderen Spur.
* **Beide Suchtiefen fassen bei UDOS die Datenspuren an** — anders als bei CP/M steht
  nach dem Löschen nichts Gesuchtes mehr im Verzeichnis (§20).

**Etappe 7 — Ebene 0 ist fertig** (§11, 2026-08-19). Aus „nichts erkannt" ist eine
Befundliste geworden: jeder geprüfte Kandidat mit seinem Grund, als eigene Ebene
`FsLayer::Erkennung` und mit `Info` als Schwere. `check`/`fsck` öffnen dafür **roh**
(Rückgabewert 2), die Oberfläche schreibt die Gründe ins Protokoll und lässt den
Prüfdialog auch ohne Dateisystem zu. Offen geblieben ist allein die **Gegenprobe der
Alternativprofile** — mit Grund, s. §20.

**Kleiner Rest, keine eigene Etappe:**

* **Ebene Medium für UDOS breiter** — heute je Datei zusammengefasst; ein Reihenlauf
  über die freien Bereiche fehlt.
* **Vollprüfung an einer PHYSISCHEN Diskette** braucht einen Arbeitsfaden mit
  Fortschritt (ein bis zwei Minuten, 0,5–0,8 s je Spur).  Bis dahin ist der Knopf in
  **beiden** Dialogen gesperrt und sagt warum (`disk_info_dialog.py`,
  `fsck_dialog.py`) — ein Fenster, das zwei Minuten steht, sieht aus wie ein Absturz.

**Fünf Dinge, die beim Umsetzen anders kamen als hier ursprünglich entworfen** — sie
sind an Ort und Stelle korrigiert, aber leicht zu übersehen:

* Die **UDOS-Systemspuren 0–2 und die Bootspur werden nicht geprüft** (§9.1) — sie
  sind Sitte, nicht Struktur.
* Der **Rückwärtszeiger des ersten Satzes nennt den Kopfsektor**, nicht `FFFF` (§9.2).
* Der Schnitt zwischen Schnell- und Vollprüfung geht durch **Bytes, nicht Spuren**
  (§6), und die **Spurzähler** bedeuten „gewollt" und „davon verfügbar" (§5).
* Ein CP/M-Blockzeiger wird **nie mitten aus der Liste gestrichen, sondern ab dort
  abgeschnitten** (§12.1a).  Ein Loch verschöbe jeden folgenden Satz des Extents — die
  Datei lieferte danach *falsche* Daten statt weniger.
* „Möglich" und „ausgewählt" sind **zwei Zahlen**.  Die CLI nannte nur die zweite und
  meldete damit „0 Reparatur(en) waeren moeglich" über einer Zeile, die eine anbot
  (die einzige trug `Datenverlust`, und den nimmt `--repair` ohne `=alle` nicht).
  In `--json` heißen sie `repairable` und `selected`.

---

## 1. Aufgabe

Das k1520DiskTool öffnet heute eine Diskette, erkennt Geometrie und Dateisystem und
zeigt das Verzeichnis. Was es **nicht** sagt, ist, ob das, was es da zeigt, in sich
stimmt. Die Disketten, um die es geht, sind dreißig bis vierzig Jahre alt, kommen aus
fremden Rechnern, sind mit abgestürzten Systemen beschrieben und mehrfach umkopiert
worden. Drei Fragen bleiben unbeantwortet:

1. **Ist dieses Dateisystem heil?** Zeigen alle Verweise dorthin, wo sie sollen?
   Passen Belegung und Inhalt zusammen? Und vor allem: **Zerstört der nächste
   Schreibvorgang etwas?** — der gefährlichste Zustand ist nicht ein Fehler, den man
   sieht, sondern ein Sektor, der Daten trägt und im Belegungsplan als frei steht.
2. **Lässt sich das wieder geradeziehen?** Bei UDOS ist der Belegungsplan die einzige
   Wahrheit über den freien Platz — steht er falsch, ist die ganze Diskette in Gefahr,
   obwohl jede einzelne Datei noch lesbar ist. Genau das ist reparierbar, weil sich der
   Plan aus den Ketten **neu ausrechnen** lässt.
3. **Was war vorher da?** Beide Dateisystemfamilien löschen, indem sie einen Verweis
   entfernen — die Daten bleiben Byte für Byte liegen. Aus einer dreißig Jahre alten
   Diskette lässt sich damit oft mehr herausholen, als ihr Verzeichnis hergibt.

Dieser Entwurf beschreibt drei Dinge:

* eine **Prüfung**, die beim Öffnen automatisch läuft und den Befund sichtbar macht,
* eine **Reparatur** über einen eigenen Dialog (*Diskette ▸ Dateisystem reparieren…*),
* eine **Wiederherstellung** gelöschter Dateien und Dateibruchstücke über einen
  zweiten Dialog (*Diskette ▸ Gelöschte Dateien suchen…*).

**Abgrenzung.** Der Emulatorkern kennt keine Dateisysteme (er sieht Spuren und
Sektoren); die Prüfung ist deshalb ausschließlich eine Sache von `libk1520disk.so` und
des DiskTool. Umgekehrt ist die *Medienebene* — Schadstellen, CRC-Fehler,
unformatierte Spuren — bereits abgedeckt (`TrackSync`-Prüf-Lesen §7.1 des physischen
Entwurfs, Diskeditor §19 des Werkzeugentwurfs); sie wird hier nicht neu erfunden,
sondern **in den Bericht hereingeholt**, weil ein Anwender nicht zwischen „Sektor
defekt" und „Zeiger falsch" unterscheiden will, solange er nur wissen möchte, ob seine
Diskette in Ordnung ist.

---

## 2. Entscheidungen

| # | Entscheidung | Warum |
|---|---|---|
| **E1** | **Die Prüfung schreibt nie.** `FsCheck` ist durchgehend lesend; jede Änderung geht ausschließlich über eine ausdrücklich gewählte Reparatur. | Sonst wäre die Automatik beim Öffnen ein Schreibzugriff auf eine schreibgeschützt geöffnete Diskette — undenkbar bei einem Einzelstück im echten Laufwerk. |
| **E2** | **Zwei Tiefen: Schnellprüfung (automatisch) und Vollprüfung (auf Verlangen).** Die Schnellprüfung fasst nur die Verwaltungsstrukturen an, die das Mounten ohnehin gelesen hat. | An einer physischen Diskette kostet jede Spur 0,5–0,8 s. Eine Vollprüfung wären zwei Minuten — als Nebenwirkung des Öffnens inakzeptabel. Der Schnitt liegt genau dort, wo die Kosten anfangen. |
| **E3** | **Gefunden wird außerhalb, repariert wird innen.** `FsCheck` erzeugt Befunde und beschreibt Reparaturen; ausgeführt werden sie von `FileSystem::repair()` in der Klasse, der das Dateisystem gehört. | Die Klasse kennt ihre Invarianten und ihren Schreibpfad. Ein Prüfmodul mit `friend`-Zugriff auf drei Dateisysteme wäre die zweite Stelle, an der jede Regel steht. |
| **E4** | **Vier Schweregrade, und einer davon heißt `Gefahr`.** `Gefahr` = *der nächste Schreibvorgang zerstört Daten* (nicht: *Daten sind bereits verloren*). | Das ist die einzige Auskunft, die eine sofortige Handlung nach sich zieht: Schreibschutz drauflassen, Abbild sichern, dann reparieren. Sie geht in einer Liste von Warnungen unter, wenn sie nicht eigens benannt ist. |
| **E5** | **Befundkennungen sind ein stabiler Vertrag** (`"udos.karte.frei_aber_belegt"`), wie die Formatnamen. | CLI, `--json`, Oberfläche und Tests hängen daran; ein umbenannter Befund bricht Skripte still. Wächter wie `FormatCatalog.Formatnamen_SindEinStabilerVertrag`. |
| **E6** | **Reparaturen laufen als EINE Transaktion und in fester Rangfolge** (Verzeichnis → Ketten → Belegungsplan → Zähler), danach wird **automatisch neu geprüft**. | Reparaturen hängen voneinander ab: ein neu aufgebauter Belegungsplan leitet sich aus den Ketten ab, also müssen die Ketten vorher stimmen. Und ein Befundzettel von vorhin beschreibt nach der ersten Änderung eine Diskette, die es nicht mehr gibt. |
| **E7** | **Retten geht vor Reparieren.** Die Voreinstellung der Wiederherstellung ist *in den Linux-Ordner holen*; das Zurückschreiben in das Dateisystem ist der zweite, ausdrückliche Schritt. | Das Zurückholen belegt Sektoren und ändert Verzeichnis und Belegungsplan — auf einer Diskette, deren Zustand gerade erst als fragwürdig erkannt wurde. Herausholen kann nichts kaputtmachen. |
| **E8** | **Ein Belegungsplan wird nur neu aufgebaut, wenn die Ketten vollständig gelesen und fehlerfrei sind.** Sonst ist der Neuaufbau gesperrt, mit Begründung. | Aus einer halb gelesenen Diskette einen Plan zu erzeugen heißt, die ungelesene Hälfte als frei zu erklären — und beim nächsten Schreiben zu überschreiben. Das ist der Fehler, den ein `fsck` niemals machen darf. |
| **E9** | **Kein Befund ohne Ort.** Jeder Befund trägt, wo es ihn gibt, Zylinder/Kopf/Sektor und lässt sich per Doppelklick im **Diskeditor** öffnen. | Der Editor (13_k1520disktool.md §19) existiert bereits und kann genau das. Ein Befund, den man nicht ansehen kann, zwingt zum Vertrauen. |
| **E10** | **Falschmeldungen sind schlimmer als fehlende Meldungen.** Erster Wächter des Testsatzes: *jedes unversehrte Abbild im Baum und jedes anlegbare Katalogformat prüft ohne Befund.* | Ein `fsck`, das bei gesunden Disketten meckert, wird weggeklickt und ist damit wertlos — auch dann, wenn es später einmal recht hat. |

---

## 3. Einordnung in den Baum

`✔` = vorhanden, `·` = geplant.

```
core/filesystem/
├── check/
│   ├── ✔ fs_check.h          Modell: FsFinding · FsSeverity · FsRepair · FsCheckReport
│   ├── ✔ fs_check.cpp        Rahmen: ordnen, zählen, begrenzen, ausgeben
│   ├── ✔ cpm_check.cpp       CP/M-Prüfungen (deckt auch SCP1700/CP-M-86 ab)
│   ├── ✔ udos_check.cpp      UDOS/ZDOS (A5120)
│   ├── ✔ udos1715_check.cpp  UDOS1715/NDOS (PC 1715, P8000)
│   ├── · fs_recover.h/.cpp   Wiederherstellung: Modell + Rahmen + Fragmentsuche
│   ├── · cpm_recover.cpp     gelöschte Verzeichnisplätze, freie Blöcke
│   └── · udos_recover.cpp    Kopfsektorsuche + Kettenverfolgung (beide UDOS-Sitten)
├── ✔ file_system.h           + virtuelle Haken `check()` und `repair()` (letzterer leer)
└── ✔ disk_volume.{h,cpp}     + Automatik beim Öffnen, Bericht über alle Volumes

✔ core/api/k1520_disk_api.{h,cpp}  k1520d_check_* / k1520d_finding_*  (· repair_*/recover_*)
✔ tools/k1520disktool.cpp         `check [--full]`                   (· `fsck`, `recover`)
✔ app/core_binding/k1520disk.py   ctypes-Deklarationen (Driftwächter!)
  app/disktool/
  · main.py / physical_cli.py     dieselben Befehle für `--physical`
    ui/
    · actions.py                  act_reparieren · act_wiederherstellen
    · fsck_dialog.py              Befund + Reparaturauswahl
    · recover_dialog.py           Suchlauf + Vorschau + Retten
    ✔ disk_info_dialog.py         Prüfbericht als Abschnitt + Schaltfläche *Vollprüfung*
    ✔ main_window.py              Streifen, Statuszeile, Protokoll, Verzahnung
```

`data/formats.yaml` bleibt unangetastet — die Prüfung fügt der Beschreibung einer
Diskette nichts hinzu, sie liest sie nur schärfer.

---

## 4. Was der Bestand schon liefert

Es muss auffallend wenig neu gebaut werden; das meiste ist als *Innenansicht
(Diagnose, Tests)* schon da und wurde bisher nur von Tests benutzt:

| Vorhanden | Liefert der Prüfung |
|---|---|
| `CpmFileSystem::directory()` / `allocationMap()` / `directoryFill()` / `totalBlocks()` / `directoryBlocks()` | den kompletten CP/M-Verzeichnisbefund ohne eine Zeile neuen Lesecode |
| `UdosFileSystem::directory()` / `readHeader()` / `recordChain()` / `bitmap()` / `reservedTrack()` | Verzeichnis, Kopfsektoren, Ketten und Belegungsplan |
| `Udos1715FileSystem::pointerBlocks()` / `recordChain()` / `readDescriptor()` | dasselbe für die Zeigersektor-Sitte |
| `UdosBitmap::looksValid()` / `countFree()` / `countUsed()` / `storedFree()` / `storedUsed()` / `refreshCounters()` | Plausibilität und Zählerabgleich — samt fertiger Reparatur |
| `SectorSpace::readSector()` → `SectorData{id_crc_ok, data_crc_ok, tail}` | die Medienebene **je Sektor**, ohne den Umweg über `TrackView` |
| `SectorSpace::trackKnown()` | den Unterschied „nicht gelesen" ↔ „nicht vorhanden" — die Grundlage von E2/E8 |
| `SectorSpace::trackFormatted()` | unformatierte Spuren mitten im Dateisystembereich |
| `DiskVolume::trackView()` / `readSectorAt()` / `writeSectorAt()` | Ort eines Befundes und der Sprung in den Diskeditor |
| `FsInfo::warnings` | die heutigen zwei Warnungen; sie werden zu regulären Befunden und verschwinden aus `info()` |
| Transaktionsmuster aus `insertAll` (Momentaufnahme + Rücknahme, `restoreFrom` bei physisch) | die Reparaturtransaktion (E6) |
| `InfoBar.zeige(text, stufe, knopf=…, bei_klick=…)` | der Meldungsstreifen samt Knopf „Befund ansehen…" — ohne Umbau |

Neu zu ergänzen sind nur zwei Lesezugänge: `CpmFileSystem::directoryRaw()` (die
32-Byte-Plätze unzerlegt — ein Platz voller Müll lässt sich zerlegt nicht beurteilen)
und `UdosFileSystem::readSectorRaw()` (Nutzdaten **und** Nachspann eines beliebigen
Sektors, für die Kopfsektorsuche der Wiederherstellung).

---

## 5. Das Modell

```cpp
enum class FsSeverity : uint8_t {
    Info,      ///< bemerkenswert, nicht falsch (z. B. „7 gelöschte Plätze wiederherstellbar")
    Warnung,   ///< in sich widersprüchlich, aber ohne Folgen (Zähler, verlorener Platz)
    Fehler,    ///< etwas ist bereits unerreichbar oder falsch (Kettenbruch, wilder Zeiger)
    Gefahr     ///< der NÄCHSTE Schreibvorgang zerstört Daten (E4)
};

enum class FsLayer : uint8_t { Medium, Verwaltung, Dateien };   ///< §6

struct FsRepair {
    std::string kind;          ///< stabile Kennung, z. B. "udos.karte.sektoren.sperren"
    std::string text;          ///< was genau geschieht, im Klartext und mit Zahlen
    bool        datenverlust  = false;   ///< verwirft Nutzdaten oder einen Verweis darauf
    bool        empfohlen     = false;   ///< genau eine je Befund, wenn es eine gibt
    bool        geraten       = false;   ///< stellt einen Wert her, der nicht ableitbar ist
    /// Ausführungsparameter — nur die zuständige Dateisystemklasse deutet sie (E3).
    int         a = 0, b = 0, c = 0, d = 0;
    std::string s;
};

struct FsFinding {
    std::string id;            ///< stabile Kennung (E5), z. B. "cpm.block.doppelt"
    FsSeverity  severity = FsSeverity::Info;
    FsLayer     layer    = FsLayer::Verwaltung;
    int         volume   = 0;
    std::string object;        ///< "TEST.COM" · "Platz 37" · "c12h0 ID 5" · "Spur 23"
    std::string text;          ///< ein Satz, mit den konkreten Zahlen darin
    /// Ort auf der Diskette; -1 = ortlos (Sprung in den Diskeditor bleibt dann aus).
    int         cyl = -1, head = -1, sector_index = -1;
    std::vector<FsRepair> repairs;      ///< 0..n; leer = nur zur Kenntnis
};

enum class FsCheckLevel : uint8_t { Schnell, Voll };

struct FsCheckReport {
    FsCheckLevel level = FsCheckLevel::Schnell;
    /// false = es wurde NICHT alles angesehen (physische Diskette, ungelesene Spuren).
    /// Steuert E8 und gehört in jede Anzeige — „ohne Befund" heißt sonst zu viel.
    bool         vollstaendig = true;
    int          spuren_gelesen = 0, spuren_gesamt = 0;
    std::vector<FsFinding> findings;    ///< nach Schwere absteigend, dann nach Ort

    bool       ohneBefund() const;
    FsSeverity hoechste() const;
    int        zaehler(FsSeverity) const;      ///< genau diese Schwere
    int        zaehlerAb(FsSeverity) const;    ///< mindestens diese Schwere
    void       sortieren();                    ///< reproduzierbar (s. u.)
    void       uebernimm(const FsCheckReport&);
    void       begrenzen(size_t je_kennung);   ///< s. u.
    std::string alsText(bool mit_volume) const;
    std::string kurzfassung() const;           ///< „2 Gefahr, 1 Warnung"
};
```

Zwei Eigenschaften dieses Berichts sind nicht Kosmetik:

**Die Reihenfolge ist reproduzierbar** (`sortieren()`: Schwere ↓, Volume, Ebene, Ort,
Kennung). Zwei Läufe über dieselbe Diskette müssen dieselbe Liste liefern — sonst
taugt weder ein Textvergleich im Test noch die Auswahl im Reparaturdialog, die über
den Index geht.

**Gleichartige Befunde werden begrenzt** (`begrenzen()`, 20 je Kennung und Volume,
dann eine Sammelzeile „und 431 weitere dieser Art"). Eine wirklich kaputte Diskette
brächte sonst fünfhundert Zeilen hervor, und fünfhundert Zeilen liest niemand — der
Bericht wäre genau dann wertlos, wenn er am nötigsten ist. Die Zahl bleibt erhalten,
nur die einzelnen Orte fallen weg.

Anschluss an die vorhandene Fassade:

```cpp
class FileSystem {
    // …
    /// @brief Prüfen — ändert NICHTS (E1).  @p nachladen=false bleibt bei den
    ///        bereits bekannten Spuren und setzt `vollstaendig=false`.
    virtual FsCheckReport check(FsCheckLevel level, bool nachladen) const;
    /// @brief EINE Reparatur ausführen.  Die Klasse deutet `FsRepair::kind` (E3).
    virtual bool repair(const FsRepair& r);
};
```

Vorgabe beider Methoden: leerer Bericht bzw. `"Dieses Dateisystem kennt keine
Reparaturen"` — damit bleibt ein künftiges viertes Dateisystem lauffähig, bevor sein
Prüfer geschrieben ist.

`DiskVolume` bündelt über alle Volumes (bei ZDOS also über beide Seiten) und ergänzt
die Befunde, die ein einzelnes Volume gar nicht sehen kann (Ebene 0, §11):

```cpp
const FsCheckReport& checkReport() const;                 ///< der letzte Bericht
const FsCheckReport& check(FsCheckLevel, bool nachladen); ///< neu prüfen
int  applyRepairs(const std::vector<std::pair<int,int>>& auswahl);  ///< Transaktion (E6)
```

---

## 6. Drei Ebenen, zwei Tiefen

Ein Befund entsteht auf einer von drei Ebenen. Das ist keine Zierde, sondern legt
fest, **was er kostet** und **wer ihn beheben kann**:

| Ebene | Betrachtet | Quelle | Kosten |
|---|---|---|---|
| **Medium** | Adressmarken, ID-CRC, Daten-CRC, Nachspann, unformatierte Spuren im Dateisystembereich | `SectorSpace::readSector` / `trackFormatted` | eine Spur je Spur — teuer, physisch sehr teuer |
| **Verwaltung** | Verzeichnis, Belegungsplan, deren innere Stimmigkeit | Verzeichnis- und Kartenspuren | wenige Spuren, beim Mounten ohnehin gelesen |
| **Dateien** | Kopfsektoren, Ketten, Kreuzbelegung, Abgleich Karte ↔ Ketten | über die ganze Diskette verstreut | die ganze Diskette |

Daraus folgt der Schnitt aus **E2**:

**Schnellprüfung** (automatisch beim Öffnen, jedes Mal)
: **Ebene Verwaltung**, und Ebene Medium nur für die dabei berührten Spuren. Bei CP/M
  ist das der Verzeichnisbereich (bei `cpa780` 128 Plätze × 32 B = 4 KB und
  damit weniger als eine Spur — c2h0 trägt 5×1024), bei UDOS
  Belegungsplan (Spur 23, 384 Byte) und die Verzeichnisdatei (Spur 22). Das ist genau
  die Menge, die `mount()` schon gelesen hat — die Prüfung kostet damit **keinen
  einzigen zusätzlichen Spurzugriff**, auch nicht an einem echten Laufwerk. Ebene
  *Dateien* bleibt außen vor: sie verlangte bei UDOS den Kopfsektor jeder Datei, also
  denselben Unterschied wie zwischen `CAT` und `CAT F=L` — an einer physischen
  Diskette Dutzende einzeln nachzuladender Spuren.

  > **Der Schnitt geht durch BYTES, nicht durch Spuren.**  Beim Bauen der ersten
  > Etappe fiel es sofort auf: das `cpa780`-Verzeichnis endet nach 4096 von 5120
  > Byte **mitten in c2h0**.  Wer die Schnellprüfung „bis zum Ende der
  > Verzeichnisspur" laufen lässt, meldet einen Schaden an Dateidaten, ohne die
  > Datei benennen zu können — und die Schnellprüfung fände plötzlich mehr als die
  > Vollprüfung an derselben Stelle sagt.

**Vollprüfung** (ausdrücklich, mit Fortschrittsanzeige)
: alle drei Ebenen, jede Spur. An einer Datei ein Wimpernschlag, an einer physischen
  Diskette ein bis zwei Minuten. Sie ist Voraussetzung für die eingreifenden
  Reparaturen (E8) und für die Wiederherstellung.

Dazwischen gibt es einen dritten, wichtigen Zustand: **Vollprüfung ohne Nachladen**
(`nachladen=false`). Sie wertet aus, was von einer physischen Diskette schon im
Speicher liegt, und meldet ehrlich `vollstaendig=false`. Für die Anzeige ist das der
Unterschied zwischen „ohne Befund" und „**bislang** ohne Befund — 62 von 160 Spuren
angesehen".

> ⚠ **`vollstaendig=false` sperrt jede Reparatur, die etwas freigibt oder verwirft.**
> Erlaubt bleiben Reparaturen, die nur *belegen* oder *nachrechnen* — sie können auch
> auf Grundlage unvollständigen Wissens nichts zerstören. Das ist dieselbe Denkweise
> wie bei `DiskMedium::complete()` (14_physische_diskette.md): eine halb gelesene
> Diskette darf sich nicht für leer erklären.

---

## 7. CP/M — was geprüft wird

Gilt für alle `type: cpm`-Profile, also CP/A, SCPX **und** SCP1700 (CP/M-86, A7100) —
dort ist das Dateisystem gewöhnliches CP/M, nur die Physik ist besonders (§22 des
Werkzeugentwurfs), und die Physik prüft die Ebene Medium ohnehin unabhängig vom
Dateisystem.

Die entscheidende Eigenheit von CP/M: **es gibt keinen gespeicherten Belegungsplan.**
`allocationMap()` wird bei jedem Mounten aus dem Verzeichnis neu gebaut. Damit
entfallen die klassischen `fsck`-Befunde *„Block als belegt markiert, gehört aber
niemandem"* und *„Block gehört einer Datei, ist aber als frei markiert"* — sie sind
strukturell unmöglich. Umgekehrt kann CP/M etwas, das UDOS nicht kann: **derselbe
Block in zwei Dateien**, und das ohne jede Warnung des Betriebssystems.

Daraus folgt eine zweite Beobachtung, die die Umsetzung stark vereinfacht hat:
weil **alles**, was eine CP/M-Datei ausmacht, im Verzeichnis steht, fällt hier die
Ebene *Dateien* fast vollständig mit der Ebene *Verwaltung* zusammen — es gibt
keine Kette, die man verfolgen müsste, und keine zweite Darstellung, gegen die man
abgleichen könnte.  Was die Vollprüfung bei CP/M zusätzlich bringt, ist deshalb im
Kern die **Medienebene**: jeden Sektor anfassen und einen CRC-Fehler auf die Datei
zurückrechnen, der er gehört.  Bei UDOS (§9) liegt das genau andersherum.

### 7.1 Ebene Verwaltung (Schnellprüfung)

| Kennung | Befund | Schwere |
|---|---|---|
| `cpm.dir.fuellbyte` | Der Verzeichnisbereich besteht aus einem einzigen, immer gleichen Byte ≠ 0xE5 (0xF6, 0x53 …): formatiert, aber nie eingerichtet — `directoryFill()` sagt es schon heute | Warnung |
| `cpm.dir.user` | Nutzerbyte ist weder 0…15 noch 0xE5 | Fehler |
| `cpm.dir.name` | Name enthält Kleinbuchstaben, Steuerzeichen oder eines von `<>.,;:=?*[]`; leerer Name bei belegtem Platz | Fehler |
| `cpm.dir.doppelt` | Zwei Plätze mit gleichem Nutzerbereich, Namen **und** Extentnummer | Fehler |
| `cpm.dir.extentluecke` | Die Extents einer Datei sind nicht 0…n lückenlos (Extent 2 vorhanden, 1 fehlt) | Fehler |
| `cpm.dir.rc` | `RC > 128`; oder `RC ≠ 128` in einem Extent, dem weitere folgen; oder `RC` passt nicht zur Zahl belegter Blockzeiger | Warnung (Länge falsch) |
| `cpm.block.ausserhalb` | Blockzeiger ≥ `totalBlocks()` — der heutige Hinweis „vermutlich das falsche Dateisystemprofil" | Fehler |
| `cpm.block.verzeichnis` | Blockzeiger zeigt in die Verzeichnisblöcke: die Datei „enthält" das Verzeichnis | Gefahr |
| `cpm.block.doppelt` | Ein Block steht in zwei Verzeichnisplätzen | **Gefahr** |
| `cpm.block.luecke` | Nullzeiger vor einem belegten Zeiger innerhalb eines Extents (dünn besetzte Datei — bei CP/M 2.2 fast immer ein Schaden, aber nicht sicher) | Warnung |
| `cpm.dir.leer` | Der Eintrag sagt *n* Sätze an, nennt aber keinen einzigen Block | Fehler |
| `cpm.dir.sonderplatz` | *n* Plätze tragen eine Sonderfunktion (CP/M-3-Kennwort, Datenträgeretikett, Zeitstempel).  **Kein Schaden** — dieses Werkzeug wertet sie nur nicht aus.  Sie als Fehler zu melden hieße, auf jeder Diskette mit Zeitstempeln 32 Fehler zu behaupten | Info |
| `cpm.dir.geloescht` | *n* freie Plätze tragen noch einen lesbaren Eintrag: wiederherstellbar (§13) | Info |
| `cpm.verz.ungelesen` | Die Verzeichnisspur ist an einer physischen Diskette noch nicht gelesen — geprüft wurde nichts | Info |
| `cpm.verz.unlesbar` / `cpm.bereich.fehlt` | Der Verzeichnisbereich bzw. `data_start` ist nicht erreichbar | Fehler |

### 7.2 Ebene Dateien und Medium (Vollprüfung)

| Kennung | Befund | Schwere |
|---|---|---|
| `cpm.medium.crc` | Ein Sektor, der zu einem belegten Block gehört, hat eine falsche Daten- oder ID-CRC — **mit dem Namen der betroffenen Datei und der Satznummer** | Fehler |
| `cpm.medium.unformatiert` | Eine Spur im Dateisystembereich trägt keine Adressmarken | Fehler |
| `cpm.medium.fehlt` | Ein laut Geometrie erwarteter Sektor fehlt in der Spur | Fehler |
| `cpm.medium.frei_beschrieben` | Ein freier Block ist nicht mit dem Füllbyte gefüllt: da liegt Altbestand.  **Bleibt unvergeben** — er ist kein Schaden, sondern ein Fund: seit Etappe 5 erscheint er als **Rohbereich im Suchlauf** (§13.1), nicht im Prüfbericht | Info |
| `cpm.medium.systemspur` | Ein Sektor der **Systemspuren** fehlt oder trägt eine falsche CRC.  Sie gehören keinem Dateisystem — das Lade-ROM liest Spur 0 blind ein —, aber ein Schaden dort kostet die Bootfähigkeit, und sonst sagt es niemand: die Erkennung meldet nur „1 Sektor mit CRC-Fehler", ohne zu sagen, welcher | Warnung |

> **Systemspuren werden PHYSISCH durchgegangen**, nicht durch Lesen der erwarteten
> IDs.  Eine Systemspur darf eine ID doppelt tragen — die A7100-Bootspur wurde in
> einem Zug über den Index hinaus beschrieben und hat 19 Adressmarken für 16
> Sektoren.  Wer je ID einmal liest, bekommt den ersten Treffer und übersieht,
> dass gerade die zweite Aufnahme die schadhafte ist.  Dafür gibt es
> `SectorSpace::trackSectors()`.  Im **Datenbereich** wird dagegen weiter über die
> IDs gelesen: dort ist maßgeblich, was das Dateisystem sieht.

Der Wert von `cpm.medium.crc` liegt in der **Rückabbildung**: der Diskeditor zeigt
schon heute einen roten Sektor, aber niemand sagt einem, dass es Satz 14 von
`STAT.COM` ist. Die Zuordnung ist billig — Block → Byte-Offset → Spur/Sektor ist
genau die Rechnung, die `CpmFileSystem::readAt()` ohnehin macht, samt Sektorversatz.

---

## 8. CP/M — was repariert wird

| Reparatur | Wirkt auf | Datenverlust |
|---|---|---|
| `cpm.platz.freigeben` | Setzt das Nutzerbyte eines unbrauchbaren Platzes auf 0xE5. Der Rest des Platzes bleibt stehen — damit ist der Eintrag **später noch wiederherstellbar** (§13), und CP/M stört er nicht mehr. | Verweis |
| `cpm.zeiger.streichen` | Nullt die Blockzeiger eines Extents, die außerhalb liegen oder ins Verzeichnis zeigen, und zieht `RC` nach. | ja (dieser Teil der Datei) |
| `cpm.rc.anpassen` | Setzt `RC` auf das, was die Blockzeiger tragen. | nein |
| `cpm.kreuz.erstem_lassen` | Bei einem doppelt belegten Block: der Block bleibt bei der Datei mit dem **kleineren Verzeichnisindex**, die andere bekommt dort einen Nullzeiger. | ja (für die zweite Datei) |
| `cpm.kreuz.kopieren` | Block auf einen freien Block kopieren und die zweite Datei dorthin zeigen lassen. Beide Dateien bleiben vollständig — eine von beiden trägt an dieser Stelle fremde Daten. **Das ist der übliche `fsck`-Weg** und die Empfehlung, solange ein freier Block da ist. | nein |
| `cpm.crc.neu` | Rechnet die Daten-CRC eines Sektors aus seinem Inhalt neu. **Der Inhalt wird damit für richtig erklärt** — der Sektor ist danach lesbar, aber nicht geheilt. | nein (aber: die Wahrheit geht verloren) |
| `cpm.verzeichnis.neu` | `mkfs()` — Verzeichnis leeren. Nur als letzte Möglichkeit, wenn der Verzeichnisbereich Müll ist und der Anwender die Diskette neu benutzen will. | **alles** |

> Die Reparatur `cpm.crc.neu` bekommt in der Oberfläche eine eigene Kennzeichnung
> („macht den Sektor lesbar, heilt ihn nicht"). Sie ist genau dann richtig, wenn ein
> Sektor beim Kopieren die CRC verloren hat, und genau dann falsch, wenn die
> Diskette an dieser Stelle wirklich verschlissen ist. Diesen Unterschied kann das
> Werkzeug nicht kennen; es muss ihn benennen und den Anwender entscheiden lassen.

---

## 9. UDOS / ZDOS (A5120) — Prüfung und Reparatur

Hier liegt der eigentliche Gewinn. UDOS führt einen **gespeicherten** Belegungsplan
(Spur 23) neben einer **selbsttragenden** Verkettung (die vier Bytes hinter jeder
Daten-CRC). Zwei unabhängige Darstellungen derselben Wahrheit — genau die Konstellation,
in der ein `fsck` etwas beweisen kann. Und der Plan ist **die einzige Instanz, die den
freien Platz kennt** (`doc/udos_diskettenformat.md` §4.2): steht er falsch, vergibt UDOS
belegte Sektoren weiter.

Erinnerung an die Sitten, die hier zu beachten sind: jede **Seite** ist ein eigenes
Dateisystem (also zwei Berichte je Diskette), ein Satz belegt `Satzlänge/128`
**physisch aufeinanderfolgende Sektoren derselben Spur**, und die Systemspuren sind
0–2, 21 (Bootspur), 22 (Verzeichnis) und 23 (Karte).

### 9.1 Ebene Verwaltung (Schnellprüfung)

| Kennung | Befund | Schwere |
|---|---|---|
| `udos.karte.ungueltig` | `UdosBitmap::looksValid()` schlägt fehl — mit dem `why`-Text, der heute weggeworfen wird | Fehler |
| `udos.karte.zaehler` | Gespeicherter Frei-/Belegtzähler ≠ ausgezählt (heute schon als `FsInfo`-Warnung) | Warnung |
| `udos.karte.geometrie` | Sektoren je Spur / Spurzahl in der Karte passen nicht zur Geometrie | Fehler |
| `udos.karte.system` | Ein Sektor, den **das Dateisystem selbst** belegt (die drei Kartensektoren, der Kopfsektor der Verzeichnisdatei, deren Sätze), steht als frei | **Gefahr** |

> ⚠ **Die Systemspuren 0–2 und die Bootspur sind Sitte, nicht Struktur — sie werden
> NICHT geprüft.**  Das war im Entwurf anders vorgesehen und beim Umsetzen an echten
> Datenträgern widerlegt: `udos_boot_scp.hfe` Seite 0 hat auf Spur 0 nur die Sektoren
> 1–3 belegt und auf Spur 1 die Sektoren 1–6 **und** 17–24; Seite 1 von
> `udos_ds77_k5601_fremdsync.hfe` hat die Spuren 0–2 **völlig frei** — eine reine
> Datenseite, die nie einen Urlader trug.  Ein Prüfer, der dort „muss belegt sein"
> verlangt, meldet auf gesunden Disketten Fehler und wird zu Recht weggeklickt (E10).
> Geprüft wird deshalb nur, was sich **ableiten** lässt.  Aus demselben Grund bleiben
> die reservierten Spuren beim Befund `belegt_aber_frei` außen vor: dort liegen
> Urlader und Bootabbild, die keiner Datei gehören und trotzdem zu Recht belegt sind.
| `udos.verz.kaputt` | Der Kopfsektor der Verzeichnisdatei (Spur 22 Sektor 1 — der EINZIGE feste Einstiegspunkt) ist unlesbar oder weist sich nicht als Typ D aus.  Danach bricht die Prüfung ab: ohne ihn gibt es keine Dateien | Fehler |
| `udos.verz.kette` | Die Verzeichnisdatei selbst ist in sich nicht schlüssig (Satzzahl ≠ Kette) | Fehler |
| `udos.verz.ungelesen` | Die Verzeichnisspur ist an einer physischen Diskette noch nicht gelesen — geprüft wurde nichts | Info |
| `udos.verz.name` | Zwei Einträge tragen denselben Namen; oder ein Name enthält unmögliche Zeichen | Warnung |
| `udos.verz.eintrag_kaputt` | Ein Eintrag zeigt auf einen Sektor, der kein plausibler Kopfsektor ist | Fehler |

### 9.2 Ebene Dateien (Vollprüfung)

| Kennung | Befund | Schwere |
|---|---|---|
| `udos.kette.bruch` | Die Kette endet vor `record_count` (Vorwärtszeiger `FFFF` zu früh) | Fehler |
| `udos.kette.zyklus` | Die Kette läuft im Kreis.  `recordChain()` bricht dabei schon heute an einer Obergrenze ab; die Prüfung führt eine Besuchtliste und nennt deshalb **den Sektor, an dem sich die Schleife schließt** | Fehler |
| `udos.kette.rueckwaerts` | Der Rückwärtszeiger eines Satzes zeigt nicht auf seinen Vorgänger.  **Der des ersten Satzes nennt den Kopfsektor**, nicht `FFFF` — mit `FFFF` als Erwartung meldete die Prüfung jede gesunde Datei der Referenzdiskette | Warnung |
| `udos.kette.spurwechsel` | Die Sektoren eines Satzes überschreiten eine Spurgrenze (verletzt §7) | Fehler |
| `udos.kette.ausserhalb` | Ein Zeiger nennt Spur oder Sektor außerhalb der Diskette | Fehler |
| `udos.kette.doppelt` | Ein Sektor liegt in den Ketten zweier Dateien | **Gefahr** |
| `udos.karte.frei_aber_belegt` | Ein Sektor gehört zu einer Datei, steht aber als **frei** in der Karte | **Gefahr** |
| `udos.karte.belegt_aber_frei` | Ein Sektor steht als belegt, gehört aber zu keiner Datei: verlorener Platz — und ein Kandidat der Wiederherstellung (§13) | Warnung |
| ~~`udos.kopf.satzlaenge`~~ | Satzlänge 0 oder kein Vielfaches von 128.  **Gibt es nicht als eigenen Befund**: `readHeader()` weist einen solchen Kopfsektor schon beim Lesen ab, der Fall erscheint deshalb als `udos.verz.eintrag_kaputt` samt Begründung.  Ein zweiter Befund für dieselbe Sache wäre nur Rauschen |
| `udos.kopf.segmente` | Die Segmentliste (Offset 40…121, nur Typ P) läuft ohne Abschluss `00 00 00 00` bis ans Ende.  Beim Zurückschreiben verlöre die Datei Segmente — und eine Programmdatei ohne ihre Segmente startet nicht | Warnung |
| `udos.kopf.letzter` | „Bytes im letzten Satz" > Satzlänge | Warnung |
| `udos.kopf.typ` | Typbyte ohne gesetztes Typbit | Warnung |
| `udos.kopf.speicher` | Typ P/P1 mit LOW/HIGH = `FFFF` oder HIGH < LOW: **UDOS lehnt die Datei beim Laden mit `MEMORY PROTECT VIOLATION` ab** (§14) | Fehler |
| `udos.kopf.rueckzeiger` | Der Rückwärtszeiger des Kopfsektors zeigt nicht auf den Verzeichnissatz, in dem sein Eintrag steht | Warnung |
| `udos.medium.nachspann` | Ein Sektor im Dateisystembereich hat weniger als 4 Byte hinter der Daten-CRC — dort ist keine Verkettung ablegbar (das ist auch die Bedingung, an der `rawCompatible()` `.img` verweigert) | **Gefahr** bei Schreibabsicht, sonst Warnung |
| `udos.medium.crc` | wie bei CP/M, mit Datei- und Satznummer | Fehler |

### 9.3 Reparaturen

| Reparatur | Wirkt | Datenverlust |
|---|---|---|
| `udos.karte.zaehler.neu` | `refreshCounters()` + speichern. Der harmloseste Eingriff überhaupt. | nein |
| `udos.karte.sektoren.sperren` | Trägt genau die Sektoren aus `udos.karte.frei_aber_belegt` als belegt nach. **Empfohlen**, weil er nur nimmt und nie gibt: er kann unter keinen Umständen Daten freigeben — deshalb ist er auch bei `vollstaendig=false` erlaubt. | nein |
| `udos.karte.system.sperren` | Systemspuren nachtragen. | nein |
| `udos.karte.neu` | **Belegungsplan vollständig aus den Ketten neu aufbauen** — Systembereiche, Verzeichnisdatei, Karte, alle Kopfsektoren, alle Sätze; alles andere frei. Gibt auch verlorenen Platz zurück. Nur bei `vollstaendig=true` **und** wenn kein Befund der Ebene Dateien mit Schwere ≥ Fehler offen ist (E8); sonst gesperrt mit genau dieser Begründung. | nein, wenn die Bedingung gilt |
| `udos.kette.rueckwaerts.neu` | Schreibt die Rückwärtszeiger aus der Vorwärtskette neu (nur der Nachspann, `writeSectorTail`-Sitte: Nutzdaten und CRC bleiben unberührt). | nein |
| `udos.kette.kuerzen` | Kette am Bruch abschneiden: `record_count`, `last_record` und „Bytes im letzten Satz" auf den erreichbaren Teil setzen. Danach ist die Datei kürzer und wieder in sich stimmig. | ja (der Rest) |
| `udos.verz.eintrag.entfernen` | Einen Eintrag herausschneiden, dessen Kopfsektor unbrauchbar ist. | Verweis |
| `udos.kopf.rueckzeiger.neu` | Rückwärtszeiger des Kopfsektors auf den richtigen Verzeichnissatz setzen. | nein |
| `udos.kopf.speicher.setzen` | LOW/HIGH aus der Segmentliste ableiten (LOW = kleinster Segmentanfang, HIGH = größtes Segmentende), STACK auf 0. **`geraten=true`** — der wirkliche Wert schließt den Arbeitsspeicher ein und steht nirgends. Macht die Datei ladbar, nicht unbedingt lauffähig. | nein |
| `udos.verz.name.umbenennen` | Einen der beiden gleichnamigen Einträge umbenennen (`NAME.1`). | nein |

> **Warum der Neuaufbau des Plans der wertvollste Eingriff überhaupt ist.** Bei UDOS
> ist die Verkettung *im Datenstrom selbst* untergebracht; sie überlebt fast alles.
> Der Belegungsplan dagegen ist eine einzige Spur — 384 Byte auf Spur 23 — und ein
> Schreibabbruch dort kostet den gesamten Überblick über die Diskette, obwohl keine
> einzige Datei Schaden genommen hat. Genau dieser Fall ist vollständig rekonstruierbar,
> und zwar beweisbar: der neu gerechnete Plan muss den alten in allen Bits enthalten,
> in denen dieser belegt sagt und eine Kette zustimmt.

---

## 10. UDOS1715 / NDOS (PC 1715, P8000)

Dieselbe Betriebssystemfamilie, andere Verkettung: der µPD765 erreicht die Bytes
hinter der Daten-CRC nicht, deshalb stehen die Adressen in eigenen **Zeigersektoren**
(je 125 Adressen, untereinander verkettet, `FIRSTBL` im Descriptor bei `80H`). Eine
„Spur" ist der **ganze Zylinder** (32 Sektoren, `UDOS-Sektor = (ID−1) + Kopf·16`).

Die Prüfungen der Ebene Verwaltung sind wörtlich die von §9.1 (der Belegungsplan folgt
derselben Struktur, nur mit 80 statt 78 Spureinträgen und beiden Zählern echt). Auf der
Ebene Dateien treten an die Stelle der Kettenprüfungen:

| Kennung | Befund | Schwere |
|---|---|---|
| `ndos.firstbl` | `FIRSTBL` zeigt außerhalb oder auf keinen plausiblen Zeigersektor | Fehler |
| `ndos.zeiger.kette` | Vor-/Rückwärtszeiger der Zeigersektoren untereinander widersprüchlich | Warnung |
| `ndos.zeiger.anzahl` | Zahl der Adressen ≠ `record_count` (+1 für den Descriptor im ersten Block) | Fehler |
| `ndos.zeiger.ausserhalb` | Eine Adresse nennt Spur/Sektor außerhalb der Diskette | Fehler |
| `ndos.zeiger.doppelt` | Eine Adresse steht in zwei Dateien | **Gefahr** |
| `ndos.satz.spurwechsel` | Ein Record überschreitet die **Spur**grenze (die Kopfgrenze darf er, `CAT` tut es) | Fehler |
| ~~`ndos.system.bereich`~~ | War als Warnung vorgesehen, wenn der gesperrte Systembereich zu keiner bekannten Ausprägung passt.  **Fallengelassen**: schon die beiden bekannten Ausprägungen unterscheiden sich (PC 1715: Spuren 0/22/23; P8000: zusätzlich Kopf 0 der Spuren 0, 21, 22, 23), und eine dritte wäre kein Schaden, sondern eine dritte Sitte.  Es gibt nichts, wogegen man prüfen könnte, ohne die nächste unbekannte Maschine für kaputt zu erklären (E10) |

Reparaturen: `ndos.karte.*` wie in §9.3, dazu `ndos.zeiger.kette.neu` (Vor- und
Rückwärtszeiger der Zeigersektoren aus ihrer Reihenfolge neu schreiben — sie sind
redundant und damit ableitbar) und `ndos.zeiger.anzahl.anpassen` (`record_count` auf
die Zahl der wirklich vorhandenen Adressen setzen).

> **Kein Sonderweg für den P8000.** Die WEGA-Startdiskette des P8000 fährt dasselbe
> NDOS; sie unterscheidet sich in der Füllung hinter dem Belegungsplan (`77H` statt
> `00`) und im Umfang des Systembereichs. Beides ist in `UdosBitmap` bzw. im Profil
> schon abgebildet — die Prüfung darf daraus **keinen** Befund machen. Wächter:
> `Udos1715P8000`-Fixture prüft ohne Befund.

---

## 11. Ebene 0 — „Warum wurde denn nichts erkannt?"   ✅ *(umgesetzt 2026-08-19)*

Der bisher unangenehmste Fall ist der, in dem das Werkzeug gar nicht erst hineinkommt:
`roh geöffnet — kein Dateisystem erkannt` (§12.6). Der Anwender bekam einen Satz und
keine Handhabe. Dabei **weiß** die Erkennung ziemlich genau, woran es lag: jede
Positivprobe (`UdosFileSystem::looksLikeUdos`, `Udos1715FileSystem::looksLikeUdos1715`,
`UdosBitmap::looksValid`, `CpaDpbRule::profile`, `cpmVerzeichnisPlausibel`) liefert
einen `why`-Text — und `disk_volume.cpp` warf ihn im `continue` weg.

Jetzt sammelt `DiskVolume::merkeAblehnung` ihn als **(Kandidat, Grund)** ein, und
`DiskVolume::check` macht daraus Befunde, sobald `hasFileSystem() == false` ist. Auf
einer roh geöffneten Diskette liefert die Prüfung damit statt eines Berichts über das
Dateisystem einen Bericht über die **Ablehnungen**:

```
$ k1520disktool check kaputt.img
kaputt.img  cpa780 / (kein Dateisystem erkannt)  Schnellprüfung

Hinweis Erkennung   cpa780    erkennung.abgelehnt   Geometrie cpa780: Verzeichnisplatz 0
                                                    trägt Nutzerbereich 0xDC — das
                                                    Verzeichnis ist nicht angelegt
Hinweis Erkennung   cpa_auto  erkennung.abgelehnt   Geometrie cpa780: …

kein Dateisystem erkannt — 2 geprüfte(r) Kandidat(en) oben          [exit 2]
```

Das ist oft schon die ganze Diagnose. Genau so wurden die drei jüngsten Fremdformate
gelöst (fremde Sync-Sitte, P8000, SCP1700) — jedes Mal war die Frage „welche Prüfung
sagt Nein und mit welcher Zahl". Die Prüfung macht aus dieser Handarbeit eine Ausgabe.

Sechs Festlegungen, die dabei entstanden sind:

* **Eine eigene Ebene, `FsLayer::Erkennung`** (Zahl 3, additiv an die C-ABI angehängt,
  `EBENE_ERKENNUNG` in `k1520disk.py`). Sie beschreibt nicht das Dateisystem, sondern
  das Ausbleiben eines solchen — sie in `Verwaltung` zu stecken hieße, von einer
  Verwaltung zu sprechen, die es nicht gibt.
* **Schwere `Info`, nie mehr.** Ein Ablehnungsgrund ist kein Schaden, sondern ein
  Fund. Auf einer erkannten Diskette darf `erkennung.*` gar nicht vorkommen — Wächter
  `FsCheckKeineFalschmeldungen.EineErkannteDisketteHatKeineBefundeDerEbene0`.
* **Kennungen:** `erkennung.abgelehnt` (ein Kandidat mit seinem Grund) und
  `erkennung.ohne_kandidat` (es kam gar keine Dateisystemprobe zum Zug — dann lag es
  schon an der Geometrie, und deren Grund steht im Text).
* **Reparaturen gibt es hier nicht**; die Handhaben sind die vorhandenen (`--fs`
  übersteuern, `keepEvenTracks`, `dropSecondSide`, Diskeditor).
* **`check` und `fsck` öffnen roh** (`roh_erlaubt`), sonst käme die Ebene 0 auf der
  Kommandozeile nie zum Zug — gerade die unerkannte Diskette ist die, über die man
  etwas erfahren will. Rückgabewert bleibt `2` (nicht erkannt), nicht `1` (Befunde).
  Mit `--repair` bleibt es beim Abbruch: reparieren lässt sich nur ein Dateisystem,
  das es gibt.
* **Die Geometrie gehört in den Befund, auch wenn kein Dateisystem darauf liegt** —
  `detection().format` wird im rohen Zweig gesetzt; sonst zeigt die Anzeige ein leeres
  Feld statt der einen Sache, die feststeht.

> **Nebenbefund, an dem sich der Wert der Ebene 0 sofort zeigte:** der Grund aus
> `cpmVerzeichnisPlausibel` lief in einen `char t[80]` und wurde mitten im Wort
> abgeschnitten („… das Verzeichnis ist nicht ange"). Solange er weggeworfen wurde,
> fiel das keinem auf. Jetzt baut ihn ein `std::string`.

---

## 12. Reparatur — Ausführung

### 12.1 Rangfolge (E6)

Reparaturen werden **nicht** in der Reihenfolge der Anzeige ausgeführt, sondern nach
Rang — weil jede spätere Stufe auf dem Ergebnis der früheren aufsetzt:

1. **Verzeichnis und Kopfsektoren** (Einträge entfernen, Namen, Kopffelder)
2. **Ketten** (Rückwärtszeiger, Kürzen, Kreuzbelegung auflösen)
3. **Belegungsplan** (Sektoren nachtragen, Neuaufbau)
4. **Zähler** (`refreshCounters`)

Der Neuaufbau des Plans nach dem Kürzen einer Kette gibt die abgeschnittenen Sektoren
frei; in der umgekehrten Reihenfolge bliebe genau dieser Platz verloren. Innerhalb
einer Stufe wird nach Ort sortiert (Spur, Sektor) — reproduzierbare Reihenfolge, damit
zwei Läufe dasselbe Ergebnis haben.

### 12.1a Kein Loch hinterlassen (CP/M)

Ein Blockzeiger, der gestrichen werden muss (`cpm.zeiger.streichen` bei einem Zeiger
hinter dem Datenbereich oder ins Verzeichnis hinein, `cpm.kreuz.erstem_lassen` bei
Kreuzbelegung), wird **nicht** an Ort und Stelle genullt.  Bei CP/M liegt Satz *k*
eines Extents in Zeiger *k / (Blockgröße/128)* — eine Null mitten in der Liste
verschiebt damit jeden folgenden Satz an eine Stelle, an der er nie stand.  Der Extent
lieferte danach falsche Daten aus, ohne dass es jemandem auffiele, und die Prüfung
meldete prompt `cpm.block.luecke`.

Deshalb: ab dem ersten Nullzeiger wird **abgeschnitten** und `RC` auf die verbliebenen
Blöcke nachgezogen.  Was davor steht, gehört der Datei wirklich; was dahinter stand,
war ohnehin nur über den gestrichenen Zeiger erreichbar.  Bei einem unversehrten
Eintrag greift die Regel nicht — dort stehen hinter der ersten Null nur weitere Nullen.
Wächter: `FsCheckReparatur.CpmWilderBlockzeigerWirdGestrichen`, `cli_dt_fsck_wilder_zeiger`.

### 12.2 Transaktion

Der ganze Lauf ist **eine** Transaktion nach dem Muster von `insertAll`: Momentaufnahme
des `DiskMedium`, ausführen, bei einem Fehler zurückrollen. Bei einer physischen
Diskette gilt dabei die bekannte Sonderregel — zurückgerollt wird mit `restoreFrom`,
damit die betroffenen Spuren erneut als **geändert** gelten und wirklich auf die
Scheibe zurückgehen (14_physische_diskette.md).

Nach dem Lauf wird **automatisch neu geprüft** und der Dialog zeigt beide Zahlen:
`vorher 11 Befunde (2 Gefahr) → nachher 3 Befunde (0 Gefahr)`. Bleibt ein Befund
stehen, den die Reparatur beseitigen sollte, ist das selbst ein Fehler und wird als
solcher gemeldet.

### 12.3 Sicherung

* **Datei:** Die vorhandene Regel greift von allein — beim ersten Schreiben legt
  `DiskVolume::flush()` `<name>~` an (§14.2). Der Dialog nennt den Pfad.
* **Physische Diskette:** Es gibt kein `~`. Der Dialog bietet vor dem ersten Eingriff
  **„Abbild sichern…"** an (`exportImage` — das ist ein reiner Lesevorgang und, wenn
  die Vollprüfung gelaufen ist, sogar kostenlos, weil alle Spuren schon im Speicher
  liegen). Das Angebot ist nicht erzwungen, aber vorausgewählt.
* Grundsätzlich: `Gefahr`-Befunde bleiben nach dem Öffnen mit gesetztem **Schreibschutz**
  stehen. Das Aufheben verlangt in der Oberfläche eine zusätzliche Bestätigung, die den
  Befund wörtlich nennt.

---

## 13. Wiederherstellung — was ein Löschen übriglässt

Der Ausgangspunkt ist eine schlichte Tatsache: **keines der drei Dateisysteme
überschreibt beim Löschen Nutzdaten.**

| | Was das Löschen tut | Was übrigbleibt |
|---|---|---|
| **CP/M** | setzt das **Nutzerbyte** des Verzeichnisplatzes auf 0xE5 | Name, Typ, Attribute, Extentnummer, `RC` und **alle 16 Blockzeiger** stehen unverändert im Platz; die Blöcke selbst sind unberührt |
| **UDOS/ZDOS** | schneidet den Verzeichniseintrag heraus und löscht die Bits in der Karte | Kopfsektor **vollständig** (Typ, Eigenschaften, ENTRY, Satzlänge, Segmente, Datum), alle Sätze, alle Kontrollblöcke — also die ganze Kette. Verloren ist allein der **Name** (er steht nur im Verzeichnis) |
| **UDOS1715/NDOS** | dasselbe | Descriptor, Zeigersektoren, Daten. Ebenfalls nur der Name fehlt |

Das ist eine ungewöhnlich gute Ausgangslage: bei CP/M überlebt der Name und die
Blockliste, bei UDOS überlebt die vollständige Struktur *außer* dem Namen. Die beiden
Familien brauchen deshalb zwei verschiedene Suchverfahren.

> **Zur Namensfrage bei UDOS.** `removeDirEntry` schiebt die folgenden Einträge nach
> vorn und zieht `FF` nach — der Name ist im Verzeichnissatz überschrieben. Der
> Suchlauf sieht sich trotzdem die Verzeichnissätze nach **Namensresten** an (eine
> Zeichenfolge, die wie ein Name aussieht, hinter dem `FF`-Ende eines Satzes): fremde
> UDOS-Ausprägungen kompaktieren möglicherweise nicht. Findet sich etwas, wird es als
> *Vorschlag* angeboten, nie als Tatsache. Ohne Fund vergibt das Werkzeug
> `GERETTET.001`, `…002`, und der Anwender darf umbenennen.

### 13.1 Was gesucht wird

**CP/M — gelöschte Verzeichnisplätze.** Ein Platz ist ein Kandidat, wenn Byte 0 = 0xE5
ist, die übrigen 31 Byte **nicht** ebenfalls alle 0xE5 sind (das ist der Zustand eines
nie benutzten Platzes nach `mkfs`), der Name aus druckbaren Großbuchstaben besteht und
mindestens ein Blockzeiger im gültigen Bereich liegt. Kandidaten werden über
(Name, Typ) zu Dateien mit mehreren Extents zusammengefasst — der Name überlebt, also
funktioniert das Gruppieren zuverlässig.

**CP/M — freie Blöcke mit Inhalt.** Blöcke, die keiner lebenden Datei gehören und nicht
aus dem Füllbyte bestehen. Sie werden zu Läufen aufeinanderfolgender Blöcke
zusammengefasst — das sind die „Dateibestandteile" ohne Verzeichnisplatz (Platz durch
`mkfs` neu aufgesetzt, Verzeichnis überschrieben, Diskette umformatiert und nur teilweise
neu beschrieben).

**UDOS/NDOS — Kopfsektorsuche.** Über alle Sektoren, die die Karte als **frei** meldet
(und, in der gründlichen Fassung, über alle überhaupt), wird nach der Signatur eines
Kopfsektors gesucht:

```
Offset  0…5   == 00 00 00 00 00 00
Offset  12    Typbyte mit genau einem gesetzten Typbit (10/20/40/80|Subtyp)
Offset 15…16  Satzlänge, Vielfaches von 128, 128…4096, passt in eine Spur
Offset 13…14  Satzanzahl > 0 und ≤ Sektoren der Diskette
Offset  8…9 / 10…11   erster/letzter Satz im gültigen Bereich
Offset 30…31 == FF 00      Offset 38…39 == FF 00 oder FF FF     ← nur ZDOS
Offset 24…29 / 32…37  druckbares ASCII                          ← NDOS: oder leer
```

Sechs Nullbytes am Anfang und zwei feste Trennmarken sind eine harte Signatur;
Fehltreffer auf Nutzdaten sind praktisch ausgeschlossen. Kopfsektoren, die zu einer
**lebenden** Datei gehören, werden anhand des Verzeichnisses aussortiert. Von jedem
verbliebenen Fund aus wird die Kette verfolgt — bei ZDOS über die Kontrollblöcke, bei
NDOS über `FIRSTBL` und die Zeigersektoren.

> **Bei NDOS trägt die Bytesignatur nicht** (gemessen 2026-08-19 an
> `udos1715_640k_pc1715_system.img`). Die beiden Trennmarken stehen dort **nicht** —
> an 1EH und 26H stehen Nullbytes —, und der Änderungsvermerk (20H…25H) ist
> unbeschrieben statt druckbar. Beides ist eine Sitte des A5120-Systems, die unser
> eigener Schreibpfad übernommen hat; verlangt man sie, findet der Suchlauf auf einer
> echten PC-1715-Diskette **keinen einzigen** Descriptor. Getragen wird die Erkennung
> dort deshalb **strukturell**: `FIRSTBL` muss auf einen echten Zeigersektor zeigen
> (ADRCTR gerade und im Bereich), und dessen **erste Eintragung muss dieser Descriptor
> selbst sein** (§6 des NDOS-Formats). Das ist zufällig nicht zu treffen und damit die
> schärfere Probe als jede Bytemarke.

> **Die Systemspuren werden NICHT übersprungen.** Der naheliegende Griff — „auf Spur
> 0–2, 21, 22, 23 legt UDOS keine Datei an, also gar nicht erst hinsehen" — ist falsch,
> und zwar an der Referenzdiskette nachgewiesen: `NOTE.TO.SD` (`udos_boot_scp.hfe`,
> Seite 1) hat ihren Kopfsektor auf **Spur 21**, und auf Spur 22 liegt eine weitere
> gewöhnliche Datei. Seite 1 ist eine reine Datenseite ohne Urlader; dort ist die
> „Systemspur" nichts als eine Spur. Es ist dieselbe Lehre wie in §9.1: **die
> Systemspuren sind Sitte, nicht Struktur.** Ausgeschlossen wird deshalb nicht die Spur,
> sondern nur, was die Belegungskarte im Bootbereich als *belegt* führt und zu keiner
> Datei gehört (Urlader, Nukleus, Bootabbild). Wächter:
> `FsRecoverUdos.EinKopfsektorAufDerBootspurWirdGefunden`.

**UDOS/NDOS — Rohbereiche.** Wie bei CP/M, aber die Läufe enden **an der Spurgrenze**:
ein Lauf darüber hinaus ließe sich nicht mehr benennen (die zweite Sektornummer in
`fragment_c12h0_s6-s26.bin` läge auf einer anderen Spur), und es ist die Körnung des
Dateisystems selbst — ein UDOS-Satz überschreitet die Spurgrenze nie (§7 des
ZDOS-Formats).

### 13.2 Wie ein Fund bewertet wird

| Güte | Bedingung | Bedeutung |
|---|---|---|
| **Sicher** | Alle Blöcke/Sektoren des Fundes gehören **keiner** lebenden Datei, und (UDOS) jede Kettenverbindung ist beidseitig schlüssig | Der Inhalt ist mit hoher Wahrscheinlichkeit unverändert der von damals |
| **Wahrscheinlich** | Struktur vollständig, aber einzelne Sektoren tragen eine falsche CRC, oder ein Rückwärtszeiger fehlt | Lesbar, mit benannten Lücken |
| **Bruchstück** | Ein Teil ist inzwischen neu vergeben (die Kette läuft in einen Sektor, der jetzt einer lebenden Datei gehört) oder bricht ab | Nur der erreichbare Anfang lässt sich retten — die Stelle wird genannt |

Wichtig ist, dass die Güte **kein Gefühl** ist, sondern eine Aussage mit Belegen: der
Dialog zeigt zu jedem Fund die Liste der Konflikte im Klartext („Satz 12–14 liegen auf
c31h0 S9–11, die jetzt zu `HELP.DAT.00` gehören").

### 13.3 Was mit einem Fund geschehen kann

**1. In den Ordner retten** (immer möglich, auch bei schreibgeschützter Diskette, auch
bei Bruchstücken). Alles unterhalb der Güte *sicher* wird mit einem Beiblatt gerettet
— bei CP/M als `<datei>.rettung.txt` neben der geretteten Datei —, das Herkunft, Güte
und die Vorbehalte nennt; fehlende Sätze werden mit dem Füllbyte aufgefüllt, damit die Offsets
stimmen — für ein Textdokument oder einen Binärdump ist das brauchbar, für ein Programm
nicht, und genau das steht dann auch dabei. Bei UDOS werden die vollständigen
Kopfsektorangaben in das bekannte Beiblatt `udos-dateiangaben.txt` geschrieben — damit
lässt sich eine gerettete Datei später mit `put` **vollwertig** wieder einspielen.

**2. Auf der Diskette wiederherstellen** (verlangt Schreibrecht; bei Güte
*Bruchstück* nur mit ausdrücklicher Bestätigung):

* *CP/M:* Nutzerbyte des Platzes zurücksetzen — mehr ist es nicht. Vorbedingung: kein
  Block des Fundes gehört inzwischen einer lebenden Datei (sonst entstünde genau der
  `cpm.block.doppelt`, den die Prüfung als **Gefahr** meldet). Diese Prüfung läuft
  unmittelbar vor dem Schreiben noch einmal.
* *UDOS/NDOS:* **gibt es nicht** — s. §13.3a.

**3. Als Rohbereich sichern** (Fragmente ohne Struktur): die Läufe werden als
`fragment_c12h0_b40-b47.bin` herausgeschrieben — Ort des Anfangs **und** Blockspanne.
(Der ursprünglich vorgesehene Name mit Sektorspanne wäre falsch geworden, sobald ein
Lauf über eine Spurgrenze geht: die zweite Sektornummer läge dann auf einer anderen
Spur als die erste.)  Dazu kommt eine Einordnung nach Inhalt —
*Text* (mindestens 90 % druckbar, `1A` und die Zeilenenden zählen mit), *Programm*
(Z80-Einsprungmuster am Anfang: `JP`, `LD SP,nn`, `DI`) oder *unklar*.  Ein *leerer*
Bereich wird gar nicht erst angeboten: ein Block, der aus **einem immer gleichen
Byte** besteht, ist Füllmuster und kein Inhalt — unabhängig davon, welches Byte es
ist (FORMAT.COM füllt je nach Menüpunkt mit `0xE5`, `0xF6` oder dem Prüfmuster
`0x53`).  Ohne diese Regel meldete die Oberflächensuche die halbe Diskette als
Bruchstück.  Die Vorschau
im Dialog zeigt die ersten 512 Byte hexadezimal mit ASCII-Spalte — dieselbe Darstellung
wie im Diskeditor.

> **Die Wiederherstellung schreibt niemals in die Diskette, solange nicht ausdrücklich
> „wiederherstellen" gewählt wurde** (E7). Das ist auch der Grund, warum der Suchlauf
> in einem schreibgeschützten Zustand vollständig benutzbar ist: der häufigste Fall ist
> „einmal alles retten, was noch da ist, dann die Diskette in Ruhe lassen".

### 13.3a Bei UDOS und NDOS wird nicht auf der Diskette wiederhergestellt

Eine Festlegung, kein Mangel — und die einzige Stelle, an der die beiden Familien
verschieden bedient werden.

**Der Grund ist der fehlende Name.** Bei CP/M ist das Zurückschreiben *ein Byte*, und
der wiederhergestellte Eintrag trägt seinen echten Namen — das Ergebnis ist genau die
Datei, die es vorher gab. Bei UDOS wären es **drei** Schreibzugriffe (Sektoren in der
Belegungskarte belegen, einen Verzeichniseintrag anlegen, den Rückwärtszeiger des
Kopfsektors umbiegen), und zwar auf einen Datenträger, den man für eine Rettung gerade
*nicht* anfassen will — für eine Datei, deren Name ohnehin frei erfunden werden muss.
Der Preis stimmt nicht.

**Der Weg zurück geht deshalb über Wege, die es schon gibt:**

1. Fund in den Ordner retten (`recover --to`, oder der Suchdialog).
2. Der Datei dort den passenden Namen geben.
3. Mit `put` wieder einspielen.

Und das ist **kein Verlust an Angaben**: `erase` schneidet den Verzeichniseintrag
heraus und löscht die Kartenbits — der Kopfsektor bleibt vollständig stehen. Typ,
Eigenschaften (W E L S R F), ENTRY, Satzlänge, Blocklänge, **alle** Speichersegmente,
LOW/HIGH/STACK und beide Datumsvermerke überleben das Löschen. `recoverExtract`
schreibt sie in dasselbe Beiblatt `udos-dateiangaben.txt`, das `extractAll` anlegt und
`insert`/`insertAll` von selbst wieder einlesen (`FileSystem::recoverEntry` →
`DiskVolume::recoverExtract`). Nachgewiesen an `ZLINK` der PC-1715-Systemdiskette:
sechs Speichersegmente, ENTRY 8492, Recordlänge 512 — nach *retten, benennen, put*
byteweise dieselbe Datei mit denselben Angaben.

`FsRecoverFind::wiederherstellbar` ist bei UDOS deshalb **immer** `false`, und
`warum_nicht` nennt diesen Weg statt nur abzusagen; `recoverRestore` ist überschrieben,
damit die Absage nicht die nichtssagende Vorgabe der Basisklasse ist. Der Dialog sperrt
den Knopf und zeigt den Grund — dieselbe Mechanik wie bei einem CP/M-Rohbereich.

---

## 14. C-ABI

Im Stil der bestehenden Schnittstelle: indexbasiert, Zeichenketten gehören der
Bibliothek, kein Rückruf in die Anwendung.

> **Stand:** Der Prüfungsteil ist umgesetzt — allerdings ohne
> `k1520d_check_progress` (es gibt noch keinen Aufruf, der lange genug läuft, um
> Fortschritt zu brauchen: die Vollprüfung ist an einer Datei ein Wimpernschlag, und
> an einer physischen Diskette ist sie noch gesperrt).  `k1520d_repair_*` und
> `k1520d_recover_*` steht seit Etappe 5 — mit drei Zusätzen gegenüber der Liste
> unten: ein zweites Argument `nachladen` an `k1520d_recover_scan` (wie bei
> `k1520d_check`), `k1520d_recover_suggestion` (der Namensvorschlag für einen
> namenlosen Fund) und `k1520d_recover_restorable`/`_blocked_why` (ob sich der Fund
> **auf der Diskette** eintragen lässt — herausholen geht immer).  Zwei Zusätze
> gegenüber dem Entwurf:
> `k1520d_check` nimmt ein drittes Argument `nachladen`, und
> `k1520d_check_tracks_read`/`_total` liefern die Spurzähler.

```c
/* ── Prüfung ──────────────────────────────────────────────────────────── */
/** level: 0 = schnell, 1 = voll.  nachladen=false bleibt bei bekannten Spuren.
 *  @return Zahl der Befunde, -1 bei Fehler (k1520d_last_error). */
K1520_API int  k1520d_check(K1520Disk h, int level, bool nachladen);
K1520_API bool k1520d_check_complete(K1520Disk h);
/** Fortschritt eines laufenden Prüf- oder Suchlaufs.  Die EINZIGE Funktion, die
 *  nebenläufig zum laufenden Lauf gerufen werden darf (wie k1520s_stats). */
K1520_API void k1520d_check_progress(K1520Disk h, int* getan, int* gesamt);

K1520_API int         k1520d_finding_count(K1520Disk h);
K1520_API const char* k1520d_finding_id(K1520Disk h, int i);       /* "udos.kette.doppelt" */
K1520_API int         k1520d_finding_severity(K1520Disk h, int i); /* 0 Info … 3 Gefahr */
K1520_API int         k1520d_finding_layer(K1520Disk h, int i);    /* 0 Medium 1 Verw. 2 Dateien */
K1520_API int         k1520d_finding_volume(K1520Disk h, int i);
K1520_API const char* k1520d_finding_object(K1520Disk h, int i);
K1520_API const char* k1520d_finding_text(K1520Disk h, int i);
K1520_API int         k1520d_finding_cyl(K1520Disk h, int i);      /* -1 = ortlos */
K1520_API int         k1520d_finding_head(K1520Disk h, int i);
K1520_API int         k1520d_finding_sector(K1520Disk h, int i);   /* laufende Nummer, 13_… §19 */

K1520_API int         k1520d_repair_count(K1520Disk h, int i);
K1520_API const char* k1520d_repair_id(K1520Disk h, int i, int j);
K1520_API const char* k1520d_repair_text(K1520Disk h, int i, int j);
K1520_API bool        k1520d_repair_destructive(K1520Disk h, int i, int j);
K1520_API bool        k1520d_repair_recommended(K1520Disk h, int i, int j);
K1520_API bool        k1520d_repair_blocked(K1520Disk h, int i, int j);   /* E8 */
K1520_API const char* k1520d_repair_blocked_why(K1520Disk h, int i, int j);
/** Alle in EINER Transaktion, in der Rangfolge aus §12.1; danach wird neu geprüft.
 *  @return Zahl der ausgeführten Reparaturen, -1 = nichts (zurückgerollt). */
K1520_API int k1520d_apply_repairs(K1520Disk h, const int* befund, const int* repair, int n);

/* ── Wiederherstellung (Etappe 5, so umgesetzt) ───────────────────────── */
/** level: 0 = nur Verzeichnisreste (billig), 1 = volle Oberflächensuche.
 *  nachladen wie bei k1520d_check.  @return Zahl der Funde, -1 bei Fehler. */
K1520_API int         k1520d_recover_scan(K1520Disk h, int level, bool nachladen);
K1520_API bool        k1520d_recover_complete(K1520Disk h);
K1520_API int         k1520d_recover_count(K1520Disk h);
K1520_API const char* k1520d_recover_name(K1520Disk h, int i);    /* "" = unbekannt */
K1520_API const char* k1520d_recover_type(K1520Disk h, int i);
K1520_API const char* k1520d_recover_origin(K1520Disk h, int i);  /* "Verzeichnisplatz 37" … */
/** Vorschlag für den Linux-Dateinamen — nie leer, auch ohne Namen. */
K1520_API const char* k1520d_recover_suggestion(K1520Disk h, int i);
K1520_API int         k1520d_recover_volume(K1520Disk h, int i);
K1520_API uint64_t    k1520d_recover_size(K1520Disk h, int i);
K1520_API int         k1520d_recover_quality(K1520Disk h, int i); /* 0 Bruchstück … 2 sicher */
K1520_API const char* k1520d_recover_detail(K1520Disk h, int i);  /* Konflikte im Klartext */
/** Lässt sich der Fund AUF DER DISKETTE eintragen?  (Herausholen geht immer.) */
K1520_API bool        k1520d_recover_restorable(K1520Disk h, int i);
K1520_API const char* k1520d_recover_blocked_why(K1520Disk h, int i);
K1520_API int         k1520d_recover_cyl(K1520Disk h, int i);     /* -1 = ortlos */
K1520_API int         k1520d_recover_head(K1520Disk h, int i);
K1520_API int         k1520d_recover_sector(K1520Disk h, int i);
K1520_API int         k1520d_recover_preview(K1520Disk h, int i, uint8_t* buf, int n);
K1520_API bool        k1520d_recover_extract(K1520Disk h, int i, const char* pfad);
K1520_API bool        k1520d_recover_restore(K1520Disk h, int i, const char* name);
```

> ⚠ **Driftwächter.** `tests/python/test_c_api.py` vergleicht Kopfdatei ↔
> `ctypes`-Deklarationen mechanisch. Jede dieser Funktionen muss im **selben Commit**
> in `app/core_binding/k1520disk.py` erscheinen, sonst ist die Python-Ebene rot.

---

## 15. Kommandozeile

```
k1520disktool check   <abbild> [--full] [--json]
k1520disktool fsck    <abbild> [--full] [--repair[=sicher|alle|<kennung>,…]]
                               [--dry-run] [--json]
k1520disktool recover <abbild> [--full] [--to <ordner>] [--list]
                               [--restore <nr>[=NAME]] [--json]
```

**Umgesetzt sind `check`, `fsck` und `recover`** (Etappen 2, 3, 5 und 6), `recover` für
CP/M, ZDOS und NDOS.  Es liefert **0**, solange der Lauf in Ordnung war — auch ohne
Fund: eine Diskette ohne gelöschte Dateien ist kein Fehler.  `--restore` gibt es nur
bei CP/M; bei UDOS/NDOS lehnt es ab und nennt den Weg über `put` (§13.3a).

* `check` bleibt, was es ist, und bekommt `--full`. Der Bericht wird zeilenweise und
  greptauglich: `SCHWERE  EBENE  VOLUME  ORT  KENNUNG  Text`.
* `fsck --repair` ohne Wert führt **nur die empfohlenen, nicht datenverlustbehafteten**
  Reparaturen aus — das ist der Wert, den man in ein Skript schreiben darf.
  `--repair=alle` nimmt auch die verlustbehafteten, `--repair=udos.karte.zaehler.neu,…`
  eine ausgewählte Menge. `--dry-run` nennt, was geschähe.
* `recover` ohne `--to` listet nur. `--restore` schreibt in das Dateisystem und
  verlangt `--write`-Semantik wie die übrigen ändernden Befehle.
* Rückgabewerte: 0 = ohne Befund · 1 = Befunde vorhanden · 2 = Reparatur gescheitert.
  (Das heutige Verhalten von `check` — Befunde ⇒ Rückgabewert ≠ 0 — bleibt damit.)
* Dieselben drei Befehle gibt es für **`--physical`** im Python-Einstieg
  (`app/disktool/physical_cli.py`), mit denselben vier Festlegungen wie dort schon:
  Ablehnung vor dem Motoranlauf, Nutzlast auf `stdout`, Fortschritt auf `stderr` aus
  einem Nebenfaden, Schadstelle ⇒ Rückgabewert 1 samt Ausweg im Text.

---

## 16. Oberfläche

### 16.1 Wo der Befund erscheint

Es gilt weiter die Rollenverteilung der sechs Meldungsorte (13_k1520disktool.md §20.4). Die Prüfung fügt
keinen siebten hinzu:

| Ort | Was die Prüfung dort ablegt |
|---|---|
| **Meldungsstreifen** | **eine** Zeile, der höchste Befund, mit Knopf „Befund ansehen…" — z. B. *„2 Sektoren tragen Daten und stehen als frei: der nächste Schreibvorgang zerstört sie."* |
| **Statuszeile rechts** | ein kompakter Zustand neben Dateien/frei/Modus/Schloss: `ohne Befund` bzw. `⚠ 3` |
| **Protokoll (F8)** | **jeder** Befund mit Uhrzeit, dazu jede ausgeführte Reparatur mit Vorher/Nachher |
| **Disketteangaben-Dialog** | der Bericht in voller Länge als Abschnitt (er gehört zu „Format, Geometrie, Erkennung und Belegung im Einzelnen") — **und die Schaltfläche „Vollprüfung"** (s. u.) |
| **Meldungsfenster** | nur vor einer Reparatur (Rückfrage) und bei Abbruch |
| **Titel** | unverändert — ein Befund ist keine ungespeicherte Änderung |

Der Streifen zeigt eine Meldung; es kann aber mehrere geben (unaufhebbarer
Schreibschutz, Schadstelle einer physischen Diskette, Prüfbefund). Rangfolge, absteigend:
**Schadstelle** → **Gefahr-Befund** → **sonstige dauerhafte Einschränkung** →
**Fehler-Befund** → **Erkennungshinweis**. Der Knopf gehört immer zur angezeigten
Meldung.

> **Die Vollprüfung wird aus den Diskettenangaben angestoßen, nicht aus dem Menü**
> (ergänzt in Etappe 2).  Der Bericht steht dort ohnehin, damit ist es der Ort, an
> dem man ihn vertiefen will — und es kommt kein Menüeintrag hinzu, der später mit
> dem Reparaturdialog um dieselbe Aufgabe konkurrierte.  **An einer physischen
> Diskette ist die Schaltfläche gesperrt** und nennt den Grund: dort zöge die
> Vollprüfung die ganze Scheibe ein und blockierte das Fenster ein bis zwei Minuten.
> Das gehört in einen Arbeitsfaden mit Fortschritt; der ist offen — an einer
> physischen Diskette bleibt der Knopf bis dahin gesperrt und sagt warum.
>
> Ohne diesen Weg wäre der wichtigste Befund des ganzen Vorhabens
> (`udos.karte.frei_aber_belegt`) in der Oberfläche unerreichbar gewesen — die
> Schnellprüfung kann ihn grundsätzlich nicht stellen.

### 16.2 Aktionen

Zwei neue Einträge, beide im Menü **Diskette**, beide über `_SPEC` in
`app/disktool/ui/actions.py` (damit greift der Wächter
`test_every_action_is_reachable_from_the_menu_bar` von allein):

| Name | Beschriftung | Kürzel | Freigabe (`_aktionen_pruefen`) |
|---|---|---|---|
| `reparieren` | *Dateisystem &reparieren…* | — | `offen` — auch schreibgeschützt, denn der Dialog **zeigt** zuerst; die Ausführung sperrt er selbst |
| `wiederherstellen` | *&Gelöschte Dateien suchen…* | — | `mit_fs` |

Bewusst **ein** Menüpunkt für Prüfen und Reparieren, nicht zwei: reparieren kann man
nur, was man vorher gesehen hat; der Dialog ist beides. Der Knopf im Streifen und der
Abschnitt im Diskettenangaben-Dialog öffnen denselben Dialog. Keine Tastenkürzel — das
erspart einen Eingriff in die Kürzeltabelle des Handbuchs, deren Wächter in beide
Richtungen prüft; das Handbuch bekommt stattdessen einen eigenen Abschnitt.

### 16.3 Der Reparaturdialog (`ui/fsck_dialog.py`)

```
┌─ Dateisystem prüfen und reparieren ─────────────────────────────────────┐
│ udos_boot_scp.hfe · udos43 · Vollprüfung · 160 von 160 Spuren angesehen │
│ 2 Gefahr   1 Fehler   4 Warnungen   3 Hinweise                          │
├─────────────────────────────────────────────────────────────────────────┤
│ ☑ ⛔ Side0  c31h0 S9   Sektor gehört zu HELP.DAT.00, steht aber frei    │
│      ↳ [Sektor im Belegungsplan nachtragen           ▾]  empfohlen      │
│ ☑ ⛔ Side0  TEST/ZLINK Sektor c44h0 S3 liegt in zwei Dateien            │
│      ↳ [Sektor bei TEST lassen, ZLINK kürzen         ▾]  Datenverlust   │
│ ☐ ✖  Side1  OS         Kette bricht nach Satz 7 von 11                  │
│      ↳ [Datei auf 7 Sätze kürzen                     ▾]  Datenverlust   │
│ ☐ ⚠  Side0  Spur 23    Freizähler sagt 812, ausgezählt sind 809         │
│      ↳ [Zähler nachrechnen                           ▾]                 │
│   ℹ  Side0  Verzeichnis 7 gelöschte Dateien wiederherstellbar → suchen… │
├─ Einzelheiten ──────────────────────────────────────────────────────────┤
│ HELP.DAT.00 belegt 14 Sätze ab c30h0 S1.  Satz 9 liegt auf c31h0 S9;    │
│ Bit 9 des Spureintrags 31 ist gelöscht.  UDOS würde diesen Sektor beim  │
│ nächsten Schreiben vergeben.                    [Im Diskeditor zeigen]  │
├─────────────────────────────────────────────────────────────────────────┤
│ 🔒 Die Diskette ist schreibgeschützt — Reparieren nicht möglich.        │
│ [Vollprüfung]  [Bericht speichern…]     [Ausgewählte reparieren] [Zu]   │
└─────────────────────────────────────────────────────────────────────────┘
```

Festlegungen:

* **Nichts ist vorausgewählt, was Daten verwirft.** Vorausgewählt sind ausschließlich
  Reparaturen mit `empfohlen && !datenverlust`.
* Ein Befund **ohne** Reparatur steht ohne Ankreuzfeld da — er ist kein Versäumnis,
  sondern eine Auskunft.
* Ein **gesperrter** Vorschlag (E8) bleibt sichtbar und nennt den Grund im Tooltip
  („Neuaufbau erst, wenn kein Kettenfehler mehr offen ist").
* Vor der Ausführung eine Rückfrage, die zählt: *„7 Reparaturen, davon 2 mit
  Datenverlust. Sicherung: `udos_boot_scp.hfe~` wird angelegt."*
* Danach: neu prüfen, Vorher/Nachher zeigen, alles ins Protokoll.

### 16.4 Der Wiederherstellungsdialog (`ui/recover_dialog.py`)

Links die Fundliste (*Name · Typ · Größe · Güte · Herkunft*, der Konflikt als
Kurzhinweis an der Zeile und in voller Länge über der Vorschau), rechts die Vorschau
(Hexdump mit ASCII-Spalte, umschaltbar auf Text). Oben die Wahl der Suchtiefe
(*Verzeichnisreste* ↔ *Ganze Oberfläche*); unten drei Knöpfe:
**„In den Ordner retten…"** (Vorgabe, immer bedienbar), **„Auf der Diskette
wiederherstellen"** (nur bei Schreibrecht; bei Bruchstücken mit Rückfrage) und
**„Alles Sichere retten…"**. Namenlose Funde bekommen ein direkt in der Liste
editierbares Namensfeld mit Vorbelegung.

Der Zielordner ist der der Ordnerseite (`default_folder_dir()`, 13_k1520disktool.md §20.8) — kein Dialog
ohne Startverzeichnis, der Wächter `test_every_file_dialog_gets_a_start_directory`
gilt auch hier.

> **Der Fortschrittsbalken ist entfallen** (Etappe 5) — und zwar nicht aus Bequemlich­keit:
> an einer **Datei** ist auch die Oberflächensuche ein Wimpernschlag (Sanduhrzeiger genügt),
> und an einer **physischen** Diskette ist die Oberflächensuche **gesperrt**, mit derselben
> Begründung wie die Vollprüfung im Prüfdialog: sie zöge die ganze Scheibe ein und liesse
> das Fenster ein bis zwei Minuten stehen.  Ein Balken hätte also nur dort etwas zu zeigen,
> wo es den Lauf noch gar nicht gibt.  Er kommt mit dem Arbeitsfaden, der beide Läufe
> zugleich betrifft.

---

## 17. Physische Diskette

Alles Bisherige gilt unverändert, mit vier Zusätzen:

1. **Die Schnellprüfung ist kostenlos** — sie sieht nur Spuren an, die das Öffnen
   ohnehin geholt hat. Das war der Grund für den Schnitt in E2.
2. **Vollprüfung und volle Suche laufen in einem Arbeitsfaden mit
   `mit_fortschritt()`**, wie das Öffnen. Sie ziehen die ganze Diskette ein — was
   nebenbei bedeutet, dass danach `refreshDetection()` greift und ein Abbild ohne
   weitere Kosten gesichert werden kann.
3. **Jede Reparatur läuft über den normalen Schreibweg** und damit über das
   Prüf-Lesen (§7.1): eine reparierte Spur gilt erst als geschrieben, wenn sie
   zurückgelesen und auf Sektorebene verglichen wurde. Eine Schadstelle beim
   Reparieren ist deshalb kein neuer Fall — sie landet im bekannten Ausweg
   („Diskette neu beschreiben").
4. **Reparieren ohne vorherige Sicherung wird abgeraten, nicht verboten.** Der Dialog
   bietet „Abbild sichern…" vorausgewählt an; nach einer Vollprüfung kostet das nichts
   mehr, weil alle Spuren im Speicher liegen.

---

## 18. Teststrategie

Ohne Hardware, in der Standardregression, nach dem Muster von `tests/unit/filesystem/`.

*Umgesetzt in `tests/unit/filesystem/test_fs_check.cpp` (33 Fälle).*

**Zuerst der Wächter gegen Falschmeldungen** (E10) — er kommt vor jedem einzelnen
Befundtest:

* `FsCheckKeineFalschmeldungen.JedeUnversehrteFixturePrueftOhneBefund` — über
  **alle** Fixtures in `tests/fixtures/disks/` (CP/A, SCPX, ZDOS beidseitig, fremde
  Sync-Sitte, UDOS1715, P8000) plus, in einem zweiten Fall, jedes über
  `DiskVolume::create` anlegbare CP/M-Katalogprofil — jeweils Schnell- **und**
  Vollprüfung. Erwartung: keine Befunde ab Schwere `Warnung`.
  **Eine Ausnahme, mit Beleg:** der A7100-Referenzdatenträger ist wirklich
  beschädigt; für ihn gibt es einen eigenen Fall, der genau die zwei erwarteten
  Befunde festnagelt.
* `FsCheckVertrag.SchweregradeUndEbenenSindStabil` friert die Zahlenwerte ein (sie
  gehen als `int` über die C-ABI), `…SpurzaehlerZeigenBeiVollstaendigerPruefungNvonN`
  die Bedeutung der Spurzähler.
  Die **Kennungen** brauchen keinen eigenen Vertragstest: jeder Schadensfall nennt
  seine Kennung wörtlich, ein Umbenennen bricht ihn also sofort.

**Dann die Schadensinjektion.** Ein Testhelfer `k1520test::beschaedige(...)` nimmt
eine `TempDisk`-Kopie einer gesunden Fixture und richtet genau **einen** definierten
Schaden an — über die vorhandenen Sektorwege (`writeSectorAt`, `writeSectorTail`,
Verzeichnisplatz roh schreiben), damit der Testcode selbst nicht am Prüfcode hängt.
Je Schaden ein Fall nach demselben Dreischritt:

1. Der Bericht enthält **genau** den erwarteten Befund (Kennung, Schwere, Ort) und
   sonst nichts Neues.
2. Die empfohlene Reparatur läuft durch, und die Nachprüfung ist sauber.
3. **Alle Dateien, die nicht betroffen waren, lesen sich Byte für Byte wie vorher** —
   das ist der Test, der eine übereifrige Reparatur auffliegen lässt.

Abgedeckte Schäden (Stand Etappe 2):

| Familie | Fälle |
|---|---|
| **CP/M** (13) | wilder Blockzeiger · Kreuzbelegung · Zeiger ins Verzeichnis · Steuerzeichen im Namen · Kleinbuchstaben · unbekanntes Nutzerbyte · Sondersatz (darf NICHT meckern) · `RC` zu groß · fehlender Dateianfang · gelöschter Platz · Verzeichnis voll 0xF6 · gefälschte Daten-CRC · zerstörter erster Platz (Grenze: die Diskette wird dann gar nicht erkannt) |
| **ZDOS** (8) | Kartenbit gelöscht (der Gefahrfall) · belegtes Bit ohne Datei · Kettenbruch · verdrehter Rückwärtszeiger · Zyklus · Sektor in zwei Dateien · angetastete Kartensektoren · falscher Freizähler |
| **NDOS** (5) | gelöschtes Planbit · `FIRSTBL` ins Leere · Satzzahl ≠ Adressen · verdrehter Zeigersektor · die P8000-Diskette bleibt ohne Befund |

Beschädigt wird **über die Datei bzw. den Sektorraum**, nicht über den Prüfcode: bei
einem `.img` ist der lineare Sektorraum bitgleich die Datei (Zusage des
`SectorSpace`), bei ZDOS werden Kartenbits und Kettenzeiger über
`SectorSpace::writeSector` gesetzt. So hängt der Test nicht an dem, was er prüft.

> **Zwei Fallen beim Schreiben solcher Tests.** (1) Eine Diskette mit zerstörtem
> ERSTEM Verzeichnisplatz wird von der Erkennung **abgelehnt** — die Prüfung kommt
> dann gar nicht zum Zug; der Weg hinein ist ein erzwungenes Profil (`--fs cpa780`).
> (2) Ein `.img` trägt **keine Geometrie**; `DiskImage::open` braucht sie mitgegeben.
> Bei `.hfe` stellt sich die Frage nicht.

**Wiederherstellung** — die Tests schreiben sich fast von selbst, weil das Werkzeug
beide Seiten beherrscht:

* `FsRecover.GeloeschteDateiKommtByteweiseZurueck` — Datei mit `get` sichern, mit `rm`
  löschen, suchen, retten, vergleichen. Für alle drei Familien.
* `FsRecover.ZurueckgeholteDateiIstWiederLesbar` — dasselbe, aber `restore` statt
  `extract`; danach `list()` und `read()` über den normalen Weg, und die anschließende
  Vollprüfung ist ohne Befund.
* `FsRecover.NachNeubelegungBleibtEinBruchstueck` — löschen, eine neue Datei schreiben
  (die den Platz nimmt), dann suchen: Güte *Bruchstück*, und der gerettete Anfang
  stimmt mit dem Original überein.
* `FsRecover.EineFrischeDisketteHatNichtsZuRetten` — kein Fund auf einer soeben
  angelegten Diskette (Gegenprobe gegen die 0xE5-Heuristik).
* `FsRecover.UdosBeiblattTraegtDieKopfangaben` — geretteter UDOS-Fund lässt sich mit
  `put` wieder vollwertig einspielen.

**Ebene 0 ✅:** `FsCheck.EineUnerkannteDisketteNenntDieAblehnungsgruende` — eine
absichtlich verstümmelte Diskette (Verzeichnisplatz 0 auf Nutzerbereich `0xDC`) roh
öffnen und prüfen, dass jeder Kandidat mit seinem `why` auftaucht, dass Ebene und
Schwere stimmen, dass keine Reparatur dranhängt und dass zweimaliges Prüfen dasselbe
liefert.  Gegenprobe
`FsCheckKeineFalschmeldungen.EineErkannteDisketteHatKeineBefundeDerEbene0` (kein
`erkennung.*` auf einer erkannten Diskette).  Dazu `cli_dt_check_ebene0` (Textausgabe
und Rückgabewert 2) und in der Oberfläche
`test_eine_roh_geoeffnete_diskette_nennt_die_ablehnungsgruende` (Protokoll) und
`test_der_pruefdialog_geht_auch_ohne_dateisystem`.

**Oberfläche und ABI:** `py_disk_c_api` um die neuen Funktionen erweitern (der
Driftwächter zwingt ohnehin dazu), `py_disktool_gui` um beide Dialoge (offscreen,
Verdrahtung: Befundliste gefüllt, Ausführen gesperrt bei Schreibschutz, kein
unerwartetes modales Fenster), `cli_disktool_fsck` für die Textausgabe und die
Rückgabewerte.

Alles bleibt in der schnellen Regression; nichts davon braucht ein Laufwerk oder das
`greaseweazle`-Paket.

---

## 19. Umsetzung in Etappen

| # | Inhalt | Ergebnis |
|---|---|---|
| **1** ✅ | Modell (`fs_check.h`), `FileSystem::check()`-Haken, CP/M-Prüfung (Verwaltung + Medium, beide Tiefen), Automatik in `DiskVolume::open`, `check --full` in der CLI, C-ABI, Streifen + Statuszeile + Protokoll + Diskettenangaben | **Umgesetzt 2026-08-18.**  Eine CP/A-Diskette sagt beim Öffnen, ob ihr Verzeichnis stimmt; `--full` findet zusätzlich Sektoren mit falscher CRC und nennt die Datei, die darauf liegt.  Noch keine Reparatur. |
| **2** ✅ | ZDOS und NDOS: Ebene Verwaltung und Ebene Dateien, Kreuzbelegung, Abgleich Karte ↔ Ketten, Zählerabgleich (aus `info()` hierher verlagert), Begrenzung gleichartiger Befunde | **Umgesetzt 2026-08-19.**  Der Gefahrfall (`frei_aber_belegt`) wird erkannt — der wichtigste Befund überhaupt.  Alle vier UDOS-Fixturen (ZDOS beidseitig, fremde Sync-Sitte, PC 1715, P8000) prüfen ohne Befund. |
| **3** ✅ | `FileSystem::repair()`, Transaktion + Rangfolge, Reparaturdialog, `fsck --repair` | **Umgesetzt 2026-08-19.**  Belegungsplan nachtragen und neu aufbauen, Zähler, Rückwärtszeiger, Kürzen, wilder Blockzeiger, Satzzahl — je Familie, mit E8-Sperre bei offenem Kettenfehler und Rücknahme der ganzen Momentaufnahme bei einem Fehlschlag.  Dialog `ui/fsck_dialog.py` (Strg+F) mit Vorauswahl nach E7, Einzelheiten und Doppelklick in den Diskeditor. |
| **4** ✅ | Ebene Medium: CRC-Übersicht mit Rückabbildung auf Dateien, **Sprung in den Diskeditor** | Die Rückabbildung ist mit den Etappen 1+2 mitgekommen („Satz 14 von `STAT.COM` liegt auf einem Sektor mit falscher CRC", bei UDOS je Datei zusammengefasst).  Der **Sprung** kam 2026-08-19 mit dem Reparaturdialog: Doppelklick auf einen Befund → `MainWindow._befund_im_editor` → `DiskEditorWindow.zeige_ort(cyl, head, sector)`. |
| **5** ✅ | Wiederherstellung CP/M (Verzeichnisplätze + freie Blöcke) mit Dialog und `recover` | **Umgesetzt 2026-08-19.**  Modell `fs_recover.h`, `check/cpm_recover.cpp` (Kandidatensuche, Güte mit Belegen, Lesen mit Auffüllen, Wiedereintragen mit erneuter Vorbedingungsprüfung), C-ABI `k1520d_recover_*`, `recover [--full] [--to] [--list] [--restore N[=NAME]]`, Dialog `ui/recover_dialog.py` (*Diskette ▸ Gelöschte Dateien suchen…*) mit Hexdump-/Textvorschau und den drei Wegen aus §13.3.  Wächter: 11 `FsRecover*`, 3 `cli_dt_recover_*`, 3 GUI-Fälle. |
| **6** ✅ | Wiederherstellung UDOS/NDOS (Kopfsektorsuche, Kettenverfolgung, Beiblatt) | **Umgesetzt 2026-08-19, und zwar rein lesend.**  `check/udos_recover.cpp` und `check/udos1715_recover.cpp`: Signatursuche über Kopfsektor bzw. Descriptor, Kettenverfolgung über Kontrollblock bzw. Zeigersektoren, Rohbereiche, Namensrest als *Vorschlag*, Beiblatt `udos-dateiangaben.txt` aus dem überlebenden Kopfsektor.  **Kein Zurückschreiben auf die Diskette** (§13.3a) — der Weg zurück heißt retten, benennen, `put`.  Nachgewiesen an `udos_boot_scp.hfe` (`NOTE.TO.SD`, 2048 B) und `udos1715_640k_pc1715_system.img` (`ZLINK`, 25 088 B, sechs Segmente): beide bytegleich zurück, beide wieder einspielbar.  Wächter: 8 `FsRecoverUdos*`/`FsRecoverNdos*`, 2 `cli_dt_recover_udos*`. |
| **7** ◐ | Ebene 0 (Ablehnungsgründe der Erkennung) und die Gegenprobe der Alternativprofile | **Ebene 0 umgesetzt 2026-08-19.**  Aus „nichts erkannt" ist eine Diagnose geworden: `erkennung.abgelehnt` je Kandidat, eigene Ebene `FsLayer::Erkennung`, roh öffnendes `check`/`fsck`, Protokoll und Prüfdialog der Oberfläche.  Die **Gegenprobe** bleibt offen (§20). |

Die Etappen 1–3 sind der Kern; 4–7 sind je für sich abschließbar und je für sich
nützlich.  Offen ist allein noch die Gegenprobe der Alternativprofile aus Etappe 7.  `✅` = fertig, `◐` = zum Teil (s. Bemerkung).

---

## 20. Grenzen und offene Punkte

* **Eine Diskette, die nicht mountet, wird nicht geprüft** — außer auf Ebene 0 (§11).
  Bricht bei UDOS die Kette der *Verzeichnisdatei selbst*, scheitert schon `mount()`.
  Das ist grundsätzlich reparierbar (der Kopfsektor der Verzeichnisdatei liegt fest auf
  Spur 22 Sektor 1, die Kette ließe sich aus den Kontrollblöcken verfolgen), verlangt
  aber einen Reparaturweg **ohne** gemountetes Dateisystem. Zurückgestellt; der
  Diskeditor ist heute die Handhabe.
* **Die Gegenprobe der Alternativprofile** (bei `unambiguous == false` jedes
  Kandidatenprofil kurz prüfen und melden, wenn ein anderes sauberer prüft) ist billig
  und nützlich, aber sie kann bei sehr ähnlichen Profilen zu einem Hin und Her führen;
  sie dürfte deshalb **nie die Wahl ändern**, sondern sie nur ansagen.
  **Zurückgestellt (2026-08-19), und zwar aus einem harten Grund: es gibt heute keinen
  Fall, an dem sie prüfbar wäre.** `filesystems:` ist absichtlich eindeutig gehalten —
  `cpa640` wurde 2026-08-11 genau deshalb *entfernt* („er sorgte nur dafür, dass jede
  16×256-Diskette ‚nicht eindeutig' gemeldet wurde"), und keine Fixture im Baum meldet
  Alternativen. Eine ungetestete Gegenprobe im Prüfpfad wäre teurer als ihr Nutzen; sie
  gehört in dieselbe Änderung wie der erste Katalogeintrag, der wieder mehrdeutig ist.
* **`cpm.block.luecke`** (Nullzeiger vor belegtem Zeiger) ist bei CP/M 2.2 fast immer
  ein Schaden, aber eben nicht sicher. Bleibt `Warnung` ohne Reparatur, bis ein echter
  Fall vorliegt.
* **Der Grad, ab dem ein Fragment „Programm" heißt**, ist eine Heuristik und wird als
  solche beschriftet. Kein Befund, keine Reparatur hängt davon ab.
* **Zwei nacheinander gelöschte CP/M-Dateien gleichen Namens werden EIN Fund**
  (Etappe 5). Der Nutzerbereich, der sie unterscheiden könnte, ist ja gerade das
  gelöschte Byte; zusammengefasst wird deshalb über den Namen allein. Auseinander­halten
  ließen sie sich nur über die Blocklisten — was in dem Moment falsch würde, in dem die
  eine die Blöcke der anderen geerbt hat.
* **Höchstens 200 Rohbereiche** meldet die Oberflächensuche (`kMaxRohbereiche`), aus
  demselben Grund wie `FsCheckReport::begrenzen`: eine Liste, die niemand mehr liest,
  ist genau dann wertlos, wenn sie am nötigsten wäre. Der Fall tritt praktisch nur bei
  einer Diskette mit gemischtem Füllmuster ein.
* **Zeitstempel** helfen bei der Wiederherstellung nicht: CP/M 2.2 führt keine, und die
  UDOS-Felder werden bei jeder Änderung überschrieben. Eine Sortierung „zuletzt
  gelöscht zuerst" ist deshalb nicht möglich; sortiert wird nach Ort.
* **Was UDOS selbst beim Löschen tut**, ist an unserer Umsetzung abgelesen und an einer
  echten Diskette bislang nur stichprobenhaft gegengeprüft. Sollte das originale
  `DELETE` den Verzeichnissatz anders kompaktieren, wird die Namensrest-Heuristik aus
  §13 wertvoller als hier angenommen — sie ist genau deshalb schon vorgesehen.
* **Bei UDOS ist die billige Suchtiefe nicht billig.** Bei CP/M ist sie es, weil alles
  Gesuchte im Verzeichnis steht; bei UDOS steht dort nach dem Löschen *nichts* mehr, und
  beide Tiefen müssen die Datenspuren ansehen. Der Unterschied ist, **welche** Sektoren
  als Kandidat gelten (nur die in der Karte freien ↔ alle) und ob Rohbereiche gesammelt
  werden — nicht, wie viele Spuren angefasst werden. An einer physischen Diskette heißt
  das: eine UDOS-Suche kostet dort immer die ganze Scheibe. Umgehen ließe sich das nicht,
  ohne die Suche selbst aufzugeben.
* **Der Weg zurück bei UDOS hat zwei Kanten, die aus `put` stammen**, nicht aus der
  Rettung: das Beiblatt wird über den **Dateinamen** zugeordnet (wer die gerettete Datei
  umbenennt, muss den Schlüssel mit umbenennen), und zwei Kopfsektorfelder überleben den
  Rundlauf `get`/`put` schon vorher nicht — `bytes_in_last == 0` wird beim Schreiben zur
  vollen Satzlänge, und LOW/HIGH/STACK werden nur geschrieben, wenn mindestens einer der
  drei Werte ungleich 0 ist (`udos_fs.cpp`, `write()`). Beides trifft eine extrahierte
  Datei genauso wie eine gerettete; beides bleibt hier unangetastet, weil die
  Null-als-„nicht angegeben"-Regel an anderer Stelle tragend ist (Offset 17 beim
  Nukleus, `doc/udos_diskettenformat.md` §6).
* **Ein NDOS-Descriptor lässt sich nicht an Bytemarken erkennen.** Die beiden festen
  Trennmarken `FF 00` (Offset 1EH und 26H), die ein ZDOS-Kopfsektor trägt, stehen auf
  einer echten PC-1715-Diskette **nicht** dort — dort sind es Nullbytes, und der
  Änderungsvermerk ist unbeschrieben statt druckbar. Verlangt man sie, findet der
  Suchlauf keinen einzigen Descriptor. Getragen wird die Erkennung deshalb strukturell:
  `FIRSTBL` muss auf einen echten Zeigersektor zeigen, dessen erste Eintragung dieser
  Descriptor selbst ist (§6 des NDOS-Formats). Das ist zufällig praktisch nicht zu
  treffen — E10 bleibt gewahrt.

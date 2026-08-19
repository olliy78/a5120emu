<!-- Ausgelagert aus CLAUDE.md am 2026-08-19.  Diese Datei gilt WIE CLAUDE.md,
     sobald an diesem Teilsystem gearbeitet wird — sie ist nur nicht mehr in jeder
     Anfrage geladen.  Begruendung: doc/merkposten/README.md -->

# k1520DiskTool — Merkposten

Zweites Anwenderprogramm neben dem Emulator: holt Dateien von CP/A-, SCPX-, **UDOS**-,
**UDOS1715**- und **SCP1700**-Disketten (CP/M-86, A7100) und schreibt sie zurück (`.img`/`.hfe`/`.dmk`).  Es teilt sich mit dem
Emulator die Container-/Medium-Schicht, hat aber **eine eigene Bibliothek**
(`libk1520disk.so`) ohne Z80 und Karten.  Voller Entwurf: `doc/design/13_k1520disktool.md`,
Bedienung: `tools/k1520disktool.md`.

```
core/filesystem/   SectorSpace (physisch + linear) · GeometryProbe (Erkennung Stufe 1)
                   FsProfile/FsCatalog · CpmFileSystem · UdosFileSystem ·
                   Udos1715FileSystem · DiskVolume
core/api/k1520_disk_api.*   C-ABI  →  libk1520disk.so
tools/k1520disktool.cpp     CLI    →  tools/dev.sh tool k1520disktool ls <abbild>
app/disktool/               PySide6-Oberfläche  →  bash run_disktool.sh
```

Was beim Weiterarbeiten zu wissen ist:

- **SCP1700/CP/M-86 (A7100) — eine Diskette mit ZWEI Datenraten (2026-08-18,
  `doc/scp1700_diskettenformat.md`, Entwurf §22).**  Die Disketten des **A7100**
  tragen ein CP/M-86; das Dateisystem ist gewöhnliches CP/M (Verzeichnis ab
  `c2h0`, 2048-B-Blöcke, 128 Plätze, 16-Bit-Zeiger — Profil `scp1700`).  Die
  **Physik** ist der Punkt: **Spur 0 Kopf 0 ist FM mit HALBER Datenrate**
  (125 kbit/s, 16×128), alle übrigen 159 Spuren MFM mit 250 (16×256).  Das CP/A-BIOS
  weiss davon („A7100-System mit 5" FM …", `biosdsk.mac`).  Vier Festlegungen:
  **(1) Der Abtastfaktor gilt JE SPUR**, nicht je Datei — er wurde an der ersten
  Spur mit Marken festgenagelt, und das war hier die Bootspur: danach kamen alle
  159 MFM-Spuren als „unformatiert" zurück.  Der bewährte Faktor kommt zuerst und
  genügt sich selbst, ein anderer muss **≥ 4 Adressmarken** vorweisen (eine
  einzelne Scheinmarke aus dem Rauschen hatte den Faktor früher schon einmal
  umgeworfen — `TrackSync::completeRead`).
  **(2) Die Rate hängt an der Spur** (`TrackImage::cell_factor`, im Katalog
  `rate: 125`): beim Laden herunterrechnen, beim **Zurückschreiben strecken**
  (`BitCodec::upsampleCells`) — sonst ginge die Bootspur mit doppelter Rate auf die
  Scheibe.  Deshalb bemisst `HfeCodec::save` die Spurlänge in **Zellen**, nicht in
  Bytes ×2.
  **(3) „Überabgetastet" heisst: KEINE Spur liegt auf der Nominalrate** — sonst
  wäre jede gemischte Diskette schreibgeschützt.
  **(3a) HFE verschraenkt zwei Seiten zu je 256 B — auch bei EINSEITIGEN Dateien.**
  Greaseweazle legt `gw read --tracks c=0:h=0` so ab (Seite 0 in den ersten 256 B,
  Rest Gap); dieses Projekt schrieb einseitige Spuren kontinuierlich.  Wer eine
  verschraenkte Datei kontinuierlich liest, zieht sich alle 256 B Gap-Bytes MITTEN
  in den Datenstrom: die kurzen ID-Felder ueberleben das, ein 131-B-Datenfeld nie —
  „alle Sektoren gefunden, keine einzige gueltige Daten-CRC".  Der Leser probiert
  jetzt beide Sitten und entscheidet am Inhalt.  **Ueberhaupt gilt: ein
  Abtastfaktor wird an GUELTIGEN CRCs gemessen, nicht an der Markenzahl** — unter
  dem falschen Faktor faellt reichlich Scheinsync heraus.  Waechter
  `HfeCodec.EinseitigeAufnahmeMitSeitenschlitzen`.
  **(3b) Ein schon defekter Sektor darf defekt zurueckkommen** — das Pruef-Lesen
  verlangte von jedem zurueckgelesenen Sektor eine gueltige Pruefsumme, auch von
  einem, der schon im Abbild kaputt war; damit liess sich eine Spur mit Schadstelle
  NIE zurueckschreiben.  Bei einem Bruchstueck wird nur noch die Lage verglichen.
  Waechter `TrackSync.EinSchonDefekterSektorDarfDefektZurueckkommen`.
  **(4) Verglichen werden VERSCHIEDENE Sektor-IDs** (`MeasuredTrack::uniqueSectors`):
  die Bootspur wurde in einem Zug über den Index hinaus beschrieben und trägt 19
  Adressmarken für 16 Sektoren.  Nebenbefund: der FM-Dekoder begann die Spur am
  Markenbyte und warf dessen Sync-Feld weg — beim ZWEITEN Rundlauf durch die Datei
  verschwand der erste Sektor.  Wächter: `Scp1700.*` (5 Fälle),
  `HfeCodec.FmSpurMitHalberRate_UeberlebtDenRundlauf`.  Am echten Laufwerk
  gegengeprüft.
- **Der Robotron P8000 fährt dasselbe NDOS (2026-08-18,
  `doc/udos1715_diskettenformat.md` §3.0a).**  Eine WEGA-Startdiskette des **P8000**
  (UDOS 2.2, 80×32×256, 250 kbit/s MFM) galt als unlesbar.  Sie ist Feld für Feld eine
  UDOS1715-Diskette; nur ihr Formatierer lässt zwischen Belegungsplan und Zählern den
  **`77H`-Nachlauf der ZDOS-Sitte** stehen (`179H` = `01`), und darauf bestand
  `UdosBitmap::looksValid` als Unterscheidungsmerkmal.  **Das Füllmuster trennt die
  Karten NICHT** — die ZDOS-Kennzeichen `11×33H`/`F7H` liegen auf `150H…15BH` und damit
  in einer 80-Spur-Karte mitten im Belegungsplan, und ZDOS scheitert ohnehin am
  Zählerabgleich (`belegt + frei = Sektoren/Spur · Spuren`, bei ZDOS die Konstante
  2464).  Geprüft wird jetzt nur noch „`00` **oder** `77H`"; `179H` gar nicht mehr.
  Zweite Eigenheit, die man nicht für einen Defekt halten darf: **der Systembereich ist
  grösser** — gesperrt ist Kopf 0 (Sektoren 0…15) der Spuren 0, **21** (Bootspur), 22
  und 23 ganz, Kopf 1 derselben Spuren trägt Dateidaten.  Wächter: `Udos1715P8000.*`
  auf der Fixture `udos1715_640k_p8000_wega.img`.  Lesen **und** Schreiben am echten
  Laufwerk gegengeprüft (Datei einfügen → 4 Spuren zurückgeschrieben und geprüft, frisch
  zurückgelesen byteweise gleich, löschen → 2 Spuren; Vollmessung zeigt genau die
  gemeldeten Spuren geändert, danach aus der Sicherung wiederhergestellt).
- **UDOS1715/NDOS — die zweite UDOS-Ausprägung (2026-08-17,
  `doc/udos1715_diskettenformat.md`, Entwurf §21).**  Die Disketten des **PC 1715**
  tragen dasselbe Betriebssystem, aber ein anderes Dateisystem, weil der **µPD765**
  nichts hinter die Daten-CRC schreiben kann: die Verkettung steht in eigenen
  **Zeigersektoren** (je bis zu 125 Adressen, `FIRSTBL` im Descriptor bei `80H`)
  statt im Gap.  Maßgebliche Quelle ist das **Handbuch auf der Diskette selbst**
  (`doc/original_docs/UDOS1715_Systemhandbuch.txt` = die Datei `UDOS.TEXT`).  Vier
  Festlegungen, die man nicht aufweichen darf:
  **(1) Die Spur ist der ganze ZYLINDER** — `UDOS-Sektor = (ID−1) + Kopf·16`, 32
  Sektoren je Spur, EIN Datenträger (kein `Side0`/`Side1`).  Umgerechnet wird in
  `headOf()`/`idOf()`, und nur dort.  Ein Record darf dabei die **Kopf**grenze
  überschreiten, die Spurgrenze nicht (`CAT` tut es).
  **(2) `.img` ist hier ERLAUBT** — es steht nichts außerhalb der Sektoren; deshalb
  ist auch die Fixture ein 640-KB-`.img` statt eines 2-MB-`.hfe`.
  **(3) Geteilt wird der Descriptor, nicht die Klasse.**  Die ersten 128 Byte sind
  bitgleich mit ZDOS → `UdosFileHeader`/`UdosPointer`/`udosTypeByte`… gemeinsam
  (`udos_fs.h`); dabei zeigte sich, dass das Typbyte ein **Bitfeld Typ+Subtyp** ist
  (`81H` = P/Subtyp 1 = das alte „P1").  `UdosBitmap` bekam eine `UdosMapSitte`
  statt eines Doppels — gleiche Offsets, aber 80 statt 78 Einträge, `00`-Füllung
  statt des ZDOS-Nachlaufs, und **beide Zähler sind bei NDOS echt**.
  **(3a) Der Kopfsektor-Bereich 40…121 ist eine LISTE von Speichersegmenten**,
  kein Wertepaar plus vier rätselhafte Bytes (Handbuch §3.2.2: „mehrere Segmente
  möglich; abgeschlossen mit `00 00 00 00`", `2AH…7FH` nur bei P-Dateien).  Das
  erklärt `doc/udos_diskettenformat.md` §6.3 nachträglich — und es deckte einen
  **Defekt** auf: `IMAGER` (3 Segmente) und `ZLINK` (6) kamen aus `get`→`put`
  verstümmelt zurück.  Seitdem wird die Liste durchgehend geführt
  (`FileEntry::segments`, `WriteOptions::udos_segments`, Beiblatt `segs=`, CLI
  `--segment`, EIN Feld im Eigenschaften-Dialog); `segment_start`/`segment_len` und
  `extra` bleiben nur als Sicht auf das erste Segment.  Bei Typ A steht dort
  Anwenderinhalt — die Liste wird nur für Typ P gelesen.
  **(4) `detect_rank` gilt jetzt über Geometriegrenzen hinweg.**  `cpa640` und
  `k5601_16x256` sind dieselbe Rohgeometrie, und eine frische UDOS1715-Diskette ist
  außerhalb ihrer Systemspuren voller 0xE5 — also ein plausibles leeres CP/M.  Ohne
  die Regel gewann die zuerst gemessene Geometrie.  Wächter: `Udos1715*` (19 Fälle,
  darunter der sektorgenaue Abgleich Belegungsplan ↔ alle 67 Dateien),
  `FsCatalog.Udos1715ProfileSindImgFaehigUndEinseitigGezaehlt`.  Am echten Laufwerk
  gegengeprüft.
- **Bootfähige Disketten (2026-08-12, `doc/design/13_k1520disktool.md` §13a).**  Das
  Werkzeug legt Disketten mit **Bootabbild** an: `create --fs NAME --boot datei.bin`
  (GUI: Rückfrage + Dateiauswahl bei „Neue Diskette", Gegenstück „Bootabbild sichern…"
  = `boot-get`).  Das Abbild ist ein **rohes Byteband** über die Systemspuren, deren
  Umriss je Familie feststeht: CP/M = alles vor `data_cyl`/`data_head` (cpa780: 15104 B),
  UDOS = Spuren 0–2 **plus Bootspur 21** (13312 B je Seite — ohne die Bootspur bricht
  der UDOS-Kaltstart mit `ERROR: 45` ab).  **Geprüft wird VOR dem Formatieren**, sonst
  bliebe bei einem zu grossen Abbild eine halbe Diskette liegen; kürzer ist erlaubt.
  Fertige Abbilder: `disks/boot_{cpa780,scpx640,scpx798,udos43}.bin`.  Wächter
  `test_disktool_bootdiskette` — baut die Diskette mit dem Werkzeug und **bootet sie**
  (CP/A bis `A>`, SCPX in beiden Geometrien, UDOS bis `%`).
- **UDOS-Dateien tragen mehr als ihre Bytes (2026-08-12, `doc/udos_diskettenformat.md`
  §6/§14).**  Der Kopfsektor steuert, wie UDOS eine Datei **lädt**; am Ende (Offset
  122/124/126) stehen **LOW ADDRESS / HIGH ADDRESS / STACK SIZE** — genau das, was
  `EXTRACT` im laufenden System meldet.  Der Lader trägt LOW/HIGH nach `(1275H)/(1277H)`
  und lässt sie vom Speicherverwalter (`1009H`) zuteilen; stehen dort `FFFF`, bricht er
  mit **`MEMORY PROTECT VIOLATION`** ab (Fehler `43H`, Meldungstabelle `13C6H`/`12B2H`,
  Index = A−40H).  Ebenso maßgeblich: **Offset 17** ist NICHT immer die Kopie der
  Satzlänge (bei 256/512 = 0) — mit dem falschen Wert startet ein neu geschriebener
  Nukleus (`OS`) nicht mehr.  Der Kopfsektor ist damit lückenlos zugeordnet; berechnet
  werden beim Schreiben nur Zeiger (6–11), Satzanzahl (13) und Bytes im letzten Satz (22).
  Alles andere führt das Werkzeug mit: `WriteOptions::udos_*` / `UdosAttrs` →
  CLI `put --type/--props/--entry/--record-len/--block-len/--segment/--mem/--extra/
  --created/--date`, `attr` zeigt und ändert sie an einer vorhandenen Datei, und ein
  **Beiblatt** `udos-dateiangaben.txt` (Schlüssel=Wert) trägt sie durch `get`→`put`.
  C-ABI: `k1520d_entry_*` + `k1520d_set_udos_attrs`.
- **UDOS-Bootdisketten laufen (2026-08-13).**  `get` → `create --boot` → `put` ergibt
  eine Diskette, die den Selbststart fährt (`OS.INIT`: Banner, `DATE`) und **Befehle
  ausführt** (`CAT`, `STATUS`, `PRINT`).  Der letzte Stolperstein war: **das
  Speicherabbild einer Programmdatei reicht über ihr logisches Dateiende hinaus** —
  `OS` ist 5504 Byte lang (`bytes_in_last`), sein Abbild 5632 (11 volle Sätze à 512),
  und in den 128 Byte dahinter steht Nukleus-Code, in den er selbst springt (`2580H`).
  Wer auf `length()` kürzt, bekommt eine Diskette, die bootet und beim ersten Befehl in
  den Monitor fällt (`BREAK 4150`).  Deshalb liefert `UdosFileSystem::readChain` **volle
  Sätze**, sobald `segment_len > length()`, und `bytes_in_last` wird mitgeführt
  (`rest=` im Beiblatt) statt ausgerechnet.  Kleinste bootfähige Diskette:
  Systemspuren + `OS` + `ZDOS` (Urlader sucht beide über das VERZEICHNIS).  Wächter:
  `DiskToolBootdiskette.GebauteUdosDisketteBootetUndFuehrtBefehleAus`.
- **Oberfläche = gewöhnliche Anwendung (2026-08-14, `doc/design/13_k1520disktool.md` §20).**
  Die zwei Knopfleisten auf halber Höhe sind weg; das Fenster hat **Menüleiste,
  ausblendbare Symbolleiste** (`Ansicht ▸ Symbolleiste` = Qts eigene
  `QToolBar.toggleViewAction()`), **Kopfbereich**, **Meldungsstreifen**, Statuszeile
  und ein **Protokoll-Dock** (F8, beim Start zu).  Vier Festlegungen, die man nicht
  aufweichen darf:
  **(1) Jede Aktion steht in der Menüleiste** — die Leiste ist nur die Abkürzung und
  ausblendbar; alle Aktionen entstehen EINMAL in `app/disktool/ui/actions.py`
  (`_SPEC` → `fenster.act_<name>`), Menü/Leiste/Kontextmenüs/Mittelspalte zeigen
  dasselbe Objekt.  Wächter `test_every_action_is_reachable_from_the_menu_bar` sucht
  jede `act_*` im Menü — eine neu ergänzte Aktion fällt sofort auf.  Menütext lang,
  Leistentext kurz (`QAction.setIconText`, Tabelle `KURZ`), sonst kippt die Leiste
  bei 1150 px in den Überlauf.
  **(2) Gesperrt wird nur in `_aktionen_pruefen()`**, in drei Stufen: *offen* /
  *schreibbar* / *ausgewählt* (Holen, Schreiben, Löschen, Eigenschaften hängen an der
  Auswahl der ZUSTÄNDIGEN Liste) — damit gibt es „Keine Datei ausgewählt" als
  Meldungsfenster nicht mehr.
  **(2a) Mittelspalte = vier Knöpfe** (`→→| →| |← |←←`: aussen die Stapel, innen die
  Auswahl); beide Hälften sind gleich gebaut und gleich breit (Überschrift + Liste,
  keine Fusszeile).  Der **Schreibschutzknopf zeigt seinen Zustand** — Symbol UND
  Beschriftung wechseln (🔒 `R/O` ↔ 🔓 `R/W`, `_schutz_anzeigen()`); ein rastender
  Knopf allein ist nicht lesbar.
  **(3) Sechs Meldungsorte, sechs Rollen (§20.4):** Titel = Identität + Qt-eigene
  Änderungsmarke (`[*]` + `setWindowModified`, **kein** selbstgemaltes `●`);
  Kopfbereich = dauerhafte Eigenschaften; Streifen (`ui/info_bar.py`) = dauerhafte
  Einschränkungen; Statuszeile links = letzte Aktion (flüchtig), rechts = Zustand als
  Widget (Dateien/frei/Modus/Schloss); Protokoll = **alles** mit Uhrzeit; Meldungs­fenster
  nur bei Abbruch/Rückfrage.  Ein Zustand gehört nie ins Protokoll.  Die **Statuszeile
  ist NICHT abschaltbar** (Symbolleiste und Protokoll schon), und das Schloss darin
  ist ein Bild, kein Emoji — 🔒 und 🔓 sehen in vielen Schriften gleich aus.
  **(4) `QSettings` nur bei benannter Anwendung** — `main.py` setzt
  `setOrganizationName`/`setApplicationName`, die Testläufe nicht; sonst schrieben
  Tests in die Einstellungen des Anwenders und erbten dessen ausgeblendete Leiste
  (`_einstellungen()` → `None`).  Symbole liegen als einfarbige SVG in `app/icons/`
  und werden in `ui/icons.py` mit der Palettenfarbe eingefärbt (`currentColor`) —
  `QIcon.fromTheme()` liefert unter Windows nichts.
  **(5) Das Handbuch ist eine `.md`, die Qt selbst setzt** (§20.7):
  `app/disktool/help/handbuch.md` → `ui/help_window.py` (F1, `QTextDocument::setMarkdown`).
  Kein Bauschritt, keine Abhängigkeit — und die Datei MUSS unter `app/` liegen, weil
  `build_payload.sh` nur diesen Baum einpackt (`doc/` ist nicht im Paket).  Qt vergibt
  Überschriften **keine Anker**, das Inhaltsverzeichnis kommt daher aus den Blöcken mit
  `headingLevel()==2`; Typografie nur über den Umweg `setMarkdown`→`toHtml`→`setHtml`
  mit `defaultStyleSheet`.  Zwei Wächter halten Handbuch und Oberfläche zusammen:
  die Tabelle „Tastenkürzel" wird in BEIDE Richtungen gegen die verdrahteten
  `QAction`s geprüft.
- **Arbeitsverzeichnisse = die des Emulators (2026-08-15, §20.8).**  Alle Dateidialoge
  des DiskTool gingen mit LEEREM Startpfad auf — für Qt das Arbeitsverzeichnis, beim
  installierten Programm also der Installationsordner.  Aufgelöst wird jetzt über
  `app.paths` (dieselbe Stelle wie beim Emulator): Abbilder → `default_disk_dir()`,
  Ordnerseite → **`default_folder_dir()`** = neu `user_files_dir()`
  (`<Datenordner>/Dateien`, Gegenstück zu `Disketten`), „Speichern unter" → neben der
  offenen Diskette.  Drei Fallen: **(1)** `K1520_DISKS` meint nur die ABBILDER und
  verschiebt den Dateiordner nicht (dafür `K1520_DATA`).  **(2)** `ensure_user_files_dir()`
  legt nur in einer INSTALLATION an — wie `seed_user_disks()`; im Quellbaum darf kein
  Ordner im Heimatverzeichnis entstehen.  Beides ruft `app/disktool/main.py` beim Start.
  **(3)** „Nie in der Installation" gilt für die ORDNERseite; bei den Abbildern fällt
  `default_disk_dir()` bewusst auf die mitgelieferten Beispiele zurück, und die liegen
  dort.  Wächter: `test_every_file_dialog_gets_a_start_directory` (kein Dialog ohne
  Startpunkt — im Quellbaum faellt der Fehler sonst nicht auf, weil das
  Arbeitsverzeichnis zufaellig stimmt).
- **Diskeditor — die Diskette als Scheibe (2026-08-13, `doc/design/13_k1520disktool.md` §19).**
  `Diskette ▸ Diskeditor` (Strg+E) → `app/disktool/ui/disk_editor.py`: zwei Scheiben
  (Spur 0 **außen**, Sektor 0 bei **12 Uhr**, Seite 1 gespiegelt), Sektor grün/rot,
  Gap orange, unformatiert grau; Klick **oder** Wählerzeile (`[−] Spur: [25] [+]`,
  Sektorschritt geht in SPURreihenfolge, nicht nach ID) → Hexfeld (32 B/Zeile,
  Überschreibmodus, ASCII-Spalte läuft mit) + CRC-Feld + *Reload/Fix CRC/Save*.
  **`Save Sektor` schreibt bis in die Datei** (`sector_write`+`flush`) — bei einem
  Sektoreditor wäre „nur im Speicher“ eine Falle; Ausnahme mit Ansage: `.img` führt
  kein CRC-Feld, eine absichtlich falsche CRC lässt sich dort nicht ablegen.  Unterbau: `core/peripherals/floppy_drive/track_view.{h,cpp}`
  (`scanTrack` → lückenlose Abschnittsfolge), `parseTrack` liefert jetzt zusätzlich
  **Byte-Offsets + gespeicherte CRCs + `deleted`**, neu `TrackCodec::writeSectorAt`
  und `sectorDataCrc`, C-ABI `k1520d_track_scan`/`k1520d_span_*`/`k1520d_sector_*`.
  Vier Festlegungen: **(1) Der Winkel ist `Byteposition ÷ Spurlänge`** — eine
  `TrackImage` IST eine Umdrehung; Bitrate/Drehzahl aus dem HFE-Kopf werden NICHT
  gebraucht (die Schreibnaht ergibt darum eine sichtbare Spirale, keine Speiche; `.img`
  hat gar keine Winkelinformation).  **(2) Gap ≠ unformatiert** — keine Adressmarke =
  unformatiert (der Zustand von `createBlank`), sonst Gap.  **(3) Geschrieben wird über
  die LAUFENDE NUMMER, nicht über die Sektor-ID** (IDs dürfen doppelt vorkommen).
  **(4) Die CRC ist mitschreibbar** (`crc_woertlich`), sonst liesse sich eine schadhafte
  Diskette nicht originalgetreu nachbilden.  **Sektoren anlegen/löschen (§19.4):**
  `TrackCodec::createSector`/`eraseSectorAt`/`newSectorPosition` — **die ID bestimmt
  die Lage** (hinter den vorhandenen mit der nächstkleineren ID, um den Gap versetzt;
  ohne kleineren hinter den Index).  Daraus folgt: 0,1,5 angelegt ⇒ ein danach
  angelegter Sektor 2 landet ebenfalls hinter der 1 und **überschreibt die 5** — das
  ist gewollt (wer Platz lassen will, gibt bei der 5 einen grösseren Gap an), die
  Oberfläche fragt vorher (`planSector` nennt Ziel, Länge und Betroffene).  Die
  **Spurlänge bleibt fest** (Gap wird überschrieben, `bitcells` bleibt gültig);
  FM/MFM ist an der SPUR, nicht am Sektor — auf einer formatierten Spur gesperrt.
  Gap-Vorschlag = Median der Gaps DIESER Spur.  Dabei zeigt **`sync_pos` jetzt auf
  den Anfang der Sync-Gruppe** (die 00 vor den A1), sonst wichen Anzeige und
  `newSectorPosition` um die Sync-Länge ab.  **§19.5:** bei UDOS nennt die Sektorzeile
  `IBM-MFM + UDOS-Erweiterung`, rechnet `128+4 Byte` (Nutzdaten+Kontrollblock; die CRC zählt wie bei CP/M nicht mit) und entschlüsselt die
  Kettenzeiger — die vier Rohbytes in einem ÄNDERBAREN Feld (`writeSectorTail` fasst dabei weder Nutzdaten noch CRC an); ob es den Anhang gibt, weiss das DATEISYSTEM, nicht der Sektor.
  Der Treffertest der Grafik ist analytisch
  (Polarkoordinaten), nicht per Szenengraph — Wächter
  `test_disk_editor_hit_test_finds_the_drawn_sector` rechnet jeden Sektor zurück; dazu
  `TrackView.*`, `TrackCodecWriteSectorAt.*`, `py_disk_c_api`.  **Grenze:** der Editor
  braucht eine geöffnete (= erkannte) Diskette — „roh öffnen“ gibt es noch nicht.
- **Dateiangaben sehen und ändern (2026-08-13, `doc/design/13_k1520disktool.md` §13c).**
  Rechtsklick/Doppelklick auf eine Datei → **Eigenschaften-Dialog**
  (`app/disktool/ui/properties_dialog.py`): UDOS-Kopfsektor voll editierbar, CP/M
  Nutzerbereich + R/O/SYS/ARC.  Dafür kam **`CpmAttrs` als zweite Überladung** von
  `FileSystem::setAttributes` (nicht eine gemeinsame Struktur — die Familien haben
  fachlich nichts gemeinsam), C-ABI `k1520d_set_cpm_attrs` + `k1520d_entry_bytes_in_last`,
  CLI `attr --ro/--sys/--arc/--user`.  Drei Festlegungen: **(1)** Der Nutzerbereich ist
  IDENTITÄT, kein Attribut — `--user` verschiebt nach `3:NAME.TYP` und wird abgelehnt,
  wenn dort schon eine gleichnamige Datei liegt; geändert werden **alle Extents**.
  **(2) Satzlänge und „Bytes im letzten Satz“ sind nicht änderbar** (sie bestimmen die
  Sektorlage; Weg dahin ist `get` + `put --record-len`) — der Dialog fasst den *Inhalt*
  einer Datei nie an.  **(3)** Geschrieben wird nur, was sich unterscheidet
  (`aenderungen()`), sonst bewegte ein blosses Ansehen das Änderungsdatum.
  Dazu ein **CP/M-Beiblatt `cpm-dateiangaben.txt`** analog zum UDOS-Beiblatt (ohne es
  ging der Nutzerbereich beim Rundlauf `extractAll`→`insertAll` verloren; `zielName()`
  benutzen **`checkFit` und `insertAll` gemeinsam**, sonst urteilt die Platzprüfung über
  einen anderen Namen als die Ausführung).  Das **Archiv** druckt seitdem alle Angaben
  als zweite Tabelle „DATEIANGABEN IM EINZELNEN“ — für die Wiederherstellung von Hand;
  maschinell reichen die Beiblätter im selben Archiv.  Wächter: `CpmFileSystemAttrs.*`,
  `DiskVolume.CpmBeiblatt*`, `py_disktool_gui`.
- **`data/formats.yaml` hat jetzt ZWEI Sektionen.**  `formats:` (Physik, liest der
  Emulator) und `filesystems:` (logische Ebene, liest nur das DiskTool).  `data_start`
  ist dort eine **Spur**, kein Byte-Offset — bei gemischter Geometrie (cpa780: drei
  128-B-Seiten, dann 1024 B) wäre er als Spurzahl gar nicht ausdrückbar; cpmtools trägt
  deshalb `offset 15104` ein, was der `SectorSpace` aus `data_start c2h0` ausrechnet.
  Mehrere Dateisysteme je Geometrie sind möglich (26×128 trägt UDOS *und* CP/M), aber
  selten — die Sektion soll **kurz bleiben** (s. u.).  Neue Formatnamen gehören in die
  Erwartungsliste von
  `FormatCatalog.Formatnamen_SindEinStabilerVertrag` bzw. `FsCatalog.ProfilnamenSind…`.
- **Ein fehlendes Dateisystemprofil ist KEIN Hindernis mehr — `CpaDpbRule` rechnet.**
  `core/filesystem/cpm/cpa_dpb.{h,cpp}` bildet die Formaterkennung des CP/A-BIOS nach
  (`biosdsk.mac`/`drdfrm`, Tabellen `dtrsl0..3`; Analyse: `doc/cpa_format_detection.md`):
  aus Sektorlängencode der Datenspur (**Zylinder 3, Kopf 0** — `dlgint`, einseitig
  adressiert), Spurzahl, ein-/beidseitig und dem Inhalt der Spur 0 entstehen
  Systemspuren, Blockgröße, Verzeichnisplätze und Sektorversatz.  Ein benanntes
  Katalogprofil **gewinnt immer**; die Ableitung ist der Rückfall und heißt `cpa_auto`
  (`--fs cpa_auto` erzwingt sie).  Damit sind **104 von 117** erzeugten Abbildern
  mountbar (vorher 12).  Die Regel reproduziert `cpa780`/`scpx798` exakt und korrigierte
  dabei einen geratenen Wert: **`cpa800` hat 192 Verzeichnisplätze, nicht 128** — am
  laufenden CP/A nachgewiesen (`DiskToolNeueDisketten.CpaFindetDateiJenseitsVonPlatz128`).
  Beim Ändern der Tabellen: `test_cpa_dpb` hält sie gegen `biosdsk.mac`.
- **Doppelschritt (`step: 2`) ist umgesetzt** (2026-08-11, war
  `doc/feature_requests/doppelschritt_disketten.md`).  `tracks:` bleibt **logisch**,
  `DiskFormat::physicalCylinder()` rechnet um; die Spurnummer im **ID-Feld ist die
  logische** (physisch c4h0 meldet `cyl=2`) — sonst verwirft der Gast-Treiber jeden
  Sektor.  Berührt `SectorSpace` (Slot kennt beide Nummern), `ImgCodec` (`.img` ist
  logisch), `DiskImage::create` (ungerade Zylinder bleiben unformatiert), `GeometryProbe`
  (die Lücken sind ein **positives** Kriterium, sonst würde eine gewöhnliche
  40-Spur-Diskette verwechselt) und `formatFitsDrive` (physische Ausdehnung).
  Guards: `ctest -R Doppelschritt` + `DiskToolNeueDisketten.CpaLiestDoppelschrittDiskette`.
- **Ein fehlender `formats:`-Eintrag ist auch kein Hindernis mehr.** Passt keine
  Katalogsgeometrie, baut `GeometryProbe::synthesize()` eine aus der Messung
  (`detection().format == "(gemessen)"`) — Spurbereiche als echte **Rechtecke** (erst
  Zylinder mit gleichem Kopf-Muster, dann die Köpfe; sonst bekäme cpa780 einen Bereich,
  den es nicht gibt), Lückenmuster als `step: 2`. **Ein so gelesener Datenträger ist
  unaufhebbar schreibgeschützt** (`setReadOnly(false)` verweigert,
  `readOnlyForced()`) — die Geometrie ist geraten, nicht belegt. Abgewiesen wird
  weiterhin, was keinen zusammenhängenden Sektorraum ergibt (Loch mitten im
  beschriebenen Bereich, uneinheitliche Sektorgrößen INNERHALB einer Spur).
  Dabei fiel eine alte Schwäche auf: „zu wenige Sektoren" war ein Schaden **ohne
  Obergrenze**, sodass 7×512 als „k5601_ss40_9x512 mit 40 defekten Spuren" durchging —
  jetzt ist mehr als ein Viertel abweichender Spuren ein anderes Format (Regel 4b).
- **`filesystems:` soll KURZ bleiben.** Vier der fünf CP/M-Profile rechnet `CpaDpbRule`
  bitgleich nach; sie stehen nur noch da, weil `create --fs NAME` einen Namen braucht und
  „cpa780" die bessere Auskunft ist als „cpa_auto". Ein neuer Eintrag braucht einen
  eigenen Grund. `cpa640` (Dateisystem ab Spur 0) wurde 2026-08-11 **entfernt**: CP/A
  kann so eine Diskette nicht erzeugen (`dtrsl1` hat ein FESTES Offset von 4 log.
  Spuren), der Eintrag machte nur jede 16×256-Diskette „nicht eindeutig". Guard:
  `FsCatalog.SechzehnMalZweihundertsechsundfuenfzigHatNurEinProfilAbZylinderZwei`.
- **Wächter „alle Formate sind mountbar"**:
  `DiskVolume.JedesKatalogformatLaesstSichAnlegenUndWiederOeffnen` legt JEDES
  `formats:`-Format an, öffnet es ohne `--fs` und prüft die Wiedererkennung.  Ein neuer
  Katalogeintrag, den die Erkennung nicht wiederfindet, fällt sofort auf.
- **`TrackCodec::writeSector`** ersetzt ein Datenfeld an Ort und Stelle und rechnet die
  CRC neu.  `buildTrack()` taugt zum Schreiben **nicht**: es baut die Spur neu und
  verlöre die Bytes hinter der Daten-CRC — bei UDOS die gesamte Dateiverkettung.
- **UDOS: jede Seite ist ein eigenes Dateisystem**, für den Anwender aber EINE Diskette.
  `DiskVolume` führt beide als `Side0`/`Side1`; `extractAll` legt die Unterverzeichnisse
  an, `insertAll` verlangt sie.  UDOS auf `.img` ist unmöglich (Kontrollblock hinter der
  Daten-CRC) und wird abgelehnt.
- **Stapeloperationen sind Transaktionen**: erst planen und urteilen, dann schreiben;
  ein Fehler rollt die Momentaufnahme des `DiskMedium` zurück.  `list()` liest **immer**
  frisch aus dem Medium — es gibt keinen zwischengespeicherten Verzeichnisstand.
- **Kreuzproben statt Selbstbestätigung** (`ctest -R DiskTool.*Roundtrip`, Label
  `format_integration`): geschrieben wird mit dem Werkzeug, gelesen vom **laufenden
  CP/A** (`TYPE`/`DIR`) bzw. **UDOS** (`CAT`/`PRINT`/`STATUS`).  Der CP/M-Lesepfad ist
  zusätzlich byteweise gegen `cpmtools` verifiziert (nicht als Abhängigkeit — die
  Prüfsummen im Test frieren das Ergebnis ein).

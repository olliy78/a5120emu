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
- **Der Robotron P8000 fährt dasselbe NDOS — ein ANDERER Rechner, nicht eine
  Spielart des PC 1715 (2026-08-18, `doc/udos1715_diskettenformat.md` §3.0a).**
  Die beiden Maschinen sind unverwandt (der P8000 startet mit dieser Diskette sein
  Hauptsystem **WEGA**); geteilt wird allein die Sitte, nach der eine Diskette
  angelegt ist.  Das Dateisystemprofil heißt trotzdem `udos1715` — nach dem Ort der
  Entschlüsselung, nicht nach der Maschine.  Eine WEGA-Startdiskette des **P8000**
  (UDOS 2.2, 80×32×256, 250 kbit/s MFM) galt als unlesbar.  Sie ist Feld für Feld eine
  NDOS-Diskette; nur ihr Formatierer lässt zwischen Belegungsplan und Zählern den
  **`77H`-Nachlauf der ZDOS-Sitte** stehen (`179H` = `01`), und darauf bestand
  `UdosBitmap::looksValid` als Unterscheidungsmerkmal.  **Das Füllmuster trennt die
  Karten NICHT** — die ZDOS-Kennzeichen `11×33H`/`F7H` liegen auf `150H…15BH` und damit
  in einer 80-Spur-Karte mitten im Belegungsplan, und ZDOS scheitert ohnehin am
  Zählerabgleich (`belegt + frei = Sektoren/Spur · Spuren`, bei ZDOS die Konstante
  2464).  Geprüft wird jetzt nur noch „`00` **oder** `77H`"; `179H` gar nicht mehr.
  Zweite Eigenheit, die man nicht für einen Defekt halten darf: **der Systembereich ist
  grösser** — gesperrt ist Kopf 0 (Sektoren 0…15) der Spuren 0, **21** (Bootspur), 22
  und 23 ganz, Kopf 1 derselben Spuren trägt Dateidaten.  Wächter: `Udos1715P8000.*`
  auf der Fixture `udosP8000_640k_wega.hfe` — sie liegt als `.hfe` vor, weil 13
  ihrer Sektoren hinter der Daten-CRC die **Schreibnaht** eines überschriebenen Sektors
  tragen und `rawCompatible()` dafür zu Recht `.img` verweigert (anders als beim
  PC 1715, dessen Fixture ein `.img` ist).  Lesen **und** Schreiben am echten Laufwerk
  gegengeprüft (Datei einfügen → 4 Spuren zurückgeschrieben und geprüft, frisch
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
  **(2b) Die Ordnerseite ist ein kleiner DATEIBROWSER (§20.10)** — Adresszeile
  (editierbar, Eingabetaste wechselt) + Ordnerknopf oben, `..` als erste Zeile,
  Verzeichnis öffnen beim Aktivieren (`itemActivated`, also Doppelklick/`Enter` —
  und einfacher Klick, wo das Thema es so vorsieht), Rücktaste = hinauf.  Beim Start
  steht der **Standardordner** darin; den Zustand „kein Ordner gewählt" gibt es nicht
  mehr.  Zwei Dinge nicht aufweichen: **`SideN/` bleibt die EINZIGE aufgeklappte
  Gruppe** (jeder andere Ordner ist ein Wegpunkt — sonst stünde in einem
  Heimatverzeichnis der Inhalt sämtlicher Unterordner), und **navigiert wird nur beim
  Aktivieren, nicht bei der Auswahl**: ein Einfachklick auf `Side0/` muss die Gruppe
  auswählen können, denn daran hängt `selected_side()` = die Zielseite beim Schreiben.
  Die Kopfzeilen BEIDER Hälften werden auf dieselbe Höhe gebunden (`_baue_mitte`),
  sonst begänne die rechte Liste tiefer als die linke.
  **(2c) Anlegen/Umbenennen im Ordner + der leere Fall (§20.11).**  Links steht
  „Keine Dateien gefunden" **im Hintergrund des leeren Feldes** (`_Tree.paintEvent`),
  sobald eine GEÖFFNETE Diskette nichts hergibt (leer formatiert oder roh geöffnet) —
  keine Platzhalterzeile, die sich auswählen und mitzählen liesse; ohne Diskette bleibt
  es leer.  Rechts legt `Neuer Ordner` (Strg+Umschalt+N) `neu`/`neu2`/… an und öffnet
  sofort das Eingabefeld, `Umbenennen` (F2) tut es an einem vorhandenen Eintrag.
  Drei Fallen: der Baum braucht **`NoEditTriggers`** (sonst öffnet der navigierende
  Doppelklick das Feld), `itemChanged` darf **nur für die gemerkte Zeile** gelten
  (es kommt auch beim Listenaufbau), und der **Schrägstrich am Ordnernamen ist
  Darstellung** — er gehört nicht ins Eingabefeld.  Fehlschläge (Name vergeben,
  `/` im Namen, Fehler des Wirtsystems) gehen über `hinweis` in Statuszeile und
  Protokoll; überschrieben wird nie.
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
- **Von der Datei zu ihren Bytes — `firstSector`** (2026-08-22, §7.1b).  Rechtsklick
  auf eine Datei → *Im Diskeditor öffnen* springt auf ihren ersten Sektor (CP/M: erster
  Blockzeiger des KLEINSTEN Extents; UDOS/NDOS: der Kopfsektor aus dem
  Verzeichniseintrag, ohne Spurzugriff).  Geliefert wird die Sektor-**Kennung**, nicht
  der Versatz.  **Falle:** `directory()` gibt den Vektor als WERT zurück — ein Zeiger
  hinein zeigt nach der Schleife ins Leere und lieferte für jede CP/M-Datei stumm
  „nichts" (deshalb eine Kopie).  Dazu sind die UDOS-Zeiger im Diskeditor jetzt
  **Verweise** (`zurück:`/`vor:` anklickbar, Kettenende bleibt Text) — die Sätze einer
  UDOS-Datei liegen verstreut, und die Kette war vorher nur durch Abtippen zu
  verfolgen.  Wächter: `test_eine_datei_laesst_sich_im_diskeditor_aufschlagen`,
  `test_die_udos_zeiger_im_diskeditor_sind_verweise`.
- **Stimmt die angesagte Dateigrösse? — `cpm.dir.groesse`** (2026-08-21, §7.1a).
  Bei CP/M steht die LAENGE im Verzeichnis (EX + RC), die DATEN stehen in den
  Blockzeigern; laufen beide auseinander, merkt es **niemand**: `CpmFileSystem::read`
  füllt einen leeren Zeiger mit Nullen und schneidet auf die angesagte Länge — die
  Datei kommt in voller Grösse heraus, teilweise erfunden.  Nachgestellt (6 von 8
  Zeigern genullt): `check --full` sagte „ohne Befund", `get` lieferte 14464 Byte mit
  10368 Byte Nullen.  Jetzt rechnet die Prüfung je Platz `belegte Zeiger` gegen
  `aufgerundet(angesagt / Blockgrösse)` — zu wenig = Fehler (Vorschlag
  `cpm.rc.anpassen`, als Datenverlust gekennzeichnet), zu viel = Warnung.  Drei
  Fallen: der Abgleich läuft schon in der **Schnellprüfung** (er braucht nur das
  Verzeichnis), `belegte == 0 && RC > 0` bleibt `cpm.dir.leer`, und bei **`RC > 128`
  wird gar nicht gerechnet** (das ist `cpm.dir.rc`, sonst zwei Befunde für einen
  Schaden — genau das deckte `FsCheckReparatur.CpmSatzzahlWirdAngepasst` auf).
  Bei UDOS/NDOS gab es den Abgleich längst (`udos.kette.bruch`, `ndos.zeiger.anzahl`).
  Wächter: `FsCheckCpmSchaden.AngesagteGroesseOhneDeckung` (fährt bis zum `extract`
  und vergleicht die Bytezahl).
- **Die Checkliste — und beide Dialoge rechnen beim Öffnen los** (2026-08-21,
  `doc/design/15_dateisystempruefung.md` §5a).  Der Prüfdialog zeigte vorher nur den
  Befund der Schnellprüfung vom Mounten, der Suchdialog stand auf „Verzeichnisreste":
  an einer gesunden Diskette taten beide sichtbar **nichts**.  Jetzt läuft beim Öffnen
  die Vollprüfung bzw. die Oberflächensuche (an einer Datei ~70 ms), und oben steht
  eine Checkliste — eine Zeile je Schritt, mit Haken, Ergebnis und, bei einem
  übersprungenen Schritt, dem Grund.  Fünf Festlegungen: **die Schritte kommen aus dem
  Prüfer** (`FsSchritt`, `bericht.schritt(id, titel)`), nicht aus der Oberfläche;
  **gezählt wird eifrig** in `FsFindings::add` bzw. `FsRecoverReport::hinzu` (die
  vorzeitigen `return bericht;` und das spätere `sortieren()` machten jede
  Nachrechnung über Indexbereiche kaputt); **ein übersprungener Schritt bleibt
  sichtbar**; `uebernimm()` **führt gleiche Kennungen zusammen** (sonst steht jede
  Zeile bei einer zweiseitigen UDOS-Diskette doppelt); und **E2 gilt weiter fürs
  ÖFFNEN der Diskette**, nicht für den Dialog — bleibt das Abbild unvollständig
  (physische Diskette), fällt es auf die Schnellprüfung zurück.  Wächter:
  `FsCheckCheckliste.*`, `FsRecoverCheckliste.*`, `py_disktool_gui`.
- **Dateisystempruefung (`fsck`) — Grundlagen und CP/M, Etappe 1** (2026-08-18,
  `doc/design/15_dateisystempruefung.md`).  `core/filesystem/check/` liefert das
  Modell (`FsFinding`/`FsSeverity`/`FsLayer`/`FsCheckReport`) und den CP/M-Pruefer;
  `FileSystem::check(level, nachladen)` ist der Haken, `DiskVolume::check()` bündelt
  über alle Volumes.  Fünf Festlegungen, die man nicht aufweichen darf:
  **(1) Die Pruefung schreibt NIE** — sonst wäre die Automatik beim Öffnen ein
  Schreibzugriff auf eine schreibgeschützt geöffnete Diskette.  Reparaturen sind ein
  eigener Schritt (Etappe 3) und gehören in `FileSystem::repair`, nicht in den Prüfer:
  die Klasse kennt ihre Invarianten.
  **(2) Beim Öffnen läuft nur die SCHNELLpruefung** (`DiskVolume::oeffnenMit` ganz am
  Ende, `nachladen=false`).  Sie sieht ausschliesslich die Verwaltungsstrukturen an,
  die `mount()` ohnehin gelesen hat — an einer physischen Diskette kostet sie damit
  **keinen einzigen zusätzlichen Spurzugriff** (0,5–0,8 s je Spur!).  Der Schnitt geht
  durch **Bytes**, nicht durch Spuren: das cpa780-Verzeichnis endet nach 4096 von
  5120 Byte mitten in c2h0.
  **(3) `FsSeverity::Gefahr` heisst „der NÄCHSTE Schreibvorgang zerstört Daten"**,
  nicht „Daten sind verloren".  Bei CP/M ist das genau ein Fall — derselbe Block in
  zwei Verzeichnisplätzen (es gibt keinen gespeicherten Belegungsplan, „verlorener
  Block" ist dort strukturell unmöglich).  Der Grad verdrängt im Meldungsstreifen
  jede andere Meldung.
  **(4) Die Kennungen sind ein VERTRAG** (`"cpm.block.doppelt"`), wie die
  Formatnamen — sie stehen in `--json`, in Skripten und in den Tests.
  **(5) Falschmeldungen sind schlimmer als fehlende.**  Erster Wächter ist deshalb
  `FsCheckKeineFalschmeldungen.*`: jede unversehrte Fixture **und** jedes anlegbare
  CP/M-Katalogprofil prüft ohne Befund ab Schwere `Warnung`.  Ausnahme mit Beleg: die
  A7100-Fixture ist wirklich beschädigt (Bootspur-Sektor 5 CRC, Sektor 10 fehlt) —
  dafür gibt es einen eigenen Fall.  Aus demselben Grund sind CP/M-3-Zeitstempel- und
  Kennwortsätze nur ein **Hinweis**, und Systemspuren werden **physisch** durchgegangen
  (`SectorSpace::trackSectors`), weil eine Bootspur eine ID doppelt tragen darf.
  Bedienung: `check [--full]` (auch `--json`), C-ABI `k1520d_check`/`k1520d_finding_*`
  (der alte Textbericht heisst jetzt **`k1520d_check_report`**), in der Oberfläche
  Statuszeile + Streifen + Protokoll + *Diskettenangaben…*.
- **Etappe 2: ZDOS und NDOS werden geprüft** (2026-08-19, Entwurf §9/§10) —
  `core/filesystem/check/udos_check.cpp` und `udos1715_check.cpp`.  Hier kann eine
  Prüfung wirklich etwas **beweisen**, weil UDOS zwei unabhängige Darstellungen
  derselben Wahrheit führt: den gespeicherten Belegungsplan und die selbsttragende
  Verkettung (ZDOS im Sektorkontrollblock, NDOS in Zeigersektoren).  Vier
  Festlegungen:
  **(1) Die Systemspuren 0–2 und die Bootspur werden NICHT geprüft** — sie sind
  Sitte, nicht Struktur, und an echten Datenträgern uneinheitlich belegt (Seite 0 von
  `udos_boot_scp.hfe`: Spur 0 nur Sektoren 1–3, Spur 1 die Sektoren 1–6 **und**
  17–24; Seite 1 von `udos_ds77_k5601_fremdsync.hfe`: Spuren 0–2 völlig frei).
  `udos.karte.system` prüft nur, was ABLEITBAR ist: die Kartensektoren selbst und die
  Verzeichnisdatei.  Aus demselben Grund bleiben die reservierten Spuren beim Befund
  `belegt_aber_frei` außen vor.
  **(2) Der Rückwärtszeiger des ERSTEN Satzes nennt den Kopfsektor**, nicht `FFFF`.
  Mit `FFFF` als Erwartung meldete die Prüfung jede gesunde Datei der
  Referenzdiskette — 42 Warnungen auf einer fehlerfreien Diskette.
  **(3) Der Zählerabgleich ist aus `info()` in die Prüfung gewandert**
  (`udos.karte.zaehler`); bei ZDOS gilt nur der Freizähler (der „belegt"-Zähler ist
  der Festwert 2464 − frei aus `FORMATPC.MAC`), bei NDOS sind beide echt.  Nebenbei
  ist er der **billige Schatten** des teuren Befundes: nach einem gelöschten
  Kartenbit schlägt schon die Schnellprüfung an, auch wenn erst die Vollprüfung sagen
  kann, welche Datei betroffen ist.
  **(4) Gleichartige Befunde werden begrenzt** (`FsCheckReport::begrenzen`, 20 je
  Kennung, dann eine Sammelzeile) — sonst brächte eine wirklich kaputte Diskette
  fünfhundert Zeilen hervor, und der Bericht wäre genau dann wertlos, wenn er am
  nötigsten ist.
- **Etappe 3+4: Reparatur und der Sprung in den Diskeditor** (2026-08-19, Entwurf
  §12/§16.3).  `FileSystem::repair(const FsRepair&)` je Familie,
  `DiskVolume::applyRepairs()` als Klammer, C-ABI `k1520d_repair_*` +
  `k1520d_apply_repairs`, CLI `fsck [--full] [--repair[=alle|sicher|<kennung>,…]]
  [--dry-run]`, Dialog `app/disktool/ui/fsck_dialog.py` (Strg+F).  Fünf
  Festlegungen:
  **(1) Der ganze Lauf ist EINE Transaktion in fester Rangfolge** — Verzeichnis →
  Ketten → Belegungsplan → Zähler.  Der Neuaufbau des Plans leitet sich aus den
  Ketten ab; in der umgekehrten Reihenfolge bliebe der abgeschnittene Platz
  verloren.  Danach wird **automatisch neu geprüft**, und jeder bisherige Index ist
  hinfällig (die Oberfläche baut ihre Liste komplett neu auf).
  **(2) Der Neuaufbau bleibt gesperrt, solange ein Kettenfehler offen ist** (E8) —
  aus halbem Wissen einen Plan zu bauen heisst, die ungelesene Hälfte für frei zu
  erklären.  Der gesperrte Vorschlag bleibt SICHTBAR und nennt seinen Grund.
  **(3) Vorausgewählt wird nur `empfohlen && !datenverlust`** — in der CLI wie im
  Dialog.  `--repair=alle` nimmt auch die verlustbehafteten; die Fusszeile nennt
  **beide** Zahlen (möglich ≠ ausgewählt), sonst stand „0 Reparaturen" über einer
  Zeile, die eine anbot.
  **(4) Ein CP/M-Blockzeiger wird nie mitten aus der Liste gestrichen, sondern ab
  dort ABGESCHNITTEN** (`cpm.zeiger.streichen`, `cpm.kreuz.erstem_lassen`).  Ein
  Loch verschöbe jeden folgenden Satz — der Extent lieferte danach falsche Daten
  aus statt weniger, und die Prüfung meldete prompt `cpm.block.luecke`.
  **(5) Der Sprung in den Diskeditor** (E9) geht über
  `DiskEditorWindow.zeige_ort(cyl, head, sector)`; `sector` ist die **Kennung** des
  Sektors, nicht seine laufende Nummer auf der Spur.  Und er hängt den Editor,
  solange der modale Dialog steht, **unter den Dialog**
  (`MainWindow._editor_an_modalen_dialog`, 2026-08-21): ein modaler Dialog sperrt
  jedes Fenster derselben Anwendung, das nicht unter ihm hängt — der Editor ging
  sonst hinter das Hauptfenster und nahm keine Eingabe an, also war „Im Diskeditor
  zeigen" erst NACH der Entscheidung zu gebrauchen.  Beim `finished` des Dialogs
  wird er ans Hauptfenster zurückgehängt (Lage und Sichtbarkeit von Hand gerettet,
  `setParent` nimmt beides), sonst stürbe er mit einem Dialog, den er überleben
  soll.  Gemessen wird am `WindowBlocked`/`WindowUnblocked` des Editors, nicht an
  der Elternschaft — die ist nur das Mittel
  (`test_der_diskeditor_ist_neben_einem_modalen_dialog_bedienbar`).
  Wächter: `FsCheckReparatur.*` (12), `cli_dt_fsck_*` (4, mit der neuen
  Schadensinjektion `poke:` im `.cli`-Prüfstand), `py_disktool_gui`.
  Alle vier UDOS-Fixturen prüfen mit `--full` **ohne Befund** (Ketten, Kreuzbelegung,
  Karte↔Ketten, CRC, Nachspann).  Wächter: `FsCheckUdosSchaden.*` (8) und
  `FsCheckNdosSchaden.*` (5) mit gezielter Schadensinjektion.
- **Etappe 5: Rettung gelöschter CP/M-Dateien** (2026-08-19, Entwurf §13).
  Modell `core/filesystem/check/fs_recover.{h,cpp}`, Suche `check/cpm_recover.cpp`,
  Haken `FileSystem::recoverScan/recoverRead/recoverRestore`, Klammer
  `DiskVolume::recoverScan/recoverRead/recoverExtract/recoverRestore`, C-ABI
  `k1520d_recover_*`, CLI `recover [--full] [--to ordner] [--list]
  [--restore N[=NAME]]`, Dialog `app/disktool/ui/recover_dialog.py`.  Sechs
  Festlegungen:
  **(1) Retten geht vor Wiederherstellen** (E7).  Der Vorgabeweg ist *in den
  Linux-Ordner holen*, und er ist an einer schreibgeschützten Diskette voll
  bedienbar — der übliche Fall ist „einmal alles retten, dann die Diskette in Ruhe
  lassen".  `restorable` (auf der Diskette eintragen) ist etwas ANDERES als rettbar.
  **(2) Die billige Suchtiefe fasst keine Datenspur an.**  `FsRecoverLevel::
  Verzeichnis` liest nur den Verzeichnisbereich; auch die Prüfsummenkontrolle der
  Fundblöcke läuft erst in der Oberflächensuche.  Ohne diese Sperre zöge eine
  „billige" Suche an einer physischen Diskette die ganze Scheibe ein.
  **(3) Bei CP/M überlebt der Name, aber NICHT der Nutzerbereich** — er stand in
  ebendem Byte, das `0xE5` geworden ist.  Zusammengefasst wird deshalb über den
  Namen allein, und eingetragen wird nach Bereich 0.
  **(4) Vor dem Schreiben werden die Vorbedingungen NOCH EINMAL geprüft**: Platz
  noch frei, Name noch derselbe, kein Block inzwischen an eine lebende Datei
  vergeben, kein gleichnamiger Eintrag.  Sonst erzeugte die Rettung genau den
  `cpm.block.doppelt`, den die Prüfung als **Gefahr** meldet.
  **(5) Ein Block aus EINEM immer gleichen Byte ist Füllmuster, kein Inhalt** —
  unabhängig davon, welches (FORMAT.COM füllt je nach Menüpunkt mit `0xE5`, `0xF6`
  oder dem Prüfmuster `0x53`).  Ohne diese Regel meldet die Oberflächensuche die
  halbe Diskette als „Bruchstück".
  **(6) Zwei Abweichungen vom Entwurf**, beide bewusst: Rohbereiche heissen
  `fragment_c12h0_b40-b47.bin` (Ort **und** Blockspanne — die Sektorspanne aus
  §13.3 wäre falsch, sobald ein Lauf über eine Spurgrenze geht), und das Beiblatt
  eines Fundes mit Vorbehalt liegt als `<datei>.rettung.txt` neben der geretteten
  Datei.  `cpm.medium.frei_beschrieben` bleibt **unvergeben**: freie Blöcke mit
  Inhalt sind kein Schaden, sondern ein Fund — sie stehen in der Suche, nicht im
  Prüfbericht.
  Wächter: `FsRecover*` (11, darunter „eine frisch angelegte Diskette hat nichts zu
  retten" über ALLE CP/M-Katalogprofile), `cli_dt_recover_*` (3), drei
  `py_disktool_gui`-Fälle.
- **Etappe 6: Rettung gelöschter UDOS-/NDOS-Dateien** (2026-08-19, Entwurf §13.1
  und §13.3b).  `check/udos_recover.cpp` (ZDOS) und `check/udos1715_recover.cpp`
  (NDOS), dazu der Haken `FileSystem::recoverEntry` und das Beiblatt in
  `DiskVolume::recoverExtract`.  **Rein lesend.**  Sechs Festlegungen:
  **(1) Es gibt kein Zurückschreiben auf die Diskette.**  Bei CP/M ist es ein Byte
  und der Name stimmt; bei UDOS wären es drei Schreibzugriffe (Karte, Verzeichnis,
  Rückwärtszeiger) für eine Datei, deren Name ohnehin erfunden werden muss.  Der Weg
  zurück heisst *retten → benennen → `put`*.  `recoverRestore` ist überschrieben,
  damit die Absage diesen Weg NENNT statt nur abzusagen (Wächter
  `cli_dt_recover_udos_kein_restore` prüft auf das Wort `put`).
  **(2) Verloren ist allein der Name.**  Typ, Eigenschaften, ENTRY, Satzlänge,
  Blocklänge, alle Segmente, LOW/HIGH/STACK und beide Datumsvermerke stehen
  unverändert im Kopfsektor.  `recoverExtract` schreibt sie in dasselbe Beiblatt
  `udos-dateiangaben.txt`, das `extractAll` anlegt — erst damit ist der Weg zurück
  vollständig.  Zugeordnet wird über den DATEINAMEN: wer die gerettete Datei
  umbenennt, muss den Schlüssel im Beiblatt mit umbenennen.
  **(3) `FsRecoverFind::name` bleibt bei UDOS IMMER leer.**  Was der Dialog zeigt,
  ist `vorschlag` — ein Namensrest hinter dem `FF`-Ende eines Verzeichnissatzes
  (falls einer überlebt hat) oder `GERETTET.001`.  Ein erfundener Name darf nie wie
  eine gesicherte Angabe aussehen.
  **(4) Die Systemspuren werden NICHT übersprungen.**  `NOTE.TO.SD` der
  Referenzdiskette hat ihren Kopfsektor auf **Spur 21**, weil Seite 1 eine reine
  Datenseite ohne Urlader ist — dieselbe Lehre wie bei der Prüfung: *Systemspuren
  sind Sitte, nicht Struktur.*  Ausgeschlossen wird nur, was die Karte im
  Bootbereich als belegt führt.  Wächter
  `FsRecoverUdos.EinKopfsektorAufDerBootspurWirdGefunden` — ohne ihn ist der Fehler
  stumm.
  **(5) Bei NDOS trägt die Bytesignatur nicht.**  Die `FF 00`-Marken bei 1EH/26H
  sind eine A5120-Sitte; auf einer echten PC-1715-Diskette stehen dort Nullbytes und
  der Änderungsvermerk ist unbeschrieben.  Getragen wird die Erkennung strukturell:
  `FIRSTBL` → Zeigersektor → dessen **erste Eintragung muss der Descriptor selbst
  sein**.
  **(6) Rohbereiche enden an der Spurgrenze** (`fragment_c12h0_s6-s26.bin`) — sonst
  nennt der Dateiname eine Sektornummer, die auf einer anderen Spur liegt.  Und
  **beide Suchtiefen fassen bei UDOS die Datenspuren an**: nach dem Löschen steht im
  Verzeichnis nichts Gesuchtes mehr (Entwurf §20).  An einer physischen Diskette
  kostet das **einmal** die ganze Scheibe — danach liegt sie im `DiskMedium`, und
  jeder weitere Lauf ist so schnell wie an einer Datei.
- **0 ist eine ANGABE, keine Abwesenheit** (2026-08-21).  Der Schreibpfad las bei
  drei UDOS-Kopfsektorfeldern die 0 als „nicht angegeben" und setzte einen
  Ersatzwert.  Bei `block_len` (Offset 17) war das lange bekannt und mit einem
  eigenen Kennzeichen gelöst (`udos_block_len_gesetzt`) — die beiden anderen fehlten:
  **„Bytes im letzten Satz"** (Offset 22; aus 0 wurde die volle Satzlänge) und
  **LOW/HIGH/STACK** (Offset 122/124/126; waren alle drei 0, lief der Schreibblock
  gar nicht und der Kopfsektor behielt seine **0xFF**-Vorbelegung — aus 0000 wurde
  FFFF).  Letzteres ist bei einer PROGRAMMdatei ein echter Schaden: UDOS weist sie
  mit `MEMORY PROTECT VIOLATION` ab, und die eigene Prüfung meldet
  `udos.kopf.speicher`.  Auf den vier Referenzdisketten trifft es 30 Dateien, davon
  keine vom Typ P — deshalb fiel es nie auf.  Jetzt `udos_bytes_in_last_gesetzt` und
  `udos_mem_gesetzt`, gesetzt vom Beiblattleser, wenn der Schlüssel dasteht.
  (NDOS war bei LOW/HIGH/STACK nie betroffen: sein Descriptor ist mit 0x00 vorbelegt
  und die Felder werden immer geschrieben — die Satzrest-Zeile hatte es aber auch.)
  **Warum es so lange unentdeckt blieb, ist die eigentliche Lehre:** der vorhandene
  Rundlauftest verglich nur den DATEIINHALT.  Der neue Wächter
  `DiskVolume.UdosRundlaufErhaeltAuchDieNullenImKopfsektor` vergleicht die
  Kopfsektorangaben über alle Dateien der Referenzdiskette und stellt vorab sicher,
  dass überhaupt Dateien mit Nullwerten dabei sind — sonst liefe er an der Sache
  vorbei.
- **Gleichnamige geloeschte CP/M-Dateien: an der EXTENT-NUMMER trennen**
  (2026-08-21).  Gruppiert wird ueber den Namen — der Nutzerbereich ist ja das
  geloeschte Byte.  Zwei nacheinander geloeschte Dateien gleichen Namens wurden
  dadurch zu EINEM Fund zusammengeworfen: gemessen 7424 B, Guete „sicher", die
  ersten 2048 B aus der anderen Datei.  **Die Extent-Nummer kommt innerhalb einer
  Datei genau einmal vor** — wiederholt sie sich, sind es beweisbar zwei Dateien.
  Getrennt wird in der Reihenfolge der Verzeichnisplaetze; der zweite Fund heisst
  `NAME.2` (sonst ueberschreibt „alles retten" den ersten).  Hat jede der beiden
  mehrere Extents, steht die Zuordnung der FOLGEplaetze nicht fest — dann Guete
  hoechstens *wahrscheinlich* mit benanntem Vorbehalt, nicht geraten.  Waechter
  `FsRecoverCpm.ZweiGleichnamigeGeloeschteDateienSindZweiFunde`; er braucht einen
  freien Platz WEITER VORN, sonst belegt die neue Datei den Platz der geloeschten
  gleich wieder und es gibt gar keine zwei Eintraege.
- **Gegenprobe der Alternativprofile** (2026-08-19, Entwurf §11a).
  `DiskVolume::gegenprobe` öffnet bei `unambiguous == false` dieselbe Datei mit jedem
  Alternativprofil und meldet `erkennung.alternative` (Info, Ebene `Erkennung`).
  **Zwei Kriterien, und beide werden gebraucht:** weniger Befunde ODER mehr sichtbare
  Dateien bei nicht schlechteren Befunden.  Der zweite Fall ist der gefährliche — ein
  zu KLEINER `dir_entries` versteckt Dateien, ohne einen einzigen Befund zu erzeugen
  (die Erkennungsprobe sieht ja nur die erste Hälfte des Verzeichnisses und findet sie
  tadellos).  Ein zu GROSSER fliegt dagegen schon bei `cpmVerzeichnisPlausibel` raus,
  weil die überzähligen Plätze in Dateidaten liegen.
  **Sie ändert die Wahl NIE** — bei ähnlichen Profilen ist „welches prüft sauberer"
  kein stabiles Kriterium, und der Anwender sähe bei jedem Öffnen ein anderes
  Dateisystem.  Endlosrekursion sperrt ein `thread_local`-Riegel; ohne Pfad (physische
  Diskette) läuft sie gar nicht.
  **Prüfbar nur über einen test-eigenen Katalog**: `data/formats.yaml` wird bewusst
  eindeutig gehalten (`cpa640` wurde 2026-08-11 genau deshalb entfernt), keines der 31
  Abbilder im Baum meldet Alternativen.  `tests/fixtures/formats_mehrdeutig.yaml` legt
  ein `scp1700` mit 64 statt 128 Verzeichniseinträgen daneben — auf der A7100-Fixture
  sind damit 43 statt 46 Dateien sichtbar, bei gleicher Befundzahl.  Wächter
  `FsCheckGegenprobe.*`, darunter `DerAusgelieferteKatalogIstEindeutig`.
- **Physische Diskette: erst LADEN, dann Dialog** (2026-08-19).  Prüf- und
  Suchdialog rufen `MainWindow._abbild_vervollstaendigen`; fehlende Spuren kommen
  mit `app/ui/physical_disk.py::mit_fortschritt` herein — derselbe Balken wie bei der
  Formaterkennung, „X von Y Spuren geladen", abbrechbar.  Die Sperre der teuren Läufe
  fragt seitdem **`abbild_vollstaendig`** statt `tool.path` (`fsck_dialog.py`,
  `recover_dialog.py`, `disk_info_dialog.py`): eine physische Diskette, die schon
  ganz im `DiskMedium` liegt, war vorher grundlos gesperrt.  Die Antwort kommt aus
  der Sitzung (`tracks_known` gegen `tracks_total`), nicht aus dem Kern.  Wächter:
  `…ein_unvollstaendiges_abbild_sperrt_die_teuren_laeufe`.
- **Ebene Medium bei UDOS/NDOS: der Reihenlauf über ALLES** (2026-08-19).  Geprüft
  wurde vorher nur, was in einer Kette steht.  Jetzt zusätzlich
  `udos.medium.frei_kaputt` (Warnung, je Seite einmal zusammengefasst): ein Sektor
  mit falscher CRC außerhalb jeder Datei ist nicht nichts — **dort liegen die
  gelöschten Dateien**, und dorthin schreibt UDOS als nächstes.  Dazu
  `udos.medium.unformatiert` (Fehler), wenn die Karte auf einer markenlosen Spur
  Belegung behauptet.  **Ein FEHLENDER freier Sektor ist bewusst KEIN Befund** —
  nichts verloren, nichts zu tun, und die Erkennung nennt ihn als Medienhinweis;
  `udos_boot_scp.hfe` hat einen auf Spur 51, und ein Prüfer, der die
  Referenzdiskette anmahnt, wird zu Recht weggeklickt (E10).  Beide Richtungen haben
  einen Wächter.
- **Erst ansehen, dann handeln — die Sektorliste eines Fundes** (Entwurf §13.3a,
  gilt für CP/M genauso).  `FsRecoverFind::orte` führt ALLE Sektoren in
  Lesereihenfolge (bei UDOS der Kopfsektor zuerst), C-ABI
  `k1520d_recover_part_count`/`k1520d_recover_part`, Python `RecoverFind.parts`,
  im Dialog eine Auswahl neben der Vorschau plus *Im Diskeditor zeigen*
  (`MainWindow._befund_im_editor`).  **Warum die ganze Liste:** die Sätze einer
  UDOS-Datei liegen verkettet und physisch verschränkt — `NOTE.TO.SD` belegt auf
  Spur 21 die Sektoren 6, 7, 12, 23, 1, 8, …; wer nur den ersten bekommt, findet den
  zweiten nicht.  Und bei UDOS gibt es keinen Namen, an dem man einen Fund
  wiedererkennen könnte.  Geführt werden Sektor-KENNUNGEN (nicht Versätze), begrenzt
  auf `kFsRecoverMaxOrte` = 512 — Bedienhilfe, kein Abbild.  Wächter:
  `FsRecover{Udos,Cpm}.DerFundFuehrtSeineSektorenAuf` (der UDOS-Fall prüft
  ausdrücklich, dass die Sektoren NICHT fortlaufend sind) und der GUI-Fall
  `…sektorweise_im_diskeditor_ansehen`.

- **Etappe 7: Ebene 0 — „warum wurde denn nichts erkannt?"** (2026-08-19, Entwurf
  §11).  Jede Positivprobe der Erkennung nennt einen Grund; der verfiel bisher im
  `continue`, und der Anwender bekam EINEN Satz statt einer Diagnose.  Jetzt sammelt
  ihn `DiskVolume::merkeAblehnung` als (Kandidat, Grund), und `DiskVolume::ebene0`
  macht bei `hasFileSystem() == false` Befunde daraus.  Vier Festlegungen:
  **(1) Eigene Ebene `FsLayer::Erkennung`** (Zahl 3, additiv an die C-ABI gehängt,
  `EBENE_ERKENNUNG` in `k1520disk.py`) und Schwere **immer `Info`** — ein
  Ablehnungsgrund ist ein Fund, kein Schaden.  Kennungen: `erkennung.abgelehnt` und
  `erkennung.ohne_kandidat` (es kam gar keine Dateisystemprobe zum Zug).
  **(2) Auf einer ERKANNTEN Diskette darf `erkennung.*` nicht vorkommen** — Wächter
  `FsCheckKeineFalschmeldungen.EineErkannteDisketteHatKeineBefundeDerEbene0`.
  **(3) `check` und `fsck` öffnen ROH** (`oeffne(..., roh_erlaubt)`), sonst käme die
  Ebene 0 auf der Kommandozeile nie zum Zug; Rückgabewert bleibt **2** (nicht erkannt),
  nicht 1 (Befunde).  Mit `--repair` bleibt es beim Abbruch.  In der Oberfläche gehen
  die Gründe ins **Protokoll**, und der Prüfdialog (Strg+F) ist auch ohne Dateisystem
  bedienbar.
  **(4) Die Geometrie gehört auch dann in `detection().format`**, wenn kein
  Dateisystem darauf liegt — sonst zeigt die Anzeige ein leeres Feld statt der einen
  Sache, die feststeht.
  Nebenbefund, der den Wert der Ebene 0 sofort belegt hat: der Grund aus
  `cpmVerzeichnisPlausibel` lief in einen `char t[80]` und war mitten im Wort
  abgeschnitten — solange er weggeworfen wurde, fiel das keinem auf.
  **Die zweite Hälfte der Etappe, die Gegenprobe der Alternativprofile, ist
  ZURÜCKGESTELLT** (Entwurf §20): `filesystems:` ist absichtlich eindeutig gehalten
  (`cpa640` wurde genau deshalb entfernt), keine Fixture meldet Alternativen — es gibt
  heute keinen Fall, an dem sie prüfbar wäre.
  Wächter: `FsCheck.EineUnerkannteDisketteNenntDieAblehnungsgruende`,
  `cli_dt_check_ebene0`, zwei `py_disktool_gui`-Fälle.
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

# K1520 — Offene Punkte

> **Stand:** 2026-09-10 · Zweig `greaseweazle_integration` (42 Commits vor `origin`,
> `origin/main` ist Vorfahr ⇒ Fast-Forward möglich).
> **Sprache:** Dieses Dokument war bis 2026-07-22 englisch. Es ist jetzt deutsch wie
> alles, worauf es verweist (`doc/design/*`, `doc/merkposten/*`).

Der Emulator ist an keiner Stelle mehr blockiert: A5120 bootet CP/A, SCPX 1526 und
UDOS 4.3 vollständig bis zum Prompt; Tastatur, Uhr, Lesen, Schreiben und FORMAT.COM
laufen, selbstgebaute Bootdisketten booten. Das Werkzeug `k1520DiskTool` liest und
schreibt CP/A, SCPX, UDOS/ZDOS, UDOS1715/NDOS und SCP1700; die Dateisystemprüfung
(`fsck`, Etappen 1–7) samt Reparatur und Rettung gelöschter Dateien ist fertig. Eine
physische Diskette am Greaseweazle ist an echter Hardware nachgewiesen.

Was hier steht, ist damit **Nacharbeit und ein kurzer Rest exotischer Fälle** —
keine Architekturfragen mehr.

---

## 1. Fällig: der Merge nach `main`

`greaseweazle_integration` trägt 42 nicht gepushte Commits (26 davon nicht auf
`origin/main`): die ganze fsck-Arbeit, den `.fileinfo`-Rundlauf, die Ordnerseite als
Dateibrowser und den Belegungsplan-Fix vom 22.08. Lokales `main` ist 16 Commits
hinter `origin/main` — vor dem Merge also `git fetch` und `main` nachziehen.

**Vor dem Merge alle vier Lanes**, nicht nur den `pre-push`-Hook (der deckt bloss die
erste ab) — der Zweig hat `core/filesystem/` breit angefasst:

| Lane | Befehl | Stand 2026-09-10 |
|------|--------|------------------|
| Regression | `tools/dev.sh test` | grün (38 s) |
| Tiefe | `tools/dev.sh test-format` | grün (59 s) |
| Breite | `tools/dev.sh test-matrix` | grün (156 s) |
| Windows | `tools/dev.sh win` | grün (1182/1182 unter wine) |

> Die Windows-Lane war beim ersten Lauf rot — zweimal derselbe Test
> (`FsRecoverCpm.EineGeloeschteDateiKommtByteFuerByteZurueck`), zweimal aus einem Grund,
> den es unter Linux nicht gibt: der byteweise Vergleich hielt zwei `std::ifstream`
> offen, während `fs::remove_all()` den Ordner löschte (`Sharing violation` — Windows
> löscht keine offene Datei), und der dadurch liegengebliebene Ordner brachte den
> Folgelauf zu Fall (`Zieldatei existiert bereits`). Behoben in
> `tests/unit/filesystem/test_fs_recover.cpp`: Vergleich über den vorhandenen Helfer
> `bytes()` (liest in eigenem Gültigkeitsbereich und schliesst), und `remove_all()`
> **vor** `create_directories()` — so machen es die vier Schwestertests längst.
> **Merke:** Ein Test, der nur beim Aufräumen am Ende sauber macht, ist unter Windows
> nach dem ersten Fehlschlag dauerhaft rot.

Die Python-Ebene muss dabei **ohne** `greaseweazle` grün sein (in der CI ist das Paket
nie installiert; Wächter `tests/python/test_gw_ohne_paket.py`).

**Aufräumen davor:** zwei ungetrackte Abbilder in `disks/` aus dem Hardware-Schreibtest
— `sicherung_udos_vor_schreibtest.hfe` (15.08.) und `udos_boot_test01.hfe` (12.08.).
Entweder als Fixture einchecken (dann mit einem Satz in `tests/fixtures/README.md`,
welche Diskette das ist) oder löschen.

---

## 2. k1520DiskTool

### 2.1 Doppelschritt-Disketten sind nicht katalogisierbar (der grösste offene Posten)

`tracks:` im Formatkatalog beschreibt zusammenhängende Bereiche; Austauschformate mit
Doppelschritt belegen nur jeden zweiten Zylinder. Die Erkennung lehnt sie deshalb
ausdrücklich ab (Kriterium `gap_tracks`), statt ein 80-Spur-Format darüberzuziehen und
Datenmüll zu lesen. **Betroffen: 13 der erzeugten Prüfabbilder** (CP/A-Geometrien `T`/`U`).

Abhilfe wäre ein Attribut `step: 2` am Format; zu ändern sind `TrackFormat`/`DiskFormat`,
die Spurabbildung im `SectorSpace` und das Lückenkriterium im `GeometryProbe`.
Ausgearbeitet: `doc/feature_requests/doppelschritt_disketten.md`, Entwurf
`doc/design/13_k1520disktool.md` §18.8.

> Der **Kern** kann Doppelschritt seit 2026-08-12 (`FloppyDriveV2::mount()` übersetzt die
> Spurdichte, Wächter `FloppyDriveV2.Doppelschritt_IstDieselbeDisketteWieEinDoppelschrittAbbild`).
> Offen ist die Katalogseite — und nachzumessen, ob die Formatier-Pipeline `T`/`U`
> seitdem auch als `.img` erzeugen könnte; heute macht sie dort nur `.hfe`.

### 2.2 Reparatur ohne gemountetes Dateisystem

Bricht bei UDOS die Kette der **Verzeichnisdatei selbst**, scheitert schon `mount()`,
und geprüft wird nur noch auf Ebene 0. Grundsätzlich reparierbar — der Kopfsektor der
Verzeichnisdatei liegt fest auf Spur 22 Sektor 1, die Kette liesse sich aus den
Kontrollblöcken verfolgen —, verlangt aber einen Reparaturweg **ohne** Dateisystem.
Zurückgestellt; heute ist der Diskeditor die Handhabe.
`doc/design/15_dateisystempruefung.md` §20.

### 2.3 Kleinere Vorbehalte

- **`HIGH ADDRESS` / `STACK SIZE`** im UDOS-Kopfsektor sind nicht eindeutig zugeordnet
  (`doc/udos_diskettenformat.md` §13.3). Beim Einfügen einer Datei vom Typ `P`/`P1`
  werden sie deshalb nicht gesetzt — für Datendateien belanglos, für ausführbare ein
  benannter Vorbehalt. Entwurf §18.3.
- **Zwei Kopfsektorfelder überleben den Rundlauf `get`/`put` nicht**: `bytes_in_last == 0`
  wird beim Schreiben zur vollen Satzlänge, und LOW/HIGH/STACK werden nur geschrieben,
  wenn mindestens einer der drei Werte ≠ 0 ist (`udos_fs.cpp`, `write()`). Bewusst
  unangetastet, weil die Regel „0 heisst nicht angegeben" an anderer Stelle tragend ist.
- **CP/M-3-Zeitstempel** (`os: cpm3`): das Schema sieht das Feld vor, ob im Bestand
  solche Disketten liegen, ist ungeprüft. Entwurf §18.2.
- **`cpm.block.luecke`** (Nullzeiger vor belegtem Zeiger) bleibt `Warnung` ohne
  Reparatur, bis ein echter Fall vorliegt.
- **Was UDOS beim Löschen wirklich tut**, ist an unserer Umsetzung abgelesen und an
  einer echten Diskette nur stichprobenhaft gegengeprüft.

---

## 3. Physische Diskette (Greaseweazle)

- **Die Sitzungsparameter merkt sich niemand** — Laufwerk und Zellrate sind bei jedem
  Einlegen neu zu wählen. Kleine, im Betrieb spürbare Nacharbeit.
- **Ein zweites physisches Laufwerk am selben Adapter ist ungetestet**
  (`doc/design/14_physische_diskette.md` §15/§16).

---

## 4. Diskettenformate — der exotische Rest

Voller Stand: `doc/format.md` §8. Alles hier ist Randlage, nichts blockiert.

- **(a) `Fehler 'S' SPUR DEFEKT` bei den Interleave-Formaten** K5601 `7` („ZIK-NK") und
  W:6 („BAP2001"), auf `.hfe` **und** `.img`. Der Emulator schreibt nachweislich
  korrekte Sektoren (IDs 1–16 fortlaufend) und verhält sich auf bestandenen und
  fallenden Spuren gleich — das `'S'` ist ein FORMAT.COM-internes Urteil, kein
  differenzieller Emulatorfehler. Endgültige Ursache braucht die **Disassemblierung
  des `'S'`-Verify-Pfads**. 2 von ~30 Formaten, im Smoke der Matrix fallen sie nicht an.
  `doc/format.md` §8.4.
- **(b) 1024-B-FM-Lesepfad (`mf3200_fmt1`)** — eine 8″-SD/FM-Diskette mit 1024-B-Sektoren
  formatiert, wird bootfähig und bootet, aber ein `DIR` am laufenden OS scheitert mit
  `Bdos Err On A: Bad Sector`. 256-B-FM (`mf3200_fmt7`) läuft und teilt den Lesestrom —
  der Unterschied ist die Sektorgrösse auf dem FM-Pfad. `doc/format.md` §8.6.1.
- **(c) 8″-Datenrate im `DriveProfile` zu niedrig** (Nebenbefund 2026-08-07):
  `bytePeriodCycles` rechnet für **alle** Laufwerke mit der 5,25″-Rate (125/250 kbit/s);
  8″ läuft real mit 250/500. Bei 360 min⁻¹ passen im Modell 2617 statt 5208 Bytes je
  Umdrehung, eine 8″-FM-Spur (4576 B) also **nicht in eine Umdrehung**. Die Korrektur
  wurde probeweise gebaut — 782/782 ctest und alle 17 `format_matrix_8inchCombo_*`
  blieben grün, aber `bootdisk_mf3200_fmt7` und `bootdisk_mf6400_fmt1` fielen um: die
  CP/A-8″-Bootkette kompensiert die falsche Rate offenbar anderswo. Deshalb **nicht
  übernommen**. `doc/udos_diskettenformat.md` §12.3 (Nebenbefund am Ende).
- **(d) `--full`-Läufe der Formatmatrix bleiben manuell**
  (`python3 tests/system/drivers/format_all.py --all --full`); dort sind K5601 `7` und
  `5` als `.hfe` bekannt rot (siehe (a)).

---

## 5. Emulator — Restposten

- **VRAM-Wischer nach ~50–65 Mio. Leerlauftakten** nach Erreichen des Prompts.
  Vermutet: Rest-Drift der Uhr und/oder sporadische ZVE2-Floppyaktivität. Kosmetisch,
  seit 2026-07 nicht wieder nachgeprüft. Spuren im Merkposten
  `project_os_boot_reaches_prompt`.

---

## 6. Erledigt seit dem letzten Stand dieses Dokuments (2026-07-22)

Nur zur Orientierung — die Einzelheiten stehen in den Analysedokumenten und in git.

- **Gap-leere `.hfe` hängt beim Formatieren** → gelöst 2026-07-06; die Ablehnung
  markenloser Abbilder beim Öffnen (`hasFormattedData`) ist ersatzlos entfallen. Der
  Nachzügler `Fehler 'U' SPUR DEFEKT` auf einer echten Leerdiskette ist seit 2026-08-06
  behoben (verwaister FORMAT-Schreibpuffer), `doc/analyse_format_leerspur.md`.
- **Doppelschritt im Kern** (siehe §2.1) → 2026-08-12.
- **SCPX `PIP`/`REN`-Hänger: Wächter fehlt** → es gibt ihn,
  `ScpxInit.CreateFormatBThenPipCopyFromBootDisk` (Temp-Diskette heisst nicht umsonst
  `scpx_pip_guard_B.hfe`) sowie `ScpxIntegration.EraDeletesFileOnDriveBWithoutBadSector`.
- **Deaktivierte Tests durchsehen** → erledigt: im Testbaum steht kein `DISABLED_` und
  kein `GTEST_SKIP` mehr, der veraltete Kommentar in `test_boot_integration.cpp` ist
  berichtigt.
- **`.img` für Doppelschritt-Geometrien `T`/`U`** → im Kern gelöst, in der Pipeline
  nicht nachgezogen (siehe §2.1).

---

## 7. Bekannte Nicht-Probleme (nicht erneut untersuchen)

- **Kein 8″-Laufwerkskarte nötig.** Der K5122 ist formatagnostisch, der Laufwerkstyp ist
  reine BIOS-Software; 8″-Formate sind über Combo-Boot-Disketten und die Profile
  `mf3200_8_ss77` / `mf6400_8_ss77` testbar und bootfähig.
- **Die vier UDOS-`SET DISKCON`-Fehlschläge sind Gastverhalten**, kein Emulatorfehler:
  Sektorlänge ≠ 128 (FORMAT.COM nutzt nur das Typ-Nibble und formatiert fest 26×128),
  8″-Typen `11`/`21` (UDOS schreibt das Datenfeld ohne den 4-Byte-Sektorkontrollblock),
  Typ `61` (FORMAT schreibt einfachschrittig, der Treiber liest schrittverdoppelt).
  Kontrollkreuz gerechnet: gleiche 5,25″-Hardware + `21` scheitert, 8″-Hardware + `41`
  bootet. `doc/udos_diskettenformat.md` §12.3.
- **Strikter „gehaltener Bus" für die ZVE1↔ZVE2-Arbitrierung** ist eine verifizierte
  Sackgasse — nicht erneut versuchen. Gültig ist das per-Byte-`/BUSRQ`-Modell.
- **`.img` für UDOS/ZDOS ist unmöglich** (Verkettung im Gap hinter der Daten-CRC),
  `rawCompatible()` sperrt es. Bei UDOS1715/NDOS ist `.img` dagegen erlaubt.

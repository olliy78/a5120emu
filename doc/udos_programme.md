# UDOS-Programme — Bestandsaufnahme aller Disketten

**Stand:** 2026-10-05 · Anlass: die beiden PRG-Lieferdisketten (`disks/prg710_udos43_k5601_system.hfe`,
`disks/prg710-1_udos43_k5601_v43_189.hfe`) aufräumen und sinnvoll bestücken.
Ergänzt `doc/udos_diskettenformat.md` (Format) und `~/projects/UDOS/README.md` (Quellenfundus).

## 1. Was untersucht wurde

32 inhaltlich verschiedene UDOS-Abbilder (nach MD5 entdoppelt; 691 Abbilddateien durchsucht) aus
`disks/`, `tests/fixtures/`, `../a5120emu`, `../prg710`, `../robotron`, `~/Documents/K1520emu/Disketten`
(dort auch die ausgepackten Ordner). Geöffnet mit `k1520disktool get`. `.scp`-Rohabzüge kann das DiskTool
nicht lesen; sie sind meist schon als `.hfe` vorhanden und wurden nicht einzeln ausgewertet.

Kurzbezeichnung der Diskettenfamilien (in der Spalte „Vorkommen“ der Tabellen):

| Kürzel | Familie | Abbilder | Systemstand (`OS`) |
|---|---|---|---|
| **P7** | PRG 710 (K2521-ZRE) | `prg710_udos43_k5601_system`, `PRG710_UDOS43_MRS_Boot`, `PRG_UDOS_software`, `PRG710-1_UDOS_MRS` (leer lesbar) | UDOS 4.3, `UDOS PRG710`, 29.04.88 |
| **P71** | PRG 710-1, V4.3 1/89 | `UDOS.PRG710-1_V4.3_1_89` = `prg710-1_udos43_k5601_v43_189` | UDOS 4.3, `UDOS PG710-1`, 09.03.89 |
| **P71a** | PRG 710-1, ältere Bootdisketten (`7101.B`) | `PRG710-1_UDOS_Boot`, `PRG710-1_UDOS43_Boot`, `prg710-1_udos_boot01`, `udos_ds77_k5601_fremdsync` | `UDOS PG710-1`, 01.01.90 (Katalog) |
| **A** | BC A5120, UDOS 4.3 | `UDOS_5120_ser`, `udos_boot_scp`, `sicherung_udos_vor_schreibtest`, `udos_boot_k5600_10/20`, `udos_boot_mf6400` | `UDOS BC.5120`, 08/90 (K5600-Boote 15.03.88) |
| **B** | A5120/USAR „1526 par“ | `UDOS_1526_par` (106 Dateien, Datenträger `NEA/AS.VOL_003`) | `UDOS BC.5120`, 31.10.91 |
| **U** | USAR (Unterzentrale Suhl) | `USAR_UDOS` (15 Dateien) | `UDOS BC.5120`, 27.06.89 |
| **PC** | PC 1715, **UDOS1715/NDOS** | `UDOS1715_patched`, `udos1715_640k_pc1715_system` u. a. | eigenes System (`NDOS`, nicht ZDOS) |
| **W** | P8000, UDOS 2.2 + WEGA-Starter | `udos_P8000`, `udosP8000_640k_wega` | `NDOS`, 22.01.88 |

Die letzten beiden sind ein **anderes Dateisystem** (NDOS, Zeigersektoren) und ein anderes UDOS; ihre
Programme sind für die PRG **nicht ohne Test** übernehmbar (§5).

## 2. Befund: PRG 710 gegen PRG 710-1

Beide Lieferdisketten tragen **dasselbe UDOS 4.3**. `ZDOS` und alle Kommandos außer `SPLIT` sind bytegleich.

- **`OS`** unterscheidet sich in 6 Byte: nur die Kennung `UDOS PRG710` ↔ `UDOS PG710-1`, plus
  Katalogdatum. Der Nukleus ist derselbe.
- **Die Boot-Spuren (13 KB) sind je Gerät verschieden** (Tastatur-/MKE-Treiber). Deshalb bleibt es bei
  **zwei Disketten**, auch wenn der Inhalt gleich wäre.
- **`SPLIT`** (Rollbereich des Bildschirms auf *n* Zeilen begrenzen, patcht den Bildschirmtreiber):
  - 29.04.88 (P7, 384 B), 09.03.89 (P71, 384 B) und eine **UDOS-4.2-Fassung** vom 08/90 auf der A5120
    (`%SPLIT FOR UDOS 4.2`, patcht andere Adressen — für 4.3 **ungeeignet**).
  - 88 ↔ 89: 203 von 384 Byte verschieden, aber nur durch **verschobene Adressen** (die 89er ist um 1 Byte
    kürzer). Inhaltlich: die 89er korrigiert den Tippfehler `EINAGBE OPTION ?` → `EINGABE OPTION ?`,
    sucht die Tabelle ohne Zähler und hat die Prüfung `FEHLER: KEIN POS_UDOS` nicht mehr.
  - **Beide laufen auf beiden Geräten, weil die `OS`-Dateien (Bildschirmtreiber) gleich sind. Das ist
    aus dem Code geschlossen, nicht am Emulator geprüft.**
  - **Empfehlung: die 89er (P71) für beide**, sofern sie am 710 im Emulator läuft.
- Nur P7: `DRUCK.IFSS`. Nur P71: `PTAPE.S1`, `TREAD.CON337`, `TWRITE.DT105`, fünf Dokumente auf Seite 1.
  Auf P71 sind alle Dateien geheim (`S`), auf P7 nicht.

## 3. Katalog

Spalte *PRG*: ✔ = ist schon auf der Lieferdiskette, ○ = sinnvoller Kandidat (begründet), ✘ = nicht für die PRG,
? = unklar. Datumsangaben sind Katalogdaten der Dateien.

### 3.1 Systemkern und Dateisystem

| Datei | Zweck | Vorkommen | PRG |
|---|---|---|---|
| `OS` | UDOS-Nukleus/physischer Systemkern (0x1000–0x25FF, 5632 B), je Rechner generiert | alle | ✔ je Gerät |
| `ZDOS` | Dateisystemschicht (Variante A, ab 0x2600) | P7 P71 A B U | ✔ |
| `NDOS` | Dateisystemschicht (Variante B, Hochspeicher) | PC W | ✘ |
| `OS.INIT` | Startskript (Typ A2, läuft über `DO`): Begrüßung, `DATE` | alle | ✔ (angepasst) |
| `OS.INIT.OLD` | Sicherung des alten Skripts mit dem Spruch | P7 (MRS-Disketten) | ✘ Müll |
| `DIRECTORY` | Verzeichnisdatei der Seite | alle | ✔ |
| `BOOT` | Bootmodul (Spur 21) | PC B | ✘ |
| `SYL0`, `SYL1` | Systemlader-Teile (Quelle für `SG`) | B | ○ nur mit `SG` |

### 3.2 Interne Kommandos (HELP `I`) und Standardkommandos

Auf allen 4.3-Disketten. Kurzbeschreibung nach `HELP.DAT.00`.

| Datei | Zweck |
|---|---|
| `ACTIVATE` / `DEACTIVATE` | Peripheriegerät in die Aktive-Geräte-Tabelle (ADT) eintragen / entfernen; lädt den Gerätetreiber |
| `LADT` | Aktive-Geräte-Tabelle ausgeben |
| `DEFINE` | Zuordnung logische Einheit ↔ Gerät ändern |
| `MASTER` | Mastergerät anzeigen/umdefinieren |
| `DISPLAY` | Speicherbelegungsplan ausgeben |
| `IMAGE` | Speicherinhalt als Procedure-File ablegen |
| `CAT` | Dateinamen suchen (nach Name, Typ `T=`, Eigenschaften `P=`, Laufwerk `D=`, Datum) |
| `COPY` / `MOVE` | Dateien kopieren (mit Auswahl nach Typ/Eigenschaft) |
| `COPY.DISK` | ganze Diskette kopieren |
| `DELETE`, `RENAME` | löschen, umbenennen |
| `COMPARE`, `DUMP`, `EXTRACT` | Dateien vergleichen; Hex-Ausgabe; Dateiattribute anzeigen |
| `PRINT` | Textdatei ausgeben (Bildschirm/Drucker) |
| `DO` | Kommandodatei abarbeiten |
| `SET` | Systemvariablen und Dateieigenschaften setzen (u. a. `DISKCON`, Typ, Eigenschaften) |
| `STATUS` | Diskettenzustand (Belegung) |
| `FORMAT` | Diskette formatieren (siehe auch `SET DISKCON`) |
| `DATE` | Systemdatum setzen/abfragen (`DATE Q` = vereinfachte Eingabe) |
| `HELP`, `HELP.DAT.00–04` | Hilfesystem; die `.DAT` sind die Hilfetexte |
| `ERROR` | Fehlercode → Fehlerart |
| `FILE.DEBUG` | Dateien ansehen/ändern (Debugger für Dateien, Jahrgang 1979; Zweck aus Namen und Meldungen erschlossen) |
| `KEY` | Tastaturbelegung umprogrammieren (ab 4.2 alle Tasten) |
| `SPLIT` | Rollbereich begrenzen, siehe §2 |
| `MASTER`, `ECHO`, `PAUSE`, `XEQ`, `ALLOCATE`, … | intern im `OS` (kein Dateiname) |

Die Versionen weichen je Familie ab (z. B. `COPY` 88/89/90, `FORMAT` 2048 B ↔ 2432 B, `SET` 2048 B ↔ 2432 B).
Die PRG-Disketten tragen die 88er/89er Stände. Die A5120-Fassungen (08/90) sind neuer, gehören aber zu einem
anderen System; bei Gleichheit des Dateisystems (ZDOS) ist ein Test sinnvoll, bevor man sie übernimmt.

### 3.3 Treiber (werden mit `ACTIVATE` geladen)

| Datei | Zweck | Vorkommen | PRG |
|---|---|---|---|
| `DRUCK.V24`, `DRUCK.IFSS`, `DRUCK.ZIFSS`, `DRUCK.PIO`, `DRUCK.1156` | Druckertreiber V.24 / IFSS (Haupt) / ZIFSS (Zusatz) / PIO / Typ 1156 | je nach Familie | ✔ V24, ZIFSS, IFSS (P7, nicht P71 → angleichen); PIO/1156 ✘ (K8025 hat kein PIO) |
| `SD`, `SD1156`, `FSD` | formatierender Druckertreiber SD 1152/1157 mit Seitenformat (Beschreibung `NOTE.TO.SD`) | P7 P71 A | ✔ |
| `TAPE.READ`, `TAPE.WRITE` | Lochband-Leser/-Stanzer (allgemein) | P7 P71 | ✔ |
| `TREAD.1210`, `TWRITE.1215` | Leser daro 1210, Stanzer daro 1215 | P7 P71 | ✔ |
| `TREAD.CON337`, `TWRITE.DT105` | Leser/Stanzer anderer Bauart (CON 337, DT 105) | nur P71 | ○ gleiche Familie, harmlos |
| `PTAPE.6022`, `PTAPE.S1` | Lochband über K6022/SIF 1000 | P7 P71 | ✔ (S1 nur P71) |
| `TAST`, `TAST.S`, `TAST.76xx.OBJ` | Tastaturtreiber (Quelle/Objekte K7604…K7637) | A, B | ✘ PRG hat eigene Tastaturwege |
| `LW` | zeigt die Laufwerkskonfiguration (Spurzahl, Seitigkeit) | B | ○ klein, nützlich |
| `RAMFL`, `RFA`, `RAMTEST` | RAM-Disketten-Treiber, Einrichten, Test | B | ○ erst klären, welche RAM-Disk (RAF-Karte?) gemeint ist |
| `PRINTER`, `LINK.PRINTER`, `PRINTER.OBJ`, `DRUCK.*.OBJ` | Druckertreiber-Baukasten (`WR V2`) | B | ✘ nur mit Entwicklung |
| `ADM_O2` | Zeichenumsetzung (ADM-Terminal?) | P7 | ? |

### 3.4 Programmierung, Assembler, Binder

| Datei | Zweck | Vorkommen | PRG |
|---|---|---|---|
| `ASM`, `ASM2`, `ASM3` | Z80-Assembler 5.9 (780301), zwei Folgeläufe, `ASM3` = Querverweisliste | alle | ✔ (`ASM2`/`ASM3` je nach Familie; geprüft: identisch) |
| `LINK` | Binder 1.7 (781207) | P7 P71 A B | ✔ |
| `PLINK` | Binder für PL/Z 4.0 | PC B | ○ nur zusammen mit PL/Z |
| `PLZSYS`, `PLZCG`, `PLZCG.OVLY1/2.DS` | Zilog-**PL/Z**-Compiler 3.0 (1979) | B | ○ (≈ 70 KB; nur falls PL/Z gewünscht) |
| `Z8ASM`, `Z8ASM2`, `ZLINK`, `ZLINK1/2`, `IMAGER`, `LTS`, `RL80`, `RL100` | Zilog-Entwicklungsprogramme (Assembler 3.03, Binder 2.2, Abbildgenerator) | PC | ✘ NDOS |
| `EDIT` 2.1 | Editor | P71a A B PC | ○ (nur auf P71a; kleiner, 6 KB) |
| `SCREEN` 1.5 | Bildschirm-Textprozessor „für UDOS 1526“ (1987), Beschreibung `SCREEN.BESCHR`, Kurzreferenz `SCREEN.BED` | P7 P71 A | ✔ |
| `SEDIT` | Bildschirmeditor (fragt Tastaturtyp ab) | B | ✘ braucht K76xx-Tastatur-Kennung |
| `EDI`, `EDR`, `EDIPRINT`, `EDI.B`, `MULTIPRINT` | Texteditor und Druckaufbereitung der IfR Berlin (1982) | PC | ✘ |
| `BASIC` 5.6 (BCD-Arithmetik) | BASIC-Interpreter (780501) | P71a, PC | ○ **sinnvoll** (31 KB; läuft über UDOS-Aufrufe, am 710 zu prüfen) |
| `SYD` | Debugger („SYD by S&T“) | B | ○ Z80-Testen, klein (10 KB) |
| `DEBUG.OBJ`, `*.S`, `*.OBJ`, `POS.*`, `SG` | Quellen/Objekte und Systemgenerator für den **physischen Systemkern** (`SG` baut `OS`; Beschreibung `NOTE.TO.SG`) | B | ○ für Anwender, die einen eigenen Kern bauen wollen |
| `GCL`, `LZP`, `ZOCS`, `TZOCS` | Zweck nicht ermittelt (stehen auf der älteren 710-1-Bootdiskette) | P71a | ? |
| `UDOS4.1.DOK`, `NOTE.TO.UDOS.4`, `.4.2`, `.4.3` | Änderungsanlagen zum UDOS-Systemhandbuch (Autor KMST/Numerik, 1986/87) | P71 A P71a | ✔ `.4.3`; ○ `.4`, `.4.2` (Hintergrund zu SPLIT/KEY) |

### 3.5 EPROM-Programmierung

| Datei | Zweck | Vorkommen | PRG |
|---|---|---|---|
| `PROG` V3.1, `OVR.PROG`, `OVR.DOCU`, `PROG.DOK` | Programmierer für EPROMs (Betriebsarten Lesen/Prüfen/Programmieren; Prüfsumme + SDLC-CRC); `PROG.DOK` beschreibt die Befehle | P7 P71 | ✔ (Dokument nur P71) |
| `PROM` V2.3 | EPROM-Programmer unter UDOS (im PRG-700-Handbuch beschrieben), siehe §3.7 | P7 P71 (Seite 1 der Anwenderdisketten) | ○ → §3.7 |
| `EPROM43` | EPROM-Programmierung für 1-KB-Bereich (0000–03FF) „aus dem RAM ab 4000“ | P71a | ○ |
| `UPRO` 1.1, `ESPRO` 1.0 | universeller Programmierer 2708…27256 (Typ, Load, Close Load) | A | ? andere Hardware als ATP 590068; zu prüfen |
| `CHECK` | Prüfsummen über Bereiche | PC | ✘ |

### 3.6 Diskette, Konvertierung, Austausch

| Datei | Zweck | Vorkommen | PRG |
|---|---|---|---|
| `COPYSD` | Disketten-Kopierer 1978 (SD = *single density*) | PC B | ✘ |
| `REORG` | Diskettenbelegung reorganisieren (V4.3) | B | ○ nützlich auf Arbeitsdisketten |
| `CPCOPY` | Kopieren zwischen UDOS- und SCP-Dateien | PC | ✘ NDOS, nur mit SCP-Diskette |
| `TRANSFER` | CP/M-Hexdatei auf UDOS-Diskette übertragen (Burkhardt/Schütz 1986) | B | ○ |
| `UDOSCNVT`, `SETFD`, `SETLP`, `GETDAY`, `SAVEDAY`, `DAY`, `KINIT`, `LP`, `WEGA`, `boot*`, `sa.*`, `wega*`, `INHALT`, `README` | P8000-Starter für **WEGA** (UNIX) und UDOS-2.2-Werkzeuge | W | ✘ |

### 3.7 MRS-700-Programmierumgebung (VEB Numerik Karl-Marx-Stadt)

**Das ist der eigentliche Zweck der PRG.** Das PRG-700-Gerätehandbuch (`~/projects/robotron/PRG710/PRG700_Geraetehandbuch.pdf`)
nennt das PRG die „Programmier- und Testeinrichtung für die speicherprogrammierbaren Steuerungen der
Generation 700“ (PC 600, **MRS 700**, EFE 700, IRS 700, PMC 600, CNC 600). Die Schnittstelle **X4** ist für den
Online-Anschluss der MRS 700 unter UDOS vorgesehen, die EPROM-Programmierung läuft unter UDOS als `PROM`
(„PROM-PROGRAMMER V2.3“, Handbuch Abschnitt „EPROM-Programmierung unter UDOS“).
Die Programme liegen **nur auf Seite 1** der Anwenderdisketten (`PRG_UDOS_software`, `PRG710_UDOS43_MRS_Boot`,
`PRG710-1_UDOS_MRS`) — deshalb waren sie in meiner ersten Zählung nicht sichtbar. Ablauf der Arbeit:

> Quelltext schreiben (`EDI`/`SCREEN`) → mit **`MPSS`** übersetzen → mit **`MRS`** auf der Steuerung testen
> (Programmtestung, Generierdaten) → mit **`PROM`** auf EPROM brennen.

Am **PRG 710 im Emulator geprüft** (UDOS gebootet von `prg710_udos43_k5601_mrs_boot.hfe`, Befehl eingetippt, Bild gelesen):

| Datei | Zweck | Befund im Emulator |
|---|---|---|
| `MRS` (9 KB) | Hauptmenü „MRS 702/703 — Inbetriebnahme und Programmtestung V4.1“, © G. Richter, VEB Numerik 1986: 1 Programmtestung, 2 Generierdaten-Erstellung, 3 Off-line-Diagnose, 4 Prüfsummenberechnung vom EPROM | startet |
| `MRS4.OVL1` (34 816 B) | Überlagerung zu Punkt 1: Programmtestung an der laufenden Steuerung (Variablen anzeigen/setzen, Unterbrechungsbelegung, Symboltabelle, serielle Kopplung); meldet „MRS 700 Betriebssystem V3.0“ | Punkt 1 meldet „**MRS zurücksetzen, warten auf Bereitschaft**“ — es fehlt die Gegenstelle (MRS 700 an X4), mehr ist ohne sie nicht zu prüfen |
| `MRS4.OVL2` (22 528 B) | Überlagerung zu Punkt 2: Generierdaten (Zähler/Zeitgeber, Anzeigetreiber, EA-Treiber-Generierung `T`, Schreiben/Lesen auf Diskette oder EPROM `S`/`L`, Prüfsummen `P`, Debugger CTRL-D) | **läuft** (Kommandoübersicht, Generierdatentabelle) |
| `MRS4.OVL4` (1408 B) | Überlagerung zu Punkt 4: Prüfsumme (SDLC-Polynom) über einen EPROM-Bereich 0000–07FF | **läuft** (fragt Anfangsadresse) |
| `MRS4.OVL3`, `OVL5`–`OVL9` | im Hauptprogramm als weitere Überlagerungen genannt | **fehlen auf allen Disketten.** Punkt 3 (Off-line-Diagnose) meldet `Programm "MRS.OVLx" nicht gefunden` |
| `MPSS` 3.1 (36 KB) | Übersetzer für das **Steuerungsprogramm** (Funktionsbausteine, Bit-/Wort-/Integer-Variablen, Zeitwerte, „Logikcode“; Listenkopf „NKM 1987“), meldet Syntaxfehler/Warnungen/Codebytes/Symboltabelle | startet, verlangt einen Quelltextnamen („Quelltext: falscher Dateiname“) |
| `EDI` (9,7 KB) | Texteditor („IfR Berlin 1982“), liegt auf denselben Disketten, vermutlich für die MPSS-Quellen (aus der Lage geschlossen) | startet (leeres Bild) |
| `PROM` V2.3 (20 KB) | EPROM-Programmer, Kommandos P,R,D,E,H,T,C,V,S,Q, Puffer 4000–47FF | **läuft** (Menü); mit dem EPROMmer des Emulators noch nicht erprobt |
| `MPE32.1`, `MPEA1.1`, `MPAT8`, `NULL`, `EXTRAM` | kleine Moduldateien (128–512 B, Ladeadresse 0000), je mit 3-Zeichen-Kennung im Kopf (`PE3`, `PEA`, `NUL`), Code arbeitet mit `OUT`/`INI` auf Modulports. Nach Aufbau und Lage die **EA-Treiber** (Eingabe 32 Kanäle, Ein-/Ausgabe, Analog 8, Null-Modul, Zusatz-RAM), die `MRS` Punkt 2 (`T` = EA-Treiber-Generierung) einbindet | nicht einzeln gestartet; Deutung aus Code, nicht aus Dokumentation |
| `ADM_O2` | Gerätetreiber für Anzeige (Ladeadresse EC80, Umsetzungstabelle `ZzYy{}|[]\`) — „Anzeigetreiber“ der Generierdaten | nicht geprüft |

Quellenlage: alle diese Dateien stammen aus **Anwenderabzügen**; die Rechte sind ungeklärt (wie bei allen PRG-Disketten,
siehe `doc/merkposten/prg710.md`). Die V4.1-Fassungen von `MRS4.OVL1` (34 816 B) und `OVL2` stehen auf allen drei
MRS-Disketten gleich; auf den älteren `7101.B`-Disketten ist `OVL1` kleiner (30 720 B) und `OVL2` **leer (0 B)**.

### 3.8 Anwendungen und Sonstiges (fremde Aufgabenbereiche)

| Datei | Zweck | Vorkommen | PRG |
|---|---|---|---|
| `URGAN`, `URGAN.DOC`, `URGAN.INIT` | Unterzentralen-Register-Analyse (SBZ Suhl, 1989), Messung am USAR | U | ✘ |
| `KDE`, `KDP*`, `NEBS`, `BAB.S`, `BAB.OBJ` | Programme zur Bearbeitung von Kundendaten bzw. MB-Kassetten der **NZ400D/128** (laut Bannertext) | B | ✘ |
| `TERMINE` | Terminliste (Karneval 87/88) | PC | ✘ |
| `S1` | Skript mit `SET DISKCON= 51 61` | P71a | ✘ Hilfsdatei (Laufwerktypen setzen) |
| `TEXT.TXT` | leer | P71a | ✘ Müll |
| `UDOS.TEXT` | Beschreibung UDOS 1715 (Diskettenaufbau, Belegungsplan) | PC | ✘ |

## 4. Empfehlung für die beiden PRG-Disketten

> **Umgesetzt 2026-10-05** (Punkte 1–3, MRS-Umgebung inklusive): `tools/prg_disketten/build.py` baut beide
> Disketten aus einem Ordner; beide booten, `SPLIT 20` und `MRS` laufen auf 710 und 710-1 im Emulator
> Punkt 4 (Zusatzprogramme) ist noch nicht erledigt.
>
> **Prüfung der gebauten Disketten (2026-10-06, Emulator, 710 und 710-1):**
> - **UDOS-Dateiangaben** (Typ, Eigenschaften, ENTRY/Startadresse, Satzlänge, Segmente, LOW/HIGH/STACK, Zusatz, Datum):
>   alle 74 Dateien je Diskette stimmen mit den Originalen überein; `EXTRACT ZDOS` bzw. `EXTRACT MRS` liest sie
>   aus dem Kopfsektor genauso zurück.
> - **Kommandos** `CAT`, `STATUS`, `LADT`, `DISPLAY`, `ERROR`, `DATE`, `EXTRACT`, `DUMP`, `PRINT`, `DEFINE`, `HELP CAT`
>   liefern auf beiden Geräten dieselbe Ausgabe (bis auf den Diskettennamen); `HELP CAT` ist gleich der Originaldiskette.
>   `HELP *` zeigt eine Seite und hält an (wartet auf eine Taste) — Verhalten des Programms, nicht der Diskette.
> - **`ASM` → `LINK`** mit eigenem Quelltext: Durchlauf 1 und 2, `TEST.OBJ` und das gebundene `TESTP` (Segment 4000–4006) werden
>   auf die Diskette geschrieben.
> - **Programme starten** (Meldung/Menü kommt): `SCREEN` 1.5, `PROG` V3.1, `PROM` V2.3, `EDI` (verlangt Dateinamen), `FORMAT`,
>   `COPY.DISK`, `TAPE.READ`, `FILE.DEBUG`, `KEY`, `MRS` (Menü auf den neuen Disketten; Punkt 2 und 4 liefen auf der Original-MRS-Diskette, Punkt 3 fehlt, s. o.).
> - **`SPLIT 8`** begrenzt auf beiden Geräten den Rollbereich auf die untersten 8 Zeilen.
> - **Nicht geprüft:** `MPSS` mit einem echten Quelltext (`MPSS QUELLE` meldet „Quelltext: Datei nicht gefunden“, obwohl die Datei da
>   ist; Aufrufsyntax ohne Handbuch unbekannt, im Programm steht der Dateiname `MPSS.BAF`, der auf keiner Diskette vorkommt),
>   `MRS` Punkt 1 (braucht die Steuerung an X4), `PROM` mit dem EPROMmer, die Treiber (`SD`, `PTAPE.*`, …) am Gerät.

Gemeinsamer Bestand (gleiche Dateien auf beiden, nur `OS` und Boot-Spur je Gerät):

1. **Pflicht, schon da:** `OS`, `ZDOS`, `OS.INIT` (neu), alle Standardkommandos, `HELP*`, `ASM*`, `LINK`,
   `SCREEN` + Dokumentation, `PROG` + `OVR.*` + `PROG.DOK`, Druck- und Lochbandtreiber.
2. **Angleichen:** `SPLIT` (89er), `DRUCK.IFSS` auch auf P71, `PTAPE.S1`/`TREAD.CON337`/`TWRITE.DT105` auch
   auf P7, Dokumente (`DRUCK.DOK`, `NOTE.TO.UDOS.4.3`, `PROG.DOK`, `SCREEN.*`) auch auf P7.
   Eigenschaften vereinheitlichen (kein `S` auf P71).
3. **MRS-Umgebung übernehmen (Entscheidung des Anwenders):** `MRS`, `MRS4.OVL1/2/4`, `MPSS`, `EDI`, `PROM`,
   `MPE32.1`, `MPEA1.1`, `MPAT8`, `NULL`, `EXTRAM`, `ADM_O2` (§3.7) — der Zweck des Geräts. Fehlt: `MRS4.OVL3/5–9`.
4. **Zusätzlich lohnend:** `BASIC`, `EDIT`, `EPROM43`, `LW`, `REORG`, `TRANSFER`, `SYD`.
5. **Nicht übernehmen:** alles aus NDOS-Familien (PC, W), Fremdanwendungen (URGAN, KDE/KDP/NEBS,
   TERMINE), Reste (`OS.INIT.OLD`, `TEXT.TXT`, `S1`).

Offen vor dem Bau: Lauftest von `BASIC`, `EDIT`, `PROM`, `EPROM43`, `LW`, `REORG`, `SYD` und `SPLIT 89` am
710 **und** 710-1 im Emulator; Zweck von `GCL`, `LZP`, `ZOCS`, `TZOCS`, `MPAT8`, `MPE32.1`, `MPEA1.1`, `NULL`,
`EXTRAM`, `ADM_O2` klären (nur Namen und Zeichenketten ausgewertet, keine Disassemblierung).

## 5. Grenzen dieser Auswertung

- Zwecke stammen aus `HELP.DAT.*`, den mitgelieferten Dokumenten und den Programmkennungen in den Dateien
  (`strings`). Wo nichts davon etwas hergab, steht „nicht ermittelt“. Das gilt auch für `ZOCS` und `TZOCS`.
- Ob ein Programm am 710 bzw. 710-1 **läuft**, ist nicht geprüft.
- Dass NDOS-Programme (PC 1715, P8000) unter ZDOS nicht laufen, ist eine Vorsicht, kein Messergebnis.
- Die Dateizählungen beziehen sich auf die extrahierten Abbilder; Dateien mit Namen, die das DiskTool in
  UDOS-Unterverzeichnisse (`Side0`/`Side1`) legt, wurden unter ihrem Basisnamen zusammengefasst.

## 6. UDOS-Entwicklerdiskette (**aufgegangen in §7, 2026-10-08**: `disks/udos_entwickler_k5601.hfe` und `tools/udos_entwickler/` sind entfernt, in git-Verlauf bis `bc86cb9`)

Gebaut aus `tools/udos_entwickler/` (`build.py`, Wächter `cli_udos_entwickler`; gleiches Schema wie die PRG-Disketten, §4).
Eine **nicht startfähige Datendiskette** (UDOS 4.3, `udos_ds77`, beidseitig) für das **zweite Laufwerk** jeder Maschine mit
ZDOS und K5601-Diskette: **A5120, PRG 710/710-1, USAR**. UDOS findet Programme auf allen aktiven Laufwerken (kein
Laufwerkszusatz nötig). Nicht für PC 1715 und P8000: dort liegt NDOS auf anderem Diskettenformat (§1); eine
UDOS1715-Entwicklerdiskette wäre ein eigener Schritt.
Die Software gilt laut Anwender (2026-10-06) als gemeinfrei und darf so verwendet werden.

**Seite 0 — Werkzeuge** (201 KB)

| Datei | Zweck |
|---|---|
| `ASM`, `ASM2`, `ASM3` | Z80-Assembler 5.9 (Durchläufe, Querverweisliste); Aufruf `ASM quelle (NOL)` |
| `LINK` | Binder 1.7; Aufruf `LINK modul… (NOM ST=0 N=name)` |
| `EDIT` 2.1, `SEDIT` 2.87 | Editoren (`SEDIT` = „SDL-Editor“, Bildschirmeditor) |
| `EDI` + `ADM_O2` | Editor (IfR Berlin 1982); **braucht den Bildschirmtreiber `ADM_O2`**, den er selbst nachlädt — ohne ihn „ADM_O2 ERROR C7“ |
| `SYD` | Symbolischer Debugger („extended by S&T“), Speicherbelegungsanzeige |
| `BASIC` | BASIC-Interpreter (UDOS BASIC, BCD-Arithmetik, 30 KB, 1978) |
| `PLZSYS`, `PLZCG`, `PLZCG.OVLY1.DS`, `PLZCG.OVLY2.DS`, `PLINK` | Zilog-PL/Z-Compiler 3.0 mit Überlagerungen und Binder |
| `TRANSFER` | Übertragen zwischen UDOS- und CP/M-Disketten (ASCII, Hex; Burkhardt/Schütz 1986) |
| `REORG` | Diskettenbelegung reorganisieren |
| `LW` | Laufwerkskonfiguration anzeigen/ändern |

**Seite 1 — Systemgenerierung und Dokumente** (103 KB): `SG` (baut den physischen Systemkern `POS` für Tastatur, Bildschirm,
Drucker und Laufwerke), seine Quellen und Objekte (`POS.INIT.S/.OBJ`, `POS.TYP.S`, `BAB.S/.OBJ`, `CTRL.S`, `DEBUG.OBJ`,
`FLOPPY.OBJ`, `ENTRYS.OBJ`, `DRUCK.*.OBJ`, `PRINTER.OBJ`, `TAST.76xx.OBJ`, `SYL0`, `SYL1`, `BOOT`, `LINK.PRINTER`), **neun
fertige Kerne** `POS_*` (Tastatur/Drucker/Bildschirmkombinationen, z. B. `POS_34_V24_F800H8024_3330`), `NOTE.TO.SG`,
`NOTE.TO.UDOS.4`, `NOTE.TO.UDOS.4.2`, `UDOS4.1.DOK`.

**Geprüft (2026-10-06, Emulator, Systemdiskette in LW 0, Entwicklerdiskette in LW 1):**
alle Werkzeuge starten auf **A5120, PRG 710 und PRG 710-1** mit ihrer Meldung bzw. Eingabeaufforderung (`ASM`/`LINK`/`PLINK`
verlangen einen Dateinamen, `EDIT`/`SEDIT` fragen `NAME?`, `SG` zeigt das Auswahlmenü, `LW` zeigt beide Laufwerke zweiseitig mit je 80
Spuren), die Ausgabe ist auf PRG 710 und 710-1 gleich. `BASIC` rechnet `PRINT 2+3` → `5` (A5120 und beide PRG). `ASM`→`LINK` mit
eigenem Quelltext lief mit denselben Dateien am PRG 710 (§4). **Nicht geprüft:** ein echter Übersetzungslauf von `PLZSYS`/`PLZCG`,
`SG` bis zum fertigen Kern, `SYD` im Einsatz, `TRANSFER` mit CP/M-Diskette, `REORG` über die Abfrage hinaus; `SEDIT` mit
den Tastaturtypen der verschiedenen Geräte.

**Bewusst nicht aufgenommen:** `GCL`, `LZP`, `ZOCS`, `TZOCS` (Zweck nicht ermittelt), `UPRO`/`ESPRO` (EPROM-Programmierer, andere
Hardware), `COPYSD`, `RAMFL`/`RFA`/`RAMTEST`/`DZR.ARAM.OBJ` (RAM-Disketten-Treiber, gerätegebunden), die Anwendungen
`KDE`/`KDP`/`NEBS`/`URGAN`, und alles aus NDOS-Familien.

## 7. UDOS-Systemdiskette des A5120 mit Entwicklerwerkzeugen (`disks/a5120_udos43_k5601_entwickler.hfe`)

Gebaut aus `tools/udos_a5120/` (`build.py`, Wächter `cli_udos_a5120`, Schema wie §4/§6). Damit hat jede der drei
UDOS-Maschinen **eine** Bootdiskette (A5120, PRG 710, PRG 710-1); die frühere Entwicklerdiskette (§6) ist entfernt, ihr Inhalt liegt auf der A5120-Diskette. Die PRG-Disketten tragen die Werkzeuge nicht (kein Platz); bei Bedarf aus der A5120-Diskette holen.

- **Bestand:** alles von `udos_boot_scp.hfe` (UDOS BC.5120, 08/90; Systemspuren `boot_udos43.bin`), außer `UPRO`/`ESPRO`
  (EPROM-Programmierer, andere Hardware) und den Dateien, die die Entwicklerdiskette gleichen Inhalts mitbringt
  (`ASM`, `ASM2`, `ASM3`, `LINK`, `NOTE.TO.UDOS.4`, `.4.2`). `EDIT` bleibt das der A5120 (unterscheidet sich vom
  `EDIT` der Entwicklerdiskette).
- **Dazu:** alles von der Entwicklerdiskette. **Seite 0:** Systemkern, Kommandos, `ASM`/`LINK`, PL/Z (`PLZSYS`, `PLZCG`
  samt Überlagerungen, `PLINK`), `REORG`, `LW`. **Seite 1:** `HELP`, Treiber, Dokumente, `EDI`+`ADM_O2`, `SEDIT`, `SYD`,
  `BASIC`, `TRANSFER`, `SG` mit Quellen und neun fertigen Kernen. Seite 0 war mit allem zu voll (266 KB gegen ~249 KB).
- **Belegung:** Seite 0 212 KB (37 KB frei), Seite 1 234 KB (16 KB frei), `check --full` ohne Befund.
- **Geprüft (2026-10-08, Emulator, A5120, Kaltstart von dieser Diskette allein):** Boot bis `UDOS BC.5120`; `STATUS`, `CAT`,
  `HELP`, `ASM`, `LINK`, `EDIT`, `SCREEN`, `SG`, `LW`, `REORG`, `TRANSFER`, `PLZSYS`, `PLINK`, `SYD`, `SEDIT`, `EDI` (Seite 1,
  lädt `ADM_O2`) melden sich; `ASM TEST.S` → `LINK TEST.OBJ` erzeugt `TESTP`; `BASIC` rechnet `PRINT 2+3` → `5`.
  **Nicht geprüft:** ein Übersetzungslauf von `PLZSYS`/`PLZCG` mit echtem PL/Z-Quelltext, `SG` bis zum fertigen Kern,
  `SYD`/`EDI` im Einsatz, `TRANSFER` mit CP/M-Diskette. Neu bauen: `python3 tools/udos_a5120/build.py --tool build/k1520disktool`.

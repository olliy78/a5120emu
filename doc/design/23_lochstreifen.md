# 23 — Lochstreifen: SIF1000-Karte K6022 als Option in allen drei Maschinen

Stand: 2026-10-05, Zweig `SIF1000`.  Entscheidungen des Anwenders vom 2026-10-05 in §2.

## 1. Ziel

Die ADA **K6022** (zwei SIF1000-Anschlüsse: Lochbandleser daro 1210, Lochbandstanzer
daro 1215; `core/cards/k6022/`, Befunde `doc/prg710/sif1000_fernschreiber.md`) steckt
bisher **fest** im PRG 710/710-1 und ist dort nur über *Maschine ▸ Lochband* bedienbar.
Künftig:

- **steckbar** in A5120 (auch A5120.16), K8915 und PRG 710/710-1 — gewählt unter
  *Einstellungen ▸ Allgemein* (Kästchen „Lochstreifen (SIF1000, K6022)“), wie die RAF;
- mit gesteckter Karte eine weitere **Karteikarte „Lochstreifen“** neben Laufwerke,
  Einstellungen, EPROMmer, darin zwei Blöcke wie die Laufwerke:
  **„Lochstreifenleser“** (Eingabedatei) und **„Lochstreifenstanzer“** (Ausgabedatei);
- beim Öffnen einer Datei ein **Formatdialog** (Roh, Intel HEX, ASCII-Art, §4).

## 2. Entscheidungen (Anwender, 2026-10-05)

| # | Frage | Entscheidung |
|---|-------|--------------|
| E1 | Dateiformate | **Roh** (Vorgabe, Austausch mit anderen Emulatoren) + **Intel HEX** + **ASCII-Art** |
| E2 | Bedeutung von Intel HEX | **Adresse = Bandposition**; Lücken lesen sich als 00H, gestanzt wird ab Adresse 0 |
| E3 | Vorgabe / altes Menü | **Überall Vorgabe aus** — auch im PRG 710; *Maschine ▸ Lochband* entfällt |
| E4 | Ausgabedatei des Stanzers | **laufend mitschreiben** (wie eine gemountete Diskette), Knopf „Neues Band“ |

Gesetzt ohne Rückfrage (mit Begründung):

- **Fest E0H–E7H in allen Maschinen** (wie die RAF auf 88H/89H).  Frei: A5120 belegt
  00–1F, 50–5F, 88/89, A8–AF; K8915 40–67, 80–83, 88/89, A8–AB.  `Config::basis` bleibt
  nur für Tests.
- **Interruptkette: hinten angehängt** (niedrigste Priorität; am PRG wie bisher Stanzer
  vor Leser).  Ohne Gasttreiber im A5120/K8915 ist die Stellung unbelegt [?].
- **Kein Bestandteil des Save-State** (wie bisher am PRG; Band und Stanzband gehören den
  Geräten, nicht der Maschine).  Ein Stand, der mit Karte gesichert wurde, lädt ohne sie.

## 3. Kern: Bestückung

`K1520Machine::installK6022()` nach dem Muster `installRaf` (`core/machines/machine.h`):
nach dem Anlegen, vor dem ersten `run()`/`reset()`; danach `false` + `k6022Fehler()`.
Die Maschine nimmt die beiden PIOs in ihre Interruptkette auf, ruft `clockTick` im
Laufweg und `reset()` beim systemweiten /RESET.  Der PRG 710 verliert sein festes
`k6022_`-Glied; alle PRG-Tests und Werkzeuge, die das Lochband benutzen, stecken die
Karte ausdrücklich (`boot_trace`/`k1520dbg` `--ptape`).

## 4. Bandformate (`core/peripherals/lochstreifen/band_format.{h,cpp}`)

Ein Band ist eine Folge von Sprossen (Zeilen), je 8 Datenspuren = 1 Byte, Spur 1 = Bit 0.
Drei Formate, je `lesen(Text/Bytes) → vector<uint8_t>` und `schreiben(vector) → Bytes`,
und `erkennen(Dateiinhalt)` für die Vorauswahl im Dialog:

- **Roh** (`.ptp`, `.bin`, Vorgabe): ein Byte je Sprosse, ohne Kopf.  Erkannt, wenn
  weder Intel HEX noch ASCII-Art passt.
- **Intel HEX** (`.hex`, `.ihx`): Satzarten 00, 01, 02, 04 (05/03 werden überlesen);
  Prüfsumme wird geprüft, Fehler mit Zeilennummer.  Adresse = Bandposition, Lücken = 00H.
  Geschrieben: 16 Bytes je Satz ab Adresse 0, über 64 KiB mit Satzart 04, Abschluss
  `:00000001FF`.  Erkannt, wenn jede nichtleere Zeile mit `:` beginnt.
- **ASCII-Art** (`.txt`, `.tape`): der Streifen läuft von oben nach unten, Spur 8 links,
  die **Transportspur zwischen Spur 3 und 4** (ISO 1154 / DIN 66016-Anordnung).
  ```
  ; K1520-Lochstreifen, 8 Spuren, ASCII-Art (doc/design/23_lochstreifen.md)
  ; Eine Zeile = eine Sprosse.  O = Loch, o = Transportloch.
  ;
  ;   8 7 6 5 4   3 2 1
     | . . . . . o . . . |  00
     | . O . . . o . . O |  41  A
     | O . . O . o . O O |  93
  ```
  Kommentarzeilen beginnen mit `;`.  Datenzeile: zwischen den beiden `|` stehen die acht
  Spuren und das Transportloch in festen Spalten; `O`/`X`/`*`/`#` = Loch, `.`/Leerzeichen
  = kein Loch.  Rechts daneben der Wert hexadezimal, bei druckbarem 7-Bit-Zeichen dazu
  das Zeichen.  **Beim Lesen zählt das Lochbild**; ein danebenstehender Hexwert, der nicht
  passt, ist ein Fehler mit Zeilennummer (schützt Handbearbeitung vor stillen Fehlern).
  Erkannt an einer Datenzeile der Form `|…|` vor der ersten Nicht-Kommentarzeile.

## 5. Dateibindung

- **Leser:** Datei + Format → Inhalt einmal gelesen und eingelegt (wie heute
  `bandEinlegenDatei`); Stellung, Länge, Bandende wie bisher.
- **Stanzer:** Datei + Format wird **gebunden**.  Eine vorhandene Datei wird gelesen und
  ist der Anfang des Bandes (wie eine gemountete Diskette — weiterstanzen hängt an);
  „Neues Band“ leert Band und Datei.  Zurückgeschrieben wird **verzögert** nach einer
  Stanzpause von ≈ 0,5 s Maschinenzeit (Muster `DiskImage::autoFlush`) sowie beim Lösen
  der Bindung, beim Wechsel der Datei und beim Zerstören der Maschine.  Ohne Bindung
  sammelt der Stanzer wie bisher im Speicher.

## 6. C-ABI und Python

`k1520_ptape_install`/`k1520_ptape_installed`/`k1520_ptape_error`; Laden und Binden mit
Formatkennung (`K1520_PTAPE_FMT_RAW/IHEX/ASCII`), Erkennung `k1520_ptape_detect_format`,
Status von Leser (Datei, Format, Stellung) und Stanzer (Datei, Format, Länge, ein/aus).
Bestehende `k1520_ptape_*` bleiben (Roh-Format), liefern ohne Karte `false`/`-1`.
Python `K1520Emulator(ptape=True)`; die Formate zusätzlich als reine Python-Funktionen
sind **nicht** vorgesehen — es gibt eine Umsetzung, im Kern.

## 7. Oberfläche

- *Einstellungen ▸ Allgemein*: Kästchen „Lochstreifen (SIF1000, K6022)“ in allen drei
  Profilen; Wechsel baut die Maschine neu (wie RAF, `rafChanged`), Konfiguration
  `general.ptape: true|false`, Vorgabe aus.
- Dock **„Lochstreifen“** (`app/ui/lochstreifen_widget.py`), nur mit gesteckter Karte, an
  die Laufwerke angedockt (tabify).  Zwei `QGroupBox`: **Lochstreifenleser** (Datei,
  Format, „Öffnen…“, „Entnehmen“, Fortschritt `gelesen / Länge`, Bandende) und
  **Lochstreifenstanzer** (Datei, Format, „Öffnen…“, „Lösen“, „Neues Band“, Länge,
  Kästchen „Stanzer ein“).  Nach jeder Dateiauswahl der Formatdialog
  (`app/ui/lochstreifen_format_dialog.py`) mit Vorauswahl aus Inhalt (Leser) bzw.
  Endung (Stanzer).
- Dateien des Lesers und Stanzers samt Format werden in der Konfiguration gemerkt und
  beim Start wieder gebunden (wie `disks`).
- *Maschine ▸ Lochband* und die Aktionen `band_einlegen` … `stanzer_ein` entfallen,
  `app/ui/lochband.py` geht auf im neuen Widget.  Keine Tastenkürzel.
- Handbuch: Abschnitt „Lochstreifen“ (ersetzt „Lochband und Fernschreiber“ für den
  Lochband-Teil; der Fernschreiber bleibt ein Anschluss im Reiter „Schnittstellen“).

## 8. Arbeitspakete

| AP | Inhalt | Wächter |
|----|--------|---------|
| L1 | Kern: `installK6022` in `K1520Machine`, A5120 (+.16), K8915, PRG ohne feste Karte; Kette, Takt, Reset; Tests auf ausdrückliches Stecken umstellen; `--ptape` in `boot_trace`/`k1520dbg` | `K6022Maschine.*` (alle Maschinen: stecken, Port belegt, zu spät, Z80-Ausgabe/Eingabe über E0H/E4H), bestehende `Prg710Lochband.*` |
| L2 | Bandformate §4 + Stanzer-Bindung §5 im Kern | `Lochstreifenformat.*` (Hin- und Rückweg je Format, Fehler mit Zeile, Erkennung), `K6022Bindung.*` |
| L3 | C-ABI + `app/core_binding/k1520.py` §6 | `py_c_api` (dreiseitiger Abgleich), `py_ptape_api` |
| L4 | Oberfläche §7, Handbuch | `py_lochstreifen_gui`, Kürzel-/Handbuch-Wächter |
| L5 | Abschluss: Merkposten `doc/merkposten/lochstreifen.md`, CLAUDE.md, alte Verweise (prg710-Merkposten), vier Lanes | — |

## 9. Stand

(je AP nachgetragen)

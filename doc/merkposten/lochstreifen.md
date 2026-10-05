<!-- Angelegt 2026-10-05 (Abschluss Entwurf 23, doc/design/23_lochstreifen.md).  Diese Datei
     gilt WIE CLAUDE.md, sobald am Lochstreifen (K6022/SIF1000) gearbeitet wird — sie ist nur
     nicht in jeder Anfrage geladen.  Begruendung: doc/merkposten/README.md -->

# Lochstreifen K6022 / SIF1000 — Merkposten

ADA **K6022** (zwei SIF1000-Anschlüsse: Lochbandleser daro 1210, Lochbandstanzer daro 1215)
als **steckbare Option** in allen drei Maschinen (A5120 auch als A5120.16, K8915,
PRG 710/710-1), fest auf **E/A E0H–E7H** (Stanzer E0H–E3H, Leser E4H–E7H).  Karte
`core/cards/k6022/`, Bandformate `core/peripherals/lochstreifen/band_format.{h,cpp}`
(Namensraum `lochstreifen`), Bestückung `K1520Machine::installK6022()`
(`core/machines/machine.h`), C-ABI `k1520_ptape_*`, Python `K1520Emulator(ptape=True)`,
Oberfläche *Einstellungen ▸ Allgemein ▸ Lochstreifen (SIF1000, K6022)* + Kasten
„Lochstreifen“ (`app/ui/lochstreifen_widget.py`, Formatdialog
`app/ui/lochstreifen_format_dialog.py`), Werkzeuge `boot_trace`/`k1520dbg --ptape`.
Entwurf und Stand je AP: **`doc/design/23_lochstreifen.md`**; SIF1000-Handschlag, Treiber
`PTAPE.6022` und UDOS-Bedienung (`F=A`): **`doc/merkposten/prg710.md`** Abschnitt
„Lochband K6022 und Fernschreiber 590069“ und `doc/prg710/sif1000_fernschreiber.md`.

## Festlegungen, die man nicht aufweichen darf

| # | Festlegung | Wächter |
|---|------------|---------|
| 1 | **Gesteckt wird nach dem Anlegen, vor dem ersten `run()`/`reset()`** — danach, doppelt oder bei belegtem Tor `false` + `k6022Fehler()` (C-ABI `k1520_ptape_error`).  Die Tore werden **vor** dem Anmelden geprüft (`K1520Bus::ioOwner`), sonst blieben bei belegtem Tor halb angemeldete Ports zurück.  Keine weitere `k1520_create_*`-Variante. | `K6022Maschine.A5120`/`A5120_16`/`K8915`/`Prg710`/`Prg710_1` (zu spät, doppelt, Tor belegt), `py_ptape_api` (`test_doppeltes_stecken_setzt_den_fehlertext`) |
| 2 | **Überall E0H–E7H, nicht konfigurierbar** (wie die RAF auf 88H/89H); `K6022::Config::basis` nur für Tests.  Frei sind die Tore in allen Maschinen (A5120: 00–1F, 50–5F, 88/89, A8–AF belegt; K8915: 40–67, 80–83, 88/89, A8–AB). | `K6022Maschine.*_Z80StanztUndLiest` (Gastcode über E0H/E4H) |
| 3 | **Interruptkette:** am A5120/K8915 **hinten angehängt** (`K1520Bus::appendInterruptChain`, Vorgabe von `k6022Einketten()`); der PRG 710 überschreibt das und setzt seine Kette neu mit der K6022 **vor** der 590069, Stanzer vor Leser (Stellung wie vor Entwurf 23).  Am A5120/K8915 gibt es keinen Gasttreiber, die Stellung ist dort unbelegt [?]. | `K6022Maschine.*_Z80StanztUndLiest` (END als IM-2-Interrupt), `Prg710Lochband.*` |
| 4 | **Takt und Reset im Laufweg jeder Maschine** (`k6022Takt()`/`k6022Reset()`); RESET lässt Band und Stanzband liegen — sie gehören den Geräten. | `K6022Test.BaenderAlsDateiUndResetLaesstSieLiegen`, `K6022Maschine.*` |
| 5 | **Nicht im Save-State** (Entwurf §2): ein mit Karte gesicherter Stand lädt auch ohne sie.  Kein eigener Wächter — wer die Karte doch in den Stand nimmt, braucht eine neue Fassungsnummer (vgl. RAF, v8). | — |
| 6 | **Formatkennungen Roh = 0, Intel HEX = 1, ASCII-Art = 2 sind ein Vertrag** (`lochstreifen::Format` = `K1520_PTAPE_FMT_RAW/IHEX/ASCII` = `PTAPE_FMT_*` in Python = Wert in der Konfiguration). | `Lochstreifenformat.KennungenSindStabil`, `py_ptape_api` (`test_formatkennungen_stimmen_mit_dem_header_ueberein`), `py_c_api` |
| 7 | **Intel HEX: Adresse = Bandposition**, Lücken lesen sich als 00H, gestanzt wird ab Adresse 0 in 16-Byte-Sätzen, über 64 KiB mit Satzart 04, Abschluss `:00000001FF`.  Gelesen werden 00/01/02/04 (03/05 überlesen), Prüfsumme mit Zeilennummer; fehlender Satz 01 wird hingenommen, Bänder über 16 MiB abgewiesen. | `Lochstreifenformat.IntelHex*` |
| 8 | **ASCII-Art: das Lochbild ist maßgeblich.**  Genau 19 Zeichen zwischen den `|` in festen Spalten (Spur 8 links, Transportloch zwischen Spur 3 und 4), ein Zeichen in einer geraden Spalte = verrutscht = Fehler.  Ein danebenstehender Hexwert, der nicht passt, ist ein Fehler mit Zeilennummer; fehlt er, ist das recht.  Datei reines ASCII, geschrieben mit LF, gelesen auch CRLF.  Die Form ist mit einem Goldwert festgenagelt. | `Lochstreifenformat.AsciiArtGoldwert`, `…AsciiArtFehlerMitZeilennummer`, `…AsciiArtTolerantBeimLesen`, `…AsciiArtHinUndZurueck` |
| 9 | **Erkennung:** Intel HEX, wenn jede nichtleere Zeile mit `:` beginnt; ASCII-Art an einer `|…|`-Zeile vor der ersten Nicht-Kommentarzeile (oder lauter Kommentare = leeres Band); sonst Roh.  Der Formatdialog wählt aus dem Inhalt einer vorhandenen, nicht leeren Datei vor, sonst aus der Endung — für Leser und Stanzer gleich. | `Lochstreifenformat.Erkennung`, `…Endungen`, `py_ptape_api` (`test_format_aus_dem_inhalt`, `test_format_nach_endung`) |
| 10 | **Leser:** Datei einmal gelesen und eingelegt; Vor- und Nachlauf (16/128 Nullbytes) setzt die Karte selbst davor/dahinter.  **Stanzer: die Datei wird GEBUNDEN** wie eine gemountete Diskette — eine vorhandene Datei ist der Anfang des Bandes (weiterstanzen hängt an), „Neues Band“ leert Band **und** Datei, Lösen behält das Band im Speicher. | `K6022Bindung.WeiterstanzenHaengtAnVorhandeneDateiAn`, `…NeuesBandLeertDieDatei`, `…LeserLiestIntelHexUndAsciiArt`, `K6022Test.LeserLiefertVorlaufInhaltNachlaufUndBandende` |
| 11 | **Zurückgeschrieben wird verzögert, in Maschinenzeit, im Lauffaden:** `clockTick` stellt nur fest, dass die Stanzpause (`kStanzpauseS` = 0,5 s Maschinenzeit) abgelaufen ist; geschrieben wird in `K6022::autoFlush()` am Ende jeder `run()`-Scheibe (`k6022AutoFlush()` in allen drei Maschinen, Vorbild `Laufwerke::autoFlush`) — keine Datei-E/A im Pfad je Instruktion.  Sofort beim Lösen, beim Wechsel der Datei und im Destruktor; die Oberfläche schreibt außerdem vor jedem Neuaufbau der Maschine und beim Beenden zurück. | `K6022Bindung.StanzerSchreibtNachDerStanzpause`, `…LoesenWechselUndDestruktorSchreiben` |
| 12 | **Konfiguration:** `general.ptape: true|false` (Vorgabe aus in allen drei Programmen, auch im PRG 710; ausdrücklich `false` in den drei `data/default_config_*.yaml`); Abschnitt `lochstreifen: {leser: {datei, format}, stanzer: {datei, format}}` — **fehlt er, bleibt alles** (Muster `disks`), fehlende Datei beim Start → Statuszeile.  Ein Wechsel des Kästchens baut die Maschine neu (wie die RAF); ohne Karte bleibt der letzte Stand gemerkt und wird beim Wiederanstecken neu gebunden. | `py_lochstreifen_gui` (`test_kaestchen_in_jedem_programm_vorgabe_aus_und_dock_folgt`, `test_wiederherstellung_aus_der_konfiguration`, `test_auslieferung_hat_die_karte_aus`) |
| 13 | **Kasten „Lochstreifen“ nur mit gesteckter Karte** — sonst samt *Ansicht*-Eintrag ausgeblendet, auch nach dem Wiederherstellen eines gespeicherten Kastenlayouts.  **Kein Tastenkürzel**, kein Eintrag in der Symbolleisten-Auswahl; *Maschine ▸ Lochband* und `app/ui/lochband.py` gibt es nicht mehr. | `py_lochstreifen_gui` (`test_keine_alten_lochband_aktionen_und_keine_kuerzel`), `test_no_shortcut_steals_a_key_from_the_emulated_machine`, Kürzeltabellen-Wächter in `py_gui_smoke` |

## Bekannte Eigenheiten der Oberfläche (gewollt)

- „Neues Band“ fragt nach, wenn das Band etwas trägt — es leert eine gebundene Datei.
- Nach einem Neuaufbau der Maschine fängt der Leser von vorn an; ein **ungebundenes**
  Stanzband geht dabei verloren (gebundene sind vorher zurückgeschrieben).
- `k1520_ptape_load`/`k1520_ptape_punch_save` bleiben Roh; `k1520_ptape_punch_clear` =
  `new_tape`.  Datei-Rückgaben als `const char*` aus einem faden-lokalen Puffer.

## Offen

- Stellung in der Interruptkette am A5120/K8915 [?] — ohne Gasttreiber unbelegt.
- Kein Gasttreiber für A5120 (CP/A) und K8915 (SCPX); belegt ist das Band nur am PRG
  (UDOS `PTAPE.6022`, `F=A`).  Bis dahin prüfen `K6022Maschine.*` mit eigenem Z80-Code.
- Save-State ohne Karte (Festlegung 5) — bei Bedarf eigener Teil mit neuer Fassung.
- Anwenderfragen zur K6022 (KOM/STA, Geschwindigkeiten daro 1210/1215): Plan PRG 710 §8.8.

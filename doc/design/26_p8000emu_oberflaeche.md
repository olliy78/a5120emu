# 26 — Die Oberfläche des P8000 Emulators (`p8000emu`, AP P16)

Fünftes Gesicht des gemeinsamen Hauptfensters (`app/ui/main_window.py`), wie K8915/PRG710/PC1715
(`doc/design/18_k8915emu_oberflaeche.md`, `doc/design/11_python_app.md` §10).  Stand 2026-10-07.
Gegenstand: Plan `doc/design/25_p8000.md` §9 (P16), Kern-API §10.9, Merkposten `doc/merkposten/p8000.md`.

## 1. Profil (`app/profil.py`, `P8000`)

| | |
|---|---|
| Programm / Titel | `p8000emu` / „P8000 Emulator" (`--machine p8000`, Starter `run_p8000emu.sh`) |
| Konfiguration | `p8000emu.yaml`; Auslieferung `data/default_config_p8000.yaml` (ohne `window.geometry`, `disks`, `platte`, `dock_state`) |
| Takt | 4 MHz; Vorgabe ×5 (Hardwaretest ≈ 16 s Maschinenzeit; der Kern schafft ≈ 15 × Echtzeit) |
| Neue Profilfelder | `terminal`, `platte`, `ptape_wahl` (am P8000 nein), `frontplatte_art`, `modell_kern`; Methoden `kern_parameter()`, `modell_hat_wdc()` |
| Aktionen | `nmi` (jetzt K8915 **und** P8000), `stand_speichern`, `stand_laden` (nur P8000, **ohne Kürzel** — die Kürzeltabelle des Handbuchs ist ein Vertrag), `p8000emu` (in den anderen Programmen unter *Werkzeuge*) |

**Modellwahl** (`general.model`, Schlüssel technisch, Texte Gerätesprache): `p8000` = Vollgerät (16-Bit-Teil +
Winchester, **Vorgabe**), `p8000-16` = ohne Winchester, `p8000-8` = nur 8-Bit-Teil.  Alle drei sind dieselbe
Kernmaschine; `modell_kern` setzt `karte16`/`wdc`.  **ROM-Fassung und Platinenindex** sind die
`hardware`-Wahl des Profils (derselbe Mechanismus wie PC 1715: Zeichensatz/Tastatur): `index` (`34` = 8-Bit 3 /
16-Bit 4, `11` = beide 1), `mon8` (3.1/3.0), `mon16` (3.1/3.0/3.3), `dram` (`1M@0`, `256K@0`, `1M@0+1M@1`), `wdc`
(4.2/4.0.05/3.4.05).  Wo eine Wahl am Modell nichts bewirkt, ist das Feld ausgegraut (`hardware_wirkt`).
`Programmprofil.kern_parameter(modell, hardware)` übersetzt das alles in EIN Wörterbuch `p8000={…}` für
`K1520Emulator` — das Hauptfenster ruft nur diese Methode (`_maschine_erzeugen`), kein `if machine == …`.
Wächter `test_jede_hardwarewahl_baut_eine_maschine` baut jeden angebotenen Wert je Modell.

## 2. Das Hauptfenster als Terminalrechner

`profil.terminal` ersetzt `ScreenWidget` durch `TerminalTabs` (`app/ui/p8000_terminal.py`), dieselbe Schnittstelle
(`set_emulator`/`start_display`/`stop_display`/`set_powered`/`params`/`key_sink`/Vollbildsignale), und die
Bildschirmtastatur durch `KeyboardP8000Widget`.  `profil.platte` hängt den Plattenkasten unter die Disketten
(derselbe Kasten „Laufwerke").  Alles Übrige — Aktionen, Symbolleiste, Statuszeile, Konfiguration,
Einstellungen, Schnittstellenreiter, Werkzeugmenü — ist unverändert das gemeinsame Fenster.

## 3. Terminal-Widget (`TerminalWidget`, `TerminalTabs`)

* **Der Kern führt die Textschicht, die Oberfläche zeichnet nur.**  Neue C-ABI `k1520_term_snapshot` liefert das
  ganze Bild (80 × 24 × [Zeichen, wirksames Attribut, Flags]) in EINEM Aufruf — 3840 Einzelaufrufe je Bild wären
  zu teuer; `k1520_term_flags` (On-Line, Video, Programm-Mode, ZG2, Caps), `k1520_term_bell_count`.
* **Zeichensatz aus den EPROM-Abzügen, nicht aus einer Schrift** (Entscheidung): `P8TEZS` (ZG1, ASCII) und
  `P8TDZS` (ZG2, deutsch) als Bitmuster 8 × 12 in `app/ui/p8000_zeichensatz.py`, erzeugt von
  `tools/p8000/zeichensatz_zu_py.py`.  Eine Schrift müsste die Umlaute auf ASCII-Stellen legen und die Zeichen
  00H–1FH des Programm-Modes erfinden; die Abzüge haben beides und sind pixelgleich zum Gerät.  Das Flag
  „mit ZG2 geschrieben" gilt je Zelle (SI/SO schaltet nur, was danach geschrieben wird).
* **Gezeichnet wird in einen 640 × 288-Puffer, nur geänderte Zeilen/Zellen**, skaliert aufs Widget.  Attribute:
  Invers (Farben vertauscht), Blinken (2 Hz), Leer (unsichtbar), Hell (volle Phosphorhelligkeit gegen 72 % normal),
  Unterstrichen (letzte Pixelzeile); Blockcursor als invertierte Zelle (blinkt).  Farben und Helligkeit folgen
  den CRT-Einstellungen (`phosphor_on/off`, `brightness`, `contrast`); Krümmung/Ecken gibt es am Terminal nicht.
* **Reiter je Kern-Terminal**: `TerminalTabs` zeigt `term_count()` Reiter (ein einziger ohne Reiterleiste,
  Name „tty1 (Konsole)", sonst „ttyN").  Heute führt der Kern nur tty1, s. §7.  Unter dem Bild steht die Zeile mit
  **Betriebsart (ADM31/VT100)**, Zeichensatz und Zustand (`modus_text()`).
* **Tasten** (`host_taste`, eigene Funktion → ohne Widget prüfbar): Zeichen, Strg+Buchstabe (Kern bildet das
  Steuerzeichen), Return/Esc/Tab/Backtab/Rücktaste/Entf als Qt-Kodes, Pfeile (BS/VT/FF/LF) und Pos1 als Qt-Kodes,
  F1–F6 = LINE ERASE … CHAR DELETE, F7/Pause = BREAK, F8 = SI/SO, Einfg = CHAR INSERT, Feststelltaste = Rasttaste
  (Zustand aus `term_flags`), ä ö ü ß Ä Ö Ü → ASCII-Stelle des ZG2 (wie ein deutsches Gerät).  Einfügen:
  Umschalt+Einfg, mittlere Maustaste.  Tasten ohne Qt-Gegenstück gehen als `0x02000000 + TerminalTaste` an den Kern
  (additiv in `P8000Machine::keyPress`, Wächter `P8000Machine.TerminalTastenUeberPrivateKodes…`).  Der Kern
  wiederholt gehaltene Tasten nicht — das Widget reicht den Wiederholungsstrom des Wirtsrechners durch.
* **Kürzel-Regel** unverändert: Fensterkürzel nur mit Strg+Umschalt (Ausnahme F11); alles andere, auch Strg+C und
  die F-Tasten, gehört dem Gast.  Die Knöpfe der Leiste und die Reiter nehmen keinen Fokus (`setFocusProxy` auf das
  Terminal des aktuellen Reiters).
* **Funktionstastenleiste** (`KeyboardP8000Widget`, Kasten „Tastatur", vorgabemäßig aus): die Tasten, die es am
  PC nicht gibt (LINE/CHAR INSERT/ERASE/DELETE, PAGE ERASE, Cursor, TAB/BACKTAB, BREAK, MODE, VIDEO, ON/OFF) und die
  Rasttasten SI/SO, CAPS LOCK, deren Stand aus `term_flags` zurückkommt.  Keine Nachbildung des Tastenfeldes — die
  Zeichentasten kommen vom Wirtsrechner.

## 4. Laufwerke, Statuszeile

* Zwei K5601 wie bei den anderen Maschinen (Einlegen/Auswerfen/Leere Diskette, Format wird erfragt, `.hfe`/`.dmk`
  nie).  Beschriftung A:/B: (UDOS zählt 0/1 — offen, §7).
* **Frontplatte** der Statuszeile (`status_bar.FRONTPLATTEN["p8000"]`): **Run** (RUN-LED der 16-Bit-Karte, panel Bit 0),
  **16-Bit** (UNIT16, Bit 1), **Platte** (WDC-Zugriff, Bit 2), **Power**; *aktiv high* (die K8915-Reihe ist aktiv low,
  beide laufen über `Frontplatte(art=…)`).  Ohne 16-Bit-Karte liefert der Kern 0 — die Lampen bleiben dunkel (ehrlich).
* Takt: eingestellter Takt wie überall.

## 5. Plattenkasten (`app/ui/platten_widget.py`)

Anschließen…/Neue Platte… (Typ K5504.50/D5126/D5146/VS)/Abtrennen, Lampe aus `hd_led`, Name + Tooltip mit Pfad.
Festlegungen: **kein Schreibschutz** (der Kern weist `wp` ab); **geschrieben wird verzögert, aber von selbst** — der Kasten
ruft `hd_flush` vor dem Abtrennen, vor einem Zwischenstand und beim Beenden; eine im Betrieb angeschlossene Platte sieht der
WDC erst nach Reset (Hinweistext, **keine** ungefragte Neustart-Automatik); **neu = mit PAR-Sektor, unformatiert**.
**Standardplatte:** beim ersten Start (kein `platte`-Abschnitt in der Konfiguration, Modell mit WDC) wird
`<Diskettenordner>/p8000_platte.img` (K5504.50, 47 MB) angelegt bzw. — falls vorhanden — angeschlossen, **nie überschrieben**;
`platte: {path: …}` steht dann in `p8000emu.yaml`, `path: ""` heißt ausdrücklich „keine Platte" (Abtrennen ist eine
Entscheidung und wird gemerkt).  Die Platte gehört der Maschine: jeder Neubau (Modell-/Hardwarewechsel) hängt dieselbe Datei an.
Wächter `test_erster_start_legt_die_standardplatte…`, `test_abtrennen_ist_eine_entscheidung…`.

## 6. Zwischenstand (P8KS) und Kern-API (additiv)

*Maschine ▸ Zwischenstand sichern…/laden…* → neue C-ABI `k1520_state_save/_load/_error` (nur P8000, sonst false).  Platte
und Floppy werden vorher geschrieben; Medien stehen nicht im Stand — Hinweis im Handbuch.  Zuverlässig nur am Prompt
(Merkposten 16: Zeichen im Flug an tty0/2/3 und in den Hub-Wandlern fehlen im Stand).  Ein Stand abweichender
Konfiguration wird abgelehnt (Meldung mit `state_error()`, Maschine unverändert).  C-ABI-Zuwachs insgesamt:
`k1520_term_snapshot`, `k1520_term_flags`, `k1520_term_bell_count`, `k1520_state_save`, `k1520_state_load`,
`k1520_state_error`; Bindung in `app/core_binding/k1520.py` (`term_snapshot`, `term_flags`, `term_bell_count`,
`state_save/_load/_error`), mechanisch gegen den Header geprüft (`py_c_api`).

## 7. Offen

1. **Weitere Kern-Terminals (tty4–tty7, tty0/2/3 als Terminal).**  Die Oberfläche ist fertig (Reiter je `term_count()`,
   Test `test_reiter_folgen_term_count`).  Im Kern fehlt: `Config::terminals` ≠ 1 (`k1520_create_p8000` weist es ab),
   `P8000Machine` führt genau ein `Terminal`/`TerminalAnschluss` (`term_`, `term_anschluss_`) und
   `KONSOLE_TTY`; alle `k1520_term_*` und `p8000Of` akzeptieren nur `i == 0`.  Nötig: je Bit in `terminals` ein
   `Terminal` + `TerminalAnschluss` an den `SerialAnschluss` des Kanals (tty0/2/3 SIO 8-Bit, tty4–7 SIO 16-Bit nach der
   Reihenfolge von `festeSchnittstellen()`), diese Kanäle NICHT beim `SerialHub` registrieren, `tastenAbgeben` je Terminal,
   `term_count/term_tty` aus der Maske, `P8KS` um die weiteren Terminals erweitern.  Bis dahin: Telnet am Hub mit
   xterm/PuTTY (Reiter *Schnittstellen*).
2. Laufwerksbeschriftung **A:/B:** statt UDOS **0/1** (die Beschriftung steckt in `drive_widget`/`status_bar`/Menü an
   sieben Stellen — ein Profilfeld `laufwerk_namen` wäre der saubere Weg).
3. Zeichen > 7 Bit und Umlaute außer ä ö ü ß Ä Ö Ü; `§` fehlt als Taste (liegt auf `@`).
4. Eine **leere** (E5) Platte lässt MON16 nach dem Hardwaretest in eine Eingabeschleife ohne `*` laufen (Merkposten 21,
   Gastverhalten) — mit der Standardplatte des ersten Starts trifft das jeden Anwender des Vollgeräts; Handbuch weist darauf hin.
   Option für später: Standard ohne Platte starten oder `AUTOBOOT` abschaltbar machen (Frage an den Anwender).
5. Terminal-Klingel (`term_bell_count`) wird noch nicht gehört (K8915 piept über `bell_count`).
6. Nur-8-Bit-ROMs `3.1n`/`2.1n` des Kerns sind nicht wählbar.
7. P18 (Paket): dritter→fünfter Starter `p8000emu`, `data/default_config_p8000.yaml`, `app/ui/p8000_zeichensatz.py` (reiner
   Python-Code, geht mit), `tools/p8000/zeichensatz_zu_py.py` bleibt im Quellbaum.

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
   Gastverhalten).  **Entschieden (P21, Anwender): das Vollgerät startet OHNE Platte** — s. §8.6.
5. Terminal-Klingel (`term_bell_count`) wird noch nicht gehört (K8915 piept über `bell_count`).
6. Nur-8-Bit-ROMs `3.1n`/`2.1n` des Kerns sind nicht wählbar.
7. P18 (Paket): dritter→fünfter Starter `p8000emu`, `data/default_config_p8000.yaml`, `app/ui/p8000_zeichensatz.py` (reiner
   Python-Code, geht mit), `tools/p8000/zeichensatz_zu_py.py` bleibt im Quellbaum.


## 8. Originalterminal, Varianten und Mehrplatz (AP P21, 2026-10-08)

Kern-API: `doc/design/28_p8000_originalterminal.md` §9; Plan `doc/design/25_p8000.md` §11.  Der Kern bleibt unberührt —
alles hier ist Oberfläche (`app/`).

### 8.1 Modelle und Profile

`general.model` kennt jetzt sieben Schlüssel (Reihenfolge = Reihenfolge im Auswahlfeld): `p8000`, `p8000-16`, `p8000-8`
(Kern-Terminal, wie bisher) · `p8000-ot`, `p8000-16-ot`, `p8000-8-ot` (**P8000 + P8000 Terminal**: dieselben drei Untervarianten
mit `terminal=original` an tty1) · `p8000-term` (**P8000 Terminal**: Kernmaschine `p8000-terminal`, nur das Terminal).
Neue Profilfelder (additiv, die anderen Programme sehen sie nicht): `modell_arten` (`kern` | `original` | `einheit`; Methoden
`modell_art`, `modell_original_terminal`, `modell_hat_rechner`, `modell_titel`), `schnittstellen_ports` (Portvorschläge) und
`schnittstellen_hinweis(modell)`.  `modell_maschine()` liefert für `p8000-term` `p8000-terminal` — **der EINE Ort, der aus dem
Modell eine Kernmaschine macht**, bleibt `Programmprofil` (kein `if machine == …` in der Oberfläche).  Die ROM-/Index-Wahl
(`hardware`) ist am Terminal ausgegraut, `kern_parameter("p8000-term")` = `{"p8000": {}}`.

**Eine GUI (P23a, 2026-10-08):** Es gibt nur `p8000emu`.  Das frühere Programm `p8000term` (Profil `P8000TERM`, `run_p8000term.sh`,
`default_config_p8000term.yaml`, Starter/Menüeintrag/Paketzeile) ist entfallen.  Im Einstellungsdialog (*Allgemein*) wählt man
*Rechnerausstattung* (Vollgerät / ohne Winchester / nur 8-Bit; `Programmprofil.ausstattungen()`) und *Betriebsart* („Computer mit
Terminal“ | „nur Terminal“; `betriebsart_wahl`, `einheit_modell()`).  Das Modell in der Konfiguration bleibt EIN Schlüssel
(`-ot`-Modelle bzw. `p8000-term`), `general.ausstattung` merkt die Ausstattung im Terminalbetrieb.  Alte Schlüssel `p8000`/`-16`/`-8`
(Kern-Terminal) werden über `modell_alt` auf `-ot` abgebildet, ein Abschnitt `terminal:` (alte Skalierung) und `machine.raf*` werden beim
Laden übergangen und beim nächsten Speichern nicht mehr geschrieben.  Die Kern-Terminal-Modelle sind nicht mehr wählbar (im Kern bleiben
sie Testgegenstelle).  `--mode computer|terminal` (Umgebung `K1520_BETRIEBSART`) stellt die Betriebsart für den Start vor.  „Nur
Terminal“ blendet den Reiter *Laufwerke*, Laufwerks-/Frontplattenfelder der Statuszeile, NMI, Einlegen/Auswerfen und Zwischenstand aus und
zeigt *Verbindung zum Rechner…*; der Moduswechsel baut die Maschine neu (mit EPROM-Rückfrage wie bei anderen Modellwechseln).  Die RAF
(`raf_wahl=False`) gibt es im P8000 nicht.  Das Terminalbild läuft im gemeinsamen CRT-Widget (§8.2); `app/ui/p8000_terminal.py` (Kern-Terminal-
Widget, Funktionstastenleiste) und der Zeichensatz-Abzug sind gelöscht.

### 8.2 Bildschirm (`app/ui/p8000_original.py`)

`OriginalTerminalWidget` ist seit P23a ein `ScreenWidget` (QOpenGLWidget, CRT-Shader, `CRTParams`) — dasselbe Widget und derselbe
CRT-Reiter wie bei den anderen Maschinen, gleiche Vorgaben.  Es gibt keine Reiter je Terminal mehr (genau ein Originalterminal).
* **Kein Dauer-Upload**: `_on_update()` fragt `term_flags` und `term_frame_count`; das Pixelbild (640 × 312, Stufen 0/1/2) wird nur
  bei geändertem Zähler geholt, über `translate` auf die Textur-Bytes 0/184/255 gebracht und hochgeladen (Texturgröße folgt dem Bild).
  Seitenverhältnis/Füllung wie bei den anderen Maschinen (`ScreenWidget.paintGL`).  Cursor, Blinken, Invers, Hell stecken im Bild.
* **Keine Farb- und Zoomwahl** am Widget mehr; Farbe, Helligkeit, Kontrast, Krümmung usw. nur über *Einstellungen ▸ CRT*.
* **Text kopieren**: Kontextmenü (rechte Maustaste) und *Maschine ▸ Bildschirminhalt als Text kopieren*: `term_text` (80 × 24), Zeilen
  `rstrip`, `\n`, in die Zwischenablage; kein Kürzel.
* Die Funktionsanzeige (Zeichensatz/Caps) geht als `flagsChanged` an die Bildschirmtastatur.

### 8.3 Tastatur: Matrix, Bildschirmtastatur, Wirtstasten

* **`app/ui/k7673_layout.py`** (ohne Qt): `TASTEN` (105 Positionen → Scancode, Beschriftung normal/mit SHIFT, Name), `BILD`
  (Tastenbild als Rechtecke in Tasteneinheiten), `ZEICHEN` (ASCII → Position + Umschalt, aus `NORMAL_Tab`/`SHIFT_Tab` der
  Firmware 5.0; `>` = SHIFT + Taste 56H nach `TGETCHAR`), `UMLAUTE` (ä ö ü ß § Ä Ö Ü → die ASCII-Taste, die der Zeichensatz 2
  zum Umlaut macht: `TGET8` addiert/subtrahiert 20H), `SONDERTASTEN` (Qt-Kode → Scancode).  **Wächter**: die Tabelle ist
  Position für Position gleich `term_matrix_scancode` des Kerns (aus dem EPROM-Abzug), alle 105 stehen genau einmal im Bild.
  Die **Anordnung** des Bildes ist ein Vorschlag (die Tastenkappen kennen wir nicht, `tastatur_k7673.md` §7.2).
* **`KeyboardK7673Widget`**: gemalte Tasten, Klick → `keyPressed(0x04000000 | Zeile << 8 | Spalte)`, Loslassen → `keyReleased`;
  das Hauptfenster setzt beides als `term_matrix_key(0, …)` ab.  **SHIFT (3×) und CTRL rasten** beim Klick (zweiter Klick löst),
  die **rechte Maustaste** hält jede Taste; LEDs (ON/OFF, CAPS LOCK, MODE) aus `keyboard_leds()` im Bildtakt.
* **Wirtstasten** (`OriginalTerminalWidget.keyPressEvent`): Sondertasten über die Tabelle, Zeichen über `ZEICHEN`; Wirts-Shift und
  Strg gehen als SHIFT/CTRL der K7673.  Braucht ein Zeichen SHIFT und der Wirt hält keins (AltGr+Q = `@`), drückt das Widget SHIFT
  selbst **und wartet `SHIFT_VORLAUF_MS` = 150 ms** mit der Taste — die K7673 sendet gleichzeitig erkannte Tasten zeilenweise,
  `3` käme sonst vor dem SHIFT.  Wirts-Autorepeat wird verworfen (die K7673 wiederholt selbst); Fokusverlust lässt alles los.
  Strg+Buchstabe wird über die Taste (nicht das Steuerzeichen) aufgelöst.  Einfügen geht über `term_send`.
* **Kürzel-Regel unverändert**: kein neues Kürzel (Aktion `verbindung` ohne), Kürzeltabelle des Handbuchs unberührt, F11 bleibt beim Fenster.

### 8.4 Verbindung und Mehrinstanz

* **Terminaleinheit**: die Leitung XB5 ist Schnittstelle 0 (`Terminal (XB5)`, Vorgabe Client).  `VerbindungDialog`
  (*Maschine ▸ Verbindung zum Rechner…*, nur im Modell `p8000-term` freigegeben): Art (Telnet/RFC 2217/Datei), Rolle,
  Rechner, Port, Verbinden/Trennen — dieselben Kernaufrufe wie der Reiter *Schnittstellen* (`serial_configure/_start/_stop`),
  im Betrieb gesperrt, Zustand im 4-Hz-Takt.  Statuszeile: „Rechner: verbunden mit …/getrennt/verbindet …".
* **Rechner mit Originalterminal**: der Reiter *Schnittstellen* bietet tty0, tty2, tty3, (tty4–7 mit 16-Bit-Karte) wie bisher als
  Server an; neu sind **Portvorschläge** (tty0 5000, tty2 5002, tty3 5003, tty4–7 5004–5007, Terminaleinheit 5004 — nur wo der
  Kern noch den Vorgabeport 5000 führt, eine gespeicherte Wahl gewinnt) und ein **Hinweistext** über den Blöcken
  (Mehrplatz, welche ttys WEGA belegt).
* **Mehrinstanz** (`app/instanz.py`, ohne Qt): `--instance NAME`/`K1520_INSTANZ` → `p8000term-NAME.yaml` und „[NAME]" im Titel;
  `--config DATEI`/`K1520_KONFIG` → Konfigurationsdatei von Hand.  **Plattensperre**: neben dem Abbild liegt `<abbild>.lock` mit der
  Prozesskennung; eine zweite Instanz auf derselben Platte bekommt sie nicht (Meldung; der Kern kennt keinen Schreibschutz für
  Platten, deshalb „nicht anschließen" statt „schreibgeschützt"), eine verwaiste Sperre (Prozess tot) wird übernommen.  Disketten
  hängen an der Konfiguration der Instanz.
* **Terminaleinheit im Hauptfenster**: `_rechner_anzeigen()` blendet Laufwerkskasten, Platte und Frontplatte aus, setzt den Titel
  und gibt die Aktion `verbindung` frei; der Kasten bleibt in der Konfiguration bestehen (ein Modellwechsel zurück bringt ihn wieder).

### 8.5 Wächter

`tests/python/test_p8000_original_gui.py` (`py_p8000_original_gui`, 33 Fälle): Profil/Modelle/Titel, Betriebsart und Modellmigration, CRT-Verdrahtung, Text kopieren, RAF-Entfernung,
Variantenwahl baut die richtige Maschine und Widgets, Boot-Smoke „P8000 + Terminal" (Einschaltmeldung im Framebuffer,
Hardwaretest über die Leitung), Frame-Pause (Zähler unverändert ⇒ kein `term_framebuffer`), Skalierung, Layout gegen den
Kern (105 Positionen, jede erreichbar), Halten von SHIFT/CTRL, Wirtstasten (Zeichen, Sonder, Shift-Vorlauf, Strg, Autorepeat,
Fokusverlust), Verbindungsdialog, Statuszeile, Portvorschläge, Instanzname/`--config`, Plattensperre,
**Mehrplatz über Loopback-Telnet** (Taste im Widget → `pr 24` = 78 am Rechner; Ausgabe des Rechners → Terminalbild).

### 8.6 Vorgabe ohne Platte (Anwenderentscheid)

Das Vollgerät legt **keine** Standardplatte mehr an (`_platte_vorbereiten` ist weg): eine leere E5-Platte schickte MON16 in den
AUTOBOOT.  Ohne Platte endet der Hochlauf am MON8-Prompt (`Press RETURN`, `>`).  Eine Platte entsteht nur über *Neue Platte…*
im Plattenkasten, der das im Hinweis sagt; ein `platte`-Abschnitt in der Konfiguration (auch `path: ""`) wirkt wie zuvor.

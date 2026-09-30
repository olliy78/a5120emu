# 18 — k8915emu: eigenes Programm für den K8915 (AP-E4h/E4i neu geschnitten)

Stand: angelegt 2026-09-29. Gehört zu `doc/design/16_k8915.md` §8a (AP-UI1); ersetzt dort
den bisherigen Zuschnitt von **E4h** (Maschinenwahl) und **E4i** (Frontplatte).

> **Stand AP-UI1 (erledigt 2026-09-30).** Umgesetzt in vier Commits (Kern → Oberfläche →
> Paketierung → Doku). Was wo steht:
>
> - **Kern:** `K1520Machine::nmi()` (Vorgabe ohne Wirkung), `K8915Machine::nmi()` merkt
>   den Druck fadensicher vor und stellt die /NMI-Flanke am Anfang von `run()` zu;
>   C-ABI `k1520_nmi`. `K7672`: `QK_TASTE_BASE | Matrixposition` spricht eine
>   **physische** Taste an; Scancode, Vorsatz und SCP-Zeichen aus den Tabellen des
>   EPROMs D3 (0080H Scancode, 0100H Tastenart, 0400H/0480H Zeichen). LED-Register 21H
>   zusätzlich Bit 7 (CAPS: Feststelltaste, `ESC [?11h/l`) und Bit 6 (`ESC [?18h/l`,
>   GRAPH [?]). Wächter `K8915Boot.Nmi*`, `K7672.Matrix*`, `test_c_api`.
> - **Oberfläche:** `app/profil.py` (Programmprofil), `app/main.py --machine`,
>   `run_k8915emu.sh`, Konfiguration `a5120emu.yaml`/`k8915emu.yaml` + Vorgaben
>   `data/default_config_{a5120,k8915}.yaml`, Umzug `config.yaml` →
>   `a5120emu.yaml` (`config_io.konfig_umziehen`), Titel „A5120 Emulator“/„K8915
>   Emulator“, Frontplatte in der Statuszeile (`status_bar.Frontplatte`), Aktion `nmi`
>   (`actions.NUR_FUER`), Werkzeugmenü startet den jeweils anderen Emulator (auch aus
>   dem DiskTool), Bildschirmtastatur `app/ui/keyboard_k7672.py`. Wächter
>   `tests/python/test_k8915emu_gui.py` (inkl. Boot bis `A>` und `dir` über die
>   Bildschirmtastatur), CLI-Fälle in `test_a5120emu_cli.py`.
> - **Paketierung:** eine Starter-Vorlage für beide Emulatoren, der **Name** wählt das
>   Profil (`k8915emu*` ⇒ `--machine k8915`); `k8915emu.desktop.in`; `.iss` mit zweitem
>   Startmenü-Eintrag. Wächter `py_packaging`.
>
> Tests: `test` 1327/1327, `test-format` 24/24, `test-matrix` 94/94, `win` 1302/1302.
> Nebenbei behoben: ein geschlossenes Hauptfenster startet keinen Autosave mehr
> (`MainWindow._geschlossen`) — sonst schrieb der Kastenabbau nach `closeEvent` den Stand
> eines toten Fensters in die Konfiguration.
>
> **Abweichungen von §2/§4:**
> - **Kein `E0`-Präfix.** Die Firmware (0326H–035FH, IRQ4 05C2H) sendet für Tasten mit
>   Bit 7 im Scancode einen **Umschalt- (2AH) oder Strg-Vorsatz (1DH)** je Tastenart,
>   kein `E0` — die Angabe „E0“ in §2 stammte aus einer fremden Nachbildung
>   (**[Adapter]**). ↑ = `2A 48`/`C8 AA`, ^S = `1D 45`/`9D C5` (= Strg+Pause, das BIOS
>   schaltet damit XOFF + LED Bit 0).
> - **Tasten ohne Code:** im DCP-Modus hat jede Taste des Fotos einen Scancode; nur
>   **CL** sendet nichts (Firmware 030BH: Tastenklick umschalten). Im SCP-Modus
>   (Boot-ROM) senden die Funktionstasten nichts (ihre ESC-Folgen liegen im fehlenden
>   Firmwareteil). Beides federt zurück.
> - **LED-Zuordnung:** READY = Bit 3 (belegt), CAPS = Bit 7 (belegt: die Feststelltaste
>   schaltet genau dieses Bit, 0303H), GRAPH = Bit 6 (**[?]**: `ESC [?18h/l` schaltet
>   Modus 60H Bit 5 = zweite Zeichentabelle samt Bit 6). Die drei unbeschrifteten
>   Punkte über ALT1/^S/MOD2 sind nachgebildet; der über ^S zeigt Bit 0 (**[?]**), die
>   beiden anderen bleiben dunkel. Farbe GRAPH/CAPS gelb **[?]** (Foto: aus = khaki).
> - **Rechte Umschalttaste:** zwei Matrixpositionen tragen 36H (16H, 66H) — gewählt 66H
>   **[?]** (für den Rechner gleichgültig).
> - **Werkzeugmenü des A5120** hat einen Eintrag mehr („K8915 Emulator starten“) —
>   von §2 verlangt; sonst ist die A5120-Oberfläche bis auf Titel/Konfig-Name gleich.
> - **Laufwerkstypen K8915:** nur 5¼″ (K5601, K5600.10, K5600.20) wählbar **[?]**.
> - **Summer** (`k1520_bell_count`) ist nicht Teil von §2 und nicht umgesetzt.

## 1. Vorgaben des Anwenders (2026-09-29)

1. Der K8915-Emulator ist ein **eigenes startbares Programm** (`k8915emu`), kein Menüpunkt im
   A5120-Emulator. Die Codebasis bleibt gemeinsam (erwartet ~99 % gleich), aber:
2. **Eigene Konfigurationsdatei** — z. B. A5120 mit drei Laufwerken und sichtbarer Tastatur,
   K8915 mit zwei Laufwerken ohne Tastatur, unabhängig voneinander.
3. **Eigene Bildschirmtastatur** nach dem Vorbild der echten K7672 (Fotos:
   `/home/olliy/Downloads/Tastatur_K8915/`), nicht die K7637 des A5120.
4. **Kein separates Kontrollpanel.** Die LEDs der Frontplatte gehen in die **Statuszeile**, der
   zusätzliche **NMI-Taster** kommt in die Symbolleiste **neben den Reset-Taster**.
5. Linux: eigenes Startskript **`run_k8915emu.sh`** (Quellbaum) bzw. eigener Starter in der
   Installation. Windows: **eigener Startmenü-Eintrag**, gleiches Icon, anderer Name.

## 2. Festlegungen (getroffen beim Planen, begründet)

- **Ein Programm, zwei Gesichter.** `app/main.py` bekommt einen Maschinenparameter; ein
  schlanker Einstieg (`app/k8915emu.py` oder `main.py --machine k8915`, vom Starter fest
  übergeben) legt ein **Programmprofil** fest: Maschinentyp, Fenstertitel („K8915 Emulator“),
  Name der Konfigurationsdatei, Tastatur-Widget, Laufwerksbestückung, Lampen, zusätzliche
  Aktionen. Alles Programm-Spezifische hängt an diesem Profil — keine `if machine == …`-
  Streuung durch die Oberfläche.
- **Konfiguration — für beide Programme gleich benannt** (Anwender 2026-09-29):
  gleiches Verzeichnis (`paths.config_dir()` → `~/.config/k1520emu/`), je Programm eine
  Datei **`a5120emu.yaml`** bzw. **`k8915emu.yaml`**; Auslieferungsvorgaben
  **`data/default_config_a5120.yaml`** bzw. **`data/default_config_k8915.yaml`** (in der
  Installation `share/k1520emu/`; `paths.default_config_file()` bekommt das Profil,
  `K1520_DEFAULT_CONFIG` wirkt weiter). **Umzug der Altdatei:** gibt es beim Start des
  A5120-Emulators noch eine `config.yaml`, aber keine `a5120emu.yaml`, wird sie EINMAL
  umbenannt (nicht kopiert, nicht gelöscht ohne Ersatz) und das im Protokoll vermerkt —
  sonst stünde der Anwender nach dem Update mit Auslieferungszustand da. Das DiskTool hat
  eine eigene Konfiguration und ist nicht betroffen (prüfen, nicht voraussetzen). Alle
  Fundstellen von `config.yaml`/`default_config.yaml` (app/, packaging/, Handbuch, CLAUDE.md,
  doc/design/11_python_app.md §10.7, Merkposten) mitziehen. Werkzeugkonsole und
  Diskettenverzeichnis bleiben gemeinsam.
- **Fenstertitel — gleichartig:** „**A5120 Emulator**“ bzw. „**K8915 Emulator**“ (Titel des
  Hauptfensters, Startmenü-Eintrag, `.desktop`-Name, Kopfzeile der Startskripte).
- **Laufwerke:** K8915 = 2 Steckplätze (K5601, K5601) wie das Gerät; Laufwerkskasten und
  Statuszeile zeigen nur die bestückten. Laufwerkstypen je Maschine aus `app/drive_types.py`.
- **Takt:** 2,4576 MHz als eingestellter Takt (`app/takt.py` je Profil).
- **Bild:** über `k1520_screen_char`/Framebuffer (nie `mem_read`, AP-E4b), K7024 mit eigenem
  Zeichensatz — der Bildschirmkasten selbst ist gemeinsam.
- **Statuszeile (K8915):** zusätzlich zu Takt/Laufwerken sechs Lampen in Gerätereihenfolge
  und -farbe: **Run** (grün, `/HALT` [?] — solange unbekannt: an, solange die Maschine läuft),
  **Input File**, **Output File**, **RUN Mode** (gelb, Port 61H Bit4/5/6, aktiv low),
  **ERROR** (rot, Bit7), **Power** (rot, an solange eingeschaltet). Quelle
  `k1520_panel_lamps` (Spiegel des letzten `run()`), Tooltip mit Bedeutung.
- **Symbolleiste (K8915):** `nmi` als eigene `QAction` neben `reset` (gleiches Muster wie
  alle Aktionen in `app/ui/actions.py`, Namensliste der Leiste je Profil; der A5120 bekommt
  sie nicht). Kern: `K1520Machine::nmi()` + C-ABI `k1520_nmi` (AP-B2: ROM 0066H → Lampen
  aus, Selbsttest von vorn; unter SCPX landet der NMI im RAM → Verhalten wie am Gerät,
  §6.12 offen, NICHT abfangen). Kürzel nur mit `Strg+Umschalt` (Wächter der
  Kürzeltabelle) — oder gar keins.
- **Bildschirmtastatur K7672** (`app/ui/keyboard_k7672.py`, gezeichnet wie die K7637):
  Layout nach Foto — Funktionsreihe CTRL · ALT1 ^S MOD2 PF1 · PF2–PF5 · PF6–PF9 ·
  CLEAR RESET BREAK; Hauptblock deutsch (QWERTZ, Ü Ö Ä ß, `|←`/DEL, `→|`, CAPS LOCK,
  `↕` Umschalt beidseitig, `<>`, ALT, Leertaste, CL, RETURN); Mittelblock PF10–PF12,
  PA1/PA2, PA3/↖, GRAPH, Kursorkreuz; Ziffernblock CE / * −, 7–9 +, 4–6 =, 1–3 ENTER, 0 , .;
  drei LEDs **GRAPH / CAPS / READY** (READY grün). Rote Zweitbeschriftungen (Prt Sc, Num,
  Pause, Pg Up/Dn, Ins, Del …) wie auf dem Foto. Gesendet wird der **DCP-Scancode**
  (Satz 1, Loslassen = |80H, E0-Präfix) aus der Scancodetabelle der K7672-Firmware
  (`doc/EPROMS/K7672/README.md`); Tasten ohne belegten Code federn zurück und senden nichts
  (wie PRINT/HLT an der K7637), dokumentiert. Die LEDs folgen `k1520_keyboard_leds`
  (Register 21H; Bit3 = „Senden frei“ ⇒ vermutlich READY, Bit0 = `ESC [?13h`, Zuordnung
  GRAPH/CAPS aus Firmware/BIOS herleiten, Rest [?]).
- **PC-Tastatur:** Tasten des Host gehen am K8915 über `k1520_key_press` an die K7672 (dort
  zu Scancodes, AP-E3b). `k1520_translate_key` ist K7637-spezifisch (AP-E4b-Befund 2) —
  für den K8915 ein Gegenstück bzw. maschinenbezogene Übersetzung vorsehen.
- **Handbuch:** eigener Abschnitt bzw. eigene Seite für k8915emu; die Kürzeltabelle ist ein
  Vertrag (beide Wächter) — neue Aktionen dort eintragen.
- **Menü „Werkzeuge“** (`app/programme.py`): beide Emulatoren und das DiskTool starten sich
  gegenseitig (Emulator A5120 ⇄ k8915emu ⇄ DiskTool).

## 3. Starter und Paketierung

- Quellbaum: `run_k8915emu.sh` (Kopie von `run_a5120emu.sh`, übergibt das K8915-Profil).
- Installation Linux: `bin/k8915emu` (wie `bin/a5120emu`, `packaging/launcher.sh`),
  `k8915emu.desktop` (gleiches Icon, Name „K8915 Emulator“), `install.sh` legt/entfernt ihn
  mit (Inventar im Ausweis `.k1520emu-installation`!).
- Windows: `packaging/k1520emu.iss` `[Icons]` + Eintrag „K8915 Emulator“, gleiches
  `a5120emu.ico`, Ziel `pythonw.exe` mit dem K8915-Einstieg; `bin/k8915emu.cmd`.
- **Vor Änderungen `doc/merkposten/paketierung.md` lesen** (elf Festlegungen). Wächter
  `py_packaging` erweitern. Keine K8915-Systemdiskette ins Paket (E4l-Rechtsfrage offen).

## 4. Wächter

- Python (offscreen): k8915emu startet, Fenstertitel, liest/schreibt `k8915emu.yaml` und
  lässt `config.yaml` unberührt (und umgekehrt); Laufwerkszahl je Profil; Statuszeile hat
  die sechs Lampen und sie folgen `k1520_panel_lamps`; NMI-Aktion nur im K8915-Profil und
  löst `k1520_nmi` aus; Bildschirmtastatur K7672 sendet für eine Stichprobe (A, ß, PF1,
  RETURN, Kursor, Umschalt) die richtigen Scancodes; LEDs folgen `k1520_keyboard_leds`;
  Kürzelwächter grün. Rauchtest: k8915emu bootet eine TempDisk-Kopie bis `A>`.
- C++/C-ABI: `k1520_nmi` (ABI-Vertrag `test_c_api.py`, `K1520_API`); A5120 unverändert.
- Paketierung: `py_packaging` kennt beide Starter und beide Startmenü-Einträge.
- Alle vier Lanes vor Abschluss: `test`, `test-format`, `test-matrix`, `win`.

## 5. Nicht in diesem AP

Drucker/V.24-Oberfläche (E4j), physische Diskette (E4k), Tastenwiederholung der K7672
(E4g), Paketinhalt „Bootdiskette“ (E4l).

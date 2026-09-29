# 18 — k8915emu: eigenes Programm für den K8915 (AP-E4h/E4i neu geschnitten)

Stand: angelegt 2026-09-29. Gehört zu `doc/design/16_k8915.md` §8a (AP-UI1); ersetzt dort
den bisherigen Zuschnitt von **E4h** (Maschinenwahl) und **E4i** (Frontplatte).

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
- **Konfiguration:** gleiches Verzeichnis (`paths.config_dir()` → `~/.config/k1520emu/`),
  andere Datei: `config.yaml` (A5120, unverändert — bestehende Anwenderkonfigurationen
  bleiben gültig) und **`k8915emu.yaml`**. Eigene Auslieferungsvorgabe
  `data/default_config_k8915.yaml` (gleicher Aufbau, `paths.default_config_file()` bekommt
  das Profil). Die Werkzeugkonsole und das Diskettenverzeichnis bleiben gemeinsam.
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

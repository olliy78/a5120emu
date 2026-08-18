# Python-Testebene (pytest)

Deckt ab, was die C++-Tests nicht erreichen können:

| Schicht | Warum hier und nicht in C++ |
|---------|------------------------------|
| **C-ABI** (`libk1520core.so` ↔ ctypes) | Der C++-Compiler prüft die Python-Seite nicht. Eine geänderte Signatur bricht **still** — ctypes merkt es erst beim Aufruf, oft als Absturz statt als Fehlermeldung. |
| **GUI** (PySide6) | Fensteraufbau, Widget-Verdrahtung, Konfigurationsrundlauf, Laufwerksleiste — läuft headless über `QT_QPA_PLATFORM=offscreen`. |
| **App-Logik** | `drive_types` (Laufwerkskatalog + Migration alter Profilnamen), `config_io` (YAML), `keyboard` (Qt-Taste → Kerncode). |

## Ausführen

```sh
tools/dev.sh test                     # läuft mit (Label "python")
ctest --test-dir build -L python      # nur diese Ebene
venv/bin/python3 -m pytest tests/python -q          # direkt, mit pytest-Ausgabe
venv/bin/python3 -m pytest tests/python -q -k c_api # einzelne Gruppe
```

CMake registriert je Testmodul **einen** ctest-Fall (`py_c_api`, `py_binding`, …).
Die Modulliste wird dabei **eingelesen** (`file(GLOB … test_*.py CONFIGURE_DEPENDS)`),
nicht gepflegt: ein neues `test_xyz.py` wird beim nächsten Bau von selbst zu
`py_xyz`. Bis 2026-08-16 stand die Liste von Hand in der `CMakeLists.txt` — wer die
Zeile vergaß, hatte einen Test, der nirgends lief und dessen Fehlen niemandem auffiel.

> **Modulnamen dürfen nicht mit einem Werkzeug kollidieren.** pytest importiert
> Testdateien ohne Paket, also belegt `tests/python/test_report.py` den Modulnamen
> `test_report` — ein `import test_report` holte dann die Testdatei statt
> `tools/test_report.py`. Deshalb heißt der Wächter dafür `test_testprotokoll.py` und
> lädt das Werkzeug über seinen Pfad (`importlib`).

Voraussetzungen werden beim Konfigurieren geprüft; fehlen sie, werden die Tests
nicht registriert und CMake sagt, was zu installieren ist:

```sh
venv/bin/python3 -m pip install -r requirements-dev.txt
```

Bevorzugt wird `venv/bin/python3` (dort liegen PySide6/PyYAML/pytest), sonst der
System-Interpreter.

## Dateien

| Datei | Inhalt |
|-------|--------|
| `conftest.py` | Pfade, Fixtures (`emulator`, `booted`, `temp_disk`, `qapp`), Headless-Qt, `run_until_text` |
| `test_c_api.py` | Header ↔ `.so` ↔ Bindung: jede Funktion exportiert **und** mit `argtypes` deklariert; Handle-Lebenszyklus; Framebuffer-Geometrie; Fehlerpfade; Formatkatalog |
| `test_binding.py` | Semantik der `K1520Emulator`-Methoden: Mount/Unmount, Schreibschutz, `create_disk`, Laufwerksstatus, `run()`-Rückgabe |
| `test_boot_smoke.py` | Kaltboot durch die Bindung bis Bootloader/CP/A-Banner/Uhrzeit-Abfrage, `.img` **und** `.hfe`, Reset, Tastatureingabe |
| `test_drive_types.py` | Laufwerkskatalog, `normalize()`, Migration alter Profilnamen |
| `test_config_io.py` | Speichern/Laden, CRT-Parameter-Rundlauf, Toleranz gegen fremde Schlüssel |
| `test_keyboard_map.py` | `qt_event_to_core_key`: Zeichen, Sondertasten, F1–F8, Ctrl-Kombinationen, Modifikatoren allein |
| `test_gui_smoke.py` | Hauptfenster offscreen: alle Panels, Emulator, Laufwerksleiste, Konfigurationsrundlauf |

## Grenzen

- **Keine Pixelprüfungen.** Der Bildschirm ist ein `QOpenGLWidget`; offscreen gibt
  es keinen FBO (Qt meldet „QOpenGLWidget: No fbo, cannot render"). Bildinhalte
  prüft die C++-Seite über das VRAM bzw. `tools/fb_ocr.py`.
- **Bildschirminhalt statt Framebuffer.** Für Textprüfungen liest
  `K1520Emulator.screen_text()` das K7024-Bildwiederholram ab `0xF800` — robust
  und unabhängig vom Rendern.
- **Disketten immer als Kopie.** Die `temp_disk`-Fixture kopiert nach `tmp_path`;
  eine committete Fixture darf ein Test nie direkt mounten (der Emulator öffnet
  schreibend).
- **Eigenes Konfigurationsverzeichnis.** `conftest.py` setzt `XDG_CONFIG_HOME` auf
  ein Unterverzeichnis, damit `~/.config/k1520emu/config.yaml` des Nutzers
  unangetastet bleibt.
- **`greaseweazle` gehört NICHT in die CI-Abhängigkeiten.** Es brächte keinen
  einzigen zusätzlichen Testfall: hinter `verfuegbar()` steht in der zweiten Zeile
  `util.usb_open` — alles dahinter braucht einen Adapter am Kabel, und die
  Hardwarefälle in `test_gw_hardware.py` hängen ohnehin an `K1520_GW_HARDWARE=1`,
  nicht am Paket (nachgemessen: mit und ohne Paket laufen dieselben 8 Fälle nicht).
  Es zu installieren würde umgekehrt die Eigenschaft *verdecken*, die der Anwender
  ohne das Paket erlebt. Geprüft wird sie deshalb ausdrücklich in
  `test_gw_ohne_paket.py`, das den Import aktiv **ausblendet** — der Fall schlägt
  damit auch auf einem Rechner an, auf dem das Paket installiert ist.
- **Die Ebene läuft OHNE `greaseweazle`** — das Paket ist eine freiwillige
  Abhängigkeit (nicht auf PyPI), und **in der CI ist es nie installiert**. Wer die
  physische Diskette prüft, setzt ein *Ersatzlaufwerk* aus dem RAM ein; dazu gehört,
  dass auch die **Verfügbarkeit** ersetzt wird (`hosttools_gelten_als_vorhanden` in
  `test_gw_gui.py` setzt `app.gw.verfuegbar`). Die Bedienwege prüfen nämlich vor dem
  Auswahldialog, ob die Hosttools da sind — richtig so, aber ohne die Ersetzung
  schlägt die Prüfung zu, bevor das Ersatzlaufwerk zum Zug kommt.
  `bitarray` steht deshalb ausdrücklich in `requirements-dev.txt`: `app/gw` rechnet
  damit, und ohne `greaseweazle` käme es sonst nicht mit.
- **Ein unerwartetes modales Fenster ist ein Fehlschlag, kein Hänger.** Die
  autouse-Sperre `kein_unerwartetes_meldungsfenster` (conftest) lässt
  `QMessageBox.critical` & Co. sofort scheitern; headless klickt sie sonst niemand
  weg, und der Fall stünde bis zum ctest-Zeitüberlauf (300 s) — im Protokoll dann
  nur „Timeout" ohne Grund. Tests, die ein Fenster **erwarten**, setzen ihren
  eigenen `monkeypatch` im Testkörper; der greift später und gewinnt.

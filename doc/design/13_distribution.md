# Feinentwurf: Verteilbares Paket (Windows / Linux / macOS)

**Module:** `packaging/` (neu), `app/paths.py` (neu), `core/api/`,
`core/peripherals/floppy_drive/format_catalog.*`, `CMakeLists.txt`, `.github/workflows/`
**Verwandt:** `doc/design/10_c_api.md` (C-ABI), `doc/design/11_python_app.md` (GUI),
`doc/design/00_konfiguration.md` (`formats.yaml`-Suche)
**Stand:** 2026-08-07 — **Schritte 1 und 2 umgesetzt** (relocatable + Linux-Paket,
`packaging/`, Guards `py_paths`/`py_packaging`).  Windows und macOS stehen aus (§10).

---

## 1. Ziel und Randbedingungen

Der Emulator soll an Anwender verteilbar sein, die weder bauen noch administrieren
wollen. Vorgaben:

| # | Anforderung | Folge für den Entwurf |
|---|-------------|------------------------|
| A1 | Fertig kompilierte Kernbibliothek im Paket (`.so`/`.dll`/`.dylib`) | plattformspezifische Payloads aus einer CI-Matrix |
| A2 | Python und Qt **nicht** mitverteilen (Paketgröße) | Bootstrap-Installer lädt beides zur Installationszeit nach |
| A3 | Alles in einem venv, keine Rückwirkung aufs System | eigenes venv im Installationsverzeichnis, kein `pip install --user` |
| A4 | Installation **ohne Admin-/Root-Rechte** | ausschließlich benutzerlokale Pfade, kein Dienst, keine Registry-HKLM, kein VC-Redist |
| A5 | Windows und Linux jetzt, macOS später | ein gemeinsamer Bootstrap-Kern, dünne plattformspezifische Hüllen |

Nicht-Ziele: Systempaket (`.deb`/`.rpm`), Systemweit-Installation, Paketmanager-Auslieferung,
Autoupdate im Hintergrund.

---

## 2. Leitidee: Payload + Bootstrap + Launcher

Das Paket wird **nicht** als geschlossenes Bundle gebaut, sondern in drei Rollen zerlegt:

```
┌─ Payload (CI-Artefakt, plattformspezifisch, ~2 MB) ───────────────────┐
│  k1520core.so|dll   app/*.py   formats.yaml   Beispieldisketten       │
│  requirements.lock  VERSION                                           │
└───────────────────────────────────────────────────────────────────────┘
                │  entpacken nach <install-root>
                ▼
┌─ Bootstrap (einmalig, braucht Netz) ──────────────────────────────────┐
│  uv holen  →  CPython holen  →  venv anlegen  →  PySide6 + PyYAML     │
└───────────────────────────────────────────────────────────────────────┘
                │
                ▼
┌─ Launcher (jeder Start) ──────────────────────────────────────────────┐
│  K1520_HOME setzen → venv-Python app/main.py                          │
└───────────────────────────────────────────────────────────────────────┘
```

Die Trennung ist der Kern des Entwurfs: **Payload und Laufzeitumgebung altern getrennt.**
Ein Emulator-Update tauscht nur die Payload (Sekunden, wenige MB); das venv mit Qt bleibt
stehen und wird nur angefasst, wenn sich der Hash von `requirements.lock` ändert.

---

## 3. Der Bootstrapper: `uv`

Beschafft wird die Laufzeit mit **[`uv`](https://github.com/astral-sh/uv)** — einem
einzelnen statischen Binary (~15–20 MB), das es für Linux, Windows und macOS gibt:

```sh
uv python install 3.12          # relocatable CPython ins Benutzerprofil
uv venv --python 3.12 "$ROOT/venv"
uv pip install --python "$ROOT/venv" --require-hashes -r "$ROOT/requirements.lock"
```

Warum `uv` und nicht Eigenbau:

- **Löst A2 und A4 zusammen.** `uv python install` lädt einen *relocatable* CPython
  (python-build-standalone) ins Benutzerprofil — kein Systempython, kein Installer, keine
  Rechte. Damit fällt die Windows-Hauptsorge weg (Python ist dort nicht vorhanden, und der
  Store-Stub `python.exe` in `WindowsApps` ist eine klassische Stolperfalle) *und* die
  Debian-/Ubuntu-Falle (Systempython ohne `python3-venv` → `venv` schlägt fehl).
- **Ein Ablauf für drei Plattformen.** Dieselben drei Befehle, nur die Hülle unterscheidet
  sich. Ohne `uv` müsste der Installer je Plattform die python-build-standalone-Release-URL
  auflösen, Prüfsummen verifizieren, entpacken und `venv` bemühen — dreimal Eigencode für
  ein gelöstes Problem.
- **Reproduzierbar.** `uv pip compile` erzeugt `requirements.lock` mit Hashes,
  `--require-hashes` erzwingt sie beim Installieren.
- **Schnell.** Der PySide6-Download dominiert; das Auflösen ist gegenüber `pip`
  vernachlässigbar.

`uv` selbst wird **in die Installation** gelegt (`<root>/tools/uv[.exe]`), nicht global —
sonst hinterlässt der Installer entgegen A3 Spuren außerhalb seines Verzeichnisses.

**Qt-Variante:** In `requirements.lock` gehört **`PySide6-Essentials`**, nicht `PySide6`.
Die GUI benutzt ausschließlich `QtCore`, `QtGui`, `QtWidgets`, `QtOpenGL`,
`QtOpenGLWidgets` — alle in Essentials enthalten. Das spart QtWebEngine & Co., grob die
Hälfte des Downloads.

### 3.1 Ablauf des Bootstraps im Detail

1. **Vorprüfung**: Zielverzeichnis schreibbar? Freier Platz ≥ 500 MB?
2. **`uv` beschaffen**: Download nach `<root>/tools/`, SHA256 gegen den **im Paket
   mitgelieferten** Erwartungswert prüfen (`packaging/uv_pins.txt`) — eine nebenher
   geladene `.sha256`-Datei deckte nur Übertragungsfehler ab, nicht eine ausgetauschte
   Quelle. Ein systemweites `uv` wird bewusst *nicht* mitbenutzt: jede Installation soll
   dieselbe geprüfte Fassung verwenden und beim Deinstallieren restlos verschwinden.
3. **CPython**: `uv python install --no-bin <pinned>`.
4. **venv** in `<root>/venv` (`--python-preference only-managed`, damit kein Systempython
   einspringt).
5. **Abhängigkeiten**: `uv pip install --require-hashes -r requirements.lock`.
6. **Rauchtest**: lädt die Kernbibliothek, ruft `k1520_version()`, importiert PySide6,
   löst `formats.yaml` auf und legt die Beispieldisketten beim Anwender ab. Schlägt etwas
   fehl, bricht die Installation **mit Klartextmeldung** ab, statt einen kaputten
   Startmenüeintrag zu hinterlassen.
7. **Verknüpfungen** (Desktop-Datei bzw. Startmenü) und `VERSION`-Stempel schreiben.

Damit **alles** in der Installation landet und beim Löschen mitgeht, bekommt uv seine
Verzeichnisse vorgegeben: `UV_PYTHON_INSTALL_DIR=<root>/python`,
`UV_CACHE_DIR=<root>/.cache-uv` (nach der Installation gelöscht, `--keep-cache` behält ihn)
und `--no-bin` beim Python — sonst legt uv ein `~/.local/bin/python3.x` an, das im PATH des
Anwenders stünde und das Deinstallieren als toter Symlink überlebte.

> **Stolperstein Arbeitsverzeichnis.** Der Rauchtest bekommt sein Skript über die
> Standardeingabe — dann steht das **Arbeitsverzeichnis** vorn in `sys.path`. Aus dem
> Quellbaum heraus gestartet prüfte der Installer so dessen `app/` statt der Installation
> und meldete „läuft", ohne das Ausgelieferte je angefasst zu haben. `install.sh` wechselt
> deshalb vorher nach `$PREFIX`; Guard: `test_packaging.py` prüft, dass der
> Rauchtest-Ausgabe der Installationspfad steht.

**Fehlerfälle, die eine eigene Meldung bekommen** (sonst landet der Anwender bei einem
Python-Traceback): kein Netz / Proxy verlangt Authentifizierung, Plattenplatz,
Virenscanner löscht `uv.exe`, bereits vorhandene Installation anderer Version.

### 3.2 Update und Deinstallation

- **Update**: derselbe Installer über dieselbe Wurzel. Payload wird ersetzt; das venv nur
  neu aufgebaut, wenn sich der Hash von `requirements.lock` ändert. Benutzerdaten (§4)
  werden nie angefasst.
- **Deinstallation**: `<root>` löschen. Benutzerdaten separat und auf Nachfrage — der
  Windows-Uninstaller fragt, das Linux-Skript hat `--purge`.

---

## 4. Verzeichnislayout

Bewusst auf allen Plattformen **gleich aufgebaut**, nur die Wurzel unterscheidet sich.

```
<install-root>/
  bin/          k1520core.so | k1520core.dll        (+ ggf. MinGW-Laufzeit-DLLs)
  app/          Python-Quellen der GUI
  share/a5120emu/formats.yaml
  share/disks/  mitgelieferte Beispieldisketten (schreibgeschützt gedacht)
  tools/        uv[.exe]
  venv/         vom Bootstrap erzeugt
  requirements.lock, VERSION, LICENSE
```

| Plattform | `<install-root>` | Benutzerdaten | Verknüpfung |
|-----------|------------------|---------------|-------------|
| Linux | `~/.local/opt/a5120emu` | `~/.config/k1520emu/` (Konfig, bestehend), `~/.local/share/a5120emu/disks/` (Arbeitsdisketten) | `~/.local/share/applications/a5120emu.desktop`, Starter `~/.local/bin/a5120emu` |
| Windows | `%LOCALAPPDATA%\A5120Emu` | `%APPDATA%\A5120Emu\` | Startmenü `%APPDATA%\Microsoft\Windows\Start Menu\Programs\` |
| macOS (später) | `~/Applications/A5120Emu.app/Contents/Resources` | `~/Library/Application Support/A5120Emu/` | die `.app` selbst |

Das Programm liegt unter Linux bewusst in `.local/opt` und **nicht** in
`.local/share/a5120emu`: dort liegen die Benutzerdaten, und beides an einer Stelle hieße,
dass die Arbeitsdisketten im Installationsverzeichnis lägen. Genau das darf nicht sein:

- **Arbeitsdisketten müssen schreibbar liegen.** Der verzögerte Autosave
  (`DiskImage::autoFlush`, `doc/K1520_architecture.md` §8.7) schreibt in die *gemountete
  Datei* zurück. Eine direkt aus `share/disks/` gemountete Beispieldiskette würde also im
  Installationsverzeichnis verändert — und beim Update kommentarlos überschrieben.
  `paths.seed_user_disks()` kopiert die Beispiele deshalb **bei der Installation** einmalig
  ins Benutzerverzeichnis (und fasst ein vorhandenes nie wieder an); der Dateidialog zeigt über
  `paths.default_disk_dir()` von Anfang an dorthin.
- **`formats.yaml` findet sich nicht von selbst.** `FormatCatalog::searchPaths()` sucht u.a.
  bei `<Verzeichnis der Programmdatei>/../share/a5120emu/` — war das per
  `readlink("/proc/self/exe")` ermittelt, ist es bei einer per `ctypes` geladenen Bibliothek
  der **Python-Interpreter im venv**, nicht `<root>/bin/`. Gelöst über den **Modulpfad**
  (§6.2), nicht über ein `K1520_FORMATS` im Launcher — die Bibliothek soll sich selbst
  finden, auch wenn sie jemand anders lädt.

---

## 5. Paketformen je Plattform

### 5.1 Windows — Inno Setup, per-user

Ausgeliefert wird ein `A5120Emu-<version>-win-x64-setup.exe`, gebaut mit **Inno Setup**:

```ini
PrivilegesRequired=lowest          ; kein UAC, kein Admin
DefaultDirName={localappdata}\A5120Emu
DisableDirPage=no                  ; Pfad änderbar (Roaming-Profile!)
ArchitecturesAllowed=x64compatible
Uninstallable=yes                  ; Eintrag unter HKCU\…\Uninstall
```

- Die Setup-Dateien sind **nur** die Payload. Nach dem Kopieren läuft der Bootstrap als
  `[Run]`-Schritt mit sichtbarer Fortschrittsausgabe (Qt-Download dauert je nach Leitung
  spürbar) — abbrechbar, mit Rückmeldung bei Fehlschlag.
- **Kein VC-Redist.** Der ließe sich per-user nicht installieren; stattdessen wird die DLL
  mit **statischer CRT** gebaut (§7).
- **SmartScreen**: eine unsignierte `.exe` wird beim ersten Start gewarnt. Ohne
  Code-Signing-Zertifikat (kostenpflichtig, EV für sofortige Reputation) bleibt das so; im
  README gehört ein Absatz „Weitere Informationen → Trotzdem ausführen". Das ist eine
  bewusste Kaufentscheidung, keine technische Lücke.
- **Virenscanner**: frisch heruntergeladene, unsignierte Binärdateien werden gelegentlich in
  Quarantäne gestellt. Der Bootstrap prüft nach dem `uv`-Download, ob die Datei noch da ist,
  und meldet das gezielt.

### 5.2 Linux — Tarball + `install.sh`   ✅ umgesetzt

`a5120emu-<version>-linux-x86_64.tar.gz` (~2 MB) mit `install.sh`:

```
./install.sh                 # nach ~/.local/opt/a5120emu
./install.sh --prefix DIR    # abweichendes Ziel
./install.sh --python 3.13   # andere Python-Fassung
./install.sh --no-shortcut   # ohne Startmenü-Eintrag
./install.sh --uninstall [--purge]
```

Legt zusätzlich `~/.local/bin/a5120emu` (Launcher) und die `.desktop`-Datei samt Icon nach
`~/.local/share/icons/hicolor/…` an, ruft `update-desktop-database` best effort. Kein
`sudo`, kein systemweiter Pfad. Ein AppImage wäre die Alternative, packt aber Qt wieder mit
ein — genau das, was A2 ausschließt.

### 5.3 macOS (Ausblick)

Dasselbe Layout in einem `.app`-Bundle; `Contents/MacOS/A5120Emu` ist der Launcher.
Zusätzliche Hürde: **Gatekeeper** setzt das Quarantäne-Attribut auf alles Heruntergeladene,
und eine unsignierte `.dylib` wird beim `dlopen` abgewiesen — anders als unter Windows hilft
hier kein „trotzdem ausführen" pro Datei. Realistisch sind zwei Wege: Apple-Developer-ID
plus Notarisierung (jährliche Gebühr, dafür klaglos), oder Installationsanleitung mit
`xattr -dr com.apple.quarantine`. Vor der ersten macOS-Auslieferung zu entscheiden.

---

## 6. Nötige Änderungen am Code

Der Installer ist der kleinere Teil der Arbeit. Der Kern ist heute **nicht
Windows-tauglich** und die GUI **nicht relocatable**. Befunde:

### 6.1 Windows-Portierung des Kerns

| Fundstelle | Problem | Lösung |
|------------|---------|--------|
| `core/api/k1520_api.h:3` | nur `extern "C"`, keine Export-Makros | Makro `K1520_API` (`__declspec(dllexport/import)` bzw. `__attribute__((visibility("default")))`) vor jede API-Funktion; alternativ `WINDOWS_EXPORT_ALL_SYMBOLS` als Übergangslösung. **Ohne das exportiert die MSVC-DLL gar nichts** — `ctypes` findet keine einzige Funktion. |
| `CMakeLists.txt` | Build ist auf GCC/Linux zugeschnitten | MSVC-Zweig: statische CRT, `/utf-8` (die Quellen enthalten deutsche Umlaute!), Warnungsflags |

Erledigt sind dagegen die Pfad- und Umgebungsfragen, die dieselbe Datei betrafen: der
Modulpfad (§6.2) hat den Windows-Zweig `GetModuleFileNameW` gleich mitbekommen,
`homeConfigDir()` kennt `%APPDATA%`, und `K1520_FORMATS` trennt unter Windows mit `;`
(dort ist `:` Teil von `C:\…`).

Der übrige Kern ist portabel (`std::filesystem`, keine POSIX-Aufrufe außerhalb dieser einen
Datei, kein `pthread`, keine `dlopen`-Nutzung).

### 6.2 Modulpfad statt Exe-Pfad   ✅ umgesetzt

Sauberer als ein `K1520_FORMATS` im Launcher: die Bibliothek ermittelt ihren **eigenen**
Pfad (`dladdr` unter Linux/macOS, `GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)`
unter Windows — beides über die Adresse eines Ankers im eigenen Modul) und leitet daraus
`<lib-dir>/../share/a5120emu/formats.yaml` ab. Damit ist das Installationsverzeichnis
**frei verschiebbar**, und dieselbe Auflösung stimmt für die GUI (Lib in `bin/`), die
statisch gelinkten Werkzeuge (dort ist das Modul die Programmdatei) und den Quellbaum.
Kandidat 2 der Suchliste heißt jetzt „Verzeichnis des eigenen Moduls".

Guard: `test_paths.py::test_installierte_bibliothek_findet_eigenen_formatkatalog` baut eine
Installation im Temp-Verzeichnis nach und lädt sie in einem eigenen Prozess — sie muss ihre
`formats.yaml` daneben finden, ohne jede Umgebungsvariable.

### 6.3 GUI: zentrale Pfadauflösung (`app/paths.py`)   ✅ umgesetzt

Vorher waren die Pfade verstreut und auf die Repo-Struktur verdrahtet: die Bindung suchte
`libk1520core.so` nur in `<repo>/build/`, das Diskettenverzeichnis kam aus dreifachem
`dirname`, `run_gui.sh` setzte `LD_LIBRARY_PATH`.

Jetzt löst `app/paths.py` alles an einer Stelle auf — `core_library()`, `formats_file()`,
`bundled_disks_dir()`, `user_disks_dir()`, `config_dir()`, `default_disk_dir()`,
`seed_user_disks()`, `describe()` — in der Reihenfolge **Umgebungsvariable
(`K1520_HOME`/`K1520_LIB`/`K1520_FORMATS`/`K1520_DISKS`) → Installationslayout →
Quellbaum**. Beide Layouts hängen an derselben Wurzel (dem Elternverzeichnis von `app/`)
und unterscheiden sich nur in den Unterverzeichnissen, es braucht also keine
Modusumschaltung — der erste existierende Kandidat gewinnt.

Mit erledigt:

- Dateiname je Plattform: `libk1520core.so` / `k1520core.dll` / `libk1520core.dylib`
- Windows: `prepare_library_load()` meldet die Verzeichnisse über `os.add_dll_directory()`
  an — seit Python 3.8 wertet `ctypes.CDLL` `PATH` dafür nicht mehr aus
- `config_dir()` liefert unter Windows `%APPDATA%`, unter macOS
  `~/Library/Application Support` — benutzt aber ein **vorhandenes** `~/.config/k1520emu`
  weiter, damit dort abgelegte Konfigurationen nicht unsichtbar werden
- `main.py --paths` gibt die ganze Auflösung aus (Rauchtest des Installers, erste Frage bei
  „findet die Bibliothek nicht"); die Ladehinweise der Bindung liegen hinter `K1520_DEBUG`

Der Quellbaum-Zweig bleibt erhalten: `run_gui.sh` und die Entwicklung aus `build/` heraus
laufen unverändert.

---

## 7. Bauen der Payloads

Eine GitHub-Actions-Matrix, ein Job je Plattform, Artefakte ans Release.

| Plattform | Builder | Toolchain-Vorgaben |
|-----------|---------|--------------------|
| Linux x86_64 | Container mit alter glibc (manylinux_2_28 bzw. Ubuntu 22.04) | `-static-libstdc++ -static-libgcc`, Release/`-O3`. **Rückwärtskompatibilität kommt von der Baseline, nicht vom Zielsystem** — auf einem aktuellen Ubuntu gebaut, läuft die `.so` auf keiner älteren Distribution. |
| Windows x64 | `windows-latest`, MSVC | `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` (statische CRT — A4: kein VC-Redist ohne Admin), `/utf-8` |
| macOS (später) | `macos-latest` | `-mmacosx-version-min=12.0`, universal2 (arm64+x86_64) via `CMAKE_OSX_ARCHITECTURES` |

Zwei weitere Bau-Einstellungen sind für ein *verteilbares* Paket nicht verhandelbar
(`packaging/build_payload.sh` setzt beide):

- **`-DK1520_FORMATS_DEFAULT=`** — der einkompilierte Fallback-Pfad des Formatkatalogs zeigt
  im Entwicklungsbaum auf `<repo>/data/formats.yaml`. Bliebe er gesetzt, trüge jede
  ausgelieferte Bibliothek den absoluten Pfad des **Baurechners** als Suchkandidaten mit
  sich herum. Guard: `test_packaging.py::test_paket_traegt_keinen_pfad_des_baurechners`
  sucht den Pfad im Binärabbild.
- **`-DBUILD_K1520_TESTS=OFF`** — spart GoogleTest per FetchContent, also einen
  Netzzugriff im Release-Job.

Weiter im Release-Job: Version aus `git describe` in `VERSION` stempeln, SHA256SUMS
beilegen, die kuratierte Diskettenauswahl aus `disks/` zusammenstellen. Und: **auf jeder
Plattform den Rauchtest fahren** (Lib laden, `k1520_version()`, `formats.yaml` finden) — ein
Payload, dessen DLL nichts exportiert, darf kein Release-Asset werden.

`requirements.lock` wird dagegen **nicht** im Release-Job aufgelöst, sondern liegt
eingecheckt unter `packaging/requirements.lock` (aus `packaging/requirements.in`, auffrischen
mit `--relock`). Damit braucht das Schnüren kein Netz, jeder Bau liefert dasselbe, und eine
geänderte Abhängigkeit ist im Diff sichtbar statt still.

---

## 8. Größen

Gemessen am ersten Linux-Paket (6 Beispieldisketten, `--disks default`):

| Bestandteil | Größe | wann |
|-------------|-------|------|
| Kernbibliothek (Release, gestrippt) | ~0,6 MB | im Paket |
| GUI-Quellen + `formats.yaml` + Symbol | ~0,4 MB | im Paket |
| 6 Beispieldisketten (`.hfe`, komprimiert) | ~1 MB | im Paket |
| **Download des Pakets** | **1,9 MB** | |
| `uv` | ~15–20 MB | einmalig bei Installation |
| CPython 3.12 (relocatable) | ~30 MB | einmalig bei Installation |
| PySide6-Essentials + PyYAML | ~76 MB | einmalig bei Installation |
| **Netzbedarf Erstinstallation** | **~120 MB** | |
| **Belegter Platz nach Installation** | **406 MB** | gemessen |

Die `.hfe`-Abbilder komprimieren sich sehr weit (Gaps und `0xE5`-Füllung), das Paket bleibt
also auch mit mehr Disketten klein. Ein Emulator-Update kostet wieder nur diese ~2 MB.

---

## 9. Verworfene Alternativen

| Ansatz | warum nicht |
|--------|-------------|
| **PyInstaller/Nuitka onefile** | bündelt Python + Qt ins Artefakt → 150–250 MB je Plattform, genau der Fall, den A2 ausschließt. Bleibt die Rückfallebene, falls Installation *ohne Internet* zur Anforderung wird — dann aber besser als optionales „Offline-Bundle" (Wheels per `uv pip download` ins Paket, `--offline` beim Installieren) statt als Standardweg. |
| **AppImage / Flatpak / Snap** | AppImage packt Qt mit ein (A2); Flatpak/Snap brauchen Laufzeitinstallation und sind Linux-only — der plattformübergreifende Bootstrap wäre trotzdem nötig. |
| **Systempython voraussetzen** | unter Windows unzuverlässig (kein Python, Store-Stub), unter Debian/Ubuntu fehlt oft `python3-venv`; die Fehlerbilder landen alle beim Anwender. |
| **PySide6 aus dem Payload** | ~70 MB je Plattform ins Paket, und die Wheels sind ohnehin plattform- **und** Python-Version-spezifisch — der Bootstrap müsste trotzdem die passende Python-Version festnageln. |
| **`pip install --user`** | verletzt A3 (Rückwirkung auf das Benutzer-Python), Konfliktrisiko mit anderen Anwendungen. |
| **Systemweite Installation (`/opt`, `Program Files`)** | verletzt A4. |

---

## 10. Umsetzungsreihenfolge

Vier Schritte, jeder für sich abnehmbar:

1. ✅ **Relocatable machen.** `app/paths.py`, Modulpfad-Auflösung in `format_catalog`,
   Umgebungsvariablen, `main.py --paths`. Guards: `tests/python/test_paths.py` (13 Fälle,
   darunter eine im Temp-Verzeichnis nachgebaute Installation).
2. ✅ **Paketierung Linux.** `packaging/` — `build_payload.sh`, `install.sh`, `launcher.sh`,
   `lib/common.sh`, `uv_pins.txt`, `requirements.in`/`.lock`, `.desktop`, Symbol. Ergebnis:
   1,9-MB-Tarball, der ohne `sudo` durchläuft, sich selbst prüft und sich rückstandsfrei
   deinstallieren lässt. Guards: `tests/python/test_packaging.py` (13 schnelle Fälle ohne
   Netz + ein vollständiger Installationslauf hinter `K1520_PACKAGING_FULL=1`).
3. **Windows-Portierung des Kerns.** Export-Makros, MSVC-Build mit statischer CRT,
   Rauchtest. Ergebnis: `k1520core.dll`, aus dem venv per `ctypes` ladbar. Die
   Pfad-/Umgebungsfragen sind mit Schritt 1 schon erledigt (§6.1).
4. **Paketierung Windows.** Inno-Setup-Skript, `install.ps1` (Gegenstück zu `install.sh`,
   dieselben Schritte), CI-Matrix, Release-Workflow.

macOS erst danach, zusammen mit der Signaturentscheidung aus §5.3. Die
plattformübergreifenden Teile sind bereits darauf ausgelegt (`uv_pins.txt` führt die
macOS-Tripel, `paths.py` kennt `.dylib` und `~/Library/Application Support`).

---

## 11. Offene Punkte

- **Code Signing**: Windows-Zertifikat (SmartScreen) und Apple Developer ID
  (Notarisierung) sind laufende Kosten — Entscheidung nötig, bevor über „Anwender" jenseits
  des Bekanntenkreises geredet wird.
- **Diskettenauswahl**: `build_payload.sh` legt vorerst sechs Abbilder bei (vier CP/A, eine
  SCPX, eine UDOS, Liste `DISKS_DEFAULT`). Unter welcher Lizenz die enthaltene
  Originalsoftware weitergegeben werden darf, ist eine Rechte-, keine Technikfrage — bis zur
  Klärung ist `--disks none` die konservative Variante.
- **Versionsprüfung Payload ↔ venv**: ob der Launcher bei Versionsversatz automatisch
  nachinstalliert oder nur warnt.
- **Proxy-Umgebungen**: `uv` respektiert `HTTPS_PROXY`; ob der Installer danach fragt, wenn
  der Download scheitert.

# Feinentwurf: Verteilbares Paket (Windows / Linux / macOS)

**Module:** `packaging/` (neu), `app/paths.py` (neu), `core/api/`,
`core/peripherals/floppy_drive/format_catalog.*`, `CMakeLists.txt`, `.github/workflows/`
**Verwandt:** `doc/design/10_c_api.md` (C-ABI), `doc/design/11_python_app.md` (GUI),
`doc/design/00_konfiguration.md` (`formats.yaml`-Suche)
**Stand:** 2026-08-07 — **Konzept, noch nicht umgesetzt.**

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
┌─ Payload (CI-Artefakt, plattformspezifisch, ~5–10 MB) ────────────────┐
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
│  Umgebung setzen (K1520_LIB, K1520_FORMATS, …) → venv-Python app/main │
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

1. **Vorprüfung**: Zielverzeichnis schreibbar? Freier Platz ≥ 500 MB? Netz erreichbar?
2. **`uv` beschaffen**: Download nach `<root>/tools/`, SHA256 gegen den im Payload
   mitgelieferten Erwartungswert prüfen. Ist ein `uv` bereits im `PATH` und aktuell genug,
   wird es benutzt (spart Download).
3. **CPython**: `uv python install <pinned>` — die Version steht im Payload (`VERSION`),
   nicht im Skript.
4. **venv** in `<root>/venv`.
5. **Abhängigkeiten**: `uv pip install --require-hashes -r requirements.lock`.
6. **Rauchtest**: `venv/bin/python -c "import PySide6; from app.core_binding..."` — lädt die
   Kernbibliothek einmal und ruft `k1520_version()`. Schlägt das fehl, bricht die
   Installation **mit Klartextmeldung** ab, statt einen kaputten Startmenüeintrag zu
   hinterlassen.
7. **Verknüpfungen** (Desktop-Datei bzw. Startmenü) und `VERSION`-Stempel schreiben.

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
| Linux | `~/.local/share/a5120emu` | `~/.config/k1520emu/` (Konfig, bestehend), `~/.local/share/a5120emu/disks/` (Arbeitsdisketten) | `~/.local/share/applications/a5120emu.desktop`, Starter `~/.local/bin/a5120emu` |
| Windows | `%LOCALAPPDATA%\A5120Emu` | `%APPDATA%\A5120Emu\` | Startmenü `%APPDATA%\Microsoft\Windows\Start Menu\Programs\` |
| macOS (später) | `~/Applications/A5120Emu.app/Contents/Resources` | `~/Library/Application Support/A5120Emu/` | die `.app` selbst |

Zwei Details, die sonst im Feld beißen:

- **Arbeitsdisketten müssen schreibbar liegen.** Der verzögerte Autosave
  (`DiskImage::autoFlush`, `doc/K1520_architecture.md` §8.7) schreibt in die *gemountete
  Datei* zurück. Eine direkt aus `share/disks/` gemountete Beispieldiskette würde also im
  Installationsverzeichnis verändert — und beim Update kommentarlos überschrieben. Der
  Launcher kopiert die Beispiele deshalb beim **Erststart** ins Benutzer-Diskettenverzeichnis
  und der Dateidialog zeigt von Anfang an dorthin (heute: `drive_widget.py:115`
  `_default_disk_dir()`, hart auf das Projektverzeichnis).
- **`formats.yaml` findet sich nicht von selbst.** `FormatCatalog::searchPaths()`
  (`format_catalog.h:44 ff.`) sucht u.a. bei `<Verzeichnis der Programmdatei>/../share/a5120emu/`
  — „Programmdatei" ist per `readlink("/proc/self/exe")` (`format_catalog.cpp:271`) ermittelt
  und damit bei einer per `ctypes` geladenen Bibliothek der **Python-Interpreter im venv**,
  nicht `<root>/bin/`. Kurzfristig deckt der Launcher das mit `K1520_FORMATS` ab (Kandidat 5,
  höchste Priorität); sauber ist die Auflösung über den **Modulpfad der Bibliothek** (§6.2).

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

### 5.2 Linux — Tarball + `install.sh`

`a5120emu-<version>-linux-x86_64.tar.gz` mit `install.sh`:

```
./install.sh                 # nach ~/.local/share/a5120emu
./install.sh --prefix DIR    # abweichendes Ziel
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
| `format_catalog.cpp:23,271` | `<unistd.h>` + `readlink("/proc/self/exe")` | Windows-Zweig `GetModuleFileNameW`; siehe auch §6.2 |
| `format_catalog.cpp:280` | `HOME`/`XDG_CONFIG_HOME` | Windows: `%APPDATA%` |
| Pfadtrenner | `K1520_FORMATS` ist `:`-getrennt | unter Windows `;` (dort ist `:` Teil von `C:\…`) |
| `CMakeLists.txt` | Build ist auf GCC/Linux zugeschnitten | MSVC-Zweig: statische CRT, `/utf-8` (die Quellen enthalten deutsche Umlaute!), Warnungsflags |

Der übrige Kern ist portabel (`std::filesystem`, keine POSIX-Aufrufe außerhalb dieser einen
Datei, kein `pthread`, keine `dlopen`-Nutzung).

### 6.2 Modulpfad statt Exe-Pfad

Sauberer als das Env-Pflaster: die Bibliothek ermittelt ihren **eigenen** Pfad
(`dladdr` unter Linux/macOS, `GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)`
unter Windows) und leitet daraus `<lib-dir>/../share/a5120emu/formats.yaml` ab. Damit ist
das Installationsverzeichnis **frei verschiebbar** und die Datenfindung funktioniert
gleichermaßen für die GUI (Lib in `bin/`), die Testwerkzeuge (Exe im Build-Baum) und den
Quellbaum. Kandidat 2 der Suchliste ändert dabei nur seine Bedeutung von „Exe-Verzeichnis"
zu „Verzeichnis des ladenden Moduls"; für die Werkzeuge, die statisch linken, bleibt es das
Exe-Verzeichnis.

### 6.3 GUI: zentrale Pfadauflösung (`app/paths.py`)

Heute sind die Pfade an drei Stellen verstreut und auf die Repo-Struktur verdrahtet:

- `app/core_binding/k1520.py:43–56` — sucht ausschließlich `libk1520core.so` in
  `<repo>/build/`, `/usr/local/lib`, `/usr/lib`
- `app/ui/drive_widget.py:119` — Disketten-Startverzeichnis über dreifaches `dirname`
- `app/main.py:34` — `sys.path`-Manipulation relativ zum Quellbaum
- `run_gui.sh` — setzt `LD_LIBRARY_PATH` auf `build/`

Ersetzt durch ein `app/paths.py` mit den Auflösungen `lib()`, `formats()`, `bundled_disks()`,
`user_disks()`, `config_dir()`, jeweils in der Reihenfolge **Umgebungsvariable →
Installationslayout → Quellbaum**. Ausdrücklich zu beachten:

- Dateiname je Plattform: `libk1520core.so` / `k1520core.dll` / `libk1520core.dylib`
- Windows: abhängige DLLs brauchen `os.add_dll_directory(<root>/bin)` **vor** `ctypes.CDLL`
  (seit Python 3.8 wird `PATH` dafür nicht mehr ausgewertet)
- `config_io.default_config_dir()` liefert unter Windows `C:\Users\…\.config\k1520emu` —
  funktioniert, ist aber unüblich; auf `%APPDATA%` umstellen (mit Migration eines bereits
  vorhandenen Verzeichnisses)

Der Quellbaum-Zweig bleibt erhalten: `run_gui.sh` und die Entwicklung aus `build/` heraus
müssen unverändert weiterlaufen.

---

## 7. Bauen der Payloads

Eine GitHub-Actions-Matrix, ein Job je Plattform, Artefakte ans Release.

| Plattform | Builder | Toolchain-Vorgaben |
|-----------|---------|--------------------|
| Linux x86_64 | Container mit alter glibc (manylinux_2_28 bzw. Ubuntu 22.04) | `-static-libstdc++ -static-libgcc`, Release/`-O3`. **Rückwärtskompatibilität kommt von der Baseline, nicht vom Zielsystem** — auf einem aktuellen Ubuntu gebaut, läuft die `.so` auf keiner älteren Distribution. |
| Windows x64 | `windows-latest`, MSVC | `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` (statische CRT — A4: kein VC-Redist ohne Admin), `/utf-8` |
| macOS (später) | `macos-latest` | `-mmacosx-version-min=12.0`, universal2 (arm64+x86_64) via `CMAKE_OSX_ARCHITECTURES` |

Weiter im Release-Job: Version aus `git describe` in `VERSION` und `k1520_version()`
stempeln, `requirements.lock` mit `uv pip compile --generate-hashes` erzeugen (nicht von
Hand pflegen), SHA256SUMS beilegen, die kuratierte Diskettenauswahl aus `disks/`
zusammenstellen. Und: **auf jeder Plattform den Rauchtest fahren** (Lib laden,
`k1520_version()`, `formats.yaml` finden) — ein Payload, dessen DLL nichts exportiert, darf
kein Release-Asset werden.

---

## 8. Größen

| Bestandteil | Größe | wann |
|-------------|-------|------|
| Kernbibliothek | ~0,6 MB | im Paket |
| GUI-Quellen + `formats.yaml` | ~0,5 MB | im Paket |
| Beispieldisketten (kuratiert, komprimiert) | ~3–5 MB | im Paket |
| **Download des Pakets** | **~5–10 MB** | |
| `uv` | ~15–20 MB | einmalig bei Installation |
| CPython (relocatable) | ~30 MB | einmalig bei Installation |
| PySide6-Essentials + PyYAML | ~70 MB | einmalig bei Installation |
| **Netzbedarf Erstinstallation** | **~120 MB** | |
| **Belegter Platz nach Installation** | **~350–450 MB** | entpackt |

Ein Emulator-Update kostet danach wieder nur die 5–10 MB der Payload.

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

Vorschlag in vier Schritten, jeder für sich abnehmbar:

1. **Relocatable machen (Linux).** `app/paths.py`, Modulpfad-Auflösung in
   `format_catalog`, Umgebungsvariablen `K1520_LIB`/`K1520_FORMATS`, `run_gui.sh` weiter
   lauffähig. Prüfbar durch: Installationslayout in ein Temp-Verzeichnis kopieren und von
   dort starten.
2. **Paketierung Linux.** `packaging/build_payload.sh`, `packaging/install.sh`, Launcher,
   `.desktop`. Ergebnis: Tarball, der auf einer frischen Distribution ohne `sudo`
   durchläuft.
3. **Windows-Portierung des Kerns.** Export-Makros, POSIX-Zweige, MSVC-Build, Rauchtest.
   Ergebnis: `k1520core.dll`, aus dem venv per `ctypes` ladbar.
4. **Paketierung Windows.** Inno-Setup-Skript, `bootstrap.ps1`, CI-Matrix, Release-Workflow.

macOS erst danach, zusammen mit der Signaturentscheidung aus §5.3.

---

## 11. Offene Punkte

- **Code Signing**: Windows-Zertifikat (SmartScreen) und Apple Developer ID
  (Notarisierung) sind laufende Kosten — Entscheidung nötig, bevor über „Anwender" jenseits
  des Bekanntenkreises geredet wird.
- **Diskettenauswahl**: welche der 28 Dateien aus `disks/` (36 MB) ins Paket gehören und
  unter welcher Lizenz die enthaltene CP/A-Software steht — das ist eine Rechte-, keine
  Technikfrage.
- **Versionsprüfung Payload ↔ venv**: ob der Launcher bei Versionsversatz automatisch
  nachinstalliert oder nur warnt.
- **Proxy-Umgebungen**: `uv` respektiert `HTTPS_PROXY`; ob der Installer danach fragt, wenn
  der Download scheitert.

# Feinentwurf: Verteilbares Paket (Windows / Linux / macOS)

**Module:** `packaging/` (neu), `app/paths.py` (neu), `core/api/`,
`core/peripherals/floppy_drive/format_catalog.*`, `CMakeLists.txt`, `.github/workflows/`
**Verwandt:** `doc/design/10_c_api.md` (C-ABI), `doc/design/11_python_app.md` (GUI),
`doc/design/00_konfiguration.md` (`formats.yaml`-Suche)
**Stand:** 2026-08-09 — **Schritte 1 und 2 umgesetzt** (relocatable + Linux-Paket,
`packaging/`, Guards `py_paths`/`py_packaging`), dazu Schlankmachen auf 146 MB (§8.1),
Löschschutz und Update-Pfad (§3.2) sowie die Umbenennung auf **K1520emu** (§4).
Windows und macOS stehen aus (§10).

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

- **Update**: derselbe Installer über dieselbe Wurzel. Die Payload wird ersetzt, die
  **Laufzeitumgebung neu aufgebaut** (`uv venv --clear`), der geladene Python unter
  `python/` bleibt liegen. Weiterbenutzen wäre verlockend — `uv` verweigert ein
  vorhandenes venv aber schlicht, und richtig wäre es auch nicht: das Schlankmachen (§8)
  einer älteren Fassung kann etwas entfernt haben, das die neue braucht, und
  wiederherstellen kann `uv` das nicht. Benutzerdaten (§4) werden nie angefasst.
  Gefunden wird die Wurzel dabei **von selbst**: der Starter in `~/.local/bin` zeigt darauf,
  und genau die wird als Ziel vorgeschlagen. Ohne das schlüge der Installer beim Update den
  Standardort vor — wer damals woanders installiert hat und Enter drückt, bekäme eine zweite
  Installation, während die alte verwaist liegen bliebe (Guard: der Update-Lauf in
  `test_installation_laeuft_durch_und_startet` ruft `install.sh -y` **ohne** `--prefix`).
- **Deinstallation**: entfernt wird **nur das Inventar aus dem Ausweis** — die Einträge, die
  der Installer selbst angelegt hat —, nicht das Verzeichnis als Ganzes. Ein `rm -rf` auf die
  Wurzel wäre einfacher, nähme dem Anwender aber alles mit, was er daneben abgelegt hat.
  Bleibt danach etwas übrig, bleibt auch das Verzeichnis stehen und der Installer zählt auf,
  was darin liegt. Das Inventar reist **in der Installation** (`.k1520emu-installation`),
  damit eine ältere Fassung nach ihrer eigenen Liste abgeräumt wird und nicht nach der einer
  neueren; ein Eintrag ist dabei ein *Name*, kein Pfad (`../…` wird verworfen, sonst zeigte
  ein verfälschter Ausweis aus der Installation heraus). Benutzerdaten separat und auf
  Nachfrage — der Windows-Uninstaller fragt, das Linux-Skript hat `--purge`.
  Guards: `test_deinstallieren_laesst_eigene_dateien_stehen`,
  `test_deinstallieren_raeumt_leere_wurzel_ganz_weg`,
  `test_deinstallieren_folgt_keinem_pfad_im_ausweis`.
- **Der Löschschutz** gehört zu beidem: seit das Ziel *erfragt* wird (§4), kann dort alles
  stehen — im schlimmsten Fall das Heimatverzeichnis. Deshalb zwei Riegel. Ziel werden darf
  nur ein leeres oder bereits von uns belegtes Verzeichnis (niemals `$HOME` oder `/`), und
  gelöscht wird nur, was sich ausweisen kann: die Datei `.k1520emu-installation`, ersatzweise
  das Inventar `VERSION` + `app/paths.py` + `share/k1520emu/` für Installationen aus der Zeit
  davor. Ohne diesen Ausweis bleibt das Verzeichnis stehen und der Installer sagt es.
  Guards: `test_installer_verweigert_das_heimatverzeichnis`,
  `test_installer_verweigert_fremdes_verzeichnis`,
  `test_deinstallieren_loescht_nur_eine_installation`.

---

## 4. Verzeichnislayout

Bewusst auf allen Plattformen **gleich aufgebaut**, nur die Wurzel unterscheidet sich.

> **Produkt heißt K1520emu, Programme heißen nach ihrer Maschine.** Emuliert wird die
> Rechnerfamilie am K1520-Bus; der A5120 ist die erste Maschine, weitere bekommen ein
> eigenes Programm in **derselben** Installation. Deshalb tragen Installation, Paket,
> `share/k1520emu/` und der Datenordner den Familiennamen, während Starter, Symbol und
> Startmenü-Eintrag `a5120emu` heißen. Eine neue Maschine bringt einen weiteren Starter samt
> `<name>.desktop.in` mit und wird in `MASCHINEN` (`install.sh`) eingetragen — daran findet
> das Deinstallieren ihre Verknüpfungen.

```
<install-root>/
  bin/          k1520core.so | k1520core.dll        (+ ggf. MinGW-Laufzeit-DLLs)
                a5120emu            Starter der Maschine (weitere daneben)
  app/          Python-Quellen der GUI
  share/k1520emu/formats.yaml
  share/disks/  mitgelieferte Beispieldisketten, GEPACKT (§8)
  tools/        uv[.exe]
  venv/         vom Bootstrap erzeugt
  requirements.lock, VERSION, LICENSE, .k1520emu-installation
```

| Plattform | `<install-root>` | Benutzerdaten | Verknüpfung |
|-----------|------------------|---------------|-------------|
| Linux | frei gewählt, Vorschlag `~/K1520emu` | `<Dokumente>/K1520emu/Disketten/` (Arbeitsdisketten), `~/.config/k1520emu/` (Konfig) | `~/.local/share/applications/a5120emu.desktop`, Starter `~/.local/bin/a5120emu` |
| Windows | `%LOCALAPPDATA%\K1520emu` | `%USERPROFILE%\Documents\K1520emu\` | Startmenü `%APPDATA%\Microsoft\Windows\Start Menu\Programs\` |
| macOS (später) | `~/Applications/K1520emu.app/Contents/Resources` | `~/Documents/K1520emu/` | die `.app` selbst |

Unter Linux wird das Ziel **erfragt** (Vorschlag `~/K1520emu`, `--prefix` und `-y` gehen
daran vorbei, ohne Terminal gilt kommentarlos der Vorschlag). Ein Pfad unter `~/.local` wäre
der übliche Ort für benutzereigene Software — aber dort findet ihn niemand wieder, und wer
seinen Emulator anfassen will, soll das können. Das Deinstallieren findet die Installation
trotzdem ohne `--prefix`: der Starter in `~/.local/bin` verrät sie.

**Der Dokumentenordner ist kein fester Name.** Er heißt `~/Dokumente`, `~/Documents`,
`~/Documenti` — verbindlich ist `XDG_DOCUMENTS_DIR` aus `~/.config/user-dirs.dirs`, die die
Desktop-Umgebung selbst pflegt. Gelesen wird die Datei direkt statt über das Programm
`xdg-user-dir` (das auf schlanken Systemen fehlt); gibt es sie nicht, wird `~/Documents` bzw.
`~/Dokumente` probiert und zuletzt auf `~/.local/share/K1520emu` zurückgefallen. Die Regel
steht **zweimal** — `paths.documents_dir()` für den Emulator und `dokumente_dir()` in
`lib/common.sh`, damit `--purge` dort aufräumt, wo der Emulator schreibt. Guard:
`test_dokumentenordner_shell_und_python_stimmen_ueberein`.

Die **Benutzerdaten** liegen damit dort, wo der Anwender sie sucht — und getrennt vom
Programm:

- **Arbeitsdisketten müssen schreibbar liegen.** Der verzögerte Autosave
  (`DiskImage::autoFlush`, `doc/K1520_architecture.md` §8.7) schreibt in die *gemountete
  Datei* zurück. Eine direkt aus `share/disks/` gemountete Beispieldiskette würde also im
  Installationsverzeichnis verändert — und beim Update kommentarlos überschrieben.
  `paths.seed_user_disks()` packt die Beispiele deshalb **beim ersten Start** einmalig
  ins Benutzerverzeichnis aus (und fasst ein vorhandenes nie wieder an); der Dateidialog zeigt über
  `paths.default_disk_dir()` von Anfang an dorthin.
- **`formats.yaml` findet sich nicht von selbst.** `FormatCatalog::searchPaths()` sucht u.a.
  bei `<Verzeichnis der Programmdatei>/../share/k1520emu/` — war das per
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

`k1520emu-<version>-linux-x86_64.tar.gz` (~2 MB) mit `install.sh`:

```
./install.sh                 # fragt nach dem Ziel (Vorschlag ~/K1520emu)
./install.sh --prefix DIR    # ohne Rückfrage dorthin
./install.sh -y              # ohne Rückfrage in den Vorschlag
./install.sh --python 3.13   # andere Python-Fassung
./install.sh --no-shortcut   # ohne Startmenü-Eintrag
./install.sh --no-slim       # Python und Qt vollständig lassen
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
`<lib-dir>/../share/k1520emu/formats.yaml` ab. Damit ist das Installationsverzeichnis
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
| Belegter Platz, ungeschlankt | 406 MB | gemessen |
| **Belegter Platz nach Installation** | **146 MB** | gemessen, nach §8.1 |

Die `.hfe`-Abbilder komprimieren sich sehr weit (Gaps und `0xE5`-Füllung), das Paket bleibt
also auch mit mehr Disketten klein. Ein Emulator-Update kostet wieder nur diese ~2 MB. Aus
demselben Grund liegen sie auch **in der Installation gepackt** (`*.hfe.gz`) und werden erst
beim ersten Start ins Arbeitsverzeichnis des Anwenders ausgepackt
(`paths.seed_user_disks()`) — ungepackt lägen sie danach doppelt auf der Platte.

### 8.1 Schlankmachen (`slim.py`)

Von den 406 MB gehören keine 15 dem Emulator; der Rest ist, was Python und Qt an Beiwerk
mitbringen. `install.sh` ruft nach dem Einrichten `slim.py` auf, das entfernt, was diese
Anwendung nie lädt: den QML/Quick-Stapel, die Qt-Entwicklungswerkzeuge (Designer, Linguist,
qmlls), die Bindungen nicht importierter Module, Typstubs und shiboken-Baumaterial, die
Übersetzungen außer de/en, CPythons Testsuite, IDLE und Tcl/Tk.

Drei Regeln, an denen sich das entlanghangelt — jede davon ist Erfahrung, keine Vorliebe:

- **Welche Qt-Bibliotheken bleiben, entscheidet `ldd`, keine Liste.** Ausgehend von den
  importierten Bindungen und den Laufzeit-Plugins wird die Hülle gebildet; was nicht darin
  liegt, fliegt. Das ist der große Posten (~130 MB). Beim Auswerten der `ldd`-Zeilen wird die
  **Ladeadresse am Ende** abgetrennt, nicht am ersten Leerzeichen — der Installationspfad
  kommt vom Anwender und darf Leerzeichen enthalten. (Am Leerzeichen geschnitten blieb die
  Hülle leer, der Sicherheitsrückfall griff, und „`~/Emulator Test`" bekam 223 statt 146 MB.)
- **Gestrippt werden nur Bibliotheken, nie Programme.** Der Interpreter von
  python-build-standalone überlebt `strip` in *keiner* Variante: die Warnung
  „allocated section `.dynstr' not in segment", eine geschriebene Datei — und beim Start
  „undefined symbol: , version". `--strip-debug` spart dort nicht einmal Platz. Gearbeitet
  wird auf einer Kopie, und **eine Warnung von `strip` genügt, sie zu verwerfen**.
- **Der Rauchtest ist die Gegenprobe.** Er baut das Hauptfenster wirklich auf (offscreen);
  fehlt ein Qt-Plugin, kommt er nicht durch und die Installation bricht ab, statt einen
  kaputten Startmenü-Eintrag zu hinterlassen. Genau so ist der Interpreter-Fehler oben
  aufgefallen.

Was übrig bleibt, ist nicht mehr sinnvoll zu drücken: 45 MB CPython und von den 71 MB Qt
allein **37,8 MB ICU** (`libicudata` 31 MB), das als `NEEDED` direkt an `libQt6Core` hängt und
nur mit einem eigenen Qt-Bau loszuwerden wäre. `--no-slim` behält alles.

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

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

> **Unter Windows läuft dieser Ablauf ohne `uv`** — dort macht ihn der
> Installationsassistent selbst, und CPython kommt direkt von
> python-build-standalone (§5.1: `uv python install` legt einen Junction an, den der
> OneDrive-Filtertreiber verweigert). Schritte 2 und 3 entfallen dadurch, der Rest
> ist derselbe, bis hin zum Rauchtest.

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

### 5.1 Windows — Inno Setup, per-user   ✅ umgesetzt

Ausgeliefert wird ein `A5120Emu-<version>-win-x64-setup.exe`, gebaut mit **Inno Setup**:

```ini
PrivilegesRequired=lowest                  ; Vorgabe: ohne UAC
PrivilegesRequiredOverridesAllowed=dialog  ; …der Anwender darf aber wählen
DefaultDirName={autopf}\K1520emu           ; folgt der Wahl (s. u.)
DisableDirPage=no                          ; Pfad änderbar (Roaming-Profile!)
ArchitecturesAllowed=x64compatible
Uninstallable=yes                          ; Eintrag unter HKCU\…\Uninstall
```

- **Wohin installiert wird, entscheidet der Anwender** (seit 2026-08-14).
  `{autopf}` löst nach der gewählten Betriebsart auf: „für alle Benutzer"
  (Administrator) → `C:\Program Files\K1520emu`, „nur für mich" (ohne UAC) →
  `%LOCALAPPDATA%\Programs\K1520emu`. Vorher stand dort fest
  `%LOCALAPPDATA%\K1520emu` — nicht falsch, aber **versteckt**: „ich musste eine
  Weile suchen, bis ich es finde" (Rückmeldung vom Testgerät). `…\Programs` ist
  der Ort, den sich per-user-Installationen unter Windows teilen (VS Code,
  Signal …), nicht `%LOCALAPPDATA%` selbst. Dass „Programme" überhaupt in Frage
  kommt, liegt daran, dass der Emulator **zur Laufzeit nicht in sein eigenes
  Verzeichnis schreibt** — die Arbeitsdisketten liegen im Dokumentenordner
  (§4). Geprüft ist beides, einschließlich des Leerzeichens in
  „Program Files": Installation, Bootstrap und Starter halten den Pfad.
- **Der Assistent sagt vorher, was er vorhat.** Eine eigene Seite nach der
  Lizenz (`CreateOutputMsgPage`) nennt die beiden Dinge, die ein Anwender nicht
  erwartet: dass **während** der Installation rund 120 MB aus dem Netz kommen
  (ohne die Ansage sieht ein minutenlanger Balken wie ein hängendes Setup aus)
  und dass **alles im Installationsordner bleibt** — kein Systempython wird
  angefasst, nichts registriert, beim Entfernen bleibt nichts zurück. Dasselbe
  noch einmal knapp in der Zusammenfassung vor dem Zugriff
  (`UpdateReadyMemo`). Guard: `test_iss_sagt_vorher_was_geladen_wird_und_was_unberuehrt_bleibt`.

- **Assistentenseite „Arbeitsdisketten"**: Vorgabe `<Dokumente>\K1520emu`, änderbar.
  Dieselbe Abfrage wie in `install.sh --data`, und aus demselben Grund — „Dokumente"
  ist hier häufig nach OneDrive umgeleitet. Die Antwort wird nur eingetragen, wenn sie
  von der Vorgabe abweicht (`K1520_DATA`, §6.1).
- **Das Setup installiert selbst** (`packaging/k1520emu.iss`), seit 2026-08-14 ohne
  jedes PowerShell-Skript: laden, auspacken, venv, `pip --require-hashes`,
  schlankmachen, Starter, Rauchtest. Jeder Schritt steht im Klartext in der
  Statuszeile und in `<app>\bootstrap.log`; die Ausgabe der aufgerufenen Programme
  läuft zeilenweise über `ExecAndLogOutput` mit.
  Vorher rief das Setup ein `install.ps1`. Das kostete drei Dinge, alle beim Anwender
  aufgeschlagen: ein **schwarzes Fenster ohne Rückmeldung** über Minuten, die
  Abhängigkeit von Windows PowerShell 5.1 samt Ausführungsrichtlinie
  („Ausführung von Skripts ist auf diesem System deaktiviert" beim Aufruf von Hand),
  und einen zweiten Installationsweg mit eigenen Fehlern.
- **Python kommt direkt von python-build-standalone, nicht über `uv`.**
  `uv python install` legt zum Schluss einen **Junction** auf die Nebenversion an
  (`python\cpython-3.12-…`). Wo OneDrive „Dateien bei Bedarf" läuft, verweigert dessen
  Filtertreiber das: `os error 448` / `STATUS_UNTRUSTED_MOUNT_POINT`, „der Pfad kann
  nicht durchlaufen werden, da er einen nicht vertrauenswürdigen Bereitstellungspunkt
  enthält" (astral-sh/uv #19616) — der Fehlschlag auf dem Testgerät am 2026-08-14.
  Abschalten lässt sich der Junction nicht. Das Setup lädt das Archiv deshalb selbst
  (Pins in `packaging/python_pins.txt`, Prüfsumme reist mit) und packt es in ein
  gewöhnliches Verzeichnis aus. Unter Linux bleibt es bei `uv`.
- **Der Zeitpunkt ist eine Entscheidung über den Fehlschlag.** Alles Nachladbare läuft
  in `PrepareToInstall`, also **vor der ersten kopierten Datei**. Eine Ausnahme in
  `ssPostInstall` räumt nämlich *nichts* zurück — der Probelauf hinterließ eine halbe
  Installation samt drei Startmenü-Einträgen, die ins Leere zeigten, also genau den
  Ausgang, den §3.1 ausschließen will. Scheitert `PrepareToInstall`, ist nichts
  kopiert, kein Symbol angelegt, kein Deinstallierer eingetragen; was der Bootstrap
  selbst schon anlegte (`python\`, `venv\`), räumt er weg. Im Nachlauf bleibt nur,
  was die Payload braucht: Starter, Ausweis, Rauchtest — und scheitert der, werden
  wenigstens die Verknüpfungen wieder entfernt.
- **Das Deinstallieren macht Inno selbst.** Es entfernt von sich aus nur, was es
  angelegt hat; das Nachgeladene (`python\`, `venv\`, `bin\`, Protokolle) steht
  namentlich in `[UninstallDelete]`. Ein pauschales `filesandordirs {app}` bleibt
  verboten — das Zielverzeichnis ist im Assistenten änderbar, dort kann Fremdes
  liegen. Guard: der Paketjob installiert über das Setup und deinstalliert wieder,
  wobei eine fremde Datei daneben überleben muss.
- **Der Diskettenordner ist auch still wählbar** (`/Daten=…`, im Assistenten die
  eigene Seite) — sonst könnte die CI nicht prüfen, dass die Beispieldisketten dort
  landen, wo der Anwender sie erwartet.
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
                             #   und nach dem Datenordner (<Dokumente>/K1520emu)
./install.sh --prefix DIR    # ohne Rückfrage dorthin
./install.sh --data DIR      # Arbeitsdisketten und Zustände dorthin
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

### 6.1 Windows-Portierung des Kerns   ✅ umgesetzt

| Fundstelle | Problem | Lösung |
|------------|---------|--------|
| `core/api/k1520_api.h`, `k1520_disk_api.h` | nur `extern "C"`, keine Export-Makros | **`core/api/k1520_export.h`**: `K1520_API` = `__declspec(dllexport)` beim Bau (`K1520_BUILD_SHARED`, von CMake auf `k1520core`/`k1520disk` gesetzt), `dllimport` für einen künftigen C-Nutzer (`K1520_USE_SHARED`), sonst `visibility("default")`. Steht vor **jeder** der 44 + 48 Funktionen. **Ohne das exportiert die MSVC-DLL gar nichts** — `ctypes` findet keine einzige Funktion. |
| `CMakeLists.txt` | Build war auf GCC/Linux zugeschnitten | MSVC-Zweig: `/utf-8` (die Quellen sind UTF-8 und voller Umlaute), `/permissive- /W3`, `/bigobj` (die Kartenmodelle mit eingebetteten EPROM-Feldern sprengen sonst das Sektionslimit), `NOMINMAX`/`WIN32_LEAN_AND_MEAN`, und die statische CRT hinter `-DK1520_MSVC_STATIC_CRT=ON` |
| `tools/boot_trace.cpp`, `tools/k1520dbg.cpp`, zwei Tests | `unistd.h`, `getpid`, `isatty`, `setenv` | **`core/util/os_compat.h`** — `k1520::os::processId/isTerminal/setEnv/unsetEnv`, die vier Stellen an einem Ort statt vier `#ifdef _WIN32` |
| `core/logger.h` | `__builtin_strrchr` hinter `#ifdef __linux__` | Bedingung auf `__GNUC__`/`__clang__` erweitert; MSVC nimmt `__FILE__` unverkürzt |
| (neu) `.gitattributes` | Git unter Windows wandelt beim Auschecken LF → CRLF, **auch in Dateien, die es fälschlich für Text hält** | `* -text` — gar keine Umwandlung. Ein eingeschobenes 0x0D in einer `.hfe`/`.img` verschiebt eine ganze Spur und sieht wie ein Emulatorfehler aus. |

**Statische CRT nur im Auslieferungsbau.** `K1520_MSVC_STATIC_CRT` steht auf `OFF`,
weil GoogleTest im selben Baum die dynamische CRT erwartet (`gtest_force_shared_crt`)
und `/MT` neben `/MD` beim Linken „mismatch detected for RuntimeLibrary" gibt. Der
Release-Bau setzt sie zusammen mit `-DBUILD_K1520_TESTS=OFF`; nur die ausgelieferte
DLL braucht sie (§5.1: kein VC-Redist ohne Administratorrechte).

**Geprüft wird auf zwei Wegen.** Verbindlich ist `.github/workflows/windows-ci.yml`
(MSVC, `windows-latest`, dieselbe `tools/dev.sh test`-Runde wie Linux, danach
`dumpbin /exports` auf beide DLLs). Für die schnelle Runde auf dem Linux-Rechner gibt
es `tools/dev.sh win`: Cross-Bau mit MinGW-w64 (`cmake/toolchain-mingw64.cmake`) und
Tests unter `wine`. Der findet in Sekunden, was plattformabhängig ist, aber **nicht**,
was MSVC-eigen ist — MinGW ist GCC und exportiert wie unter Linux per Vorgabe alles.

**Der Datenordner wird beim Installieren erfragt** (2026-08-12) — damit ist auch die
offene OneDrive-Frage beantwortet, ohne sie zentral zu entscheiden. Vorgeschlagen wird
`<Dokumente>/K1520emu`; wählt der Anwender etwas anderes, trägt der Installer es als
`K1520_DATA` in **beide** Starter ein (Emulator und Diskettenwerkzeug — sonst öffnete
dessen Dateidialog woanders). **Die Vorgabe wird bewusst NICHT festgeschrieben:** sonst
stünde dort ein fester Pfad, und die Auflösung könnte einem später umbenannten
Dokumentenordner oder einem Sprachwechsel nicht mehr folgen. Guards:
`test_env_data_schlaegt_den_dokumentenordner`,
`test_ohne_env_data_bleibt_die_aufloesung_dynamisch`,
`test_ersetze_platzhalter_traegt_den_datenordner_ein`.

Warum überhaupt gefragt wird: unter Windows ist „Dokumente" häufig nach OneDrive
umgeleitet, und der Autosave schreibt bei **jedem** Diskettenzugriff zurück — das in
einen synchronisierten Ordner zu legen, ist eine Entscheidung des Anwenders. Der
Windows-Installer (§5.1) bekommt dieselbe Abfrage als Assistentenseite.

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
(`K1520_HOME`/`K1520_LIB`/`K1520_FORMATS`/`K1520_DATA`/`K1520_DISKS`) → Installationslayout →
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

### 7.1 Stand der Umsetzung

| Datei | Auslöser | Inhalt |
|-------|----------|--------|
| `.github/workflows/ci.yml` | nur von Hand | `tools/dev.sh test` (C++ + Python-Ebene) auf `ubuntu-latest` |
| `.github/workflows/slow-tests.yml` | nur von Hand | `test-format`, `test-matrix` |
| `.github/workflows/release.yml` | von Hand oder Tag `v*` | `build_payload.sh` auf **ubuntu-22.04**, Rauchtest, Asset am Release-Entwurf |
| `.github/workflows/windows-probe.yml` | nur von Hand | MSVC-Bau der Kernbibliothek — **noch rot** (§6.1) |

Bedienung, nötige GitHub-Einstellungen und Fehlersuche: **`doc/ci_pipeline.md`**.

Drei Festlegungen, die man beim Ändern nicht übersehen darf:

- **Kein Lauf startet von selbst.** Kein Push-, kein PR-, kein Zeitplan-Auslöser — die
  Regression läuft lokal vor jedem Push (`.githooks/pre-push`), der Lauf auf GitHub ist die
  Gegenprobe auf sauberem System und wird angestoßen, wenn jemand sie haben will. Einzige
  Ausnahme: ein gepushter Tag `v*` baut das Release-Paket.

- **Alle Testjobs gehen durch `tools/dev.sh`**, nie durch rohe `cmake`/`ctest`-Aufrufe. Dort
  stehen Build-Typ, `LOG_LEVEL` und die ausgeschlossenen Label; eine CI, die daran vorbeiruft,
  prüft etwas anderes als der Entwickler vor dem Push.
- **Der Release-Job läuft auf `ubuntu-22.04`, nicht auf `ubuntu-latest`.** Die
  Rückwärtskompatibilität kommt von der Baseline (oben in diesem Abschnitt). Wird das Abbild
  abgekündigt, ist der Nachfolger ein Container mit alter glibc — nicht das jeweils neueste
  Ubuntu.

Der Rauchtest des Release-Jobs prüft genau die drei Dinge, an denen ein Paket still kaputt sein
kann: die Bibliothek lädt und `k1520_version()` antwortet, `app/main.py --paths` erkennt das
Installationslayout und findet Bibliothek **und** `formats.yaml`, und im Binärabbild steht kein
Pfad des Baurechners (Gegenprobe zu `-DK1520_FORMATS_DEFAULT=`).

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
3. ✅ **Windows-Portierung des Kerns.** Export-Makros (`core/api/k1520_export.h`),
   MSVC-Zweig im `CMakeLists.txt`, `core/util/os_compat.h`, `.gitattributes`.
   Ergebnis: `k1520core.dll`/`k1520disk.dll`, aus dem venv per `ctypes` ladbar, und
   die **volle Regression** läuft auf `windows-latest` (`windows-ci.yml`) — nicht nur
   ein Rauchtest. Lokale Vorprüfung: `tools/dev.sh win` (MinGW + wine). Die
   Pfad-/Umgebungsfragen waren mit Schritt 1 schon erledigt (§6.1).
4. ✅ **Paketierung Windows.** Ausgeliefert wird **eine** Datei: das Setup
   (`packaging/k1520emu.iss`, gebaut von `build_payload.sh --setup`). Es installiert
   selbst — kein `install.ps1` mehr, kein PowerShell im Installationsweg (§5.1);
   `launcher.cmd`/`disktool_launcher.cmd` füllt es aus den Vorlagen. Die
   **Windows-Payload** baut `build_payload.sh` mit (vier plattformabhängige Stellen:
   Bibliotheksnamen, Laufzeitbindung, beigelegte Starter, `.zip` statt `.tar.gz`); das
   `.zip` hängt seit 2026-08-14 **nicht mehr am Release** — ohne Installationsskript
   wäre es für einen Anwender nutzlos, es bleibt Prüfstück der CI. Das Schlankmachen
   greift auch unter Windows (**116 MB**, gemessen 2026-08-14 im Paketjob). Guards: die
   Textprüfungen an der `.iss` in `tests/python/test_packaging.py` (kein PowerShell,
   Bootstrap vor dem Kopieren, Aufräumen nach Fehlschlag, `dontcopy` für früh
   gebrauchte Dateien, vollständiges `[UninstallDelete]`, Pins passen zur
   Wheel-Fassung) und der Job `paket` in `windows-ci.yml`, der das Setup **wirklich
   fährt** (`-f paket=true`) — installieren, Schlankmachen prüfen, deinstallieren,
   wobei eine fremde Datei daneben überlebt. Das Symbol liegt seit 2026-08-14
   als `packaging/icon.ico` bei (aus dem SVG mit `tools/svg_to_ico.py`, sieben
   Auflösungen) und hängt an vier Stellen: Setup, Eintrag unter „Apps" und
   beiden Verknüpfungen — ohne das trüge alles davon das Python-Symbol, weil
   die Verknüpfung auf `pythonw.exe` zeigt. **Offen:** die Signatur
   (§11 — SmartScreen warnt bei jedem Erstinstall).

macOS erst danach, zusammen mit der Signaturentscheidung aus §5.3. Die
plattformübergreifenden Teile sind bereits darauf ausgelegt (`uv_pins.txt` führt die
macOS-Tripel, `paths.py` kennt `.dylib` und `~/Library/Application Support`).

---

## 11. Offene Punkte

- **Code Signing**: Windows-Zertifikat (SmartScreen) und Apple Developer ID
  (Notarisierung) sind laufende Kosten — Entscheidung nötig, bevor über „Anwender" jenseits
  des Bekanntenkreises geredet wird.
- **Diskettenauswahl**: `build_payload.sh` legt sechs Abbilder bei (vier CP/A, eine
  SCPX, eine UDOS, Liste `DISKS_DEFAULT`) — genug, um jedes der drei Betriebssysteme
  zu starten, ohne das Paket aufzublähen. Mit `--disks all` kommen alle aus `disks/`
  mit, mit `--disks none` keine.
- ~~**Schlankmachen unter Windows**~~ ✅ erledigt 2026-08-12: `slim.py` liest die
  PE-Importtabelle selbst (`pe_imports`), weil auf dem Rechner des Anwenders kein
  `dumpbin` liegt. Eine Windows-Installation belegt damit **116 MB** (gemessen
  2026-08-14 im Paketjob, `--disks none`).

  Dabei ist am 2026-08-14 ein stiller Aussetzer aufgefallen, der 40 MB kostete:
  `slim_cpython` suchte den Interpreter ausschließlich unter `<root>/python/*/`
  — die Anordnung, die `uv python install` erzeugt. Der Windows-Assistent packt
  ihn direkt nach `<root>/python/`, dort traf **kein einziges** Muster, und
  Tcl/Tk, IDLE, `ensurepip`, die Header und ein zweites `pip` blieben stehen,
  ohne dass irgendetwas fehlschlug. `cpython_baeume()` erkennt jetzt beide
  Anordnungen (Merkmal: `Lib`/`lib` unmittelbar darunter). Zwei weitere Posten
  kamen dazu: `pip` fliegt aus der **Laufzeit**umgebung (unter Linux legt
  `uv venv` von vornherein keines an, `python -m venv` schon — 11 MB), und
  unter Windows fliegt **OpenSSL** (`libcrypto-3` allein 8 MB): der Emulator
  redet mit nichts im Netz, `hashlib` rechnet ohne `_hashlib` mit den
  eingebauten Implementierungen weiter, und pip braucht es nur beim
  Einrichten — da ist es noch da. Guards: `test_slim_findet_cpython_in_BEIDEN_anordnungen`,
  `test_slim_raeumt_die_windows_anordnung_wirklich_ab`,
  `test_slim_wirft_pip_aus_der_laufzeitumgebung`, dazu die auf **130 MB**
  verschärfte Schranke im Paketjob (mit den vorherigen 220 MB gingen die
  163 MB als „in Ordnung" durch).
- **Versionsprüfung Payload ↔ venv**: ob der Launcher bei Versionsversatz automatisch
  nachinstalliert oder nur warnt.
- **Proxy-Umgebungen**: `uv` respektiert `HTTPS_PROXY`; ob der Installer danach fragt, wenn
  der Download scheitert.

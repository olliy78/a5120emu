"""
K1520-Emulator — zentrale Pfadauflösung
=======================================

Eine Stelle beantwortet, wo die Kernbibliothek, der Formatkatalog, die
mitgelieferten Disketten und die Benutzerdaten liegen — im **Quellbaum**
(Entwicklung, Bibliothek aus ``build/``) genauso wie in einer **Installation**
aus einem verteilten Paket (Konzept: ``doc/design/13_distribution.md``).

Jede Auflösung geht dieselbe Reihenfolge durch:

1. **Umgebungsvariable** — ``K1520_LIB``, ``K1520_FORMATS``, ``K1520_HOME``,
   ``K1520_DISKS``.  Hat immer Vorrang, damit sich von außen (Launcher, Test,
   Fehlersuche) jeder Pfad umbiegen lässt.
2. **Installationslayout** ``<root>/{bin,app,share/k1520emu,share/disks}``
3. **Quellbaum** ``<repo>/{build,app,data,disks}``

``<root>`` und ``<repo>`` sind dabei **dasselbe** Verzeichnis — das
Elternverzeichnis von ``app/`` —, die beiden Layouts unterscheiden sich nur in
ihren Unterverzeichnissen.  Deshalb braucht es keine Modusumschaltung: es wird
schlicht der erste existierende Kandidat genommen, und eine Installation ist
allein dadurch frei verschiebbar.

Benutzerdaten liegen **nie** im Installationsverzeichnis: Arbeitsdisketten
werden vom Emulator zurückgeschrieben (verzögerter Autosave, siehe
``doc/K1520_architecture.md`` §8.7) und würden beim nächsten Update
kommentarlos überschrieben.

Beispiel::

    from app import paths
    lib = paths.core_library()          # …/build/libk1520core.so
    paths.prepare_library_load()        # Windows: DLL-Suchpfad anmelden
"""

import os
import sys
from pathlib import Path
from typing import List, Optional

# ─── Umgebungsvariablen ──────────────────────────────────────────────────────

ENV_HOME = "K1520_HOME"        # Wurzel der Installation bzw. des Quellbaums
ENV_LIB = "K1520_LIB"          # Kernbibliothek (Datei oder Verzeichnis)
ENV_FORMATS = "K1520_FORMATS"  # Formatkatalog (Datei oder Verzeichnis)
ENV_DATA = "K1520_DATA"        # Datenordner des Anwenders (enthält Disketten/)
ENV_DISKS = "K1520_DISKS"      # Verzeichnis der Arbeitsdisketten
# ENV_DISKS ist die SPEZIELLERE Angabe und schlägt deshalb ENV_DATA: wer nur die
# Disketten woanders haben will, soll dafür nicht den ganzen Datenordner
# verschieben müssen (dort liegen auch Zustände und `logs/`).

#: Name des Formatkatalogs (identisch mit ``kCatalogFileName`` im Kern).
FORMATS_FILE = "formats.yaml"

#: Verzeichnisname der Benutzerkonfiguration — historisch ``k1520emu``,
#: NICHT ``a5120emu``; eine Umbenennung würde bestehende Konfigurationen
#: unsichtbar machen.
CONFIG_DIRNAME = "k1520emu"

#: Verzeichnisname der Benutzerdaten im Dokumentenordner.  Das Projekt heißt
#: nach der Rechnerfamilie, nicht nach einer Maschine: der A5120 ist die erste,
#: weitere K1520-Rechner bekommen eigene Programme in derselben Installation.
DATA_DIRNAME = "K1520emu"

#: Unterordner der Arbeitsdisketten.  Bewusst eine Ebene tiefer, damit später
#: Druckausgaben o. Ä. danebenpassen, ohne die Disketten umziehen zu müssen.
DISKS_DIRNAME = "Disketten"

#: Unterordner für Dateien, die das DiskTool von Disketten holt bzw. auf sie
#: schreibt — der Platz „daneben", den DISKS_DIRNAME oben vorsieht.  Getrennt
#: von den Disketten, weil es zwei verschiedene Dinge sind: hier liegen einzelne
#: Dateien und die Beiblätter, dort die Abbilder ganzer Datenträger.
FILES_DIRNAME = "Dateien"

#: Verzeichnisname unter Windows/macOS, wo Programmnamen großgeschrieben sind.
APP_DIRNAME = "K1520emu"


# ─── Plattform ───────────────────────────────────────────────────────────────

def _is_windows() -> bool:
    return sys.platform.startswith("win")


def _is_macos() -> bool:
    return sys.platform == "darwin"


#: Die beiden Bibliotheken der Installation.  ``k1520core`` ist der Emulator,
#: ``k1520disk`` das Dateisystem-Werkzeug (k1520DiskTool) — bewusst getrennt,
#: siehe doc/design/13_k1520disktool.md §2.
CORE_STEM = "k1520core"
DISK_STEM = "k1520disk"


def library_filenames(stem: str = CORE_STEM) -> List[str]:
    """Mögliche Dateinamen einer Bibliothek auf dieser Plattform.

    Der zweite Name deckt eine später vergebene SOVERSION ab (heute baut CMake
    ohne, siehe ``CMakeLists.txt``).
    """
    if _is_windows():
        return [f"{stem}.dll", f"lib{stem}.dll"]
    if _is_macos():
        return [f"lib{stem}.dylib", f"lib{stem}.1.dylib"]
    return [f"lib{stem}.so", f"lib{stem}.so.1"]


# ─── Wurzel ──────────────────────────────────────────────────────────────────

def base_dir() -> Path:
    """Wurzel von Installation bzw. Quellbaum (Elternverzeichnis von ``app/``)."""
    env = os.environ.get(ENV_HOME)
    if env:
        return Path(env).expanduser().resolve()
    return Path(__file__).resolve().parents[1]


def is_installed_layout() -> bool:
    """Läuft der Emulator aus einer Installation (statt aus dem Quellbaum)?

    Erkennungsmerkmal ist ``<root>/bin`` mit einer Kernbibliothek darin — der
    Quellbaum hat sie in ``build/``.
    """
    bin_dir = base_dir() / "bin"
    return any((bin_dir / name).exists() for name in library_filenames())


# ─── Kernbibliothek ──────────────────────────────────────────────────────────

def library_candidates(stem: str = CORE_STEM) -> List[Path]:
    """Alle Kandidaten für eine Bibliothek in absteigender Priorität.

    Wird auch für die Fehlermeldung gebraucht („gesucht in: …"), enthält
    deshalb bewusst auch nicht existierende Pfade.
    """
    names = library_filenames(stem)
    out: List[Path] = []

    # 1) explizite Vorgabe — Datei oder Verzeichnis.  Sie gilt nur für den
    #    Emulatorkern; eine Vorgabe für BEIDE Bibliotheken zugleich wäre
    #    zweideutig (dieselbe Datei kann nicht beides sein).
    env = os.environ.get(ENV_LIB) if stem == CORE_STEM else None
    if env:
        p = Path(env).expanduser()
        out.extend([p / n for n in names] if p.is_dir() else [p])

    base = base_dir()
    # 2) Installationslayout
    out.extend(base / "bin" / n for n in names)
    # 3) Quellbaum
    out.extend(base / "build" / n for n in names)

    # 4) systemweite Installation (nur unixoid; unter Windows sinnlos)
    if not _is_windows():
        for d in ("/usr/local/lib", "/usr/lib"):
            out.extend(Path(d) / n for n in names)
    return out


def library(stem: str = CORE_STEM) -> Path:
    """Pfad einer Bibliothek (Quellbaum wie Installation).

    Raises:
        FileNotFoundError: mit der vollständigen Kandidatenliste und einem
            Hinweis, wie sie zu bauen ist.
    """
    candidates = library_candidates(stem)
    for path in candidates:
        if path.exists():
            return path.resolve()

    listing = "\n".join(f"  {p}" for p in candidates)
    hinweis = (f"Oder Pfad vorgeben:  {ENV_LIB}=/pfad/zur/bibliothek"
               if stem == CORE_STEM else "")
    raise FileNotFoundError(
        f"Bibliothek ({' / '.join(library_filenames(stem))}) nicht gefunden.\n"
        f"Gesucht in:\n{listing}\n\n"
        f"Bauen:  tools/dev.sh build\n" + hinweis
    )


def core_library() -> Path:
    """Pfad der Kernbibliothek (Emulator)."""
    return library(CORE_STEM)


def disk_library() -> Path:
    """Pfad der Bibliothek des k1520DiskTool."""
    return library(DISK_STEM)


def prepare_library_load() -> None:
    """Vorbereitung, damit ``ctypes`` die Bibliothek samt Abhängigkeiten lädt.

    Nur unter Windows nötig und dort zwingend: seit Python 3.8 wertet
    ``ctypes.CDLL`` ``PATH`` nicht mehr aus, abhängige DLLs (etwa eine
    MinGW-Laufzeit neben der Bibliothek) werden ohne angemeldetes
    Suchverzeichnis nicht gefunden.  Unter Linux/macOS ein No-op — dort trägt
    der Loader den Pfad der geladenen Bibliothek selbst nach.
    """
    if not _is_windows():
        return
    seen = set()
    for path in library_candidates():
        d = path.parent
        if d in seen or not d.is_dir():
            continue
        seen.add(d)
        try:
            os.add_dll_directory(str(d))  # type: ignore[attr-defined]
        except (OSError, AttributeError):
            pass  # nicht anmeldbar → der Standardsuchpfad muss reichen


# ─── Formatkatalog ───────────────────────────────────────────────────────────

def formats_candidates() -> List[Path]:
    """Kandidaten für ``formats.yaml`` in absteigender Priorität.

    Spiegelt bewusst die Suche des Kerns (``FormatCatalog::searchPaths``), denn
    der Kern lädt den Katalog selbst — hier wird er nur für Diagnose und für
    den Launcher aufgelöst, der ``K1520_FORMATS`` setzen kann.
    """
    out: List[Path] = []
    env = os.environ.get(ENV_FORMATS)
    if env:
        sep = ";" if _is_windows() else ":"
        for item in env.split(sep):
            if not item:
                continue
            p = Path(item).expanduser()
            out.append(p / FORMATS_FILE if p.is_dir() else p)

    base = base_dir()
    out.append(base / "share" / "k1520emu" / FORMATS_FILE)   # Installation
    out.append(base / "data" / FORMATS_FILE)                 # Quellbaum
    out.append(config_dir() / FORMATS_FILE)                  # Benutzerkonfiguration
    return out


def formats_file() -> Optional[Path]:
    """Erster existierender Formatkatalog — oder ``None``."""
    for p in formats_candidates():
        if p.is_file():
            return p.resolve()
    return None


# ─── Benutzerverzeichnisse ───────────────────────────────────────────────────

def config_dir() -> Path:
    """Verzeichnis der automatisch gesicherten Konfiguration.

    * Linux/BSD: ``${XDG_CONFIG_HOME:-~/.config}/k1520emu``
    * Windows:   ``%APPDATA%\\K1520emu``
    * macOS:     ``~/Library/Application Support/K1520emu``

    Unter Windows/macOS wird ein **bereits vorhandenes** ``~/.config/k1520emu``
    weiterbenutzt: dort haben frühere Fassungen ihre Konfiguration abgelegt,
    und ein stiller Wechsel würde sie unsichtbar machen.
    """
    xdg = os.environ.get("XDG_CONFIG_HOME")
    if xdg:
        return Path(xdg) / CONFIG_DIRNAME

    legacy = Path.home() / ".config" / CONFIG_DIRNAME
    if _is_windows():
        appdata = os.environ.get("APPDATA")
        if appdata and not legacy.is_dir():
            return Path(appdata) / APP_DIRNAME
        return legacy
    if _is_macos():
        native = Path.home() / "Library" / "Application Support" / APP_DIRNAME
        return legacy if legacy.is_dir() else native
    return legacy


def _windows_documents() -> Optional[Path]:
    """Der Dokumentenordner laut Registrierung — oder ``None``.

    Gelesen wird ``HKCU\\…\\Explorer\\User Shell Folders`` → ``Personal``: die
    Stelle, die der Explorer und die Known-Folder-API als Wahrheit benutzen und
    die **OneDrive beim Umleiten umschreibt**.  Ohne sie zeigte die Auflösung auf
    ``%USERPROFILE%\\Documents``, während die Dateien des Anwenders längst unter
    ``%USERPROFILE%\\OneDrive\\Dokumente`` liegen.

    ``User Shell Folders`` (nicht ``Shell Folders``) ist die richtige Hälfte: dort
    steht der Wert unaufgelöst, also z. B. ``%USERPROFILE%\\Documents`` — deshalb
    das ``expandvars``.  Die andere Hälfte pflegt Windows nur als Zwischenspeicher
    und schreibt sie nicht immer nach.

    Eigene Funktion, damit die Registrierung im Test austauschbar ist — sie lässt
    sich nicht sinnvoll in ein Temp-Verzeichnis stellen.
    """
    if not _is_windows():
        return None
    try:
        import winreg
        schluessel = r"Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders"
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, schluessel) as k:
            wert, _typ = winreg.QueryValueEx(k, "Personal")
    except (ImportError, OSError):
        return None
    if not wert:
        return None
    pfad = Path(os.path.expandvars(wert))
    return pfad if pfad.is_dir() else None


def documents_dir() -> Optional[Path]:
    """Der **Dokumentenordner** des Anwenders — oder ``None``, wenn es keinen gibt.

    Sein Name ist sprachabhängig (``~/Dokumente``, ``~/Documents``,
    ``~/Documenti``), verbindlich ist deshalb ``XDG_DOCUMENTS_DIR``.  Gelesen
    wird ``~/.config/user-dirs.dirs`` — die Datei, die die Desktop-Umgebung
    selbst pflegt — **direkt** und nicht über das Programm ``xdg-user-dir``:
    das fehlt auf schlanken Systemen, und eine Zeile zu lesen ist billiger als
    ein Prozessstart.

    Unter **Windows** ist der Ordner häufig umgeleitet — OneDrive verschiebt ihn
    nach ``%USERPROFILE%\\OneDrive\\Dokumente``, Firmen auf ein Netzlaufwerk.
    ``~/Documents`` ist dann schlicht falsch (und existiert oft nicht einmal).
    Maßgeblich ist die Registrierung, dieselbe Quelle, die der Explorer und die
    Known-Folder-API lesen — siehe :func:`_windows_documents`.

    macOS hat den Ort ohne Umleitung.
    """
    if _is_windows():
        aus_registrierung = _windows_documents()
        if aus_registrierung is not None:
            return aus_registrierung
        cand = Path.home() / "Documents"
        return cand if cand.is_dir() else None

    if _is_macos():
        cand = Path.home() / "Documents"
        return cand if cand.is_dir() else None

    env = os.environ.get("XDG_DOCUMENTS_DIR")
    if env:
        return Path(os.path.expandvars(env)).expanduser()

    xdg_cfg = os.environ.get("XDG_CONFIG_HOME")
    user_dirs = (Path(xdg_cfg) if xdg_cfg else Path.home() / ".config") / "user-dirs.dirs"
    if user_dirs.is_file():
        try:
            for zeile in user_dirs.read_text(encoding="utf-8").splitlines():
                zeile = zeile.strip()
                if not zeile.startswith("XDG_DOCUMENTS_DIR="):
                    continue
                wert = zeile.split("=", 1)[1].strip().strip('"').strip("'")
                # In der Datei steht „$HOME/Dokumente" — die Variable ist Teil
                # des Formats, nicht der Shell, die sie sonst aufgelöst hätte.
                wert = wert.replace("$HOME", str(Path.home()))
                if wert:
                    return Path(wert).expanduser()
        except OSError:
            pass

    for name in ("Documents", "Dokumente"):
        cand = Path.home() / name
        if cand.is_dir():
            return cand
    return None


def user_data_dir() -> Path:
    """Verzeichnis der Benutzerdaten (Arbeitsdisketten, Zustände).

    Liegt im **Dokumentenordner** (``~/Dokumente/K1520emu``): dort sucht der
    Anwender seine Dateien, dorthin greift seine Datensicherung, und Windows
    und macOS haben denselben Ort.  Ein Pfad unter ``~/.local/share`` wäre
    idiomatischer, aber der Dateimanager zeigt versteckte Ordner nicht — und
    Arbeitsdisketten sind Anwenderdaten, keine Anwendungsinterna.

    Gibt es keinen Dokumentenordner (schlankes System, dienstähnlicher
    Betrieb), bleibt der plattformübliche Datenpfad als Reserve.

    ``K1520_DATA`` schlägt beides.  Der Installer fragt den Ordner ab und trägt
    die Antwort in den Starter ein — **aber nur, wenn sie von der Vorgabe
    abweicht**: sonst bliebe ein fester Pfad stehen, und die Auflösung oben
    könnte einem umbenannten Dokumentenordner nicht mehr folgen.  Der häufigste
    Grund für eine abweichende Wahl ist ein nach OneDrive umgeleitetes
    ``Documents`` — der Autosave schreibt bei jedem Diskettenzugriff zurück, und
    das in einen synchronisierten Ordner zu legen, ist eine Entscheidung des
    Anwenders, keine des Entwurfs.
    """
    env = os.environ.get(ENV_DATA)
    if env:
        return Path(env).expanduser()

    docs = documents_dir()
    if docs is not None:
        return docs / DATA_DIRNAME

    if _is_windows():
        appdata = os.environ.get("APPDATA")
        if appdata:
            return Path(appdata) / APP_DIRNAME
        return Path.home() / APP_DIRNAME
    if _is_macos():
        return Path.home() / "Library" / "Application Support" / APP_DIRNAME
    xdg = os.environ.get("XDG_DATA_HOME")
    base = Path(xdg) if xdg else Path.home() / ".local" / "share"
    return base / DATA_DIRNAME


def user_disks_dir() -> Path:
    """Verzeichnis der **Arbeitsdisketten** des Anwenders.

    Muss schreibbar sein: der Emulator schreibt Änderungen an einer gemounteten
    Diskette verzögert in die Datei zurück.  Deshalb liegt es außerhalb der
    Installation — sonst würden mitgelieferte Beispiele verändert und beim
    nächsten Update überschrieben.
    """
    env = os.environ.get(ENV_DISKS)
    if env:
        return Path(env).expanduser()
    return user_data_dir() / DISKS_DIRNAME


def user_files_dir() -> Path:
    """Verzeichnis der **Dateien**, die das DiskTool von Disketten holt.

    Das Gegenstück zu :func:`user_disks_dir` für die Ordnerseite des
    Diskettenwerkzeugs.  Es liegt aus demselben Grund außerhalb der Installation:
    dort wird geschrieben (extrahierte Dateien, Beiblätter), und ein Update darf
    das nicht überbügeln.

    ``K1520_DISKS`` wirkt hier bewusst **nicht** — das ist die Angabe für die
    Abbilder.  Wer beides verschieben will, setzt ``K1520_DATA``.
    """
    return user_data_dir() / FILES_DIRNAME


def default_folder_dir() -> Path:
    """Startverzeichnis für die **Ordnerseite** des DiskTool und ihre Dialoge.

    Anders als :func:`default_disk_dir` darf das Ergebnis noch nicht existieren:
    der Ordner entsteht beim ersten Extrahieren.  Gibt es weder ihn noch den
    Datenordner, wird der Dokumentenordner genommen und erst zuletzt das
    Heimatverzeichnis — **nie** die Installation, denn dorthin gehören keine
    Anwenderdateien.
    """
    files = user_files_dir()
    if files.is_dir():
        return files
    eltern = files.parent
    if eltern.is_dir():
        return eltern
    docs = documents_dir()
    return docs if docs is not None else Path.home()


def ensure_user_files_dir() -> Optional[Path]:
    """Den Dateiordner beim Erststart einer **Installation** anlegen.

    Das Gegenstück zu :func:`seed_user_disks` für die Ordnerseite des DiskTool:
    dort werden Beispieldisketten ausgepackt, hier entsteht der leere Ordner, in
    den das Werkzeug seine Dateien holt.  Damit hat der Anwender nach der
    Installation beides sichtbar nebeneinander im Dokumentenordner.

    Im **Quellbaum** geschieht nichts — dort soll kein Ordner im Heimatverzeichnis
    entstehen, bloß weil jemand das Werkzeug einmal gestartet hat.

    Returns:
        Das Verzeichnis, wenn es (nun) existiert, sonst ``None``.
    """
    if not is_installed_layout():
        ziel = user_files_dir()
        return ziel if ziel.is_dir() else None
    ziel = user_files_dir()
    try:
        ziel.mkdir(parents=True, exist_ok=True)
    except OSError:
        # Ein nicht anlegbarer Ordner ist kein Grund, das Programm nicht zu
        # starten — die Dialoge weichen dann auf den Dokumentenordner aus.
        return None
    return ziel


def bundled_disks_dir() -> Optional[Path]:
    """Verzeichnis der **mitgelieferten** Beispieldisketten (schreibgeschützt gedacht)."""
    base = base_dir()
    for cand in (base / "share" / "disks", base / "disks"):
        if cand.is_dir():
            return cand
    return None


def default_disk_dir() -> Path:
    """Startverzeichnis für Datei-Dialoge.

    Bevorzugt die Arbeitsdisketten des Anwenders, sobald es sie gibt; sonst die
    mitgelieferten Beispiele, sonst die Wurzel.
    """
    user = user_disks_dir()
    if user.is_dir() and any(user.iterdir()):
        return user
    bundled = bundled_disks_dir()
    return bundled if bundled else base_dir()


def seed_user_disks(patterns=("*.hfe", "*.dmk", "*.img")) -> int:
    """Kopiert die mitgelieferten Beispieldisketten **einmalig** ins Benutzerverzeichnis.

    Gedacht für den Erststart nach einer Installation (``main.py`` ruft es auf,
    der Installer ebenfalls).  Ist das Zielverzeichnis schon vorhanden, geschieht
    nichts — der Anwender hat es dann bereits in der Hand.  Im Quellbaum
    ebenfalls ein No-op: dort wird direkt aus ``disks/`` gearbeitet.

    Im Paket liegen die Abbilder **gepackt** (``*.hfe.gz``): sie bestehen
    überwiegend aus Füllmuster, und gebraucht werden sie genau einmal — hier.
    Ungepackt lägen sie danach doppelt auf der Platte.  Beides wird angenommen,
    damit ältere Pakete und der Quellbaum weiter funktionieren.

    Returns:
        Anzahl angelegter Dateien.
    """
    import gzip
    import shutil

    if not is_installed_layout():
        return 0
    target = user_disks_dir()
    if target.exists():
        return 0
    source = bundled_disks_dir()
    if source is None:
        return 0

    target.mkdir(parents=True, exist_ok=True)
    count = 0
    for pattern in patterns:
        for f in sorted(source.glob(pattern)):
            shutil.copy2(f, target / f.name)
            count += 1
        for f in sorted(source.glob(pattern + ".gz")):
            with gzip.open(f, "rb") as gepackt, open(target / f.stem, "wb") as offen:
                shutil.copyfileobj(gepackt, offen)
            count += 1
    return count


# ─── Diagnose ────────────────────────────────────────────────────────────────

def describe() -> str:
    """Mehrzeilige Übersicht aller aufgelösten Pfade — für ``--paths`` und Fehlerberichte."""
    try:
        lib = str(core_library())
    except FileNotFoundError:
        lib = "NICHT GEFUNDEN"
    fmt = formats_file()
    bundled = bundled_disks_dir()
    layout = "Installation" if is_installed_layout() else "Quellbaum"
    return "\n".join([
        f"Layout:            {layout}",
        f"Wurzel:            {base_dir()}",
        f"Kernbibliothek:    {lib}",
        f"Formatkatalog:     {fmt if fmt else 'NICHT GEFUNDEN (der Kern sucht weiter selbst)'}",
        f"Disketten (Paket): {bundled if bundled else '—'}",
        f"Disketten (Nutzer):{user_disks_dir()}",
        f"Dateien (DiskTool):{user_files_dir()}",
        f"Konfiguration:     {config_dir()}",
    ])

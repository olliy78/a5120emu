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
2. **Installationslayout** ``<root>/{bin,app,share/a5120emu,share/disks}``
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
ENV_DISKS = "K1520_DISKS"      # Verzeichnis der Arbeitsdisketten

#: Name des Formatkatalogs (identisch mit ``kCatalogFileName`` im Kern).
FORMATS_FILE = "formats.yaml"

#: Verzeichnisname der Benutzerkonfiguration — historisch ``k1520emu``,
#: NICHT ``a5120emu``; eine Umbenennung würde bestehende Konfigurationen
#: unsichtbar machen.
CONFIG_DIRNAME = "k1520emu"

#: Verzeichnisname der Benutzerdaten (Arbeitsdisketten).
DATA_DIRNAME = "a5120emu"

#: Verzeichnisname unter Windows/macOS, wo Programmnamen großgeschrieben sind.
APP_DIRNAME = "A5120Emu"


# ─── Plattform ───────────────────────────────────────────────────────────────

def _is_windows() -> bool:
    return sys.platform.startswith("win")


def _is_macos() -> bool:
    return sys.platform == "darwin"


def library_filenames() -> List[str]:
    """Mögliche Dateinamen der Kernbibliothek auf dieser Plattform.

    Der zweite Name deckt eine später vergebene SOVERSION ab (heute baut CMake
    ohne, siehe ``CMakeLists.txt``).
    """
    if _is_windows():
        return ["k1520core.dll", "libk1520core.dll"]
    if _is_macos():
        return ["libk1520core.dylib", "libk1520core.1.dylib"]
    return ["libk1520core.so", "libk1520core.so.1"]


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

def library_candidates() -> List[Path]:
    """Alle Kandidaten für die Kernbibliothek in absteigender Priorität.

    Wird auch für die Fehlermeldung gebraucht („gesucht in: …"), enthält
    deshalb bewusst auch nicht existierende Pfade.
    """
    names = library_filenames()
    out: List[Path] = []

    # 1) explizite Vorgabe — Datei oder Verzeichnis
    env = os.environ.get(ENV_LIB)
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


def core_library() -> Path:
    """Pfad der Kernbibliothek.

    Raises:
        FileNotFoundError: mit der vollständigen Kandidatenliste und einem
            Hinweis, wie sie zu bauen ist.
    """
    candidates = library_candidates()
    for path in candidates:
        if path.exists():
            return path.resolve()

    listing = "\n".join(f"  {p}" for p in candidates)
    raise FileNotFoundError(
        f"Kernbibliothek ({' / '.join(library_filenames())}) nicht gefunden.\n"
        f"Gesucht in:\n{listing}\n\n"
        f"Bauen:  tools/dev.sh build\n"
        f"Oder Pfad vorgeben:  {ENV_LIB}=/pfad/zur/bibliothek"
    )


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
    out.append(base / "share" / "a5120emu" / FORMATS_FILE)   # Installation
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
    * Windows:   ``%APPDATA%\\A5120Emu``
    * macOS:     ``~/Library/Application Support/A5120Emu``

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


def user_data_dir() -> Path:
    """Verzeichnis der Benutzerdaten (Arbeitsdisketten, Zustände).

    * Linux/BSD: ``${XDG_DATA_HOME:-~/.local/share}/a5120emu``
    * Windows:   ``%APPDATA%\\A5120Emu``
    * macOS:     ``~/Library/Application Support/A5120Emu``
    """
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
    return user_data_dir() / "disks"


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

    Gedacht für den Erststart nach einer Installation (der Launcher ruft es
    auf).  Ist das Zielverzeichnis schon vorhanden, geschieht nichts — der
    Anwender hat es dann bereits in der Hand.  Im Quellbaum ebenfalls ein
    No-op: dort wird direkt aus ``disks/`` gearbeitet.

    Returns:
        Anzahl kopierter Dateien.
    """
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
        f"Konfiguration:     {config_dir()}",
    ])

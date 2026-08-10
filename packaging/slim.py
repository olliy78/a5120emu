#!/usr/bin/env python3
"""
Schlankmachen einer Installation — entfernt, was der Emulator nie lädt.

Aufruf (mit dem Python DER INSTALLATION, `install.sh` macht das):

    <root>/venv/bin/python3 slim.py <root> [--dry-run] [--keep-tools]

Eine frische Installation belegt ~400 MB, davon entfallen keine 15 MB auf den
Emulator selbst.  Der Rest sind Python und Qt — und beide bringen sehr viel mit,
was eine Widgets-Anwendung nicht braucht: den QML/Quick-Stapel, die
Qt-Entwicklungswerkzeuge (Designer, Linguist, qmlls …), die Bindungen für
Module, die nie importiert werden, Typstubs und shiboken-Baumaterial, die
Testsuite und Tcl/Tk von CPython.  Danach sind es ~146 MB.

Der Schnitt ist bewusst **beweisbar** statt geraten: welche Qt-Bibliotheken
bleiben, entscheidet nicht eine Liste, sondern `ldd` — ausgehend von den
Bindungen, die die GUI importiert, und den Plugins, die Qt zur Laufzeit
nachlädt.  Alles, was von dort aus nicht erreichbar ist, fliegt raus.  Direkt
danach fährt `install.sh` seinen Rauchtest (Fenster bauen, Emulator starten);
schlägt der fehl, war der Schnitt zu tief und die Installation bricht ab, statt
den Anwender damit allein zu lassen.

Entwurf und die drei Regeln dahinter: doc/design/13_distribution.md §8.1.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

# ─── Was die GUI wirklich benutzt ────────────────────────────────────────────

#: PySide6-Bindungen, die `app/` importiert (siehe `rg "PySide6\.[A-Za-z]+" app/`).
BINDINGS_KEEP = {
    "QtCore", "QtGui", "QtWidgets", "QtOpenGL", "QtOpenGLWidgets",
    # QtDBus/QtNetwork werden nicht importiert, aber von Plugins nachgeladen —
    # darüber entscheidet die ldd-Hülle, nicht diese Liste.
}

#: Qt-Plugin-Verzeichnisse, die zur Laufzeit gebraucht werden.  Das Plugin für
#: die Fensterumgebung (`platforms/`) ist überlebenswichtig, `imageformats/`
#: und `iconengines/` liefern Symbole und Bilder, `platformthemes/` die
#: Dateidialoge des Desktops.
PLUGINS_KEEP = {
    "platforms", "platforminputcontexts", "platformthemes", "xcbglintegrations",
    "egldeviceintegrations", "wayland-decoration-client", "wayland-graphics-integration-client",
    "wayland-shell-integration", "imageformats", "iconengines", "generic", "styles", "tls",
}

#: Einzelne Plugins, die trotz passender Gruppe rausfliegen.  Die
#: Bildschirmtastatur ist für einen Emulator mit eigener Tastaturmatrix ohne
#: Sinn — und sie ist der EINZIGE Grund, warum sonst der ganze QML/Quick-Stapel
#: (~16 MB) über die ldd-Hülle stehenbliebe.
PLUGIN_FILES_DROP = (
    "platforminputcontexts/libqtvirtualkeyboardplugin.so",
)

#: Qt-Übersetzungen: nur diese Sprachen bleiben (die Standarddialoge sollen
#: deutsch bleiben, alles andere sind ~13 MB für nichts).
TRANSLATIONS_KEEP_PREFIX = ("qt_de", "qtbase_de", "qt_en", "qtbase_en")

#: Verzeichnisse in `PySide6/Qt/`, die nur Entwicklung/QML betreffen.
QT_DIRS_DROP = ("qml", "metatypes", "libexec", "mkspecs", "include", "doc", "resources")

#: Verzeichnisse in `PySide6/` selbst, die nur beim ERZEUGEN von Bindungen
#: gebraucht werden (shiboken) — `support/` und `lib/` bleiben, die werden
#: importiert bzw. gelinkt.
PYSIDE_DIRS_DROP = ("include", "typesystems", "glue", "doc", "scripts")

#: Qt-Werkzeuge, die als ausführbare Dateien im Wheel liegen.
TOOLS_DROP = (
    "assistant", "designer", "linguist", "lupdate", "lrelease", "lconvert",
    "qmlformat", "qmlls", "qmllint", "qmlimportscanner", "qmlcachegen",
    "qmltyperegistrar", "qmlprofiler", "qmlscene", "qmltestrunner", "qml",
    "qsb", "balsam", "balsamui", "svgtoqml", "shader_baker", "materialeditor",
    "pyside6-*", "uic", "rcc", "qtpaths", "androiddeployqt", "deploy_lib",
)

#: CPython: Verzeichnisse, die eine reine Laufzeit nicht braucht.
CPYTHON_DIRS_DROP = (
    "include", "share",
    "lib/pkgconfig",
    "lib/python*/test", "lib/python*/idlelib", "lib/python*/tkinter",
    "lib/python*/turtledemo", "lib/python*/ensurepip", "lib/python*/pydoc_data",
    "lib/python*/lib2to3", "lib/python*/distutils",
    # config-3.x-…: Makefile, python.o und libpython.a — nur zum ÜBERSETZEN
    # eigener Erweiterungen da.
    "lib/python*/config-*",
    # pip im Basis-Python: das venv ist eigenständig und wird von uv bestückt.
    "lib/python*/site-packages/pip", "lib/python*/site-packages/pip-*.dist-info",
)

#: CPython: Dateien/Glob-Muster (Tcl/Tk und statische Bibliotheken).
CPYTHON_GLOBS_DROP = (
    "lib/libtcl*", "lib/libtk*", "lib/tcl*", "lib/tk*", "lib/itcl*", "lib/thread*",
    "lib/*.a", "lib/python*/lib-dynload/_tkinter*.so",
)


# ─── Hilfsmittel ─────────────────────────────────────────────────────────────

class Cutter:
    """Sammelt, was entfernt wird, und meldet am Ende die Bilanz."""

    def __init__(self, dry_run: bool):
        self.dry_run = dry_run
        self.freed = 0
        self.removed = 0

    @staticmethod
    def _size(path: Path) -> int:
        if path.is_symlink() or path.is_file():
            try:
                return path.lstat().st_size
            except OSError:
                return 0
        total = 0
        for root, _, files in os.walk(path):
            for f in files:
                try:
                    total += (Path(root) / f).lstat().st_size
                except OSError:
                    pass
        return total

    def drop(self, path: Path) -> None:
        if not path.exists() and not path.is_symlink():
            return
        self.freed += self._size(path)
        self.removed += 1
        if self.dry_run:
            return
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path, ignore_errors=True)
        else:
            try:
                path.unlink()
            except OSError:
                pass


def strip_datei(datei: Path, cut: Cutter) -> None:
    """Symboltabelle aus @p datei entfernen — über eine Kopie, die eingewechselt wird.

    Gearbeitet wird auf einer Kopie, und zwar aus zwei Gründen.  Erstens läuft
    slim.py mit dem Python DER Installation; an manchen Dateien säße `strip`
    damit an etwas Laufendem („Text file busy").  Zweitens — und wichtiger —
    lässt sich ein misslungener Schnitt so folgenlos verwerfen: **eine Warnung
    von `strip` genügt, um die Kopie wegzuwerfen**.

    Der Grund für diese Strenge steht im Interpreter von
    python-build-standalone: `strip` meldet dort „allocated section `.dynstr'
    not in segment", schreibt trotzdem eine Datei — und die startet nicht mehr
    („undefined symbol: , version").  Das gilt für *jede* Schaltervariante,
    `--strip-debug` spart dort nicht einmal Platz.  Deshalb werden hier auch nur
    gemeinsame Bibliotheken angefasst, keine Programme.
    """
    if datei.is_symlink() or not datei.is_file():
        return
    kopie = datei.with_name(datei.name + ".slim-tmp")
    try:
        vorher = datei.stat().st_size
        shutil.copy2(datei, kopie)
        r = subprocess.run(["strip", "--strip-unneeded", str(kopie)],
                           capture_output=True, timeout=120)
        nachher = kopie.stat().st_size
        if r.returncode == 0 and not r.stderr.strip() and nachher < vorher:
            os.replace(kopie, datei)
            cut.freed += vorher - nachher
        else:
            kopie.unlink()
    except (OSError, subprocess.SubprocessError):
        if kopie.exists():
            try:
                kopie.unlink()
            except OSError:
                pass


def linked_libraries(binary: Path, inside: Path) -> set:
    """Alle Bibliotheken **unterhalb** von @p inside, die @p binary (transitiv) braucht.

    `ldd` löst rekursiv auf, ein Durchlauf je Wurzel genügt also.  Interessant
    sind nur Treffer innerhalb der Installation — die Systembibliotheken des
    Wirts gehen uns nichts an.
    """
    try:
        out = subprocess.run(["ldd", str(binary)], capture_output=True, text=True,
                             timeout=60)
    except (OSError, subprocess.SubprocessError):
        return set()
    treffer = set()
    for zeile in out.stdout.splitlines():
        if "=>" not in zeile:
            continue
        # „name => /pfad/zur/datei (0x00007f…)" — abgetrennt wird die
        # LADEADRESSE am Ende, nicht am ersten Leerzeichen: der
        # Installationspfad kommt vom Anwender und darf Leerzeichen enthalten
        # („~/Emulator Test").  Am ersten Leerzeichen geschnitten blieb die
        # Hülle leer, und der Sicherheitsrückfall unten ließ dann ganz Qt stehen.
        ziel = zeile.split("=>", 1)[1].strip()
        if ziel.endswith(")") and " (" in ziel:
            ziel = ziel.rsplit(" (", 1)[0].strip()
        if not ziel or ziel == "not found":
            continue
        p = Path(ziel)
        try:
            p = p.resolve()
        except OSError:
            continue
        if inside in p.parents:
            treffer.add(p)
    return treffer


# ─── Die eigentlichen Schnitte ───────────────────────────────────────────────

def slim_cpython(root: Path, cut: Cutter) -> None:
    """CPython auf eine Laufzeit eindampfen (Testsuite, Tcl/Tk, Header, IDLE)."""
    for base in sorted((root / "python").glob("*")):
        if not base.is_dir():
            continue
        for muster in CPYTHON_DIRS_DROP:
            for treffer in base.glob(muster):
                cut.drop(treffer)
        for muster in CPYTHON_GLOBS_DROP:
            for treffer in base.glob(muster):
                cut.drop(treffer)


def slim_symbole(root: Path, cut: Cutter) -> None:
    """Symboltabellen aus den **Bibliotheken** entfernen, wo noch welche drin sind.

    Qt kommt bereits gestrippt aus dem Wheel, C-Erweiterungen fremder Pakete
    nicht (PyYAML: 2,6 → 0,4 MB).  Deshalb wird nicht aufgezählt, sondern über
    alles gelaufen — künftige Abhängigkeiten sind damit gleich mit erfasst.
    Programme bleiben unangetastet, siehe `strip_datei`.
    """
    if not shutil.which("strip") or cut.dry_run:
        return
    kandidaten = []
    for base in sorted((root / "python").glob("*")):
        if base.is_dir() and not base.is_symlink():
            kandidaten += list(base.glob("lib/python*/lib-dynload/*.so"))
    for s in (root / "venv" / "lib").glob("python*/site-packages"):
        kandidaten += list(s.rglob("*.so")) + list(s.rglob("*.so.*"))
    for datei in kandidaten:
        strip_datei(datei, cut)


def slim_libpython(root: Path, cut: Cutter) -> None:
    """`libpython3.x.so` entfernen — **falls** sie wirklich niemand braucht.

    python-build-standalone liefert den Interpreter *statisch gelinkt* aus und
    legt die gemeinsame Bibliothek zusätzlich bei, für Programme, die Python
    einbetten. Wir betten nichts ein: weder `bin/python3.x` noch die
    Erweiterungsmodule (shiboken/PySide6, `lib-dynload`) verweisen darauf — dann
    sind das 30 MB für nichts.

    Geprüft wird das, statt es anzunehmen: erst wenn kein einziges geladenes
    Objekt die Bibliothek anzieht, fliegt sie raus.  Zieht eine künftige
    Abhängigkeit sie doch, bleibt sie liegen.
    """
    for base in sorted((root / "python").glob("*")):
        if not base.is_dir() or base.is_symlink():
            continue
        kandidaten = [p for p in (base / "lib").glob("libpython*.so*")
                      if p.is_file() and not p.is_symlink()]
        if not kandidaten:
            continue
        lib_dir = (base / "lib").resolve()

        prüflinge = list(base.glob("bin/python3*"))
        prüflinge += list(base.glob("lib/python*/lib-dynload/*.so"))
        prüflinge += list((root / "venv" / "lib").glob("python*/site-packages/**/*.so"))
        prüflinge += list((root / "venv" / "lib").glob("python*/site-packages/**/*.so.*"))

        gebraucht = set()
        for p in prüflinge:
            if p.is_file():
                gebraucht |= {q for q in linked_libraries(p, lib_dir)
                              if q.name.startswith("libpython")}

        for kandidat in kandidaten:
            if kandidat.resolve() not in gebraucht:
                cut.drop(kandidat)
                for link in (base / "lib").glob("libpython*.so"):
                    if link.is_symlink() and not link.resolve().exists():
                        cut.drop(link)


def _pyside_dir(root: Path):
    treffer = list((root / "venv" / "lib").glob("python*/site-packages/PySide6"))
    return treffer[0] if treffer else None


def slim_pyside(root: Path, cut: Cutter) -> None:
    """Qt auf das eindampfen, was die GUI (Widgets + OpenGL) tatsächlich lädt."""
    pyside = _pyside_dir(root)
    if pyside is None:
        return
    qt = pyside / "Qt"

    # 1. Bindungen, die nie importiert werden (je 1–9 MB).
    for datei in pyside.glob("*.abi3.so"):
        if datei.name.split(".")[0] not in BINDINGS_KEEP:
            cut.drop(datei)

    # 1a. Typstubs (~3 MB) und das Baumaterial für EIGENE Bindungen.  Beides
    #     liest zur Laufzeit niemand: `.pyi` wendet sich an Editor und
    #     Typprüfer, `include/`+`typesystems/`+`glue/` an shiboken.
    for stub in pyside.glob("*.pyi"):
        cut.drop(stub)
    cut.drop(pyside / "py.typed")
    for name in PYSIDE_DIRS_DROP:
        cut.drop(pyside / name)

    # 2. Qt-Werkzeuge (Designer, Linguist, qmlls …) — reine Entwicklungshilfen.
    for muster in TOOLS_DROP:
        for treffer in pyside.glob(muster):
            if treffer.is_file() or treffer.is_symlink():
                cut.drop(treffer)

    # 3. Ganze Zweige, die nur QML/Entwicklung betreffen.
    for name in QT_DIRS_DROP:
        cut.drop(qt / name)

    # 4. Übersetzungen bis auf Deutsch/Englisch.
    trans = qt / "translations"
    if trans.is_dir():
        for datei in trans.iterdir():
            if not datei.name.startswith(TRANSLATIONS_KEEP_PREFIX):
                cut.drop(datei)

    # 5. Plugins: nur die Gruppen, die zur Laufzeit gebraucht werden.
    plugins = qt / "plugins"
    if plugins.is_dir():
        for gruppe in plugins.iterdir():
            if gruppe.is_dir() and gruppe.name not in PLUGINS_KEEP:
                cut.drop(gruppe)
        for rel in PLUGIN_FILES_DROP:
            cut.drop(plugins / rel)

    # 6. Qt-Bibliotheken: alles, was von den verbliebenen Bindungen und Plugins
    #    aus nicht erreichbar ist.  DAS ist der große Posten (~130 MB), und er
    #    wird nicht geraten, sondern mit ldd bestimmt.
    lib = qt / "lib"
    if not lib.is_dir():
        return
    lib_resolved = lib.resolve()

    wurzeln = [p for p in pyside.glob("*.abi3.so")]
    wurzeln += list(pyside.glob("../shiboken6/*.so*"))
    if plugins.is_dir():
        wurzeln += [p for p in plugins.rglob("*.so")]

    gebraucht = set()
    for w in wurzeln:
        gebraucht |= linked_libraries(w, lib_resolved)

    if not gebraucht:
        # Kein ldd oder nichts aufgelöst — dann lieber nichts anfassen, als die
        # Installation zu zerlegen.
        print("     (ldd lieferte nichts — Qt-Bibliotheken bleiben unangetastet)")
        return

    for datei in lib.iterdir():
        if datei.is_dir():
            continue
        try:
            ziel = datei.resolve()
        except OSError:
            continue
        if ziel not in gebraucht:
            cut.drop(datei)


def slim_tools(root: Path, cut: Cutter) -> None:
    """`uv` entfernen — es wird nur beim Installieren und Aktualisieren gebraucht.

    Beides läuft aus dem Paket heraus, das dabei sein eigenes `uv` mitbringt.
    """
    cut.drop(root / "tools")
    cut.drop(root / ".cache-uv")


# ─── Einstieg ────────────────────────────────────────────────────────────────

def main(argv) -> int:
    args = [a for a in argv[1:] if not a.startswith("-")]
    dry = "--dry-run" in argv[1:]
    keep_tools = "--keep-tools" in argv[1:]
    if len(args) != 1:
        print(__doc__)
        return 2
    root = Path(args[0]).resolve()
    if not (root / "venv").is_dir():
        print(f"Fehler: {root} sieht nicht nach einer Installation aus", file=sys.stderr)
        return 1

    cut = Cutter(dry)
    slim_cpython(root, cut)
    slim_pyside(root, cut)
    slim_libpython(root, cut)
    # Zuletzt strippen — was vorher schon weg ist, muss nicht erst gestrippt werden.
    slim_symbole(root, cut)
    if not keep_tools:
        slim_tools(root, cut)

    print(f"     {cut.removed} Einträge entfernt, "
          f"{cut.freed / (1024 * 1024):.0f} MB frei{' (Probelauf)' if dry else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

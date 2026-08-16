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

Unter **Windows** gilt dasselbe mit drei Unterschieden, die gemessen und nicht
geraten sind (Erkundungsstufe im Paketjob, 2026-08-12): die Qt-DLLs liegen
direkt in `PySide6/` statt in `PySide6/Qt/lib`, die Bindungen heißen `.pyd`,
und CPython liegt als `Lib/`+`DLLs/`+`tcl/` statt `lib/pythonX.Y/`.  Die
Abhängigkeiten liest dort kein `ldd`, sondern :func:`pe_imports` — `slim.py`
läuft beim ANWENDER, und der hat kein Visual Studio und damit kein `dumpbin`.

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
    # Windows: Qt fragt darüber, ob eine Netzverbindung besteht.  Winzig, und
    # ohne die Gruppe meldet Qt beim Start eine Warnung.
    "networkinformation",
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

IST_WINDOWS = sys.platform.startswith("win")

#: CPython unter Windows liegt anders: `Lib/` statt `lib/pythonX.Y/`, die
#: Erweiterungsmodule in `DLLs/`, Tcl/Tk als eigener Zweig `tcl/`.  Die
#: Muster oben greifen dort nicht (auch nicht case-insensitiv — `glob` vergleicht
#: das Muster, nicht das Dateisystem).  Gemessen 2026-08-12: Lib 27,7 MB,
#: DLLs 17,1 MB, tcl 6,7 MB, include 1,2 MB, libs 0,7 MB.
CPYTHON_WIN_DIRS_DROP = (
    "include", "libs", "tcl", "Scripts/__pycache__",
    "Lib/test", "Lib/idlelib", "Lib/tkinter", "Lib/turtledemo",
    "Lib/ensurepip", "Lib/pydoc_data", "Lib/lib2to3", "Lib/distutils",
    "Lib/site-packages/pip", "Lib/site-packages/pip-*.dist-info",
)

#: Windows: Tcl/Tk-Laufzeit, das zugehoerige Erweiterungsmodul — und OpenSSL.
#:
#: Zu OpenSSL (`libcrypto-3` allein sind 8 MB): der Emulator und das DiskTool
#: reden mit NICHTS im Netz; sie oeffnen Dateien.  `import ssl` kommt in
#: `app/` nicht vor, und die Stellen der Standardbibliothek, die es versuchen
#: (`http.client`, `urllib.request`), fangen den Fehlschlag selbst ab.
#: `hashlib` ebenso — ohne `_hashlib` rechnet es mit den eingebauten
#: Implementierungen weiter.  Qts eigener TLS-Baustein sucht seinerseits ein
#: OpenSSL des SYSTEMS und hat mit diesem hier nie gearbeitet.
#:
#: Gebraucht wird es genau einmal, naemlich von pip beim Einrichten — und das
#: ist vorbei, wenn `slim.py` laeuft.  Ein Update packt Python ohnehin frisch
#: aus, bevor pip wieder laeuft.  (Unter Linux bleibt OpenSSL vorerst stehen:
#: dort ist der Platzbedarf nicht das Problem, und ungemessen schneidet hier
#: niemand.)
CPYTHON_WIN_GLOBS_DROP = (
    "DLLs/_tkinter*.pyd", "DLLs/tcl*.dll", "DLLs/tk*.dll", "DLLs/_test*.pyd",
    "DLLs/_ssl.pyd", "DLLs/_hashlib.pyd", "DLLs/libcrypto-3*.dll", "DLLs/libssl-3*.dll",
)

#: Windows: die Qt-Bibliotheken liegen NICHT in `Qt/lib`, sondern direkt neben
#: den Bindungen in `PySide6/`.  Der Kehraus dort darf deshalb ausschliesslich
#: `Qt6*.dll` anfassen — `QtCore.pyd` liegt im selben Verzeichnis, und ein
#: blindes „alles weg, was nicht erreichbar ist" loeschte die Bindung selbst.
QT_DLL_MUSTER_WIN = "Qt6*.dll"

#: Windows: bleibt trotz allem stehen.  `opengl32sw.dll` (19,7 MB) ist Qts
#: SOFTWARE-OpenGL (Mesa llvmpipe) und wird erst zur Laufzeit nachgeladen —
#: keine Importtabelle nennt sie, der Kehraus wuerde sie also verwerfen.  Sie ist
#: aber genau das, was die Oberflaeche auf Rechnern ohne brauchbare
#: GPU-Treiber rettet: virtuelle Maschinen, Remotedesktop, alte Hardware — die
#: typische Umgebung fuer einen Retro-Emulator.  Der Rauchtest laeuft offscreen
#: und wuerde den Ausfall NICHT bemerken; er zeigte sich erst beim Anwender als
#: schwarzes Fenster.  19,7 MB sind der Preis dafuer.
WIN_IMMER_BEHALTEN = ("opengl32sw.dll",)


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


def pe_imports(datei: Path) -> list:
    """Die DLL-Namen aus der Importtabelle einer PE-Datei — ohne fremde Werkzeuge.

    Warum von Hand statt `dumpbin /dependents`: **`slim.py` läuft beim
    ANWENDER**, und dort gibt es kein Visual Studio.  `ldd` ist auf jedem Linux
    da, `dumpbin` auf keinem normalen Windows.  Die Importtabelle zu lesen ist
    dafür überschaubar — Header, Datenverzeichnis 1, dann je Eintrag den Namen.

    Anders als `ldd` löst das **nicht rekursiv** auf; den Baum läuft
    :func:`linked_libraries` ab.

    Gibt eine Liste von Dateinamen zurück (ohne Pfad, Schreibweise wie in der
    Datei).  Bei allem, was nicht lesbar oder kein PE ist: leere Liste — der
    Aufrufer behandelt das wie „nichts gefunden" und fasst dann nichts an.
    """
    import struct
    try:
        roh = datei.read_bytes()
    except OSError:
        return []
    try:
        if roh[:2] != b"MZ":
            return []
        pe = struct.unpack_from("<I", roh, 0x3C)[0]
        if roh[pe:pe + 4] != b"PE\0\0":
            return []
        # COFF-Header: Sektionszahl und Größe des optionalen Headers.
        anzahl_sektionen = struct.unpack_from("<H", roh, pe + 6)[0]
        opt_groesse      = struct.unpack_from("<H", roh, pe + 20)[0]
        opt = pe + 24
        magic = struct.unpack_from("<H", roh, opt)[0]
        if magic == 0x20B:      # PE32+ (64 Bit)
            dd = opt + 112
        elif magic == 0x10B:    # PE32
            dd = opt + 96
        else:
            return []
        # Datenverzeichnis 1 = Importtabelle (RVA, Größe).
        import_rva = struct.unpack_from("<I", roh, dd + 8)[0]
        if not import_rva:
            return []

        # Sektionstabelle, um RVAs in Dateiversätze umzurechnen.
        sektionen = []
        st = opt + opt_groesse
        for i in range(anzahl_sektionen):
            e = st + i * 40
            v_groesse, v_adr, r_groesse, r_versatz = struct.unpack_from("<IIII", roh, e + 8)
            sektionen.append((v_adr, max(v_groesse, r_groesse), r_versatz))

        def versatz(rva):
            for v_adr, groesse, r_versatz in sektionen:
                if v_adr <= rva < v_adr + groesse:
                    return r_versatz + (rva - v_adr)
            return None

        namen = []
        eintrag = versatz(import_rva)
        if eintrag is None:
            return []
        while True:
            # IMAGE_IMPORT_DESCRIPTOR ist 20 Byte; der Name steht an Versatz 12.
            block = roh[eintrag:eintrag + 20]
            if len(block) < 20 or block == b"\0" * 20:
                break
            name_rva = struct.unpack_from("<I", block, 12)[0]
            if not name_rva:
                break
            n = versatz(name_rva)
            if n is not None:
                ende = roh.index(b"\0", n)
                namen.append(roh[n:ende].decode("ascii", "replace"))
            eintrag += 20
        return namen
    except (struct.error, IndexError, ValueError):
        return []


def _linked_windows(binary: Path, inside: Path) -> set:
    """Wie :func:`linked_libraries`, aber über die PE-Importtabelle.

    Der Unterschied zu `ldd` ist die REKURSION: `ldd` löst den ganzen Baum auf,
    die Importtabelle nennt nur die direkten Nachbarn.  Also selbst ablaufen —
    Breitensuche, jeder Name wird neben der jeweiligen Datei und in @p inside
    gesucht (so sucht auch Windows: zuerst im Verzeichnis des Moduls).

    Groß-/Kleinschreibung ist unter Windows egal, in einem Verzeichnisvergleich
    hier aber nicht — deshalb wird ein Kleinbuchstaben-Verzeichnis der
    vorhandenen Dateien angelegt statt je Name im Dateisystem zu raten.
    """
    vorrat = {}
    for verzeichnis in {binary.parent, inside}:
        if verzeichnis.is_dir():
            for d in verzeichnis.iterdir():
                if d.is_file():
                    vorrat.setdefault(d.name.lower(), d)

    gefunden, offen, gesehen = set(), [binary], {binary.resolve()}
    while offen:
        aktuell = offen.pop()
        for name in pe_imports(aktuell):
            ziel = vorrat.get(name.lower())
            if ziel is None:
                continue          # Systembibliothek — geht uns nichts an
            aufgeloest = ziel.resolve()
            if aufgeloest in gesehen:
                continue
            gesehen.add(aufgeloest)
            offen.append(ziel)
            if inside == aufgeloest.parent or inside in aufgeloest.parents:
                gefunden.add(aufgeloest)
    return gefunden


def linked_libraries(binary: Path, inside: Path) -> set:
    """Alle Bibliotheken **unterhalb** von @p inside, die @p binary (transitiv) braucht.

    Unter Linux/macOS über `ldd` (löst rekursiv auf, ein Durchlauf je Wurzel
    genügt), unter Windows über die PE-Importtabelle (siehe
    :func:`_linked_windows`).  Interessant sind nur Treffer innerhalb der
    Installation — die Systembibliotheken des Wirts gehen uns nichts an.
    """
    if sys.platform.startswith("win"):
        return _linked_windows(binary, inside)
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

def cpython_baeume(root: Path) -> list:
    """Die CPython-Bäume unter `<root>/python` — es gibt ZWEI Anordnungen.

    `uv python install` legt den Baum in ein VERSIONIERTES Unterverzeichnis
    (`<root>/python/cpython-3.12.11-linux-x86_64-gnu/`), der Windows-Assistent
    packt ihn dagegen direkt nach `<root>/python/` (das Archiv trägt `python`
    als oberste Ebene).

    Wer nur die erste Anordnung kennt, findet unter Windows **nichts**: die
    Muster laufen dann gegen `Lib/`, `DLLs/`, `tcl/` als Wurzel und treffen
    keines.  Gemessen am 2026-08-14: die Installation belegte 163 statt 120 MB,
    weil Tcl/Tk, IDLE, ensurepip, die Header und ein zweites pip einfach
    stehenblieben.  Deshalb wird hier ERKANNT statt angenommen — Merkmal ist
    das Vorhandensein von `Lib`/`lib` unmittelbar darunter.
    """
    basis = root / "python"
    if not basis.is_dir():
        return []
    if (basis / "Lib").is_dir() or (basis / "lib").is_dir():
        return [basis]
    return [p for p in sorted(basis.glob("*")) if p.is_dir()]


def slim_cpython(root: Path, cut: Cutter) -> None:
    """CPython auf eine Laufzeit eindampfen (Testsuite, Tcl/Tk, Header, IDLE)."""
    dirs = CPYTHON_WIN_DIRS_DROP if IST_WINDOWS else CPYTHON_DIRS_DROP
    globs = CPYTHON_WIN_GLOBS_DROP if IST_WINDOWS else CPYTHON_GLOBS_DROP
    for base in cpython_baeume(root):
        for muster in tuple(dirs) + tuple(globs):
            for treffer in base.glob(muster):
                cut.drop(treffer)


def slim_symbole(root: Path, cut: Cutter) -> None:
    """Symboltabellen aus den **Bibliotheken** entfernen, wo noch welche drin sind.

    Qt kommt bereits gestrippt aus dem Wheel, C-Erweiterungen fremder Pakete
    nicht (PyYAML: 2,6 → 0,4 MB).  Deshalb wird nicht aufgezählt, sondern über
    alles gelaufen — künftige Abhängigkeiten sind damit gleich mit erfasst.
    Programme bleiben unangetastet, siehe `strip_datei`.
    """
    # Unter Windows gegenstandslos: `strip` ist ein ELF-Werkzeug, und MSVC legt
    # Debugsymbole ohnehin in eine eigene `.pdb`, die gar nicht erst mitkommt.
    if IST_WINDOWS or not shutil.which("strip") or cut.dry_run:
        return
    kandidaten = []
    for base in sorted((root / "python").glob("*")):
        if base.is_dir() and not base.is_symlink():
            kandidaten += list(base.glob("lib/python*/lib-dynload/*.so"))
    for s in site_packages(root):
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
    # Windows kennt das Problem nicht: dort ist `python312.dll` die Laufzeit
    # selbst und wird von `python.exe` gebraucht — es gibt keine ueberzaehlige
    # zweite Ausfertigung.
    if IST_WINDOWS:
        return
    for base in cpython_baeume(root):
        if base.is_symlink():
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


def site_packages(root: Path) -> list:
    """Die `site-packages` des venv — unter Windows `venv/Lib/`, sonst `venv/lib/pythonX.Y/`."""
    treffer = list((root / "venv" / "lib").glob("python*/site-packages"))
    treffer += [d for d in ((root / "venv" / "Lib" / "site-packages"),) if d.is_dir()]
    return treffer


def _pyside_dir(root: Path):
    for sp in site_packages(root):
        if (sp / "PySide6").is_dir():
            return sp / "PySide6"
    return None


def slim_pyside(root: Path, cut: Cutter) -> None:
    """Qt auf das eindampfen, was die GUI (Widgets + OpenGL) tatsächlich lädt."""
    pyside = _pyside_dir(root)
    if pyside is None:
        return

    # Windows legt das Wheel anders aus (gemessen 2026-08-12, Erkundungsstufe im
    # Paketjob): die Qt-DLLs und die Plugins liegen DIREKT in `PySide6/`, nicht
    # unter `PySide6/Qt/`; die Bindungen heissen `.pyd` statt `.abi3.so`.
    qt = pyside if IST_WINDOWS else pyside / "Qt"
    bindung_muster = "*.pyd" if IST_WINDOWS else "*.abi3.so"

    # 1. Bindungen, die nie importiert werden (je 1–9 MB).
    for datei in pyside.glob(bindung_muster):
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
        kandidaten = list(pyside.glob(muster))
        if IST_WINDOWS:
            kandidaten += list(pyside.glob(muster + ".exe"))
        for treffer in kandidaten:
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
    plugins = qt / "plugins"   # unter Windows ist `qt` = `pyside`, also PySide6/plugins
    if plugins.is_dir():
        for gruppe in plugins.iterdir():
            if gruppe.is_dir() and gruppe.name not in PLUGINS_KEEP:
                cut.drop(gruppe)
        for rel in PLUGIN_FILES_DROP:
            cut.drop(plugins / rel)

    # 6. Qt-Bibliotheken: alles, was von den verbliebenen Bindungen und Plugins
    #    aus nicht erreichbar ist.  DAS ist der große Posten (~130 MB), und er
    #    wird nicht geraten, sondern aus den Abhängigkeiten bestimmt (ldd bzw.
    #    die PE-Importtabelle).
    lib = pyside if IST_WINDOWS else qt / "lib"
    if not lib.is_dir():
        return
    lib_resolved = lib.resolve()

    wurzeln = list(pyside.glob(bindung_muster))
    wurzeln += list(pyside.glob("../shiboken6/*.pyd" if IST_WINDOWS else "../shiboken6/*.so*"))
    if IST_WINDOWS:
        wurzeln += list((pyside / ".." / "shiboken6").glob("*.dll"))
    if plugins.is_dir():
        wurzeln += [p for p in plugins.rglob("*.dll" if IST_WINDOWS else "*.so")]

    gebraucht = set()
    for w in wurzeln:
        gebraucht |= linked_libraries(w, lib_resolved)

    if not gebraucht:
        # Nichts aufgelöst — dann lieber nichts anfassen, als die Installation zu
        # zerlegen.
        print("     (keine Abhängigkeiten aufgelöst — Qt-Bibliotheken bleiben unangetastet)")
        return

    # UNTER WINDOWS ist `lib` das Verzeichnis PySide6/ SELBST, in dem auch die
    # Bindungen (`QtCore.pyd`), Python-Dateien und `opengl32sw.dll` liegen.  Ein
    # blindes „weg, was nicht erreichbar ist" löschte hier die Bindung, mit der
    # der Kehraus gerade erst gerechnet hat.  Deshalb fasst er dort
    # ausschließlich `Qt6*.dll` an.
    kandidaten = sorted(lib.glob(QT_DLL_MUSTER_WIN)) if IST_WINDOWS else \
                 [d for d in lib.iterdir() if not d.is_dir()]

    for datei in kandidaten:
        if IST_WINDOWS and datei.name in WIN_IMMER_BEHALTEN:
            continue
        try:
            ziel = datei.resolve()
        except OSError:
            continue
        if ziel not in gebraucht:
            cut.drop(datei)


def slim_venv_paketverwaltung(root: Path, cut: Cutter) -> None:
    """`pip` aus der Laufzeitumgebung werfen — sie ist eine LAUFZEIT, kein Bauplatz.

    In die Installation wird genau einmal etwas installiert, und das ist zu
    diesem Zeitpunkt vorbei.  Unter Linux stellt sich die Frage gar nicht:
    `uv venv` legt von vornherein kein pip an.  Unter Windows baut der
    Assistent die Umgebung mit `python -m venv`, und das bringt pip mit —
    11 MB, die nie wieder jemand anfasst.  (Ein Update baut die Umgebung neu,
    pip ist dann wieder da, solange es gebraucht wird.)
    """
    for sp in site_packages(root):
        for muster in ("pip", "pip-*.dist-info", "setuptools", "setuptools-*.dist-info",
                       "pkg_resources", "_distutils_hack", "distutils-precedence.pth"):
            for treffer in sp.glob(muster):
                cut.drop(treffer)
    for bin_dir in ((root / "venv" / "Scripts"), (root / "venv" / "bin")):
        if not bin_dir.is_dir():
            continue
        for muster in ("pip.exe", "pip3.exe", "pip3.*.exe", "pip", "pip3", "pip3.*",
                       "easy_install*", "wheel*"):
            for treffer in bin_dir.glob(muster):
                cut.drop(treffer)


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
    slim_venv_paketverwaltung(root, cut)
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

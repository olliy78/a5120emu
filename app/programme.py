"""Die Nachbarprogramme starten — DiskTool, Emulator, Werkzeugkonsole.

Die Installation bringt vier Programme mit, die dieselben Disketten anfassen:
die beiden Oberflächen (``a5120emu`` und ``k1520DiskTool``) und die beiden
Konsolenwerkzeuge (``k1520dbg``, ``k1520disktool-cli``).  Wer eines davon offen
hat, braucht regelmässig ein zweites — deshalb kann jede Oberfläche die andere
aufrufen, und der Emulator zusätzlich eine **Eingabeaufforderung**, in der die
Konsolenwerkzeuge ohne Pfadangabe laufen.

Drei Festlegungen, die den Aufbau tragen:

* **Gestartet wird der eigene Interpreter mit dem Skript des anderen Programms**
  (``sys.executable`` + ``<root>/app/…/main.py``), nicht der Starter aus
  ``bin/``.  Der Starter existiert nur in einer Installation; der Quellbaum hat
  stattdessen ``run_a5120emu.sh``/``run_disktool.sh``, und der Weg über den
  Interpreter ist in beiden Layouts derselbe.  Die Umgebung
  (``K1520_HOME``, ``K1520_DATA``, unter Linux ``LD_LIBRARY_PATH``) erbt das
  Kind ohnehin — es steht damit genau so da wie das startende Programm.

* **Das Kind wird abgekoppelt** (eigene Sitzung bzw. eigene Prozessgruppe).
  Sonst nähme das Beenden des Elternteils es mit, und ein „mal eben das DiskTool
  aufmachen" endete beim Schliessen des Emulatorfensters.

* **Die Konsole ist eine erzeugte Startdatei, kein Kommandoband.**  Sie liegt im
  Konfigurationsverzeichnis, wird bei jedem Start überschrieben und ist damit
  les- und kopierbar: wer sie anpassen will (eigener Assembler im ``PATH``,
  anderer Arbeitsordner), hat eine Vorlage vor sich.  Dasselbe Muster wie
  ``packaging/k1520dbg.cmd.in`` beim Windows-Installer.
"""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Sequence

from app import paths

#: Kennungen der beiden Oberflächen für :func:`programm_starten`.
EMULATOR = "emulator"
DISKTOOL = "disktool"

#: (Skript unterhalb der Wurzel, Anzeigename) je Kennung.
_PROGRAMME = {
    EMULATOR: ("app/main.py", "a5120emu"),
    DISKTOOL: ("app/disktool/main.py", "k1520DiskTool"),
}

#: Name des Debugger-Handbuchs in :func:`app.paths.doc_dir`.
HANDBUCH_DBG = "handbuch_k1520dbg.md"

#: Dateiname der erzeugten Startdatei der Werkzeugkonsole.
KONSOLENDATEI = "werkzeugkonsole"


def _ist_windows() -> bool:
    return sys.platform.startswith("win")


def _ist_macos() -> bool:
    return sys.platform == "darwin"


# ─── Oberflächen ─────────────────────────────────────────────────────────────

def _interpreter() -> str:
    """Der Interpreter, mit dem das Kind läuft — unter Windows der fensterlose.

    ``pythonw.exe`` statt ``python.exe``: sonst steht hinter dem gestarteten
    Fenster eine leere Eingabeaufforderung, die erst mit ihm wieder verschwindet.
    """
    exe = sys.executable or "python3"
    if _ist_windows():
        ohne_fenster = Path(exe).with_name("pythonw.exe")
        if ohne_fenster.is_file():
            return str(ohne_fenster)
    return exe


def _abgekoppelt(befehl: Sequence[str], cwd: Optional[Path] = None,
                 neues_fenster: bool = False) -> None:
    """``befehl`` starten und vom eigenen Lebenslauf lösen.

    Die ``Popen``-Hülle wird bewusst nicht festgehalten: ``subprocess`` räumt
    beendete Kinder beim nächsten Start selbst ab, und ein Verweis hier hiesse,
    das Kind zu überwachen — das ist nicht die Aufgabe.
    """
    kw = {}
    if _ist_windows():
        flags = subprocess.CREATE_NEW_PROCESS_GROUP
        flags |= (subprocess.CREATE_NEW_CONSOLE if neues_fenster
                  else getattr(subprocess, "DETACHED_PROCESS", 0x00000008))
        kw["creationflags"] = flags
    else:
        kw["start_new_session"] = True
    subprocess.Popen(list(befehl), cwd=str(cwd) if cwd else None, **kw)


def arbeitsordner() -> Optional[Path]:
    """Das Verzeichnis, in dem ein gestartetes Programm stehen soll.

    Die Arbeitsdisketten des Anwenders — dort gehen seine Dateidialoge auf, und
    dorthin legt der Kern sein Protokoll (``logs/`` im Arbeitsverzeichnis).
    Dieselbe Wahl treffen die Starter des Pakets (``packaging/launcher.sh``).
    Lässt sich das Verzeichnis nicht anlegen, wird ``None`` geliefert und das
    Kind erbt schlicht das Arbeitsverzeichnis des Elternteils.
    """
    ziel = paths.user_disks_dir()
    try:
        ziel.mkdir(parents=True, exist_ok=True)
    except OSError:
        return None
    return ziel


def programm_starten(kennung: str, argumente: Sequence[str] = ()) -> None:
    """Die andere Oberfläche starten (:data:`EMULATOR` oder :data:`DISKTOOL`).

    Raises:
        RuntimeError: wenn das Skript nicht zu finden ist oder der Start
            scheitert — mit einer Meldung, die in ein Meldungsfenster passt.
    """
    rel, name = _PROGRAMME[kennung]
    skript = paths.base_dir() / rel
    if not skript.is_file():
        raise RuntimeError(
            f"{name} ist nicht zu finden.\n\nErwartet wurde:\n{skript}")
    try:
        _abgekoppelt([_interpreter(), str(skript), *argumente],
                     cwd=arbeitsordner())
    except OSError as e:
        raise RuntimeError(f"{name} liess sich nicht starten:\n{e}") from e


# ─── Werkzeugkonsole ─────────────────────────────────────────────────────────

def beispieldiskette(ordner: Optional[Path]) -> str:
    """Name einer Diskette aus ``ordner`` für die Beispielzeile der Konsole.

    Ein Beispiel, das der Anwender abtippen kann, ist mehr wert als ein
    Platzhalter — gibt es aber keine Diskette (frische Installation, leerer
    Ordner), bleibt es beim Platzhalter.
    """
    if ordner is not None and ordner.is_dir():
        for muster in ("*.hfe", "*.dmk", "*.img"):
            treffer = sorted(p.name for p in ordner.glob(muster))
            if treffer:
                return treffer[0]
    return "DISKETTE.hfe"


_UMSCHRIFT = str.maketrans({
    "ä": "ae", "ö": "oe", "ü": "ue", "Ä": "Ae", "Ö": "Oe", "Ü": "Ue",
    "ß": "ss", "—": "-", "–": "-", "„": '"', "“": '"', "”": '"', "…": "...",
    "▸": ">",
})


def _nur_ascii(text: str) -> str:
    """Umlaute und Typografie auflösen — für die Windows-Startdatei.

    ``cmd.exe`` liest eine Batchdatei in der Kodepage, die gerade eingestellt
    ist (meist 850 oder 437), nicht in UTF-8; ein Umlaut käme dort als Kauderwelsch
    heraus.  Die Datei wird deshalb ASCII gehalten — wie die mitgelieferte
    Vorlage ``packaging/k1520dbg.cmd.in``.
    """
    return text.translate(_UMSCHRIFT).encode("ascii", "replace").decode("ascii")


def konsolentext(ordner: Optional[Path] = None) -> str:
    """Der Begrüssungstext der Werkzeugkonsole.

    Er nennt genau drei Dinge: einen Aufruf, den man abtippen kann, wo man
    steht, und wo das Handbuch liegt.
    """
    if ordner is None:
        ordner = arbeitsordner()
    diskette = beispieldiskette(ordner)
    dbg = paths.debugger()
    cli = paths.disktool_cli()
    dbg_name = dbg.name if dbg else "k1520dbg"
    cli_name = cli.name if cli else "k1520disktool-cli"
    handbuch = paths.doc_file(HANDBUCH_DBG)

    zeilen: List[str] = [
        "",
        "  K1520-Werkzeuge — Eingabeaufforderung",
        "",
        f"    {dbg_name} {diskette}",
        "        Debugger: die Maschine startet mit dieser Diskette in Laufwerk A:,",
        "        „b 0x0100“ setzt einen Haltepunkt, „g“ lässt laufen, „q“ beendet.",
        "",
        f"    {cli_name} ls {diskette}",
        "        Was auf der Diskette liegt (get/put holen und schreiben Dateien).",
        "",
        f"  Arbeitsordner:  {ordner if ordner else os.getcwd()}",
    ]
    if handbuch is not None:
        zeilen.append(f"  Handbuch:       {handbuch}")
    else:
        zeilen.append(f"  Handbuch:       {HANDBUCH_DBG} (nicht mitgeliefert)")
    zeilen.append("")
    return "\n".join(zeilen)


def konsolenumgebung() -> dict:
    """Die Umgebungsvariablen, die in der Konsole gesetzt werden.

    Nur was die Werkzeuge wirklich brauchen: die Wurzel, der Formatkatalog (sonst
    sucht der Kern ihn neben der Bibliothek, was in einer verschobenen
    Installation danebengehen kann) und unter Linux/macOS der Bibliothekspfad —
    ``k1520dbg`` bindet den Kern zwar statisch, aber ein später hinzukommendes
    Werkzeug tut das vielleicht nicht.
    """
    umgebung = {"K1520_HOME": str(paths.base_dir())}
    katalog = paths.formats_file()
    if katalog is not None:
        umgebung["K1520_FORMATS"] = str(katalog)
    return umgebung


def _startdatei() -> Path:
    """Pfad der erzeugten Startdatei (Endung je Plattform)."""
    endung = ".cmd" if _ist_windows() else ".sh"
    return paths.config_dir() / (KONSOLENDATEI + endung)


def _schreibe_startdatei(ordner: Optional[Path]) -> Path:
    """Die Startdatei der Werkzeugkonsole erzeugen und ihren Pfad liefern."""
    ziel = _startdatei()
    ziel.parent.mkdir(parents=True, exist_ok=True)
    werkzeuge = paths.tools_dir()
    umgebung = konsolenumgebung()
    text = konsolentext(ordner)

    if _ist_windows():
        zeilen = [
            "@echo off",
            "rem Von a5120emu erzeugt (Werkzeuge > Werkzeugkonsole).",
            "rem Wird bei jedem Start ueberschrieben — zum Behalten woandershin kopieren.",
            "setlocal",
        ]
        for name, wert in umgebung.items():
            zeilen.append(f'set "{name}={wert}"')
        zeilen.append(f'set "PATH={werkzeuge};%PATH%"')
        if ordner is not None:
            zeilen.append(f'cd /d "{ordner}"')
        for zeile in _nur_ascii(text).splitlines():
            # `echo.` ist die einzige Form, die eine LEERE Zeile ausgibt.
            zeilen.append("echo." if not zeile.strip() else f"echo {zeile}")
        zeilen.append("cmd /k")
        inhalt = "\r\n".join(zeilen) + "\r\n"
        # `mbcs` ist die ANSI-Kodepage des Rechners; der Text ist ohnehin ASCII,
        # ein nicht-lateinischer Pfad käme so wenigstens noch durch.
        ziel.write_text(inhalt, encoding="mbcs", errors="replace")
        return ziel

    zeilen = [
        "#!/bin/sh",
        "# Von a5120emu erzeugt (Werkzeuge ▸ Werkzeugkonsole).",
        "# Wird bei jedem Start überschrieben — zum Behalten woandershin kopieren.",
    ]
    for name, wert in umgebung.items():
        zeilen.append(f"{name}={shlex.quote(wert)}; export {name}")
    zeilen.append(f'PATH={shlex.quote(str(werkzeuge))}":$PATH"; export PATH')
    if ordner is not None:
        zeilen.append(f"cd {shlex.quote(str(ordner))} 2>/dev/null || true")
    zeilen += [
        "cat <<'K1520_ENDE'",
        text,
        "K1520_ENDE",
        # Eine INTERAKTIVE Shell zum Schluss — sonst schlösse sich das Fenster
        # sofort wieder.  `$SHELL` ist die des Anwenders samt seiner Einrichtung.
        'exec "${SHELL:-/bin/sh}" -i',
    ]
    ziel.write_text("\n".join(zeilen) + "\n", encoding="utf-8")
    ziel.chmod(0o755)
    return ziel


#: Terminalprogramme und das Argument, hinter dem ihr Kommando steht.
#: Der Reihe nach probiert; ``x-terminal-emulator`` ist unter Debian die Wahl
#: des Anwenders selbst und steht deshalb vorn.
TERMINALS = [
    ("x-terminal-emulator", ["-e"]),
    ("konsole", ["-e"]),
    ("gnome-terminal", ["--"]),
    ("xfce4-terminal", ["-x"]),
    ("mate-terminal", ["-x"]),
    ("tilix", ["-e"]),
    ("alacritty", ["-e"]),
    ("kitty", []),
    ("foot", []),
    ("wezterm", ["start", "--"]),
    ("urxvt", ["-e"]),
    ("xterm", ["-e"]),
]


def terminalbefehl(startdatei: Path) -> Optional[List[str]]:
    """Der Aufruf, der ein Terminalfenster mit ``startdatei`` öffnet.

    ``None``, wenn kein Terminalprogramm zu finden ist — dann bleibt dem
    Aufrufer nur, den Pfad der Startdatei zu nennen.  ``$TERMINAL`` schlägt die
    Liste: wer die Variable gesetzt hat, hat sich entschieden.
    """
    if _ist_windows():
        # `cmd /c <batch>` in einem NEUEN Fenster (siehe :func:`konsole_starten`):
        # die Batchdatei endet auf `cmd /k`, der Anwender bleibt also in derselben
        # Konsole sitzen.  Der Umweg über `start` täte dasselbe, öffnete aber
        # kurz ein zweites Fenster, das sich gleich wieder schliesst.
        return ["cmd.exe", "/c", str(startdatei)]
    if _ist_macos():
        # Terminal.app führt ein übergebenes Skript aus und bleibt offen.
        return ["open", "-a", "Terminal", str(startdatei)]

    eigenes = os.environ.get("TERMINAL")
    kandidaten = ([(eigenes, ["-e"])] if eigenes else []) + TERMINALS
    for name, flaggen in kandidaten:
        pfad = shutil.which(name)
        if pfad:
            return [pfad, *flaggen, "/bin/sh", str(startdatei)]
    return None


def konsole_starten() -> Path:
    """Ein Konsolenfenster mit den K1520-Werkzeugen öffnen.

    Returns:
        Den Pfad der erzeugten Startdatei — der Aufrufer kann ihn nennen.

    Raises:
        RuntimeError: wenn kein Terminalprogramm gefunden wird oder der Start
            scheitert.  Die Meldung nennt die Startdatei, damit der Anwender sie
            selbst aufrufen kann.
    """
    ordner = arbeitsordner()
    datei = _schreibe_startdatei(ordner)
    befehl = terminalbefehl(datei)
    if befehl is None:
        raise RuntimeError(
            "Es wurde kein Terminalprogramm gefunden.\n\n"
            "Die vorbereitete Startdatei lässt sich von Hand aufrufen:\n"
            f"{datei}")
    try:
        # Unter Windows muss das Fenster hier entstehen (``CREATE_NEW_CONSOLE``) —
        # der Emulator läuft unter ``pythonw.exe`` und hat selbst keine Konsole,
        # die das Kind erben könnte.  Unter Linux/macOS bringt das Terminalprogramm
        # sein Fenster mit.
        _abgekoppelt(befehl, cwd=ordner, neues_fenster=_ist_windows())
    except OSError as e:
        raise RuntimeError(
            f"Das Konsolenfenster liess sich nicht öffnen:\n{e}\n\n"
            f"Die Startdatei liegt bereit unter:\n{datei}") from e
    return datei

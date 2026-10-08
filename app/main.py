#!/usr/bin/env python3
"""
K1520 Emulator — A5120, K8915, PRG710, PC1715 und P8000 Emulator
===============================================================

Main entry point for the Qt6 GUI application.  EIN Programm, fünf Gesichter:
``--machine k8915`` wählt das Programmprofil des K8915 Emulators (eigene
Konfiguration, Tastatur K7672, Frontplatte, NMI-Taster — `app/profil.py`),
``--machine prg710`` das des PRG710 Emulators (PRG 710 / PRG 710-1, Modellwahl),
``--machine pc1715`` das des PC1715 Emulators (PC 1715, Modellwahl),
``--machine p8000`` das des P8000 Emulators (Terminal, Winchester, Modellwahl),
(``--mode terminal`` stellt dort die Betriebsart „nur Terminal“ ein, ``--mode computer`` „Computer mit
Terminal“); ohne Schalter ist es der A5120 Emulator.  Die Starter übergeben den Schalter
fest (``run_k8915emu.sh``, ``run_prg710emu.sh``, ``run_pc1715emu.sh``, ``run_p8000emu.sh``, ``bin/k8915emu``, Startmenü).

Usage:
    python3 app/main.py [--machine a5120|k8915|prg710|pc1715|p8000]
                        [--mode computer|terminal]  (nur p8000)
                        [--instance NAME] [--config DATEI] [DISKETTE …]

Requirements:
    - PySide6 (Qt6 Python bindings)
    - Python 3.8+
    - libk1520core.so built in build/ directory

Setup (details: SETUP.md):
    1. Build the C++ core:      tools/dev.sh build
    2. Python dependencies:     python3 -m pip install -r requirements.txt
    3. Run the GUI:             bash run_a5120emu.sh
       (sets LD_LIBRARY_PATH=build, activates venv, runs this file)
"""

import os
import sys
import signal
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

# ── Ausgabe auf UTF-8 festnageln ────────────────────────────────────────────
#
# Unter Windows benutzt Python fuer eine UMGELEITETE Ausgabe (Pipe, Datei) die
# Kodepage des Systems, nicht UTF-8 — an einer echten Konsole dagegen sehr wohl
# UTF-8.  Der Unterschied faellt genau dort auf, wo niemand hinsieht: `--help`
# enthaelt einen Gedankenstrich, und der ist in cp1252 das Byte 0x97.  Wer die
# Ausgabe in eine Datei leitet oder sie (wie die Testebene) einliest, bekam
# `UnicodeDecodeError` bzw. gar nichts.  An der Konsole aendert die Zeile nichts,
# dort ist UTF-8 ohnehin schon eingestellt.
for _strom in (sys.stdout, sys.stderr):
    try:
        _strom.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):      # kein TextIO (z. B. umgebogen)
        pass


# --machine: Programmprofil wählen — ebenfalls vor allem anderen, denn --help und
# die Diskettenprüfung reden schon vom Programm.  Der Schalter wird aus argv
# entfernt, damit ihn keine spätere Auswertung (Qt, Disketten) als Datei nimmt.
from app import profil as _profile

_MASCHINE = ""
_rest = []
_args = iter(sys.argv[1:])
for _arg in _args:
    if _arg == "--machine":
        _MASCHINE = next(_args, "")
        if not _MASCHINE:
            print("--machine braucht einen Namen: a5120, k8915, prg710, pc1715 oder p8000", file=sys.stderr)
            sys.exit(2)
    elif _arg.startswith("--machine="):
        _MASCHINE = _arg.split("=", 1)[1]
    elif _arg == "--mode" or _arg.startswith("--mode="):
        # Betriebsart des P8000 (computer | terminal) für diesen Start vorwählen; sie wird danach
        # wie jede Einstellung gemerkt.  Über die Umgebung an das Hauptfenster gereicht.
        _wert = next(_args, "") if _arg == "--mode" else _arg.split("=", 1)[1]
        if _wert not in ("computer", "terminal"):
            print("--mode: computer oder terminal", file=sys.stderr)
            sys.exit(2)
        os.environ["K1520_BETRIEBSART"] = _wert
    elif _arg in ("--instance", "--config"):
        # Mehrinstanzbetrieb (app/instanz.py): eigener Konfigurationsname bzw. -datei.  Über die
        # Umgebung weitergereicht, damit auch ein von hier gestartetes Kindprogramm sie kennt.
        _wert = next(_args, "")
        if not _wert:
            print(f"{_arg} braucht einen Wert", file=sys.stderr)
            sys.exit(2)
        os.environ["K1520_INSTANZ" if _arg == "--instance" else "K1520_KONFIG"] = _wert
    elif _arg.startswith(("--instance=", "--config=")):
        _name, _wert = _arg.split("=", 1)
        os.environ["K1520_INSTANZ" if _name == "--instance" else "K1520_KONFIG"] = _wert
    else:
        _rest.append(_arg)
try:
    PROFIL = _profile.profil(_MASCHINE)
except ValueError as _e:
    print(f"--machine: {_e}", file=sys.stderr)
    sys.exit(2)
sys.argv[1:] = _rest

# --paths: aufgelöste Pfade ausgeben und beenden.  Steht VOR den Qt- und
# Bindungs-Importen, damit die Auskunft auch dann kommt, wenn genau das fehlt,
# wonach gefragt wird (Kernbibliothek, PySide6).  Rauchtest des Installers,
# siehe doc/design/13_distribution.md §3.1.
if "--paths" in sys.argv[1:]:
    from app import paths
    print(paths.describe())
    sys.exit(0)

# --help: ebenfalls vor den Qt-Importen, damit die Hilfe auch ohne PySide6 kommt.
# Laufwerke, die das Profil ab Werk bestückt (A5120: A:–C: + leerer D:-Platz, also
# vier Steckplätze; K8915: zwei) — so viele Disketten nimmt die Kommandozeile.
_ANZAHL_LAUFWERKE = 4 if PROFIL.maschine == "a5120" else 2   # K8915, PRG: zwei
_LAUFWERKE_TEXT = ("bis zu vier Abbilder, in Laufwerksreihenfolge A: B: C: D:"
                   if _ANZAHL_LAUFWERKE == 4 else
                   "bis zu zwei Abbilder, in Laufwerksreihenfolge A: B:")
_KOPF = {"a5120": "Emulator des Buerocomputers A5120 (K1520-Bus)",
         "k8915": "Emulator des Arbeitsplatzcomputers K8915 (K1520-Bus)",
         "prg710": "Emulator der Programmiergeraete PRG 710 und PRG 710-1 (K1520-Bus)",
         "pc1715": "Emulator der Buerocomputer PC 1715 und PC 1715W",
         "p8000": "Emulator des 16-Bit-Arbeitsplatzcomputers P8000 (U880 + U8001, UDOS und WEGA)"}
_P = PROFIL.programm
_MODUS_HILFE = ("  {p} --mode MODUS      Betriebsart: computer (Rechner mit Terminal) oder terminal\n"
                "                        (nur das Terminal, Verbindung zu einem anderen Rechner)\n"
                if PROFIL.maschine == "p8000" else "")
HILFE = f"""{_P} — {_KOPF[PROFIL.maschine]}

  {_P} [DISKETTE …]     {_LAUFWERKE_TEXT}
{_MODUS_HILFE.format(p=_P)}  {_P} --instance NAME  eigene Konfiguration ({PROFIL.konfig_datei[:-5]}-NAME.yaml), mehrere Fenster nebeneinander
  {_P} --config DATEI   Konfigurationsdatei von Hand festlegen
  {_P} --paths          aufgeloeste Pfade zeigen (Bibliothek, Katalog, Disketten)
  {_P} --help           diese Hilfe

Angenommen werden .img, .hfe und .dmk.  Die genannten Disketten liegen beim
Kaltstart bereits im Laufwerk — die Maschine bootet also von der ersten.  Sie
ersetzen die zuletzt gemerkte Belegung nur fuer diesen Lauf; gespeichert wird
nichts.

Ohne Oberflaeche (Skript, Makefile, Fehlersuche) faehrt dieselbe Maschine unter
`k1520dbg`; `k1520dbg DISKETTE --console` ist die Konsolenfassung.
"""
if "--help" in sys.argv[1:] or "-h" in sys.argv[1:]:
    print(HILFE)
    sys.exit(0)

# Diskettenargumente hier auswerten — ebenfalls VOR den Qt-Importen.  Ein
# Tippfehler im Dateinamen soll eine Zeile im Terminal ergeben, nicht ein Fenster
# mit leerem Laufwerk; und er soll auch dann gemeldet werden, wenn PySide6 gar
# nicht installiert ist.
CLI_DISKS = []
for _arg in sys.argv[1:]:
    if _arg.startswith("-"):
        print(f"{_P}: unbekannte Option '{_arg}' — `--help` zeigt die Bedienung",
              file=sys.stderr)
        sys.exit(2)
    if not Path(_arg).is_file():
        print(f"{_P}: '{_arg}' gibt es nicht", file=sys.stderr)
        sys.exit(2)
    CLI_DISKS.append(_arg)
if len(CLI_DISKS) > _ANZAHL_LAUFWERKE:
    _zahl = "vier" if _ANZAHL_LAUFWERKE == 4 else "zwei"
    print(f"{_P}: {len(CLI_DISKS)} Disketten angegeben, die Maschine hat {_zahl} "
          f"Laufwerke", file=sys.stderr)
    sys.exit(2)

from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QTimer
from app.ui.main_window import MainWindow


def main():
    """Main entry point."""
    app = QApplication(sys.argv)

    # Set application metadata
    app.setApplicationName("K1520 Emulator")
    app.setApplicationVersion("1.0.0")

    # Ctrl+C im Terminal sauber beenden: Qts C++-Eventloop kehrt sonst nie nach
    # Python zurück, sodass der SIGINT-Handler nie läuft.  Ein periodischer
    # No-op-Timer hält den Interpreter am Ticken, der Handler quittet die App.
    signal.signal(signal.SIGINT, lambda *_: app.quit())
    sigint_timer = QTimer()
    sigint_timer.start(200)
    sigint_timer.timeout.connect(lambda: None)

    # Beim ersten Start einer Installation die Beispieldisketten ins
    # Arbeitsverzeichnis des Anwenders auspacken (im Quellbaum und bei
    # vorhandenem Verzeichnis wirkungslos).
    from app import paths
    paths.seed_user_disks()

    # Create and show main window
    try:
        window = MainWindow(CLI_DISKS, profil=PROFIL)
        window.show()
    except Exception as e:
        # Startabbrüche des Cores (z. B. fehlender Diskettenformat-Katalog
        # data/formats.yaml) tragen eine mehrzeilige, erklärende Meldung —
        # unverändert ausgeben und den Emulator beenden.
        print(f"Failed to start emulator: {e}", file=sys.stderr)
        return 1

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())

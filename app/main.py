#!/usr/bin/env python3
"""
K1520 A5120 Emulator
====================

Main entry point for the Qt6 GUI application.

Usage:
    python3 app/main.py

Requirements:
    - PySide6 (Qt6 Python bindings)
    - Python 3.8+
    - libk1520core.so built in build/ directory

Setup (details: SETUP.md):
    1. Build the C++ core:      tools/dev.sh build
    2. Python dependencies:     python3 -m pip install -r requirements.txt
    3. Run the GUI:             bash run_gui.sh
       (sets LD_LIBRARY_PATH=build, activates venv, runs this file)
"""

import sys
import signal
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

# --paths: aufgelöste Pfade ausgeben und beenden.  Steht VOR den Qt- und
# Bindungs-Importen, damit die Auskunft auch dann kommt, wenn genau das fehlt,
# wonach gefragt wird (Kernbibliothek, PySide6).  Rauchtest des Installers,
# siehe doc/design/13_distribution.md §3.1.
if "--paths" in sys.argv[1:]:
    from app import paths
    print(paths.describe())
    sys.exit(0)

# --help: ebenfalls vor den Qt-Importen, damit die Hilfe auch ohne PySide6 kommt.
HILFE = """a5120emu — Emulator des Buerocomputers A5120 (K1520-Bus)

  a5120emu [DISKETTE …]     bis zu vier Abbilder, in Laufwerksreihenfolge A: B: C: D:
  a5120emu --paths          aufgeloeste Pfade zeigen (Bibliothek, Katalog, Disketten)
  a5120emu --help           diese Hilfe

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
        print(f"a5120emu: unbekannte Option '{_arg}' — `--help` zeigt die Bedienung",
              file=sys.stderr)
        sys.exit(2)
    if not Path(_arg).is_file():
        print(f"a5120emu: '{_arg}' gibt es nicht", file=sys.stderr)
        sys.exit(2)
    CLI_DISKS.append(_arg)
if len(CLI_DISKS) > 4:
    print(f"a5120emu: {len(CLI_DISKS)} Disketten angegeben, die Maschine hat vier "
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
        window = MainWindow(CLI_DISKS)
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

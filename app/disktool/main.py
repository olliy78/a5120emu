#!/usr/bin/env python3
"""
k1520DiskTool
=============

Dateiaustausch zwischen Linux und K1520-Disketten (CP/A, SCPX, UDOS) —
Oberfläche zu `libk1520disk.so`.

Aufruf:
    bash run_disktool.sh [abbild] [ordner]
    python3 app/disktool/main.py [abbild] [ordner]

Voraussetzungen:
    - PySide6
    - libk1520disk.so gebaut (tools/dev.sh build)

Die Kommandozeilenversion desselben Unterbaus ist `build/k1520disktool`
(`tools/dev.sh tool k1520disktool ls <abbild>`).
"""

import signal
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

# --paths: aufgelöste Pfade ausgeben und beenden.  Steht VOR den Qt- und
# Bindungs-Importen, damit die Auskunft auch dann kommt, wenn genau das fehlt,
# wonach gefragt wird (DiskTool-Bibliothek, PySide6) — wie in app/main.py.
if "--paths" in sys.argv[1:]:
    from app import paths
    print(paths.describe())
    sys.exit(0)

# --physical: Kommandozeile für ein ECHTES Laufwerk.  Steht hier, weil das
# C++-Werkzeug (`k1520disktool-cli`) den Adapter nicht ansprechen kann — der Kern
# kennt Greaseweazle nicht, der Arbeitsfaden ist Python
# (doc/design/14_physische_diskette.md §12.3).  Wie `--paths` VOR den Qt-Importen:
# eine Kommandozeile darf keine Oberfläche brauchen.
if "--physical" in sys.argv[1:]:
    from app.disktool.physical_cli import main as physical_main
    sys.exit(physical_main(sys.argv[1:]))

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QApplication

from app.disktool.ui.main_window import MainWindow


def main() -> int:
    app = QApplication(sys.argv)
    # Organisation UND Name: erst damit legt QSettings eine Datei an — und nur
    # dann merkt sich das Fenster Größe, Leisten und zuletzt geöffnete Abbilder
    # (MainWindow._einstellungen).  Die Testläufe benennen sich bewusst nicht.
    app.setOrganizationName("k1520emu")
    app.setApplicationName("k1520DiskTool")

    # Ctrl+C im Terminal: Qts Eventloop kehrt sonst nie nach Python zurück.
    signal.signal(signal.SIGINT, lambda *_: app.quit())
    ticker = QTimer()
    ticker.start(200)
    ticker.timeout.connect(lambda: None)

    # Arbeitsverzeichnisse wie beim Emulator (app/main.py): beim Erststart einer
    # Installation die Beispieldisketten auspacken und den Dateiordner anlegen.
    # Beides ist im Quellbaum wirkungslos.  Ohne das gingen die Dialoge im
    # INSTALLATIONSORDNER auf — dort liegt das Programm, nicht die Arbeit des
    # Anwenders (doc/design/13_k1520disktool.md §20.8).
    from app import paths
    paths.seed_user_disks()
    dateiordner = paths.ensure_user_files_dir()

    abbild = sys.argv[1] if len(sys.argv) > 1 else None
    ordner = sys.argv[2] if len(sys.argv) > 2 else None
    if not ordner and dateiordner is not None:
        ordner = str(dateiordner)

    try:
        fenster = MainWindow(abbild, ordner)
    except FileNotFoundError as e:
        # Fehlende libk1520disk.so trägt eine erklärende, mehrzeilige Meldung.
        print(f"k1520DiskTool kann nicht starten:\n{e}", file=sys.stderr)
        return 1

    fenster.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())

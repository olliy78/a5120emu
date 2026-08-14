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

    abbild = sys.argv[1] if len(sys.argv) > 1 else None
    ordner = sys.argv[2] if len(sys.argv) > 2 else None

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

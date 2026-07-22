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

Build steps:
    1. Build the C++ core:
       mkdir -p build && cd build
       cmake .. -DCMAKE_BUILD_TYPE=Release
       make -j4
    
    2. Install Python dependencies:
       pip install PySide6
    
    3. Run the GUI:
       python3 app/main.py
"""

import sys
import signal
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

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

    # Create and show main window
    try:
        window = MainWindow()
        window.show()
    except Exception as e:
        print(f"Failed to start emulator: {e}", file=sys.stderr)
        return 1

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())

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
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from PySide6.QtWidgets import QApplication
from app.ui.main_window import MainWindow


def main():
    """Main entry point."""
    app = QApplication(sys.argv)
    
    # Set application metadata
    app.setApplicationName("K1520 Emulator")
    app.setApplicationVersion("1.0.0")
    
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

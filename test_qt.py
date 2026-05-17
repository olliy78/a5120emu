import sys
import os
from PyQt5.QtWidgets import QApplication, QMainWindow

# Mocking parts of the app to allow importing widgets
sys.path.append(os.getcwd())

def test_a():
    print("Test A: QApplication + Empty QMainWindow")
    try:
        app = QApplication(sys.argv)
        mw = QMainWindow()
        mw.show()
        app.processEvents()
        print("OK")
    except Exception as e:
        print(f"CRASH/Error: {e}")

def test_b():
    print("Test B: QApplication + DriveWidget")
    try:
        app = QApplication(sys.argv)
        from app.ui.drive_widget import DriveWidget
        class DummyEmulator:
            def __init__(self):
                self.floppy_controller = type('FC', (), {'get_drive': lambda s, i: type('D', (), {'is_inserted': lambda s: False})()})()
        
        emulator = DummyEmulator()
        dw = DriveWidget(emulator)
        dw.show()
        app.processEvents()
        print("OK")
    except Exception as e:
        import traceback
        traceback.print_exc()
        print(f"CRASH/Error: {e}")

def test_c():
    print("Test C: QApplication + ScreenWidget")
    try:
        app = QApplication(sys.argv)
        from app.ui.screen_widget import ScreenWidget
        sw = ScreenWidget()
        sw.show()
        app.processEvents()
        print("OK")
    except Exception as e:
        import traceback
        traceback.print_exc()
        print(f"CRASH/Error: {e}")

def test_d():
    print("Test D: QApplication + MainWindow from app/ui/main_window.py")
    try:
        app = QApplication(sys.argv)
        from app.ui.main_window import MainWindow
        class DummyEmulator:
            def __init__(self):
                self.floppy_controller = type('FC', (), {'get_drive': lambda s, i: type('D', (), {'is_inserted': lambda s: False})()})()
        
        emulator = DummyEmulator()
        mw = MainWindow(emulator)
        mw.show()
        app.processEvents()
        print("OK")
    except Exception as e:
        import traceback
        traceback.print_exc()
        print(f"CRASH/Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        mode = sys.argv[1]
        if mode == "A": test_a()
        elif mode == "B": test_b()
        elif mode == "C": test_c()
        elif mode == "D": test_d()

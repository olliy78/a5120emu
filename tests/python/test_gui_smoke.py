"""GUI-Rauchtest (PySide6, headless über ``QT_QPA_PLATFORM=offscreen``).

Was hier geprüft wird, ist die **Verdrahtung**: Baut sich das Fenster mit allen
Panels auf, hängt ein Emulator daran, greifen Konfigurations- und
Laufwerksfunktionen ineinander, lässt sich alles wieder schließen.

Was hier NICHT geprüft wird: gerenderte Pixel.  Der Bildschirm ist ein
``QOpenGLWidget``; offscreen gibt es keinen Framebuffer-Objekt-Kontext (Qt meldet
„QOpenGLWidget: No fbo, cannot render").  Bildinhalte prüft die C++-Seite über
das VRAM bzw. ``tools/fb_ocr.py``.
"""

import pytest

from conftest import requires_core

pytestmark = requires_core

pytest.importorskip("PySide6", reason="PySide6 nicht installiert")


@pytest.fixture
def window(qapp):
    """Aufgebautes Hauptfenster; wird am Testende geschlossen."""
    from app.ui.main_window import MainWindow
    w = MainWindow()
    w.show()
    qapp.processEvents()
    yield w
    w.close()
    qapp.processEvents()


def test_main_window_builds_with_all_panels(window):
    from app.ui.screen_widget import ScreenWidget
    from app.ui.keyboard import KeyboardWidget
    from app.ui.drive_widget import DriveWidget
    from app.ui.settings_widget import SettingsWidget

    assert window.windowTitle()
    for widget_type in (ScreenWidget, KeyboardWidget, DriveWidget, SettingsWidget):
        assert window.findChildren(widget_type), f"{widget_type.__name__} fehlt im Fenster"


def test_main_window_owns_a_working_emulator(window):
    from app.core_binding.k1520 import K1520Emulator

    assert isinstance(window.emulator, K1520Emulator)
    assert window.emulator.run(50_000) > 0


def test_default_drive_configuration_is_applied(window):
    """Die Laufwerksleiste zeigt genau die bestückten Slots (A:/B:/C:, D: leer)."""
    import app.drive_types as dt

    assert window._drive_types == list(dt.DEFAULT_DRIVE_TYPES)
    assert window.drives_widget.present_drives() == [0, 1, 2]


def test_changing_the_drive_bay_rebuilds_the_panels(window, qapp):
    """Ein leerer Slot verschwindet aus der Leiste, ein neuer kommt hinzu."""
    import app.drive_types as dt

    window.drives_widget.set_drive_types(["K5601", dt.NO_DRIVE, "MF3200", "K5601"])
    qapp.processEvents()
    assert window.drives_widget.present_drives() == [0, 2, 3]

    window.drives_widget.set_drive_types(dt.DEFAULT_DRIVE_TYPES)
    qapp.processEvents()
    assert window.drives_widget.present_drives() == [0, 1, 2]


def test_screen_widget_geometry_matches_the_core_framebuffer(window):
    from app.ui.screen_widget import FB_WIDTH, FB_HEIGHT

    assert (FB_WIDTH, FB_HEIGHT) == (640, 288), "Framebuffer-Geometrie geändert"
    # Die C-API muss dieselbe Geometrie melden, sonst zeigt die GUI Müll.
    from app.core_binding.k1520 import _lib
    assert _lib.k1520_fb_width(window.emulator._handle) == FB_WIDTH
    assert _lib.k1520_fb_height(window.emulator._handle) == FB_HEIGHT


def test_config_roundtrip_through_the_window(window, tmp_path):
    """Konfiguration aus dem laufenden Fenster speichern und zurücklesen."""
    import app.config_io as cfg

    path = tmp_path / "gui-config.yaml"
    data = cfg.build_config(window.screen_widget.params, {"speed": 1.0}, [],
                            {"width": 800, "height": 600}, window._drive_types)
    cfg.save_config(str(path), data)
    assert cfg.load_config(str(path)) == data


def test_window_survives_event_processing(window, qapp):
    """Ein paar Runden Eventloop ohne Ausnahme — fängt Timer-/Signalfehler."""
    for _ in range(10):
        qapp.processEvents()


def test_mounting_a_disk_updates_the_drive_panel(window, qapp, temp_disk):
    """Diskette mounten → die Laufwerksanzeige meldet das Laufwerk als belegt."""
    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert window.emulator.mount_disk(0, path, "cpa780", False), \
        window.emulator.last_error()
    qapp.processEvents()
    assert window.emulator.is_disk_active(0)


def test_drive_panel_shows_the_adaptation_notice(window, qapp, tmp_path):
    """Passt die Diskette nicht zum Laufwerk, steht der Hinweis im Laufwerkskasten."""
    drives = window.drives_widget
    fmt, path = "k5601_ss40_26x128", str(tmp_path / "vierzig_spuren.img")
    assert window.emulator.create_disk(0, path, fmt, False), window.emulator.last_error()
    assert window.emulator.unmount_disk(0)

    # Über den Wiederherstellungsweg mounten — derselbe Pfad wie beim Programmstart.
    drives.load_mounts([{"drive": 0, "path": path, "format": fmt,
                         "write_protect": False}])
    qapp.processEvents()

    label = drives._panels[0]._notice_label
    assert label.isVisibleTo(drives), "Hinweis muss sichtbar sein"
    assert "Double Step aktiviert" in label.text()
    assert "schrittverdoppelt" in label.toolTip(), "Tooltip erklärt den Hinweis"

    # Nach dem Aushängen verschwindet er wieder.
    drives.load_mounts([])
    qapp.processEvents()
    assert not drives._panels[0]._notice_label.isVisibleTo(drives)

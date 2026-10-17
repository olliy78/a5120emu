"""CopyQM-Abbilder (.cqm) im Laufwerkskasten des Emulators (PySide6, headless).

Ein ``.cqm`` ist nur lesbar: Write-Protect gesetzt und nicht abwaehlbar, Hinweis im
Kasten (kein Meldungsfenster), Kommentar als Tooltip, kein Formatdialog,
„Speichern unter…" nur in .hfe/.dmk/.img.
"""

import pytest

from conftest import requires_core
from cqm_bau import leeres_720k

pytestmark = requires_core

pytest.importorskip("PySide6", reason="PySide6 nicht installiert")


@pytest.fixture
def drives(qapp):
    from app.ui.drive_widget import DriveWidget
    from app.core_binding.k1520 import K1520Emulator
    emu = K1520Emulator()
    w = DriveWidget(emu)
    yield w
    w.deleteLater()
    del emu


@pytest.fixture
def cqm_datei(tmp_path):
    p = tmp_path / "test.cqm"
    p.write_bytes(leeres_720k(b"Image file by test\nzweite Zeile"))
    return str(p)


def test_open_filter_knows_cqm(qapp):
    from app.ui.drive_widget import DriveWidget
    assert "*.cqm" in DriveWidget.DISK_FILTER
    # nie Ziel von Neu / Speichern unter
    assert "cqm" not in DriveWidget.BLANK_FILTER
    assert "cqm" not in DriveWidget.SAVE_FILTER_ALL
    assert "cqm" not in DriveWidget.SAVE_FILTER_BITSTREAM


def test_cqm_is_mounted_read_only_without_message_box(drives, cqm_datei):
    assert drives.mount_path(0, cqm_datei)
    panel = drives._panels[0]
    emu = drives.emulator
    assert emu.disk_container(0) == "cqm"
    assert emu.is_disk_write_protected(0)
    # Haken gesetzt und nicht abwaehlbar
    assert panel._wp_check.isChecked()
    assert not panel._wp_check.isEnabled()
    # Hinweis im Kasten, kein Meldungsfenster
    assert panel._notice_label.isVisibleTo(drives) or panel._notice_label.text()
    assert "CopyQM" in panel._notice_label.text()
    assert "nur lesbar" in panel._notice_label.text()
    assert "nicht zurückschreiben" in panel._notice_label.toolTip()


def test_wp_cannot_be_cleared_in_the_core(drives, cqm_datei):
    assert drives.mount_path(0, cqm_datei)
    drives.emulator.set_disk_write_protect(0, False)
    assert drives.emulator.is_disk_write_protected(0)


def test_comment_is_tooltip_of_the_image_name(drives, cqm_datei):
    assert drives.mount_path(0, cqm_datei)
    tipp = drives._panels[0]._path_display.toolTip()
    assert "Image file by test" in tipp and "zweite Zeile" in tipp
    assert "720K Double-Sided" in tipp
    assert "ok" in tipp                      # Pruefwert


def test_container_info_via_the_emulator_api(drives, cqm_datei):
    assert drives.mount_path(0, cqm_datei)
    e = drives.emulator
    assert e.disk_container_info(0, "comment") == "Image file by test\nzweite Zeile"
    assert e.disk_container_info(0, "crc") == "ok"
    assert e.disk_container_info(0, "interleave") == "3"
    assert e.disk_container_info(0, "type") == "cqm"
    assert e.disk_container_info(1, "comment") == ""


def test_unmount_restores_the_write_protect_box(drives, cqm_datei):
    assert drives.mount_path(0, cqm_datei)
    panel = drives._panels[0]
    panel._toggle_btn.click()                 # Unmount
    assert not drives.is_mounted(0)
    assert panel._wp_check.isEnabled()
    assert panel._path_display.toolTip() == ""


def test_no_format_dialog_for_cqm(drives, cqm_datei, monkeypatch):
    """Toggle-Knopf mit .cqm: Dateidialog liefert den Pfad, ein Formatdialog darf
    nicht aufgehen."""
    from PySide6.QtWidgets import QFileDialog
    from app.ui import drive_widget
    monkeypatch.setattr(QFileDialog, "getOpenFileName",
                        staticmethod(lambda *a, **k: (cqm_datei, "")))

    def kein_dialog(*a, **k):
        raise AssertionError("Formatdialog fuer .cqm")
    monkeypatch.setattr(drive_widget.FormatDialog, "frage", staticmethod(kein_dialog))
    drives._panels[0]._toggle_btn.click()
    assert drives.is_mounted(0)
    assert drives.emulator.disk_container(0) == "cqm"


def test_save_as_converts_and_cqm_target_is_refused(drives, cqm_datei, tmp_path,
                                                    monkeypatch):
    from PySide6.QtWidgets import QFileDialog, QMessageBox
    assert drives.mount_path(0, cqm_datei)
    panel = drives._panels[0]

    # .cqm als Ziel: abgelehnt (mit Meldung, der Nutzer hat es bewusst gewaehlt)
    meldungen = []
    monkeypatch.setattr(QMessageBox, "critical",
                        staticmethod(lambda *a, **k: meldungen.append(a[1:])))
    ziel_cqm = str(tmp_path / "x.cqm")
    monkeypatch.setattr(QFileDialog, "getSaveFileName",
                        staticmethod(lambda *a, **k: (ziel_cqm, "")))
    panel._saveas_btn.click()
    assert meldungen and not (tmp_path / "x.cqm").exists()
    assert drives.emulator.disk_container(0) == "cqm"

    # .hfe: konvertiert, danach beschreibbar
    ziel_hfe = str(tmp_path / "kopie.hfe")
    monkeypatch.setattr(QFileDialog, "getSaveFileName",
                        staticmethod(lambda *a, **k: (ziel_hfe, "")))
    panel._saveas_btn.click()
    assert drives.emulator.disk_container(0) == "hfe"
    assert panel._wp_check.isEnabled()
    assert panel._path_display.toolTip() == ""

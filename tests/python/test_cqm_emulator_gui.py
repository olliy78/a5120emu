"""CopyQM-Abbilder (.cqm) im Laufwerkskasten des Emulators (PySide6, headless).

Ein ``.cqm`` ist schreibbar (Haken frei), Kommentar als Tooltip, kein Formatdialog,
Hinweise je Programmprofil im Kasten (kein Meldungsfenster), „Speichern unter…"
als .cqm nur, wenn das Medium es traegt (sonst klare Meldung).
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


def test_cqm_is_a_target_of_new_and_save_as(qapp):
    from app.ui.drive_widget import DriveWidget
    assert "*.cqm" in DriveWidget.DISK_FILTER
    assert "*.cqm" in DriveWidget.BLANK_FILTER
    assert "*.cqm" in DriveWidget.SAVE_FILTER_ALL
    assert "*.cqm" in DriveWidget.SAVE_FILTER_CQM
    assert "cqm" not in DriveWidget.SAVE_FILTER_BITSTREAM


def test_cqm_is_mounted_writable_without_message_box(drives, cqm_datei, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    monkeypatch.setattr(QMessageBox, "critical", staticmethod(
        lambda *a, **k: pytest.fail("Meldungsfenster beim Einlegen")))
    assert drives.mount_path(0, cqm_datei)
    panel = drives._panels[0]
    emu = drives.emulator
    assert emu.disk_container(0) == "cqm"
    assert not emu.is_disk_write_protected(0)
    assert not panel._wp_check.isChecked() and panel._wp_check.isEnabled()
    # Der Haken laesst sich setzen und wieder loesen
    panel._wp_check.setChecked(True)
    assert emu.is_disk_write_protected(0)
    panel._wp_check.setChecked(False)
    assert not emu.is_disk_write_protected(0)
    # ohne Profil-Hinweise keine Hinweiszeile
    assert not panel._notice_label.text()


@pytest.mark.parametrize("maschine,udos,boot", [
    ("a5120", True, True), ("prg710", True, False), ("k8915", False, False),
    ("pc1715", False, False), ("p8000", False, False), ("mc80", False, False)])
def test_cqm_hints_follow_the_program_profile(drives, cqm_datei, maschine, udos, boot):
    from app.profil import profil
    drives.cqm_hinweise = profil(maschine).cqm_hinweise
    assert drives.mount_path(0, cqm_datei)
    text = drives._panels[0]._notice_label.text()
    assert ("mit UDOS nicht verwendbar" in text) == udos
    assert ("für Bootdisketten ungeeignet" in text) == boot
    assert "Sektorkontrollblöcke" in text or not udos


def test_no_cqm_hints_for_other_containers(drives, tmp_path):
    from app.profil import profil
    drives.cqm_hinweise = profil("a5120").cqm_hinweise
    ziel = str(tmp_path / "leer.hfe")
    assert drives.emulator.create_disk(0, ziel, "", False)
    drives._mounts[0] = (ziel, "", False)
    drives._update_notice(0)
    assert "CopyQM" not in drives._panels[0]._notice_label.text()


def test_main_window_hands_the_profile_hints_to_the_drive_box():
    from app.profil import profil
    assert profil("a5120").cqm_hinweise and len(profil("a5120").cqm_hinweise) == 2
    assert len(profil("prg710").cqm_hinweise) == 1
    for m in ("k8915", "pc1715", "p8000", "mc80"):
        assert profil(m).cqm_hinweise == ()


def test_wp_can_be_cleared_in_the_core(drives, cqm_datei):
    # Seit dem CQM-Schreiben (2026-10-17) erzwingt der Kern keinen Schreibschutz mehr.
    assert drives.mount_path(0, cqm_datei)
    drives.emulator.set_disk_write_protect(0, False)
    assert not drives.emulator.is_disk_write_protected(0)


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


def test_unmount_clears_the_container_tooltip(drives, cqm_datei):
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


def test_save_as_cqm_roundtrip_and_refusals(drives, cqm_datei, tmp_path, monkeypatch):
    from PySide6.QtWidgets import QFileDialog, QMessageBox
    meldungen = []
    monkeypatch.setattr(QMessageBox, "critical",
                        staticmethod(lambda *a, **k: meldungen.append(a[2])))

    def ziel(pfad):
        monkeypatch.setattr(QFileDialog, "getSaveFileName",
                            staticmethod(lambda *a, **k: (pfad, "")))

    # .cqm -> .cqm: geht (einheitliche MFM-Geometrie)
    assert drives.mount_path(0, cqm_datei)
    panel = drives._panels[0]
    neu = tmp_path / "kopie.cqm"
    ziel(str(neu))
    panel._saveas_btn.click()
    assert not meldungen and neu.exists()
    assert drives.emulator.disk_container(0) == "cqm"
    assert drives.emulator.disk_notice(0) == ""

    # .cqm -> .hfe: konvertiert, danach ohne Container-Tooltip
    ziel_hfe = str(tmp_path / "kopie.hfe")
    ziel(ziel_hfe)
    panel._saveas_btn.click()
    assert drives.emulator.disk_container(0) == "hfe"
    assert panel._path_display.toolTip() == ""

    # Leerdiskette (unformatiert) -> .cqm: klare Meldung VOR dem Kern, Bindung bleibt
    leer = str(tmp_path / "leer.hfe")
    assert drives.emulator.create_disk(1, leer, "", False)
    drives.mount_path(1, leer)
    ziel(str(tmp_path / "leer.cqm"))
    drives._panels[1]._saveas_btn.click()
    assert meldungen and "CopyQM" in meldungen[-1] and "unformatiert" in meldungen[-1]
    assert not (tmp_path / "leer.cqm").exists()
    assert drives.emulator.disk_container(1) == "hfe"


def test_mixed_geometry_boot_disk_is_refused_with_the_reason(drives, tmp_path, monkeypatch):
    """CP/A-Bootdiskette (128-B-Systemspuren + 512-B-Daten): als .cqm nicht darstellbar,
    die Meldung nennt ausdruecklich die gemischten Formate, die Datei entsteht nicht."""
    import shutil
    from pathlib import Path
    from PySide6.QtWidgets import QFileDialog, QMessageBox
    quelle = Path(__file__).resolve().parents[2] / "disks/a5120_cpa/a5120_cpa_k5601_system.hfe"
    if not quelle.exists():
        pytest.skip("Bootdiskette nicht vorhanden")
    kopie = tmp_path / "boot.hfe"
    shutil.copy(quelle, kopie)
    assert drives.mount_path(0, str(kopie))
    meldungen = []
    monkeypatch.setattr(QMessageBox, "critical",
                        staticmethod(lambda *a, **k: meldungen.append(a[2])))
    ziel = str(tmp_path / "boot.cqm")
    monkeypatch.setattr(QFileDialog, "getSaveFileName",
                        staticmethod(lambda *a, **k: (ziel, "")))
    drives._panels[0]._saveas_btn.click()
    assert meldungen and "gemischten Formate" in meldungen[-1], meldungen
    assert not (tmp_path / "boot.cqm").exists()
    assert drives.emulator.disk_container(0) == "hfe"

"""Ziehen und Ablegen von aussen im k1520DiskTool (PySide6, headless).

Regeln: Abbild (.hfe .dmk .img .cqm) auf die Diskettenansicht = Datei > Oeffnen;
EIN Verzeichnis auf die Ordneransicht = Arbeitsordner; Dateien/Ordner auf der
Diskettenliste wie bisher auf die Diskette; alles andere wird abgelehnt.
"""

import shutil

import pytest

from conftest import requires_disk

pytestmark = requires_disk

pytest.importorskip("PySide6")


@pytest.fixture(scope="module")
def qt_app():
    from PySide6.QtWidgets import QApplication
    yield QApplication.instance() or QApplication([])


@pytest.fixture
def window(qt_app):
    from app.disktool.ui.main_window import MainWindow
    w = MainWindow()
    yield w
    w._close_tool()


def _drop(ziel, urls=(), text=None, art="drop"):
    """Ein Drag-Ereignis an `ziel` (Widget) schicken; liefert ``accepted``."""
    from PySide6.QtCore import QEvent, QMimeData, QPointF, Qt, QUrl
    from PySide6.QtGui import QDragEnterEvent, QDropEvent
    from PySide6.QtWidgets import QApplication
    mime = QMimeData()
    if urls:
        mime.setUrls([u if isinstance(u, QUrl) else QUrl.fromLocalFile(str(u))
                      for u in urls])
    if text is not None:
        mime.setText(text)
    pos = QPointF(5, 5)
    ein = QDragEnterEvent(pos.toPoint(), Qt.CopyAction, mime, Qt.LeftButton,
                          Qt.NoModifier)
    ein.setAccepted(False)
    QApplication.sendEvent(ziel, ein)
    if art == "enter" or not ein.isAccepted():
        return ein.isAccepted()
    ev = QDropEvent(pos, Qt.CopyAction, mime, Qt.LeftButton, Qt.NoModifier)
    ev.setAccepted(False)
    QApplication.sendEvent(ziel, ev)
    return ev.isAccepted()


def _links(w):
    return w.disk_view.tree.viewport()


def _rechts(w):
    return w.folder_view.tree.viewport()


def _status(w):
    return w.statusBar().currentMessage()


@pytest.fixture
def abbild(tmp_path, fixture_disks):
    p = tmp_path / "Arbeit.IMG"          # Grossschreibung zaehlt nicht
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", p)
    return p


def test_image_on_left_opens_like_file_open(window, abbild):
    assert _drop(_links(window), [abbild])
    assert window.tool is not None
    assert str(abbild) in str(window.tool.path)
    assert str(abbild.resolve()) in window._zuletzt


def test_hfe_on_left_opens(window, fixture_disks, tmp_path):
    p = tmp_path / "x.hfe"
    shutil.copy(fixture_disks / "udos_boot_scp.hfe", p)
    assert _drop(_links(window), [p])
    assert window.tool is not None


def test_cqm_on_left_opens(window, tmp_path):
    from cqm_bau import leeres_720k
    p = tmp_path / "leer.cqm"
    p.write_bytes(leeres_720k(b"Test"))
    assert _drop(_links(window), [p])
    assert window.tool is not None and window.tool.ist_cqm


def test_two_images_are_refused_without_dialog(window, abbild, tmp_path):
    zweite = tmp_path / "zwei.img"
    shutil.copy(abbild, zweite)
    assert not _drop(_links(window), [abbild, zweite])
    assert window.tool is None
    assert "Nicht abgelegt" in _status(window)


def test_image_plus_other_file_is_refused(window, abbild, tmp_path):
    t = tmp_path / "a.txt"
    t.write_text("x")
    assert not _drop(_links(window), [abbild, t])
    assert window.tool is None


def test_unsaved_changes_ask_first(window, abbild, tmp_path, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    assert window.open_image(str(abbild))
    window.set_read_only(False)
    q = tmp_path / "N.TXT"
    q.write_bytes(b"abc")
    window.tool.insert(q, "N.TXT")
    assert window.tool.dirty
    fragen = []

    def antwort(*a, **k):
        fragen.append(a)
        return QMessageBox.Cancel
    monkeypatch.setattr(QMessageBox, "question", staticmethod(antwort))
    zweite = tmp_path / "zwei.img"
    shutil.copy(abbild, zweite)
    _drop(_links(window), [zweite])
    assert len(fragen) == 1
    assert str(zweite) not in str(window.tool.path)      # Abbruch: nichts geoeffnet


def test_folder_on_right_becomes_work_folder(window, tmp_path):
    ordner = tmp_path / "arbeit"
    ordner.mkdir()
    gemeldet = []
    window.folder_view.folder_changed.connect(gemeldet.append)
    assert _drop(_rechts(window), [ordner])
    assert window.folder_view.folder == ordner
    assert gemeldet == [str(ordner)]


def test_file_or_two_folders_on_right_are_refused(window, tmp_path):
    a = tmp_path / "a"
    b = tmp_path / "b"
    a.mkdir()
    b.mkdir()
    f = tmp_path / "f.txt"
    f.write_text("x")
    vorher = window.folder_view.folder
    assert not _drop(_rechts(window), [f])
    assert not _drop(_rechts(window), [a, b])
    assert window.folder_view.folder == vorher
    assert "Nicht abgelegt" in _status(window)


def test_image_on_right_is_refused(window, abbild):
    assert not _drop(_rechts(window), [abbild])
    assert window.tool is None


def test_plain_file_and_folder_on_left_still_write_to_the_disk(window, abbild, tmp_path,
                                                               monkeypatch):
    assert window.open_image(str(abbild))
    window.set_read_only(False)
    gesehen = []
    monkeypatch.setattr(window, "insert_paths",
                        lambda pfade, v: gesehen.append((list(pfade), v)))
    window.disk_view.files_dropped.disconnect()
    window.disk_view.files_dropped.connect(window._einfuegen_pfade)
    d = tmp_path / "datei.txt"
    d.write_text("x")
    ordner = tmp_path / "ordner"
    ordner.mkdir()
    assert _drop(_links(window), [d])
    assert _drop(_links(window), [ordner])
    assert gesehen == [([str(d)], 0), ([str(ordner)], 0)]


def test_image_extension_conflict_rule_opens_instead_of_writing(window, abbild,
                                                                monkeypatch):
    gesehen = []
    monkeypatch.setattr(window, "insert_paths", lambda *a: gesehen.append(a))
    assert _drop(_links(window), [abbild])
    assert gesehen == [] and window.tool is not None


def test_drag_from_disk_to_folder_still_extracts(window, abbild, tmp_path):
    from PySide6.QtCore import QMimeData
    from app.disktool.ui.disk_view import DISK_MIME
    assert window.open_image(str(abbild))
    ordner = tmp_path / "ziel"
    ordner.mkdir()
    window.folder_view.set_folder(ordner)
    ref = next(e.ref for e in window.tool.list())
    gesehen = []
    window.folder_view.disk_files_dropped.disconnect()
    window.folder_view.disk_files_dropped.connect(gesehen.append)
    from PySide6.QtCore import QPoint, QPointF, Qt
    from PySide6.QtGui import QDragEnterEvent, QDropEvent
    from PySide6.QtWidgets import QApplication
    mime = QMimeData()
    mime.setData(DISK_MIME, ref.encode())
    ein = QDragEnterEvent(QPoint(5, 5), Qt.CopyAction, mime, Qt.LeftButton,
                          Qt.NoModifier)
    QApplication.sendEvent(_rechts(window), ein)
    assert ein.isAccepted()
    ev = QDropEvent(QPointF(5, 5), Qt.CopyAction, mime, Qt.LeftButton, Qt.NoModifier)
    QApplication.sendEvent(_rechts(window), ev)
    assert gesehen == [[ref]]


def test_text_and_remote_urls_are_refused_everywhere(window):
    from PySide6.QtCore import QUrl
    for ziel in (_links(window), _rechts(window)):
        assert not _drop(ziel, text="irgendein Text")
        assert not _drop(ziel, urls=[QUrl("http://example.org/a.img")])


def test_drops_elsewhere_are_refused(window):
    from PySide6.QtWidgets import QToolBar
    f = window
    f.show()
    ziele = [f.log_dock.text.viewport(), f.statusBar(), f.menuBar(),
             f.findChild(QToolBar), f.kopf, f.info_bar]
    for z in ziele:
        assert not _drop(z, text="x"), z
        assert not _drop(z, urls=[__file__]), z


def test_dialogs_and_editor_accept_no_drops(window, abbild):
    assert window.open_image(str(abbild))
    from PySide6.QtWidgets import QDialog, QLineEdit
    dlg = QDialog(window)
    zeile = QLineEdit(dlg)
    assert not _drop(zeile, text="x")
    assert not _drop(dlg, urls=[__file__])
    window.open_disk_editor()
    editor = window._disk_editor if hasattr(window, "_disk_editor") else None
    if editor is not None:
        assert not _drop(editor, urls=[__file__])

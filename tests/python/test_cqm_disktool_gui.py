"""CopyQM (.cqm) und tar-Disketten im k1520DiskTool (PySide6, headless).

Geprueft wird die Verdrahtung: Filter, Schreiben/Speichern (.cqm wie .img), Ablehnungs-
meldungen, Schreibsperre der tar-Disketten samt Begruendung, Statuszeile,
Angaben-Dialog (Kommentar & Co.), tar-Satz (Reihenfolge, Oeffnen), Archiv.
"""

import struct
import zipfile

import pytest

from conftest import requires_disk
from cqm_bau import cqm

pytestmark = requires_disk

pytest.importorskip("PySide6")

DISK = 737280


def _kopf(name: str, daten: bytes) -> bytes:
    h = bytearray(512)
    h[0:len(name)] = name.encode()
    h[100:108] = b"%07o\0" % 0o644
    h[108:116] = b"%07o\0" % 25
    h[116:124] = b"%07o\0" % 1
    h[124:136] = b"%011o\0" % len(daten)
    h[136:148] = b"%011o\0" % 612345678
    h[156] = ord("0")
    h[148:156] = b"        "
    h[148:155] = b"%06o\0" % sum(h)
    h[155] = ord(" ")
    return bytes(h)


def _tar_roh() -> bytes:
    s = b""
    for name, daten in (("a.txt", b"Hallo\n"), ("dir/b.bin", bytes(range(200)))):
        s += _kopf(name, daten) + daten + bytes(-len(daten) % 512)
    return (s + bytes(1024)).ljust(DISK, b"\xe5")


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


@pytest.fixture
def cqm_tar(tmp_path):
    p = tmp_path / "ari.cqm"
    p.write_bytes(cqm(_tar_roh(), b"P8000 Lieferdiskette\nStand 1989"))
    return p


@pytest.fixture
def cqm_leer(tmp_path):
    from cqm_bau import leeres_720k
    p = tmp_path / "leer.cqm"
    p.write_bytes(leeres_720k(b"Leeres Testabbild"))
    return p


def test_open_filters_know_cqm_and_targets_do_too(window):
    assert "*.cqm" in window.OEFFNEN_FILTER
    import inspect
    from app.disktool.ui import main_window
    # .cqm ist seit dem Schreiben im Kern auch Ziel von Neu und Speichern unter
    for dialog in (main_window.MainWindow._neu_dialog,
                   main_window.MainWindow._speichern_unter_dialog):
        assert "*.cqm" in inspect.getsource(dialog)


@pytest.fixture
def cqm_cpm(tmp_path):
    """Eine echte, beschreibbare .cqm mit CP/M-Dateisystem (im Kern angelegt)."""
    from app.core_binding.k1520disk import DiskTool
    p = tmp_path / "arbeit.cqm"
    DiskTool.create(p, "cpa800", "TEST").close()
    return p


def test_cqm_is_not_locked_and_has_no_forced_read_only(window, cqm_leer):
    assert window.open_image(str(cqm_leer))
    t = window.tool
    assert t.ist_cqm and not t.read_only_forced
    assert "CopyQM" not in window.st_schutz.text()
    from app.disktool.ui.schreibsperre import sperrgrund
    assert sperrgrund(t) == ""


def test_cqm_with_filesystem_can_be_changed_saved_and_reloaded(window, cqm_cpm, tmp_path):
    assert window.open_image(str(cqm_cpm))
    window.set_read_only(False)
    assert window.act_schreibschutz.isEnabled()
    assert window.act_speichern.isEnabled()
    assert "Gesperrt" not in window.act_speichern.toolTip()
    quelle = tmp_path / "HALLO.TXT"
    quelle.write_bytes(b"Hallo CopyQM\r\n" * 50)
    window.tool.insert(quelle, "HALLO.TXT")
    assert window.save()
    window._close_tool()
    assert window.open_image(str(cqm_cpm))
    assert window.tool.ist_cqm
    assert [e.name for e in window.tool.list()] == ["HALLO.TXT"]
    ziel = tmp_path / "zurueck.txt"
    window.tool.extract("HALLO.TXT", ziel)
    assert ziel.read_bytes().startswith(b"Hallo CopyQM")
    # Loeschen und Wiederherstellen des Schreibschutz-Schalters wie bei .img
    window.set_read_only(False)
    window.tool.erase("HALLO.TXT")
    assert window.save()
    assert window.tool.list() == []


def test_cqm_comment_survives_writing_back(window, tmp_path):
    from app.core_binding.k1520disk import DiskTool
    p = tmp_path / "k.cqm"
    DiskTool.create(p, "cpa800", "K").close()
    # Kommentar der Vorlage: Datei mit eigenem Kopf nachbauen ist Sache des Kerns
    # (CqmWrite.KopffelderBleibenErhalten); hier zaehlt, dass der Standardkommentar bleibt.
    assert window.open_image(str(p))
    vorher = window.tool.container_info().get("comment")
    window.set_read_only(False)
    window.tool.insert(__file__, "T.PY")
    assert window.save()
    window._close_tool()
    assert window.open_image(str(p))
    assert window.tool.container_info().get("comment") == vorher


def test_save_as_cqm_works_for_a_uniform_disk(window, cqm_cpm, tmp_path):
    assert window.open_image(str(cqm_cpm))
    ziel = tmp_path / "zweite.cqm"
    assert window.save_as(ziel) and ziel.exists()
    assert window.tool.ist_cqm


def test_save_as_cqm_of_mixed_geometry_gives_the_reason(window, tmp_path, monkeypatch,
                                                         fixture_disks):
    """CP/A-Bootdiskette (cpa780: 128-B-Systemspuren): als .cqm nicht darstellbar -
    verstaendliche Meldung, Datei unveraendert, die offene Diskette bleibt."""
    import shutil
    from PySide6.QtWidgets import QMessageBox
    meldungen = []
    monkeypatch.setattr(QMessageBox, "critical",
                        staticmethod(lambda *a, **k: meldungen.append(a[2])))
    quelle = tmp_path / "boot.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", quelle)
    assert window.open_image(str(quelle))
    assert not window.save_as(tmp_path / "boot.cqm")
    assert meldungen and "gemischten Formate" in meldungen[-1], meldungen
    assert not (tmp_path / "boot.cqm").exists()
    assert window.tool.path == str(quelle)


def test_udos_to_cqm_is_refused_before_the_core(window, tmp_path, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    meldungen = []
    monkeypatch.setattr(QMessageBox, "critical",
                        staticmethod(lambda *a, **k: meldungen.append(a[2])))
    from app.disktool.ui.main_window import MainWindow
    grund = MainWindow._container_grund(tmp_path / "x.cqm", "udos_ds77")
    assert "Sektorkontrollblöcke von UDOS" in grund
    assert MainWindow._container_grund(tmp_path / "x.hfe", "udos_ds77") == ""
    assert MainWindow._container_grund(tmp_path / "x.cqm", "udos1715") == ""
    assert MainWindow._container_grund(tmp_path / "x.cqm", "cpa800") == ""


def test_create_cqm_with_udos_or_mixed_format_gives_a_reason(window, tmp_path, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    meldungen = []
    monkeypatch.setattr(QMessageBox, "critical",
                        staticmethod(lambda *a, **k: meldungen.append(a[2])))
    assert not window.create_disk(tmp_path / "m.cqm", "cpa780")
    assert meldungen and "gemischten Formate" in meldungen[-1], meldungen
    assert not (tmp_path / "m.cqm").exists()
    assert window.create_disk(tmp_path / "ok.cqm", "cpa800", "OK")
    assert window.tool.ist_cqm and not window.tool.read_only


def test_cqm_info_has_no_mount_hints(window, cqm_leer):
    """Hinweise zu UDOS/Bootdisketten gehoeren in den Emulator, nicht ins DiskTool."""
    assert window.open_image(str(cqm_leer))
    assert "mit UDOS nicht verwendbar" not in window.protokoll.toPlainText()
    assert "Bootdisketten ungeeignet" not in window.protokoll.toPlainText()


def test_tar_disk_shows_friendly_name_and_is_locked(window, tmp_path):
    img = tmp_path / "t.img"
    img.write_bytes(_tar_roh())
    window.open_image(str(img))
    t = window.tool
    assert t.ist_tar and t.filesystem == "tar720"
    assert "tar-Archiv (UNIX)" in window.kopf.angaben.text()
    assert "tar720" not in window.kopf.angaben.text()
    assert "tar-Archiv" in window.st_schutz.text()
    assert "Archivstrom" in window.act_loeschen.toolTip()
    from app.disktool.ui.disk_info_dialog import angaben_text
    assert "tar-Archiv (UNIX)" in angaben_text(t)


def test_cqm_info_dialog_shows_comment_and_header(window, cqm_tar):
    window.open_image(str(cqm_tar))
    from app.disktool.ui.disk_info_dialog import angaben_text
    text = angaben_text(window.tool)
    for erwartet in ("CopyQM", "720K Double-Sided", "Interleave", "Skew",
                     "Prüfwert", "ok", "Kommentar:", "P8000 Lieferdiskette",
                     "Stand 1989", "Nur lesbar"):
        assert erwartet in text, erwartet
    # Etikett "** NONE **" gilt als keines
    assert "Etikett" not in text


def test_comment_is_tooltip_at_the_file_name(window, cqm_tar):
    window.open_image(str(cqm_tar))
    assert "P8000 Lieferdiskette" in window.kopf.pfad.toolTip()
    window._close_tool()
    window.kopf.leeren()
    assert window.kopf.pfad.toolTip() == ""


def test_plain_img_is_not_locked(window, tmp_path, fixture_disks):
    import shutil
    ziel = tmp_path / "c.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", ziel)
    window.open_image(str(ziel))
    window.set_read_only(False)
    assert window.act_schreibschutz.isEnabled()
    assert "Gesperrt" not in window.act_speichern.toolTip()
    assert window.tool.container_info() == {"type": "img"}
    assert not window.tool.read_only_forced


def test_tar_disk_stays_locked_even_inside_a_cqm(window, cqm_tar):
    assert window.open_image(str(cqm_tar))
    t = window.tool
    assert t.ist_tar and t.ist_cqm and t.read_only_forced
    assert "tar-Archiv" in window.st_schutz.text()
    assert "Archivstrom" in window.act_speichern.toolTip()
    assert not window.act_schreibschutz.isEnabled()
    # Konvertieren nach .hfe geht weiterhin
    assert window.save_as(window_ziel(cqm_tar, "x.hfe"))


def window_ziel(quelle, name):
    return quelle.parent / name


def test_tar_satz_order_proposal_sorts_numbers_naturally():
    from app.disktool.ui.tar_satz_dialog import reihenfolge_vorschlag
    namen = ["/x/ariane10.cqm", "/x/ariane2.cqm", "/x/ariane1.cqm", "/x/ariane3.cqm"]
    assert [p.rsplit("/", 1)[1] for p in reihenfolge_vorschlag(namen)] == \
        ["ariane1.cqm", "ariane2.cqm", "ariane3.cqm", "ariane10.cqm"]


def test_tar_satz_dialog_moves_entries(qt_app):
    from app.disktool.ui.tar_satz_dialog import TarSatzDialog
    d = TarSatzDialog(["/x/b2.cqm", "/x/b1.cqm", "/x/b3.cqm"])
    assert [p[-6:] for p in d.pfade()] == ["b1.cqm", "b2.cqm", "b3.cqm"]
    d.liste.setCurrentRow(0)
    d.b_ab.click()
    assert [p[-6:] for p in d.pfade()] == ["b2.cqm", "b1.cqm", "b3.cqm"]
    d.b_auf.click()
    assert [p[-6:] for p in d.pfade()] == ["b1.cqm", "b2.cqm", "b3.cqm"]
    d.liste.setCurrentRow(0)
    d.b_auf.click()                     # am Rand: nichts
    assert [p[-6:] for p in d.pfade()] == ["b1.cqm", "b2.cqm", "b3.cqm"]


def _strom_teil(strom: bytes, nr: int, tmp_path):
    """Teil *nr* eines tar-Stroms, der auf zwei 720K-Abbilder verteilt ist."""
    teil = strom[nr * DISK:(nr + 1) * DISK].ljust(DISK, b"\xe5")
    p = tmp_path / f"satz{nr + 1}.cqm"
    p.write_bytes(cqm(teil, f"Teil {nr + 1}".encode()))
    return p


def test_tar_satz_opens_split_stream_as_one(window, tmp_path):
    gross = bytes(range(256)) * (DISK // 256 + 100)          # laenger als eine Diskette
    strom = _kopf("gross.bin", gross) + gross + bytes(-len(gross) % 512) + bytes(1024)
    teile = [_strom_teil(strom, i, tmp_path) for i in range(2)]

    # Einzeln: die zweite Diskette beginnt mitten in einer Datei, die erste endet abgeschnitten
    window.open_image(str(teile[0]))
    assert any(f.id == "tar.abgeschnitten" for f in window.tool.findings().findings)
    protokoll = window.protokoll.toPlainText()
    assert "tar.abgeschnitten" in protokoll
    assert "tar-Satz öffnen" in protokoll             # lesbarer Verweis statt `tar-satz`

    # Als Satz: eine vollstaendige Datei
    assert window.open_image(str(teile[0]), satz=[str(p) for p in teile])
    namen = [e.name for e in window.tool.list()]
    assert "gross.bin" in namen
    e = next(x for x in window.tool.list() if x.name == "gross.bin")
    assert e.size == len(gross) and not e.damaged
    assert window.tool.read_only_forced
    assert "tar-Satz aus 2 Abbildern" in window.kopf.pfad.text()


def test_tar_satz_action_exists_without_shortcut(window):
    assert window.act_tar_satz.shortcut().isEmpty()
    assert "tar-" in window.act_tar_satz.text()


def test_archive_of_a_cqm_records_the_source(cqm_tar, tmp_path):
    pytest.importorskip("yaml")
    import hashlib
    import yaml
    from app.core_binding.k1520disk import DiskTool
    from app.disktool.archive import INVENTAR_DATEI, create_archive
    ziel = tmp_path / "a.zip"
    with DiskTool.open(cqm_tar) as d:
        create_archive(d, ziel)
    with zipfile.ZipFile(ziel) as z:
        namen = z.namelist()
        daten = yaml.safe_load(z.read(INVENTAR_DATEI))
        txt = z.read([n for n in namen if n.endswith(".txt")][0]).decode()
    assert any(n.endswith(".hfe") for n in namen)          # die Manier: Abbild als .hfe
    assert daten["source"] == "ari.cqm"
    sc = daten["source_container"]
    assert sc["type"] == "cqm" and sc["crc"] == "ok"
    assert sc["comment"].startswith("P8000 Lieferdiskette")
    assert sc["sha256"] == hashlib.sha256(cqm_tar.read_bytes()).hexdigest()
    assert "CopyQM" in txt and "P8000 Lieferdiskette" in txt


def test_archive_of_a_plain_image_has_no_source_container(tmp_path, fixture_disks):
    pytest.importorskip("yaml")
    import yaml
    from app.core_binding.k1520disk import DiskTool
    from app.disktool.archive import INVENTAR_DATEI, create_archive
    ziel = tmp_path / "a.zip"
    with DiskTool.open(fixture_disks / "cpa_cpa780_k5601_clock.img") as d:
        create_archive(d, ziel)
    with zipfile.ZipFile(ziel) as z:
        assert "source_container" not in yaml.safe_load(z.read(INVENTAR_DATEI))


def test_cqm_wrong_crc_still_opens_but_reports(window, tmp_path):
    """Sicherheitsnetz fuer den Pruefwert: ein kaputtes Abbild wird abgewiesen oder
    gemeldet — nie still als heil ausgegeben."""
    roh = bytearray(_tar_roh())
    datei = bytearray(cqm(bytes(roh), b"x"))
    datei[0x5C:0x60] = struct.pack("<I", 0x12345678)
    datei[0x84] = (-sum(datei[:0x84])) & 0xFF
    p = tmp_path / "kaputt.cqm"
    p.write_bytes(bytes(datei))
    geoeffnet = window.open_image(str(p))
    if geoeffnet:
        assert window.tool.container_info()["crc"] == "falsch"

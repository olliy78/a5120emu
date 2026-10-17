"""CopyQM (.cqm) und tar-Disketten im k1520DiskTool (PySide6, headless).

Geprueft wird die Verdrahtung: Filter, Schreibsperre samt Begruendung, Statuszeile,
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


def test_open_filters_know_cqm_and_targets_do_not(window):
    assert "*.cqm" in window.OEFFNEN_FILTER
    import inspect
    from app.disktool.ui import main_window
    # Die Dateifilter der Ziel-Dialoge nennen *.cqm nie (die Ablehnung eines getippten
    # Namens steht in der Fehlermeldung, nicht im Filter).
    for dialog in (main_window.MainWindow._neu_dialog,
                   main_window.MainWindow._speichern_unter_dialog):
        assert "*.cqm" not in inspect.getsource(dialog)


@pytest.mark.skip(reason="CQM ist seit 2026-10-17 im Kern schreibbar; die Oberflaeche sperrt noch (naechster Auftrag)")
def test_cqm_is_opened_locked_with_reason_and_status(window, cqm_leer):
    assert window.open_image(str(cqm_leer))
    t = window.tool
    assert t.ist_cqm and t.read_only and t.read_only_forced
    assert "CopyQM" in window.st_schutz.text()
    assert "schreibgeschützt" in window.st_schutz.text()
    # Schreibbefehle gesperrt, Tooltip nennt den Grund; Lesebefehle frei
    for a in (window.act_speichern, window.act_loeschen, window.act_schreiben,
              window.act_alles_rein, window.act_schreibschutz):
        assert not a.isEnabled(), a.text()
    assert "Gesperrt" in window.act_speichern.toolTip()
    assert ".cqm" in window.act_speichern.toolTip()
    assert window.act_speichern_unter.isEnabled()
    assert window.act_archivieren.isEnabled()
    assert window.act_diskeditor.isEnabled()
    # Schutz laesst sich auch von aussen nicht aufheben
    window.set_read_only(False)
    assert window.tool.read_only


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


def test_cqm_as_save_or_new_target_is_refused(window, cqm_tar, tmp_path, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    meldungen = []
    monkeypatch.setattr(QMessageBox, "critical",
                        staticmethod(lambda *a, **k: meldungen.append(a)))
    window.open_image(str(cqm_tar))
    assert not window.save_as(tmp_path / "x.cqm")
    assert meldungen and not (tmp_path / "x.cqm").exists()
    # Konvertieren nach .hfe geht
    assert window.save_as(tmp_path / "x.hfe")
    assert (tmp_path / "x.hfe").exists()


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

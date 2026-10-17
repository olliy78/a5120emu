"""Archiv: Herkunft (`provenance`) und Beschreibungsdateien (`beilagen`) ohne Skript —
Kommandozeile (`python3 -m app.disktool.archive`), Archiv-Dialog und die Pruefregeln
in `app/disktool/archiv_meta.py` (offscreen)."""

import zipfile

import pytest
import yaml

from conftest import requires_disk

pytestmark = requires_disk

pytest.importorskip("PySide6")

DISK = "cpa_cpa780_k5601_clock.img"


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


def _cli(args):
    from app.disktool.archive import main
    return main([str(a) for a in args])


def _inventar(zp):
    from app.disktool.archive import INVENTAR_DATEI
    with zipfile.ZipFile(zp) as z:
        return z.namelist(), yaml.safe_load(z.read(INVENTAR_DATEI))


def _inhalte(zp):
    """Namen und Bytes aller Mitglieder (ohne Zeitstempel)."""
    import re
    with zipfile.ZipFile(zp) as z:
        aus = {n: z.read(n) for n in z.namelist()}
    # Erstellzeit (Sekunden bzw. Minute) ist das einzig Fluechtige.
    for n, b in aus.items():
        aus[n] = re.sub(rb"(created: |Archiviert +)[^\r\n]*", rb"\1-", b)
    return aus


# ─── Kommandozeile ───────────────────────────────────────────────────────────

def test_cli_gueltig(fixture_disks, tmp_path):
    meta = tmp_path / "meta.yaml"
    meta.write_text("source_url: http://example.invalid/\nset_name: Datei\n"
                    "version: 1\nhinweise: [eins]\n", encoding="utf-8")
    text = tmp_path / "info.txt"
    text.write_text("Beschreibung ä\n", encoding="utf-8")
    ziel = tmp_path / "a.zip"
    rc = _cli([fixture_disks / DISK, ziel, "--bezeichnung", "T1",
               "--herkunft-datei", meta, "--herkunft", "version=2.0",
               "--herkunft", "hinweise=zwei", "--herkunft", "eigen_x=y",
               "--beilage", f"{text}=beschr.txt"])
    assert rc == 0
    namen, inv = _inventar(ziel)
    assert inv["provenance"] == {"source_url": "http://example.invalid/",
                                 "set_name": "Datei", "version": "2.0",
                                 "hinweise": ["eins", "zwei"], "eigen_x": "y"}
    assert "beschr.txt" in namen
    assert [a["path"] for a in inv["attachments"] if a["path"] == "beschr.txt"]
    with zipfile.ZipFile(ziel) as z:
        assert z.read("beschr.txt").decode("utf-8") == "Beschreibung ä\n"
        assert "HERKUNFT / BESCHREIBUNG" in z.read("T1.txt").decode("utf-8")


def test_cli_ohne_angaben_bleibt_bitgleich(fixture_disks, tmp_path):
    a, b = tmp_path / "a.zip", tmp_path / "b.zip"
    assert _cli([fixture_disks / DISK, a, "X"]) == 0
    assert _cli([fixture_disks / DISK, b, "--bezeichnung", "X"]) == 0
    assert _inhalte(a) == _inhalte(b)
    assert "provenance" not in _inventar(a)[1]


@pytest.mark.parametrize("name", ["diskarchive.yaml", "dateien", "dateien/x.txt",
                                  "../x.txt", "a/b.txt", "T1.txt", "T1.hfe",
                                  "/abs.txt", "a\\b.txt"])
def test_cli_lehnt_unzulaessige_beilagennamen_ab(fixture_disks, tmp_path, name, capsys):
    t = tmp_path / "t.txt"
    t.write_text("x", encoding="utf-8")
    ziel = tmp_path / "a.zip"
    assert _cli([fixture_disks / DISK, ziel, "--bezeichnung", "T1",
                 "--beilage", f"{t}={name}"]) == 2
    assert "Fehler" in capsys.readouterr().err
    assert not ziel.exists()


def test_cli_doppelte_beilage_binaer_gross_schluessel(fixture_disks, tmp_path, capsys):
    t = tmp_path / "t.txt"
    t.write_text("x", encoding="utf-8")
    z = tmp_path / "a.zip"
    basis = [fixture_disks / DISK, z]
    assert _cli(basis + ["--beilage", t, "--beilage", t]) == 2          # doppelt
    b = tmp_path / "b.bin"
    b.write_bytes(b"MZ\x00\x01")
    assert _cli(basis + ["--beilage", b]) == 2                          # Binaerdatei
    g = tmp_path / "gross.txt"
    g.write_bytes(b"a" * (4 * 1024 * 1024 + 1))
    assert _cli(basis + ["--beilage", g]) == 2                          # zu gross
    assert _cli(basis + ["--herkunft", "Gross=1"]) == 2                 # Schluessel
    assert _cli(basis + ["--herkunft", "ohnegleich"]) == 2
    assert _cli(basis + ["--herkunft-datei", tmp_path / "gibtsnicht.yaml"]) == 2
    assert _cli(basis + ["--beilage", tmp_path / "gibtsnicht.txt"]) == 2
    assert not z.exists()
    assert capsys.readouterr().err.count("Fehler") == 7


def test_cli_latin1_wird_nach_utf8_uebernommen(fixture_disks, tmp_path):
    t = tmp_path / "l1.txt"
    t.write_bytes("Grüße\r\n".encode("latin-1"))
    ziel = tmp_path / "a.zip"
    assert _cli([fixture_disks / DISK, ziel, "--beilage", t]) == 0
    with zipfile.ZipFile(ziel) as z:
        assert z.read("l1.txt") == "Grüße\r\n".encode("utf-8")
    # ausdrueckliche Kodierung; falsche Wahl wird gemeldet statt geraten
    t2 = tmp_path / "u16.txt"
    t2.write_bytes("ä".encode("utf-16"))
    ziel2 = tmp_path / "b.zip"
    assert _cli([fixture_disks / DISK, ziel2, "--beilage", t2,
                 "--beilage-kodierung", "utf-16"]) == 0
    assert _cli([fixture_disks / DISK, tmp_path / "c.zip", "--beilage", t,
                 "--beilage-kodierung", "ascii"]) == 2


def test_cli_herkunft_wird_nicht_ueberschrieben_ohne_force(fixture_disks, tmp_path):
    ziel = tmp_path / "a.zip"
    assert _cli([fixture_disks / DISK, ziel]) == 0
    assert _cli([fixture_disks / DISK, ziel]) == 2
    assert _cli([fixture_disks / DISK, ziel, "--force"]) == 0


def test_beilage_gelangt_nie_auf_die_diskette(fixture_disks, temp_disk, tmp_path):
    """Rundlauf: Archiv entpacken, `dateien/` einfuegen - die Beilage fehlt auf der Diskette."""
    from app.core_binding.k1520disk import DiskTool
    t = tmp_path / "BESCHR.TXT"
    t.write_text("nur im Archiv\n", encoding="utf-8")
    ziel = tmp_path / "a.zip"
    assert _cli([fixture_disks / DISK, ziel, "--beilage", t]) == 0
    aus = tmp_path / "aus"
    with zipfile.ZipFile(ziel) as z:
        z.extractall(aus)
    assert (aus / "BESCHR.TXT").is_file()
    assert not (aus / "dateien" / "BESCHR.TXT").exists()

    kopie = temp_disk(DISK)
    with DiskTool.open(kopie, read_only=False) as d:
        d.insert_all(aus / "dateien")
        assert "BESCHR.TXT" not in {e.name.upper() for e in d.list()}


# ─── Dialog ──────────────────────────────────────────────────────────────────

def _dialog(window, fixture_disks, einst=None):
    from app.disktool.ui.archiv_dialog import ArchivDialog
    assert window.open_image(fixture_disks / DISK)
    return ArchivDialog("Etikett", window, einst, stamm_fn=lambda b: "Etikett")


def test_dialog_unberuehrt_ist_bitgleich(window, fixture_disks, tmp_path):
    dlg = _dialog(window, fixture_disks)
    assert dlg.provenance() == {} and dlg.beilagen() == {}
    a, b = tmp_path / "a.zip", tmp_path / "b.zip"
    assert window.archive(a, bezeichnung="Etikett")
    assert window.archive(b, bezeichnung=dlg.bezeichnung(),
                          provenance=dlg.provenance(), beilagen=dlg.beilagen())
    assert _inhalte(a) == _inhalte(b)


def test_dialog_felder_landen_im_archiv(window, fixture_disks, tmp_path):
    dlg = _dialog(window, fixture_disks)
    dlg.felder["source_url"].setText("http://example.invalid/")
    dlg.felder["set_name"].setText("Satz A")
    dlg.felder["version"].setText("1.2")
    dlg.felder["web_label"].setText("")                       # leer: kein Schluessel
    dlg.f_note.setPlainText("Zeile 1\nZeile 2")
    dlg._zeile("eigen", "wert")
    dlg._zeile("", "")                                         # leere Zeile faellt weg
    t = tmp_path / "b.txt"
    t.write_bytes("Grüße".encode("latin-1"))
    assert dlg.beilage_hinzufuegen(t)
    dlg._vorschau(0)
    assert "Grüße" in dlg.vorschau.toPlainText()

    ziel = tmp_path / "a.zip"
    assert window.archive(ziel, bezeichnung="Etikett", provenance=dlg.provenance(),
                          beilagen=dlg.beilagen())
    namen, inv = _inventar(ziel)
    assert inv["provenance"] == {"source_url": "http://example.invalid/",
                                 "set_name": "Satz A", "version": "1.2",
                                 "note": "Zeile 1\nZeile 2", "eigen": "wert"}
    assert "b.txt" in namen and not any(n.startswith("dateien/b") for n in namen)


def test_dialog_fehler_stehen_im_dialog(window, fixture_disks, tmp_path):
    from PySide6.QtWidgets import QDialog
    dlg = _dialog(window, fixture_disks)
    dlg._zeile("Gross", "x")
    dlg.accept()
    assert dlg.result() != QDialog.Accepted and "chluessel" in dlg.fehler.text()
    dlg.tabelle.setRowCount(0)
    dlg._zeile("version", "x")                                 # festes Feld
    dlg.accept()
    assert dlg.result() != QDialog.Accepted and "festes Feld" in dlg.fehler.text()
    dlg.tabelle.setRowCount(0)
    b = tmp_path / "x.bin"
    b.write_bytes(b"\x00\x01")
    assert not dlg.beilage_hinzufuegen(b) and "Textdatei" in dlg.fehler.text()
    kol = tmp_path / "Etikett.txt"                              # Name des Archivs selbst
    kol.write_text("x", encoding="utf-8")
    assert not dlg.beilage_hinzufuegen(kol) and "kollidiert" in dlg.fehler.text()


def test_dialog_merkt_werte_ausser_beilagen(window, fixture_disks, tmp_path):
    from PySide6.QtCore import QSettings
    from PySide6.QtWidgets import QDialog
    QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, str(tmp_path / "cfg"))
    einst = QSettings(str(tmp_path / "t.ini"), QSettings.IniFormat)
    dlg = _dialog(window, fixture_disks, einst)
    dlg.felder["set_name"].setText("Merk")
    dlg._zeile("k", "v")
    t = tmp_path / "b.txt"
    t.write_text("x", encoding="utf-8")
    dlg.beilage_hinzufuegen(t)
    dlg.accept()
    assert dlg.result() == QDialog.Accepted

    neu = _dialog(window, fixture_disks, einst)
    assert neu.felder["set_name"].text() == "Merk" and neu.b_auf.isChecked()
    assert neu.provenance() == {"set_name": "Merk", "k": "v"}
    assert neu.beilagen() == {}
    neu.auf_leer()
    assert neu.provenance() == {}
    assert _dialog(window, fixture_disks, einst).provenance() == {}


def test_cqm_quelle_und_eigene_angaben_stehen_nebeneinander(window, tmp_path):
    import cqm_bau
    pfad = tmp_path / "q.cqm"
    pfad.write_bytes(cqm_bau.leeres_720k())
    assert window.open_image(pfad)
    ziel = tmp_path / "a.zip"
    assert window.archive(ziel, bezeichnung="Q", provenance={"set_name": "S"})
    _, inv = _inventar(ziel)
    assert inv["provenance"] == {"set_name": "S"}
    assert inv["source_container"]["type"] == "cqm" or "description" in inv["source_container"]


# ─── Pruefregeln und Sammlungsskript ─────────────────────────────────────────

def test_pofo_skript_benutzt_dieselben_regeln():
    import sys
    from pathlib import Path
    from app.disktool import archiv_meta
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root / "tools" / "p8000"))
    try:
        import pofo_archive
    finally:
        sys.path.pop(0)
    assert pofo_archive.create_archive.__module__ == "app.disktool.archive"
    # Unterordner (dateien_fat/...) sind dort erlaubt, `..` und dateien/ nie
    archiv_meta.pruefe_beilagen({"dateien_fat/A.TXT": b"x"}, unterordner=True)
    for schlecht in ("../x", "dateien/x", "a//b"):
        with pytest.raises(archiv_meta.ArchivMetaFehler):
            archiv_meta.pruefe_beilagen({schlecht: "x"}, unterordner=True)

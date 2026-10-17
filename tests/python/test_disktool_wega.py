"""WEGA-(UNIX-)Disketten im DiskTool: Eigenschaften, Archiv, `.fileinfo`.

Der Fehler, den dieses Modul haelt: das DiskTool unterschied nur „CP/M oder nicht" und
behandelte jede Diskette mit gefuelltem Typfeld wie UDOS.  Bei WEGA steht dort aber
eine Dateiart (`-`, `d`, `c`), und die Datei traegt eine Inode (Modus, uid, gid,
Zeiten) statt eines Kopfsektors.  Alle Faelle laufen an einer **selbst angelegten**
WEGA-Diskette — kein Fremdabbild noetig.
"""

import hashlib
import zipfile

import pytest

from conftest import requires_disk

pytestmark = requires_disk

pytest.importorskip("PySide6")
yaml = pytest.importorskip("yaml")

STAND = 400000000          # fester Zeitstempel (Sekunden seit 1970, GMT)


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
def wega_disk(tmp_path):
    """Eine WEGA-Diskette mit Programm, Konfigurationsdatei und Verzeichnissen."""
    from app.core_binding.k1520disk import DiskTool

    quelle = tmp_path / "quelle"
    (quelle / "bin").mkdir(parents=True)
    (quelle / "etc").mkdir()
    (quelle / "usr" / "leer").mkdir(parents=True)
    (quelle / "bin" / "hallo").write_bytes(b"echo hallo\n")
    (quelle / "etc" / "passwd").write_bytes(b"root::0:0::/:/bin/sh\n")
    (quelle / "usr" / "daten").write_bytes(bytes(range(256)) * 12)

    pfad = tmp_path / "wega.img"
    with DiskTool.create(pfad, "wega720", "probe") as d:
        d.insert_all(quelle)
        d.set_unix_attrs("bin/hallo", mode=0o4711, uid=5, gid=6, mtime=STAND)
        d.set_unix_attrs("etc/passwd", mode=0o600, uid=0, gid=3, mtime=STAND + 1)
        d.set_unix_attrs("usr", mode=0o750, uid=9, gid=8, mtime=STAND + 2)
        d.flush()
    return pfad


def _eintrag(tool, name):
    return next(e for e in tool.list() if e.name == name)


# ─── Die Art kommt aus dem Dateisystem ───────────────────────────────────────

def test_filesystem_family_is_asked_not_guessed(wega_disk):
    from app.core_binding.k1520disk import DiskTool
    with DiskTool.open(wega_disk) as d:
        assert d.filesystem_type == "wega"
        assert d.ist_wega and not d.ist_udos and not d.ist_cpm
        e = _eintrag(d, "bin/hallo")
        # Genau das, woran das alte Werkzeug scheiterte: ein gefuelltes Typfeld.
        assert e.type == "-"
        assert e.ist_unix
        assert (e.unix_mode, e.unix_uid, e.unix_gid) == (0o104711, 5, 6)
        assert e.unix_mtime == STAND and e.unix_nlink == 1 and e.unix_inode > 2
        assert e.attrs == "-rws--x--x"
        assert _eintrag(d, "usr").type == "d"


def test_unix_attrs_are_validated_and_leave_the_disk_alone_on_error(wega_disk):
    from app.core_binding.k1520disk import DiskTool, K1520DiskError
    with DiskTool.open(wega_disk, read_only=False) as d:
        with pytest.raises(K1520DiskError, match="07777"):
            d.set_unix_attrs("bin/hallo", mode=0o170755)
        with pytest.raises(K1520DiskError, match="uid"):
            d.set_unix_attrs("bin/hallo", uid=70000)
        with pytest.raises(K1520DiskError, match="nicht gefunden"):
            d.set_unix_attrs("gibt/es/nicht", mode=0o644)
        e = _eintrag(d, "bin/hallo")
        assert (e.unix_mode, e.unix_uid) == (0o104711, 5)
        assert d.unix_blocks("bin/hallo")             # Blockliste: mindestens ein Block
        assert d.unix_blocks("gibt/es/nicht") == ""


def test_unix_attrs_refused_on_a_non_wega_disk(tmp_path):
    from app.core_binding.k1520disk import DiskTool, K1520DiskError
    with DiskTool.create(tmp_path / "cpm.img", "cpa780", "x") as d:
        with pytest.raises(K1520DiskError, match="UNIX"):
            d.set_unix_attrs("X.COM", mode=0o755)


# ─── Eigenschaften-Dialog ────────────────────────────────────────────────────

def _dialog(window, name):
    from app.disktool.ui.properties_dialog import PropertiesDialog
    return PropertiesDialog(window.tool, _eintrag(window.tool, name), window)


def test_dialog_shows_only_unix_fields(window, wega_disk):
    assert window.open_image(wega_disk)
    d = _dialog(window, "bin/hallo")

    assert d.wega and not d.udos and d.art == "wega"
    assert d.f_oktal.text() == "4711"
    assert d.f_uid.value() == 5 and d.f_gid.value() == 6
    assert d.f_mtime.text() == "1982-09-04 15:06:40"
    assert d.f_rechte[(6, 1)].isChecked() and not d.f_rechte[(3, 4)].isChecked()
    assert d.f_sonder[0o4000].isChecked() and not d.f_sonder[0o2000].isChecked()
    # Kein einziges UDOS-/CP/M-Feld
    for udos_feld in ("f_typ", "f_eig", "f_entry", "f_low", "f_segs", "f_user", "f_ro"):
        assert not hasattr(d, udos_feld), udos_feld
    # Ohne Zutun darf nichts geschrieben werden.
    assert d.aenderungen() == {}


def test_dialog_edits_mode_owner_and_time_as_one_transaction(window, wega_disk):
    assert window.open_image(wega_disk)
    window.set_read_only(False)
    d = _dialog(window, "etc/passwd")

    # Kaestchen und Oktalfeld halten einander fest
    d.f_rechte[(0, 4)].setChecked(True)                    # Andere: lesen
    assert d.f_oktal.text() == "0604"
    assert d.l_modus.text() == "-rw----r--"
    d.f_oktal.setText("0755")
    d.f_oktal.textEdited.emit("0755")
    assert d.f_rechte[(6, 1)].isChecked() and d.f_rechte[(0, 1)].isChecked()
    d.f_uid.setValue(12)
    d.f_gid.setValue(34)
    d.f_mtime.setText("2001-02-03 04:05:06")
    assert d.aenderungen() == {"mode": 0o755, "uid": 12, "gid": 34, "mtime": 981173106}
    assert d.uebernehmen()

    e = _eintrag(window.tool, "etc/passwd")
    assert (e.unix_mode, e.unix_uid, e.unix_gid, e.unix_mtime) == (
        0o100755, 12, 34, 981173106)
    # Dateiart, Inode, Groesse und Links sind Struktur und bleiben
    assert e.type == "-" and e.size == 21 and e.unix_nlink == 1


def test_dialog_rejects_bad_input_without_writing(window, wega_disk, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    gemeldet = []
    monkeypatch.setattr(QMessageBox, "warning", lambda *a, **k: gemeldet.append(a[2]))
    assert window.open_image(wega_disk)
    window.set_read_only(False)

    d = _dialog(window, "bin/hallo")
    d.f_oktal.setText("9")
    assert not d.uebernehmen() and "Oktalzahl" in gemeldet[-1]
    d.f_oktal.setText("17777")
    assert not d.uebernehmen() and "Dateiart" in gemeldet[-1]
    d.f_oktal.setText("4711")
    d.f_mtime.setText("gestern")
    assert not d.uebernehmen() and "Zeitpunkt" in gemeldet[-1]
    assert _eintrag(window.tool, "bin/hallo").unix_mode == 0o104711


def test_dialog_for_a_directory_and_read_only_disk(window, wega_disk):
    from PySide6.QtWidgets import QDialogButtonBox
    assert window.open_image(wega_disk)
    assert window.tool.read_only
    d = _dialog(window, "usr")
    assert d.wega and d.f_oktal.text() == "0750"
    assert not d.knoepfe.button(QDialogButtonBox.Save).isEnabled()
    assert "schreibgeschützt" in d.hinweis.text()


# ─── Archiv ──────────────────────────────────────────────────────────────────

def test_catalogue_is_ls_style_without_udos_terms(window, wega_disk, tmp_path):
    assert window.open_image(wega_disk)
    ziel = tmp_path / "archiv.zip"
    assert window.archive(ziel, bezeichnung="WEGA Probe")

    with zipfile.ZipFile(ziel) as z:
        text = z.read("WEGA_Probe.txt").decode("utf-8")

    assert "Modus" in text and "uid" in text and "gid" in text and "Links" in text
    assert "-rws--x--x" in text                                # ls -l-Modus
    assert "-rw-------" in text and "drwxr-x---" in text
    assert "0104711" in text                                   # Oktalmodus
    # Keine UDOS-Begriffe (Typ P1, Eigenschaften WELS, Satzlaenge, ENTRY …)
    for udos in ("UDOS", "PROCEDURE", "Satzlaenge", "ENTRY", "MEMORY PROTECT",
                 "Side0", "Nutzerbereich", "CP/M"):
        assert udos not in text, udos
    assert "LEGENDE" in text and "setuid" in text


def test_archive_inventory_carries_unix_keys_additively(window, wega_disk, tmp_path):
    from app.disktool.archive import INVENTAR_DATEI, INVENTAR_VERSION

    assert window.open_image(wega_disk)
    ziel = tmp_path / "archiv.zip"
    assert window.archive(ziel, bezeichnung="WEGA Probe")

    with zipfile.ZipFile(ziel) as z:
        namen = set(z.namelist())
        daten = yaml.safe_load(z.read(INVENTAR_DATEI))
        assert daten["diskarchive"] == INVENTAR_VERSION == 1     # additiv: Fassung bleibt
        je_pfad = {f["path"]: f for f in daten["files"]}
        hallo = je_pfad["dateien/bin/hallo"]
        assert hallo["unix"] == {"mode": "0104711", "uid": 5, "gid": 6, "mtime": STAND}
        assert hallo["dir"] == "bin" and hallo["name"] == "hallo"
        assert "disk_name" not in hallo, "der Pfad steht schon in dir/name"
        assert je_pfad["dateien/etc/passwd"]["unix"]["mode"] == "0100600"
        for f in daten["files"]:
            inhalt = z.read(f["path"])
            assert f["size"] == len(inhalt)
            assert f["sha256"] == hashlib.sha256(inhalt).hexdigest()
        # Verzeichnisse sind keine Dateien des Inventars
        assert not any(f["name"] in ("usr", "leer", "bin") for f in daten["files"])

        anhang = {a["path"] for a in daten["attachments"]}
        assert "dateien/wega-dateiangaben.txt" in anhang
        assert "dateien/usr.fileinfo" in anhang                  # Verzeichnisrechte
        assert "dateien/bin/hallo.fileinfo" in anhang
        assert "dateien/udos-dateiangaben.txt" not in anhang

        # Nichts faellt unter den Tisch: jedes Mitglied der .zip ist erklaert
        erklaert = (set(je_pfad) | anhang
                    | {daten["image"]["path"], daten["catalogue"]["path"], INVENTAR_DATEI})
        assert {n for n in namen if not n.endswith("/")} == erklaert

        fi = z.read("dateien/bin/hallo.fileinfo").decode("utf-8")
    assert "fs=wega" in fi and "mode=0104711" in fi and "uid=5" in fi
    assert "typ=" not in fi and "satz=" not in fi


def test_archive_roundtrip_restores_unix_attributes(window, wega_disk, tmp_path):
    """Archiv → Ordner → neue Diskette: Modus, uid, gid und mtime kommen zurueck."""
    from app.core_binding.k1520disk import DiskTool

    assert window.open_image(wega_disk)
    ziel = tmp_path / "archiv.zip"
    assert window.archive(ziel, bezeichnung="WEGA Probe")
    ausgepackt = tmp_path / "aus"
    with zipfile.ZipFile(ziel) as z:
        z.extractall(ausgepackt)

    neu = tmp_path / "neu.img"
    with DiskTool.create(neu, "wega720", "neu") as d:
        d.insert_all(ausgepackt / "dateien")
        for name in ("bin/hallo", "etc/passwd", "usr"):
            alt = _eintrag(window.tool, name)
            e = _eintrag(d, name)
            assert (e.unix_mode, e.unix_uid, e.unix_gid, e.unix_mtime) == (
                alt.unix_mode, alt.unix_uid, alt.unix_gid, alt.unix_mtime), name
        # Zubehoer kam nicht als Datei mit
        assert not any("fileinfo" in e.name or "dateiangaben" in e.name for e in d.list())


def test_extract_all_writes_unix_fileinfo_and_directory_folders(wega_disk, tmp_path):
    from app.core_binding.k1520disk import DiskTool

    ziel = tmp_path / "raus"
    with DiskTool.open(wega_disk) as d:
        d.extract_all(ziel)
    assert (ziel / "usr" / "leer").is_dir()
    assert (ziel / "usr.fileinfo").read_text().count("fs=wega") == 1
    assert (ziel / "bin" / "hallo").read_bytes() == b"echo hallo\n"
    assert "mode=0104711" in (ziel / "bin" / "hallo.fileinfo").read_text()
    bb = (ziel / "wega-dateiangaben.txt").read_text()
    assert "usr/leer art=d" in bb and "UDOS" not in bb

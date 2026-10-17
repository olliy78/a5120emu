"""Archiv: Herkunftsangaben (`provenance=`), Beiblaetter an der Wurzel (`beilagen=`) und
das Archiv ueber einen tar-Satz ohne Abbild (`ohne_abbild=True`).

Die tar-Disketten werden hier selbst gebaut (wie in test_disktool_tar.py)."""

import hashlib
import zipfile

import pytest

from conftest import requires_disk

pytestmark = requires_disk

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


def _strom(dateien) -> bytes:
    s = b""
    for name, daten in dateien:
        s += _kopf(name, daten) + daten + bytes(-len(daten) % 512)
    return s + bytes(1024)


def _lade(zp):
    import yaml
    from app.disktool.archive import INVENTAR_DATEI
    z = zipfile.ZipFile(zp)                  # der Aufrufer schliesst (`with z:`)
    return z, yaml.safe_load(z.read(INVENTAR_DATEI))


def test_provenance_und_beiblatt_landen_im_archiv(tmp_path):
    pytest.importorskip("yaml")
    from app.core_binding.k1520disk import DiskTool
    from app.disktool.archive import create_archive
    p = tmp_path / "t.img"
    p.write_bytes(_strom([("a.txt", b"Hallo\n")]).ljust(DISK, b"\xe5"))
    prov = {"source_url": "http://example.invalid/arch/", "set_name": "Testsatz",
            "set_id": 7, "version": "2.0", "web_label": "ETIKETT",
            "hinweise": ["Zeile eins", "Zeile zwei"]}
    notiz = "Beschreibung mit Umlaut ä\r\nzweite Zeile\n"
    ziel = tmp_path / "t.zip"
    with DiskTool.open(p) as d:
        create_archive(d, ziel, provenance=prov,
                       beilagen={"beschreibung_pofo_7.txt": notiz})
    z, daten = _lade(ziel)
    with z:
        assert daten["diskarchive"] == 1                      # nur additiv
        assert daten["provenance"] == prov
        bb = next(a for a in daten["attachments"] if a["path"] == "beschreibung_pofo_7.txt")
        roh = z.read("beschreibung_pofo_7.txt")
        assert roh == notiz.encode("utf-8")                   # Zeilenumbrueche unveraendert
        assert bb["size"] == len(roh) and bb["sha256"] == hashlib.sha256(roh).hexdigest()
        # Das Beiblatt gehoert NICHT unter `files` (sonst waere es eine „Fassung").
        assert all(f["name"] != "beschreibung_pofo_7.txt" for f in daten["files"])
        assert "image" in daten
        txt = z.read("t.txt").decode("utf-8")
    assert "HERKUNFT / BESCHREIBUNG" in txt
    assert "http://example.invalid/arch/" in txt and "Testsatz" in txt
    assert "  - Zeile zwei" in txt and "beschreibung_pofo_7.txt" in txt


def test_archiv_ohne_provenance_bleibt_wie_vorher(tmp_path):
    pytest.importorskip("yaml")
    from app.core_binding.k1520disk import DiskTool
    from app.disktool.archive import create_archive
    p = tmp_path / "t.img"
    p.write_bytes(_strom([("a.txt", b"Hallo\n")]).ljust(DISK, b"\xe5"))
    ziel = tmp_path / "t.zip"
    with DiskTool.open(p) as d:
        create_archive(d, ziel)
    z, daten = _lade(ziel)
    with z:
        assert "provenance" not in daten
        assert "HERKUNFT" not in z.read("t.txt").decode("utf-8")


def test_satzarchiv_ohne_abbild_hat_vollstaendige_dateien(tmp_path):
    pytest.importorskip("yaml")
    from app.core_binding.k1520disk import DiskTool
    from app.disktool.archive import create_archive
    gross = bytes((i * 7) & 0xFF for i in range(800_000))
    strom = _strom([("a.txt", b"Hallo\n"), ("big.bin", gross)])
    d1, d2 = tmp_path / "s1.img", tmp_path / "s2.img"
    d1.write_bytes(strom[:DISK])
    d2.write_bytes(strom[DISK:].ljust(DISK, b"\xe5"))
    ziel = tmp_path / "satz.zip"
    with DiskTool.open_tar_satz([d1, d2]) as d:
        create_archive(d, ziel, bezeichnung="satz", herkunft="tar-Satz aus 2 Abbildern",
                       provenance={"disks": ["s1.img", "s2.img"]}, ohne_abbild=True)
    z, daten = _lade(ziel)
    with z:
        assert not any(n.endswith(".hfe") for n in z.namelist())
        assert "image" not in daten and "source_container" not in daten
        assert daten["source"] == "tar-Satz aus 2 Abbildern"
        big = next(f for f in daten["files"] if f["name"] == "big.bin")
        assert big["size"] == len(gross) and "damaged" not in big
        assert big["sha256"] == hashlib.sha256(gross).hexdigest()
        txt = z.read("satz.txt").decode("utf-8")
    assert "kein Abbild" in txt or "ein Abbild liegt nicht bei" in txt


def test_cpm_name_mit_schraegstrich_bricht_das_archiv_nicht_ab(tmp_path, fixture_disks):
    """OS/M-Disketten tragen UNIX-Namen ("/Z/HOFFM", "GW/ZSID.DOC"): als Wirtsname waere das
    ein absoluter Pfad bzw. ein fehlender Unterordner.  Der Auszug macht '_' daraus."""
    pytest.importorskip("yaml")
    import shutil
    from app.core_binding.k1520disk import DiskTool
    from app.disktool.archive import create_archive
    img = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", img)
    roh = img.read_bytes()
    alt, neu = b"BRUN    COM", b"/B/N    COM"
    assert roh.count(alt) == 1
    img.write_bytes(roh.replace(alt, neu))
    ziel = tmp_path / "a.zip"
    with DiskTool.open(img) as d:
        assert any(e.name == "/B/N.COM" for e in d.list())
        create_archive(d, ziel)
    z, daten = _lade(ziel)
    with z:
        f = next(f for f in daten["files"] if f.get("disk_name") == "/B/N.COM")
        assert f["name"] == "_B_N.COM" and f["path"] in z.namelist()

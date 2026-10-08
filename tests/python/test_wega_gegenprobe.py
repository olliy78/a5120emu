"""Gegenprobe des WEGA-Schreibpfads (AP P17, ``doc/design/27_wega_dateisystem.md``).

Was ``libk1520disk`` auf eine WEGA-Diskette schreibt, liest hier ein ZWEITER,
unabhängiger Leser: die Python-Vorstufe ``tools/p8000/wega_s3fs.py`` (P14), die nur
den System-III-Aufbau kennt und mit dem C++-Code nichts teilt.  Liest sie dieselben
Bytes zurück, stimmen Superblock-, Inode- und Verzeichnislage — eine
Selbstbestätigung des Kerns wäre das nicht.

Die Vorstufe kann nur einfach indirekt (bis 69 KB) — größere Dateien prüft
``k1520_test_wega_fs`` gegen den Kern selbst.
"""

import importlib.util
import os

import pytest

from conftest import PROJECT_ROOT, requires_disk

pytestmark = requires_disk

_pfad = PROJECT_ROOT / "tools" / "p8000" / "wega_s3fs.py"
_spec = importlib.util.spec_from_file_location("wega_s3fs_werkzeug", _pfad)
s3fs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(s3fs)


def test_die_vorstufe_liest_was_der_kern_schreibt(tmp_path):
    from app.core_binding.k1520disk import DiskTool

    img = tmp_path / "wega.img"
    klein = tmp_path / "klein.txt"
    klein.write_bytes(b"WEGA 3.0\n")
    mittel = tmp_path / "mittel.bin"
    mittel.write_bytes(bytes((i * 31 + i // 512) & 0xFF for i in range(60000)))

    disk = DiskTool.create(img, "wega720", "probe")
    disk.insert(klein, "etc/motd")
    disk.insert(mittel, "usr/lib/x/mittel.bin")
    disk.flush()
    disk.close()

    # Diskette: Dateisystem ab Block 0, kein Vorlauf (anders als die Platte)
    fs = s3fs.S3fs(os.fspath(img), 0, 0)
    assert sorted(fs.verzeichnis(2)) == [".", "..", "etc", "usr"]
    assert fs.daten(fs.suche("/etc/motd")) == klein.read_bytes()
    assert fs.daten(fs.suche("/usr/lib/x/mittel.bin")) == mittel.read_bytes()
    mode, groesse, _ = fs.inode(fs.suche("/usr/lib/x"))
    assert mode & 0o170000 == 0o040000 and groesse == 48


def test_eine_lieferdiskette_ist_fuer_beide_leser_gleich():
    ordner = os.environ.get(
        "K1520_WEGA_IMG",
        os.path.expanduser("~/Documents/K1520emu/Disketten/P8000/WEGA3.0"))
    img = os.path.join(ordner, "w30root4.img")
    if not os.path.exists(img):
        pytest.skip(f"WEGA-Abbild fehlt: {img}")
    from app.core_binding.k1520disk import DiskTool

    disk = DiskTool.open(img)
    fs = s3fs.S3fs(img, 0, 0)
    try:
        # list() liefert den ganzen Baum — verglichen wird die Wurzel
        namen = {e.name for e in disk.list() if "/" not in e.name}
        assert namen == {n for n in fs.verzeichnis(2) if n not in (".", "..")}
    finally:
        disk.close()

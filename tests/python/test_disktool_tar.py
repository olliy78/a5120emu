"""tar-Disketten (roher tar-Strom ohne Dateisystem) und der Containerkommentar (.cqm)
ueber die C-ABI: Dateisystemart ``tar``, UNIX-Angaben am Eintrag, nur lesbar,
``DiskTool.container_info()``.  Die Abbilder werden hier selbst gebaut."""

import struct

import pytest

from conftest import requires_disk

pytestmark = requires_disk

DISK = 737280


def _kopf(name: str, daten: bytes, mode=0o644, uid=25, gid=1, mtime=612345678) -> bytes:
    h = bytearray(512)
    h[0:len(name)] = name.encode()
    h[100:108] = b"%07o\0" % mode
    h[108:116] = b"%07o\0" % uid
    h[116:124] = b"%07o\0" % gid
    h[124:136] = b"%011o\0" % len(daten)
    h[136:148] = b"%011o\0" % mtime
    h[156] = ord("0")
    h[148:156] = b"        "
    h[148:155] = b"%06o\0" % sum(h)
    h[155] = ord(" ")
    return bytes(h)


def _tar() -> bytes:
    s = b""
    for name, daten in (("a.txt", b"Hallo\n"), ("dir/b.bin", bytes(range(200)))):
        s += _kopf(name, daten) + daten + bytes(-len(daten) % 512)
    return (s + bytes(1024)).ljust(DISK, b"\xe5")


def _cqm(raw: bytes, kommentar: bytes) -> bytes:
    from_crc = _crc(raw)
    f = bytearray(133)
    f[0:3] = b"CQ\x14"
    f[3:5] = struct.pack("<H", 512)
    f[0x10] = 9
    f[0x12] = 2
    f[0x1C:0x1C + 17] = b"720K Double-Sided"
    f[0x58] = 1
    f[0x5A] = f[0x5B] = 80
    f[0x5C:0x60] = struct.pack("<I", from_crc)
    f[0x60:0x6B] = b"** NONE ** "
    f[0x6F:0x71] = struct.pack("<H", len(kommentar))
    f[0x74] = 3
    f[0x75] = 2
    f[0x84] = (-sum(f[:0x84])) & 0xFF
    out = bytes(f) + kommentar
    for i in range(0, len(raw), 30000):
        blk = raw[i:i + 30000]
        out += struct.pack("<h", len(blk)) + blk
    return out


def _crc(daten: bytes) -> int:
    """CopyQM-Pruefwert: CRC-32 (0xEDB88320), Start 0, kein Endxor, Index & 0x3F."""
    tab = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        tab.append(c)
    crc = 0
    for b in daten:
        crc = tab[(b ^ crc) & 0x3F] ^ (crc >> 8)
    return crc & 0xFFFFFFFF


def test_tar_disk_reports_tar_and_unix_fields(tmp_path):
    from app.core_binding.k1520disk import DiskTool
    p = tmp_path / "t.img"
    p.write_bytes(_tar())
    with DiskTool.open(p) as d:
        assert d.filesystem == "tar720"
        assert d.filesystem_type == "tar"
        assert d.ist_tar and not d.ist_wega and not d.ist_udos and not d.ist_cpm
        assert d.read_only
        e = next(x for x in d.list() if x.name == "dir/b.bin")
        assert e.ist_unix and e.size == 200
        assert (e.unix_mode, e.unix_uid, e.unix_gid) == (0o100644, 25, 1)
        assert e.attrs == "-rw-r--r--"
        assert d.container_info() == {"type": "img"}


def test_container_info_carries_the_cqm_comment(tmp_path):
    from app.core_binding.k1520disk import DiskTool
    p = tmp_path / "t.cqm"
    p.write_bytes(_cqm(_tar(), b"Image file by test"))
    with DiskTool.open(p) as d:
        info = d.container_info()
        assert info["type"] == "cqm"
        assert info["comment"] == "Image file by test"
        assert info["description"] == "720K Double-Sided"
        assert info["label"] == ""             # "** NONE **" gilt als kein Etikett
        assert info["interleave"] == "3" and info["skew"] == "2"
        assert info["cylinders"] == "80/80"
        assert info["crc"] == "ok"
        assert d.ist_tar and d.read_only


def test_tar_archive_carries_unix_keys_not_udos(tmp_path):
    pytest.importorskip("yaml")
    import zipfile
    import yaml
    from app.core_binding.k1520disk import DiskTool
    from app.disktool.archive import INVENTAR_DATEI, create_archive
    p = tmp_path / "t.img"
    p.write_bytes(_tar())
    ziel = tmp_path / "t.zip"
    with DiskTool.open(p) as d:
        create_archive(d, ziel)
    with zipfile.ZipFile(ziel) as z:
        daten = yaml.safe_load(z.read(INVENTAR_DATEI))
    b = next(f for f in daten["files"] if f["name"] == "b.bin")
    assert b["dir"] == "dir"
    assert b["unix"] == {"mode": "0100644", "uid": 25, "gid": 1, "mtime": 612345678}

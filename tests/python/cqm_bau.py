"""Testhilfe: baut CopyQM-Abbilder (.cqm) in Python nach doc/design/33_cqm.md.

Gegenstueck zu ``tests/unit/peripherals/test_cqm_codec.cpp``.  Die Besonderheit, die man
nicht "reparieren" darf: der Pruefwert ist CRC-32 (0xEDB88320, Start 0, kein Endxor) mit
dem Tabellenfehler ``(b ^ crc) & 0x3F`` statt ``& 0xFF``.
"""

import struct

DISK_720K = 737280


def cqm_crc(daten: bytes) -> int:
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


def _packen(raw: bytes) -> bytes:
    """Datenstrom: Folgen gleicher Bytes als ``i16 -n, byte``, der Rest als Rohblock."""
    out = bytearray()
    i, n = 0, len(raw)
    roh = bytearray()

    def roh_ausgeben():
        for k in range(0, len(roh), 30000):
            blk = roh[k:k + 30000]
            out.extend(struct.pack("<h", len(blk)) + blk)
        roh.clear()

    while i < n:
        j = i
        while j < n and raw[j] == raw[i] and j - i < 30000:
            j += 1
        if j - i >= 8:
            roh_ausgeben()
            out.extend(struct.pack("<h", -(j - i)) + bytes([raw[i]]))
        else:
            roh.extend(raw[i:j])
        i = j
    roh_ausgeben()
    return bytes(out)


def cqm(raw: bytes, kommentar: bytes = b"", *, sektor=512, sektoren=9, seiten=2,
        zylinder=80, bezeichnung=b"720K Double-Sided", etikett=b"** NONE ** ",
        interleave=3, skew=2) -> bytes:
    """Ein vollstaendiges ``.cqm`` aus den entpackten Nutzdaten ``raw``."""
    f = bytearray(133)
    f[0:3] = b"CQ\x14"
    f[3:5] = struct.pack("<H", sektor)
    f[0x10] = sektoren
    f[0x12] = seiten
    f[0x1C:0x1C + len(bezeichnung)] = bezeichnung
    f[0x58] = 1
    f[0x5A] = f[0x5B] = zylinder
    f[0x5C:0x60] = struct.pack("<I", cqm_crc(raw))
    f[0x60:0x6B] = etikett.ljust(11)[:11]
    f[0x6F:0x71] = struct.pack("<H", len(kommentar))
    f[0x74] = interleave
    f[0x75] = skew
    f[0x84] = (-sum(f[:0x84])) & 0xFF
    return bytes(f) + kommentar + _packen(raw)


def leeres_720k(kommentar: bytes = b"Testabbild fuer die Oberflaeche") -> bytes:
    """Ein unbeschriebenes 720K-Abbild (0xE5) — mountbar, ohne Dateisystem."""
    return cqm(bytes([0xE5]) * DISK_720K, kommentar)

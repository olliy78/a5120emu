"""CopyQM-Wandler für die WEGA-Datenträger (``tools/p8000/cqm2img.py``, AP P14).

Die Tests benutzen eine SELBST ERZEUGTE Mini-CQM (wenige Sektoren) — kein
eingechecktes Abbild.  Die echten WEGA-3.0-Abbilder werden nur geprüft, wenn sie
auf dem Rechner liegen (``K1520_WEGA_CQM`` oder ``~/projects/robotron/P8000/
discs/WEGA3.0``); sonst werden diese Fälle übersprungen.
"""

import importlib.util
import os
import struct
from pathlib import Path

import pytest

from conftest import PROJECT_ROOT

_pfad = PROJECT_ROOT / "tools" / "p8000" / "cqm2img.py"
_spec = importlib.util.spec_from_file_location("cqm2img_werkzeug", _pfad)
cqm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cqm)


def packen(daten):
    """Wiederholungen ab 4 Byte als Lauf, Rest roh — blockweise über die ganze Datenmenge."""
    out = bytearray()
    i = 0
    roh = bytearray()

    def rohe_ausgeben():
        while roh:
            n = min(len(roh), 0x7FFF)
            out.extend(struct.pack("<h", n) + bytes(roh[:n]))
            del roh[:n]

    while i < len(daten):
        j = i
        while j < len(daten) and daten[j] == daten[i] and j - i < 0x7FFF:
            j += 1
        if j - i >= 4:
            rohe_ausgeben()
            out.extend(struct.pack("<h", -(j - i)) + bytes([daten[i]]))
            i = j
        else:
            roh.append(daten[i])
            i += 1
    rohe_ausgeben()
    return bytes(out)


def mini_cqm(daten, ssz=256, spt=4, koepfe=2, spuren=2, kommentar=b"", label=b"Mini"):
    kopf = bytearray(cqm.HEADER_LEN)
    kopf[0:2] = b"CQ"
    kopf[2] = 0x14
    struct.pack_into("<H", kopf, 3, ssz)
    struct.pack_into("<H", kopf, 0x10, spt)
    struct.pack_into("<H", kopf, 0x12, koepfe)
    kopf[0x1C:0x1C + len(label)] = label
    kopf[0x58] = 1
    kopf[0x5A] = kopf[0x5B] = spuren
    struct.pack_into("<H", kopf, 0x6F, len(kommentar))
    kopf[0x84] = (-sum(kopf[:0x84])) & 0xFF
    return bytes(kopf) + kommentar + packen(daten)


def beispieldaten(ssz=256, spt=4, koepfe=2, spuren=2):
    # Läufe über Sektorgrenzen (E5-Füllung), rohe Strecken und ein Zählmuster
    d = bytearray()
    d += bytes([0xE5]) * (ssz * 3 // 2)
    d += bytes(range(256)) * 2
    d += bytes([0x00]) * ssz
    gesamt = ssz * spt * koepfe * spuren
    while len(d) < gesamt:
        d.append((len(d) * 7) & 0xFF)
    return bytes(d[:gesamt])


def test_mini_cqm_rundlauf():
    roh = beispieldaten()
    meta, aus = cqm.decode(mini_cqm(roh, kommentar=b"Hallo WEGA"))
    assert aus == roh
    assert meta["warnungen"] == []
    assert (meta["sektorgroesse"], meta["sektoren"], meta["koepfe"], meta["spuren"]) == (256, 4, 2, 2)
    assert meta["kommentar"] == "Hallo WEGA"
    assert meta["bezeichnung"] == "Mini"
    assert meta["kopf_pruefbyte_ok"]
    assert meta["bytes"] == len(roh)


def test_lauf_ueber_mehrere_sektoren_wird_nicht_je_sektor_neu_begonnen():
    roh = bytes([0xE5]) * (256 * 16)
    kopf_und_strom = mini_cqm(roh)
    # Ein einziger Wiederholungsblock (int16 -4096 + 1 Byte) trägt die ganze Datenmenge
    assert len(kopf_und_strom) == cqm.HEADER_LEN + 3
    assert cqm.decode(kopf_und_strom)[1] == roh


def test_abgeschnittener_strom_wird_gemeldet_und_aufgefuellt():
    d = mini_cqm(beispieldaten())
    meta, aus = cqm.decode(d[:-40])
    assert any("entpackt" in w for w in meta["warnungen"])
    assert len(aus) == 256 * 4 * 2 * 2


def test_kaputter_kopfbyte_wird_gemeldet():
    d = bytearray(mini_cqm(beispieldaten()))
    d[0x84] ^= 0x55
    meta, aus = cqm.decode(bytes(d))
    assert "Kopfpruefbyte stimmt nicht" in meta["warnungen"]
    assert aus == beispieldaten()


def test_keine_cqm_wird_abgewiesen():
    with pytest.raises(cqm.CqmError):
        cqm.decode(b"XX" + bytes(200))
    with pytest.raises(cqm.CqmError):
        cqm.decode(b"CQ")


def test_nicht_gespeicherte_spuren_werden_aufgefuellt():
    roh = beispieldaten(spuren=2)
    d = bytearray(mini_cqm(roh))
    d[0x5B] = 3  # 3 Spuren insgesamt, 2 gespeichert
    d[0x84] = (-sum(d[:0x84])) & 0xFF
    meta, aus = cqm.decode(bytes(d))
    assert len(aus) == 256 * 4 * 2 * 3
    assert aus[:len(roh)] == roh and aus[len(roh):] == bytes(256 * 4 * 2)
    assert meta["warnungen"]


def test_kommandozeile_schreibt_img(tmp_path):
    roh = beispieldaten()
    quelle = tmp_path / "m.cqm"
    quelle.write_bytes(mini_cqm(roh))
    ziel = tmp_path / "m.img"
    assert cqm.main([str(quelle), str(ziel)]) == 0
    assert ziel.read_bytes() == roh
    # -d ZIELDIR
    assert cqm.main(["-d", str(tmp_path / "aus"), str(quelle)]) == 0
    assert (tmp_path / "aus" / "m.img").read_bytes() == roh
    # --info schreibt nichts
    assert cqm.main(["--info", str(quelle)]) == 0
    assert cqm.main([str(tmp_path / "gibtsnicht.cqm")]) == 2


# --- echte WEGA-Abbilder, nur wenn vorhanden ------------------------------------

_WEGA = Path(os.environ.get("K1520_WEGA_CQM", "~/projects/robotron/P8000/discs/WEGA3.0")).expanduser()
_echte = sorted(_WEGA.glob("*.cqm")) if _WEGA.is_dir() else []


@pytest.mark.skipif(not _echte, reason="WEGA-3.0-CQM-Abbilder nicht vorhanden")
def test_echte_wega_abbilder_sind_vollstaendig():
    assert len(_echte) == 17
    for f in _echte:
        meta, roh = cqm.decode(f.read_bytes())
        assert meta["warnungen"] == [], (f.name, meta["warnungen"])
        assert (meta["spuren"], meta["koepfe"]) == (80, 2)
        assert len(roh) == 80 * 2 * meta["sektoren"] * meta["sektorgroesse"]


@pytest.mark.skipif(not _echte, reason="WEGA-3.0-CQM-Abbilder nicht vorhanden")
def test_echte_wega_dateisystemdisketten_tragen_ein_unix_dateisystem():
    # Superblock = Block 1 (512 B), big-endian: s_isize (16 Bit), s_fsize (32 Bit);
    # Inode 2 = Wurzelverzeichnis (Modus 040xxx) im Block 2.
    pruefte = 0
    for f in _echte:
        if not f.name.startswith(("w30root", "w30usr", "w30doc")):
            continue
        meta, roh = cqm.decode(f.read_bytes())
        assert meta["sektorgroesse"] == 512
        isize, fsize = struct.unpack_from(">HI", roh, 512)
        assert (isize, fsize) == (59, 1440)
        mode = struct.unpack_from(">H", roh, 1024 + 64)[0]
        assert mode & 0o170000 == 0o040000
        pruefte += 1
    assert pruefte == 15

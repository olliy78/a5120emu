"""C-ABI des DiskTools: `core/api/k1520_disk_api.h` ↔ `libk1520disk.so` ↔ ctypes.

Dieselbe Mechanik wie `test_c_api.py` für den Emulator, und aus demselben Grund:
der C++-Compiler prüft die Python-Seite nicht, und ctypes meldet eine falsche
Signatur erst beim Aufruf — meist als Absturz statt als Fehlermeldung.  Die drei
Seiten werden deshalb **mechanisch** verglichen.

Dazu ein paar fachliche Proben an den echten Disketten des Projekts: die
Bindung soll nicht nur laden, sondern das Richtige liefern.
"""

import re

import pytest

from conftest import PROJECT_ROOT, requires_disk

pytestmark = requires_disk

HEADER = PROJECT_ROOT / "core" / "api" / "k1520_disk_api.h"
BINDING = PROJECT_ROOT / "app" / "core_binding" / "k1520disk.py"


def header_functions() -> set:
    """Alle im Header deklarierten `k1520d_*`-Funktionen."""
    text = HEADER.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return set(re.findall(r"\b(k1520d_[a-z0-9_]+)\s*\(", text))


# ─── Die drei Seiten ─────────────────────────────────────────────────────────

def test_header_declares_the_expected_api():
    funcs = header_functions()
    assert HEADER.exists(), f"C-API-Header fehlt: {HEADER}"
    assert len(funcs) >= 30, f"nur {len(funcs)} Funktionen im Header gefunden"
    for essential in ("k1520d_open", "k1520d_close", "k1520d_list",
                      "k1520d_extract_all", "k1520d_insert_all", "k1520d_flush"):
        assert essential in funcs


def test_every_header_function_is_exported_by_the_library():
    from app.core_binding.k1520disk import _lib
    missing = [f for f in sorted(header_functions()) if not hasattr(_lib, f)]
    assert not missing, (
        "im Header deklariert, aber nicht in libk1520disk.so exportiert: "
        + ", ".join(missing)
    )


def test_every_header_function_has_ctypes_signatures():
    """Ohne argtypes/restype konvertiert ctypes still nach `int` — der Handle-
    Zeiger wird dabei auf 64-Bit-Systemen abgeschnitten."""
    import app.core_binding.k1520disk  # noqa: F401  (lädt die Bibliothek)

    source = BINDING.read_text("utf-8")
    declared = set(re.findall(r"_lib\.(k1520d_[a-z0-9_]+)\.argtypes", source))
    undeclared = sorted(header_functions() - declared)
    assert not undeclared, (
        "C-API-Funktionen ohne ctypes-Deklaration in app/core_binding/k1520disk.py: "
        + ", ".join(undeclared)
    )


def test_version_string():
    from app.core_binding.k1520disk import version
    assert version().startswith("k1520disk")


# ─── Katalog ─────────────────────────────────────────────────────────────────

def test_filesystem_catalogue_is_reachable():
    from app.core_binding.k1520disk import filesystems

    names = {f.name: f for f in filesystems()}
    assert "cpa780" in names and "udos_ds77" in names
    assert names["cpa780"].type == "cpm"
    assert names["udos_ds77"].type == "udos"
    assert names["scpx640"].format == "cpa640", (
        "scpx640 und cpa640 teilen sich die Geometrie — genau dafür gibt es die "
        "eigene `filesystems:`-Sektion"
    )
    for f in filesystems():
        assert f.description, f"{f.name} ohne Beschreibung"


def test_catalog_report_names_the_loaded_files():
    from app.core_binding.k1520disk import catalog_report
    text = catalog_report()
    assert "formats.yaml" in text, text


# ─── Fachliche Proben an den echten Disketten ────────────────────────────────

def test_open_detects_cpa_boot_disk(fixture_disks):
    from app.core_binding.k1520disk import DiskTool

    with DiskTool.open(fixture_disks / "cpa_cpa780_k5601_clock.img") as d:
        assert d.filesystem == "cpa780"
        assert d.volume_count == 1
        assert d.volume_dir(0) == "", "eine CP/M-Diskette hat keine Seitenordner"
        names = {e.name for e in d.list()}
        assert "@OS.COM" in names and "PIP.COM" in names
        assert len(names) == 24


def test_open_treats_both_udos_sides_as_one_disk(fixture_disks):
    from app.core_binding.k1520disk import DiskTool

    with DiskTool.open(fixture_disks / "udos_boot_scp.hfe") as d:
        assert d.filesystem == "udos_ds77"
        assert d.volume_count == 2
        assert [v.dir for v in d.volumes()] == ["Side0", "Side1"]
        # `STATUS` des laufenden UDOS: 850 bzw. 1310 freie Sektoren à 128 B.
        assert [v.free // 128 for v in d.volumes()] == [850, 1310]

        entries = d.list()
        assert len(entries) == 69
        # Jede Datei kennt ihre Seite und ist darüber eindeutig adressierbar.
        help_dat = [e for e in entries if e.name == "HELP.DAT.00"]
        assert len(help_dat) == 1
        assert help_dat[0].ref == "Side1/HELP.DAT.00"
        assert help_dat[0].size == 9919
        assert help_dat[0].type == "A"

        assert "Altbestand" in d.remarks, d.remarks


def test_unknown_geometry_raises_with_the_measurement(fixture_disks):
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    with pytest.raises(K1520DiskError) as exc:
        DiskTool.open(fixture_disks / "cpa_mini.hfe")
    text = str(exc.value)
    assert "passt zu keinem Format" in text
    assert "4 Sektoren" in text, f"die Meldung muss die Messung nennen:\n{text}"


def test_detect_without_keeping_the_disk_open(fixture_disks):
    from app.core_binding.k1520disk import detect

    assert detect(fixture_disks / "udos_boot_scp.hfe") == "udos_ds77"
    assert detect(fixture_disks / "cpa_mini.hfe") == ""


# ─── Schreiben (nur auf Kopien) ──────────────────────────────────────────────

def test_create_write_read_roundtrip(tmp_path):
    from app.core_binding.k1520disk import DiskTool

    quelle = tmp_path / "quelle.bin"
    quelle.write_bytes(bytes(range(256)) * 4)

    abbild = tmp_path / "neu.hfe"
    with DiskTool.create(abbild, "udos_ds77", label="PYTEST") as d:
        assert d.volume_count == 2
        assert [v.label for v in d.volumes()] == ["PYTEST", "PYTEST"]

        d.insert(quelle, "Side1/DATEN.BIN")
        assert d.dirty
        d.flush()

        namen = {e.ref for e in d.list()}
        assert "Side1/DATEN.BIN" in namen

        zurueck = tmp_path / "zurueck.bin"
        d.extract("Side1/DATEN.BIN", zurueck)
        assert zurueck.read_bytes() == quelle.read_bytes()


def test_insert_all_requires_the_side_directories(tmp_path, fixture_disks):
    import shutil
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    abbild = tmp_path / "udos.hfe"
    shutil.copy(fixture_disks / "udos_boot_scp.hfe", abbild)

    quelle = tmp_path / "quelle"
    (quelle / "Side0").mkdir(parents=True)
    (quelle / "Side0" / "NUR.EINE").write_text("Seite 0")

    with DiskTool.open(abbild) as d:
        with pytest.raises(K1520DiskError) as exc:
            d.insert_all(quelle)
        assert "Side1/" in str(exc.value)
        assert not d.dirty, "die Diskette darf dabei nicht angefasst worden sein"

        # Mit beiden Unterverzeichnissen geht es.
        (quelle / "Side1").mkdir()
        (quelle / "Side1" / "UND.EINE").write_text("Seite 1")
        d.insert_all(quelle)
        refs = {e.ref for e in d.list()}
        assert "Side0/NUR.EINE" in refs and "Side1/UND.EINE" in refs


def test_check_fit_reports_without_writing(tmp_path, fixture_disks):
    import shutil
    from app.core_binding.k1520disk import DiskTool

    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)

    klein = tmp_path / "klein"
    klein.mkdir()
    (klein / "winzig.txt").write_text("kurz")

    gross = tmp_path / "gross"
    gross.mkdir()
    (gross / "riesig.bin").write_bytes(b"Q" * (900 * 1024))

    with DiskTool.open(abbild) as d:
        assert d.fits(klein)
        assert not d.fits(gross)
        assert "frei sind" in d.check_fit(gross)
        assert not d.dirty


def test_backup_copy_on_first_write(tmp_path, fixture_disks):
    import shutil
    from app.core_binding.k1520disk import DiskTool

    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    original = abbild.read_bytes()

    quelle = tmp_path / "neu.txt"
    quelle.write_text("Inhalt")

    with DiskTool.open(abbild) as d:
        d.insert(quelle, "NEU.TXT")
        d.flush()

    sicherung = abbild.with_name(abbild.name + "~")
    assert sicherung.exists(), "beim ersten Schreiben fehlt die Sicherungskopie"
    assert sicherung.read_bytes() == original
    assert abbild.read_bytes() != original

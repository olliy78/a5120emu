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

    with DiskTool.open(abbild, read_only=False) as d:
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

    with DiskTool.open(abbild, read_only=False) as d:
        d.insert(quelle, "NEU.TXT")
        d.flush()

    sicherung = abbild.with_name(abbild.name + "~")
    assert sicherung.exists(), "beim ersten Schreiben fehlt die Sicherungskopie"
    assert sicherung.read_bytes() == original
    assert abbild.read_bytes() != original


# ─── Bootabbild (Systemspuren) ───────────────────────────────────────────────

def test_boot_capacity_is_known_without_a_disk():
    """Die Oberfläche muss schon bei der Auswahl wissen, ob eine Bootdiskette geht."""
    from app.core_binding.k1520disk import boot_capacity, filesystems

    assert boot_capacity("cpa780") == 15104
    assert boot_capacity("cpa800") == 0, "eine Datendiskette beginnt auf Zylinder 0"
    assert boot_capacity("gibtsnicht") == 0

    nach_name = {f.name: f for f in filesystems()}
    assert nach_name["cpa780"].bootable
    assert not nach_name["cpa800"].bootable
    assert nach_name["cpa780"].boot_capacity == 15104


def test_boot_image_roundtrip(tmp_path, fixture_disks):
    """Bootabbild aus einer echten Diskette holen und in eine neue einspielen."""
    from app.core_binding.k1520disk import DiskTool

    bin_ = tmp_path / "cpa780_boot.bin"
    with DiskTool.open(fixture_disks / "cpa_cpa780_k5601_clock.img") as d:
        assert d.boot_area_size() == 15104
        d.read_boot_image(bin_)
    assert bin_.stat().st_size == 15104
    # Die ersten 512 Byte sind der bekannte Bootsektor.
    assert bin_.read_bytes()[:3] == b"SYL"

    abbild = tmp_path / "bootfaehig.hfe"
    with DiskTool.create(abbild, "cpa780", boot_image=bin_) as d:
        assert d.boot_area_size() == 15104
        zurueck = tmp_path / "zurueck.bin"
        d.read_boot_image(zurueck)
        assert zurueck.read_bytes() == bin_.read_bytes()


def test_boot_image_too_large_creates_nothing(tmp_path):
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    zu_gross = tmp_path / "zu_gross.bin"
    zu_gross.write_bytes(b"\x5A" * 15105)
    abbild = tmp_path / "nicht_angelegt.hfe"

    with pytest.raises(K1520DiskError) as exc:
        DiskTool.create(abbild, "cpa780", boot_image=zu_gross)
    assert "15105" in str(exc.value) and "15104" in str(exc.value)
    assert not abbild.exists(), "trotz Fehler wurde eine Diskette angelegt"


# ─── UDOS-Dateiangaben lesen und ändern ──────────────────────────────────────

def test_udos_file_attributes_are_readable(fixture_disks):
    """Alles, was den Ladevorgang steuert, muss sichtbar sein — die Oberfläche
    soll es später anzeigen und ändern können."""
    from app.core_binding.k1520disk import DiskTool

    with DiskTool.open(fixture_disks / "udos_boot_scp.hfe") as d:
        nach_name = {e.name: e for e in d.list()}

        zdos = nach_name["ZDOS"]
        assert (zdos.type, zdos.attrs) == ("P1", "WS")
        assert zdos.entry == 0x2600
        assert zdos.record_len == 1024
        assert zdos.block_len == 1024
        assert (zdos.segment, zdos.segment_len) == (0x2600, 5521)
        # LOW/HIGH/STACK — genau das, was `EXTRACT` im laufenden UDOS meldet.
        assert (zdos.low_addr, zdos.high_addr, zdos.stack_size) == (0x2600, 0x3FD4, 0x80)
        assert zdos.created.startswith("V 4.2")

        # Der Nukleus: Satzlänge 512, zweite Längenangabe 0 (nicht deren Kopie!).
        os_ = nach_name["OS"]
        assert (os_.record_len, os_.block_len) == (512, 0)
        assert (os_.low_addr, os_.high_addr) == (0x1000, 0x25FF)


def test_udos_attributes_can_be_changed_without_touching_the_file(tmp_path, fixture_disks):
    import shutil
    from app.core_binding.k1520disk import DiskTool

    abbild = tmp_path / "udos.hfe"
    shutil.copy(fixture_disks / "udos_boot_scp.hfe", abbild)

    with DiskTool.open(abbild, read_only=False) as d:
        vorher = next(e for e in d.list() if e.name == "CAT")
        inhalt = tmp_path / "cat.vorher"
        d.extract("CAT", inhalt)

        d.set_udos_attrs("CAT", properties="WEL", entry=0x4444,
                         memory=(0x4000, 0x5FFF, 0x0200))
        nachher = next(e for e in d.list() if e.name == "CAT")
        assert nachher.attrs == "WEL"
        assert nachher.entry == 0x4444
        assert (nachher.low_addr, nachher.high_addr, nachher.stack_size) == (0x4000, 0x5FFF, 0x200)
        # Nicht genannte Felder bleiben stehen …
        assert nachher.record_len == vorher.record_len
        assert nachher.segment == vorher.segment
        # … und der Dateiinhalt ist unangetastet.
        danach = tmp_path / "cat.nachher"
        d.extract("CAT", danach)
        assert danach.read_bytes() == inhalt.read_bytes()


def test_changing_attributes_needs_a_writable_disk(fixture_disks):
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    with DiskTool.open(fixture_disks / "udos_boot_scp.hfe") as d:
        with pytest.raises(K1520DiskError) as exc:
            d.set_udos_attrs("CAT", properties="WEL")
        assert "schreibgeschuetzt" in str(exc.value)


def test_cpm_has_no_such_attributes(fixture_disks, tmp_path):
    import shutil
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    with DiskTool.open(abbild, read_only=False) as d:
        assert all(e.record_len == 0 for e in d.list())
        with pytest.raises(K1520DiskError):
            d.set_udos_attrs("PIP.COM", properties="WEL")


# ─── Sektoransicht (Diskeditor) ──────────────────────────────────────────────

def test_track_view_covers_the_whole_revolution(fixture_disks):
    """Die Abschnitte einer Spur decken [0,1) lückenlos ab — darauf ruht die Grafik."""
    from app.core_binding.k1520disk import DiskTool, GAP, SECTOR

    with DiskTool.open(fixture_disks / "cpa_cpa780_k5601_clock.img") as d:
        assert d.medium_cylinders > 0 and d.medium_heads == 2
        t = d.track(0, 0)
        assert t.exists and t.formatted
        assert t.encoding == "MFM"
        assert t.sectors == 26
        assert t.bytes > 0

        assert t.spans[0].start == 0.0
        for a, b in zip(t.spans, t.spans[1:]):
            assert a.end == b.start
        assert abs(t.spans[-1].end - 1.0) < 1e-9

        arten = {s.kind for s in t.spans}
        assert arten == {GAP, SECTOR}, "eine formatierte Spur hat beides"

        sektoren = [s for s in t.spans if s.is_sector]
        assert [s.id for s in sektoren] == list(range(1, 27))
        assert all(s.size == 128 and s.ok for s in sektoren)
        assert [s.index for s in sektoren] == list(range(26))


def test_sector_data_and_crc_are_readable(fixture_disks):
    from app.core_binding.k1520disk import DiskTool

    with DiskTool.open(fixture_disks / "cpa_cpa780_k5601_clock.img") as d:
        daten = d.sector_data(0, 0, 0)
        assert len(daten) == 128
        # Die gespeicherte CRC ist die, die zu den Daten gehört — der Sektor ist heil.
        assert d.sector_crc(0, 0, 0) == d.sector_crc_for(0, 0, 0, daten)
        # Andere Daten, andere CRC.
        assert d.sector_crc_for(0, 0, 0, bytes(128)) != d.sector_crc(0, 0, 0)


def test_sector_write_needs_a_writable_disk(fixture_disks):
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    with DiskTool.open(fixture_disks / "cpa_cpa780_k5601_clock.img") as d:
        with pytest.raises(K1520DiskError, match="schreibgesch"):
            d.sector_write(0, 0, 0, bytes(128))


def test_sector_write_replaces_only_that_sector(tmp_path, fixture_disks):
    import shutil
    from app.core_binding.k1520disk import DiskTool

    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    with DiskTool.open(abbild) as d:
        d.set_read_only(False)
        # cpa780 hat gemischte Geometrie: ab Zylinder 2 sind es 1024-B-Sektoren.
        # Die Größe kommt deshalb aus dem Sektor selbst, nicht aus einer Annahme.
        gross = next(s for s in d.track(2, 0).spans if s.is_sector).size
        nachbar = d.sector_data(2, 0, 1)
        neu = bytes(i & 0xFF for i in range(gross))
        d.sector_write(2, 0, 0, neu)

        assert d.sector_data(2, 0, 0) == neu
        assert d.sector_data(2, 0, 1) == nachbar, "der Nachbarsektor bleibt unberührt"
        assert d.track(2, 0).spans[1].ok, "CRC wurde mitgerechnet"

        # Eine wörtliche CRC macht den Sektor absichtlich defekt …
        d.sector_write(2, 0, 0, neu, crc=0x1234)
        assert d.sector_crc(2, 0, 0) == 0x1234
        kaputt = next(s for s in d.track(2, 0).spans if s.is_sector and s.index == 0)
        assert not kaputt.ok
        # … und die Nutzdaten stehen trotzdem richtig da.
        assert d.sector_data(2, 0, 0) == neu


def test_sector_write_refuses_a_wrong_length(tmp_path, fixture_disks):
    import shutil
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    with DiskTool.open(abbild) as d:
        d.set_read_only(False)
        vorher = d.sector_data(2, 0, 0)
        with pytest.raises(K1520DiskError):
            d.sector_write(2, 0, 0, bytes(len(vorher) - 1))
        assert d.sector_data(2, 0, 0) == vorher, "nichts halb geschrieben"
        with pytest.raises(K1520DiskError):
            d.sector_data(2, 0, 99)


def test_unformatted_track_is_told_apart_from_a_gap(tmp_path):
    """Eine echte Leerdiskette: Bytes ja, Adressmarken nein — das ist grau, nicht orange."""
    from app.core_binding.k1520disk import DiskTool, UNFORMATTED

    ziel = tmp_path / "leer.hfe"
    DiskTool.create(ziel, "cpa780").close()
    with DiskTool.open(ziel) as d:
        t = d.track(60, 0)
        assert t.formatted, "cpa780 legt eine formatierte Diskette an"

    # Gegenprobe an einer Spur, die es in der Ausdehnung des Mediums nicht gibt.
    with DiskTool.open(ziel) as d:
        t = d.track(d.medium_cylinders + 5, 0)
        assert not t.exists and not t.formatted
        assert [s.kind for s in t.spans] == [UNFORMATTED]

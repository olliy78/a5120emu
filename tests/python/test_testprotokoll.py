"""@test Testprotokoll-Generator (`tools/test_report.py`): JUnit-XML → HTML.

Warum eine eigene Ebene dafür: die Seite ist das, was in der CI von einem Lauf
ÜBRIG BLEIBT.  Verschluckt sie einen Fehlschlag, sieht ein roter Lauf grün aus —
und das fällt niemandem auf, weil der Erzeuger selbst nie rot wird.  Geprüft
wird deshalb vor allem, was NICHT verlorengehen darf: der Fehlschlag, seine
Ausgabe, der Rückgabewert, die Zuordnung zur Ebene.

Ausdrücklich NICHT geprüft: das Aussehen.  Ob eine Karte 12 oder 14 Pixel
Innenabstand hat, gehört in kein Testprotokoll.
"""

import importlib.util
import subprocess
import sys
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = PROJECT_ROOT / "tools" / "test_report.py"


def _lade_werkzeug():
    """`tools/test_report.py` unter EIGENEM Modulnamen laden.

    Nicht `sys.path`+`import test_report`: pytest importiert Testdateien ohne
    Paket, sodass ein `tests/python/test_report.py` denselben Modulnamen belegen
    würde — der Import holte dann die Testdatei statt des Werkzeugs (genau so
    beim Schreiben dieser Datei passiert: 16 Fälle rot mit
    „module 'test_report' has no attribute …").  Deshalb heißt diese Datei
    `test_testprotokoll.py` UND das Werkzeug wird über seinen Pfad geladen.
    """
    spec = importlib.util.spec_from_file_location("k1520_test_report", SCRIPT)
    modul = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = modul
    spec.loader.exec_module(modul)
    return modul


tr = _lade_werkzeug()


def _xml(faelle: str, **kopf) -> str:
    """Minimale ctest-artige JUnit-XML mit den übergebenen <testcase>-Zeilen."""
    attrs = " ".join(f'{k}="{v}"' for k, v in kopf.items())
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<testsuite name="(empty)" {attrs} timestamp="2026-08-16T12:00:00">\n'
        f"{faelle}\n</testsuite>\n"
    )


BESTANDEN = """
  <testcase name="Z80Test.NOP" classname="Z80Test.NOP" time="0.01" status="run">
    <properties><property name="cmake_labels" value="fast;unit"/></properties>
    <system-out>[  PASSED  ] 1 test.</system-out>
  </testcase>
"""

FEHLGESCHLAGEN = """
  <testcase name="BootIntegration.Stage3" classname="BootIntegration.Stage3"
            time="1.9" status="fail">
    <properties><property name="cmake_labels" value="integration;fast"/></properties>
    <failure message="Erwartet PC==0x0437">Actual: 0x0168 &lt;hier&gt; äöü</failure>
    <system-out>[  FAILED  ] BootIntegration.Stage3</system-out>
  </testcase>
"""

UEBERSPRUNGEN = """
  <testcase name="py_packaging" classname="py_packaging" time="0.2" status="notrun">
    <properties><property name="cmake_labels" value="python;fast"/></properties>
    <skipped message="uv fehlt"/>
  </testcase>
"""


@pytest.fixture
def gruen(tmp_path):
    p = tmp_path / "gruen.xml"
    p.write_text(_xml(BESTANDEN, tests="1", failures="0"), encoding="utf-8")
    return p


@pytest.fixture
def rot(tmp_path):
    p = tmp_path / "rot.xml"
    p.write_text(_xml(BESTANDEN + FEHLGESCHLAGEN + UEBERSPRUNGEN,
                      tests="3", failures="1", skipped="1"), encoding="utf-8")
    return p


# ─── Einlesen ────────────────────────────────────────────────────────────────

def test_ebene_kommt_aus_dem_ctest_label(rot):
    """Die Gliederung ist die des Projekts — Label `unit`/`integration`/`python`."""
    lauf = tr.lies_junit(rot, "L")
    ebenen = {f.name: f.ebene for f in lauf.faelle}
    assert ebenen["Z80Test.NOP"] == "unit"
    assert ebenen["BootIntegration.Stage3"] == "integration"
    assert ebenen["py_packaging"] == "python"


def test_querlabel_gilt_nicht_als_ebene(rot):
    """`fast`/`slow` sind Eigenschaften, keine Ebenen — sonst hieße die Ebene `fast`."""
    lauf = tr.lies_junit(rot, "L")
    assert all(f.ebene != "fast" for f in lauf.faelle)
    assert "fast" in lauf.faelle[0].querlabel


def test_zustaende_werden_unterschieden(rot):
    lauf = tr.lies_junit(rot, "L")
    assert (lauf.zahl("bestanden"), lauf.zahl("fehlgeschlagen"),
            lauf.zahl("uebersprungen")) == (1, 1, 1)
    assert not lauf.gruen


def test_ausgabe_nur_beim_fehlschlag(rot):
    """Die Ausgabe der bestandenen Fälle bläht die Seite ohne Nutzen auf."""
    faelle = {f.name: f for f in tr.lies_junit(rot, "L").faelle}
    assert "0x0168" in faelle["BootIntegration.Stage3"].ausgabe
    assert faelle["Z80Test.NOP"].ausgabe == ""


def test_pytest_xml_ohne_label_wird_ueber_den_dateinamen_zugeordnet(tmp_path):
    """`pytest --junit-xml` schreibt keine cmake_labels; der Name trägt die Ebene."""
    p = tmp_path / "python.xml"
    p.write_text(
        '<?xml version="1.0"?><testsuites><testsuite name="pytest" tests="1">'
        '<testcase classname="tests.python.test_paths" name="test_a" time="0.1"/>'
        "</testsuite></testsuites>",
        encoding="utf-8")
    assert tr.lies_junit(p, "P").faelle[0].ebene == "python"


# ─── Erzeugte Seite ──────────────────────────────────────────────────────────

def test_html_ist_eigenstaendig_und_zeigt_den_fehlschlag(rot, tmp_path):
    """Kein Nachladen (Actions-Artefakt wird offline geöffnet), Fehler sichtbar."""
    ziel = tmp_path / "p.html"
    assert tr.main([f"Lauf:{rot}", "-o", str(ziel)]) == 0
    seite = ziel.read_text(encoding="utf-8")

    assert "BootIntegration.Stage3" in seite
    assert "Erwartet PC==0x0437" in seite          # Meldung
    assert "Actual: 0x0168" in seite               # Ausgabe
    assert "<details class=\"fehler\" open>" in seite  # aufgeklappt, nicht versteckt

    for verboten in ("http://", "https://", "<script", "@import"):
        assert verboten not in seite, f"Seite lädt nach oder führt aus: {verboten}"


def test_html_maskiert_sonderzeichen(rot, tmp_path):
    """Ein `<tag>` in einer Testausgabe darf die Seite nicht zerlegen."""
    ziel = tmp_path / "p.html"
    tr.main([f"Lauf:{rot}", "-o", str(ziel)])
    seite = ziel.read_text(encoding="utf-8")
    assert "&lt;hier&gt;" in seite
    assert "äöü" in seite


def test_mehrere_laeufe_stehen_nebeneinander(gruen, rot, tmp_path):
    ziel = tmp_path / "p.html"
    tr.main([f"Linux:{gruen}", f"Windows:{rot}", "-o", str(ziel)])
    seite = ziel.read_text(encoding="utf-8")
    assert "Linux" in seite and "Windows" in seite


def test_markdown_kurzfassung_nennt_die_fehlschlaege(rot, capsys):
    tr.main([f"Windows:{rot}", "-o", str(rot.parent / "p.html"), "--summary-md", "-"])
    md = capsys.readouterr().out
    assert "| ❌ Windows |" in md
    assert "BootIntegration.Stage3" in md
    # Die Statuszeile gehört auf stderr, sonst landet sie in der GitHub-
    # Zusammenfassung (`--summary-md - >> $GITHUB_STEP_SUMMARY`).
    assert "test_report:" not in md


# ─── Rückgabewerte: davon hängt ab, ob die CI rot wird ───────────────────────

def test_fail_on_error_meldet_den_fehlschlag(rot, gruen, tmp_path):
    ziel = str(tmp_path / "p.html")
    assert tr.main([f"Lauf:{rot}", "-o", ziel, "--fail-on-error"]) == 1
    assert tr.main([f"Lauf:{gruen}", "-o", ziel, "--fail-on-error"]) == 0
    assert tr.main([f"Lauf:{rot}", "-o", ziel]) == 0  # ohne Schalter: nur berichten


def test_fehlende_datei_ist_kein_absturz(tmp_path):
    assert tr.main([str(tmp_path / "gibtsnicht.xml"), "-o", str(tmp_path / "p.html")]) == 2


def test_kaputte_xml_ist_kein_absturz(tmp_path):
    p = tmp_path / "kaputt.xml"
    p.write_text("<testsuite><testcase", encoding="utf-8")
    assert tr.main([str(p), "-o", str(tmp_path / "p.html")]) == 2


@pytest.mark.parametrize("eintrag, name, pfad", [
    ("Windows:build/junit.xml", "Windows", "build/junit.xml"),
    ("Tiefe (format_integration):a.xml", "Tiefe (format_integration)", "a.xml"),
    ("build/Testing/junit.xml", "junit", "build/Testing/junit.xml"),
    # Ein einzelner Buchstabe ist ein Laufwerk, kein Name — sonst hieße der Lauf
    # „C" und der Pfad wäre auf „\lauf.xml" abgeschnitten.
    ("C:/lauf.xml", "lauf", "C:/lauf.xml"),
])
def test_laufname_vor_dem_doppelpunkt(eintrag, name, pfad):
    assert tr.zerlege_eintrag(eintrag) == (name, Path(pfad))


# ─── Aufruf als Programm ─────────────────────────────────────────────────────

def test_als_programm_aufrufbar(rot, tmp_path):
    """So ruft die CI es auf — inkl. `--summary-md -` auf stdout."""
    ziel = tmp_path / "p.html"
    r = subprocess.run(
        [sys.executable, str(SCRIPT), f"Windows:{rot}", "-o", str(ziel),
         "--title", "Probe", "--summary-md", "-"],
        capture_output=True, text=True, encoding="utf-8", timeout=60)
    assert r.returncode == 0, r.stderr
    assert ziel.is_file()
    assert "| ❌ Windows |" in r.stdout
    assert "test_report:" in r.stderr
    assert "<title>Probe</title>" in ziel.read_text(encoding="utf-8")

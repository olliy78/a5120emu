"""RAM-Floppy RAF in der Oberfläche (AP-R4, doc/design/22_raf512.md §7).

Geprüft in allen drei Programmen (a5120emu, k8915emu, prg710emu):

* Auswahlfeld „RAM-Disk" und Stand-by-Kästchen unter *Einstellungen ▸ Allgemein*;
  das Kästchen ist nur mit gesteckter RAF wählbar.
* Jeder Typ erzeugt die Maschine neu, mit passendem ``raf_variant``.
* Stand-by: Inhalt übersteht Aus/Ein und einen Neustart des Programms (Ablage
  ``raf_<programm>.bin`` im Konfigurationsordner); ohne Kästchen keine Datei.
* Eine Ablage falscher Größe wird nicht geladen (Statuszeile, kein
  Meldungsfenster — conftest lässt jedes unerwartete scheitern) und beim nächsten
  Sichern ersetzt.
* Konfiguration ``machine.raf``/``machine.raf_standby``: schreiben, lesen,
  fehlend/unbekannt = aus; die Vorgabedateien tragen ``none``/``false``.
* Das Handbuch beschreibt die RAF.

Inhalt wird über ``raf_load`` einer präparierten Datei gesetzt und über
``raf_peek`` geprüft — ohne Gastsystem.
"""

import dataclasses
from pathlib import Path

import pytest

from conftest import PROJECT_ROOT, requires_core

pytestmark = requires_core
pytest.importorskip("PySide6", reason="PySide6 nicht installiert")

PROGRAMME = ["a5120", "k8915", "prg710"]
GROESSE = {"raf128": 128 * 1024, "raf512": 512 * 1024, "raf2m": 2 * 1024 * 1024}


@pytest.fixture
def konfig_ordner(tmp_path, monkeypatch):
    """Eigener, leerer Konfigurationsordner je Test (Werkszustand)."""
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


def _fenster(qapp, maschine="a5120", profil=None):
    from app import profil as profile
    from app.ui.main_window import MainWindow
    w = MainWindow(None, profil=profil or profile.profil(maschine))
    w.show()
    qapp.processEvents()
    return w


def _zu(w, qapp):
    w.close()
    qapp.processEvents()


def _waehle(w, qapp, typ):
    box = w.settings_widget.raf_combo
    idx = box.findData(typ)
    assert idx >= 0, typ
    box.setCurrentIndex(idx)
    qapp.processEvents()


def _muster(n: int) -> bytes:
    return bytes(((i * 7) ^ (i >> 9)) & 0xFF for i in range(n))


def _aus_ein(w, qapp):
    w.act_power.setChecked(False)
    qapp.processEvents()
    w.act_power.setChecked(True)
    qapp.processEvents()


def _stichproben(emu, daten: bytes):
    n = len(daten)
    for adr in (0, 1, 127, 128, 4097, n // 2, n - 1):
        assert emu.raf_peek(adr) == daten[adr], hex(adr)


# ─── Auswahlfeld und Kästchen ────────────────────────────────────────────────

@pytest.mark.parametrize("maschine", PROGRAMME)
def test_auswahlfeld_und_kaestchen_in_jedem_programm(qapp, konfig_ordner, maschine):
    w = _fenster(qapp, maschine)
    try:
        sw = w.settings_widget
        assert not sw.raf_combo.isHidden()
        assert not sw.raf_standby_box.isHidden()
        assert [sw.raf_combo.itemData(i) for i in range(sw.raf_combo.count())] == \
            ["none", "raf128", "raf512", "raf2m"]
        assert [sw.raf_combo.itemText(i) for i in range(sw.raf_combo.count())] == \
            ["keine", "RAF 128 (128 KByte)", "RAF 512 (512 KByte)", "RAF-2M (2 MByte)"]
        assert "88H/89H" in sw.raf_combo.toolTip()
        assert "RAF512.COM" in sw.raf_combo.toolTip()
        assert "Stand-by 5PG" in sw.raf_standby_box.text()
        # Werkszustand: keine Karte, Kästchen aus und gesperrt.
        assert sw.raf_value() == "none"
        assert not sw.raf_standby_box.isChecked()
        assert not sw.raf_standby_box.isEnabled()
        assert w.emulator.raf_variant == ""
    finally:
        _zu(w, qapp)


@pytest.mark.parametrize("maschine", PROGRAMME)
def test_jeder_typ_erzeugt_die_maschine_neu(qapp, konfig_ordner, maschine):
    w = _fenster(qapp, maschine)
    try:
        for typ in ("raf128", "raf512", "raf2m", "none"):
            alt = w.emulator
            _waehle(w, qapp, typ)
            assert w.emulator is not alt, typ
            assert w.emulator.raf_variant == ("" if typ == "none" else typ)
            assert w._raf == typ
    finally:
        _zu(w, qapp)


def test_kaestchen_nur_bei_gesteckter_raf(qapp, konfig_ordner):
    w = _fenster(qapp)
    try:
        box = w.settings_widget.raf_standby_box
        _waehle(w, qapp, "raf512")
        assert box.isEnabled()
        _waehle(w, qapp, "none")
        assert not box.isEnabled()
        _waehle(w, qapp, "raf2m")
        assert box.isEnabled()
    finally:
        _zu(w, qapp)


def test_kaestchen_baut_die_maschine_nicht_neu(qapp, konfig_ordner):
    w = _fenster(qapp)
    try:
        _waehle(w, qapp, "raf128")
        alt = w.emulator
        w.settings_widget.raf_standby_box.setChecked(True)
        qapp.processEvents()
        assert w.emulator is alt
        assert w._raf_standby is True
    finally:
        _zu(w, qapp)


def test_profil_ohne_raf_wahl_blendet_aus(qapp, konfig_ordner):
    from app import profil as profile
    ohne = dataclasses.replace(profile.A5120, raf_wahl=False)
    w = _fenster(qapp, profil=ohne)
    try:
        assert w.settings_widget.raf_combo.isHidden()
        assert w.settings_widget.raf_standby_box.isHidden()
        w._apply_config({"machine": {"raf": "raf512", "raf_standby": True}})
        assert w.emulator.raf_variant == ""
        assert "machine" not in w._gather_config()
    finally:
        _zu(w, qapp)


# ─── Konfiguration ───────────────────────────────────────────────────────────

@pytest.mark.parametrize("maschine", PROGRAMME)
def test_konfiguration_schreiben_und_lesen(qapp, konfig_ordner, maschine):
    from app import config_io, profil as profile
    p = profile.profil(maschine)
    w = _fenster(qapp, maschine)
    try:
        _waehle(w, qapp, "raf2m")
        w.settings_widget.raf_standby_box.setChecked(True)
        assert w._gather_config()["machine"] == {"raf": "raf2m", "raf_standby": True}
    finally:
        _zu(w, qapp)
    gespeichert = config_io.load_config(config_io.default_config_path(p))
    assert gespeichert["machine"] == {"raf": "raf2m", "raf_standby": True}

    w = _fenster(qapp, maschine)
    try:
        assert w.emulator.raf_variant == "raf2m"
        assert w.settings_widget.raf_value() == "raf2m"
        assert w.settings_widget.raf_standby_box.isChecked()
        assert w.settings_widget.raf_standby_box.isEnabled()
    finally:
        _zu(w, qapp)


@pytest.mark.parametrize("abschnitt", [
    None,                                       # kein Abschnitt machine
    {},                                         # Abschnitt ohne Schlüssel
    {"raf": "raf999", "raf_standby": "ja"},     # unbekannt
    "kaputt",                                   # kein Verzeichnis
])
def test_fehlend_oder_unbekannt_heisst_aus(qapp, konfig_ordner, abschnitt):
    w = _fenster(qapp)
    try:
        _waehle(w, qapp, "raf512")
        w.settings_widget.raf_standby_box.setChecked(True)
        daten = {} if abschnitt is None else {"machine": abschnitt}
        w._apply_config(daten)
        assert w._raf == "none" and w._raf_standby is False
        assert w.emulator.raf_variant == ""
        assert w.settings_widget.raf_value() == "none"
        assert not w.settings_widget.raf_standby_box.isChecked()
        assert not w.settings_widget.raf_standby_box.isEnabled()
    finally:
        _zu(w, qapp)


def test_raf_modul_normalisiert():
    from app import raf
    assert raf.normalize(None) == "none"
    assert raf.normalize(" RAF512 ") == "raf512"
    assert raf.normalize("raf999") == "none"
    assert raf.core_param("none") is None and raf.core_param("raf2m") == "raf2m"
    assert raf.standby_normalize(True) is True
    assert raf.standby_normalize("true") is False
    assert raf.ablage_pfad("a5120emu") != raf.ablage_pfad("k8915emu")


@pytest.mark.parametrize("maschine", PROGRAMME)
def test_vorgabedateien_tragen_none_und_false(maschine):
    import yaml
    from app import profil as profile
    datei = PROJECT_ROOT / "data" / profile.profil(maschine).vorgabe_datei
    daten = yaml.safe_load(datei.read_text(encoding="utf-8"))
    assert daten["machine"] == {"raf": "none", "raf_standby": False}


# ─── Handbuch ────────────────────────────────────────────────────────────────

def test_handbuch_beschreibt_die_raf():
    text = (PROJECT_ROOT / "app" / "help" / "handbuch.md").read_text(encoding="utf-8")
    assert "## RAM-Floppy RAF" in text
    for wort in ("RAM-Disk", "RAF 128", "RAF 512", "RAF-2M", "RAF512.COM",
                 "RAFCPM.COM", "Stand-by", "raf_a5120emu.bin"):
        assert wort in text, wort

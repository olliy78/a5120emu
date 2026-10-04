"""RAM-Floppy RAF in der Oberfläche: Stand-by-Ablage (AP-R4, doc/design/22_raf512.md §7.1).

Ausgelagert aus ``test_raf_gui.py`` (dort Auswahl, Konfiguration, Handbuch), damit
kein ctest-Fall an die 300-s-Frist heranreicht.  Gemeinsamer Teil wie dort.

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


# ─── Stand-by ────────────────────────────────────────────────────────────────

@pytest.mark.parametrize("maschine", PROGRAMME)
def test_standby_rundreise_ueber_aus_ein_und_neustart(qapp, konfig_ordner, tmp_path,
                                                      maschine):
    from app import profil as profile, raf
    ablage = Path(raf.ablage_pfad(profile.profil(maschine).programm))
    assert ablage.parent == konfig_ordner
    daten = _muster(GROESSE["raf128"])
    quelle = tmp_path / "vorbereitet.bin"
    quelle.write_bytes(daten)

    w = _fenster(qapp, maschine)
    try:
        _waehle(w, qapp, "raf128")
        w.settings_widget.raf_standby_box.setChecked(True)
        qapp.processEvents()
        assert w.emulator.raf_load(quelle)
        _aus_ein(w, qapp)                       # Netz-Ein verwirft — Stand-by hält
        _stichproben(w.emulator, daten)
        assert ablage.read_bytes() == daten
    finally:
        _zu(w, qapp)

    # Neues Programm, neue Maschine: Konfiguration + Ablage stellen alles her.
    ablage.write_bytes(daten)
    w = _fenster(qapp, maschine)
    try:
        assert w.settings_widget.raf_value() == "raf128"
        assert w.settings_widget.raf_standby_box.isChecked()
        assert w.emulator.raf_variant == "raf128"
        _stichproben(w.emulator, daten)
    finally:
        _zu(w, qapp)


def test_standby_ueber_neuaufbau_der_maschine(qapp, konfig_ordner, tmp_path):
    """Ein Neuaufbau (hier: Laufwerkswechsel) schreibt erst und lädt dann."""
    daten = _muster(GROESSE["raf512"])
    quelle = tmp_path / "vorbereitet.bin"
    quelle.write_bytes(daten)
    w = _fenster(qapp)
    try:
        _waehle(w, qapp, "raf512")
        w.settings_widget.raf_standby_box.setChecked(True)
        assert w.emulator.raf_load(quelle)
        alt = w.emulator
        w._apply_drive_types(w._drive_types, cold_restart=True)
        assert w.emulator is not alt
        _stichproben(w.emulator, daten)
    finally:
        _zu(w, qapp)


def test_ohne_standby_keine_datei_und_inhalt_weg(qapp, konfig_ordner, tmp_path):
    from app import raf
    ablage = Path(raf.ablage_pfad("a5120emu"))
    daten = _muster(GROESSE["raf128"])
    quelle = tmp_path / "vorbereitet.bin"
    quelle.write_bytes(daten)
    w = _fenster(qapp)
    try:
        _waehle(w, qapp, "raf128")
        assert w.emulator.raf_load(quelle)
        _aus_ein(w, qapp)
        assert all(w.emulator.raf_peek(a) == 0 for a in (1, 127, 4097))
    finally:
        _zu(w, qapp)
    assert not ablage.exists()


def test_ohne_standby_bleibt_eine_vorhandene_ablage_unberuehrt(qapp, konfig_ordner):
    from app import raf
    ablage = Path(raf.ablage_pfad("a5120emu"))
    alt = _muster(GROESSE["raf512"])
    ablage.write_bytes(alt)
    w = _fenster(qapp)
    try:
        _waehle(w, qapp, "raf128")              # andere Größe, kein Stand-by
        assert w.emulator.raf_peek(1) == 0      # nicht geladen
        _aus_ein(w, qapp)
    finally:
        _zu(w, qapp)
    assert ablage.read_bytes() == alt


def test_ablage_falscher_groesse_wird_nicht_geladen_und_ersetzt(qapp, konfig_ordner,
                                                               tmp_path):
    from app import raf
    ablage = Path(raf.ablage_pfad("a5120emu"))
    daten = _muster(GROESSE["raf512"])
    quelle = tmp_path / "vorbereitet.bin"
    quelle.write_bytes(daten)
    w = _fenster(qapp)
    try:
        _waehle(w, qapp, "raf512")
        w.settings_widget.raf_standby_box.setChecked(True)
        assert w.emulator.raf_load(quelle)
        # Wechsel auf RAF 128: die 512-K-Ablage wird geschrieben, passt aber nicht.
        _waehle(w, qapp, "raf128")
        assert w.emulator.raf_variant == "raf128"
        assert ablage.stat().st_size == GROESSE["raf512"]
        assert all(w.emulator.raf_peek(a) == 0 for a in (1, 127, 4097))
        meldung = w.statusBar().currentMessage()
        assert "RAM-Disk" in meldung and "nicht geladen" in meldung
    finally:
        _zu(w, qapp)
    # Beim Beenden ersetzt: jetzt 128 KByte.
    assert ablage.stat().st_size == GROESSE["raf128"]


def test_ablage_falscher_groesse_beim_start(qapp, konfig_ordner):
    from app import config_io, profil as profile, raf
    ablage = Path(raf.ablage_pfad("k8915emu"))
    ablage.write_bytes(bytes(GROESSE["raf2m"]))
    pfad = config_io.default_config_path(profile.K8915)
    daten = config_io.standard_konfiguration(profile.K8915)
    daten["machine"] = {"raf": "raf128", "raf_standby": True}
    config_io.save_config(pfad, daten)
    w = _fenster(qapp, "k8915")
    try:
        assert w.emulator.raf_variant == "raf128"
        assert "nicht geladen" in getattr(w, "_raf_hinweis", "")
    finally:
        _zu(w, qapp)



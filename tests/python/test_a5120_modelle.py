"""A5120-Ausführungen in Bindung und Oberfläche — Entwurf 30, AP A5.

Die Bestückung selbst prüfen die C++-Wächter (`A5120Ausfuehrung.*`); hier geht es um den
Weg ``k1520_create_a5120_ausfuehrung`` → ``K1520Emulator(ausfuehrung=…)`` bzw.
``machine="a5120-alt"``, um die Modellzeilen des Programmprofils ``a5120emu`` (zwei neue
Modelle, 8″ gesperrt), die Bildschirmtastatur je Modell (K7636 ⇄ K7637) und darum, dass
eine Konfiguration ohne Modell der A5120 „neu“ bleibt.
"""

from pathlib import Path

import pytest

from conftest import requires_core

pytestmark = requires_core


def _emu(**kw):
    from app.core_binding.k1520 import K1520Emulator
    return K1520Emulator(**kw)


# ── Bindung ──────────────────────────────────────────────────────────────────

def test_vorgabe_ist_die_ausfuehrung_neu():
    e = _emu()
    assert e.ausfuehrung == "neu"
    assert e.framebuffer_size() == (640, 288)
    assert e.machine == "a5120"


@pytest.mark.parametrize("kw", [dict(ausfuehrung="alt"), dict(machine="a5120-alt")])
def test_alt_hat_den_kleinen_bildschirm(kw):
    e = _emu(**kw)
    assert e.ausfuehrung == "alt"
    assert e.machine == "a5120-alt"
    assert e.framebuffer_size() == (512, 256)
    assert not e.tape_installed()
    e.mem_write(0xFC00, ord("A"))
    e.mem_write(0xFC00 + 15 * 64, ord("Z"))
    zeilen = e.screen_text().split("\n")
    assert len(zeilen) == 16 and all(len(z) == 64 for z in zeilen)
    assert zeilen[0][0] == "A" and zeilen[15][0] == "Z"
    assert e.keyboard_leds() == 0


def test_kmbg_hat_die_kassettenkarte_fest():
    e = _emu(machine="a5120-kmbg")
    assert e.ausfuehrung == "kmbg"
    assert e.tape_installed()
    # tape=True ist am KMBG ohne Wirkung — kein „steckt bereits“-Fehler.
    e2 = _emu(ausfuehrung="kmbg", tape=True)
    assert e2.tape_installed()


def test_laufwerke_der_ausfuehrung_und_eigene():
    e = _emu(ausfuehrung="alt")
    assert e.drive_default_format(3) == ""          # Vorgabe: Steckplatz 4 frei
    e = _emu(ausfuehrung="alt", drive_types=["K5601", "K5601", "none", "none"])
    assert e.drive_default_format(0) != e.drive_default_format(2)


def test_acht_lehnt_der_kern_mit_grund_ab():
    with pytest.raises(RuntimeError, match="K5602"):
        _emu(ausfuehrung="acht")


def test_falsche_kombinationen():
    with pytest.raises(ValueError):
        _emu(ausfuehrung="alt", em="em256")
    with pytest.raises(ValueError):
        _emu(machine="k8915", ausfuehrung="alt")
    with pytest.raises(ValueError):
        _emu(ausfuehrung="gibtsnicht")
    with pytest.raises(ValueError):
        _emu(machine="a5120-alt", ausfuehrung="kmbg")


def test_ausfuehrung_an_anderen_maschinen_leer():
    assert _emu(machine="k8915").ausfuehrung == ""


# ── Programmprofil ───────────────────────────────────────────────────────────

def test_profil_kennt_die_fruehen_modelle():
    from app import profil
    p = profil.profil("a5120")
    assert p.standard_modell() == "a5120"
    assert p.modell_maschine("a5120-alt") == "a5120-alt"
    assert p.modell_maschine("a5120-kmbg") == "a5120-kmbg"
    assert p.modell_tastatur("a5120-alt") == "k7636"
    assert p.modell_tastatur("a5120-kmbg") == "k7636"
    assert p.modell_tastatur("a5120") == "k7637"
    assert p.modell_em("a5120-alt") is None
    namen = {m[0]: m[3] for m in p.modelle}
    assert namen["a5120-alt"] == "A5120 (64×16, K7636)"
    assert namen["a5120-kmbg"] == "A5120 Kassette"
    assert [g[0] for g in p.gesperrte_modelle] == ["a5120-8"]


def test_migration_fehlendes_modell_ist_neu():
    from app import profil
    p = profil.profil("a5120")
    assert p.modell_normalisieren(None) == "a5120"
    assert p.modell_normalisieren("") == "a5120"
    assert p.modell_normalisieren("a5120-8") == "a5120"   # gesperrt = nicht wählbar
    # Andere Programme kennen die frühen A5120 nicht.
    assert profil.profil("k8915").modell_normalisieren("a5120-alt") == "k8915"


# ── Oberfläche ───────────────────────────────────────────────────────────────

@pytest.fixture
def konfig_ordner(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


def _fenster(qapp):
    from app import profil
    from app.ui.main_window import MainWindow
    w = MainWindow(None, profil=profil.profil("a5120"))
    w.show()
    qapp.processEvents()
    return w


def _zu(w, qapp):
    w.close()
    qapp.processEvents()


def test_modellwahl_tauscht_maschine_und_tastatur(qapp, konfig_ordner):
    from app import config_io
    from app.ui.keyboard import KeyboardWidget
    from app.ui.keyboard_k7636 import KeyboardK7636Widget

    w = _fenster(qapp)
    try:
        assert w._model == "a5120"
        assert w.emulator.ausfuehrung == "neu"
        assert type(w.keyboard_widget) is KeyboardWidget
        combo = w.settings_widget.model_combo
        i = combo.findData("a5120-8")
        assert i >= 0 and not combo.model().item(i).isEnabled()
        assert combo.findData("a5120-alt") >= 0 and combo.findData("a5120-kmbg") >= 0

        w._on_model_selected("a5120-alt")
        qapp.processEvents()
        assert w.emulator.ausfuehrung == "alt"
        assert w.emulator.framebuffer_size() == (512, 256)
        assert isinstance(w.keyboard_widget, KeyboardK7636Widget)
        assert w.screen_widget.key_sink is w.keyboard_widget
        assert not w.kassette_dock.toggleViewAction().isVisible()

        w._on_model_selected("a5120-kmbg")
        qapp.processEvents()
        assert w.emulator.ausfuehrung == "kmbg"
        assert w.emulator.tape_installed()
        assert w.kassette_dock.toggleViewAction().isVisible()
        assert isinstance(w.keyboard_widget, KeyboardK7636Widget)
        w._autosave_now()
        cfg = config_io.load_config(str(konfig_ordner / "a5120emu.yaml"))
        assert cfg["general"]["model"] == "a5120-kmbg"
    finally:
        _zu(w, qapp)

    # Neustart: das gemerkte Modell kommt wieder; zurück zum „neu“ mit der K7637.
    w = _fenster(qapp)
    try:
        assert w._model == "a5120-kmbg" and w.emulator.ausfuehrung == "kmbg"
        w._on_model_selected("a5120")
        qapp.processEvents()
        assert w.emulator.ausfuehrung == "neu"
        assert type(w.keyboard_widget) is KeyboardWidget
        assert w.emulator.framebuffer_size() == (640, 288)
    finally:
        _zu(w, qapp)


def test_konfiguration_ohne_modell_startet_den_a5120_neu(qapp, konfig_ordner):
    (konfig_ordner / "a5120emu.yaml").write_text(
        "version: 1\ngeneral: {}\n", encoding="utf-8")
    w = _fenster(qapp)
    try:
        assert w._model == "a5120" and w.emulator.ausfuehrung == "neu"
    finally:
        _zu(w, qapp)

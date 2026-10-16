"""A5120-Ausfuehrungen in Bindung und Oberflaeche — Entwurf 30, AP A5.

Die Bestueckung selbst pruefen die C++-Waechter (`A5120Ausfuehrung.*`); hier geht es um den
Weg ``k1520_create_a5120_ausfuehrung`` → ``K1520Emulator(ausfuehrung=…)`` bzw.
``machine="a5120-alt"``, um die Modellzeilen des Programmprofils ``a5120emu`` (drei neue
Modelle, darunter „BCU 880“ mit Lade-ROM V0.6 ueber ``k1520_create_a5120_konfig``, 8″
gesperrt), die Bildschirmtastatur je Modell (K7636 ⇄ K7637) und darum, dass
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


# ── Lade-ROM und Tastatur (k1520_create_a5120_konfig, Modell „BCU 880“) ─────────

def test_laderom_und_tastatur_vorgaben():
    e = _emu()
    assert (e.laderom, e.tastatur) == ("v09_2", "k7637")
    e = _emu(ausfuehrung="alt")
    assert (e.laderom, e.tastatur) == ("v09_2", "k7636")
    assert _emu(machine="k8915").laderom == ""


def test_bcu_bestueckung_ueber_die_bindung():
    e = _emu(machine="a5120-alt", laderom="v06", tastatur="k7636")
    assert (e.ausfuehrung, e.laderom, e.tastatur) == ("alt", "v06", "k7636")
    assert e.framebuffer_size() == (512, 256)
    # Satz 1 der BCU-Disketten: K7024 80×24 mit der K7636 („neu“ + tastatur).
    e = _emu(laderom="v06", tastatur="k7636")
    assert (e.ausfuehrung, e.laderom, e.tastatur, e.machine) == ("neu", "v06", "k7636", "a5120")
    assert e.framebuffer_size() == (640, 288)
    e = _emu(laderom="v09_1")
    assert (e.laderom, e.tastatur) == ("v09_1", "k7637")


def test_laderom_und_tastatur_falsch():
    with pytest.raises(ValueError):
        _emu(laderom="v07")
    with pytest.raises(ValueError):
        _emu(tastatur="k7672")
    with pytest.raises(ValueError):
        _emu(laderom="v06", em="em256")
    with pytest.raises(ValueError):
        _emu(machine="k8915", laderom="v06")
    with pytest.raises(RuntimeError, match="K7637"):   # Kern: am „alt“ keine SIO-Tastatur
        _emu(ausfuehrung="alt", tastatur="k7637")


def test_c_abi_lehnt_unbekannte_werte_mit_grund_ab():
    from app.core_binding import k1520
    lib = k1520._lib
    for args in ((1, 3, 0), (1, 0, 3), (4, 0, 0)):
        h = lib.k1520_create_a5120_konfig(*args, None, None, None, None)
        assert not h, args
        assert lib.k1520_last_init_error()
    h = lib.k1520_create_a5120_konfig(1, 2, 0, None, None, None, None)
    try:
        assert (lib.k1520_a5120_laderom(h), lib.k1520_a5120_tastatur(h)) == (2, 2)
    finally:
        lib.k1520_destroy(h)


def test_bcu_lieferdiskette_bootet_ueber_die_bindung(tmp_path):
    """Die ausgelieferte Startdiskette (disks/a5120_bcu/, nur als Kopie) mit der Bestueckung des
    Modells „A5120 BCU 880“ und den Laufwerken K5600.10 bis zum Prompt."""
    import shutil
    from conftest import ausgelieferte_diskette
    from app import profil
    p = profil.profil("a5120")
    kopie = tmp_path / "bcu_start.hfe"
    shutil.copyfile(ausgelieferte_diskette("a5120_bcu12_k5600-10_start-syl.hfe"), kopie)
    e = _emu(drive_types=["K5600.10", "K5600.10", "K5600.10", "none"],
             machine=p.modell_maschine("a5120-bcu"), **p.kern_parameter("a5120-bcu", {}))
    assert e.laderom == "v06"
    assert e.mount_disk(0, str(kopie), e.drive_default_format(0))
    e.power_on()
    for _ in range(400):
        e.run(100_000)
        if "\nA>" in e.screen_text():
            break
    assert "BCU 880 VERS 1.2" in e.screen_text()
    assert "\nA>" in e.screen_text(), e.screen_text()


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
    assert namen["a5120-bcu"] == "A5120 BCU 880 (Lade-ROM V0.6)"
    assert p.modell_maschine("a5120-bcu") == "a5120-alt"
    assert p.modell_tastatur("a5120-bcu") == "k7636"
    assert p.kern_parameter("a5120-bcu", {}) == {"laderom": "v06", "tastatur": "k7636"}
    assert p.kern_parameter("a5120-alt", {}) == {} and p.kern_parameter("a5120", {}) == {}
    assert [g[0] for g in p.gesperrte_modelle] == ["a5120-8"]


def test_migration_fehlendes_modell_ist_neu():
    from app import profil
    p = profil.profil("a5120")
    assert p.modell_normalisieren(None) == "a5120"
    assert p.modell_normalisieren("") == "a5120"
    assert p.modell_normalisieren("a5120-8") == "a5120"   # gesperrt = nicht waehlbar
    # Andere Programme kennen die fruehen A5120 nicht.
    assert profil.profil("k8915").modell_normalisieren("a5120-alt") == "k8915"


# ── Oberflaeche ───────────────────────────────────────────────────────────────

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

    # Neustart: das gemerkte Modell kommt wieder; zurueck zum „neu“ mit der K7637.
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


def test_modell_bcu_baut_lade_rom_v06_und_k7636(qapp, konfig_ordner):
    """Das Modell „A5120 BCU 880“ = Ausfuehrung „alt“ + Lade-ROM V0.6 + K7636; die Laufwerke
    bleiben die des Anwenders (Merkposten a5120_alt Nr. 14), das Modell kommt nach dem Neustart
    wieder, und zurueck zum „alt“ steckt wieder das Vorgabe-ROM."""
    from app import config_io
    from app.ui.keyboard_k7636 import KeyboardK7636Widget

    w = _fenster(qapp)
    try:
        combo = w.settings_widget.model_combo
        i = combo.findData("a5120-bcu")
        assert i >= 0 and combo.model().item(i).isEnabled()
        laufwerke = list(w._drive_types)
        w._on_model_selected("a5120-bcu")
        qapp.processEvents()
        assert (w.emulator.ausfuehrung, w.emulator.laderom, w.emulator.tastatur) == ("alt", "v06", "k7636")
        assert isinstance(w.keyboard_widget, KeyboardK7636Widget)
        assert w.emulator.framebuffer_size() == (512, 256)
        assert list(w._drive_types) == laufwerke
        w._autosave_now()
        cfg = config_io.load_config(str(konfig_ordner / "a5120emu.yaml"))
        assert cfg["general"]["model"] == "a5120-bcu"
    finally:
        _zu(w, qapp)

    w = _fenster(qapp)
    try:
        assert w._model == "a5120-bcu" and w.emulator.laderom == "v06"
        w._on_model_selected("a5120-alt")
        qapp.processEvents()
        assert (w.emulator.ausfuehrung, w.emulator.laderom) == ("alt", "v09_2")
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

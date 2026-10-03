"""prg710emu — der PRG710 Emulator als drittes Gesicht des Hauptfensters (AP-P5d).

doc/design/20_prg710.md AP-P5d: Programmprofil (Titel, Konfiguration, Takt
2,4576 MHz), Modellwahl PRG 710 / PRG 710-1 mit Tastaturtausch, Bildschirmtastatur
K7609 (ET ohne Kürzel), ``keyRelease`` wird gemeldet, Schnittstellenreiter je
Variante, Konfiguration getrennt von den anderen Programmen.
"""

from pathlib import Path

import pytest

from conftest import PROJECT_ROOT, requires_core, run_until_text

pytestmark = requires_core

QK_TASTE_BASE = 0x03000000


@pytest.fixture
def konfig_ordner(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


def _fenster(qapp, maschine="prg710"):
    from app import profil
    from app.ui.main_window import MainWindow
    w = MainWindow(None, profil=profil.profil(maschine))
    w.show()
    qapp.processEvents()
    return w


def _zu(w, qapp):
    w.close()
    qapp.processEvents()


def _menue(w, titel):
    for aktion in w.menuBar().actions():
        if aktion.text() == titel:
            return list(aktion.menu().actions())
    raise AssertionError(f"kein Menü {titel}")


def _mitte(kw, taste):
    unit, ox, oy = kw._geometry()
    return kw._rect_of(taste, unit, ox, oy).center().toPoint()


def test_profil_und_titel(qapp, konfig_ordner):
    from app import profil
    p = profil.profil("prg710")
    assert (p.programm, p.titel, p.konfig_datei) == ("prg710emu", "PRG710 Emulator",
                                                      "prg710emu.yaml")
    assert p.nenntakt_hz == 2_457_600 and p.nenntakt_text == "2,4576 MHz"
    assert p.modellwahl and not p.frontplatte
    w = _fenster(qapp)
    try:
        assert w.windowTitle() == "PRG710 Emulator"
        assert w.emulator.machine == "prg710" and w.emulator.prg_variant == 0
        assert w.CPU_HZ == 2_457_600
        assert w._drive_types[:2] == ["K5601", "K5601"]
        assert not hasattr(w, "act_nmi")
    finally:
        _zu(w, qapp)


def test_modellwahl_tauscht_maschine_und_tastatur(qapp, konfig_ordner):
    from app import config_io
    from app.ui.keyboard_k7609 import KeyboardK7609Widget
    from app.ui.keyboard_k7672 import KeyboardK7672Widget

    w = _fenster(qapp)
    try:
        assert not w.settings_widget.model_combo.isHidden()
        assert isinstance(w.keyboard_widget, KeyboardK7609Widget)
        assert w.emulator.serial_count() == 4   # K8025 ×3 + Fernschreiber (AP-P8c)
        w._on_model_selected("prg710-1")
        qapp.processEvents()
        assert w.emulator.machine == "prg710-1" and w.emulator.prg_variant == 1
        assert isinstance(w.keyboard_widget, KeyboardK7672Widget)
        assert w.screen_widget.key_sink is w.keyboard_widget
        # Schnittstellen je Variante: das 710-1 hat eine weniger (A32-B trägt die Tastatur).
        assert w.emulator.serial_count() == 3
        w._autosave_now()
        cfg = config_io.load_config(str(konfig_ordner / "prg710emu.yaml"))
        assert cfg["general"]["model"] == "prg710-1"
    finally:
        _zu(w, qapp)

    # Neustart: das gemerkte Modell kommt wieder, mit seiner Tastatur.
    w = _fenster(qapp)
    try:
        assert w._model == "prg710-1"
        assert isinstance(w.keyboard_widget, KeyboardK7672Widget)
        w._on_model_selected("prg710")
        assert w.emulator.serial_count() == 4
        assert isinstance(w.keyboard_widget, KeyboardK7609Widget)
    finally:
        _zu(w, qapp)


def test_unbekanntes_modell_wird_die_vorgabe(qapp, konfig_ordner):
    (konfig_ordner / "prg710emu.yaml").write_text(
        "version: 1\ngeneral: {model: a5120.16}\n", encoding="utf-8")
    w = _fenster(qapp)
    try:
        assert w._model == "prg710" and w.emulator.prg_variant == 0
    finally:
        _zu(w, qapp)


def test_k7609_tabelle_stimmt_mit_der_csv(qapp):
    import csv
    from app.ui import keyboard_k7609 as k

    zeilen = {int(r["code_hex"], 16): r for r in csv.DictReader(
        (PROJECT_ROOT / "doc" / "prg710" / "k7609_codes.csv").open(encoding="utf-8"))}
    for code, lo, up in k.ZEICHEN:
        r = zeilen[code]
        assert (r["zeichen"], r["shift_zeichen"]) == (lo, up), hex(code)
    w = k.KeyboardK7609Widget()
    for t in w._keys:
        if t.code is not None:
            assert t.code == QK_TASTE_BASE | t.pos and 0 <= t.pos <= 0x3F
    assert w._by_pos[0x37].low == "ET1" and w._by_pos[0x38].low == "ET2"
    offen = [t for t in w._keys if t.kind == "dead"]
    assert len(offen) == 10 and all("[?]" in t.name for t in offen)


def test_et_ohne_kuerzel_und_keyrelease(qapp, konfig_ordner):
    """ET1 der Bildschirmtastatur sendet 37H, das Loslassen wird gemeldet."""
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QAction
    from PySide6.QtTest import QTest

    w = _fenster(qapp)
    try:
        for a in w.findChildren(QAction):
            s = a.shortcut().toString()
            assert not s or s == "F11" or s.startswith("Ctrl+Shift+"), s
        gedrueckt, losgelassen = [], []
        w.emulator.key_press = lambda c, s=False, ct=False: gedrueckt.append(c)
        w.emulator.key_release = lambda c: losgelassen.append(c)
        kw = w.keyboard_widget
        kw.resize(900, 300)
        mitte = _mitte(kw, kw._by_pos[0x37])
        QTest.mousePress(kw, Qt.LeftButton, Qt.NoModifier, mitte)
        QTest.mouseRelease(kw, Qt.LeftButton, Qt.NoModifier, mitte)
        assert gedrueckt == [QK_TASTE_BASE | 0x37]
        assert losgelassen == [QK_TASTE_BASE | 0x37]
        # Offene Taste: federt zurück, sendet nichts.
        tot = next(t for t in kw._keys if t.low == "S1")
        QTest.mouseClick(kw, Qt.LeftButton, Qt.NoModifier, _mitte(kw, tot))
        assert gedrueckt == [QK_TASTE_BASE | 0x37]
    finally:
        _zu(w, qapp)


def test_bildschirmtastatur_startet_das_boot_rom(qapp):
    """Echter Lauf: NKM-LOADER, ET1 der Nachbildung → „DISKERROR C2“ (ohne Diskette)."""
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    from app.core_binding.k1520 import K1520Emulator
    from app.ui.keyboard_k7609 import KeyboardK7609Widget

    emu = K1520Emulator(machine="prg710")
    kw = KeyboardK7609Widget()
    kw.keyPressed.connect(lambda c, s, ct: emu.key_press(c, s, ct))
    kw.keyReleased.connect(lambda c: emu.key_release(c))
    emu.power_on()
    assert run_until_text(emu, "NKM-LOADER", 20_000_000)
    emu.run(3_000_000)
    kw.resize(900, 300)
    mitte = _mitte(kw, kw._by_pos[0x37])
    QTest.mousePress(kw, Qt.LeftButton, Qt.NoModifier, mitte)
    emu.run(100_000)
    QTest.mouseRelease(kw, Qt.LeftButton, Qt.NoModifier, mitte)
    emu.run(10_000_000)
    assert "DISKERROR C2" in emu.screen_text()


def test_werkzeugmenue_kennt_alle_emulatoren(qapp, konfig_ordner, monkeypatch):
    from app import programme
    gerufen = []
    monkeypatch.setattr(programme, "programm_starten",
                        lambda k, *a, **kw: gerufen.append(k))
    erwartet = {"a5120": programme.EMULATOR, "k8915": programme.K8915EMU,
                "prg710": programme.PRG710EMU}
    for maschine in erwartet:
        w = _fenster(qapp, maschine)
        try:
            menue = _menue(w, "&Werkzeuge")
            for andere, kennung in erwartet.items():
                aktion = getattr(w, f"act_{andere}emu", None)
                if andere == maschine:
                    assert aktion is None
                    continue
                assert aktion in menue and aktion.shortcut().isEmpty()
                aktion.trigger()
                assert gerufen[-1] == kennung
        finally:
            _zu(w, qapp)


def test_konfiguration_getrennt_und_vorgabe_als_datei(qapp, konfig_ordner):
    from app import config_io, paths, profil

    a5120 = konfig_ordner / "a5120emu.yaml"
    a5120.write_text("version: 1\ngeneral: {speed: 5.0}\n", encoding="utf-8")
    vorher = a5120.read_bytes()
    w = _fenster(qapp)
    try:
        assert (konfig_ordner / "prg710emu.yaml").is_file()
        w._autosave_now()
    finally:
        _zu(w, qapp)
    assert a5120.read_bytes() == vorher
    assert not (konfig_ordner / "config.yaml").exists()
    assert paths.default_config_file("default_config_prg710.yaml") is not None
    std = config_io.standard_konfiguration(profil.PRG710)
    assert std["general"]["model"] == "prg710"
    assert "disks" not in std and "geometry" not in std.get("window", {})


def test_cli_hilfe_und_pfade():
    import subprocess
    import sys
    main = PROJECT_ROOT / "app" / "main.py"
    r = subprocess.run([sys.executable, str(main), "--machine", "prg710", "--help"],
                       capture_output=True, text=True, encoding="utf-8")
    assert r.returncode == 0 and "prg710emu" in r.stdout and "zwei Abbilder" in r.stdout
    r = subprocess.run([sys.executable, str(main), "--machine", "prg710", "--paths"],
                       capture_output=True, text=True, encoding="utf-8")
    assert "Vorgabe (PRG710)" in r.stdout


def test_starter_und_handbuch():
    starter = (PROJECT_ROOT / "run_prg710emu.sh").read_text(encoding="utf-8")
    assert "--machine prg710" in starter
    hb = (PROJECT_ROOT / "app" / "help" / "handbuch.md").read_text(encoding="utf-8")
    assert "## Der PRG710 Emulator" in hb and "ET1" in hb


# ─── EPROMmer (AP-P7c) ───────────────────────────────────────────────────────

EPROM_AKTIONEN = ("eprom_einlegen", "eprom_leer", "eprom_speichern", "eprom_loeschen",
                  "eprom_entnehmen")


def test_eprom_kasten_nur_im_prg_und_ohne_kuerzel(qapp, konfig_ordner):
    from app.ui import actions
    w = _fenster(qapp)
    try:
        assert w.eprom_dock is not None and w.eprom_dock.windowTitle() == "EPROMmer"
        assert w._aktion("dock_eprom") is not None
        for name in EPROM_AKTIONEN:
            a = getattr(w, f"act_{name}")
            assert a.shortcut().isEmpty(), name          # Kürzeltabelle = Vertrag
        # Im Menü steht alles: Maschine ▸ EPROMmer und Ansicht ▸ EPROMmer.
        from PySide6.QtWidgets import QMenu
        unter = [m for m in w.menuBar().findChildren(QMenu) if m.title() == "E&PROMmer"]
        assert len(unter) == 1
        assert unter[0].actions() == [getattr(w, f"act_{n}") for n in EPROM_AKTIONEN]
        assert w.act_dock_eprom in _menue(w, "&Ansicht")
        # Die Knöpfe des Kastens sind die Aktionen selbst.
        assert [k.defaultAction() for k in w.eprom_widget.knoepfe] == \
            [getattr(w, f"act_{n}") for n in EPROM_AKTIONEN]
        assert "dock_eprom" in actions.reihenfolge("prg710")
    finally:
        _zu(w, qapp)
    w = _fenster(qapp, "a5120")
    try:
        assert w.eprom_dock is None and not hasattr(w, "act_eprom_einlegen")
        assert "dock_eprom" not in actions.reihenfolge("a5120")
    finally:
        _zu(w, qapp)


def test_eprom_bedienung_ueber_die_aktionen(qapp, konfig_ordner, tmp_path, monkeypatch):
    from PySide6.QtWidgets import QFileDialog
    abbild = tmp_path / "quelle.bin"
    abbild.write_bytes(bytes(range(256)) * 8)                     # 2 KB → U2716
    ziel = tmp_path / "gebrannt.bin"
    monkeypatch.setattr(QFileDialog, "getOpenFileName",
                        staticmethod(lambda *a, **k: (str(abbild), "")))
    monkeypatch.setattr(QFileDialog, "getSaveFileName",
                        staticmethod(lambda *a, **k: (str(ziel), "")))
    w = _fenster(qapp)
    try:
        assert "leer" in w.eprom_widget.sockel.text()
        w.act_eprom_einlegen.trigger()
        assert w.emulator.eprom_type() == 2
        assert "U2716" in w.eprom_widget.sockel.text()
        assert "quelle.bin" in w.eprom_widget.sockel.text()
        w.act_eprom_loeschen.trigger()
        assert "geändert" in w.eprom_widget.sockel.text()
        w.act_eprom_speichern.trigger()
        assert ziel.read_bytes() == b"\xff" * 2048
        assert "geändert" not in w.eprom_widget.sockel.text()
        u555 = [a for a in w.act_eprom_leer.menu().actions() if "U555" in a.text()][0]
        u555.trigger()
        assert w.emulator.eprom_type() == 1
        w.act_eprom_entnehmen.trigger()
        assert w.emulator.eprom_type() == 0
        text = w.eprom_widget.protokoll_text()
        for teil in ("U2716 eingelegt", "UV-gelöscht", "gespeichert nach", "U555 eingelegt",
                     "entnommen"):
            assert teil in text, (teil, text)
    finally:
        _zu(w, qapp)


def test_eprom_sockel_ueberlebt_den_neubau_der_maschine(qapp, konfig_ordner):
    """Ein Neubau der Maschine (Laufwerkswechsel; beim Modellwechsel fragt AP-P9 erst
    nach) — das PROM wandert mit, samt „geändert“."""
    w = _fenster(qapp)
    try:
        w.emulator.eprom_insert_data(b"\x01\x02\x03", 1, "", True)
        w._apply_drive_types(["K5601", "K5601", "none", "none"], cold_restart=True)
        qapp.processEvents()
        assert w.emulator.prg_variant == 0
        assert w.emulator.eprom_type() == 1 and w.emulator.eprom_modified()
        assert w.emulator.eprom_read()[:4] == b"\x01\x02\x03\xff"
        assert w.eprom_widget.emulator is w.emulator
    finally:
        _zu(w, qapp)

"""pc1715emu — der PC1715 Emulator als viertes Gesicht des Hauptfensters (AP-5a).

doc/design/21_pc1715.md §8.3: Programmprofil (Titel, Konfiguration, Takt 2,458 MHz),
Modellwahl (Bildschirm K7222/K7221; der 1715W ist gesperrt), Bildschirmtastatur im
1715-Tastenbild (physische Matrixpositionen, gemerkte Umschalter), Bildgröße samt
Statuszeile, Schnittstellenreiter Drucker X4 / V.24 X5, Konfiguration getrennt von den
anderen Programmen und ein Lauf bis `A>` mit Eingabe über die Bildschirmtastatur.
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


def _fenster(qapp, maschine="pc1715"):
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


def _klick(kw, p):
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    QTest.mouseClick(kw, Qt.LeftButton, Qt.NoModifier, _mitte(kw, kw._by_pos[p]))


def test_profil_und_titel(qapp, konfig_ordner):
    from app import profil
    p = profil.profil("pc1715")
    assert (p.programm, p.titel, p.konfig_datei, p.vorgabe_datei) == (
        "pc1715emu", "PC1715 Emulator", "pc1715emu.yaml", "default_config_pc1715.yaml")
    assert p.nenntakt_hz == 2_458_000 and p.nenntakt_text == "2,458 MHz"
    assert p.modellwahl and not p.frontplatte and not p.eprommer
    assert "nmi" not in p.eigene_aktionen
    w = _fenster(qapp)
    try:
        assert w.windowTitle() == "PC1715 Emulator"
        assert w.emulator.machine == "pc1715"
        assert w.CPU_HZ == 2_458_000
        assert w._drive_types[:2] == ["K5601", "K5601"]
        assert not hasattr(w, "act_nmi")
        # Bildgröße samt Statuszeile (25. Zeile von CP/A).
        assert w.emulator.framebuffer_size() == (640, 300)
    finally:
        _zu(w, qapp)


def test_modellwahl_bildschirm_und_gesperrter_1715w(qapp, konfig_ordner):
    from app import config_io, profil
    from app.ui.keyboard_pc1715 import KeyboardPc1715Widget

    p = profil.profil("pc1715")
    # PC 1715W: sichtbar, aber nicht wählbar und in einer Konfiguration unbekannt.
    assert [g[0] for g in p.gesperrte_modelle] == ["pc1715w"]
    assert p.modell_normalisieren("pc1715w") == "pc1715"
    with pytest.raises(ValueError):
        from app.core_binding.k1520 import K1520Emulator
        K1520Emulator(machine="pc1715w")

    w = _fenster(qapp)
    try:
        combo = w.settings_widget.model_combo
        assert not combo.isHidden()
        i1715w = combo.findData("pc1715w")
        assert i1715w >= 0 and not combo.model().item(i1715w).isEnabled()
        assert combo.itemData(i1715w, 3)          # Qt.ToolTipRole: die Begründung
        assert isinstance(w.keyboard_widget, KeyboardPc1715Widget)
        w._on_model_selected("pc1715-k7221")
        qapp.processEvents()
        assert w.emulator.machine == "pc1715-k7221"
        assert w.emulator.framebuffer_size() == (512, 255)
        assert w.screen_widget.key_sink is w.keyboard_widget
        w._autosave_now()
        cfg = config_io.load_config(str(konfig_ordner / "pc1715emu.yaml"))
        assert cfg["general"]["model"] == "pc1715-k7221"
    finally:
        _zu(w, qapp)

    # Neustart: das gemerkte Modell kommt wieder.
    w = _fenster(qapp)
    try:
        assert w._model == "pc1715-k7221"
        assert w.emulator.framebuffer_size() == (512, 255)
        # Das Bild läuft mit der neuen Größe (Textur wird im GL-Kontext neu angelegt).
        w.emulator.power_on()
        w.emulator.run(200_000)
        w.screen_widget._on_update()
        assert (w.screen_widget._fb_w, w.screen_widget._fb_h) == (512, 255)
    finally:
        _zu(w, qapp)


def test_unbekanntes_modell_wird_die_vorgabe(qapp, konfig_ordner):
    (konfig_ordner / "pc1715emu.yaml").write_text(
        "version: 1\ngeneral: {model: pc1715w}\n", encoding="utf-8")
    w = _fenster(qapp)
    try:
        assert w._model == "pc1715" and w.emulator.machine == "pc1715"
    finally:
        _zu(w, qapp)


def test_schnittstellenreiter_drucker_x4_und_v24_x5(qapp, konfig_ordner):
    w = _fenster(qapp)
    try:
        namen = [(b.info.name, b.info.stecker) for b in w.serial_widget.bloecke()]
        assert namen == [("Drucker", "X4"), ("V.24", "X5")]
    finally:
        _zu(w, qapp)


def test_tastenbild_ist_die_matrix():
    from app.ui import keyboard_pc1715 as k
    from app.ui.keyboard_pc1715 import KeyboardPc1715Widget

    w = KeyboardPc1715Widget()
    tasten = [t for t in w._keys if t.pos >= 0]
    positionen = [t.pos for t in tasten]
    assert len(positionen) == len(set(positionen)), "jede Matrixposition höchstens einmal"
    assert all(0 <= p < 13 * 8 for p in positionen)
    for t in tasten:
        assert t.code == QK_TASTE_BASE | t.pos
    # 13 × 8 = 104 Positionen, davon sieben ohne Taste im Schaltbild und die zweite
    # Ziffernblock-Taste S (zweimal in der Matrix, hier zwei Kappen).
    assert len(positionen) >= 90
    # Return = ET (3,4); SI/SO (8,7); LOCK (8,5); REP (8,6); F1 (4,7); Leertaste (12,1).
    assert (k.ET, k.SISO, k.LOCK, k.REP) == (28, 71, 69, 70)
    assert w._by_pos[k.pos(4, 7)].low == "F1" and w._by_pos[k.pos(12, 1)].name.startswith("Leertaste")
    for n in range(1, 16):
        assert w._by_pos[k.FTASTEN[n]].low == f"F{n}"
    # Ziffernblock: 00 (2,1) und CE (0,6).
    assert w._by_pos[k.pos(2, 1)].low == "00" and w._by_pos[k.pos(0, 6)].low == "CE"
    # Alle Zeichentasten tragen ihr Zeichen.
    for p, (lo, up) in k._ZEICHEN.items():
        t = w._by_pos[p]
        assert lo.upper() == t.low or lo == t.low, (p, lo, t.low)


def test_bildschirmtastatur_sendet_gemerkte_umschalter_vor_der_taste(qapp, konfig_ordner):
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QAction
    from PySide6.QtTest import QTest
    from app.ui import keyboard_pc1715 as k

    w = _fenster(qapp)
    try:
        for a in w.findChildren(QAction):
            s = a.shortcut().toString()
            assert not s or s == "F11" or s.startswith("Ctrl+Shift+"), s
        ereignis = []
        w.emulator.key_press = lambda c, s=False, ct=False: ereignis.append(("v", c))
        w.emulator.key_release = lambda c: ereignis.append(("^", c))
        kw = w.keyboard_widget
        kw.resize(1200, 300)
        taste = lambda p: QK_TASTE_BASE | p

        # Gewöhnliche Taste: 'a' = (7,5).
        _klick(kw, k.pos(7, 5))
        assert ereignis == [("v", taste(k.pos(7, 5))), ("^", taste(k.pos(7, 5)))]

        # SHIFT merken (sendet noch nichts), dann 'a': erst Shift, dann a; lösen umgekehrt.
        ereignis.clear()
        _klick(kw, k.SHIFT_L)
        assert ereignis == [] and kw._shift
        _klick(kw, k.pos(7, 5))
        assert ereignis == [("v", taste(k.SHIFT_L)), ("v", taste(k.pos(7, 5))),
                            ("^", taste(k.pos(7, 5))), ("^", taste(k.SHIFT_L))]
        assert not kw._shift, "gilt nur für die eine Taste"

        # CTRL und REP zusammen mit 'c'.
        ereignis.clear()
        _klick(kw, k.CTRL)
        _klick(kw, k.REP)
        _klick(kw, k.pos(1, 4))
        assert [e for e in ereignis if e[0] == "v"] == [
            ("v", taste(k.CTRL)), ("v", taste(k.REP)), ("v", taste(k.pos(1, 4)))]
        assert ereignis[-1] == ("^", taste(k.CTRL))

        # LOCK und SI/SO rasten im ROM: ein Klick = Druck + Loslassen, Anzeige schaltet um.
        ereignis.clear()
        _klick(kw, k.LOCK)
        assert ereignis == [("v", taste(k.LOCK)), ("^", taste(k.LOCK))]
        assert kw.lock_active() and kw._is_active(kw._by_pos[k.LOCK])
        _klick(kw, k.LOCK)
        assert not kw.lock_active()
        ereignis.clear()
        _klick(kw, k.SISO)
        assert kw._is_active(kw._by_pos[k.SISO]) and ereignis[0] == ("v", taste(k.SISO))

        # ET (Return) und Leertaste.
        ereignis.clear()
        _klick(kw, k.ET)
        assert ereignis[0] == ("v", taste(k.ET))

        # Host-Tastatur: Pfeil → physische Taste, Zeichen → ASCII.
        from PySide6.QtGui import QKeyEvent
        from PySide6.QtCore import QEvent
        ev = QKeyEvent(QEvent.KeyPress, Qt.Key_Up, Qt.NoModifier)
        assert kw.map_host_key(ev) == (taste(k.pos(5, 7)), False, False)
        ev = QKeyEvent(QEvent.KeyPress, Qt.Key_F3, Qt.ShiftModifier)
        assert kw.map_host_key(ev) == (taste(k.FTASTEN[3]), False, False)
        ev = QKeyEvent(QEvent.KeyPress, Qt.Key_A, Qt.NoModifier, "a")
        assert kw.map_host_key(ev) == (ord("a"), False, False)
    finally:
        _zu(w, qapp)


def test_scp_bootet_in_der_oberflaeche_und_nimmt_die_bildschirmtastatur_an(
        qapp, konfig_ordner, temp_disk):
    """Echter Lauf im Fenster: SCP 1715 bis `A>`, dann „DIR“ über die Nachbildung."""
    w = _fenster(qapp)
    try:
        emu = w.emulator
        emu.set_key_repeat_realtime(True)
        assert emu.mount_disk(0, temp_disk("pc1715_scp1715_v0006_boot.hfe"), "cpa800", False), \
            emu.last_error()
        emu.power_on()
        assert run_until_text(emu, "A>", 60_000_000), emu.screen_text()
        # Batch 5 000 Takte, sobald die Tastatur im Spiel ist (wie K7637).
        kw = w.keyboard_widget
        kw.resize(1200, 300)
        from app.ui import keyboard_pc1715 as k
        from PySide6.QtCore import Qt
        from PySide6.QtTest import QTest

        def laufe(n):
            for _ in range(n // 5000):
                emu.run(5000)

        # Shift + 'd' → großes D, i, r, ET.
        _klick(kw, k.SHIFT_L)
        QTest.mousePress(kw, Qt.LeftButton, Qt.NoModifier, _mitte(kw, kw._by_pos[k.pos(1, 5)]))
        laufe(400_000)
        QTest.mouseRelease(kw, Qt.LeftButton, Qt.NoModifier, _mitte(kw, kw._by_pos[k.pos(1, 5)]))
        laufe(300_000)
        assert "A>D" in emu.screen_text()
        # Der Rest von „DIR“ und ET: jede Taste einzeln gehalten.
        for p in (k.pos(9, 0), k.pos(0, 0), k.ET):
            if p != k.ET:
                _klick(kw, k.SHIFT_L)          # jeder Buchstabe einzeln groß (Shift wird je Klick gemerkt)
            m = _mitte(kw, kw._by_pos[p])
            QTest.mousePress(kw, Qt.LeftButton, Qt.NoModifier, m)
            laufe(300_000)
            QTest.mouseRelease(kw, Qt.LeftButton, Qt.NoModifier, m)
            laufe(300_000)
        laufe(3_000_000)
        text = emu.screen_text()
        assert "A>DIR" in text and "INSTSCP" in text, text
    finally:
        _zu(w, qapp)


def test_werkzeugmenue_kennt_alle_emulatoren(qapp, konfig_ordner, monkeypatch):
    from app import programme
    gerufen = []
    monkeypatch.setattr(programme, "programm_starten",
                        lambda k, *a, **kw: gerufen.append(k))
    erwartet = {"a5120": programme.EMULATOR, "k8915": programme.K8915EMU,
                "prg710": programme.PRG710EMU, "pc1715": programme.PC1715EMU}
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
        assert (konfig_ordner / "pc1715emu.yaml").is_file()
        w._autosave_now()
    finally:
        _zu(w, qapp)
    assert a5120.read_bytes() == vorher
    assert paths.default_config_file("default_config_pc1715.yaml") is not None
    std = config_io.standard_konfiguration(profil.PC1715)
    assert std["general"]["model"] == "pc1715"
    assert "disks" not in std and "geometry" not in std.get("window", {})


def test_cli_hilfe_und_pfade():
    import subprocess
    import sys
    main = PROJECT_ROOT / "app" / "main.py"
    r = subprocess.run([sys.executable, str(main), "--machine", "pc1715", "--help"],
                       capture_output=True, text=True, encoding="utf-8")
    assert r.returncode == 0 and "pc1715emu" in r.stdout and "zwei Abbilder" in r.stdout
    r = subprocess.run([sys.executable, str(main), "--machine", "pc1715", "--paths"],
                       capture_output=True, text=True, encoding="utf-8")
    assert r.returncode == 0 and "Vorgabe (PC1715)" in r.stdout
    assert "NICHT GEFUNDEN" not in r.stdout.split("Vorgabe (PC1715)")[1].splitlines()[0]


def test_starter_und_handbuch():
    starter = (PROJECT_ROOT / "run_pc1715emu.sh").read_text(encoding="utf-8")
    assert "--machine pc1715" in starter
    hb = (PROJECT_ROOT / "app" / "help" / "handbuch.md").read_text(encoding="utf-8")
    assert "## Der PC1715 Emulator" in hb and "SHIFT" in hb and "PC 1715W" in hb

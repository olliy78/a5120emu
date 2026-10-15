"""mc80emu — der MC80 Emulator als sechstes Gesicht des Hauptfensters (AP M6).

doc/design/31_mc80.md, doc/design/32_… §6 M6: Programmprofil (Titel, Konfiguration, Takt
2,4576 MHz, Modelle .30/.31, .33/.21/.22 gesperrt, SPE-Zahl), Bild 512 × 256 aus
``k1520_framebuffer`` über dasselbe CRT-Widget, keine Laufwerke, Bildschirmtastatur auf
Zeichenebene (Bild A5 der BDA), Speicherabzug laden, Werkzeugmenü, Konfiguration getrennt
von den anderen Programmen, Starter und Handbuch.
"""

from pathlib import Path

import pytest

from conftest import PROJECT_ROOT, requires_core

pytestmark = requires_core

SPIELE = PROJECT_ROOT / "doc" / "mc80" / "spiele"


@pytest.fixture
def konfig_ordner(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


def _fenster(qapp, maschine="mc80"):
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


def _laufe(emu, takte, schritt=5000):
    """In kleinen Scheiben fahren (Tastatur im Spiel: 5 000 Takte, tests/README.md)."""
    n = 0
    while n < takte:
        n += emu.run(schritt)


def _bis_text(emu, nadel, takte=12_000_000):
    n = 0
    while n < takte:
        n += emu.run(200_000)
        if nadel in emu.screen_text(25):
            return True
    return False


def _gestoppt(w):
    """Die Maschine nur noch von Hand fahren (der Emulationsfaden steht)."""
    w.run_timer.stop()
    return w.emulator


def _taste(kw, qapp, taste):
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    unit, ox, oy = kw._geometry()
    m = kw._rect_of(taste, unit, ox, oy).center().toPoint()
    QTest.mouseClick(kw, Qt.LeftButton, Qt.NoModifier, m)
    qapp.processEvents()


def _per_beschriftung(kw, text):
    return next(k for k in kw._keys if k.low == text and k.x < 16.5)


def _tippen_bildschirmtastatur(w, qapp, text, nach=60_000):
    """Jede Taste einzeln anklicken, dazwischen die Maschine fahren (20 ms Tastenabstand)."""
    kw = w.keyboard_widget
    for ch in text:
        if ch == "\n":
            taste = _per_beschriftung(kw, "ENTER")
        else:
            taste = next(k for k in kw._keys if k.code == ord(ch))
        _taste(kw, qapp, taste)
        _laufe(w.emulator, nach)


def _init(w, qapp):
    """Kaltstart bis zur Meldung, dann INIT (legt die RAM-Kette an, ~2,5 Mio. Takte)."""
    emu = _gestoppt(w)
    assert _bis_text(emu, "Version 3.0"), emu.screen_text(25)
    for _ in range(100):                       # bis der Prompt steht (Kommandoschleife)
        if _zeile(emu, 24) == ">":
            break
        _laufe(emu, 200_000, 50_000)
    _laufe(emu, 500_000)
    _tippen_bildschirmtastatur(w, qapp, "init")
    _laufe(emu, 300_000)
    assert _zeile(emu, 24) == ">INIT", emu.screen_text(25)     # Echo der Bildschirmtastatur
    _tippen_bildschirmtastatur(w, qapp, "\n")
    _laufe(emu, 4_000_000)
    # INIT legt die ADT an (LDIR 0C71H → FC00H) und meldet sich wieder mit dem Prompt.
    assert _zeile(emu, 24) == ">", emu.screen_text(25)
    assert emu.mem_read(0xFC00) == emu.mem_read(0x0C71)
    return emu


def _zeile(emu, n):
    return emu.screen_text(25).split("\n")[n].rstrip()


# ── Profil, Fenster, Bild ────────────────────────────────────────────────────

def test_profil_und_titel(qapp, konfig_ordner):
    from app import profil
    p = profil.profil("mc80")
    assert (p.programm, p.titel, p.konfig_datei, p.vorgabe_datei) == (
        "mc80emu", "MC80 Emulator", "mc80emu.yaml", "default_config_mc80.yaml")
    assert p.nenntakt_hz == 2_457_600 and p.nenntakt_text == "2,4576 MHz"
    assert p.modellwahl and not p.frontplatte and not p.eprommer and not p.terminal
    assert not p.laufwerke and not p.raf_wahl and not p.ptape_wahl
    assert p.tape_wahl, "Kassette K5020 an 30H seit AP M7"
    w = _fenster(qapp)
    try:
        assert w.windowTitle() == "MC80 Emulator"
        assert w.emulator.machine == "mc80"
        assert w.CPU_HZ == 2_457_600
        assert w.emulator.framebuffer_size() == (512, 256)
        assert not hasattr(w, "act_nmi") and not hasattr(w, "act_stand_speichern")
        # Keine Laufwerke: Kasten, Menüeinträge, Reiter und Statusfelder fehlen.
        assert w.drives_dock.isHidden()
        assert not w.drives_dock.toggleViewAction().isVisible()
        assert not w.act_einlegen.isVisible() and not w.act_auswerfen.isVisible()
        tabs = w.settings_widget.tabs
        assert not tabs.isTabVisible(tabs.indexOf(w.settings_widget._drives_tab))
        assert w.status_widget.felder() == []
        from app.ui.keyboard_mc80 import KeyboardMc80Widget
        assert isinstance(w.keyboard_widget, KeyboardMc80Widget)
        assert w.screen_widget.key_sink is w.keyboard_widget
    finally:
        _zu(w, qapp)


def test_modellwahl_gesperrte_modelle_und_spe(qapp, konfig_ordner):
    from app import config_io, profil
    p = profil.profil("mc80")
    assert [m[0] for m in p.modelle] == ["mc80", "mc80-31"]
    assert [g[0] for g in p.gesperrte_modelle] == ["mc80-33", "mc80-21", "mc80-22"]
    assert p.modell_normalisieren("mc80-33") == "mc80"      # gesperrt = unbekannt
    assert p.kern_parameter("mc80", {"spe": "2"}) == {"spe": "2"}
    assert p.kern_parameter("mc80", {}) == {"spe": "1"}

    w = _fenster(qapp)
    try:
        combo = w.settings_widget.model_combo
        assert not combo.isHidden()
        for gesperrt in ("mc80-33", "mc80-21", "mc80-22"):
            i = combo.findData(gesperrt)
            assert i >= 0 and not combo.model().item(i).isEnabled(), gesperrt
        i31 = combo.findData("mc80-31")
        assert i31 >= 0 and combo.model().item(i31).isEnabled()
        w._on_model_selected("mc80-31")
        qapp.processEvents()
        assert w.emulator.machine == "mc80-31"
        assert w.drives_dock.isHidden()           # auch nach dem Neuaufbau
        w._on_hardware_selected("spe", "2")
        qapp.processEvents()
        assert w._hardware == {"spe": "2"}
        assert w.emulator.machine == "mc80-31"
        w._autosave_now()
        cfg = config_io.load_config(str(konfig_ordner / "mc80emu.yaml"))
        assert cfg["general"]["model"] == "mc80-31"
        assert str(cfg["general"]["spe"]) == "2"
        assert "disks" not in cfg or not cfg["disks"]
    finally:
        _zu(w, qapp)


def test_bindung_kennt_beide_varianten_und_die_spe_zahl():
    from app.core_binding.k1520 import K1520Emulator, MACHINE_TYPES
    assert MACHINE_TYPES["mc80"] == MACHINE_TYPES["mc80-31"] == 6
    for maschine, spe in (("mc80", 1), ("mc80-31", 2), ("mc80", "2")):
        emu = K1520Emulator(machine=maschine, spe=spe)
        assert emu.machine == maschine and emu.machine_type() == 6
        assert emu.framebuffer_size() == (512, 256)
        del emu
    with pytest.raises(RuntimeError):
        K1520Emulator(machine="mc80", spe=3)


def test_egos_meldung_erscheint_im_framebuffer(qapp, konfig_ordner):
    """Das Bild kommt als Bitmap (00H/FFH je Bildpunkt) aus k1520_framebuffer; der Text der
    Meldung ist über k1520_screen_char (Mustervergleich) und das Kontextmenü lesbar."""
    w = _fenster(qapp)
    try:
        emu = _gestoppt(w)
        assert _bis_text(emu, "Version 3.0"), emu.screen_text(25)
        fb = bytes(emu.get_framebuffer())
        assert len(fb) == 512 * 256 and set(fb) == {0x00, 0xFF}
        assert fb.count(0xFF) > 500                    # Schrift steht im Bild
        text = w.screen_widget.bildschirmtext()
        assert "mc 80.30. microcomputer" in text and "Version 3.0" in text
        assert len(text.split("\n")) == 25
        # Das Widget holt das Bild ab (Größe aus dem Kern, ohne GL im Offscreen-Betrieb).
        w.screen_widget._bild_holen()
        assert (w.screen_widget._fb_w, w.screen_widget._fb_h) == (512, 256)
        assert w.act_text_kopieren.shortcut().isEmpty()
    finally:
        _zu(w, qapp)


def test_taste_kommt_an_bildschirmtastatur_und_pc_tastatur(qapp, konfig_ordner):
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    w = _fenster(qapp)
    try:
        emu = _init(w, qapp)                   # INIT über die Bildschirmtastatur
        # Die PC-Tastatur: über das Bild, durch die Nachbildung (EGOS: „a“ → „A“).
        w.screen_widget.setFocus()
        for ch in "cat":
            QTest.keyClick(w.screen_widget, getattr(Qt, f"Key_{ch.upper()}"), Qt.NoModifier, 1)
            _laufe(emu, 60_000)
        _laufe(emu, 600_000)
        assert _zeile(emu, 24) == ">CAT", emu.screen_text(25)
    finally:
        _zu(w, qapp)


def test_tastenbild_der_bildschirmtastatur():
    from PySide6.QtCore import Qt
    from app.ui import keyboard_mc80 as k
    tasten = k._build_layout_mc80()
    nach_text = {t.low: t for t in tasten if t.x < 16.5}
    assert nach_text["ENTER"].code == int(Qt.Key_Return)
    assert nach_text["OFF"].code == int(Qt.Key_Escape)
    assert nach_text["⇥"].code == int(Qt.Key_Tab)
    s = [t for t in tasten if t.low.startswith("S") and len(t.low) == 2]
    assert sorted(t.code - int(Qt.Key_F1) for t in s) == list(range(8))
    # Buchstaben: klein ohne, groß mit Umschaltung; Ziffern mit Zweitbelegung.
    assert (nach_text["Q"].code, nach_text["Q"].shift_code) == (ord("q"), ord("Q"))
    assert (nach_text["1"].code, nach_text["1"].shift_code) == (ord("1"), ord("!"))
    # Unbekannte Codes senden nichts.
    for name in ("↑", "↓", "←", "→", "CL", "CRLF"):
        assert nach_text[name].kind == "dead" and nach_text[name].code is None, name
    # 61 alphanumerische + Sonderfunktionen, 8 Steuertasten, 20 im Ziffernblock [BDA S.12].
    assert len([t for t in tasten if t.x >= 19.4]) == 20
    assert len(s) == 8
    assert sum(1 for t in tasten if t.kind == "shift") == 2


# ── Speicherabzug laden ──────────────────────────────────────────────────────

def test_ladeadresse_aus_dem_dateinamen():
    from app.ui.main_window import ladeadresse_aus_namen
    assert ladeadresse_aus_namen("/x/tetris_4000_4fff.bin") == "4000"
    assert ladeadresse_aus_namen("pharao_6400-94e0.bin") == "6400"
    assert ladeadresse_aus_namen("wall_8000-93ff.bin") == "8000"
    assert ladeadresse_aus_namen("irgendwas.bin") == "4000"


def test_speicherabzug_laden_und_tetris_starten(qapp, konfig_ordner):
    """Menü *Maschine ▸ Speicherabzug laden…* (kein Kürzel): TETRIS nach INIT ab 4000H,
    gestartet mit `te` + ENTER — das Bild ändert sich (Entwurf 31 §4, Wächter Mc80Spiele)."""
    datei = SPIELE / "tetris_4000_4fff.bin"
    w = _fenster(qapp)
    try:
        assert w.act_speicherabzug_laden in _menue(w, "&Maschine")
        assert w.act_speicherabzug_laden.shortcut().isEmpty()
        emu = _init(w, qapp)
        assert w._speicherabzug_laden(str(datei), 0x4000) == 4096
        inhalt = datei.read_bytes()
        assert bytes(emu.mem_read(0x4000 + i) for i in range(16)) == inhalt[:16]
        assert "4096 Byte ab 4000H" in w.statusBar().currentMessage()
        vorher = bytes(emu.get_framebuffer())
        _tippen_bildschirmtastatur(w, qapp, "te\n")
        _laufe(emu, 6_000_000, 50_000)
        assert bytes(emu.get_framebuffer()) != vorher
        # Fehler kommen als Meldung, nicht als Ausnahme.
        from PySide6.QtWidgets import QMessageBox
        gemeldet = []
        orig = QMessageBox.warning
        QMessageBox.warning = staticmethod(lambda *a, **kw: gemeldet.append(a))
        try:
            assert w._speicherabzug_laden(str(datei), 0xF800) == 0
        finally:
            QMessageBox.warning = orig
        assert gemeldet and "10000H" in str(gemeldet[0])
    finally:
        _zu(w, qapp)


# ── Kassette (AP M7) ─────────────────────────────────────────────────────────

@requires_core
@pytest.mark.parametrize("modell", ["mc80", "mc80-31"])
def test_kassette_folgt_der_karte_und_legt_kompaktkassetten_an(qapp, konfig_ordner, tmp_path, modell):
    """K5020 an 30H: Kasten „Kassette“ nur mit Karte; eine neu angelegte .k52 ist eine
    Kompaktkassette ohne BOT-/EOT-Marke mit 80 mm Vorspann (doc/mc80/kassette.md §3)."""
    import struct
    w = _fenster(qapp)
    try:
        if modell != "mc80":
            w._on_model_selected(modell)
            qapp.processEvents()
        assert w.emulator.machine == modell
        box = w.settings_widget.tape_box
        assert not box.isChecked() and not w.emulator.tape_installed()
        assert not w.kassette_dock.isVisible()
        box.setChecked(True)
        qapp.processEvents()
        assert w.emulator.tape_installed() and w.kassette_dock.isVisible()
        assert w.emulator.machine == modell
        band = tmp_path / "egos.k52"
        assert w.kassette_widget.laufwerke[0].einlegen(str(band))
        assert w.emulator.tape_status(0)["loaded"] and band.exists()
        laenge, vorspann, abstand, marke, _ = struct.unpack_from("<IIIII", band.read_bytes(), 12)
        assert (vorspann, abstand, marke) == (80_000, 0, 0)
        assert w.emulator.tape_eject(0)
        assert w.emulator.tape_new(0) and w.emulator.tape_status(0)["loaded"]
    finally:
        _zu(w, qapp)


# ── Werkzeugmenü, Kürzel, Konfiguration, Starter ─────────────────────────────

def test_werkzeugmenue_kennt_alle_emulatoren(qapp, konfig_ordner, monkeypatch):
    from app import programme
    gerufen = []
    monkeypatch.setattr(programme, "programm_starten",
                        lambda k, *a, **kw: gerufen.append(k))
    erwartet = {"a5120": programme.EMULATOR, "k8915": programme.K8915EMU,
                "prg710": programme.PRG710EMU, "pc1715": programme.PC1715EMU,
                "p8000": programme.P8000EMU, "mc80": programme.MC80EMU}
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


def test_kein_kuerzel_des_mc80_fensters_stiehlt_eine_taste(qapp, konfig_ordner):
    from PySide6.QtGui import QAction
    from test_gui_smoke import _als_kuerzel, _handbuch_kuerzel
    w = _fenster(qapp)
    try:
        fremd = [f"{a.shortcut().toString()} ({a.text()})"
                 for a in w.findChildren(QAction)
                 if a.shortcut().toString()
                 and a.shortcut().toString() != "F11"
                 and not a.shortcut().toString().startswith("Ctrl+Shift+")]
        assert fremd == []
        im_handbuch = {_als_kuerzel(k).toString() for k in _handbuch_kuerzel()}
        fehlend = [a.shortcut().toString() for a in w.findChildren(QAction)
                   if not a.shortcut().isEmpty()
                   and a.shortcut().toString() not in im_handbuch]
        assert fehlend == []
    finally:
        _zu(w, qapp)


def test_konfiguration_getrennt_und_vorgabe_als_datei(qapp, konfig_ordner):
    from app import config_io, paths, profil
    a5120 = konfig_ordner / "a5120emu.yaml"
    a5120.write_text("version: 1\ngeneral: {speed: 5.0}\n", encoding="utf-8")
    vorher = a5120.read_bytes()
    w = _fenster(qapp)
    try:
        assert (konfig_ordner / "mc80emu.yaml").is_file()
        w._autosave_now()
    finally:
        _zu(w, qapp)
    assert a5120.read_bytes() == vorher
    assert paths.default_config_file("default_config_mc80.yaml") is not None
    std = config_io.standard_konfiguration(profil.MC80)
    assert std["general"]["model"] == "mc80"
    assert "disks" not in std and "drive_types" not in std
    assert "geometry" not in std.get("window", {})
    assert "speicherabzug_laden" in std["window"]["toolbar"]
    assert "dock_drives" not in std["window"]["toolbar"]


def test_cli_hilfe_und_pfade(tmp_path):
    import subprocess
    import sys
    main = PROJECT_ROOT / "app" / "main.py"
    r = subprocess.run([sys.executable, str(main), "--machine", "mc80", "--help"],
                       capture_output=True, text=True, encoding="utf-8")
    assert r.returncode == 0 and "mc80emu" in r.stdout and "keine Abbilder" in r.stdout
    r = subprocess.run([sys.executable, str(main), "--machine", "mc80", "--paths"],
                       capture_output=True, text=True, encoding="utf-8")
    assert r.returncode == 0 and "Vorgabe (MC80)" in r.stdout
    assert "NICHT GEFUNDEN" not in r.stdout.split("Vorgabe (MC80)")[1].splitlines()[0]
    # Eine Diskette auf der Kommandozeile: die Maschine hat keine Laufwerke.
    d = tmp_path / "x.hfe"
    d.write_bytes(b"\0")
    r = subprocess.run([sys.executable, str(main), "--machine", "mc80", str(d)],
                       capture_output=True, text=True, encoding="utf-8")
    assert r.returncode == 2 and "keine" in r.stderr


def test_starter_und_handbuch():
    starter = (PROJECT_ROOT / "run_mc80emu.sh").read_text(encoding="utf-8")
    assert "--machine mc80" in starter
    hb = (PROJECT_ROOT / "app" / "help" / "handbuch.md").read_text(encoding="utf-8")
    assert "## Der MC80 Emulator" in hb and "Speicherabzug laden" in hb and "INIT" in hb
    # Die Kürzeltabelle bleibt hinter dem Kapitel (Vertrag).
    assert hb.index("## Der MC80 Emulator") < hb.index("## Tastenkürzel")

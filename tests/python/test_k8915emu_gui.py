"""k8915emu — der K8915 Emulator als eigenes Programm (AP-UI1).

doc/design/18_k8915emu_oberflaeche.md §4: dasselbe Hauptfenster mit dem
Programmprofil des K8915 — eigener Titel, eigene Konfigurationsdatei, zwei
Laufwerke, sechs Lampen der Frontplatte in der Statuszeile, NMI-Taster neben
Reset, Bildschirmtastatur K7672.  Und umgekehrt: der A5120 Emulator bleibt, wie
er war — bis auf Titel und Namen seiner Konfiguration (mit einmaligem Umzug der
alten ``config.yaml``).
"""

import os
from pathlib import Path

import pytest

from conftest import PROJECT_ROOT, requires_core, run_until_text

pytestmark = requires_core

EPROM_D3 = PROJECT_ROOT / "doc" / "EPROMS" / "K7672" / "7672.03-D3.bin"


@pytest.fixture
def konfig_ordner(tmp_path, monkeypatch):
    """Eigenes Konfigurationsverzeichnis je Test — dort liegen beide Dateien."""
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


def _fenster(qapp, maschine="k8915", disks=None):
    from app import profil
    from app.ui.main_window import MainWindow
    w = MainWindow(disks, profil=profil.profil(maschine))
    w.show()
    qapp.processEvents()
    return w


def _menue(w, titel):
    """Die Einträge des Menüs *titel* — sofort als Liste (die Hülle des QMenu
    überlebt den Ausdruck sonst nicht)."""
    for aktion in w.menuBar().actions():
        if aktion.text() == titel:
            return list(aktion.menu().actions())
    raise AssertionError(f"kein Menü {titel}")


def _zu(w, qapp):
    w.close()
    qapp.processEvents()


# ─── Profil, Titel, Konfiguration ────────────────────────────────────────────

def test_titles_and_machines_of_the_two_programs(qapp, konfig_ordner):
    for maschine, titel in (("a5120", "A5120 Emulator"), ("k8915", "K8915 Emulator")):
        w = _fenster(qapp, maschine)
        try:
            assert w.windowTitle() == titel
            assert w.emulator.machine == maschine
        finally:
            _zu(w, qapp)


def test_k8915emu_reads_and_writes_its_own_config_and_leaves_the_others_alone(
        qapp, konfig_ordner):
    """k8915emu schreibt ``k8915emu.yaml`` — ``config.yaml``/``a5120emu.yaml``
    bleiben unberührt, und umgekehrt."""
    from app import config_io

    alt = konfig_ordner / "config.yaml"
    alt.write_text("version: 1\ngeneral: {speed: 2.0}\n", encoding="utf-8")
    a5120 = konfig_ordner / "a5120emu.yaml"
    a5120.write_text("version: 1\ngeneral: {speed: 5.0}\n", encoding="utf-8")
    vorher = (alt.read_bytes(), a5120.read_bytes())

    w = _fenster(qapp, "k8915")
    try:
        k8915 = konfig_ordner / "k8915emu.yaml"
        assert k8915.is_file(), "erster Start schreibt die eigene Konfiguration"
        w._apply_speed(1.0)
        w._autosave_now()
        assert config_io.load_config(str(k8915))["general"]["speed"] == 1.0
        assert config_io.load_config(str(k8915))["drive_types"][:2] == ["K5601", "K5601"]
    finally:
        _zu(w, qapp)
    assert (alt.read_bytes(), a5120.read_bytes()) == vorher

    # Umgekehrt: der A5120 liest SEINE Datei (Tempo 5) und fasst k8915emu.yaml nicht an.
    k8915_vorher = (konfig_ordner / "k8915emu.yaml").read_bytes()
    w = _fenster(qapp, "a5120")
    try:
        assert w.speed_factor == 5.0
    finally:
        _zu(w, qapp)
    assert (konfig_ordner / "k8915emu.yaml").read_bytes() == k8915_vorher
    assert alt.read_bytes() == vorher[0], "a5120emu.yaml gibt es — kein Umzug"


def test_the_old_config_yaml_moves_once_to_a5120emu_yaml(qapp, konfig_ordner, capsys):
    """Umzug: ``config.yaml`` ohne ``a5120emu.yaml`` wird EINMAL umbenannt (nicht
    kopiert), mit Vermerk im Protokoll — und ihr Inhalt gilt."""
    alt = konfig_ordner / "config.yaml"
    alt.write_text("version: 1\ngeneral: {speed: 2.0}\n", encoding="utf-8")

    w = _fenster(qapp, "a5120")
    try:
        assert w.speed_factor == 2.0, "die alte Konfiguration gilt weiter"
    finally:
        _zu(w, qapp)
    assert not alt.exists(), "umbenannt, nicht kopiert"
    assert (konfig_ordner / "a5120emu.yaml").is_file()
    assert "Konfiguration umgezogen" in capsys.readouterr().out


def test_the_k8915_does_not_take_over_the_old_config(qapp, konfig_ordner):
    """Die Altdatei gehörte dem A5120 — der K8915 lässt sie liegen."""
    from app import config_io, profil

    alt = konfig_ordner / "config.yaml"
    alt.write_text("version: 1\n", encoding="utf-8")
    assert config_io.konfig_umziehen(profil.K8915) == ""
    assert alt.is_file()
    assert not (konfig_ordner / "a5120emu.yaml").exists()


def test_the_shipped_k8915_default_config_is_found_and_complete():
    from app import config_io, paths, profil

    assert paths.default_config_file(profil.K8915.vorgabe_datei) is not None
    vorgabe = config_io.standard_konfiguration(profil.K8915)
    for abschnitt in ("crt", "general", "drive_types", "window"):
        assert abschnitt in vorgabe, abschnitt
    assert "disks" not in vorgabe and "geometry" not in vorgabe["window"]
    assert vorgabe["drive_types"] == ["K5601", "K5601", "none", "none"]
    leiste = vorgabe["window"]["toolbar"]
    assert leiste.index("nmi") == leiste.index("reset") + 1, "NMI neben Reset"


def test_drive_count_follows_the_profile(qapp, konfig_ordner):
    for maschine, zahl in (("a5120", 3), ("k8915", 2)):
        w = _fenster(qapp, maschine)
        try:
            assert len(w.drives_widget.present_drives()) == zahl
            assert len(w.status_widget.felder()) == zahl
            erlaubt = {w.settings_widget._drive_combos[0].itemData(i)
                       for i in range(w.settings_widget._drive_combos[0].count())}
            assert ("MF6400" in erlaubt) == (maschine == "a5120")
        finally:
            _zu(w, qapp)


def test_the_clock_is_the_machines_own(qapp, konfig_ordner):
    w = _fenster(qapp, "k8915")
    try:
        assert w.CPU_HZ == 2_457_600
        w._apply_speed(1.0)
        assert w.status_widget.takt.text() == "Takt: 2,4576 MHz"
        assert w.settings_widget.speed_combo.itemText(0) == "2,4576 MHz"
    finally:
        _zu(w, qapp)


# ─── Frontplatte und NMI ─────────────────────────────────────────────────────

def test_status_bar_shows_six_lamps_that_follow_the_panel_latch(qapp, konfig_ordner,
                                                                 monkeypatch):
    w = _fenster(qapp, "k8915")
    try:
        platte = w.status_widget.frontplatte
        assert [l.name for l in platte.lampen()] == [
            "Run", "Input File", "Output File", "RUN Mode", "ERROR", "Power"]
        # 60H = ERROR + Input File (aktiv low), wie nach einem Lesefehler.
        monkeypatch.setattr(w.emulator, "panel_lamps", lambda: 0x60)
        w._update_frontplatte()
        assert platte.zustand() == {"Run": True, "Input File": True,
                                    "Output File": False, "RUN Mode": False,
                                    "ERROR": True, "Power": True}
        monkeypatch.setattr(w.emulator, "panel_lamps", lambda: 0xB0)   # bereit
        w._update_frontplatte()
        assert platte.zustand()["RUN Mode"] and not platte.zustand()["ERROR"]
        # Ausgeschaltet: alles dunkel, auch Run und Power.
        w.act_power.setChecked(False)
        assert not any(platte.zustand().values())
    finally:
        _zu(w, qapp)


def test_the_a5120_has_no_panel_and_no_nmi(qapp, konfig_ordner):
    from PySide6.QtGui import QAction
    w = _fenster(qapp, "a5120")
    try:
        assert w.status_widget.frontplatte is None
        assert not hasattr(w, "act_nmi")
        assert not [a for a in w.findChildren(QAction) if "NMI" in a.text()]
        assert "nmi" not in w._leisten_inhalt
    finally:
        _zu(w, qapp)


def test_the_nmi_action_sits_next_to_reset_and_fires_k1520_nmi(qapp, konfig_ordner,
                                                                monkeypatch):
    w = _fenster(qapp, "k8915")
    try:
        leiste = [a for a in w.controls_bar.actions() if not a.isSeparator()]
        assert leiste.index(w.act_nmi) == leiste.index(w.act_reset) + 1
        assert w.act_nmi.shortcut().isEmpty(), "kein Kürzel (Kürzeltabelle = Vertrag)"
        assert w.act_nmi in _menue(w, "&Maschine")

        gerufen = []
        monkeypatch.setattr(w.emulator, "nmi", lambda: gerufen.append(True))
        w.act_nmi.trigger()
        assert gerufen == [True]
    finally:
        _zu(w, qapp)


def test_the_nmi_really_reaches_the_rom(qapp, konfig_ordner):
    """Ohne Ersatz: NMI im Selbsttest ⇒ ROM 0066H ⇒ Latch 61H = FFH."""
    w = _fenster(qapp, "k8915")
    try:
        w.run_timer.stop()
        w.emulator.run(2_000_000)
        w.act_nmi.trigger()
        w.emulator.run(200)
        assert w.emulator.panel_lamps() == 0xFF
    finally:
        _zu(w, qapp)


def test_no_shortcut_of_the_k8915_window_steals_a_key(qapp, konfig_ordner):
    """Derselbe Wächter wie beim A5120 — für das zweite Gesicht des Fensters."""
    from PySide6.QtGui import QAction
    from test_gui_smoke import _als_kuerzel, _handbuch_kuerzel

    w = _fenster(qapp, "k8915")
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


def test_the_tools_menu_starts_the_other_emulator(qapp, konfig_ordner, monkeypatch):
    from app import programme
    gerufen = []
    monkeypatch.setattr(programme, "programm_starten",
                        lambda k, *a, **kw: gerufen.append(k))
    for maschine, andere in (("a5120", programme.K8915EMU),
                             ("k8915", programme.EMULATOR)):
        w = _fenster(qapp, maschine)
        try:
            aktion = getattr(w, f"act_{'k8915' if maschine == 'a5120' else 'a5120'}emu")
            assert aktion in _menue(w, "&Werkzeuge")
            assert aktion.shortcut().isEmpty()
            aktion.trigger()
            assert gerufen[-1] == andere
        finally:
            _zu(w, qapp)


# ─── Bildschirmtastatur K7672 ────────────────────────────────────────────────

def _scan_aus_dem_eprom():
    d = EPROM_D3.read_bytes()
    return d[0x80:0x100], d[0x100:0x180]


def test_the_k7672_keys_carry_the_scancodes_of_the_firmware(qapp):
    """Stichprobe A, ß, PF1, RETURN, Kursor, Umschalt: Matrixposition → Scancode
    wie im EPROM D3 (0080H), und der Kurzhinweis nennt denselben."""
    from app.ui.keyboard_k7672 import KeyboardK7672Widget, TASTE_BASE

    scan, art = _scan_aus_dem_eprom()
    kb = KeyboardK7672Widget()
    nach_name = {k.name: k for k in kb._keys}
    erwartet = {"A": 0x1E, "ß": 0x0C, "PF1": 0x3B, "RETURN": 0x1C,
                "Kursor aufwärts": 0xC8, "Umschalttaste links (rastet für eine Taste)": 0x2A}
    for name, code in erwartet.items():
        taste = nach_name.get(name) or next(k for k in kb._keys if k.low == name)
        assert scan[taste.matrix] == code, name
        assert taste.scan == code, name
    # Jede Taste der Nachbildung: Scancode im Layout == EPROM (Tippfehlerwächter).
    for k in kb._keys:
        assert scan[k.matrix] == k.scan, k.name
        if k.code is not None:
            assert k.code == TASTE_BASE | k.matrix
    # ↑ trägt Bit 7 = Vorsatz, und zwar Umschalt (Tastenart Bit 6) ⇒ 2A 48.
    hoch = nach_name["Kursor aufwärts"]
    assert art[hoch.matrix] & 0x40
    assert "mit Vorsatz" in kb._tip(hoch)


def test_the_k7672_widget_sends_matrix_keys_with_its_modifiers(qapp):
    from PySide6.QtCore import QPoint, Qt
    from PySide6.QtTest import QTest
    from app.ui.keyboard_k7672 import KeyboardK7672Widget, taste

    kb = KeyboardK7672Widget()
    kb.resize(kb.sizeHint())
    kb.show()
    qapp.processEvents()
    gesendet = []
    kb.keyPressed.connect(lambda c, s, k: gesendet.append((c, s, k)))

    def klick(name):
        key = next(k for k in kb._keys if k.name == name or k.low == name)
        unit, ox, oy = kb._geometry()
        mitte = kb._rect_of(key, unit, ox, oy).center()
        QTest.mouseClick(kb, Qt.LeftButton, pos=QPoint(int(mitte.x()), int(mitte.y())))

    klick("A")
    klick("Umschalttaste links (rastet für eine Taste)")
    klick("A")
    klick("CTRL (Steuertaste, rastet für eine Taste)")
    klick("C")
    assert gesendet == [(taste(0x20), False, False), (taste(0x20), True, False),
                        (taste(0x31), False, True)]
    kb.close()


def test_the_k7672_lamps_follow_keyboard_leds(qapp, konfig_ordner, monkeypatch):
    w = _fenster(qapp, "k8915")
    try:
        w.keyboard_dock.show()
        qapp.processEvents()
        monkeypatch.setattr(w.emulator, "keyboard_leds", lambda: 0x88)
        w._run_emulator()
        assert w.keyboard_widget.lampen() == {"GRAPH": False, "CAPS": True, "READY": True}
        monkeypatch.setattr(w.emulator, "keyboard_leds", lambda: 0x40)
        w._run_emulator()
        assert w.keyboard_widget.lampen() == {"GRAPH": True, "CAPS": False, "READY": False}
    finally:
        _zu(w, qapp)


# ─── AP-UI2: Zeichnung der K7672 und Beschriftung der Frontplatte ───────────

def test_every_panel_lamp_carries_its_label_next_to_it(qapp, konfig_ordner):
    """Sechs Lampen, sechs sichtbare Schilder — jedes rechts neben SEINER Lampe
    (an der Farbe allein unterscheidet man die drei gelben nicht)."""
    w = _fenster(qapp, "k8915")
    try:
        platte = w.status_widget.frontplatte
        assert platte.beschriftungen() == [
            "Run", "Input", "Output", "Mode", "Error", "Power"]
        lampen = platte.lampen()
        for i, lampe in enumerate(lampen):
            schild = platte.schild(lampe.name)
            assert schild.isVisible() and schild.width() > 0, lampe.name
            assert lampe.name in schild.toolTip()
            links = lampe.geometry().right()
            rechts = (lampen[i + 1].geometry().left() if i + 1 < len(lampen)
                      else platte.width())
            assert links < schild.geometry().left() < rechts, lampe.name
    finally:
        _zu(w, qapp)


def test_k7672_keys_do_not_overlap_and_return_is_one_tall_key(qapp):
    from app.ui.keyboard_k7672 import BLINDSTUECKE, KeyboardK7672Widget

    kb = KeyboardK7672Widget()
    zellen = [(k.x, k.y, k.w, k.h, k.name) for k in kb._keys]
    zellen += [(x, y, w, h, "Blindstück") for x, y, w, h, _g in BLINDSTUECKE]
    for i, a in enumerate(zellen):
        for b in zellen[i + 1:]:
            ueberlapp = (min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0]) > 1e-6
                         and min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1]) > 1e-6)
            assert not ueberlapp, (a[4], b[4])
    ret = [k for k in kb._keys if k.name == "RETURN"]
    assert len(ret) == 1 and ret[0].h == 2.0 and ret[0].matrix == 0x38
    unit = 40.0
    kappe = kb.kappe_von(ret[0], unit, 0.0, 0.0)
    boxen = {t: b for t, b, *_ in kb.legenden_boxen(ret[0], kb._oberseite(kappe, unit),
                                                     unit)}
    assert set(boxen) == {"↵", "RETURN"}
    assert boxen["↵"].bottom() <= boxen["RETURN"].top(), "↵ oben, RETURN unten"


@pytest.mark.parametrize("name, oben_links, unten_links, rechts, rechts_unten", [
    ("Ü (} ])", "}", "]", "Ü", False),
    ("Ö (| \\)", "|", "\\", "Ö", False),
    ("Ä ({ [)", "{", "[", "Ä", False),
    ("ß", "?", "¯", "ß", True),
])
def test_k7672_umlaut_keys_are_labelled_like_the_photo(qapp, name, oben_links,
                                                       unten_links, rechts, rechts_unten):
    """Links oben und links unten die ASCII-Zeichen, rechts groß der Umlaut
    (beim ß in der unteren Zeile) — nicht mehr klein übereinander."""
    from app.ui.keyboard_k7672 import KeyboardK7672Widget

    kb = KeyboardK7672Widget()
    key = next(k for k in kb._keys if k.name == name)
    unit = 40.0
    kappe = kb.kappe_von(key, unit, 0.0, 0.0)
    boxen = {t: (b, g) for t, b, g, _a in kb.legenden_boxen(key, kb._oberseite(kappe, unit),
                                                             unit)}
    assert set(boxen) == {oben_links, unten_links, rechts}
    mitte = kappe.center()
    ol, ul, r = boxen[oben_links][0], boxen[unten_links][0], boxen[rechts][0]
    assert ol.center().x() < mitte.x() and ul.center().x() < mitte.x()
    assert r.center().x() > mitte.x()
    assert ol.center().y() < mitte.y() < ul.center().y()
    assert (r.center().y() > mitte.y()) == rechts_unten
    assert boxen[rechts][1] > boxen[oben_links][1], "der Umlaut ist groß"


def _farbe_nah(c, ziel, tol=40):
    return all(abs(a - b) <= tol for a, b in zip((c.red(), c.green(), c.blue()),
                                                 (ziel.red(), ziel.green(), ziel.blue())))


@pytest.mark.parametrize("breite, mehr_hoehe", [
    (900, 0), (1203, 0), (1275, 7), (1533, 0), (1777, 13)])
def test_k7672_cutouts_never_lose_a_key_or_cover_one(qapp, monkeypatch, breite,
                                                     mehr_hoehe):
    """Wächter zum Befund „rot" (AP-UI2): Qts Pfadvereinigung verlor bei rund
    einem Drittel der Breiten die Einfassung von RETURN/Umschalt rechts (1203 px
    war eine davon).  In Kennfarben gezeichnet muss bei jeder Größe gelten:
    jede Kappe ist Kappe (nirgends Gehäuse oder Ausschnitt darauf), jede Fuge
    zwischen zwei Nachbarn desselben Blocks ist Ausschnitt (nie Gehäuse), und
    zwischen den Blöcken liegt Gehäuse."""
    from PySide6.QtGui import QColor
    from app.ui import keyboard_k7672 as mod

    GEHAEUSE, GRUND, KANTE, KAPPE = (QColor(255, 0, 255), QColor(0, 200, 0),
                                     QColor(0, 0, 255), QColor(255, 255, 255))
    for attr, farbe in (("_C_GEHAEUSE", GEHAEUSE), ("_C_GRUND", GRUND),
                        ("_C_SCHATTEN", KANTE), ("_C_FLANKE", KAPPE),
                        ("_C_KAPPE", KAPPE), ("_C_BLIND", KAPPE)):
        monkeypatch.setattr(mod, attr, farbe)

    kb = mod.KeyboardK7672Widget()
    kb.resize(breite, kb.heightForWidth(breite) + mehr_hoehe)
    bild = kb.grab().toImage()
    unit, ox, oy = kb._geometry()

    def px(x, y):
        return QColor(bild.pixel(int(x), int(y)))

    fehler = []
    i = 0.06 * unit
    for key in kb._keys:
        k = kb.kappe_von(key, unit, ox, oy)
        for x, y in ((k.left() + i, k.top() + i), (k.right() - i, k.top() + i),
                     (k.left() + i, k.bottom() - i), (k.right() - i, k.bottom() - i),
                     (k.right() - i, k.center().y())):
            if not _farbe_nah(px(x, y), KAPPE, 60):
                fehler.append(f"Kappe {key.name} bei ({x:.0f},{y:.0f})")
    zellen = [(key.x, key.y, key.w, key.h, key.gruppe, key.name) for key in kb._keys]
    for a in zellen:
        for b in zellen:
            if a[4] != b[4] or abs(a[0] + a[2] - b[0]) > 1e-6:
                continue
            y0, y1 = max(a[1], b[1]), min(a[1] + a[3], b[1] + b[3])
            if y1 <= y0:
                continue
            x = ox + (kb._pad[0] + b[0]) * unit
            y = oy + (kb._pad[1] + (y0 + y1) / 2) * unit
            if not _farbe_nah(px(x, y), GRUND):
                fehler.append(f"Fuge {a[5]} | {b[5]} bei ({x:.0f},{y:.0f})")
    # Zwischen Haupt- und Mittelblock (DEL ↔ PF10): Gehäuse.
    x = ox + (kb._pad[0] + 15.25) * unit
    y = oy + (kb._pad[1] + 2.0) * unit
    if not _farbe_nah(px(x, y), GEHAEUSE):
        fehler.append("zwischen DEL und PF10 kein Gehäuse")
    assert not fehler, fehler[:10]


# ─── Rauchtest: k8915emu bootet bis zum Prompt, getippt auf der K7672 ────────

def test_k8915emu_boots_to_the_prompt_and_the_on_screen_keyboard_types_dir(
        qapp, konfig_ordner, temp_disk):
    """Netz-Ein mit TempDisk-Kopie in A:, Selbsttest, RETURN auf der
    Bildschirmtastatur (SCP-Modus: Zeichen 0DH) ⇒ SCPX bis ``A>``, dann ``dir`` +
    RETURN auf der Bildschirmtastatur (DCP-Modus: Scancodes) ⇒ Verzeichnis."""
    from PySide6.QtCore import QPoint, Qt
    from PySide6.QtTest import QTest

    diskette = temp_disk("k8915scpx_boot1.hfe")
    w = _fenster(qapp, "k8915", disks=[diskette])
    try:
        w.run_timer.stop()          # der Test fährt die Maschine selbst
        emu = w.emulator
        w.keyboard_dock.show()
        qapp.processEvents()
        kb = w.keyboard_widget

        def klick(name):
            key = next(k for k in kb._keys if k.name == name or k.low == name)
            unit, ox, oy = kb._geometry()
            mitte = kb._rect_of(key, unit, ox, oy).center()
            QTest.mouseClick(kb, Qt.LeftButton, pos=QPoint(int(mitte.x()), int(mitte.y())))
            emu.run(60_000)          # Zeichenzeit(en) der seriellen Leitung

        assert run_until_text(emu, "* Coldstart *  Disk on A: ready", 80_000_000), \
            emu.screen_text()
        klick("RETURN")
        assert run_until_text(emu, "size: 1 kByte groups 0 ... 03FH", 200_000_000), \
            emu.screen_text()
        for _ in range(300):
            zeilen = [z.rstrip(" \x00") for z in emu.screen_text().split("\n") if z.strip(" \x00")]
            if zeilen and zeilen[-1] == "A>":
                break
            emu.run(100_000)
        else:
            pytest.fail("kein Prompt:\n" + emu.screen_text())

        for name in ("D", "I", "R", "RETURN"):
            klick(name)
        assert run_until_text(emu, "RADE     COM", 20_000_000), emu.screen_text()
        assert "A>dir" in emu.screen_text()
        # Lampen: bereit (RUN Mode), kein Fehler; READY der Tastatur an.
        w._update_frontplatte()
        zustand = w.status_widget.frontplatte.zustand()
        assert zustand["RUN Mode"] and not zustand["ERROR"], zustand
        w._run_emulator()
        assert kb.lampen()["READY"]
    finally:
        _zu(w, qapp)


# ─── AP-T1a: Wege, die der Abdeckungsbau ungeprüft fand ─────────────────────

def test_an_unknown_machine_is_refused_with_the_known_names():
    """Profil und Bindung lehnen einen unbekannten Maschinennamen mit den bekannten
    im Text ab (``--machine``-Tippfehler, alte Konfiguration); Groß/klein zählt
    beim Profil nicht (der Starter übergibt klein, ein Anwender vielleicht nicht)."""
    from app import profil
    from app.core_binding.k1520 import K1520Emulator

    assert profil.profil("K8915") is profil.K8915
    assert profil.profil("") is profil.VORGABE is profil.A5120
    with pytest.raises(ValueError, match="k8915"):
        profil.profil("z9001")
    with pytest.raises(ValueError, match="z9001"):
        K1520Emulator(machine="z9001")


def test_a_failed_config_move_keeps_the_old_file_and_says_why(konfig_ordner, monkeypatch,
                                                             capsys):
    """Scheitert das Umbenennen ``config.yaml`` → ``a5120emu.yaml`` (Rechte, anderes
    Laufwerk), bleibt die Altdatei liegen, der Start geht weiter und das Protokoll
    nennt den Grund."""
    from app import config_io, profil

    alt = konfig_ordner / "config.yaml"
    alt.write_text("version: 1\n", encoding="utf-8")

    def verweigert(a, b):
        raise PermissionError("Zugriff verweigert")

    monkeypatch.setattr(config_io.os, "rename", verweigert)
    assert config_io.konfig_umziehen(profil.A5120) == ""
    assert alt.is_file()
    assert not (konfig_ordner / "a5120emu.yaml").exists()
    out = capsys.readouterr().out
    assert "gescheitert" in out and "Zugriff verweigert" in out


def test_host_keys_on_the_k7672_widget(qapp):
    """Host-Tasten an der Bildschirmtastatur K7672: die rastende Umschalttaste der
    Nachbildung macht einen Host-Buchstaben groß, die Feststelltaste des PCs geht als
    eigene Taste an den Kern (das BIOS führt die Feststellung); F1 leuchtet auf
    PF1-Position, Umschalt/Strg auf den Umschalttasten.  Kurzhinweise nennen
    Scancode und Vorsatz — oder „sendet nichts"."""
    from PySide6.QtCore import QEvent, Qt
    from PySide6.QtGui import QKeyEvent
    from app.ui.keyboard_k7672 import KeyboardK7672Widget

    kb = KeyboardK7672Widget()
    ev = lambda key, text="": QKeyEvent(QEvent.KeyPress, key, Qt.NoModifier, text)

    assert kb.map_host_key(ev(Qt.Key_CapsLock)) == (int(Qt.Key_CapsLock), False, False)
    assert kb.map_host_key(ev(Qt.Key_A, "a")) == (0x61, False, False)
    kb._shift = True
    assert kb.map_host_key(ev(Qt.Key_A, "a")) == (0x41, True, False)
    assert kb.map_host_key(ev(Qt.Key_1, "1"))[0] == 0x31, "Umschaltung nur für Buchstaben"
    kb._shift = False
    assert kb.lock_active() is False

    f1 = kb._keys_for_host_event(ev(Qt.Key_F1))
    assert [k.matrix for k in f1] == [kb._F_MATRIX[0]]
    assert all(k.kind == "shift" for k in kb._keys_for_host_event(ev(Qt.Key_Shift)))
    assert kb._keys_for_host_event(ev(Qt.Key_Shift))
    assert all(k.kind == "ctrl" for k in kb._keys_for_host_event(ev(Qt.Key_Control)))
    assert kb._keys_for_host_event(ev(Qt.Key_F24)) == []

    tips = {kb._tip(k) for k in kb._keys}
    assert any("mit Vorsatz" in t for t in tips)
    assert any(t.endswith("— sendet nichts") for t in tips)
    kb.close()


def _tasten_mitschreiben(w, monkeypatch):
    """Was die Oberfläche an den Kern schickt: ``("p", code, shift, ctrl)``/``("r", code)``."""
    protokoll = []
    monkeypatch.setattr(w.emulator, "key_press",
                        lambda code, shift=False, ctrl=False:
                        protokoll.append(("p", code, shift, ctrl)))
    monkeypatch.setattr(w.emulator, "key_release",
                        lambda code: protokoll.append(("r", code)))
    return protokoll


def _taste(w, art, key, mods, text="", autorep=False):
    from PySide6.QtGui import QKeyEvent
    from PySide6.QtWidgets import QApplication
    QApplication.sendEvent(w.screen_widget, QKeyEvent(art, key, mods, text, autorep))


def test_the_k8915_gui_sends_modifiers_as_keys_and_drops_qt_autorepeat(
        qapp, konfig_ordner, monkeypatch):
    """AP-E4g: am K8915 gehen Umschalt/Strg als EIGENE Tasten an die K7672 (im
    DCP-Modus meldet sie 2AH/1DH einzeln), das Qt-Autorepeat wird verworfen (die
    Tastatur wiederholt selbst), und das Loslassen trägt denselben Code wie das
    Drücken — auch wenn die Umschaltung dazwischen fällt (`A` → `a`), sonst hinge die
    Wiederholung der K7672 fest.  Am A5120 bleibt alles, wie es war."""
    from PySide6.QtCore import QEvent, Qt
    P, R = QEvent.KeyPress, QEvent.KeyRelease

    w = _fenster(qapp, "k8915")
    try:
        w.run_timer.stop()
        got = _tasten_mitschreiben(w, monkeypatch)
        # ALT nicht: die Menüleiste wertet es vor dem Widget aus (s. _MODIFIKATOREN).
        for key, mod in ((Qt.Key_Shift, Qt.ShiftModifier), (Qt.Key_Control, Qt.ControlModifier)):
            _taste(w, P, key, mod)
            _taste(w, R, key, Qt.NoModifier)
            assert got[-2:] == [("p", int(key), False, False), ("r", int(key))]
        got.clear()

        _taste(w, P, Qt.Key_A, Qt.NoModifier, "a")
        _taste(w, P, Qt.Key_A, Qt.NoModifier, "a", autorep=True)   # Qt-Wiederholung
        _taste(w, R, Qt.Key_A, Qt.NoModifier, "a", autorep=True)
        assert got == [("p", 0x61, False, False)], "Autorepeat wird nicht weitergereicht"
        _taste(w, R, Qt.Key_A, Qt.NoModifier, "a")
        got.clear()

        # Umschalt+A gedrückt, Umschalt zuerst losgelassen: Loslassen trägt den Code
        # des Drückens (0x41), nicht den, den die Taste jetzt ergäbe (0x61).
        _taste(w, P, Qt.Key_A, Qt.ShiftModifier, "A")
        _taste(w, R, Qt.Key_A, Qt.NoModifier, "a")
        assert got == [("p", 0x41, True, False), ("r", 0x41)]
        got.clear()

        # Fokusverlust mit gehaltenen Tasten: alles loslassen.
        from PySide6.QtGui import QFocusEvent
        from PySide6.QtWidgets import QApplication
        _taste(w, P, Qt.Key_Shift, Qt.ShiftModifier)
        _taste(w, P, Qt.Key_X, Qt.ShiftModifier, "X")
        QApplication.sendEvent(w.screen_widget, QFocusEvent(QEvent.FocusOut))
        assert sorted(c for c in got if c[0] == "r") == sorted(
            [("r", int(Qt.Key_Shift)), ("r", 0x58)])
    finally:
        _zu(w, qapp)

    # A5120: reine Modifikatoren erzeugen weiter keinen Tastencode.
    w = _fenster(qapp, "a5120")
    try:
        w.run_timer.stop()
        got = _tasten_mitschreiben(w, monkeypatch)
        _taste(w, P, Qt.Key_Shift, Qt.ShiftModifier)
        _taste(w, R, Qt.Key_Shift, Qt.NoModifier)
        _taste(w, P, Qt.Key_A, Qt.NoModifier, "a", autorep=True)
        assert got == []
    finally:
        _zu(w, qapp)


def test_the_toolbar_dialog_offers_only_the_programs_own_actions(qapp, konfig_ordner,
                                                                  monkeypatch):
    """*Symbolleiste einrichten* bietet je Programm nur, was es dort gibt: der
    K8915 den NMI-Taster, der A5120 nicht.  Jeder angebotene Name hat einen Text (sonst leere Zeile im Dialog)."""
    from app.ui import main_window as mw

    angebote = {}

    class Attrappe:
        def __init__(self, reihenfolge, inhalt, namen, standard, eltern):
            angebote[eltern.profil.maschine] = (list(reihenfolge), dict(namen), standard)

        def exec(self):
            return False

    monkeypatch.setattr(mw, "ToolbarDialog", Attrappe)
    for maschine in ("a5120", "k8915"):
        w = _fenster(qapp, maschine)
        try:
            w._leiste_einrichten()
        finally:
            _zu(w, qapp)

    folge_a, namen_a, std_a = angebote["a5120"]
    folge_k, namen_k, std_k = angebote["k8915"]
    assert "nmi" in folge_k and "nmi" not in folge_a
    for folge, namen in ((folge_a, namen_a), (folge_k, namen_k)):
        for name in filter(None, folge):
            assert namen.get(name), f"kein Dialogtext für {name!r}"
    assert "nmi" in std_k and "nmi" not in std_a


def test_a_failed_start_of_the_other_emulator_is_reported(qapp, konfig_ordner,
                                                          monkeypatch):
    """Lässt sich der andere Emulator nicht starten (Skript fehlt, Interpreter weg),
    sagt ein Hinweisfenster warum — mit dem Titel des ANDEREN Programms."""
    from app import programme
    from app.ui import main_window as mw

    def scheitert(kennung, *a, **kw):
        raise RuntimeError(f"{kennung} nicht gefunden")

    gemeldet = []
    monkeypatch.setattr(programme, "programm_starten", scheitert)
    monkeypatch.setattr(mw.QMessageBox, "warning",
                        lambda eltern, titel, text: gemeldet.append((titel, text)))
    w = _fenster(qapp, "k8915")
    try:
        w.act_a5120emu.trigger()
    finally:
        _zu(w, qapp)
    assert len(gemeldet) == 1
    titel, text = gemeldet[0]
    assert "A5120" in titel and "nicht gefunden" in text

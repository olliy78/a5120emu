"""`app/ui/keyboard.py` — das nachgebildete K7637-Tastenfeld.

Die Bildschirmtastatur ist eine *Nachbildung*: Jede Taste trägt ihren
**physischen** K7637-Code aus der BIOS-Umkodiertabelle `cp37`.  Genau daran
kann man sich vertun, ohne dass es auffällt — ein falscher Code sieht auf dem
Bild richtig aus und kommt im Gast als etwas anderes an.  Geprüft wird deshalb
die Tabelle selbst, nicht das Aussehen.

Der Rohcode-Weg (`RAW_BASE`) muss mit `K7637::QK_RAW_BASE` im Kern
übereinstimmen; die C++-Seite hat dafür ihren eigenen Wächter
(`K7637.RawCode_IsSentVerbatim`).
"""

import re
from pathlib import Path

import pytest

from PySide6.QtCore import QEvent, Qt
from PySide6.QtGui import QKeyEvent, QMouseEvent

from app.ui import keyboard as kbd


REPO = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def keys():
    return kbd._build_layout()


@pytest.fixture(scope="module")
def by_name(keys):
    return {k.name: k for k in keys}


def test_raw_base_matches_the_core():
    """`RAW_BASE` ist ein Vertrag mit `core/peripherals/k7637/k7637.h`."""
    header = (REPO / "core/peripherals/k7637/k7637.h").read_text(encoding="utf-8")
    m = re.search(r"QK_RAW_BASE\s*=\s*(0x[0-9A-Fa-f]+)", header)
    assert m, "QK_RAW_BASE im Kern-Header nicht gefunden"
    assert int(m.group(1), 16) == kbd.RAW_BASE


@pytest.mark.parametrize("name, code", [
    # Die Codes stammen aus cp37 (disks/cpa_cpa780_*.prn).
    ("ET1 (BIOS: CR)",                    0xFF),
    ("ENTER (Ziffernblock, ≠ ET1)",       0xC0),
    ("CE (Eingabe löschen)",              0xB9),
    ("SEL 0",                             0xA0),
    ("SEL 3",                             0xA3),
    ("PF 1 / PA 1",                       0xC1),
    ("PF 12",                             0xCC),
    ("M / MON (BIOS: PF 14)",             0xB0),
    ("RESET (BIOS: PF 15)",               0xAF),
    ("Ziffernblock 00",                   0xB1),
    ("Tabulator (BIOS: TAB)",             0x9F),
    ("Kursor aufwärts",                   0x94),
    ("Kursor abwärts",                    0x95),
    ("Kursor links",                      0x96),
    ("Kursor rechts",                     0x97),
    ("Kursor Wort zurück",                0x9B),
    ("Kursor Wort vorwärts",              0x91),
    ("Kursor Seite zurück",               0x9C),
    ("Kursor Seite vorwärts",             0x9A),
    ("INS MD / INS L",                    0xA8),
    ("DEL CH / DEL L",                    0xBB),
])
def test_special_keys_carry_their_physical_code(by_name, name, code):
    key = by_name[name]
    assert key.code == kbd.raw(code), f"{name}: 0x{key.code & 0xFF:02X} statt 0x{code:02X}"


@pytest.mark.parametrize("name, code", [
    ("PF 1 / PA 1",      0xFA),   # PA 1
    ("PF 3 / PA 3",      0xF8),   # PA 3
    ("PF 5 / CLEAR",     0xFC),
    ("PF 7 / FM",        0xBE),
    ("PF 10 / EREOF",    0x98),
    ("PF 11 / ERINP",    0x99),
    ("INS MD / INS L",   0x93),   # INS L
    ("DEL CH / DEL L",   0xB3),   # DEL L
])
def test_upper_legend_sends_the_shift_code(by_name, name, code):
    """Die obere Beschriftung ist die Umschaltebene — eigener Code, nicht `code`."""
    key = by_name[name]
    assert key.shift_code == kbd.raw(code)
    assert key.code_for(shift=True) == kbd.raw(code)
    assert key.code_for(shift=False) == key.code


def test_et1_and_enter_are_two_different_keys(by_name):
    """Auf der echten Tastatur zwei Tasten — ET1 wird zu CR, ENTER zu pf0c."""
    assert by_name["ET1 (BIOS: CR)"].code != by_name["ENTER (Ziffernblock, ≠ ET1)"].code


def test_printable_keys_send_ascii(keys):
    """Buchstaben/Ziffern gehen als ASCII — der Kern reicht sie unverändert durch."""
    letters = {k.low: k for k in keys if len(k.low) == 1 and k.low.isalpha()}
    assert letters["Q"].code == ord("q") and letters["Q"].shift_code == ord("Q")
    assert len(letters) == 26, "es fehlen Buchstabentasten"


def test_ascii_set_is_complete(keys):
    """Jedes druckbare ASCII-Zeichen ist auf genau einer Taste erreichbar.

    Die Ziffernreihe der K7637 ist bitgepaart; ein Tippfehler in einer
    Umschaltebene fällt nur dadurch auf, dass ein Zeichen fehlt (oder doppelt
    ist — der Ziffernblock ist die einzige gewollte Doppelung).
    """
    reachable = set()
    for k in keys:
        for code in (k.code, k.shift_code):
            if code is not None and 0x20 <= code <= 0x7E:
                reachable.add(code)
    # Kleinbuchstaben stehen für die ungeshifteten Buchstabentasten.
    missing = {c for c in range(0x20, 0x7F)} - reachable - set(range(0x41, 0x5B))
    assert not missing, "nicht erreichbar: " + " ".join(
        f"{chr(c)}(0x{c:02X})" for c in sorted(missing))


def test_print_and_hlt_send_nothing(by_name):
    """PRINT/HLT stehen in keiner vorliegenden Codetabelle — lieber nichts senden."""
    for name in ("PRINT — Tastencode unbekannt", "HLT — Tastencode unbekannt"):
        key = by_name[name]
        assert key.code is None and key.kind == "dead"


def test_modifiers_have_no_code(keys):
    mods = [k for k in keys if k.kind in ("shift", "ctrl", "lock")]
    assert [k.kind for k in mods].count("shift") == 2, "zwei Umschalttasten"
    assert [k.kind for k in mods].count("ctrl") == 2, "CTRL und ET2"
    assert [k.kind for k in mods].count("lock") == 1
    assert all(k.code is None for k in mods)


def test_keys_do_not_overlap(keys):
    """Zwei Tasten auf derselben Fläche wären im Bild nicht unterscheidbar."""
    for i, a in enumerate(keys):
        for b in keys[i + 1:]:
            overlap_x = a.x < b.x + b.w - 1e-6 and b.x < a.x + a.w - 1e-6
            overlap_y = a.y < b.y + b.h - 1e-6 and b.y < a.y + a.h - 1e-6
            assert not (overlap_x and overlap_y), (
                f"{a.name or a.low!r} und {b.name or b.low!r} überlappen")


# ── Bedienung: Klick → Signal ────────────────────────────────────────────────

def _click(widget, key, button=Qt.LeftButton):
    """Eine Taste der Nachbildung anklicken (Drücken UND Loslassen)."""
    unit, ox, oy = widget._geometry()
    center = widget._rect_of(key, unit, ox, oy).center()
    for typ in (QMouseEvent.Type.MouseButtonPress, QMouseEvent.Type.MouseButtonRelease):
        ev = QMouseEvent(typ, center, widget.mapToGlobal(center.toPoint()),
                         button, button, Qt.NoModifier)
        if typ == QMouseEvent.Type.MouseButtonPress:
            widget.mousePressEvent(ev)
        else:
            widget.mouseReleaseEvent(ev)


@pytest.fixture
def widget(qapp):
    w = kbd.KeyboardWidget()
    w.resize(w.sizeHint())
    return w


def _recorder(widget):
    seen = []
    widget.keyPressed.connect(lambda c, s, x: seen.append((c, s, x)))
    return seen


def test_click_sends_the_physical_code(widget):
    """Ein Klick auf CE schickt 0xB9 als Rohcode — nicht irgendein ASCII."""
    seen = _recorder(widget)
    ce = next(k for k in widget._keys if k.low == "CE")
    _click(widget, ce)
    assert seen == [(kbd.raw(0xB9), False, False)]


def test_shift_applies_to_exactly_one_key(widget):
    """SHIFT ist ein Einmal-Modifikator: '1'→'!', danach wieder '1'."""
    seen = _recorder(widget)
    shift = next(k for k in widget._keys if k.kind == "shift")
    one = next(k for k in widget._keys if k.low == "1" and k.up == "!")
    _click(widget, shift)
    _click(widget, one)
    _click(widget, one)
    assert [c for c, _, _ in seen] == [ord("!"), ord("1")]


def test_lock_stays_until_shift_releases_it(widget):
    """LOCK rastet ein; SHIFT hebt ihn auf (wie am Original)."""
    seen = _recorder(widget)
    lock = next(k for k in widget._keys if k.kind == "lock")
    shift = next(k for k in widget._keys if k.kind == "shift")
    a = next(k for k in widget._keys if k.low == "A")
    _click(widget, lock)
    _click(widget, a)
    _click(widget, a)
    _click(widget, shift)
    _click(widget, a)
    assert [c for c, _, _ in seen] == [ord("A"), ord("A"), ord("a")]


def test_dead_keys_send_nothing(widget):
    seen = _recorder(widget)
    for name in ("PRINT — Tastencode unbekannt", "HLT — Tastencode unbekannt"):
        _click(widget, next(k for k in widget._keys if k.name == name))
    assert seen == []


# ── Anzeigen ─────────────────────────────────────────────────────────────────

def test_led_bits_match_the_core_header():
    """Die Bitmaske ist ein Vertrag mit `k1520_keyboard_leds`."""
    header = (REPO / "core/peripherals/k7637/k7637.h").read_text(encoding="utf-8")
    for name, wert in (("LED_G00", kbd.LED_G00), ("LED_G01", kbd.LED_G01),
                       ("LED_G02", kbd.LED_G02), ("LED_G03", kbd.LED_G03),
                       ("LED_G04", kbd.LED_G04), ("LED_ERROR", kbd.LED_ERROR)):
        m = re.search(rf"{name}\s*=\s*(0x[0-9A-Fa-f]+)", header)
        assert m, f"{name} im Kern-Header nicht gefunden"
        assert int(m.group(1), 16) == wert, name


def test_function_leds_follow_the_mask(widget):
    """Die fünf Funktionsanzeigen zeigen, was der Kern meldet."""
    widget.set_leds(kbd.LED_G00 | kbd.LED_G03)
    unit, ox, oy = widget._geometry()
    an = [name for _, _, lit, name in widget._led_spots(unit, ox, oy) if lit]
    assert "Selektor 0 (G00)" in an and "Selektor 3 (G03)" in an
    assert "Selektor 1 (G01)" not in an


def test_error_display_blinks_only_while_switched_on(widget):
    """Die Fehleranzeige blinkt — der Zeitgeber läuft nur, solange sie an ist."""
    assert not widget._blink_timer.isActive()
    widget.set_leds(kbd.LED_ERROR)
    assert widget._blink_timer.isActive()
    widget.set_leds(0)
    assert not widget._blink_timer.isActive()


def test_lock_display_follows_the_shift_lock(widget):
    """Die LOCK-Anzeige hängt an der Tastatur selbst, nicht am Rechner."""
    def lock_an():
        unit, ox, oy = widget._geometry()
        return next(lit for _, _, lit, name in widget._led_spots(unit, ox, oy)
                    if name.startswith("Umschaltfeststeller"))

    assert not lock_an()
    _click(widget, next(k for k in widget._keys if k.kind == "lock"))
    assert lock_an()


def test_power_display_follows_the_mains_switch(widget):
    """Die Betriebsanzeige leuchtet, solange die Tastatur Spannung hat."""
    def betrieb_an():
        unit, ox, oy = widget._geometry()
        return next(lit for _, _, lit, name in widget._led_spots(unit, ox, oy)
                    if name.startswith("Betriebsanzeige"))

    assert betrieb_an()
    widget.set_powered(False)
    assert not betrieb_an()


# ── Die echte Tastatur mitzeigen ─────────────────────────────────────────────

def _taste(key, text="", mods=Qt.NoModifier, typ=None):
    """Ein Host-Tastenereignis bauen."""
    from PySide6.QtCore import QEvent
    typ = typ or QEvent.Type.KeyPress
    return QKeyEvent(typ, int(key), mods, text)


def _hell(widget, name_or_low):
    """Leuchtet die Taste mit dieser Beschriftung/diesem Namen gerade auf?"""
    return any(k.low == name_or_low or k.name == name_or_low
               for keys in widget._host_down.values() for k in keys)


def test_host_key_highlights_the_matching_key(widget):
    """Tippen auf der PC-Tastatur zeigt auf der Nachbildung, was angesprochen wird."""
    ev = _taste(Qt.Key_A, "a")
    widget.host_key_press(ev)
    assert _hell(widget, "A")
    widget.host_key_release(_taste(Qt.Key_A, "a", typ=QEvent.Type.KeyRelease))
    assert not _hell(widget, "A")


def test_host_special_keys_find_their_physical_key(widget):
    """Sondertasten leuchten dort auf, wohin der Kern sie übersetzt."""
    for qtkey, beschriftung in ((Qt.Key_Return, "ET1"), (Qt.Key_Enter, "ENTER"),
                                (Qt.Key_Up, "↑"), (Qt.Key_Tab, "|←|"),
                                (Qt.Key_F1, "PF 1")):
        widget.host_key_press(_taste(qtkey))
        assert _hell(widget, beschriftung), f"{beschriftung} leuchtet nicht"
        widget.host_key_release(_taste(qtkey, typ=QEvent.Type.KeyRelease))
        assert not _hell(widget, beschriftung)


def test_held_modifiers_are_visible(widget):
    """Strg gedrückt halten sieht man — beide Steuertasten leuchten."""
    widget.host_key_press(_taste(Qt.Key_Control, mods=Qt.ControlModifier))
    assert _hell(widget, "CTRL") and _hell(widget, "ET2")
    # …und Strg+C hebt zusätzlich das C hervor.
    widget.host_key_press(_taste(Qt.Key_C, "\x03", Qt.ControlModifier))
    assert _hell(widget, "C")
    widget.host_key_release(_taste(Qt.Key_Control, mods=Qt.NoModifier,
                                   typ=QEvent.Type.KeyRelease))
    assert not _hell(widget, "CTRL")


def test_shift_shows_both_shift_keys(widget):
    widget.host_key_press(_taste(Qt.Key_Shift, mods=Qt.ShiftModifier))
    hell = [k for keys in widget._host_down.values() for k in keys]
    assert len(hell) == 2 and all(k.kind == "shift" for k in hell)


def test_numpad_digit_highlights_the_numeric_block(widget):
    """Dieselbe Ziffer gibt es zweimal — die Herkunft entscheidet."""
    from PySide6.QtCore import Qt as _Qt
    widget.host_key_press(_taste(_Qt.Key_7, "7", _Qt.KeypadModifier))
    treffer = [k for keys in widget._host_down.values() for k in keys]
    assert treffer and treffer[0].name == "Ziffernblock 7"
    widget._host_down.clear()
    widget.host_key_press(_taste(_Qt.Key_7, "7"))
    treffer = [k for keys in widget._host_down.values() for k in keys]
    assert treffer and treffer[0].name != "Ziffernblock 7"


def test_host_caps_lock_shows_on_the_emulated_keyboard(widget):
    """Der Feststeller der PC-Tastatur überträgt sich — abgelesen am Buchstaben."""
    assert not widget.lock_active()
    widget.host_key_press(_taste(Qt.Key_A, "A"))       # Großbuchstabe ohne Shift
    assert widget.lock_active(), "Feststeller der echten Tastatur nicht erkannt"
    widget.host_key_press(_taste(Qt.Key_A, "a"))       # wieder klein
    assert not widget.lock_active()


def test_onscreen_lock_uppercases_host_keys(widget):
    """Rückrichtung: der Feststeller der Nachbildung wirkt auf die PC-Eingabe.

    Die Feststelltaste der echten Tastatur lässt sich von einem Programm aus
    nicht schalten; stattdessen setzt die Nachbildung den Buchstaben selbst um.
    """
    _click(widget, next(k for k in widget._keys if k.kind == "lock"))
    assert widget.map_host_key(_taste(Qt.Key_A, "a")) == (ord("A"), True, False)
    # Ohne Feststeller bleibt es beim Kleinbuchstaben.
    _click(widget, next(k for k in widget._keys if k.kind == "lock"))
    assert widget.map_host_key(_taste(Qt.Key_A, "a")) == (ord("a"), False, False)


def test_onscreen_ctrl_applies_to_host_keys(widget):
    """Angeklicktes CTRL wirkt auf die nächste Taste der echten Tastatur."""
    _click(widget, next(k for k in widget._keys if k.kind == "ctrl"))
    assert widget.map_host_key(_taste(Qt.Key_C, "c")) == (ord("c"), False, True)


def test_focus_loss_clears_stuck_highlights(widget):
    """Ohne Fokus kommt kein Loslassen mehr — sonst bliebe die Taste hell."""
    widget.host_key_press(_taste(Qt.Key_A, "a"))
    assert widget._host_down
    widget.clear_host_keys()
    assert not widget._host_down


# ── Aufbau: was auf dem Foto zu sehen ist ────────────────────────────────────

def test_function_leds_sit_centred_above_their_keys(widget):
    """Die fünf Anzeigen stehen mittig über SEL 0…3 und INS MD.

    Genau die Tasten, deren Lampen CP/A schaltet (Selektor 0…3 + INS-Modus) —
    daneben verlöre die Leiste ihre Bedeutung.
    """
    unit, ox, oy = widget._geometry()
    reihe0 = sorted((k for k in widget._keys if k.y == 0.0), key=lambda k: k.x)[:5]
    for key, (cx, _, _, _) in zip(reihe0, widget._led_spots(unit, ox, oy)):
        mitte = widget._rect_of(key, unit, ox, oy).center().x()
        assert abs(cx - mitte) < 0.5, f"Anzeige über {key.low!r} sitzt nicht mittig"


def test_leds_in_the_keyfield_sit_in_half_width_modules(keys):
    """LOCK- und Betriebsanzeige stecken in einem Modul halber Tastenbreite."""
    module = [k for k in keys if k.led]
    assert len(module) == 2
    for m in module:
        assert 0.3 < m.w < 0.6, f"{m.name}: {m.w} ist keine halbe Tastenbreite"
        assert m.kind == "dead" and m.code is None


def test_blind_modules_flank_the_space_bar(keys):
    """Links und rechts der Leertaste und neben ET1 sitzt je ein Blindmodul."""
    blind = sorted(k.x for k in keys if k.name == "Blindmodul" and k.y == 5.0)
    assert len(blind) == 3
    space = next(k for k in keys if k.name == "Leertaste")
    et1 = next(k for k in keys if k.low == "ET1")
    assert blind[0] < space.x < blind[1] < et1.x < blind[2]


def test_enter_is_labelled_letter_by_letter(by_name):
    """ENTER traegt seine Grossbuchstaben aufrecht untereinander."""
    enter = by_name["ENTER (Ziffernblock, ≠ ET1)"]
    assert enter.vertical and enter.low == "ENTER"
    assert enter.h > 2.5, "der ENTER-Balken geht über drei Reihen"


def test_digit_row_is_flush_with_the_function_row(keys):
    """CTRL ist EINE Taste breit — dadurch steht 1 über 1 und PF 4 über 9.

    Mit einer breiteren CTRL-Taste verrutscht die ganze Ziffernreihe um eine
    halbe Tastenbreite, und die Spalten stimmen nirgends mehr.
    """
    ctrl = next(k for k in keys if k.low == "CTRL")
    assert ctrl.w == 1.0, "CTRL hat normale Tastenbreite"

    def mitte(k):
        return k.x + k.w / 2

    fn = {k.low: k for k in keys if k.y == 0.0}
    ziffern = {k.low: k for k in keys if k.y == 1.0 and k.low in ("1", "2", "9")}
    assert abs(mitte(ziffern["1"]) - mitte(fn["1"])) < 0.1
    assert abs(mitte(ziffern["2"]) - mitte(fn["2"])) < 0.1
    assert abs(mitte(ziffern["9"]) - mitte(fn["PF 4"])) < 0.1, "PF 4 steht über 9"
    # …und zwischen PF 12 und RESET klafft keine Lücke mehr.
    assert abs(fn["RESET"].x - (fn["PF 12"].x + 1.0)) < 0.05
    # Die Fehleranzeige steht über RESET, nicht über M.
    unit, ox, oy = 40.0, 0.0, 0.0


def test_letter_rows_step_down_to_the_right(by_name):
    """1, Q, A, Z sind wie auf jeder Schreibmaschine schräg nach rechts versetzt."""
    def mitte(k):
        return k.x + k.w / 2

    eins = mitte(by_name["1 / !"])
    for oben, unten in (("1 / !", "Q"), ("Q", "A"), ("A", "Z")):
        assert mitte(by_name[unten]) > mitte(by_name[oben]) + 0.05, (
            f"{unten} muss rechts von {oben} stehen")
    assert mitte(by_name["Z"]) - eins < 1.5, "der Versatz bleibt unter einer Taste"


def test_the_block_stays_rectangular_on_the_right(keys):
    """Ziffernblock rechteckig: CE und die rechte Umschalttaste fangen den Versatz auf."""
    ce = next(k for k in keys if k.low == "CE")
    shifts = [k for k in keys if k.kind == "shift"]
    assert ce.w > 1.0, "CE ist breiter als eine Taste"
    assert all(k.w > 1.0 for k in shifts), "beide Umschalttasten sind breit"
    # Alle vier Spalten des Ziffernblocks beginnen bündig übereinander.
    spalten = {round(k.x, 2) for k in keys
               if k.name.startswith("Ziffernblock") and k.style == "dark"}
    assert len(spalten) == 4, f"Ziffernblock nicht rechteckig: {sorted(spalten)}"


def test_the_key_field_has_no_gaps(keys):
    """Zwischen den Modulen ist kein Blech: jede Reihe ist lückenlos gefüllt.

    Ausgenommen ist die Funktionsreihe — sie sitzt frei auf der Wanne.
    """
    for y in (1.0, 2.0, 3.0, 4.0, 5.0):
        reihe = sorted((k for k in keys if k.y == y), key=lambda k: k.x)
        for links, rechts in zip(reihe, reihe[1:]):
            assert abs((links.x + links.w) - rechts.x) < 1e-6, (
                f"Lücke in Reihe {y:.0f} zwischen {links.name!r} und {rechts.name!r}")


def test_cursor_and_numeric_block_abut_the_letters(keys):
    """Kursor- und Ziffernblock schließen direkt an den Buchstabenblock an."""
    def rechte_kante(name):
        k = next(x for x in keys if x.name == name)
        return k.x + k.w

    # Linke Kante des Kursorblocks: die Abwärtstaste steht dort ganz links.
    kursor = next(k.x for k in keys if k.name == "Kursor abwärts")
    ziffern = min(k.x for k in keys if k.name.startswith("Ziffernblock")
                  and k.style == "dark")
    assert abs(rechte_kante("] / }") - kursor) < 1e-6, "Kursorblock klebt am ]"
    assert abs(kursor + 2.0 - ziffern) < 1e-6, "Ziffernblock klebt am Kursorblock"
    # Die rechte Umschalttaste und das Blindmodul neben ET1 enden dort ebenfalls.
    rechte_shift = max((k for k in keys if k.kind == "shift"), key=lambda k: k.x)
    assert abs(rechte_shift.x + rechte_shift.w - kursor) < 1e-6


def test_error_lamp_sits_above_the_reset_key(widget):
    """Die Fehleranzeige steht über RESET — nicht über M."""
    unit, ox, oy = widget._geometry()
    reset = next(k for k in widget._keys if k.low == "RESET")
    mitte = widget._rect_of(reset, unit, ox, oy).center().x()
    fehler = next(cx for cx, _, _, name in widget._led_spots(unit, ox, oy)
                  if name.startswith("Fehleranzeige"))
    assert abs(fehler - mitte) < 0.5


def test_every_code_survives_the_core_translation(keys):
    """Jeder Tastencode muss den Kern auch erreichen.

    `K7637::translateKey` reicht nur druckbares ASCII (0x20…0x7E) durch; alles
    darunter fällt unter den Tisch, wenn es nicht als **Rohcode** kommt.  Genau
    daran war die ESC-Taste still wirkungslos: sie trug 0x1B als blanken
    Tastencode und sendete deshalb gar nichts.
    """
    for k in keys:
        for code in (k.code, k.shift_code):
            if code is None:
                continue
            roh = (code & ~0xFF) == kbd.RAW_BASE
            assert roh or 0x20 <= code <= 0x7E, (
                f"{k.name}: 0x{code:X} ist weder Rohcode noch druckbares ASCII")


@pytest.mark.parametrize("qtkey, taste", [
    (Qt.Key_Escape, "ESC"),
    (Qt.Key_Backspace, "DEL CH"),
    (Qt.Key_Tab, "|←|"),
])
def test_host_keys_hit_the_key_that_does_the_job(widget, qtkey, taste):
    """Esc, Rückschritt und Tabulator treffen die Taste, die im Gast wirkt.

    Am laufenden CP/A nachgemessen: **DEL CH** löscht ein Zeichen rückwärts
    (0x08 als blanker Code täte gar nichts), die **ESC-Taste** schickt 0x1B —
    vorher lag Esc auf DEL L, was im CP/A zwar auch ESC ergibt, auf der
    Nachbildung aber die falsche Taste aufleuchten ließ.
    """
    widget.host_key_press(_taste(qtkey))
    hell = [k for keys in widget._host_down.values() for k in keys]
    assert hell and (hell[0].low == taste or hell[0].name.startswith(taste)), (
        f"{taste} leuchtet nicht: {[k.low for k in hell]}")


# ── Beide Eingabewege müssen dasselbe schicken ──────────────────────────────

def _klickcode(widget, key):
    """Was ein Mausklick auf diese Taste sendet."""
    return int(key.code_for(widget.schicht())) & 0xFF


@pytest.mark.parametrize("zustand", ["nichts", "pc-feststeller", "lock"])
def test_host_key_and_click_agree(widget, zustand):
    """Rücktaste am PC und angeklickte DEL-CH-Taste schicken DASSELBE Byte.

    Vorher nicht: der *erkannte* Feststeller der PC-Tastatur schaltete die
    Nachbildung auf die Umschaltebene, die Host-Tasten aber nicht — dann sendete
    der Klick DEL L (0xB3) und die Rücktaste DEL CH (0xBB).  Dieselbe Taste,
    zwei Wirkungen, je nach Eingabeweg.
    """
    if zustand == "pc-feststeller":
        widget._host_caps = True
    elif zustand == "lock":
        widget._lock = True

    delch = next(k for k in widget._keys if k.name.startswith("DEL CH"))
    host = widget.map_host_key(_taste(Qt.Key_Backspace, "\b"))[0] & 0xFF
    assert host == _klickcode(widget, delch)


def test_host_caps_does_not_switch_special_keys(widget):
    """Die Feststelltaste des PC macht aus Buchstaben Großbuchstaben — mehr nicht.

    Auf der echten Tastatur schaltet SIE keine Sondertaste um: die Rücktaste
    bleibt die Rücktaste.  (Der Feststeller der Nachbildung dagegen schon — das
    ist seine Aufgabe.)
    """
    widget._host_caps = True
    assert widget.map_host_key(_taste(Qt.Key_Backspace, "\b"))[0] == kbd.raw(0xBB)
    assert widget.map_host_key(_taste(Qt.Key_A, "A"))[0] == ord("A")


def test_shift_reaches_the_upper_legend_of_special_keys(widget):
    """Umschalt+F1 ist PA 1 — die obere Beschriftung, wie an der echten Tastatur."""
    assert widget.map_host_key(
        _taste(Qt.Key_F1, mods=Qt.ShiftModifier))[0] == kbd.raw(0xFA)
    assert widget.map_host_key(_taste(Qt.Key_F1))[0] == kbd.raw(0xC1)

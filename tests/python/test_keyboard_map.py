"""`app/ui/keyboard.py` — Qt-Tastenereignis → Kern-Tastencode.

`qt_event_to_core_key` sitzt zwischen der Host-Tastatur und dem emulierten
K7637.  Die Abbildung ist die einzige Stelle, an der Ctrl-Kombinationen,
Sondertasten und das Host-Layout auseinandersortiert werden — reine Funktion,
also gut isoliert prüfbar.
"""

import pytest

from PySide6.QtCore import Qt

from conftest import requires_core

from app.ui.keyboard import qt_event_to_core_key


class FakeKeyEvent:
    """Minimales QKeyEvent-Double: `key()`, `modifiers()`, `text()`."""

    def __init__(self, key, text="", modifiers=Qt.NoModifier):
        self._key, self._text, self._mods = int(key), text, modifiers

    def key(self):
        return self._key

    def text(self):
        return self._text

    def modifiers(self):
        return self._mods


def test_printable_character_uses_the_event_text():
    """Das erzeugte Zeichen zählt, nicht der Tastencode (Host-Layout!)."""
    assert qt_event_to_core_key(FakeKeyEvent(Qt.Key_A, "a")) == (ord("a"), False, False)
    assert qt_event_to_core_key(
        FakeKeyEvent(Qt.Key_A, "A", Qt.ShiftModifier)) == (ord("A"), True, False)


def test_digits_and_symbols():
    assert qt_event_to_core_key(FakeKeyEvent(Qt.Key_7, "7")) == (ord("7"), False, False)
    assert qt_event_to_core_key(FakeKeyEvent(Qt.Key_Slash, "/")) == (ord("/"), False, False)


@pytest.mark.parametrize("key", [
    Qt.Key_Return, Qt.Key_Enter, Qt.Key_Backspace, Qt.Key_Tab, Qt.Key_Backtab,
    Qt.Key_Escape,
    Qt.Key_Delete, Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right,
])
def test_special_keys_pass_through_unchanged(key):
    """Sondertasten reicht der Kern als Qt::Key_* 1:1 durch."""
    assert qt_event_to_core_key(FakeKeyEvent(key)) == (int(key), False, False)


@pytest.mark.parametrize("key", [Qt.Key_F1, Qt.Key_F4, Qt.Key_F8, Qt.Key_F9,
                                 Qt.Key_F10, Qt.Key_F12])
def test_function_keys_f1_to_f8_pass_through(key):
    assert qt_event_to_core_key(FakeKeyEvent(key)) == (int(key), False, False)


def test_ctrl_letter_becomes_lowercase_plus_ctrl_flag():
    """Ctrl+C → ('c', ctrl=True); der Kern rechnet selbst `& 0x1F`."""
    result = qt_event_to_core_key(
        FakeKeyEvent(Qt.Key_C, "\x03", Qt.ControlModifier))
    assert result == (ord("c"), False, True)


@pytest.mark.parametrize("key", [
    Qt.Key_Shift, Qt.Key_Control, Qt.Key_Alt, Qt.Key_Meta,
    Qt.Key_CapsLock, Qt.Key_AltGr,
])
def test_bare_modifiers_produce_no_keycode(key):
    assert qt_event_to_core_key(FakeKeyEvent(key)) is None


def test_space_without_text_still_maps():
    """Manche Plattformen liefern für die Leertaste keinen Text."""
    assert qt_event_to_core_key(FakeKeyEvent(Qt.Key_Space)) == (0x20, False, False)


def test_unknown_key_without_text_is_dropped():
    assert qt_event_to_core_key(FakeKeyEvent(Qt.Key_F13)) is None


def test_non_ascii_text_is_dropped():
    """Umlaute kennt der A5120-Zeichensatz an dieser Stelle nicht."""
    assert qt_event_to_core_key(FakeKeyEvent(Qt.Key_Odiaeresis, "ö")) is None


# ─── Die Abbildung im Kern selbst (ohne Maschine) ────────────────────────────

@requires_core
@pytest.mark.parametrize("qtkey, code, taste", [
    (Qt.Key_Return,    0xFF, "ET1"),
    (Qt.Key_Enter,     0xC0, "ENTER des Ziffernblocks"),
    (Qt.Key_Backspace, 0x9F, "|←| — die Rücktaste der K7637"),
    (Qt.Key_Delete,    0xBB, "DEL CH"),
    (Qt.Key_Tab,       0x91, "→| — die Taste an der Tabulatorstelle"),
    (Qt.Key_Backtab,   0x9B, "|← — Umschalt+Tab"),
    (Qt.Key_Escape,    0x1B, "ESC-Taste"),
    (Qt.Key_Up,        0x94, "Kursor aufwärts"),
    (Qt.Key_Down,      0x95, "Kursor abwärts"),
    (Qt.Key_Left,      0x96, "Kursor links"),
    (Qt.Key_Right,     0x97, "Kursor rechts"),
    (Qt.Key_F1,        0xC1, "PF 1"),
    (Qt.Key_F8,        0xC8, "PF 8"),
    (Qt.Key_F9,        0xC9, "PF 9"),
    (Qt.Key_F10,       0xCA, "PF 10"),
    (Qt.Key_F12,       0xCC, "PF 12"),
])
def test_core_maps_host_keys_to_the_right_physical_key(qtkey, code, taste):
    """Welche Taste der echten K7637 spricht ein Host-Anschlag an?

    Bis hierher war das nur am laufenden Gast zu sehen; `k1520_translate_key`
    beantwortet es ohne Maschine.  Abgebildet wird die HARDWARE, nicht eine
    Wirkung: die Rücktaste des PC spricht die Rücktaste der K7637 an (`|←|`,
    Reihe 2 Position 14, 0x9F), die Entf-Taste DEL CH.  Welche von beiden ein
    Zeichen loescht, entscheidet der Gast und ist je OS verschieden (SCPX:
    `|←|`; CP/A und UDOS: DEL CH) — nachgemessen in
    `doc/design/08_k7637_keyboard.md` §5.1a.  Esc schickt 0x1B, nicht DEL L.
    """
    from app.core_binding.k1520 import K1520Emulator
    assert K1520Emulator.translate_key(int(qtkey)) == code, taste


@requires_core
def test_printable_and_raw_codes_pass_through():
    from app.core_binding.k1520 import K1520Emulator as E
    assert E.translate_key(ord("a")) == ord("a")
    assert E.translate_key(ord("A")) == ord("A")
    assert E.translate_key(ord("c"), ctrl=True) == 0x03      # Strg-C
    assert E.translate_key(0x02000000 | 0xB9) == 0xB9        # Rohcode CE
    # Ein Steuerzeichen als BLANKER Tastencode kommt nicht durch — genau daran
    # war die ESC-Taste der Bildschirmtastatur wirkungslos.
    assert E.translate_key(0x1B) == 0x00

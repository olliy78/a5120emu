"""`app/ui/keyboard.py` — Qt-Tastenereignis → Kern-Tastencode.

`qt_event_to_core_key` sitzt zwischen der Host-Tastatur und dem emulierten
K7637.  Die Abbildung ist die einzige Stelle, an der Ctrl-Kombinationen,
Sondertasten und das Host-Layout auseinandersortiert werden — reine Funktion,
also gut isoliert prüfbar.
"""

import pytest

from PySide6.QtCore import Qt

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
    Qt.Key_Return, Qt.Key_Enter, Qt.Key_Backspace, Qt.Key_Tab, Qt.Key_Escape,
    Qt.Key_Delete, Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right,
])
def test_special_keys_pass_through_unchanged(key):
    """Sondertasten reicht der Kern als Qt::Key_* 1:1 durch."""
    assert qt_event_to_core_key(FakeKeyEvent(key)) == (int(key), False, False)


@pytest.mark.parametrize("key", [Qt.Key_F1, Qt.Key_F4, Qt.Key_F8])
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

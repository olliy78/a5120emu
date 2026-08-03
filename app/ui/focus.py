"""
K1520 Emulator - Tastaturfokus
===============================

Der Tastaturfokus gehört dem **emulierten Rechner**, nicht der Bedienoberfläche.

Qt gibt normalerweise jedem angeklickten Bedienelement den Fokus — nach einem
Klick auf „Power ON" oder „Mount" landeten Tastendrücke deshalb beim Knopf
statt beim Bildschirm-Widget, und man musste erst wieder in die Röhre klicken.
Zwei Maßnahmen verhindern das:

1. :func:`release_focus` setzt alle reinen Bedienelemente (Knöpfe, Auswahl-
   felder, Regler, Reiter, Scrollflächen) auf ``Qt.NoFocus`` — sie lassen sich
   mit der Maus normal bedienen, nehmen dem Bildschirm den Fokus aber nicht ab.
   Dieselbe Technik nutzt die Bildschirmtastatur schon lange (``keyboard.py``).
   Muss nach jedem Neuaufbau von Panels erneut aufgerufen werden.
2. :class:`ScreenFocusGuard` holt den Fokus zurück zum Bildschirm — nach jedem
   Klick, der nicht in eine echte Texteingabe ging, und beim Aktivieren des
   Fensters.  Damit kommt der Fokus auch aus den Zahlenfeldern der Einstellungen
   (die den Fokus zum Tippen brauchen) von selbst wieder zurück.
"""

from PySide6.QtCore import QEvent, QObject, Qt, QTimer
from PySide6.QtWidgets import (
    QAbstractButton, QAbstractScrollArea, QAbstractSpinBox, QApplication,
    QComboBox, QLineEdit, QPlainTextEdit, QSlider, QTabBar, QTextEdit, QWidget
)


# Bedienelemente, die den Fokus nicht brauchen: Klick = Bedienung, sonst nichts.
_CONTROL_TYPES = (QAbstractButton, QComboBox, QSlider, QTabBar,
                  QAbstractScrollArea, QLineEdit)


def wants_keyboard(widget: QWidget) -> bool:
    """``True`` für Widgets, die selbst Tastatureingaben entgegennehmen.

    Das sind nur echte Texteingaben — Zahlenfelder, beschreibbare Textfelder und
    editierbare Auswahlfelder.  Alles andere ist ein Bedienelement, dessen
    Fokusanspruch dem Emulator die Tastatur wegnimmt.
    """
    if isinstance(widget, QAbstractSpinBox):
        return not widget.isReadOnly()
    if isinstance(widget, QLineEdit):
        return not widget.isReadOnly()
    if isinstance(widget, (QTextEdit, QPlainTextEdit)):
        return not widget.isReadOnly()
    if isinstance(widget, QComboBox):
        return widget.isEditable()
    return False


def in_text_input(widget) -> bool:
    """``True``, wenn *widget* eine Texteingabe ist oder in einer steckt.

    Der Ahnen-Durchlauf ist nötig, weil zusammengesetzte Widgets eigene
    Kindelemente haben (ein ``QDoubleSpinBox`` z.B. eine interne ``QLineEdit``),
    die weder auf ``NoFocus`` gesetzt noch als Klickziel als „Bedienelement"
    gewertet werden dürfen.
    """
    node = widget if isinstance(widget, QWidget) else None
    while node is not None:
        if wants_keyboard(node):
            return True
        node = node.parentWidget()
    return False


def release_focus(root: QWidget) -> None:
    """Alle Bedienelemente unterhalb (und einschließlich) *root* auf ``NoFocus``.

    Texteingaben und deren Innenleben bleiben unangetastet, sonst könnte man dort
    nichts mehr eintippen.
    """
    for widget in [root, *root.findChildren(QWidget)]:
        if isinstance(widget, _CONTROL_TYPES) and not in_text_input(widget):
            widget.setFocusPolicy(Qt.NoFocus)


class ScreenFocusGuard(QObject):
    """Anwendungsweiter Ereignisfilter, der den Fokus beim Bildschirm hält.

    Nach jedem Mausklick in das Hauptfenster, der nicht in eine Texteingabe ging,
    und nach jedem Aktivieren des Fensters bekommt das Bildschirm-Widget den
    Fokus zurück.  Die Zuweisung passiert verzögert (``singleShot(0)``), damit
    die normale Klickverarbeitung — inklusive eines eventuell geöffneten modalen
    Dialogs — vorher abläuft; nach dem Schließen des Dialogs liegt der Fokus dann
    wieder auf der Röhre.
    """

    def __init__(self, window: QWidget, screen: QWidget):
        super().__init__(window)
        self._window = window
        self._screen = screen
        app = QApplication.instance()
        if app is not None:
            app.installEventFilter(self)

    def focus_screen(self) -> None:
        """Fokus (zurück) auf das Bildschirm-Widget, falls es sichtbar ist."""
        screen = self._screen
        if screen is not None and screen.isVisible():
            screen.setFocus(Qt.OtherFocusReason)

    def _belongs_to_window(self, obj) -> bool:
        """``True``, wenn *obj* ein Widget im Hauptfenster ist (kein Dialog/Menü)."""
        return (isinstance(obj, QWidget)
                and (obj is self._window or self._window.isAncestorOf(obj)))

    def eventFilter(self, obj, event):
        etype = event.type()
        if etype == QEvent.MouseButtonPress:
            if self._belongs_to_window(obj) and not in_text_input(obj):
                QTimer.singleShot(0, self.focus_screen)
        elif etype == QEvent.WindowActivate and obj is self._window:
            # Rückkehr aus einer anderen Anwendung: weitertippen im Emulator,
            # außer man hatte zuletzt bewusst in einem Eingabefeld gestanden.
            if not in_text_input(self._window.focusWidget()):
                QTimer.singleShot(0, self.focus_screen)
        return False

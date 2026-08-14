"""Das Handbuch — eine `.md`-Datei, von Qt selbst gesetzt.

``QTextBrowser`` versteht Markdown seit Qt 5.14 (:meth:`QTextDocument.setMarkdown`).
Damit braucht die Hilfe **keinen Bauschritt und keine Abhängigkeit**: die Quelle
liegt als `app/disktool/help/handbuch.md` im Baum, ist dort lesbar und wird im
Paket ohne Zutun mitgeliefert (`packaging/build_payload.sh` schiebt `app/` als
Ganzes in die Nutzlast).  Entwurf und die verworfenen Alternativen:
`doc/design/13_k1520disktool.md` §20.7.

Zwei Dinge, die Qts Markdown **nicht** kann, und wie sie hier gelöst sind:

* **Überschriften bekommen keine Anker**, `[…](#abschnitt)` geht also nicht.  Das
  Inhaltsverzeichnis links wird deshalb nicht aus Ankern gebaut, sondern aus den
  Textblöcken mit ``headingLevel() == 2`` — gesprungen wird auf den Block selbst.
  Das ist genauer als eine Textsuche: eine Überschrift, deren Wortlaut auch im
  Fließtext vorkommt, führt nicht in die Irre.
* **Die Typografie ist karg.**  Ein Formatvorlagenblatt wirkt nur auf HTML, nicht
  auf importiertes Markdown — deshalb der Umweg ``setMarkdown`` → ``toHtml`` →
  ``setHtml`` mit gesetztem ``defaultStyleSheet``.
"""

from __future__ import annotations

from pathlib import Path
from typing import List, Tuple

from PySide6.QtCore import Qt, QUrl
from PySide6.QtGui import QDesktopServices, QKeySequence, QShortcut, QTextCursor
from PySide6.QtWidgets import (
    QHBoxLayout, QLabel, QLineEdit, QListWidget, QListWidgetItem, QPushButton,
    QSplitter, QTextBrowser, QVBoxLayout, QWidget,
)

#: Die Handbuchdatei — neben dem Paket-Baum, nicht in `doc/` (das ist nicht dabei).
HANDBUCH = Path(__file__).resolve().parents[1] / "help" / "handbuch.md"

#: Qts CSS ist eine Teilmenge; das hier wirkt und reicht für Text und Tabellen.
STIL = """
h1 { font-size: 20pt; }
h2 { font-size: 14pt; }
h3 { font-size: 12pt; }
p, li { line-height: 130%; }
th, td { padding: 3px 9px; }
th { background: palette(alternate-base); }
"""


def lade_handbuch(pfad: Path = HANDBUCH) -> str:
    """Den Handbuchtext lesen.  Fehlt die Datei, sagt das Fenster das auch."""
    try:
        return pfad.read_text(encoding="utf-8")
    except OSError as e:
        return (f"# Handbuch nicht gefunden\n\nDie Datei `{pfad}` liess sich nicht "
                f"lesen:\n\n    {e}\n")


def abschnitte(text: str) -> List[str]:
    """Die `##`-Überschriften des Handbuchs, in ihrer Reihenfolge."""
    return [z[3:].strip() for z in text.splitlines() if z.startswith("## ")]


class HelpWindow(QWidget):
    """Eigenes Fenster (nicht modal — man liest nach und arbeitet weiter)."""

    def __init__(self, parent=None):
        super().__init__(parent, Qt.Window)
        self.setWindowTitle("k1520DiskTool — Handbuch")
        self.resize(940, 700)

        self.text = lade_handbuch()

        # ── Inhaltsverzeichnis ──────────────────────────────────────────────
        self.inhalt = QListWidget()
        self.inhalt.setMaximumWidth(280)
        for name in abschnitte(self.text):
            QListWidgetItem(name, self.inhalt)
        self.inhalt.currentTextChanged.connect(self.springe_zu)

        # ── Anzeige ─────────────────────────────────────────────────────────
        self.browser = QTextBrowser()
        self.browser.setOpenExternalLinks(False)
        self.browser.setOpenLinks(False)
        self.browser.anchorClicked.connect(self._verweis)
        self._setze_text(self.text)

        # ── Suche ───────────────────────────────────────────────────────────
        self.suchfeld = QLineEdit()
        self.suchfeld.setPlaceholderText("Im Handbuch suchen …")
        self.suchfeld.setClearButtonEnabled(True)
        self.suchfeld.returnPressed.connect(self.weitersuchen)
        self.btn_weiter = QPushButton("Weiter")
        self.btn_weiter.clicked.connect(self.weitersuchen)
        self.meldung = QLabel("")

        kopf = QHBoxLayout()
        kopf.setContentsMargins(0, 0, 0, 0)
        kopf.addWidget(self.suchfeld, 1)
        kopf.addWidget(self.btn_weiter)
        kopf.addWidget(self.meldung)

        teiler = QSplitter(Qt.Horizontal)
        teiler.addWidget(self.inhalt)
        teiler.addWidget(self.browser)
        teiler.setStretchFactor(0, 0)
        teiler.setStretchFactor(1, 1)

        lay = QVBoxLayout(self)
        lay.addLayout(kopf)
        lay.addWidget(teiler, 1)

        QShortcut(QKeySequence.Find, self, self.suchfeld.setFocus)
        QShortcut(QKeySequence.FindNext, self, self.weitersuchen)
        QShortcut(QKeySequence(Qt.Key_Escape), self, self.close)

    # ── Inhalt ──────────────────────────────────────────────────────────────

    def _setze_text(self, markdown: str) -> None:
        """Markdown setzen — über HTML, damit das Formatblatt greift."""
        dok = self.browser.document()
        dok.setMarkdown(markdown)
        html = dok.toHtml()
        dok.setDefaultStyleSheet(STIL)
        dok.setHtml(html)
        self.browser.moveCursor(QTextCursor.Start)

    def ueberschriften(self) -> List[Tuple[str, int]]:
        """(Text, Blockposition) aller `##`-Überschriften im gesetzten Dokument."""
        gefunden = []
        block = self.browser.document().begin()
        while block.isValid():
            if block.blockFormat().headingLevel() == 2:
                gefunden.append((block.text().strip(), block.position()))
            block = block.next()
        return gefunden

    def springe_zu(self, ueberschrift: str) -> bool:
        """Zu einem Abschnitt springen; ``False``, wenn es ihn nicht gibt."""
        for text, pos in self.ueberschriften():
            if text == ueberschrift:
                cursor = self.browser.textCursor()
                cursor.setPosition(pos)
                self.browser.setTextCursor(cursor)
                # Den Abschnitt an den oberen Rand holen: erst ans Ende der Sicht,
                # dann zurück — sonst klebt die Überschrift unten.
                self.browser.verticalScrollBar().setValue(
                    self.browser.verticalScrollBar().maximum())
                self.browser.setTextCursor(cursor)
                self.browser.ensureCursorVisible()
                return True
        return False

    # ── Suche ───────────────────────────────────────────────────────────────

    def weitersuchen(self) -> bool:
        """Nächste Fundstelle; am Ende wird einmal von vorn begonnen."""
        wort = self.suchfeld.text().strip()
        if not wort:
            self.meldung.setText("")
            return False
        if not self.browser.find(wort):
            self.browser.moveCursor(QTextCursor.Start)
            if not self.browser.find(wort):
                self.meldung.setText(f'„{wort}" nicht gefunden')
                return False
            self.meldung.setText("von vorn")
            return True
        self.meldung.setText("")
        return True

    # ── Verweise ────────────────────────────────────────────────────────────

    def _verweis(self, url: QUrl) -> None:
        """Ein Verweis im Text.

        Innerhalb des Handbuchs gibt es nur Abschnitte; alles mit Schema (``http``,
        ``mailto``) geht an den Systembrowser — das Hilfefenster ist keiner.
        """
        if url.scheme() in ("http", "https", "mailto"):
            QDesktopServices.openUrl(url)

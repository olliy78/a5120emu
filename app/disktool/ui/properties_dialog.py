"""Eigenschaften einer Datei — anzeigen und ändern.

Eine Datei auf einer K1520-Diskette trägt mehr als ihre Bytes.  Bei **UDOS** steht
im Kopfsektor, wie das Betriebssystem sie *lädt* — Typ, Eigenschaften,
Einsprungadresse, Satzlänge und der Speicher, den sie belegt
(`doc/udos_diskettenformat.md` §6/§14).  Bei **CP/M** ist es viel weniger, aber
nicht nichts: drei Attributbits und der Nutzerbereich.  Beides geht beim
Extrahieren verloren und ist auf einer gewöhnlichen Linux-Datei nirgends ablesbar
— deshalb dieser Dialog.

Zwei Festlegungen, die man kennen muss:

* **Satzlänge und „Bytes im letzten Satz“ sind hier nur ANZEIGE.**  Beide bestimmen
  die Sektorlage der Daten; sie zu ändern hieße, die Datei neu zu schreiben, nicht
  bloß den Kopfsektor zu ändern.  Der Dialog fasst den Inhalt einer Datei nicht an.
* **Ein Feld, das nicht angefasst wurde, wird nicht geschrieben.**  Gesendet wird
  nur, was sich gegenüber dem geladenen Stand unterscheidet — so kann der Dialog
  ein einzelnes Feld setzen, ohne die übrigen zu kennen.

Der Dialog ist ohne Diskette benutzbar (headless prüfbar): er bekommt einen
``Entry`` und ein ``DiskTool``; das Schreiben passiert erst in :meth:`uebernehmen`.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Optional

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QGroupBox,
    QHBoxLayout, QLabel, QLineEdit, QMessageBox, QSpinBox, QVBoxLayout, QWidget,
)

if TYPE_CHECKING:                                   # pragma: no cover
    from app.core_binding.k1520disk import DiskTool, Entry

#: UDOS-Dateitypen mit Klartext (`doc/udos_diskettenformat.md` §6.1).
UDOS_TYPEN = [
    ("A",  "A — ASCII (Textdatei)"),
    ("B",  "B — BINARY"),
    ("P",  "P — PROCEDURE (ausführbares Programm)"),
    ("P1", "P1 — Procedure Untertyp 1 (Treiber/Modul)"),
    ("D",  "D — DIRECTORY (das Verzeichnis selbst)"),
]

#: UDOS-Eigenschaften: Buchstabe → Klartext (Offset 19).
UDOS_EIGENSCHAFTEN = [
    ("W", "W — schreibgeschützt (WRITE PROTECTED)"),
    ("E", "E — löschgeschützt (ERASE PROTECTED)"),
    ("L", "L — Eigenschaften gesperrt (LOCKED)"),
    # Das `&&` ist kein Tippfehler: Qt liest ein einzelnes `&` als Tastenkürzel und
    # verschluckt es — der UDOS-Suchoperator heisst aber `P=&`.
    ("S", "S — geheim (SECRET, ohne P=&& nicht gelistet)"),
    ("R", "R — wahlfreier Zugriff (RANDOM)"),
    ("F", "F — feste Speicherzuteilung (FORCE)"),
]


def _hex16(v: int) -> str:
    return f"{v & 0xFFFF:04X}"


def _parse_hex(text: str, feld: str) -> int:
    """Hexadezimal einlesen; leer = 0.  Wirft ``ValueError`` mit Klartext."""
    t = text.strip().upper().removeprefix("0X").removesuffix("H")
    if not t:
        return 0
    try:
        v = int(t, 16)
    except ValueError:
        raise ValueError(f"{feld}: '{text}' ist keine hexadezimale Zahl") from None
    if not 0 <= v <= 0xFFFF:
        raise ValueError(f"{feld}: {text} passt nicht in 16 Bit")
    return v


class PropertiesDialog(QDialog):
    """Eigenschaften einer Datei — je nach Dateisystem UDOS oder CP/M."""

    def __init__(self, tool: "DiskTool", entry: "Entry", parent=None):
        super().__init__(parent)
        self.tool = tool
        self.entry = entry
        self.setWindowTitle(f"Eigenschaften — {entry.name}")
        self.setMinimumWidth(520)

        # Beide UDOS-Ausprägungen (ZDOS auf dem A5120, NDOS auf dem PC 1715) führen
        # dieselben Kopfsektorangaben — nur CP/M nicht.
        self.udos = bool(entry.type) or tool.filesystem_type.startswith("udos")

        lay = QVBoxLayout(self)
        lay.addWidget(self._kopf())
        lay.addWidget(self._udos_teil() if self.udos else self._cpm_teil(), 1)

        self.hinweis = QLabel("")
        self.hinweis.setWordWrap(True)
        self.hinweis.setStyleSheet("color: #a06000;")
        if tool.read_only:
            self.hinweis.setText(
                "Die Diskette ist schreibgeschützt geöffnet — die Felder lassen sich "
                "ansehen, aber nicht ändern.  Zum Ändern den Haken „Nur lesen“ "
                "entfernen.")
        lay.addWidget(self.hinweis)

        self.knoepfe = QDialogButtonBox(
            QDialogButtonBox.Save | QDialogButtonBox.Close)
        self.knoepfe.button(QDialogButtonBox.Save).setText("Übernehmen")
        self.knoepfe.button(QDialogButtonBox.Close).setText("Schließen")
        self.knoepfe.accepted.connect(self.uebernehmen)
        self.knoepfe.rejected.connect(self.reject)
        self.knoepfe.button(QDialogButtonBox.Save).setEnabled(not tool.read_only)
        lay.addWidget(self.knoepfe)

    # ════════════════════════════════════════════════════════════════════════
    # Aufbau
    # ════════════════════════════════════════════════════════════════════════

    def _kopf(self) -> QWidget:
        """Was feststeht: Name, Größe, Seite, Zustand — nichts davon änderbar."""
        e = self.entry
        kasten = QGroupBox("Datei")
        form = QFormLayout(kasten)
        form.addRow("Name", QLabel(e.name))
        if e.side_dir:
            form.addRow("Seite", QLabel(f"{e.side_dir} (eigenständiger Datenträger)"))
        form.addRow("Größe", QLabel(f"{e.size} Byte"))
        if e.damaged:
            defekt = QLabel("nicht vollständig lesbar (CRC-Fehler oder Kettenbruch)")
            defekt.setStyleSheet("color: #b00000;")
            form.addRow("Zustand", defekt)
        return kasten

    def _udos_teil(self) -> QWidget:
        e = self.entry
        seite = QWidget()
        aussen = QVBoxLayout(seite)
        aussen.setContentsMargins(0, 0, 0, 0)

        # ── Typ und Eigenschaften ───────────────────────────────────────────
        oben = QGroupBox("Typ und Eigenschaften")
        form = QFormLayout(oben)

        self.f_typ = QComboBox()
        for kurz, text in UDOS_TYPEN:
            self.f_typ.addItem(text, kurz)
        i = self.f_typ.findData(e.type)
        self.f_typ.setCurrentIndex(i if i >= 0 else 0)
        form.addRow("Dateityp", self.f_typ)

        self.f_eig = {}
        kasten_eig = QVBoxLayout()
        for kurz, text in UDOS_EIGENSCHAFTEN:
            k = QCheckBox(text)
            k.setChecked(kurz in e.attrs)
            self.f_eig[kurz] = k
            kasten_eig.addWidget(k)
        huelle = QWidget()
        huelle.setLayout(kasten_eig)
        form.addRow("Eigenschaften", huelle)

        self.f_erstellt = QLineEdit(e.created)
        self.f_erstellt.setMaxLength(6)
        self.f_erstellt.setToolTip(
            "6 Zeichen: Datum „JJMMTT“ ODER ein Versionstext wie „V 4.3 “.")
        form.addRow("Erstellt", self.f_erstellt)

        self.f_geaendert = QLineEdit(e.date)
        self.f_geaendert.setMaxLength(6)
        self.f_geaendert.setToolTip("Datum der letzten Änderung, „JJMMTT“.")
        form.addRow("Geändert", self.f_geaendert)
        aussen.addWidget(oben)

        # ── Laden und Speicher ──────────────────────────────────────────────
        unten = QGroupBox("Laden und Speicher")
        f2 = QFormLayout(unten)

        self.f_entry = QLineEdit(_hex16(e.entry))
        self.f_entry.setToolTip(
            "ENTRY — Einsprungadresse; nur bei Typ P/P1 ausgewertet (hex).")
        f2.addRow("Einsprung (ENTRY)", self.f_entry)

        # EIN Feld für die GANZE Liste: eine Programmdatei kann mehrere Segmente
        # haben (`ZLINK` einer PC-1715-Diskette sechs).  Zwei Kästchen für Anfang und
        # Länge zeigten davon nur das erste und behaupteten damit etwas Falsches.
        self.f_segs = QLineEdit(e.segments)
        self.f_segs.setPlaceholderText("z. B.  2600+1591  4000+0200")
        w = self.f_segs
        w.setToolTip(
            "SEGMENTS — alle Speichersegmente als ANFANG+LÄNGE (hex), durch "
            "Leerzeichen getrennt.  Die Länge ist NICHT die Dateigröße: sie reicht "
            "bei Programmen über das logische Dateiende hinaus.  Bei Typ A steht an "
            "dieser Stelle Anwenderinhalt — dann bleibt das Feld leer.")
        f2.addRow("Segment", w)

        self.f_low = QLineEdit(_hex16(e.low_addr))
        self.f_high = QLineEdit(_hex16(e.high_addr))
        self.f_stack = QLineEdit(_hex16(e.stack_size))
        zeile2 = QHBoxLayout()
        for beschriftung, feld in (("LOW", self.f_low), ("HIGH", self.f_high),
                                   ("STACK", self.f_stack)):
            zeile2.addWidget(QLabel(beschriftung))
            zeile2.addWidget(feld, 1)
        w2 = QWidget()
        w2.setLayout(zeile2)
        w2.setToolTip(
            "Was der Lader zuteilen lässt (Kopfsektor 122/124/126, hex) — genau das, "
            "was EXTRACT im laufenden System meldet.  Stehen dort FFFF, weist UDOS "
            "die Datei mit MEMORY PROTECT VIOLATION ab.")
        f2.addRow("Speicher", w2)

        self.f_block = QSpinBox()
        self.f_block.setRange(0, 65535)
        self.f_block.setValue(e.block_len)
        self.f_block.setToolTip(
            "Kopfsektor Offset 17 — zweite Längenangabe.  0 ist ein GÜLTIGER Wert "
            "(bei Satzlänge 256/512); mit dem falschen Wert startet ein neu "
            "geschriebener Nukleus nicht mehr.")
        f2.addRow("Zweite Länge", self.f_block)

        self.f_zusatz = QLineEdit(f"{e.extra:08X}")
        self.f_zusatz.setToolTip(
            "Kopfsektor 44…47 — Bedeutung offen; unverändert übernehmen (hex).")
        f2.addRow("Zusatz", self.f_zusatz)

        # Nur Anzeige: beides bestimmt die Sektorlage der Daten.
        f2.addRow("Satzlänge", self._nur_anzeige(
            f"{e.record_len} Byte",
            "Die Zuteilungseinheit von UDOS.  Sie zu ändern hieße, die Datei neu zu "
            "schreiben — dieser Dialog fasst den Inhalt nicht an."))
        f2.addRow("Letzter Satz", self._nur_anzeige(
            f"{e.bytes_in_last} Byte belegt",
            "Bytes im letzten Satz (Kopfsektor 22).  Bestimmt zusammen mit der "
            "Satzanzahl die logische Länge der Datei."))
        aussen.addWidget(unten)
        return seite

    def _cpm_teil(self) -> QWidget:
        e = self.entry
        kasten = QGroupBox("CP/M-Attribute")
        form = QFormLayout(kasten)

        self.f_user = QSpinBox()
        self.f_user.setRange(0, 15)
        self.f_user.setValue(e.user)
        self.f_user.setToolTip(
            "Nutzerbereich 0…15.  Er gehört zur Identität der Datei: eine Änderung "
            "verschiebt sie nach „3:NAME.TYP“.")
        form.addRow("Nutzerbereich", self.f_user)

        self.f_ro = QCheckBox("R/O — nur lesen")
        self.f_ro.setChecked(e.read_only)
        self.f_sys = QCheckBox("SYS — Systemdatei (im DIR unsichtbar)")
        self.f_sys.setChecked(e.system)
        self.f_arc = QCheckBox("ARC — archiviert")
        self.f_arc.setChecked(e.archived)
        for k in (self.f_ro, self.f_sys, self.f_arc):
            form.addRow("", k)

        hinweis = QLabel(
            "Mehr führt CP/M 2.2 nicht: kein Datum, keine Ladeadresse, keine "
            "Satzlänge.  Die Länge ergibt sich aus der Zahl der 128-Byte-Sätze, "
            "die tatsächliche Nutzlänge steht nirgends.")
        hinweis.setWordWrap(True)
        form.addRow(hinweis)
        return kasten

    @staticmethod
    def _nur_anzeige(text: str, warum: str) -> QLabel:
        w = QLabel(text)
        w.setToolTip(warum)
        w.setTextInteractionFlags(Qt.TextSelectableByMouse)
        w.setStyleSheet("color: #555555;")
        return w

    # ════════════════════════════════════════════════════════════════════════
    # Übernehmen
    # ════════════════════════════════════════════════════════════════════════

    def eigenschaften_text(self) -> str:
        """Die angehakten UDOS-Eigenschaften als Buchstabenfolge ("WS")."""
        return "".join(k for k, w in self.f_eig.items() if w.isChecked())

    def aenderungen(self) -> dict:
        """Was sich gegenüber dem geladenen Stand unterscheidet.

        Der Rückgabewert geht so, wie er ist, an ``set_udos_attrs`` bzw.
        ``set_cpm_attrs`` — ein leeres Wörterbuch heißt „nichts zu tun“.

        Raises:
            ValueError: eine Eingabe ist keine gültige Zahl (mit Klartext).
        """
        e = self.entry
        aend: dict = {}
        if not self.udos:
            if self.f_user.value() != e.user:      aend["user"] = self.f_user.value()
            if self.f_ro.isChecked() != e.read_only:  aend["read_only"] = self.f_ro.isChecked()
            if self.f_sys.isChecked() != e.system:    aend["system"] = self.f_sys.isChecked()
            if self.f_arc.isChecked() != e.archived:  aend["archived"] = self.f_arc.isChecked()
            return aend

        if self.f_typ.currentData() != e.type:
            aend["type"] = self.f_typ.currentData()
        neu_eig = self.eigenschaften_text()
        if neu_eig != e.attrs:
            # Ein leeres Feld hieße „unverändert“ — alle löschen sagt man mit ";".
            aend["properties"] = neu_eig or ";"
        if self.f_erstellt.text() != e.created:
            aend["created"] = self.f_erstellt.text()
        if self.f_geaendert.text() != e.date:
            aend["modified"] = self.f_geaendert.text()

        eintritt = _parse_hex(self.f_entry.text(), "Einsprung")
        if eintritt != e.entry:
            aend["entry"] = eintritt
        segs = " ".join(self.f_segs.text().split())
        if segs != e.segments:
            aend["segments"] = segs
        mem = (_parse_hex(self.f_low.text(), "LOW"),
               _parse_hex(self.f_high.text(), "HIGH"),
               _parse_hex(self.f_stack.text(), "STACK"))
        if mem != (e.low_addr, e.high_addr, e.stack_size):
            aend["memory"] = mem
        if self.f_block.value() != e.block_len:
            aend["block_len"] = self.f_block.value()
        zusatz = self.f_zusatz.text().strip() or "0"
        try:
            zusatz_wert = int(zusatz, 16)
        except ValueError:
            raise ValueError(f"Zusatz: '{zusatz}' ist keine hexadezimale Zahl") from None
        if zusatz_wert != e.extra:
            aend["extra"] = zusatz_wert
        return aend

    def uebernehmen(self) -> bool:
        """Änderungen schreiben und den Dialog schließen.  False = nicht geschrieben."""
        from app.core_binding.k1520disk import K1520DiskError

        try:
            aend = self.aenderungen()
        except ValueError as e:
            QMessageBox.warning(self, "Ungültige Eingabe", str(e))
            return False

        if not aend:
            self.accept()
            return True

        try:
            if self.udos:
                self.tool.set_udos_attrs(self.entry.ref, **aend)
            else:
                self.tool.set_cpm_attrs(self.entry.ref, **aend)
        except K1520DiskError as e:
            QMessageBox.critical(self, "Nicht übernommen", str(e))
            return False

        self.accept()
        return True

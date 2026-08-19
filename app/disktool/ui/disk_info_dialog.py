"""„Diskettenangaben…" — alles über den Datenträger, an einer Stelle.

Der Kopfbereich des Fensters trägt nur, was man ständig sieht (Pfad, Format,
Dateisystem).  Geometrie, Alternativen der Erkennung, Auffälligkeiten des Mediums
und die Belegung je Seite stehen hier — nachschlagbar, statt den Kopf zu füllen
(doc/design/13_k1520disktool.md §20.2).
"""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import (
    QDialog, QDialogButtonBox, QTextBrowser, QVBoxLayout,
)


def angaben_text(tool) -> str:
    """Die Angaben als Klartext — auch ohne Dialog prüfbar."""
    zeilen = [
        f"Datei:            {tool.path}",
        f"Format:           {tool.format}",
        f"Dateisystem:      {tool.filesystem} ({tool.filesystem_type or 'unbekannt'})",
        f"Erkennung:        {'eindeutig' if tool.unambiguous else 'nicht eindeutig'}",
    ]
    if tool.alternatives:
        zeilen.append(f"Auch möglich:     {', '.join(tool.alternatives)}")
    zeilen += [
        f"Medium:           {tool.medium_cylinders} Zylinder × "
        f"{tool.medium_heads} Kopf/Köpfe",
        f"Schreibschutz:    {'ja' if tool.read_only else 'nein'}",
        f"Ungespeichert:    {'ja' if tool.dirty else 'nein'}",
    ]
    if tool.remarks:
        zeilen.append(f"Auffälligkeiten:  {tool.remarks}")

    boot = tool.boot_area_size(0)
    zeilen.append(f"Systemspuren:     {boot} Byte" if boot > 0
                  else "Systemspuren:     keine (Datendiskette)")

    zeilen.append("")
    zeilen.append("BELEGUNG")
    for v in tool.volumes():
        name = f"{v.dir} " if tool.volume_count > 1 else ""
        zeilen.append(
            f"  {name}{v.label or '(ohne Namen)'}: "
            f"{v.used // 1024} KB belegt, {v.free // 1024} KB frei "
            f"von {v.total // 1024} KB")

    zeilen.append("")
    zeilen += befund_zeilen(tool)
    return "\n".join(zeilen)


def befund_zeilen(tool) -> list:
    """Der Prüfbericht als Abschnitt — hier steht er in voller Länge.

    Der Meldungsstreifen trägt nur EINE Zeile; nachschlagbar ist der Befund an
    dieser Stelle (doc/design/15_dateisystempruefung.md §16.1).
    """
    if not tool.has_filesystem:
        return ["PRÜFBERICHT", "  (kein Dateisystem — nichts zu prüfen)"]

    bericht = tool.findings()
    kopf = "PRÜFBERICHT"
    if bericht.tracks_total:
        kopf += (f"   ({bericht.tracks_read} von {bericht.tracks_total} Spuren"
                 f" angesehen)")
    zeilen = [kopf]
    if not bericht.findings:
        # Der Unterschied ist wichtig genug für ein eigenes Wort: an einer
        # physischen Diskette heißt „nichts gefunden" nur „bislang nichts".
        zeilen.append("  ohne Befund" if bericht.complete else "  bislang ohne Befund")
        return zeilen

    for f in bericht.findings:
        wo = f"c{f.cyl}h{f.head}" if f.ortbar else ""
        zeilen.append(f"  {f.schwere:<8}{f.ebene:<11}{wo:<8}{f.object:<16}{f.text}")
    if not bericht.complete:
        zeilen.append("  (unvollständig — es sind noch nicht alle Spuren gelesen)")
    return zeilen


class DiskInfoDialog(QDialog):
    """Nur-Lese-Fenster mit :func:`angaben_text` — plus der Vollprüfung.

    Der Prüfbericht steht hier ohnehin; damit ist dies auch der Ort, an dem sich
    die **volle** Prüfung anstoßen lässt.  Sie fasst jede Spur an und findet
    dadurch, was die Schnellprüfung nicht sehen kann: Kettenbrüche,
    Kreuzbelegungen, den Abgleich Belegungsplan ↔ Dateien und Sektoren mit
    falscher Prüfsumme (doc/design/15_dateisystempruefung.md §6).
    """

    def __init__(self, tool, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Diskettenangaben")
        self.resize(620, 420)
        self.tool = tool
        #: Wurde hier eine Prüfung gefahren?  Dann muss das Fenster dahinter seine
        #: Anzeige auffrischen — der Befund ist ein Zustand der Diskette.
        self.geprueft = False

        self.text = QTextBrowser()
        self.text.setLineWrapMode(QTextBrowser.NoWrap)
        self.text.setPlainText(angaben_text(tool))

        self.knoepfe = QDialogButtonBox(QDialogButtonBox.Close)
        self.knopf_voll = self.knoepfe.addButton("&Vollprüfung",
                                                 QDialogButtonBox.ActionRole)
        self.knopf_voll.setToolTip(
            "Jede Spur ansehen: Ketten, Kreuzbelegungen, Belegungsplan und "
            "Prüfsummen.  Die Schnellprüfung beim Öffnen kann das nicht leisten.")
        self.knopf_voll.clicked.connect(self._vollpruefung)
        # An einer PHYSISCHEN Diskette zieht die Vollprüfung die ganze Scheibe ein
        # (0,5–0,8 s je Spur) und blockierte das Fenster ein bis zwei Minuten.  Das
        # gehört in einen Arbeitsfaden mit Fortschritt — bis es den gibt, ist der
        # Knopf gesperrt und sagt warum.
        if not tool.has_filesystem:
            self.knopf_voll.setEnabled(False)
            self.knopf_voll.setToolTip("Ohne erkanntes Dateisystem gibt es nichts "
                                       "zu prüfen")
        elif not tool.path:
            self.knopf_voll.setEnabled(False)
            self.knopf_voll.setToolTip(
                "An einer echten Diskette müsste dafür jede Spur einzeln gelesen "
                "werden (ein bis zwei Minuten) — das kommt mit dem "
                "Reparaturdialog.")

        self.knoepfe.rejected.connect(self.reject)
        self.knoepfe.accepted.connect(self.accept)

        lay = QVBoxLayout(self)
        lay.addWidget(self.text, 1)
        lay.addWidget(self.knoepfe)

    def _vollpruefung(self) -> None:
        QGuiApplication.setOverrideCursor(Qt.WaitCursor)
        try:
            self.tool.check(voll=True)
        finally:
            QGuiApplication.restoreOverrideCursor()
        self.geprueft = True
        self.text.setPlainText(angaben_text(self.tool))

"""„Diskettenangaben…" — alles über den Datenträger, an einer Stelle.

Der Kopfbereich des Fensters trägt nur, was man ständig sieht (Pfad, Format,
Dateisystem).  Geometrie, Alternativen der Erkennung, Auffälligkeiten des Mediums
und die Belegung je Seite stehen hier — nachschlagbar, statt den Kopf zu füllen
(doc/design/13_k1520disktool.md §20.2).
"""

from __future__ import annotations

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
    return "\n".join(zeilen)


class DiskInfoDialog(QDialog):
    """Nur-Lese-Fenster mit :func:`angaben_text`."""

    def __init__(self, tool, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Diskettenangaben")
        self.resize(620, 420)

        self.text = QTextBrowser()
        self.text.setLineWrapMode(QTextBrowser.NoWrap)
        self.text.setPlainText(angaben_text(tool))

        self.knoepfe = QDialogButtonBox(QDialogButtonBox.Close)
        self.knoepfe.rejected.connect(self.reject)
        self.knoepfe.accepted.connect(self.accept)

        lay = QVBoxLayout(self)
        lay.addWidget(self.text, 1)
        lay.addWidget(self.knoepfe)

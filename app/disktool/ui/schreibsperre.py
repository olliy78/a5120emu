"""Warum sich an dieser Diskette nichts schreiben lässt — an EINER Stelle.

Zwei Arten von Disketten sind nicht nur „schreibgeschützt geöffnet", sondern
**nicht beschreibbar**: ein CopyQM-Abbild (``.cqm``, der Kern kann es nicht
zurückschreiben) und eine tar-Diskette (ein Archivstrom, kein Dateisystem).  Der
Haken „Schreibschutz" lässt sich dort nicht abwählen; die Oberfläche soll deshalb
nicht raten „Schreibschutz aufheben", sondern den wahren Grund nennen.
"""

from __future__ import annotations


def sperrgrund(tool) -> str:
    """Begründung, warum die Diskette unaufhebbar gesperrt ist ("" = beschreibbar
    oder nur schreibgeschützt geöffnet)."""
    if tool is None or not tool.read_only_forced:
        return ""
    teile = []
    if tool.ist_tar:
        teile.append("tar-Disketten sind nur lesbar: sie tragen einen Archivstrom und "
                     "kein Dateisystem.")
    if tool.ist_cqm:
        teile.append("CopyQM-Abbilder (.cqm) sind nur lesbar. „Speichern unter…“ als "
                     ".hfe, .dmk oder .img legt eine beschreibbare Kopie an.")
    if teile:
        return " ".join(teile)
    return "Der Schreibschutz dieser Diskette lässt sich nicht aufheben."


def schutz_text(tool) -> str:
    """Kurztext für die Statuszeile ("" = nichts Besonderes)."""
    if tool is None or not tool.read_only_forced:
        return ""
    if tool.ist_cqm:
        return "R/O  schreibgeschützt (CopyQM)"
    if tool.ist_tar:
        return "R/O  nur lesbar (tar-Archiv)"
    return "R/O  nicht beschreibbar"


def dateisystem_anzeige(tool) -> str:
    """Name des Dateisystems in der Form, die man dem Anwender zeigt."""
    if tool.ist_tar:
        return "tar-Archiv (UNIX)"
    return tool.filesystem

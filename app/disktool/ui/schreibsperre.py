"""Warum sich an dieser Diskette nichts schreiben lässt — an EINER Stelle.

Eine tar-Diskette (ein Archivstrom, kein Dateisystem) ist nicht nur „schreibgeschützt
geöffnet", sondern **nicht beschreibbar**.  Der Haken „Schreibschutz" lässt sich dort
nicht abwählen; die Oberfläche soll deshalb nicht raten „Schreibschutz aufheben",
sondern den wahren Grund nennen.  (Ein CopyQM-Abbild, ``.cqm``, gehört seit dem
Schreiben im Kern NICHT mehr hierher — es ist wie ein ``.img`` beschreibbar.)
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
    if teile:
        return " ".join(teile)
    return "Der Schreibschutz dieser Diskette lässt sich nicht aufheben."


def schutz_text(tool) -> str:
    """Kurztext für die Statuszeile ("" = nichts Besonderes)."""
    if tool is None or not tool.read_only_forced:
        return ""
    if tool.ist_tar:
        return "R/O  nur lesbar (tar-Archiv)"
    return "R/O  nicht beschreibbar"


def dateisystem_anzeige(tool) -> str:
    """Name des Dateisystems in der Form, die man dem Anwender zeigt."""
    if tool.ist_tar:
        return "tar-Archiv (UNIX)"
    return tool.filesystem

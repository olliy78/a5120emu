"""Die Angabendatei (`.fileinfo`) schreiben — ohne Qt, ohne Kern.

Eine Linux-Datei trägt nur Bytes.  Was daneben in einem `<datei>.fileinfo` steht,
sind die Angaben, die das Dateisystem der Diskette führt und das Wirtsystem nicht:
bei UDOS Typ, Eigenschaften, Satzlänge, ENTRY, Segmente und die Speicheranforderung
LOW/HIGH/STACK, bei CP/M Nutzerbereich und Attribute
(`doc/bug_disktool_Programmdatei.md` §2.1).

**Gelesen** wird diese Form ausschliesslich im Kern (`leseFileinfo` in
`core/filesystem/disk_volume.cpp`) — es gibt genau einen Leser.  Geschrieben wird
sie an drei Stellen: dort beim Extrahieren, hier vom Eingabedialog der Oberfläche
und von der Kommandozeile der physischen Diskette.  Deshalb steht das Schreiben
hier und nicht im Dialog: `app/disktool/physical_cli.py` läuft **ohne Qt**.
"""

from __future__ import annotations

from pathlib import Path


def schreibe_fileinfo(ziel, angaben: dict, name: str) -> Path:
    """Eine Angabendatei schreiben — eine Zeile ``schlüssel=wert`` je Angabe.

    Args:
        ziel: Pfad der zu schreibenden Datei (üblich: ``<datei>.fileinfo``).
        angaben: Schlüssel/Wert, wie sie der Kern liest — ``typ``, ``eig``,
            ``start``, ``satz``, ``block``, ``segment``, ``segs``, ``mem``,
            ``zusatz``, ``erst``, ``geaend`` bzw. bei CP/M ``attr``.  Leere Werte
            werden weggelassen; unbekannte Schlüssel überliest der Leser.
        name: der **echte** Name auf der Diskette — bei CP/M mit Nutzerbereich
            (``3:SYSTEM.COM``), für den im Linux-Dateinamen ein Unterstrich steht.
    """
    p = Path(ziel)
    zeilen = [
        f"# k1520DiskTool — Angaben zu '{name}'.",
        "# Von Hand eingegeben (die Datei brachte keine mit).  Beim Einfuegen",
        "# (`put`) werden sie wieder uebernommen.",
        # `fs=` in der ersten Sachzeile: ein .fileinfo einer CP/M-Datei darf nicht
        # stillschweigend als UDOS-Angabe gelesen werden.
        f"fs={angaben.get('fs', 'udos')}",
        f"name={name}",
    ]
    for k, v in angaben.items():
        if k in ("fs", "name") or v in ("", None):
            continue
        zeilen.append(f"{k}={v}")
    p.write_text("\n".join(zeilen) + "\n", encoding="utf-8")
    return p

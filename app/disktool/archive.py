"""Archivieren: Abbild + Dateien + Inhaltsverzeichnis in EINER `.zip`.

Warum drei Dinge und nicht nur das Abbild:

* **Das Abbild** (immer als `.hfe`) ist die verlustfreie Fassung — es trägt die
  Spurstruktur, die Sektorkontrollblöcke von UDOS und alles, was ein Dateisystem
  nicht abbildet.  Es ist die Fassung, aus der sich alles Übrige wiederherstellen
  lässt.
* **Die Dateien** sind die benutzbare Fassung: lesbar ohne dieses Werkzeug, mit
  jedem Betriebssystem, auch in zwanzig Jahren.
* **Das Inhaltsverzeichnis** rettet, was beim Extrahieren zwangsläufig verloren
  geht.  Ein Linux- oder Windows-Dateisystem kennt keinen UDOS-*Dateityp* (`P1`
  gegen `B`), keine UDOS-*Eigenschaften* (`WELS`), keine CP/M-*Nutzerbereiche*
  und kein CP/M-`SYS`-Attribut.  Ohne diese Datei wäre nach dem Auspacken nicht
  mehr erkennbar, welche Datei ein ausführbares Programm war.

Benutzbar aus der Oberfläche und als Skript::

    python3 -m app.disktool.archive disks/udos_boot_scp.hfe archiv.zip
"""

from __future__ import annotations

import datetime
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:                                   # pragma: no cover
    from app.core_binding.k1520disk import DiskTool

#: Unterordner im Archiv, der die extrahierten Dateien aufnimmt.
DATEI_ORDNER = "dateien"


def _legende(hat_udos: bool, hat_cpm: bool) -> str:
    """Erklärt die Kürzel — das macht das Archiv selbsterklärend."""
    zeilen = ["LEGENDE", ""]
    if hat_udos:
        zeilen += [
            "  UDOS-Dateitypen   A  = ASCII (Textdatei)",
            "                    B  = BINARY",
            "                    P  = PROCEDURE (ausfuehrbares Programm)",
            "                    P1 = Procedure Untertyp 1 (Treiber/Modul)",
            "                    D  = DIRECTORY (das Verzeichnis selbst)",
            "  UDOS-Eigenschaften  W = schreibgeschuetzt   E = loeschgeschuetzt",
            "                      L = Eigenschaften gesperrt   S = geheim",
            "                      R = wahlfreier Zugriff  F = feste Speicherzuteilung",
            "  Seite             Side0 = UDOS-Laufwerk 0, Side1 = Laufwerk 4;",
            "                    jede Seite ist ein eigenstaendiger Datentraeger.",
        ]
    if hat_cpm:
        zeilen += [
            "  CP/M-Attribute    RO = nur lesen · SYS = Systemdatei (in DIR unsichtbar)",
            "                    ARC = archiviert",
            "  Nutzerbereich     0-15; im Namen als Praefix, z. B. 3:NAME.TYP",
        ]
    zeilen += [
        "  Groesse            Nutzbytes laut Verzeichnis.  CP/M rundet Dateien auf",
        "                     128-B-Saetze auf; die extrahierte Datei kann deshalb",
        "                     etwas laenger sein als die urspruengliche Nutzlaenge.",
        "  [DEFEKT]           die Datei war nicht vollstaendig lesbar (CRC-Fehler",
        "                     oder Kettenbruch) — sie liegt trotzdem bei.",
    ]
    return "\n".join(zeilen)


def inhaltsverzeichnis(tool: "DiskTool", quelle: str = "") -> str:
    """Das Inhaltsverzeichnis als Text — Kopf, Tabelle, Legende."""
    eintraege = tool.list()
    hat_udos = any(e.type for e in eintraege)
    hat_cpm = any(e.user or e.attrs in ("RO", "SYS", "ARC") for e in eintraege) \
        or not hat_udos
    mehrseitig = tool.volume_count > 1

    z = []
    z.append("k1520DiskTool — Inhaltsverzeichnis einer K1520-Diskette")
    z.append("=" * 70)
    z.append("")
    z.append(f"Abbild        {Path(quelle or tool.path).name}")
    z.append(f"Format        {tool.format}")
    z.append(f"Dateisystem   {tool.filesystem}"
             + ("" if tool.unambiguous else "   (nicht eindeutig erkannt)"))
    if not tool.unambiguous and tool.alternatives:
        z.append(f"              auch moeglich: {', '.join(tool.alternatives)}")
    if tool.remarks:
        z.append(f"Medium        {tool.remarks}")
    z.append(f"Archiviert    {datetime.datetime.now().strftime('%Y-%m-%d %H:%M')}")
    z.append("")

    for v in tool.volumes():
        name = v.dir or "Datentraeger"
        z.append(f"{name}"
                 + (f"  '{v.label}'" if v.label else "")
                 + f"   {v.used // 1024} KB belegt, {v.free // 1024} KB frei"
                 + f"  (von {v.total // 1024} KB)")
    z.append("")

    kopf = f"{'Seite':<6}" if mehrseitig else ""
    z.append(f"{kopf}{'Name':<34}{'Typ':<4}{'Groesse':>9}  {'Eigensch.':<10}{'Datum':<8}")
    z.append("-" * 70)
    for e in eintraege:
        seite = f"{e.side_dir:<6}" if mehrseitig else ""
        marke = "  [DEFEKT]" if e.damaged else ""
        z.append(f"{seite}{e.name:<34}{e.type:<4}{e.size:>9}  "
                 f"{e.attrs:<10}{e.date:<8}{marke}")
    z.append("-" * 70)
    z.append(f"{len(eintraege)} Dateien")
    z.append("")
    z.append(_legende(hat_udos, hat_cpm))
    z.append("")
    z.append("Der Unterordner `" + DATEI_ORDNER + "/` enthaelt dieselben Dateien als")
    z.append("gewoehnliche Dateien; das beiliegende .hfe-Abbild ist die verlustfreie")
    z.append("Fassung, aus der sich alles Uebrige wiederherstellen laesst.")
    return "\n".join(z) + "\n"


def create_archive(tool: "DiskTool", zip_path, text_mode: bool = False) -> Path:
    """Abbild, Dateien und Inhaltsverzeichnis in eine `.zip` packen.

    Das Abbild wird immer als **`.hfe`** abgelegt — auch wenn die Quelle ein `.img`
    oder `.dmk` ist: HFE trägt die Spurstruktur und damit auch das, was ein rohes
    Sektorabbild nicht darstellen kann (UDOS-Sektorkontrollblöcke).

    Die Diskette wird dabei **nicht** angefasst: der Export bindet nicht um, und
    ein Schreibschutz bleibt bestehen.

    Returns:
        Der geschriebene Archivpfad.
    """
    ziel = Path(zip_path)
    if ziel.suffix.lower() != ".zip":
        ziel = ziel.with_suffix(".zip")

    stamm = Path(tool.path).stem
    text = inhaltsverzeichnis(tool)

    with tempfile.TemporaryDirectory(prefix="k1520_archiv_") as tmp:
        tmpdir = Path(tmp)

        # 1) Abbild verlustfrei als .hfe — ohne die Bindung umzuhaengen.
        abbild = tmpdir / f"{stamm}.hfe"
        tool.export_image(abbild)

        # 2) Die Dateien wie bei einem gewoehnlichen Auszug (inkl. SideN/).
        dateien = tmpdir / DATEI_ORDNER
        dateien.mkdir()
        tool.extract_all(dateien, text=text_mode)

        # 3) Alles einpacken.
        ziel.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(ziel, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(abbild, abbild.name)
            z.writestr(f"{stamm}.txt", text)
            for p in sorted(dateien.rglob("*")):
                if p.is_file():
                    z.write(p, str(Path(DATEI_ORDNER) / p.relative_to(dateien)))
                elif p.is_dir() and not any(p.iterdir()):
                    # Eine leere Seite bleibt als leerer Ordner sichtbar — das ist
                    # die ehrliche Auskunft „diese Seite ist unbeschrieben".
                    z.writestr(str(Path(DATEI_ORDNER) / p.relative_to(dateien)) + "/", "")
    return ziel


def main(argv=None) -> int:                          # pragma: no cover
    argv = list(sys.argv[1:] if argv is None else argv)
    if len(argv) != 2:
        print("Aufruf: python3 -m app.disktool.archive <abbild> <archiv.zip>",
              file=sys.stderr)
        return 1

    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    try:
        with DiskTool.open(argv[0]) as d:            # schreibgeschuetzt — nur lesen
            ziel = create_archive(d, argv[1])
    except K1520DiskError as e:
        print(f"Fehler: {e}", file=sys.stderr)
        return 1
    print(f"archiviert: {ziel}")
    return 0


if __name__ == "__main__":                           # pragma: no cover
    sys.exit(main())

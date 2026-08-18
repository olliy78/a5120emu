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

  Es führt deshalb **jede ermittelbare Dateiangabe** auf — bei UDOS den ganzen
  Kopfsektor (`doc/udos_diskettenformat.md` §6): Satzlänge, Einsprungadresse,
  Speichersegment und die Speicheranforderung LOW/HIGH/STACK.  Damit ist die
  Diskette auch **von Hand** wiederherstellbar: Dateien zurückschreiben, die
  Angaben im Eigenschaften-Dialog einstellen, fertig.  Maschinell geht es
  einfacher — die Beiblätter `udos-dateiangaben.txt` bzw. `cpm-dateiangaben.txt`
  liegen im Ordner `dateien/` und werden beim Einfügen von selbst wieder gelesen.

Benutzbar aus der Oberfläche und als Skript::

    python3 -m app.disktool.archive disks/udos_boot_scp.hfe archiv.zip
"""

from __future__ import annotations

import datetime
import re
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:                                   # pragma: no cover
    from app.core_binding.k1520disk import DiskTool

#: Unterordner im Archiv, der die extrahierten Dateien aufnimmt.
DATEI_ORDNER = "dateien"

#: Name der Dateien im Archiv, wenn nichts Besseres bekannt ist.
NAMENLOS = "diskette"


def dateiname(bezeichnung: str) -> str:
    """Aus der Beschriftung einer Diskette einen Dateinamen machen.

    Eine **physische** Diskette hat keinen Pfad, aus dem sich ein Name ableiten
    liesse — ihr Name ist das, was auf dem Aufkleber steht.  Der darf alles
    enthalten (Leerzeichen, Schrägstriche, Umlaute); hier wird daraus etwas, das
    auf jedem Dateisystem und in jedem ZIP-Programm heil ankommt.  Leer heisst
    „kein Name" — der Aufrufer fällt dann auf den Pfad bzw. :data:`NAMENLOS`
    zurück.
    """
    name = re.sub(r'[\\/:*?"<>|\x00-\x1f]', " ", bezeichnung or "")
    name = re.sub(r"\s+", "_", name.strip())
    return name.strip("._")[:64]


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
    if hat_udos:
        zeilen += [
            "",
            "SPALTEN DER DATEIANGABEN (UDOS-Kopfsektor, doc/udos_diskettenformat.md §6)",
            "",
            "  Satz     Satzlaenge in Byte — die Zuteilungseinheit von UDOS.  Ein Satz",
            "           belegt Satzlaenge/128 aufeinanderfolgende Sektoren EINER Spur.",
            "           Mit der falschen Satzlaenge zurueckgeschrieben bootet eine",
            "           Systemdiskette nicht mehr (`ZDOS` hat 1024, `OS` hat 512).",
            "  Rest     Bytes im letzten Satz (Offset 22).  Zusammen mit der Satzanzahl",
            "           ergibt das die logische Laenge; das Speicherabbild einer",
            "           Programmdatei reicht darueber hinaus (s. Segment).",
            "  ENTRY    Einsprungadresse (hex), nur bei Typ P/P1 ausgewertet.",
            "  Segment  Anfang (hex) + Laenge des Speichersegments.  Die Laenge ist NICHT",
            "           die Dateigroesse: `OS` ist 5504 Byte lang, sein Abbild 5632.",
            "  LOW/HIGH/STACK   was der Lader zuteilen laesst (Offset 122/124/126, hex) —",
            "           genau das, was EXTRACT im laufenden System meldet.  Fehlt es,",
            "           weist UDOS die Datei mit MEMORY PROTECT VIOLATION ab.",
            "  Block    zweite Laengenangabe (Offset 17).  0 ist ein GUELTIGER Wert",
            "           (bei Satzlaenge 256/512); der falsche Wert legt einen neu",
            "           geschriebenen Nukleus lahm.",
            "  Zusatz   Offset 44…47 (hex), Bedeutung offen — unveraendert uebernehmen.",
            "  erstellt Erstellungsvermerk: ein Datum JJMMTT ODER ein Versionstext",
            "           (`V 4.3`).  `_` steht fuer ein Leerzeichen.",
        ]
    if hat_cpm:
        zeilen += [
            "",
            "SPALTEN DER DATEIANGABEN (CP/M)",
            "",
            "  Mehr als diese fuehrt CP/M 2.2 nicht: keine Ladeadresse, keine Satzlaenge,",
            "  kein Datum.  Der Nutzerbereich gehoert zur IDENTITAET der Datei — dieselbe",
            "  Datei kann in mehreren Bereichen liegen; im Namen steht er als Praefix.",
        ]
    return "\n".join(zeilen)


def _udos_angaben(eintraege, mehrseitig: bool) -> list:
    """Die Kopfsektorangaben aller Dateien als Tabelle (eine Zeile je Datei)."""
    kopf = f"{'Seite':<6}" if mehrseitig else ""
    kopfzeile = (f"{kopf}{'Name':<20}{'Typ':<4}{'Eig.':<6}{'Groesse':>8}{'Satz':>6}"
                 f"{'Rest':>6}  {'ENTRY':<6}{'Segment':<14}"
                 f"{'LOW':<6}{'HIGH':<6}{'STACK':<7}{'Block':>6}  {'Zusatz':<9}"
                 f"{'erstellt':<9}{'geaendert':<9}").rstrip()
    z = ["DATEIANGABEN IM EINZELNEN", "", kopfzeile, "-" * len(kopfzeile)]
    for e in eintraege:
        seite = f"{e.side_dir:<6}" if mehrseitig else ""
        # Leerzeichen im Erstellungsvermerk werden zu `_` — sonst zerfaellt die Spalte.
        erstellt = (e.created or "-").replace(" ", "_")
        z.append((
            f"{seite}{e.name:<20}{e.type:<4}{e.attrs or '-':<6}{e.size:>8}"
            f"{e.record_len:>6}{e.bytes_in_last:>6}  {e.entry:04X}  "
            f"{e.segment:04X}+{e.segment_len:<9}"
            f"{e.low_addr:04X}  {e.high_addr:04X}  {e.stack_size:04X}   "
            f"{e.block_len:>6}  {e.extra:08X} {erstellt:<9}{e.date or '-':<9}").rstrip())
    z.append("-" * len(kopfzeile))
    return z


def _cpm_angaben(eintraege) -> list:
    """Nutzerbereich und Attributbits — mehr fuehrt CP/M nicht."""
    def ja(an: bool) -> str:
        return "ja" if an else "-"

    kopfzeile = (f"{'Name':<20}{'Nutzer':>7}{'Groesse':>10}   "
                 f"{'R/O':<5}{'SYS':<5}{'ARC':<5}").rstrip()
    z = ["DATEIANGABEN IM EINZELNEN", "", kopfzeile, "-" * len(kopfzeile)]
    for e in eintraege:
        z.append((f"{e.name:<20}{e.user:>7}{e.size:>10}   "
                  f"{ja(e.read_only):<5}{ja(e.system):<5}{ja(e.archived):<5}").rstrip())
    z.append("-" * len(kopfzeile))
    return z


def inhaltsverzeichnis(tool: "DiskTool", quelle: str = "",
                       bezeichnung: str = "", herkunft: str = "") -> str:
    """Das Inhaltsverzeichnis als Text — Kopf, Tabelle, Legende.

    Args:
        quelle: Dateiname, unter dem die Diskette geführt wird (Vorgabe: der
            Pfad der offenen Diskette).
        bezeichnung: Die Beschriftung der Diskette, wie sie der Anwender angibt
            (der Text auf dem Aufkleber).  Sie ist bei einer **physischen**
            Diskette die einzige Auskunft darüber, WELCHE Diskette das war —
            deshalb steht sie im Kopf, nicht nur im Dateinamen.
        herkunft: Woher die Diskette kam, wenn es keine Datei gibt (z. B.
            „Echtes Laufwerk A am Greaseweazle").
    """
    eintraege = tool.list()
    hat_udos = any(e.type for e in eintraege)
    hat_cpm = any(e.user or e.attrs in ("RO", "SYS", "ARC") for e in eintraege) \
        or not hat_udos
    mehrseitig = tool.volume_count > 1

    z = []
    z.append("k1520DiskTool — Inhaltsverzeichnis einer K1520-Diskette")
    z.append("=" * 70)
    z.append("")
    if bezeichnung:
        z.append(f"Beschriftung  {bezeichnung}")
    datei = Path(quelle or tool.path).name
    if datei:
        z.append(f"Abbild        {datei}")
    if herkunft:
        z.append(f"Herkunft      {herkunft}")
    elif not datei:
        z.append("Herkunft      physisches Laufwerk — kein Abbild als Quelle")
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

    # Alles, was das Verzeichnis sonst noch ueber die Dateien weiss — die
    # Grundlage dafuer, die Diskette von Hand wiederherstellen zu koennen.
    if eintraege:
        z += (_udos_angaben(eintraege, mehrseitig) if hat_udos
              else _cpm_angaben(eintraege))
        z.append("")

    z.append(_legende(hat_udos, hat_cpm))
    z.append("")
    z.append("Der Unterordner `" + DATEI_ORDNER + "/` enthaelt dieselben Dateien als")
    z.append("gewoehnliche Dateien; das beiliegende .hfe-Abbild ist die verlustfreie")
    z.append("Fassung, aus der sich alles Uebrige wiederherstellen laesst.")
    z.append("")
    z.append("Zum Wiederherstellen der DATEIEN (ohne das Abbild): den Ordner `"
             + DATEI_ORDNER + "/`")
    z.append("auf eine leere Diskette einfuegen.  Die Angaben oben gehen dabei nicht")
    z.append("verloren — sie stehen im Ordner nochmals maschinenlesbar als Beiblatt")
    z.append("(`udos-dateiangaben.txt` bzw. `cpm-dateiangaben.txt`), und das Werkzeug")
    z.append("liest es beim Einfuegen von selbst.  Ohne Beiblatt stellt man sie ueber")
    z.append("„Eigenschaften…“ im Diskettenfenster ein.")
    return "\n".join(z) + "\n"


def create_archive(tool: "DiskTool", zip_path, text_mode: bool = False,
                   bezeichnung: str = "", herkunft: str = "") -> Path:
    """Abbild, Dateien und Inhaltsverzeichnis in eine `.zip` packen.

    Das Abbild wird immer als **`.hfe`** abgelegt — auch wenn die Quelle ein `.img`
    oder `.dmk` ist: HFE trägt die Spurstruktur und damit auch das, was ein rohes
    Sektorabbild nicht darstellen kann (UDOS-Sektorkontrollblöcke).

    Die Diskette wird dabei **nicht** angefasst: der Export bindet nicht um, und
    ein Schreibschutz bleibt bestehen.

    Args:
        bezeichnung: Beschriftung der Diskette.  Sie benennt die Dateien IM
            Archiv (`<name>.hfe` / `<name>.txt`) und steht im Kopf des
            Inhaltsverzeichnisses.  Ohne sie entscheidet der Dateiname der
            offenen Diskette — eine physische Diskette hat keinen, dort ist die
            Beschriftung die einzige Auskunft (`NAMENLOS` als letzter Rückfall).
        herkunft: Woher die Diskette kam, wenn es keine Quelldatei gibt.

    Returns:
        Der geschriebene Archivpfad.
    """
    ziel = Path(zip_path)
    if ziel.suffix.lower() != ".zip":
        ziel = ziel.with_suffix(".zip")

    stamm = dateiname(bezeichnung) or Path(tool.path).stem or NAMENLOS
    text = inhaltsverzeichnis(tool, bezeichnung=bezeichnung, herkunft=herkunft)

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
    if not 2 <= len(argv) <= 3:
        print("Aufruf: python3 -m app.disktool.archive <abbild> <archiv.zip>"
              " [beschriftung]", file=sys.stderr)
        return 1

    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    try:
        with DiskTool.open(argv[0]) as d:            # schreibgeschuetzt — nur lesen
            ziel = create_archive(d, argv[1],
                                  bezeichnung=argv[2] if len(argv) > 2 else "")
    except K1520DiskError as e:
        print(f"Fehler: {e}", file=sys.stderr)
        return 1
    print(f"archiviert: {ziel}")
    return 0


if __name__ == "__main__":                           # pragma: no cover
    sys.exit(main())

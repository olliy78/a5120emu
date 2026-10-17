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

* Das **maschinenlesbare Verzeichnis** (`diskarchive.yaml`) ist dasselbe für
  Programme: Dateiname, Verzeichnis, Größe und SHA-256 jeder Datei im Archiv.
  Es beantwortet die Frage, für die man sonst jedes Archiv auspacken müsste —
  *wie viele Fassungen von `XYZ.COM` gibt es in meiner Sammlung und auf welchen
  Disketten liegen sie?*  Absichtlich **ohne** Dateisystemangaben: die stehen im
  Inhaltsverzeichnis und in den Beiblättern, hier zählt die Inventur.

Benutzbar aus der Oberfläche und als Skript::

    python3 -m app.disktool.archive disks/udos_boot_scp.hfe archiv.zip
"""

from __future__ import annotations

import datetime
import hashlib
import re
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import TYPE_CHECKING

import yaml

if TYPE_CHECKING:                                   # pragma: no cover
    from app.core_binding.k1520disk import DiskTool

#: Unterordner im Archiv, der die extrahierten Dateien aufnimmt.
DATEI_ORDNER = "dateien"

#: Name der Dateien im Archiv, wenn nichts Besseres bekannt ist.
NAMENLOS = "diskette"

#: Name des maschinenlesbaren Verzeichnisses im Archiv.
INVENTAR_DATEI = "diskarchive.yaml"

#: Fassung des Datenformats von :data:`INVENTAR_DATEI` (Schluessel ``diskarchive``).
#:
#: Erhoeht wird sie nur, wenn ein Leser der alten Fassung das Archiv **falsch**
#: verstuende.  Neue Schluessel kommen ohne Erhoehung hinzu — wer liest, ueberliest
#: Unbekanntes; das ist die ganze Verabredung zur Erweiterbarkeit.
INVENTAR_VERSION = 1


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


def _legende(hat_udos: bool, hat_cpm: bool, hat_wega: bool = False) -> str:
    """Erklärt die Kürzel — das macht das Archiv selbsterklärend."""
    if hat_wega:
        return _legende_wega()
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


def _legende_wega() -> str:
    """Die Legende einer WEGA-Diskette — UNIX-Begriffe, keine UDOS-Kürzel."""
    return "\n".join([
        "LEGENDE",
        "",
        "  Modus        wie `ls -l`: erstes Zeichen die Dateiart, dann dreimal rwx",
        "               fuer Besitzer, Gruppe, Andere.  Dateiart:",
        "                 -  gewoehnliche Datei        d  Verzeichnis",
        "                 c  Zeichen-Geraet            b  Block-Geraet",
        "                 p  FIFO (Pipe)               m  gemultiplexte Geraete",
        "               s/S an Stelle von x: setuid (Besitzer) bzw. setgid (Gruppe);",
        "               t/T: sticky-Bit.  Gross = Ausfuehrungsrecht fehlt.",
        "  Oktal        derselbe Modus als Zahl; 0100755 = Datei, rwxr-xr-x.",
        "  Links        Zahl der Namen (Hard Links) auf diese Inode.",
        "  uid / gid    numerischer Besitzer und Gruppe (0 = Superuser).  Die Namen",
        "               stehen in /etc/passwd und /etc/group auf der Diskette selbst.",
        "  Groesse      Bytes; bei Geraetedateien statt dessen `major,minor`.",
        "  Datum        Zeit der letzten Aenderung des INHALTS (mtime, GMT).",
        "  Inode        Nummer der Inode (Inode 2 = Wurzel).",
        "  mtime/atime/ctime  Sekunden seit 1970 (GMT).  atime = letzter Zugriff,",
        "               ctime = letzte Aenderung der Inode (chmod/chown setzen sie).",
        "  [DEFEKT]     die Inode oder ihre Bloecke waren nicht lesbar.",
        "",
        "WAS DER ORDNER `dateien/` ZURUECKGIBT",
        "",
        "  Jede Datei hat ein `<datei>.fileinfo` (fs=wega) mit Modus, uid, gid und",
        "  mtime; ebenso jedes Verzeichnis (`<ordner>.fileinfo` neben dem Ordner).",
        "  Beim Einfuegen (`put <ordner>`) werden diese vier Angaben wiederhergestellt;",
        "  Links, Inode-Nummer, atime und ctime vergibt das Dateisystem neu.",
        "  `wega-dateiangaben.txt` listet ALLE Inodes, auch Geraetedateien, die als",
        "  Datei nicht existieren koennen — sie werden nicht ausgelesen.",
    ])


def _wega_zeit(t: int) -> str:
    if not t:
        return "-"
    return datetime.datetime.fromtimestamp(
        t, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M")


def _wega_tabelle(eintraege) -> list:
    """Die Dateien im Stil von `ls -l` (Modus, Links, uid, gid, Größe, Datum, Name)."""
    kopf = (f"{'Modus':<11}{'Links':>5} {'uid':>5} {'gid':>5} {'Groesse':>9}  "
            f"{'Datum (GMT)':<17} Name")
    z = [kopf, "-" * 70]
    for e in eintraege:
        groesse = e.unix_rdev_text or str(e.size)
        marke = "  [DEFEKT]" if e.damaged else ""
        z.append(f"{e.attrs or '?':<11}{e.unix_nlink:>5} {e.unix_uid:>5} {e.unix_gid:>5} "
                 f"{groesse:>9}  {e.date or '-':<17} {e.name}{marke}")
    z.append("-" * 70)
    return z


def _wega_angaben(eintraege) -> list:
    """Oktalmodus, Inode und alle drei Zeiten — das, was `ls -l` nicht zeigt."""
    kopf = f"{'Oktal':<9}{'Inode':>6}  {'mtime':<17}{'atime':<17}{'ctime':<17} Name"
    z = ["DATEIANGABEN IM EINZELNEN", "", kopf, "-" * len(kopf)]
    for e in eintraege:
        z.append(f"{'0%06o' % e.unix_mode:<9}{e.unix_inode:>6}  "
                 f"{_wega_zeit(e.unix_mtime):<17}{_wega_zeit(e.unix_atime):<17}"
                 f"{_wega_zeit(e.unix_ctime):<17} {e.name}")
    z.append("-" * len(kopf))
    return z


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


#: Beschriftete Zeilen der CopyQM-Metadaten im Inhaltsverzeichnis.
_CQM_ZEILEN = (("description", "Bezeichnung"), ("label", "Etikett"),
               ("interleave", "Interleave"), ("skew", "Skew"),
               ("cylinders", "Zylinder"), ("crc", "Pruefwert"))


def _cqm_angaben(tool: "DiskTool") -> dict:
    """Die Container-Angaben eines `.cqm` ({} bei jedem anderen Container)."""
    try:
        info = tool.container_info()
    except Exception:
        return {}
    return info if info.get("type") == "cqm" else {}


#: Beschriftung der Schluessel von ``provenance`` im Inhaltsverzeichnis, in der
#: Reihenfolge der Ausgabe.  Unbekannte Schluessel folgen danach unter ihrem Namen.
_HERKUNFT_ZEILEN = (
    ("source_url", "Quelle"), ("retrieved", "Abgerufen"), ("set_name", "Satz"),
    ("set_id", "Satz-Nr."), ("version", "Version"), ("file_name", "Dateiname"),
    ("file_type", "Dateityp"), ("file_format", "Dateiformat"),
    ("web_label", "Etikett (Web)"), ("source_sha256", "sha256 der Quelle"),
    ("container_description", "Container"), ("container_comment", "Container-Kommentar"),
    ("disks", "Disketten"), ("disk_index", "Diskette Nr."),
    ("note", "Bemerkung"),
)


def _herkunft_abschnitt(provenance: dict, beilagen) -> list:
    """Der Abschnitt „Herkunft/Beschreibung" (leer, wenn nichts angegeben ist).

    Allgemein gehalten: das Archiv weiss nichts von einer bestimmten Webseite, es
    druckt, was der Aufrufer unter ``provenance`` mitgibt.  Der lange Beschreibungstext
    steht NICHT hier, sondern in einem Beiblatt (``beilagen``); hier wird auf es
    verwiesen.  Listen (``hinweise``) werden Zeile fuer Zeile aufgefuehrt.
    """
    if not provenance and not beilagen:
        return []
    z = ["HERKUNFT / BESCHREIBUNG", "-" * 70]
    bekannt = {k for k, _ in _HERKUNFT_ZEILEN}
    reihe = [(k, n) for k, n in _HERKUNFT_ZEILEN if k in (provenance or {})]
    reihe += [(k, k) for k in (provenance or {}) if k not in bekannt and k != "hinweise"]
    for k, name in reihe:
        wert = provenance[k]
        if wert in ("", None, []):
            continue
        if isinstance(wert, (list, tuple)):
            wert = ", ".join(str(w) for w in wert)
        text = str(wert).strip().replace("\n", "\n" + " " * 15)
        z.append(f"{name:<14} {text}")
    hinweise = (provenance or {}).get("hinweise") or []
    if hinweise:
        z.append("Hinweise")
        z += [f"  - {h}" for h in hinweise]
    if beilagen:
        z.append("Beiblaetter    " + ", ".join(sorted(beilagen)))
        z.append("               (der Beschreibungstext steht im Beiblatt, nicht hier)")
    z.append("")
    return z


def inhaltsverzeichnis(tool: "DiskTool", quelle: str = "",
                       bezeichnung: str = "", herkunft: str = "",
                       provenance: dict = None, beilagen=(),
                       ohne_abbild: bool = False) -> str:
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
    # Die Art kommt aus dem Dateisystem, nicht aus der Typspalte: bei UDOS ist `type`
    # ein Dateityp (P1), bei WEGA eine Dateiart (-/d/c), bei CP/M leer — „hat einen
    # Typ => UDOS" machte aus jeder WEGA-Diskette eine UDOS-Diskette.
    # UNIX-Sicht (ls -l, unix:-Schluessel): WEGA und tar-Disketten (nur lesbar)
    hat_wega = tool.ist_wega or tool.ist_tar
    hat_udos = (not hat_wega) and (tool.ist_udos or any(e.type for e in eintraege))
    hat_cpm = (not hat_wega) and (
        any(e.user or e.attrs in ("RO", "SYS", "ARC") for e in eintraege)
        or not hat_udos)
    mehrseitig = tool.volume_count > 1

    z = []
    z.append("k1520DiskTool — Inhaltsverzeichnis einer K1520-Diskette")
    z.append("=" * 70)
    z.append("")
    if bezeichnung:
        z.append(f"Beschriftung  {bezeichnung}")
    # Ein Satz aus mehreren Disketten hat KEIN einzelnes Abbild: weder Dateiname
    # noch Container-Angaben der ersten Diskette duerfen fuer ihn stehen.
    datei = "" if ohne_abbild else Path(quelle or tool.path).name
    if datei:
        z.append(f"Abbild        {datei}")
    cqm = {} if ohne_abbild else _cqm_angaben(tool)
    if cqm:
        z.append("Container     CopyQM (.cqm, nur lesbar)"
                 + (" — kein Abbild im Archiv" if ohne_abbild
                    else " — im Archiv als .hfe abgelegt"))
        for schluessel, name in _CQM_ZEILEN:
            if cqm.get(schluessel):
                z.append(f"{name:<13} {cqm[schluessel]}")
    if herkunft:
        z.append(f"Herkunft      {herkunft}")
    elif not datei and not ohne_abbild:
        z.append("Herkunft      physisches Laufwerk — kein Abbild als Quelle")
    z.append(f"Format        {tool.format}")
    z.append(f"Dateisystem   {tool.filesystem}"
             + ("" if tool.unambiguous else "   (nicht eindeutig erkannt)"))
    if not tool.unambiguous and tool.alternatives:
        z.append(f"              auch moeglich: {', '.join(tool.alternatives)}")
    if tool.remarks:
        z.append(f"Medium        {tool.remarks}")
    if cqm and cqm.get("comment", "").strip():
        z.append("Kommentar     " + cqm["comment"].strip().replace("\n", "\n              "))
    z.append(f"Archiviert    {datetime.datetime.now().strftime('%Y-%m-%d %H:%M')}")
    z.append("")
    z += _herkunft_abschnitt(provenance, beilagen)

    for v in tool.volumes():
        name = v.dir or "Datentraeger"
        z.append(f"{name}"
                 + (f"  '{v.label}'" if v.label else "")
                 + f"   {v.used // 1024} KB belegt, {v.free // 1024} KB frei"
                 + f"  (von {v.total // 1024} KB)")
    z.append("")

    if hat_wega:
        z += _wega_tabelle(eintraege)
        z.append(f"{sum(1 for e in eintraege if e.type != 'd')} Eintraege, "
                 f"{sum(1 for e in eintraege if e.type == 'd')} davon Verzeichnisse")
        z.append("")
    else:
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
        z += (_wega_angaben(eintraege) if hat_wega
              else _udos_angaben(eintraege, mehrseitig) if hat_udos
              else _cpm_angaben(eintraege))
        z.append("")

    z.append(_legende(hat_udos, hat_cpm, hat_wega))
    z.append("")
    z.append("Der Unterordner `" + DATEI_ORDNER + "/` enthaelt dieselben Dateien als")
    if ohne_abbild:
        z.append("gewoehnliche Dateien; ein Abbild liegt nicht bei (die Dateien stammen aus")
        z.append("einem Satz mehrerer Disketten — die Abbilder sind die Einzelarchive).")
    else:
        z.append("gewoehnliche Dateien; das beiliegende .hfe-Abbild ist die verlustfreie")
        z.append("Fassung, aus der sich alles Uebrige wiederherstellen laesst.")
    z.append("")
    z.append("Zum Wiederherstellen der DATEIEN (ohne das Abbild): den Ordner `"
             + DATEI_ORDNER + "/`")
    z.append("auf eine leere Diskette einfuegen.  Die Angaben oben gehen dabei nicht")
    z.append("verloren — sie stehen im Ordner nochmals maschinenlesbar als Beiblatt")
    if hat_wega:
        z.append("(`<datei>.fileinfo` je Datei und Verzeichnis, `wega-dateiangaben.txt`),")
        z.append("und das Werkzeug liest die `.fileinfo` beim Einfuegen von selbst.  Ohne")
        z.append("sie stellt man Modus und Besitzer ueber „Eigenschaften…“ ein.")
    else:
        z.append("(`udos-dateiangaben.txt` bzw. `cpm-dateiangaben.txt`), und das Werkzeug")
        z.append("liest es beim Einfuegen von selbst.  Ohne Beiblatt stellt man sie ueber")
        z.append("„Eigenschaften…“ im Diskettenfenster ein.")
    return "\n".join(z) + "\n"


# ════════════════════════════════════════════════════════════════════════════
# Maschinenlesbares Verzeichnis (`diskarchive.yaml`)
# ════════════════════════════════════════════════════════════════════════════

#: Kopf der `diskarchive.yaml` — das Format erklaert sich selbst, wie das
#: Inhaltsverzeichnis auch.  Ein Archiv aus dieser Sammlung soll in zwanzig
#: Jahren auch ohne dieses Werkzeug auswertbar sein.
INVENTAR_KOPF = """\
# k1520DiskTool — maschinenlesbares Verzeichnis dieses Archivs.
#
# Zweck ist die INVENTUR einer Sammlung: welche Datei liegt auf welcher Diskette,
# und welche Fassungen davon gibt es (Vergleich ueber sha256) — beantwortbar,
# ohne die Archive auszupacken.  Angaben des Dateisystems stehen hier bewusst
# NICHT; die fuehren das Inhaltsverzeichnis (.txt) und die Beiblaetter.
#
#   diskarchive   Fassung dieses Datenformats.  Neue Schluessel koennen ohne
#                 Erhoehung hinzukommen — wer liest, ueberliest Unbekanntes.
#   transfer      'binary' = Byte fuer Byte ausgelesen, 'text' = beim Auszug
#                 umgesetzt (Zeilenenden, Textende).  Die Pruefsumme haengt daran.
#   label         Beschriftung der Diskette (der Aufkleber), kann leer sein.
#   source        Woher sie kam: Dateiname des Abbilds oder ein Klartext wie
#                 'Echtes Laufwerk A am Greaseweazle'.
#   image         das verlustfreie .hfe-Abbild; sein sha256 kennzeichnet die
#                 DISKETTE als Ganzes (zwei gleiche Abbilder = dieselbe Diskette).
#   catalogue     das menschenlesbare Inhaltsverzeichnis.
#   source_container  nur bei einer .cqm-Quelle: ihre Metadaten (type, description, label,
#                 comment, interleave, skew, cylinders, crc) und sha256 der Quelldatei.
#   provenance    nur wenn der Aufrufer Herkunftsangaben mitgab (z. B. Webseite, Satz,
#                 Version, Abrufdatum, sha256 der Quelle, Hinweise): freie Schluessel,
#                 die das Archiv nicht auswertet.
#   files_root    Ordner im Archiv, unter dem die Dateien liegen.
#   files         eine Zeile je Datei der Diskette:
#     path        Pfad IM ARCHIV (Eintrag der .zip), Trenner '/', stets relativ.
#     dir         Verzeichnis relativ zu files_root — leer, 'Side0'/'Side1' bei
#                 zweiseitigen UDOS-Disketten, und bei hierarchischen Datei-
#                 systemen (FAT, Unix) spaeter auch mehrstufig 'A/B'.
#     name        Dateiname ohne Verzeichnis.  Es gilt immer:
#                 path == files_root + '/' + (dir + '/' wenn dir) + name
#     size        Groesse in Byte, sha256 die Pruefsumme — beide der Datei IM
#                 ARCHIV, nicht der Laengenangabe des Verzeichnisses.
#     disk_name   nur wenn der Name auf der Diskette ein anderer ist als der
#                 Dateiname (CP/M-Nutzerbereich: '3:X.COM' -> '3_X.COM').
#     unix        nur bei WEGA-(UNIX-)Disketten: mode (oktal, samt Dateiart,
#                 z. B. "0100755"), uid, gid, mtime (Sekunden seit 1970, GMT).
#     damaged     nur wenn wahr: die Datei war nicht vollstaendig lesbar; ihr
#                 Inhalt ist unvollstaendig und zaehlt nicht als Fassung.
#   attachments   Beiblaetter des Auszugs — Angaben ueber die Dateien, aber
#                 selbst KEINE Dateien der Diskette.  Ein vom Aufrufer mitgegebenes
#                 Beiblatt (z. B. `beschreibung_*.txt`) liegt an der Wurzel der .zip,
#                 NICHT unter `dateien/` (sonst wuerde `put` es als Datei einfuegen).
"""


def _sha256(pfad: Path) -> str:
    """Prüfsumme einer Datei — stückweise, ein Abbild kann gross sein."""
    h = hashlib.sha256()
    with open(pfad, "rb") as f:
        for stueck in iter(lambda: f.read(1 << 20), b""):
            h.update(stueck)
    return h.hexdigest()


def _angabe(im_archiv: str, groesse: int, pruefsumme: str) -> dict:
    return {"path": im_archiv, "size": groesse, "sha256": pruefsumme}


def _angabe_datei(pfad: Path, im_archiv: str) -> dict:
    return _angabe(im_archiv, pfad.stat().st_size, _sha256(pfad))


def _erwartete_namen(eintraege) -> dict:
    """Pfad im Dateiordner → Verzeichniseintrag.

    Die Zuordnung muss der Namensgebung von ``extractAll`` folgen: der
    Nutzerbereich steht als Präfix im Namen (``3:X.COM``) und wird für das
    Wirtsdateisystem zu ``3_X.COM``; die UDOS-Verzeichnisdatei (Typ ``D``) ist
    Dateisystemstruktur und wird gar nicht erst ausgelesen.
    """
    erwartet = {}
    for e in eintraege:
        if e.type == "D":
            continue
        if e.ist_unix and e.type != "-":
            continue            # WEGA: Verzeichnisse/Geraete sind keine Dateien des Auszugs
        datei = e.name.replace(":", "_")
        if not e.ist_unix:
            datei = datei.replace("/", "_")      # wie DiskVolume::extractAll (CP/M-Namen)
        erwartet[f"{e.side_dir}/{datei}" if e.side_dir else datei] = e
    return erwartet


def inventar(eintraege, dateien_ordner: Path, *, abbild: Path = None, abbild_name: str = "",
             verzeichnis_name: str, verzeichnis_text: str,
             bezeichnung: str = "", quelle: str = "", herkunft: str = "",
             text_mode: bool = False, container=None,
             quelle_pfad: str = "", provenance: dict = None,
             beilagen: dict = None) -> dict:
    """Das maschinenlesbare Verzeichnis als Datenstruktur.

    Gerechnet wird über die Dateien, wie sie **im Archiv landen** — nicht über
    das Verzeichnis der Diskette.  Nur so passen Grösse und Prüfsumme zu dem,
    was ein Auswerter später in Händen hält (CP/M rundet auf 128-B-Sätze auf,
    ein Textauszug setzt Zeilenenden um).

    Args:
        eintraege: Das Verzeichnis der Diskette (``DiskTool.list()``) — es wird
            hier nicht erneut gelesen; an einer physischen Diskette kostet das
            bei UDOS jeden Kopfsektor.
        dateien_ordner: Der vorbereitete Ordner mit den ausgelesenen Dateien.
    """
    erwartet = _erwartete_namen(eintraege)
    dateien, beiblaetter = [], []
    for p in sorted(dateien_ordner.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(dateien_ordner).as_posix()
        angabe = _angabe_datei(p, f"{DATEI_ORDNER}/{rel}")
        e = erwartet.get(rel)
        if e is None:
            # Was nicht im Verzeichnis stand, ist ein Beiblatt des Auszugs.
            beiblaetter.append(angabe)
            continue
        ordner, _, name = rel.rpartition("/")
        eintrag = {"path": angabe["path"], "dir": ordner, "name": name,
                   "size": angabe["size"], "sha256": angabe["sha256"]}
        # Bei WEGA ist der Name ein Pfad ("bin/ls") und steht schon in `dir`/`name`.
        if e.name != name and e.name != rel:
            eintrag["disk_name"] = e.name
        if e.ist_unix:
            # Additiv (diskarchive bleibt 1): was ein Linux-Dateisystem nicht traegt.
            eintrag["unix"] = {"mode": f"0{e.unix_mode:06o}", "uid": e.unix_uid,
                               "gid": e.unix_gid, "mtime": e.unix_mtime}
        if e.damaged:
            eintrag["damaged"] = True
        dateien.append(eintrag)

    daten = {
        "diskarchive": INVENTAR_VERSION,
        "generator": _erzeuger(),
        "created": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "transfer": "text" if text_mode else "binary",
        "label": bezeichnung,
        "source": Path(quelle).name if quelle else herkunft,
        "catalogue": _angabe(verzeichnis_name,
                             len(verzeichnis_text.encode("utf-8")),
                             hashlib.sha256(
                                 verzeichnis_text.encode("utf-8")).hexdigest()),
        "files_root": DATEI_ORDNER,
        "files": dateien,
        "attachments": beiblaetter,
    }
    if abbild is not None:
        # Reihenfolge der Schluessel: image gehoert hinter source (wie bisher).
        neu = {}
        for k, v in daten.items():
            neu[k] = v
            if k == "source":
                neu["image"] = _angabe_datei(abbild, abbild_name)
        daten = neu
    for pfad, inhalt in sorted((beilagen or {}).items()):
        b = inhalt.encode("utf-8") if isinstance(inhalt, str) else bytes(inhalt)
        daten["attachments"].append(_angabe(pfad, len(b), hashlib.sha256(b).hexdigest()))
    if provenance:
        daten["provenance"] = dict(provenance)
    if container:
        # Additiv (diskarchive bleibt 1): das .cqm selbst steht nicht im Archiv (dort
        # liegt die verlustfreie .hfe-Fassung), wohl aber seine Herkunft: Metadaten
        # samt Kommentar und die Pruefsumme der Quelldatei.
        daten["source_container"] = dict(container)
        if quelle_pfad and Path(quelle_pfad).is_file():
            daten["source_container"]["sha256"] = _sha256(Path(quelle_pfad))
    return daten


def _erzeuger() -> str:
    """Wer das Archiv geschrieben hat — bei einem Formatstreit die erste Frage."""
    try:
        from app.core_binding.k1520disk import version
        return f"k1520DiskTool ({version()})"
    except Exception:                                # pragma: no cover
        return "k1520DiskTool"


def inventar_yaml(daten: dict) -> str:
    """:func:`inventar` als YAML-Text — mit dem erklärenden Kopf davor."""
    return INVENTAR_KOPF + yaml.safe_dump(
        daten, sort_keys=False, allow_unicode=True, default_flow_style=False)


def create_archive(tool: "DiskTool", zip_path, text_mode: bool = False,
                   bezeichnung: str = "", herkunft: str = "",
                   provenance: dict = None, beilagen: dict = None,
                   ohne_abbild: bool = False) -> Path:
    """Abbild, Dateien und beide Inhaltsverzeichnisse in eine `.zip` packen.

    Zwei Verzeichnisse, weil zwei Leser: `<name>.txt` fuer den Menschen (mit
    Legende und allen Dateiangaben), :data:`INVENTAR_DATEI` fuer die Maschine
    (Pfad, Groesse, sha256 — die Inventur einer ganzen Sammlung).

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
        provenance: Herkunftsangaben als Dict (freie Schluessel; Webseite, Satz,
            Version, Abrufdatum, Pruefsumme der Quelle, ``hinweise``: Liste von
            Zeilen).  Landet als Abschnitt `provenance:` in der `diskarchive.yaml`
            (additiv, ``diskarchive`` bleibt 1) und als Abschnitt
            „HERKUNFT / BESCHREIBUNG" im Inhaltsverzeichnis.
        beilagen: ``{Pfad im Archiv: Text|Bytes}`` — zusaetzliche Beiblaetter an der
            Wurzel der `.zip`, in `attachments` verzeichnet.
        ohne_abbild: kein `.hfe` ins Archiv (und kein `image:`) — fuer ein Archiv
            ueber MEHRERE Disketten (tar-Satz), wo es ein einzelnes Abbild nicht gibt.

    Returns:
        Der geschriebene Archivpfad.
    """
    ziel = Path(zip_path)
    if ziel.suffix.lower() != ".zip":
        ziel = ziel.with_suffix(".zip")

    stamm = dateiname(bezeichnung) or Path(tool.path).stem or NAMENLOS
    # Eine Regel fuer alle Aufrufer (CLI, Dialog, Sammlungsskript): archiv_meta.py.
    from app.disktool.archiv_meta import pruefe_beilagen, pruefe_schluessel
    for k in provenance or ():
        pruefe_schluessel(k)
    pruefe_beilagen(beilagen, stamm=stamm, unterordner=True)
    text = inhaltsverzeichnis(tool, bezeichnung=bezeichnung, herkunft=herkunft,
                              provenance=provenance, beilagen=tuple(beilagen or ()),
                              ohne_abbild=ohne_abbild)
    eintraege = tool.list()

    with tempfile.TemporaryDirectory(prefix="k1520_archiv_") as tmp:
        tmpdir = Path(tmp)

        # 1) Abbild verlustfrei als .hfe — ohne die Bindung umzuhaengen.
        abbild = tmpdir / f"{stamm}.hfe"
        if not ohne_abbild:
            tool.export_image(abbild)

        # 2) Die Dateien wie bei einem gewoehnlichen Auszug (inkl. SideN/).
        dateien = tmpdir / DATEI_ORDNER
        dateien.mkdir()
        tool.extract_all(dateien, text=text_mode)

        # 3) Das maschinenlesbare Verzeichnis — es rechnet ueber die Dateien,
        #    wie sie gleich in die .zip gehen, also nach dem Auszug.
        daten = inventar(eintraege, dateien,
                         abbild=None if ohne_abbild else abbild,
                         abbild_name=abbild.name,
                         verzeichnis_name=f"{stamm}.txt", verzeichnis_text=text,
                         bezeichnung=bezeichnung,
                         quelle="" if ohne_abbild else tool.path,
                         herkunft=herkunft, text_mode=text_mode,
                         container={} if ohne_abbild else _cqm_angaben(tool),
                         quelle_pfad="" if ohne_abbild else tool.path,
                         provenance=provenance, beilagen=beilagen)

        # 4) Alles einpacken.
        ziel.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(ziel, "w", zipfile.ZIP_DEFLATED) as z:
            if not ohne_abbild:
                z.write(abbild, abbild.name)
            for pfad, inhalt in sorted((beilagen or {}).items()):
                z.writestr(pfad, inhalt)
            z.writestr(f"{stamm}.txt", text)
            z.writestr(INVENTAR_DATEI, inventar_yaml(daten))
            for p in sorted(dateien.rglob("*")):
                if p.is_file():
                    z.write(p, str(Path(DATEI_ORDNER) / p.relative_to(dateien)))
                elif p.is_dir() and not any(p.iterdir()):
                    # Eine leere Seite bleibt als leerer Ordner sichtbar — das ist
                    # die ehrliche Auskunft „diese Seite ist unbeschrieben".
                    z.writestr(str(Path(DATEI_ORDNER) / p.relative_to(dateien)) + "/", "")
    return ziel


def main(argv=None) -> int:
    import argparse
    from app.disktool import archiv_meta

    p = argparse.ArgumentParser(
        prog="python3 -m app.disktool.archive",
        description="Eine Diskette (Abbild) als .zip archivieren: Abbild als .hfe, alle "
                    "Dateien, lesbares und maschinenlesbares Inhaltsverzeichnis.")
    p.add_argument("abbild")
    p.add_argument("archiv", metavar="archiv.zip")
    p.add_argument("beschriftung", nargs="?", default="",
                   help="Beschriftung der Diskette (wie --bezeichnung)")
    p.add_argument("--bezeichnung", metavar="TEXT", default="",
                   help="Beschriftung der Diskette (der Aufkleber); benennt die "
                        "Dateien im Archiv")
    p.add_argument("--ohne-abbild", action="store_true",
                   help="kein .hfe ins Archiv (nur Dateien und Verzeichnisse)")
    p.add_argument("--text", action="store_true", help="Zeilenenden umsetzen")
    p.add_argument("--force", action="store_true", help="vorhandenes Archiv ersetzen")
    archiv_meta.add_argumente(p.add_argument_group("Herkunft und Beschreibung"))
    o = p.parse_args(sys.argv[1:] if argv is None else argv)

    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    bezeichnung = o.bezeichnung or o.beschriftung
    ziel = Path(o.archiv)
    if ziel.suffix.lower() != ".zip":
        ziel = ziel.with_suffix(".zip")
    try:
        # ERST die Eingaben, dann die Diskette: ein Tippfehler soll sofort auffallen.
        stamm = dateiname(bezeichnung) or Path(o.abbild).stem or NAMENLOS
        prov, beil = archiv_meta.aus_optionen(o, stamm=stamm)
        archiv_meta.pruefe_beilagen(beil, stamm=stamm)
    except archiv_meta.ArchivMetaFehler as e:
        print(f"Fehler: {e}", file=sys.stderr)
        return 2
    if ziel.exists() and not o.force:
        print(f"Fehler: {ziel} gibt es schon - mit --force ersetzen.", file=sys.stderr)
        return 2

    try:
        with DiskTool.open(o.abbild) as d:           # schreibgeschuetzt - nur lesen
            ziel = create_archive(d, ziel, text_mode=o.text, bezeichnung=bezeichnung,
                                  provenance=prov or None, beilagen=beil or None,
                                  ohne_abbild=o.ohne_abbild)
    except archiv_meta.ArchivMetaFehler as e:
        print(f"Fehler: {e}", file=sys.stderr)
        return 2
    except K1520DiskError as e:
        print(f"Fehler: {e}", file=sys.stderr)
        return 1
    print(f"archiviert: {ziel}")
    return 0


if __name__ == "__main__":                           # pragma: no cover
    sys.exit(main())

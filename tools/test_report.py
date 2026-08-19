#!/usr/bin/env python3
"""Testprotokoll: aus ctest-/pytest-JUnit-XML wird EINE eigenständige HTML-Seite.

Motivation: die CI meldet bisher nur „grün" oder „rot" plus ein Textprotokoll
im Fehlerfall (`LastTest.log`, gigantisch).  Was fehlt, ist der Blick auf den
LAUF als Ganzes — welche Ebene wie lange braucht, welche Fälle wackeln, was
genau ausgegeben hat, was fehlschlug.  Genau das erzeugt dieses Script.

    ctest --test-dir build --output-junit build/Testing/junit.xml ...
    python3 tools/test_report.py build/Testing/junit.xml -o testprotokoll.html

Mehrere Läufe kommen nebeneinander in EIN Protokoll (Linux + Windows, oder
schnelle Runde + Formatläufe); der Name vor dem Doppelpunkt ist die Spalte:

    python3 tools/test_report.py Linux:linux.xml Windows:windows.xml -o p.html

Gegliedert wird nach der Taxonomie des Projekts (tests/README.md): das
ctest-LABEL ist die Ebene, der GoogleTest-Suitenname die Gruppe darin.  ctest
schreibt die Label als `<property name="cmake_labels">` mit, pytest nicht — dort
greift der Rückfall über den Dateinamen.

Die Seite ist bewusst OHNE externe Mittel (kein CDN, kein Font, kein Bild):
ein GitHub-Actions-Artefakt wird lokal aus dem ZIP heraus geöffnet, da lädt
nichts nach.  Ausgabe eines Fehlschlags kommt vollständig hinein, Ausgabe eines
bestandenen Falls NICHT — sonst wären es 850 KB XML statt 60 KB Seite.
"""

from __future__ import annotations

import argparse
import html
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

# Die Ebenen aus tests/README.md, in der Reihenfolge „schnell und eng" →
# „langsam und breit".  Alles andere landet unter „sonstige".
EBENEN = ["unit", "debugtools", "integration", "cli", "system", "python"]

# Label, die keine Ebene bezeichnen, sondern eine Eigenschaft quer dazu.
QUERLABEL = {"fast", "slow", "format_integration", "format_matrix"}


@dataclass
class Fall:
    """Ein Testfall aus der XML — bei ctest also EIN ctest-Fall."""

    name: str
    ebene: str
    gruppe: str
    dauer: float
    zustand: str  # "bestanden" | "fehlgeschlagen" | "uebersprungen"
    meldung: str = ""
    ausgabe: str = ""
    querlabel: list[str] = field(default_factory=list)


@dataclass
class Lauf:
    """Alle Fälle einer XML-Datei plus die Kopfdaten des Laufs."""

    name: str
    quelle: str
    faelle: list[Fall] = field(default_factory=list)
    zeitstempel: str = ""

    @property
    def dauer(self) -> float:
        return sum(f.dauer for f in self.faelle)

    def zahl(self, zustand: str) -> int:
        return sum(1 for f in self.faelle if f.zustand == zustand)

    @property
    def gruen(self) -> bool:
        return self.zahl("fehlgeschlagen") == 0 and bool(self.faelle)


# ─── Einlesen ────────────────────────────────────────────────────────────────


def _ebene_und_querlabel(labels: list[str], quelle: str) -> tuple[str, list[str]]:
    """Ebene = das eine Label, das eine Ebene benennt; Rest ist quer dazu.

    Ohne Label (pytest schreibt keine) wird der Dateiname befragt — ein
    `pytest --junit-xml` aus `tests/python/` ist die Ebene `python`.
    """
    quer = sorted(l for l in labels if l in QUERLABEL)
    for l in labels:
        if l in EBENEN:
            return l, quer
    stem = Path(quelle).stem.lower()
    for e in EBENEN:
        if e in stem:
            return e, quer
    return "sonstige", quer


def _gruppe(name: str, classname: str) -> str:
    """GoogleTest: `Suite.Fall` → `Suite`.  ctest-Eigenbau: der Präfix bis `_`."""
    basis = classname or name
    if "." in basis:
        return basis.split(".", 1)[0]
    # cli_dbg_step_… / py_c_api / format_matrix_… → sprechender Präfix
    for prefix in ("cli_", "py_", "format_matrix_", "bootdisk_"):
        if basis.startswith(prefix):
            return prefix.rstrip("_")
    return basis


def _text(elem: ET.Element | None) -> str:
    return (elem.text or "").strip() if elem is not None else ""


def lies_junit(pfad: Path, name: str) -> Lauf:
    """JUnit-XML einlesen — ctest (bare <testsuite>) und pytest (<testsuites>)."""
    wurzel = ET.parse(pfad).getroot()
    suiten = [wurzel] if wurzel.tag == "testsuite" else list(wurzel.iter("testsuite"))

    lauf = Lauf(name=name, quelle=str(pfad))
    for suite in suiten:
        lauf.zeitstempel = lauf.zeitstempel or suite.get("timestamp", "")
        for tc in suite.findall("testcase"):
            labels: list[str] = []
            for prop in tc.iter("property"):
                if prop.get("name") == "cmake_labels":
                    labels = [l for l in (prop.get("value") or "").split(";") if l]

            fehler = tc.find("failure")
            if fehler is None:
                fehler = tc.find("error")
            uebersprungen = tc.find("skipped")

            if fehler is not None:
                zustand = "fehlgeschlagen"
            elif uebersprungen is not None:
                zustand = "uebersprungen"
            else:
                zustand = "bestanden"

            # Ausgabe nur bei Fehlschlag mitführen (Größe!).  ctest legt sie in
            # <system-out>, pytest in <system-err> bzw. den Text des <failure>.
            ausgabe = ""
            if zustand == "fehlgeschlagen":
                teile = [
                    _text(fehler),
                    _text(tc.find("system-out")),
                    _text(tc.find("system-err")),
                ]
                ausgabe = "\n".join(t for t in teile if t)

            meldung = ""
            if fehler is not None:
                meldung = fehler.get("message", "") or ""
            elif uebersprungen is not None:
                meldung = uebersprungen.get("message", "") or ""

            ebene, quer = _ebene_und_querlabel(labels, str(pfad))
            lauf.faelle.append(
                Fall(
                    name=tc.get("name", "?"),
                    ebene=ebene,
                    gruppe=_gruppe(tc.get("name", "?"), tc.get("classname", "")),
                    dauer=float(tc.get("time", "0") or 0),
                    zustand=zustand,
                    meldung=meldung,
                    ausgabe=ausgabe,
                    querlabel=quer,
                )
            )
    return lauf


# ─── Darstellung ─────────────────────────────────────────────────────────────

CSS = """
:root{
  --bg:#fbfbfa; --flaeche:#fff; --rand:#e4e2dd; --text:#1f1e1c; --leise:#6b6862;
  --gruen:#1a7f4b; --gruen-bg:#e8f5ee; --rot:#b3261e; --rot-bg:#fdeceb;
  --gelb:#8a6100; --gelb-bg:#fdf3e0; --akzent:#3b5bdb;
}
@media (prefers-color-scheme: dark){ :root:not([data-theme="light"]){
  --bg:#17181a; --flaeche:#1f2124; --rand:#33363b; --text:#e8e6e3; --leise:#9a9791;
  --gruen:#5fd39a; --gruen-bg:#14301f; --rot:#ff9a90; --rot-bg:#3a1a17;
  --gelb:#e8c07a; --gelb-bg:#332614; --akzent:#8aa3ff;
}}
:root[data-theme="dark"]{
  --bg:#17181a; --flaeche:#1f2124; --rand:#33363b; --text:#e8e6e3; --leise:#9a9791;
  --gruen:#5fd39a; --gruen-bg:#14301f; --rot:#ff9a90; --rot-bg:#3a1a17;
  --gelb:#e8c07a; --gelb-bg:#332614; --akzent:#8aa3ff;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);
  font:15px/1.55 ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;}
.huelle{max-width:1080px;margin:0 auto;padding:32px 20px 72px}
h1{font-size:26px;margin:0 0 4px;letter-spacing:-.01em}
h2{font-size:18px;margin:36px 0 12px;letter-spacing:-.01em}
.kopfzeile{color:var(--leise);font-size:13.5px;margin:0 0 24px}
.marke{display:inline-block;padding:2px 10px;border-radius:999px;font-size:12.5px;
  font-weight:600;vertical-align:2px}
.marke.gut{background:var(--gruen-bg);color:var(--gruen)}
.marke.schlecht{background:var(--rot-bg);color:var(--rot)}
.marke.mau{background:var(--gelb-bg);color:var(--gelb)}
.karten{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(132px,1fr))}
.karte{background:var(--flaeche);border:1px solid var(--rand);border-radius:10px;padding:14px 16px}
.karte .zahl{font-size:26px;font-weight:650;line-height:1.15;font-variant-numeric:tabular-nums}
.karte .was{font-size:12.5px;color:var(--leise);margin-top:2px}
.karte.gut .zahl{color:var(--gruen)} .karte.schlecht .zahl{color:var(--rot)}
.karte.mau .zahl{color:var(--gelb)}
.tabellenrahmen{overflow-x:auto;background:var(--flaeche);
  border:1px solid var(--rand);border-radius:10px}
table{border-collapse:collapse;width:100%;font-size:14px}
th,td{text-align:left;padding:9px 14px;border-bottom:1px solid var(--rand);white-space:nowrap}
th{font-size:12px;text-transform:uppercase;letter-spacing:.04em;color:var(--leise);font-weight:600}
tr:last-child td{border-bottom:none}
td.zahl,th.zahl{text-align:right;font-variant-numeric:tabular-nums}
.balken{height:6px;border-radius:3px;background:var(--akzent);opacity:.75;min-width:2px}
details{background:var(--flaeche);border:1px solid var(--rand);border-radius:10px;
  margin-bottom:10px;overflow:hidden}
details>summary{cursor:pointer;padding:11px 16px;font-weight:550;list-style:none;
  display:flex;gap:10px;align-items:center}
details>summary::-webkit-details-marker{display:none}
details>summary::before{content:"▸";color:var(--leise);font-size:12px}
details[open]>summary::before{content:"▾"}
details>summary:hover{background:color-mix(in srgb,var(--akzent) 7%,transparent)}
.inhalt{border-top:1px solid var(--rand)}
.fehler{border-color:var(--rot)}
.fehler>summary{background:var(--rot-bg);color:var(--rot)}
pre{margin:0;padding:14px 16px;overflow-x:auto;font-size:12.5px;line-height:1.5;
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  background:color-mix(in srgb,var(--text) 4%,transparent)}
.leise{color:var(--leise)} .mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
.rechts{margin-left:auto;font-weight:400;color:var(--leise);font-size:13px}
footer{margin-top:48px;color:var(--leise);font-size:12.5px;
  border-top:1px solid var(--rand);padding-top:16px}
"""


def _dauer(s: float) -> str:
    if s < 1:
        return f"{s*1000:.0f} ms"
    if s < 60:
        return f"{s:.2f} s"
    return f"{int(s//60)} min {s%60:.0f} s"


def _e(s: str) -> str:
    return html.escape(s, quote=True)


def _karte(zahl, was: str, art: str = "") -> str:
    return (f'<div class="karte {art}"><div class="zahl">{zahl}</div>'
            f'<div class="was">{_e(was)}</div></div>')


def _lauf_abschnitt(lauf: Lauf, mehrere: bool) -> str:
    fehl = [f for f in lauf.faelle if f.zustand == "fehlgeschlagen"]
    uebe = [f for f in lauf.faelle if f.zustand == "uebersprungen"]
    teile: list[str] = []

    if mehrere:
        marke = ('<span class="marke gut">bestanden</span>' if lauf.gruen
                 else '<span class="marke schlecht">fehlgeschlagen</span>')
        teile.append(f"<h2>{_e(lauf.name)} {marke}</h2>")

    teile.append('<div class="karten">')
    teile.append(_karte(len(lauf.faelle), "Fälle"))
    teile.append(_karte(lauf.zahl("bestanden"), "bestanden", "gut"))
    teile.append(_karte(len(fehl), "fehlgeschlagen", "schlecht" if fehl else ""))
    if uebe:
        teile.append(_karte(len(uebe), "übersprungen", "mau"))
    teile.append(_karte(_dauer(lauf.dauer), "Rechenzeit"))
    teile.append("</div>")

    # ─ Fehlschläge zuerst und aufgeklappt: darum liest man ein Protokoll ─
    if fehl:
        teile.append("<h2>Fehlgeschlagen</h2>")
        for f in fehl:
            kopf = (f'<summary>{_e(f.name)}'
                    f'<span class="rechts">{_e(f.ebene)} · {_dauer(f.dauer)}</span></summary>')
            rumpf = ""
            if f.meldung:
                rumpf += f'<div class="inhalt"><pre>{_e(f.meldung)}</pre></div>'
            if f.ausgabe:
                rumpf += f'<div class="inhalt"><pre>{_e(f.ausgabe)}</pre></div>'
            if not rumpf:
                rumpf = '<div class="inhalt"><pre class="leise">(keine Ausgabe)</pre></div>'
            teile.append(f'<details class="fehler" open>{kopf}{rumpf}</details>')

    # ─ Ebenen: die Gliederung des Projekts (tests/README.md) ─
    ebenen = [e for e in EBENEN + ["sonstige"]
              if any(f.ebene == e for f in lauf.faelle)]
    hoechste = max((sum(f.dauer for f in lauf.faelle if f.ebene == e) for e in ebenen),
                   default=1) or 1

    teile.append("<h2>Ebenen</h2>")
    teile.append('<div class="tabellenrahmen"><table><thead><tr>'
                 '<th>Ebene</th><th class="zahl">Fälle</th><th class="zahl">Fehler</th>'
                 '<th class="zahl">Zeit</th><th>Anteil</th></tr></thead><tbody>')
    for e in ebenen:
        drin = [f for f in lauf.faelle if f.ebene == e]
        zeit = sum(f.dauer for f in drin)
        nfehl = sum(1 for f in drin if f.zustand == "fehlgeschlagen")
        breite = max(2, round(100 * zeit / hoechste))
        fehlzelle = (f'<td class="zahl" style="color:var(--rot);font-weight:600">{nfehl}</td>'
                     if nfehl else '<td class="zahl leise">0</td>')
        teile.append(
            f'<tr><td class="mono">{_e(e)}</td><td class="zahl">{len(drin)}</td>'
            f'{fehlzelle}<td class="zahl">{_dauer(zeit)}</td>'
            f'<td style="width:38%"><div class="balken" style="width:{breite}%"></div></td></tr>')
    teile.append("</tbody></table></div>")

    # ─ Alle Fälle, nach Ebene → Gruppe geschachtelt ─
    teile.append("<h2>Alle Fälle</h2>")
    for e in ebenen:
        drin = [f for f in lauf.faelle if f.ebene == e]
        gruppen = sorted({f.gruppe for f in drin})
        nfehl = sum(1 for f in drin if f.zustand == "fehlgeschlagen")
        stand = (f'<span style="color:var(--rot)">{nfehl} Fehler</span>' if nfehl
                 else '<span class="leise">alle bestanden</span>')
        innen = []
        for g in gruppen:
            gf = sorted((f for f in drin if f.gruppe == g), key=lambda f: -f.dauer)
            gfehl = sum(1 for f in gf if f.zustand == "fehlgeschlagen")
            zeilen = []
            for f in gf:
                zeichen = {"bestanden": "✓", "fehlgeschlagen": "✗",
                           "uebersprungen": "–"}[f.zustand]
                farbe = {"bestanden": "var(--gruen)", "fehlgeschlagen": "var(--rot)",
                         "uebersprungen": "var(--gelb)"}[f.zustand]
                kurz = f.name.split(".", 1)[1] if "." in f.name else f.name
                zeilen.append(
                    f'<tr><td style="color:{farbe};width:1%">{zeichen}</td>'
                    f'<td class="mono">{_e(kurz)}</td>'
                    f'<td class="zahl leise">{_dauer(f.dauer)}</td></tr>')
            marke = (f'<span class="rechts" style="color:var(--rot)">{gfehl} Fehler</span>'
                     if gfehl else f'<span class="rechts">{len(gf)}</span>')
            innen.append(
                f'<details{" open" if gfehl else ""}><summary>{_e(g)}{marke}</summary>'
                f'<div class="inhalt tabellenrahmen" style="border:0;border-radius:0">'
                f'<table><tbody>{"".join(zeilen)}</tbody></table></div></details>')
        teile.append(
            f'<details{" open" if nfehl else ""}><summary>{_e(e)}'
            f'<span class="rechts">{len(drin)} Fälle · {stand}</span></summary>'
            f'<div class="inhalt" style="padding:10px 12px">{"".join(innen)}</div></details>')

    # ─ Die langsamsten: wo die Wanduhr hingeht ─
    langsam = sorted(lauf.faelle, key=lambda f: -f.dauer)[:15]
    if langsam:
        teile.append("<h2>Die 15 langsamsten</h2>")
        teile.append('<div class="tabellenrahmen"><table><thead><tr><th>Fall</th>'
                     '<th>Ebene</th><th class="zahl">Zeit</th></tr></thead><tbody>')
        for f in langsam:
            teile.append(f'<tr><td class="mono">{_e(f.name)}</td>'
                         f'<td class="leise">{_e(f.ebene)}</td>'
                         f'<td class="zahl">{_dauer(f.dauer)}</td></tr>')
        teile.append("</tbody></table></div>")

    return "\n".join(teile)


def baue_html(laeufe: list[Lauf], titel: str) -> str:
    gesamt = sum(len(l.faelle) for l in laeufe)
    fehler = sum(l.zahl("fehlgeschlagen") for l in laeufe)
    marke = ('<span class="marke gut">bestanden</span>' if fehler == 0
             else f'<span class="marke schlecht">{fehler} fehlgeschlagen</span>')
    jetzt = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    quellen = " · ".join(f"{_e(l.name)}: {_e(Path(l.quelle).name)}" for l in laeufe)

    rumpf = "\n".join(_lauf_abschnitt(l, len(laeufe) > 1) for l in laeufe)
    return f"""<title>{_e(titel)}</title>
<style>{CSS}</style>
<div class="huelle">
<h1>{_e(titel)} {marke}</h1>
<p class="kopfzeile">{gesamt} Fälle aus {len(laeufe)} Lauf/Läufen · erzeugt {jetzt}<br>
<span class="mono">{quellen}</span></p>
{rumpf}
<footer>Erzeugt von <span class="mono">tools/test_report.py</span> aus
JUnit-XML (<span class="mono">ctest --output-junit</span>).
Ebenen und Label: siehe <span class="mono">tests/README.md</span>.</footer>
</div>
"""


def baue_markdown(laeufe: list[Lauf]) -> str:
    """Kurzfassung für die GitHub-Zusammenfassung (`$GITHUB_STEP_SUMMARY`)."""
    zeilen = ["| Lauf | Fälle | bestanden | fehlgeschlagen | übersprungen | Zeit |",
              "|---|--:|--:|--:|--:|--:|"]
    for l in laeufe:
        zeichen = "✅" if l.gruen else "❌"
        zeilen.append(
            f"| {zeichen} {l.name} | {len(l.faelle)} | {l.zahl('bestanden')} | "
            f"{l.zahl('fehlgeschlagen')} | {l.zahl('uebersprungen')} | {_dauer(l.dauer)} |")

    fehl = [(l, f) for l in laeufe for f in l.faelle if f.zustand == "fehlgeschlagen"]
    if fehl:
        zeilen += ["", "**Fehlgeschlagen:**"]
        zeilen += [f"- `{f.name}` ({l.name}, Ebene {f.ebene})" for l, f in fehl[:25]]
        if len(fehl) > 25:
            zeilen.append(f"- … und {len(fehl)-25} weitere")
    return "\n".join(zeilen) + "\n"


# ─── Aufruf ──────────────────────────────────────────────────────────────────


def zerlege_eintrag(eintrag: str) -> tuple[str, Path]:
    """`"Windows:lauf.xml"` → `("Windows", Path("lauf.xml"))`.

    Ein einzelner Buchstabe vor dem Doppelpunkt ist KEIN Name, sondern ein
    Windows-Laufwerksbuchstabe (`C:\\lauf.xml`) — sonst hieße der Lauf „C" und
    der Pfad wäre abgeschnitten.
    """
    name, sep, rest = eintrag.partition(":")
    if sep and len(name) > 1:
        return name, Path(rest)
    return Path(eintrag).stem, Path(eintrag)


def main(argv: list[str] | None = None) -> int:
    # Unter Windows benutzt Python fuer eine UMGELEITETE Ausgabe die Kodepage des
    # Systems statt UTF-8 — und genau so wird dieses Programm aufgerufen
    # (`--summary-md - >> $GITHUB_STEP_SUMMARY`).  Die Kurzfassung traegt ein
    # Kreuzchen und einen Haken; in cp1252 gibt es beide nicht, und das Programm
    # brach beim Schreiben ab.  An einer echten Konsole aendert die Zeile nichts.
    for _strom in (sys.stdout, sys.stderr):
        try:
            _strom.reconfigure(encoding="utf-8")
        except (AttributeError, ValueError):
            pass

    p = argparse.ArgumentParser(
        description="JUnit-XML (ctest/pytest) → eigenständiges HTML-Testprotokoll")
    p.add_argument("xml", nargs="+", metavar="[NAME:]DATEI",
                   help="JUnit-XML; optionaler Anzeigename vor dem Doppelpunkt")
    p.add_argument("-o", "--out", default="testprotokoll.html", help="Ausgabedatei (HTML)")
    p.add_argument("--title", default="Testprotokoll", help="Überschrift der Seite")
    p.add_argument("--summary-md", metavar="DATEI",
                   help="zusätzlich eine Markdown-Kurzfassung schreiben "
                        "(für $GITHUB_STEP_SUMMARY); '-' = stdout")
    p.add_argument("--fail-on-error", action="store_true",
                   help="Rückgabewert 1, wenn ein Fall fehlschlug")
    a = p.parse_args(argv)

    laeufe: list[Lauf] = []
    for eintrag in a.xml:
        name, pfad = zerlege_eintrag(eintrag)
        if not pfad.is_file():
            print(f"test_report: {pfad} gibt es nicht", file=sys.stderr)
            return 2
        try:
            laeufe.append(lies_junit(pfad, name))
        except ET.ParseError as e:
            print(f"test_report: {pfad} ist keine gültige XML — {e}", file=sys.stderr)
            return 2

    ziel = Path(a.out)
    if ziel.parent != Path(""):
        ziel.parent.mkdir(parents=True, exist_ok=True)
    ziel.write_text(baue_html(laeufe, a.title), encoding="utf-8")

    if a.summary_md:
        md = baue_markdown(laeufe)
        if a.summary_md == "-":
            sys.stdout.write(md)
        else:
            Path(a.summary_md).write_text(md, encoding="utf-8")

    gesamt = sum(len(l.faelle) for l in laeufe)
    fehler = sum(l.zahl("fehlgeschlagen") for l in laeufe)
    groesse = ziel.stat().st_size / 1024
    # Auf stderr, damit `--summary-md -` reines Markdown auf stdout liefert und
    # sich direkt an $GITHUB_STEP_SUMMARY anhängen lässt.
    print(f"test_report: {ziel} — {gesamt} Fälle, {fehler} fehlgeschlagen "
          f"({groesse:.0f} KB)", file=sys.stderr)

    return 1 if (a.fail_on_error and fehler) else 0


if __name__ == "__main__":
    sys.exit(main())

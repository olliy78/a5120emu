#!/usr/bin/env python3
"""Führt einen CLI-Testfall aus einer `.cli`-Datei aus.

Ersetzt die bis 2026-08-07 üblichen escapten Shell-Einzeiler im CMake.  Deren
Probleme, die dieses Skript beseitigt:

  * Erwartungen mussten als CMake-Regex durch zwei Ebenen Escaping — Klammern
    und eckige Klammern überlebten das nicht, weshalb im CMake der Kommentar
    stand: „regex uses '.' for literal []()*+".  Hier sind Erwartungen normale
    Zeichenketten (`expect:`), Regex nur, wo wirklich nötig (`expect_re:`).
  * Das `mktemp; cp …; …; rm -f`-Ritual stand 15x kopiert da; jetzt einmal hier.
  * Bricht ein Lauf ab, blieben Kopien der Diskette liegen — hier räumt ein
    `finally` auf.

Dateiformat (Zeilenweise, `#` ist Kommentar):

    tool:       k1520dbg | boot_trace       Werkzeug (Pfad kommt von CMake)
    disk:       <fixture>                   Fixture nach /tmp kopieren → %DISK%
    disk_path:  <pfad>                      literaler Pfad → %DISK% (keine Kopie)
    tmpfile <n>:                            freier Temp-Pfad → %n%
    timeout:    <sekunden>                  Vorgabe 120

    file <n>:                               Block → Temp-Datei, Pfad als %n%
      <inhalt…>
    capture_file: <n>                       Inhalt dieser Datei NACH dem Lauf an die
                                            geprüfte Ausgabe anhängen (für Kommandos,
                                            die ihr Ergebnis in eine Datei schreiben)
    stdin:                                  Block → auf die Standardeingabe
      <zeilen…>

    setup_run:  <argumente>                 Vorlauf, Ausgabe wird verworfen
    run:        <argumente>                 der gemessene Lauf

    expect:     <text>                      muss in der Ausgabe vorkommen
    expect_re:  <ERE>                       dito, als regulärer Ausdruck
                                            (Python-`re`: `.` deckt KEINEN
                                            Zeilenumbruch ab — anders als CMakes
                                            Regex, aus dem die Fälle stammen)
    forbid:     <text>                      darf NICHT vorkommen
    forbid_re:  <ERE>
    exit:       <n>                         erwarteter Exit-Code

`expect*`/`forbid*` sind wiederholbar; geprüft wird die zusammengefasste
Standard- und Fehlerausgabe des `run:`-Laufs (plus der Inhalt aller
`capture_file:`-Dateien).

Platzhalter: `%DISK%`, jeder `file`-/`tmpfile`-Name, sowie `%CLI_DIR%` (das
Verzeichnis `tests/cli`, z. B. für `-x %CLI_DIR%/scripts/…`).  Der Lauf findet in
einem Temp-Verzeichnis statt, damit Dateien, die das Werkzeug nebenbei anlegt,
mit aufgeräumt werden.

Bewusst KEINE Golden-Volldateien: die Ausgaben enthalten Taktzahlen und
Zeitstempel, ein Vollvergleich wäre bei jeder Timing-Änderung rot.  Die
Erwartung ist die inhaltliche Aussage — und die steht jetzt im Testverzeichnis
statt im Build-System.
"""

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


class CaseError(Exception):
    """Fehler in der Falldatei selbst (nicht im geprüften Werkzeug)."""


def parse_case(path):
    """`.cli`-Datei → dict.  Blockdirektiven sammeln eingerückte Folgezeilen."""
    case = {"expect": [], "expect_re": [], "forbid": [], "forbid_re": [],
            "setup_run": [], "capture_file": [], "files": {}, "tmpfiles": [], "stdin": None,
            "tool": None, "disk": None, "disk_path": None, "run": None,
            "exit": None, "timeout": 120, "doc": []}
    block_key = None
    block_lines = []

    def close_block():
        nonlocal block_key, block_lines
        if block_key is None:
            return
        text = "\n".join(block_lines)
        if text:
            text += "\n"
        if block_key == "stdin":
            case["stdin"] = text
        else:                                  # file <name>
            case["files"][block_key] = text
        block_key, block_lines = None, []

    for raw in open(path, encoding="utf-8").read().split("\n"):
        if block_key is not None and (raw.startswith("  ") or raw == ""):
            if raw == "" and not block_lines:
                continue                        # Leerzeile direkt nach dem Block
            block_lines.append(raw[2:] if raw.startswith("  ") else "")
            continue
        close_block()

        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            case["doc"].append(line.lstrip("# ").rstrip())
            continue
        if ":" not in line:
            raise CaseError(f"{path}: Zeile ohne Direktive: {raw!r}")

        key, _, value = line.partition(":")
        key, value = key.strip(), value.strip()

        if key == "stdin" and not value:
            block_key, block_lines = "stdin", []
        elif key.startswith("file ") and not value:
            block_key, block_lines = key[5:].strip(), []
        elif key.startswith("tmpfile"):
            case["tmpfiles"].append(key[7:].strip())
        elif key in ("expect", "expect_re", "forbid", "forbid_re", "setup_run",
                     "capture_file"):
            case[key].append(value)
        elif key in ("tool", "disk", "disk_path", "run"):
            case[key] = value
        elif key in ("exit", "timeout"):
            case[key] = int(value)
        else:
            raise CaseError(f"{path}: unbekannte Direktive {key!r}")
    close_block()

    if not case["tool"]:
        raise CaseError(f"{path}: 'tool:' fehlt")
    if case["run"] is None:
        raise CaseError(f"{path}: 'run:' fehlt")
    return case


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case")
    ap.add_argument("--fixtures", required=True, help="Verzeichnis der Testdisketten")
    ap.add_argument("--tool", action="append", default=[], metavar="NAME=PFAD",
                    help="Werkzeugpfad, wiederholbar")
    args = ap.parse_args()

    tools = dict(t.split("=", 1) for t in args.tool)
    case = parse_case(args.case)
    if case["tool"] not in tools:
        raise CaseError(f"unbekanntes Werkzeug {case['tool']!r} "
                        f"(bekannt: {', '.join(sorted(tools))})")

    tmpdir = tempfile.mkdtemp(prefix="k1520_cli_")
    try:
        subst = {}

        if case["disk"]:
            src = os.path.join(args.fixtures, case["disk"])
            if not os.path.exists(src):
                raise CaseError(f"Fixture fehlt: {src}")
            dst = os.path.join(tmpdir, case["disk"])
            shutil.copy(src, dst)          # NIE die committete Diskette mounten
            subst["DISK"] = dst
        elif case["disk_path"]:
            subst["DISK"] = case["disk_path"]

        for name, content in case["files"].items():
            path = os.path.join(tmpdir, name)
            with open(path, "w", encoding="utf-8") as f:
                f.write(content)
            subst[name] = path
        for name in case["tmpfiles"]:
            subst[name] = os.path.join(tmpdir, name)
        for name in case["capture_file"]:
            subst.setdefault(name, os.path.join(tmpdir, name))
        # Verzeichnis tests/cli — für Skripte, die mitgeliefert werden.
        subst["CLI_DIR"] = os.path.dirname(os.path.dirname(os.path.abspath(args.case)))

        def expand(text):
            for key, value in subst.items():
                text = text.replace("%" + key + "%", value)
            left = re.findall(r"%[A-Za-z_][A-Za-z0-9_.]*%", text)
            if left:
                raise CaseError(f"unaufgelöste Platzhalter: {', '.join(left)}")
            return text

        exe = tools[case["tool"]]
        # Platzhalter gelten auch in der Standardeingabe — Debugger-Kommandos wie
        # `trace %datei%` oder `lst %quelle.mac%` brauchen den echten Pfad.
        stdin = expand(case["stdin"]) if case["stdin"] else None

        for extra in case["setup_run"]:
            subprocess.run([exe] + shlex.split(expand(extra)),
                           input=stdin, text=True, cwd=tmpdir,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=case["timeout"])

        proc = subprocess.run([exe] + shlex.split(expand(case["run"])),
                              input=stdin, text=True, cwd=tmpdir,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=case["timeout"])
        output = proc.stdout or ""

        # Kommandos, die ihr Ergebnis in eine Datei schreiben (trace, itrace, dump)
        for name in case["capture_file"]:
            path = subst[name]
            if not os.path.exists(path):
                print(f"--- {os.path.basename(args.case)} FEHLGESCHLAGEN")
                print(f"  * capture_file: {name} wurde nicht angelegt ({path})")
                return 1
            with open(path, encoding="utf-8", errors="replace") as f:
                output += "\n--- Inhalt von " + name + " ---\n" + f.read()

        problems = []
        for needle in case["expect"]:
            if needle not in output:
                problems.append(f"fehlt in der Ausgabe: {needle!r}")
        for pattern in case["expect_re"]:
            if not re.search(pattern, output):
                problems.append(f"kein Treffer für /{pattern}/")
        for needle in case["forbid"]:
            if needle in output:
                problems.append(f"unerwartet in der Ausgabe: {needle!r}")
        for pattern in case["forbid_re"]:
            if re.search(pattern, output):
                problems.append(f"unerwarteter Treffer für /{pattern}/")
        if case["exit"] is not None and proc.returncode != case["exit"]:
            problems.append(f"Exit-Code {proc.returncode}, erwartet {case['exit']}")

        if problems:
            print(f"--- {os.path.basename(args.case)} FEHLGESCHLAGEN")
            for p in problems:
                print(f"  * {p}")
            print(f"--- Kommando: {exe} {expand(case['run'])}")
            print("--- Ausgabe (letzte 40 Zeilen):")
            for line in output.split("\n")[-40:]:
                print("  " + line)
            return 1
        return 0
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CaseError as e:
        print(f"FALLDATEI FEHLERHAFT: {e}", file=sys.stderr)
        sys.exit(2)
    except subprocess.TimeoutExpired as e:
        print(f"ZEITÜBERSCHREITUNG nach {e.timeout}s: {e.cmd}", file=sys.stderr)
        sys.exit(3)

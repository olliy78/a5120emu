#!/usr/bin/env python3
"""
Versionswerkzeug (doc/ci_pipeline.md §7.1) — nur Standardbibliothek.

    tools/version.py                      Bauversion (wie --bau)
    tools/version.py --basis              Inhalt von VERSION
    tools/version.py --bau                0.3.0-beta+g1a2b3c4 | 0.3.0-beta.2 (Tag auf HEAD)
    tools/version.py --windows            X.Y.Z.B für VersionInfoVersion des Installers
    tools/version.py --beispielordner     Beispieldisketten_v03[-beta.2|-test]
    tools/version.py --vorab              Rückgabewert 0 = Vorabversion
    tools/version.py --pruefe-tag v0.3.0-beta.1   Rückgabewert ≠ 0, wenn das Tag nicht zu VERSION passt
    tools/version.py --pruefe-release 0.3.0-beta.1  (dev.sh release) Version gültig, frei, größer als alle Tags
    tools/version.py --release-basis 0.3.0-beta.1   Basis, die VERSION dafür tragen muss
    tools/version.py --naechste-basis 0.3.0         VERSION nach der Endfassung (0.4.0-beta)

Die Logik selbst steht in app/version.py (die Oberfläche braucht sie in einer
Installation ebenfalls); diese Datei ist nur die Kommandozeile davor.
"""
import argparse
import importlib.util
import sys
from pathlib import Path

WURZEL = Path(__file__).resolve().parents[1]


def _lade():
    # Kein Bytecode: das Werkzeug läuft auch in Repos (dev.sh release, CI), deren
    # Arbeitsbaum SAUBER sein muss — ein `app/__pycache__/` machte ihn unsauber.
    sys.dont_write_bytecode = True
    # Per Dateipfad statt `import app.version`: `app/__init__.py` und die
    # Oberfläche sollen hier nicht mitgeladen werden (kein Qt, kein venv).
    spec = importlib.util.spec_from_file_location("k1520_version", WURZEL / "app" / "version.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules["k1520_version"] = mod          # dataclass-/Typing-Hilfen finden das Modul
    spec.loader.exec_module(mod)
    return mod


def main(argv=None) -> int:
    v = _lade()
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = p.add_mutually_exclusive_group()
    g.add_argument("--basis", action="store_true")
    g.add_argument("--bau", action="store_true")
    g.add_argument("--windows", action="store_true")
    g.add_argument("--beispielordner", action="store_true")
    g.add_argument("--vorab", action="store_true")
    g.add_argument("--pruefe-tag", metavar="TAG")
    g.add_argument("--pruefe-release", metavar="VERSION")
    g.add_argument("--release-basis", metavar="VERSION")
    g.add_argument("--naechste-basis", metavar="VERSION")
    p.add_argument("--wurzel", type=Path, default=WURZEL,
                   help="anderes Repo/Verzeichnis mit VERSION (für Tests)")
    a = p.parse_args(argv)
    try:
        if a.basis:
            print(v.lies_basis(a.wurzel))
        elif a.windows:
            print(v.windows_zahl(v.bauversion(a.wurzel)))
        elif a.beispielordner:
            print(v.beispielordner(v.bauversion(a.wurzel)))
        elif a.vorab:
            return 0 if v.ist_vorabversion(v.bauversion(a.wurzel)) else 1
        elif a.pruefe_tag:
            basis = v.lies_basis(a.wurzel)
            v.zerlege_tag(a.pruefe_tag)
            if not v.tag_passt(a.pruefe_tag, basis):
                erwartet = f"v{basis}.N" if "-" in basis else f"v{basis}"
                print(f"Tag {a.pruefe_tag} passt nicht zu VERSION ({basis}): "
                      f"erwartet {erwartet}", file=sys.stderr)
                return 1
        elif a.pruefe_release:
            v.pruefe_neue_version(a.pruefe_release.lstrip("v"), v.alle_tags(a.wurzel))
        elif a.release_basis:
            print(v.basis_zu(a.release_basis.lstrip("v")))
        elif a.naechste_basis:
            print(v.naechste_basis(a.naechste_basis.lstrip("v")))
        else:
            print(v.bauversion(a.wurzel))
    except v.VersionsFehler as e:
        print(f"version.py: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

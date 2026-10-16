"""Waechter: im Code stehen keine Umlaute (Oberflaeche ausgenommen).

Die Regeln (welche Dateien, welche Zeichenketten) stehen in
``tools/umlaute_ersetzen.py``; dieser Test fragt dieselbe Logik und schlaegt an,
sobald sie in einer geprueften Quelldatei noch etwas zu ersetzen faende.
Abhilfe:  python3 tools/umlaute_ersetzen.py
"""

import importlib.util
import subprocess

import pytest

from conftest import PROJECT_ROOT


def _werkzeug():
    spez = importlib.util.spec_from_file_location(
        "umlaute_ersetzen", PROJECT_ROOT / "tools" / "umlaute_ersetzen.py")
    mod = importlib.util.module_from_spec(spez)
    spez.loader.exec_module(mod)
    return mod


def test_keine_umlaute_im_code():
    try:
        r = subprocess.run(["git", "ls-files"], cwd=PROJECT_ROOT, capture_output=True,
                           text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        pytest.skip("kein git-Arbeitsbaum")
    w = _werkzeug()
    verstoesse = []
    for rel in r.stdout.splitlines():
        if w.ausgenommen(rel):
            continue
        p = PROJECT_ROOT / rel
        if not p.is_file():
            continue
        try:
            text = p.read_bytes().decode("utf-8")
        except UnicodeDecodeError:
            continue
        if not w.HAT_UMLAUT.search(text):
            continue
        if w.bearbeite(rel, text, lambda *a: None) != text:
            verstoesse.append(rel)
    assert not verstoesse, (
        "Umlaute im Code (python3 tools/umlaute_ersetzen.py ersetzt sie): "
        + ", ".join(verstoesse[:20]))

"""Handbuchanzeige (:mod:`app.ui_help`) — der Platzhalter ``{{VERSION}}``.

Die ``handbuch.md`` beider Programme bleibt versionsfrei (``Version {{VERSION}}``
unter der Überschrift) und kann nie veralten; ersetzt wird erst beim Anzeigen
(doc/ci_pipeline.md §7.2).
"""

import pytest

from conftest import PROJECT_ROOT

from app import ui_help, version

HANDBUECHER = [
    PROJECT_ROOT / "app" / "help" / "handbuch.md",
    PROJECT_ROOT / "app" / "disktool" / "help" / "handbuch.md",
]


@pytest.mark.parametrize("pfad", HANDBUECHER, ids=lambda p: p.parent.parent.name)
def test_handbuch_traegt_den_platzhalter_und_zeigt_die_version(pfad):
    quelle = pfad.read_text(encoding="utf-8")
    assert "Version {{VERSION}}" in quelle.split("\n## ")[0], \
        "der Platzhalter gehört unter die Überschrift"
    angezeigt = ui_help.lade_handbuch(pfad)
    assert "{{VERSION}}" not in angezeigt
    assert f"Version {version.fassung()}" in angezeigt


def test_handbuch_des_emulators_ersetzt_ebenfalls():
    from app.ui.help_window import lade_handbuch
    assert "{{VERSION}}" not in lade_handbuch()

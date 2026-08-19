"""@test Beigelegte Fremdquellen (`third_party/`) und ihre Lizenzen.

Dieses Projekt steht unter der **MIT-Lizenz**. Beigelegter Fremdcode wird mit
ausgeliefert — seine Lizenz wird damit Teil der Auslieferung. Der Vorfall, aus dem
diese Datei entstand: der Debugger hing an GNU **readline** (GPLv3+), was ein
ausgeliefertes Binärabbild zu einem Gesamtwerk unter GPLv3 gemacht hätte
(`doc/design/13_distribution.md` §10a.3).

Geprüft wird deshalb dreierlei, und zwar ohne Netz:
  1. Jede Fremdquelle hat eine LICENSE **und** eine README mit Herkunft und Stand.
  2. Ihre Lizenz ist permissiv (MIT/BSD/ISC/Apache) — keine Copyleft-Lizenz.
  3. Das Projekt selbst ist noch MIT (sonst gilt die Regel oben nicht mehr).

Das ist bewusst eine TEXT-Prüfung: sie soll auffallen, wenn jemand eine Quelle
beilegt, nicht juristisch beraten.
"""

import re
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[2]
THIRD_PARTY = PROJECT_ROOT / "third_party"

# Wortlaute, an denen sich eine Copyleft-Lizenz erkennen lässt.
COPYLEFT = [
    "GNU GENERAL PUBLIC LICENSE",
    "GNU LESSER GENERAL PUBLIC LICENSE",
    "GNU AFFERO",
    "MOZILLA PUBLIC LICENSE",
    "EUROPEAN UNION PUBLIC LICENCE",
]

# Wortlaute, die eine permissive Lizenz belegen.
PERMISSIV = [
    "MIT License",
    "Permission is hereby granted, free of charge",       # MIT
    "Redistribution and use in source and binary forms",  # BSD
    "Permission to use, copy, modify, and/or distribute", # ISC
    "Apache License",
]


def fremdquellen():
    if not THIRD_PARTY.is_dir():
        return []
    return sorted(p for p in THIRD_PARTY.iterdir() if p.is_dir())


def test_es_gibt_ueberhaupt_fremdquellen():
    """Schutz gegen einen stillen Leerlauf: findet der Test nichts, prueft er nichts."""
    assert fremdquellen(), (
        "third_party/ ist leer oder fehlt — entweder wurde es verschoben (dann diesen "
        "Test nachziehen) oder der Test laeuft ins Leere und meldet trotzdem gruen."
    )


@pytest.mark.parametrize("quelle", fremdquellen(), ids=lambda p: p.name)
def test_fremdquelle_hat_lizenz_und_herkunft(quelle: Path):
    lizenz = next((quelle / n for n in ("LICENSE", "LICENSE.md", "LICENSE.txt",
                                        "COPYING") if (quelle / n).is_file()), None)
    assert lizenz is not None, f"{quelle.name}: keine LICENSE beigelegt"
    assert lizenz.stat().st_size > 100, f"{quelle.name}: LICENSE ist verdaechtig kurz"

    readme = quelle / "README.md"
    assert readme.is_file(), (
        f"{quelle.name}: keine README.md — Herkunft, Stand und der Grund fuer die "
        f"Beilage muessen nachlesbar sein")
    text = readme.read_text(encoding="utf-8")
    assert re.search(r"https?://", text), f"{quelle.name}: README nennt keine Herkunft (URL)"
    assert re.search(r"[0-9a-f]{7,40}|v?\d+\.\d+", text), \
        f"{quelle.name}: README nennt keinen Stand (Commit oder Version)"


@pytest.mark.parametrize("quelle", fremdquellen(), ids=lambda p: p.name)
def test_fremdquelle_ist_permissiv_lizenziert(quelle: Path):
    lizenz = next(quelle.glob("LICENSE*"), None) or next(quelle.glob("COPYING*"), None)
    text = lizenz.read_text(encoding="utf-8", errors="replace")

    for c in COPYLEFT:
        assert c.lower() not in text.lower(), (
            f"{quelle.name}: Copyleft-Lizenz ({c}) in einer beigelegten Quelle. "
            f"Dieses Projekt ist MIT — das ginge nicht zusammen, sobald ein Binaerabbild "
            f"ausgeliefert wird (doc/design/13_distribution.md §10a.3).")

    assert any(p.lower() in text.lower() for p in PERMISSIV), (
        f"{quelle.name}: LICENSE nicht als permissiv erkannt. Ist sie es, den Wortlaut "
        f"in PERMISSIV aufnehmen; ist sie es nicht, gehoert die Quelle hier nicht her.")


def test_isocline_ist_mit_lizenziert():
    """Namentlich, weil der Zeileneditor GENAU wegen der Lizenz ausgetauscht wurde."""
    lizenz = THIRD_PARTY / "isocline" / "LICENSE"
    assert lizenz.is_file(), "third_party/isocline/LICENSE fehlt"
    assert "MIT License" in lizenz.read_text(encoding="utf-8")


def test_projekt_ist_weiterhin_mit_lizenziert():
    """Faellt das weg, ist die Regel „nur permissive Fremdquellen" neu zu bewerten."""
    lizenz = PROJECT_ROOT / "LICENSE"
    assert lizenz.is_file()
    assert "MIT License" in lizenz.read_text(encoding="utf-8")

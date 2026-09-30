"""`app/modell.py` — Modellwahl A5120/A5120.16.

Reine Python-Logik ohne Kern und ohne Qt: die eine Stelle, die einen
Modellschlüssel (``general.model`` der Konfiguration) auf den core-``em``-
Parameter von ``K1520Emulator`` und den Anzeigenamen abbildet.
"""

import app.modell as modell


def test_default_model_is_a5120_without_em():
    assert modell.DEFAULT_MODEL == modell.A5120
    assert modell.em_for(modell.A5120) is None


def test_a5120_16_maps_to_em256():
    assert modell.em_for(modell.A5120_16) == "em256"


def test_normalize_falls_back_to_the_default():
    """Ältere/fremde Konfigurationen ohne (oder mit unbekanntem) Eintrag."""
    for wert in (None, "", "   ", "unbekannt", "EM256", 42):
        assert modell.normalize(wert) == modell.DEFAULT_MODEL


def test_normalize_is_case_insensitive_for_known_keys():
    assert modell.normalize("A5120.16") == modell.A5120_16
    assert modell.normalize(" a5120.16 ") == modell.A5120_16


def test_every_model_has_a_label():
    for schluessel, _em, beschriftung in modell.MODELS:
        assert beschriftung
        assert modell.label(schluessel) == beschriftung

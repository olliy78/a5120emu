"""
K1520 Emulator - Modellwahl (A5120 / A5120.16)
================================================

Single source of truth für das wählbare Maschinenmodell, geteilt von der
Einstellungen-Dropdown (:class:`~app.ui.settings_widget.SettingsWidget`) und
der Konfiguration (`app/config_io.py`, Abschnitt ``general.model``) — nach
demselben Schnitt wie :mod:`app.drive_types` und :mod:`app.takt`.

Der A5120.16 (`doc/design/17_a5120_16.md`) ist am Kern nur ein weiterer
Konstruktorparameter von ``K1520Emulator``: ``em="em256"`` steckt die
Steuerkarte 062-9005 samt Speicherkarte EM256 (062-9000) auf den K1520-Bus.
``em=None``/``"none"`` ist der unveränderte A5120 — die Vorgabe, auch in
``data/default_config.yaml``.  EM064 (062-9001) ist am Kern schon als Variante
angelegt (§6 des Plans), hat hier aber bewusst noch keinen eigenen Eintrag:
geprüft ist bislang nur EM256.
"""

#: Interne Modellschlüssel — stehen so in der Konfiguration (``general.model``).
A5120 = "a5120"
A5120_16 = "a5120.16"

#: Vorgabe: der unveränderte A5120, ohne Erweiterungsmodul.
DEFAULT_MODEL = A5120

#: (Schlüssel, core-``em``-Parameter oder ``None``, Anzeigename für die Auswahl)
MODELS = [
    (A5120, None, "A5120 (ohne Erweiterung)"),
    (A5120_16, "em256", "A5120.16 (EM256 + U8001)"),
]

_EM_FOR = {schluessel: em for schluessel, em, _ in MODELS}
_LABEL_FOR = {schluessel: label for schluessel, _, label in MODELS}


def normalize(model) -> str:
    """Ein bekannter Modellschlüssel — alles Unbekannte/Fehlende wird die Vorgabe.

    Trägt das auch ältere/fremde Konfigurationen ohne den Eintrag: die laufen
    unverändert als A5120 weiter.
    """
    model = str(model).strip().lower() if model else ""
    return model if model in _EM_FOR else DEFAULT_MODEL


def em_for(model) -> str:
    """Der core-``em=``-Parameter für *model* (``None`` = kein Erweiterungsmodul)."""
    return _EM_FOR.get(normalize(model))


def label(model) -> str:
    """Anzeigename für die Auswahl."""
    return _LABEL_FOR.get(normalize(model), _LABEL_FOR[DEFAULT_MODEL])


# ── PRG 710 / PRG 710-1 (prg710emu) ─────────────────────────────────────────
# Dort ist die Modellwahl keine Erweiterung, sondern die Maschine selbst; die
# Zuordnung Schlüssel → Kern steht im Programmprofil (`app/profil.py`), hier nur
# die Schlüssel, damit Konfiguration und Tests dieselben Namen benutzen.
PRG710 = "prg710"
PRG710_1 = "prg710-1"

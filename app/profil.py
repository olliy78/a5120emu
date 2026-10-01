"""Programmprofile: A5120 Emulator und K8915 Emulator — ein Programm, zwei Gesichter.

Die Oberfläche (`app/ui/main_window.py`) ist für beide Rechner dieselbe; was sie
unterscheidet, steht **hier** und nirgends sonst (doc/design/18_k8915emu_oberflaeche.md
§2): Maschinentyp des Kerns, Fenstertitel, Name der Konfigurationsdatei und ihrer
Auslieferungsvorgabe, Laufwerksbestückung, Nenntakt, Bildschirmtastatur, Lampen der
Frontplatte und die zusätzlichen Aktionen.  Wer eine Eigenheit einer Maschine
braucht, fragt das Profil — kein ``if machine == …`` quer durch die Oberfläche.

Gewählt wird das Profil beim Start (``app/main.py --machine k8915``, fest
übergeben vom Starter ``run_k8915emu.sh`` bzw. ``bin/k8915emu``); ohne Angabe ist
es der A5120 — so verhalten sich ältere Starter und Tests unverändert.

Das Modul importiert kein Qt: `app/paths.py` und `app/config_io.py` brauchen es
schon, bevor PySide6 geladen ist (``--paths``, ``--help``).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Tuple


@dataclass(frozen=True)
class Programmprofil:
    """Alles, worin sich die beiden Emulatoren unterscheiden."""

    #: Maschinenname der Bindung (``K1520Emulator(machine=…)``).
    maschine: str
    #: Programmname — Starter, ``.desktop``, Meldungen auf der Konsole.
    programm: str
    #: Fenstertitel, Startmenü-Eintrag, ``.desktop``-Name, Kopf der Startskripte.
    titel: str
    #: Rechner, wie er im Text genannt wird („Der A5120 läuft mit …").
    rechner: str
    #: Einzeiler für „Über“ und ``--help``.
    beschreibung: str
    #: Konfiguration des Anwenders in :func:`app.paths.config_dir`.
    konfig_datei: str
    #: Auslieferungsvorgabe (``data/`` bzw. ``share/k1520emu/``).
    vorgabe_datei: str
    #: Nenntakt in Hertz und so, wie ihn der Anwender liest.
    nenntakt_hz: int
    nenntakt_text: str
    #: Bildschirmtastatur: ``"k7637"`` oder ``"k7672"``.
    tastatur: str
    #: Frontplatte mit Lampen in der Statuszeile (K8915: Run … Power).
    frontplatte: bool = False
    #: Aktionen, die nur dieses Programm hat (Namen aus `app/ui/actions.py`).
    eigene_aktionen: Tuple[str, ...] = field(default_factory=tuple)
    #: Die jeweils ANDERE Maschine (für *Werkzeuge ▸ … starten*).
    andere: str = ""
    #: Frühere Namen serieller Schnittstellen → heutiger Name (Kern).  Die
    #: Konfiguration ist über den NAMEN verschlüsselt; ohne diese Abbildung wären
    #: Einstellungen unter einem alten Namen herrenlos (K8915: AP-S12).
    alte_schnittstellen: Tuple[Tuple[str, str], ...] = field(default_factory=tuple)

    def schnittstellen_umbenennen(self, daten: dict) -> dict:
        """Abschnitt ``schnittstellen`` mit alten Namen auf die heutigen abbilden.

        Ein Eintrag unter dem heutigen Namen geht vor (er ist der jüngere); alles
        andere bleibt, wie es ist.
        """
        if not isinstance(daten, dict) or not self.alte_schnittstellen:
            return daten
        neu = dict(daten)
        for alt, heute in self.alte_schnittstellen:
            if alt in neu:
                wert = neu.pop(alt)
                neu.setdefault(heute, wert)
        return neu

    # Laufwerke stehen in `app/drive_types.py` (je Maschine) — dort ist der
    # Laufwerkskatalog; das Profil reicht nur durch.  Spät importiert: die
    # Bindung (und damit die Kernbibliothek) soll hier nicht mitkommen.
    def standard_laufwerke(self) -> list:
        """Laufwerksbestückung ab Werk, je K5122-Steckplatz ein Kernprofil."""
        from app import drive_types as dt
        return dt.default_drive_types(self.maschine)

    def waehlbare_laufwerke(self) -> list:
        """Wählbare Laufwerkstypen im Einstellungskasten (Einträge aus DRIVE_TYPES)."""
        from app import drive_types as dt
        return dt.drive_types_for(self.maschine)


A5120 = Programmprofil(
    maschine="a5120",
    programm="a5120emu",
    titel="A5120 Emulator",
    rechner="A5120",
    beschreibung="Emulator des Bürocomputers robotron A5120 (K1520-Bus)",
    konfig_datei="a5120emu.yaml",
    vorgabe_datei="default_config_a5120.yaml",
    nenntakt_hz=2_450_000,
    nenntakt_text="2,45 MHz",
    tastatur="k7637",
    andere="k8915",
)

K8915 = Programmprofil(
    maschine="k8915",
    programm="k8915emu",
    titel="K8915 Emulator",
    rechner="K8915",
    beschreibung="Emulator des Arbeitsplatzcomputers robotron K8915 (K1520-Bus)",
    konfig_datei="k8915emu.yaml",
    vorgabe_datei="default_config_k8915.yaml",
    # 2,4576 MHz (K8915Machine::CPU_HZ; Quarz 9,8304 MHz / 4).
    nenntakt_hz=2_457_600,
    nenntakt_text="2,4576 MHz",
    tastatur="k7672",
    frontplatte=True,
    eigene_aktionen=("nmi",),
    andere="a5120",
    # Bis AP-S12 hießen SIO1-B und SIO2-A nach dem Entwurf „IFS 1"/„IFS 2"; seitdem
    # nach der Beschriftung am Gerät.  „V.24" (SIO1-A) blieb.
    alte_schnittstellen=(("IFS 1", "Drucker/IFSS1"), ("IFS 2", "DFÜ/IFSS2")),
)

#: Alle Profile nach Maschinenname.
PROFILE = {p.maschine: p for p in (A5120, K8915)}

#: Das Profil ohne Angabe — ältere Starter, Tests, ``app/main.py`` ohne Schalter.
VORGABE = A5120


def profil(name: str = "") -> Programmprofil:
    """Profil zu einem Maschinennamen (``""`` = Vorgabe A5120).

    Raises:
        ValueError: bei einem unbekannten Namen — mit den bekannten im Text.
    """
    if not name:
        return VORGABE
    try:
        return PROFILE[name.lower()]
    except KeyError:
        raise ValueError(f"unbekannte Maschine {name!r} "
                         f"(bekannt: {', '.join(PROFILE)})") from None

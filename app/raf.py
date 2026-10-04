"""
K1520 Emulator - RAM-Floppy RAF (RAF 128 / RAF 512 / RAF-2M)
=============================================================

Single source of truth für die wählbare RAM-Disk, geteilt vom Auswahlfeld unter
*Einstellungen ▸ Allgemein* (:class:`~app.ui.settings_widget.SettingsWidget`) und
der Konfiguration (Abschnitt ``machine``: ``raf``/``raf_standby``,
doc/design/22_raf512.md §7) — nach demselben Schnitt wie :mod:`app.modell`.

Am Kern ist die RAF ein Konstruktorparameter von ``K1520Emulator(raf=…)``; ein
Wechsel erzeugt also die Maschine neu (eine Karte steckt man nicht im Betrieb).

**Stand-by-Ablage** (§7.1, Stand-by-Versorgung 5PG): der Inhalt liegt als Rohdatei
im Konfigurationsordner.  Der Ordner (:func:`app.paths.config_dir`) ist für ALLE
drei Programme derselbe (``~/.config/k1520emu``) — die Datei heißt deshalb je
Programm anders: ``raf_a5120emu.bin``, ``raf_k8915emu.bin``, ``raf_prg710emu.bin``
(:func:`ablage_pfad`).  Sonst lüde der K8915 die RAM-Disk, die der A5120 eben
beschrieben hat — und eine laufende zweite Instanz überschriebe sie.  EINE Datei je
Programm, nicht je Typ: passt die Größe nicht zum gewählten Typ, wird sie nicht
geladen und beim nächsten Schreiben ersetzt (so verlangt es §7.1).
"""

import os

from app import paths

#: Interne Schlüssel — stehen so in der Konfiguration (``machine.raf``).
KEINE = "none"

#: (Schlüssel, Anzeigename) in der Reihenfolge des Auswahlfelds.
TYPEN = [
    (KEINE, "keine"),
    ("raf128", "RAF 128 (128 KByte)"),
    ("raf512", "RAF 512 (512 KByte)"),
    ("raf2m", "RAF-2M (2 MByte)"),
]

#: Hinweistext am Auswahlfeld (§7.1).
TIPP = ("RAM-Floppy des ZWG der AdW auf E/A 88H/89H.  Unter CP/A und SCPX mit "
        "RAF512.COM (Laufwerk P:) oder RAFCPM.COM (M:) einbinden; das CP/A mit "
        "eingebautem RAF-Treiber legt M: selbst an.  Der Inhalt übersteht RESET.  "
        "Ein Wechsel schaltet die Maschine aus und neu ein.")

#: Hinweistext am Stand-by-Kästchen.
STANDBY_TIPP = ("Wie die Stand-by-Versorgung 5PG: der Inhalt der RAM-Disk wird beim "
                "Beenden, beim Ausschalten und vor jedem Neuaufbau der Maschine in "
                "den Konfigurationsordner geschrieben und beim Einschalten wieder "
                "geladen.  Aus: keine Datei wird gelesen oder geschrieben.")

_SCHLUESSEL = {k for k, _ in TYPEN}


def normalize(typ) -> str:
    """Ein bekannter RAF-Schlüssel — Fehlendes/Unbekanntes wird ``"none"``.

    Trägt ältere/fremde Konfigurationen: ohne Eintrag keine RAF.
    """
    typ = str(typ).strip().lower() if typ else ""
    return typ if typ in _SCHLUESSEL else KEINE


def core_param(typ):
    """Der ``raf=``-Parameter für ``K1520Emulator`` (``None`` = keine Karte)."""
    typ = normalize(typ)
    return None if typ == KEINE else typ


def standby_normalize(wert) -> bool:
    """``machine.raf_standby`` — nur ein echtes ``true`` schaltet ein."""
    return wert is True


def ablage_pfad(programm: str) -> str:
    """Pfad der Stand-by-Ablage des Programms *programm* (``raf_<programm>.bin``)."""
    return os.path.join(str(paths.config_dir()), f"raf_{programm}.bin")

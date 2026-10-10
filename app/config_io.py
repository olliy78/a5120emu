"""
K1520 Emulator - Configuration load/save (YAML)
===============================================

The configuration file is a top-level YAML mapping so further sections can be
added over time.  Sections carried today:

* ``crt``     — picture-tube look (see :class:`~app.ui.screen_widget.CRTParams`)
* ``general`` — general emulator settings: emulation ``speed`` and the machine
  ``model`` (``"a5120"``/``"a5120.16"``, ``app/modell.py``; missing = ``"a5120"``,
  so older configurations keep running unchanged)
* ``machine`` — Bestückung jenseits von Modell und Laufwerken: ``raf``
  (``none``/``raf128``/``raf512``/``raf2m``, RAM-Floppy, `app/raf.py`) und
  ``raf_standby`` (Inhalt in ``raf_<programm>.bin`` im Konfigurationsordner
  behalten, doc/design/22_raf512.md §7).  Fehlend/unbekannt = keine RAF, kein
  Stand-by.
* ``drive_types`` — the drive-bay configuration: one core ``DriveProfile`` name
  per K5122 slot (``"none"`` = empty slot), restored on the next start
* ``disks``   — the mounted disk images, so they are restored on the next start
* ``schnittstellen`` — die seriellen Schnittstellen nach aussen (AP-S7, Entwurf 19 §9):
  je Name aus dem Kern ``betriebsart``/``rolle``/``host``/``port``/``loop``/
  ``rtscts_bruecke``/``xonxoff``/``taktquelle``/``datei`` und ``aktiv`` (Zustand beim
  Beenden → Wiederaufnahme beim Start).  Die Auslieferungsvorgabe trägt keinen
  Abschnitt; fehlend heisst „nicht anfassen".
* ``window``  — window size, dock layout (which panels are active, their
  arrangement and sizes) **and die Symbolleiste** (``toolbar``: die Aktionsnamen
  in ihrer Reihenfolge, ``""`` = Trennstrich; ``toolbar_style``: Qts
  ``ToolButtonStyle``), so the previous look is restored on the next start.
  Der Leisten-INHALT steht bewusst nicht im ``dock_state``: Qt merkt sich dort
  nur Ort und Sichtbarkeit einer Leiste, nicht, was darin liegt.

Example::

    version: 1
    crt:
      phosphor_on: '#78D41D'
      brightness: 2.5
      ...
    general:
      speed: 1.0
    disks:
      - drive: 0
        path: /home/user/projects/a5120emu_ui/disks/a5120_cpa_k5601_system.hfe
        format: cpa780
        write_protect: false
    window:
      width: 1024
      height: 680
      dock_state: <base64 QMainWindow.saveState()>
      toolbar: [konfig_laden, konfig_speichern, '', power, reset]
      toolbar_style: 3

The auto-persisted configuration lives under ``~/.config/k1520emu/`` (honouring
``$XDG_CONFIG_HOME``) — **one file per program**, named in the program profile
(`app/profil.py`): ``a5120emu.yaml`` for the A5120 Emulator, ``k8915emu.yaml`` for
the K8915 Emulator (doc/design/18_k8915emu_oberflaeche.md §2).  Every
exported/loaded file uses the very same syntax, so a saved config can later be
loaded back verbatim.

**Umzug der Altdatei** (:func:`konfig_umziehen`): bis 2026-09 hiess die Datei des
A5120 ``config.yaml``.  Findet der A5120 Emulator beim Start noch eine
``config.yaml``, aber keine ``a5120emu.yaml``, wird sie EINMAL umbenannt — sonst
stünde der Anwender nach dem Update mit dem Auslieferungszustand da.

**Die Auslieferungskonfiguration** (:func:`standard_konfiguration`) ist eine
Datei desselben Aufbaus, die mit dem Programm kommt statt vom Anwender:
``data/default_config_a5120.yaml`` bzw. ``data/default_config_k8915.yaml`` im
Quellbaum, ``share/k1520emu/`` in einer Installation.  Sie ist der Zustand nach der Erstinstallation und das Ziel von
*Ansicht ▸ Standard zurücksetzen*.  Sie trägt bewusst KEINEN ``disks``-Abschnitt
— Diskettenpfade sind rechnerspezifisch, und ohne den Abschnitt lässt das
Zurücksetzen die eingelegten Disketten in Ruhe.
"""

import os
from typing import Callable, Dict

import yaml

from app import instanz
from app import paths
from app import profil as profile
from app.ui.screen_widget import CRTParams

#: FORMATversion der Konfigurationsdatei — NICHT die Programmversion.  Sie steigt
#: nur bei einer inkompatiblen Änderung des Aufbaus (doc/ci_pipeline.md §7.4);
#: die meisten Fassungen ändern sie nicht.  Die Programmversion steht als
#: ``geschrieben_von`` daneben (reine Auskunft für die Fehlersuche).
CONFIG_VERSION = 1

#: Migrationskette: ``{n: f}`` hebt eine Datei der Formatversion ``n`` auf
#: ``n + 1`` (``f(data) -> data``).  Derzeit leer, weil es nur Format 1 gibt.
#: Beim Laden läuft die Kette von der gelesenen bis zur eigenen Version.
_MIGRATIONEN: Dict[int, Callable[[dict], dict]] = {}

#: Name der Konfiguration des A5120 bis 2026-09 — Quelle des einmaligen Umzugs.
ALTE_KONFIG_DATEI = "config.yaml"


def default_config_dir() -> str:
    """Directory of the auto-persisted configuration (``~/.config/k1520emu``).

    Platform-dependent — the resolution (and the fallback to an existing
    ``~/.config/k1520emu`` on Windows/macOS) lives in :func:`app.paths.config_dir`.
    """
    return str(paths.config_dir())


def default_config_path(profil: "profile.Programmprofil" = None) -> str:
    """Path of the auto-persisted configuration file of *profil* (default A5120)."""
    profil = profil or profile.VORGABE
    von_hand = instanz.konfig_pfad_vorgabe()           # --config DATEI
    if von_hand:
        return von_hand
    return os.path.join(default_config_dir(), instanz.konfig_datei(profil.konfig_datei))


def konfig_umziehen(profil: "profile.Programmprofil" = None) -> str:
    """Die Altdatei ``config.yaml`` einmalig in ``a5120emu.yaml`` umbenennen.

    Nur für den A5120 Emulator (dem gehörte die Altdatei), nur wenn die neue
    Datei noch fehlt, und nur als UMBENENNUNG — nicht kopiert (sonst lägen zwei
    Stände nebeneinander, und niemand wüsste, welcher gilt) und nicht gelöscht
    ohne Ersatz.  Die Meldung geht ins Protokoll (stdout), wie die übrigen
    ``[config]``-Zeilen.

    Returns:
        Die Meldung, wenn umgezogen wurde, sonst ``""``.
    """
    profil = profil or profile.VORGABE
    if profil.maschine != "a5120":
        return ""
    alt = os.path.join(default_config_dir(), ALTE_KONFIG_DATEI)
    neu = default_config_path(profil)
    if not os.path.isfile(alt) or os.path.exists(neu):
        return ""
    try:
        os.rename(alt, neu)
    except OSError as e:
        meldung = f"[config] Umzug {alt} → {neu} gescheitert: {e}"
        print(meldung)
        return ""
    meldung = f"[config] Konfiguration umgezogen: {alt} → {neu}"
    print(meldung)
    return meldung


def build_config(crt: CRTParams, general: dict, disks: list,
                 window: dict = None, drive_types: list = None,
                 schnittstellen: dict = None, machine: dict = None) -> dict:
    """Assemble the full configuration dict from the live application state.

    ``drive_types`` is the per-slot list of core ``DriveProfile`` names (one per
    K5122 slot, ``"none"`` = empty slot), so the drive-bay configuration is
    restored on the next start.

    ``schnittstellen`` (AP-S7): Einstellung der seriellen Schnittstellen je Name aus
    dem Kern samt ``aktiv``.  ``None`` = Abschnitt weglassen (fehlend heisst beim
    Laden „nicht anfassen"); ein leeres Verzeichnis wird geschrieben.

    ``machine`` (doc/design/22_raf512.md §7.2): ``{"raf": …, "raf_standby": …}``;
    ``None`` = Abschnitt weglassen (beim Laden heisst das: keine RAF).
    """
    from app import version as _version
    data = {
        "version": CONFIG_VERSION,
        # Auskunft, nie Grundlage einer Fallunterscheidung (§7.4).
        "geschrieben_von": _version.fassung(),
        "crt": crt.to_dict(),
        "general": dict(general or {}),
        "drive_types": list(drive_types) if drive_types else [],
        "disks": list(disks or []),
        "window": dict(window or {}),
    }
    if machine is not None:
        data["machine"] = dict(machine)
    if schnittstellen is not None:
        data["schnittstellen"] = dict(schnittstellen)
    return data


def format_version(data: dict) -> int:
    """Formatversion einer gelesenen Konfiguration; fehlt oder unlesbar → 1."""
    v = data.get("version", 1) if isinstance(data, dict) else 1
    return v if isinstance(v, int) and not isinstance(v, bool) and v >= 1 else 1


def ist_neuer_als_dieses_programm(data: dict) -> bool:
    """Stammt die Datei von einem NEUEREN Programm (höhere Formatversion)?"""
    return format_version(data) > CONFIG_VERSION


def migriere(data: dict) -> dict:
    """Die Migrationskette von der gelesenen bis zur eigenen Formatversion.

    Eine höhere gelesene Version bleibt unangetastet (sie wird nur gelesen, so
    gut es geht, und :func:`save_config` schützt die Datei).
    """
    v = format_version(data)
    while v < CONFIG_VERSION:
        schritt = _MIGRATIONEN.get(v)
        if schritt is None:
            raise ValueError(f"keine Migration von Konfigurationsformat {v} auf {v + 1}")
        data = schritt(data)
        v += 1
        data["version"] = v
    return data


def _datei_ist_neuer(path: str) -> bool:
    """Trägt die vorhandene Datei ``path`` eine höhere Formatversion als wir?"""
    try:
        with open(path, "r", encoding="utf-8") as f:
            vorhanden = yaml.safe_load(f)
    except Exception:
        return False                 # fehlt oder defekt: nichts zu schützen
    return ist_neuer_als_dieses_programm(vorhanden)


def save_config(path: str, data: dict, erzwingen: bool = False) -> bool:
    """Write *data* to *path* as YAML, creating parent directories as needed.

    **Schutz vor Überschreiben** (doc/ci_pipeline.md §7.4): trägt die vorhandene
    Datei eine HÖHERE Formatversion als dieses Programm, hat ein neueres sie
    geschrieben — ein Probelauf mit einer alten Fassung darf die Einstellungen
    der neuen nicht zerstören.  Dann wird nichts geschrieben (Autosave wie
    Beenden laufen hier durch).  ``erzwingen`` ist für die ausdrückliche Wahl
    des Anwenders (*Konfiguration speichern unter…*).

    Returns:
        ``True``, wenn geschrieben wurde; ``False``, wenn der Schutz griff.
    """
    if not erzwingen and _datei_ist_neuer(path):
        global _schutz_gemeldet
        if not _schutz_gemeldet:
            _schutz_gemeldet = True
            print(f"[config] {path} stammt von einem neueren Programm "
                  f"(Format > {CONFIG_VERSION}) und wird nicht überschrieben")
        return False
    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, sort_keys=False, allow_unicode=True)
    return True


#: Die Schutzmeldung nur einmal je Lauf ins Protokoll (Autosave schreibt oft).
_schutz_gemeldet = False


def load_config(path: str) -> dict:
    """Read a YAML configuration file and return it as a dict (``{}`` if empty).

    Läuft die Migrationskette bis zur eigenen Formatversion; eine NEUERE Datei
    wird unverändert geliefert (siehe :func:`ist_neuer_als_dieses_programm`).
    """
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)
    if not isinstance(data, dict):
        return {}
    return migriere(data) if data else {}


def standard_konfiguration(profil: "profile.Programmprofil" = None) -> dict:
    """Die mitgelieferte Auslieferungskonfiguration (``{}``, wenn es keine gibt).

    Sie liegt als ``default_config_<maschine>.yaml`` neben dem Formatkatalog
    (:func:`app.paths.default_config_file`) und wird an zwei Stellen gebraucht:
    beim ERSTEN Start, solange es noch keine Konfiguration des Anwenders
    (``a5120emu.yaml``/``k8915emu.yaml``) gibt, und bei *Ansicht ▸ Standard
    zurücksetzen*.

    **Ein Fehlschlag ist kein Grund, den Start abzubrechen**: fehlt oder bricht
    die Datei, kommt ein leeres Verzeichnis zurück und der Emulator bleibt bei
    den im Programm eingebauten Vorgaben (CRTParams(), Tempo 1,0,
    Laufwerke des Profils, Standard-Symbolleiste).  Sie ist eine Beigabe,
    keine Voraussetzung.
    """
    profil = profil or profile.VORGABE
    pfad = paths.default_config_file(profil.vorgabe_datei)
    if pfad is None:
        return {}
    try:
        return load_config(str(pfad))
    except Exception as e:           # defekte YAML-Datei, Leserechte …
        print(f"[config] Vorgabe-Konfiguration {pfad} nicht lesbar: {e}")
        return {}

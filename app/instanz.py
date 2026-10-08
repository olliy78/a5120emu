"""Mehrinstanzbetrieb: mehrere Prozesse desselben Programms nebeneinander (P8000-Mehrplatz).

doc/design/26_p8000emu_oberflaeche.md §8.4.  Zwei Dinge unterscheiden eine Instanz von der anderen:

* **Ein Instanzname** (``--instance NAME`` auf der Kommandozeile oder ``K1520_INSTANZ``) hängt sich an
  den Namen der Konfigurationsdatei (``p8000emu-arbeitsplatz2.yaml``) und an den Fenstertitel.
  Ohne Namen ändert sich nichts — die Dateinamen der Einzelinstanz bleiben, wie sie sind.
  ``--config DATEI`` (``K1520_KONFIG``) legt die Konfigurationsdatei ganz von Hand fest.
* **Eine Sperrdatei neben einem Plattenabbild** (``<abbild>.lock`` mit der Prozesskennung): zwei
  Rechner-Instanzen auf DERSELBEN Platte würden sich gegenseitig überschreiben (der Kern hält die
  Platte im Speicher und schreibt zurück).  Die zweite Instanz bekommt die Platte nicht und sagt es.
  Die Sperre ist kein Zwang des Betriebssystems, sondern ein Schutz vor dem Versehen; eine verwaiste
  Datei (Prozess tot) wird übernommen.

Das Modul importiert kein Qt (``--paths``/``--help`` brauchen es vor PySide6).
"""

from __future__ import annotations

import os
import re
from typing import Optional

ENV_INSTANZ = "K1520_INSTANZ"
ENV_KONFIG = "K1520_KONFIG"


def name() -> str:
    """Der Instanzname (bereinigt: Buchstaben, Ziffern, ``-`` ``_``); ``""`` = Einzelinstanz."""
    roh = os.environ.get(ENV_INSTANZ, "")
    return re.sub(r"[^A-Za-z0-9_-]", "", roh)[:40]


def konfig_datei(basis: str) -> str:
    """``p8000emu.yaml`` → ``p8000emu-NAME.yaml`` (nur mit Instanzname)."""
    n = name()
    if not n:
        return basis
    stamm, endung = os.path.splitext(basis)
    return f"{stamm}-{n}{endung}"


def konfig_pfad_vorgabe() -> str:
    """Von Hand gesetzte Konfigurationsdatei (``--config``), sonst ``""``."""
    return os.environ.get(ENV_KONFIG, "")


def _lebt(pid: int) -> bool:
    if pid <= 0:
        return False
    if os.name == "nt":                      # pragma: no cover - Windows
        import ctypes
        h = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)   # QUERY_LIMITED_INFORMATION
        if h:
            ctypes.windll.kernel32.CloseHandle(h)
            return True
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def sperre_nehmen(pfad: str) -> Optional[int]:
    """Sperrdatei neben *pfad* anlegen.

    Returns:
        ``None``, wenn die Sperre jetzt uns gehört (oder nicht anlegbar ist — dann gibt es
        keinen Schutz, aber auch keinen Fehler); sonst die Prozesskennung der Instanz, der die
        Platte schon gehört.
    """
    sperre = pfad + ".lock"
    eigene = os.getpid()
    for _ in range(2):
        try:
            fd = os.open(sperre, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
        except FileExistsError:
            try:
                with open(sperre, encoding="ascii") as f:
                    pid = int((f.read().strip() or "0"))
            except (OSError, ValueError):
                pid = 0
            if pid == eigene:
                return None
            if _lebt(pid):
                return pid
            try:
                os.unlink(sperre)              # verwaist (Prozess tot): übernehmen
            except OSError:
                return None
            continue
        except OSError:
            return None                        # Ordner nicht beschreibbar: kein Schutz
        with os.fdopen(fd, "w", encoding="ascii") as f:
            f.write(str(eigene))
        return None
    return None


def sperre_loesen(pfad: str) -> None:
    """Die eigene Sperre neben *pfad* entfernen (fremde bleibt unberührt)."""
    sperre = pfad + ".lock"
    try:
        with open(sperre, encoding="ascii") as f:
            if int((f.read().strip() or "0")) != os.getpid():
                return
        os.unlink(sperre)
    except (OSError, ValueError):
        pass

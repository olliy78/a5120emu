"""Alle Aktionen des Emulatorfensters an **einer** Stelle.

Menüleiste und Symbolleiste zeigen dieselbe ``QAction`` — deshalb gibt es sie
genau einmal, mitsamt Kürzel, Symbol und Statustext.  Wer eine Aktion sperrt oder
umbenennt, tut es damit überall; und die einrichtbare Symbolleiste
(:mod:`app.ui.toolbar_config`) kann über :data:`REIHENFOLGE` anbieten, was es
gibt, ohne die Bauanweisung des Fensters zu kennen.  Dasselbe Muster wie beim
DiskTool (`app/disktool/ui/actions.py`).

``erzeuge_aktionen(fenster)`` hängt sie als ``fenster.act_<name>`` an und
verdrahtet jede mit der gleichnamig eingetragenen Methode des Fensters.

**Warum jedes Kürzel Strg+Umschalt trägt.** Die Tastatur gehört dem emulierten
Rechner: ``app/ui/screen_widget.py`` reicht jeden Tastendruck an den K7637
weiter — auch ``Strg+C``, ``Strg+S``, ``Strg+P`` und die Funktionstasten, und
genau die braucht CP/M (``^S`` hält die Ausgabe an, ``^P`` schaltet den Drucker
zu, ``^C`` macht den Warmstart).  Qt wertet ein Kürzel aber VOR dem Widget aus:
was das Fenster für sich beansprucht, kommt beim Gast nie an.  Deshalb liegt
jede Bedienung auf ``Strg+Umschalt+…``; die einzige Ausnahme ist ``F11``
(Vollbild), das der Bildschirm ohnehin abfängt und nicht weiterreicht.
"""

from __future__ import annotations

from typing import List, Tuple

from PySide6.QtGui import QAction, QKeySequence

from app.ui_icons import icon

#: (Name, Beschriftung, Symbol, Kürzel, Statustext, Methode des Fensters, rastend)
#:
#: Die Beschriftung ist die des MENÜS; die Symbolleiste bekommt über :data:`KURZ`
#: ein kürzeres Wort, sonst wird die Leiste breiter als das Fenster.
_SPEC: List[Tuple] = [
    # ── Datei ───────────────────────────────────────────────────────────────
    # „Einlegen"/„Auswerfen" tragen ein Untermenü je bestücktem Laufwerk (das
    # Fenster füllt es in `_disk_menues_bauen`) — ohne Laufwerk wäre die Frage
    # „welches?" unbeantwortbar, und ein Dialog, der sie stellt, wäre ein
    # Umweg um ein Menü herum.
    ("einlegen", "Diskette &einlegen", "open", None,
     "Ein Diskettenabbild (.hfe, .dmk, .img) in eines der Laufwerke einlegen",
     None, False),
    ("auswerfen", "Diskette aus&werfen", "eject", None,
     "Die eingelegte Diskette aus einem Laufwerk nehmen", None, False),
    ("konfig_laden", "Konfiguration &laden…", "config-open", "Ctrl+Shift+O",
     "Eine gespeicherte Konfiguration laden (Bildröhre, Laufwerke, Disketten) — "
     "die Maschine startet danach kalt", "_on_load_config", False),
    ("konfig_speichern", "Konfiguration &speichern…", "config-save", "Ctrl+Shift+S",
     "Die jetzige Einrichtung als YAML-Datei sichern", "_on_save_config", False),
    ("beenden", "&Beenden", None, "Ctrl+Shift+Q", "Den Emulator beenden", "close", False),

    # ── Maschine ────────────────────────────────────────────────────────────
    # Ein RASTENDER Schalter: „ein" ist ein Zustand, kein Vorgang — und der
    # Knopf in der Leiste zeigt ihn dadurch an, ohne eine eigene Anzeige.
    ("power", "Rechner &ausschalten", "power", "Ctrl+Shift+P",
     "Netzschalter — Einschalten ist immer ein KALTSTART (Boot-ROM, alle "
     "Disketten neu eingelegt)", "_on_power_toggle", True),
    ("reset", "&Rückstellen", "reset", "Ctrl+Shift+R",
     "Systemweites /RESET — die Maschine startet neu vom Boot-ROM, bleibt aber an",
     "_on_reset", False),
    # NMI-Taster der K8915-Frontplatte (nur im Profil K8915, siehe NUR_FUER).
    # KEIN Kürzel: selten gebraucht, und jedes Strg+Umschalt+… müsste in die
    # Kürzeltabelle des Handbuchs (ein Vertrag).
    ("nmi", "&NMI-Taster", "nmi", None,
     "NMI-Taster der Frontplatte — K8915: solange das Boot-ROM eingeblendet ist: Lampen "
     "aus, Selbsttest von vorn.  Unter SCPX springt die CPU ins RAM bei 0066H "
     "(wie am Gerät, meist ein Absturz).  P8000: NMI-Taste (U880 bzw. — bei "
     "angeschalteter 16-Bit-Karte — U8001, dort „Press NMI“)", "_on_nmi", False),
    # Zwischenstand der Maschine (nur P8000, P8KS).  KEIN Kürzel (die Kürzeltabelle des
    # Handbuchs ist ein Vertrag); zuverlässig nur am Prompt, siehe Handbuch.
    ("stand_speichern", "Zwischenstand &sichern…", "config-save", None,
     "Den Zustand der laufenden Maschine in eine Datei sichern (P8KS) — zuverlässig nur am "
     "Prompt; Disketten und Platte müssen beim Laden dieselben sein", "_stand_speichern", False),
    ("stand_laden", "Zwischenstand &laden…", "config-open", None,
     "Einen gesicherten Zwischenstand laden — nur bei gleicher Konfiguration und mit "
     "denselben Disketten/derselben Platte", "_stand_laden", False),

    # ── EPROMmer (nur PRG 710, AP-P7c) ──────────────────────────────────────
    # Der virtuelle Sockel der ATP 590068 (doc/prg710/eprommer.md).  KEIN Kürzel:
    # jedes Strg+Umschalt+… müsste in die Kürzeltabelle des Handbuchs (ein Vertrag).
    ("eprom_einlegen", "PROM-Abbild &einlegen…", "open", None,
     "Ein PROM-Abbild (rohes .bin; bis 1 KB = U555, bis 2 KB = U2716) in den Sockel "
     "des EPROMmers stecken", "_eprom_einlegen", False),
    # Das Untermenü (U555 / U2716) füllt das Fenster.
    ("eprom_leer", "&Leeres PROM einlegen", None, None,
     "Ein gelöschtes PROM (alle Zellen FFH, ohne Datei) in den Sockel stecken",
     None, False),
    ("eprom_speichern", "PROM-Abbild &speichern unter…", "config-save", None,
     "Den Inhalt des gesteckten PROM als rohes .bin sichern", "_eprom_speichern", False),
    ("eprom_loeschen", "PROM &UV-löschen", None, None,
     "Alle Zellen auf FFH setzen — wie im UV-Löschgerät (die Software kann nicht löschen)",
     "_eprom_loeschen", False),
    ("eprom_entnehmen", "PROM e&ntnehmen", "eject", None,
     "Das PROM aus dem Sockel nehmen (ungespeicherte Änderungen gehen verloren)",
     "_eprom_entnehmen", False),

    # ── Ansicht ─────────────────────────────────────────────────────────────
    ("vollbild", "&Vollbild", "fullscreen", "F11",
     "Nur die Bildröhre, ohne Menü, Leisten und Kästen (Esc beendet es wieder)",
     "toggle_fullscreen", True),
    ("leiste_einrichten", "Symbolleiste &einrichten…", None, None,
     "Welche Schaltflächen die Symbolleiste zeigt und in welcher Reihenfolge",
     "_leiste_einrichten", False),
    # Kein Kürzel: ein Griff, den man selten und mit Bedacht tut — und jedes
    # weitere Strg+Umschalt+… ist eines mehr, das im Handbuch stehen muss.
    ("standard", "&Standard zurücksetzen", "reset-view", None,
     "Bildröhre, Tempo, Laufwerke, Fenster, Kästen und Symbolleiste auf die "
     "Auslieferung zurücksetzen — überschreibt die gespeicherte Konfiguration",
     "_standard_zuruecksetzen", False),

    # ── Werkzeuge ───────────────────────────────────────────────────────────
    # Die Nachbarprogramme derselben Installation.  Sie fassen dieselben
    # Disketten an, also gehören sie erreichbar — und zwar aus dem laufenden
    # Programm heraus, nicht nur aus dem Startmenü.
    ("disktool", "&k1520DiskTool starten", None, None,
     "Das Diskettenwerkzeug öffnen — Dateien von einer Diskette holen und auf "
     "sie schreiben; es läuft neben dem Emulator weiter",
     "_disktool_starten", False),
    # Der jeweils andere Emulator — dasselbe Programm mit dem anderen Profil.
    ("k8915emu", "K&8915 Emulator starten", None, None,
     "Den K8915 Emulator öffnen — ein eigenes Programm mit eigener "
     "Konfiguration; es läuft neben diesem weiter",
     "_k8915emu_starten", False),
    ("a5120emu", "&A5120 Emulator starten", None, None,
     "Den A5120 Emulator öffnen — ein eigenes Programm mit eigener "
     "Konfiguration; es läuft neben diesem weiter",
     "_a5120emu_starten", False),
    ("prg710emu", "&PRG710 Emulator starten", None, None,
     "Den PRG710 Emulator öffnen (PRG 710 / PRG 710-1) — ein eigenes Programm "
     "mit eigener Konfiguration; es läuft neben diesem weiter",
     "_prg710emu_starten", False),
    ("pc1715emu", "PC&1715 Emulator starten", None, None,
     "Den PC1715 Emulator öffnen (PC 1715) — ein eigenes Programm "
     "mit eigener Konfiguration; es läuft neben diesem weiter",
     "_pc1715emu_starten", False),
    ("p8000emu", "P&8000 Emulator starten", None, None,
     "Den P8000 Emulator öffnen (P8000) — ein eigenes Programm "
     "mit eigener Konfiguration; es läuft neben diesem weiter",
     "_p8000emu_starten", False),
    ("konsole", "&Werkzeugkonsole öffnen", None, None,
     "Ein Konsolenfenster, in dem der Debugger k1520dbg und die Kommandozeile "
     "des DiskTool ohne Pfadangabe laufen — es steht im Diskettenordner",
     "_konsole_starten", False),

    # ── Hilfe ───────────────────────────────────────────────────────────────
    ("hilfe", "&Handbuch…", "help", "Ctrl+Shift+H",
     "Bedienung, Begriffe und Tastenkürzel", "open_help", False),
    ("ueber", "Ü&ber a5120emu…", None, None, "Fassung und Herkunft",
     "_on_about", False),
]

#: Beschriftung in der Symbolleiste (``QAction.setIconText``).
KURZ = {
    "einlegen": "Einlegen",
    "auswerfen": "Auswerfen",
    "konfig_laden": "Laden",
    "konfig_speichern": "Sichern",
    "power": "Power",
    "reset": "Reset",
    "nmi": "NMI",
    "k8915emu": "K8915",
    "a5120emu": "A5120",
    "prg710emu": "PRG710",
    "pc1715emu": "PC1715",
    "p8000emu": "P8000",
    "stand_speichern": "Stand sichern",
    "stand_laden": "Stand laden",
    "eprom_einlegen": "PROM",
    "eprom_leer": "Leer",
    "eprom_speichern": "Sichern",
    "eprom_loeschen": "UV",
    "eprom_entnehmen": "Entnehmen",
    "vollbild": "Vollbild",
    "standard": "Standard",
    "disktool": "DiskTool",
    "konsole": "Konsole",
    "hilfe": "Hilfe",
}

#: Name im Einrichtdialog der Symbolleiste, wo die gewöhnliche Beschriftung
#: nicht taugt — der Netzschalter heisst mal so und mal so.
DIALOG_NAME = {"power": "Rechner ein-/ausschalten"}

#: Beschriftung des Netzschalters je Zustand — die Leiste zeigt immer „Power",
#: das Menü sagt, was ein Klick TUT.
POWER_TEXT = {True: "Rechner &ausschalten", False: "Rechner &einschalten"}

#: Aktionen, die nicht jedes Programm hat (Name → Maschinen der Profile,
#: `app/profil.py`, die sie haben).  Alle übrigen haben alle.
NUR_FUER = {
    "nmi": ("k8915", "p8000"),           # NMI-Taster der Frontplatte
    "stand_speichern": ("p8000",),       # Zwischenstand (P8KS)
    "stand_laden": ("p8000",),
    "k8915emu": ("a5120", "prg710", "pc1715", "p8000"),     # die jeweils ANDEREN Emulatoren
    "a5120emu": ("k8915", "prg710", "pc1715", "p8000"),
    "prg710emu": ("a5120", "k8915", "pc1715", "p8000"),
    "pc1715emu": ("a5120", "k8915", "prg710", "p8000"),
    "p8000emu": ("a5120", "k8915", "prg710", "pc1715"),
    # EPROMmer (Kasten und Bedienung) — nur der PRG 710 hat einen.
    "eprom_einlegen": ("prg710",),
    "eprom_leer": ("prg710",),
    "eprom_speichern": ("prg710",),
    "eprom_loeschen": ("prg710",),
    "eprom_entnehmen": ("prg710",),
    "dock_eprom": ("prg710",),
}

#: Die Bedienung des EPROMmer-Sockels, in der Reihenfolge von Kasten und Menü.
EPROM: List[str] = ["eprom_einlegen", "eprom_leer", "eprom_speichern",
                    "eprom_loeschen", "eprom_entnehmen"]

#: Was die Symbolleiste aufnehmen kann, in der Reihenfolge des Einrichtdialogs.
#: ``None`` ist ein Trennstrich zwischen zwei Gruppen.  Die Namen der
#: Kastenschalter (``dock_*``) gehören keinem Eintrag in :data:`_SPEC` — sie
#: kommen von Qt selbst (``QDockWidget.toggleViewAction``) und werden vom
#: Fenster unter diesen Namen abgelegt.
REIHENFOLGE: List = [
    "konfig_laden", "konfig_speichern",
    None,
    "power", "reset", "nmi",
    None,
    "einlegen", "auswerfen",
    None,
    "stand_speichern", "stand_laden",
    None,
    "dock_drives", "dock_settings", "dock_screen", "dock_keyboard", "dock_eprom",
    None,
    "eprom_einlegen", "eprom_entnehmen",
    None,
    "vollbild", "standard", "hilfe",
]

#: Was beim ersten Start darin steht — genau die Wege, die man täglich geht.
#: Alles Übrige lässt sich über *Ansicht ▸ Symbolleiste einrichten* dazuholen.
STANDARD: List = [
    "konfig_laden", "konfig_speichern",
    None,
    "power", "reset",
    None,
    "dock_settings", "dock_drives", "dock_keyboard",
    None,
    "hilfe",
]


#: Die Standardleiste des K8915: dieselbe, mit dem NMI-Taster neben Reset — wie
#: an der Frontplatte des Geräts.
STANDARD_K8915: List = [
    "konfig_laden", "konfig_speichern",
    None,
    "power", "reset", "nmi",
    None,
    "dock_settings", "dock_drives", "dock_keyboard",
    None,
    "hilfe",
]


def gibt_es(name: str, maschine: str = "a5120") -> bool:
    """Hat das Programm der Maschine *maschine* die Aktion *name*?"""
    erlaubt = NUR_FUER.get(name)
    return erlaubt is None or maschine in erlaubt


def reihenfolge(maschine: str = "a5120") -> List:
    """:data:`REIHENFOLGE` ohne die Aktionen, die es in diesem Programm nicht gibt."""
    return [n for n in REIHENFOLGE if n is None or gibt_es(n, maschine)]


def standard(maschine: str = "a5120") -> List:
    """Inhalt der Symbolleiste beim ersten Start (Programmprofil *maschine*)."""
    return list(STANDARD_K8915 if maschine in ("k8915", "p8000") else STANDARD)


def erzeuge_aktionen(fenster, maschine: str = "a5120") -> None:
    """Die Aktionen anlegen, verdrahten und als ``fenster.act_<name>`` ablegen.

    Aktionen aus :data:`NUR_FUER`, die nicht zur Maschine *maschine* gehören,
    entstehen gar nicht erst — der A5120 hat keinen NMI-Taster, weder im Menü
    noch in der Leiste noch im Einrichtdialog.
    """
    for name, text, bild, kuerzel, tipp, methode, rastend in _SPEC:
        if not gibt_es(name, maschine):
            continue
        a = QAction(text, fenster)
        if bild:
            a.setIcon(icon(bild))
        if kuerzel:
            a.setShortcut(QKeySequence(kuerzel))
        a.setStatusTip(tipp)
        a.setToolTip(tipp)
        a.setIconText(KURZ.get(name, text.replace("&", "").rstrip("…")))
        a.setCheckable(rastend)
        # Ohne `addAction` gilt ein Kürzel nur, solange die Aktion in einem
        # SICHTBAREN Menü oder einer sichtbaren Leiste hängt — im Vollbild (beide
        # verborgen) oder nach dem Ausräumen der Leiste wäre es still weg.
        fenster.addAction(a)
        if methode is not None:
            ziel = getattr(fenster, methode)
            if rastend:
                a.toggled.connect(ziel)
            else:
                # `triggered` reicht ein `checked` durch, das die Methoden nicht wollen.
                a.triggered.connect(lambda *_, f=ziel: f())
        setattr(fenster, f"act_{name}", a)

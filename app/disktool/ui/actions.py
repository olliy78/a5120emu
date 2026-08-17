"""Alle Aktionen des Hauptfensters an **einer** Stelle.

Menüleiste, Symbolleiste und die beiden Kontextmenüs zeigen dieselbe ``QAction`` —
deshalb gibt es sie genau einmal, mitsamt Kürzel, Symbol und Statustext.  Wer eine
Aktion sperrt, sperrt sie damit überall; das ist der ganze Zweck der Umstellung
weg von den früheren Schaltflächen (doc/design/13_k1520disktool.md §20.3).

``erzeuge_aktionen(fenster)`` hängt sie als ``fenster.act_<name>`` an und
verdrahtet jede mit der gleichnamig eingetragenen Methode des Fensters.
"""

from __future__ import annotations

from typing import List, Tuple

from PySide6.QtGui import QAction, QActionGroup, QKeySequence

from app.disktool.ui.icons import icon

#: (Name, Beschriftung, Symbol, Kürzel, Statustext, Methode des Fensters, rastend)
#:
#: Die Beschriftung ist die des MENÜS.  Die Symbolleiste bekommt über
#: :data:`KURZ` ein kürzeres Wort — sonst wird die Leiste so breit, dass sie
#: schon bei 1150 px in den Überlauf (») kippt und die letzten Knöpfe verbirgt.
_SPEC: List[Tuple] = [
    # ── Datei ───────────────────────────────────────────────────────────────
    ("oeffnen", "Abbild &öffnen…", "open", "Ctrl+O",
     "Ein Diskettenabbild öffnen (.hfe, .dmk, .img)", "_oeffnen_dialog", False),
    ("neu", "&Neue Diskette…", "disk-new", "Ctrl+N",
     "Eine Diskette anlegen — wahlweise bootfähig", "_neu_dialog", False),
    ("speichern", "&Speichern", "save", "Ctrl+S",
     "Die Änderungen in die Abbilddatei schreiben", "save", False),
    ("speichern_unter", "Speichern &unter…", "save-as", "Ctrl+Shift+S",
     "Unter neuem Namen oder in einem anderen Container speichern und dort "
     "weiterarbeiten", "_speichern_unter_dialog", False),
    ("archivieren", "&Archivieren…", "archive", "Ctrl+Shift+A",
     "Abbild (.hfe), alle Dateien und ein Inhaltsverzeichnis in eine .zip",
     "_archivieren_dialog", False),
    ("schliessen", "S&chließen", None, "Ctrl+W",
     "Die Diskette schließen; das Fenster bleibt offen", "close_disk", False),
    ("beenden", "&Beenden", None, "Ctrl+Q", "k1520DiskTool beenden", "close", False),

    # ── Bearbeiten (arbeitet auf der Auswahl) ───────────────────────────────
    ("alles_waehlen", "&Alles auswählen", None, "Ctrl+A",
     "Alle Einträge der aktiven Liste auswählen", "_alles_waehlen", False),
    ("holen", "In den Ordner &holen", "out", "Ctrl+Right",
     "Die ausgewählten Dateien von der Diskette in den Ordner holen",
     "_extrahieren_auswahl", False),
    ("schreiben", "Auf die Diskette &schreiben", "in", "Ctrl+Left",
     "Die ausgewählten Dateien aus dem Ordner auf die Diskette schreiben",
     "_einfuegen_auswahl", False),
    ("loeschen", "&Löschen", "delete", "Del",
     "Die ausgewählten Dateien von der Diskette löschen", "_loeschen_auswahl", False),
    ("eigenschaften", "&Eigenschaften…", "properties", "Alt+Return",
     "Die Dateiangaben ansehen und ändern", "_eigenschaften_auswahl", False),

    # ── Diskette ────────────────────────────────────────────────────────────
    ("schreibschutz", "Schr&eibschutz", "lock", "Ctrl+R",
     "Solange er gesetzt ist, kann die Diskette nicht verändert werden "
     "(Symbol und Beschriftung zeigen den Zustand: 🔒 R/O ↔ 🔓 R/W)",
     "_schreibschutz_umgeschaltet", True),
    ("alles_raus", "Alles e&xtrahieren…", "out-all", None,
     "Den ganzen Disketteninhalt in einen Ordner holen", "_alles_extrahieren", False),
    ("alles_rein", "Alles ei&nfügen…", "in-all", None,
     "Einen ganzen Ordner auf die Diskette schreiben", "_alles_einfuegen", False),
    # Die echte Diskette: laden und überschreiben stehen NEBENEINANDER — es sind
    # die beiden Richtungen desselben Wegs, und der Bediener sucht sie zusammen.
    ("physisch", "&Physische Diskette laden…", "disk-physical", "Ctrl+Shift+O",
     "Eine ECHTE Diskette in einem echten Laufwerk am Greaseweazle öffnen — "
     "das Öffnen misst eine Stichprobe der Spuren und dauert etwa zehn Sekunden",
     "_physisch_dialog", False),
    ("physisch_schreiben", "Physische Diskette &überschreiben…", "disk-physical-write",
     None,
     "Das geöffnete Speicherabbild auf eine echte Diskette schreiben — "
     "ihr bisheriger Inhalt geht dabei verloren",
     "_physisch_schreiben_dialog", False),

    # Einer der beiden Auswege aus einer Schadstelle (14_physische_diskette.md
    # §7.2; der andere ist, das Abbild in eine Datei zu sichern): neue Diskette
    # einlegen, alles noch einmal wegschreiben.  Nur sichtbar, solange ein
    # echtes Laufwerk offen ist — an einer Datei ergibt es keinen Sinn.
    ("neu_beschreiben", "Diskette neu &beschreiben…", None, None,
     "Das vollständige Speicherabbild noch einmal auf die eingelegte Diskette "
     "schreiben — für eine frische, fehlerfreie.  Nur bereits gelesene Spuren "
     "werden geschrieben", "_neu_beschreiben", False),
    # Zurechtschneiden des SPEICHERABBILDS (§12.6) — nicht der Diskette.  Beides
    # löst vom Laufwerk: danach stimmt die Spurnummer nicht mehr mit der
    # Kopfposition überein, ein Rückschreiben ginge auf die falschen Zylinder.
    ("gerade_spuren", "&Ungerade Spuren entfernen", None, None,
     "Jede zweite Spur wegwerfen — aus einer im Doppelschritt beschriebenen, aber "
     "einfachschrittig gelesenen Diskette wird das, was ein 40-Spur-Laufwerk sieht",
     "_gerade_spuren", False),
    ("seite1_weg", "&Seite 1 entfernen", None, None,
     "Die Rückseite aus dem Speicherabbild werfen — bei einer einseitig "
     "beschriebenen Diskette steht dort nur Altbestand",
     "_seite1_weg", False),
    ("bootabbild", "&Bootabbild sichern…", "boot", None,
     "Die Systemspuren als .bin sichern — damit lässt sich später eine neue "
     "Diskette bootfähig anlegen", "_bootabbild_sichern_dialog", False),
    ("diskeditor", "&Diskeditor…", "disk-editor", "Ctrl+E",
     "Die Diskette Spur für Spur und Sektor für Sektor ansehen und bearbeiten",
     "open_disk_editor", False),
    ("angaben", "Disketten&angaben…", None, None,
     "Format, Geometrie, Erkennung und Belegung im Einzelnen", "_angaben_dialog", False),

    # ── Übertragung ─────────────────────────────────────────────────────────
    ("ordner", "&Zielordner wählen…", "folder", None,
     "Den Linux-Ordner der rechten Hälfte wählen", "_ordner_dialog", False),

    # ── Ansicht ─────────────────────────────────────────────────────────────
    ("aktualisieren", "&Aktualisieren", "refresh", "F5",
     "Verzeichnis und Ordner neu einlesen", "_aktualisieren", False),

    # ── Hilfe ───────────────────────────────────────────────────────────────
    ("hilfe", "&Handbuch…", None, "F1", "Bedienung, Begriffe und Tastenkürzel",
     "open_help", False),
    ("ueber", "Ü&ber k1520DiskTool…", None, None, "Fassung und Herkunft",
     "_ueber_dialog", False),
]


#: Beschriftung in der Symbolleiste (``QAction.setIconText``).
KURZ = {'oeffnen': 'Öffnen',
    'neu': 'Neu',
    'physisch': 'Diskette laden',
    'physisch_schreiben': 'Diskette schreiben',
    'neu_beschreiben': 'Neu schreiben',
    'speichern': 'Speichern',
    'speichern_unter': 'Unter…',
    'archivieren': 'Archiv',
    'schliessen': 'Schließen',
    'beenden': 'Beenden',
    'alles_waehlen': 'Alles',
    'holen': 'Holen',
    'schreiben': 'Schreiben',
    'loeschen': 'Löschen',
    'eigenschaften': 'Angaben',
    'schreibschutz': 'R/O',
    'alles_raus': 'Alles holen',
    'alles_rein': 'Alles einfügen',
    'gerade_spuren': 'Ungerade weg',
    'seite1_weg': 'Seite 1 weg',
    'bootabbild': 'Bootabbild',
    'diskeditor': 'Diskeditor',
    'angaben': 'Diskette',
    'ordner': 'Ordner',
    'aktualisieren': 'Neu lesen',
    'hilfe': 'Handbuch',
    'ueber': 'Über'}


def erzeuge_aktionen(fenster) -> None:
    """Die Aktionen anlegen, verdrahten und als ``fenster.act_<name>`` ablegen."""
    for name, text, bild, kuerzel, tipp, methode, rastend in _SPEC:
        a = QAction(text, fenster)
        if bild:
            a.setIcon(icon(bild))
        if kuerzel:
            a.setShortcut(QKeySequence(kuerzel))
        a.setStatusTip(tipp)
        a.setToolTip(tipp)
        if name in KURZ:
            a.setIconText(KURZ[name])
        a.setCheckable(rastend)
        ziel = getattr(fenster, methode)
        if rastend:
            a.toggled.connect(ziel)
        else:
            # `triggered` reicht ein `checked` durch, das die Methoden nicht wollen.
            a.triggered.connect(lambda *_, f=ziel: f())
        setattr(fenster, f"act_{name}", a)

    _uebertragungsmodus(fenster)


def _uebertragungsmodus(fenster) -> None:
    """Binär/Text als Radiogruppe — ein Modus, zwei sich ausschließende Punkte.

    Er verändert die **Bytes** (CR LF ↔ LF); deshalb steht er nicht nur im Menü,
    sondern dauerhaft rechts in der Statuszeile.
    """
    fenster.act_binaer = QAction("&Binär", fenster)
    fenster.act_text = QAction("&Text (CR LF ↔ LF)", fenster)
    gruppe = QActionGroup(fenster)
    gruppe.setExclusive(True)
    for a, tipp in ((fenster.act_binaer, "Dateien Byte für Byte übertragen"),
                    (fenster.act_text, "Zeilenenden beim Übertragen umsetzen")):
        a.setCheckable(True)
        a.setStatusTip(tipp)
        gruppe.addAction(a)
    fenster.act_binaer.setChecked(True)
    fenster.gruppe_modus = gruppe

"""Die Angaben zu einer Datei erfragen, die keine mitbringt.

Eine Linux-Datei trägt nur Bytes.  Ein UDOS-Kopfsektor trägt darüber hinaus
**Typ**, **Eigenschaften**, **Satzlänge** und — bei einem Programm — **ENTRY**,
die **Speichersegmente** und die **Speicheranforderung** LOW/HIGH/STACK.  Ohne sie
kann das Werkzeug nicht wissen, was für eine Datei es einfügt; was dabei entstünde,
wäre eine Datei, die nicht läuft (`doc/bug_disktool_Programmdatei.md` §2.3).

Deshalb wird gefragt statt geraten — aber nur bei der **UDOS-Familie**.  Bei CP/M
geht dieser Dialog von selbst nie auf: dort sind Nutzerbereich 0 und „keine
Attribute" kein Notbehelf, sondern der Normalfall, den auch das echte CP/M erzeugt
(§2.4).

Die eigentliche Leistung des Dialogs ist das **Abblenden**: bei Typ ``A`` oder
``B`` sind ENTRY, Segmente und LOW/HIGH/STACK kein Anwenderinhalt, sondern schlicht
unbelegt — dort etwas einzutragen erzeugt einen Kopfsektor, den es so auf keiner
echten Diskette gibt.  Die Felder werden beim Wechsel des Typs abgeblendet **und
auf 0 gesetzt**, nicht bloß ignoriert.

Herausgegeben wird kein Feldsalat, sondern eine fertige Angabendatei
(:meth:`FileinfoDialog.schreibe` → :func:`app.disktool.fileinfo.schreibe_fileinfo`)
in derselben Zeilenform wie ein ``.fileinfo``.  So gibt es für die
Kopfsektorangaben genau **eine** Zeilenform und **einen** Leser — der im Kern —
statt zwanzig Feldern durch die C-ABI.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QGroupBox,
    QHBoxLayout, QLabel, QLineEdit, QMessageBox, QVBoxLayout, QWidget,
)

from app.disktool.fileinfo import schreibe_fileinfo
from app.disktool.ui.properties_dialog import (
    UDOS_EIGENSCHAFTEN, _hex16, _parse_hex,
)

#: Auswahl für den Dateityp.  ``D`` fehlt mit Absicht: die Verzeichnisdatei ist
#: Dateisystemstruktur, keine Nutzdatei — sie lässt sich nicht einfügen.
UDOS_TYPEN_EINGABE = [
    ("A",  "A — ASCII (Textdatei)"),
    ("B",  "B — BINARY"),
    ("P",  "P — PROCEDURE (ausführbares Programm)"),
] + [(f"P{n}", f"P{n} — Procedure Untertyp {n}") for n in range(1, 16)]

#: Satzlängen, die UDOS kennt (Vielfache von 128, doc/udos_diskettenformat.md §6).
SATZLAENGEN = [128, 256, 512, 1024]

#: Typen, bei denen ENTRY, Segmente und Speicheranforderung überhaupt belegt sind.
PROGRAMMTYPEN = {"P"} | {f"P{n}" for n in range(1, 16)}


def typ_vorschlag(pfad) -> str:
    """Ein *Vorschlag* aus dem Augenschein — kein Automatismus.

    Dieselbe Haltung wie bei den Namensresten der Wiederherstellung: das Werkzeug
    sagt, was es vermutet, und der Anwender entscheidet.  Endet die Datei auf
    ``.COM``/``.OBJ`` oder beginnt sie mit einem Z80-Einsprungmuster
    (``C3`` = JP, ``31`` = LD SP, ``F3`` = DI), ist ``P`` wahrscheinlich; sonst
    ``A`` bei überwiegend druckbarem Inhalt und ``B`` sonst.
    """
    p = Path(pfad)
    try:
        kopf = p.read_bytes()[:512]
    except OSError:
        return "B"
    if p.suffix.upper() in (".COM", ".OBJ"):
        return "P"
    if kopf[:1] in (b"\xc3", b"\x31", b"\xf3"):
        return "P"
    if not kopf:
        return "A"
    druckbar = sum(1 for b in kopf if 32 <= b < 127 or b in (9, 10, 13, 26))
    return "A" if druckbar >= len(kopf) * 0.95 else "B"


class FileinfoDialog(QDialog):
    """Kopfsektorangaben zu **einer** Datei erfragen (UDOS-Familie).

    Args:
        pfad: die einzufügende Datei — Name und Typvorschlag kommen daher.
        mehrere: sind noch weitere Dateien in der Warteschlange?  Dann gibt es
            „Für alle übernehmen"; sonst stünde der Anwender bei einem Ordner mit
            dreißig Dateien dreißigmal vor demselben Fenster.
    """

    def __init__(self, pfad, mehrere: bool = False, parent=None):
        super().__init__(parent)
        self.pfad = Path(pfad)
        self.setWindowTitle(f"Angaben zu {self.pfad.name}")
        self.setMinimumWidth(560)
        #: „Für alle übernehmen" angekreuzt?  Liest der Aufrufer nach `exec()`.
        self.fuer_alle = False

        lay = QVBoxLayout(self)
        lay.addWidget(self._erklaerung())
        lay.addWidget(self._typ_teil())
        self.kasten_programm = self._programm_teil()
        lay.addWidget(self.kasten_programm)
        lay.addWidget(self._datum_teil())

        self.f_alle = QCheckBox("Für alle weiteren Dateien übernehmen")
        self.f_alle.setToolTip(
            "Dieselben Angaben für jede weitere Datei dieses Vorgangs verwenden — "
            "ohne diesen Haken kommt das Fenster für jede Datei erneut.")
        self.f_alle.setVisible(mehrere)
        lay.addWidget(self.f_alle)

        self.f_merken = QCheckBox("Angaben als .fileinfo neben der Datei speichern")
        self.f_merken.setChecked(True)
        self.f_merken.setToolTip(
            "Was hier eingetippt wurde, soll beim nächsten Mal nicht wieder "
            "eingetippt werden müssen: die Angaben landen als "
            f"„{self.pfad.name}.fileinfo“ neben der Datei und werden künftig von "
            "selbst gelesen.")
        lay.addWidget(self.f_merken)

        knoepfe = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        knoepfe.button(QDialogButtonBox.Ok).setText("Einfügen")
        knoepfe.accepted.connect(self._pruefen_und_schliessen)
        knoepfe.rejected.connect(self.reject)
        lay.addWidget(knoepfe)

        self._typ_geaendert()

    # ════════════════════════════════════════════════════════════════════════
    # Aufbau
    # ════════════════════════════════════════════════════════════════════════

    def _erklaerung(self) -> QLabel:
        t = QLabel(
            f"Zu „{self.pfad.name}“ gibt es keine Angaben — weder ein "
            f"„{self.pfad.name}.fileinfo“ noch ein Beiblatt im Ordner.  UDOS braucht "
            "sie, um die Datei zu laden; ohne sie entstünde eine Datei, die nicht "
            "läuft.")
        t.setWordWrap(True)
        return t

    def _typ_teil(self) -> QWidget:
        kasten = QGroupBox("Typ und Eigenschaften")
        form = QFormLayout(kasten)

        self.f_typ = QComboBox()
        for kurz, text in UDOS_TYPEN_EINGABE:
            self.f_typ.addItem(text, kurz)
        i = self.f_typ.findData(typ_vorschlag(self.pfad))
        self.f_typ.setCurrentIndex(i if i >= 0 else 0)
        self.f_typ.currentIndexChanged.connect(self._typ_geaendert)
        self.f_typ.setToolTip(
            "Vorgeschlagen ist, was der Augenschein hergibt (Endung, Einsprung"
            "muster, druckbarer Inhalt) — ein Vorschlag, kein Automatismus.")
        form.addRow("Dateityp", self.f_typ)

        self.f_eig = {}
        spalte = QVBoxLayout()
        for kurz, text in UDOS_EIGENSCHAFTEN:
            k = QCheckBox(text)
            self.f_eig[kurz] = k
            spalte.addWidget(k)
        huelle = QWidget()
        huelle.setLayout(spalte)
        form.addRow("Eigenschaften", huelle)

        self.f_satz = QComboBox()
        for n in SATZLAENGEN:
            self.f_satz.addItem(f"{n} Byte", n)
        self.f_satz.setToolTip(
            "Die Zuteilungseinheit von UDOS.  Ein Satz belegt Satzlänge/128 "
            "aufeinanderfolgende Sektoren EINER Spur.")
        form.addRow("Satzlänge", self.f_satz)

        self.f_block = QComboBox()
        self.f_block.addItem("wie Satzlänge", -1)
        self.f_block.addItem("0", 0)
        for n in SATZLAENGEN:
            self.f_block.addItem(str(n), n)
        self.f_block.setToolTip(
            "Kopfsektor Offset 17.  0 ist ein GÜLTIGER Wert (bei Satzlänge "
            "256/512) — deshalb steht er hier zur Wahl und ist nicht „leer“.")
        form.addRow("Zweite Längenangabe", self.f_block)
        return kasten

    def _programm_teil(self) -> QWidget:
        kasten = QGroupBox("Laden und Speicher (nur bei Typ P/P1…)")
        form = QFormLayout(kasten)

        self.f_entry = QLineEdit("0000")
        self.f_entry.setToolTip("ENTRY — Einsprungadresse (hex).")
        form.addRow("Einsprung (ENTRY)", self.f_entry)

        self.f_segs = QLineEdit("")
        self.f_segs.setPlaceholderText("z. B.  4000+03FE  8442+0026")
        self.f_segs.setToolTip(
            "Alle Speichersegmente als ANFANG+LÄNGE (hex), durch Leerzeichen oder "
            "Komma getrennt.  Die Länge ist NICHT die Dateigröße.")
        form.addRow("Speichersegmente", self.f_segs)

        self.f_low = QLineEdit("0000")
        self.f_high = QLineEdit("0000")
        self.f_stack = QLineEdit("0080")
        zeile = QHBoxLayout()
        for beschriftung, feld in (("LOW", self.f_low), ("HIGH", self.f_high),
                                   ("STACK", self.f_stack)):
            zeile.addWidget(QLabel(beschriftung))
            zeile.addWidget(feld, 1)
        self.w_mem = QWidget()
        self.w_mem.setLayout(zeile)
        self.w_mem.setToolTip(
            "Was der Lader zuteilen lässt (Kopfsektor 122/124/126, hex).  Stehen "
            "dort FFFF, weist UDOS die Datei mit MEMORY PROTECT VIOLATION ab.")
        form.addRow("Speicher", self.w_mem)

        self.f_zusatz = QLineEdit("00000000")
        self.f_zusatz.setToolTip("Kopfsektor 44…47 — Bedeutung offen (hex).")
        form.addRow("Zusatz", self.f_zusatz)
        return kasten

    def _datum_teil(self) -> QWidget:
        kasten = QGroupBox("Vermerke")
        form = QFormLayout(kasten)
        self.f_erstellt = QLineEdit("")
        self.f_erstellt.setMaxLength(6)
        self.f_erstellt.setToolTip(
            "6 Zeichen: Datum „JJMMTT“ ODER ein Versionstext wie „V 4.3 “.  Leer = "
            "heutiges Datum.")
        form.addRow("Erstellt", self.f_erstellt)
        self.f_geaendert = QLineEdit("")
        self.f_geaendert.setMaxLength(6)
        self.f_geaendert.setToolTip("Datum der letzten Änderung, „JJMMTT“; leer = heute.")
        form.addRow("Geändert", self.f_geaendert)
        return kasten

    # ════════════════════════════════════════════════════════════════════════
    # Verhalten
    # ════════════════════════════════════════════════════════════════════════

    @property
    def typ(self) -> str:
        return self.f_typ.currentData()

    def _typ_geaendert(self, *_) -> None:
        """ENTRY/Segmente/Speicher abblenden **und leeren**, wo sie unbelegt sind.

        Bei Typ ``A``/``B`` steht an diesen Stellen des Kopfsektors kein Feld,
        sondern Anwenderinhalt bzw. nichts.  Etwas stehenzulassen und beim Schreiben
        zu ignorieren wäre die halbe Lösung — der Anwender sähe Werte, die nicht
        gelten, und beim nächsten Typwechsel gälten sie plötzlich doch.
        """
        an = self.typ in PROGRAMMTYPEN
        self.kasten_programm.setEnabled(an)
        if not an:
            self.f_entry.setText("0000")
            self.f_segs.setText("")
            self.f_low.setText("0000")
            self.f_high.setText("0000")
            self.f_stack.setText("0000")
            self.f_zusatz.setText("00000000")

    def _pruefen_und_schliessen(self) -> None:
        try:
            self.angaben()
        except ValueError as e:
            QMessageBox.warning(self, "Angaben", str(e))
            return
        self.fuer_alle = self.f_alle.isChecked()
        self.accept()

    # ════════════════════════════════════════════════════════════════════════
    # Ergebnis
    # ════════════════════════════════════════════════════════════════════════

    def angaben(self) -> dict:
        """Die Eingaben als Schlüssel/Wert-Tabelle — wirft ``ValueError``."""
        satz = int(self.f_satz.currentData())
        block = int(self.f_block.currentData())
        segs = self.f_segs.text().replace(",", " ").split()
        for s in segs:
            if "+" not in s:
                raise ValueError(f"Speichersegment „{s}“: erwartet ANFANG+LÄNGE, "
                                 "z. B. 4000+03FE")
            _parse_hex(s.split("+", 1)[0], "Segmentanfang")
            _parse_hex(s.split("+", 1)[1], "Segmentlänge")
        eig = "".join(k for k, w in self.f_eig.items() if w.isChecked())
        return {
            "fs": "udos",
            "typ": self.typ,
            "eig": eig or "-",
            "start": _hex16(_parse_hex(self.f_entry.text(), "ENTRY")),
            "satz": str(satz),
            "block": str(satz if block < 0 else block),
            # `rest=` („Bytes im letzten Satz") kommt hier BEWUSST nicht vor.  Es ist
            # eine Aussage über den Inhalt, nicht über den Anwenderwillen, und 0 ist
            # dort ein GÜLTIGER Wert — stünde die Zeile da, hielte der Leser die 0
            # für eine Angabe und der Schreibpfad rechnete sie nicht mehr aus.
            "segment": _segment_erstes(segs),
            "mem": ":".join((_hex16(_parse_hex(self.f_low.text(), "LOW")),
                             _hex16(_parse_hex(self.f_high.text(), "HIGH")),
                             _hex16(_parse_hex(self.f_stack.text(), "STACK")))),
            "zusatz": self.f_zusatz.text().strip() or "0",
            "segs": " ".join(segs),
            "erst": self.f_erstellt.text().strip() or "-",
            "geaend": self.f_geaendert.text().strip() or "-",
        }

    def schreibe(self, ziel, name: str = "") -> Path:
        """Die Angaben als ``.fileinfo`` nach @p ziel schreiben.

        Dieselbe Zeilenform, die der Kern schreibt und liest — es gibt genau eine.
        """
        return schreibe_fileinfo(ziel, self.angaben(), name or self.pfad.name)

    @property
    def merken(self) -> bool:
        """„Angaben als .fileinfo neben der Datei speichern" angekreuzt?"""
        return self.f_merken.isChecked()


def _segment_erstes(segs) -> str:
    """Das erste Segment als ``ANFANG:LÄNGE`` (dezimale Länge, wie im Beiblatt)."""
    if not segs:
        return "0000:0"
    anfang, laenge = segs[0].split("+", 1)
    return f"{_hex16(_parse_hex(anfang, 'Segmentanfang'))}:" \
           f"{_parse_hex(laenge, 'Segmentlänge')}"

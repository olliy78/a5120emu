"""`app/ui/keyboard_k7636.py` — Bildschirmtastatur K7636.01 (fruehe A5120, AP A3).

Geprueft wird die TABELLE, nicht das Aussehen: ein falscher Code sieht auf dem Bild richtig
aus.  Die erzeugte Python-Tabelle muss mit `doc/a5120_alt/k7636_01_codes.csv` uebereinstimmen
(bis auf die zwei dokumentierten Berichtigungen), jede bestueckte Position mit Code braucht
eine Kappe, keine Position darf doppelt belegt sein, und der Code einer Kappe ist die
physische Position (``TASTE_BASE | Rechenadresse``) — Gegenstueck ``K7636::QK_TASTE_BASE``.
"""

import csv
import re
from pathlib import Path

import pytest

from PySide6.QtCore import QEvent, QPointF, Qt
from PySide6.QtGui import QKeyEvent, QMouseEvent

from app.ui import keyboard_k7636 as k36
from app.ui.k7636_tabelle import POSITIONEN

REPO = Path(__file__).resolve().parents[2]
CSV = REPO / "doc/a5120_alt/k7636_01_codes.csv"

#: Berichtigungen gegenueber der gedruckten Tabelle (k7636.md §6): Position → (a, b)
KORREKTUR = {"A15": (0x0A, 0x0A), "C02": (0x73, 0x53)}

#: Positionen mit Code, die am Foto KEINE Kappe tragen (leere Fassung / Abdeckung); sie
#: stehen im Widget als `kind="dead"` ohne Code (k7636.md "Bildschirmtastatur nach Foto").
OHNE_KAPPE = {"C15", "B15", "B17", "A53", "F57"}
#: Modifier/Sonderleitungen und Lampen: kein Code oder kein Tastenfeld
KEIN_TASTENCODE = {"SL1", "SL2", "SL3", "SL4", "SL5", "SL6", "A99"}


@pytest.fixture(scope="module")
def keys():
    return k36._build_layout_k7636()


@pytest.fixture(scope="module")
def by_pos(keys):
    return {k.pos: k for k in keys if k.pos}


def _csv():
    with CSV.open(encoding="utf-8") as f:
        return list(csv.DictReader(f))


def test_tabelle_entspricht_der_csv():
    zeilen = _csv()
    assert len(zeilen) == 128
    for z in zeilen:
        pos = z["pos"].split("/")[0]
        if pos in ("-", ""):
            continue
        adr = int(z["rechenadresse_hex"], 16)
        a, b = int(z["code_a_hex"], 16), int(z["code_b_hex"], 16)
        a, b = KORREKTUR.get(pos, (a, b))
        assert POSITIONEN[pos] == (adr, a, b), pos
    assert POSITIONEN["B99"] == POSITIONEN["B11"]


def test_kerntabelle_ist_dieselbe_wie_die_python_tabelle():
    """core/peripherals/k7636/k7636_tabelle.h und app/ui/k7636_tabelle.py kommen aus einem
    Erzeuger — hier die Gegenprobe der Kernseite (Zeile je Rechenadresse)."""
    text = (REPO / "core/peripherals/k7636/k7636_tabelle.h").read_text(encoding="utf-8")
    zeilen = re.findall(r'\{"([A-Z0-9]*)", 0x([0-9A-F]{2}), 0x([0-9A-F]{2})\},  // ([0-9A-F]{2})H', text)
    assert len(zeilen) == 128
    for pos, a, b, adr in zeilen:
        if pos:
            assert POSITIONEN[pos] == (int(adr, 16), int(a, 16), int(b, 16)), pos


def test_rechenadresse_nach_doku():
    assert POSITIONEN["C00"][0] == 0x77            # LOCK
    assert POSITIONEN["B11"][0] == 0x7F            # SHIFT
    assert POSITIONEN["C01"] == (0x62, 0x61, 0x41)  # a / A
    assert POSITIONEN["A10"][1:] == (0xFF, 0xFF)    # ET1
    assert POSITIONEN["A00"][1:] == (0xFE, 0xFE)    # ET2


def test_jede_bestueckte_position_hat_eine_kappe(by_pos):
    # Jede Tabellenposition mit Code hat GENAU EINE Taste im Widget (auch die leeren
    # Fassungen, als tote Teile); die Menge der toten ist dokumentiert.
    fehlend = {p for p, (_, a, b) in POSITIONEN.items()
               if (a or b) and p not in by_pos and p not in KEIN_TASTENCODE
               and p != "B99"}
    assert not fehlend
    tot = {p for p, k in by_pos.items() if k.kind == "dead" and (k.a or k.b) and p != "F59"}
    assert tot == OHNE_KAPPE


def test_kappe_traegt_die_position_als_code(keys):
    for k in keys:
        if k.kind in ("shift", "lock", "dead"):
            assert k.code is None
            continue
        assert k.code == k36.taste(k.matrix), k.name
        assert k.a == POSITIONEN[k.pos][1] and k.b == POSITIONEN[k.pos][2]


def test_keine_doppelbelegung(keys):
    pos = [k.pos for k in keys if k.pos]
    assert len(pos) == len(set(pos))
    assert all(p in POSITIONEN for p in pos)
    adressen = [k.matrix for k in keys if k.kind == "normal"]
    assert len(adressen) == len(set(adressen))


def test_taste_base_passt_zum_kern():
    h = (REPO / "core/peripherals/k7636/k7636.h").read_text(encoding="utf-8")
    m = re.search(r"QK_TASTE_BASE\s*=\s*(0x[0-9A-Fa-f]+)", h)
    assert int(m.group(1), 16) == k36.TASTE_BASE


def test_widget_zeichnet_und_sendet_die_position(qapp):
    w = k36.KeyboardK7636Widget()
    w.resize(w.sizeHint())
    assert not w.grab().isNull()
    gesendet = []
    w.keyPressed.connect(lambda c, s, ctl: gesendet.append((c, s)))
    key = w._by_pos["C01"]
    unit, ox, oy = w._geometry()
    mitte = w._rect_of(key, unit, ox, oy).center()
    for typ in (QEvent.MouseButtonPress, QEvent.MouseButtonRelease):
        ev = QMouseEvent(typ, QPointF(mitte), QPointF(mitte), Qt.LeftButton,
                         Qt.LeftButton, Qt.NoModifier)
        (w.mousePressEvent if typ == QEvent.MouseButtonPress else w.mouseReleaseEvent)(ev)
    assert gesendet == [(k36.taste(0x62), False)]
    w.deleteLater()


def test_umschalttaste_setzt_das_shift_flag(qapp):
    w = k36.KeyboardK7636Widget()
    w.resize(w.sizeHint())
    unit, ox, oy = w._geometry()

    def klick(pos):
        mitte = w._rect_of(w._by_pos[pos], unit, ox, oy).center()
        for typ in (QEvent.MouseButtonPress, QEvent.MouseButtonRelease):
            ev = QMouseEvent(typ, QPointF(mitte), QPointF(mitte), Qt.LeftButton,
                             Qt.LeftButton, Qt.NoModifier)
            (w.mousePressEvent if typ == QEvent.MouseButtonPress else w.mouseReleaseEvent)(ev)

    gesendet = []
    w.keyPressed.connect(lambda c, s, ctl: gesendet.append((c, s)))
    klick("B11")
    klick("C01")
    assert gesendet == [(k36.taste(0x62), True)]
    w.deleteLater()


@pytest.mark.parametrize("text, qkey, pos", [
    ("a", Qt.Key_A, "C01"),
    ("=", Qt.Key_Equal, "E11"),
    ("m", Qt.Key_M, "B07"),     # nicht die Taste M der Funktionsreihe (F56)
])
def test_host_taste_leuchtet_die_richtige_kappe(qapp, text, qkey, pos):
    w = k36.KeyboardK7636Widget()
    ev = QKeyEvent(QEvent.KeyPress, qkey, Qt.NoModifier, text)
    assert [k.pos for k in w._keys_for_host_event(ev)] == [pos]
    w.deleteLater()


def test_sondertasten_des_pc_leuchten(qapp):
    w = k36.KeyboardK7636Widget()
    for qkey, pos in ((Qt.Key_Return, "A10"), (Qt.Key_Up, "C16"), (Qt.Key_F1, "C54")):
        ev = QKeyEvent(QEvent.KeyPress, qkey, Qt.NoModifier, "")
        assert [k.pos for k in w._keys_for_host_event(ev)] == [pos]
    w.deleteLater()


# --- Bildschirmtastatur nach Foto (k7636.md) ------------------------------------------------

def test_lage_ist_daten_und_ueberlappungsfrei(keys):
    from app.ui.keyboard_k7636 import LAGE
    assert len(LAGE) == len(keys)
    rects = [(k.x, k.y, k.x + k.w, k.y + k.h, k.name) for k in keys]
    for i, a in enumerate(rects):
        for b in rects[i + 1:]:
            ueber = min(a[2], b[2]) - max(a[0], b[0]), min(a[3], b[3]) - max(a[1], b[1])
            assert not (ueber[0] > 0.05 and ueber[1] > 0.05), (a[4], b[4])


def test_beschriftungen_nach_foto(by_pos):
    legende = {"E00": "ESC", "D00": "CTRL", "A99": "ALT", "E16": "INS", "E17": "HOME",
               "D16": "DEL", "D17": "END", "C16": "PgUp", "C17": "PgDn", "A10": "ENTER",
               "A00": "ET2", "D50": "CE", "C54": "S1", "C56": "S3", "A54": "S", "A56": "S2",
               "F54": "CI", "F56": "M", "D59": "\u2212", "C59": "+", "B59": "\u00f7",
               "A59": "*", "A52": "00", "A51": "0"}
    for pos, text in legende.items():
        assert by_pos[pos].low == text, pos
    # Reihe D/C/B der Hauptbloecke tragen die Buchstaben der QWERTZ-Lage
    assert "".join(by_pos[p].low for p in ("D01", "D02", "D03", "D04", "D05", "D06")) == "QWERTY"
    assert by_pos["B01"].low == "Z"


def test_farben_je_gruppe(by_pos):
    assert by_pos["D50"].style == by_pos["C50"].style == by_pos["C54"].style == "red"
    assert by_pos["F54"].style == "red"
    assert by_pos["F59"].style == "gruen"
    assert {by_pos[p].style for p in ("F51", "F52", "F53")} == {"lampe"}
    assert by_pos["C01"].style == "dark"
    assert by_pos["E16"].style == by_pos["D59"].style == "light"


def test_fenstergroesse_und_zeichnen(qapp):
    w = k36.KeyboardK7636Widget()
    sh = w.sizeHint()
    # Seitenverhaeltnis der Tastatur am Foto: rund 4:1 (Tastenfeld ~28 x 6 Raster)
    assert 3.4 < sh.width() / sh.height() < 4.6
    assert w.minimumSizeHint().width() <= sh.width() and w.minimumSizeHint().height() > 0
    w.set_leds(0x3f)
    w.resize(sh)
    assert not w.grab().isNull()
    w.deleteLater()


def test_tote_teile_senden_nichts(qapp):
    w = k36.KeyboardK7636Widget()
    w.resize(w.sizeHint())
    gesendet = []
    w.keyPressed.connect(lambda c, s, ctl: gesendet.append(c))
    unit, ox, oy = w._geometry()
    for pos in ("C15", "F57", "F59", "F53"):
        mitte = w._rect_of(w._by_pos[pos], unit, ox, oy).center()
        for typ in (QEvent.MouseButtonPress, QEvent.MouseButtonRelease):
            ev = QMouseEvent(typ, QPointF(mitte), QPointF(mitte), Qt.LeftButton,
                             Qt.LeftButton, Qt.NoModifier)
            (w.mousePressEvent if typ == QEvent.MouseButtonPress else w.mouseReleaseEvent)(ev)
    assert gesendet == []
    w.deleteLater()


def test_kein_tastenkuerzel_ohne_strg_umschalt():
    quelle = (REPO / "app/ui/keyboard_k7636.py").read_text(encoding="utf-8")
    assert "QShortcut" not in quelle and "setShortcut" not in quelle

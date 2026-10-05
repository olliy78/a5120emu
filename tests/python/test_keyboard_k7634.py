"""`app/ui/keyboard_k7634.py` — Bildschirmtastatur K7634 (K8915 Gen 1).

Wie bei der K7637 (`test_keyboard_layout.py`) wird die TABELLE geprüft, nicht das
Aussehen: ein falscher Code sieht auf dem Bild richtig aus.  Die Codetabelle der
Nachbildung muss mit `doc/k8915g2/k7634.md` §5 übereinstimmen, jede Taste muss
eine Position haben, keine Position darf doppelt belegt sein, und die drei am
Gerät fehlenden Kappen sind als ergänzt markiert.
"""

import re
from pathlib import Path

import pytest

from PySide6.QtCore import QEvent, QPointF, Qt
from PySide6.QtGui import QKeyEvent, QMouseEvent

from app.ui import keyboard_k7634 as k34

REPO = Path(__file__).resolve().parents[2]

#: Positionen, die die K7634.04 als unbestückt (00H) führt, auf denen das Foto aber
#: beschriftete Kappen zeigt (STRG CHOI PICK LOC, UPDATE ⇑ ⇓) — Zuordnung [?].
UNBESTUECKT_MIT_KAPPE = {"G17", "G51", "G52", "G53", "E51", "E52", "E53"}


@pytest.fixture(scope="module")
def keys():
    return k34._build_layout_k7634()


@pytest.fixture(scope="module")
def by_pos(keys):
    return {k.pos: k for k in keys if k.pos}


def _doku_tabelle():
    """§5 der Doku: Liste der 16 ROM-Gruppen, je 8 × (Position, a, b)."""
    text = (REPO / "doc/k8915g2/k7634.md").read_text(encoding="utf-8")
    gruppen = []
    for zeile in text.splitlines():
        if not re.match(r"^\| `2[0-9A-F]{2}` \||^\| `1F[08]` \|", zeile):
            continue
        zellen = [z.strip() for z in re.split(r"(?<!\\)\|", zeile)[1:-1]]
        eintraege = []
        for z in zellen[2:]:
            codes = re.findall(r"`([0-9A-F]{2})•?/([0-9A-F]{2})•?`", z)
            a, b = codes[-1]
            m = re.match(r"(B11/B99|SL\d|[A-G]\d\d)\b", z)
            eintraege.append((m.group(1) if m else "", int(a, 16), int(b, 16)))
        gruppen.append(tuple(eintraege))
    return gruppen


def test_tabelle_entspricht_der_doku():
    doku = _doku_tabelle()
    assert len(doku) == 16
    assert [tuple(g) for g in k34.TABELLE] == doku


def test_rechenadresse_nach_doku():
    """Bit 6…4 Spalte, Bit 3 Matrix, Bit 2…0 Zeile (k7634.md §3.4) — Stichproben."""
    assert k34.POSITIONEN["C00"][0] == 0x77          # LOCK
    assert k34.POSITIONEN["B11"][0] == 0x7F          # SHIFT
    assert k34.POSITIONEN["B99"][0] == 0x7F
    assert k34.POSITIONEN["A16"][:2] == (0x45, 0x06)  # ← (A4 → Spalte 4, Matrix 0, Z5)
    assert k34.POSITIONEN["E02"][1:] == (0x32, 0x22)  # Druckfehler 33H berichtigt [?]


def test_jede_taste_hat_einen_code(keys):
    for k in keys:
        if k.kind in ("shift", "lock", "ctrl"):
            continue
        assert k.code == k34.taste(k.matrix), k.name
        if k.pos not in UNBESTUECKT_MIT_KAPPE:
            assert k.a > 0, f"{k.name}: Tabellencode 00H"


def test_keine_doppelbelegung(keys):
    pos = [k.pos for k in keys if k.pos]
    assert len(pos) == len(set(pos))
    codes = [k.code for k in keys if k.code is not None]
    assert len(codes) == len(set(codes))
    # Nur die beiden Umschalttasten teilen sich eine Adresse (7FH), und die senden nicht.
    adressen = [k.matrix for k in keys if k.matrix >= 0 and k.kind != "shift"]
    assert len(adressen) == len(set(adressen))


def test_alle_bestueckten_positionen_haben_eine_kappe(by_pos):
    """Jeder belegte Matrixpunkt der K7634.04 (außer G53 Betriebsbereitschaft, SL) liegt
    unter einer Kappe — sonst fehlte eine Taste der Nachbildung."""
    fehlend = {p for p in k34.POSITIONEN if p not in by_pos and p != "G53"}
    assert not fehlend


def test_die_drei_ergaenzten_kappen(keys, by_pos):
    ergaenzt = {k.pos for k in keys if k.ergaenzt}
    assert ergaenzt == {"G04", "G05", "A15"}
    assert (by_pos["G05"].a, by_pos["G05"].low) == (0xC1, "PF1")
    assert by_pos["G04"].a == 0xFD                    # REC
    assert by_pos["A15"].a == 0x0A                    # ↵
    assert not by_pos["A16"].ergaenzt and by_pos["A16"].a == 0x06
    for p in ergaenzt:
        assert "[?]" in by_pos[p].name


def test_funktionsreihe_in_reihenfolge_der_positionen(keys):
    reihe = [k.pos for k in sorted((k for k in keys if k.y == 0), key=lambda k: k.x)]
    assert reihe == [f"G{i:02d}" for i in range(1, 18)] + ["G51", "G52", "G53"]


def test_widget_zeichnet_und_sendet_die_position(qapp):
    w = k34.KeyboardK7634Widget()
    w.resize(w.sizeHint())
    assert not w.grab().isNull()
    gesendet = []
    w.keyPressed.connect(lambda c, s, ctl: gesendet.append(c))
    key = w._by_pos["A16"]
    unit, ox, oy = w._geometry()
    mitte = w._rect_of(key, unit, ox, oy).center()
    for typ in (QEvent.MouseButtonPress, QEvent.MouseButtonRelease):
        ev = QMouseEvent(typ, QPointF(mitte), QPointF(mitte), Qt.LeftButton,
                         Qt.LeftButton, Qt.NoModifier)
        (w.mousePressEvent if typ == QEvent.MouseButtonPress else w.mouseReleaseEvent)(ev)
    assert gesendet == [k34.taste(0x45)]
    w.deleteLater()


@pytest.mark.parametrize("text, qkey, pos", [
    ("o", Qt.Key_O, "D09"),      # nicht OFF
    ("h", Qt.Key_H, "C06"),      # nicht HOLD
    ("p", Qt.Key_P, "D10"),      # nicht PF7
    ("=", Qt.Key_Equal, "E11"),
])
def test_host_taste_leuchtet_die_richtige_kappe(qapp, text, qkey, pos):
    w = k34.KeyboardK7634Widget()
    ev = QKeyEvent(QEvent.KeyPress, qkey, Qt.NoModifier, text)
    assert [k.pos for k in w._keys_for_host_event(ev)] == [pos]
    w.deleteLater()

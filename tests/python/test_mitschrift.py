"""Mitschrift des Terminals (``app/mitschrift.py``): Rohbytes → Text → angehängte Datei."""

from app.mitschrift import LEERLAUF_S, Mitschrift


class Uhr:
    def __init__(self):
        self.t = 100.0

    def __call__(self):
        return self.t


def _m(tmp_path, name="log.txt"):
    uhr = Uhr()
    return Mitschrift(str(tmp_path / name), uhr=uhr), uhr, tmp_path / name


def test_zeilen_kommen_sofort_die_angefangene_nach_dem_leerlauf(tmp_path):
    m, uhr, f = _m(tmp_path)
    m.schreibe(b"login: ")
    assert f.read_text() == ""                       # noch keine Zeile
    m.schreibe(b"root\r\n% ")
    assert f.read_text() == "login: root\n"
    m.takt()
    assert f.read_text() == "login: root\n"          # zu früh
    uhr.t += LEERLAUF_S + 0.1
    m.takt()
    assert f.read_text() == "login: root\n% "        # der Prompt ist da


def test_escape_folgen_und_steuerzeichen_fallen_weg_auch_ueber_lesegrenzen(tmp_path):
    m, _, f = _m(tmp_path)
    m.schreibe(b"a\x1b[2J\x1b[1;")                   # CSI zerreisst …
    m.schreibe(b"5Hb\x1b=  c\x1bT\x1b(Bd\x07\r\n")    # … und geht weiter; ADM31 ESC = r c
    assert f.read_text() == "abcd\n"


def test_rueckschritt_nimmt_das_letzte_zeichen_der_zeile_zurueck(tmp_path):
    m, _, f = _m(tmp_path)
    m.schreibe(b"lx\x08 \x08s\n")                    # Echo eines Rückschritts: BS SP BS
    assert f.read_text() == "ls\n"


def test_eine_vorhandene_datei_wird_angehaengt(tmp_path):
    f = tmp_path / "alt.txt"
    f.write_text("frueher\n", encoding="utf-8")
    m = Mitschrift(str(f))
    m.schreibe(b"neu\n")
    m.abschliessen()
    assert f.read_text(encoding="utf-8") == "frueher\nneu\n"
    m2 = Mitschrift(str(f))                          # erneutes Öffnen ändert nichts am Bestand
    assert f.read_text(encoding="utf-8") == "frueher\nneu\n" and m2.aktiv


def test_zielwechsel_und_aus(tmp_path):
    m, _, f = _m(tmp_path)
    m.schreibe(b"halb")
    assert m.setze_pfad("") is False                 # aus: die angefangene Zeile geht noch ins alte Ziel
    assert f.read_text() == "halb" and not m.aktiv
    m.schreibe(b"vergessen\n")
    assert f.read_text() == "halb"


def test_nicht_anlegbare_datei_meldet_fehler_und_laeuft_nicht(tmp_path):
    m = Mitschrift(str(tmp_path / "gibt-es-nicht" / "x.txt"))
    assert not m.aktiv and m.fehler
    m.schreibe(b"x\n")                               # darf nichts werfen


def test_umlaute_latin1_werden_utf8(tmp_path):
    m, _, f = _m(tmp_path)
    m.schreibe("Grüße\n".encode("latin-1"))
    assert f.read_text(encoding="utf-8") == "Grüße\n"

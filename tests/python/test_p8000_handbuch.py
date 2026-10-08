"""Das Handbuchkapitel „Bedienung des P8000" (AP P23c, Entwurf 25 §12 Punkt 8).

Das Kapitel beschreibt, was man am P8000 tippt — und verweist dabei auf Menüeinträge, Reiter, Knöpfe und
Auswahlwerte der Oberfläche.  Eine Hilfe, die von der Oberfläche abweicht, ist schlimmer als keine
(dasselbe Prinzip wie bei der Kürzeltabelle): geprüft wird deshalb, dass das Kapitel da ist, dass jeder
dort genannte Menüpfad ``*Menü ▸ Eintrag*`` wirklich im Fenster des P8000 steht und dass die
Auswahlwerte der Rechnerausstattung und Betriebsart nicht erfunden sind.  Die Kürzeltabelle bleibt
unberührt (die beiden Wächter in ``test_gui_smoke`` prüfen sie in beide Richtungen).
"""

import re
from pathlib import Path

import pytest

from conftest import requires_core

pytestmark = requires_core

KAPITEL = "Bedienung des P8000"
ABSCHNITTE = (
    "Überblick: was ist was", "Einschalten und Selbsttest", "Der U880-Monitor (MON8)",
    "Der U8000-Monitor (MON16)", "UDOS booten (8-Bit-Teil)", "WEGA: die Platte vorbereiten und installieren",
    "WEGA starten, anmelden, beenden", "Terminal und Tastatur", "Fehlersuche",
)


@pytest.fixture
def umgebung(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    monkeypatch.setenv("K1520_DISKS", str(tmp_path / "disks"))
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


def _kapitel() -> str:
    from app.ui.help_window import lade_handbuch
    text = lade_handbuch()
    assert f"\n## {KAPITEL}\n" in text, "das Kapitel fehlt im Handbuch"
    return text.split(f"\n## {KAPITEL}\n", 1)[1].split("\n## ", 1)[0]


def _glatt(text: str) -> str:
    """Beschriftung ohne Mnemonik-&, ohne Auslassungspunkte und ohne Haken."""
    return text.replace("&", "").replace("…", "").replace("...", "").strip()


def test_kapitel_ist_da_und_im_inhaltsverzeichnis():
    from app.ui.help_window import abschnitte, lade_handbuch
    k = _kapitel()
    for name in ABSCHNITTE:
        assert f"### {name}\n" in k, name
    assert KAPITEL in abschnitte(lade_handbuch())
    # Die Kürzeltabelle ist ein Vertrag und steht weiter hinter dem Kapitel.
    text = lade_handbuch()
    assert text.index(f"## {KAPITEL}") < text.index("## Tastenkürzel")


def test_kapitel_belegt_die_monitorbefehle():
    """Die Befehlstabellen enthalten die Befehle der Quellen (Anhang C/D, U880SM.S, p.init.s)."""
    k = _kapitel()
    mon8 = k.split("### Der U880-Monitor (MON8)", 1)[1].split("\n### ", 1)[0]
    mon16 = k.split("### Der U8000-Monitor (MON16)", 1)[1].split("\n### ", 1)[0]
    for befehl in ("D", "C", "F", "M", "PR", "PW", "R", "B", "G", "N", "I", "GE", "S", "Q", "T", "O", "X"):
        assert f"| `{befehl}` |" in mon8, befehl
    for befehl in ("D", "PR", "PRS", "PW", "PWS", "R", "B", "HR", "HW", "GE", "Q", "QRES", "T", "O"):
        assert re.search(rf"\|[^|\n]*`{befehl}`[^|\n]*\|", mon16), befehl
    for wort in ("O U", "O D", "O F", "MAXSEG", "Press NMI", "Press RETURN", "wega", "sa.format",
                 "sa.mkfs", "sa.install", "md(0,16000)wega", "halt", "sync;sync", "CAT"):
        assert wort in k, wort
    # RAM-Ausbau → MAXSEG laut doc/p8000/ram_konfiguration.md
    for maxseg in ("`07`", "`0F`", "`3F`", "`1F`", "`7E`"):
        assert maxseg in k, maxseg


def test_jeder_genannte_menuepfad_steht_in_der_oberflaeche(qapp, umgebung):
    """``*Menü ▸ Eintrag*`` im Kapitel → Menüleiste, Einstellungsreiter, Ansicht-Kästen, Modellwahl."""
    from PySide6.QtWidgets import QLabel, QPushButton
    from app import profil
    from app.ui.main_window import MainWindow

    k = _kapitel()
    pfade = sorted({m.strip() for m in re.findall(r"\*([^*\n]+? ▸ [^*\n]+?)\*", k)})
    assert len(pfade) >= 10, pfade

    w = MainWindow(None, profil=profil.profil("p8000"))
    try:
        w.run_timer.stop()
        w.show()
        qapp.processEvents()
        menues = {}
        for a in w.menuBar().actions():
            if a.menu() is not None:
                eintraege = set()

                def sammle(m):
                    for e in m.actions():
                        eintraege.add(_glatt(e.text()))
                        if e.menu() is not None:
                            sammle(e.menu())
                sammle(a.menu())
                # „Rechner ausschalten“ und „einschalten“ sind EIN rastender Eintrag
                if {"Rechner ausschalten", "Rechner einschalten"} & eintraege:
                    eintraege |= {"Rechner ausschalten", "Rechner einschalten"}
                menues[_glatt(a.text())] = eintraege
        reiter = {w.settings_widget.tabs.tabText(i) for i in range(w.settings_widget.tabs.count())}
        felder = [_glatt(l.text()).rstrip(":") for l in w.settings_widget.findChildren(QLabel)]
        modelle = [zeile[3] for zeile in w.profil.modelle]
        betriebsarten = ["Computer mit Terminal", "nur Terminal"]

        fehlend = []
        for pfad in pfade:
            teile = [t.strip() for t in pfad.split(" ▸ ")]
            kopf, rest = teile[0], teile[1:]
            if kopf in menues:
                if _glatt(rest[0]) not in menues[kopf]:
                    fehlend.append(pfad)
            elif kopf == "Einstellungen":
                if rest[0] not in reiter:
                    fehlend.append(pfad)
                elif len(rest) > 1 and not any(f.startswith(rest[1]) for f in felder):
                    fehlend.append(pfad)
            elif kopf == "Rechnerausstattung":
                if not any(m.startswith(rest[0]) for m in modelle):
                    fehlend.append(pfad)
            elif kopf == "Betriebsart":
                if rest[0] not in betriebsarten:
                    fehlend.append(pfad)
            else:
                fehlend.append(pfad)
        assert fehlend == [], f"im Handbuch genannt, aber nicht in der Oberfläche: {fehlend}"

        # Knöpfe und Kästen, die das Kapitel beim Namen nennt
        knoepfe = {_glatt(b.text()) for b in w.findChildren(QPushButton)}
        for name in ("Anschließen", "Neue Platte", "Abtrennen", "Leere Diskette"):
            assert f"*{name}" in k, name
            assert name in knoepfe or any(name in b for b in knoepfe), name
        # Betriebsart- und Ausstattungswerte, wie sie im Kapitel in Anführungs-/Kursivform stehen
        for name in ("Computer mit Terminal", "nur Terminal"):
            assert name in k and name in betriebsarten
        for name in ("Vollgerät", "ohne Winchester", "nur 8-Bit-Teil"):
            assert name in k and any(m.startswith(name) for m in modelle), name
    finally:
        w.close()
        qapp.processEvents()


def test_ram_werte_im_kapitel_gehoeren_zu_den_ausbauten_der_oberflaeche():
    """Die MAXSEG-Tabelle nennt Ausbauten, die die Auswahl im Einstellungsdialog wirklich anbietet."""
    from app import p8000_ram
    k = _kapitel()
    angebot = " | ".join(anzeige for _wert, anzeige in p8000_ram.AUSBAUTEN)
    for groesse in ("2 × 256 KB", "4 × 256 KB", "4 × 1 MB", "RAM-Karte 16 MB"):
        assert groesse in k, groesse
        assert groesse in angebot, groesse

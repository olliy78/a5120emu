"""`k1520disktool --physical` — die Kommandozeile fürs echte Laufwerk.

**Ohne Hardware**: das Ersatzlaufwerk aus :mod:`gw_fake` (eine ``.hfe`` statt eines
Greaseweazle) wird an die Stelle von :meth:`app.gw.PhysicalSession.start` gesetzt.
Damit läuft derselbe Weg wie an der Scheibe — Sitzung, Arbeitsfaden, Erkennung,
Dateizugriff, Rückführung —, nur ohne USB.

Geprüft wird das, was eine Kommandozeile ausmacht und was an der Hardware zu spät
auffiele:

* die **Sperre**: verändernde Befehle brauchen ``--write``, und zwar **bevor** der
  Motor anläuft — sonst wäre das erste, was ein Tippfehler tut, ein Schreibvorgang;
* **Exit-Kennungen** (0 ok · 1 Fehler · 2 nicht erkannt) — davon hängt jedes Skript ab;
* dass stdout **maschinenlesbar** bleibt: der Fortschritt geht auf stderr;
* dass die Befehle dasselbe liefern wie an einer Datei.

@see doc/design/14_physische_diskette.md §12.3
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

from gw_fake import HfeDevice

FIXTURE = "udos1715_640k_pc1715_system.img"
FIXTURE_HFE = "udos_boot_scp.hfe"


@pytest.fixture
def hfe(fixture_disks):
    return fixture_disks / FIXTURE_HFE


@pytest.fixture
def cli(monkeypatch, hfe):
    """`main()` mit einem Ersatzlaufwerk statt eines Adapters.

    Gibt eine Funktion zurück, die die Kommandozeile fährt und
    ``(exit, stdout, stderr)`` liefert.
    """
    from app.disktool import physical_cli
    from app.gw import PhysicalSession, Sync, TrackWorker

    geraet = HfeDevice(hfe)

    def start(*, drive="a", cell_rate_kbps=250, num_cyls=80, num_heads=2,
              writable=False, read_ahead=True, for_emulator=False, rpm=300,
              verify_writes=True, double_step=False):
        sync = Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
                    cell_rate_kbps=cell_rate_kbps, rpm=rpm, writable=writable,
                    read_ahead=read_ahead, verify_writes=verify_writes)
        worker = TrackWorker(sync, geraet)
        worker.start()
        return PhysicalSession(sync, worker, geraet, drive=drive, writable=writable,
                               cell_rate_kbps=cell_rate_kbps,
                               num_cyls=geraet.num_cyls, num_heads=geraet.num_heads)

    monkeypatch.setattr(PhysicalSession, "start", staticmethod(start))
    monkeypatch.setattr(physical_cli, "BEFEHLE", physical_cli.BEFEHLE)

    def fahre(*argv, capsys=None):
        rc = physical_cli.main(["--physical", *argv])
        return rc

    fahre.geraet = geraet
    return fahre


# ─────────────────────────────────────────────────────────────────────────────
# Die Sperre — vor dem Motor, nicht danach
# ─────────────────────────────────────────────────────────────────────────────

@pytest.mark.parametrize("befehl", ["put", "rm", "rewrite"])
def test_veraendernde_befehle_brauchen_write(cli, capsys, befehl, tmp_path):
    """Ohne ``--write`` wird abgelehnt — und das Laufwerk bleibt unberührt.

    Der zweite Teil ist der eigentliche Punkt: die Prüfung steht VOR dem Öffnen der
    Sitzung.  Ein Tippfehler soll nicht erst nach zwei Minuten Einlesen auffallen,
    und er soll die Diskette überhaupt nicht anfassen.
    """
    rc = cli(befehl, str(tmp_path / "x"))
    aus = capsys.readouterr()
    assert rc == 1
    assert "--write" in aus.err
    assert cli.geraet.gelesen == [], "die Diskette wurde trotz Ablehnung angefasst"
    assert cli.geraet.geschrieben == []


def test_unbekannter_befehl_faengt_gar_nicht_erst_an(cli, capsys):
    rc = cli("loeschmich")
    aus = capsys.readouterr()
    assert rc == 1
    assert "unbekannter Befehl" in aus.err
    assert cli.geraet.gelesen == []


def test_ohne_befehl_kommt_die_hilfe(cli, capsys):
    rc = cli()
    aus = capsys.readouterr()
    assert rc == 1
    assert "save-as" in aus.out and "--write" in aus.out
    assert cli.geraet.gelesen == []


def test_raw_gibt_es_nur_ohne_dateisystem(cli, capsys):
    rc = cli("--raw", "ls")
    aus = capsys.readouterr()
    assert rc == 1
    assert "--raw" in aus.err
    assert cli.geraet.gelesen == []


# ─────────────────────────────────────────────────────────────────────────────
# Lesen
# ─────────────────────────────────────────────────────────────────────────────

def test_ls_nennt_die_dateien_auf_stdout(cli, capsys):
    rc = cli("ls", "-q")
    aus = capsys.readouterr()
    assert rc == 0
    zeilen = [z for z in aus.out.splitlines() if z.strip()]
    assert "Side1/HELP.DAT.00" in zeilen
    assert len(zeilen) == 69, f"69 Dateien erwartet, {len(zeilen)}"
    # stdout ist die Nutzlast — Fortschritt und Befund gehören auf stderr.
    assert "erkannt" not in aus.out


def test_ls_lang_zeigt_typ_und_freiplatz(cli, capsys):
    rc = cli("ls", "-l", "-q")
    aus = capsys.readouterr()
    assert rc == 0
    assert "udos_ds77" in aus.out
    assert "Side0" in aus.out and "Side1" in aus.out
    assert "frei von" in aus.out


def test_info_nennt_die_herkunft(cli, capsys):
    rc = cli("info", "-q")
    aus = capsys.readouterr()
    assert rc == 0
    assert "echtes Laufwerk" in aus.out
    assert "udos_ds77" in aus.out


def test_get_holt_eine_datei_byteweise_gleich(cli, capsys, tmp_path, hfe):
    from app.core_binding.k1520disk import DiskTool

    with DiskTool.open(hfe) as datei:
        soll = None
        for e in datei.list_names():
            if e.name == "HELP.DAT.00" and e.side_prefix == "Side1/":
                ziel = tmp_path / "soll.bin"
                datei.extract(e.ref, ziel)
                soll = ziel.read_bytes()
    assert soll

    rc = cli("get", "Side1/HELP.DAT.00", "--to", str(tmp_path / "aus"), "-q")
    aus = capsys.readouterr()
    assert rc == 0, aus.err
    geholt = tmp_path / "aus" / "Side1_HELP.DAT.00"
    assert geholt.exists(), sorted(p.name for p in (tmp_path / "aus").iterdir())
    assert geholt.read_bytes() == soll


def test_get_ohne_treffer_meldet_das(cli, capsys, tmp_path):
    rc = cli("get", "GIBTSNICHT*", "--to", str(tmp_path), "-q")
    aus = capsys.readouterr()
    assert rc == 1
    assert "kein Eintrag passt" in aus.err


# ─────────────────────────────────────────────────────────────────────────────
# Sichern und Schreiben
# ─────────────────────────────────────────────────────────────────────────────

def test_save_as_sichert_die_ganze_diskette(cli, capsys, tmp_path, hfe):
    """`save-as` ist der Weg VOR dem ersten Schreibversuch — er muss ohne
    ``--write`` gehen und die ganze Diskette holen."""
    ziel = tmp_path / "sicherung.hfe"
    rc = cli("save-as", str(ziel), "-q")
    aus = capsys.readouterr()
    assert rc == 0, aus.err
    assert ziel.exists() and ziel.stat().st_size > 0
    # Die Sicherung muss dasselbe Dateisystem tragen wie das Original.
    from app.core_binding.k1520disk import DiskTool
    with DiskTool.open(ziel) as kopie, DiskTool.open(hfe) as orig:
        assert kopie.filesystem == orig.filesystem
        assert [e.name for e in kopie.list_names()] == \
               [e.name for e in orig.list_names()]


@pytest.mark.parametrize("befehl,ziel", [("save-as", "sicherung.hfe"),
                                         ("archive", "archiv.zip")])
def test_wer_eine_datei_anlegt_ueberschreibt_sie_nicht(cli, capsys, tmp_path,
                                                       befehl, ziel):
    """`save-as` und `archive` folgen derselben Regel — und zwar VOR dem Motor.

    Beide legen eine Datei an, beide werden im Stapel gefahren: derselbe Zielname
    zweimal ist ein Tippfehler, und die Diskette davor liegt dann schon wieder im
    Schrank.  Geprüft wird deshalb, bevor eingelesen wird — sonst kämen zwei
    Minuten Lesen und danach der Abbruch.
    """
    p = tmp_path / ziel
    p.write_bytes(b"schon da")
    assert cli.geraet.gelesen == [], "Vorbedingung: noch nichts gelesen"

    rc = cli(befehl, str(p), "-q")
    aus = capsys.readouterr()
    assert rc == 1
    assert "gibt es schon" in aus.err and "--force" in aus.err
    assert p.read_bytes() == b"schon da", "die vorhandene Datei wurde angefasst"
    assert cli.geraet.gelesen == [], "die Diskette wurde trotz Abbruch gelesen"

    assert cli(befehl, str(p), "--force", "-q") == 0, capsys.readouterr().err
    assert p.read_bytes() != b"schon da"


@pytest.mark.parametrize("befehl", ["save-as", "archive"])
def test_vergessener_zielname_faengt_gar_nicht_erst_an(cli, capsys, befehl):
    """Auch der fehlende Dateiname darf die Diskette nicht kosten."""
    rc = cli(befehl, "-q")
    aus = capsys.readouterr()
    assert rc == 1
    assert "Zieldateinamen" in aus.err
    assert cli.geraet.gelesen == [], "die Diskette wurde trotz Abbruch gelesen"


def test_archive_sichert_diskette_und_verzeichnisse(cli, capsys, tmp_path):
    """`archive` ist `save-as` fuer eine Sammlung — mit beiden Verzeichnissen.

    Der Aufkleber muss durchkommen: eine physische Diskette hat keinen Dateinamen,
    und ohne ihn stuende in der Inventur nicht, WELCHE Diskette das war.
    """
    import zipfile

    import yaml

    ziel = tmp_path / "archiv.zip"
    rc = cli("archive", str(ziel), "--label", "UDOS 4.3 Nr. 7", "-q")
    aus = capsys.readouterr()
    assert rc == 0, aus.err
    assert ziel.exists()
    assert "archiviert:" in aus.out

    with zipfile.ZipFile(ziel) as z:
        namen = set(z.namelist())
        daten = yaml.safe_load(z.read("diskarchive.yaml"))
    assert "UDOS_4.3_Nr._7.hfe" in namen and "UDOS_4.3_Nr._7.txt" in namen
    assert any(n.startswith("dateien/") for n in namen)
    assert daten["label"] == "UDOS 4.3 Nr. 7"
    assert "Greaseweazle" in daten["source"], "die Herkunft fehlt"
    assert daten["files"]


def test_archive_ohne_label_nimmt_den_datentraegernamen(cli, capsys, tmp_path):
    """Ohne Aufkleber bleibt das, was die Diskette selbst ueber sich sagt."""
    import zipfile

    import yaml

    ziel = tmp_path / "ohne_label.zip"
    assert cli("archive", str(ziel), "-q") == 0, capsys.readouterr().err
    capsys.readouterr()

    with zipfile.ZipFile(ziel) as z:
        daten = yaml.safe_load(z.read("diskarchive.yaml"))
    assert daten["label"], "weder Aufkleber noch Datentraegername"
    assert daten["label"] != "diskette", "das ist der letzte Rueckfall, kein Name"


def test_put_schreibt_und_fuehrt_zurueck(cli, capsys, tmp_path):
    """Der ganze Schreibweg — samt Prüf-Lesen und Rückführung ans Laufwerk."""
    quelle = tmp_path / "CLI.TXT"
    quelle.write_bytes(b"aus der Kommandozeile\r\n" * 4)

    # Eine NEUE Datei bringt keine Kopfsektorangaben mit; bei UDOS werden sie
    # seit dem `.fileinfo` verlangt statt geraten (doc/bug_disktool_Programmdatei.md §2.2).
    rc = cli("--write", "put", str(quelle), "--force", "-q", "--type", "A")
    aus = capsys.readouterr()
    assert rc == 0, aus.err + aus.out
    assert cli.geraet.geschrieben, "es wurde nichts ans Laufwerk zurueckgegeben"
    assert "geschrieben" in aus.err and "geprueft" in aus.err

    # Und die Datei steht wirklich auf dem Medium: dieselbe Sitzung noch einmal.
    rc = cli("ls", "-q")
    aus = capsys.readouterr()
    assert rc == 0
    assert any(z.endswith("CLI.TXT") for z in aus.out.splitlines()), aus.out


def test_schadstelle_wird_gemeldet_und_der_ausweg_genannt(cli, capsys, tmp_path):
    """Eine Spur, die nichts annimmt: Exit 1, Spurnummer und der Ausweg im Text.

    Ohne diese Meldung sähe ein Skript einen Erfolg, wo die Diskette die Daten gar
    nicht angenommen hat (§7.1/§7.2).
    """
    quelle = tmp_path / "DEFEKT.TXT"
    quelle.write_bytes(b"x" * 200)
    # ALLE Datenspuren schadhaft — welche der Allokator nimmt, entscheidet er selbst.
    cli.geraet.schadhaft = {(c, h) for c in range(cli.geraet.num_cyls)
                            for h in range(cli.geraet.num_heads)}

    rc = cli("--write", "put", str(quelle), "--force", "-q", "--type", "A")
    aus = capsys.readouterr()
    assert rc == 1
    assert "Schadstelle" in aus.err
    assert "rewrite" in aus.err, "der Ausweg muss dabeistehen"

"""Physische Diskette (Greaseweazle) — **ohne Hardware**.

Das Laufwerk hängt nicht immer am Rechner, in der CI/CD-Kette nie.  Der Entwurf ist
deshalb so geschnitten, dass die Hardware nur in der äußersten Schale vorkommt: alles
unterhalb von „Aufträge und Bitzellen" ist ohne Adapter prüfbar
(doc/design/14_physische_diskette.md §14).

Hier steht deshalb ein **Ersatzlaufwerk über einer ``.hfe``-Datei**.  Es liefert
dieselben Bitzellen, die der Adapter liefern würde — denn eine HFE-Aufnahme *ist*
genau das, samt Phasenversatz und Jitter der echten Aufnahme.  Benutzt wird
``udos_boot_scp.hfe``: ein echter Greaseweazle-Mitschnitt einer beidseitigen
UDOS-Diskette.

``greaseweazle`` wird hier **nicht** importiert; das Paket ist freiwillig.
"""

from __future__ import annotations

import threading
import time

import pytest

from app.core_binding.k1520disk import DiskTool, K1520DiskError
from app.gw import JobKind, Priority, Sync, TrackWorker
from gw_fake import HfeDevice

FIXTURE = "udos_boot_scp.hfe"


# Das Ersatzlaufwerk (HfeDevice) steht in gw_fake.py — es wird auch von den
# Oberflächentests gebraucht.

@pytest.fixture
def hfe(fixture_disks):
    return fixture_disks / FIXTURE


# ════════════════════════════════════════════════════════════════════════════
# Die C-ABI für sich
# ════════════════════════════════════════════════════════════════════════════


def test_frisch_angelegte_diskette_ist_vollstaendig_unbekannt():
    with Sync(num_cyls=8, num_heads=2, read_ahead=False) as s:
        st = s.stats
        assert st.tracks_total == 16
        assert st.tracks_known == 0
        assert st.tracks_dirty == 0
        assert not st.busy


def test_ohne_vorauslesen_gibt_es_nichts_zu_tun():
    with Sync(num_cyls=4, num_heads=1, read_ahead=False) as s:
        assert s.take_job(50) is None


def test_vorauslesen_stellt_von_selbst_auftraege_ein():
    with Sync(num_cyls=4, num_heads=1, read_ahead=True) as s:
        auftrag = s.take_job(500)
        assert auftrag is not None
        assert auftrag.kind is JobKind.READ
        assert auftrag.prio is Priority.READAHEAD


def test_shutdown_beendet_den_arbeitsfaden():
    s = Sync(num_cyls=4, num_heads=1, read_ahead=False)
    s.shutdown()
    auftrag = s.take_job(500)
    assert auftrag is not None and auftrag.kind is JobKind.STOP
    s.close()


def test_ein_zweiter_arbeitsfaden_wird_abgewiesen():
    with Sync(num_cyls=4, num_heads=1, read_ahead=False) as s:
        drin = threading.Event()

        def erster():
            drin.set()
            s.take_job(400)

        t = threading.Thread(target=erster)
        t.start()
        drin.wait(1.0)
        time.sleep(0.05)
        assert s.take_job(50) is None      # abgewiesen, nicht bedient
        t.join()
        assert "zweiter" in s.last_error


# ════════════════════════════════════════════════════════════════════════════
# Arbeitsfaden + Kern
# ════════════════════════════════════════════════════════════════════════════


def test_arbeitsfaden_holt_nur_die_gebrauchten_spuren(hfe):
    """Der Kern des Auftrags: kein Vollabzug, sondern Spuren auf Anforderung."""
    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=False) as s:
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            time.sleep(0.3)
            assert geraet.gelesen == [], "es wurde ungefragt gelesen"
            assert s.stats.tracks_known == 0
        finally:
            worker.stop()
    assert worker.errors == 0


def test_vorauslesen_fuellt_das_abbild(hfe):
    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=True) as s:
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            ende = time.monotonic() + 20
            while time.monotonic() < ende and s.stats.tracks_known < s.stats.tracks_total:
                time.sleep(0.05)
            st = s.stats
            assert st.tracks_known == st.tracks_total, f"nur {st.tracks_known} gelesen"
            assert st.errors == 0
        finally:
            worker.stop()
    # Jede Spur genau EINMAL — ein Auftrag je Spur (§5.2).
    assert len(geraet.gelesen) == len(set(geraet.gelesen))


def test_disktool_oeffnet_die_physische_diskette_wie_eine_datei(hfe):
    """Die Nagelprobe: dieselbe Diskette einmal als Datei, einmal „physisch".

    Erkennung, Dateisystem und Verzeichnis müssen übereinstimmen — der einzige
    Unterschied ist, dass die Spuren einzeln geholt werden.
    """
    with DiskTool.open(hfe) as datei:
        erwartet_fs = datei.filesystem
        erwartet_baende = datei.volume_count
        erwartet_namen = sorted(e.side_prefix + e.name for e in datei.list())

    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=True) as s:
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            with DiskTool.open_physical(s) as physisch:
                assert physisch.filesystem == erwartet_fs
                assert physisch.volume_count == erwartet_baende
                namen = sorted(e.side_prefix + e.name for e in physisch.list())
                assert namen == erwartet_namen
        finally:
            worker.stop()
    assert worker.errors == 0


def test_datei_aus_der_physischen_diskette_ist_byteweise_gleich(hfe, tmp_path):
    """Eine Datei holen — über den Umweg Bitzellen muss dasselbe herauskommen."""
    aus_datei = tmp_path / "aus_datei.bin"
    with DiskTool.open(hfe) as datei:
        eintraege = [e for e in datei.list() if e.size > 0]
        assert eintraege, "Fixture ohne Dateien"
        name = eintraege[0].side_prefix + eintraege[0].name
        datei.extract(name, aus_datei)

    aus_laufwerk = tmp_path / "aus_laufwerk.bin"
    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=True) as s:
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            with DiskTool.open_physical(s) as physisch:
                physisch.extract(name, aus_laufwerk)
        finally:
            worker.stop()
    assert aus_laufwerk.read_bytes() == aus_datei.read_bytes()


@pytest.mark.parametrize("name", [
    "udos_boot_scp.hfe", "cpa_cpa780_k5601_clock.hfe",
    "cpa_cpa780_k5601_noclock.hfe", "scpx17_5x1024_k5601_hardy.hfe",
    "scpx17_cpa780_k5601.hfe",
])
def test_stichprobe_erkennt_dasselbe_wie_die_vollmessung(fixture_disks, name):
    """**Der Wächter der Stichprobe: nie etwas ANDERES als die Vollmessung.**

    Dieselbe Diskette einmal als Datei (Vollmessung) und einmal „physisch"
    (Stichprobe) muss dasselbe Dateisystem ergeben.  Ohne diesen Vergleich blieb
    unbemerkt, dass die Stichprobe drei Disketten falsch erkannte — darunter eine
    77-Spur-Diskette als 40-Spur (`udos_ss40` statt `udos_ss77`), womit die halbe
    Diskette unsichtbar gewesen wäre.

    Ursache war, dass `GeometryProbe::match` Altbestand **duldet** und die Kandidaten
    nach absoluten Zählungen ordnet: eine Stichprobe schrumpft diese Zahlen
    ungleichmäßig und bevorzugt dadurch zu kleine Formate.  Zu wenig ist deshalb
    nicht nur langsam — es ist falsch.
    """
    pfad = fixture_disks / name
    if not pfad.exists():
        pytest.skip(f"{name} fehlt")

    with DiskTool.open(pfad) as datei:
        erwartet = datei.filesystem

    geraet = HfeDevice(pfad)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=False) as s:
        worker = TrackWorker(s, geraet, poll_ms=20)
        worker.start()
        try:
            with DiskTool.open_physical(s) as physisch:
                assert physisch.filesystem == erwartet, (
                    f"{name}: als Datei {erwartet}, physisch {physisch.filesystem} — "
                    "die Stichprobe urteilt anders als die Vollmessung")
                # Und sie darf dabei nicht die halbe Diskette gelesen haben.
                assert s.stats.tracks_known < geraet.num_cyls, (
                    f"{s.stats.tracks_known} Spuren für die Erkennung — "
                    "die Stichprobe greift nicht")
        finally:
            worker.stop()


def test_eine_leere_spur_verdirbt_nicht_die_ganze_diskette(fixture_disks):
    """Eine unformatierte Spur darf den Zellraten-Faktor nicht festlegen.

    `TrackSync::completeRead` ermittelt einmal je Sitzung, ob der Adapter
    überabtastet liefert (§8.1), und behält den Faktor für die ganze Diskette.
    Gesucht wurde er aber bei **jeder** Spur neu — und eine unformatierte Spur ist
    Rauschen: bei irgendeinem falschen Faktor findet sich darin zufällig eine
    Marke, die ihn festschreibt.  Danach war jede weitere Spur unlesbar
    (`scpx17_cpa780_k5601.hfe`: 6181-B-Spuren kamen als 2658 B mit einem Sektor).

    Sequentiell fiel das nie auf, weil leere Spuren am **Ende** liegen — das
    Format war da längst erkannt.  Wer die Diskette in anderer Reihenfolge liest
    (die Stichprobe der Formaterkennung tut genau das), verlor die halbe Diskette.
    """
    pfad = fixture_disks / "scpx17_cpa780_k5601.hfe"
    if not pfad.exists():
        pytest.skip("Fixture fehlt")

    with DiskTool.open(pfad) as datei:
        erwartet = {(c, h): datei.track(c, h).bytes
                    for c, h in ((4, 1), (39, 0), (42, 0))}
        leer = [(c, h) for c in (80, 81) for h in (0, 1)
                if not datei.track(c, h).formatted]
        assert leer, "Fixture ohne unformatierte Spuren — der Fall wird nicht geprüft"

    geraet = HfeDevice(pfad)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=False) as s:
        worker = TrackWorker(s, geraet, poll_ms=20)
        worker.start()
        try:
            with DiskTool.open_physical(s, "scpx640") as physisch:
                for c, h in leer:            # ERST die leeren Spuren …
                    physisch.track(c, h)
                for (c, h), bytes_soll in erwartet.items():   # … dann echte
                    assert physisch.track(c, h).bytes == bytes_soll, (
                        f"c{c}h{h}: {physisch.track(c, h).bytes} statt {bytes_soll} "
                        "Byte — eine leere Spur hat den Faktor verdorben")
        finally:
            worker.stop()


def test_eine_datei_laesst_sich_auf_eine_echte_diskette_schreiben(fixture_disks):
    """Der Gegenweg: geladene ``.hfe`` → echtes Laufwerk (§12.4).

    Geprüft wird die ganze Kette an ZWEI verschiedenen Disketten: die Zieldiskette
    trägt anfangs CP/A, danach muss sie die UDOS-Quelle tragen — mit derselben
    Dateiliste.  Nur so ist belegt, dass wirklich geschrieben und nicht bloss
    gelesen wurde.
    """
    quelle = fixture_disks / "udos_boot_scp.hfe"
    ziel   = fixture_disks / "cpa_cpa780_k5601_noclock.hfe"
    if not (quelle.exists() and ziel.exists()):
        pytest.skip("Fixtures fehlen")

    geraet = HfeDevice(ziel)          # das „Laufwerk" mit der ANDEREN Diskette
    with DiskTool.open(ziel) as vorher:
        assert vorher.filesystem != "udos_ds77", "Zieldiskette trägt schon UDOS"

    with DiskTool.open(quelle) as datei:
        soll = sorted(e.side_prefix + e.name for e in datei.list())
        with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
                  writable=True, read_ahead=False) as s:
            worker = TrackWorker(s, geraet, poll_ms=10)
            worker.start()
            try:
                n = datei.write_to_physical(s)
                assert n == geraet.num_cyls * geraet.num_heads, \
                    f"nur {n} Spuren eingestellt"
                assert s.flush(120_000), "Zurückschreiben scheiterte"
                st = s.stats
                assert st.tracks_dirty == 0 and st.tracks_defect == 0
                # Geschrieben UND zurückgelesen — der Verify-Lauf gehört dazu (§7.1).
                assert st.writes_done == n and st.verifies_done == n
            finally:
                worker.stop()

    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=False) as s2:
        worker = TrackWorker(s2, geraet, poll_ms=10)
        worker.start()
        try:
            with DiskTool.open_physical(s2) as danach:
                assert danach.filesystem == "udos_ds77", \
                    f"auf der Diskette liegt {danach.filesystem}"
                assert sorted(e.side_prefix + e.name
                              for e in danach.list()) == soll
        finally:
            worker.stop()


def test_ueberschreiben_verweigert_eine_zu_kleine_diskette(fixture_disks):
    """Passt es nicht, wird **gar nichts** geschrieben.

    Eine halb überschriebene Diskette wäre das schlechteste Ergebnis: die alte ist
    fort, die neue unvollständig.  Deshalb prüft `copyTo` die Geometrie vorher.
    """
    quelle = fixture_disks / "udos_boot_scp.hfe"      # 80 Zylinder
    if not quelle.exists():
        pytest.skip("Fixture fehlt")

    with DiskTool.open(quelle) as datei:
        with Sync(num_cyls=40, num_heads=1, writable=True, read_ahead=False) as s:
            with pytest.raises(K1520DiskError) as fehler:
                datei.write_to_physical(s)
            assert "80" in str(fehler.value) and "40" in str(fehler.value)
            assert s.stats.tracks_dirty == 0, "es wurde doch etwas eingestellt"


def test_doppelschritt_faehrt_jeden_zweiten_zylinder_an(fixture_disks):
    """Spur *n* liegt bei Doppelschritt auf dem physischen Zylinder *2n* (§12.5).

    So schreibt ein 40-Spur-Laufwerk (K5600.10); ein 80-Spur-Laufwerk erreicht
    dieselben Spuren nur mit doppeltem Schritt.  **Oberhalb des Geräts bleibt alles
    logisch**: das Abbild hat 40 Spuren, keine 80 mit Lücken — sonst müsste jede
    Schicht darüber die Umrechnung noch einmal wissen.
    """
    from app.gw.device import Device

    d = Device.__new__(Device)              # ohne Adapter — geprüft wird nur die Lage
    for doppelt, erwartet in ((False, [0, 1, 39]), (True, [0, 2, 78])):
        d.double_step = doppelt
        assert [d._position(c) for c in (0, 1, 39)] == erwartet, \
            f"double_step={doppelt}"


def test_doppelschritt_und_nur_seite_null_gehen_durch_den_dialog():
    """Was die beiden Haken bedeuten, muss bis in die Sitzungsargumente durchkommen.

    Die Rechnung sitzt an EINER Stelle (`_geometrie_rechnen`): aus den physischen
    Zylindern des Laufwerks und den Haken folgt die **logische** Geometrie.  Bei
    Doppelschritt passt nur die Hälfte der Spuren auf dieselbe Strecke.
    """
    pytest.importorskip("PySide6")
    from PySide6.QtWidgets import QApplication
    from app.ui.physical_disk import PhysicalDiskDialog, PhysicalSession
    import inspect

    QApplication.instance() or QApplication([])
    d = PhysicalDiskDialog()
    d._laufwerkspuren.setCurrentIndex(0)          # 80 Zylinder
    assert d.auswahl()["num_cyls"] == 80 and d.auswahl()["num_heads"] == 2

    d._doppelschritt.setChecked(True)
    d._nur_seite0.setChecked(True)
    wahl = d.auswahl()
    assert wahl["num_cyls"] == 40, "Doppelschritt halbiert die Spurzahl nicht"
    assert wahl["num_heads"] == 1
    assert wahl["double_step"] is True

    # Und die Sitzung muss genau diese Argumente annehmen.
    erlaubt = set(inspect.signature(PhysicalSession.start).parameters) - {"cls"}
    assert set(wahl) <= erlaubt, f"unbekannt: {set(wahl) - erlaubt}"


def test_nur_seite_null_liest_die_rueckseite_gar_nicht(fixture_disks):
    """„Nur Seite 0" spart nicht bloss Zeit — es blendet Altbestand aus.

    Auf einer Diskette, die einmal zweiseitig formatiert war, steht auf Seite 1
    noch das alte Format.  Wird sie nicht angefahren, sieht die Erkennung eine
    saubere einseitige Diskette (und der Kopf bleibt auf Seite 0).
    """
    pfad = fixture_disks / "udos_boot_scp.hfe"
    if not pfad.exists():
        pytest.skip("Fixture fehlt")

    geraet = HfeDevice(pfad)
    with Sync(num_cyls=geraet.num_cyls, num_heads=1, read_ahead=False) as s:
        worker = TrackWorker(s, geraet, poll_ms=20)
        worker.start()
        try:
            with DiskTool.open_physical(s) as t:
                assert t.medium_heads == 1
                assert t.list(), "nichts gelesen"
        finally:
            worker.stop()
    assert all(h == 0 for _, h in geraet.gelesen), \
        f"Seite 1 wurde angefahren: {[x for x in geraet.gelesen if x[1]][:3]}"


def test_die_sondenzahl_bleibt_klein():
    """Acht Spuren — nicht acht Zylinder auf beiden Seiten.

    Die Rechnung über den Katalog ergab acht **(Zylinder, Kopf)-Paare**; daraus
    versehentlich „acht Zylinder × beide Köpfe" zu machen verdoppelt die Wartezeit
    am echten Laufwerk auf nichts.  Auf Kopf 1 genügt EINE Sonde (ein- oder
    zweiseitig), alles Weitere entscheidet sich auf Kopf 0.
    """
    from app.core_binding.k1520disk import DiskTool, K1520DiskError

    assert DiskTool.probe_track_count(80, 2) == 8
    assert DiskTool.probe_track_count(40, 1) == 7      # ohne die Kopf-1-Sonde
    # Nie mehr als ein Bruchteil der Diskette, egal wie gross sie ist.
    for cyls, heads in ((40, 1), (40, 2), (77, 2), (80, 2), (160, 2)):
        assert DiskTool.probe_track_count(cyls, heads) <= 10, (cyls, heads)


def test_erkennung_holt_nur_eine_stichprobe(hfe):
    """Das Öffnen darf nicht die ganze Diskette einziehen.

    Am echten Laufwerk kostet jede Spur 0,5–0,8 s; 160 Spuren sind anderthalb
    Minuten.  Nachgerechnet über alle Formatpaare des Katalogs trennen acht Spuren
    alles, was trennbar ist — die wichtigste ist Zylinder 3, dieselbe, die das
    CP/A-BIOS liest.  Deshalb misst die Erkennung an einem nachladenden Medium nur
    Sondenspuren; die Vollmessung ist der Rückfall, wenn nichts passt.

    Geprüft wird beides: dass es deutlich weniger Spuren sind UND dass trotzdem
    richtig erkannt wird — eine schnelle Fehlerkennung wäre kein Fortschritt.
    """
    geraet = HfeDevice(hfe)
    gesamt = geraet.num_cyls * geraet.num_heads
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=False) as s:          # kein Vorauslesen: nur das Nötige
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            with DiskTool.open(hfe) as datei, DiskTool.open_physical(s) as physisch:
                assert physisch.filesystem == datei.filesystem, \
                    "die Stichprobe erkennt etwas anderes als die Vollmessung"
                assert physisch.format == datei.format
                assert len(physisch.list()) == len(datei.list())
            gelesen = s.stats.tracks_known
        finally:
            worker.stop()
    assert gelesen < gesamt // 3, \
        f"{gelesen} von {gesamt} Spuren gelesen — die Stichprobe greift nicht"


def test_gescheitertes_lesen_bringt_niemanden_zum_haengen(hfe):
    """Ein Laufwerksfehler darf den Vordergrund nicht bis zur Frist warten lassen."""

    class KaputtesLaufwerk(HfeDevice):
        def read_track(self, cyl, head):
            raise OSError("Kabel ab")

    geraet = KaputtesLaufwerk(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=True, request_timeout_ms=30000) as s:
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            ende = time.monotonic() + 10
            while time.monotonic() < ende and s.stats.tracks_failed < 4:
                time.sleep(0.05)
            assert s.stats.tracks_failed >= 4
            assert s.stats.tracks_known == 0
            assert worker.errors >= 4
            assert "Kabel ab" in worker.last_error
        finally:
            worker.stop()


# ════════════════════════════════════════════════════════════════════════════
# Drift-Wächter: C-Kopf ↔ ctypes
# ════════════════════════════════════════════════════════════════════════════
#
# Die drei Strukturen der Sync-ABI stehen zweimal: in core/api/k1520_sync_api.h und
# als ctypes.Structure in app/gw/sync.py.  Weicht die REIHENFOLGE ab, liest Python
# stillschweigend die falschen Felder — kein Absturz, nur falsche Zahlen.  Deshalb
# werden beide Seiten hier mechanisch verglichen.

import re
from pathlib import Path

HEADER = Path(__file__).resolve().parents[2] / "core" / "api" / "k1520_sync_api.h"


def _c_struct_felder(name: str) -> list[str]:
    """Feldnamen einer `typedef struct { … } NAME;` aus dem C-Kopf, in Reihenfolge."""
    text = HEADER.read_text("utf-8")
    # [^{}]* statt .*? — sonst beginnt der Treffer bei der ERSTEN Struktur des Kopfes
    # und zieht deren Felder mit herein.
    m = re.search(r"typedef struct \{([^{}]*)\}\s*" + name + r"\s*;", text, re.S)
    assert m, f"{name} steht nicht im Kopf {HEADER}"
    felder = []
    for zeile in m.group(1).splitlines():
        zeile = re.sub(r"/\*.*?\*/", "", zeile)          # Blockkommentare
        zeile = zeile.split("///")[0].split("//")[0].strip()
        treffer = re.match(r"^(?:const\s+)?\w+\s+(\w+)\s*;", zeile)
        if treffer:
            felder.append(treffer.group(1))
    return felder


@pytest.mark.parametrize("c_name,py_name", [
    ("K1520SyncSpec", "_Spec"),
    ("K1520SyncJob", "_Job"),
    ("K1520SyncStats", "_Stats"),
])
def test_die_ctypes_struktur_passt_zum_c_kopf(c_name, py_name):
    from app.gw import sync as sync_modul

    erwartet = _c_struct_felder(c_name)
    py = [f[0] for f in getattr(sync_modul, py_name)._fields_]
    assert py == erwartet, (
        f"{py_name} weicht von {c_name} ab.\n  C:      {erwartet}\n  Python: {py}")


def test_die_auftragsarten_stimmen_ueberein():
    """`JobKind` muss dieselben Werte tragen wie die Aufzählung im C-Kopf."""
    from app.gw import JobKind

    text = HEADER.read_text("utf-8")
    for name, wert in re.findall(r"K1520_SYNC_JOB_(\w+)\s*=\s*(\d+)", text):
        assert JobKind[name].value == int(wert), f"JobKind.{name}"


# ════════════════════════════════════════════════════════════════════════════
# Schreib-Verify gegen die echte Spur
# ════════════════════════════════════════════════════════════════════════════


def test_geschriebene_spuren_werden_zurueckgelesen(hfe):
    """Jedem Schreibvorgang folgt ein Prüf-Lesen — sonst bliebe ein Schaden blind."""
    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              writable=True, read_ahead=False, write_settle_ms=50) as s:
        worker = TrackWorker(s, geraet, poll_ms=30)
        worker.start()
        try:
            with DiskTool.open_physical(s, read_only=False) as d:
                pass
        finally:
            worker.stop()

    # Ohne Schreibzugriff darf gar nichts geschrieben (und nichts geprüft) worden sein.
    assert geraet.geschrieben == []


def test_eine_schadstelle_wird_gemeldet_statt_verschwiegen(hfe, tmp_path):
    """Der eigentliche Zweck: eine Diskette, die nicht mehr trägt, fällt auf.

    Der Verify-Lauf des Gastsystems liefe gegen das Speicherabbild und sähe nichts;
    das Prüf-Lesen gegen die Scheibe sieht es.
    """
    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              writable=True, read_ahead=False, write_settle_ms=50,
              request_timeout_ms=20000) as s:
        worker = TrackWorker(s, geraet, poll_ms=30)
        worker.start()
        try:
            # Erst öffnen (liest die Diskette), dann sie „beschädigen" und schreiben.
            with DiskTool.open_physical(s, read_only=False) as d:
                eintraege = [e for e in d.list() if e.size > 0]
                assert eintraege, "Fixture ohne Dateien"
                geraet.schadhaft = set(geraet._spuren)      # NICHTS nimmt mehr an
                quelle = tmp_path / "PROBE.TXT"
                quelle.write_bytes(b"schadstelle\r\n" * 4)
                ziel = eintraege[0].side_prefix.replace("Side1/", "Side0/") + "PROBE.TXT"
                d.insert(quelle, ziel, overwrite=True)
                # Das Speichern MUSS scheitern — und die Meldung muss die Spur nennen,
                # nicht bloss „fehlgeschlagen".
                from app.core_binding.k1520disk import K1520DiskError
                with pytest.raises(K1520DiskError) as fehler:
                    d.flush()
                assert "nicht schreiben" in str(fehler.value), str(fehler.value)
            assert not s.flush(20000), "der Schaden wurde als Erfolg verbucht"
        finally:
            worker.stop()

        st = s.stats
        assert st.tracks_defect > 0, "keine Schadstelle erkannt"
        assert st.verify_failed >= 2, "es wurde nicht wiederholt"
        assert s.defect_tracks, "die Spurnummer fehlt in der Meldung"
        assert "/" in s.defect_tracks
        assert "liess sich nicht schreiben" in s.last_error


def test_neu_beschreiben_stellt_alles_wieder_ein(hfe):
    """Der Ausweg: heile Diskette einlegen, alles Bekannte erneut wegschreiben."""
    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              writable=True, read_ahead=True, write_settle_ms=50) as s:
        worker = TrackWorker(s, geraet, poll_ms=30)
        worker.start()
        try:
            ende = time.monotonic() + 20
            while time.monotonic() < ende and s.stats.tracks_known < s.stats.tracks_total:
                time.sleep(0.05)
            bekannt = s.stats.tracks_known
            assert bekannt > 0

            eingestellt = s.rewrite_all()
            assert eingestellt == bekannt, "es wurden nicht alle bekannten Spuren gestellt"

            ende = time.monotonic() + 60
            while time.monotonic() < ende and s.stats.tracks_dirty:
                time.sleep(0.05)
            assert s.stats.tracks_dirty == 0, "die Rückführung wurde nicht fertig"
            assert s.stats.tracks_defect == 0
            # Jede bekannte Spur wurde geschrieben UND geprüft.
            assert len(set(geraet.geschrieben)) == bekannt
            assert s.stats.verifies_done >= bekannt
        finally:
            worker.stop()

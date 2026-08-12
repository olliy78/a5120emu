"""Paketierung (``packaging/``) — was das verteilte Paket enthalten muss.

Die schnellen Fälle laufen **ohne Netz** und in Sekunden: Syntax der Skripte,
Optionen, und ein vollständiger Durchlauf von ``build_payload.sh`` gegen die
bereits gebaute Bibliothek (``--skip-build``).  Sie fangen genau das ab, was
beim Schnüren schiefgehen kann, ohne dass es jemand merkt — eine Datei, die
nicht mehr mitkopiert wird.

Der **vollständige** Durchlauf (installieren, Python und Qt laden, starten)
lädt ~120 MB und dauert Minuten; er läuft nur mit ``K1520_PACKAGING_FULL=1``:

    K1520_PACKAGING_FULL=1 venv/bin/python3 -m pytest tests/python/test_packaging.py

Entwurf: doc/design/13_distribution.md
"""

import os
import shutil
import subprocess
import sys
import tarfile
import time
from pathlib import Path

import pytest

from conftest import PROJECT_ROOT, requires_core

PACKAGING = PROJECT_ROOT / "packaging"
SCRIPTS = ["install.sh", "build_payload.sh", "launcher.sh",
           "disktool_launcher.sh", "lib/common.sh"]
MAX_INSTALL_MB = 160   # frisch geschlankt sind es ~146 MB (§8 des Entwurfs)


def _sh(*args, **kw):
    return subprocess.run(args, capture_output=True, text=True, timeout=600, **kw)


def _slim():
    """``packaging/slim.py`` als Modul laden (es liegt außerhalb des Pakets)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("slim", PACKAGING / "slim.py")
    modul = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(modul)
    return modul


#: ``packaging/install.sh`` ist der **Unix**-Installer.  Unter Windows läuft er
#: zwar in der Git-Bash, bekommt aber Windows-Pfade herein, die MSYS als
#: ``/c/Users/…`` zurückgibt und relativ zum Arbeitsverzeichnis auflöst — das
#: Ergebnis ist ``D:/a/repo/C:\Users\…`` und prüft nichts.  Windows bekommt
#: sein eigenes ``install.ps1`` (Entwurf §10 Schritt 4); bis dahin laufen hier
#: nur die Fälle, die die Skripte *lesen* statt sie auszuführen.
nur_unix_installer = pytest.mark.skipif(
    sys.platform.startswith("win"),
    reason="Unix-Installer — Windows bekommt install.ps1 (Entwurf §10 Schritt 4)")

#: ``slim.py`` schlankt ELF-Bibliotheken (``ldd``, ``strip``).  Unter Windows
#: gibt es weder das eine noch das andere — die Regeln dort sind Sache der
#: Windows-Paketierung (§10 Schritt 4).
nur_elf = pytest.mark.skipif(
    sys.platform.startswith("win"),
    reason="ldd/strip sind ELF-Werkzeuge — unter Windows gegenstandslos")

# ─── Skripte ─────────────────────────────────────────────────────────────────

@pytest.mark.parametrize("script", SCRIPTS)
def test_skript_ist_syntaktisch_gueltig(script):
    """`sh -n` über jedes Skript — Bourne-Syntax, keine Bashismen."""
    out = _sh("sh", "-n", str(PACKAGING / script))
    assert out.returncode == 0, out.stderr


@pytest.mark.parametrize("script", ["install.sh", "build_payload.sh"])
def test_hilfe_bleibt_folgenlos(script, tmp_path):
    """`--help` beschreibt sich selbst und fasst nichts an."""
    out = _sh("sh", str(PACKAGING / script), "--help", cwd=tmp_path)
    assert out.returncode == 0, out.stderr
    assert "--prefix" in out.stdout or "--out" in out.stdout
    assert not list(tmp_path.iterdir()), "--help hat Dateien angelegt"


def test_unbekannte_option_bricht_ab(tmp_path):
    out = _sh("sh", str(PACKAGING / "install.sh"), "--gibtsnicht", cwd=tmp_path)
    assert out.returncode != 0
    assert "unbekannte Option" in out.stderr


def test_disktool_launcher_hat_den_platzhalter():
    """Wie beim Emulator-Starter — nur zeigt er auf app/disktool/main.py."""
    text = (PACKAGING / "disktool_launcher.sh").read_text(encoding="utf-8")
    assert 'ROOT="@ROOT@"' in text, "Platzhalter, den install.sh ersetzt, fehlt"
    assert '"$ROOT/venv/bin/python3"' in text
    assert '"$ROOT/app/disktool/main.py"' in text


def test_launcher_hat_genau_einen_platzhalter():
    """@ROOT@ wird beim Installieren ersetzt — bleibt einer stehen, startet nichts."""
    text = (PACKAGING / "launcher.sh").read_text(encoding="utf-8")
    assert 'ROOT="@ROOT@"' in text, "Platzhalter, den install.sh ersetzt, fehlt"
    assert '"$ROOT/venv/bin/python3"' in text
    assert '"$ROOT/app/main.py"' in text


def test_launcher_wechselt_ins_datenverzeichnis():
    """Sonst legt der Kern sein `logs/` dort an, wo der Anwender gerade steht."""
    text = (PACKAGING / "launcher.sh").read_text(encoding="utf-8")
    assert "user_data_dir()" in text, "Starter fragt die Pfadauflösung nicht"
    assert 'cd "$DATEN"' in text, "Starter wechselt nicht ins Datenverzeichnis"


def test_uv_pins_vollstaendig():
    """Version und je eine Prüfsumme für alle Zielplattformen des Entwurfs."""
    zeilen = [z.split() for z in (PACKAGING / "uv_pins.txt").read_text(encoding="utf-8").splitlines()
              if z.strip() and not z.startswith("#")]
    pins = {z[0]: z[1] for z in zeilen if len(z) == 2}
    assert "version" in pins
    for ziel in ("x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
                 "x86_64-apple-darwin", "aarch64-apple-darwin",
                 "x86_64-pc-windows-msvc"):
        assert ziel in pins, f"Prüfsumme für {ziel} fehlt"
        assert len(pins[ziel]) == 64, f"{ziel}: keine SHA256"


def test_lock_nagelt_mit_hashes_fest():
    """Der Installer installiert mit --require-hashes — ohne Hashes schlüge das fehl."""
    lock = (PACKAGING / "requirements.lock").read_text(encoding="utf-8")
    assert "--hash=sha256:" in lock
    assert "pyside6-essentials==" in lock.lower(), "Qt-Vollausbau statt Essentials?"
    assert "pyside6-addons" not in lock.lower(), "PySide6-Addons (~170 MB) im Paket"


@pytest.mark.parametrize("datei,starter", [
    ("a5120emu.desktop.in", "a5120emu"),
    ("k1520disktool.desktop.in", "k1520disktool"),
])
def test_desktop_eintrag_ist_gueltig(datei, starter):
    text = (PACKAGING / datei).read_text(encoding="utf-8")
    assert text.startswith("[Desktop Entry]")
    for feld in ("Type=Application", "Name=", f'Exec="@ROOT@/bin/{starter}"', "Icon=a5120emu"):
        assert feld in text, f"{feld} fehlt im Startmenü-Eintrag {datei}"


@nur_unix_installer
@pytest.mark.parametrize("eingabe,erwartet", [
    ("~/Emulatoren/A5120", "{home}/Emulatoren/A5120"),
    ("~", "{home}"),
    ("/abs/pfad", "/abs/pfad"),
])
def test_abs_path_loest_tilde_auf(eingabe, erwartet, tmp_path):
    """„~/…" muss zum Heimatverzeichnis werden.

    Die Tilde im Muster von ``${p#...}`` dehnt die Shell sonst selbst aus, das
    Muster passt dann nie — und im Installationspfad steht ein Verzeichnis
    namens „~".
    """
    out = subprocess.run(
        ["sh", "-c", f'. "{PACKAGING}/lib/common.sh"; abs_path "{eingabe}"'],
        capture_output=True, text=True, env={**os.environ, "HOME": str(tmp_path)})
    assert out.returncode == 0, out.stderr
    assert out.stdout.strip() == erwartet.format(home=tmp_path)


@pytest.mark.parametrize("wurzel", [
    "/opt/a5120emu",
    "/home/anna/Emulator Test",          # Leerzeichen — seit der Zielabfrage üblich
    "/home/anna/a&b|c",                  # Sonderzeichen, an denen `sed` zerbrach
])
def test_ersetze_root_vertraegt_jeden_pfad(tmp_path, wurzel):
    """@ROOT@ wird eingesetzt, ohne dass der Pfad als Ausdruck gelesen wird."""
    vorlage = tmp_path / "vorlage"
    vorlage.write_text('Exec="@ROOT@/bin/a5120emu"\nnichts\n')
    out = _sh("sh", "-c", f'. "{PACKAGING}/lib/common.sh"; ersetze_root "$1" "$2"',
              "sh", str(vorlage), wurzel)
    assert out.returncode == 0, out.stderr
    assert out.stdout == f'Exec="{wurzel}/bin/a5120emu"\nnichts\n'


@pytest.mark.parametrize("aufbau", ["user-dirs.dirs", "nur ~/Documents", "gar nichts"])
@nur_unix_installer
def test_dokumentenordner_shell_und_python_stimmen_ueberein(tmp_path, aufbau):
    """`--purge` muss dort aufräumen, wo der Emulator schreibt.

    Die Regel steht zweimal — in ``lib/common.sh`` für den Installer und in
    ``app/paths.py`` für den Emulator.  Laufen sie auseinander, löscht das
    Deinstallieren am Datenverzeichnis vorbei, ohne dass es jemandem auffällt.
    """
    heim = tmp_path / "heim"
    heim.mkdir()
    if aufbau == "user-dirs.dirs":
        (heim / "Dokumente").mkdir()
        (heim / ".config").mkdir()
        (heim / ".config" / "user-dirs.dirs").write_text(
            'XDG_DOCUMENTS_DIR="$HOME/Dokumente"\n')
    elif aufbau == "nur ~/Documents":
        (heim / "Documents").mkdir()

    umgebung = {k: v for k, v in os.environ.items()
                if k not in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_DOCUMENTS_DIR")}
    umgebung["HOME"] = str(heim)

    schale = subprocess.run(
        ["sh", "-c", f'. "{PACKAGING}/lib/common.sh"; benutzerdaten_dir'],
        capture_output=True, text=True, env=umgebung)
    assert schale.returncode == 0, schale.stderr

    python = subprocess.run(
        [sys.executable, "-c",
         "import sys; sys.path.insert(0, sys.argv[1]);"
         "from app import paths; print(paths.user_data_dir())", str(PROJECT_ROOT)],
        capture_output=True, text=True, env=umgebung)
    assert python.returncode == 0, python.stderr

    assert schale.stdout.strip() == python.stdout.strip()


@pytest.mark.parametrize("datei,starter", [
    ("a5120emu.desktop.in", "a5120emu"),
    ("k1520disktool.desktop.in", "k1520disktool"),
])
def test_desktop_exec_ist_gequotet(datei, starter):
    """Ein Pfad mit Leerzeichen macht ein ungequotetes ``Exec=`` unbrauchbar."""
    text = (PACKAGING / datei).read_text(encoding="utf-8")
    assert f'Exec="@ROOT@/bin/{starter}"' in text


# ─── Schutz des Zielverzeichnisses ───────────────────────────────────────────
#
# `--uninstall` löscht sein Ziel mit `rm -rf`.  Seit das Ziel ERFRAGT wird, ist
# ein Tippfehler („~") eine Katastrophe — diese Tests halten beide Riegel fest.
# Sie brauchen kein Netz: der Installer prüft, bevor er irgendetwas tut.

def _install_sh(prefix, heim, *extra):
    umgebung = {k: v for k, v in os.environ.items()
                if k not in ("XDG_DATA_HOME", "XDG_CONFIG_HOME")}
    umgebung["HOME"] = str(heim)
    return subprocess.run(["sh", str(PACKAGING / "install.sh"), "--prefix", str(prefix),
                           *extra], capture_output=True, text=True, env=umgebung,
                          timeout=120)


@nur_unix_installer
def test_installer_verweigert_das_heimatverzeichnis(tmp_path):
    heim = tmp_path / "heim"
    (heim / "Dokumente").mkdir(parents=True)
    out = _install_sh(heim, heim)
    assert out.returncode != 0
    assert "darf kein Installationsverzeichnis sein" in out.stdout + out.stderr
    assert (heim / "Dokumente").exists()


@nur_unix_installer
def test_installer_verweigert_fremdes_verzeichnis(tmp_path):
    heim = tmp_path / "heim"
    heim.mkdir()
    fremd = tmp_path / "fremd"
    fremd.mkdir()
    (fremd / "wichtig.txt").write_text("nicht anfassen")
    out = _install_sh(fremd, heim)
    assert out.returncode != 0
    assert "nicht leer" in out.stdout + out.stderr
    assert (fremd / "wichtig.txt").exists()


def _fake_installation(wurzel: Path, inventar=("bin", "app", "share", "venv")) -> Path:
    """Baut nach, was der Installer hinterlässt — samt Ausweis mit Inventar."""
    wurzel.mkdir(parents=True, exist_ok=True)
    for eintrag in inventar:
        (wurzel / eintrag).mkdir()
        (wurzel / eintrag / "inhalt").write_text("vom installer")
    (wurzel / "VERSION").write_text("test\n")
    marke = ["k1520emu test", "# Vom Installer angelegt"]
    marke += [f"eintrag {e}" for e in inventar] + ["eintrag VERSION"]
    (wurzel / ".k1520emu-installation").write_text("\n".join(marke) + "\n")
    return wurzel


@nur_unix_installer
def test_deinstallieren_laesst_eigene_dateien_stehen(tmp_path):
    """Entfernt wird das Inventar des Ausweises — nicht das Verzeichnis.

    Wer seine eigenen Disketten oder Notizen neben den Emulator legt, soll sie
    nach dem Deinstallieren wiederfinden.  Ein `rm -rf` auf die Wurzel wäre
    einfacher und nähme alles mit.
    """
    heim = tmp_path / "heim"
    heim.mkdir()
    wurzel = _fake_installation(tmp_path / "K1520emu")
    (wurzel / "meine_notizen.txt").write_text("wichtig")
    (wurzel / "meine_disketten").mkdir()
    (wurzel / "meine_disketten" / "eigen.hfe").write_bytes(b"x")

    out = _install_sh(wurzel, heim, "--uninstall")
    assert out.returncode == 0, out.stderr

    for weg in ("bin", "app", "share", "venv", "VERSION", ".k1520emu-installation"):
        assert not (wurzel / weg).exists(), f"{weg} hätte entfernt werden müssen"
    assert (wurzel / "meine_notizen.txt").read_text(encoding="utf-8") == "wichtig"
    assert (wurzel / "meine_disketten" / "eigen.hfe").exists()
    assert "bleibt stehen" in out.stdout
    assert "meine_notizen.txt" in out.stdout, "der Anwender erfährt nicht, was übrig ist"


@nur_unix_installer
def test_deinstallieren_raeumt_leere_wurzel_ganz_weg(tmp_path):
    """Liegt nichts Fremdes darin, verschwindet auch das Verzeichnis selbst."""
    heim = tmp_path / "heim"
    heim.mkdir()
    wurzel = _fake_installation(tmp_path / "K1520emu")
    # Laufzeitspuren des Emulators: das Protokoll liegt seit e6db12c unter
    # `logs/` im Arbeitsverzeichnis, die alte Schreibweise daneben.
    (wurzel / "logs").mkdir()
    (wurzel / "logs" / "k1520_20260809_120000.log").write_text("protokoll")
    (wurzel / "k1520_20260808_090000.log").write_text("aelteres protokoll")

    out = _install_sh(wurzel, heim, "--uninstall")
    assert out.returncode == 0, out.stderr
    assert not wurzel.exists(), out.stdout


@nur_unix_installer
def test_deinstallieren_folgt_keinem_pfad_im_ausweis(tmp_path):
    """Ein Eintrag ist ein NAME. „../…" darf nicht aus der Installation herauszeigen."""
    heim = tmp_path / "heim"
    heim.mkdir()
    wurzel = _fake_installation(tmp_path / "K1520emu")
    daneben = tmp_path / "fremd"
    daneben.mkdir()
    (daneben / "unbeteiligt.txt").write_text("nicht anfassen")
    marke = wurzel / ".k1520emu-installation"
    marke.write_text(marke.read_text(encoding="utf-8") + "eintrag ../fremd\n")

    out = _install_sh(wurzel, heim, "--uninstall")
    assert out.returncode == 0, out.stderr
    assert (daneben / "unbeteiligt.txt").exists(), "Ausweis führte aus der Installation heraus"
    assert "fragwürdiger Eintrag" in out.stdout + out.stderr


@nur_unix_installer
def test_deinstallieren_findet_die_installation_ueber_den_starter(tmp_path):
    """Ohne --prefix verrät der Starter in ~/.local/bin die Wurzel."""
    heim = tmp_path / "heim"
    (heim / ".local" / "bin").mkdir(parents=True)
    wurzel = _fake_installation(tmp_path / "woanders" / "K1520emu")
    (wurzel / "bin" / "a5120emu").write_text("#!/bin/sh\n")
    (heim / ".local" / "bin" / "a5120emu").symlink_to(wurzel / "bin" / "a5120emu")

    umgebung = {k: v for k, v in os.environ.items()
                if k not in ("XDG_DATA_HOME", "XDG_CONFIG_HOME")}
    umgebung["HOME"] = str(heim)
    out = subprocess.run(["sh", str(PACKAGING / "install.sh"), "--uninstall"],
                         capture_output=True, text=True, env=umgebung, timeout=120)
    assert out.returncode == 0, out.stderr
    assert str(wurzel) in out.stdout
    assert not wurzel.exists()


@nur_unix_installer
def test_deinstallieren_loescht_nur_eine_installation(tmp_path):
    """Ohne Ausweis wird nichts gelöscht — auch nicht, wenn der Starter dorthin zeigt."""
    heim = tmp_path / "heim"
    heim.mkdir()
    fremd = tmp_path / "Dokumente"
    fremd.mkdir()
    (fremd / "brief.txt").write_text("wichtig")
    out = _install_sh(fremd, heim, "--uninstall")
    assert out.returncode == 0, out.stderr
    assert "sieht nicht nach einer Installation aus" in out.stdout + out.stderr
    assert (fremd / "brief.txt").exists()


def test_slim_ist_gueltiges_python():
    import ast
    ast.parse((PACKAGING / "slim.py").read_text(encoding="utf-8"))


@nur_elf
def test_slim_liest_ldd_auch_bei_leerzeichen_im_pfad(tmp_path, monkeypatch):
    """Abgetrennt wird die Ladeadresse am Ende, nicht am ersten Leerzeichen.

    Am ersten Leerzeichen geschnitten blieb die Hülle leer, der
    Sicherheitsrückfall griff — und die Installation behielt ganz Qt (223 statt
    146 MB), sobald der Anwender „~/Emulator Test" als Ziel angab.
    """
    slim = _slim()
    libdir = tmp_path / "Emulator Test" / "lib"
    libdir.mkdir(parents=True)
    lib = libdir / "libQt6Core.so.6"
    lib.write_bytes(b"")

    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    fake = bin_dir / "ldd"
    fake.write_text('#!/bin/sh\nprintf "\\tlibQt6Core.so.6 => %s (0x00007f1234567000)\\n" '
                    f'"{lib}"\n')
    fake.chmod(0o755)
    monkeypatch.setenv("PATH", f"{bin_dir}{os.pathsep}{os.environ['PATH']}")

    assert slim.linked_libraries(tmp_path / "egal.so", libdir.resolve()) == {lib.resolve()}


@pytest.mark.skipif(not shutil.which("strip"), reason="strip nicht vorhanden")
def test_slim_strippt_bibliotheken_aber_keine_programme(tmp_path, monkeypatch):
    """`strip` zerstört den Interpreter von python-build-standalone.

    Er meldet „allocated section `.dynstr' not in segment", schreibt trotzdem
    eine Datei — und die startet nicht mehr.  Das gilt für jede Variante,
    ``--strip-debug`` spart dort nicht einmal Platz.  Deshalb werden nur
    gemeinsame Bibliotheken angefasst.
    """
    slim = _slim()
    bin_dir = tmp_path / "python" / "cpython-3.12" / "bin"
    bin_dir.mkdir(parents=True)
    (bin_dir / "python3.12").write_bytes(b"ELF")
    dynload = tmp_path / "python" / "cpython-3.12" / "lib" / "python3.12" / "lib-dynload"
    dynload.mkdir(parents=True)
    (dynload / "_dbm.so").write_bytes(b"ELF")
    site = tmp_path / "venv" / "lib" / "python3.12" / "site-packages"
    site.mkdir(parents=True)
    (site / "_yaml.so").write_bytes(b"ELF")

    angefasst = []
    monkeypatch.setattr(slim, "strip_datei", lambda d, c: angefasst.append(d))
    slim.slim_symbole(tmp_path, slim.Cutter(dry_run=False))

    namen = {p.name for p in angefasst}
    assert "python3.12" not in namen, "der Interpreter darf nicht gestrippt werden"
    assert {"_dbm.so", "_yaml.so"} <= namen


def test_slim_verweigert_fremdes_verzeichnis(tmp_path):
    """Ohne venv/ ist es keine Installation — slim.py darf dort nichts löschen."""
    opfer = tmp_path / "wichtig.txt"
    opfer.write_text("nicht loeschen")
    out = _sh(sys.executable, str(PACKAGING / "slim.py"), str(tmp_path))
    assert out.returncode != 0
    assert "Installation" in out.stderr
    assert opfer.exists()


def test_slim_behaelt_was_die_gui_importiert():
    """Die Bindungsliste muss decken, was `app/` tatsächlich importiert."""
    import re
    quelle = " ".join(p.read_text(encoding="utf-8") for p in (PROJECT_ROOT / "app").rglob("*.py"))
    benutzt = set(re.findall(r"PySide6\.([A-Za-z]+)", quelle))
    text = (PACKAGING / "slim.py").read_text(encoding="utf-8")
    behalten = set(re.findall(r'"(Qt[A-Za-z]+)"', text.split("PLUGINS_KEEP")[0]))
    fehlt = benutzt - behalten
    assert not fehlt, f"slim.py wuerde entfernen, was die GUI importiert: {sorted(fehlt)}"


# ─── Payload schnüren ────────────────────────────────────────────────────────

@requires_core
def test_payload_enthaelt_alles_zum_starten(tmp_path):
    """Ein vollständiger Schnür-Lauf gegen die gebaute Bibliothek.

    Prüft den Inhalt gegen die Liste dessen, was der Emulator zum Start
    braucht — fällt eine Datei aus dem Skript, schlägt das hier fehl und nicht
    erst beim Anwender.
    """
    out = _sh("sh", str(PACKAGING / "build_payload.sh"),
              "--skip-build", "--build-dir", str(PROJECT_ROOT / "build"),
              "--out", str(tmp_path), "--version", "test", "--disks", "none")
    assert out.returncode == 0, out.stdout + out.stderr

    stage = next(p for p in tmp_path.glob("k1520emu-test-*") if p.is_dir())
    for pflicht in [
        "install.sh", "launcher.sh", "slim.py", "a5120emu.desktop.in", "uv_pins.txt",
        "lib/common.sh", "requirements.lock", "VERSION", "README.md",
        "payload/bin/libk1520core.so",
        "payload/app/main.py", "payload/app/paths.py",
        "payload/app/core_binding/k1520.py", "payload/app/ui/main_window.py",
        "payload/share/k1520emu/formats.yaml",
        "payload/share/icons/a5120emu.svg",
        # k1520DiskTool: Bibliothek, Kommandozeile, Oberflaeche, Starter.
        # Ohne diese Zeilen laege app/disktool/ zwar im Paket (die ganze app/-
        # Ebene wird eingepackt), faende aber keine Bibliothek — das Werkzeug
        # startete beim Anwender mit „libk1520disk.so nicht gefunden".
        "payload/bin/libk1520disk.so",
        "payload/bin/k1520disktool-cli",
        "payload/app/disktool/main.py",
        "payload/app/disktool/archive.py",
        "payload/app/disktool/ui/main_window.py",
        "payload/app/core_binding/k1520disk.py",
        "disktool_launcher.sh", "k1520disktool.desktop.in",
    ]:
        assert (stage / pflicht).exists(), f"{pflicht} fehlt im Paket"

    assert os.access(stage / "install.sh", os.X_OK), "install.sh ist nicht ausführbar"
    assert not list(stage.rglob("__pycache__")), "Bytecode des Baurechners im Paket"

    archiv = next(tmp_path.glob("k1520emu-test-*.tar.gz"))
    with tarfile.open(archiv) as tf:
        namen = tf.getnames()
    assert all(n.startswith(stage.name) for n in namen), \
        "Archiv entpackt ohne gemeinsames Wurzelverzeichnis"
    pruefsumme = archiv.with_suffix(".gz.sha256").read_text(encoding="utf-8").split()[0]
    assert len(pruefsumme) == 64


@requires_core
def test_beispieldisketten_liegen_gepackt_im_paket(tmp_path):
    """Abbilder sind zu ~90 % Füllmuster und werden genau einmal gebraucht.

    Ungepackt lägen sie nach dem Erststart doppelt auf der Platte — in der
    Installation und beim Anwender.  Ausgepackt wird beim Seeden
    (``paths.seed_user_disks``), und zwar bitgleich.
    """
    import gzip

    out = _sh("sh", str(PACKAGING / "build_payload.sh"),
              "--skip-build", "--build-dir", str(PROJECT_ROOT / "build"),
              "--out", str(tmp_path), "--version", "test")
    assert out.returncode == 0, out.stdout + out.stderr

    stage = next(p for p in tmp_path.glob("k1520emu-test-*") if p.is_dir())
    disks = stage / "payload" / "share" / "disks"
    gepackt = sorted(disks.glob("*.hfe.gz"))
    assert gepackt, "keine gepackten Beispieldisketten im Paket"
    assert not list(disks.glob("*.hfe")), "ungepackte Abbilder im Paket"

    original = PROJECT_ROOT / "disks" / gepackt[0].stem
    with gzip.open(gepackt[0], "rb") as f:
        assert f.read() == original.read_bytes(), "gepacktes Abbild weicht ab"


@requires_core
def test_paket_traegt_keinen_pfad_des_baurechners(tmp_path):
    """Der einkompilierte Katalog-Fallback muss im Release leer sein.

    Sonst schleppte jede ausgelieferte Bibliothek den absoluten Pfad des
    Baurechners als Suchkandidaten mit sich herum (`K1520_FORMATS_DEFAULT`).
    Geprüft wird an einer frisch als Release gebauten Bibliothek.
    """
    build = tmp_path / "build"
    cfg = _sh("cmake", "-S", str(PROJECT_ROOT), "-B", str(build),
              "-DCMAKE_BUILD_TYPE=Release", "-DK1520_FORMATS_DEFAULT=",
              "-DBUILD_K1520_TESTS=OFF")
    assert cfg.returncode == 0, cfg.stderr
    bld = _sh("cmake", "--build", str(build), "--target", "k1520core", "-j4")
    assert bld.returncode == 0, bld.stderr[-3000:]

    roh = (build / "libk1520core.so").read_bytes()
    assert bytes(str(PROJECT_ROOT / "data" / "formats.yaml"), "utf-8") not in roh


# ─── Vollständige Installation (langsam, braucht Netz) ───────────────────────

voll = pytest.mark.skipif(
    not os.environ.get("K1520_PACKAGING_FULL"),
    reason="lädt ~120 MB — mit K1520_PACKAGING_FULL=1 einschalten",
)


@requires_core
@voll
def test_installation_laeuft_durch_und_startet(tmp_path):
    """Paket schnüren, in ein eigenes HOME installieren, GUI headless starten."""
    out = _sh("sh", str(PACKAGING / "build_payload.sh"),
              "--skip-build", "--build-dir", str(PROJECT_ROOT / "build"),
              "--out", str(tmp_path / "dist"), "--version", "test")
    assert out.returncode == 0, out.stdout + out.stderr
    stage = next(p for p in (tmp_path / "dist").glob("k1520emu-test-*") if p.is_dir())

    heim = tmp_path / "heim"
    heim.mkdir()
    # Ein Dokumentenordner wie auf einem eingerichteten Desktop — dorthin gehören
    # die Arbeitsdisketten, und der Name ist sprachabhängig.
    (heim / "Dokumente").mkdir()
    (heim / ".config").mkdir()
    (heim / ".config" / "user-dirs.dirs").write_text('XDG_DOCUMENTS_DIR="$HOME/Dokumente"\n')
    umgebung = {k: v for k, v in os.environ.items()
                if k not in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_DOCUMENTS_DIR")}
    umgebung["HOME"] = str(heim)

    # Eigenes Ziel — und zwar eines, das NICHT der Vorschlag ist: so ist zugleich
    # belegt, dass das Deinstallieren weiter unten die Installation ohne --prefix
    # wiederfindet (über den Starter in ~/.local/bin).
    ziel = heim / "Programme" / "A5120"
    inst = subprocess.run(["sh", str(stage / "install.sh"), "--prefix", str(ziel)],
                          capture_output=True, text=True, env=umgebung, timeout=1800)
    assert inst.returncode == 0, inst.stdout + inst.stderr
    assert "läuft" in inst.stdout, "Rauchtest des Installers hat nicht bestätigt"
    assert "Oberfläche: baut auf" in inst.stdout, \
        "Rauchtest hat das Fenster nicht gebaut — Qt-Plugins ungeprüft"
    # Der Rauchtest muss die INSTALLATION geprüft haben, nicht zufällig den
    # Baum, aus dem heraus er gestartet wurde (sys.path[0] = Arbeitsverzeichnis).
    assert str(ziel) in inst.stdout, "Rauchtest prüfte nicht die Installation"

    starter = ziel / "bin" / "a5120emu"
    assert starter.is_file() and os.access(starter, os.X_OK)
    assert (heim / ".local" / "share" / "applications" / "a5120emu.desktop").is_file()
    # Beispieldisketten liegen beim Anwender, nicht in der Installation — und
    # kommen dort AUSGEPACKT und bitgleich an (im Paket liegen sie gepackt).
    nutzer_disks = heim / "Dokumente" / "K1520emu" / "Disketten"
    ausgepackt = sorted(nutzer_disks.glob("*.hfe"))
    assert ausgepackt
    assert not list(nutzer_disks.glob("*.gz")), "gepackte Datei blieb beim Anwender liegen"
    for f in ausgepackt:
        assert f.read_bytes() == (PROJECT_ROOT / "disks" / f.name).read_bytes(), \
            f"{f.name} kam beschädigt beim Anwender an"

    # Platzbedarf: ohne das Schlankmachen wären es ~400 MB.  Gezählt wird wie
    # `du` — Verzeichnis-Symlinks nicht verfolgen, jede Datei nur einmal (uv legt
    # neben cpython-3.12.13-… einen Verweis cpython-3.12-… an).
    gesehen, bytes_ = set(), 0
    for wurzel, _, dateien in os.walk(ziel, followlinks=False):
        for name in dateien:
            st = os.lstat(os.path.join(wurzel, name))
            if (st.st_dev, st.st_ino) in gesehen:
                continue
            gesehen.add((st.st_dev, st.st_ino))
            bytes_ += st.st_size
    belegt = bytes_ / (1024 * 1024)
    assert belegt < MAX_INSTALL_MB, f"Installation belegt {belegt:.0f} MB"
    assert not (ziel / "tools").exists(), "uv blieb nach der Installation liegen"

    # Der Starter läuft aus der Installation heraus und löst alles dorthin auf.
    umgebung["QT_QPA_PLATFORM"] = "offscreen"
    ausk = subprocess.run([str(starter), "--paths"], capture_output=True, text=True,
                          env=umgebung, timeout=60)
    assert ausk.returncode == 0, ausk.stderr
    assert "Layout:            Installation" in ausk.stdout

    # Der Starter wechselt ins Benutzerdatenverzeichnis, bevor er den Emulator
    # startet: der Kern legt sein Protokoll unter `logs/` im ARBEITSVERZEICHNIS
    # an, und das wäre sonst der Ort, an dem der Anwender gerade zufällig steht
    # — beim Start über das Startmenü das Heimatverzeichnis.
    von_wo = tmp_path / "irgendwo"
    von_wo.mkdir()
    daten = nutzer_disks.parent
    lauf = subprocess.Popen([str(starter)], cwd=von_wo, env=umgebung,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        frist = time.monotonic() + 30
        while time.monotonic() < frist and not (daten / "logs").exists():
            time.sleep(0.2)
    finally:
        lauf.terminate()
        lauf.wait(timeout=30)
    assert (daten / "logs").exists(), "Protokoll nicht bei den Benutzerdaten gelandet"
    assert not (von_wo / "logs").exists(), \
        "Protokoll landete im Arbeitsverzeichnis des Aufrufers"

    # Der Ausweis sagt, was dem Installer gehört — daran hängt das Deinstallieren.
    ausweis = (ziel / ".k1520emu-installation").read_text(encoding="utf-8")
    for eintrag in ("bin", "app", "share", "venv", "python"):
        assert f"eintrag {eintrag}" in ausweis, f"{eintrag} fehlt im Inventar"

    # Eine frische Installation enthält nichts, was nicht hineingehört — der Kern
    # legt sein Protokoll im Arbeitsverzeichnis an, und das ist beim Rauchtest
    # genau hier.
    assert not (ziel / "logs").exists(), "Protokollverzeichnis des Rauchtests blieb liegen"
    assert not list(ziel.glob("k1520_*.log")), "Protokoll des Rauchtests blieb liegen"

    # Noch einmal — und zwar OHNE --prefix: das ist der Update-Fall.  Der
    # Installer muss die bestehende Installation über den Starter finden, sonst
    # legte er eine zweite am Standardort an und ließe die alte verwaisen.
    # `uv venv` verweigert ein vorhandenes venv, deshalb wird es erneuert.
    erneut = subprocess.run(["sh", str(stage / "install.sh"), "-y"],
                            capture_output=True, text=True, env=umgebung, timeout=1800)
    assert erneut.returncode == 0, erneut.stdout + erneut.stderr
    assert str(ziel) in erneut.stdout, "Update fand die bestehende Installation nicht"
    assert not (heim / "K1520emu").exists(), "Update legte eine zweite Installation an"
    assert "erneuert" in erneut.stdout, "bestehende Laufzeitumgebung nicht erneuert"
    assert "läuft" in erneut.stdout

    # Deinstallieren OHNE --prefix: der Pfad kommt aus dem Starter-Symlink.
    deinst = subprocess.run(["sh", str(stage / "install.sh"), "--uninstall"],
                            capture_output=True, text=True, env=umgebung, timeout=300)
    assert deinst.returncode == 0, deinst.stderr
    assert not ziel.exists(), "Deinstallieren fand die Installation nicht"
    assert any(nutzer_disks.glob("*.hfe")), "Deinstallieren hat Anwenderdisketten gelöscht"
    assert not (heim / ".local" / "bin" / "python3.12").exists(), \
        "uv hat einen Python-Symlink im PATH des Anwenders hinterlassen"


# ─── k1520DiskTool im Paket ──────────────────────────────────────────────────
#
# Das Werkzeug kam nach der Paketierung dazu.  Die `app/`-Ebene wird als Ganzes
# eingepackt, seine Oberflaeche lag also von selbst im Paket — die BIBLIOTHEK
# und die Kommandozeile aber nicht, und ohne sie startet es beim Anwender nicht.
# Diese Faelle halten beides fest.

@requires_core
def test_disktool_findet_seine_bibliothek_im_installationslayout(tmp_path):
    """Die Auflösung muss ``<wurzel>/bin`` finden, nicht nur ``build/``.

    Der eigentliche Fallstrick: die Bindung suchte ihre Bibliothek anfangs
    selbst in ``build/`` — in einer Installation liegt sie in ``bin/``, und die
    Suche liefe ins Leere.  Seit sie über :mod:`app.paths` geht, gilt für beide
    Bibliotheken derselbe Weg; dieser Test hält das fest.
    """
    wurzel = tmp_path / "installation"
    (wurzel / "bin").mkdir(parents=True)
    (wurzel / "app").mkdir()
    for name in ("libk1520core.so", "libk1520disk.so"):
        (wurzel / "bin" / name).write_bytes(b"\x7fELF-attrappe")

    umgebung = dict(os.environ, K1520_HOME=str(wurzel))
    out = _sh(sys.executable, "-c",
              "import sys; sys.path.insert(0, sys.argv[1]);"
              "from app import paths;"
              "print(paths.is_installed_layout());"
              "print(paths.core_library());"
              "print(paths.disk_library())",
              str(PROJECT_ROOT), env=umgebung)
    assert out.returncode == 0, out.stderr
    erkannt, kern, werkzeug = out.stdout.split()
    assert erkannt == "True"
    assert kern == str(wurzel / "bin" / "libk1520core.so")
    assert werkzeug == str(wurzel / "bin" / "libk1520disk.so"), \
        "die DiskTool-Bibliothek wird im Installationslayout nicht gefunden"


@requires_core
def test_disktool_kommandozeile_laeuft_aus_dem_paket(tmp_path):
    """Das mitgelieferte Programm muss ohne den Quellbaum arbeiten.

    Geprüft wird der geschnürte Stand, nicht ``build/``: Läuft `formats`, dann
    hat das Programm auch den Formatkatalog des Pakets gefunden.
    """
    out = _sh("sh", str(PACKAGING / "build_payload.sh"),
              "--skip-build", "--build-dir", str(PROJECT_ROOT / "build"),
              "--out", str(tmp_path), "--version", "test", "--disks", "none")
    assert out.returncode == 0, out.stdout + out.stderr

    stage = next(p for p in tmp_path.glob("k1520emu-test-*") if p.is_dir())
    cli = stage / "payload" / "bin" / "k1520disktool-cli"
    assert os.access(cli, os.X_OK), "die Kommandozeile ist nicht ausführbar"

    hilfe = _sh(str(cli), "--help")
    assert hilfe.returncode == 0, hilfe.stderr
    assert "k1520disktool" in hilfe.stdout

    formate = _sh(str(cli), "formats", cwd=stage / "payload")
    assert formate.returncode == 0, formate.stderr
    for erwartet in ("cpa780", "udos_ds77"):
        assert erwartet in formate.stdout, \
            f"'{erwartet}' fehlt — der Formatkatalog des Pakets wurde nicht gefunden"


def test_installer_legt_beide_starter_an():
    """`install.sh` muss Emulator UND Werkzeug einen Starter geben."""
    text = (PACKAGING / "install.sh").read_text(encoding="utf-8")
    assert 'ersetze_root "$SELF_DIR/launcher.sh"' in text
    assert 'ersetze_root "$SELF_DIR/disktool_launcher.sh"' in text
    assert '> "$PREFIX/bin/k1520disktool"' in text
    assert 'k1520disktool.desktop.in' in text


def test_deinstallieren_raeumt_auch_das_werkzeug_weg():
    """Die Aufräumliste muss beide Namen kennen.

    `--uninstall` entfernt genau die Namen aus einer Liste. Stünde dort nur die
    Maschine, bliebe der Starter des Werkzeugs samt Startmenü-Eintrag als Leiche
    zurück und zeigte auf ein gelöschtes Verzeichnis.
    """
    text = (PACKAGING / "install.sh").read_text(encoding="utf-8")
    assert 'WERKZEUGE="k1520disktool"' in text
    assert 'STARTER="$MASCHINEN $WERKZEUGE"' in text
    assert 'for _m in $STARTER; do' in text, \
        "die Aufräumschleife läuft noch über $MASCHINEN und übersieht das Werkzeug"

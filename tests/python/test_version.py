"""Versionen und Releases (doc/ci_pipeline.md §7) — ``VERSION``, ``tools/version.py``,
``app/version.py`` und der Befehl ``tools/dev.sh release``.

Alles läuft ohne Kern und ohne Qt: die Ableitung der Bauversion wird an
Wegwerf-Repositories in ``tmp_path`` durchgespielt, ``dev.sh release`` nur mit
``--trocken`` (nichts wird getaggt, nichts gepusht).
"""

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from conftest import PROJECT_ROOT

from app import version as v

VERSION_PY = PROJECT_ROOT / "tools" / "version.py"

# Wegwerf-Repos dürfen weder signieren noch auf die Benutzerkonfiguration angewiesen sein.
_GIT = ["git", "-c", "user.name=Test", "-c", "user.email=t@example.org",
        "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", "-c", "init.defaultBranch=main"]


def git(repo: Path, *args: str) -> str:
    r = subprocess.run([*_GIT, "-C", str(repo), *args], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return r.stdout.strip()


def neues_repo(tmp_path: Path, basis: str = "0.3.0-beta") -> Path:
    repo = tmp_path / "repo"
    repo.mkdir()
    git(repo, "init", "-q")
    (repo / "VERSION").write_text(basis + "\n")
    git(repo, "add", "VERSION")
    git(repo, "commit", "-q", "-m", "start")
    return repo


def cli(*args: str, wurzel: Path = None):
    cmd = [sys.executable, str(VERSION_PY), *args]
    if wurzel is not None:
        cmd += ["--wurzel", str(wurzel)]
    return subprocess.run(cmd, capture_output=True, text=True)


# ─── Die Datei VERSION ───────────────────────────────────────────────────────

def test_version_datei_hat_eine_zeile_und_gueltige_grammatik():
    text = (PROJECT_ROOT / "VERSION").read_text(encoding="utf-8")
    assert text.endswith("\n") and text.count("\n") == 1, "genau eine Zeile"
    assert v.zerlege_basis(text)                        # wirft bei ungültiger Grammatik


@pytest.mark.parametrize("text", ["0.3.0", "0.3.0-beta", "10.20.30-rc", "1.0.0"])
def test_basis_grammatik_gueltig(text):
    v.zerlege_basis(text)


@pytest.mark.parametrize("text", ["0.3", "0.3.0-beta.1", "0.3.0-alpha", "v0.3.0", "0.3.0+dev",
                                  "0.3.0-beta-1", "", "a.b.c"])
def test_basis_grammatik_ungueltig(text):
    with pytest.raises(v.VersionsFehler):
        v.zerlege_basis(text)


def test_cmake_und_python_lesen_dieselbe_basis():
    """Der Kern (CMake) liest ``VERSION`` selbst; die Grammatik dort muss der hier gleichen."""
    cmake = (PROJECT_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "file(STRINGS" in cmake and "CMAKE_CONFIGURE_DEPENDS" in cmake
    assert "K1520_VERSION_VOLL" in cmake and "+dev" in cmake
    assert (PROJECT_ROOT / "core" / "version.h.in").is_file()
    assert not (PROJECT_ROOT / "core" / "version.h").exists(), "version.h wird erzeugt, nicht eingecheckt"


# ─── Bauversion ──────────────────────────────────────────────────────────────

def test_bau_ohne_tag_traegt_den_hash(tmp_path):
    repo = neues_repo(tmp_path)
    kurz = git(repo, "rev-parse", "--short=7", "HEAD")
    assert v.bauversion(repo) == f"0.3.0-beta+g{kurz}"
    assert cli("--bau", wurzel=repo).stdout.strip() == f"0.3.0-beta+g{kurz}"


def test_bau_bei_unsauberem_baum_endet_auf_dirty(tmp_path):
    repo = neues_repo(tmp_path)
    (repo / "neu.txt").write_text("x")
    assert v.bauversion(repo).endswith(".dirty")
    (repo / "VERSION").write_text("0.3.0-beta\n# geaendert\n")
    assert v.bauversion(repo).endswith(".dirty")


def test_bau_ohne_git_ist_unbekannt(tmp_path):
    ordner = tmp_path / "quellarchiv"
    ordner.mkdir()
    (ordner / "VERSION").write_text("0.3.0-beta\n")
    assert v.bauversion(ordner) == "0.3.0-beta+unbekannt"


def test_bau_mit_passendem_tag_ist_das_tag_ohne_v(tmp_path):
    repo = neues_repo(tmp_path)
    git(repo, "tag", "-a", "v0.3.0-beta.2", "-m", "Vorabversion")        # annotiert, wie dev.sh es setzt
    assert v.bauversion(repo) == "0.3.0-beta.2"
    assert cli("--bau", wurzel=repo).stdout.strip() == "0.3.0-beta.2"


def test_bau_ignoriert_ein_tag_das_nicht_zur_basis_passt(tmp_path):
    repo = neues_repo(tmp_path)
    git(repo, "tag", "v0.4.0-beta.1")
    git(repo, "tag", "v0.3.0-rc.1")
    git(repo, "tag", "irgendwas")
    assert v.bauversion(repo).startswith("0.3.0-beta+g")


def test_bau_nimmt_von_mehreren_passenden_tags_das_hoechste(tmp_path):
    repo = neues_repo(tmp_path)
    git(repo, "tag", "v0.3.0-beta.2")
    git(repo, "tag", "v0.3.0-beta.10")
    assert v.bauversion(repo) == "0.3.0-beta.10"


# ─── Windows-Dateiversion ────────────────────────────────────────────────────

@pytest.mark.parametrize("bau, erwartet", [
    ("0.3.0-beta.1", "0.3.0.1"),
    ("0.3.0-beta.49", "0.3.0.49"),
    ("0.3.0-rc.1", "0.3.0.51"),
    ("0.3.0", "0.3.0.100"),
    ("0.3.0-beta+g1a2b3c4", "0.3.0.0"),
    ("0.3.0-beta+g1a2b3c4.dirty", "0.3.0.0"),
    ("0.3.0-beta+dev", "0.3.0.0"),
    ("0.3.0+unbekannt", "0.3.0.0"),
])
def test_windows_zahl(bau, erwartet):
    assert v.windows_zahl(bau) == erwartet


def test_windows_zahl_ordnet_beta_rc_endfassung():
    zahlen = [v.windows_zahl(x) for x in ("0.3.0-beta.1", "0.3.0-beta.9", "0.3.0-rc.1", "0.3.0-rc.3", "0.3.0")]
    schluessel = [tuple(int(t) for t in z.split(".")) for z in zahlen]
    assert schluessel == sorted(schluessel) and len(set(schluessel)) == len(schluessel)


# ─── Beispielordner ──────────────────────────────────────────────────────────

@pytest.mark.parametrize("bau, ordner", [
    ("0.3.0", "Beispieldisketten_v03"),
    ("0.3.2", "Beispieldisketten_v03"),               # Patch-Release: kein neuer Ordner
    ("0.3.0-beta.2", "Beispieldisketten_v03-beta.2"),
    ("0.3.0-rc.1", "Beispieldisketten_v03-rc.1"),
    ("0.3.0-beta+g1a2b3c4", "Beispieldisketten_v03-test"),
    ("0.3.0-beta+dev", "Beispieldisketten_v03-test"),
    ("1.2.0", "Beispieldisketten_v102"),
    ("unbekannt", "Beispieldisketten_unbekannt"),
])
def test_beispielordner(bau, ordner):
    assert v.beispielordner(bau) == ordner


def test_vorabversion_erkennung():
    assert v.ist_vorabversion("0.3.0-beta.1") and v.ist_vorabversion("0.3.0-beta+g1")
    assert not v.ist_vorabversion("0.3.0") and not v.ist_vorabversion("0.3.0+g1")


# ─── Tag prüfen (release.yml) ────────────────────────────────────────────────

@pytest.mark.parametrize("basis, tag, passt", [
    ("0.3.0-beta", "v0.3.0-beta.1", True),
    ("0.3.0-beta", "v0.3.0-beta.12", True),
    ("0.3.0-rc", "v0.3.0-rc.1", True),
    ("0.3.0", "v0.3.0", True),
    ("0.3.0-beta", "v0.3.0-rc.1", False),         # falsche Stufe
    ("0.3.0-beta", "v0.3.0", False),              # Endfassung braucht Basis ohne Zusatz
    ("0.3.0", "v0.3.0-beta.1", False),
    ("0.3.0-beta", "v0.4.0-beta.1", False),       # falsche Zahl
    ("0.3.0-beta", "v0.3.0-beta", False),         # laufende Nummer fehlt
    ("0.3.0-beta", "v0.3.0-beta.0", False),
    ("0.3.0-beta", "vorab", False),
])
def test_tag_passt(basis, tag, passt):
    assert v.tag_passt(tag, basis) is passt


def test_pruefe_tag_ueber_die_kommandozeile(tmp_path):
    repo = neues_repo(tmp_path)
    assert cli("--pruefe-tag", "v0.3.0-beta.1", wurzel=repo).returncode == 0
    r = cli("--pruefe-tag", "v0.4.0-beta.1", wurzel=repo)
    assert r.returncode != 0 and "passt nicht zu VERSION" in r.stderr


def test_vorab_ueber_die_kommandozeile(tmp_path):
    repo = neues_repo(tmp_path)
    assert cli("--vorab", wurzel=repo).returncode == 0              # 0.3.0-beta+g…
    (repo / "VERSION").write_text("0.3.0\n")
    git(repo, "commit", "-q", "-am", "Version 0.3.0")
    assert cli("--vorab", wurzel=repo).returncode != 0              # Endfassung
    git(repo, "tag", "v0.3.0")
    assert cli("--bau", wurzel=repo).stdout.strip() == "0.3.0"


# ─── Ordnung und Release-Prüfung ─────────────────────────────────────────────

def test_ordnung_beta_rc_endfassung():
    ordnung = ["0.3.0-beta.1", "0.3.0-beta.2", "0.3.0-beta.10", "0.3.0-rc.1", "0.3.0",
               "0.3.1-beta.1", "0.4.0-beta.1", "0.4.0"]
    assert sorted(reversed(ordnung), key=v.sortierschluessel) == ordnung


def test_release_version_muss_neu_und_groesser_sein():
    tags = ["v0.1.0", "v0.2.0", "vor-merge-origin-main"]
    v.pruefe_neue_version("0.3.0-beta.1", tags)
    v.pruefe_neue_version("0.2.1", tags)
    for schlecht in ("0.2.0", "0.1.5", "0.2.0-rc.1"):
        with pytest.raises(v.VersionsFehler):
            v.pruefe_neue_version(schlecht, tags)
    with pytest.raises(v.VersionsFehler):               # beta nach rc derselben Basis
        v.pruefe_neue_version("0.3.0-beta.2", tags + ["v0.3.0-rc.1"])
    with pytest.raises(v.VersionsFehler):               # Grammatik
        v.pruefe_neue_version("0.3.0-beta", tags)


def test_basis_zu_und_naechste_basis():
    assert v.basis_zu("0.3.0-beta.1") == "0.3.0-beta"
    assert v.basis_zu("0.3.0-rc.4") == "0.3.0-rc"
    assert v.basis_zu("0.3.0") == "0.3.0"
    assert v.naechste_basis("0.3.0") == "0.4.0-beta"
    assert v.naechste_basis("0.3.1") == "0.3.2-beta"
    with pytest.raises(v.VersionsFehler):
        v.naechste_basis("0.3.0-beta.1")


# ─── Laufzeit: app/version.py::fassung ───────────────────────────────────────

def test_fassung_in_der_installation_ist_die_erste_angabe_der_version_datei(tmp_path):
    (tmp_path / "VERSION").write_text("0.3.0-beta+g1a2b3c4 (linux-x86_64, 2026-10-10, Python 3.12)\n")
    assert v.fassung(tmp_path) == "0.3.0-beta+g1a2b3c4"


def test_fassung_im_quellbaum_ist_die_bauversion():
    assert v.fassung(PROJECT_ROOT).startswith(v.lies_basis(PROJECT_ROOT))


# ─── tools/dev.sh release (nur --trocken) ────────────────────────────────────

pytestmark_bash = pytest.mark.skipif(
    sys.platform.startswith("win") or shutil.which("bash") is None,
    reason="dev.sh ist ein Bash-Skript")


def release_repo(tmp_path: Path) -> Path:
    """Wegwerf-Repo mit den drei Dateien, die ``dev.sh release`` braucht."""
    repo = neues_repo(tmp_path)
    for rel in ("tools/dev.sh", "tools/version.py", "app/version.py"):
        ziel = repo / rel
        ziel.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(PROJECT_ROOT / rel, ziel)
    git(repo, "add", ".")
    git(repo, "commit", "-q", "-m", "release-Werkzeug")
    git(repo, "tag", "v0.2.0")
    return repo


def release(repo: Path, *args: str):
    return subprocess.run(["bash", "tools/dev.sh", "release", *args, "--trocken"],
                          cwd=repo, capture_output=True, text=True, stdin=subprocess.DEVNULL)


@pytestmark_bash
def test_release_trocken_vorabversion(tmp_path):
    repo = release_repo(tmp_path)
    vorher = git(repo, "rev-parse", "HEAD")
    r = release(repo, "0.3.0-beta.1")
    assert r.returncode == 0, r.stdout + r.stderr
    assert "git tag -a v0.3.0-beta.1" in r.stdout
    assert "git push origin main v0.3.0-beta.1" in r.stdout
    assert "tools/dev.sh test" in r.stdout and "test-format" not in r.stdout     # nur EINE Runde
    # Trocken heisst: nichts hat sich bewegt.
    assert git(repo, "rev-parse", "HEAD") == vorher
    assert "v0.3.0-beta.1" not in git(repo, "tag")
    assert (repo / "VERSION").read_text() == "0.3.0-beta\n"


@pytestmark_bash
def test_release_trocken_endfassung_hat_vier_runden_und_folgefassung(tmp_path):
    repo = release_repo(tmp_path)
    r = release(repo, "0.3.0")
    assert r.returncode == 0, r.stdout + r.stderr
    for runde in ("test", "test-format", "test-matrix", "win"):
        assert f"tools/dev.sh {runde}\n" in r.stdout.replace("\x1b[0m", "")
    assert "echo 0.3.0 > VERSION" in r.stdout
    assert 'git commit -q -m Version 0.3.0' in r.stdout
    assert "echo 0.4.0-beta > VERSION" in r.stdout
    assert "Naechste Fassung: 0.4.0-beta" in r.stdout
    assert "v0.3.0" not in git(repo, "tag")


@pytestmark_bash
def test_release_wechsel_auf_rc_committet_die_basis(tmp_path):
    repo = release_repo(tmp_path)
    r = release(repo, "0.3.0-rc.1")
    assert r.returncode == 0, r.stdout + r.stderr
    assert "echo 0.3.0-rc > VERSION" in r.stdout


@pytestmark_bash
@pytest.mark.parametrize("version, grund", [
    ("0.2.0", "existiert bereits"),
    ("0.1.9", "nicht größer"),
    ("0.3.0-beta", "keine Versionsangabe"),
    ("quatsch", "keine Versionsangabe"),
])
def test_release_lehnt_ungueltige_versionen_ab(tmp_path, version, grund):
    repo = release_repo(tmp_path)
    r = release(repo, version, "--zweig-egal")
    assert r.returncode != 0 and grund in r.stderr + r.stdout


@pytestmark_bash
def test_release_verlangt_main_und_sauberen_baum(tmp_path):
    repo = release_repo(tmp_path)
    git(repo, "checkout", "-q", "-b", "nebenzweig")
    r = release(repo, "0.3.0-beta.1")
    assert r.returncode != 0 and "nur von main" in r.stdout + r.stderr
    assert release(repo, "0.3.0-beta.1", "--zweig-egal").returncode == 0     # abschaltbar

    (repo / "unsauber.txt").write_text("x")
    r = release(repo, "0.3.0-beta.1", "--zweig-egal")
    assert r.returncode != 0 and "nicht sauber" in r.stdout + r.stderr


@pytestmark_bash
def test_release_ohne_version_ist_ein_fehler(tmp_path):
    repo = release_repo(tmp_path)
    r = release(repo)
    assert r.returncode != 0

#!/usr/bin/env python3
"""pofo_archive.py -- P8000-CopyQM-Sammlung (pofo.de) als DiskTool-Archive ablegen.

Aufruf (aus dem Repo, nach `tools/dev.sh build`):

    python3 tools/p8000/pofo_archive.py \\
        --quelle ~/projects/robotron/P8000/discs \\
        --meta   ~/projects/robotron/P8000/cqm_ref \\
        --ziel   ~/projects/robotron/P8000/archive  [--nur NAME ...] [--trocken]

Je Abbild `<name>.cqm` entsteht `<ziel>/<name>.zip` genau so, wie das DiskTool es
macht (`app.disktool.archive.create_archive`): .hfe + extrahierte Dateien + Beiblaetter
+ `<name>.txt` + `diskarchive.yaml`.  Dazu die Herkunft (`provenance:`) aus
`pofo_meta.json` und die Beschreibung des Satzes als Beiblatt
`beschreibung_pofo_<set_id>.txt`.  Mehrdisketten-tar-Saetze (Namen in SAETZE) bekommen
zusaetzlich `<ziel>/saetze/<satz>_satz.zip` aus `DiskTool.open_tar_satz`, in dem die
Dateien ueber die Diskettengrenzen vollstaendig sind.  Zum Schluss wird jedes Archiv
nachgeprueft und `<ziel>/UEBERSICHT.md` geschrieben.

Die Quellen werden nur gelesen.  Die Notiztexte der Webseite sind fremde Daten: sie
werden kopiert (Latin-1 -> UTF-8), nie ausgewertet.

Hinweis zum Interpreter: `python3 -I` ignoriert PYTHONPATH und das Skriptverzeichnis;
das Skript setzt den Repo-Wurzelpfad deshalb selbst in `sys.path`.  Die Kernbibliothek
`libk1520disk.so` findet `app/core_binding` ueber `app/paths.py` (K1520_LIB oder
`<repo>/build`).
"""

import argparse
import collections
import datetime
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import yaml  # noqa: E402

from app.core_binding.k1520disk import DiskTool, K1520DiskError  # noqa: E402
from app.disktool.archive import INVENTAR_DATEI, create_archive  # noqa: E402

WEB_URL = "http://www.pofo.de/P8000/arch/"

#: Mehrdisketten-tar-Saetze: Name des .tar-Gegenstuecks -> Einzel-.cqm in Lesereihenfolge.
SAETZE = {
    "ariane": ["ariane1", "ariane2"],
    "konsul": ["konsul1", "konsul2", "konsul3"],
    "lcc": ["lcc1", "lcc2"],
    "psc": ["psc1", "psc2"],
    "w11data": [f"w11data{i}" for i in range(1, 8)],
    "w21pasc": ["w21pasc1", "w21pasc2"],
    "wegaf77": ["wegaf771", "wegaf772"],
    "wegbas": ["wegbas12", "wegbas22"],
    # Aus dem Befund abgeleitet (kein .tar-Gegenstueck, nicht auf der Webseite):
    # ingdb11 endet mitten in einer Datei, ingdb12 beginnt mit deren Fortsetzung.
    "ingdb_11_12": ["ingdb11", "ingdb12"],
}
ABGELEITET = {"ingdb_11_12"}

#: Von Hand nachgepruefte Befunde je Abbild (stehen in UEBERSICHT.md unter Besonderheiten).
ANMERKUNGEN = {
    "curspdoc": "Bitkipper im .tar: pascal_doc/pascal.dr Byte 116761 ist im .tar 0x78, im CQM "
                "0x79; der CQM-Pruefwert stimmt -> das .tar ist dort fehlerhaft (doc/merkposten/cqm.md Nr. 3)",
    "konsul2": "Mitte einer Datei: setzt kon/doku/ln4.dok von konsul1 fort; allein ohne tar-Kopf, "
               "erst im Satz konsul_satz lesbar",
    "wdos_dcp": "MS-DOS-Diskette (FAT12, Kennung 'DCP  3.3'); das DiskTool liest kein FAT, die Dateien "
                "stammen aus dem kleinen FAT12-Leser des Skripts",
    "w31usr7": "5 Befunde 'Gefahr' wega.block.frei_belegt an getty.M.z (Block 982-986 zugleich in der "
               "Freiliste): Dateisystem-Eigenheit der Diskette, Dateien ausgelesen",
}

SCHWERE = {0: "Hinweis", 1: "Warnung", 2: "Fehler", 3: "Gefahr"}


def sha256_datei(p) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# Webseiten-Metadaten
# ---------------------------------------------------------------------------

class Meta:
    def __init__(self, ordner: Path):
        self.ordner = ordner
        self.eintraege = json.loads((ordner / "pofo_meta.json").read_text(encoding="utf-8"))
        idx = ordner / "pofo_arch_index.html"
        stamp = idx if idx.exists() else ordner / "pofo_meta.json"
        self.abruf = datetime.datetime.fromtimestamp(stamp.stat().st_mtime).date().isoformat()
        self.satzglieder = collections.defaultdict(list)
        for k, v in self.eintraege.items():
            if k.endswith(".cqm") and v.get("set_id") is not None:
                self.satzglieder[v["set_id"]].append(k)

    def von(self, cqm_name: str) -> dict:
        return self.eintraege.get(cqm_name) or {}

    def notiz(self, set_id):
        """Rohbytes der Notizseite oder None."""
        if set_id is None:
            return None
        p = self.ordner / "notes" / f"{set_id}.txt"
        return p.read_bytes() if p.is_file() else None


def provenance_von(meta: Meta, cqm_name: str, quelle: Path) -> dict:
    m = meta.von(cqm_name)
    sid = m.get("set_id")
    prov = {
        "source_url": WEB_URL,
        "retrieved": meta.abruf,
        "set_name": m.get("name", ""),
        "set_id": sid,
        "version": m.get("version", ""),
        "file_name": cqm_name,
        "file_type": m.get("filetype", ""),
        "file_format": m.get("fileformat", ""),
        "web_label": m.get("label", ""),
        "source_sha256": sha256_datei(quelle),
    }
    if sid is not None:
        prov["note_url"] = f"{WEB_URL}?action=display_note&set_id={sid}"
        prov["set_members"] = sorted(meta.satzglieder.get(sid, []))
    if not m:
        prov["web_entry"] = "kein Eintrag auf der Webseite"
    return prov


def beschreibung_beilage(meta: Meta, sid) -> dict:
    """{'beschreibung_pofo_<id>.txt': Text} oder {}."""
    roh = meta.notiz(sid)
    if roh is None:
        return {}
    text = roh.decode("latin-1")            # Latin-1 -> UTF-8, Zeilenumbrueche unveraendert
    kopf = (f"Quelle:   {WEB_URL}?action=display_note&set_id={sid}\n"
            f"Abgerufen: {meta.abruf}\n"
            f"Der folgende Text stammt unveraendert von der Webseite (Latin-1 nach UTF-8\n"
            f"umgesetzt); er wird nur aufbewahrt, nicht ausgewertet.\n"
            + "=" * 70 + "\n")
    return {f"beschreibung_pofo_{sid}.txt": kopf + text}


def auszug(meta: Meta, sid, zeilen=4) -> str:
    roh = meta.notiz(sid)
    if roh is None:
        return ""
    ls = [z.strip() for z in roh.decode("latin-1").splitlines() if z.strip()]
    return " / ".join(ls[:zeilen])[:200]


# ---------------------------------------------------------------------------
# Befunde / Hinweise
# ---------------------------------------------------------------------------

def befunde_von(d: DiskTool) -> list:
    r = d.check(voll=True)
    return [(f.id, f.severity, f.object, f.text) for f in r.findings]


def hinweise_aus(befunde, satz_info: str = "") -> list:
    z = []
    if satz_info:
        z.append(satz_info)
    for fid, sev, obj, text in befunde:
        if sev >= 1 or fid in ("tar.fortsetzung", "tar.abgeschnitten"):
            z.append(f"[{SCHWERE.get(sev, sev)}] {fid} ({obj}): {text}")
    return z


# ---------------------------------------------------------------------------
# FAT12 (billig, nur lesen) fuer wdos_dcp
# ---------------------------------------------------------------------------

def fat12_dateien(img: bytes) -> dict:
    """{Pfad: Bytes} aus einem FAT12-Abbild; {} wenn das BPB unplausibel ist."""
    le = lambda o, n: int.from_bytes(img[o:o + n], "little")
    bps, spc, res, nfat = le(11, 2), img[13], le(14, 2), img[16]
    nroot, tot, spf = le(17, 2), le(19, 2), le(22, 2)
    if bps not in (512, 1024) or spc == 0 or nfat == 0 or spf == 0:
        return {}
    fat0 = res * bps
    rootstart = (res + nfat * spf) * bps
    datastart = rootstart + nroot * 32
    cl_bytes = spc * bps
    fat = img[fat0:fat0 + spf * bps]

    def nxt(c):
        v = int.from_bytes(fat[c * 3 // 2:c * 3 // 2 + 2], "little")
        return v >> 4 if c & 1 else v & 0xFFF

    def kette(c):
        out, guard = [], 0
        while 2 <= c < 0xFF0 and guard < 5000:
            out.append(c)
            c = nxt(c)
            guard += 1
        return out

    def lies(c, groesse=None):
        b = b"".join(img[datastart + (k - 2) * cl_bytes: datastart + (k - 1) * cl_bytes]
                     for k in kette(c))
        return b if groesse is None else b[:groesse]

    ergebnis = {}

    def verz(daten, prefix):
        for o in range(0, len(daten), 32):
            e = daten[o:o + 32]
            if len(e) < 32 or e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 0x08:
                continue                      # geloescht, LFN, Datentraegername
            name = e[:8].decode("latin-1").rstrip()
            ext = e[8:11].decode("latin-1").rstrip()
            voll = prefix + name + ("." + ext if ext else "")
            c = int.from_bytes(e[26:28], "little")
            if e[11] & 0x10:
                if name in (".", ".."):
                    continue
                verz(lies(c), voll + "/")
            else:
                ergebnis[voll] = lies(c, int.from_bytes(e[28:32], "little"))

    verz(img[rootstart:datastart], "")
    return ergebnis


# ---------------------------------------------------------------------------
# GNU tar als Gegenprobe
# ---------------------------------------------------------------------------

def norm_name(n: str) -> str:
    n = n.lstrip("/").replace(":", "_")      # wie der Auszug (Wirtsdateisystem)
    while n.startswith("./"):
        n = n[2:]
    return "/".join("__" if t == ".." else t for t in n.split("/"))


def tar_liste(tarpfad: Path):
    """[(name, groesse)] der regulaeren Dateien laut GNU tar, plus stderr."""
    p = subprocess.run(["tar", "--quoting-style=literal", "-tvf", str(tarpfad)],
                       capture_output=True, text=True, errors="replace")
    out, harte = [], set()
    for zeile in p.stdout.splitlines():
        m = re.match(r"^(\S)\S{9}\s+\S+\s+(\d+)\s+\d{4}-\d\d-\d\d \d\d:\d\d (.*)$", zeile)
        if not m:
            continue
        if m.group(1) == "h":                 # harter Link: Name da, Groesse 0 in der Liste
            harte.add(norm_name(m.group(3).split(" link to ")[0]))
        elif m.group(1) == "-":
            out.append((norm_name(m.group(3)), int(m.group(2))))
    return out, harte, p.stderr.strip()


def tar_inhalte(tarpfad: Path, scratch: Path) -> dict:
    """{Name: sha256} der regulaeren Dateien, von GNU tar entpackt."""
    ziel = scratch / "tarx"
    if ziel.exists():
        shutil.rmtree(ziel)
    ziel.mkdir()
    subprocess.run(["tar", "-xf", str(tarpfad), "-C", str(ziel), "--no-same-owner",
                    "--no-same-permissions"], capture_output=True)
    # Archivierte Modi koennen 000 sein (Verzeichnisse ohne Rechte): erst oeffnen.
    subprocess.run(["chmod", "-R", "u+rwX", str(ziel)], capture_output=True)
    out = {}
    for p in ziel.rglob("*"):
        if p.is_file() and not p.is_symlink():
            out[norm_name(p.relative_to(ziel).as_posix())] = sha256_datei(p)
    shutil.rmtree(ziel)
    return out


# ---------------------------------------------------------------------------
# Archive erzeugen
# ---------------------------------------------------------------------------

def archiviere_einzel(quelle: Path, name: str, ziel: Path, meta: Meta, satz_info: dict):
    cqm = quelle / f"{name}.cqm"
    prov = provenance_von(meta, f"{name}.cqm", cqm)
    sid = prov.get("set_id")
    with DiskTool.open_raw(cqm) as d:
        ci = d.container_info()
        prov["container_description"] = ci.get("description", "")
        prov["container_comment"] = ci.get("comment", "")
        befunde = befunde_von(d) if d.has_filesystem else []
        info = ""
        if name in satz_info:
            s, i, n, nachbarn = satz_info[name]
            info = f"Diskette {i} von {n} des tar-Satzes '{s}' ({', '.join(nachbarn)})"
            prov["tar_satz"] = s
            prov["disk_index"] = i
        hinweise = hinweise_aus(befunde, info)
        if name in satz_info and satz_info[name][0] in ABGELEITET:
            hinweise.append("Zugehoerigkeit zum Satz aus dem Befund abgeleitet (tar.abgeschnitten/"
                            "tar.fortsetzung passen zusammen); kein .tar-Gegenstueck")
        for e in d.list() if d.has_filesystem else []:
            if e.damaged:
                folge = ""
                if name in satz_info:
                    s, i, n, nachbarn = satz_info[name]
                    if i < n:
                        folge = f"; setzt sich auf {nachbarn[i]}.cqm fort"
                hinweise.append(f"Datei '{e.name}' ist auf dieser Diskette abgeschnitten{folge}")
        if name in satz_info and satz_info[name][1] > 1 and d.filesystem != "tar720":
            hinweise.append("Diskette beginnt mitten in einer Datei des Satzes (kein tar-Kopf "
                            f"am Anfang); die Erkennung '{d.filesystem or 'keine'}' ist hier "
                            "keine Aussage ueber den Inhalt")
        beilagen = beschreibung_beilage(meta, sid)
        if not d.has_filesystem:
            hinweise.append("Kein vom DiskTool lesbares Dateisystem; Abbild liegt im Archiv")
            fat = fat12_dateien(_roh(cqm))
            if fat:
                hinweise.append(f"MS-DOS FAT12: {len(fat)} Dateien zusaetzlich extrahiert "
                                "(Beiblatt-Ordner dateien_fat/, ohne Dateiangaben)")
                for p, b in fat.items():
                    beilagen[f"dateien_fat/{p}"] = b
        if hinweise:
            prov["hinweise"] = hinweise
        out = ziel / f"{name}.zip"
        create_archive(d, out, provenance=prov, beilagen=beilagen)
        return {"name": name, "zip": out, "fs": d.filesystem or "-", "befunde": befunde,
                "prov": prov, "crc": ci.get("crc"), "fat": len(beilagen) and
                sum(1 for k in beilagen if k.startswith("dateien_fat/"))}


def _roh(cqm: Path) -> bytes:
    import cqm2img
    return cqm2img.decode(cqm.read_bytes())[1]


def archiviere_satz(quelle: Path, satz: str, glieder: list, ziel: Path, meta: Meta):
    pfade = [quelle / f"{g}.cqm" for g in glieder]
    erstes = f"{glieder[0]}.cqm"
    prov = provenance_von(meta, erstes, pfade[0])
    sid = prov.get("set_id")
    prov["file_name"] = ", ".join(f"{g}.cqm" for g in glieder)
    prov["source_sha256"] = {f"{g}.cqm": sha256_datei(p) for g, p in zip(glieder, pfade)}
    prov["disks"] = [f"{g}.cqm" for g in glieder]
    prov["satz"] = satz
    web = [meta.von(f"{g}.cqm") for g in glieder]
    prov["web_label"] = [w.get("label", "") for w in web]
    abgeleitet = satz in ABGELEITET
    hinweise = ([ "Satz aus dem Befund abgeleitet (kein .tar-Gegenstueck, nicht auf der Webseite)"]
                if abgeleitet else []) + [f"tar-Satz '{satz}' aus {len(glieder)} Disketten in der Reihenfolge: "
                + ", ".join(f"{g}.cqm" for g in glieder)]
    with DiskTool.open_tar_satz(pfade) as d:
        befunde = befunde_von(d)
        for fid, sev, obj, text in befunde:
            if sev >= 1:
                hinweise.append(f"[{SCHWERE.get(sev, sev)}] {fid} ({obj}): {text}")
        offen = [e.name for e in d.list() if e.damaged]
        # Pro Einzeldiskette: welche Datei dort abgeschnitten ist (Fortsetzung steht
        # im Satz vollstaendig drin).
        for g in glieder:
            with DiskTool.open_raw(quelle / f"{g}.cqm") as e1:
                for e in e1.list():
                    if e.damaged:
                        hinweise.append(f"'{e.name}' endet auf {g}.cqm abgeschnitten und wird "
                                        "auf der folgenden Diskette fortgesetzt; im Satz "
                                        "vollstaendig")
        if offen:
            hinweise.append("Auch im Satz abgeschnitten (Strom endet): " + ", ".join(offen))
        prov["hinweise"] = hinweise
        out = ziel / "saetze" / f"{satz}_satz.zip"
        create_archive(d, out, bezeichnung=f"{satz}_satz",
                       herkunft=f"tar-Satz aus {len(glieder)} Abbildern: "
                                + ", ".join(f"{g}.cqm" for g in glieder),
                       provenance=prov, beilagen=beschreibung_beilage(meta, sid),
                       ohne_abbild=True)
        return {"name": f"{satz}_satz", "zip": out, "fs": d.filesystem, "befunde": befunde,
                "prov": prov, "glieder": glieder, "abgeschnitten": offen}


# ---------------------------------------------------------------------------
# Nachpruefung
# ---------------------------------------------------------------------------

def pruefe_zip(zp: Path, scratch: Path, tarpfad: Path = None, erlaube_leer=False):
    """-> (Probleme[], Info{})"""
    probleme, info = [], {}
    ziel = scratch / zp.stem
    if ziel.exists():
        shutil.rmtree(ziel)
    with zipfile.ZipFile(zp) as z:
        if z.testzip():
            probleme.append("ZIP defekt: " + z.testzip())
        z.extractall(ziel)
        namen = set(z.namelist())
    daten = yaml.safe_load((ziel / INVENTAR_DATEI).read_text(encoding="utf-8"))
    teile = list(daten["files"]) + list(daten["attachments"]) + [daten["catalogue"]]
    if "image" in daten:
        teile.append(daten["image"])
    for f in teile:
        p = ziel / f["path"]
        if f["path"] not in namen or not p.is_file():
            probleme.append(f"fehlt im ZIP: {f['path']}")
            continue
        if p.stat().st_size != f["size"] or sha256_datei(p) != f["sha256"]:
            probleme.append(f"Groesse/sha256 stimmt nicht: {f['path']}")
    erklaert = {f["path"] for f in teile}
    unerklaert = [n for n in namen if not n.endswith("/") and n not in erklaert
                  and n != INVENTAR_DATEI]
    if unerklaert:
        probleme.append(f"nicht im Verzeichnis erklaert: {unerklaert[:3]}")
    info["dateien"] = len(daten["files"])
    info["beiblaetter"] = len(daten["attachments"])
    info["bytes_dateien"] = sum(f["size"] for f in daten["files"])
    info["beschaedigt"] = [f["path"] for f in daten["files"] if f.get("damaged")]
    if not daten["files"] and not erlaube_leer:
        probleme.append("keine Dateien im Archiv")

    if tarpfad is not None and tarpfad.is_file():
        soll, harte, err = tar_liste(tarpfad)
        # Harte Links: GNU tar listet sie mit Groesse 0, der Auszug liefert das Ziel.
        ist = collections.Counter((f["path"][len("dateien/"):], f["size"])
                                  for f in daten["files"]
                                  if not f["name"].endswith(".fileinfo")
                                  and f["path"][len("dateien/"):] not in harte)
        info["harte_links"] = len(harte)
        ist = collections.Counter({k: v for k, v in ist.items()})
        sollz = collections.Counter(soll)
        fehlt = sollz - ist
        zuviel = ist - sollz
        info["tar_namen_groessen"] = "gleich" if not fehlt and not zuviel else \
            f"Abweichung: {len(fehlt)} nur in tar, {len(zuviel)} nur im Archiv"
        if fehlt or zuviel:
            info["tar_abweichung"] = {"nur_tar": sorted(fehlt)[:5], "nur_archiv": sorted(zuviel)[:5]}
        hashes = tar_inhalte(tarpfad, scratch)
        eigene = {f["path"][len("dateien/"):]: f["sha256"] for f in daten["files"]}
        diff = [n for n, h in hashes.items() if n in eigene and eigene[n] != h]
        fehlend = [n for n in hashes if n not in eigene]
        info["tar_inhalt"] = "gleich" if not diff and not fehlend else \
            f"{len(diff)} Dateien mit anderem Inhalt: {diff[:3]}; {len(fehlend)} nur im tar: {fehlend[:3]}"
        info["tar_inhalt_diff"] = diff
        if err:
            info["tar_stderr"] = err.splitlines()[:2]
    return probleme, info


# ---------------------------------------------------------------------------
# crdownload-Befund
# ---------------------------------------------------------------------------

def crdownload_bericht(quelle: Path) -> list:
    """Befund (nur Bericht) ueber die abgebrochenen Downloads: sind sie vollstaendig?"""
    import cqm2img
    z = []
    with tempfile.TemporaryDirectory(prefix="pofo_crd_") as tmp:
        for c in sorted(quelle.glob("*.cqm.crdownload")):
            cb = c.read_bytes()
            try:
                meta = cqm2img.parse_header(cb)
                erw = meta["spuren"] * meta["koepfe"] * meta["sektoren"] * meta["sektorgroesse"]
                out, pos = cqm2img.unpack_stream(cb, meta["datenstart"], erw + 1)
            except Exception as e:                        # wirklich abgebrochen
                z.append(f"{c.name}: nicht lesbar ({e})")
                continue
            zeile = (f"{c.name}: {len(cb)} B, Datenstrom {'vollstaendig' if len(out) == erw and pos == len(cb) else 'UNVOLLSTAENDIG'} "
                     f"({len(out)} von {erw} B entpackt)")
            tmpc = Path(tmp) / "x.cqm"
            tmpc.write_bytes(cb)
            try:
                with DiskTool.open_raw(tmpc) as d:
                    zeile += (f", CQM-Pruefwert {d.container_info().get('crc')}, Erkennung "
                              f"{d.filesystem or 'keine'}, {len(d.list()) if d.has_filesystem else 0} Eintraege")
            except K1520DiskError as e:
                zeile += f", DiskTool: {str(e)[:60]}"
            t = quelle / (c.name.replace(".cqm.crdownload", ".tar.crdownload"))
            if t.is_file():
                tb = t.read_bytes()
                zeile += (f"; {t.name}: {len(tb)} B, erster tar-Kopf "
                          f"{'gueltig' if _tar_kopf_ok(tb[:512]) else 'ungueltig'}, "
                          f"{'identisch mit dem entpackten Abbild' if bytes(out) == tb else 'WEICHT vom entpackten Abbild ab'}")
            z.append(zeile)
        for t in sorted(quelle.glob("*.tar.crdownload")):
            if not (quelle / t.name.replace(".tar.", ".cqm.")).is_file():
                z.append(f"{t.name}: {t.stat().st_size} B ohne zugehoerige .cqm.crdownload")
    return z


def _tar_kopf_ok(h: bytes) -> bool:
    if len(h) < 512 or h == bytes(512):
        return False
    try:
        soll = int(h[148:154].split(b"\0")[0].strip() or b"0", 8)
    except ValueError:
        return False
    return soll == sum(h[:148]) + 32 * 8 + sum(h[156:512])


# ---------------------------------------------------------------------------
# Uebersicht
# ---------------------------------------------------------------------------

def oberste(daten, n=4):
    c = collections.Counter(f["path"][len("dateien/"):].split("/")[0] for f in daten)
    return ", ".join(k for k, _ in c.most_common(n))


def inhalt_blick(zp: Path) -> str:
    """Kurze sachliche Inhaltsangabe: oberste Pfade, README/Versionsstrings im Namen."""
    with zipfile.ZipFile(zp) as z:
        daten = yaml.safe_load(z.read(INVENTAR_DATEI))
        fs = [f for f in daten["files"] if not f["name"].endswith(".fileinfo")]
        if not fs:
            return "keine Dateien"
        z1 = f"{len(fs)} Dateien, oberste Ebene: {oberste(fs)}"
        merk = [f["name"] for f in fs if re.search(r"(?i)readme|liesmich|version|install|release",
                                                   f["name"])][:3]
        if merk:
            z1 += f"; auffaellig: {', '.join(merk)}"
        for f in fs:
            if re.search(r"(?i)^(readme|liesmich)", f["name"]) and f["size"] < 20000:
                t = z.read(f["path"]).decode("latin-1", "replace")
                zl = [x.strip() for x in t.splitlines() if x.strip()][:1]
                if zl:
                    z1 += f"; {f['name']} beginnt: \"{zl[0][:70]}\""
                break
    return z1


def schreibe_uebersicht(ziel: Path, ergebnisse: list, saetze: list, meta: Meta,
                        dubletten: list, unbekannt_web: list, crd: list, quelle: Path,
                        pruef: dict, beschr: dict):
    L = ["# Uebersicht P8000-CopyQM-Sammlung (pofo.de)", "",
         f"Erzeugt {datetime.date.today().isoformat()} mit `tools/p8000/pofo_archive.py`; "
         f"Webseite {WEB_URL}, abgerufen {meta.abruf}.  Quelle: `{quelle}` (nur gelesen).", ""]
    L.append(f"{len(ergebnisse)} Einzelarchive, {len(saetze)} Satzarchive (`saetze/`).  "
             "Spalte Befunde = Pruefung des Dateisystems (Anzahl je Schwere), "
             "Archiv-Pruefung = ZIP entpackt, sha256 aller Einträge gegen `diskarchive.yaml`.")
    L.append("")
    gruppen = collections.defaultdict(list)
    for r in ergebnisse:
        p = r["prov"]
        gruppen[(p.get("set_id") if p.get("set_id") is not None else 10**6,
                 p.get("set_name") or "(kein Satz auf der Webseite)")].append(r)
    for (sid, sname), rs in sorted(gruppen.items(), key=lambda x: (x[0][0], x[0][1])):
        ver = rs[0]["prov"].get("version", "")
        kopf = f"{sname}" + (f" {ver}" if ver else "") + \
            (f" (Satz {sid})" if sid != 10**6 else "")
        L += [f"## {kopf}", ""]
        a = auszug(meta, sid if sid != 10**6 else None)
        if a:
            L += [f"Beschreibung (Webseite, Auszug): {a}", ""]
        L += ["| Abbild | Etikett | Dateisystem | Dateien | Groesse | Befunde | Besonderheiten |",
              "|---|---|---|---|---|---|---|"]
        for r in sorted(rs, key=lambda x: x["name"]):
            i = pruef[r["name"]][1]
            z = collections.Counter(SCHWERE.get(b[1], "?") for b in r["befunde"])
            bef = ", ".join(f"{v} {k}" for k, v in z.items()) or "-"
            bes = []
            if pruef[r["name"]][0]:
                bes.append("PRUEFUNG: " + "; ".join(pruef[r["name"]][0]))
            if i.get("beschaedigt"):
                bes.append(f"{len(i['beschaedigt'])} abgeschnitten/defekt")
            if r["crc"] != "ok":
                bes.append(f"CQM-Pruefwert {r['crc']}")
            if i.get("tar_inhalt") and i["tar_inhalt"] != "gleich":
                bes.append("tar-Gegenstück: " + i["tar_inhalt"])
            if i.get("tar_namen_groessen") not in (None, "gleich"):
                bes.append("tar-Namen: " + i["tar_namen_groessen"])
            if r["name"] in beschr:
                bes.append(beschr[r["name"]])
            if r["name"] in ANMERKUNGEN:
                bes.append(ANMERKUNGEN[r["name"]])
            bes.append(r["blick"])
            wl = r["prov"].get("web_label", "").replace("|", "/")
            L.append(f"| {r['name']} | {wl} | {r['fs']} | {i['dateien']} | "
                     f"{i['bytes_dateien'] // 1024} KB | {bef} | {'; '.join(bes)} |")
        L.append("")
    if saetze:
        L += ["## Satzarchive (tar-Saetze ueber mehrere Disketten)", "",
              "| Satz | Disketten | Dateien | Groesse | Befunde | tar-Gegenstück (GNU tar) | Abgeschnitten im Satz |",
              "|---|---|---|---|---|---|---|"]
        for r in saetze:
            probleme, i = pruef[r["name"]]
            z = collections.Counter(SCHWERE.get(b[1], "?") for b in r["befunde"])
            bef = ", ".join(f"{v} {k}" for k, v in z.items()) or "-"
            tarz = f"Namen/Groessen {i.get('tar_namen_groessen', '-')}; Inhalt {i.get('tar_inhalt', '-')}"
            L.append(f"| {r['name']} | {', '.join(r['glieder'])} | {i['dateien']} | "
                     f"{i['bytes_dateien'] // 1024} KB | {bef} | {tarz} | "
                     f"{', '.join(r['abgeschnitten']) or '-'}"
                     + (f" PRUEFUNG: {probleme}" if probleme else "") + " |")
        L.append("")
    # ---- Auffaelligkeiten
    A, ohne_satz = [], []
    for r in ergebnisse + saetze:
        probleme, i = pruef[r["name"]]
        for p in probleme:
            A.append(f"{r['name']}: {p}")
        if i.get("tar_inhalt") and i["tar_inhalt"] != "gleich":
            A.append(f"{r['name']}: Inhalt weicht vom tar-Gegenstück ab: {i['tar_inhalt']}")
        if i.get("tar_namen_groessen") not in (None, "gleich"):
            A.append(f"{r['name']}: tar-Namen/Groessen: {i['tar_namen_groessen']} "
                     f"{i.get('tar_abweichung', '')}")
        if r.get("crc") not in (None, "ok"):
            A.append(f"{r['name']}: CopyQM-Pruefwert {r['crc']}")
        schwer = collections.OrderedDict()
        for b in r["befunde"]:
            if b[1] >= 2:
                schwer.setdefault((b[1], b[0], b[2]), []).append(b[3])
        for (sev, fid, obj), texte in schwer.items():
            A.append(f"{r['name']}: Befund {SCHWERE[sev]} {fid} ({obj}), {len(texte)}x: {texte[0]}")
    for r in ergebnisse:
        if r["fs"] in ("-", "") or (r["fs"].startswith("cpa") and pruef[r["name"]][1]["dateien"] == 0):
            fort = " (Fortsetzungsdiskette eines tar-Satzes ohne tar-Kopf am Anfang)" \
                if r["prov"].get("disk_index", 1) > 1 else ""
            A.append(f"{r['name']}: Dateisystem nicht erkannt/leer ('{r['fs']}'){fort}; "
                     "nur Abbild im Archiv")
        if r["prov"].get("set_id") is None:
            ohne_satz.append(r["name"])
        elif meta.notiz(r["prov"]["set_id"]) is None:
            A.append(f"{r['name']}: Satz {r['prov']['set_id']} hat auf der Webseite keine Notizseite")
        if pruef[r["name"]][1].get("beschaedigt") and r["name"] not in {
                g for s in saetze for g in s["glieder"]}:
            A.append(f"{r['name']}: abgeschnittene Dateien ohne Satz-Fortsetzung: "
                     f"{pruef[r['name']][1]['beschaedigt'][:3]}")
    if ohne_satz:
        A.append(f"{len(ohne_satz)} Disketten ohne Satz/Beschreibung auf der Webseite "
                 f"(set_id fehlt; nur Etikett/Dateiname bekannt): {', '.join(ohne_satz)}")
    A.extend(dubletten)
    for n in unbekannt_web:
        A.append(f"{n}: nicht in pofo_meta.json (Webseite kennt das Abbild nicht)")
    A += [f"crdownload (nicht archiviert, nur Befund): {z}" for z in crd]
    L += ["## Auffaelligkeiten", ""] + ([f"- {a}" for a in A] or ["- keine"]) + [""]
    (ziel / "UEBERSICHT.md").write_text("\n".join(L), encoding="utf-8")
    return A


# ---------------------------------------------------------------------------

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--quelle", required=True, type=Path)
    ap.add_argument("--meta", required=True, type=Path)
    ap.add_argument("--ziel", required=True, type=Path)
    ap.add_argument("--nur", action="append", default=[],
                    help="nur dieses Abbild (Name ohne .cqm) bzw. diesen Satz; wiederholbar")
    ap.add_argument("--trocken", action="store_true", help="nur anzeigen, nichts schreiben")
    o = ap.parse_args(argv)

    quelle = o.quelle.expanduser().resolve()
    meta = Meta(o.meta.expanduser().resolve())
    cqms = sorted(p.stem for p in quelle.glob("*.cqm"))
    # Dubletten im Unterordner nur pruefen, nie archivieren.
    dubletten = []
    for sub in sorted(p for p in quelle.iterdir() if p.is_dir()):
        for c in sorted(sub.glob("*.cqm")):
            top = quelle / c.name
            if top.is_file() and sha256_datei(top) == sha256_datei(c):
                dubletten.append(f"{sub.name}/{c.name}: byteidentisch mit {c.name}, nicht doppelt archiviert")
            else:
                dubletten.append(f"{sub.name}/{c.name}: KEIN byteidentisches Gegenstueck oben -- nicht archiviert")
    unbekannt_web = [n for n in cqms if f"{n}.cqm" not in meta.eintraege]

    satz_info = {}
    for s, gl in SAETZE.items():
        fehlend = [g for g in gl if g not in cqms]
        if fehlend:
            print(f"Satz {s}: Glieder fehlen: {fehlend}", file=sys.stderr)
            continue
        for i, g in enumerate(gl, 1):
            satz_info[g] = (s, i, len(gl), gl)

    waehle = set(o.nur)
    einzel = [n for n in cqms if not waehle or n in waehle or
              (n in satz_info and satz_info[n][0] in waehle)]
    saetze = [s for s in SAETZE if s in {v[0] for v in satz_info.values()}
              and (not waehle or s in waehle or any(g in waehle for g in SAETZE[s]))]
    print(f"{len(einzel)} Einzelarchive, {len(saetze)} Satzarchive geplant -> {o.ziel}")
    if o.trocken:
        for n in einzel:
            print("  ", n, satz_info.get(n, ("", 0, 0, []))[0])
        for s in saetze:
            print("  Satz", s, SAETZE[s])
        return 0

    ziel = o.ziel.expanduser()
    ziel.mkdir(parents=True, exist_ok=True)
    (ziel / "saetze").mkdir(exist_ok=True)
    ergebnisse, satzergebnisse, fehler = [], [], []
    for n in einzel:
        try:
            ergebnisse.append(archiviere_einzel(quelle, n, ziel, meta, satz_info))
            print("ok  ", n)
        except (K1520DiskError, OSError, KeyError, ValueError) as e:
            fehler.append((n, str(e)))
            print("FEHL", n, e, file=sys.stderr)
    for s in saetze:
        try:
            satzergebnisse.append(archiviere_satz(quelle, s, SAETZE[s], ziel, meta))
            print("ok   Satz", s)
        except (K1520DiskError, OSError, KeyError, ValueError) as e:
            fehler.append((f"{s}_satz", str(e)))
            print("FEHL Satz", s, e, file=sys.stderr)

    # ---- Nachpruefung
    pruef, beschr = {}, {}
    with tempfile.TemporaryDirectory(prefix="pofo_pruef_") as tmp:
        scratch = Path(tmp)
        for r in ergebnisse:
            n = r["name"]
            tar = quelle / f"{n}.tar"
            leer = r["fs"] in ("-", "") or n in {"konsul2"}
            tarp = tar if (tar.is_file() and n not in satz_info) else None
            pruef[n] = pruefe_zip(r["zip"], scratch, tarp, erlaube_leer=leer)
            r["blick"] = inhalt_blick(r["zip"])
        for r in satzergebnisse:
            pruef[r["name"]] = pruefe_zip(r["zip"], scratch, quelle / f"{r['name'][:-5]}.tar")
    for r in ergebnisse:
        if r["fat"]:
            beschr[r["name"]] = f"MS-DOS-FAT12-Diskette, {r['fat']} Dateien aus dem Abbild gelesen (dateien_fat/)"
    crd = crdownload_bericht(quelle)
    A = schreibe_uebersicht(ziel, ergebnisse, satzergebnisse, meta, dubletten, unbekannt_web,
                            crd, quelle, pruef, beschr)
    schlecht = [(n, p[0]) for n, p in pruef.items() if p[0]]
    print(f"\n{len(ergebnisse)} Einzel- und {len(satzergebnisse)} Satzarchive, "
          f"{len(fehler)} Fehlschlaege, {len(schlecht)} mit Pruefproblemen, "
          f"{len(A)} Auffaelligkeiten -> {ziel / 'UEBERSICHT.md'}")
    for n, m in fehler:
        print("  Fehlschlag", n, m)
    for n, p in schlecht:
        print("  Pruefung", n, p)
    return 1 if fehler or schlecht else 0


if __name__ == "__main__":
    sys.exit(main())

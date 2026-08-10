# Die Pipeline: Bauen und Testen auf GitHub

Alles, was das Projekt auf GitHubs Rechnern tut, steht in `.github/workflows/`.
Dieses Dokument beschreibt, **was dort läuft**, **was du in GitHub einstellen musst**,
damit es überhaupt läuft, und **wie du einen Lauf anstößt**.

> **Nichts läuft von selbst.** Kein Bau bei einem Push, keiner bei einem Pull Request,
> kein Zeitplan. Jeder Lauf wird von Hand angestoßen — bis auf eine Ausnahme: der
> Push eines Versions-Tags `v*` baut das Release-Paket (siehe [§4](#4-release-paket-bauen)).
> Auch das ist eine bewusste Handlung, und wer sie loswerden will, streicht den
> `push:`-Block in `release.yml`.

Die Pipeline ersetzt **nicht** den lokalen `pre-push`-Hook (`.githooks/pre-push`), der
vor jedem Push `tools/dev.sh test` fährt. Sie ist die **Gegenprobe auf einem sauberen
System**: frisch ausgecheckt, frisch gebaut, ohne die 40 Dinge, die auf einem
Entwicklungsrechner mit der Zeit im Hintergrund stehen.

---

## 1. Was es gibt

| Workflow | Anzeigename in GitHub | Auslöser | Dauer |
|----------|----------------------|----------|-------|
| `ci.yml` | **Bauen und Regression** | nur von Hand | ~8–12 min (mit warmem ccache ~4) |
| `slow-tests.yml` | **Langsame Tests (Format)** | nur von Hand | ~20–40 min |
| `release.yml` | **Release-Paket** | von Hand **oder** Push eines Tags `v*` | ~6–10 min |
| `windows-probe.yml` | **Windows-Sonde (experimentell)** | nur von Hand | ~5 min, **schlägt derzeit fehl** |

Alle Testläufe rufen **`tools/dev.sh`** auf, nie `cmake`/`ctest` direkt. Dort stehen
Build-Typ, `LOG_LEVEL` und die ausgeschlossenen Label; eine Pipeline, die daran
vorbeiruft, prüft etwas anderes als der Entwickler vor dem Push. Wer die Kommandos
ändert, ändert sie in `dev.sh` — beide Seiten folgen dann automatisch.

---

## 2. Einmalige Einstellungen in GitHub

Ohne diese drei Punkte tut sich nichts oder es scheitert an der letzten Stelle.

### 2.1 Actions einschalten

**Settings → Actions → General → Actions permissions**

Auf *Allow all actions and reusable workflows* stellen. Die Pipeline benutzt vier
Actions, alle von GitHub selbst: `actions/checkout`, `actions/setup-python`,
`actions/cache`, `actions/upload-artifact`. Wer die Erlaubnis enger fassen will, wählt
*Allow <owner>, and select non-<owner>, actions* und hakt „Allow actions created by
GitHub" an — mehr braucht es nicht.

### 2.2 Schreibrecht für den Release-Job

**Settings → Actions → General → Workflow permissions → Read and write permissions**

Nur `release.yml` braucht es, dafür aber zwingend: ohne Schreibrecht scheitert das
Anhängen der Pakete ans Release mit **HTTP 403**, nachdem alles andere schon
durchgelaufen ist. Der Workflow fordert das Recht zusätzlich selbst an
(`permissions: contents: write`), aber diese Anforderung kann die Repository-Einstellung
nur **einschränken**, nicht erweitern.

Ein Geheimnis (Secret) ist nirgends nötig — `GITHUB_TOKEN` stellt GitHub je Lauf selbst.

### 2.3 Die Workflows müssen auf `main` liegen

Den Knopf *Run workflow* zeigt GitHub nur für Workflows, die auf dem
**Standard-Branch** liegen. Solange `.github/workflows/` nur auf einem Nebenzweig
existiert, ist der Reiter *Actions* leer. Nach dem Merge nach `main` erscheinen alle
vier — und lassen sich dann auch **auf jedem beliebigen Branch** starten (die
Branch-Auswahl im Dialog bestimmt, welcher Stand gebaut wird).

### 2.4 Kostenrahmen (nur bei privatem Repository)

Ist das Repository **öffentlich**, sind die Läufe kostenlos. Ist es **privat**, zählen
sie gegen das monatliche Kontingent (Free: 2 000 Minuten). Die vier Workflows sind
deshalb bewusst manuell: die Regression kostet ~10 Minuten je Lauf, die langsamen
Formatläufe ~40. Verbrauch nachsehen: **Settings → Billing → Plans and usage**.

Aufbewahrung der Artefakte: standardmäßig 90 Tage, die Workflows setzen kürzere Fristen
(7 Tage für Fehlerprotokolle, 30 für Pakete).

---

## 3. Einen Lauf anstoßen

### Im Browser

1. Reiter **Actions**
2. links den Workflow wählen, z. B. **Bauen und Regression**
3. rechts **Run workflow** (der Knopf erscheint nur, wenn [§2.3](#23-die-workflows-müssen-auf-main-liegen) erfüllt ist)
4. Branch wählen (Vorgabe `main`), bei manchen Workflows zusätzlich eine Auswahlliste
   ausfüllen, dann **Run workflow** drücken.

Der Lauf taucht nach ein paar Sekunden in der Liste auf; ein Klick darauf zeigt die
Schritte live mit.

### Von der Kommandozeile (`gh`)

Einmalig einzurichten — auf diesem Rechner ist `gh` **nicht** installiert:

```sh
sudo apt install gh        # Debian/Ubuntu; sonst https://cli.github.com
gh auth login              # einmal anmelden (Browser oder Token)
```

```sh
gh workflow list                              # welche Workflows gibt es
gh workflow run ci.yml --ref main             # Regression anstoßen
gh run list --workflow=ci.yml --limit 5       # letzte Läufe
gh run watch                                  # dem laufenden Lauf zusehen
gh run view --log-failed                      # nur die fehlgeschlagenen Schritte
gh run download <run-id>                      # Artefakte holen (Pakete, Protokolle)
```

`gh workflow run` gibt selbst keine Lauf-Nummer zurück — die steht in
`gh run list` (der oberste Eintrag), oder man hängt einfach `gh run watch` an.

---

## 4. Die Workflows im Einzelnen

### 4.1 Bauen und Regression (`ci.yml`)

Die Standardrunde, identisch zu dem, was der `pre-push`-Hook lokal fährt:

```sh
tools/dev.sh test        # baut build/, dann ctest ohne format_integration/format_matrix
```

Der Job installiert dafür `build-essential cmake ccache libreadline-dev` und die vier
Systembibliotheken, die PySide6 auch im Offscreen-Betrieb braucht (`libegl1 libgl1
libxkbcommon0 libdbus-1-3`). Anschließend legt er ein `venv/` im Arbeitsbaum an und
installiert `requirements-dev.txt` hinein — **ohne das registriert CMake die
Python-Testebene nicht** und die neun `py_*`-Fälle fallen still aus (siehe
`tests/python/CMakeLists.txt`).

- **Erwartet:** 789 Tests grün (Stand 2026-08-10).
- **ccache** wird zwischen Läufen aufbewahrt; der erste Lauf ist der langsamste.
- **Bei Fehlschlag** lädt der Job `build/Testing/Temporary/LastTest.log` und `logs/`
  als Artefakt `ci-logs` hoch (7 Tage). Lokal nachstellen:
  `tools/dev.sh test -R <Namensmuster> --output-on-failure`.

Anstoßen:

```sh
gh workflow run ci.yml --ref main
```

### 4.2 Langsame Tests (Format) (`slow-tests.yml`)

Die beiden Testsätze, die aus der Standardregression ausgeschlossen sind — **vor einem
Merge nach `main` und vor einem Release** sinnvoll:

| Umfang | Was | Kommando |
|--------|-----|----------|
| `format_integration` | die **Tiefe**: je Laufwerkstyp ein Format über die ganze Diskette | `tools/dev.sh test-format` |
| `format_matrix` | die **Breite**: 88 FORMAT.COM-Menüeinträge, Smoke Spur 0–2 | `tools/dev.sh test-matrix -j4` |
| `beide` (Vorgabe) | nacheinander beides | |

Anstoßen:

```sh
gh workflow run slow-tests.yml --ref main                       # beide
gh workflow run slow-tests.yml --ref main -f umfang=format_matrix
```

### 4.3 Release-Paket (`release.yml`)

Schnürt das verteilbare Anwenderpaket (`packaging/build_payload.sh`, ~2 MB: Kern,
GUI, `formats.yaml`, Beispieldisketten) und prüft es. Entwurf und Begründungen:
`doc/design/13_distribution.md`.

**Gebaut wird auf `ubuntu-22.04`, nicht auf `ubuntu-latest`** — die
Rückwärtskompatibilität kommt von der Baseline des Baurechners, nicht vom Zielsystem.
Eine gegen die glibc von Ubuntu 24.04 gelinkte Bibliothek läuft auf keiner älteren
Distribution.

Danach der **Rauchtest** — er prüft genau die drei Dinge, an denen ein Paket still
kaputt sein kann:

1. `app/main.py --paths` erkennt das Installationslayout und findet Bibliothek **und**
   `formats.yaml`,
2. die Bibliothek lädt per `ctypes` und `k1520_version()` antwortet,
3. im Binärabbild steht **kein Pfad des Baurechners** (Gegenprobe zu
   `-DK1520_FORMATS_DEFAULT=`).

Ein Paket, das daran scheitert, wird kein Release-Asset.

**Drei Wege, es zu starten:**

```sh
# a) Probelauf auf einem Branch — Ergebnis nur als Artefakt, kein Release
gh workflow run release.yml --ref main
gh workflow run release.yml --ref main -f disks=all       # alle Disketten aus disks/

# b) von Hand auf einem vorhandenen Tag — hängt die Pakete ans Release
git tag -a v1.2.0 -m "Fassung 1.2.0" && git push origin v1.2.0
gh workflow run release.yml --ref v1.2.0

# c) automatisch: der Push des Tags allein genügt schon
git tag -a v1.2.0 -m "Fassung 1.2.0" && git push origin v1.2.0
```

Weg (c) ist der einzige nicht-manuelle Auslöser der ganzen Pipeline. Wer auch ihn
loswerden will, streicht in `release.yml` den Block

```yaml
on:
  push:
    tags: ['v*']
```

— dann bleibt Weg (b), der dasselbe tut.

**Das Ergebnis** liegt an zwei Stellen:

- als Artefakt `k1520emu-linux-x86_64` am Lauf (30 Tage), immer;
- bei einem Tag zusätzlich am GitHub-Release — als **Entwurf**, wenn es das Release
  noch nicht gab. Ein Release ist nach außen gerichtet; veröffentlicht wird es von
  Hand unter *Releases → Edit → Publish release*.

Die Version im Dateinamen und in `VERSION` kommt aus `git describe --tags` — deshalb
checkt der Job die volle Historie aus (`fetch-depth: 0`, dabei `filter: blob:none`,
weil die ~500 MB Diskettenabbilder in der Historie hier niemand braucht).

### 4.4 Windows-Sonde (`windows-probe.yml`)

**Schlägt derzeit fehl, und das ist der Zweck.** Der Kern ist noch nicht
MSVC-tauglich (`doc/design/13_distribution.md` §6.1):

- `core/api/k1520_api.h` trägt nur `extern "C"`, keine Export-Makros — eine MSVC-DLL
  exportiert damit **gar nichts**, und `ctypes` findet keine einzige Funktion. Die
  Sonde setzt übergangsweise `WINDOWS_EXPORT_ALL_SYMBOLS`.
- `CMakeLists.txt` ist auf GCC zugeschnitten; MSVC braucht `/utf-8` (die Quellen
  enthalten deutsche Umlaute) und die statische CRT (kein VC-Redist ohne
  Administratorrechte).

Der Job baut deshalb **nur die Kernbibliothek** und prüft mit `dumpbin /exports`, ob
`k1520_version` überhaupt exportiert wird. Er ist die Messlatte für die Portierung:
sobald er grün ist, wird daraus ein zweiter Job in `release.yml` (Paket + Inno Setup,
§5.1 des Entwurfs).

```sh
gh workflow run windows-probe.yml --ref main
```

---

## 5. Wenn etwas nicht geht

| Symptom | Ursache | Abhilfe |
|---------|---------|---------|
| Reiter *Actions* leer, kein **Run workflow** | Workflows liegen nicht auf dem Standard-Branch | nach `main` mergen ([§2.3](#23-die-workflows-müssen-auf-main-liegen)) |
| Release-Job grün, aber `gh release upload` → **403** | Workflow-Rechte auf *Read only* | [§2.2](#22-schreibrecht-für-den-release-job) |
| `py_*`-Tests fehlen im ctest-Bericht | `venv/` oder `requirements-dev.txt` nicht installiert | Schritt „Python-Testebene vorbereiten" im Protokoll ansehen |
| Rauchtest meldet „Payload wird nicht als Installation erkannt" | `bin/libk1520core.so` fehlt in der Payload | `packaging/build_payload.sh` lokal fahren und `dist/*/payload/` ansehen |
| Lauf bricht mit „The runner image `ubuntu-22.04` is deprecated" ab | GitHub hat das Abbild abgekündigt | **nicht** einfach `ubuntu-latest` einsetzen — Nachfolger ist ein Container mit alter glibc (manylinux_2_28), sonst fällt die Baseline ([§4.3](#43-release-paket-releaseyml)) |

Ein fehlgeschlagener Lauf lässt sich fast immer lokal nachstellen — die Pipeline ruft
ja nur `tools/dev.sh` auf:

```sh
tools/dev.sh test                 # was ci.yml fährt
tools/dev.sh test-format          # was slow-tests.yml fährt
packaging/build_payload.sh        # was release.yml fährt
```

---

## 6. Wartung

- **Action-Fassungen** (`actions/checkout@v4` …) hin und wieder anheben; Dependabot
  kann das übernehmen (`.github/dependabot.yml`, derzeit nicht eingerichtet).
- **Neue Testebene?** In `tools/dev.sh` eintragen — die Pipeline erbt sie dann.
- **Neuer Workflow?** Hier in [§1](#1-was-es-gibt) und in der Tabelle in `CLAUDE.md`
  ergänzen, sonst findet ihn niemand wieder.

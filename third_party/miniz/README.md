# miniz — beigelegte Fremdquelle

Deflate-Packer und -Entpacker für die **gepackten Plattenabbilder** (`<name>.<kürzel>.img.gz`)
der P8000-Winchester und des DiskTool.  Die gzip-Hülle (Kopf, CRC-32, Länge) schreibt und
liest `core/util/gzip_datei.cpp` selbst — miniz liefert nur das rohe Deflate und `mz_crc32`.

| | |
|---|---|
| **Herkunft** | https://github.com/richgel999/miniz (Release-Asset `miniz-3.0.2.zip`) |
| **Stand** | Version **3.0.2** (`MZ_VERSION "11.0.2"`), sha256 des Archivs `ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5`, geholt 2026-10-09 |
| **Lizenz** | **MIT** (`LICENSE`, Copyright © 2013–2014 RAD Game Tools and Valve Software, © 2010–2014 Rich Geldreich and Tenacious Software LLC) — dieselbe wie dieses Projekt |
| **Geändert** | nein, unverändert übernommen (nur `miniz.c`, `miniz.h`, `LICENSE` aus dem Archiv) |

## Warum miniz und nicht zlib oder zstd

- **Eine Datei, keine Abhängigkeit**: kein Systempaket, kein `FetchContent`, kein Netz beim
  Konfigurieren; baut unverändert mit GCC, MinGW und MSVC.
- **gzip statt zstd**: `.gz` öffnet jedes Werkzeug auf jedem System.  Die Packrate ist für
  Plattenabbilder ohne Belang (eine leere 47-MB-Platte wird ~250 KB), und die Geschwindigkeit
  spielt keine Rolle mehr, seit im Hintergrundfaden gepackt wird.

## Wie es gebaut wird

`miniz.c` ist eine **Amalgamation**: genau diese eine Datei wird übersetzt (Ziel `miniz` im
`CMakeLists.txt` der Wurzel), mit `MINIZ_NO_ARCHIVE_APIS`, `MINIZ_NO_STDIO`, `MINIZ_NO_TIME`
und **`MINIZ_NO_ZLIB_COMPATIBLE_NAMES`** — Letzteres ist Pflicht: `libk1520core.so` wird in
denselben Prozess wie Qt geladen, und Qt bringt zlib mit; Namen wie `deflate`/`crc32` aus
miniz würden mit dessen Symbolen kollidieren.  Benutzt werden nur `tdefl_*`, `tinfl_*`, `mz_crc32`.

## Aktualisieren

```sh
curl -sLO https://github.com/richgel999/miniz/releases/download/<ver>/miniz-<ver>.zip
unzip miniz-<ver>.zip miniz.c miniz.h LICENSE -d third_party/miniz/
# Stand oben nachtragen, dann:
tools/dev.sh test -R "GzipDatei|Platte|WegaPlatte"
```

Prüfen, dass die `LICENSE` weiterhin MIT ist.  Wächter: `py_third_party_lizenzen`.

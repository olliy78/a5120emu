# Merkposten — ausgelagerte CLAUDE.md-Abschnitte

Die Dateien in diesem Verzeichnis waren Abschnitte von `CLAUDE.md`. Sie **gelten
unverändert weiter**, sobald an dem betreffenden Teilsystem gearbeitet wird — sie
sind nur nicht mehr in jeder einzelnen Anfrage geladen.

| Datei | Teilsystem |
|-------|------------|
| `paketierung.md` | `packaging/` — Anwenderpaket, `install.sh`, Inno-Setup |
| `boot_debugging.md` | `boot_trace` / `k1520dbg`, die acht Boot-Invarianten im Volltext |
| `disktool.md` | `core/filesystem/` + `app/disktool/` — Dateisysteme, Oberfläche |
| `physische_diskette.md` | `TrackSync` + `app/gw/` — echtes Laufwerk am Greaseweazle |
| `k8915.md` | `core/machines/k8915/` + Karten `zre8762`/`k7028`, Peripherie `k7672` — zweite Maschine (nicht ausgelagert, sondern direkt hier angelegt; `CLAUDE.md` trägt nur den Kurzabsatz „Zweite Maschine: K8915“) |
| `serielle_schnittstellen.md` | `core/serial/` + Anschlüsse in `k8025`/`k7028` + Dock „Schnittstellen" — Telnet/RFC 2217/Datei nach außen (direkt hier angelegt; `CLAUDE.md` trägt nur den Kurzabsatz) |
| `prg710.md` | `core/machines/prg710/` + Karten `k2521`/`prg710_speicher`/`atp590068`, `i8279`/`k7609` — dritte Maschine PRG 710 / 710-1 (direkt hier angelegt; `CLAUDE.md` trägt nur den Kurzabsatz „Dritte Maschine: PRG 710 / PRG 710-1“) |
| `pc1715.md` | `core/machines/pc1715/` + `pc1715_zre`, `i8275`, `tastatur1715`, K5122-`Portlage::Pc1715`, Primitive `z80_dma`/`upd765` — vierte Maschine PC 1715 / PC 1715W (direkt hier angelegt; `CLAUDE.md` trägt nur den Kurzabsatz „Vierte Maschine: PC 1715 / PC 1715W“) |
| `raf.md` | `core/cards/raf/` + `installRaf` in allen Maschinen, `k1520_raf_*`, `app/raf.py` — RAM-Floppy RAF 128/512/2M (direkt hier angelegt; `CLAUDE.md` trägt nur den Kurzabsatz „RAM-Floppy RAF“) |
| `lochstreifen.md` | `core/cards/k6022/` + `core/peripherals/lochstreifen/` + `installK6022` in allen Maschinen, `k1520_ptape_*`, Kasten „Lochstreifen“ — Lochstreifen K6022/SIF1000 (direkt hier angelegt; `CLAUDE.md` trägt nur den Kurzabsatz) |

## Warum ausgelagert

`CLAUDE.md` wird bei **jeder** Anfrage mitgelesen, und in einer langen Sitzung wird
diese Anfrage sehr oft gestellt: gemessen am 2026-08-18 liest dieses Projekt im
Schnitt rund 530 000 Token Kontext je Werkzeugaufruf wieder ein. Die Datei war auf
92 KB gewachsen (~23 000 Token), davon rund 60 KB Fachwissen zu vier Teilsystemen,
das nur beim Arbeiten an genau diesem Teilsystem gebraucht wird. Nach dem Umzug sind
es ~41 KB. Jeder Subagent erbt diese Ersparnis mit.

## Regel beim Ergänzen

Die Trennlinie ist **Stolperdraht gegen Archäologie**:

- **In `CLAUDE.md` bleibt**, was jemand wissen muss, der das Teilsystem *nicht*
  kennt und trotzdem gerade dabei ist, es kaputtzumachen — Invarianten, Verbote,
  „nicht wieder aufweichen"-Sätze. Kurz, ohne Begründung, mit Verweis hierher.
- **Hierher gehört** die Begründung, die Fehlersuche, die Messung, der Wächtername,
  die Vorgeschichte — alles, was man liest, *nachdem* man weiß, dass es einen angeht.

Neue Erkenntnisse zu einem dieser vier Teilsysteme kommen also **hierher**, nicht in
`CLAUDE.md`. Wächst ein Abschnitt in `CLAUDE.md` erneut über etwa eine Bildschirmseite,
gehört auch er hierher.

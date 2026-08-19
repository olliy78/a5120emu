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

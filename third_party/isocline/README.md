# isocline — beigelegte Fremdquelle

Zeileneditor für die interaktive Sitzung von `k1520dbg`: Pfeiltasten, History,
Tab-Vervollständigung, UTF-8.

| | |
|---|---|
| **Herkunft** | https://github.com/daanx/isocline |
| **Stand** | `8d6dc1ef95b1b46711e66eb23d39d4467a0fcdac` (Zweig `main`, geholt 2026-08-18) |
| **Lizenz** | **MIT** (`LICENSE`, Copyright © 2021 Daan Leijen) — dieselbe wie dieses Projekt |
| **Geändert** | nein, unverändert übernommen |

## Warum beigelegt und nicht als Systembibliothek

**Lizenz.** Der Vorgänger war GNU readline, und das steht unter der **GPLv3+**. Ein
ausgeliefertes Binärabbild, das readline einbindet, wäre ein Gesamtwerk unter GPLv3 —
dieses Projekt steht unter MIT. isocline ist ebenfalls MIT; damit gibt es im Paket
keine zweite Lizenzordnung. Begründung im Zusammenhang: `doc/design/13_distribution.md`
§10a.3.

**Windows.** readline gibt es dort nicht (der `CMakeLists.txt` suchte es unter Windows
gar nicht erst), die Sitzung hatte also **keinerlei** Zeilenbearbeitung. isocline setzt
Windows über die Console-API um — die Zeilenbearbeitung ist damit zum ersten Mal auf
beiden Plattformen dieselbe.

**Keine Abhängigkeitskette.** Reines C, kein termcap/ncurses, keine C++-Laufzeit. Das
Paket bleibt „eine Datei, alles drin"; vorher hing `k1520dbg` an `libreadline.so.8`
*und* `libtinfo.so.6` und startete ohne sie überhaupt nicht.

## Wie es gebaut wird

`src/isocline.c` ist eine **Amalgamation** — sie inkludiert die übrigen
Übersetzungseinheiten. Es wird deshalb genau **eine** Datei übersetzt (Ziel `isocline`
im `CMakeLists.txt` der Wurzel); die anderen 30 Dateien sind Beiwerk dieser einen.
Nicht einzeln in den Bau aufnehmen — das gäbe doppelte Symbole.

## Aktualisieren

```sh
curl -sL https://github.com/daanx/isocline/archive/refs/heads/main.tar.gz | tar xz
cp -r isocline-main/src isocline-main/include isocline-main/LICENSE third_party/isocline/
# Stand oben nachtragen, dann:
tools/dev.sh test -R cli_dbg
```

Prüfen, dass die `LICENSE` weiterhin MIT ist — ein Lizenzwechsel wäre der einzige
Grund, den Zeileneditor wieder auszutauschen. Wächter: `py_third_party_lizenzen`.

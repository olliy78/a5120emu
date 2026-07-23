# Feature Request: k1520dbg/boot_trace — effizientes Treiben interaktiver Programme

Entstanden aus der HARDY-Analyse (`doc/analyse_hardy.md`): das Reverse-Engineering und
Ans-Laufen-Bringen eines **interaktiven, bildschirm-/tastaturgetriebenen** Testprogramms
(Menüs, Dialoge, VRAM-Ausgaben) war mit den vorhandenen Debugger-Features deutlich
mühsamer als nötig. Die folgenden Verbesserungen hätten die Arbeit spürbar beschleunigt.
Sortiert nach Nutzen. Kontext: das GoogleTest-Guard `tests/cpp/test_hardy.cpp` musste am
Ende **genau diese fehlenden Fähigkeiten in C++ nachbauen** (`runSmallUntil`,
`pressKeyUntil`) — ein starkes Signal, dass sie ins Werkzeug gehören.

---

## 1. ★★★ Bildschirm-konditioniertes Laufen/Anhalten (`gscreen` / `until screen ~`)

**Schmerz:** Um durch Banner → Dialog → Menü → Test zu navigieren, wurde ständig
`keys X` + `g <geratene Zyklen>` + `screen` wiederholt und die Zyklenzahl geraten
(`g 8000000`, `g 40000000` …). Zu wenig → Screen noch nicht fertig; zu viel → Zeit
verschwendet. Das war der **größte Einzel-Zeitfresser** und Fehlerquelle
(inkonsistente Stopp-Punkte).

**Feature:** VRAM-Text als Halte-/Warte-Bedingung, analog zu `--until` (boot_trace) und
dem existierenden `wp`/`b … if`:
- `gscreen "<text>"` — läuft, bis der 80×24-Text-VRAM (ab `0xF800`) `<text>` enthält
  (Substring; optional `/regex/`), mit Zyklen-Cap.
- Als Breakpoint-Bedingung: `bscreen "<text>"` bzw. `g until screen ~ "<text>"`.
- boot_trace: `--until 'screen ~ "A>"'` (bislang nur RAM-/PC-Bedingungen).

**Nutzen:** Menü-Navigation wird **deterministisch** statt geraten. Genau das macht
`runSmallUntil()` im Guard-Test — es gehört ins Tool.

**Impl-Notiz:** kleine Helferfunktion „VRAM→ASCII" (existiert schon als `screen`-Renderer)
+ `find`/`std::regex_search`; in die bestehende `g`-Schleife als Abbruchprüfung (wie
`--until`/Watchpoints) einhängen.

---

## 2. ★★★ Savestate INKL. VRAM (+ gemountete Disk-Referenz)

**Schmerz:** Der Boot bis `A>` kostet **~90–130 Mio. Zyklen pro Lauf**. Für jedes
Bildschirm-Experiment (Taste X am Untermenü, anderer Menüpunkt …) wurde von vorn gebootet.
`savestate`/`loadstate` existiert, sichert aber **NICHT das VRAM** (und nicht die
gemounteten Images) — d.h. nach `loadstate` ist der Bildschirm leer, `screen` unbrauchbar,
und bildschirmbasiertes Weiterarbeiten unmöglich. Deshalb war der Checkpoint für genau
diese Art Arbeit wertlos.

**Feature:** Save-Format erweitern (neue Version) um:
- **VRAM** (0xF800–0xFFFF bzw. K7024-Zustand), sodass `screen` nach `loadstate` stimmt.
- **Referenz auf gemountete Images** (Pfad + COW-Kopie), sodass Disk-Zugriffe nach
  `loadstate` weiterlaufen (heute laut Doku bewusst NICHT enthalten).

**Nutzen:** „einmal booten, oft resümieren" wird auch für **interaktive/Screen-Arbeit**
nutzbar — der 2-Sekunden-/100-M-Zyklen-Boot wird echte Einmalkosten. Für HARDY: Snapshot
direkt am Testmenü, dann pro Testabschnitt neu laden statt neu booten.

**Impl-Notiz:** `captureState/restoreState` (core) + `savestate`-Serialisierung
(`callstack_tracker.h`-Umfeld) um VRAM/K7024 + Mount-Metadaten ergänzen, Save-Format-Version
hochziehen.

---

## 3. ★★ `keys`-Verbesserungen für Direkt-Poll-Programme

**Schmerz A (Leertaste):** Eine Leertaste zu senden war nicht offensichtlich — `keys `
(nur Whitespace) → „unknown command 'keys'"; es funktionierte nur über den Backslash-
Escape `keys \ ` (Backslash+Space). Kostete Rätselraten.

**Schmerz B (verlorene Tasten):** HARDY liest die Tastatur per **Direkt-Poll** der SIO
(kein BDOS-Puffer). Eine mit fester Timing gesendete Taste geht verloren, wenn das Programm
im Moment des Anschlags nicht gerade pollt → Navigation schlug scheinbar fehl (viel Zeit
verloren mit „warum reagiert 'x' nicht?"). Der Guard-Test musste dafür `pressKeyUntil`
bauen (Taste wiederholen, bis der Folge-Screen erscheint).

**Feature:**
- Klarer Space-Escape: `keys \s` (und generisch `keys \x20` / `\xNN`), dokumentiert.
- `keyuntil "<taste>" "<screen-text>"` — drückt die Taste wiederholt (bis Cap), bis der
  VRAM `<screen-text>` enthält (robust gegen Direkt-Poll-Verlust). Kombiniert 1+3.

**Nutzen:** Interaktive Menüs deterministisch bedienbar; kein Raten mehr, ob eine Taste
ankam.

---

## 4. ★★ Funktionierender Mehr-Byte-Speicher-Dump (`x`/`dump ADDR N`)

**Schmerz:** `x 0x272C 32` gab **nur 1 Byte** aus (die Count-Angabe griff nicht wie
erwartet). Um einen String/Puffer zu inspizieren, musste auf ein externes Python-Skript
gegen `docs/hardy.com` ausgewichen werden — umständlich und fehleranfällig (Datei- vs.
Laufzeit-Offset).

**Feature:** `dump ADDR N` (bzw. `x ADDR N` reparieren): klassischer Hex+ASCII-Dump von N
Bytes ab ADDR aus dem **Laufzeit-Speicher** (`memReadDebug`), z.B.
```
272C: 0D 0A 4D 45 4D 44 49 32 20 61 6B 74 69 76 3A 20  ..MEMDI2 aktiv:
```

**Nutzen:** Strings/Tabellen/Handshake-Puffer direkt im laufenden Zustand lesen (statt im
Binary raten). Für die HARDY-Arbeit hätte das die MEMDI-String-`$`-Terminator-Suche und
die `[04xx]`-Konfigbyte-Analyse in Sekunden erledigt.

---

## 5. ★ Kleinere Ergänzungen

- **`screen`-Suche:** `screen find "<text>"` / `screen /regex/` → gibt Zeile/Spalte bzw.
  „nicht gefunden" aus (skriptbar; heute muss man den 24-Zeilen-Dump visuell absuchen).
- **`bt`-Fold:** bei Endlos-`RST 38H`-Schleifen (Fetch-Crash) flutete der Callstack-Tracker
  `bt` mit identischen `0038`-Frames; die echten Aufrufer waren verdrängt. Ein Kollabieren
  identischer Folge-Frames (`↻ ×N`, wie `boot_trace --fold`) würde den relevanten Rahmen
  sofort zeigen.
- **RST-38-/Fetch-0xFF-Hinweis:** ein automatischer Hinweis bei `where`/Stop, wenn die CPU
  an `0x0038` steht und `[0x0038]==0xFF` (typische Signatur eines Speicher-Disable-/Read-
  Gate-Problems), spart die manuelle Deutung.

---

## Priorisierung

Wenn nur eines umgesetzt wird: **#1 (bildschirm-konditioniertes Warten)** — es adressiert
den dominanten Zeitfresser und macht interaktive Emulator-Arbeit erst skriptbar. **#2
(Savestate+VRAM)** ist der zweitgrößte Hebel (Boot-Einmalkosten). #3 und #4 sind billige,
hochfrequente Ergonomie-Gewinne. Alle vier zusammen hätten die HARDY-Rechner-Test-Arbeit
schätzungsweise um die Hälfte verkürzt und stünden für die restlichen 6 Testabschnitte
direkt bereit.

# Feature Request: k1520dbg — Tastatur-Diagnose (was kommt am Rechner an?)

Entstanden aus dem UDOS-Interaktivtest (`doc/analyse_udos.md` §13, Sitzung 2026-08-04).

> **Vorab, damit niemand dieselbe Fehlspur läuft: an der Kodierung ist nichts kaputt.**
> `keys` sendet exakt die getippten ASCII-Codes. Der Weg ist durchgängig korrekt: die
> REPL schreibt die Eingabezeile **nicht** klein (kein `tolower` auf `line`), `keys`
> reicht jedes Zeichen als ASCII-Keycode weiter, und `K7637::translateKey` gibt
> druckbares ASCII (`0x20..0x7E`) unverändert aus („Shift is presumed already encoded
> in the keycode value supplied by the caller"). Die GUI ist ebenso korrekt —
> `qt_event_to_core_key` nimmt `event.text()`, also das vom Host-Layout tatsächlich
> erzeugte Zeichen.
>
> Was in der UDOS-Sitzung nach einem Werkzeugfehler aussah, war die
> **Schreibkonvention der Maschine**: UDOS' Konsoltreiber (ab `0x063F`) **invertiert**
> die Buchstabenschreibung — ungeshiftet (Klein-ASCII) ergibt Großbuchstaben, mit
> Shift Kleinbuchstaben. Gemessen (`iow 0x5C` + Bildschirmspeicher):
>
> ```
> keys cat   → IN(5CH)=63 61 74        VRAM: 25 43 41 54   = "%CAT"   ✔ Kommando läuft
> keys CAT   → IN(5CH)=43 41 54        VRAM: 25 63 61 74   = "%cat"   ✘ NONEXISTENT COMMAND
> ```
>
> **Konsequenz für Skripte:** UDOS treibt man aus dem Debugger mit *klein* getippten
> Kommandos (`keys cat\s*.*\sp=&\sf=l\r`). Das ist kein Emulator-Artefakt, sondern das
> Verhalten des realen A5120 — die Befehlsreferenz in `doc/analyse_udos.md` schreibt
> Kommandos deshalb groß, obwohl man sie klein tippt.

**Der eigentliche Schmerz ist also nicht die Kodierung, sondern die Beobachtbarkeit:**
Man sieht dem Debugger nicht an, welches Byte die Tastatur abliefert. Steht auf dem
Schirm etwas anderes als getippt, gibt es keinen kurzen Weg zu der Frage „liegt es an
mir, am Modell oder am Betriebssystem?". Ich habe dafür zwei Runden gebraucht (erst
`translateKey` und die GUI gelesen, dann die REPL auf `tolower` durchsucht) und die
Frage am Ende über einen Umweg entschieden, auf den man erst kommen muss:

```
(dbg) iow 0x5C
(dbg) keys Z
[io] c38495088  IN (5CH)=61  ZVE1.PC=063F      ← 'a' kommt unveraendert an
                                                 ⇒ die Wandlung passiert IM OS
```

Das setzt voraus, dass man (a) den Konsolport des laufenden OS kennt (bei UDOS `0x5C`,
bei CP/A ein anderer) und (b) auf die Idee kommt, einen I/O-Watch statt des Bildschirms
zu befragen. Beides ist genau das Wissen, das man in dem Moment nicht hat.

Genau dieser Vergleich — **gesendetes Byte gegen angezeigtes Byte** — ist die Diagnose,
die hier gefehlt hat. Er haette die Case-Inversion sofort als Software-Verhalten des
Betriebssystems ausgewiesen (Byte kommt unveraendert an, wird erst danach gedreht),
statt sie als Werkzeugfehler erscheinen zu lassen.

Sortiert nach Nutzen.

---

## 1. ★★★ `keys --echo` / `keys -v` — anzeigen, was gesendet wurde

**Schmerz:** `keys` ist heute stumm. Ob ein Zeichen überhaupt in die Tastatur ging, mit
welchem Code, und ob die Maschine es abgeholt hat, bleibt unsichtbar.

**Vorschlag:** ein Schalter, der je Zeichen eine Zeile ausgibt — gesendeter Code, das
Zeichen, und ob er innerhalb des Laufbudgets vom Rechner **gelesen** wurde:

```
(dbg) keys --echo DIR\r
  'D' → 0x44   gelesen @c37745051 IN(5CH)   ZVE1.PC=063F
  'I' → 0x49   gelesen @c37820118 IN(5CH)   ZVE1.PC=063F
  'R' → 0x52   gelesen @c37894402 IN(5CH)   ZVE1.PC=063F
  CR  → 0xFF   gelesen @c37970771 IN(5CH)   ZVE1.PC=063F      (ET1 → cp37 → 0x0D)
```

Das beantwortet in **einem** Kommando drei Fragen, die heute drei Werkzeuge brauchen:
Kommt es an? Mit welchem Code? Und (bei `nicht gelesen`) pollt das Programm gerade
überhaupt die Tastatur? Der letzte Punkt ist der bekannte Stolperstein hinter
`keyuntil` (direkt pollende Programme verlieren Tastendrücke) — hier würde man ihn
sehen statt ihn zu vermuten.

**Umsetzung:** Der Lesezeitpunkt ist bereits verfügbar — die I/O-Watch-Infrastruktur
(`iow`) hängt am selben Trace-Callback. `keys` müsste für die Dauer des Tastendrucks
einen internen Watch auf den SIO-Datenport setzen. **Den Port sollte der Debugger
selbst finden** (s. Punkt 2), sonst verlagert sich das Problem nur.

## 2. ★★★ `dev kbd` — Zustand der Tastaturstrecke auf einen Blick

**Schmerz:** `dev sio` zeigt die SIO-Kanäle, aber niemand sagt einem, **welcher** Kanal
die Tastatur ist und was gerade in der Strecke steckt. Der K7637 ist ein serielles
Gerät mit eigener Sendewarteschlange (`tx_queue_`, 9600 Baud, `SERIAL_BYTE_CYCLES`) —
ein Byte kann längst „gedrückt" und trotzdem noch unterwegs sein.

**Vorschlag:**

```
(dbg) dev kbd
  K7637 Serientastatur → SIO A32 Kanal A (Daten 0x5C, Status 0x5D)
  gehaltene Taste: keine   Autorepeat: aus
  Sendewarteschlange: 1 Byte   [0x52 'R' frei ab c37894402, in 118 Takten]
  vom Rechner gelesen: 47 Bytes   zuletzt c37745051 (ZVE1.PC=063F)
  Konsolport-Erkennung: 0x5C (aus den letzten 200 IN-Zugriffen)
```

Die letzte Zeile ist der Schlüssel: Der Debugger kann den Konsolport **messen** statt
ihn erfragen zu lassen — welcher Port wird in einer Warteschleife wiederholt gelesen?
Für UDOS ist das `0x5D`/`0x5C`, für CP/A und SCPX jeweils ein anderer, und die
Erkennung würde in allen Fällen ohne Vorwissen funktionieren. Damit hat auch Punkt 1
seinen Port.

## 3. ★★ `keys` mit expliziter Shift-Angabe (`\S<zeichen>`)

**Schmerz:** Kein akuter — der Vollständigkeit halber. Der `shift`-Parameter von
`A5120Machine::keyPress(kc, shift, ctrl)` wird für druckbares ASCII **ignoriert**; die
Groß-/Kleinschreibung steckt allein im Keycode. Das ist eine bewusste und in
GUI/K7637/Debugger konsistente Modellentscheidung, aber eine latente Falle: Ruft
irgendwann jemand `keyPress('a', shift=true)` in der Erwartung von `'A'`, bekommt er
`'a'` — und der Parameter suggeriert das Gegenteil.

**Vorschlag (klein):** entweder den Parameter für druckbares ASCII **wirken lassen**
(`shift` ⇒ `toupper` bzw. die Shift-Belegung der Bildschirmtastatur) oder ihn dort
explizit als „ohne Wirkung" dokumentieren. Für `keys` genügt eine Escape-Sequenz
`\S<zeichen>`, die shift=true mitschickt — dann kann man das Modell überhaupt testen.
Ein Test wie `K7637.ShiftFlagHasNoEffectOnPrintableAscii` friert die Entscheidung ein,
statt sie im Kommentar zu lassen.

## 4. ★ `keys` respektiert Laufbudget-Rückmeldung

**Schmerz:** `keys` läuft je Zeichen fest `600000` + `150000` Takte. Bei einem
Programm, das langsamer pollt, gehen Zeichen verloren; bei einem schnellen ist es
unnötig zäh (eine 10-Zeichen-Zeile kostet 7,5 Mio. Takte). Für `dialog`-Skripte über
mehrere Menüs summiert sich das spürbar.

**Vorschlag:** Sobald Punkt 1 den Lesezeitpunkt kennt, kann `keys` **adaptiv** werden:
weiterlaufen bis das Byte abgeholt wurde (mit Obergrenze), dann sofort das nächste
Zeichen. Das macht `keys` gleichzeitig schneller und robuster — und `keyuntil` in
vielen Fällen überflüssig.

---

## Nicht umsetzen

- **Eine Groß-/Kleinschreibungs-Umschaltung in `keys`.** Es gibt nichts zu reparieren
  (s. Kasten oben); eine Option würde die Fehlspur nur zementieren. Der richtige Ort
  für die Klarstellung ist Punkt 1 (man sieht den Code) und `tools/k1520dbg.md`.
- **Ein Caps-Lock-Modell im K7637.** Der reale K7637 hat eine Umschaltung, aber kein
  bekanntes Programm im Projekt hängt davon ab — die Konsoltreiber der Betriebssysteme
  legen die Schreibung ohnehin selbst fest (UDOS invertiert, CP/M-artige großen auf).

## Regressionsnetz

Neue Kommandos gehören in **beide** Netze (s. `tools/k1520dbg.md` §9):
`tests/dbg/all_commands_smoke.dbg` (Dispatch) **und** ein gezielter CLI-Test auf den
Meldungs-Wortlaut. Für Punkt 1/2 bietet sich `disks/udos_boot_scp.hfe` an: UDOS zeigt
am `%`-Prompt reproduzierbar den Fall „gesendet `0x63`, angezeigt `C`" — genau die
Konstellation, die diesen Feature Request ausgelöst hat.

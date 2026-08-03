# Feature Request: k1520dbg/boot_trace — Fremd-Betriebssysteme mit ihren Originalquellen sezieren

Entstanden aus der UDOS-Analyse (`doc/analyse_udos.md`): Zu **UDOS 4.3 für den A5120**
liegen die vollständigen, kommentierten MACRO-80-Quellen vor
(`~/projects/UDOS/UDOS_/`: `DEBUBC43`, `POSIBC43`, `SYL0/1BC43`, `UNFLOPPY`, `NDOS` …)
plus das geladene Treiberbinary (`UDOS/FLOPPY76/5122/UDOSFLOP.700`). Sie waren der
Hebel der ganzen Analyse (Methodik: `doc/analyse_udos.md` §11) — aber **kein einziges
Werkzeug konnte sie direkt verwenden**. Alles lief über Handarbeit: Opcode-Längen
zählen, Hex-Dumps mit Disassembler-Ausgabe vergleichen, C++-Code mit `fprintf`
instrumentieren und neu bauen.

Die vorhandene `.prn`-Annotation (`-l`, `tools/prn_listing.h`) greift hier **prinzipiell
nicht**: Ihr Parser verlangt links Ladeadresse **und** emittierte Objektbytes
(`prn_listing.h`, „Parse-Regel"). Das haben nur die selbst gebauten CP/A-Listings.
Fremdquellen sind praktisch immer reiner Quelltext oder ein nacktes Binary.

Sortiert nach Nutzen. Die ★★★-Punkte 1–3 adressieren dieselbe Lücke aus drei Richtungen;
Punkt 4/5 hätten die eigentliche Fehlerursache **in einem Kommando** gezeigt statt in zwei
Instrumentierungs-Runden; Punkt 7 ist ein handfester Usability-Defekt mit Messwerten.

---

## 1. ★★★ `.MAC`/`.ASM`-Quelltext als Annotationsquelle (`-l datei.mac@ORG`)

**Schmerz:** `DEBUBC43.MAC` ist `ORG 0` **ohne Adressspalte**. Um die Trap-Einsprünge zu
finden, habe ich die Opcode-Längen ab `0x0000` von Hand durchgezählt
(`LD SP,nn`=3, `CALL nn`=3, `LD (nn),HL`=3, `PUSH IY`=2 …) bis `0x0024` / `0x0038` /
`0x0066` herauskamen. Das ist exakt die Arbeit, die ein Werkzeug erledigen soll — und
sie ist fehleranfällig: Ein einziges falsch gezähltes Byte verschiebt alles Folgende.

**Feature:** `-l datei.mac@0x0700` — ein **minimaler MACRO-80-Vorpass**, der nur die
*Längen* der Anweisungen kennen muss (nicht deren korrekte Codierung), um jeder
Quellzeile eine Adresse zuzuordnen. Zu beachten: `ORG`, `.PHASE`/`DEPHASE`, `ASEG`,
`DS`, `DB`/`DW` (inkl. `DB 'text'`), `EQU`, `.Z80`-Mnemonik. Ergebnis ist dieselbe
`Adresse → Quellzeile`-Tabelle, die `prnlst` heute aus `.prn` baut → **alle bestehenden
Konsumenten (`u`, `bt`, Trace-Zeilen, PC-Histogramme) profitieren ohne Änderung**.

**80/20-Variante (sehr billig, hier ausreichend):** Disassemblat-Quellen wie diese
tragen die Adresse **im Labelnamen** (`M03F8:` → `0x03F8`, `M1084:` → `0x1084`,
`M0EDC EQU 0EDCH`). Ein Modus `-l datei.mac@labels` könnte die Adresskarte allein aus
`M[0-9A-F]{4}`-Labels aufspannen und die Zeilen dazwischen interpolieren. Das hätte
den größten Teil des Nutzens für einen Bruchteil des Aufwands.

**Nutzen:** Aus `bt`-Zeile `#2 03B3 (call → 0BFD)` würde
`#2 03B3 M03F8+… CALL M0BFD ; BETRIEBSSYSTEM LADEN` — die Analyse wäre um Stunden
kürzer gewesen.

---

## 2. ★★★ Automatischer Versatz-Abgleich (`-l datei@auto`)

**Schmerz:** Die gebootete Diskette trägt einen **anderen Build** als die Quellen. Die
Sequenz um `M03F8` lag live ab `0x0395`, also ca. `0x45` verschoben. Das vorhandene
`@OFFSET` setzt voraus, dass man den Versatz **schon kennt** — ich habe ihn durch
Nebeneinanderlegen von Quelltext und `u 0x0390 24` per Auge gefunden.

**Feature:** `-l datei@auto` — Objektbytes einer markanten, sprungfreien Sequenz aus
dem Listing/Assemblat nehmen, im Live-RAM danach suchen und den Versatz ableiten;
Ergebnis mit Trefferzahl/Konfidenz melden:

```
-l DEBUBC43.mac@auto → Versatz -0x0045 (37 von 40 Ankerbytes, 3 Abweichungen bei 0x03D1)
```

**Nutzen:** Beantwortet nebenbei die immer gleiche Frage „passt diese Quelle überhaupt
zu diesem Image?" — und warnt, wenn sie es *fast* tut.

**Impl-Notiz:** Braucht Punkt 1 (oder ein `.prn`/Binary) für die Objektbytes; die Suche
selbst ist ein simpler Musterscan über 64 KB.

---

## 3. ★★★ Binärabgleich Datei ↔ RAM (`verify <datei> @<adr>`)

**Schmerz:** Dass `UDOSFLOP.700` *der* laufende Treiber ist, habe ich durch Vergleich
von `xxd`-Ausgabe mit `u 0x0700` **per Auge** festgestellt — und dabei zufällig eine
Abweichung entdeckt (`0x072F`: Datei `32 D3 0F`, RAM `32 CA 0F`), die belegt, dass es
ein anderer Build ist.

**Feature:**

```
(dbg) verify ~/projects/UDOS/UDOS/FLOPPY76/5122/UDOSFLOP.700 @0x0700
  1280 Bytes, 1278 identisch (99.8 %), 2 Abweichungen:
    0x0730  Datei D3   RAM CA
    0x0885  Datei D3   RAM CA
```

Gegenstück `dump <adr> <len> <datei>` (RAM → Datei) für den externen Disassembler.

**Nutzen:** Beantwortet in einem Kommando drei Fragen, die bei jedem Fremd-OS auftauchen:
Ist das die richtige Datei? Ist es derselbe Build? Wo genau nicht? Ist zudem die
Grundlage für Punkt 2.

---

## 4. ★★★ `dev pio` zeigt nur die BS-PIO — die K5122-PIOs fehlen

**Schmerz:** Der **gesamte Fehler** stand in einer einzigen Zeile Portzustand:

```
A(ie=1 pend=1 ius=1 iei=1 vec=BA mode=0)   ← pending UND ius gesetzt
```

Diese Zeile war mit dem Debugger **nicht erreichbar**: `dev pio` gibt ausschließlich
`m.bsPioState()` aus (`k1520dbg.cpp:1549`), und `A5120Machine` exponiert zwar
`k5122State()`, `ctcState()`, `bsPioState()`, `kbdSioState()`, `dfueSioState()`
(`a5120.h:154–162`) — aber **keinen Zugriff auf die beiden K5122-PIOs**. Ich musste
`Z80PIO::getVector()` mit einem `fprintf` instrumentieren und neu bauen, um sie zu
sehen. Ironischerweise existiert `Z80PIO::debugState()` bereits und liefert genau die
gebrauchten Felder.

**Feature:** `dev pio [all|bs|k5122ctrl|k5122data]` (Default `all`), je Port
`mode / ie / pending / ius / iei / vector / mask / dir / in / out`. Reine Verdrahtung:
zwei Accessoren in `a5120.h` + Schleife im vorhandenen Ausgabeblock.

**Nutzen:** Vom „C++ patchen und neu bauen" zum Einzeiler. Das ist der mit Abstand
beste Aufwand/Nutzen-Punkt der Liste.

---

## 5. ★★ Interrupt-Diagnose: Vektor **und Quelle** in `itrace`/`bint`

**Schmerz:** `--itrace` liefert `seq,cyc,kind,int_pc,isr_pc,sp`. Damit sah ich zwar
`isr_pc=0xFFFF` (Sprung ins Leere), aber **weder den Vektor noch das Gerät**. Um an den
Vektor zu kommen, musste ich `boot_trace` mit `--log-cycle …:debug` laufen lassen und
nach `INT-Quittung: Vektor=` greppen; um das Gerät zu bestimmen, `K1520Bus::
interruptAcknowledge()` **und** `K5122::getVector()` instrumentieren — zwei zusätzliche
Bau-/Lauf-Runden für eine Information, die der Bus zum Quittungszeitpunkt kennt.

**Feature:**
- `itrace`-CSV um `vector`, `device`, `port` erweitern (`…,0xFF,K5122,ctrl_pio.A`).
- `bint` druckt beim Halt Vektor + Quellgerät + aufgelöste Tabellenadresse.
- Fallback sichtbar machen: Wenn `interruptAcknowledge()` mangels Gerät `0xFF` liefert,
  gehört das als **`SPURIOUS`** markiert, nicht als normaler Vektor — der Unterschied
  zwischen „Gerät hat Vektor 0xFF" und „kein Gerät hat geantwortet" war der Kern des
  Bugs und im Log nicht unterscheidbar.

---

## 6. ★★ `ivt` — IM-2-Vektortabelle auf einen Blick

**Schmerz:** Ich habe `(I<<8) | (vektor & 0xFE)` von Hand gerechnet und dann
stückweise `x 0x0FE0 16` / `x 0x0FB8 8` gedumpt, um zu sehen, dass `0x0FE6` eine gültige
ISR (`0A0A`) enthält, `0x0FBA` und `0x0FFE` dagegen `FFFF`.

**Feature:** `ivt` — für das aktuelle `I` alle Chips der Daisy-Chain durchgehen, deren
**programmierte** Vektoren einsammeln und tabellieren:

```
(dbg) ivt                                   I=0x0F  (IM 2)
  Vektor  Tabelle  Eintrag  Gerät                    Status
  0xB8    0x0FB8   FFFF     K2526 BS-PIO A           ⚠ zeigt ins Leere
  0xBA    0x0FBA   FFFF     K5122 ctrl-PIO A (Index) ⚠ zeigt ins Leere   ← IE=1!
  0xE6    0x0FE6   0A0A     K2526 CTC ch3            ok
  0xFF    0x0FFE   FFFF     (Fallback „kein Gerät")  ⚠
```

**Nutzen:** Dieser eine Screen hätte die Ursache sofort sichtbar gemacht: ein Gerät mit
**freigegebenem** Interrupt, dessen Tabelleneintrag leer ist. Für jedes Fremd-OS, das
seine eigene IM-2-Tabelle aufbaut, ist das die zentrale Diagnose.

---

## 7. ★★ `g <n>` läuft auf der ZVE1-Uhr → scheinbarer Hänger

**Schmerz (gemessen, `disks/udos_boot_scp.hfe`):**

| Kommando | Wandzeit |
|---|---|
| `g 15000000` | **0,30 s** (`ran 15027479 cyc`) |
| `g 15500000` | **> 90 s** (abgebrochen) |
| `g 20000000` | **> 240 s** (abgebrochen) |

`boot_trace` fährt dieselbe Disk in Sekunden auf 45 Mio. Takte. Ich habe deshalb
mehrfach `k1520dbg` für tot gehalten und auf `boot_trace` ausweichen müssen — genau in
der Phase, in der die interaktiven Fähigkeiten am nötigsten waren.

**Beleg:** `A5120Machine::cpuCycles()` liefert `zre_.cpu().cycles`, also die Uhr **der
ZVE1** (`a5120.h:57`); `g` schleift darauf (`k1520dbg.cpp:821`:
`while (m.cpuCycles()-start < cycles) m.run(50000)`). Sobald ZVE2 den Bus hält und ZVE1
geparkt ist — nach dem Absturz steht ZVE1 auf `0x0038`, während ZVE2 **7,1 Mio.**
Instruktionen ausführt — kriecht diese Uhr, die Maschine läuft aber weiter.
`boot_trace` zählt maschinenweit (`total_cycles_`) und ist deshalb unauffällig.

**Feature:**
- `g`/`--until` auf die **maschinenweite** Uhr stellen (oder `g --machine <n>` anbieten
  und die Uhrenwahl in `where`/Prompt anzeigen — heute steht nur beim Start
  „Clock = ZVE1 cycles").
- **Fortschrittsanzeige + Ctrl-C** für lange Läufe: alle paar Sekunden
  `… cyc=… ZVE1 PC=… ZVE2 PC=… busrq=…` auf stderr. Ein hängender Lauf wäre damit
  sofort als „ZVE2 dreht durch" erkennbar statt als toter Debugger.

---

## 8. ★ Kleinigkeiten mit gutem Verhältnis

- **`u` mit Quellannotation auch ohne Treffer sauber ausrichten** — bei
  `u 0x0BFD 40` lief die Disassembly in nicht geladenen Speicher und produzierte 40
  Zeilen `FF RST 38H`. Ein Hinweis „ab 0x0C00 unbeschriebener Speicher (0xFF)" statt
  40 sinnloser Zeilen spart Scrollen. (Nebenbei: `u <adr> <hi>` wird als
  `u <adr> <anzahl>` interpretiert — die Verwechslung kostete einen Fehlversuch.)
- **`--watchio` zeigt beide CPU-PCs, sagt aber nicht, *wer* zugegriffen hat.** Die Zeile
  `OUT(11)=03 (Z1.PC=0510 Z2.PC=026B)` habe ich zuerst ZVE1 zugeschrieben; tatsächlich
  war es ZVE2 (Boot-ROM-Routine `0x0269`). Diese Fehlzuordnung hat mich eine ganze
  Analyse-Schleife gekostet. Die Callbacks der beiden CPUs sind in `k2526.cpp:86/96`
  getrennt — die Herkunft ist also bekannt und sollte als `von=ZVE2` mit ausgegeben
  werden (und als Filter `--watchio 0x11:zve2` nutzbar sein).
- **`--watchio` zeichnet die frühe Boot-Phase nicht auf** (Ursache nicht nachverfolgt):
  Mit `--watchio 0x11` erschienen nur **4** Schreibzugriffe (ab Takt 2 505 634), das
  Port-Histogramm desselben Laufs meldet aber **10** — es fehlen genau die sechs
  Boot-ROM-Zugriffe (`zre.prn` `0x00DF`/`0x00E7`/`0x00ED`, u. a. das Setzen des Vektors
  `0xBA`). Die Differenz war zunächst verwirrend und ließ mich den Vektor `0xBA`
  fälschlich für unprogrammiert halten. Entweder aufzeichnen ab Takt 0 oder in der
  Ausgabe vermerken, ab wann aufgezeichnet wurde.

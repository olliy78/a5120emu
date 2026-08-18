# k1520dbg — Handbuch

**Programme auf dem A5120 debuggen, ohne einen A5120 aufschrauben zu müssen.**

Dieses Handbuch richtet sich an **Anwender** des Emulators: an alle, die ein Programm
für einen K1520-Rechner untersuchen, reparieren oder verstehen wollen — ein eigenes
Assemblerprogramm, ein altes `.COM` ohne Quelltext, einen Treiber, der die Diskette
nicht mehr liest.

Es setzt Kenntnisse des Z80-Assemblers voraus, aber **keine** Kenntnis des
Emulator-Quelltexts.

> **Die drei Dokumente zum Debugger**
>
> | Dokument | Was drin steht |
> |---|---|
> | **dieses Handbuch** | Wie man den Debugger für **eigene Programme** benutzt |
> | `tools/k1520dbg.md` | **Referenz**: jedes Kommando, jede Option, jede Ausgabeform |
> | `tools/how_to_debug_and_trace.md` | Rezepte für die **Emulatorentwicklung** (Boot-Kette, ZVE1↔ZVE2-DMA) |
>
> Wer wissen will *was ein Kommando genau tut*, schlägt in der Referenz nach. Wer
> wissen will *wie man eine Aufgabe löst*, ist hier richtig.

---

## Inhalt

1. [Warum ein Debugger im Emulator mehr kann als an echter Hardware](#1-warum-ein-debugger-im-emulator-mehr-kann-als-an-echter-hardware)
2. [Die erste Sitzung](#2-die-erste-sitzung)
3. [Anhalten, schauen, weiterlaufen](#3-anhalten-schauen-weiterlaufen)
4. [Ohne Quelltext: der eingebaute Disassembler](#4-ohne-quelltext-der-eingebaute-disassembler)
5. [Mit Quelltext: `.prn`-Listings und `.MAC`-Quellen](#5-mit-quelltext-prn-listings-und-mac-quellen)
6. [In die Hardware hinein: Karten, PIO, SIO, CTC](#6-in-die-hardware-hinein-karten-pio-sio-ctc)
7. [Die Zeit anhalten: Snapshots, Rückwärtsschritt, Zustandsdateien](#7-die-zeit-anhalten-snapshots-rückwärtsschritt-zustandsdateien)
8. [Programme fernsteuern und Sitzungen automatisieren](#8-programme-fernsteuern-und-sitzungen-automatisieren)
9. [Die Maschine live bedienen: der Konsolenmodus](#9-die-maschine-live-bedienen-der-konsolenmodus)
10. [Rezepte für typische Fälle](#10-rezepte-für-typische-fälle)
11. [Grenzen und Fallstricke](#11-grenzen-und-fallstricke)
12. [In den eigenen Arbeitsablauf einbinden](#12-in-den-eigenen-arbeitsablauf-einbinden)
13. [Spickzettel](#13-spickzettel)

---

## 1. Warum ein Debugger im Emulator mehr kann als an echter Hardware

Am originalen A5120 ist Fehlersuche mühsam. Es gibt keinen Halt-Knopf, der die
Peripherie einfriert; ein Logikanalysator hängt an den Leitungen, die man vorher
angelötet hat; und jeder Versuch kostet einen Kaltstart. Vor allem aber gilt: **wer
misst, verändert.** Ein Monitorprogramm im Speicher belegt RAM, ändert den Stapel und
braucht Interrupts, die es eigentlich beobachten sollte.

Im Emulator sind fünf Dinge möglich, die an der Maschine nicht gehen:

**Die ganze Maschine steht wirklich still.** Ein Haltepunkt friert nicht nur die CPU
ein, sondern auch die CTC-Zähler, die PIO-Interruptlogik, den Kopf des
Diskettenlaufwerks und den Zeichenzähler des Bildschirms. Man kann beliebig lange
nachdenken, ohne dass ein Zeitgeber weiterläuft. Zurück im Programm geht es exakt an
derselben Stelle weiter — kein Zeitverzug, keine verpassten Interrupts.

**Man sieht in die Bausteine hinein.** `dev ctc` zeigt den Zählerstand jedes
CTC-Kanals *und* dessen Interruptzustand (`en`/`pend`/`ius`/`iei`) — Bits, die an echter
Hardware nur intern existieren und über keinen Anschluss auslesbar sind. Dasselbe für
alle drei PIOs, die beiden SIO-Kanäle und den Diskettenkontroller.

**Beobachten kostet nichts.** Der Debugger lebt außerhalb der emulierten Maschine. Er
belegt kein Byte RAM, verschiebt keinen Stapel, verbraucht keinen Interruptvektor. Ein
Programm läuft unter dem Debugger **exakt** so wie ohne ihn — auch das
Interrupt-Timing bleibt gleich.

**Man kann rückwärts.** Einen Schritt zu weit gesteppt? `rs` macht ihn rückgängig.
Der Absturz liegt hinter einem? `rc` springt zum vorigen Haltepunkttreffer zurück.
An echter Hardware bleibt nur: neu starten und beim nächsten Mal früher anhalten.

**Jeder Lauf ist gleich.** Dieselbe Diskette, dieselben Tastenanschläge, dieselbe
Taktzahl — derselbe Ablauf, bis aufs Bit. Ein Fehler, der „nur manchmal" auftritt,
tritt hier immer an derselben Taktnummer auf. Und wer den Zustand einmal sichert
(`savestate`), fängt beim nächsten Mal direkt vor dem Fehler an, statt drei Minuten zu
booten.

Dazu kommt eine Kleinigkeit mit großer Wirkung: **die Diskette ist sicher.** Der
Debugger mountet standardmäßig eine Kopie (Copy-on-Write); was das Programm schreibt,
landet in einer Wegwerfdatei. Man kann ein Formatierprogramm zwanzigmal auf eine
wertvolle Diskette loslassen, ohne sie zu verlieren.

---

## 2. Die erste Sitzung

### Aufrufen

```sh
k1520dbg meine_diskette.img               # Diskette in Laufwerk A:
k1520dbg system.hfe -b daten.hfe          # zweite Diskette in Laufwerk B:
k1520dbg a.hfe -b b.hfe -c c.hfe -d d.hfe # alle vier Laufwerke
k1520dbg system.hfe --console             # sofort live bedienen (§9)
```

Angenommen werden `.img`, `.hfe` und `.dmk`; die K5122 hat **vier** Laufwerke. Ohne Diskette startet der Debugger auch —
dann kommt die Maschine allerdings nicht über das Boot-ROM hinaus.

**Mountmodus** (nur wenn nötig):

| Schalter | Wirkung |
|---|---|
| *(Vorgabe)* | Copy-on-Write — Schreibzugriffe landen in einer Temp-Kopie und werden verworfen |
| `--rw` | Original beschreiben (wenn Änderungen erhalten bleiben sollen) |
| `--ro` | Original schreibgeschützt (die Maschine sieht /WP wie bei geklebter Kerbe) |

### Bis zum eigenen Programm kommen

Der Debugger startet die Maschine am Boot-ROM — genau wie ein Kaltstart. Der Weg zum
eigenen Programm führt deshalb durch den Systemstart. Statt Taktzahlen zu raten,
wartet man auf den **Bildschirminhalt**:

```
(dbg) gscreen "A>"          ← laufen, bis der Bildschirm „A>" zeigt
(dbg) keys hardy\r          ← Programmname tippen, Enter (\r)
```

Fragt das System beim Start nach der Uhrzeit, kommt das davor:

```
(dbg) gscreen "Uhrzeit"
(dbg) keys 120000\r
(dbg) gscreen "A>"
```

### Am Programmanfang anhalten

Ein CP/M-artiges Programm wird ab **0100H** geladen und dort gestartet. Das ist der
eine Haltepunkt, den man fast immer will:

```
(dbg) b 0x0100
(dbg) keys hardy\r
(dbg) g
   (ran 4908245 cyc)
** bp ZVE1 : ZVE1 PC=0100
  ZVE1 PC=0100 SP=C3F9(->C2B6) AF=0044[-Z-P--] BC=0080 DE=D109 HL=0000 IX=D26F IY=2200
  => 0100: C3 5D 04       JP 045DH
```

Damit steht die Maschine **vor** dem ersten Befehl des Programms: der Lader ist fertig,
das Programm hat noch nichts getan. `=>` markiert immer den nächsten, noch **nicht**
ausgeführten Befehl.

> **Wichtig, weil es anderswo anders ist:** ein Haltepunkt greift **vor** der
> Instruktion. Was `r` anzeigt, ist der Zustand *davor*. Wer aus gdb kommt, kennt das;
> wer aus manchen Monitorprogrammen kommt, erwartet den Zustand *danach*.

Und schon hier zeigt sich, wo man gelandet ist:

```
(dbg) bt
  #0 0100
  #1 C2B3 (call → 0100, ret C2B6)     ← der CCP hat uns aufgerufen
  #2 E6B2 (call → E6FA, ret E6B5)
  #3 C52D (call → D20C, ret C530)
```

---

## 3. Anhalten, schauen, weiterlaufen

### Laufen und Schritte

| Befehl | Wirkung |
|---|---|
| `g` | weiterlaufen bis zum nächsten Haltepunkt (Ctrl-C bricht ab) |
| `g 500000` | genau 500 000 Takte laufen |
| `gu 0x0A20` | laufen, bis Adresse `0A20` erreicht ist (einmalig) |
| `s` / `s 10` | ein / zehn Befehle **hinein**-steppen (in `CALL` hinein) |
| `n` | einen Befehl **über**-steppen (`CALL`, `LDIR`, `INIR` als Ganzes) |
| `fin` | aus dem laufenden Unterprogramm **heraus**laufen |

### Haltepunkte

```
(dbg) b 0x0A20                 Haltepunkt setzen
(dbg) b 0x0A20 if A==0xFF      … nur, wenn A gleich FF ist
(dbg) b LESEN                  … auf ein Symbol/Label (siehe §5)
(dbg) tb 0x0A20                nur einmal halten, dann sich selbst löschen
(dbg) bi 0x0A20 99             die nächsten 99 Treffer überspringen
(dbg) bl                       alle Haltepunkte mit Trefferzähler zeigen
(dbg) bd 0x0A20                löschen
```

Die **Bedingung** ist ein vollwertiger Ausdruck: Register (`A`, `HL`, `SP`), Speicher
(`[0x0446]` byteweise, `[HL]w` als Wort), Rechnen und Vergleichen. Damit lässt sich
„halte beim 500. Sektor" oder „halte, wenn der Fehlercode ungleich 0 ist" direkt
hinschreiben — statt fünfhundertmal `g` zu tippen.

### Register und Speicher ansehen

```
(dbg) r                     Register + Flags, Rücksprungadresse, Schattenregister
(dbg) d 0x0446 32           Hex+ASCII-Dump
(dbg) x/8xb 0x0446          acht Bytes hex   (gdb-Syntax)
(dbg) x/4dw HL              vier Worte dezimal ab HL
(dbg) x/s 0xF800            Zeichenkette
(dbg) e 0x0446 3E 00 C9     Bytes überschreiben (Poke, hex)
(dbg) set HL 0x6000         Register setzen
```

> **Zahlenfalle:** Adressen sind **dezimal**, wenn kein `0x`/`H` dabeisteht.
> `d 6000` liest ab dezimal 6000 = `1770H`. Hex immer als `0x6000` oder `6000H`.

### Beobachten statt anhalten

Manchmal will man nicht halten, sondern **mitschreiben**:

```
(dbg) logpoint 0x0A20 A HL      bei jedem Durchlauf A und HL drucken, weiterlaufen
(dbg) disp [0x0446]             bei jedem Halt automatisch diese Speicherzelle zeigen
(dbg) trace lauf.txt 0x0400 0x0800   jeden Befehl im Fenster in eine Datei schreiben
```

---

## 4. Ohne Quelltext: der eingebaute Disassembler

Der häufigste Fall bei alten Programmen: es gibt nur das `.COM`. Der Debugger bringt
einen vollständigen Z80-Disassembler mit (inklusive `ED`/`CB`/`DD`/`FD`), der bei jedem
Halt und auf Zuruf arbeitet.

```
(dbg) u 0x045D 8
  045D: F3             DI
  045E: 31 44 0D       LD SP,0D44H
  0461: 3A 07 00       LD A,(0007H)
  0464: 3D             DEC A
  0465: 32 46 04       LD (0446H),A
  0468: ED 57          LD A,I
  046A: 32 21 04       LD (0421H),A
  046D: 01 00 01       LD BC,0100H
```

`u` ohne Argumente macht dort weiter, wo es aufgehört hat — man blättert sich also
durch. Läuft die Ausgabe in unbeschriebenen Speicher (lauter `RST 38H` aus `FF`-Bytes),
bricht sie von selbst ab, statt vierzig sinnlose Zeilen zu drucken.

### Sich Namen geben

Ein Disassemblat ohne Namen liest sich schlecht. Was man herausgefunden hat, kann man
festhalten — und danach überall als Adresse benutzen:

```
(dbg) sym add LESEN 0x0A20
(dbg) sym add FEHLERCODE 0x0446
(dbg) b LESEN                   funktioniert ab jetzt
(dbg) x/1xb FEHLERCODE
```

Größere Namenslisten schreibt man in eine Datei (eine Zeile `name adresse` oder
`adresse name`) und lädt sie mit `-s namen.sym` beim Start oder `sym namen.sym`
während der Sitzung. Sprungziele im Disassemblat werden danach mit `<name>` annotiert.

### Sich orientieren, wenn man nichts weiß

Drei Kommandos beantworten die Frage „was tut dieses Programm überhaupt gerade?":

**`bt` — wie bin ich hierher gekommen?** Ein mitlaufender Aufrufstapel, nicht geraten,
sondern aus jedem `CALL`/`RST`/`RET` mitgeschrieben.

**`hist` — wo verbringt es seine Zeit?** Der Profiler zählt die Programmzähler über
eine gewählte Taktzahl und zeigt die Spitzenreiter. Eine Endlosschleife fällt sofort
auf:

```
(dbg) hist 2000000
hist over 2000236 cyc:
  ZVE1 top (201242 instrs, 34 distinct PCs):
      6.06%   12194  0E67
      6.06%   12194  0E65
      6.06%   12194  0E63
      …
```

Elf Adressen zwischen `0E56` und `0E69`, jede exakt gleich oft — das ist eine
Warteschleife von elf Befehlen. Ohne dieses Kommando hätte man das mit Einzelschritten
gesucht.

**`screen`** zeigt den Textbildschirm als Text. Was das Programm ausgibt, ist oft der
schnellste Hinweis darauf, wo es steht.

---

## 5. Mit Quelltext: `.prn`-Listings und `.MAC`-Quellen

Liegt der Quelltext vor, muss man nicht disassemblieren. Der Debugger legt die
**Originalquelle mit ihren Kommentaren** an jede Disassemblatzeile.

### MACRO-80-Listings (`.prn`)

Ein `.prn` ist das Listing, das der Assembler neben dem Objektcode ausgibt — es
enthält Adresse, Objektbytes, Quelltext und Kommentare. Genau die richtige Kost:

```sh
k1520dbg system.img -l bios.prn
```

```
(dbg) u 0xD433 3
  D433: DB 5D          IN A,(5DH)  ; in a,(kbdpcs)
  D435 <kbdstc>: E6 01  AND 01H    ; kbdstc: cpl
```

Zwei Dinge passieren beim Laden: die Kommentare erscheinen hinter dem Disassemblat,
**und alle Labels des Listings werden zu Symbolen**. Danach funktioniert `b kbdstc`,
`list kbdstc`, `u kbdstc` unmittelbar.

`list` (kurz `l`) zeigt statt Disassemblat den **Quelltext** um eine Adresse herum:

```
(dbg) list 0xD435 6
     D433  in a,(kbdpcs)
  => D435  kbdstc: cpl ;auf NOP, wenn Status negiert
     D436  bit 3,a ;Taste gedrueckt?
     D438  jr z,kbdst0 ;nein
```

### Quelltext ohne Adressen (`.MAC`, `.ASM`)

Für viele Fremdprogramme gibt es kein Listing, sondern nur reinen Quelltext ohne
Adressspalte. Auch den nimmt der Debugger: er **assembliert ihn selbst** und baut
dieselbe Zuordnung Adresse → Quellzeile auf.

```
(dbg) lst treiber.mac
```

### Passt die Quelle überhaupt zu diesem Programm?

Das ist die eigentliche Frage — und der Debugger beantwortet sie. `@auto` sucht die
Objektbytes der Quelle im Speicher, bestimmt daraus den Ladeversatz selbst und
**urteilt über die Passung**:

```
(dbg) lst bios.prn@auto
  @auto: Versatz -33792 / 7C00 — gleiches Programm, ANDERER Build (5409 von 7456 Bytes, 72.5 %)
```

Vier Urteile gibt es: *identischer Build* · *gleicher Build, wenige Abweichungen* ·
*gleiches Programm, ANDERER Build* · *schwacher Treffer*. Unter 60 % wird der Versatz
gar nicht erst angewandt — eine kaum passende Quelle würde sonst Kommentare über
fremden Code legen, und das ist schlimmer als gar keine Kommentare.

> **Ein echtes Beispiel, und die Lehre daraus.** Das obige Urteil stammt aus einer
> echten Sitzung: das BIOS-Listing gehört zwar zu dieser Diskette, ist aber ein anderer
> Build als das laufende BIOS. Am Einsprung stimmen die Kommentare noch (`IN A,(5DH)`
> ↔ `in a,(kbdpcs)`), zwei Befehle später nicht mehr (`AND 01H` ↔ `cpl`). Die Regel
> ist deshalb: **das Disassemblat ist die Wahrheit, der Kommentar ist ein Hinweis.**
> Wo beide Mnemoniks auseinandergehen, hat die Quelle unrecht, nicht die CPU.

Wie weit die Übereinstimmung reicht, klärt `verify` — es vergleicht eine Datei
byteweise mit dem Speicher und listet die Abweichungen:

```
(dbg) verify treiber.bin @0x0A00
```

Damit sind drei Fragen in einem Kommando beantwortet: die richtige Datei? derselbe
Build? und wenn nein — wo genau nicht?

---

## 6. In die Hardware hinein: Karten, PIO, SIO, CTC

Hier liegt der eigentliche Unterschied zu einem gewöhnlichen CP/M-Debugger. Der
K1520-Rechner ist kein Chip, sondern ein Bus mit Steckkarten, und der Debugger sieht
in **jede** hinein.

### Die Karten und ihre Anschlüsse

| Karte | Was sie ist | Ports | Ansehen mit |
|---|---|---|---|
| **K2526** (ZRE) | CPU-Karte: beide Z80, System-CTC, BS-PIO | CTC, PIO `08–0B` | `dev ctc`, `dev pio bs` |
| **K5122** | Diskettenkontroller mit zwei PIOs | Steuer `10–13`, Daten `14–17`, Laufwerkswahl `18` | `dev`, `dev pio k5122ctrl` |
| **K8025** | Serienkarte: Tastatur und Drucker | Tastatur-SIO `5C/5D` | `dev sio` |
| **K7024** | Bildschirm | Textpuffer ab `F800` | `screen` |
| **K3526** | Arbeitsspeicher | — | `d`, `x` |

### Zeitgeber: `dev ctc`

```
(dbg) dev ctc
  CTC (K2526)  vecBase=F8  IEI=1 IEO=1
    ch0 ctl=03 TC=00 cnt=0   run=0  INT(en=0 pend=0 ius=0 iei=1)
    ch1 ctl=03 TC=00 cnt=0   run=0  INT(en=0 pend=0 ius=0 iei=1)
    ch2 ctl=B1 TC=30 cnt=31  run=1  INT(en=1 pend=0 ius=0 iei=1)
    ch3 ctl=D1 TC=C8 cnt=87  run=1  INT(en=1 pend=0 ius=0 iei=1)
```

Zu lesen ist das so: Kanal 0 und 1 sind angehalten (`run=0`), Kanal 2 und 3 laufen mit
Interrupt (`en=1`) und stehen gerade bei 31 bzw. 87. `pend` heißt „Interrupt liegt an,
aber die CPU hat ihn noch nicht angenommen", `ius` heißt „wird gerade bedient". Genau
diese beiden Bits erklären die meisten Interruptprobleme — und genau sie sind an echter
Hardware unsichtbar.

### Parallelbausteine: `dev pio`

```
(dbg) dev pio bs
  BS-PIO (K2526, Ports 08-0B)  IEI=1 IEO=1
    A mode=3 out=FF in=B6 dir=7F vec=B8  INT(en=0 pend=0 ius=0 iei=1)
    B mode=3 out=16 in=02 dir=E2 vec=FF  INT(en=0 pend=0 ius=0 iei=1)
```

Je Port: Betriebsart, zuletzt ausgegebenes Byte, anliegendes Eingabebyte,
Richtungsmaske (bei Betriebsart 3 bitweise), Interruptvektor und Interruptzustand.
`dev pio all` zeigt alle drei PIOs; die beiden K5122-PIOs sind die interessanten, wenn
ein Diskettenzugriff hakt.

### Serienbaustein: `dev sio`

```
(dbg) dev sio
  SIO kbd/prn (K8025 A32)  IEI=1 IEO=1
    A rr0=04 rr1=01 wr1=00 vec=00  irq(rx=0 tx=0 ext=0) ius=0 iei=1  rxQ=0 txBusy=0
```

`rxQ` ist die Zahl wartender Empfangszeichen, `txBusy` sagt, ob gerade gesendet wird.
Wer wissen will, warum eine Tastatureingabe nicht ankommt, sieht hier zuerst nach.

### Wer schreibt eigentlich auf diesen Port?

Ein Port-Beobachter meldet **jeden** Zugriff mit Taktnummer, Wert und dem
**Programmzähler des Verursachers**:

```
(dbg) iow 0x5D
(dbg) g 300000
[io] c31576965  IN  (5DH)=04  ZVE1.PC=D435
[io] c31619215  IN  (5DH)=04  ZVE1.PC=D435
[io] c31621112  IN  (5DH)=04  ZVE1.PC=D435
```

Übersetzt: das BIOS pollt an Adresse `D435` den Tastaturstatus, im Abstand von rund
1900 Takten, und bekommt jedes Mal `04`. Diese eine Ausgabe beantwortet gleichzeitig
*wer*, *wie oft*, *was* und *wann* — an echter Hardware bräuchte man dafür einen
Logikanalysator, und die PC-Spalte bekäme man auch damit nicht.

`iob <port>` hält an, statt zu drucken; `iod`/`iol` löschen bzw. listen.

### Wer schreibt eigentlich in diese Speicherzelle?

Dasselbe für den Speicher — die klassische Frage bei überschriebenen Variablen:

```
(dbg) wb 0x0446                 anhalten, wenn jemand 0446 beschreibt
(dbg) g
   (ran 54 cyc)
** watch WR [0446]=C3 by ZVE1.PC=0468 : ZVE1 PC=0468
  => 0468: ED 57          LD A,I
```

Der Schreiber war der Befehl bei `0465` (`LD (0446H),A`), der Wert `C3`. Auch ganze
Bereiche gehen (`wp 0x6000..0x60FF`), mit Wertbedingung (`wb 0x0446 == 3`) oder nur bei
echter Änderung (`changed`).

> **Merken:** Speicher- und Portbeobachter halten **nach** dem Zugriff an — deshalb
> steht oben `PC=0468` und nicht `0465`. Das ist Absicht: Ein Schreibzugriff, den man
> vorher abbricht, wäre nicht beobachtbar; so steht der beobachtete Wert wirklich im
> Speicher.

### Interrupts: `ivt`, `bint`, `bnmi`

Bei IM 2 zeigt jeder Interruptvektor in eine Tabelle. `ivt` löst diese Kette komplett
auf — von der Quelle über den Tabelleneintrag bis zur tatsächlichen Serviceroutine:

```
(dbg) ivt
  ZVE1: I=F7  IM 2  IFF1=1
  Vektor Tabelle Eintrag Geraet                     Status
   0xE8  0xF7E8  E39A    K5122 ctrl-PIO A           ok
   0xFC  0xF7FC  E5FD    K2526 CTC ch2              ok
   0xFE  0xF7FE  E62B    K2526 CTC ch3              ok
```

Ein scharf geschalteter Baustein, dessen Tabelleneintrag ins Leere zeigt, fällt hier
sofort auf — das ist die häufigste Ursache für einen Rechner, der „einfach abstürzt".

Dazu die Ereignis-Haltepunkte:

| Befehl | Hält an, wenn … |
|---|---|
| `bint` | ein Interrupt **angenommen** wird (an der ersten Zeile der ISR) |
| `bnmi` | ein NMI kommt — auf dem A5120 die **Speicherschutzverletzung** |
| `breti` | vor jedem `RETI`/`RETN` |
| `itrace datei` | schreibt jeden angenommenen Interrupt mit (ohne anzuhalten) |

### Diskette: `dev` und `disk verify`

```
(dbg) dev
  K5122: D0 mounted  cyl=11 head=0  idle  headPos=2214/5505 secSize=1024  /BUSRQ-pend=no
```

Kopfposition, Zylinder, Seite, Sektorgröße und ob gerade übertragen wird. `disk verify`
liest die ganze Diskette durch und meldet Sektor-IDs und CRC-Zustand je Spur — die
Antwort auf „liegt es am Programm oder an der Diskette?".

---

## 7. Die Zeit anhalten: Snapshots, Rückwärtsschritt, Zustandsdateien

### Einen Schritt zurück

Vor **jedem** Vorwärtskommando legt der Debugger selbsttätig einen vollständigen
Zustandsabzug an (Ringpuffer, Tiefe 200):

```
(dbg) rs          ein Kommando rückgängig
(dbg) rs 5        fünf Kommandos rückgängig
(dbg) rc          zurück zum vorigen Haltepunkttreffer
```

Das ändert die Arbeitsweise: man muss nicht mehr vorsichtig steppen. Einfach `n`
drücken, und wenn es zu weit war, `rs`.

### Benannte Abzüge

```
(dbg) snap vorher            Zustand merken
(dbg) e 0x0446 00            etwas ausprobieren
(dbg) g
(dbg) restore vorher         alles zurück
(dbg) snap diff vorher nachher    was hat sich geändert?
```

`snap diff` nennt geänderte Register und geänderte Speicherbereiche — die Antwort auf
„was genau hat dieses Unterprogramm angefasst?".

### Einmal booten, immer wieder anfangen

Der teuerste Teil jeder Sitzung ist der Systemstart. Den zahlt man **einmal**:

```
(dbg) savestate hardy_start.state
  state saved → hardy_start.state (PC=0100)
```

Beim nächsten Mal:

```sh
k1520dbg meine_diskette.img
(dbg) loadstate hardy_start.state
  state loaded ← hardy_start.state
  => 0100: C3 5D 04       JP 045DH
```

Gesichert werden 64 KB RAM, beide CPUs, die ROM-Einblendung, das Tastatursubsystem,
der Diskettenkontroller samt Kopfposition und der Bildschirmspeicher. **Nicht**
enthalten sind die Diskettenabbilder selbst — die werden beim Start ganz normal
gemountet. Ein laufender Diskettentransfer wird beim Laden auf Ruhe gesetzt.

---

## 8. Programme fernsteuern und Sitzungen automatisieren

### Tasten schicken

```
(dbg) keys dir\r                    Text tippen, Enter
(dbg) keys \e                       ESC
(dbg) keys \s                       Leertaste
(dbg) keys \x03                     roher Tastencode (hier Strg-C)
```

Die Maschine läuft dabei weiter — die Tastatur ist seriell (9600 Baud) modelliert, ein
Anschlag braucht also seine Zeit, genau wie im Original.

### Auf den Bildschirm warten statt Takte zu raten

```
(dbg) gscreen "Bitte waehlen"       laufen, bis der Text erscheint
(dbg) gscreen /Fehler [0-9]+/       … oder ein regulärer Ausdruck
(dbg) bscreen "SPUR DEFEKT"         ab jetzt bei jedem g/n halten, wenn das erscheint
(dbg) keyuntil "\r" "A>"            eine Taste wiederholen, bis der Bildschirm passt
```

`bscreen` ist der Trick für „irgendwann kommt eine Fehlermeldung, ich weiß nicht
wann": armen, laufen lassen, und der Debugger hält genau im Moment der Meldung — mit
allen Registern und dem vollständigen Hardwarezustand von genau diesem Augenblick.

Ganze Menüfolgen fährt `dialog` aus einer Datei ab (je Zeile: worauf warten, was
tippen).

### Sitzungen als Datei

Alle Kommandos lassen sich in eine Datei schreiben und abfahren:

```sh
k1520dbg diskette.img -x sitzung.dbg
```

```
# sitzung.dbg — Zeilen mit # sind Kommentare
gscreen "A>"
b 0x0100
keys meinprog\r
g
r
```

Ebenso über eine Pipe (`printf 'b 0x0100\ng\nr\nq\n' | k1520dbg diskette.img`), was
sich für wiederholbare Prüfungen und Skripte anbietet. `rj` gibt die Register als
JSON-Zeile aus, wenn ein Skript sie weiterverarbeiten soll.

Alle Ausgaben gehen auf die Fehlerausgabe und lassen sich damit mitschneiden:
`k1520dbg … 2>&1 | tee sitzung.log`.

---

## 9. Die Maschine live bedienen: der Konsolenmodus

Bis hierher wurde die Maschine *ferngesteuert* — `keys` tippt, `gscreen` wartet. Manchmal
will man aber einfach **selbst tippen**: ein Menü durchklicken, eine Eingabe
ausprobieren, ein Programm bedienen, bis der Fehler kommt. Dafür gibt es `console`.

```
(dbg) console
```

Ab da geht jeder Tastenanschlag an die Maschine, und der Bildschirm steht im Terminal —
in Echtzeit. `console 4` läuft vierfach (praktisch, um über einen langen Selbsttest
hinwegzukommen), `console 0.5` halb so schnell.

| Taste | Wirkung |
|---|---|
| `Ctrl-]` | zurück in den Debugger |
| `Ctrl-C` | geht **an das Gastprogramm** — nicht an den Debugger |
| Pfeile, `Entf`, `Tab`, `Esc`, `F1`–`F8` | auf die Tasten der K7637 abgebildet |

### Der eigentliche Grund: Haltepunkte bleiben scharf

Das ist der Punkt, an dem der Konsolenmodus mehr ist als eine Notlösung ohne
Oberfläche:

```
(dbg) b 0x0100
(dbg) console
… von Hand die Uhrzeit eingeben, „meinprog" tippen, Enter …
** bp ZVE1 : ZVE1 PC=0100
  => 0100: C3 5D 04       JP 045DH
(dbg) bt
```

Der Konsolenmodus **verlässt sich selbst**, sobald ein Haltepunkt, ein Watchpoint oder
eine `bscreen`-Bedingung greift — und man steht mit allen Werkzeugen (`bt`, `dev`,
`snap`, `u`) genau im interessanten Moment. Bedienen und sezieren im selben Programm;
die Oberfläche kann keine Haltepunkte, und bis 2026-08-19 konnte der Debugger nicht
live tippen.

Ein typischer Ablauf: `bscreen "FEHLER"` armen, in den Konsolenmodus gehen, das Programm
von Hand bis zur Fehlermeldung bedienen — und in dem Augenblick, in dem sie erscheint,
steht die Maschine.

### Grenzen

* Braucht ein **Terminal**. Über eine Pipe sagt `console` das und verweist auf
  `keys`/`gscreen`.
* Das Terminal sollte **80×25** Zeichen haben.
* Dargestellt wird der Zeichencode als ASCII; wo der Zeichengenerator des A5120 eine
  andere Glyphe zeigt, sieht man den ASCII-Code.
* Programme, die die Tastatur **direkt abfragen** statt gepuffert zu lesen (HARDY tut
  das), können einen live getippten Anschlag verpassen. Das ist originalgetreu — die
  Maschine verhält sich so. Für solche Fälle bleibt `keyuntil` (§8).

---

## 10. Rezepte für typische Fälle

### „Mein Programm hängt"

```
(dbg) hist 2000000        wo dreht es sich?   → Adressbereich der Schleife
(dbg) b <adresse aus der Schleife>
(dbg) g
(dbg) bt                  wer hat die Schleife aufgerufen?
(dbg) r                   worauf wartet sie? (Register ansehen)
```

Wartet die Schleife auf ein Portbit, verrät `iow <port>`, ob der Baustein überhaupt
antwortet. Wartet sie auf eine Speicherzelle, verrät `wp <adresse>`, ob sie je
beschrieben wird — und wenn nicht, ist meist ein Interrupt nicht scharf: `ivt`.

### „Mein Programm stürzt ab"

```
(dbg) bnmi                Speicherschutzverletzung? (auf dem A5120 ein NMI)
(dbg) b 0x0000            Neustart durch Sprung auf 0?
(dbg) g
(dbg) bt                  Aufrufweg zum Absturz
(dbg) rc                  zurück zum vorigen Halt und diesmal langsam
```

### „Wer überschreibt meine Variable?"

```
(dbg) wb 0xNNNN
(dbg) g
```

Der Halt nennt Wert, Adresse und den Programmzähler des Schreibers.

### „Der Diskettenzugriff geht schief"

```
(dbg) dev                          Kopfposition, Zylinder, Sektorgröße
(dbg) disk verify                  ist die Diskette selbst in Ordnung?
(dbg) dev pio k5122ctrl            was steht im Kontroller?
(dbg) iow 0x18                     Laufwerkswahl mitschreiben
(dbg) bxfer read                   im Moment des Lesetransfers anhalten
```

### „Die Tastatur reagiert nicht"

```
(dbg) dev sio                      wartet ein Zeichen (rxQ)?
(dbg) iow 0x5D                     pollt das Programm überhaupt?
(dbg) ivt                          ist der Tastaturinterrupt scharf?
```

### „Ich will wissen, was dieses fremde Programm tut"

```sh
k1520dbg diskette.img -l quelle.mac@auto     falls Quelle vorhanden
```
```
(dbg) b 0x0100
(dbg) keys progname\r
(dbg) g
(dbg) u 0x0100 40           erster Blick
(dbg) trace lauf.txt 0x0100 0x0800     kompletter Ablauf in eine Datei
(dbg) g 500000
```

---

## 11. Grenzen und Fallstricke

- **Zwei CPUs.** Die ZRE-Karte trägt zwei Z80: **ZVE1** ist die Haupt-CPU, **ZVE2** ein
  DMA-Prozessor, der nur während Diskettenübertragungen läuft. Alle Kommandos ohne
  Zusatz meinen ZVE1. Für ZVE2 gibt es `b2`, `s2`, `r 2`, `rj2`; `where` zeigt beide auf
  einen Blick. Für normale Anwendungsprogramme spielt ZVE2 keine Rolle.
- **Der Bereich `0000H–07FFH`** wird von beiden CPUs benutzt. Wer dort zum
  Experimentieren hineinschreibt, kann von einer laufenden Übertragung überschrieben
  werden; freien Speicher (z. B. ab `6000H`) benutzen.
- **Beobachter halten nach dem Zugriff** (§6), Haltepunkte davor (§2).
- **Bedingte Haltepunkte kosten Zeit** — die Bedingung wird bei jedem Treffer der
  Adresse ausgewertet. Für sehr heiße Adressen lieber `bi` (Treffer überspringen).
- **`fin` braucht einen sauberen Stapel.** Bei zerschossenem Stapel läuft es in die
  Sicherheitsgrenze.
- **Ein Listing deckt nur seinen Adressbereich ab.** Ein BIOS-Listing sagt nichts über
  ein Programm bei `0100H`. Mehrere Listings gleichzeitig zu laden ist erlaubt und
  üblich.
- **Für vollständige statische Listings** eines Programms ist ein
  Kommandozeilendisassembler besser geeignet als das interaktive `u`.

---

## 12. In den eigenen Arbeitsablauf einbinden

Der Debugger ist kein Programm, das man doppelklickt und wieder schließt. Er steht
neben Editor, Assembler und Konsole — der Ablauf ist *bearbeiten → assemblieren → auf
die Diskette → laufen lassen → anhalten und nachsehen*. Dieser Abschnitt sagt, wie man
ihn dort hineinstellt.

### Was mitgeliefert wird

| Programm | Was es ist |
|---|---|
| `k1520dbg` | dieser Debugger — **nur** Kommandozeile |
| `k1520disktool-cli` | Dateien auf die Diskette und zurück — **nur** Kommandozeile |
| `k1520disktool` | dasselbe mit Oberfläche |
| `a5120emu` | der Emulator mit Oberfläche — nimmt Disketten entgegen (`a5120emu a.hfe b.hfe`, bis zu vier); die Konsolenfassung ist `k1520dbg console` (§9) |

> **Das Programm `a5120emu` hat keine Konsolenfassung** — es nimmt nicht einmal eine
> Diskette auf der Kommandozeile entgegen (einziges Argument ist `--paths`, eine
> Pfadauskunft). **Die Konsolenfassung des Emulators ist `k1520dbg console`** (§9):
> Maschine live bedienen, Bildschirm im Terminal. Für den nicht-interaktiven Betrieb —
> Skript, Makefile, CI — gibt es `keys`/`screen`/`gscreen`/`dialog` mit `-x` (§8).

### Pfade

Nach der Installation liegen die Programme in `<Installation>/bin`. Damit sie ohne
Pfadangabe laufen:

**Linux** — der Installer legt einen Starter in `~/.local/bin` ab, das genügt meist
schon. Sonst in die eigene `~/.bashrc`/`~/.zshrc`:

```sh
export PATH="$HOME/K1520emu/bin:$PATH"
```

**Windows** — im Paket liegt `k1520dbg.cmd`, **zum Anpassen gedacht**: Kopie anlegen,
Arbeitsordner und eigene Werkzeuge (Assembler!) eintragen, Verknüpfung dorthin legen,
wo man arbeitet. Es öffnet eine Eingabeaufforderung, in der `k1520dbg` und
`k1520disktool-cli` bereitstehen:

```bat
set "K1520_ROOT=%LOCALAPPDATA%\K1520emu"
set "ARBEIT=%USERPROFILE%\Projekte\z80"
set "PATH=%K1520_ROOT%in;%PATH%"
cd /d "%ARBEIT%"
cmd /k
```

### Umgebungsvariablen

Im Normalfall braucht man **keine**: die Programme finden ihre Beigaben über den Pfad
ihrer eigenen Programmdatei (`<bin>/../share/k1520emu/`). Nötig werden sie nur, wenn
etwas woanders liegt:

| Variable | Wofür | Wann nötig |
|---|---|---|
| `K1520_FORMATS` | Diskettenformat-Katalog `formats.yaml` (Datei **oder** Ordner; mehrere mit `:` bzw. unter Windows `;`) | eigener Katalog, oder Programm aus dem Bauverzeichnis gestartet |
| `K1520_HOME` | Wurzel der Installation | ungewöhnliches Layout |
| `K1520_LIB` | Kernbibliothek | mehrere Fassungen nebeneinander |
| `K1520_DISKS` | Ordner der Arbeitsdisketten | Disketten liegen woanders |
| `K1520_DATA` | Datenordner insgesamt (enthält `Disketten/`) | verschiebt beides auf einmal |

Was tatsächlich gefunden wurde, sagt `a5120emu --paths` — die erste Frage, wenn etwas
fehlt. Eigene Formate lassen sich auch ohne Variable dauerhaft hinterlegen:
`~/.config/k1520emu/formats.yaml` (Windows: `%APPDATA%\k1520emu\formats.yaml`).

### Die Runde: bearbeiten → assemblieren → testen

```sh
# 1. bearbeiten und assemblieren (Ihre Werkzeuge, hier beispielhaft)
z80asm -o PROG.COM prog.asm

# 2. auf die Arbeitsdiskette legen
k1520disktool-cli put arbeit.hfe PROG.COM
k1520disktool-cli ls arbeit.hfe

# 3a. einfach ausprobieren — mit Oberfläche, Diskette liegt schon im Laufwerk
a5120emu arbeit.hfe

# 3b. oder gleich mit Haltepunkt am Programmanfang
k1520dbg arbeit.hfe -l prog.lst
```
```
(dbg) gscreen "A>"
(dbg) b 0x0100
(dbg) keys prog\r
(dbg) g
```

Drei Kniffe, die diese Runde kurz halten:

**Den Systemstart nur einmal zahlen.** Beim ersten Mal am Programmanfang
`savestate start.state`; danach beginnt jede Sitzung mit `loadstate start.state` statt
mit einem Kaltstart (§7). Achtung: der Zustand enthält die Diskette **nicht** — nach
einem neuen `put` das Programm neu laden lassen.

**Die ganze Sitzung als Datei.** Was man jedes Mal tippt, gehört in eine `.dbg`-Datei
und wird mit `-x` übergeben (§8) — damit ist der Testlauf ein einziger Aufruf und passt
in ein Makefile oder eine Prüfliste.

**Schreibzugriffe.** Der Debugger mountet die Diskette standardmäßig als Kopie; was Ihr
Programm schreibt, ist nach dem Beenden weg. Soll es bleiben (weil das Programm eine
Datei anlegt, die Sie ansehen wollen), `--rw` benutzen — dann aber auf einer Kopie der
Diskette arbeiten.

**Die Diskette bleibt ansehbar, während der Debugger läuft** — `k1520disktool-cli ls`
in einem zweiten Fenster liest die Datei, ohne den Emulator zu stören. Bei `--rw` erst
nach dem Beenden, sonst sieht man einen halb geschriebenen Stand.

---

## 13. Spickzettel

```
LAUFEN     g [N] · gu ADR · s [N] · n [N] · fin · rs [N] · rc
HALTEN     b ADR [if AUSDRUCK] · tb · bd · bi ADR N · bl
           bint · bnmi · breti          Interrupt / NMI / RETI
           bbusrq · bxfer [read|write]  Diskettenübertragung
BEOBACHTEN wp/wpr/wb ADR[..ADR] [==W|changed]   Speicher lesen/schreiben
           iow/iob PORT                        Ein-/Ausgabe
           logpoint ADR AUSDR…                 drucken, weiterlaufen
           trace DATEI [von bis] · itrace DATEI
ANSEHEN    r · where · bt · hist TAKTE · d ADR [N] · x/8xb ADR · u [ADR] [N]
           screen · disp AUSDRUCK
HARDWARE   dev · dev ctc · dev sio · dev pio [all|bs|k5122ctrl|k5122data]
           ivt · disk verify
QUELLE     -l DATEI[@auto] · lst DATEI · list ADR · sym add NAME ADR · verify DATEI @ADR
ZUSTAND    snap NAME · restore NAME · snap diff A B · savestate F · loadstate F
STEUERN    keys TEXT (\r \s \e \xNN) · gscreen "TXT" · bscreen "TXT" · keyuntil
LIVE       console [tempo]   selbst tippen; Ctrl-] zurueck; Haltepunkte bleiben scharf
ÄNDERN     e ADR BYTES(hex) · set REG WERT
SONST      reset · alias · source DATEI · q
AUSDRÜCKE  A HL SP PC … · [ADR] Byte · [ADR]w Wort · + - * / & | ^ << >> · == != < >
ZAHLEN     Adressen dezimal, sofern nicht 0x1234 / 1234H — bei „e" sind Bytes hex
```

---

## Weiterlesen

| Frage | Dokument |
|---|---|
| Was tut Kommando X genau? | `tools/k1520dbg.md` (Referenz) |
| Wie ist der A5120 aufgebaut? | `doc/K1520_architecture.md` |
| Wie funktioniert das Diskettenformat? | `doc/cpa_format_detection.md`, `doc/udos_diskettenformat.md` |
| Dateien von einer Diskette holen | `tools/k1520disktool.md` |
| Emulatorentwicklung, Boot-Kette | `tools/how_to_debug_and_trace.md` |

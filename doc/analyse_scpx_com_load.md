# SCPX 1526 — `.COM`-Laden schlägt fehl (`BAD SECTOR`): Analyse

**Stand:** 2026-07-10. Branch `scpx_boot`, Fixture `disks/scpx_boot.hfe`.

Das OS **SCPX 1526 V1.7** bootet vollständig bis zum `A>`-Prompt; Tastatur und der CCP-eingebaute
`DIR` funktionieren. **Transiente Programme (`STAT`, `PIP`, …) luden zunächst nicht** — sie endeten mit
`SCPX ERR ON A: BAD SECTOR`. Dieses Dokument hält die vollständige Diagnose (§1–§9) **und die
Lösung (§10)** fest.

> ## ✅ GELÖST (2026-07-10) — os-gated „gehaltener Bus" für den Laufzeit-Read (§10)
> Der in §6/§9 als richtig erkannte Fix (gehaltener Bus **nur** für den Laufzeit-Read) ist jetzt
> umgesetzt, indem er auf einen **„OS-läuft"-Zustand** gated wird (§9.4b): ein `os_running_`-Flag
> wird gesetzt, sobald ZVE1 den interaktiven Prompt `E079` **das erste Mal** erreicht. Ab da hält
> der Emulator `/BUSRQ` für den **ganzen** Laufzeit-Read über an (nur ZVE2 liest), ausgelöst am
> **read-eindeutigen** Poll-Wait `E8B5` (mit `[EC0B]==E8B5` und aktivem Lese-Transfer), beendet,
> sobald ZVE2 das Ergebnis über `[EC0B]!=E8B5` signalisiert. Während des Boots (`os_running_==false`)
> ist der Mechanismus **komplett inert** → er umgeht die §6.1-Boot-Sackgassen (der Boot behält die
> Per-Byte-Drossel). **Verifiziert:** `A>STAT` → `A: R/W, Space: 522k`, `A>PIP` → `*`-Prompt; alle
> Tests grün (ctest inkl. boot_integration/k5122/k2526 + 5 format_integration, Legacy 58/58).
> Die frühere Aussage „nicht per Koroutine gatebar" bleibt korrekt — der **temporale** Gate
> (`os_running_`) statt eines zustandsbasierten Diskriminators ist genau der Ausweg aus §9.4.

---

## 1. Symptom & Reproduktion

- `A>STAT` → `SCPX ERR ON A: BAD SECTOR`. `A>PIP` → korrupter Prompt.
- `DIR` (CCP-Built-in) listet dagegen fehlerfrei: `INIT/PIP/MODF/STAT/MODX/POWER/SEPR/SYSG/SYSP`
  `.COM` + `BIOSG617/717 BIOSK617/717 CCPBD17 SYL17` `.SYS`.

**Reproduktion** (ohne GUI, Boot ~14 M Takte → im Hintergrund laufen lassen; `keys` braucht ein
**literales `\r`** für Enter):

```
b 0xE079      ; CONIN-Wartepunkt (Prompt bereit)
g
bd 0xE079
keys STAT\r
g 40000000
screen        ; zeigt "SCPX ERR ON A: BAD SECTOR"
```

⚠️ **`loadstate` reproduziert den Bug NICHT treu** (der Zustand bei `E079` führt zu einem viel
kürzeren, anderen Read-Verlauf) — immer den vollen Boot fahren.

## 2. Die Diskette ist einwandfrei

82 Zyl × 2 Köpfe, MFM, **16×256 B/Spur (N=1)**, Sektor-IDs **1..16 sequenziell**, **alle ID- und
Daten-CRCs gültig** (geprüft mit `HfeImage` + `TrackCodec::parseTrack`). Es ist **kein** Medien-,
Interleave-/Skew- oder CRC-Datenproblem. Die frühere Vermutung „Sektor-Skew" ist damit widerlegt.

## 3. Laufzeit-Lesearchitektur von SCPX (ZVE1 ↔ ZVE2)

Der Laufzeit-BIOS-Read ist **nicht** reines ZVE1-Programmed-I/O, sondern nutzt ZVE2 als DMA-Leser:

1. ZVE1 **poist an `[0x0000]` eine ZVE2-Lese-Koroutine** (SCPX: `JP E9C8`), führt die Positionierung
   durch, löst `/STR` → `/BUSRQ` aus.
2. ZVE2 startet ab `PC=0` (holt das gepoiste `[0x0000]`), läuft die **komplette Lese-/Matcher-/
   Retry-/Seek-Logik** und liest die Bytes über Port `0x16`.
3. ZVE1 wartet an der Poll-Schleife **`E8B5`** auf `[EC0B]`; ZVE2 signalisiert dort das Ergebnis und
   **versetzt sich per `OUT(04H)=0` selbst in Reset**. Danach restauriert ZVE1 den Warmstart-Vektor
   `[0x0000]=JP DE03`.

Auf **echter Hardware** hält `/BUSRQ` ZVE1 den **gesamten** Transfer über an — es läuft **nur ZVE2**,
ununterbrechbar. In unserem **Per-Byte-/BUSRQ-Modell** (für den Boot getunt) wird `/BUSRQ` pro Byte
freigegeben, sodass **ZVE1 in den Byte-Lücken mitläuft** und dieselbe Read-/Seek-Logik ausführt wie
ZVE2. Genau das ist die gemeinsame Wurzel der beiden Fehlerschichten.

## 4. Schicht 1 — Kopfposition divergiert vom Spurregister (Dual-CPU-Stepping)

Beim `.COM`-Read steppen **beide** CPUs den Schrittmotor:

```
ZVE1 (busrq=0):  0→1→2→3→4          ; ZVE1 seekt auf die Zielspur
ZVE2 (busrq=1):        4→5→6→7→8    ; ZVE2 über-fährt beim eigenen Seek-Retry
ZVE1 (busrq=0):              8→7→6→5
```

Der Kopf landet physisch auf **cyl 5**, das BIOS-IDAM-Template will jedoch **cyl 0** (`[EC01]=0`).
→ Der Matcher scheitert am **cyl-Vergleich** (`E9FD  CP C`), scannt alle 16 (gültigen!) Sektoren,
wiederholt das ~43× und meldet dann `BAD SECTOR` (Handler `0xD098`:
`LD IX,D0CA;CALL D0E5;CP 03;JP Z,0`).

- Das **Spurregister liegt bei `[0xEBFA]`** (aus dem Laufwerks-Deskriptor `IX+2/+3`). Es wird von
  **ZVE1 UND ZVE2** an `E706` geschrieben und divergiert dadurch von der physischen Kopfposition.
- Der **/TO-Sensor** (Port `0x12` bit7 = ctrl-PIO Port B) ist **korrekt** (bit7=0 nur bei physCyl 0)
  und **nicht** die Ursache.
- Der **Boot steppt nachweislich nur mit ZVE1** (`busrq=0`) — deshalb bootet er trotz des Modells.
  ZVE2-Stepping tritt ausschließlich zur Laufzeit auf.

**Verworfener Teil-Fix:** In `K5122::doStep()` Schritte bei `bus_.isBUSRQ()` ignorieren (nur ZVE1
positioniert den Kopf). Ergebnis: Kopf bleibt korrekt, **Boot + DIR + alle 590 ctest + 58 Legacy
grün** — aber der Fix **legt Schicht 2 frei**, sodass `STAT` statt eines Fehlers **hängt** (255 367×
Read auf cyl0/head0). Ein Hänger ist schlechtere UX als der Fehler → revertiert.

## 5. Schicht 2 — CRC-Template wird von einem Interrupt korrumpiert

Mit korrekt positioniertem Kopf (cyl 0) matcht der Matcher (`E9E6–EA1A`) cyl/head/sec/size, scheitert
dann aber am **IDAM-CRC-Vergleich**:

```
EA0F  IN A,(16H)     ; X6 = IDAM-CRC-Hi aus dem Stream = 0xFA (korrekt)
EA11  EXX
EA12  CP D           ; Vergleich gegen Schatten-D'
EA15  JR NZ,E9E5     ; ← GENOMMEN → Neustart (endloser Retry)
```

Unser Stream liefert die **korrekte** Standard-IBM-CCITT-CRC `0xFA0C` (über `3×A1 FE 00 00 01 01`),
identisch mit dem Template `[EC05/06]=FA 0C`. Der Vergleich benutzt jedoch **`D'/E'`**, geladen im
Matcher-Setup (`E9C8`) via `LD SP,EC0D; POP DE; EXX` — d. h. **`D'/E' = [EC0D/0E]`**. Dieser Bereich
ist ein **Mini-Stack** und enthält beim Fehlschlag `E9EF`/`E975` (Code-Adressen) statt der CRC.

**Ursache:** Der Matcher läuft (in den Byte-Lücken) auf **ZVE1 mit aktiven Interrupts**. Ein
**CTC-/Timer-Interrupt** feuert mitten im Matcher und **pusht die Rücksprung-PC auf `SP=EC0D`** →
`[EC0D/0E]` wird mit der Matcher-PC überschrieben. Nachweis: `wp 0xEC0D` zeigt Writes, deren Datenwert
exakt die Low-Bytes der Matcher-PCs (`E9EA/E9ED/E9EF/E9F1/E9F3/E9F5…`) sind.

Auf echter HW liest **nur ZVE2 ununterbrechbar** (ZVE1 per `/BUSRQ` angehalten) → kein Interrupt
trifft den EC0D-Mini-Stack. `DIR` funktioniert, weil dessen Lese-Timing den kritischen
Interrupt-Zeitpunkt (zufällig) meidet.

## 6. Der Fix: gehaltener Bus **nur für den Laufzeit-Read** — VERSUCHT, offen

Beide Schichten verschwinden, sobald der Laufzeit-Read wie echte HW läuft: **`/BUSRQ` für den
gesamten Transfer halten, ZVE1 komplett anhalten, nur ZVE2 liest** — dann steppt/matcht genau eine
CPU und kein Interrupt trifft den Matcher.

⚠️ Der **globale** „gehaltene Bus"-Umbau ist in `project_per_byte_busrq_model` als **verifizierte
Sackgasse (3 Showstopper)** für den **Boot-Pfad** markiert. Der Boot braucht die Per-Byte-Drossel
(getuntes ZVE1↔ZVE2-Handshake, Zeitschleifen-Abwarten). Der Weg ist deshalb ein **Fork**:
Boot bleibt per-Byte, nur der Laufzeit-Read hält den Bus.

### 6.1 Warum der naive Fork scheitert (drei verworfene Versuche, 2026-07-10)

Der gehaltene Bus lässt sich **nicht** sauber einklinken, weil die **Read-Completion ZVE1 braucht**:
ZVE2 signalisiert das Ende über den Fortsetzungs-Vektor **`[EC0B]`**; **ZVE1** (in seiner Poll-
Schleife **`E8B5`**: `LD HL,(EC0B); SBC HL,BC; JR Z,E8B5`) sieht das, läuft weiter und beendet den
K5122-Transfer (`/STR=1`). Hält man ZVE1 den ganzen Transfer über an, findet die Completion nie
statt. Die konkreten Ergebnisse:

1. **Trigger = ZVE2-Auto-Start (hohe Koroutine ≥0xC000), Ende = ZVE2-Selbst-Reset.**
   → **Boot-Hänger.** ZVE2 macht **keinen** `OUT(04H)=0` (das einzige `OUT(04H)` im Read ist
   ZVE1s Arm @E88E) → das Ende wird nie erkannt.
2. **Trigger = Auto-Start, Ende = `[EC0B]`-Änderung.** → Boot erreicht das OS, aber **ein Read
   scheitert (`BAD SECTOR`)**: der Auto-Start hält ZVE1 **mitten im Setup** (@E896, vor `E8B1`
   wo ZVE1 `[EC0B]=E8B5` setzt) an; beim Resume überschreibt ZVE1 ZVE2s Signal wieder.
3. **Trigger = ZVE1 erreicht Poll-Schleife `E8B5`, Ende = `[EC0B]`-Änderung.** → **33 M
   Engage/Exit-Toggles, Boot-Hänger.** `E8B5` ist **keine** read-spezifische, sondern eine
   allgemein oft besuchte Warteschleife; das Ein-/Ausklinken oszilliert und blockiert den Fortschritt.

### 6.2 Was ein tragfähiger Fix braucht

- Ein **read-eindeutiges** Einklink-Signal (nicht die generische Poll-Schleife `E8B5`), das *nach*
  ZVE1s Setup (inkl. `[EC0B]=E8B5`) und *vor* dem Matcher/Seek-Retry greift.
- Ein **byte-genaues Transfer-Ende** (ZVE2 stoppt das Lesen / `[EC0B]`-Übergang), das ZVE1 **einmal**
  laufen lässt, um die Completion zu fahren — ohne dabei erneut Setup-Code zu durchlaufen.
- Alternative Denkrichtung: nicht ZVE1 „anhalten", sondern die **Interrupts während des Matchers
  sperren** (Layer 2) und **ZVE2-Schritte während `/BUSRQ` unterdrücken** (Layer 1, `doStep`) —
  wobei Letzteres (siehe §4) den Read zwar von `BAD SECTOR` auf einen Hänger verschiebt und daher
  allein nicht genügt. Beides zusammen, sauber konditioniert, ist der nächste zu prüfende Ansatz.

Alle drei Versuche wurden revertiert (kein Boot-Regressions-Risiko). Der Code steht auf dem grünen
Committed-Stand; diese Analyse ist die Arbeitsgrundlage.

## 7. Schlüssel-Adressen (SCPX 1526 BIOS im RAM)

| Bereich | Adresse |
|---|---|
| CONIN-Wartepunkt (Prompt) | `E079` / `E07C` |
| ZVE2-Lese-Koroutine (an `[0x0000]` gepoist) | `E9C8` |
| Matcher (cyl `E9FD` / head `EA02` / sec `EA07` / size `EA0C` / **IDAM-CRC `EA12`/`EA17` → Fail `EA15`**) | `E9E6–EA1A` |
| DATA-Feld-Lesung | `EA9A` |
| ZVE1-Poll-Wait auf `[EC0B]` | `E8B5` |
| Read-Setup (poist `[0001]` @`E858`, restauriert @`E8A0`) | `E88C–E8C0` |
| Seek (schreibt `[EBFA]` @`E706`; /TO-Reset `E701`) | `E6E2–E742` |
| BAD-SECTOR-Handler / Meldungstabelle | `D098` / `D0BA`,`D0CA` |
| BIOS-Read-Kette | `D853→D718→D605→D5D4→DE24→E40D` |
| Template-/Mini-Stack-Block | `EC00–EC0F` (`FE cyl head sec size CRChi CRClo …`; `EC0D`=SP) |
| Spurregister (per Laufwerk) | `EBFA` |
| ZVE2-Reset/Start | `OUT(04H)` |

## 8. Werkzeuge

- **RAM-Dump:** `k1520dbg savestate` (Datei: 7 B Magic + 1 B Version + 4 B regsize, dann **65536 B RAM
  ab Offset 12**). Eine C-API-Harness bootet **nicht** sauber (VRAM bleibt uniform) — den savestate
  nutzen.
- **Disassembler:** `python3 tools/z80_disasm2.py --org 0 --entry 0xADDR ram.bin` (Strings sind
  ASCII im RAM).
- **Boot-Check:** `boot_trace --until PC==0xE079`.
- **Watch/Trace:** `k1520dbg` `wp <A>` (Speicher-Write), `trace <file> lo hi` (beide CPUs pro
  Instruktion — der Matcher läuft auf ZVE1 *und* ZVE2, daher `g` statt `s` verwenden).

## 9. Umsetzungsversuch §6.2 (2026-07-10) — Schicht 1+2 gefixt, **Schicht 3** entdeckt

Der in §6.2 skizzierte surgische Weg (Kopf-Schritte unter `/BUSRQ` unterdrücken + Interrupts im
Matcher sperren) wurde implementiert und Schritt für Schritt gegen die Fixture `disks/scpx_boot.hfe`
verifiziert. Ergebnis: **beide dokumentierten Schichten sind lösbar**, aber darunter liegt eine
**dritte**, die den Read weiterhin scheitern lässt. Details (jeder Punkt reproduziert):

### 9.1 Schicht 1 (Kopf-Divergenz) — gefixt, boot-sicher
`K5122::doStep()` ignoriert Schritte, solange `bus_.isBUSRQ()` (nur ZVE1 positioniert den Kopf).
Bisect-verifiziert: SCPX bootet damit auf **exakt 13 165 225 Takte** = unveränderte Baseline; der
Kopf steht beim `STAT`-Read danach **korrekt auf cyl4/head1** (statt der divergierten cyl0/head0 aus
§4). `dev` bestätigt `cyl=4 head=1 READING`, Template `[EC01/02/03]=04 01 09` (cyl4,head1,sec9).
Der Kopf ist damit **nicht mehr** die Ursache.

### 9.2 Schicht 2 (EC0D-Korruption) — mit SP-Guard gefixt, verifiziert
Statt „INT während der ganzen Runde sperren" (bricht den Boot, weil der Index-ISR interruptgetrieben
ist und der SCPX-**Boot dieselbe hohe Koroutine E9C8** nutzt) wurde die INT-Zustellung an ZVE1 nur
gesperrt, **solange ZVE1s SP im Mini-Stack-Fenster `[EC00,EC10]` liegt** (= `LD SP,EC0D; POP DE`
im Matcher-Setup E9C8/E9D1). `wp 0xEC0C/0D/0E` bestätigt: **keine** Matcher-PC-Pushes mehr — die
einzigen Writes stammen aus legitimem BIOS-Setup (PC=E7F5/E7F7/E8B5). `BAD SECTOR` verschwindet.

### 9.3 Schicht 3 (NEU) — vorzeitiger Warmstart-Restore desynchronisiert ZVE2
Mit 1+2 gefixt lädt `STAT` **immer noch nicht**, sondern **hängt**. Ursache per Trace + Dump im
Hänger eindeutig:

- ZVE1 sitzt in seiner Poll-Schleife `E8B5/E8B8` und wartet auf `[EC0B]`.
- **ZVE2 loopt endlos** in `E9E6–E9FB` (der IDAM-Marken-Suche) mit **`DE=E8C1` statt `FEA1`** und
  **`SP=EC1D` statt `EC0F`**. Der Matcher sucht so die Bytes `E8`/`C1` statt der Adressmarke
  `FE`/`Sync A1` (Setup `E9DD: LD DE,FEA1H`) → findet nie eine Marke, `E9FB JR NZ E9E6` immer genommen.
- **Kernbefund** (`d 0x0000`): im Hänger steht **`[0x0000] = C3 03 DE` = `JP DE03`** — der
  **Warmstart-Vektor**, NICHT die Lese-Koroutine `JP E9C8`. ZVE1 hat also (§3.3) den Warmstart-Vektor
  **restauriert, obwohl ZVE2 den Read noch nicht beendet hat.** ZVE2 kam dadurch (Neustart ab PC=0
  bzw. durch den desynchronisierten Kontext) **nie wieder durch das Setup `E9C8`** (das `SP=EC0F`
  und `DE=FEA1` setzen würde); `DE=E8C1` = `[EC0B/0C]` (Reste aus dem verschobenen Mini-Stack).

Das ist **kein** Kopf- und **kein** CRC-Template-Problem mehr, sondern der klassische
**ZVE1↔ZVE2-Desync des Per-Byte-/BUSRQ-Modells**: ZVE1 läuft in den Byte-Lücken der eigenen
Read-Completion voraus und überschreibt `[0x0000]`, bevor ZVE2 fertig ist. Auf echter HW hält
`/BUSRQ` ZVE1 den ganzen Transfer über an — genau das verhindert den vorzeitigen Restore.

### 9.4 Konsequenz für den Fix
Schicht 3 bestätigt §6: der Laufzeit-Read braucht den **gehaltenen Bus** (ZVE1 komplett anhalten,
bis ZVE2 signalisiert). Der harte Blocker bleibt die **Gating-Frage**: der SCPX-**Boot** benutzt
**dieselbe** Koroutine `E9C8`, denselben Poll-Wait `E8B5` und denselben Warmstart-Restore wie der
Laufzeit-Read — ein Held-Bus, der auf die Koroutinen-Adresse (≥0xC000) gated, **bricht den Boot**
(empirisch: erster `g` läuft 400 M Takte ohne `E079` zu erreichen, hängt bei ~EB30). Es gibt
**keinen** rein zustandsbasierten Diskriminator „Boot-Read vs. Laufzeit-Read" — der Unterschied ist
nur **temporal** (vor/nach dem Prompt). Ein tragfähiger Fix braucht daher entweder
(a) einen Held-Bus, der den Boot-Handshake (Zeitschleifen-Abwarten, §6.1) übersteht, **oder**
(b) einen expliziten „OS läuft"-Zustand (z. B. gesetzt, sobald der interaktive Prompt `E079` das
erste Mal erreicht ist), auf den der Held-Bus gated wird. (b) ist der nächste konkret zu prüfende
Ansatz; er umgeht die in §6.1 verifizierten Boot-Sackgassen, weil der Held-Bus dann während des
gesamten Boots inaktiv bleibt.

### 9.5 Reproduktion der drei Befunde (k1520dbg, gegen Temp-Kopie!)
```
b 0xE079 ; g ; bd 0xE079 ; keys STAT\r        # bis zum Read-Hänger fahren
dev                                           # → cyl=4 head=1 READING (Schicht 1 ok)
wp 0xEC0C ; wp 0xEC0D ; wp 0xEC0E ; g 400000  # → nur Setup-Writes, keine INT-Pushes (Schicht 2 ok)
d 0x0000 8                                     # → C3 03 DE = JP DE03 (Schicht 3: Warmstart vorzeitig)
trace t.txt 0xE9E6 0xEA1A ; g 30000 ; trace off  # → ZVE2 loopt mit DE=E8C1, SP=EC1D
```
Die Code-Änderungen (doStep-Guard, SP-Guard) wurden nach der Diagnose **revertiert**, da der
resultierende Hänger schlechtere UX ist als der `BAD SECTOR`-Fehler (vgl. §4) — der Committed-Stand
bleibt grün. Diese Analyse ist die aktualisierte Arbeitsgrundlage; §9.4(b) ist der empfohlene
nächste Schritt.

## 10. LÖSUNG (2026-07-10) — os-gated „gehaltener Bus" (§9.4b umgesetzt)

Der in §9.4(b) empfohlene Weg löst alle drei Schichten **auf einen Schlag**, weil er ihre
gemeinsame Wurzel beseitigt: ZVE1 läuft während des Laufzeit-Reads **gar nicht mehr** in den
Byte-Lücken mit. Kein ZVE1-Kopf-Stepping (Schicht 1), kein CTC-Interrupt an ZVE1 im Matcher
(Schicht 2), kein verfrühter `[0x0000]`-Restore durch ZVE1 (Schicht 3).

### 10.1 Warum der Gate funktioniert, wo §6.1 scheiterte
§6.1 scheiterte, weil kein **zustandsbasierter** Diskriminator „Boot-Read vs. Laufzeit-Read"
existiert (beide nutzen Koroutine `E9C8`, Poll-Wait `E8B5`, Warmstart-Restore). Der Unterschied ist
rein **temporal**: Laufzeit-Reads passieren *nach* dem ersten Erreichen des Prompts. Genau darauf
gated der Fix — `os_running_` wird gesetzt, sobald ZVE1 `E079` erreicht. Der gehaltene Bus ist
damit während des **gesamten** Boots inaktiv (der Boot behält unverändert die getunte
Per-Byte-Drossel, `project_per_byte_busrq_model`), und die §6.1-Boot-Sackgassen entfallen.

### 10.2 Der Engage/Release-Automat (`core/machines/a5120/a5120.cpp`, `run()`)
Nur wenn `os_running_`:
- **Engage:** ZVE1 `PC==E8B5` **und** `[EC0B]==E8B5` **und** `K5122::isReadTransferActive()`. Das
  ist **read-eindeutig** (ZVE1 hat gerade in `E8B1` `[EC0B]=E8B5` geschrieben und parkt am Poll-Wait,
  ein Lese-Stream läuft) — im Gegensatz zur „generischen Schleife `E8B5`" aus §6.1(3). Ab hier wird
  `/BUSRQ` jede Iteration **gehalten** (`bus_.assertBUSRQ()`), sodass der bestehende BUSRQ-Block nur
  ZVE2 steppt und ZVE1 eingefroren bei `PC==E8B5` stehen bleibt (führt die Poll-Instruktion nicht aus).
- **Release:** `[EC0B]!=E8B5` (ZVE2 hat seinen Fortsetzungs-Vektor geschrieben = Read fertig). Dann
  `K5122::releaseHeldRead()` (stoppt die Per-Byte-Drossel + gibt `/BUSRQ` frei, **ohne** die
  ctrl-PIO-Interrupt-Freigabe anzufassen — anders als `endDmaTransfer()`, weil SCPX' ZVE2 sie nicht
  löscht) und ZVE1 fährt mit `JP (HL)` fort und verarbeitet die gelesenen Daten. Für den nächsten
  Sektor durchläuft ZVE1 das Setup erneut → sauberer Zyklus, keine Oszillation.
- Der bestehende **`[0x03F8]`-Completion-Watcher** (CP/A-Boot) ist während `held_read_active_`
  ausgeblendet — SCPX signalisiert über `[EC0B]`, nicht `[0x03F8]`.

### 10.3 Verifikation
```
b 0xE079 ; g ; bd 0xE079 ; keys STAT\r ; g 40000000 ; screen   # → "A: R/W, Space: 522k", zurück zu A>
b 0xE079 ; g ; bd 0xE079 ; keys PIP\r  ; g 40000000 ; screen   # → "*"-Prompt (PIP wartet auf Eingabe)
```
Beide vormals fehlschlagenden Programme laden jetzt. Regressionsfrei: ctest (inkl.
`boot_integration`, `k5122`, `k2526`, 5× `format_integration`) grün, Legacy-Harness 58/58.

### 10.4 Betroffene Dateien
- `core/machines/a5120/a5120.cpp` — `os_running_`-Erkennung + Engage/Release-Automat in `run()`;
  `[0x03F8]`-Watcher gegen den gehaltenen Read geguardet. Konstanten `kScpx*` (E079/E8B5/EC0B).
- `core/machines/a5120/a5120.h` — Member `os_running_`, `held_read_active_`.
- `core/cards/k5122/k5122.{h,cpp}` — `isReadTransferActive()` (Engage-Bedingung) und
  `releaseHeldRead()` (interrupt-neutrales Transfer-Ende).

## 11. SCPX-Laufzeit-SCHREIBEN (`ERA`/`PIP`) — Analyse & Fix (2026-07-11)

> **Kurz:** `ERA`/Löschen auf B: ist GELÖST (kein `BAD SECTOR` mehr, Datei wird gelöscht);
> `PIP`-Kopie schreibt die Dateidaten korrekt nach B: (als Temp-Datei `STAT.$$$`), hängt aber
> in der finalen Rename-Stufe (`$$$`→`COM`) an einer 4., andersartigen Schicht (§11.4).

### 11.1 Symptom
Jeder Laufzeit-Schreibzugriff auf B: (`ERA B:STAT.COM`, `PIP B:=A:STAT.COM`) endete mit
`SCPX ERR ON B: BAD SECTOR`. Die K5122-Schreibmechanik selbst war korrekt (Log `>>> WRITE D1
C=2 S=1..4` + Verify-`READ … 0 CRC-Fehler`), SCPX verwarf das Ergebnis aber wegen eines falschen
Completion-Status.

### 11.2 Wurzel (drei Schichten, alle im Post-Write-Verify)
Der Fix aus §10 hält den Bus nur für den *Read*. Der SCPX-Datenfeld-Write (`beginWriteField`/
`commitWriteField`, greift bei `transferring_ && !write_mode_ && BUSRQ` auf der `/WE`-Flanke)
läuft INNERHALB eines gehaltenen Transfers; direkt danach dispatcht ZVE1 über eine lange Kette
(`E929→…→E88C`) zum Nachfolge-*Verify-Read*, bevor er dessen `/STR` ausgibt.
1. Der `/STR=1`-Abtast-Timeout (`kStrEndSampleCycles=320`) ist zu kurz für diese ~2000-Takt-
   Dispatch-Lücke — er beendete `transferring_` mittendrin.
2. **Eigentlicher Übeltäter:** `K5122::releaseHeldRead()` setzte `transferring_` beim Schreib-Ende
   (`[EC0B]:E8B5→E929`) UNBEDINGT auf `false`. Damit fehlte der Verify-Runde das
   `isReadTransferActive()`-Signal → der os-Gate engagierte nie → ZVE2 blieb im Reset (kein
   Byte-Throttle-`/BUSRQ`, das `zve2StartFromReset` triggert).
3. ~1 Mio Takte später kaperte der Index-ISR (`EB74-EB81`, schreibt unbedingt `[EC0B]=E998`) den
   Handshake-Vektor → degradierter Blindscan (`E9A9`, self-mod `E9FD`→`JR E9B8`, ungeprüfter
   IDAM-Accept) → Fehlercode `[EC0A]=0x10` → `BAD SECTOR`.

### 11.3 Fix
- **Post-Write-Gnadenfenster** (`K5122::post_write_grace_`, 6000 Takte): `commitWriteField()` öffnet
  es; `releaseHeldRead()` lässt `transferring_` darin am Leben (`if (post_write_grace_<=0)
  transferring_=false;`), sodass der os-Gate den Verify sofort engagiert und ZVE2 aus dem Reset
  neu startet. `dma_pending_`/`byte_ready_` werden weiter IMMER zurückgesetzt (sonst Write-Runaway
  auf Port 0x14). Der `/STR=1`-Timeout nutzt im Fenster `kPostWriteStrEndCycles=4000`.
  `a5120.cpp` schließt das Fenster beim Verify-Engage (`afs_.endPostWriteGrace()`), damit es nicht
  in Folge-Reads leckt. (Nur nach einem Write aktiv → Boot/Read-Pfad unberührt.)
- **ZVE1-Interrupt-Guard** (`a5120.cpp`): keine INT-Zustellung an ZVE1, solange `SP∈[0xEC00,0xEC10]`
  (SCPX-Mini-Stack). Dort liest der IDAM-Matcher per `LD SP,EC0D; POP DE` seinen CRC-Sollwert; ein
  ISR-PUSH würde `[EC0D/0E]` überschreiben und den CRC-Vergleich dauerhaft brechen. Der Interrupt
  bleibt anstehend (kein Verlust). Neuer `K2526::cpuSP()`. Verifiziert wirksam (0 Korruption über
  400 M Takte).

Guard-Test `ScpxIntegration.EraDeletesFileOnDriveBWithoutBadSector`: bootet SCPX, `ERA B:STAT.COM`,
prüft KEIN `BAD SECTOR` + gefiltertes `DIR B:STAT.COM` → `NO FILE`.

### 11.4 NOCH OFFEN — PIP-Rename-Finalisierung (4. Schicht)
`PIP B:=A:STAT.COM` schreibt die Daten vollständig nach B: (verifiziert: B:-HFE ändert sich, Boot
von B: zeigt `STAT $$$`), hängt aber beim finalen Rename `$$$`→`COM`. Dieser Pfad ist **kategorisch
verschieden** von §11.2: PIP.COMs eigener `E671`-SEEK/Read läuft als **ZVE1-programmierte E/A**
(`bt`: `E671←E65E←E526←…`, `busrq=0%`), durchläuft NIE das reguläre `E8B5`-DMA-Setup, und der
Matcher-CRC-Sollwert `[EC0D/EC0E]` ist dabei **statisch falsch** (`E295`, ändert sich nicht mit dem
Ziel-C/H/S) → `EA12 CP D'` scheitert ewig → 12× Neu-Read-Spin, kein Prompt. K5122-CRC ist korrekt
(berechnet `EA32` = geliefert). `[EC0B]` bleibt konstant `E8C1` (nie `E8B5`). Offen ist, ob PIP.COMs
(nicht quelloffener) Pfad ein CRC-Priming vergisst = NICHT core-fixbar ohne Guest-RAM-Eingriff, oder
ob `E671` den `E8B5`-DMA-Pfad erreichen SOLLTE (fehlender Branch). Nächster Diagnoseschritt:
`wp 0xEC0B..0xEC0C` über `E671→E9E6`. KEIN Blind-Patch (würde in Guest-RAM schreiben).

### 11.5 Betroffene Dateien
- `core/cards/k5122/k5122.{h,cpp}` — `post_write_grace_`/`endPostWriteGrace()`, `releaseHeldRead()`-
  Guard, `commitWriteField()` öffnet das Fenster, `/STR`-Timeout-Schwelle.
- `core/machines/a5120/a5120.cpp` — `endPostWriteGrace()` beim Engage, ZVE1-INT-Guard.
- `core/cards/k2526/k2526.h` — `cpuSP()`.
- `tests/cpp/test_boot_integration.cpp` — Guard-Test `EraDeletesFileOnDriveBWithoutBadSector`.

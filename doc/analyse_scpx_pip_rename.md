# SCPX `PIP`-Kopie — Rename-Finalisierung hängt (Handoff / Weitermachen)

> ## ✅ GELÖST 2026-07-12 (verifiziert)
> `PIP B:=A:STAT.COM` (Gleichname) UND `REN B:STAT.COM=B:STAT2.COM` (gelöschter Eintrag) laufen jetzt
> vollständig durch: DIR B: zeigt `STAT COM`, der `A>`-Prompt kehrt zurück (k1520dbg, frischer Boot +
> keys, beide Trigger). Der Fix war KEINE gezielte Änderung, sondern ein **Seiteneffekt des
> rotationsgekoppelten, encoding-abhängigen K5122-Byte-Takts (Commit `1d547d0` + laufende, bei der
> Verifikation noch uncommittete `k5122.cpp/.h`-Verfeinerungen `consumeByteSlot()`/`currentBytePeriod()`,
> ursprünglich für INIT-Format).** Das
> bestätigt die Wurzelanalyse unten (§4d/§4e): der Hänger lag in der K5122-Byte-Taktung/`resyncToNextMark`
> für den ungetakteten ZVE1-`E671`-Read — unter dem alten flachen `kBytePeriodCycles=150` pinnte
> `head_pos_`; der neue rotationsgekoppelte Takt löst den Pin. **Offen nur noch:** ein Regressions-Guard-
> Test in `ScpxIntegration` (s. doc/open_points.md §4). Der Rest dieses Dokuments ist die (weiterhin
> korrekte) Wurzelanalyse — historisch für das Verständnis, warum der Timing-Fix wirkt.

> **Zweck dieses Dokuments:** In sich geschlossener Wiedereinstieg für eine NEUE Session.
> Alle Kernbefunde stehen inline (die Trace-Artefakte im `/tmp`-Scratchpad überleben nicht).
> Voraussetzung ist der Commit **`b1184c3`** (`feat(scpx): Laufzeit-Schreiben repariert`) auf
> Branch `scpx_boot`. Vertiefung: `doc/analyse_scpx_com_load.md` §10 (Read-Fix) und §11 (Write-Fix).

> ## ⚑ 2026-07-11 ROOT CAUSE GEFUNDEN — die §4/§4b-Prämisse (stale `[EC0D]`-CRC) war ein IRRWEG
>
> Neue Session (Opus, live disasm+trace) hat die Wurzel gefunden. **`[EC0D]=0xE295` ist NICHT stale/Müll**,
> sondern der **konstante DATA-Feld-CRC-Seed** `= CRC16(A1 A1 A1 FB) = 0xE295` (offline bewiesen). `[EC0D]`
> ist ein GETEILTER Slot: Setter **`E7DF`** schreibt die **ID-Feld-CRC** hinein (für den ID-Matcher), Setter
> **`EB03`** überschreibt sie mit dem konstanten Daten-Seed `0xE295`. Ein `wpr`/`wp`-Trace zeigt: der
> ID-Matcher (auf **ZVE2**) liest korrekt wechselnde ID-CRCs (`E0/24/49/8D/64/32/A0/FD/39` = neun
> verschiedene Sektoren) — der CRC-Handshake **funktioniert**. „`[EC0D]=E295` beim Matcher" war eine
> Momentaufnahme des Daten-Seeds, kein Fehler.
>
> **Die echte Wurzel (a5120.cpp:314-326):** Das os-gate „gehaltener Bus" (com_load_bug-Fix) engagiert NUR,
> wenn `ZVE1.PC==0xE8B5` (Poll-Wait) UND `[EC0B]==0xE8B5`. Der Rename/Finalisierungs-Read läuft aber über
> **`E671`** mit Fortsetzungsvektor **`[EC0B]=0xE8C1`** und parkt NIE an `E8B5` → **das Gate engagiert nie
> → ZVE1 fährt den IDAM-Matcher SELBST** (bus-master=ZVE1, ZVE2 idle), ungetaktet. Der K5122-Lesestrom ist
> aber auf den **getakteten ZVE2-Pfad** ausgelegt (Per-Byte-BUSRQ-Drossel); von ZVE1 direkt gelesen findet
> der Matcher denselben Sektor NIE (Beweis: `hist` = 100% in der A1-Sync-Schleife `E9F3-E9F6` auf
> `A: cyl0/head0/sec14, MFM` — ein GÜLTIGER Sektor, den **ZVE2 im selben Lauf problemlos liest**, CRC `0x32`).
> Die frühen Reads der Operation laufen über den DMA-Pfad (Gate engagiert, ZVE2 liest → 9 Sektoren OK);
> erst der finale `E671`-Read fällt durchs Gate → Hänger. Call-Chain: `PIP.COM(TPA 05BD/1B53/0B12)` →
> `BDOS CALL 0005` → `BIOS DAC0/D3B8` → `E671` → Matcher auf ZVE1.
>
> **Fix-Versuch 2026-07-11 — GESCHEITERT, wichtiges Negativergebnis (s. §4e):** Ich habe die ZVE2-
> Übernahme unterdrückt, während ZVE1 die Koroutine selbst fährt (Latch `zve1_owns_read_` in
> `a5120.cpp`/`.h`, Engage bei „Read aktiv + ZVE1.PC∈E9C8..EAB0 nach Boot", Release bei
> `!isReadTransferActive`). Ergebnis: **hilft NICHT.** ZVE2 wird zwar stillgelegt, aber **ZVE1 allein findet
> den Sektor trotzdem nicht** — `head_pos_` bleibt bei **~66** hängen, weil `K5122::resyncToNextMark()`
> (ausgelöst durch die `OUT(10) B5→85`-MK1-Flanke bei JEDEM Matcher-Restart) den Kopf immer wieder auf
> DIESELBE frühe Marke zurücksetzt. Der Dual-CPU-Stomp war also NICHT (allein) die Wurzel. Änderung wieder
> **revertiert** (unwirksam + im lastragenden Arbitrierungs-Loop). **Neue Fix-Richtung:** im **K5122-Lese-
> strom/Resync** für UNGETAKTETE ZVE1-`IN(16)`-Reads — warum pinnt `resyncToNextMark` bei schnellem ZVE1
> (aber nicht beim getakteten ZVE2, wo derselbe Code jeden Restart resynct)? Nächster Diagnoseschritt: die
> IN/OUT von `head_pos_` in `resyncToNextMark` über den Spin loggen (`nextMark(fromPos)` liefert offenbar
> wiederholt die frühe Marke — prüfen, ob `fromPos` beim Restart VOR der zuletzt getesteten Marke steht).
> ⚠️ Strikter Held-Bus bleibt [[project_per_byte_busrq_model]]-Sackgassen-Verdacht. Belege: §4d/§4e.

## 1. Stand in einem Satz

SCPX-Laufzeit-**Schreiben** ist repariert: `ERA B:STAT.COM` löscht sauber (kein `BAD SECTOR`), und
`PIP B:=A:STAT.COM` **schreibt die Dateidaten korrekt** nach B: (als PIP-Temp `STAT.$$$`, verifiziert:
B:-HFE-md5 ändert sich, Boot von B: zeigt `STAT $$$`). **Offen:** PIP **hängt** beim finalen atomaren
Rename `STAT.$$$` → `STAT.COM` und kehrt nie zum `A>`-Prompt zurück.

> **★ PRÄZISE EINGRENZUNG (2026-07-11, per Ausschluss-Tests) — ZWEI spezifische Directory-Kollisionen:**
> Der Hänger ist NICHT „jedes Schreiben von STAT.COM" und NICHT „jeder gelöschte Eintrag", sondern eng:
>
> | Kommando (B: = Kopie von scpx_boot.hfe) | Ergebnis |
> |---|---|
> | `PIP B:=A:STAT.COM` bzw. `PIP B:STAT.COM=A:STAT.COM` (Quelle- **UND** Zielname = STAT.COM) | **HÄNGT** |
> | `PIP B:STAT.COM=A:MODF.COM` (Ziel STAT.COM, andere Quelle) | läuft |
> | `PIP B:MODF.COM=A:STAT.COM` (andere Ziel-Datei überschreiben) | läuft |
> | `PIP B:STAT2.COM=A:STAT.COM` (anderer Zielname) | läuft |
> | `PIP B:STAT.COM=B:STAT2.COM` (STAT2→STAT.COM auf B:, auch mit gelöschtem STAT.COM-Eintrag) | läuft |
> | `PIP B:STAT2` → `ERA B:STAT2` → `PIP B:STAT2` (recreate nach Löschen) | läuft |
> | `REN B:STAT3.COM=B:STAT2.COM` (Rename auf frischen Namen) | läuft |
> | `REN B:STAT.COM=B:STAT2.COM` (Rename auf STAT.COM mit gelöschtem STAT.COM-Eintrag; auch 250 M Takte) | **HÄNGT** |
>
> ⇒ **Trigger A:** Cross-Drive-Kopie mit **identischem Quell- UND Zielnamen** (`A:STAT.COM → B:STAT.COM`).
> Weicht *irgendein* Name ab, läuft es. **Trigger B:** `REN` auf einen Zielnamen, für den ein **gelöschter**
> Directory-Eintrag existiert. Bemerkenswert: **PIP** schreibt denselben Ziel-Eintrag `STAT.COM` (mit
> gelöschtem Vorgänger) PROBLEMLOS — nur **REN** hängt dort ⇒ PIP/REN nehmen verschiedene Directory-
> Schreibpfade; beide Trigger führen aber in denselben `E671`/stale-`[EC0D]`-Hänger (§4).
>
> **Nutzbarer Weg zu `STAT.COM` auf B:** (umgeht beide Trigger):
> `ERA B:STAT.COM` → `PIP B:STAT2.COM=A:STAT.COM` → `PIP B:STAT.COM=B:STAT2.COM` (→ STAT.COM zurück;
> `B>STAT` verifiziert lauffähig) → optional `ERA B:STAT2.COM`.
>
> **Für die RE bedeutet das:** Der auslösende Code-Pfad hängt vom **Vergleich Quell-/Ziel-Dateiname**
> (Trigger A) bzw. vom Antreffen eines **gelöschten Eintrags mit dem Zielnamen im REN-Pfad** (Trigger B)
> ab — nicht von der Datei-Allokation. Das ist ein starker Filter für die Suche nach der fehlenden
> `[EC0D]`-Priming-Stelle (§6): den `E671`-Aufruf im Directory-**Match**-Pfad (Quell=Ziel-Vergleich /
> gelöschter-Eintrag-Skip) untersuchen, nicht im Daten-Kopier-Pfad.

## 2. Was schon gefixt & committet ist (b1184c3) — NICHT erneut anfassen

Der Write-Fix aus §11 (drei Schichten + INT-Guard) ist regressionsfrei (voller ctest + 58/58 Legacy,
Guard-Test `ScpxIntegration.EraDeletesFileOnDriveBWithoutBadSector`). Betroffen: `k5122.{h,cpp}`
(`post_write_grace_`, `releaseHeldRead()`-Guard, `endPostWriteGrace()`), `a5120.cpp` (Engage-Klärung +
ZVE1-INT-Guard `SP∈[0xEC00,0xEC10]`), `k2526.h` (`cpuSP()`). **Der INT-Guard ist verifiziert wirksam**
(0 `[EC0D]`-Korruption über 400 M Takte) — er löst das PIP-Rename-Problem aber NICHT (§4).

## 3. Reproduktion (exakt)

```sh
# 1) Screen sehen (hängt sichtbar):
SP=$(mktemp -d)
cp disks/scpx_boot.hfe $SP/A.hfe; cp disks/scpx_boot.hfe $SP/B.hfe
cat > $SP/pip.fds <<'EOF'
boot 48
type ERA B:STAT.COM
enter
boot 15
type PIP B:=A:STAT.COM
enter
boot 300
type DIR B:
enter
boot 25
dump final
EOF
tools/dev.sh build trace
build_trace/format_driver $SP/A.hfe $SP/B.hfe $SP/pip.fds 2>/dev/null | grep -A17 "final"
# → "A>PIP B:=A:STAT.COM" bleibt stehen, kein A>, DIR wird nie ausgeführt.

# 2) K5122-Ops sehen (info): die 12× `>>> READ D0 C=0 H=0` am Ende = der Spin.
FD_LOGLEVEL=info build_trace/format_driver $SP/A.hfe $SP/B.hfe $SP/pip.fds 2>&1 >/dev/null | grep ">>>" | tail -20

# 3) Interaktiv sezieren (k1520dbg kann Tastatur via `keys`, frischer Kaltstart —
#    NICHT loadstate, das serialisiert os_running_/held_read_active_ nicht):
cat > $SP/dbg <<'EOF'
b 0xE079
g
bd 0xE079
keys ERA B:STAT.COM\r
keys PIP B:=A:STAT.COM\r
wp 0xEC0B
wp 0xEC0C
g
EOF
build_trace/k1520dbg -b $SP/B.hfe -x $SP/dbg $SP/A.hfe   # Cross-Drive: A: = $A, B: = $B
```

## 4. Was der Spin IST (per Trace belegt) — die 4. Schicht

Nach dem korrekten Daten-Schreiben ruft PIP.COMs eigener Code die IDAM-Matcher-Routine **DIREKT auf
ZVE1** auf (normaler `CALL`, kein DMA), um den A:-Directory-Sektor für den Rename zu lesen:

- **`bt` am Matcher (`E9E6`):** `E671←E65E←E526←D3B2←D9F5←C8EF←CAAD←CE81←CEFE` — alles PIP.COM-eigener
  Low-Memory-Code. `where`: `bus-master=ZVE1`, `BUSRQ=no`. `format_driver` PC-Histogramm: `busrq 0%`,
  Top-PCs `E9F3/E9FB/E9EA` (ZVE1-Matcher-Schleife). **Also KEIN ZVE2/DMA-Pfad.**
- **`E671`** (SEEK-Trigger: `LD(EBFEH),DE; IX=Deskriptor; [EC0A]=6; [EC09]=Retry; [EC08]=0; → E685→SEEK
  E6E2`) durchläuft NIE das reguläre `E840-E8B4`/`E8B5`-DMA-Setup, das `[EC0B]` frisch auf `0xE8B5`
  armieren würde. Deshalb kann der os-Gate (a5120.cpp, engagiert nur bei `PC==E8B5`) hier **prinzipiell
  nie** greifen — und `transferring_`-Halten (§11-Fix) hilft nicht.
- **Der Matcher (Disasm org D000, verifiziert):**
  ```
  E9D1 LD SP,EC0DH ; E9D4 POP DE ; E9D5 EXX          ; DE' := [EC0D/EC0E]  = CRC-Sollwert
  E9D6 LD HL,(EC03) ; E9D9 LD BC,(EC01) ; E9DD LD DE,FEA1  ; E=A1 D=FE C=cyl B=head L=sec H=size
  E9E6.. Schleife: IN(16) + CP E(A1) CP D(FE) CP C(cyl) CP B(head) CP L(sec) CP H(size)
  EA11 EXX ; EA12 CP D'(CRC-Hi) ; EA15 JR NZ,E9E5 ; EA17 CP E'(CRC-Lo)
  ```
- **Der Fehlpunkt:** `[EC0D/EC0E]` (der CRC-Sollwert `D'/E'`) ist **STATISCH FALSCH** (`E295`) — er
  ändert sich NICHT mit dem Ziel-Sektor, obwohl ein IDAM-CRC von C/H/S/N abhängt. Belegt über zwei
  verschiedene `E671`-Aufrufe derselben hängenden Sitzung:
  ```
  Aufruf 1: Ziel cyl=3 head=0 sec=0D(13) size=1   [EC0D/EC0E]=95 E2
  Aufruf 2: Ziel cyl=3 head=1 sec=01    size=1   [EC0D/EC0E]=95 E2  (UNVERÄNDERT)
  ```
  **K5122s eigene CRC ist korrekt** (rechnerisch: IBM-CRC-16/CCITT für die früher beobachtete Suche
  cyl0/head0/sec14/size1 = `0xEA32`; K5122-Stream lieferte CRC-Hi `0xEA` — passt). Der Vergleich
  scheitert also, weil der **Soll-Wert** `E295` mit keiner realen Sektor-CRC übereinstimmt → `EA12 CP D'`
  schlägt immer fehl → `JR NZ,E9E5` → ewige Suche → 12× `startReadTransfer`-Neu-Read (`ZVE2 in Reset`
  + ~1-Index-Perioden-Lücken), kein Prompt.
- `[EC0B/EC0C]` bleibt über die `E671`-Aufrufe konstant `0xE8C1` (generischer „Read-OK"-Fortsetzungs-
  code), **nie `0xE8B5`**.
- **Der INT-Guard (b1184c3) verhindert LIVE-Korruption** von `[EC0D]` (verifiziert: 0 `bint`/`wp`-Treffer
  über 400 M Takte), aber `E295` war schon vor jeder Interrupt-Zustellung da (deterministisch bei
  `cyc=69824216`) → der Sollwert ist nie korrekt befüllt worden, nicht nachträglich zerstört.

## 4b. NEU (2026-07-11, offline bewiesen): Der Deskriptor ist IN SICH inkonsistent

Rein rechnerisch (kein Boot nötig, `$CLAUDE_JOB_DIR/tmp/crc.py`+`crcscan.cpp`, Standard-IBM-CCITT
poly 0x1021 init 0xFFFF über `A1 A1 A1 FE C H S N` — dasselbe Modell, das die Disk und die
funktionierenden Boot/DIR-Reads nutzen) gilt für den hängenden Rename-Read:

- Deskriptor-C/H/S (`[EC01..04]`) = **cyl3 head0 sec13 size1**; dessen **korrekte** IDAM-CRC ist **`0x24BD`**.
- Der Erwartungswert `[EC0D/EC0E]` = **`0xE295`** ist aber:
  - **keine** gültige IDAM-CRC IRGENDEINES Sektors (Brute-Force cyl0–79 × head0–1 × sec0–40 × size0–4,
    Standardmodell → nur `0x95E2` bei cyl20/head1/sec24/size2, NICHT `0xE295`);
  - **keine** DATA-Feld-CRC irgendeines Sektors der Disk (alle 2560 Sektoren gescannt, 0 Treffer);
  - **kein** CRC-Akkumulator-Seed nach A1/A1A1/A1A1A1/…FE-Präambel.
- ⇒ **Der Deskriptor ist intern inkonsistent: C/H/S sind für das Rename-Ziel FRISCH gesetzt
  (SEEK-Pfad `E6E2`→`E6E8 LD (EC01),HL`), der Erwartungswert `[EC0D]` ist es NICHT** — `0xE295` ist
  reiner Alt-/Müllwert. Der Matcher (`E9D4 POP DE`=`[EC0D]`, `EA12 CP D'`) kann nie treffen, egal wie
  gut der Lesestrom ist → 12× Neu-Read-Spin. **Der Lesepfad (DMA vs. ZVE1-programmiert) ist damit
  NICHT die Wurzel** — die Wurzel ist, dass der Schritt, der `[EC0D]` als `CRC(A1A1A1FE+CHRN)`
  **berechnet und schreibt**, auf dem Rename-Pfad **nicht läuft** (oder mit falschem C/H/S lief).
  Das deckt sich mit der Nutzer-Vermutung „System glaubt noch, die Datei existiere" = **stale
  In-RAM-Deskriptor** statt Geräte-/Timing-Bug.

## 4c. NEU: WAS `E671` liest = STAT.COMs eigene Dateidaten (nicht das Directory)

Offline aus `scpx_boot.hfe` (Extraktor `tools/scpx_extract`, Sektor-Dump):
- **Directory** liegt auf **cyl2/head0/sec1–2** (Dump cyl2/h0/s1 beginnt mit `00 'INIT    COM'`).
  Systemspuren = cyl0–1. STAT.COM-Directory-Eintrag: `C2 H0 S1 off96`, Alloc-Blöcke **0B 0C 0D**.
- Block→physisch (1 KB-Blöcke = 4×256B, 16 Sek/Spur, 2 Köpfe, Datenbereich ab cyl2/h0/s1):
  Block 0x0B → **cyl3/head0/sec13–16**, Block 0x0C → **cyl3/head1/sec1–4**.
- Die zwei hängenden `E671`-Reads zielen genau auf **cyl3/head0/sec13** und **cyl3/head1/sec1** →
  das sind **STAT.COMs Dateidaten-Blöcke 0x0B/0x0C**. Sektor-Dump bestätigt Programmcode:
  cyl3/h0/s13 = `32 C0 1E 11 00 00 0E 19 CD 05 00…` (`LD (1EC0),A; LD DE,0; LD C,19; CALL 0005`),
  cyl3/h1/s1 = `0E 17 CD 05 00 C9` (`LD C,17; CALL 0005; RET`) — beides BDOS-aufrufender Transient-Code.

⇒ **Der Rename-Hänger ist ein ZWEITER Lese-Durchgang über bereits kopierte Quell-Dateidaten**
(Verify / Gleichnamen-Sicherheits-Reread), NICHT ein Directory-Zugriff. Der ERSTE Durchgang
(Kopie A:→B:STAT.$$$) läuft über den funktionierenden DMA-Pfad (`E8B5`) und liest dieselben Blöcke
FEHLERFREI — inklusive korrektem `[EC0D]`. Der zweite Durchgang läuft über `E671` (ZVE1-programmierte
E/A) und findet `[EC0D]`=Müll (`0xE295`) statt des korrekten `0x24BD` (Block 0x0B) / `0x56E0` (0x0C) vor.
**Kernfrage damit:** Setzt der DMA-Pfad `[EC0D]` selbst (und `E671` erbt/überschreibt es nicht mehr
korrekt), ODER klobbert der zwischenzeitliche B:-SCHREIBpfad das gemeinsame `[EC0D]`-Scratch, sodass
der Folge-Reread hängt? (= Nutzer-Vermutung „stale state nach Schreib-/Programm-Ende".)

**(Diese §4c-Frage ist durch §4d beantwortet/überholt — der Setter läuft, `[EC0D]` ist korrekt; der
Hänger liegt an der ZVE1-vs-ZVE2-Taktung des Reads, nicht an `[EC0D]`.)**

## 4d. ROOT CAUSE (2026-07-11, live disasm+trace) — ausführlich

**(1) `[EC0D]` ist ein geteilter Slot; `0xE295` ist eine KONSTANTE.**
Disasm der zwei Setter (RAM, via `k1520dbg u`):
```
; ID-Feld-CRC-Setter (schreibt Soll-ID-CRC für den Matcher)
E7E3 LD A,FEH ; E7E5 LD (EC00),A          ; [EC00]=FE (IDAM-Marke)
E7E8 LD HL,EC0D ; E7ED LD IY,EC00 ; E7EB LD E,05
E7F1 CALL EB2A                            ; CRC16 über [EC00..EC04]=FE,cyl,head,sec,size, Seed CDB4
E7F4 LD (HL),C ; E7F5 INC HL ; E7F6 LD (HL),B   ; [EC0D/EC0E] = ID-Feld-CRC
; DATA-Feld-CRC-Seed-Setter (überschreibt [EC0D] für die Datenfeld-Prüfung)
EB03 LD A,FBH ; EB05 LD (EC00),A          ; [EC00]=FB (DATA-Marke)
EB08 LD IY,EC00 ; EB0C LD E,01
EB0E CALL EB2A                            ; CRC16 über [EC00]=FB, Seed CDB4
EB11 LD (EC0D),BC                         ; [EC0D/EC0E] = DATA-CRC-Seed = 0xE295 (KONSTANT)
; EB2A: LD BC,CDB4 (=CRC16(A1A1A1)); EB2D: nibble-CRC-16-Schritt über (IY+0)
```
Offline verifiziert: `CRC16(A1A1A1) = 0xCDB4` (= der Seed in `EB2A`), `CRC16(A1A1A1 FB) = 0xE295`,
`ID-CRC(cyl2/h0/s1) = 0x1764` (lo `0x64` — genau der Wert, den `E7F5` im funktionierenden `DIR B:`
schrieb). ⇒ **`0xE295` ist der Daten-Seed, nicht Müll.**

**(2) Der ID-Matcher funktioniert — er liest wechselnde, korrekte ID-CRCs.**
`wp`/`wpr 0xEC0D` über den PIP-Lauf: jeder `E7F5`-Write (ID-CRC lo) wird sofort von **ZVE2** `@E9D5`
(POP DE des Matchers) mit DEMSELBEN Wert gelesen — Werte `E0,24,49,8D,64,32,A0,FD,39` = neun
verschiedene Zielsektoren. ZVE2 findet also Sektoren und schreitet fort. `0x24BD`→lo`24`? nein: die
CRC-Bytereihenfolge ist `C`=lo an `[EC0D]`; die beobachteten lo-Bytes decken reale Sektor-ID-CRCs ab
(z.B. `0x32` = `CRC(cyl0/h0/s14)=0xEA32`). Der CRC-Handshake ist gesund.

**(3) Der Hänger: ZVE1 fährt den Matcher selbst, ungetaktet.**
`where`/`bt`/`dev`/`hist` im Spin (30 M cyc nach PIP):
```
where: ZVE1 E9EF IN A,(16H) ; ZVE2 E9F1 [run] ; bus-master=ZVE1 ; K5122 D0 cyl0 head0 READING
bt:   E079 ← E65E(E671) ← E50D ← D3B8 ← DAC0 ← 08DF(→0005 BDOS) ← 0B12 ← 1B53 ← 05BD   (TPA=PIP.COM)
dev:  K5122 D0 cyl0 head0 READING secSize=256 /BUSRQ-pend=yes
x/16 EC00: FB 00 00 0E 01 85 01 10 | 00 00 10 C1 E8 95 E2 8E
          -> Deskriptor cyl0 head0 sec14 size1(256B), density [EC05]=85=MFM, [EC0B/0C]=E8C1, [EC0D/0E]=E295
hist 4M cyc: ZVE1 100% in E9E6..EA08 (A1-Sync-Skip E9F3/E9F5/E9F6 je 15.8%), ZVE2 keine Samples
```
Der Matcher hängt in der A1-Sync-Skip-Schleife auf `A: cyl0/head0/sec14` (MFM, gültiger Sektor, den
ZVE2 im selben Lauf las → CRC `0x32`). Über ~75 Umdrehungen (4 M cyc) findet ZVE1 den Sektor NIE.

**(4) Warum ZVE1 hängt, ZVE2 nicht — das os-Gate greift für Pfad 2 nicht.**
`a5120.cpp:314-326`: Engage nur bei `ZVE1.PC==E8B5 && [EC0B]==E8B5 && isReadTransferActive()`.
Der `E671`-Read hat `[EC0B]=E8C1` und parkt nie an `E8B5` → `held_read_active_` bleibt false → ZVE1
läuft in den Byte-Lücken (bzw. hier: fährt die an `[0000]/[0001]` gepoiste Matcher-Koroutine ganz
selbst). Der K5122-Strom ist auf den getakteten ZVE2-Pfad ausgelegt (Per-Byte-BUSRQ-Drossel,
`k5122.cpp:65,92-98`; `head_pos_` läuft, aber ohne die ZVE2-Taktung + `update()`-Kadenz zwischen den
Bytes trifft der Matcher den IDAM nie). Die frühen Reads der Operation laufen über den Poll-Wait-Pfad
(`[EC0B]=E8B5`, Gate engagiert, ZVE2 liest) → 9 Sektoren OK; nur der finale `E671`-Read fällt durch.

**Konsequenz:** Nicht `[EC0D]` reparieren (ist korrekt). Nächster Ansatz s. §4e.

## 4e. Fix-Versuch (ZVE2 unterdrücken) GESCHEITERT — Wurzel liegt tiefer (K5122-Resync)

Getestet (dann revertiert): Latch `zve1_owns_read_` (a5120.cpp) — sobald ZVE1 nach dem Boot die Lese-
Koroutine `E9C8..EAB0` selbst betritt UND `isReadTransferActive()`, wird die ZVE2-Übernahme für die
GANZE Read-Dauer unterdrückt (kein `zve2Step`, kein Start-aus-Reset), Release bei `!isReadTransferActive`.
Zwei Varianten (per-Instr-Check + gelatcht) — **beide beheben den Hänger NICHT.**

**Beobachtung mit stillgelegter ZVE2:** ZVE1 hängt WEITER im Matcher, `dev` zeigt `K5122 D0 cyl0 head0
READING headPos=66/5328` über 120 M cyc. `headPos=66` bleibt fast konstant → **der Kopf wird immer
wieder auf dieselbe frühe Marke zurückgesetzt.** Ursache: der Matcher schreibt bei JEDEM Restart
`OUT(10) 0xB5` (MK1 bit4=1) nach `0x85` (bit4=0) → **MK1-steigende Flanke → `resyncToNextMark()`**
(`k5122.cpp:641-648`), das `head_pos_ = romReadResyncTarget(track,head_pos_) = nextMark(head_pos_)-4`
setzt. Bei UNGETAKTETER ZVE1 (liest `IN(16)` back-to-back, kein BUSRQ-Stall) restartet der Matcher so
schnell, dass `resyncToNextMark` den Kopf nie über die frühe Marke hinauskommen lässt (Pin bei ~66).
Beim GETAKTETEN ZVE2-Pfad läuft derselbe Code, aber die Per-Byte-BUSRQ-Drossel lässt `head_pos_`
zwischen den (selteneren) Resyncs weit genug vorlaufen → er findet den Sektor.

⇒ **Die Wurzel ist die `resyncToNextMark`/Stream-Interaktion für ungetaktete ZVE1-Reads, NICHT die
CPU-Arbitrierung.** Zu klären: liefert `nextMark(fromPos)` beim Restart wiederholt die frühe Marke,
weil `fromPos` (=`head_pos_`) beim `OUT(10) 0xB5` noch VOR der zuletzt getesteten Marke steht? Ein
`wp`/Logpoint auf `head_pos_` (bzw. LOG in `resyncToNextMark`) über ~2 Restarts zeigt es. Mögliche
Fixes (zu prüfen, KEIN Blind-Patch): (a) `resyncToNextMark` beim MK1-Puls NUR vorwärts über die aktuelle
Marke hinaus resyncen; (b) ZVE1-`IN(16)`-Reads ebenfalls per-Byte takten (aber ZVE1 ist Bus-Master —
kein BUSRQ-Stall möglich → bräuchte ein anderes Warte-Signal); (c) den `E671`-Read doch auf den
getakteten ZVE2-Pfad umbiegen (Held-Bus — Sackgassen-Verdacht [[project_per_byte_busrq_model]]).

## 5. Die zwei offenen Hypothesen (bewusst KEIN Blind-Patch)

1. **`E671` soll DMA umgehen (SCPX-Design):** PIP nutzt für „einzelnen Sektor lesen" ZVE1-programmierte
   E/A statt DMA; `[EC0D/EC0E]` ist dann nie als per-Aufruf-CRC-Ziel vorgesehen, und der Bug steckt in
   PIP.COMs eigenem (uns NICHT als kommentierte Quelle vorliegendem) Aufrufpfad, der ein CRC-Priming
   vergisst → **außerhalb dessen, was der K1520-Core ohne Guest-RAM-Eingriff reparieren kann.** Dann
   muss geklärt werden, WELCHE Emulator-Zustands-Differenz PIPs Code diesen Priming-Zweig überspringen
   lässt (PIP läuft auf echter HW; die frühen A:-Reads von PIP.COM/STAT.COM-Quelle funktionieren ja).
2. **`E671` SOLLTE den `E8B5`-DMA-Pfad erreichen** und tut es aus einem Grund nicht (fehlender Branch);
   der aktuelle ZVE1-Direktlauf ist selbst schon das Symptom.

> ⚠️ Ein „`[EC0B]`/`[EC0D]` vom Emulator künstlich setzen" wäre ein Eingriff in Guest-RAM statt in einen
> Geräte-/Timing-Bug — NICHT tun, bevor Hypothese 1 vs. 2 entschieden ist.

## 6. Konkreter nächster Schritt (disambiguiert 1 vs. 2)

`wp 0xEC0B` **und** `wp 0xEC0C` über den `E671`→`E9E6`-Übergang laufen lassen (Script §3.3): Wird
`[EC0B]` in diesem Fenster jemals auf `0xE8B5` armiert?
- **Nie `E8B5`** → `E671` nimmt strukturell den Nicht-DMA-Pfad → Hypothese 1. Dann: den Code zwischen
  `E742` (Ende SEEK/Schrittpuls) und dem tatsächlichen Sprung disassemblieren und suchen, WO PIPs Code
  den CRC-Sollwert `[EC0D/EC0E]` hätte befüllen sollen — bzw. welche Bedingung ihn überspringt.
- **Doch mal `E8B5`** → Hypothese 2; den fehlenden Branch (`E742`→`E840`) finden.

Ergänzend nützlich: mit `bt`/`where` prüfen, ob die FRÜHEREN, FUNKTIONIERENDEN A:-Reads von PIP
(PIP.COM-Laden, STAT.COM-Quelle: `READ D0 C=3/C=4/C=5`) über DENSELBEN `E671`-Pfad oder über den
DMA-Pfad liefen — der Unterschied zwischen „geht" und „hängt" ist der Schlüssel.

## 7. Schlüssel-Adressen / RAM (SCPX 1526 BIOS + PIP.COM, im RAM)

| Bedeutung | Adresse / Wert |
|---|---|
| ZVE1-Prompt (os_running_) | `E079` |
| DMA-Poll-Wait (Gate-Engage) | `E8B5`, `[EC0B]==E8B5` |
| Fortsetzungsvektor (Read-OK) | `[EC0B]=E8C1` |
| Fehler-/Retry-Vektor | `E975` (`LD E,05`) → BAD-SECTOR-Handler `D098` |
| Index-ISR (kapert `[EC0B]=E998`) | `EB74-EB81` |
| IDAM-Matcher (Schleife) | `E9E6`-`EA1A`; Setup `E9D1`; Cyl-Cmp `E9FD` |
| **PIP ZVE1-Direkt-SEEK/Read** | **`E671`** (Stack `E671←E65E←E526←D3B2←D9F5←…`) |
| Matcher-CRC-Sollwert (falsch `E295`) | `[EC0D/EC0E]` |
| Ziel-IDAM (C/H/S/N) | `[EC01]=cyl [EC02]=head [EC03]=sec [EC04]=size` |
| SCPX-Mini-Stack-Fenster (INT-Guard) | `[EC00..EC10]` |

## 8. Werkzeuge / Merksätze

- Tastatur nur über `format_driver` (Script) oder `k1520dbg keys <text>\r` — `boot_trace` kann NICHT tippen.
- `savestate`/`loadstate` reproduziert den Gate NICHT (serialisiert `os_running_`/`held_read_active_`
  nicht) → immer frischer Kaltstart + `keys`.
- Boot bis `A>` ≈ 48 Mcyc; PIP-Kopie + Spin liegt weit darüber (langsam, mehrere Minuten unter dbg).
- Disks: immer Temp-Kopien von `disks/scpx_boot.hfe` für A: UND B: (format_driver/k1520dbg mounten beide
  beschreibbar; k1520dbg nutzt per Default COW).
- Kommentierte Quellen: Boot-ROM `doc/EPROMS/zre.prn`, SCPX-Read-Pfad `doc/EPROMS/scpx_readpath.prn`
  (partiell, read-fokussiert). PIP.COMs eigener Code (`E671`-Pfad, D000-E9FF) liegt NICHT kommentiert vor.

## 9. Delegation

Diese Analyse ist Aufgabe für den `boot-disasm-analyst` (sonnet). Er hat das Problem bereits über 4
Runden bis hierher seziert; ein Wiedereinstieg sollte ihm dieses Dokument + die Repro (§3) + die
konkrete Frage (§6) mitgeben.

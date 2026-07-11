# SCPX `PIP`-Kopie — Rename-Finalisierung hängt (Handoff / Weitermachen)

> **Zweck dieses Dokuments:** In sich geschlossener Wiedereinstieg für eine NEUE Session.
> Alle Kernbefunde stehen inline (die Trace-Artefakte im `/tmp`-Scratchpad überleben nicht).
> Voraussetzung ist der Commit **`b1184c3`** (`feat(scpx): Laufzeit-Schreiben repariert`) auf
> Branch `scpx_boot`. Vertiefung: `doc/analyse_scpx_com_load.md` §10 (Read-Fix) und §11 (Write-Fix).

## 1. Stand in einem Satz

SCPX-Laufzeit-**Schreiben** ist repariert: `ERA B:STAT.COM` löscht sauber (kein `BAD SECTOR`), und
`PIP B:=A:STAT.COM` **schreibt die Dateidaten korrekt** nach B: (als PIP-Temp `STAT.$$$`, verifiziert:
B:-HFE-md5 ändert sich, Boot von B: zeigt `STAT $$$`). **Offen:** PIP **hängt** beim finalen atomaren
Rename `STAT.$$$` → `STAT.COM` und kehrt nie zum `A>`-Prompt zurück.

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

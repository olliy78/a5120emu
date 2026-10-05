# K8915 „Generation 2“ — EPROM-Abzüge des Geräts des Anwenders

Stand 2026-10-05 (Entwurf 24, `doc/design/24_k8915_varianten.md`). Alle Dateien sind rohe
Abzüge ohne Kopf (je 1 KB = 2708/U555), ausgelesen vom Anwender; Originale in
`~/projects/eprommer/k8915gen2_*`. Prüfsummen: `MD5SUMS`.
Reihenfolge der Dateien auf der ZRE: **aus dem Inhalt erschlossen** (siehe unten), nicht am
Gerät abgelesen **[?]**. Reihenfolge auf der 2708-Karte: Namensgebung des Anwenders nach
Entwurf 24 §5.3 (Adresse relativ zur Kartenstartadresse), **ebenfalls nicht abgesichert [?]**.

## ZRE (3 × U555, Beschriftung 175/176/177)

| Datei | Platz (erschlossen) | Beschriftung | MD5 | Inhalt |
|---|---|---|---|---|
| `k8915g2_zre_0000_175.bin` | 0000–03FF | 175 | `5c410151…` | Einstieg `F3 ED 5E` (DI, IM 2), `OUT (A8H)`, kopiert sich per LDIR nach **FC00** und läuft dort; Selbsttest-Meldungen „MROM RAM SIO KEY CTC DIAGNOSTIC ENTER: LADER / "#": ZYKL." |
| `k8915g2_zre_0400_176.bin` | 0400–07FF | 176 | `3a1d25b6…` | Sprungtabelle `C3 1A 04 C3 2B 04`, Floppy (Ports 10H–19H, v. a. 16H), Meldungen „Coldstart * Disk on A: ready“, „Drive A: not ready“, „Disk-error“, „No system disk“ |
| `k8915g2_zre_0800_177.bin` | 0800–0BFF | 177 | `3c778fad…` | Lader-Meldungen „Loading complete, replace disk by previous disk“ …; Sprungziele 03xx–09xx; Speicherzugriffe auf F7xx |
| `k8915g2_zre_0000-0BFF.bin` | 0000–0BFF | — | `e965073e…` | Verkettung der drei (abgeleitet, 3 KB) |

Die Reihenfolge 175 → 176 → 177 folgt aus dem Inhalt (Einstieg, Sprungtabelle, Schlussteil).
**Nicht** gleich dem V3-Boot-ROM (`doc/EPROMS/K8915/k8915_boot_2732.bin`): 1007 von 1024 Bytes
unterscheiden sich, auch nicht gleich den PRG-710-Ladern. Verwandt im Aufbau (A8H, 61H, 10H–19H,
Meldungen), aber ein eigener Stand.

## K7024 (Zeichengenerator, 2 × 2708)

| Datei | MD5 | Befund |
|---|---|---|
| `k8915g2_k7024_171.bin` | `24b817ed…` | **byteidentisch** mit `doc/EPROMS/K7024/v171.bin` |
| `k8915g2_k7024_172.bin` | `90e84aea…` | **byteidentisch** mit `doc/EPROMS/K7024/v172.bin` |

Das ist der Zeichensatz des A5120 (zwei 1-KB-Bausteine, `v171`/`v172`). Neu ist nichts; die
Dateien liegen hier nur der Vollständigkeit halber.

## 2708-Karte PFS K3820 (Platine `012-7040`, Handbuch: `012-7041`) — 16 × 1 KB

Dateien `k8915g2_pfs3820_<Adresse>.bin` (Adresse = Anfang des Chips **relativ zur
Kartenstartadresse**, Wickelbrücken X8/X9 — **noch nicht abgelesen**). Zusätzlich
`k8915g2_pfs3820_0000-3FFF.bin` (16 KB, abgeleitet).

**Nur zwei der 16 Plätze sind programmiert, 14 sind leer (alle Bytes FFH,
MD5 `9a8918b1…`).** Programmiert:

| Datei | MD5 | Inhalt |
|---|---|---|
| `k8915g2_pfs3820_3C00.bin` | `9c3ba057…` | Einstieg `F3 ED 5E 3E 0E D3 A8 AF D3 E2 …`; Sprungziele FC–FF (Code läuft bei **FC00**); Meldungen „MROM RAM **I/O** KEY CTC DIAGNOSTIC ENTER: LADER / OFF: ZYKL."; Ports E0H–E4H (ATS), B3H/B1H, FBH–FEH. Verwandt mit ZRE 175, aber **eigene Fassung** (Selbsttest „I/O“ statt „SIO“, andere Ports) |
| `k8915g2_pfs3820_3000.bin` | `9d2ef67f…` | Dieselben Lader-Meldungen wie ZRE 177 („Loading complete, replace …“), Code gleicher Bauart (`21 1C 0C 22 84 0C`; ZRE 177: `21 1C F7 22 84 F7`) — aber die Arbeitszellen liegen bei **0Cxx** statt F7xx |

`k8915g2_EPROMKarte_1_1.bin` (Anwenderdatei) ist byteidentisch mit `…_3C00` und wurde **nicht**
übernommen (Doppelabzug).

**Deutung (Vermutung [?], AP-V3):** Die Karte enthält eine **andere Fassung des Urladers** als
die ZRE: dieselbe Programmfamilie, anderer Stand (Selbsttest „I/O“, Arbeitszellen 0Cxx, also
passend zum 1-KB-RAM der K2521 bei 0C00–0FFF). Da der Code bei FC00 läuft und die Karte laut
Handbuch auf 4-KB-Grenzen startet, liegt die Karte vermutlich bei **C000** (der Chip „3C00“
dann bei FC00–FFFF). Ob das die Lage am Gerät ist, zeigen nur die Brücken X8/X9 und die
Beschriftung der Chips.

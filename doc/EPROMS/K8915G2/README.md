# K8915 „Generation 2“ — EPROM-Abzüge des Geräts des Anwenders

Stand 2026-10-05 (Entwurf 24, `doc/design/24_k8915_varianten.md`). Alle Dateien sind rohe
Abzüge ohne Kopf (je 1 KB = 2708/U555), ausgelesen vom Anwender; Originale in
`~/projects/eprommer/k8915gen2_*`. Prüfsummen: `MD5SUMS`.
Reihenfolge der Dateien auf der ZRE: **aus dem Inhalt bestätigt** (AP-V3a, Belege in
`k8915g2_zre.prn`, Kopf „BEFUND 1“), die Plätze selbst nicht am Gerät abgelesen **[?]**. Reihenfolge auf der 2708-Karte: Namensgebung des Anwenders nach
Entwurf 24 §5.3 (Adresse relativ zur Kartenstartadresse), **ebenfalls nicht abgesichert [?]**.

## ZRE (3 × U555, Beschriftung 175/176/177)

| Datei | Platz (erschlossen) | Beschriftung | MD5 | Inhalt |
|---|---|---|---|---|
| `k8915g2_zre_0000_175.bin` | 0000–03FF | 175 | `5c410151…` | Einstieg `F3 ED 5E` (DI, IM 2), `OUT (A8H)`, kopiert sich per LDIR nach **FC00** und läuft dort; Selbsttest-Meldungen „MROM RAM SIO KEY CTC DIAGNOSTIC ENTER: LADER / "#": ZYKL." |
| `k8915g2_zre_0400_176.bin` | 0400–07FF | 176 | `3a1d25b6…` | Sprungtabelle `C3 1A 04 C3 2B 04`, Floppy (Ports 10H–19H, v. a. 16H), Meldungen „Coldstart * Disk on A: ready“, „Drive A: not ready“, „Disk-error“, „No system disk“ |
| `k8915g2_zre_0800_177.bin` | 0800–0BFF | 177 | `3c778fad…` | Lader-Meldungen „Loading complete, replace disk by previous disk“ …; Textroutine `0907H`; Sprungziele 03F3 (→ 175) sowie 04xx/05xx (→ 176) und 08xx–09xx (eigene Lage); Speicherzugriffe auf F7xx; **24-Bit-Summe falsch** (Byte 0A33H = 04H, s. u.) |
| `k8915g2_zre_0000-0BFF.bin` | 0000–0BFF | — | `e965073e…` | Verkettung der drei (abgeleitet, 3 KB) |

Die Reihenfolge 175 → 176 → 177 = 0000/0400/0800 ist durch den Code **bestätigt** (AP-V3a
2026-10-05; keine Korrektur nötig): 175 springt mit `JP 0400H` auf die Sprungtabelle von 176;
176 ruft sechsmal `CALL 0907H` (Textroutine in 177) und kopiert 23 Byte ab `098FH` (177) nach
`FFE0H`; 177 springt mit `JP 03F3H` zurück in den Vektor von 175; die absoluten Sprungziele von
176 liegen in 04xx–07xx (+ 08xx/09xx), die von 177 in 08xx–09xx. **Nur 175 wird nach FC00H
kopiert**; 176 und 177 laufen unverschoben aus dem ROM (Entwurf 24 sagte irrtümlich „177 läuft
bei 04xx“). Der Selbsttest „MROM“ summiert die drei Bausteine bei 0000/0400/0800.

**Prüfsummen:** jeder Baustein trägt in den letzten drei Bytes die 24-Bit-Summe seiner ersten
3FDH Byte (höherwertiges zuerst). Stimmt für 175, 176 und beide Karten-Chips. **177 stimmt
nicht:** berechnet `00A680H`, gespeichert `00A67CH` — Byte `0A33H` (Lade 0233H) ist `04H`
statt `00H`, in sonst ungenutztem Füllbereich (ein Bit). Entweder Lesefehler oder Bitfehler im
2708 [?]; am Gerät **177 ein zweites Mal lesen** (und den „MROM“-Selbsttest des Geräts beobachten).

Kommentierte Listings: `k8915g2_zre.prn` (3 KB, Lade- **und** Lauf-Adresse, Anhang mit der
Gegenüberstellung ZRE ↔ Karte) und `k8915g2_pfs3820.prn`, erzeugt von
`tools/gen_k8915g2_prn.py`, Wächter `cli_k8915g2_prn_passt_zur_quelle`.
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

**Befund AP-V3a (Listing `k8915g2_pfs3820.prn`):** Chip „3C00“ ist eine Fassung des 175
(A8H = 0EH statt 06H, Ports E0H–E4H und B1H/B3H statt 61H, prüft `F3 ED 5E` bei D001H–D003H, eigener
Kopierer, Selbsttest „I/O“ statt „SIO“ ohne SIO-Init). Chip „3000“ ist eine Fassung des 177 mit
Zellen bei 0Cxx, Port E4H statt 61H und kürzerer Textroutine (kein SIO-Init, **Taste über
`IN E1H`/`IN E0H`** statt SIO 52H/53H). Beide Chips tragen die richtige 24-Bit-Summe. Der
Floppy-Lader (176) hat auf der Karte kein Gegenstück. Die Ports E0H–E4H und das fehlende
SIO-Init passen zu einer anderen Ein-/Ausgabekarte als der ZRE-Fassung (Gen 1 = K7634 + K7028 [?]).

**Deutung (Vermutung [?], AP-V3):** Die Karte enthält eine **andere Fassung des Urladers** als
die ZRE: dieselbe Programmfamilie, anderer Stand (Selbsttest „I/O“, Arbeitszellen 0Cxx, also
passend zum 1-KB-RAM der K2521 bei 0C00–0FFF). Da der Code bei FC00 läuft und die Karte laut
Handbuch auf 4-KB-Grenzen startet, liegt die Karte vermutlich bei **C000** (der Chip „3C00“
dann bei FC00–FFFF). Ob das die Lage am Gerät ist, zeigen nur die Brücken X8/X9 und die
Beschriftung der Chips.

# SCPX 1526 — Zugriff auf 5×1024-Disketten & der Freeze-Fix

**Stand:** 2026-07-23. Branch `scpx_boot`. Ausgelöst durch: „INIT formatiert eine Diskette als
5×1024, meldet Erfolg, aber beim Zugriff friert der Rechner ein."

Dieses Dokument hält fest, **(1)** dass die 5×1024-Diskette gültig ist, **(2)** warum der Zugriff
einfror, **(3)** den Fix (der Zugriff terminiert jetzt statt einzufrieren) und **(4)** warum ein aus
einer **16×256**-Systemdiskette gebootetes SCPX eine **5×1024**-Diskette grundsätzlich **nicht lesen
kann** (SCPX ist Single-Format).

---

## 1. Die 5×1024-Diskette ist gültig

`INIT.COM` (SCPX-Gegenstück zu FORMAT.COM) mit **Format-Option 3 „DD-DS 5×1024"** erzeugt eine
valide MFM-Mischgeometrie (verifiziert mit `HfeImage` + `TrackCodec::parseTrack`):

| Bereich          | Geometrie   | Rolle                     |
|------------------|-------------|---------------------------|
| Zyl 0 (beide Köpfe) | 16×256 B | System-/Directory-Spur    |
| Zyl 1–79 (beide Köpfe) | 5×1024 B | Datenbereich           |

- Alle ID- und Daten-CRCs gültig, 0 Fehler; INIT meldet `BAD TRACKS: - NO -`.
- INIT schreibt **track 0 bewusst als 16×256** (getestet: auch aus einer 26×128-Blank wird track 0
  zu 16×256), den Rest als 5×1024. Die Spuren sind durchgängig **MFM** (5×1024 in FM wäre pro
  Umdrehung viel zu lang; das 5,25″-K5601 wird real immer im MFM-Betrieb gefahren). INIT
  schreibt+verifiziert alle Spuren mit Pfadbyte `0x81/0x85` (MK=0, MFM-A1).

**Fazit:** Die Diskette ist nicht defekt — sie ist genau das, was SCPX' eigenes INIT erzeugt.

## 2. Warum der Zugriff einfror

Der SCPX-Laufzeit-Read nutzt ZVE2 als DMA-Leser: ZVE1 poist eine ZVE2-Lese-Koroutine (Matcher
**`E9C8`**), löst `/STR`→`/BUSRQ` aus und **wartet an der Poll-Schleife `0xE8B5`** auf `[EC0B]`; ZVE2
signalisiert das Read-Ende über `[EC0B] != 0xE8B5`. Der Emulator hält für diesen Laufzeit-Read
`/BUSRQ` über den ganzen Transfer (os-gated „gehaltener Bus", s. `analyse_scpx_com_load.md` §10).

Findet ZVE2 sein Sektor-IDAM **nicht**, re-armt der Matcher `E9C8` **endlos** (er hat keinen eigenen
Timeout) und signalisiert nie `[EC0B] != 0xE8B5`. Auf echter Hardware terminiert hier der
**Disketten-Index-Interrupt** (Record-not-found → `[EC0B]=E998`, danach der reguläre Retry/Abbruch).
Im gehaltenen Bus wird aber (a) ZVE1 an `0xE8B5` eingefroren und (b) im Run-Loop die INT-Zustellung
übersprungen → der Index-Interrupt kann **nie** feuern → **Endlos-Hänger** (Rechner tot).

Der Boot ist davon nicht betroffen: dort läuft die **Per-Byte-BUSRQ-Drossel**, ZVE1 läuft in den
Byte-Lücken mit und nimmt seinen Index-Interrupt.

## 3. Fix: der No-Progress-Watchdog

`core/machines/a5120/a5120.cpp` — im gehaltenen Laufzeit-Read zählt `held_read_cycles_`; nach
`kHeldReadWatchdogCycles = 1'500'000` (~3 Umdrehungen; Index-Periode ≈490000 Takte) **ohne**
`[EC0B]`-Fortschritt wird der Bus freigegeben (`held_read_watchdog_` sperrt das Re-Engage, bis
`[EC0B]` wechselt). ZVE1 läuft dann wieder, nimmt seinen Index-Interrupt, und der Zugriff
**terminiert sauber** — genau die geforderte HW-Semantik: *die FM/MFM-Erkennung darf nicht
einfrieren.* Ein erfolgreicher Sektor-Read (IDAM-Suche ≤ 1 Umdrehung + Daten-Streaming) liegt weit
unter der Schwelle → **unberührt**, keine Regression (597/597 ctest + 58/58 Legacy grün).

Statt eines Hängers erscheint jetzt `SCPX ERR ON B: BAD SECTOR`, und das System bleibt bedienbar.
Guard: `ScpxIntegration.WrongFormatReadTerminatesInsteadOfFreezing`
(`tests/integration/test_boot_integration.cpp`).

> **Nachtrag 2026-08-05 — nicht verwechseln.** Es gab zwei verschiedene
> `BAD SECTOR`-Meldungen im 5×1024-Umfeld:
>
> 1. **Ohne `MODF`** — 5×1024-Diskette am 16×256-System. Das ist die in §4 vermessene
>    SCPX-Single-Format-Eigenschaft und gilt **unverändert**; der obige Guard prüft sie
>    weiterhin scharf.
> 2. **Nach `MODF` Option 3** (B: korrekt auf 5×1024 umgestellt) erschien beim *ersten*
>    Zugriff einmalig ebenfalls `BAD SECTOR`. Diese Meldung ist seit `76a959a`
>    **verschwunden** — per git-bisect und gezielter Mutation dem dortigen Fix 3
>    zugeordnet: `/STR=1` **durch ZVE2** beendet dessen Busbesitz jetzt sofort
>    (K5122-Doku §5.5) statt erst ~2 Byteperioden später über die `/STR=1`-Abtastung.
>    In diesem Fenster lief ZVE2 eine Instruktion zu weit (bei UDOS zerschrieb das
>    nachweislich den CRC-Puffer). Die einmalige Meldung war ein Artefakt genau dieses
>    Fensters, kein SCPX-Verhalten. `ScpxInit.Builds5x1024SystemViaInitModfSyspAndBoots`
>    akzeptiert deshalb beides und prüft stattdessen, dass B: **danach lesbar** ist.

## 4. Warum 5×1024 vom 16×256-System aus trotzdem `BAD SECTOR` gibt

**SCPX hat keinen Format-Autodetect wie CP/A — ein SCPX-System funktioniert für genau EIN Format.**
Das aus einer 16×256-Systemdiskette gebootete SCPX ist ein 256-Byte-System. Belege aus dem
ZVE2-Matcher `E9C8` (Disassembly des laufenden BIOS):

- Der Datenfeld-Read ist fest auf **256 Byte** verdrahtet (`LD E,00H` → `INIR` liest 256 B),
  unabhängig vom DPB-Größencode.
- Mess-Instrumentierung des Matchers über einen `DIR`-Read (ZVE2-PC-Zähler):

  | Disk auf B: | arm (Re-Arm) | FE gefunden | ID-Vergleich ok | Datenfeld erreicht |
  |-------------|-------------:|------------:|----------------:|-------------------:|
  | 16×256 (geht) | 33824 | 20 | 20 | 20 |
  | 5×1024 (BAD SECTOR) | 13276 | **1** | **0** | **0** |

  Bei 16×256 synchronisiert ZVE2 sauber auf die IDAMs und liest; bei 5×1024 synchronisiert die
  256-B-orientierte Lese-/Sync-Logik **nie** auf die 1024-B-Spur (in ~7 Umdrehungen nur *ein*
  zufälliges FE, das auch nicht passt).

`MODF.COM` Option 3 stellt zwar den *angeforderten* Größencode auf 3 (=1024) um (der Matcher
verlangt `sizecode=3`), aber die eigentliche Lese-/Sync-Routine bleibt die des 256-B-Systems → die
halb umgeschaltete Kombination synchronisiert nicht → `BAD SECTOR`. **Das ist erwartetes
SCPX-Single-Format-Verhalten**, kein Emulator-Bug.

**Um 5×1024 wirklich zu nutzen**, bräuchte man ein SCPX-System, das *für* 5×1024 gebaut/konfiguriert
ist (passende `BIOS*/CCP*`-`.SYS` bzw. eine per SYSP/CPABCGEN erzeugte 5×1024-Systemdiskette) — nicht
`MODF` auf dem laufenden 16×256-System.

### Schlüsseladressen / Handshake

- ZVE1-Poll-Wait `0xE8B5` (`LD HL,(EC0B); SBC HL,BC; JR Z`); Fortsetzung `JP (HL)`.
- ZVE1-Read-Arm ab `0xE860`: Pfadbyte aus `[EC05]`; `OUT(04),0` restartet ZVE2 aus Reset.
- ZVE2-Restart-Trampolin `[0x0000]=JP DE03` → `JP DFF5` (Coldstart `LD SP,0080H` → `JP C800`=CCP).
- ZVE2-Read-Matcher `0xE9C8`: `LD SP,EC0D`, `POP DE` (ID-CRC-Soll), wanted-ID aus `[EC01/EC03]`,
  `DE=FEA1` (FE/A1), armt `[EC05]`, `IN(16)`, skip A1, erwartet FE, vergleicht cyl/head/sec/sizecode
  + ID-CRC; Datenfeld ab `0xEA9A` (`INIR`, fest 256 B). Timeout/Abbruch: Index-ISR → `[EC0B]=E998`
  → Blindscan `0xE975` → `BAD SECTOR`.
- Handshake-RAM: `[EC01/02]`=cyl/head, `[EC03/04]`=sec/sizecode, `[EC05]`=Pfadbyte, `[EC0B]`=ZVE2→
  ZVE1-Fortsetzungsvektor, `[EC0D/0E]`=erwartete ID-CRC.

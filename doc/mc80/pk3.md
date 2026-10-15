# MC80.33 — AKM + Kassettenbandspeicher PK 3 (Analyse AP M7)

Stand 2026-10-15.  Ergebnis: **das Modell .33 bleibt gesperrt** — vom Magnetband-ROM liegt nur die
erste Hälfte vor, der physische Treiber ist damit nicht lauffähig.

## Quellen

| Kürzel | Datei (`doc/original_docs/mc80/`) | Inhalt |
|---|---|---|
| [PK3] | `pk-3-doku.pdf` (23 S., Scan) | WZUI „MERAMAT“, Kassettenbandspeicher PK-3, technische Daten (Bl. 3) |
| [PK3-IF] | `pk-3-doku2.pdf` (41 S., Scan) | Anlage 1 „Interface: Kassettenbandspeicher PK-3 — Steuereinheit, logische Struktur und technische Kenndaten“ |
| — | `pk-3-5214-doku.pdf` (28 S.), `pk-3-sp1.pdf` (11 S.) | CM 5214 / Schaltplan — nicht ausgewertet |
| ROM | `doc/EPROMS/MC80/mc80.33-1000.bin` (2 KB) | Magnetbandsoftware 1000H–17FFH |

## Laufwerk (aus [PK3] Bl. 3, vgl. `hardware_3x.md` §9)

Kassette nach **ISO 3407**, **32 Bit/mm PE**, 2 Spuren (Spurbreite 1,45 mm), Blockzwischenraum
nominal **20,3 mm**, Übertragung max. **8 000 Bit/s**, Lesen in beiden Richtungen; Kopfspalt
Schreiben–Lesen **3,81 mm**; Nenngeschwindigkeit **0,254 m/s**, Start-/Stoppzeit ≤ 30 ms,
Schnelllauf 1 m/s, Umspulen des ganzen Bandes ≤ 90 s.  Das Kernprofil `PK3` (254 mm/s, 1 000 mm/s,
8 000 Bit/s, Spalt 3,81 mm) passt dazu; Start 30 ms ist dort eingetragen, Bremsweg/Anlauf sind
Annahmen [?].  Die Interface-Signale von [PK3-IF] (PO, RP, RW, RS, PRZ, PSZ, IZ/KZ, GOT, ZD,
STRAB, BET, IO, ZB, TACH) und die AKM-Schaltung sind **nicht** ausgewertet — ohne Treiber gibt es
nichts, woran ein Modell zu prüfen wäre.

## ROM

- 1000H–17C3H gleich dem MAME-Satz (`zve_3.rom`): logischer Treiber KLOG, KINIT/READ/WRITE (59 Byte
  Unterschied, alle ab 17C4H).
- Ab **17E0H** beginnt ein **anderer physischer Treiber** (davor FFH).  Er übernimmt das Gerüst des
  K5020-Treibers (IX+0 Status besetzt/Fehler, IX+3 Laufwerk), ruft aber gleich im ersten Befehl
  `CALL 1A12H` und springt nach `1883H` — beides in der **fehlenden** Hälfte 1800H–1FFFH, ebenso die
  Sprungleiste 1FF7H/1FFDH, über die KLOG den Treiber erreicht.
- Im vorhandenen Teil steht kein `IN n`, nur `OUT (85H)` (Segmentregister); die Portbasis kommt wie
  beim K5020-Treiber aus IX+2 [Annahme nach dem gleichen Gerüst] — die AKM-Ports und ihre
  Bitbelegung stecken also ganz in der fehlenden Hälfte.

## Folge

`Mc80Machine` lehnt `Variante::Mc8033` weiter ab, `mc80emu` führt das Modell ausgegraut.  Nötig wären
der zweite EPROM `mc80.33-1800` (bzw. ein vollständiger 4-KB-Abzug 1000H–1FFFH) und für ein
Kartenmodell die AKM-Unterlage (Z.-Nr. 50300.3110, `hardware_3x.md` §9).  Wunschliste: `egos.md` §5 Nr. 8.

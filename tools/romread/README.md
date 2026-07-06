# romread — A5120 Boot-EPROM Dumper

`romread.com` ist ein CP/A-/CP/M-Programm (Z80, .COM), das das **Boot-EPROM der
ZRE/K2526** eines A5120 ausliest und seinen Inhalt (1 KB, `0000H–03FFH`) als Datei
**`ROM.BIN`** auf das **aktuelle Laufwerk** schreibt.

Gedacht zum Auslesen des echten EPROMs aus einem realen A5120.

## Wie es funktioniert

Das Boot-EPROM liegt nach dem Einschalten bei `0000H–03FFH` und wird vom
Betriebssystem über das BS-PIO-Bit **B0 (`/LD-ROM`)** ausgeblendet, sodass dort
normales RAM erscheint. Zum Auslesen muss das EPROM kurz wieder eingeblendet
werden. Da das laufende Programm selbst im TPA bei `0100H` liegt, würden dabei
Programmcode (`0100H–03FFH`) **und** der BDOS-Einsprung (`0005H`) vom EPROM
verdeckt. Deshalb:

1. Ein kurzes, **lageunabhängiges Lese-Stub** wird nach `8400H` kopiert (oberhalb
   des EPROM-Fensters, dort nicht verdeckt) und mit hohem Stack (`7F00H`) und
   gesperrten Interrupts (`DI`) ausgeführt:
   - BS-PIO Port B (`I/O 0AH`) lesen, **Bit0 setzen → EPROM einblenden**;
   - `0000H..03FFH` (1024 B) per `LDIR` nach `BUFFER` (`8000H`) kopieren;
   - den ursprünglichen Port-B-Wert **unverändert zurückschreiben → EPROM aus**;
   - `EI`, `RET`.
   Der Read-Modify-Write erhält die übrigen Port-B-Ausgänge **B2 (`/SPS-ESR`,
   Q240), B3 (`/WAIT-ZVE2`), B4 (`/SA`)** — es wird also weder die Speicher­schutz­logik
   noch die DMA-CPU noch die Netzlogik angetastet.
2. Zurück im normalen Speicher (EPROM aus, BDOS sichtbar) wird `BUFFER` über das
   BDOS in die Datei `ROM.BIN` geschrieben (8 Sätze à 128 B), dann Warmstart.

Innerhalb des EPROM-Fensters (`DI…EI`) erfolgen **keine** Interrupts und **keine**
BDOS-Aufrufe — der BDOS-Vektor `0005H` ist in diesem Moment verdeckt.

## Bauen

```sh
python3 tools/romread/build.py          # -> tools/romread/build/romread.com
```

Nutzt M80 + LINKMT aus dem Schwesterprojekt `CPA_Workbench/tools` über den
`cparun`-Emulator (Ladeadresse `0100H`). Pfad ggf. überschreiben:
`CPA_TOOLS=/pfad/zu/CPA_Workbench/tools python3 tools/romread/build.py`.

## Auf dem echten A5120 verwenden

1. `romread.com` mit **writeDiskUI** (`CPA_Workbench/tools/writeDiskUI.py`) auf
   eine CP/A-Diskette schreiben (Format z. B. `cpa780`/`cpa800`).
2. A5120 von einer System-Diskette booten, Diskette mit `romread.com` einlegen
   (z. B. in B:), ggf. mit `B:` zum Laufwerk wechseln und `ROMREAD` starten.
3. Das Programm legt `ROM.BIN` (1024 B) auf dem **aktuellen Laufwerk** an.
4. Diskette mit **readDiskUI** (`CPA_Workbench/tools/readDiskUI.py`) auslesen und
   `ROM.BIN` auf den PC kopieren.

> Voraussetzung TPA: das Programm nutzt die festen Adressen `7F00H` (Stack),
> `8000H` (Puffer) und `8400H` (Stub). Bei einem A5120 mit Standard-RAM (TPA bis
> ≈`0C400H`) ist das problemlos.

## Im Emulator validiert

Das Programm ist mit `k1520dbg` end-to-end verifiziert (CP/A läuft, EPROM wird
ein-/ausgeblendet, System überlebt):

```sh
A=$(mktemp --suffix=.img); cp disks/cpadisk_autofs_noclk_noautoexec.img "$A"
RR=$(pwd)/tools/romread/build/romread.com
printf "g 100000000\nload $RR 0x100\nset PC 0x100\nset SP 0x7F00\ng 5000000\nsave /tmp/buf.bin 0x8000 1024\nscreen\nq\n" \
  | tools/dev.sh tool k1520dbg "$A"
cmp /tmp/buf.bin doc/EPROMS/zre.rom    # → identisch: Puffer == echtes Boot-EPROM
```

Ergebnis: Bildschirm zeigt „ROM.BIN geschrieben (1024 Bytes)“, der Lesepuffer ist
**byte-genau** das Boot-EPROM, und die geschriebene Datei liegt verbatim im
Disk-Image.

> **Hinweis zum In-Emulator-Diskettentest:** Eine mit `cpmcp` frisch erzeugte
> *leere* Datendiskette wird vom Emulator-Floppymodell (cpa780) noch nicht
> kompatibel beschrieben/gelesen — der Funktionsnachweis erfolgt deshalb über das
> direkte Laden via `k1520dbg` (oben). Auf echter Hardware mit writeDiskUI/
> readDiskUI (echtes CP/A) entfällt diese Einschränkung.

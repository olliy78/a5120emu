# CP/A Formaterkennung & K5122-Protokoll — Referenz für die Emulation

Quelle: `CPA_Workbench/src/bc_a5120/` — `biosdsk.mac` (Diskettentreiber + Formaterkennung
`seldsk`/`drdfrm`), `biosdskp.mac` (**K5122-Hilfsprozessor-Treiber**, der echte
Byte-Strom-Code), `biosdpbm.mac` (DPB-Struktur), `biosnuc.mac` (Login/`dgetpb`).

Diese Datei dokumentiert (a) **welche Formate CP/A unterstützt**, (b) **den
Formaterkennungs-Algorithmus** und (c) **das K5122-Byte-Protokoll**, das der emulierte
K5122 nachbilden muss. Verwandte Analyse des konkreten Hängers:
[`analyse_cpa_boot.md`](analyse_cpa_boot.md).

---

## 1. Unterstützte Diskettenformate (5"-Teil)

Die Formaterkennung wählt anhand des **Sektorlängencodes** (size code, aus dem ID-Feld)
eine Tabelle `dtrsl0..3` und darin per (40/80-Spur, SS/DS) einen Eintrag. Struktur eines
Eintrags (`dsll=5` Bytes): `dsltrk` (log. Spuren), `dslspt` (**128-B-Sektoren/Spur**),
`dsldir` (Dir-Einträge−1), `dsloff` (2·Systemspuren + Flag fest/variabel),
`dslblk` (rel. Adresse Blockgrößen-Tab).

| size code | Sektorgröße | spt (128-B) | 40 SS | 80 SS | 40 DS (80 log) | 80 DS (160 log) |
|-----------|-------------|-------------|-------|-------|-----------------|-----------------|
| 0 | 128 B  | 26 | 40,26,63,off2  | 80,26,127,off2 | 80,26,127,off0  | 160,26,127,off0 |
| 1 | 256 B  | 32 | 40,32,63,off3f | 80,32,63,off3f | 80,32,127,off4f | 160,32,127,off4f |
| 2 | 512 B  | 36 | 40,36,0,off0   | 80,36,0,off0   | 80,36,0,off0    | 160,36,0,off0   |
| 3 | **1024 B** | **40** | 40,40,63,off2 | 80,40,127,off2 | 80,40,127,off0 | **160,40,191,off4** |

(off = `2·Systemspuren`; Suffix `f`=festes Offset, sonst variabel/„Directory möglich".
8"-Formate existieren zusätzlich unter `if disk8`.) Blockgrößen: `dbl1k`=Shift 3 (1K),
`dbl2k`/`dbl2k0`=Shift 4 (2K). Für die Testdiskette (5", 80 Tr., DS, 1024 B) gilt der
**fett** markierte Eintrag: 160 log. Spuren, **spt=40**, 191 Dir-Einträge, 2K-Blöcke.

**Diskettenlayout-Regeln** (vom Anwender bestätigt, mit der Erkennung konsistent):
- **Mit Systemspur:** Spuren `C0/H0 … C1/H0` immer **128 B**; Datenformat variabel;
  **Directory auf `C2/H0`**.
- **Ohne Systemspur:** nur Datenbereich; **Directory auf `C0/H0`**.

---

## 2. Formaterkennungs-Algorithmus (`seldsk`→`drdfrm`, biosdsk.mac)

`seldsk` (Z.40): `call dgetpb` (HL→DPH, IX→DPB); bei `E`-Bit0=0 (BDOS-LOGIN) und
gelöschtem `dpbfrm`-Flag → `drdfrm` (sonst nur Bereitschaftstest / DPH zurück).

**`drdfrm` — automatische Formaterkennung** (Z.81–405), nur **READ ID** + wenige
Sektorlesungen:

1. DPB-Flags, `dpbofs`=0 zurücksetzen; `dbtrk`=0, `dbsec`=1; Sektor-Translate-Tab 1,2,3,…
2. **`dsidtr`** (READ ID, Spur 0): liefert `H`=size code → `(dbslc)`. Fehler → „Systemspuren annehmen".
3. **`dbtran`** (lies Spur 0/Sektor 1) → Puffer `(dbdma)`; Byte 0 prüfen:
   `0E5h`=leer, `40h`=IBM-Daten (0 Systemspuren), `<20h`=Lader; sonst Directory →
   `dpbofs` setzen. (So wird **mit/ohne Systemspur** unterschieden.)
4. **Datenspur-Analyse** (`ld a,dlgint(=3); call dsidtt`): READ ID auf der ersten
   Datenspur → `H`=size code (`d`), `E`=Spur. SS/DS: Vorder-/Rückseite-ID lesen und
   vergleichen. 40/80: prüfen, ob die **ID-Spurnummer** == `dlgint` (Doppelstep) oder
   == `2·dlgint` (Einzelstep); sonst `seldle` (SELECT-Fehler).
5. `seldl1`: `dpbslc` = `d` (size code der Datenspur) → `dtrsla` wählt `dtrsl0..3` →
   per 40/80/DS den Eintrag → DPB füllen (`dpbtrk/dpbspt/dpbdir/dpbofs/dpbbls/…`).
6. `sf128z`: ab Spur 0 per READ ID die **abweichend 128-B-formatierten Systemspuren**
   zählen (`dpbfsm`), bis der Datenformat-size-code erscheint.
7. Kapazität `((Spuren−ofs)·spt/2^bls)−1` über **IX** rechnen; `selext` → Statuszeile,
   `ret` mit HL=DPH. Fehlerpfad `seldle`: `dpbslc=0FFh; HL=0` (SELECT-Fehler).

**Kernpunkt:** Die Erkennung braucht von der Hardware fast nur die **ID-Felder**
(`cyl, side, sec, size code`) per READ ID und je eine 128-B-Sektorlesung. Aus size code
(Spur 0 vs. Datenspur), ID-Spurnummer (40/80) und Vorder/Rückseite-Vergleich (SS/DS)
plus dem ersten Directory-Byte (System­spuren) ergibt sich das Format.

---

## 3. K5122-Byte-Protokoll (`biosdskp.mac`, läuft auf dem Hilfsprozessor/ZVE2)

Der Treiber bedient den K5122 über **Steuerport `flcoad`** (Marken-/Lese-Modus) und
**Datenport `fldabd`** (Byte-Strom). Der emulierte K5122 streamt die (Robotron-)Spur als
Bytefolge; der Treiber sucht darin Marken und liest Felder.

**READ DATA (`dio`)** — Sektor lesen:
- Sucht im Strom `A1`-Lückenbytes (`cp e`, e=`A1`), dann `FE` (`cp d`) = IDAM.
- Vergleicht danach **cyl (`cp c`), side (`cp b`), sec (`cp l`), size code (`cp h`)**;
  bei Abweichung → `serr` (falsche Sekt-ID, Wiederholzähler `seerc`=60).
- Bei Treffer: Daten-Marke suchen (`A1…FB`), `inir` Datenbytes, CRC ignorieren.

**READ ID (`diotsr`/`diotrr`, Z.139)** — beliebige Sektor-ID lesen:
- Sucht `A1…FE`; das Byte nach `FE` ist die **Spur** (`ld l,a` → `ft.trk`); `inir` 3
  weitere Bytes = **side, sec, size code** (→ `ft.sid/ft.sec/ft.len`); CRC ignoriert.
- Prüft `cp c` (richtige Spur). Rückgabe an diskio: `E=cyl, D=side, L=sec, H=size code`.
- **Wichtig:** READ ID liefert *immer* die ID der nächsten gefundenen Marke — auch wenn
  die Spur ein „falsches"/unerwartetes Format hat, liefert der echte Controller die
  *tatsächlichen* ID-Bytes. Der emulierte K5122 muss das ebenso tun.

Mark-/Modus-Steuerworte (an `flcoad`), relevant für die Marken-Resynchronisation:
`10110101b` (b5/b1, Mark reset, „Lesen 1"), `10000101b` (85/81 MFM bzw. 87/83 FM,
„Lesen Marke entspr. Aufzeichnungsverfahren"), `10111101b` (Ende/Seite halten),
`10100101b`/`10111001b` (Seite/AMF). FM vs. MFM wird über das „Lesen-Marke"-Steuerwort
(`diom01`: `85/81` MFM, `87/83` FM) unterschieden — bei FM kommt nach `FE` **kein**
`A1`-Sync. Der echte Controller liefert bei falschem Aufzeichnungsverfahren keine gültige
Marke → `serr`/Timeout; **das emulierte K5122 muss für FM-Spuren entsprechend reagieren**,
wenn wir FM-Formate unterstützen wollen.

---

## 4. Aktueller Stand / Fehlerpunkt

Für die 1024-B-DS-80-Diskette **baut die Erkennung den korrekten DPB** (spt=40,
size code 3) — die READ-ID/Lese-Pfade des emulierten K5122 funktionieren für dieses
Format grundsätzlich. **Offen:** während der `dir`-LOGIN-Erkennung berechnet der
Algorithmus aus einer Read-Rückmeldung `[E3F9]=0x0101` und ruft die Record→Track-
Divide-Routine `C772` (`÷ [D1BE].spt`), die mit noch nicht gesetztem `[D1BE]` durch 0
teilt (Hänger `@0xC7A3`). Es ist zu klären, **welche Probe-Rückmeldung unseres K5122
von der echten Hardware abweicht** und den Algorithmus in diesen Pfad treibt — siehe
[`analyse_cpa_boot.md`](analyse_cpa_boot.md) §5a/§5b. Die hier dokumentierten Formate &
das READ-ID-Protokoll sind die Grundlage, um das im K5122-Emulator korrekt umzusetzen.

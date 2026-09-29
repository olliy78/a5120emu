# U8001/U8002 (Z8001/Z8002): Befehlstabelle, Disassembler, Assembler

Header-only, ohne Abhängigkeiten außer der Standardbibliothek. Tabelle und Dekoder liegen
seit S3 unter `core/primitives/z8000/`, weil der CPU-Kern sie benutzt (eine Tabelle, nicht
zwei). Arbeitspaket S2 aus
`doc/design/17_a5120_16.md`.

| Datei | Inhalt |
|---|---|
| `core/primitives/z8000/z8k_table.h` | **Die** Befehlstabelle `kRows` (438 Zeilen, 195 Mnemoniks) + aufbereitete Form `Insn` + Schnellsuche `Table::candidates(w0)` + Namen (cc, Steuerregister) |
| `core/primitives/z8000/z8k_codec.h` | `decode()` (Worte per Rückruf, nur so viele wie nötig) und `encode()`; `Decoded::cycles()` |
| `z8k_disasm.h` | Text in Zilog-Syntax: `disasm()`, `formatDecoded()` |
| `z8k_asm.h` | Zwei-Pass-Assembler `assemble()`, Einzelbefehl `assembleLine()`, Listing |
| `../z8kasm.cpp` | Kommandozeile (`tools/dev.sh tool z8kasm …`) |

Tests: `tests/debugtools/test_z8k_{table,disasm,asm}.cpp`, `tests/cli/cases/z8kasm_*.cli`,
Firmware-Fixtures `tests/fixtures/z8000/`.

## Die Tabelle

Eine Zeile je Kodierung, so geschrieben wie das Formatbild im Handbuch:

```cpp
//  mn      w0                     w1                     ops                ns  ss  sl  ans ass asl perN flags
{"ADD",  "01 000001 ssss dddd", "",                    "RW:d,X:s",        10, 10, 13,  0,  0,  0,  0, 0},
{"LDIR", "10 111011 ssss 0001", "0000 rrrr dddd 0000", "IR:d,IR:s,RW:r",  11, 11, 11,  0,  0,  0,  9, Z8K_REPEAT},
```

- `w0`/`w1`: `0`/`1` fest, Kleinbuchstabe = Feld. `w1 = ""` → kein festes zweites Wort.
- `ops`: Operandenart + Feld (`RW:d`, `BX:s:x`, `#1`). Erweiterungsworte (DA, X, BA,
  RA16, IMB/IMW/IML, PORT) folgen hinter w0 [w1] in Operandenreihenfolge.
- Takte `ns/ss/sl` = nichtsegmentiert / segmentiert kurz / segmentiert lang;
  `ans/ass/asl` = Bedingung nicht erfüllt; `perN` = Zusatztakte je Einheit
  (Wiederholung, Schiebestelle, LDM-Register, MULTL-Einsbit, HALT/MREQ-Warteschleife).
- `flags`: `Z8K_B/L/Q` (Breite), `Z8K_PRIV`, `Z8K_JUMP/CALL/RET/REPEAT`, `Z8K_EPA`,
  `Z8K_ALT` (Zweitkodierung).

Feld 0 unterscheidet Adressierungsarten (IM↔IR, DA↔X, RA↔BA): die Arten `IR`, `X`,
`BA` und die BX-Basis verlangen ein Feld ≠ 0, `CTL` einen Wert 2..7. Mehr gibt es an
Nebenbedingungen nicht. `TableIsUnambiguous` prüft für alle 65 536 ersten Worte, dass
höchstens eine Zeile passt bzw. das zweite Wort eindeutig entscheidet.

### So benutzt S3 (CPU-Kern) die Tabelle

```cpp
z8k::Decoded d;
if (!z8k::decode([&](int i) { return fetchInstrWord(i); }, fcw.seg, d)) { /* unbelegt */ }
// d.insn->index  → Handler-Tabelle (einmal je Prozess aus insn->mn + src->ops aufbauen)
// d.op[i]        → reg / reg2 / seg / value / disp je nach Kind
int cyc = d.cycles(condTrue) + d.insn->perN * n;
```

- `fetch(i)` wird für i = 0, 1, 2 … aufgerufen, in Reihenfolge und nur so weit nötig
  — jeder Aufruf kann ein eigener Buszyklus sein (Status „Instruction Fetch" für i = 0,
  sonst „Instruction Space").
- Relative Ziele stehen als `disp` = Byteabstand zum **Folgebefehl** (JR, DJNZ, CALR,
  LDR, LDAR einheitlich).
- `Kind::IR/BA/BX/RP` heißen im segmentierten Modus Registerpaar, `IO` ist immer ein
  Wortregister (E/A-Adressen sind 16 Bit).
- `Kind::SHR` hat `disp` < 0 (Rechtsschieben), `SHL` ≥ 0 — genau wie das Vorzeichen im
  zweiten Wort. SDL/SDA holen die Weite zur Laufzeit aus dem Register.

## Syntax (Disassembler-Ausgabe = Assembler-Eingabe)

```
LD R0,#%ED4D          LDB RH2,#%55          LDL RR2,#%0012ED4D
LD R2,@R5             LD R2,@RR4            (segmentiert: Zeiger = Registerpaar)
LD R4,%231A(R3)       LD R4,<<5>>%231A(R3)  LDB RH2,|<<15>>%23|   (kurzer Offset)
LDL R5(#%0018),RR2    LD R2,RR4(R3)         LDR R2,<<13>>%0208    (Ziel absolut)
JP NZ,%1234  JR %0100  RET                  (Bedingung T wird weggelassen)
SETFLG C,Z   DI VI,NVI   LDCTL PSAPOFF,R5   LDCTLB RH3,FLAGS
INC R1       RL R2      SRL R3              (ohne Anzahl = 1)
LDB.L RH1,#%12                              (Langform 2001 1212 statt C112)
```

Assembler zusätzlich: `ORG`, `EQU`/`:=`, `DB/BVAL`, `DW/WVAL`, `DL/LVAL`, `DS/BLKB`,
`EVEN`, `ALIGN`, `SEG`/`NONSEG`, `END`; Zahlen `%1234`, `%(2)1010`, `0x1234`, `1234H`,
`0b1010`, `'A'`, `$`; `SEG(x)`, `OFF(x)`; Kommentare `;…` und `!…!`. Adressen im
segmentierten Modus sind 32-Bit-Werte wie im Registerpaar (`<<s>>o` = s·2²⁴ + o).
Ohne `|…|` wird immer die lange Form erzeugt (Länge hängt nie von einem Symbolwert ab).
`--lax` / `AsmOptions::laxPointers`: wie `z8001asm.py` `@Rn` und `@RRn` in beiden Modi,
Registernummer wie geschrieben.

```sh
tools/dev.sh tool z8kasm -s -l -o fw.bin fw.s              # assemblieren, Listing
tools/dev.sh tool z8kasm -s -o fw.bin --sym fw.sym fw.s    # dazu Symboldatei für k1520dbg
tools/dev.sh tool z8kasm -d fw.bin -s --start %40          # disassemblieren
```

### Symboldatei für den Debugger (`--sym`, S5b)

`--sym DATEI` schreibt die **Marken** des Programms (keine `EQU`-Konstanten — sie sind keine
Adressen und würden jede gleichlautende Zahl im Disassembler beschriften), eine je Zeile,
nach Adresse sortiert:

```
# z8kasm-Symbole (U8001) fuer k1520dbg: <<SEG>>%OFFS NAME — fw.s
<<3>>%0100 START
<<3>>%0104 LOOP
```

- Segmentiert assemblierte Marken tragen ihr Segment (Wert `s·2²⁴ + o` → `<<s>>%o`).
- Nichtsegmentiert assemblierte Marken haben keins; sie bekommen `--sym-seg N` (Vorgabe 0) —
  das Segment, in dem das Programm läuft.
- `k1520dbg` liest die Datei mit `sym datei` bzw. `-s datei` (U8001-Zeilen erkennt es am
  `<<`; Z80-Zeilen `ADDR NAME` dürfen in derselben Datei stehen). Auch `NAME <<s>>%o` und
  `NAME = <<s>>%o` werden gelesen. Anzeige und Gebrauch: `tools/k1520dbg.md` §11.
- Baustein: `z8k::formatSymbols(AsmResult, nonsegSeg)`, Marken in `AsmResult::labels`;
  Wächter `Z8kAsm.SymboldateiNurMarkenMitSegment`, `cli_z8kasm_symbole`,
  `cli_dbg_u8000_symbole`.

## Quellen, Lücken, Unsicherheiten

Maßgeblich: Zilog *Z8000 CPU User's Reference Manual* — OCR-Fassung
`~/projects/A5120.16/docs/508523565-Zilog-Z8000-Reference-Manual.txt` (Formatbilder
teilweise lesbar, Anhang C „Clock Cycles", Opcode-Map); das Markdown-Transkript im
CPA_Workbench hat bei den **Bitmustern viele Fehler** (z. B. `ADD Rd,@Rs` als
`00000001 Rs 0000`, `DIV X` SS 110) und taugt nur für Takte zum Gegenlesen.
`doc/trascripted/CPU_U8001_8002.md` enthält keine Befehlsliste.

1. **EPA-Befehle (0E/0F/4E/4F/8E/8F).** Die Bitbilder der sieben Schablonen (§6.8) sind
   im OCR verloren. Die Tabelle führt sie roh: zwei Worte (+ DA/X-Adresse bei 4E/4F),
   Felder als `#%xx`. Genügt für Länge und Extended-Instruction-Trap (ohne EPU der
   einzige Fall am A5120.16). Takte 0.
2. **LDPS @Rs:** Anhang C 12/12/12, Befehlsseite 12/16/16 → Seite übernommen.
3. **DIV X:** Anhang unlesbar, Transkript 109/110/112, Befehlsseite 109/109/112 → Seite.
4. **MULTL DA/X segmentiert:** nur Anhang C (283/283/286, 284/284/287), auf der Seite
   unlesbar — unsicher.
5. **DIV/DIVL** sind bei Division durch 0 um 94/714 und bei Überlauf um 82/693 Takte
   kürzer (Befehlsseite, Note 2) — in der Tabelle steht der Normalfall.
6. **Byte-Schieben (SLLB/SRLB/SLAB/SRAB):** Anzahl als 8-Bit-Wert im unteren Byte des
   zweiten Worts („8- or 16-bit"), oberes Byte als 0 angenommen.
7. **`LDB Rbd,#data` Langform** (`20 0d` + Wort mit doppeltem Byte): welches Byte die
   CPU nimmt, sagt das Handbuch nicht; der Disassembler zeigt das untere.
8. **Unbelegte Kodierungen:** 7078 erste Worte passen auf keine Zeile (reservierte
   Opcodes, R0 als Zeiger/Index/Basis, LDCTL 0/1, …). Die Opcode-Map sagt dazu nur, die
   CPU „interpretiert jedes Bitmuster als irgendeinen korrekt kodierten Befehl" — welchen,
   steht nirgends. Die Tabelle bleibt streng beim Handbuch; S3 muss entscheiden (MAME als
   Orakel).
9. **Ungerade Registerpaare** (`RR3`, `@RR5`, `RQ2`) sind im Handbuch nicht zulässig.
   Der Dekoder nimmt den Feldwert, wie er ist (der Disassembler zeigt dann `@RR5`), der
   Assembler lehnt ab (außer `--lax`). Was ein echter U8001 damit tut, bleibt offen
   (Plan §8).
10. **Segmentierte lange Adresse:** das untere Byte des ersten Worts ist laut Handbuch
    0; der Dekoder ignoriert es.

## Befunde an `z8001asm.py` / den em256ful-Firmwares

Nebenprobe `Z8kFirmware.*`: nichtsegmentiert assembliert ergibt z8kasm dieselben Bytes
wie `z8001asm.py` — bis auf **PUSH/POP**, wo `z8001asm.py` das Stapelregister auf gerade
maskiert (`@R15` → Feld 14). Gelesen **so, wie der U8001 sie mit FCW %C000 ausführt**
(segmentiert), zeigen die Firmwares drei Fehler des Werkzeugs:

- **Adressen sind einwortig** (nichtsegmentierte Form). `JP EQ,DONE` in `fw_march`
  (`5E06 01DE`) liest der U8001 als `JP Z,|<<1>>%DE|` — kurzer Offset, **Segment 1**.
- **Zeiger bleiben ungerade:** `LD @R5,R4` wird `2F54` = `LD @RR5,R4`; ebenso `@R7`,
  `@R11`. In `fw_march` stehen `@RR8` und `@R7` nebeneinander.
- `@R15` in PUSH/POP wird zu `@RR14` — segmentiert zufällig richtig, nichtsegmentiert
  falsch.

# MC80 — Kassette an der K5020 (AP M7)

Stand 2026-10-15, Zweig `kassette_mc80`.  Karte und Laufwerk: `doc/merkposten/kassette.md`,
Treiber-Disassemblat `doc/mc80/egos.prn` (zitiert als [P xxxx]), Auswertung `doc/mc80/egos.md` §2,
Betriebsanleitung Magnetband [EGOS2 S.n] = `doc/original_docs/mc80/mc80.3x-egos.pdf` ab PDF-Seite 22.
**[?]** = Annahme, aus dem Verhalten des Treibers erschlossen, nicht aus einer Hardwareunterlage.

## 1. Was nachgebildet ist

| Modell | Karte | Laufwerk (Profil) | Bittakt | neue Kassette |
|---|---|---|---|---|
| MC80.30 | umgebaute K5020 an 30H–37H | LW 1200, 47,6 mm/s, 1 523 Bit/s | **extern**: ASP-CTC K2 über den Koppelbus | Kompaktkassette (§3) |
| MC80.31 | AKB K5020 an 30H–37H | K5200 mit **19 cm/s**, 6 000 Bit/s [?] (§2.2) | Bustakt (Karte) | Kompaktkassette (§3) |
| MC80.33 | AKM + PK 3 | — | — | gesperrt: Treiber-ROM halb (`doc/mc80/pk3.md`) |

Die Karte ist eine **steckbare Option** wie an den anderen Maschinen (`installK5020()` nach dem
Anlegen, C-ABI `k1520_tape_*`, Oberfläche *Einstellungen ▸ Allgemein ▸ Kassette*, Profilfeld
`tape_wahl`).  `Mc80Machine` überschreibt `k5020Basis()` (30H), `k5020Konfigurieren()` (Takt,
Profile, Maße neuer Kassetten) und `k5020Einketten()` (hinter der ASP; am .30 die Taktleitung).

**Bedienung** (EGOS nach `INIT`, nur Laufwerk 1, [P 17DC]; [EGOS2 S.5–6]):

| Kommando | Wirkung |
|---|---|
| `KINIT 1` (= FORMAT) | Bandmarke, Block `89H 'DIRECTORY' 00 01 FFH`, 14 leere Verzeichnisblöcke, Bandmarke [P 111C] |
| `WRITE 1,NAME,seg,rama,rame` | Bereich aus dem RAM-Segment `seg` (Einsernummer, 01 = SPE 2) als Datei; Dateikopf 128 B (Satzzahl FA71H, Ladeadresse FA78H [P 15FB]), Daten in 256-B-Blöcken |
| `READ 1,NAME,R` | an die Ladeadresse zurück; „die RAM-Kettung wird auf die Anfangsadresse der Prozedur geführt“ [EGOS2 S.6] — ein dort stehendes Kettenglied mit Kennbyte (z. B. 01 = Kommando) ist danach wieder aufrufbar |
| `WRITE 1,NAME` / `,A` / `,B` | Arbeitsbereich mit Kennbyte 13H („E“) bzw. 1CH („F“) [P 156D] — nicht geprüft |

INIT legt an 4000H das leere Glied `00 F5 7A FF` an, das zum Directory (Kennbyte 04) bei BAF8H
führt; ein eigenes Programm an 4000H muss diesen Abstand übernehmen, sonst findet KLOG das
Directory nicht und meldet `%C7` [P 10CD].

Beobachtet (Emulator): `WRITE 1,TST,01,4000,40FF` schreibt Dateikopf, **einen** Datenblock mit
4000H–40FFH und danach fünf weitere 256-B-Blöcke mit dem RAM-Inhalt dahinter, dann eine Bandmarke;
der Dateikopf nennt 1 Satz.  Wozu die fünf Blöcke dienen, ist nicht untersucht [?].

## 2. Takt und Bitrate

### 2.1 MC80.30: Takt aus der ASP

MB_INIT programmiert **ASP-CTC K2 als Zähler, Zeitkonstante 2** (`D6H ← 47H, 02H` [P 1F96]);
K1 ist der Zeitgeber des Treibers (10 ms / 26,7 ms).  Woher K2 zählt und wie der Ausgang die
K5020 erreicht (X3B06/X4B06, Forum 12849), steht in keiner vorliegenden Unterlage.

**Entscheidung:** Der Koppelbus-Ausgang ZC/TO2 ist der **Schreibtakt** der umgebauten Karte
(`K5020::Takt::ExternerCtc`, `setExternerTakt`).  Er liegt an, solange K2 als Zähler läuft
(`Mc80Machine::aspBandtakt`: Steuerwort D6 = 1, Zeitkonstante geladen); ohne ihn übernimmt das
Schieberegister kein Byte.  Die **Bitrate kommt aus dem Profil LW 1200** (Dichte × Geschwindigkeit),
nicht aus einer Frequenz — die Eingangsfrequenz von K2 ist unbekannt [?].  Die Leseseite ist
selbsttaktend und hängt nicht an der Leitung.

### 2.2 MC80.31: 19 cm/s

Der Leseinterrupt [P 1BE8] prüft **jedes** gelesene AAH darauf, ob es die Postambel ist:
`DI`, `DJNZ` 255 (≈ 1,35 ms), dann BMS lesen [P 1C3A].  Im Emulator dauert die ganze Unterbrechung
für ein AAH ≈ **1,97 ms**.  Die Daten-PIO hält ein Byte; kommt das übernächste, bevor die CPU das
nächste gelesen hat, geht eines verloren.

- 12 000 Bit/s (K5200 mit 38 cm/s, 0,67 ms je Byte): schon **ein** AAH kostet ein Byte — auch
  die Präambel AAH selbst; im Emulator war so nicht einmal das Directory lesbar (`%D1`).  Mit einer
  Präambel ABH (§2.3) ginge ein Block ohne AAH in den Daten, einer mit AAH nicht.
- 6 000 Bit/s (19 cm/s, 1,33 ms je Byte): ein einzelnes AAH geht, **zwei AAH hintereinander**
  verlieren das übernächste Byte (≈ 3,94 ms gegen 4,0 ms — knapp).
- 1 523 Bit/s (LW 1200): unkritisch.

Deshalb **K5200 mit 19 cm/s** am .31 [?] — mit 38 cm/s wäre der Treiber unbrauchbar.  Die Grenze
„zwei AAH hintereinander“ ist eine Eigenschaft des Treibers; ob sie am Gerät genauso knapp ist,
hängt an der tatsächlichen Unterbrechungsdauer [?].  EGOS setzt das Statusbit 6 (19 cm/s) nie.

### 2.3 Präambel AAH oder ABH

`egos.md` (M1) las „das erste Byte muss ABH sein“.  **Berichtigt:** [P 1BF0–1C03] nimmt als erstes
Byte **AAH** (über die Postambelprobe: BMS ist noch 1 → Datenbyte) **oder ABH** (direkt); alles
andere wird verworfen.  Beim Mitlesen nach dem Schreiben wird Bit 0 des ersten Bytes ausgeblendet
[P 1BC5], damit beide Präambeln zum Puffer passen.  Die Karte reicht die Präambel unverändert durch
(Festlegung 10 der Kassette); das Modell liefert AAH.  Dass ABH eigens zugelassen ist, spricht für
ein am Gerät beobachtetes Kippen von Bit 0 — oder dafür, dass der Programmierer die lange
Postambelprobe für das erste Byte umgehen wollte [?].

## 3. Die Kassette: kurzer Vorspann, keine Marken

Mit der Robotron-Kassette des Kernmodells (1 m Vorspann, BOT-Marke 450 mm dahinter) scheitert EGOS
an beiden Modellen — aus zwei Gründen, beide im Treiber belegt:

1. **AEB-Interrupt während des Vorlaufs.**  Nach dem Rückspulen fährt EGOS zum Schreiben 80 × 250 ms
   = **20 s**, zum Lesen 66 × 250 ms = 16,5 s vorwärts [P 1D94–1DA9, 1E3A–1E63] und schaltet den
   AEB-Interrupt (Steuer-PIO A, `17H` = aktiv bei 0) nach **250 ms** scharf [P 1E17].  Jede
   BOT-Marke danach löst [P 1CDF] aus: Vorwärtslauf → Status „Bandende“, Fehler 1AH (`%C2`).
   (Den Interrupt, der beim Scharfschalten im Vorspann sofort ansteht, schluckt EGOS über den
   Hilfsvektor BAH = nur `RETI`: Vektor BAH, `83H`, `EI / NOP / NOP / DI`, erst dann der echte
   Vektor [P 1DF4–1E08, 1CF9].)
2. **Rückspulen.**  [P 196F]: erst **2 s vorwärts**, dann `UMS`, danach muss AEB 4096 Abfragen
   lang aktiv sein.  Das geht nur gleich aus, wenn 2 s Vorwärtslauf aus dem Vorspann herausführen —
   sonst hält `UMS` sofort, und eine frisch eingelegte Kassette steht woanders als eine
   zurückgespulte; das Lesen (16,5 s) begann im Emulator dann hinter dem ersten Block, den das
   Schreiben (20 s) angelegt hatte (`%D1`).  Am LW 1200 sind 2 s **95 mm**.

**Entscheidung:** neue Kassetten am MC80 sind `Geometrie::kompaktkassette()` — **Vorspann 80 mm,
keine BOT-/EOT-Marke** [?] (Maße sonst wie Vorgabe).  Die Maße stehen in der `.k52`; eine
Robotron-Kassette lässt sich am MC80 einlegen, EGOS kommt mit ihr aber nicht zurecht.  Gesetzt über
`K5020::Config::neue_kassette` → `KassettenLaufwerk::setNeueKassette` (auch `k1520_tape_new` und
`tape_load` mit fehlender Datei).

## 4. Kernfund

`Mc80Machine::powerOn()` rief `reset()` und damit `bestueckungAbschliessen()` — der Konstruktor
fährt powerOn(), also schlug `installK5020()` am MC80 **immer** fehl (ebenso RAF/K6022, die die
Oberfläche dort nicht anbietet).  Jetzt wie beim K8915: `resetHardware()` ohne Abschluss, `reset()`
schließt ab.  Wächter `Mc80K5020.SteckbarNachDemAnlegenNichtNachDemErstenLauf`.

## 5. Wächter

| Test | prüft |
|---|---|
| `Mc80Kassette.SpeichernUndLaden/Mc8030`, `/Mc8031` | KINIT, WRITE, RAM zurücksetzen, READ ,R, Programm läuft (Abnahme) |
| `Mc80KassettePraeambel.LiestAuchPraeambelABH` | Band mit ABH-Präambeln lädt genauso |
| `Mc80K5020.*` | Stecken/Tore, Takt/Profil/Kassette je Modell, Takt aus ASP-CTC K2, Init durch EGOS |
| `K5020Test.ExternerTaktSperrtUndGibtDieAusgabeFrei`, `.BustaktFragtDenExternenTaktNicht` | Karte |
| `py_mc80emu_gui` (`test_kassette_folgt_der_karte_und_legt_kompaktkassetten_an`), `py_kassette_gui` (mit `mc80`) | Oberfläche |

## 6. Offen

- Frequenz und Weg des externen Takts (X3B06/X4B06) [?]; Geschwindigkeit des K5200 am .31 [?].
- Vorspannlänge echter MC80-Kassetten; ob sie Marken tragen [?].
- Die fünf zusätzlichen 256-B-Blöcke bei `WRITE` (§1).
- Arbeitsbereiche (`WRITE 1,NAME` mit Kennbyte 13H/1CH) und Bandmarke suchen bei mehreren Dateien
  sind nicht eigens geprüft.

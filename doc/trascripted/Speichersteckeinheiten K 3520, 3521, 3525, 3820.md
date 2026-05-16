# robotron

Speichersteckeinheiten

K 3520; 3521; 3525

K 3820

Betriebsdokumentation

6. Auflage
Karl-Marx-Stadt, 1987

© VEB Kombinat Robotron, 1987

Inhaltsverzeichnis

1. Anschluß von Speichern
1.1. Speicheranschlüsse
1.2. Ansteuerbedingungen
2. Allgemeine Beschreibung
2.1. Technische Daten
2.2. Speicherorganisation
2.3. Anschlußbedingungen
2.4. Bauelementebasis
3. Operativspeicher OPS K 3520
3.1. Kurzcharakteristik
3.2. Technische Daten
3.3. Programmierung der STE
3.3.1. Programmierfelder der STE
3.3.2. Adreßcodierung
3.3.3. Auswahl des Speichersperrsignals MEMDI
3.3.4. "WAIT"-Bildung
3.3.5. Betriebsspannungszuführung 5 PG
3.4. Funktionsbeschreibung
3.4.1. Verwendungszweck
3.4.2. Funktion
4. Operativspeicher OPS K 3521
4.1. Kurzcharakteristik
4.2. Technische Daten
4.3. Einsatzbedingungen für den Stütz-Akkumulator
4.4. Programmierung der STE
4.5. Funktionsbeschreibung
4.5.1. Verwendungszweck
4.5.2. Funktion

5. Operativspeicher OPS K 3525
5.1. Kurzcharakteristik
5.2. Technische Daten
5.3. Programmierung der STE
5.3.1. Programmierfelder der STE
5.3.2. Adressenzuordnung
5.3.3. Auswahl des Speichersperrsignals MEMDI
5.3.4. "WAIT"-Bildung
5.4. Funktionsbeschreibung
5.4.1. Verwendungszweck
5.4.2. Funktion
6. Programmierbarer Festwertspeicher PFS K 3820
6.1. Kurzcharakteristik
6.2. Technische Daten
6.3. Programmierung der STE
6.3.1. Programmierfelder der STE
6.3.2. Adressenzuordnung
6.3.3. Belegung der EPROM-Bauelemente auf der STE
6.3.4. Auswahl des Speichersperrsignals MEMDI
6.3.5. "WAIT"-Bildung
6.4. Funktionsbeschreibung
6.4.1. Verwendungszweck
6.4.2. Funktion
6.4.2.1. Funktionsbeschreibung
6.4.2.2. Adreßdecodierung durch 4 Bit Volladder PS83
Serviceschaltpläne


1. Anschluß von Speichern

1.1. Speicheranschlüsse

Der Adreßbus AB0 ... AB15 ermöglicht die Adressierung eines 64 K Byte-Speichers, dessen Konfiguration nach Belieben zusammengestellt werden kann.
Zur Anwendung kommen derzeit folgende Speichersteckeinheiten:

- K 3520  4 K Byte statischer Schreib-Lese-Speicher (s-RAM) n MOS
- K 3525  16 K Byte dynamischer Schreib-Lese-Speicher
- K 3820  16 K Byte programmierbarer Festwertspeicher (EPROM)
- K 3521  4 K Byte Schreib-Lese-Speicher CMOS

Durch die Signale $\overline{\text{MEMDI1}}$ und $\overline{\text{MEMDI2}}$ des Koppelbusses (siehe Busrichtlinie K 1520) kann die Speicherkapazität von 64 K Byte auf das Doppelte erweitert werden (Codierbrücken X6:2/3 - X7:2/3).
Der gesamte Speicher ist mit dem Systembus über folgende Signale verbunden:

| Signal | Bedeutung |
| :--- | :--- |
| AB0 ... AB15 | Adreßbus |
| DB0 ... DB7 | Datenbus |
| $\overline{\text{MREQ}}$ | Speicheranforderung |
| $\overline{\text{RD}}$ | Lesesteuersignal |
| $\overline{\text{WR}}$ | Schreibsteuersignal |
| $\overline{\text{MEMDI}}$ | Speichersperre |
| $\overline{\text{RDY}}$ | Quittungssignal des Speichers |
| $\overline{\text{WAIT}}$ | WAIT-Anforderung an ZRE (Zentrale Recheneinheit) |
| TAKT | Systemtakt |
| $\overline{\text{M1}}$ | Befehlslesezyklus |

1.2. Ansteuerbedingungen

Alle Speichermodule sind durch ihre dynamischen Kennwerte auf das Signalspiel des gemeinsamen Bussystems des K 1520 abgestimmt.
Folgende Bedingungen sind zu beachten:
Die Adresse muß mindestens 530 ns am Bus stabil anliegen. $\overline{\text{MREQ}}$ = low (aktiv) ist 140 ÷ 240 ns nach Anliegen der gültigen Adresse erforderlich. Das Signal bleibt bis zum Adreßwechsel aktiv. Es muß dabei mindestens 300 ns vor Schreibimpulsende $\overline{\text{WR}}$ = high gültig sein und bis zu dessen Ende anliegen, wenn der Speicher beschrieben wird.
$\overline{\text{WR}}$ = low muß spätestens 300 ns vor dem folgenden Adreßwechsel gewährleistet sein und bis zum Adreßwechsel aktiv bleiben.
Beim Lesen erscheint $\overline{\text{RD}}$ = low (aktiv) spätestens 170 ns nach Adreßwechsel und bleibt mindestens bis $\overline{\text{MREQ}}$ nach high aktiv. $\overline{\text{M1}}$ wird wie die Adresse geschaltet.
Gelesene Daten sind spätestens 450 ns nach Adreßwechsel gültig.

![[Pasted image 20260516143416.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt ein Zeitlaufschema der Steuersignale für den Speicherzugriff. Die Signale "Adresse" und "M1" liegen über einen Zeitraum von 530 ns an. Das Signal "MREQ" wird 140 bis 240 ns nach Adressbeginn aktiv (low). Das Schreibsignal "WR" muss mindestens 300 ns vor dem Adresswechsel beendet sein. Das Lesesignal "RD" wird spätestens 170 ns nach dem Adresswechsel aktiv.*

2. Allgemeine Beschreibung

2.1. Technische Daten

| Parameter | Wert |
| :--- | :--- |
| Steckeinheiten Abmessungen: | 215 mm x 170 mm |
| Steckraster: | 20 mm |
| Steckverbinder: | 2 x 58-pol. |
| Betriebsspannung: | 5 P $\hat{=}$ + 5 V |
| | 5 N $\hat{=}$ - 5 V |
| | 5 PG $\hat{=}$ + 5 V |
| | 12 P $\hat{=}$ + 12 V |
| Einsatzklasse: | oberhalb 40 °C ist Zwangsbelüftung erforderlich |

2.2. Speicherorganisation

Die im Punkt 1.1. genannten Speichermodule stehen zur Verfügung, die beliebig und mehrfach bis zur Kapazitätsgrenze eingesetzt werden können (max. 64 K Byte).
Alle Speichermodule können entsprechend des Speichervolumens über Programmiereinrichtungen auf den Steckeinheiten (Wickelbrücken oder auch Schalter) wahlweise zusammenhängenden Adreßbereichen zugeordnet werden, wobei die Speicheranfangsadresse der Steckeinheiten durch 4 K teilbare Werte annehmen.
Dadurch werden geschlossene Speicherbereiche garantiert, die den Erfordernissen der Programmsysteme entsprechen. Adressen dürfen dabei nicht mehrfach belegt werden.
Die Speichersteckeinheiten werden ein- und ausgangsseitig auf den BUS geschaltet. Dadurch ist der Einsatz der Speicher-STE steckplatzunabhängig.
Alle speicherspezifischen Adreß-, Daten- und Steuerleitungen des Busses sind durch Pufferschaltkreise mit Low-Power-Schottky-Eingängen von den Steuer- und Speicherschaltkreisen entkoppelt. Die Pufferschaltkreise der Datenleitungen arbeiten bidirektional und besitzen einen "Tri-state" Zustand. Die erzeugten Steuersignale werden über Open-Kollektor-Baustufen ausgesendet.
Die Geschwindigkeitssynchronisation zwischen ZRE und Speicher erfolgt durch eine "WAIT-Steuerung".
Das auf der entsprechenden STE erzeugte Quittungssignal $\overline{\text{RDY}}$ aktiv low wird gebildet, wenn bei einem gültigen Lese- oder Schreibaufruf die Adresse hardwaremäßig erkannt wurde.

2.3. Anschlußbedingungen

Signalpegel
low-Potential:
Eingänge - 1,0 V ... + 0,85 V
Ausgänge 0 V ... + 0,45 V
high-Potential:
Eingänge + 2,0 V ... + 5,5 V
Ausgänge + 2,4 V ... + 5,5 V

Signalbelastung
Alle von der Speicher-STE empfangenen Signale (Adreß- und Steuerbits, Dateneingänge) werden mit max. 0,25 mA belastet. Datenausgang ist belastbar mit 15 TTL-LE. Open-Kollektor-Ausgänge der STE treiben max. 10 TTL-LE (16 mA). Diese Ausgänge müssen mindestens einen Lastwiderstand besitzen (auch außerhalb der STE).

Speicher-STE-spezifische Signale:

| Signal | Beschreibung |
| :--- | :--- |
| Adresse | AB0 ... AB15, 16 Bit. AB0 ... AB9 adressieren die Speicherchips ($\hat{=}$ 1 K Bit), nachfolgende Bits entschlüsseln Adreßgruppen und die hochwertigen Bits wählen die STE aus. |
| Daten | DB0 ... DB7, 8 Bit. Lesen aus dem Speicher oder Schreiben in den Speicher wird gesteuert durch die Signale $\overline{\text{RD}}$ und $\overline{\text{WR}}$. |
| $\overline{\text{MREQ}}$ | Speicheranforderungssignal, es wirkt als Taktsignal für den Speicher ($\overline{\text{CE}}$ Eingänge der Speicherchips). |
| $\overline{\text{WR}}$ | Befehlssignal "Schreiben in den Speicher". Es steuert die Funktion "Lesen" oder "Schreiben" der Speicherchips über deren WE-Eingänge. |
| $\overline{\text{RD}}$ | Befehlssignal "Speicher lesen". Es bestimmt die Wirkungsrichtung der bidirektionalen Datenpuffer. |
| $\overline{\text{MEMDI}}$ | Speichersperrsignal |
| $\overline{\text{MEMDI1/2}}$ | $\overline{\text{MEMDI}}$ wird über den Systembus X1/B09 und die Signale $\overline{\text{MEMDI1/2}}$ werden über den Koppelbus geführt. Sie sind über Wickelbrücken bzw. Schalter zu programmieren. |

- Brücke X6:1 - X7:1 geschlossen - $\overline{\text{MEMDI}}$ wirksam
- Brücke X6:2 - X7:2 geschlossen - $\overline{\text{MEMDI1}}$ wirksam
- Brücke X6:3 - X7:3 geschlossen - $\overline{\text{MEMDI2}}$ wirksam

Das Sperrsignal schaltet die Datenausgangspuffer in den "Tri-state"-Zustand und sperrt die $\overline{\text{CE}}$-Eingänge der Speicher. Die Datenausgänge sind hochohmig und können von anderen externen Geräten benutzt werden ohne die entsprechende Speicher-STE zu belasten.
- Normalkonfiguration bei einer maximalen Speicherkapazität von 64 K Byte:
Brücke $\overline{\text{MEMDI}}$ geschlossen
Brücke $\overline{\text{MEMDI1/2}}$ offen
- Adreßerweiterung über 64 K Byte:
Brücke $\overline{\text{MEMDI}}$ offen
Brücke $\overline{\text{MEMDI1}}$ oder $\overline{\text{MEMDI2}}$ je nach gewünschter Programmierung geschlossen
Beachte: Zusatzverdrahtung auf dem Koppelbus und Zusatzelektronik erforderlich!

| Signal | Beschreibung |
| :--- | :--- |
| $\overline{\text{RFSH}}$ | Steuersignal zum Auffrischen dynamischer RAM-Speicher |
| TAKT, M1 | Systemtakt und Kennzeichen "Befehlslesezyklus" - sind Steuersignale zur Bildung des Signals $\overline{\text{WAIT}}$. |

Signale, die von der Speicher-STE gebildet werden:

| Signal | Beschreibung |
| :--- | :--- |
| Daten | DB0 ... DB7, 8 Bit. Die Funktionen "Speicher lesen" oder "Speicher schreiben" werden durch die Steuersignale $\overline{\text{WR}}$ (über WE-Eingänge der Speicherchips) und $\overline{\text{RD}}$ (Eingang DIEN der Datenausgangspuffer $\hat{=}$ Steuerung der Wirkungsrichtung) gesteuert. |
| $\overline{\text{WAIT}}$ | Das Signal $\overline{\text{WAIT}}$ aktiv low löst einen "WAIT"-Zyklus im Prozessor aus - während eines Befehlslesezyklus. Dieser Zyklus ist bei einer Speicherzugriffszeit $\geq$ 450 ns erforderlich. |
| $\overline{\text{RDY}}$ | $\overline{\text{RDY}}$ aktiv low, wenn auf der betreffenden STE der adressierte Speicherplatz hardwaremäßig vorhanden ist und zum Datenaustausch zur Verfügung steht. |

2.4. Bauelementebasis

Zur Anwendung kommen im wesentlichen folgende integrierte Schaltkreise:

| Realisierung DDR | RGW | SU | NSW | Allgemein-typ ESER | Bezeichnung | robotrontron-Nr. | Sachnummer |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| | MH 3205 | | 8205 | U 205 | schneller 1 aus 8-Decoder | SE05 | 001-0-174-070 |
| | MH 3212 | K 589/K12 | 8212 | U 212 | 8 Bit par. Ein- und Ausgaberegister | SE12 | 001-0-174-088 |
| | MH 3216 | K 589/K16 | 8216 | U 216 | 4 Bit bidirektionaler Bustreiber | SE16 | 001-0-174-071 |
| | | | SN75361 | | TTL zu MOS Treiber, 14-polig | PZ61 | |
| | | K 565RU1A | i2107B | XO7B | 4 K dyn. RAM (4096 x 1 Bit) | Q 250 | 001-0-174-075 |
| U 202 D | | | 2102A-4 | X2A4 | 1 K stat. RAM (1024 x 1 Bit) | Q 240 | 001-0-174-048 |
| U 555 | | | 2708 | 4708 | 1 KByte UV-löschb. PROM | Q 260 | 001-0-507-667 |
| | | | S 1902 | | 1 K stat. CMOS RAM (1024 x 1 Bit) | Q 270 | |
| D 100 | MH 7400 | K 155LA3 | SN7400 | T 100 | 2-Eing.-Nand Gatter, 4-fach | PS00 | 001-0-507-701 |
| D 103 | MH 7403 | | SN7403 | T 103 | 2-Eing.-Nand Gatter, offener Kollektor | PS03 | 001-0-507-702 |
| D 126 | | | TL7426 | T 126 | 2-Eing.-Nand Gatter, 4-fach, offener Kollektor | PS26 | 001-0-507-729 |
| D 174 | MH 7474 | K 555TM2 | SN7474 | T 174 | D-FF, 2-fach | PS74 | 001-0-507-712 |
| D 183 | UCYJM3 | K 155IM3 | SN7483 | T 183 | 4 Bit Volladder mit Übertrag | PS83 | 001-0-174-140 |
| D 186 | | K 555LP5 | SN7486 | T 186 | 2-Eing.-Exklusiv-Oder, 4-fach | PS86 | 001-0-174-164 |
| D 200 | | | SN74H00 | T 200 | 2-Eing.-Nand, 4-fach, schnell | PH00 | |
| D 204 | | | SN74H04 | T 204 | Inverter, 6-fach, schnell | PH04 | 001-0-507-733 |
| D 220 | | | | T 220 | 4-Eing.-Nand, 2-fach, schnell | PH20 | |
| D 400 | | K 158LA3 | | T 400 | 2-Eing.-Nand, 4-fach, low | | |

Die Wirkungsweise der aufgeführten Bauelemente ist aus der technischen Unterlage "Bauelementeübersicht" zu ersehen.
Die in der Tabelle aufgeführten Äquivalenztypen von Bauelementen verschiedener Hersteller sind die vom Stand April 1980 empfohlenen. Die Angaben sind nicht vollständig.

3. Operativspeicher OPS K 3520

3.1. Kurzcharakteristik

Der Schreib-Lese-Speicher (Operativspeicher) dient zur Speicherung aller variablen Daten während des Programmablaufs im Mikrorechner K 1520.
Er besteht aus einem 4 K Byte großen statischen nMOS-RAM-Halbleiterspeicher mit den zur Entkopplung, Auswahl und Ansteuerung erforderlichen bipolaren Schaltkreisen.

3.2. Technische Daten

| Parameter | Wert |
| :--- | :--- |
| Speicherkapazität: | 4 K Byte (Matrix von 4 x 8 Speicherchips) |
| Speicherschaltkreistyp: | Q 240 (1 K x 1 Bit; nMOS) |
| Zugriffszeit: | $\leq$ 530 ns |
| Betriebsarten: | "Lesen" oder "Schreiben" als abgeschlossene Zyklen in beliebiger Reihenfolge |
| Datenerhalt: | Gespeicherte Daten gehen bei Abschaltung der Betriebsspannung verloren. Prinzipiell ist ein Datenerhalt möglich, wenn im Ruhezustand des Speichers eine externe Spannung von außen über die Klemme 5 PG zugeführt wird. Sie muß $\geq$ 2 V sein. |
| Stromversorgung: | 5 V $\pm$ 5 %, $\leq$ 0,6 A für Steuerelektronik und Pufferschaltkreise |
| | 5 V $\pm$ 5 %, $\approx$ 1,1 A für Speicherschaltkreise |

3.3. Programmierung der Steckeinheit

3.3.1. Programmierfelder der Steckeinheit

Die Programmierfelder bestehen aus Wickelstiftpaaren, die wahlweise gebrückt werden können (auch als Schalter ausgeführt).

![[Pasted image 20260516143602.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt die Bestückungsseite der Leiterplatte OPS - K 3520 (Abb. 1). Eingezeichnet sind die Positionen der Wickelbrückenfelder X3 bis X11 sowie die Steckverbinder X1 und X2 am unteren Rand.*

Abb. 1

3.3.2. Adreßcodierung

Die 16 Adreßleitungen werden auf der Speichersteckeinheit wie folgt verwendet:

AB0 ... AB9 - interne Chipadressierung $\hat{=}$ 1 K Bit
AB10, AB11 - Auswahl einer der 4 1 K-Blöcke auf der STE
AB12 ... AB15 - Auswahl der STE entsprechend der vorgegebenen Adressenzuordnung

Programmiervorschrift

Über 4 Codierbrücken X8:1 ... 4, X9:1 ... 4 wird dem Speichermodul ein wählbarer zusammenhängender Adreßbereich von 4 K-Adressen zugeordnet.
Die programmierte Auswahleinrichtung erhält in binärer Verschlüsselung die Anfangsadresse des gewünschten Adreßbereiches. Es ergibt sich eine durch 4 K teilbare Adresse.

Tabelle zur Codierung der Anfangsadresse

| Adreßbereich | X8:4 - X9:4 | X8:3 - X9:3 | X8:2 - X9:2 | X8:1 - X9:1 |
| :--- | :---: | :---: | :---: | :---: |
| 0000 - 0FFF | - | - | - | - |
| 1000 - 1FFF | - | - | - | Brücke |
| 2000 - 2FFF | - | - | Brücke | - |
| 3000 - 3FFF | - | - | Brücke | Brücke |
| 4000 - 4FFF | - | Brücke | - | - |
| ... | | | | |
| F000 - FFFF | Brücke | Brücke | Brücke | Brücke |
| den Brücken sind folgende Wertigkeiten zugeordnet: | $\hat{=}$ 32 K | $\hat{=}$ 16 K | $\hat{=}$ 8 K | $\hat{=}$ 4 K |

Beispielsweise entspricht die Codierung aller 4 Brücken einer Speicheranfangsadresse dieser STE von
32 K + 16 K + 8 K + 4 K = 60 K,
d. h. die Anfangsadresse dieser STE ist das 60. K $\hat{=}$ F000_H.

3.3.3. Auswahl des Speichersperrsignals MEMDI

| Signal | Wickelbrücken X6:1 - X7:1 | Wickelbrücken X6:2 - X7:2 | Wickelbrücken X6:3 - X7:3 |
| :--- | :---: | :---: | :---: |
| $\overline{\text{MEMDI}}$ (X1:B09) | Brücke | - | - |
| $\overline{\text{MEMDI1}}$ (X2:A21) | - | Brücke | - |
| $\overline{\text{MEMDI2}}$ (X2:B21) | - | - | Brücke |

Bei Bestückung mit Schaltern entspricht "Brücke" dem geschlossenen Schalter.

3.3.4. "WAIT"-Bildung

Von der Zugriffszeit der aufgerufenen Speicherschaltkreise hängt es ab, ob während des Befehlszyklusses im K 2526 eine Zeitverlängerung durch das Steuersignal $\overline{\text{WAIT}}$ vorgenommen werden muß.
Für den allgemeinen Anwendungsfall gilt folgende Codierung:
- "WAIT"-Bildung im M1-Zyklus: Brücke X10:3 - X11:3 offen
- Unterdrückung der "WAIT"-Bildung: Brücke X10:3 - X11:3 geschlossen

3.3.5. Betriebsspannungszuführung 5 PG

Im Normalfall werden die RAM-Bausteine über die Betriebsspannung 5 PG versorgt. In Sonderfällen, wo die Anschlüsse 5 PG auf dem Bus nicht belegt sind, kann die Spannung 5 PG steckeinheitenseitig durch Brückung der Wickelstifte X10:2 - X11:2 mit 5 P verbunden werden.

3.4. Funktionsbeschreibung

3.4.1. Verwendungszweck

Der OPS K 3520 wird im Erzeugnisprogramm "Dezentrale Datentechnik" als Operativspeicher (statischer Schreib-Lese-Speicher) eingesetzt.

3.4.2. Funktion

Auf der Steckeinheit befinden sich die Funktionsgruppen:
- Speicher-Matrix
- Ein- und Ausgabepuffer
- Auswahlelektronik
- Steuerelektronik

Die Wirkungsweise der Schaltung ist im Blockschaltbild Abb. 2 dargestellt.
Die Speichermatrix besteht aus 4 Gruppen zu je 8 Speicherchips Q 240. Jedes Chip enthält 1 K Bit. Eine Gruppe von 8 Chips bildet einen Speicherbereich von 1 K Byte.
Jeder der 4 vorhandenen Blöcke wird durch ein gesondertes $\overline{\text{CE}}$-Signal aktiviert.
Alle 10 Adreßeingänge der Speicherchips und der Steuereingang $\overline{\text{WE}}$ (Schreib-Lese-Steuerung) sind miteinander verbunden und werden über Schottky-TTL-Pufferschaltkreise SE12 (A2/3/4) gespeist.
Bei den Datenausgangs- und -eingangsleitungen sind jeweils die gleichen Bits der 4 Speicherblöcke parallel geschaltet und mit bidirektional arbeitenden Datenpufferschaltkreisen SE16 (A5/14) verbunden, die die Verbindung mit dem Systembus herstellen. Ist die STE nicht angesteuert, sind die Datenpuffer hochohmig und belasten nicht den Systembus.
Die ebenfalls über SE12 verstärkten Adreßsignale AB10 und AB11 (A3) werden im 1 aus 8-Decoder SE05 (A9) decodiert und bilden eins der 4 Speicheransteuersignale $\overline{\text{CE1}}$ ... $\overline{\text{CE4}}$, wenn gleichzeitig $\overline{\text{MREQ}}$ = low anliegt, $\overline{\text{MEMDI}}$ = high ist und die Adreßsignale AB12 ... AB15 entsprechend der Anfangscodierung der Wickelbrücken ausgewählt wurde. Die Steckeinheit wird angesprochen, wenn alle 4 Eingänge der Auswerteschaltung, die den umgeschlüsselten Adreßbits AB12 ... AB15 entsprechen, den Pegel high besitzen. A7/8 = 0 ist das Freigabesignal für den 1 aus 8-Decoder A9 und gibt das 4er-Nand A7/6 frei, das $\overline{\text{CS}}$ der Ausgabepuffer bildet.
Die Adreßdecodierung erfolgt durch den Exklusiv-Oder-Baustein PS86 (A6) in Verbindung mit der programmierten Anfangsadressierung durch das Programmierfeld X8-X9. Ein geschlossener Schalter bzw. eine Brücke schaltet das Potential low auf den Eingang des PS86.

![[Pasted image 20260516143727.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt das Blockschaltbild K 3520 (Abb. 2). Zu sehen sind Funktionsblöcke wie Eingangspuffer für Adresse und Steuersignale, Blockauswahl, Steuerelektronik und Adressenumschlüssler, prog. STE-Anfangsadresse, WAIT-Bildung, Auswahleinrichtung, Speicheranordnung (4x1k Byte) und Datenpuffer. Die Signale wie AB0...15, WR, RD, MREQ, RFSH, MEMDI, M1, TAKT sowie Ausgänge für Daten DB0...7, RDY und WAIT sind eingezeichnet.*

Abb. 2
Blockschaltbild K 3520

Das Freigabesignal, was die Bildung der $\overline{\text{CE1}}$ ... $\overline{\text{CE4}}$-Signale freigibt, aktiviert also gleichzeitig auch die Datenausgangsverstärker zum Datentransfer über den Systembus.
$\overline{\text{RD}}$ steuert dabei die Wirkungsrichtung dieser Puffer. Über A7/6 wird auch das Steuersignal $\overline{\text{RDY}}$ gebildet und die Blockierung der "WAIT"-Bildung-Funktionsgruppe aufgehoben, wenn die Brücke X10:3 - X11:3 nicht gewickelt ist. $\overline{\text{WAIT}}$ wird von einer Schiebekette von 2 D-Flip-Flops durch die Signale $\overline{\text{M1}}$ und TAKT gebildet. $\overline{\text{WAIT}}$ und $\overline{\text{RDY}}$ werden über Open-Kollektor-Baustufen auf den Systembus geführt.
Die Signale zur Durchschaltung der Prioritätskette für die Interruptbearbeitung und für die Busherrschaft im Systembus $\overline{\text{IEI}}$, $\overline{\text{IEO}}$ und $\overline{\text{BAI}}$, $\overline{\text{BAO}}$ sowie $\overline{\text{IEIT}}$, $\overline{\text{IEOT}}$ sind auf der Speichersteckeinheit jeweils miteinander gebrückt.
Um bei Netzausfall einen Datenerhalt der Speicherschaltkreise zu ermöglichen (durch eine externe Stützung der Betriebsspannung), ist die Stromversorgung der STE in zwei Kreise aufgeteilt.
Die Speicherchips werden über die Klemmen 5 PG gespeist. Die gespeicherten Informationen bleiben erhalten, wenn im Ruhezustand des Speichers die Spannung 5 PG auf eine Schlafspannung von minimal 2 V abgesenkt wird. Die Spannung 5 P für die Puffer-, Auswahl- und Steuerschaltkreise kann dabei abgeschaltet sein.
Damit beim Zu- und Abschaltvorgang der Spannung 5 PG keine undefinierten Ansteuerbedingungen am Speicher wirksam werden können, die zum Datenverlust oder Datenverfälschen führen, werden die $\overline{\text{CE}}$-Signale konjunktiv mit einem internen Speichersperrsignal verknüpft. Dieses Sperrsignal, gebildet in der Komperatorschaltung (V5/V11), wird low, sobald die Betriebsspannung 5 P die untere Toleranzgrenze unterschreitet. Damit ist sichergestellt, daß der Treiber PS26 (A11) dann kein aktivierendes Ansteuersignal für die Speicherchips aussenden kann.
Die Brücke X12-X13 muß geschlossen sein.
Über die mit der Spannung 5 PG verbundenen Arbeitswiderstände der Open-Kollektor-Treiberbaustufen wird auch im Schlafzustand der erforderliche high-Pegel am $\overline{\text{CE}}$-Eingang der Speicherchips aufrechterhalten.
Stütz- und Siebkondensatoren in der Leitungsführung der Betriebsspannungen 5 P und 5 PG verhindern kurz- und langfristige Störungen.

4. Operativspeicher OPS K 3521

4.1. Kurzcharakteristik

Im Schreib-Lese-Speicher (Operativspeicher) OPS K 3521 werden während des Programmablaufs im Mikrorechner K 1520 variable Daten gespeichert. Im Gegensatz zum OPS K 3520 bleiben die Daten auch nach einer zwischenzeitlichen Programmunterbrechung durch Netzabschaltung am Rechner für eine vorgegebene Haltezeit für die weitere Programmabarbeitung erhalten.
Der OPS K 3521 besteht aus dem STE-Typ 012-7012 mit indirektem Steckverbinder. Er ist ein 4 K Byte statischer Halbleiterspeicher, bestückt mit CMOS-Bausteinen und den zur Entkopplung, Auswahl und Ansteuerung erforderlichen bipolaren Schaltkreisen.
Zusätzlich befindet sich auf der STE die zur Stützung der Speicherbetriebsspannung gehörende Logik einschließlich der Stützspannungsquelle.

4.2. Technische Daten

| Parameter | Wert |
| :--- | :--- |
| Speicherkapazität: | 4 K Byte |
| Speicherschaltkreistyp: | Q 270 bei STE 012-7012 (pinkompatibel) |
| | Q 271 bei STE 012-7131 (nicht pinkompatibel) |
| | 1 K x 1 Bit; CMOS |
| Zugriffszeit: | $\leq$ 530 ns |
| Betriebsart: | "Lesen" und "Schreiben" als abgeschlossene Zyklen in beliebiger Reihenfolge. |
| Datenerhalt: | Durch gepufferte NK-Akkumulatoren, die sich auf der STE befinden, wird bei Netzausfall die Betriebsspannung für die Speicherschaltkreise gestützt. |
| | Datenhaltezeit $\geq$ 200 Stunden |
| Stützspannungsquelle: | Reihenschaltung von 3 NK-Knopfzellen mit je 1,2 V und 0,225 Ah Typ KBL 0,225 vom VEB GLZ nach TGL 22807 |
| Stromversorgung: | 5 P = 5 V $\pm$ 5 %, $\leq$ 0,6 A für Steuerelektronik und Pufferschaltkreise |
| | 5 PG = 5 V $\pm$ 5 %, $\leq$ 40 mA für Speicherschaltkreise und Akkuladestrom |
| | 12 NR = 26 V +12%/-20%, $\leq$ 10 mA Wechselspannung zur Erzeugung der Hilfsspannung für die Batteriespannungsüberwachungsschaltung. Sie muß mind. 200 ms vor dem Zuschalten der Systemspannungen 5 P und 5 PG anliegen. |
| Stützspannungsüberwachung: | Vor dem Zuschalten der Systemspannungen bewertet eine Kontrollschaltung den Spannungszustand der Batterie und speichert das Ergebnis ab. |
| | Im Betriebszustand kann die Aussage: |
| | - Stützspannung war größer als die minimale |
| | - Stützspannung war gleich Schlafspannung der |
| | - Stützspannung war kleiner Speicherschaltkreise |
| | angezeigt werden (V9) oder logisch auf den Bus ausgewertet werden - über das Steuersignal SUE. LED-Anzeige ein bzw. $\overline{\text{SUE}}$ = low $\hat{=}$ der Aussage: Datenerhalt war gesichert. |

4.3. Einsatzbedingungen für den Stütz-Akkumulator

Die NK-Knopfzellen werden in Einzelgehäusen gehalten, die an der Griffseite der STE angeordnet sind. Das Wechseln der Knopfzellen ist im gestreckten Zustand der STE möglich und kann auch im Betriebszustand des Rechners erfolgen.
Während der Lagerung und des Transportes sind die Knopfzellen auf der STE nicht zu bestücken. Es wird davon ausgegangen, daß bei einer Neubestückung grundsätzlich geladene Zellen zum Einsatz kommen. Im Betriebszustand werden die Zellen mit einem mittleren Ladestrom von 5 mA geladen (ständige Pufferung). Dieser Strom führt auch bei Grenztemperaturen nicht zu Überladungserscheinungen an den Zellen. Der maximal vorkommende Entladestrom bei abgeschaltetem Rechner beträgt 500 µA. Der reale Wert hängt von den konkreten Typen und der Qualität der CMOS-Schaltkreise ab und kann zwischen wenigen µA bis zu 500 µA bei 5 V Betriebsspannung streuen. Aus diesen Vorgaben ergibt sich als Richtwert, daß der Ladezustand der Zellen erhalten wird, wenn die Ladezeit allgemein ein Siebentel der Entladezeit beträgt.
Die Lebensdauer der Akkus wird durch die nutzbare mAh-Kapazität der Zellen bestimmt. Angaben dazu sind in der Einsatzvorschrift des Akkuherstellers und in der TGL 22807 festgelegt. Da die Einsatztemperatur im K 1520 bis zu 60 °C betragen kann, entstehen hohe Belastungen für die NK-Elemente. Temperaturen über 35 °C bewirken zunehmende chemische Umsetzungen der aktiven Masse, die die Kapazität und damit die Lebensdauer erheblich reduzieren. Es gelten daher laut Qualitätsvereinbarung folgende zusätzliche Einsatzbedingungen:
Erfolgt der Betrieb der Zellen im Temperaturbereich bis 40 °C bei zusätzlich insgesamt einer Woche Spitzentemperaturen bis 60 °C, so ergibt sich eine garantierte Lebensdauer der Zellen von einem Jahr, wobei die Lebensdauergrenze bei einer nutzbaren Kapazität von 100 mAh definiert ist. Besteht die Grenztemperatur von 60 °C über einen langen Zeitraum, so verringert sich die definierte Lebensdauer auf 3 Monate.
Aus diesen Garantiewerten ist abzuleiten, daß die Zellen bei Erreichen der angegebenen 100 mAh-Grenze auszuwechseln sind, wenn eine Datenhaltezeit von $\geq$ 200 Stunden sicher gewährleistet werden muß.
Reduziert man die Anforderungen an die langen Datenhaltezeiten, so lassen sich die Zellen noch nutzen, wenn die Kapazität von 100 mAh unterschritten ist. Die reale thermische Belastung über die Zeit ist aber in der Praxis schwer erfaßbar, so daß der Kapazitätszustand der Zellen nicht exakt vorhersagbar ist. Ein ökonomischer Einsatz der Zellen wird ermöglicht, wenn im Betrieb unter konkreten Einsatzbedingungen im Endprodukt praktische Werte für den Akkutausch abgeleitet werden.
Als Kriterium für die nutzbare Grenzkapazität und den dabei erreichbaren Ladezustand kann die Anzeige der Batteriespannungsüberwachungsschaltung genutzt werden. Wird im Zustand des Datenerhalts die Batteriespannung auf der STE wiederholt gemessen und sinkt bei Einhaltung normaler Lade- und Entladezyklen unter 3,2 V, dann sind die Zellen auszutauschen.
Neben den zusätzlichen, bereits erwähnten, Qualitätsvereinbarungen bei Temperaturen über 35 °C, gelten die Festlegungen der TGL 22807 und die Behandlungsvorschrift des Akkuherstellers über Lagerung und Einsatz der Zellen.
Ist im Rechner eine Totalentladung von Zellen aufgetreten, sind diese außerhalb des Rechners mittels Ladegerät nach Vorschrift des Herstellers zu laden. Der Ladezustand bei der Lagerung von Zellen ist durch regelmäßige Erhaltungsladungen zu sichern. Eine Ladung von vollständig entladenen Zellen auf der STE mit dem geringen Strom von ca. 5 mA führt nicht zur vollen Ausnutzung der Nennkapazität der Zellen.
Eine Lagerung von entladenen Zellen ist bis zu einer Lagerzeit von einem Jahr ohne Einschränkung der elektrischen Parameter möglich, wenn die Umgebungstemperatur 20 °C $\pm$ 5 °C und die relative Luftfeuchte 60 % $\pm$ 15 % eingehalten werden. Danach müssen sie unbedingt mittels Ladegerät 2 bis 3 mal mit Nennströmen geladen und entladen werden, bevor sie im Rechner eingesetzt werden können.
Knopfzellen anderer Hersteller mit vergleichbaren elektrischen und konstruktiven Daten können eingesetzt werden, wenn die Behandlungsvorschriften dieser Erzeugnisse entsprechende Beachtung finden. Es können dabei Einschränkungen in den technischen Daten der Speicher-STE bezüglich Einsatzbedingungen und Datenhaltezeit ergeben.

4.4. Programmierung der Steckeinheit

Die Programmierung erfolgt analog der Speichersteckeinheit OPS K 3520 (siehe Punkt 3.3.).

4.5. Funktionsbeschreibung

4.5.1. Verwendung

Der OPS K 3521 wird im Erzeugnisprogramm "Dezentrale Datentechnik" als Operativspeicher (statischer Schreib-Lese-Speicher) eingesetzt, dessen Daten auch bei Systemspannungsabschaltung über eine gewisse Haltezeit gesichert werden.

4.5.2. Funktion

Die Grundschaltung des OPS K 3521 entspricht der des OPS K 3520 (siehe Punkt 3.4.2.).
Er ist nur nicht für eine externe Stützung der Betriebsspannung für die Speicherschaltkreise vorgesehen.
Da unterschiedliche CMOS-RAM-Schaltkreise zum Einsatz kommen, gibt es 4 STE-Typen mit gleicher logischer Schaltung:
- STE 012-7012, 012-7017, RAM pinkompatibel zum U 202
- STE 012-7131, 012-7136, RAM nicht pinkompatibel zum U 202

Die STE beinhaltet die Funktionsgruppen:
- Speichermatrix
- Ein- und Ausgabepuffer
- Auswahl- und Steuerelektronik

Sie sind mit denen des OPS K 3520 weitgehend identisch und werden hier nicht beschrieben. Aus dem Blockschaltbild zum OPS K 3521 sind die für den CMOS-Speicher zusätzlichen Schaltteile ersichtlich.
Im Betriebszustand wird die Speichermatrix von 5 PG über Pufferschaltung mit der Betriebsspannung 5 PGI versorgt. Eine Betriebsspannung von 5 PGI ist auch bei den RAM-Bausteinen erforderlich. Diese Spannung bewirkt die Ladung der Stützbatterien G1 ... G3, wobei der Vorwiderstand (R26) den Ladestrom auf ca. 5 mA begrenzt.
Die Pufferschaltung, bestehend aus den Transistorschaltungen V6, V10, blockt bei Ausfall der Systemspannungen (5 P und 5 PG) die Batteriespannung vom Eingang ab, damit keine Ausgleichströme fließen können.
Der Kondensator C4 verhindert beim Zu- und Abschalten der Systemspannungen größere Spannungseinbrüche (dynamischer Stromanstieg über den RAM-Bausteinen beim Zu- und Abschalten). Gleichzeitig werden Prellerscheinungen bei der Kontaktierung der Knopfzellen unterdrückt (C5:1 ... C5:16). Die Spannung der Stützakkus kann im vollgeladenen Zustand ca. 4,1 V betragen. Wird die Spannung < 3 V, liegt der Entladezustand vor. Der Nennwert der Batteriespannung beträgt ca. 3,6 V.
Die um den Spannungsabfall über dem Vorwiderstand R26 verringerte Spannung 5 PGI liegt an den Speicherschaltkreisen an. Es ist die Schlafspannung der CMOS-RAM-Bausteine (bei Q 271 < 2 V). Sie bewirkt, daß Entladeströme von $\leq$ 500 µA fließen.
Bei kleineren Schlafspannungen als im Datenblatt angegeben, tritt ein Datenverlust im Speicher ein.
Die nachfolgende Batteriespannungsüberwachungsschaltung bewertet den Pegel der Spannung 5 PGI, um den Datenerhalt zu sichern, bevor die eigentlichen Systemspannungen 5 P und 5 PG zugeschaltet werden.
Das Prinzip dieser Schaltung ist aus dem Blockschaltbild ersichtlich.
Am Widerstand R19 wird die erforderliche Referenzspannung eingestellt. Am Eingang 05 des Operationsverstärkers MAA 741 liegt die zu überwachende Batteriespannung des Akkus. Der A16 ist so dimensioniert, daß bei einem Absinken der Batteriespannung unter 2 V die Leuchtdiode V9 verlischt (A16/10 wird low). Gleichzeitig geht das Steuersignal $\overline{\text{SUE}}$ auf low A15/03; A12/11 und ist am Koppelbus X2:B22 auswertbar.
Es gilt:
- $\overline{\text{SUE}}$ = high
LED-Anzeige V9 leuchtet: 5 PGI $\geq$ minimale Schlafspannung
Datenerhalt ist gesichert
- $\overline{\text{SUE}}$ = low
LED-Anzeige V9 leuchtet nicht: 5 PGI < minimale Schlafspannung bzw. Akkus sind nicht bestückt
Datenerhalt ist nicht garantiert

Der FF-Speicherkreis ist so dimensioniert, daß eine Abspeicherung der Vergleicheraussage unmittelbar nach der Einschaltflanke der Hilfsspannung 5 PH erfolgt. Zu diesem Zeitpunkt dürfen die Systemspannungen 5 P und 5 PG nicht anliegen, da sie zu einer Beeinflussung der Stützspannung führen. Ein auftretendes Signal "low" an der Vergleicherbaustufe stellt jederzeit das FF in den Fehlerzustand.
Die für die Funktion dieser Schaltung erforderlichen Hilfsspannungen 5 PH und 5 NH werden durch eine schnelle Gleichrichtung aus einer voreilenden Wechselspannung von 26 V_~ gewonnen. Für eine sichere Bewertung der Stützspannung muß die Wechselspannung mind. 200 ms vor den Systemspannungen anliegen.

![[Pasted image 20260516143813.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt das Blockschaltbild K 3521 (Abb. 3). Dargestellt ist die Schaltung zur Pufferspeisung und Batteriestützung. Die Systemspannung 5PG gelangt über eine Pufferschaltung zu 5PGI, die die Speicheranordnung und eine Hilfsspannungserzeugung speist. Eine Stützbatterie (3x KBL 0,225) ist über einen Ladewiderstand R26 an 5PGI angebunden. Ein Spannungsvergleicher mit Referenzspannung (R19) und Speicher-FF steuert über einen Verstärker die LED V9 und das Signal SUE (X2:B22).*

Abb. 3
Blockschaltbild K 3521

5. Operativspeicher OPS K 3525

5.1. Kurzcharakteristik

Mit dem Schreib-Lese-Speicher (Operativspeicher) OPS K 3525 werden variable Daten während des Programmablaufs im Mikrorechner K 1520 gespeichert.
Aufgrund des hohen Integrationsgrades der verwendeten Speicherschaltkreise Q 250 können räumlich kleine und billige Speicher aufgebaut werden.
Verwendung findet der Steckeinheitentyp 012-7121 mit indirektem Steckverbinder. Auf der STE befindet sich ein 16 K Byte großer dynamischer Halbleiterspeicher mit 4 K x 1 Bit-nMOS-Speicherschaltkreisen.
Außerdem enthält die STE folgende Funktionsgruppen:
- Ein- und Ausgabepuffer
- Auswahlelektronik
- Ansteuerelektronik

5.2. Technische Daten

| Parameter | Wert |
| :--- | :--- |
| Speicherkapazität: | 16 K Byte (Matrix von 4 x 8 Speicherchips vorgesehene Abrüstvarianten: 8 K und 12 K Byte) |
| Speicherschaltkreistyp: | Q 250 (4 K x 1 Bit; nMOS) |
| Zugriffszeit: | $\leq$ 350 ns |
| Betriebsarten: | "Lesen" oder "Schreiben" als abgeschlossene Zyklen in beliebiger Reihenfolge. Eine statische Ansteuerung des Speichers über einen Zeitraum > 4 µs ist nicht zulässig. |
| Datenerhalt: | Bei Abschaltung der Betriebsspannung geht die Information verloren. Alle $\leq$ 2 ms muß durch eine spezielle Refresh-Steuerung die Speicherzelle regeneriert werden. Diese Steuerung übernimmt die ZVE des $\overline{\text{MR}}$ K 2526. |
| Stromversorgung: | 5 P: + 5 V $\pm$ 5 %, $\leq$ 0,6 A |
| | 12 P: + 12 V $\pm$ 5 %, $\leq$ 0,55 A |
| | 5 N: - 5 V $\pm$ 5 %, $\leq$ 50 µA |
| | Der Strombedarf bei 12 P ist sehr stark von der Betriebsart des Speichers abhängig. Der o. g. Wert ergibt sich im Halt-Zustand des $\overline{\text{MR}}$. Für das Speicherbauelement ist vom Hersteller vorgeschrieben, daß die Spannung an jedem Pin, bezogen auf die Betriebsspannung 5 N, stets positiver als - 0,3 V bleiben muß. Das ist nur gewährleistet, wenn bei Anlegen der Spannungen 12 P und 5 P unbedingt die 5 N am Baustein wirksam ist. Aufgrund dieser Vorgaben wird für die STE eine Reihenfolge der Spannungszu- und -abschaltung: 5 N - 5 P - 12 P und eine Ausfallüberwachung bei 12 P und 5 P vorgeschrieben. |

5.3. Programmierung der Steckeinheit

5.3.1. Programmierfelder der STE

Die Programmierfelder können als Wickelstiftpaare oder zum Teil auch als Mikroschalter ausgeführt sein. Entsprechend der Zuordnung der Wickelstiftpaare müssen Brücken gewickelt werden. Die entsprechende Belegung auf der STE ist aus Abb. 4 ersichtlich.

![[Pasted image 20260516143921.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt die Bestückungsseite OPS - K 3525 (Abb. 4). Markiert sind die Programmierfelder X1 bis X12 sowie die Steckverbinder X1 und X2.*

Abb. 4

5.3.2. Adressenzuordnung

Die 16 Adreßsignale werden wie folgt verwendet:
AB0 ... AB11 - interne Chipadressierung
AB12, AB13 - Auswahl einer der vier 4 K-Blöcke auf der STE in Abhängigkeit von der Adressenzuordnung der STE
AB14, AB15 - Auswahl der STE in Abhängigkeit der programmierten Adressierung durch X8:3,4 - X9:3,4 (Anfangsadresse des Speicherbereiches der STE)

Festlegung der Anfangsadresse der STE:
Über die 4 Wickelbrücken (bzw. Schalter) X8:1 ... X8:4; X9:1 ... X9:4 wird der STE OPS K 3525 ein wählbarer zusammenhängender Adreßbereich von 16 K Byte zugeordnet. Mit dem Programmierfeld kann in binärer Verschlüsselung die Anfangsadresse des gewünschten Adreßbereiches festgelegt werden. Diese Adresse ist ein ganzzahliges Vielfaches von 4 K.

| Adreßbereich | X8:4 - X9:4 | X8:3 - X9:3 | X8:2 - X9:2 | X8:1 - X9:1 |
| :--- | :---: | :---: | :---: | :---: |
| 0000 - 3FFF | - | - | - | - |
| 1000 - 4FFF | - | - | - | Brücke |
| 2000 - 5FFF | - | - | Brücke | - |
| 3000 - 6FFF | - | - | Brücke | Brücke |
| 4000 - 7FFF | - | Brücke | - | - |
| ... | | | | |
| C000 - FFFF | Brücke | Brücke | - | - |
| Adressenzuordnung bei beschalteter Brücke | $\hat{=}$ 32 K | $\hat{=}$ 16 K | $\hat{=}$ 8 K | $\hat{=}$ 4 K |

5.3.3. Auswahl des Speichersperrsignals MEMDI

| Signal | Wickelbrücken X6:1 - X7:1 | Wickelbrücken X6:2 - X7:2 | Wickelbrücken X6:3 - X7:3 |
| :--- | :---: | :---: | :---: |
| $\overline{\text{MEMDI}}$ (X1:B09) | Brücke | - | - |
| $\overline{\text{MEMDI1}}$ (X2:A21) | - | Brücke | - |
| $\overline{\text{MEMDI2}}$ (X2:B21) | - | - | Brücke |

5.3.4. Bildung von "WAIT"

Von der Zugriffszeit der aufgerufenen Speicherschaltkreise hängt es ab, ob während des Befehlszyklusses im $\overline{\text{MR}}$ K 2526 ein "WAIT"-Zyklus durch das Steuersignal $\overline{\text{WAIT}}$ = 0 angefordert werden muß.
Für den allgemeinen Anwendungsfall kann die Einstellung wie folgt vorgenommen werden:
- Bildung von "WAIT" im M1-Zyklus:
Brücke X10-X11 geschlossen
Brücke X11-X12 offen
- keine "WAIT"-Bildung:
Brücke X11-X12 geschlossen
Brücke X10-X11 offen

5.4. Funktionsbeschreibung

5.4.1. Verwendungszweck

Der OPS K 3525 wird im Erzeugnisprogramm "Dezentrale Datentechnik" als Operativspeicher (dynamischer Schreib-Lese-Speicher) eingesetzt. Mit Hilfe der verwendeten Speicherbausteine sind billigere und kapazitätsmäßig große Operativspeicher im Mikrorechner realisierbar.

5.4.2. Funktion

Auf der Speichersteckeinheit befinden sich folgende Funktionsgruppen:
- Schreib- und Leseansteuerung
- Auswahlelektronik
- Ein- und Ausgabepuffer

Eine eigene Regenerierungssteuerung, die bei Verwendung von dynamischen RAM-Schaltkreisen erforderlich ist, ist auf der Speichersteckeinheit nicht vorhanden.
Das Auffrischen der Speicherinhalte muß durch eine geeignete "Refresh"-Ansteuerung von außen erfolgen; über die Adreßleitungen AB0 ... AB6 und das Signal $\overline{\text{RFSH}}$ aktiv low (über den Bus). Die Wirkungsweise der Schaltung geht aus dem Blockschaltbild (Abb. 5) hervor.
Der Speicher besteht aus 4 x 8 Speicherbausteinen Q 250, die eine Kapazität von 4 x 4 K Byte $\hat{=}$ 16 K Byte ergeben. Die Adreßsignale AB0 ... AB11 und das Schreibsteuersignal $\overline{\text{WR}}$ sind durch Schottky-TTL-Pufferschaltkreise SE12 (A3/4/2) vom Bus entkoppelt. Die Signale liegen parallel an allen Speicherbausteinen.
Die Adreßleitungen wählen beim Aufruf Speicherzugriff die entsprechend adressierte Speicherzelle (8 Bit Aufrufbreite) zum Beschreiben oder Lesen aus oder sie adressieren beim Auffrischaufrunf eine Zeile von 64 Speicherzellen in jedem Baustein. Bei diesem "RFSH"-Aufruf werden alle 4 Blöcke (4 x 4 K Byte) über den $\overline{\text{CE}}$-Treiber A5/6 (2-fach TTL-MOS-Treiber) gleichzeitig angesteuert.
Die Auswahl der 4 Speicherblöcke beim Schreib-/Leseaufruf erfolgt über einen 1 aus 8-Decoder SE05 (A10) durch Bildung $\overline{\text{CE1}}$ ... $\overline{\text{CE4}}$-Signale in Abhängigkeit von den umgeschlüsselten Adreßbits AB12K und AB13K.
Die Ansteuerbedingungen sind:
- Schreib-/Leseaufruf: $\overline{\text{MREQ}}$ . $\overline{\text{MEMDI}}$ . $\overline{\text{RFSH}}$
- Auffrischaufrunf: $\overline{\text{RFSH}}$ . $\overline{\text{MREQ}}$

![[Pasted image 20260516143953.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt das Blockschaltbild K 3525 (Abb. 5). Es enthält Eingangspuffer für Adresse und Steuersignale, Programm-STE-Adr., Adressen-umschlüssler, Blockauswahl, Treiber, Speicherranordnung (4x4k Byte), Steuerelektronik, Datenpuffer, WAIT-Bildung und Auswahleinrichtung. Signale wie AB0...15, WR, RD, MREQ, RFSH, MEMDI, M1, TAKT sowie Ausgänge für Daten DB0...7, RDY und WAIT sind dargestellt.*

Abb. 5
Blockschaltbild K 3525

Während des Auffrischzyklus werden über A8/12 durch das Signal $\overline{\text{RFSH}}$ = low die $\overline{\text{CE}}$-Eingänge der Speicherbausteine auf "high" geschaltet. Damit bleiben die Datenausgänge hochohmig und es tritt ausgangsseitig keine Beeinflussung durch die ausgewählte STE auf.
Über den 1 aus 8-Decoder A10 und dem speziellen $\overline{\text{CE}}$-Treiber P361 A5/6 werden die entsprechenden Blöcke auf der Speichermatrix angewählt. Sobald ein $\overline{\text{CE}}$-Signal im Schreib- bzw. Leseaufruf gebildet wird, werden auch die bidirektional arbeitenden Datentreiber SE16 A7 und A11 aktiviert ($\overline{\text{CS}}$ A7/A11 über A9-A8/02).
Das Steuersignal $\overline{\text{RD}}$ steuert dabei die Datenflußrichtung dieser Datentreiber. Beim Lesen ist $\overline{\text{RD}}$ = low und die Daten sind vom Speicher auf den Bus geschaltet.
Da zwischen Datenein- und Datenausgang der Speicherelemente ein Polaritätswechsel vorliegt, müssen die Daten vor dem Einschreiben negiert werden (A8, A12).
Gleichzeitig mit der Bildung des Signals $\overline{\text{CS}}$ für die Datentreiber wird über die Open-Kollektor-Baustufe A15 das Signal $\overline{\text{RDY}}$ = low gebildet und die "WAIT"-Bildung am Eingang R des FF A14 freigegeben, sofern die Brücke X11-X12 nicht geschlossen ist (vergleiche Punkt 2.3.4.).
Bei Speicherelementen mit hoher Zugriffszeit kann beim Befehlslesen somit eine Zyklusverlängerung durch einen "WAIT"-Takt eingeschoben werden.
Gebildet wird $\overline{\text{WAIT}}$ über die beiden FF A14, gesteuert durch die Signale TAKT und $\overline{\text{M1}}$.
Durch diese Schiebekette ist das Signal $\overline{\text{WAIT}}$ solange low, wie zur Einblendung eines TW-Taktes durch die ZRE nötig ist. Durch eine Open-Kollektor-Stufe A15/06 ist das Signal auf den Bus geschaltet.
Die Adreßbits AB12 ... AB15, die auf der STE die Blockauswahl vornahmen, werden in Abhängigkeit der Programmierung der Anfangsadressierung durch die Brücken X9:1 ... X9:4; X10:1 ... X10:4 zur internen Adresse AB12K ... AB15K durch den Adderbaustein A13 umgerechnet. Die Funktionsweise dieses Adderbausteins T 183 ist aus Punkt 6.4.2.2. zu ersehen.
Das Freigabesignal für den 1 aus 8-Decoder A10 wird nur gebildet, wenn die Adreßbits AB14K und AB15K 0-Potential besitzen. Dann adressieren die Bit AB12K und AB13K die Speicherblöcke der STE.

Die Prioritätsketten $\overline{\text{BAI}}$; $\overline{\text{BAO}}$
$\overline{\text{IEI}}$; $\overline{\text{IEO}}$
$\overline{\text{IEIT}}$; $\overline{\text{IEOT}}$ sind auf der Steckeinheit nur gebrückt.
Kurz- und langseitige Störungen auf der Betriebsspannung werden durch Sieb- und Stützkondensatoren gesiebt.

Beachte!
Aufgrund der spezifischen dynamischen Eigenschaften der dynamischen RAM-Bausteine (Störbeeinflussung) ist die Speichersteckeinheit K 3525 in der Nähe der ZRE und in Systemen mit Buserweiterung mittels Busverstärker BVE im Primärbus anzuordnen.

6. Programmierbarer Festwertspeicher PFS K 3820

6.1. Kurzcharakteristik

Mit Hilfe des programmierbaren Festwertspeichers PFS K 3820 ist es möglich, nicht variable Daten (Festdaten) zu speichern. Ein Datenerhalt ist auch nach Abschaltung bzw. Ausfall der Betriebsspannungen gewährleistet. Auf der STE (Typ 012-7041) befindet sich ein 16 K-Byte großer programmierbarer Festwertspeicher (EPROM-Speicher) mit den Funktionsgruppen:
- Entkopplung (Ein- und Ausgabepuffer)
- zur Auswahl und Ansteuerung der Speicherbauelemente.

Die EPROM-Speicherchips sind über 24-polige DIL-Steckfassungen auf der Steckeinheit kontaktiert. Das Programmieren der EPROM-Schaltkreise erfolgt außerhalb der STE auf einem Entkopplung, Auswahl und Ansteuerung erforderlichen bipolaren Schaltkreisen. Eine Änderung der gespeicherten Leseinformationen ist nur mittels Programmiergerät möglich.

6.2. Technische Daten

| Parameter | Wert |
| :--- | :--- |
| Speicherkapazität: | 16 K Byte (Anordnung von 16 Speicherchips) |
| Speicherschaltkreistyp: | Q 260 |
| | 1 K x 8 Bit; nMOS |
| Zugriffszeit: | $\leq$ 350 ns |
| Betriebsart: | "Lesen" als abgeschlossener Zyklus (Programmieren und Löschen der Speicherbauelemente ist nur extern mit Programmiergerät möglich). |
| Datenerhalt: | Ist auch nach Abschaltung bzw. Ausfall der Betriebsspannung gewährleistet. |
| Stromversorgung: | 5 P = 5 V $\pm$ 5 %, $\leq$ 0,9 A |
| | 5 N = - 5 V $\pm$ 5 %, $\leq$ 0,5 A |
| | 12 P = 12 V $\pm$ 5 %, $\leq$ 0,9 A |
| Beachte! | Die Spannung 5 N darf nicht später als 10 ms nach Zuschalten von 5 P bzw. 12 P ihren Nennwert erreichen und höchstens 10 ms vor Wegfall der 5 P bzw. 12 P abschalten. |

6.3. Programmierung der Steckeinheit

6.3.1. Programmierfelder der Steckeinheit

Die Auswahlfelder bestehen aus Wickelstiftpaaren oder Mikroschaltern. Im ersteren Fall erfolgt die Codierung durch das Wickeln von Brücken.

![[Pasted image 20260516144035.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt die Bestückungsseite PFS - K 3820 (Abb. 6). Eingezeichnet sind Wickelbrückenfelder X6 bis X11 sowie die Steckverbinder X1 und X2.*

Abb. 6

6.3.2. Adressenzuordnung

Die 16 Adreßleitungen des Systembusses haben folgende Zuordnung:

AB0 ... AB9 - interne Chipadressierung
AB12 ... AB15 - Durch Umschlüsselung wird im Speicher die Adresse AB12K ... AB15K wirksam. Sie ergibt sich aus der stellenrichtigen Subtraktion der im Programmierfeld X8-X9 ausgewählten Anfangsadresse der STE und der angelegten Adresse AB12 ... AB15.
AB10, AB11 - Auswahl einer der 16 1 KByte-Blöcke der STE (Chipauswahl)
AB14K, AB15K - STE wird ausgewählt, wenn beide Signale 0-Potential besitzen

Festlegung der Anfangsadresse der STE:
Die 4 Wickelbrücken bzw. Schalter erhalten in binärer Verschlüsselung die Anfangsadresse der STE innerhalb des Gesamtspeichers des MR. Die Programmierung erfolgt durch entsprechende Brücken des Programmierfeldes X8:1 ... X8:4, X9:1 ... X9:4. Diese Adresse ist ein ganzzahliges Vielfaches von 4 K.

| Adreßbereich | X8:4 - X9:4 | X8:3 - X9:3 | X8:2 - X9:2 | X8:1 - X9:1 |
| :--- | :---: | :---: | :---: | :---: |
| 0000 - 3FFF | - | - | - | - |
| 1000 - 4FFF | - | - | - | Brücke |
| 2000 - 5FFF | - | - | Brücke | - |
| 3000 - 6FFF | - | - | Brücke | Brücke |
| 4000 - 7FFF | - | Brücke | - | - |
| ... | | | | |
| C000 - FFFF | Brücke | Brücke | - | - |
| Wertigkeit der Wickelbrücke | $\hat{=}$ 32 K | $\hat{=}$ 16 K | $\hat{=}$ 8 K | $\hat{=}$ 4 K |

6.3.3. Belegung der EPROM-Bauelemente auf der STE

Die programmierten EPROM-Bauelemente befinden sich auf DIL-Steckfassungen. Die Belegung geht aus Abb. 7 hervor. Es ist aber zu beachten, daß die in der Abbildung genannten Adressen relativ zur programmierten Anfangsadresse der STE zu werten sind.

| 3000 - 33FF | 3400 - 37FF | 3800 - 3BFF | 3C00 - 3FFF |
| :--- | :--- | :--- | :--- |
| 2000 - 23FF | 2400 - 27FF | 2800 - 2BFF | 2C00 - 2FFF |
| 1000 - 13FF | 1400 - 17FF | 1800 - 1BFF | 1C00 - 1FFF |
| 0000 - 03FF | 0400 - 07FF | 0800 - 0BFF | 0C00 - 0FFF |

Abb. 7

6.3.4. Auswahl des Speichersperrsignals MEMDI

| Signal | Wickelbrücken X6:1 - X7:1 | Wickelbrücken X6:2 - X7:2 | Wickelbrücken X6:3 - X7:3 |
| :--- | :---: | :---: | :---: |
| $\overline{\text{MEMDI}}$ (X1:B09) | Brücke | - | - |
| $\overline{\text{MEMDI1}}$ (X2:A21) | - | Brücke | - |
| $\overline{\text{MEMDI2}}$ (X2:B21) | - | - | Brücke |

Bei Verwendung von Schaltern entspricht "Brücke" dem geschlossenen Schalter.

6.3.5. Bildung von "WAIT"

Von der Zugriffszeit der verwendeten Speicherbauelemente hängt es ab, ob während des Befehlszyklus im MR eine Zeitverlängerung über "WAIT" vorgenommen werden muß.
Es gilt folgendes:
- Bildung von "WAIT" im M1-Zyklus: Brücke X10-X11 offen
- keine "WAIT"-Bildung: Brücke X10-X11 geschlossen

6.4. Funktionsbeschreibung

6.4.1. Verwendungszweck

Der PFS K 3820 wird im Erzeugnisprogramm "Dezentrale Datentechnik" als programmierbarer Festwertspeicher (nur Lese-Speicher) eingesetzt und erhält feste Programme oder Daten.

6.4.2. Funktion

6.4.2.1. Funktionsbeschreibung

Auf der Steckeinheit befinden sich folgende Funktionsgruppen:
- Speichermatrix
- Ein- und Ausgabepuffer
- Auswahlelektronik
- Steuerelektronik

Die Wirkungsweise der Schaltung ist aus dem Blockschaltbild (Abb. 8) zu ersehen.
Die Speichermatrix besteht aus 16 Speicherbausteinen Q 260 zu je 1 K Byte Speicherkapazität.
Die Adreßeingänge AB0 ... AB9 liegen parallel an allen Eingängen der Speicherchips an, sie werden über Schottky-TTL-Pufferschaltk. ise SE12 (A2/3) verstärkt. Die Datenausgänge D00 ... D07 sind ebenfalls parallelgeschaltet und über die Datenpufferschaltkreise SE16 (A7/8) auf den Systembus geführt. Diese Ausgänge sind "Tri-state"-Ausgänge.
Die Auswahl und der Aufruf der 1 K-Speicherbereiche erfolgt über 16 $\overline{\text{CS}}$-Signale. Liegt ein Speicheraufruf vor, wird über zwei 1 aus 8-Decoder SE05 (A5, A14) eines der 16 $\overline{\text{CS}}$-Signale aktiv mit low. Der adressierte Speicherplatz wird gelesen.
Die Umcodierung der über den Bus angelegten Adreßbits AB12 ... AB15 wird durch den Adderbaustein PS83 vorgenommen (siehe Punkt 6.4.2.2.).
Dieser Adderbaustein A1 verknüpft die angelegte Adresse AB12 ... AB15 mit der im Auswahlfeld codierten Bereichsanfangsadresse. Es entstehen am Ausgang die internen STE-Adressen AB12K ... AB15K.
Die Adreßbits AB10, AB11 und die umcodierten Bits AB12K und AB13K (F0/F1) bilden durch den Decoder SE05 A5/A14 die Auswahlsignale $\overline{\text{CS1}}$ ... $\overline{\text{CS16}}$ der Speicherchips, vorausgesetzt, die Ausgänge des Adderbausteins AB14K und AB15K (F2/F3) geben den Decoder über das Nand A12 frei ($\overline{\text{MEMDI}}$ und $\overline{\text{RFSH}}$ = high).
Gleichzeitig wird mit dem Freigabesignal (A12/6 = 0) über das Nand A12/8 $\overline{\text{CS}}$ = low für die Datenpuffer A7/8 gebildet und die Daten aus dem Speicher auf den Bus geschaltet. Das Netzwerk für die Bildung des Steuersignals "WAIT" wird ebenfalls freigegeben (Eingang R (A10) = high). Ist die Brücke X10-X11 nicht gesetzt, wird aus mit den Signalen $\overline{\text{M1}}$ und TAKT gesteuerte Schiebekette aus 2 FF A10 das $\overline{\text{WAIT}}$ = low aktiv abgeleitet und über die Open-Kollektor-Baustufe A13 auf den Bus geschaltet.
Die Bildung des Signals $\overline{\text{RDY}}$ wird bei den Festwertspeichern vom Datenausgang der Speicherchips abgeleitet. Das hat den Vorteil, daß das Signal neben der Aufrufbestätigung der STE auch eine Aussage über das hardwaremäßige Vorhandensein des adressierten ROM-Speicherchips bringt. Ausgewertet wird, ob die Datenleitung einen gültigen Logikpegel hat oder ob der hochohmige "Tri-state"-Zustand vorliegt. Dazu reicht aus, wenn ein Datenbit durch die Komperatorschaltung AS10 (A11) bewertet wird.
Liegt der hochohmige Zustand vor, werden die Spannungspegel an den Eingängen des Komperators so eingestellt (durch die zwei Spannungsteiler), daß der Ausgang A11/09 low ist. Bei einem gelesenen Logikpegel auf der Datenleitung werden die Potentiale an den Spannungsteilern über die Eingangsdiode so verändert, daß am Eingang eine positive Steuerspannung entsteht. Der Ausgang wird high und bildet $\overline{\text{RDY}}$ = low (aktiv).
Zur Durchschaltung der Prioritätsketten für die Unterbrechungsbearbeitung und für die Busherrschaft im Systembus werden die Klemmen: $\overline{\text{IEI}}$, $\overline{\text{IEO}}$
$\overline{\text{BAI}}$, $\overline{\text{BAO}}$
$\overline{\text{IEIT}}$, $\overline{\text{IEOT}}$ miteinander gebrückt.
Kurz- und langzeitige Störungen auf den Betriebsspannungen 5 P, 5 N und 12 P sind durch Folien- und Elektrolytkondensatoren abgeblockt.

6.4.2.2. Adreßdecodierung durch 4 Bit Volladder PS83

Über den 4-Bit Volladder SN 7483 ($\hat{=}$ T 183) erfolgt die Adreßdecodierung für die Auswahl der ROM-Chips dieses Festwertspeichers. Die Eingänge A0, A1, B0, B1 und C0 werden verknüpft zu F0, F1 und dem Halbbyteübertrag C2. A2, A3, B2, B3 und C2 führen zu F2, F3 und C4. Der Ausgang C4 ist in diesem speziellen Fall der Speicher-STE nicht angeschlossen.

![[Pasted image 20260516144231.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt das Logiksymbol des Volladders PS83 mit seinen Ein- und Ausgängen sowie die zugehörige Wahrheitstabelle für die Verknüpfung der Adressbits.*

| C2 = 0 | C2 = 1 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| A0 | B0 | A1 | B1 | F0 | F1 | C2 | A2 | B2 | A3 | B3 | F2 | F3 | C4 | F2 | F3 | C4 |
| 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 |
| 1 | 0 | 0 | 0 | 0 | 1 | 0 | 1 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 1 | 0 |
| 0 | 1 | 0 | 0 | 0 | 1 | 0 | 0 | 1 | 0 | 0 | 1 | 0 | 0 | 0 | 1 | 0 |
| 1 | 1 | 0 | 0 | 1 | 1 | 0 | 1 | 1 | 0 | 0 | 0 | 1 | 0 | 1 | 1 | 0 |
| 0 | 0 | 1 | 0 | 1 | 1 | 0 | 0 | 0 | 1 | 0 | 0 | 1 | 0 | 1 | 1 | 0 |
| 1 | 0 | 1 | 0 | 0 | 0 | 1 | 1 | 0 | 1 | 0 | 1 | 1 | 0 | 0 | 0 | 1 |
| 0 | 1 | 1 | 0 | 0 | 0 | 1 | 0 | 1 | 1 | 0 | 1 | 1 | 0 | 0 | 0 | 1 |
| 1 | 1 | 1 | 0 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 0 | 0 | 1 | 1 | 0 | 1 |
| 0 | 0 | 0 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 1 | 0 | 1 | 0 | 1 | 1 | 0 |
| 1 | 0 | 0 | 1 | 0 | 0 | 1 | 1 | 0 | 0 | 1 | 1 | 1 | 0 | 0 | 0 | 1 |
| 0 | 1 | 0 | 1 | 0 | 0 | 1 | 0 | 1 | 0 | 1 | 1 | 1 | 0 | 0 | 0 | 1 |
| 1 | 1 | 0 | 1 | 1 | 0 | 1 | 1 | 1 | 0 | 1 | 0 | 0 | 1 | 1 | 0 | 1 |
| 0 | 0 | 1 | 1 | 1 | 0 | 1 | 0 | 0 | 1 | 1 | 0 | 0 | 1 | 1 | 0 | 1 |
| 1 | 0 | 1 | 1 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 0 | 1 | 0 | 1 | 1 |
| 0 | 1 | 1 | 1 | 0 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 | 1 | 1 |
| 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 1 |

Wahrheitstabelle:
C2 = interner Übertrag
C0 = Übertragseingang
C4 = Übertragsausgang
F0 ... F3 = Summenausgänge
A0 ... A3 = Eingangsbit Summand A
B0 ... B3 = Eingangsbit Summand B

![[Pasted image 20260516144305.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt das Blockschaltbild K 3820 (Abb. 8). Enthalten sind Eingangspuffer für Adressen und Steuersignale, Adressen-umschlüssler, prog. STE-Adresse, Blockauswahl, Speicheranordnung (16k Byte, 16 Chips), Steuerelektronik, Datenpuffer, RDY-Bildung, WAIT-Bildung und Auswahleinr. Die Systembussignale AB0...15, WR, RD, MREQ, RFSH, MEMDI, M1, TAKT sowie Ausgänge für Daten DB0...7, RDY und WAIT sind eingezeichnet.*

Abb. 8
Blockschaltbild K 3820

Änderungen und Korrekturen zu den Serviceschaltplänen K 3520; K 3521; K 3525; K 3820

Beachte: Auf allen Stromlaufplänen der Speichersteckeinheiten sind die angegebenen Anschlüsse am Koppel- und Systembus der Ebene B in C zu ändern.
Beispiel: Adreßleitung AB01 X1:B19 in X1:C19

Stromlaufplan K 3520 (1): Anschluß X1:C18 AB1 muß geändert werden in AB3 C18
Anschluß X1:A18 AB0 muß geändert werden in AB2 A18

Stromlaufplan K 3521 (1): Das Steuersignal SUE ist low aktiv (X2:C22), also $\overline{\text{SUE}}$.
X2:27; C27 - Spannung 12 NR ändern in 26 V_~.
Die Matrix muß von 4 K- in 16 KByte RAM geändert werden.
STE Typ 012-7121 ändern in 012-7021 (Bl. 1 und 2)

Stromlaufplan K 3820: Folgende Schaltkreise sind falsch bezeichnet
A13 entspricht A7 U216
A7 entspricht A9 T204
A8 entspricht A10 T174
A11 entspricht A12 T220
A12 entspricht A13 T103
A9 entspricht A14 U205
A15 entspricht A11 AS10
A14 entspricht A8 U216

| Belegungsplan | Ergänzung |
| :--- | :--- |
| STE 1.12.517011.0/09 | 083-4-710-016 |
| STE 1.12.517121.0/09 | 083-4-710-018 |
| STE 1.12.517041.0/09 | 083-4-710-015 |
| STE Typ 012-7136 1.12.517136.0/09 ändern in Typ 012-7012 1.12.517012.0/09 | 083-4-710-053 |

Schaltteilliste
STE Typ 012-7136 1.12.517136.0/01 ändern in STE Typ 012-7012 1.12.517012.0/01
in Klammern: STE Typ 012-7131 1.12.517131.0/01
Pos. A1:1 ... A1:32 Schaltkreis RAM S6508 ändern in RAM 1902 (RAM S 6508)
Beachte: - PIN-Belegung 1902 entspricht der des 4 K stat. RAM nMOS X2A4 (siehe Bausteinübersicht Seite 27 - U202)
- PIN-Belegung S 6508 (siehe Bausteinübersicht Seite 28)

robotron

VEB Robotron
Buchungsmaschinenwerk
Karl-Marx-Stadt
DDR-9010 Karl-Marx-Stadt
Annaberger Str. 93
PSF 129

Exporteur:
Robotron-Export/Import
Volkseigener
Außenhandelsbetrieb
der Deutschen
Demokratischen Republik
DDR-1140 Berlin
Allee der Kosmonauten 24
PSF 11

1.62.540014.4 (GER)
831.53.01.002
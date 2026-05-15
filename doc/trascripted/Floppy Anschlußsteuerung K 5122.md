# **robotron**

## Anschlußsteuerung K 5122

### Betriebsdokumentation


4. Auflage  
    Karl-Marx-Stadt, 1987
    

© VEB Kombinat Robotron, 1987

# Inhaltsverzeichnis

I. Verwendung und Einordnung  
II. Funktionsbeschreibung  
III. Beschreibung spezieller Baugruppen  
IV. Kurzzeichenübersicht  
V. Serviceschaltpläne

# 1. Allgemeines

Die Steckeinheit soll der Kopplung flexibler Folienspeicher an die zentralen Recheneinheiten ZRE 2521 bis 25/27 dienen.  
Sie ist unter der Bezeichnung Anschlußsteuerung Folienspeicher **AFS 6400 K 5122** Bestandteil des Mikrorechnersystems K 1520.

# 2. Blockschaltbild

![[Pasted image 20260510232013.png]]
![Bildunterschrift: Abb. 2 Blockschaltbild mit Funktionsgruppen: PLL, Markensteuerung, 24-Bit Schieberegister, Schreib-P-ROM, Daten-PIO, Adreßdecoder, Steuerbustreiber und Logik, Datenbustreiber, Steuer-PIO, 8212 Leitungstreiber]

# 3. Schnittstelle zum K 1520-Bus

## 3.1. Adressierung und Auswahl der Steckeinheit

Die Schnittstelle zwischen ZRE und Anschlußeinheit ist der Rechnerbus K 1520, der durch die Systembusrichtlinie MR K 1520 charakterisiert wird. Die Systembussignale sind dem Steckverbinder X1 und zum Teil dem Steckverbinder X2 zugeordnet. Über den Rechnerbus erfolgt der gesamte Datenaustausch zwischen ZRE und den Anschlußeinheiten. Er besteht aus Daten-, Adreß-, Steuer- und Koppelbus sowie den Leitungen für die Stromversorgung.  
Der Datenbus besteht aus 8 Leitungen, die zur byteseriellen Datenübertragung verwendet werden. Von den 16 Adreßleitungen werden die niederen 8 (AB₀ ... AB₇) zur Adressierung der Anschlußeinheit genutzt. Durch ihre Decodierung werden 9 Ein/Ausgabe-Register ausgewählt, die der Steuerung und dem Datenaustausch dienen.

Die Leitungen AB₀ - AB₁ werden direkt an die beiden PIO geführt. Sie unterscheiden, ob ein Steuer- oder Datenregister bzw. Tor A oder Tor B eines PIO adressiert werden.

|   |   |
|---|---|
|Leitung|Bedeutung|
|AB₀ = 1|Steuerwort|
|AB₀ = 0|Datenwort|
|AB₁ = 1|Tor B|
|AB₁ = 0|Tor A|

Die Adressen AB₂ ... AB₇ werden durch 2 Schaltkreise 8205 decodiert und bilden die **/CS**-Signale für die beiden PIO A1.1, A1.2 und den 8212 A4. Die Adressierung kann unter der Bedingung AB₄ = 1 wahlfrei erfolgen. Je nach der festgelegten Adresse werden die beiden 8205 A3.1, A3.2 durch Brücken miteinander verbunden. Zusätzlich wird an den 8205 A3.1 das Signal **/IODI** geführt, wodurch die Anschlußeinheit auch bei gültiger Adresse abgeschaltet werden kann. Die **/CS**-Signale werden zur Vermeidung von Fehlern beim Interrupt Quit-tungszyklus während des **/M1**-Signals (/M1 = 0) über A3.2/5 gesperrt. Aus dem gleichen Grund wird das **/WR**-Signal an den **/CST**-Eingang des 8212 (A4/01) geführt.

In Abstimmung mit der ZRE K 2526/27 wurde für die Steckeinheit K 5122 folgende Adreßbelegung festgelegt:

**Adressen**

|   |   |   |   |   |   |   |   |   |   |   |   |   |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
|A₇|A₆|A₅|A₄|A₃|A₂|A₁|A₀||PIO|Tor|Wort|**/CS3**|
|0|0|0|1|0|0|0|0|10|S|A|D|1|
|0|0|0|1|0|0|1|1|11|S|A|C|1|
|0|0|0|1|0|0|1|0|12|S|B|D|1|
|0|0|0|1|0|0|1|1|13|S|B|C|1|
|0|0|0|1|0|1|0|0|14|D|A|D|1|
|0|0|0|1|0|1|0|1|15|D|A|C|1|
|0|0|0|1|0|1|1|0|16|D|B|D|1|
|0|0|0|1|0|1|1|1|17|D|B|C|1|
|0|0|0|1|1|0|0|0|18|-|-|-|0|

**Bezeichnung:**

- S = Steuer-PIO **/CS1**
    
- D = Daten-PIO **/CS2**
    
- A = Tor A
    
- B = Tor B
    
- C = Steuerwort
    
- D = Datenwort
    
- **/CS3** = Auswahl 8212 mit 0 aktiv
    

Die NAND-Verknüpfung der **/CS**-Signale ergibt die Funktion **/CE** für die gesamte Steckeinheit.

## 3.2. Bustreiber und deren Steuerung

Um die elektrischen Bedingungen der Systembusrichtlinie einzuhalten, werden die Steuerleitungen **/M1**, **/IORQ**, **/RD**, **/WR**, **/RESET** und Takt über zwei Schaltkreise 8216 geführt. Dadurch verbrauchen diese Leitungen nur eine K 1520-Lasteinheit I_IL = 0,25 mA.  
Die NAND-Verknüpfung der Signale **/RESET** und **/M1** im A6.2 schafft eine Möglichkeit, die beiden PIO in ihren Grundzustand zurückzusetzen.  
Die Datenbusleitungen müssen aus Gründen der kapazitiven Belastung ebenfalls über bidirektionale Treiber 8216 A5.1, A5.2 an die PIO herangeführt werden.  
Die Umschaltung der bidirektionalen Treiber erfolgt durch das Signal **/DIEN**.

|   |   |
|---|---|
|**/DIEN**|Datenfluß|
|0|DI ———> DB|
|1|DO ———> DB|

Die Leitungen DB sind direkt mit dem Datenbus verbunden, während die Leitungen DI und DO zunächst miteinander verknüpft werden und danach zu den Datenleitungen der PIO führen.  
Die Steuerlogik zur Bildung des **/DIEN**-Signals ist so aufgebaut, daß die Treiber ständig auf Eingang geschaltet sind. Nur wenn die ZRE eine Information von der Anschlußsteuerung lesen will oder wenn durch die Anschlußsteuerung ein nicht quittierter Interrupt vorliegt und ein Interruptquittungszyklus ausgeführt wird, schalten sich die Treiber auf den Datenbus auf.  
Durch die gleiche Logik erfolgt mit geringem Zusatzaufwand die Bildung des Signals **/RDY** – Anschlußsteuerung bereit.

![[Pasted image 20260510232642.png]]
![Bildunterschrift: Abb. 1 Steuerung für Bustreiber und RDY-Bildung]

**Logische Verknüpfungen (gemäß Abb. 1):**

**/DIEN** = **/IORQ** ∧ **/d**  
**/RDY** = **/IORQ** ∧ **/c**

**d** = /(a ∧b)  
**a** = /(M1 ∧ IEI ∧ IEO)  
**b** = /(RD ∧ CE)  
**c** = /(a ∧ /CE)

Die Schaltung Abb. 1 ergab sich nach Minimierung des Hardwareaufwandes durch Umstellen der Funktionen **/DIEN** und **/RDY**.

Die ausführlichen Funktionen lauten:

/DIEN = /(IEO ∧ M1 ∧ IEI ∧ IORQ) ∨ /(CE ∧ RD ∧ IORQ)  
/RDY = /(IEO ∧ M1 ∧ IEI ∧ IORQ) ∨ /(CE ∧ IORQ)

**Bedeutung der Teilfunktionen:**  
/IEO ∧ M1 ∧ /IEI ∧ IORQ ≙ Interrupt Quittungszyklus  
CE ∧ RD ∧ IORQ ≙ Lesezyklus  
CE ∧ IORQ ≙ Ein/Ausgabezyklus

Das Signal **/RDY** wird entsprechend den Forderungen der Systembusrichtlinie durch eine Open-Kollektor-Stufe (T103) gebildet.

# 4. Steuerung der Anschlußeinheit und der Laufwerke

## 4.1. Steuer-PIO

Die Steuerung der Anschlußeinheit und der Laufwerke erfolgt durch den Steuer-PIO A1.2 sowie den 8212 A4. Der Steuer-PIO sendet und empfängt Steuersignale über seine beiden Ein/Ausgabetore.  
Er ist in die Interruptkette für zeitkritische Geräte eingeordnet (**/IEI**-**/IEO**), wobei das Interrupt zur Auswertung einiger Statussignale verwendet wird. Der Steuer-PIO wird über den Systembus unmittelbar von der ZRE programmierbar gesteuert.

Die beiden Tore arbeiten in den Betriebsarten:  
Tor A – OUTPUT MODE (Mode 0)  
Tor B – BIT MODE (Mode 3)

und haben folgende Bedeutung:

**Tor A**  

| Pin   | Signalbezeichnung              | Kurzzeichen | Ein/Ausgang | Bemerkung    |     |
| :---- | :----------------------------- | :---------- | :---------- | :----------- | --- |
| A₀    | /WRITE ENABLE                  | /WE         | Ausgang     |              |     |
| A₁    | MARK                           | MK          | Ausgang     |              |     |
| A₂    | /FAULT RESET                   | /FR         | Ausgang     | MF 3200, MFS |     |
| A₃    | /START                         | /STR        | Ausgang     |              |     |
| A₄    | MARK1                          | MK1         | Ausgang     |              |     |
| A₅    | MARK RESET oder STEP DIREKTION | MR SD       | Ausgang     |              |     |
| A₆    | /HEAD LOAD                     | /HL         | Ausgang     |              |     |
| A₇    | /STEP                          | /ST         | Ausgang     |              |     |
| /ASTB | INDEX                          | IX          | Eingang     |              |     |

**Tor B**  

| Pin | Signalbezeichnung | Kurzzeichen | Ein/Ausgang | Bemerkung    |     |
| :-- | :---------------- | :---------- | :---------- | :----------- | --- |
| B₀  | /LAUFWERK BEREIT  | /RDYL       | Eingang     | MFS          |     |
| B₁  | MARKE ERKANNT     | MKE         | Eingang     |              |     |
| B₂  | /HIGH FREQUENCY   | /HF         | Ausgang     |              |     |
| B₃  | PRECOMPENSATION   | **PRE**     | Ausgang     |              |     |
| B₄  | /FAULT ADAPTER    | /FA         | Eingang     |              |     |
| B₅  | /WRITE PRODECT    | /WP         | Eingang     |              |     |
| B₆  | /FAULT            | /FW         | Eingang     | MF 3200, MFS |     |
| B₇  | /TRACK 00         | /TO         | Eingang     |              |     |

## 4.2. Laufwerksteuerung

Die Steuerung der Laufwerke ist im Prinzip eine reine Softwarelösung. Die notwendige Hardware beschränkt sich auf die Register des Steuer-PIO und des 8212 sowie die Leitungstreiber bzw. die Widerstandsbeschaltung an den Empfangsleitungen.  
Als Leitungstreiber für die Steuersignale, außer **/SE** und **/LCK**, wurde der Schaltkreis P450 verwendet, der hinsichtlich Strombedarf und Open-Kollektor-Stufe den gegebenen Forderungen entspricht. Eine Eingangsstufe der Laufwerke MF 3200 und MFS verbraucht I_ILmax = 24 mA. Da 4 Laufwerke parallel angeschlossen werden können, muß ein Leitungstreiber mindestens I_OLmin = 96 mA liefern. Deshalb wurde der o. g. Schaltkreis verwendet. Er besitzt I_OLmax = 300 mA.  
Für die Signale **/SE** und **/LCK** wurde der Schaltkreis T106 eingesetzt. Er besitzt 40 mA. Diese sind ausreichend, da jede dieser Leitungen nur ein Laufwerk ansteuert.  
Die Empfangsleitungen sind entsprechend den technischen Forderungen des Laufwerkes mit folgenden Widerstandsnetzwerk beschaltet:

![[Pasted image 20260511003421.png]]
![Bildunterschrift: Abb. 2 Eingangsseitige Anpassung (Widerstandsnetzwerk 150 Ohm / 220 Ohm / 330 Ohm zwischen Laufwerk und Anschlußsteuerung)]

Die Anpaßstufe für das **/RD**-Signal mußte aufgrund von Störproblemen auf die oben angegebene Weise verändert werden.  
Die Auswahl der einzelnen Laufwerke und die Verriegelung /Motor On (MFS) wird durch die Signale /SE1 ... /SE4 und /LCK1 ... /LCK4 realisiert.  
/SE - /SELECT, /LCK - /LOCK  
Die Signale werden von einem 8 Bit-E/A-Tor (A4) bereitgestellt. Dieser Schaltkreis wird durch den Adreßbus ausgewählt (CS3).  
CS3 ∧ IORQ = CS2 (A4)  
Der folgende OUT spricht dann das Tor an.  
Durch das Signal RESET = 1 werden alle Ausgänge auf 1 geschaltet. Damit ist kein Laufwerk ausgewählt oder verriegelt.

**Belegung 8212:**  

| Ausgang | Signal | Ausgang | Signal |  
| :--- | :--- | :--- | :--- |  
| DO1 | **/LCK2** | DO5 | **/LCK3** |  
| DO2 | **/LCK1** | DO6 | **/LCK4** |  
| DO3 | **/SE3** | DO7 | **/SET** |  
| DO4 | **/SE4** | DO8 | **/SEZ** |

Die eigentliche Steuerung der Laufwerke erfolgt mit Hilfe von Mikroprogrammen über die Leitungen /SD, /HL, /RDYL, /ST, /WE, /FR, /TO, /IX, /SE, /LCK, /WP.  
Vom übergeordneten System können die Quittungssignale /TO, /FW, /IX, /WP, /RDYL durch Abfrage oder Interrupt behandelt werden.  
Die beim MF 6400 nicht benötigten Signale /FW und /FR können bei einem eventuellen Anschluß eines Doppelkoplaufwerkes für die Signale /TS (doppelseitige Diskette) und /HS (Kopfauswahl) verwendet werden.

## 4.3. Steuerleitungen für Anschlußeinheit

Für die Modulation, Demodulation und Übertragung der Daten werden folgende Steuer- und Quittungssignale verwendet:  
/FR, /STR, MK, MK1, MR, /FA, /SYN, MKE, PRE, HF

Diese Signale haben für die Anschlußsteuerung folgende Bedeutung:  
logisch high = 1 = 5 P  
logisch low = 0 = 0 V

/WE:
/WE = 1 : Schreiben gesperrt  
/WE = 0 : Schreiben – Freigabe der Schreibsteuerung einschließlich Takterzeugung

/STR:  
/STR = 1 : Anschluß inaktiv  
/STR = 0 : Bildung vom **/BUSRQ** möglich

Das Signal /RDYL = 0 wird durch das Programm (Anfangslader) als **MFS** gewertet.  
Beim MF 6400 ist deshalb die Brücke R nicht zu bestücken.

/MK1:  
/MK1 = 1 : Information ins Schieberegister einlesen  
/MK1 = 0 : ständig 1 einlesen (Eingang A12.3/12)  
/MK1 = 0 : Schreiben MFM bzw. Schreiben FM-Marken (Eingang A2.1/20)  
/MK1 = 1 : Dateninformation Schreiben

/MK:  
Dieses Signal wird ebenfalls doppelt genutzt.  
/MK = 0 : Markenerkennung FM und Index-Marke MFM (Synchronisationsbyte)  
/MK = 1 : Erkennung MFM-Synchronisationsbyte (A1) (Eingang A2.2/23)  
/MK = 0 : Markenschreiben FM bzw. MFM-Synchronisationsbyte  
/MK = 1 : Schreiben Taktinformation MFM (Eingang A2.2/22)

MR:  
Dieses Signal wird zum Rücksetzen des Marken-FF A12.4/01 benutzt. Das Marken-FF speichert die Information, daß eine Marke gültig erkannt wurde.  
MR = 0 : Signal inaktiv  
MR = 1 : FF wird statisch zurückgesetzt

/FA:  
Quittungssignal für ordnungsgemäßen zeitlichen Ablauf der Datenübertragung zwischen Speicher und Anschlußeinheit bzw. Fehlermeldung bei Verletzung der Zeitkriterien während der Datenübertragung.  
/FA = 0 : Fehler  
/FA = 1 : kein Fehler  
Das Fehlersignal wird gespeichert und kann nur durch das Signal **STR** = 1 zurückgesetzt werden.

PRE:  
Mit Hilfe dieses Signals wird die Präcompensationsschaltung für die Schreibimpulse eingeschaltet.  
PRE = 0 : Schreiben ohne Präcompensation  
PRE = 1 : Schreiben mit Präcompensation  
Bei Aufzeichnungen (FM) muß PRE ständig 0 sein.

MKE:  
Bei Synchronisation der Datenübertragung mit **/WAIT** erfolgt eine Auswertung des Signals **MKE** (A12.4/05).  
MKE = 1 : Marke erkannt  
MKE = 0 : keine Marke

/HF:  
Das Signal **HF** bestimmt die Frequenz des Schreib- bzw. Lesetaktes.  
HF = 0 : hohe Frequenz für MFM-Aufzeichnungsverfahren 8"-Laufwerke (MF 6400)  
HF = 1 : niedrige Frequenz FM-Aufzeichnungsverfahren 8"-Laufwerke (MF 3200, MF 6400) und MFM-Aufzeichnungsverfahren 5"-Laufwerke (MFS)

# 5. Datenübertragung zwischen Datenspeicher und Folienspeicher

## 5.1. Daten-PIO

Für die Zwischenspeicherung eines Datenbytes auf der Anschlußsteuerung wird ein PIO A1.1 mit zwei E/A-Toren verwendet.  
Der PIO stellt die Schnittstelle zum Datenbus des übergeordneten Systems dar. Dabei dient Tor A als Ausgabetor und Tor B als Eingabetor für jeweils ein Byte.

## 5.2. Parallel-Serien-Wandlung

Da die zu schreibenden Daten seriell auf den Datenträger aufgezeichnet werden, muß eine Parallel-Serien-Wandlung durchgeführt werden. Das geschieht durch ein 24 Bit-Schieberegister.

**Abb. 3**

Für die eigentliche Parallel-Serien-Wandlung werden 16 Bit benötigt. Diese ergeben sich aus dem Aufzeichnungsverfahren des Folienspeichers. Bit 16 bis 21 werden benötigt für die Schreibprecompensation bzw. Auswahl der Taktinformation beim MFM-Aufzeichnungsverfahren bei Bytewechsel.  
Die Parallel-Serien-Wandlung vollzieht sich folgendermaßen:  
Der Daten-PIO stellt ein Byte zur Übernahme ins Schieberegister bereit. Diese Dateninformation liegt ebenfalls am Schreib-ROM A2.1 an. Entsprechend dieser Information stellt der ROM an seinen Ausgängen die dazugehörige Taktinformation zur Übernahme ins Schieberegister bereit.  
Während der FM-Aufzeichnung sind die Ausgänge des ROM in Three-state-Zustand und ins Schieberegister werden Einsen übernommen. Der ROM enthält ebenfalls die Taktkombinationen für die Markenbytes und die Synchronisationsbytes.  
Der Ausgang WDA dient als serieller Ausgang des Schieberegisters bei Parallel-Serien-Wandlung.  
Beim Lesevorgang wird der Eingang AO als serieller Eingang verwendet und die Information vom Folienspeicher seriell ins Schieberegister eingeschoben. Die Ausgänge Q0 ... Q7 sind mit den PIO-Eingängen B0 ... B7 verbunden und dienen der Byteübertragung zum PIO.  
Die Steuerung der einzelnen Funktionen geschieht mit Hilfe der Signale V, C1 und C2.

V = 0 serieller Betrieb / C1 ⎍ Schiebetakt (Bed. V = 0)  
V = 1 paralleler Betrieb / C2 ⎍ Parallelübernahmetakt (Bed. V = 1)

#### 5.3. Markenerkennung

Die Notwendigkeit einer Markenerkennung ergibt sich aus der Verwendung der Aufzeichnungsformate nach ISO (TC-97) SC-11 Nr. 149 bzw. 347 KROS 5110 bei der Sektorierung einer Spur durch aufgezeichnete Adreßmarken, die außerdem zur Synchronisation der Datenübertragung genutzt werden. Eine Adreßmarke bzw. ein Synchronisationsbyte setzt sich aus zwei Informationen zusammen, der Dateninformation und der zwischengeschalteten Taktinformation (Abb. 4).  
Um die Adreßmarken bzw. die Synchronisationsbyte erkennen zu können, müssen beide Informationen decodiert werden. Dies erfolgt mit Hilfe eines rückgekoppelten Festwertspeichers A2.2. Der ROM hat eine Kapazität von 1 KByte. An die Adreßleitungen A0 ... A7 werden die Schieberegisterausgänge Q0 ... Q7 angeschlossen. Die Information, die an den Schieberegisterausgängen anliegt, wertet der ROM ständig als Adresse und legt den Inhalt der adressierten Speicherzelle an seine Ausgänge O1-O8.  
Der Ausgang O7 gibt das Signal AM (= 1) ab, wenn diese gültig erkannt wurde. Dazu ist allerdings die Decodierung von Takt- und Datenbyte notwendig. In der Schaltung mit dem rückgekoppelten ROM wird dabei ausgenutzt, daß die geschachtelte Takt- und Dateninformation um jeweils ein Bit verschoben ist. Das Taktbyte liegt dabei zuerst an den Eingängen des Festwertspeichers. Die Speicherzelle, die durch dieses Bitmuster adressiert wird, schaltet den Ausgang O8 auf 1.  
Dieses Signal wird als Rückkoppelsignal /RK bezeichnet und an den D-Eingang eines FF A12.4/12 geführt. Mit dem nächsten Schiebetakt wird das Datenbyte der Adreßmarke an die Eingänge des ROM gelegt. Gleichzeitig erfolgt die Durchschaltung der /RK-Information an den Ausgang des FF A12.4/9 = /RKQ und dieser liegt als weiterer Eingang A9 an dem ROM. Wenn beide Bedingungen erfüllt sind, wird die Speicherzelle angesprochen, deren Inhalt das Signal AM = 1 (07) ausgibt. Dieses Signal wird anschließend mit dem Zwischentakt ZT verknüpft. Der entstandene Impuls setzt das Marken-FF auf Q = 1 (MKE) und dieses Signal ist die Quittung dafür, daß eine Marke erkannt wurde. Die Verknüpfung mit dem Zwischentakt ist notwendig, um Verzögerungszeiten und Einschwingvorgänge an den Speicherausgängen ausschließen zu können.
Mit Hilfe des Signals /MK wird entschieden, ob das Synchronisationsbyte A1 oder das Synchronisationsbyte C2 bzw. die FM-Marken erkannt werden sollen.
Die Entscheidung, welche Marke speziell erkannt wurde, muß durch Auswertung des Datenbytes der Marke erfolgen, welches als erstes Byte zur CPU übertragen wird. Die Rückführung des Signals AME an den Rücksetzeingang des Rückkoppel-FF A12.4/13 bewirkt, daß keine weitere Adreßmarke erkannt werden kann, wenn MKE = 1 ist.

![[Pasted image 20260515132034.png|697]]
![[Pasted image 20260515132112.png]]
**Abb. 4 Darstellung der Adreßmarken und Synchronisationsbytes**

![[Pasted image 20260515132213.png]]
**Abb. 5 Markenerkennung**

| RK | AM | |
| :--- | :--- | :--- |
| 0 | 0 | vor der Marke |
| 1 | 0 | Taktbyte |
| 0 | 1 | Datenbyte |
| 1 | 1 | nach der Marke |

Für den 1 KByte ROM ergeben sich somit folgende Speichercodierungen:

| Adresse | Speicherinhalt |
| :--- | :--- |
| **Gruppe 0** | 14 -> 80 |
| | C7 -> 80 |
| | D7 -> 80 |
| **Gruppe 1** | 0A -> 80 |
| **Gruppe 2** | C2 -> 40 |
| | F8 -> 40 |
| | FB -> 40 |
| | FC -> 40 |
| | FE -> 40 |
| **Gruppe 3** | A1 -> 40 |

### Inhalt ROM AFS 6400 A2.1 - Schreib-ROM - Speicherinhalt

| Adr. | 0 1 | 2 3 | 4 5 | 6 7 | 8 9 | A B | C D | E F | 0 1 | 2 3 | 4 5 | 6 7 | 8 9 | A B | C D | E F |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **0000** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **20** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **40** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **60** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **80** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **A0** | 000A | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **C0** | 0000 | 1400 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **E0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | C700 | 00C7 | D700 | C700 |
| **0100** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **20** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **40** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **60** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **80** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **A0** | 000A | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **C0** | 0000 | 1400 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **E0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | C700 | 00C7 | D700 | C700 |
| **0200** | FFFE | FCFC | F9F8 | F8F8 | F3F2 | F0F0 | F1F0 | F0F0 | E7E6 | E4E4 | E1E0 | E0E0 | E3E2 | E0E0 | E1E0 | E0E0 |
| **20** | CFCE | CCCC | C9C8 | C8C8 | C3C2 | C0C0 | C1C0 | C0C0 | C7C6 | C4C4 | C1C0 | C0C0 | C3C2 | C0C0 | C1C0 | C0C0 |
| **40** | 9F9E | 9C9C | 9998 | 9898 | 9392 | 9090 | 9190 | 9090 | 8786 | 8484 | 8180 | 8080 | 8382 | 8080 | 8180 | 8080 |
| **60** | 8F8E | 8C8C | 8988 | 8888 | 8382 | 8080 | 8180 | 8080 | 8786 | 8484 | 8180 | 8080 | 8382 | 8080 | 8180 | 8080 |
| **80** | 3F3E | 3C3C | 3938 | 3838 | 3332 | 3030 | 3130 | 3030 | 2726 | 2424 | 2120 | 2020 | 2322 | 2020 | 2120 | 2020 |
| **A0** | 0F0E | 0C0C | 0908 | 0808 | 0302 | 0000 | 0100 | 0000 | 0706 | 0404 | 0100 | 0000 | 0302 | 0000 | 0100 | 0000 |
| **C0** | 1F1E | 1C1C | 1918 | 1818 | 1312 | 1010 | 1110 | 1010 | 0706 | 0404 | 0100 | 0000 | 0302 | 0000 | 0100 | 0000 |
| **E0** | 0F0E | 0C0C | 0908 | 0808 | 0302 | 0000 | 0100 | 0000 | 0706 | 0404 | 0100 | 0000 | 0302 | 0000 | 0100 | 0000 |
| **0300** | 7F7E | 7C7C | 7978 | 7878 | 7372 | 7070 | 7170 | 7070 | 6766 | 6464 | 6160 | 6060 | 6362 | 6060 | 6160 | 6060 |
| **20** | 4F4E | 4C4C | 4948 | 4848 | 4342 | 4040 | 4140 | 4040 | 4746 | 4444 | 4140 | 4040 | 4342 | 4040 | 4140 | 4040 |
| **40** | 1F1E | 1C1C | 1918 | 1818 | 1312 | 1010 | 1110 | 1010 | 0706 | 0404 | 0100 | 0000 | 0302 | 0000 | 0100 | 0000 |
| **60** | 0F0E | 0C0C | 0908 | 0808 | 0302 | 0000 | 0100 | 0000 | 0706 | 0404 | 0100 | 0000 | 0302 | 0000 | 0100 | 0000 |
| **80** | 3F3E | 3C3C | 3938 | 3838 | 3332 | 3030 | 3130 | 3030 | 2726 | 2424 | 2120 | 2020 | 2322 | 2020 | 2120 | 2020 |
| **A0** | 0F0E | 0C0C | 0908 | 0808 | 0302 | 0000 | 0100 | 0000 | 0706 | 0404 | 0100 | 0000 | 0302 | 0000 | 0100 | 0000 |
| **C0** | 1F1E | 1C1C | 1918 | 1818 | 1312 | 1010 | 1110 | 1010 | 0706 | 0404 | 0100 | 0000 | 0302 | 0000 | 0100 | 0000 |
| **E0** | 0F0E | 0C0C | 0908 | 0808 | 0302 | 0000 | 0100 | 0000 | 0706 | 0404 | 0100 | 0000 | 0302 | 0000 | 0100 | 0000 |

***

### Inhalt ROM AFS 6400 A2.2 - Marken-ROM - Speicherinhalt

| Adr. | 0 1 | 2 3 | 4 5 | 6 7 | 8 9 | A B | C D | E F | 0 1 | 2 3 | 4 5 | 6 7 | 8 9 | A B | C D | E F |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **0000** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 8000 | 0000 | 0000 | 0000 |
| **20** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **40** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **60** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **80** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **A0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **C0** | 0000 | 0000 | 0000 | 0080 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **E0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **0100** | 0000 | 0000 | 0000 | 0000 | 0000 | 8000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **20** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **40** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **60** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **80** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **A0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **C0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **E0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **0200** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **20** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **40** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **60** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **80** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **A0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **C0** | 0000 | 4000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **E0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 4000 | 0400 | 4000 | 4000 |
| **0300** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **20** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **40** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **60** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **80** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **A0** | 0040 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **C0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |
| **E0** | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 | 0000 |

#### 5.4. Schreibsteuerung

Die Schreibsteuerung besteht im wesentlichen aus einem quarzgesteuerten Taktgenerator mit einer nachfolgenden Anordnung von Teiler, Zähler und der Präcompensationsschaltung. Die erforderlichen Frequenzen für die Aufzeichnung betragen 1 MHz bzw. 500 kHz mit einer Toleranz von ± 0,1 % über den Spannungsbereich von 4,75 V - 5,25 V und bei den möglichen Temperaturabweichungen von 0 °C ... 70 °C. Die geforderten Toleranzen werden durch die Eigenschaften des Quarzes und dessen Beschaltung gewährleistet. Die Grundfrequenz des Quarzes beträgt 10000 kHz.

![[Pasted image 20260515133058.png]]
**Abb. 6 Taktgenerator**

Mit /HF = 1 wird die Grundfrequenz halbiert. Ein Dezimalzähler A19 teilt diese Frequenz auf 1/10 (Ausgang P1) herunter.
Bei Aufzeichnung von Informationen auf magnetische Datenträger entsteht der Effekt der Spitzenverschiebung, der zu Fehlern bei der Wiedergabe der Daten führen kann. Die Größe der Spitzenverschiebung ist u. a. von der Bitdichte (Spur-Nummer) und der Bitfolge abhängig. Aus diesen Gründen wird beim Aufzeichnungsverfahren MFM eine Verschiebung (Präcompensation) der Schreibimpulse durchgeführt, um die Spitzenverschiebung in gewissen Grenzen auszugleichen. Die im Adapter eingesetzte Präcompensationsschaltung führt eine Verschiebung an den kritischen Stellen, d. h. an den Übergängen von max. Bitdichte auf größere Bitabstände (3 µs und 4 µs bzw. 6 µs und 8 µs) und umgekehrt, um 200 ns bzw. 400 ns durch.

#### 5.5. Lesesteuerung

Der Grundbestandteil der Lesesteuerung ist eine PLL-Schaltung (phase lock loop-Phasenregelschleife), die mit einem 16-stelligen Binärzähler A13 gekoppelt wird.
Die PLL hat die Aufgabe, die Lesedaten (RD) zu synchronisieren, d. h. die Langzeitschwankungen der Bitabstände infolge Gleichlaufschwankungen des Antriebssystems der Diskette auszugleichen. Die PLL besteht aus Vergleicher, Ansteuerung (D/A-Wandler), VCO und Teiler.
Der VCO (voltage controlled oszillator - spannungsgesteuerter Oszillator) schwingt auf der durch R21 einzustellenden Grundfrequenz von 2 MHz. Diese Frequenz ändert sich je nach Ergebnis des Vergleiches des Takt-Daten-Gemisches RD mit der halben VCO Frequenz. Somit liegen die Takte T1_L (Lesetakt) und ZT (Zwischentakt) ständig synchron zu den Lesedaten.

![[Pasted image 20260515133642.png]]
**Abb. 7 Präcompensation und Bildung von /ASTB**

![[Pasted image 20260515133707.png]]
**Abb. 8 Blockschaltbild PLL-Schaltung**

Die Decodierung der Schreibdaten für die Präcompensation geschieht im A3.4. Das Einschalten der Präcompensationsschaltung erfolgt durch das Signal PRE, das beim MF 6400 ab Spur 43 und beim MFS ab Spur 25 vom Mikroprogramm bereitgestellt werden muß.
Die Ausgangssignale /PR (A3.4/12) und /PV (A3.4/10) bestimmen die Richtung der Verschiebung.
/PR = 0 - Schreibimpuls wird verzögert
/PV = 0 - Schreibimpuls wird vorverschoben
In einem 8 Bit-Datenselektor (A20), dessen Adreßeingänge A0 - A2 vom Zähler A19 gesteuert werden, erfolgt in Abhängigkeit von den Zuständen an den Eingängen 1 bis 3 die Bildung des Schreibtaktes (A20/05). Mit der 0-1-Flanke von A20/06 wird das FF A12.1 gekippt, dessen Ausgang 09 den Datenselektor sperrt. Damit wird die Bildung eines weiteren Schreibimpulses innerhalb eines Zyklus des Zählers A19 verhindert. Sein Ausgang P1 (A19/12) hebt die Sperre wieder auf.
Aller 16 Takte wird im Bitzähler A13 zum Zeichen, daß ein Byte abgearbeitet wurde, der Übertrag P1 erzeugt (A13/12), der, durch einen Takt des A19 gesteuert, im FF A12.3/06 das Signal /ASTB bzw. V (A12.3/05) bildet. Das FF wird mit der nächsten Flanke des Schreibtaktes rückgesetzt. Während des Lesens ist es gesperrt.
Der Takt C2 für die Parallelübernahme des nächsten Bytes ins Schieberegister entspricht dem negierten Schreibtakt.
Wird die niedrige Frequenz, die 500 kHz beträgt, eingestellt, vergrößern sich die Werte für die Spitzenverschiebung und die Impulsbreite des Schreibtaktes auf 400 ns.

Der D-Eingang des FF (A12.3) ist mit /MK1 beschaltet, so daß unabhängig von /RD_log.1 in das Schieberegister eingelesen werden kann. Damit können die Störflecke vor und nach dem Datenfeld mikroprogrammgesteuert überlesen werden.

T1_L
- Mit dem Signal HF läßt sich die Frequenz des Lesetaktes einstellen:
- HF = 1 Frequenz T1_L = 1 MHz
- HF = 0 Frequenz T1_L = 500 kHz
- während des Schreibens ist der Lesetakt = 1
- Der Lesetakt schaltet den Zähler A13 mit dessen Signal P1 /BSTB des Daten-PIO gebildet wird.

ZT 
- Die Rückflanke des Zwischentaktes kippt bei AM = 1 das Marken-FF (A12.4)
- Seine Verknüpfung mit RD schaltet das FF A12.3, dessen Ausgang 08 der serielle Eingang des Schieberegisters ist.
- Bei der Arbeit mit der niedrigen Geschwindigkeit besteht der Zwischentakt aus 3 Einzelimpulsen. Damit wird garantiert, daß auch bei Kurzzeitschwankungen ≤ 20 % des RD-Signals, diese noch sicher erkannt wird.

![[Pasted image 20260515133840.png]]
**Abb. 9 Taktbildung Lesen**

![[Pasted image 20260515133902.png]]
**Abb. 10 Bildung des seriellen Eingangssignals ES für das Schieberegister bei niedriger Frequenz**

Der Ablauf des Lesens ist folgendermaßen organisiert:
Die Anschlußsteuerung wird durch den Steuer-PIO auf das Erkennen einer Marke programmiert (siehe Punkt 5.3.).
Nach Auswertung des Datenbytes der Marke wird bei positiven Ausgang (Marke erkannt) das Signal AM mit dem ZT-Takt verknüpft. Das entstandene Signal setzt einmal das Marken-FF (MKE = 1) und zum anderen den Zähler (A13). Mit dem nächsten T1_L-Taktimpuls wird der P1-Ausgang des Zählers 0. Am Ausgang A6.4/06 entsteht /BSTB, das die Übernahme des Datenbytes der Marke in den Daten-PIO veranlaßt. Der Zähler und das Schieberegister wird mit C1 (= T1_L) weitergeschaltet. Nach 16 Takten entsteht erneut /BSTB und das nächste Datenbyte wird übernommen usw.. Die Beendigung der Datenübertragung erfolgt mit dem Rücksetzen des Marken-FF durch /MR oder durch Abschalten von /STR.
Weiterhin besteht die Möglichkeit des sofortigen Umschaltens auf Schreiben durch /WE = 0 - Abschalten von T1_L (A6.4/11) - und /MR = 0 (Sperren von /BSTB).

### 5.6. Synchronisation der Datenübertragung

#### 5.6.1. Arbeit im DMA-Betrieb

Die Anschlußeinheit ist hauptsächlich für diese Arbeitsweise konzipiert. Mit ihr ist eine effektive Nutzung der Folienspeicher als externer Datenspeicher möglich.
Die ZRE K 2526 besitzt zwei Mikroprozessoren, wobei der zweite als programmgesteuerter DMA-Kanal arbeitet. Das Abschalten der zentralen CPU und die Umschaltung des Datenbusses auf die 2. CPU erfolgt durch das Signal /BUSRQ.
BUSRQ wird gebildet, wenn der Daten-PIO zum Datenaustausch mit dem Bus bereit ist. Seine Bereitschaft zeigt er durch RDY an. BUSRQ wird durch /STR unterdrückt. In einem 1 aus 8-Decoder (A3.3) werden die Signale /ARDY und /BRDY ausgewertet. Bei entsprechender Codierung entsteht das Signal /BUSRQ (A7/08).

**Wahrheitstabelle**

| Signal | Codierung | | | |
| :--- | :--- | :--- | :--- | :--- |
| /BRDY (AO) | 0 | 1 | 0 | |
| /ARDY (A1) | 0 | 0 | 1 | |
| Masse (A2) | 0 | 0 | 0 | |
| /BUSRQ | - | 0 | 0 | bei entsprechender |
| /WAIT | 0 | - | - | Brückenbestückung |
| aktiver Ausgang A3.3 | 00 | 01 | 02 | |

Die Umschaltung von /BUSRQ auf /WAIT wird in den nächsten Punkten beschrieben.

Ausgang 00: /ARDY = 0, /BRDY = 0
Bildung des /WAIT-Signals = Warten auf die Ein- oder Ausgabe des nächsten Bytes
Ausgang 01: /ARDY = 0, /BRDY = 1
Lesen - Ausgabe eines Bytes vom PIO an den BUS
Ausgang 02: /ARDY = 1, /BRDY = 0
Schreiben - Übernahme eines Bytes vom BUS an den PIO

#### 5.6.2. Synchronisation mit WAIT - ohne Simultanarbeit

Beim Einsatz des Adapters in zeitkritischen Geräten besteht die Möglichkeit, die Datenübertragung mit /WAIT zu synchronisieren.
Die Bedingung zur Bildung von /WAIT sind am Decoder A3.3 der Wahrheitstabelle (5.6.1.) zu entnehmen. Die erforderliche Brückenbestückung ist im Punkt 5.6.3. ersichtlich.

Bedingungen für /WAIT und /RDY: /CS = 0 - Auswahl (Daten-PIO = /RDY)
C/D = 0 - Datenübertragung
STR = 1

Das vom Systembus bereitgestellte Signal /IORQ wird bei dieser Arbeitsweise nur außerhalb des /WAIT-Zustandes auf der Steckeinheit wirksam.

#### 5.6.3. Brückenbestückung der Synchronisationssteuerung

Die erforderliche Betriebsart wird durch Einlöten von Brücken eingestellt.

| Betriebsart | Brücken zwischen den Lötpunkten            |
| :---------- | :----------------------------------------- |
| BUSRQ       | 17-19, 20-22, 24-27, 7-8, 9-10, 2-3, 25-28 |
| WAIT        | 18-19, 21-22, 23-26, 27-28, 9-8, 6-7, 1-2  |

Außerdem sind die Brücken W3 (entsprechend der gewünschten Adresse) sowie W2 und W10 zu bestücken.

### 5.7. Übertragungsfehler

Mit dem Starten der Datenübertragung erfolgt ständig eine Kontrolle des Informationsaustausches zwischen Daten-PIO und Systembus bzw. Schieberegister.

![[Pasted image 20260515134315.png]]
**Abb. 11 Bildung des Signals Fehler**

Mit STR wird der Grundzustand der FF's (A12.2) definiert.
STR = 0 /FA = 1 Speicherkreis gelöscht
STR = 1 Kontrolle auf Übertragungsfehler

Übertragungsfehler beim Lesen: /FA = 0, wenn BRDY = 0 bei der 0-1-Flanke von /BSTB
Das nächste Byte wird in den Daten-PIO übernommen, bevor die CPU die Daten abgefordert hat und /RDY wieder gesetzt ist.
Übertragungsfehler beim Schreiben: /FA = 0, wenn ARDY = 0 bei der 0-1-Flanke von /ASTB
Das nächste Byte soll in das Schieberegister übernommen werden, obwohl die CPU das neue Byte noch nicht bereitgestellt hat. Das Fehlersignal /FA liegt am Steuer-PIO an und kann dort ausgewertet werden.

### 5.8. Steckerbelegung

Der Anschluß X3 auf der K 5122 bildet die Verbindung zum Laufwerk MF 6400.

| Belegung | Bemerkung | Belegung | Bemerkung |
| :--- | :--- | :--- | :--- |
| A1 | 00 | B1 | 00 |
| A2 | /HL | B2 | /SD |
| A3 | /FR | B3 | /SE4 |
| A4 | /SE3 | B4 | /SE2 |
| A5 | /SET | B5 | /WD |
| A6 | /RDYL | B6 | /WE |
| A7 | /ST | B7 | /LCK4 |
| A8 | 00 | B8 | /LCK3 |
| A9 | /LCK2 | B9 | /LCK1 |
| A10 | 00 | B10 | /WP |
| A11 | /TO | B11 | /FW |
| A12 | 00 | B12 | /IX |
| A13 | 00 | B13 | /RD |

### III. Beschreibung spezieller Baugruppen

**Inhaltsverzeichnis**

1. Beschreibung der PLL-Schaltung
1.1. Prinzip
1.2. Schaltungsbeschreibung
1.3. Prüfung der PLL-Schaltung
1.3.1. Überprüfung der VCO-Frequenz
1.3.2. Statische Prüfung

#### 1. Beschreibung der PLL-Schaltung

##### 1.1. Prinzip

Die in der Beschreibung verwendeten Abkürzungen haben folgende Bedeutung:
PLL - phase lock loop, Phasenregelkreis, Phasenregelschleife
VCO - voltage controlled oscillator, spannungsgesteuerter Oszillator
PC - phase comparator, Phasenvergleicher

Die PLL ist ein Regelsystem, dessen Aufgabe darin besteht, einen Oszillator in Frequenz und Phase mit einem Eingangssignal zu synchronisieren. Sie erzeugt also Taktimpulse, die in fester Relation zum Eingangssignal stehen.
Die PLL besteht aus vier Funktionsblöcken. Diese sind der PC, Tiefpaß, VCO und Teiler. Abb. 1 zeigt das Blockschaltbild eines PLL-Systems.

![[Pasted image 20260515134527.png]]
**Abb. 1 Blockschaltbild**

Liegen keine Eingangsdaten an (/RD = 1), schwingt der Oszillator auf seiner Grundfrequenz f₀. Mit dem Anlegen des Eingangssignals vergleicht der PC Phase und Frequenz des Eingangs-signals mit der geteilten VCO-Frequenz und ermittelt daraus eine Fehlerspannung U_e. Sie ist das Verhältnis von Phase und Frequenzdifferenz zweier Signale. Der Tiefpaß wandelt das digitale Fehlersignal in eine Gleichspannungsdifferenz um, die durch einen Operationsverstärker verstärkt und an den Steuereingang des VCO geführt wird. Die verstärkte Fehlerspannung verändert die VCO-Frequenz solange, bis diese mit der Frequenz der Eingangsinformation identisch ist. Die PLL ist damit "eingerastet".
Eine Phasendifferenz zur Erzeugung der Fehlerspannung bleibt bestehen. Damit wird garantiert, daß die PLL eingerastet bleibt und Schwankungen des Eingangssignals folgen kann. Für die PLL sind folgende statische und dynamische Stabilitätsgrenzen definiert:

Haltebereich (hold-in-range)
Fangbereich (lock-in-range oder capture range)
Ziehbereich (pull-in-range)
Ausrastbereich (pull-out-range)

Der Haltebereich ist derjenige Bereich, innerhalb dessen die PLL statisch stabil arbeiten kann. In der Praxis wird der Haltebereich durch denjenigen Frequenzbereich definiert, der durch VCO ausgesteuert werden kann.
Der normale Betriebsbereich sollte der Fangbereich sein. In ihm erfolgt das Einrasten innerhalb einer Schwebung zwischen Eingangs- und Ausgangssignal.
Innerhalb des Ziehbereiches ist das Einrasten der PLL nach einer endlich langen Zeit gewährleistet. Die VCO-Frequenz "schaukelt" sich in einer gewissen Zeit (pull-in-time) auf den Wert der Eingangsfrequenz auf.
Der Ausrastbereich ist der maximale Frequenzsprung, der am Eingang angelegt werden kann, ohne daß das System ausrastet. Rastet die PLL infolge eines Störimpulses einmal aus, so würde das System mit einem Ziehvorgang wieder einrasten. Da dies unter Umständen sehr lange dauert, beschränkt man in den meisten Fällen den Arbeitsbereich auf den Fangbereich.

![[Pasted image 20260515134615.png]]
**Abb. 2 Statische und dynamische Stabilitätsgrenzen**

##### 1.2. Schaltungsbeschreibung

Das /RD-Signal gelangt über die Widerstandskombination (R8.4, R9.2) an den Schmitt-Trigger-Eingang des monostabilen Multivibrators (A15), der die Aufgabe hat, einen Impuls mit einer Breite von 780 ns zu erzeugen. Der Ausgang des UV's (A15/06) ist an den Phasenkomparator geführt, der aus zwei NAND-Gattern (A6.5/11, A6.5/08) besteht. Der PC vergleicht die Eingangsdaten (/RD) mit dem durch das FF (A12.5) geteilten VCO-Takt. Die Ausgänge des PC - Phase A und Phase B (A6.5/11, A6.5/08) gehen zu zwei symmetrisch aufgebauten Tiefpaß-Filtern, die aus den Widerständen R17.2, R5.1 sowie dem Kondensator C5.1 auf der einen Seite und R17.1, R5.2 und C5.2 auf der anderen Seite gebildet werden. Die Filterausgänge liegen an den positiven bzw. negativen Eingängen des Operationsverstärkers (A18). Die Verstärkung des Operationsverstärkers wird durch die Widerstände R19, R20 bestimmt. Sie sind so ausgelegt, daß ein maximaler Regelbereich des VCO erreicht wird. Der Regelbereich des VCO bestimmt die Größe des Fang- und Haltebereiches maßgebend. Die Dimensionierung der Konstantstromquelle (V1T, R7, R21, C8) und die Schaltschwelle des Komparators (A16) bestimmen die Grundfrequenz f₀ des VCO. Sie wird mit dem Dickschicht-Einstellregler R21 auf 2 MHz ± 1 % eingestellt. (/RD = 1).
Mit R9.1 und V2.5D wird der Transistor V1T auf seinen Arbeitspunkt eingestellt. Über die Emitter-Kollektorstrecke fließt ein konstanter Strom, der den Kondensator C8 auflädt. Wird die Schwellspannung des Komparators erreicht, erscheint an dessen Ausgang log. 1. Dadurch werden das Gatter von A9.1 umgeschaltet und der Kondensator C8 über den Widerstand R2 gegen Masse entladen. Gleichzeitig wird durch Veränderung des Spannungsteilers R14/R12.2 - R4 über A9/10 parallel zu R12.2 gegen Masse - die Referenzspannung des Komparators (A16/11) herabgesetzt. Unterschreitet die Spannung über C8 den Wert der Referenzspannung, schaltet der Ausgang (A16/09) wieder auf 0 und der Vorgang kann mit einer Kondensatoraufladung von vorn beginnen. Ändert sich nun die Spannung an der Basis des Transistors V1T infolge einer von f₀ abweichenden Eingangsfrequenz, so wird auch der Ladestrom des Kondensators verändert. Das wiederum ruft eine Veränderung der Zeitkonstante und somit der Frequenz des VCO hervor.
In den folgenden Impulsdiagrammen sind eingerastete PLL-Systeme bei unterschiedlichen Eingangsfrequenzen dargestellt:

![[Pasted image 20260515134647.png]]
**Diagramm 1 VCO = 2 MHz /RD = 500 kHz**

![[Pasted image 20260515134707.png]]
**Diagramm 2 VCO = 2,88 MHz /RD = 720 kHz**

![[Pasted image 20260515134751.png]]
**Diagramm 3 VCO = 760 kHz /RD = 380 kHz**

##### 1.3. Prüfung der PLL-Schaltung

In der Fertigung erfolgt die Prüfung der PLL-Schaltung mit dem Wobbelgenerator WG 500, der speziell für diesen Anwendungsfall entwickelt wurde.
Steht dieses Gerät nicht zur Verfügung, können einige statische Messungen mit folgenden Meßgeräten durchgeführt werden:
Oszillograf
Zähler
Impulsgenerator

Die Anschlußsteuerung muß mit folgenden Signalen und Spannungen beschaltet werden:

| Steckverbinder X1 | | |
| :--- | :--- | :--- |
| | A1/C1 | = Masse |
| | A15 | = 5 N |
| | A28/C28 | = 12 P |
| | A29/C29 | = 5 P |
| **Steckverbinder X3** | B13 | = /RD Brücken 29-30 und 31-32 |

###### 1.3.1. Überprüfung der VCO-Frequenz (2 MHz ± 1 %) - /RD = 1

Zähler an Steckverbinder X2 - C16 oder Brücke 23-24 (MP2) anschließen.
Korrektur der VCO-Frequenz mittels R21.

###### 1.3.2. Statische Prüfung

Impulsgenerator an /RD - X3/B13 anschließen.
Folgende Impulsform einstellen: (0,6 µs Impuls, 2 µs Periode für /RD)

![[Pasted image 20260515134833.png]]
Oszillograf an X2 - C16 (MP2) und MP3 (/RD) anschließen. PLL ist eingerastet, wenn VCO- und /RD-Frequenz synchron laufen, d. h. jeder 4. VCO-Taktimpuls muß innerhalb der /RD-Information liegen. (2 µs Periode, 0,78 µs Puls für /RD, VCO Pulse darunter).

![[Pasted image 20260515134846.png]]
Durch Verändern der /RD-Frequenz läßt sich der Halte- und Ziehbereich annähernd bestimmen. Eine dynamische Messung, die erst eine Aussage über die volle Funktionsfähigkeit der PLL-Schaltung geben kann, erfolgt mit dem Wobbelgenerator WG 500.

![[Pasted image 20260515134907.png]]
**Abb. 3 PLL-Schaltung**

### IV. Kurzzeichenübersicht

| Kurzzeichen | englisch                | deutsch                           |
| :---------- | :---------------------- | :-------------------------------- |
| AB0 ... AB7 |                         | Adressenbus                       |
| AM          | adress marker           | Adressenmarke                     |
| ARDY        | A-ready                 | Quittungssignal des PIO Tor A     |
| ASTR        | A-strobe                | Steuersignal des PIO Tor A        |
| BAI         | bus acknowledge input   | Bus-Bestätigung-Eingabe           |
| BAO         | bus acknowledge output  | Bus-Bestätigung-Ausgabe           |
| BRDY        | B-ready                 | Quittungssignal des PIO Tor B     |
| BSTR        | B-strobe                | Steuersignal des PIO Tor B        |
| BUSRQ       | bus request             | Bus anfordern                     |
| C           | clock                   | Takt                              |
| CRC         | cyclic redundancy check | zyklisches Prüfzeichen            |
| DIEN        |                         | Steuersignal für U 216            |
| ES          |                         | Eingang Schieberegister (seriell) |
| FA          | fault adapter           | Fehler Adapter                    |
| FR          | fault reset             | Fehler rücksetzen                 |
| HL          | head load               | Kopf laden                        |
| HF          |                         | hohe Frequenz                     |
| ID          | identizing label        | Identifikationsmarke              |
| IEI         | interrupt enable input  | Unterbrechungsgenehmigung Eingabe |
| IEO         | interrupt enable output | Unterbrechungsgenehmigung Ausgabe |
| INT         | interrupt               | Unterbrechung                     |
| IODI        | input/output disable    | Eingabe/Ausgabe abschalten        |
| IORQ        | input/out request       | Eingabe/Ausgabe anfordern         |
| IX          | index                   | Index                             |
| LCK         | lock                    | verriegeln                        |
| M1          |                         | Maschinenzyklus                   |
| MK          | mark                    | Marke                             |
| MKE         |                         | Marke erkannt                     |
| MR          | mark reset              | Marke rücksetzen                  |
| MP          |                         | Meßpunkt                          |
| PLL         | phase lock loop         | Phasenregelschleife               |
| PR          |                         | Präcompensation rückwärts         |
| PV          |                         | Präcompensation vorwärts          |
| RD          | read data               | Lesedaten                         |
| RDY         | ready                   | Bereitschaft                      |
| RK          |                         | Rückkopplung                      |
| SD          | step direction          | Schrittrichtung                   |
| SE          | select                  | Laufwerksauswahl                  |
| ST          | step                    | Schritt                           |
| STR         |                         | Start                             |
| SYN         |                         | synchron                          |
| TO          | track 00                | Spur 00                           |
| WD          | write data              | Schreibdaten                      |
| WE          | write enable            | Schreibgenehmigung                |
| WP          | write protect           | Schreibschutz                     |
| WR          | write                   | Schreiben                         |

**robotron**
VEB Robotron
Buchungsmaschinenwerk
Karl-Marx-Stadt
Annaberger Straße 93
PSF 129
DDR Karl-Marx-Stadt
9010

Exporteur:
Robotron - Export/Import
Volkseigener Außenhandelsbetrieb
der Deutschen Demokratischen Republik
Allee der Kosmonauten 24
PSF 11
DDR Berlin
1140

832.53.01.004 (GER)
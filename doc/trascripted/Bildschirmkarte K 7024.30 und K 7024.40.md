# Bildschirmkarte K7024

**robotron**

**Anschlußsteuerung K 7024.30**
**K 7024.40**

### Betriebsdokumentation

3. Auflage
Karl-Marx-Stadt, 1985

© VEB Kombinat Robotron, 1985

### Inhaltsverzeichnis

I. Allgemeines
II. Technische Daten
III. Funktionsbeschreibung
IV. Bestimmung des Bildinhaltspeicher-Adreßbereiches
V. Programmierung des Zeichengenerators
Anschluß Monitor K 7022
Anlage 1 bis 3

**I. Allgemeines**

Mit Hilfe der Anschlußsteuerung K 7024.30 und K 7024.40 kann der Monitor K 7222.11 (Einbaugerät) und K 7222.21 (Auftischgerät) am Systembus K 1520 angesteuert werden. Der Adapter K 7024.40 hat gegenüberdem K 7024.30 eine zusätzliche Steuerelektronik zur Darstellung frei wählbarer Schirmbildfelder "Invers".

Die Steckeinheiten enthalten einen Bildinhaltspeicher mit einer Kapazität von 2 KByte, einen programmierbaren Zeichengenerator und die Steuerelektronik zur Erzeugung eines Schirmbildes im Format 24 Zeilen mit 80 Zeichen. Maximal 116 alphanumerische Zeichen oder quasigrafische Zeichen können im Rasterfeld 8 x 12 Bildpunkte im Zeichengenerator gespeichert werden. Anschlußsteuerung und Monitor sind durch ein abgeschirmtes 6adriges Kabel verbunden.

**II. Technische Daten**

Steckeinheitenabmessungen: 215 mm x 170 mm
Steckraster: 20 mm
Steckverbinder: 2 x 58-polig, indirekt
Bauform 304-58, TGL 29331/03
Steckverbinder indirekt
STL 102-10 TGL 29331/04 oder
STL 102-5 TGL 29331/04 -7 PdAu
Monitoranschluß: 3 Steuerleitungen (/VIDEO, /BSYN, /INTENS)
Einsatzbedingungen: 5/60/30/95/10⁻¹E

Stromversorgung:

Betriebsspannung | Stromaufnahme
:--- | :---
5 P: + 5 V ± 5 % | ca. 2,0 A
12 P: + 12 V ± 5 % | ca. 0,15 A
5 N: - 5 V ± 5 % | ca. 0,1 A

Ein- und Ausgangsleitungen zum Systembus K 1520:

16 Adreßleitungen AB0 ... AB15
(Eingänge Low-Power-Schottky-TTL)

8 Datenleitungen DB0 ... DB7
(Ein- und Ausgänge Low-Power-Schottky-TTL)

6 Steuerleitungen:
/MREQ, /WR, /RD, /MEMDI, /RFSH, /RESET

1 Steuerleitung: /RDY (Ausgang TTL-Pegel)

Bildwiederholspeicher-Anfangsadresse: von 0000_H bis F800_H in 2 KByte-Raster
Anzeigekapazität: 1920 Zeichen
Anzahl der Zeilen: 24
Zeichenzahl/Zeile: 80
Positionsraster: 8 x 12 Bildpunkte
Bildablenkfrequenz: 51 Hz
Zeichenabstand: 1 Punkt
Zeilenabstand: 2 Linien
Zeichenumfang: 116 darstellbare Zeichen
Zeichencode: 7-Bit-Code entsprechend TGL 23207/01
Zeichengenerator: 2 Stück EPROM
Helligkeitsstufen: Normal, hell oder Intensiv hell
Darstellungsarten: Normal, bei K 7024.40 Normal und Invers
Kursor: wahlweise blinkend oder ruhend

**III. Funktionsbeschreibung**

Inhaltsverzeichnis

1. Prinzip der Erzeugung des Schirmbildes
2. Blockschaltbild
3. Taktgenerator
4. Punktzähler
5. Zeichenpositionszähler
6. Linienzähler
7. Zeilenzähler
8. Bildinhaltspeicher
9. Einzeichenspeicher
10. Zeichengenerator und Parallelserienwandler
11. Videosignalerzeugung
12. Synchronsignalerzeugung
13. Kursoraufbereitung
14. Lese-, Schreibsteuerung, Adressen- und Datenumschaltung
15. Intensitäts- und Inverssteuerung

**1. Prinzip der Erzeugung des Schirmbildes**

Die Darstellung der Zeichen auf dem Bildschirm des an einer Anschlußsteuerung angeschlossenen Monitors erfolgt nach dem Prinzip der Bilddarstellung des Fernsehens. Der Schreibstrahl wird mit einer Horizontalablenkfrequenz von ca. 15,4 kHz und einer niedrigen Vertikalablenkfrequenz von 51 Hz über den Bildschirm abgelenkt und dabei in seiner Intensität gesteuert. Die Anschlußsteuerung ABSK 7024.30 und K 7024.40 ist so aufgebaut, daß 1920 Zeichen im Raster zur Abbildung kommen, d. h. 80 Zeichen auf 24 Zeilen. Jedes Zeichen besteht aus 12 Horizontallinien zu je 8 Bildpunkten, wobei der 8. Bildpunkt stets dunkel getastet wird und den horizontalen Zeichenabstand bildet.

**Zeichendarstellung**

| | 1. | 2. | 3. | 4. | 5. | 6. | 7. | 8. | Bildpunkt |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1. | | | | | | | | | |
| 2. | X | X | X | X | X | | | | |
| 3. | | X | | | | X | | | |
| 4. | | X | | | | | X | | |
| 5. | | X | | | | X | | | |
| 6. | | X | X | X | X | | | | |
| 7. | | X | | | | X | | | |
| 8. | | X | | | | | X | | |
| 9. | | X | | | | X | | | |
| 10. | X | X | X | X | X | | | | |
| 11. | | | | | | | | | |
| 12. | X | X | X | X | X | X | X | | Horizontallinie |

![[Pasted image 20260515151111.png]]
![[Pasted image 20260515151140.png]]
**Abb. 1: Blockschaltbild ABS K 7024.30/K 7024.40**

Jede Horizontallinie beinhaltet 640 Bildpunkte. Das gesamte Bildfeld hat 288 Horizontallinien. Die 11. und 12. Linie eines Zeichens ist der Zeichenabstand zum Kursor und dem Kursor vorbehalten. Im 2 KByte-Bildinhaltspeicher ist jeder Zeichenposition auf dem Bildschirm eine feste Adresse zugeordnet. Die 1. Speicherzelle des 2 KByte RAM-Speichers entspricht der 1. Zeichenposition auf dem Bildschirm usw.. Die Anfangsadresse ist auf der STE 012-6821 bzw. Typ 062-8640 hardwaremäßig festgelegt. Durch Zeichenpositions- und Zeilenzähler oder bei Anforderung durch System- und Koppelbus wird dem Bildinhaltspeicher die aktuelle Zeichenpositionsadresse mitgeteilt. Sie entspricht der Position des Schreibstrahls auf dem Bildschirm. Die Codierung des auf der Speicheradresse gespeicherten Zeichens ist Teil der Adresse für den Zeichengenerator, der über einen Parallel-Serienwandler dem Helltastverstärker im Monitor zugeführt wird und die Intensitätssteuerung des Schreibstrahls bewirkt.

**2. Blockschaltbild**

Zum besseren Verständnis der Funktion der Anschlußsteuerung K 7024.30 ist in Abb. 1 das Blockschaltbild dargestellt. Die gestrichelte Linie veranschaulicht die Zusatzelektronik für die Inverssteuerung.

**3. Taktgenerator**

Alle Zeitabläufe bei der Darstellung der Zeichen auf dem Bildschirm werden von einer quarzstabilisierten Rechteckimpulsfolge mit einer Frequenz von 13,83 MHz und einem Tastverhältnis von 0,5 (TTL-Pegel) gesteuert. Der Taktgenerator besteht aus IS A12, dem Quarz V1, R4, R7 und C7. Aus der Periodendauer von 72,5 ns ergibt sich die Schreibzeit für einen Bildpunkt.

**4. Punktzähler**

Der Punkttakt UPT steuert eine achtstellige Schiebekette (A31, A32, A33, A34), so daß nach jedem Bildpunkttakt die "low"-Information auf den D-Eingang des nachfolgenden FF's gelangt. Nach 8 Bildpunkten erfolgt ein neuer Umlauf. Zu diesem Zeitpunkt sind alle Ausgänge der Schiebekette high und Ausgang A51/08 ist low. Dieses Potential am Eingang A31/12 startet einen neuen Umlauf (siehe Diagramm 1).

![[Pasted image 20260515151219.png]]
**Diagramm 1: Punktzähler-Ausgangssignal**

**5. Zeichenpositionszähler**

Der Zeichenpositionszähler wird mit dem 1. Bildpunkttakt gesteuert. Er besteht aus einem Binärzähler (A45/A46), an dessen Ausgängen parallel 3 Konjunktionsgatter zur Entschlüsselung des 81. (A66), 90. (A68) und 108. (A64) Zählimpulses angeschlossen sind. Mit dem 1. Bildpunkttakt beim 81. Zeichen das FF A47 gesetzt und das Dunkeltastsignal für den Zeilenrücklauf HOR mitlow gebildet (A47/06). Es bewirkt bei der Videosignalerzeugung das Dunkeltasten ab Zeichen 82. Mit dem 1. Bildpunkt des 1. Zeichen wird das FF wieder rückgesetzt und das Signal HOR = 1 (inaktiv). Der 5. Bildpunkt und der Zählertakt 108 sperrt den Zählereingang A46 über A47/12. Mit dem 1. Bildpunkt des gleichen Zählerstandes wird über das Nand A47/03 der Binärzähler auf den Zählerstand "0" geladen. Damit wird auch die Zählersperre am Nand A47/12 wieder aufgehoben. Der aktuelle Zählerstand des Zeichenpositionszählers bildet nach teilweiser Verschlüsselung die unteren 7 Bit der zum Lesen des Bildinhaltspeichers benötigten Adresse (siehe 8.).

**6. Linienzähler**

Der Linienzähler A49 ist ein Binärzähler mit dem Zählbereich 0 ... 11 (dezimal) entsprechend dem Bildaufbau von 12 Linien pro Zeichenzeile. Getaktet wird der Zähler mit dem Zeichenpositionszählertakt 81. Die Ausgänge LP0, LP1, LP2, des Zählers bilden einen Teil der Adresse für den Zeichengenerator. LP3 bzw. /LP3 werden als /CS verwendet. Die nachfolgende Logik (A29) erkennt die 12. Linie und das FF A10/08/11 wird gesetzt. Über das Nand A48/11 wird der Linienzähler zum Zeitpunkt 1. Bildpunkt mit "0" geladen (Grundstellung), gleichzeitig wird der Takteingang des Zählers A49 über das Nand A48/02 gesperrt.

Zeitlicher Ablauf des Löschvorganges:
PZ 1 Linienzähler auf 12
:
PZ 5 12. Linie FF kippt an
:
PZ 1 Linienzähler auf "0" (Löschen) (A48/11)
:
PZ 5 12. Linie FF kippt zurück - Freigabe Linienzähler (A48/03)

**7. Zeilenzähler**

Der Zeilenzähler wird mit dem vom Linienzähler kommenden Takt /LP3 angesteuert. Der Zeilenzähler zählt entsprechend dem Bildaufbau von 0 bis 23 (dezimal), das entspricht 24 Zeilen. Ist die 12. Linie der letzten Zeile geschrieben, sind alle unnegierten Ausgänge der Zählkette A25/A26/A27 high, ebenso der Linienzählertakt /LP3. Das Mehrfachnand A24 schaltet den Ausgang auf low und setzt das FF A27.

Für die Zeit von 1,5 ms (≙ der Schreibzeit für 2 Zeilen) wird das Signal BR (Bildrücklauf) - mit low aktiv - erzeugt. Dies führt zur Dunkeltastung des Bildschirms während des Bildrücklaufes. Dieses Signal ist also aktiv, nachdem die 12. Linie der 24. Zeichenzeile geschrieben worden ist. Das Signal BR setzt das FF A27/13 zurück, das entspricht dem Zählerstand 0. Damit wird auch der Ausgang A24/08 high und gibt das FF A27 frei. Die nächstfolgende Flanke (aufsteigend) vom Linienzähler /LP3 = 1 schaltet die "0" des D-Einganges auf den gleichen Eingang des FF A28 (eine Zeile). Die 2. Schaltflanke /LP3 = 1 schaltet das 2. FF und BR wird inaktiv und gibt auch den Zeilenzähler frei (2. Zeile).

**8. Bildinhaltspeicher**

Die maximale Speicherkapazität des RAM-Bildinhaltspeichers beträgt 2048 x 8 Bit. Die darzustellenden Zeichen werden im 7-Bit-Code gespeichert, wobei mit dem 8. Bit festgelegt wird, ob das betreffende Zeichen mit oder ohne Kursor auf dem Bildschirm angezeigt werden soll. Entsprechend der Bildmatrix 24 x 80 = 1920 Zeichen werden für die Anzeige die Speicherplätze 0 bis 1919 (siehe Abb. 2) belegt und angezeigt. Über den Systembus können jedoch alle 2048 Speicherplätze beschrieben und gelesen werden. Die Adressierung des 2 KByte-Speichers erfolgt über 11 Adreßleitungen (A0 ... A9) und /CS bzw. CS. Die Blockauswahl der beiden 1 KByte-Blöcke erfolgt über /CS und CS. Über die beiden Adderbausteine A43/A44 wird eine Speicheradressenumcodierung erreicht. Aus 7 Zeichenpositionszähler- und 5 Zeilenzählerausgängen werden 11 Adreßleitungen gebildet. Das ist erforderlich, da durch die Zeichenzahl 80 pro Zeile die direkte Verwendung der Zählerausgänge als Adresse nicht möglich ist. Deshalb wird der Information des Zeichenpositionszählers bei jeder neuen Zeile mit der Information des Zeilenzählers durch Addition in den Adderschaltkreisen A43/A44 korregiert. Damit ergeben sich die Zeichen am Zeilenanfang die Dezimalwerte 0, 80, 160 ... 1840.

x + 0                                                                                         x + 1919                                    x + 2048
| <-- Umlaufbereich für Anzeige von 1920 Zeichen --> | <-- / (128 freie Bytes)  --> | 

**Abb. 2: Bildinhaltspeicherorganisation**

Die Steuerung des Bildinhaltspeichers erfolgt über die Signale /WE und /CS. Sind beide aktiv (low), werden die einzuschreibenden Daten über die Dateneingänge DI in den aktuell adressierten Speicherplatz transportiert. Über die DO-Ausgänge können die Daten des adressierten Speicherplatzes gelesen und im Einzeichenspeicher A101 und A122 zwischengespeichert werden. Bei entsprechender Steuerung werden sie über die Datentreiber A81 und A121 auf den Systembus ausgegeben.

Folgende Funktionszustände des Speichers sind möglich:

| | /CS | /WE |
| :--- | :--- | :--- |
| DO hochohmig | high | unbestimmt |
| Schreiben | low | low |
| Lesen | low | high |

**9. Einzeichenspeicher**

Das beim Lesen des Bildinhaltspeichers auf den Ausgangsleitungen liegende Datenbyte wird im Einzeichenspeicher A101 und A122 in ISO-7-Bit-Codierung gespeichert. Ist ZT1 am V-Eingang des 4-Bit-Schieberegisters high, wird mit der H-L-Flanke des Punkttaktes die Information an den A0 ... A4-Eingängen übernommen und gleichzeitig auf den Ausgang geschaltet. Bis zum nächstfolgenden 1. Bildpunkt des nachfolgenden Zeichens bleibt die Information erhalten. Sie ist ein Teil der Adresse für den Zeichengenerator.

**10. Zeichengenerator und Parallel-Serienwandler**

Der Zeichengenerator wird durch 2 EPROM-Schaltkreise (A103 und A123) mit je 1 KByte Speicherkapazität gebildet. In ihnen sind max. 116 verschiedene alphanumerische Zeichen bzw. Symbole im Punktraster 8 x 12 gespeichert, wobei in einem die Bildpunkte der 1. bis 8. Linie und im anderen die Bildpunkte der Linie 9 bis 12 gespeichert sind. Für jedes Zeichen sind 12 Byte belegt. Jedes Byte beinhaltet das Punktraster des Zeichens für eine bestimmte Zeilenlinie (siehe Abb. 3).

| Byteadresse | | | | | | | | | | hex. | Byte-Inhalt | | | | | | | | hex. | Linien-Nr. |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| A9 | A8 | A7 | A6 | A5 | A4 | A3 | A2 | A1 | A0 | | D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 | | |
| 1 | 0 | 0 | 0 | 1 | 0 | 1 | 0 | 0 | 0 | 228 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 00 | 1 |
| 1 | 0 | 0 | 0 | 1 | 0 | 1 | 0 | 0 | 1 | 229 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 0 | FE | 2 |
| 1 | 0 | 0 | 0 | 1 | 0 | 1 | 0 | 1 | 0 | 22A | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 80 | 3 |
| 1 | 0 | 0 | 0 | 1 | 0 | 1 | 0 | 1 | 1 | 22B | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 80 | 4 |
| 1 | 0 | 0 | 0 | 1 | 0 | 1 | 1 | 0 | 0 | 22C | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 80 | 5 |
| 1 | 0 | 0 | 0 | 1 | 0 | 1 | 1 | 0 | 1 | 22D | 1 | 1 | 1 | 1 | 1 | 0 | 0 | 0 | F8 | 6 |

**Abb. 3: Programmierbeispiel des Zeichengenerators Buchstabe E ISO-7-Bit-Code 45_H**

Zur Adressierung eines Bytes sind 10 Adreßleitungen vorgegeben. Die Adreßleitung A0 ... A2 werden durch den aktuellen Linienzählerstand gebildet, wobei das 4. Bit des Linienzählers durch /CS//WE die Auswahl der beiden Chips bewirkt. A3 ... A9 werden durch den ISO-7-Bit-Code des Zeichens selbst bestimmt. 
Das 8. Bit des Zeichens wird gesondert im Funktionsblock "Kursoraufbereitung" ausgewertet und ist nicht Inhalt des Zeichengenerators. Da das adressierte Byte an den Ausgängen des Zeichengenerators parallel anliegt, aber die Verarbeitung der den Bildpunkten entsprechenden Bits seriell erfolgt, ist ein als Parallel-Serienwandler arbeitendes Schieberegister A104 und A124 nachgeschaltet.
Der 8. Bildpunkttakt übernimmt das anliegende Byte ins Schieberegister und mit der Frequenz des an den Steuereingängen C1 und C2 liegenden Punkttaktes wird die Information hinausgeschoben.

![[Pasted image 20260515151956.png]]
**Abb. 4: Erzeugung des Signals /BSYN**

![[Pasted image 20260515152643.png]]
**Abb. 5: Impulsdiagramm zu Abb. 4**

**11. Videosignalerzeugung**

In dem 8-fach Nand A88 wird das Signal VIDEO erzeugt. Dieses Signal steuert in der Bildröhre des Monitors die Intensität des Strahlstromes.
Es wird mit folgenden Signale verknüpft:

- Dunkeltastsignal bei Bildrücklauf /BR (A28/08)
- Dunkeltastsignal bei Zeilenrücklauf /HOR (A47/06)
- Dunkeltastsignal beim Anliegen der Geräteadresse
- Dunkeltastsignal gebildet beim Anliegen des Steuersignals /RESET = 0.

**12. Synchronsignalerzeugung**

Das zur Erzeugung des Schirmbildes im Monitor erforderliche Synchronsignal /BSYN (siehe Impulsdiagramm 6 Abb. 5) wird aus den Positionszählertakten 40, 81 und 90 und den Linienzählertakten /LP2 und /LP3 erzeugt. Die Bildung des Signals ist aus nachfolgendem Impulsdiagramm ersichtlich.

**13. Kursoraufbereitung**

Ein Zeichen wird auf dem Bildschirm des Monitors mit Kursor dargestellt, wenn das 8. Bit des darzustellenden Zeichens high ist. Eingeschrieben wird das 8. Bit in das FF A28/05 mit dem 8. Bildpunkt. Soll ein Kursor dargestellt werden, ist der Ausgang A28/05 = low. Es wird unabhängig vom Pegel des Videosignals ein heller Bildpunkt zur Anzeige kommen. Aber nur dann, wenn der Setzeingang des A28/04 high ist. Diese Freigabe erfolgt nur ab der 11. Linie des Zeichens (Linienzählertakte LP0, LP1, /LP3 = 1 - A85/06 - A86/08). Soll der Kursor blinken, muß die Wickelbrücke X14:2 und X13:2 geschlossen sein. Am Binärzähler A89 wird das Signal BR frequenzmäßig auf 3 Hz untersetzt (Bildrücklauffrequenz 51 Hz; Zähltakt 16 ergibt 3 Hz). Dieser Takt steuert über A85/06; A86/08 den Setzeingang des FF's A28/05.
Für den ruhenden Kursor muß die Brücke X14:1 und X13:1 geschlossen sein.

|                 | **B r ü c k e** |             |
| :-------------- | :-------------- | :---------- |
| Kursor blinkend | X13:2 - X14:2   | geschlossen |
| Kursor ruhend   | X13:1 - X14:1   | geschlossen |

**14. Lese-, Schreibsteuerung, Adressen- und Datenumschaltung**

Die Lese- und Schreibsteuerung stellt die Verbindung zwischen dem K 1520-Systembus und dem Bildinhaltspeicher dar. Dabei erfolgt die Ansteuerung mit dem gleichen Steuersignalen wie bei Speichersteckeinheiten.
Der Adreßbus (AB0 ... AB15) und das Steuersignal /MREQ = 0 (Speicheranforderung) bilden die gültige Speicheradresse für den Bildinhaltspeicher (/MEMDI = 1; /RFSH = 1).
Liegt eine Busanforderung vor, wird die am Adreßbusliegende Adresse decodiert, d. h. Ausgang des Nand A6/08 ist low. Das bedeutet "/Adresse erkannt" (siehe 12.). Die internen Adressen werden gesperrt (Eingang A42/13 ist low, A41/01 ist high, d. h. der Ausgang wird hochohmig) und die Treiber A21 und A22 freigegeben.
Die Datenbustreiber A81/A121 sind über die Eingänge /DIEN in Abhängigkeit vom Signal /RD (Nand A7/03) richtungsgesteuert.
Der Bildinhaltspeicher wird beschrieben, wenn /WE-Eingang durch das Steuersignal /WR = 0 ist (A21/02). Über den DI-Eingang wird die adressierte Speicherzelle beschrieben. Mit Ende des Speicherverkehrs werden Treiber und die Freigabe wieder weggeschaltet. Über die Adderbausteine A43/A44, dem Treiber A42 und dem bidirektionalen Treiber A41 wird der Bildwiederholspeicher intern adressiert. (Umlaufadressenbildung Pkt. 8.).
Da keine Busanforderung durch die ZRE erfolgt, ist der Ausgang A6/08 = high, d. h. der bidirektionale Treiber A41 undder Treiber A42 sind über /CS und CS2 aktiv geschaltet. Gleichzeitig ist der Treiber A21 und der Adreßtreiber A22 inaktiv über die Steuereingänge /CS bzw. CS2, also hochohmig.
Die Auswahl der jeweils 1 KByte-Speichergruppen erfolgt über die Decodierung der Zeichenzählerstände 2³ und 2⁴ durch das Adder A43.

**K 7024.40: Lesesperre**

Über die /DIEN-Eingänge der Datenbustreiber A81/A121 wird beim Lesen auf den Systembus der Datenweg zum Bus gesperrt, wenn die Verbindung X15:1 - X16:1 vorhanden ist.
Die Lesesperre ist erforderlich, wenn vom Anwender die letzten 2 KByte des Gesamt-RAM-Speichers genutzt werden sollen.
Ist die Lesesperre nicht erforderlich, so wird Kontakt X15:2 - X16:2 verbunden.

Der Grundzustand der Anschlußsteuerung ergibt sich nach dem infolge Netzeinschaltung aktiv geschaltetem Signal /RESET. Durch /RESET = 0 wird der statische Haltekreis A8/08/11 gesetzt und sperrt die Anzeige des Videosignals (Nand A88/08). Wird /RESET inaktiv, ist das Nand wieder freigegeben. Mit dem Signal "/Adresse erkannt" am A6/08 bildet sich über A7/08 - A8/03 das Quittungssignal /RDY = 0.

**15. Intensitäts- und Inverssteuerung**

Es sind für die Intensitäts- und Inverssteuerung 12 Attributszeichen festgelegt. (Zeichen 0/4 bis 0/F der Codetabelle für ISO-7-Bit-Code).
Bei den Interfacesteckeinheiten K 7024.40 und K 7024.30 bewirkt das Datenbit DB1 = 1 die Einschaltung der Intensiv-Helligkeit und DB1 = 0 das Zurückschalten auf die normale Helligkeit.
Die Datenbits DB0 ... DB6 werden dem Einzeichenspeicher (A101, A122) entnommen und einer Erkennungslogik zugeführt.
Ein decodiertes Attributszeichen mit DB1 = 1 schaltet das FF A62/11, A62/03 ein. Das wiederum aktiviert das Signal INTENS über A63/05, A63/08, A91/09, A86/03 und A90/08/11.
Das FF A84/03/06 gibt die Erkennungslogik im Zeitraum 1. bis 81. Zeichen frei (Nand A85/08).
Eine Inverssteuerung ist nur auf der STE K 7024.40 realisiert.
Die Steuerung erfolgt durch das Datenbit DB0 mit folgender Codierung:

|              | DB1 low            | DB1 high         |
| :----------- | :----------------- | :--------------- |
| **DB0 low**  | normale Darstellg. | Intensiv         |
| **DB0 high** | Invers             | Intensiv, Invers |

Ein decodiertes Attributszeichen mit DB0 = 1 schaltet das FF A52/06/08 ein und bewirkt am Exklusiv-Oder A109 verknüpft mit dem Signal VIDEO eine Inversdarstellung (A109/13 ist high). Mit DB0 = 0 wird die Inversdarstellung abgeschaltet.
Die FF's A63, A7 und A91 bewirken ein Abschalten der Intensität und/oder der Inversdarstellung am Linienrand und über Übernahme in die nächste Zeile.
Ist die Brücke X15 und X17 geschlossen, wird die 12. Linie (Kursorlinie) stets intensiv hell geschrieben.
Eine Darstellung ab 12. Linie entsprechend der quasigrafischen Darstellung des Bildes wird erreicht, wenn X16 und X17 geschlossen ist (statt X15 - X17) - sollte der helle Kursor vom Anwender störend empfunden werden.

**IV. Bestimmung des Bildinhaltspeicher-Adreßbereiches**

Das Freigabesignal "Adresse erkannt" (A6/08) wird mit low aktiv, wenn alle Eingänge des Nand A6 high sind. Die Eingänge des 2-Eingangs-Exklusiv-Oder-Bausteines A4/A5 sind alle so beschaltet, daß entweder über die geschlossene Brücke X11 ... X12 low am Eingang liegt und der andere Eingang bei entsprechender Adreßleitung high ist (Ausgang dann high) oder eine offene Brücke (≙ 1), dann wäre die entsprechende Adreßleitung low.
Wird also die Anfangsadresse des Bildinhaltspeichers auf dem Adreßbus X1 angeboten, kann sie decodiert werden und das erste Byte des Bildinhaltspeichers wird angesteuert.
Die Anfangsadresse dieses Speichers kann wahlweise im 2 KByte Raster festgelegt werden. Tabelle 1 zeigt die Zuordnung der Drahtbrücken X11 ... X12.

X = Brücke vorhanden

| Adreßbereich Bildinhaltspeicher | X11:1 X12:1 | X11:2 X12:2 | X11:3 X12:3 | X11:4 X12:4 | X11:5 X12:5 |
| :------------------------------ | :---------: | :---------: | :---------: | :---------: | :---------: |
| 0000_H - 07FF_H                 |      -      |      -      |      -      |      -      |      -      |
| 0800_H - 0FFF_H                 |      -      |      -      |      -      |      -      |      X      |
| 1000_H - 17FF_H                 |      X      |      -      |      -      |      -      |      -      |
| 1800_H - 1FFF_H                 |      X      |      -      |      -      |      -      |      X      |
| 2000_H - 27FF_H                 |      -      |      X      |      -      |      -      |      -      |
| 2800_H - 2FFF_H                 |      -      |      X      |      -      |      -      |      X      |
| 3000_H - 37FF_H                 |      X      |      X      |      -      |      -      |      -      |
| 3800_H - 3FFF_H                 |      X      |      X      |      -      |      -      |      X      |
| 4000_H - 47FF_H                 |      -      |      -      |      X      |      -      |      -      |
| ...                             |      :      |      :      |      :      |      :      |      :      |
| F000_H - F7FF_H                 |      X      |      X      |      X      |      X      |      -      |
| F800_H - FFFF_H                 |      X      |      X      |      X      |      X      |      X      |
|                                 |    ≙ 4 K    |    ≙ 8 K    |   ≙ 16 K    |   ≙ 32 K    |    ≙ 2 K    |
|                                 |             |             |             |             |             |

**Tabelle 1: Schaltverbindungen für Anfangsadresse Bildinhaltspeicher**

**V. Programmierung des Zeichengenerators**

Die Anwendungsmöglichkeiten eines Bildschirmgerätes sind vielfältig. Die unterschiedlichen Anwendungsfälle verlangen auch einen unterschiedlichen Zeichenvorrat wie lateinische, kyrillische, länderspezifische Sonderzeichen oder grafische Symbole, d. h. der Zeichengenerator wird inhaltlich den jeweiligen Anwendungsfällen angepaßt.
Als Hilfsmittel zur Erarbeitung des EPROM-Inhaltes und der entsprechenden Adressenzuordnung dient Anlage 2. Tabelle 2 zeigt den ISO-7-Bit-Code des gewünschten Zeichens. Dieser Code wird in Tabelle 3 eingetragen.
Bedingt durch das Darstellungstaktprinzip des Zeichens durch ein 10 x 8 Punktraster (einbezogen der Zeichenzwischenraum) wird jedes Zeichen 10 Byte Speichervolumen benötigen. 1 Byte trägt dabei die Information einer Zeilenlinie (siehe Tabelle 3).
Durch den verwendeten EPROM-Schaltkreis stehen zur Adressierung 10 Adreßleitungen zur Verfügung. Die Adreßleitungen A0 bis A2 am Chip werden durch den aktuellen Linienzählerstand gebildet. Der Linienzählerstand /LP3 bewirkt die Auswahl des jeweils ausgewählten EPROM's über die Steuereingänge /CS//WE. Die restlichen 7 Bit der Adresse sind durch den Zeichencode bestimmt.
Der Code des Zeichens und der Linienzählerstand - eingetragen in Tabelle 3, Anlage 2 - ergeben die vollständige Adresse am Zeichengenerator für beide EPROM-Schaltkreise. Die auf diese Weise zu ermittelnden Adreß- und Datenzuordnungen der Zeichen und Symbole sind anschließend auf geeignete Datenträger zu übertragen, die vom EPROM-Ladegerät lesbar sind.
Anlage 1 zeigt die Anordnung der EPROM-Schaltkreise auf der Steckeinheit. Außer den in Anlage 3 aufgeführten Codetabellen können, je nach Kundenwunsch, weitere programmiert werden. Es müssen nur entsprechende Eingabedaten erstellt werden.

**Anschluß des Monitors**

Der Monitor ist an die Steckeinheit Typ 062-8640 über einen 5-poligen Steckverbinder anzuschließen (10-polige Steckverbinder STL 102.10 TGL 29331/04 - ältere Ausführung).
Die Zuführung erfolgt über ein koaxiales Kabel (max. 5 m). Dieses Kabel in verschiedenen Längen gehört mit zum Lieferumfang des Gerätes.

**Steckerbelegung X5:**

|     | A       | B   |
| :-- | :------ | :-- |
| 5   | /VIDEO  | -   |
| 4   | -       | 00  |
| 3   | /BSYN   | -   |
| 2   | -       | 00  |
| 1   | /INTENS | -   |

**Anlage 1:**
**Lage der Zeichengeneratoren, Steckverbinder und Wickelstifte**

![[Pasted image 20260515153556.png]]

**Anlage 2:**

![[Pasted image 20260515153631.png]]
**Tabelle 2: Die schraffierten Felder dürfen nicht mit Zeichen/Symbolen belegt werden!**

![[Pasted image 20260515153738.png]]
**Tabelle 3: Formular zur Programmierung des Zeichengenerators**

**Anlage 3: Zeichenvorrat lateinisch, Standard-lateinisch, kyrillisch**

![[Pasted image 20260515153906.png]]
![[Pasted image 20260515153944.png]]

**robotron**

**VEB Robotron**
**Buchungsmaschinenwerk**
**Karl-Marx-Stadt**

DDR-9010 Karl-Marx-Stadt
Annaberger Straße 93
PSF 129
Exporteur:
Robotron - Export/Import
Volkseigener Außenhandelsbetrieb der Deutschen Demokratischen Republik
DDR-1140 Berlin
Allee der Kosmonauten 24
PSF 11

scan by sylvi @ 2011
Kv 2369 85 III 8-9 7426
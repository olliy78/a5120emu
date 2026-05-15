# robotron

**Anschlußsteuerung DFÜ / Drucker**
**ASS K 8025.50 ... K 8025.80**

**Betriebsdokumentation**


Überarbeitete Auflage
4. Auflage
Karl-Marx-Stadt, 1987

**Inhaltsverzeichnis**

I. Verwendung und Einordnung
II. Technische Daten
III. Bauelementebasis
IV. Konstruktiver Aufbau
V. Funktionsbeschreibung
VI. Kurzzeichenübersicht
VII. Serviceschaltpläne

© VEB Kombinat Robotron, 1987

## I. Verwendung und Einordnung

Die Steckeinheit ASS K 8025 dient in der Maximalausstattung dem Anschluß für DFÜ über die Schnittstellen V.24 und IFSS, dem log. Datenaustausch zur seriellen Tastatur K 7637 sowie dem Anschluß von zwei Seriendruckern mit serieller Schnittstelle (IFSS). Sie arbeitet am Bus des Mikrorechners K 1520 und steuert den Informationsaustausch zwischen DFÜ-Einrichtung, Tastatur K 7637 bzw. Drucker und der ZRE K 2526. Dazu wurden als Interfacebausteine zwei SIO-Schaltkreise eingesetzt. In Abhängigkeit vom Einsatz des Druckers im zeitkritischen bzw. zeitunkritischen Betrieb werden die SIO in die Interruptkette des Gesamtsystems eingeordnet, wobei die DFÜ in jedem Fall die höhere Priorität gegenüber dem Drucker besitzt.

Mit Ausnahme der Druckerschnittstellen ist die STE K 8025 voll kompatibel zur STE K 6028.
Die STE K 8025.XX mit Index 1 wurde mit Einführung der seriellen Tastatur K 7637 wirksam. Sie ist dadurch gekennzeichnet, daß der PIO-Schaltkreis durch ein Register U 212 ersetzt wurde. Die Bezeichnung der Ausführung ändert sich von

alt: B 062-8440 |0| in neu: B 062-8440 |1|

Die neu eingeführte STE ist voll kompatibel, während die alte Ausführung nicht für Anlagen mit serieller Tastatur K 7637 eingesetzt werden kann. Die Bestell-Nr. für beide Ausführungen ist gleich.
Die Besonderheiten der Tastaturadaptierung einschließlich der zu beachtenden Einstellungen sind in der Betriebsdokumentation K 7637, Pkt. 5, beschrieben.

Anmerkung: In nachfolgender Beschreibung wird zur Vereinfachung nur von der STE K 8025 gesprochen. Es handelt sich dabei um die Steckeinheit ASS K 8025 in der Ausstattungsvariante K 8025.50 ... K 8025.80.

## II. Technische Daten

**Steckeinheitenabmessung:** 215 mm x 170 mm
**Steckverbinder:** 2 x 58-polig, indirekt, zum Rechnerbus
**(Maximalausstattung)** 3 x 5-polig, indirekt, zur Peripherie
1 x 13-polig, indirekt, zur Peripherie
**Einsatzklasse:** EKL 3
**Stromversorgung:**
5 P = + 5 V ± 5 % mit I_5P ≤ 1 A *
12 P = + 12 V ± 5 % mit I_12P ≤ 0,5 A
12 N = - 12 V ± 5 % mit I_12N ≤ 0,4 A
5 PH = + 5 V ± 5 % mit I_5PH ≤ 20 mA +

* je nach Ausstattung
+ 5 PH ist eine Hilfsspannung zur Netzferneinschaltung

**Signalpegel:**
- Ein- und Ausgangsleitungen zum Systembus K 1520
TTL-kompatibel
U_IL ≤ 0,8 V / U_OL ≤ 0,4 V
U_IH ≥ 2 V / U_OH ≥ 2,4 V

- Ein- und Ausgangsleitungen des Interfaces IFSS
entsprechend KROS-R-5006/01 ... /04
≤ 25 V bei einem Stromfluß von 15 mA ... 25 mA (≙ high), bei einem Stromfluß von 0 mA ... 3 mA (≙ low)

- Ein- und Ausgangsleitungen des Interfaces V.24 entsprechend CCITT-Empfehlung V.28
- 3 V ... - 25 V ≙ Binärziffer 1 (high) auf Datenstromweg (Zustand "Z") ≙ Zustand "offen" auf Signal- und Kontrollweg
+ 3 V ... + 25 V ≙ Binärziffer 0 (low) auf Datenstromweg (Zustand "A") ≙ Zustand "geschlossen" auf Signal- und Kontrollweg

**Betriebsart:** Dauerbetrieb, durch Ferneinschaltung Arbeit im unbesetzten Zustand möglich (siehe Punkt 2.3.5.)

## III. Bauelementebasis

Auf der Steckeinheit K 8025 sind im wesentlichen folgende Bauelemente eingesetzt:

- **für die Busschnittstelle**
U 205: Schneller 1 aus 8-Decoder
U 212: Paralleles 8-Bit-Ein- und Ausgaberegister
U 216: 4-Bit-bidirektionaler Bustreiber

- **zur DFÜ/Drucker-Steuerung**
U 856 (Q 304): Serieller E/A-Baustein SIO
U 212: Paralleles 8-Bit-Ein-/Ausgaberegister

- **zur Taktversorgung**
U 857 (Q 302): Zähler-Zeitgeber-Baustein CTC

- **für das V.24-Interface**
P 150: Leitungstreiber für die Ausgabedaten, zweifach
P 154: Leitungsempfänger für die Eingabedaten, vierfach

- **für die IFSS-Interface**
MB 104: Optokoppler mit Basisanschluß

Eine Beschreibung aller genannten Bauelemente ist im Heft "Bausteinübersicht" zu finden.

## IV. Konstruktiver Aufbau

Die Steckeinheit ASS K 8025 ist ein programmierbarer Datenübertragungsadapter zur seriellen Datenübertragung. Sie wird unter Beachtung der Prioritäten in das Paneel des Rechnerbusses eingesteckt.
Neben den beiden Kanälen für den Anschluß von zwei Seriendruckern kann sich auf der Steckeinheit die erforderliche Elektronik für die Datenfernübertragung befinden. Deshalb ergeben sich, entsprechend den Erfordernissen, verschiedene Ausstattungsvarianten:

- **K 8025.50:** Anschluß für 2 Drucker, DFÜ/V.24, DFÜ/IFSS
- **K 8025.60:** Anschluß für 2 Drucker, DFÜ/V.24
- **K 8025.70:** Anschluß für 2 Drucker, DFÜ/IFSS
- **K 8025.80:** Anschluß für 2 Drucker
(Varianten .60 bis .80 als abgerüstete Varianten der K 8025.50)

Die Verbindung zum Rechnerbus bzw. zur Peripherie wird über folgende Steckverbindungen hergestellt:

**2 x Steckerleiste 58-polig (Bauform 304-58)**
Steckverbinder X1 und X2 - Rechnerbus

**2 x Steckerleiste 5-polig (Bauform 103-5)**
Steckverbinder X3 - Anschluß für Drucker
Steckverbinder X4 - Anschluß für Zusatzdrucker

**1 x Steckerleiste 5-polig (Bauform 103-5)**
Steckverbinder X5 - Anschluß für DFÜ/IFSS

**1 x Steckerleiste 13-polig (Bauform 103-13)**
Steckverbinder X6 - Anschluß für DFÜ/V.24

## V. Funktionsbeschreibung

**Inhaltsverzeichnis**

1. Allgemeine Übersicht
1.1. Datenfernverarbeitung über Schnittstelle V.24
1.2. Datenübertragung über IFSS
2. Beschreibung der Funktionskomplexe
2.1. Blockschaltbild
2.2. Anschluß an den Rechnerbus
2.3. Steuerung der Datenfernübertragung
2.4. Druckersteuerung
3. Einstellungsmöglichkeiten auf der Steckeinheit K 8025
3.1. Einstellungsmöglichkeiten über Brücken
3.2. Einstellungsmöglichkeiten über DIL-Schalter
4. Besonderheiten der ASS K 8025.70
5. Kontaktbelegung der Steckverbinder

### 1. Allgemeine Übersicht

#### 1.1. Datenfernverarbeitung über Schnittstelle V.24

![[Pasted image 20260515171756.png]]**Abb. 1 Zweig eines Datenfernverarbeitungssystems**

Werden als Datenendeinrichtung (DEE) die Geräte des Erzeugnisprogrammes "Dezentrale Datentechnik" eingesetzt, so sind als Datenübertragungseinrichtung (DÜE) MODEM'S anschließbar, welche den Empfehlungen V.1, V.2, V.21 bzw. V.23, V.24 und V.28 der CCITT entsprechen.

Um bei größeren Netzen Kosten für Fernmeldewege einzusparen, ist eine Zusammenfassung vieler Datenstationen über Leitungskonzentratoren möglich:

![[Pasted image 20260515171831.png]]
**Abb. 2 Rationeller DFÜ-Netzaufbau über Leitungskonzentratoren**

Eine weitere Form der Leitungsverdichtung ist der Mehrpunktbetrieb (Multipoint-Netze) über Standleitungen:

![[Pasted image 20260515171952.png]]
**Abb. 3 Prinzipieller Aufbau eines Mehrpunkt-Netzes**

Als Gerätesteuereinheit werden sogenannte Multiplexoren eingesetzt, welche u. a. die Aufgabe haben, bestimmte Codes zu wandeln, Fehler zu behandeln bzw. Geschwindigkeitsunterschiede anzupassen. Ihr Einsatz ist nur dann erforderlich, wenn die EDVA selbst nicht über eine eigene DFÜ-Steuereinheit verfügt.

#### 1.2. Datenübertragung über IFSS

Die Datenübertragung erfolgt mittels Stromschleifen getrennt für Sender und Empfänger. Die Stromeinspeisung wird über Festwiderstände oder über eine Konstantstromquelle gewährleistet. Auf der Steckeinheit K 8025 erfolgt die Einspeisung über Festwiderstände. Während bei DFÜ über IFSS bei Senden und Empfangen im Aktiv- oder Passivmodus gearbeitet werden kann, wird der Datenaustausch mit den Seriendruckern ausschließlich im Aktivmodus durchgeführt.

Folgende Verschaltungsmöglichkeiten sind realisierbar:

![[Pasted image 20260515172025.png]]
**Abb. 4 Betriebsweise IFSS**


### 2. Beschreibung der Funktionskomplexe

#### 2.1. Blockschaltbild

![[Pasted image 20260515172057.png]]
**Abb. 5 Blockschaltbild**

Wie aus dem Blockschaltbild zu ersehen ist, kann die Anschlußsteuerung K 8025 in drei Funktionskomplexe unterteilt werden:

- Anschluß an den Rechnerbus
- Steuerung der Datenfernübertragung
- Druckersteuerung

#### 2.2. Anschluß an den Rechnerbus

Der Anschluß an den Rechnerbus wird sowohl für die DFÜ- als auch für die Druckersteuerung benötigt. Er umfaßt folgende Funktionsgruppen:

- Adreßdecodierung
- Bereitschaftsmeldung/Busumschaltung

#### 2.2.1. Adreßdecodierung

Der Adreßdecoder ist über Brücken frei programmierbar und kann somit den Erfordernissen des jeweiligen Betriebssystems angepaßt werden.
Im Betriebssystem SIOS 1526 werden vom zentralen Adreßbus folgende Ein- und Ausgabekanäle der Steckeinheit K 8025 zur Verfügung gestellt:

- **IN/OUT 50 ... 53** zur DFÜ-Steuerung mittels SIO (A33)
- **IN/OUT 54 ... 57** zur Abfrage bestimmter Übertragungsparameter mittels Register U 212
- **IN/OUT 58 ... 5B** zur Taktversorgung mittels CTC (A34)
- **IN/OUT 5C ... 5F** zur Druckersteuerung mittels SIO (A32)

Die Zuordnung dieser Kanäle erfolgt über die 1 aus 8-Decoder A13 und A21. Sind die Leitungen `/RESET` und `/M1` inaktiv, wird der Adreßdecoder A13 durch low-Potential am Eingang E2 arbeitsfähig. Mit dem Aufsteuern der Adreßleitung AB6 gelangt über die Brücke W1:1 der 1 aus 8-Decoder A21 in den aktiven Bereich, wodurch abhängig vom Potential der Adreßleitungen AB2 ... AB4 über Brücke W1:4 die DFÜ-SIO-, über Brücke W1:2 die Register-, über Brücke W1:5 die CTC- bzw. über Brücke W1:3 die Drucker-SIO-Ansteuerung erfolgt. Die Adreßleitungen AB0 und AB1 dienen der Kanalauswahl bzw. der Steuerungs- oder Datenauswahl.

#### 2.2.2. Bereitschaftsmeldung

Mit `/RDY` = low wird die Bereitschaftsmeldung zur ZRE signalisiert. Es gibt zwei Möglichkeiten der `/RDY`-Bildung:

- **Erkennung einer gültigen Adresse**
Wird von der ZRE ein Ein-/Ausgabekanal für die DFÜ- bzw. Druckerarbeit belegt, schaltet A22-08 auf high. Dadurch wird gemeinsam mit der Bedingung `/IORQ` = high das Gatter A24-03 und damit `/RDY` auf low gelegt.

- **Interrupt-Anmeldung**
In jedem Fall besitzt die DFÜ die höhere Priorität gegenüber der Druckersteuerung. Über die Brücken W1:8 bis W1:11 kann jedoch bestimmt werden, ob die Drucker in der Interruptkette unmittelbar nach der DFÜ liegen sollen (= zeitkritischer Betrieb) oder ob die Drucker erst in der niederen Interruptkette eingebunden werden sollen (= zeitunkritisch scher Betrieb).
Standardmäßig sind o. g. Brücken wie im Stromlaufplan gezeichnet eingelötet; die Drucker arbeiten dabei zeitkritisch.

Wird ein Interrupt durch die DFÜ-Steuerung angemeldet (IEO des DFÜ-SIO = low) und keine prioritätenmäßig vor der DFÜ liegende Baugruppe hat die Logikkette bereits belegt, schaltet A26-03 nach low. Das ist die Voraussetzung, um während des Interrupt-Quittungszyklus der CPU unter der Bedingung `/M1` und `/IORQ` = high über A24-03 `/RDY` auf low zu legen.

Wird der angemeldete Interrupt durch die CPU bestätigt, muß von der DFÜ-Steuerung der Interruptvektor über den Datenbus ausgesendet werden. Demzufolge ist eine Busumschaltung in Richtung zur ZRE erforderlich. Mit dem low-Potential von A26-08 und `/IORQ` = high schaltet A16-03 nach low. Damit wird an den bidirektionalen Bustreibern A11 und A12 die erforderliche Datenflußrichtung DI nach DB eingestellt.

Die gleiche Datenflußrichtung muß bei einem Input mit gültiger Adresse gewährleistet sein. Wird unter dieser Bedingung `/RD` und `/IORQ` aktiv, schaltet über A22-06 Gatter A16-03 nach low; ein Datenfluß zur ZRE ist möglich.

### 2.3. Steuerung der Datenfernübertragung

Kernstück der Steuerung der Datenfernübertragung ist der serielle Ein-/Ausgabebaustein A33 (SIO). Dieser hochintegrierte Schaltkreis sichert die Umsetzung eines 8-Bit-breiten Datenformates in ein serielles Datenformat für "Senden" und umgekehrt für "Empfangen". Durch seine Programmierbarkeit ist er u. a. in der Lage, asynchron, synchron bzw. bitorientiert synchron zu arbeiten, verschiedenartige Fehler zu erkennen, CRC-Codes zu erzeugen oder auch in der asynchronen Arbeitsweise Stopbits verschiedener Länge bzw. Paritätsbits zu erzeugen.
Die Steckeinheit ist so aufgebaut, daß dem V.24-Anschluß der Kanal A und dem IFSS-Anschluß der Kanal B des SIO zugeordnet ist.

#### 2.3.1. Taktauswahl

In Abhängigkeit vom Übertragungsverfahren (synchron, asynchron) ist es erforderlich, die Taktierung von Sender und Empfänger des SIO von verschiedenen Taktgebern zu realisieren. Da im Synchronbetrieb die Bitfolgen in einem festen Zeitraster liegen, sind Taktgeber hoher Genauigkeit notwendig. Deshalb erfolgt hier in der Regel die Taktierung von außen, d.h. von der Datenübertragungseinrichtung (MODEM) über die V.24-Schnittstelle. Im Asynchronbetrieb wird der Takt durch das Datenendgerät selbst bereitgestellt. Im Normalfall geschieht dies sowohl für DFÜ über V.24 als auch über IFSS vom CTC-Baustein der ZRE. Durch Veränderung bestimmter Brücken ist es problemlos möglich, den Takt vom eigenen CTC A34 zuzuführen.

**Taktauswahlmöglichkeiten auf der Steckeinheit K 8025**

- **Asynchronbetrieb über das V.24-Interface**
Über die Leitung ZC/TO vom Kanal 0 des CTC der ZRE wird der von der gewählten Übertragungsgeschwindigkeit abhängige Empfangsschrittakt (RxCA) bzw. Sendeschrittakt (TxCA) bereitgestellt. Dazu müssen die Brücke W1:7 vorhanden sein und die DIL-Schalter A46/PIN2-7 sowie A46/PIN3-6 geschlossen werden. Die Möglichkeit der eigenen Taktversorgung ist über Kanal 2 des CTC A34 durch Veränderung der Brücke W1:7 gegeben.

- **Asynchron über das IFSS-Interface**
Durch ausschließliche Verwendung von SIO's der Bond-Variante 0 wird die Taktierung von Sender und Empfänger des Kanals B gemeinsam über PIN 27 (RxTxCB) durchgeführt. Über Wickelverbindung X7:X8 erfolgt die Taktzuführung von ZC/TO des CTC der ZRE. Bei eigener Taktversorgung über Kanal 1 des CTC A34 muß die Wickelverbindung X8:X9 hergestellt werden.

- **Synchronbetrieb über das V.24-Interface**
Der Empfangsschrittakt vom MODEM gelangt über Leitung V115 pegelgewandelt durch Leitungsempfänger A48 über die DIL-Schalter-Verbindung A46/PIN1-8 zu RxCA des SIO.
Der Sendeschrittakt des MODEM wird von Leitung V114 über Leitungsempfänger A48, Brücke W1:6 und DIL-Schalter-Verbindung A46/PIN5-4 zu TxCA des SIO geführt.
Für Test- und Inbetriebnahmezwecke wurde eine Sendeschrittaktleitung in der DEE realisiert, deren Taktgeber auf der Basis des CTC-Taktes arbeitet. Damit wird eine Direktkopplung zweier DEE im Synchronbetrieb ermöglicht. Diese Taktleitung erfüllt weitgehend die Bedingungen der Leitung V113 entsprechend CCITT.

#### 2.3.2. Realisierung des V.24-Anschlusses

Die CCITT-Empfehlung V.24 definiert die Funktionen der Schnittstellenleitungen, die zwischen DEE und DÜE möglich sind. Die elektrischen Eigenschaften dieser Schnittstellenleitungen legt die Empfehlung V.28 fest.
Folgende Verbindungswege sind an der 13-poligen Steckerleiste X6 der Steckeinheit K 8025 realisiert:

**X6 A1 - V102 (Betriebserde)**
Dieser Leiter bestimmt das allgemeine Bezugspotential für alle Verbindungswege.

**X6 A3 - V103 (Sendedaten)**
Die über diesen Stromweg zu übertragenden Signale werden von der Dateneindeinrichtung erzeugt. Die DEE steuert die Leitung in den Kennzustand "Z" während der Zeit zwischen den Datenzeichen bzw. wenn keine Signale übertragen werden. Die Sendedatenleitung ist über Pegelwandler A52 mit dem Anschluß TxDA des SIO verbunden.

**X6 B4 - V104 (Empfangsdaten)**
Die auf diesem Stromweg ankommenden Signale werden von der DÜE der Empfangsseite als Bestätigung für den Datenempfang von der entfernt liegenden Datenstation erzeugt. Diese Leitung hat im Halbduplexbetrieb den Kennzustand "Z", so lange der Stromweg V105 (Aufforderung zum Senden) "geschlossen" ist. Die Empfangsdatenleitung ist über Pegelwandler A47 mit dem Anschluß RxDA des SIO verbunden.

**X6 A5 - V105 (Sendeaufforderung)**
Die Signale dieses Stromweges, erzeugt von der DEE, dienen dazu, das am gleichen Ort befindliche Datenübertragungsgerät in den Zustand zum Senden von Daten zu schalten. Im Halbduplexbetrieb hält der Zustand "offen" die Dateneinrichtung empfangsbereit; der Zustand "geschlossen" liegt während der Datensendung vor. Die Signale für diese Steuerleitung werden über den RTSA-Anschluß des SIO-Schaltkreises ausgegeben und vom Pegelwandler A52 gewandelt.

**X6 B6 - V106 (Sendebereitschaft)**
Die Datenübertragungseinrichtung erzeugt die Signale dieses Stromweges und zeigt mit ihrer Bereitschaft an, Daten zu senden. Der Übergang vom Zustand "offen" in den Zustand "geschlossen" ist die Antwort auf die gleiche Zustandsänderung des Stromweges V105. Da für die Sendebereitschaft der DÜE ihre Betriebsbereitschaft Voraussetzung ist, sind die Leitungen V106 (pegelgewandelt über A47) und V107 konjunktiv verknüpft. Sie bilden zusammen über A23-08 das Signal `/CTSA` des SIO.

**X6 A7 - V107 (Betriebsbereitschaft)**
Die über diesen Stromweg ankommenden Signale werden von der Datenübertragungseinrichtung erzeugt und zeigen die Betriebsbereitschaft derselben sowohl für das Senden als auch für den Empfang von Daten an. Ist diese Leitung "offen", werden anormale Zustände oder das Nichtvorhandensein einer Verbindung zwischen DÜE und Übertragungskanal signalisiert. Nur wenn die Dateneinrichtung betriebsbereit ist, d. h. Zustand "geschlossen", kann die DEE Sende- und Empfangsroutinen starten (siehe Beschreibung der Stromwege V106 und V109). Eine Forderung verschiedener Postverwaltungen bzw. Fernmelde-Betriebsgesellschaften besteht darin, daß die Betriebsbereitschaft allein abfragbar ist. Da dies über den SIO-Kanal A wegen der Verknüpfungen mit anderen Stromwegen nicht realisierbar ist, wurde die Leitung V107 zusätzlich mit dem Eingang DCDB des SIO zusammengeschaltet. Hier ist eine Abfrage jederzeit möglich, weil zur Steuerung des IFSS-Anschlusses keine Steuerleitungen benötigt werden.

Die Signale des Stromweges V107 werden über Pegelwandler A48 TTL-gerecht der Steuerelektronik des Adapters zur Verfügung gestellt.

**X6 B8 - V108 (Anschalten der Datenendstelle an den Übertragungsweg)**
Die von der DEE für diesen Stromweg erzeugten Signale gelangen vom Anschluß DTRA des SIO über Pegelwandler A51 zum MODEM. Der Zustand "geschlossen" bewirkt die Verbindung des Signalumwandlers mit dem Übertragungskanal.

**X6 A9 - V109 (Empfangssignalpegel)**
Mit den Signalen auf dieser Leitung wird der DEE angezeigt, ob auf dem Übertragungskanal ein Datenträger empfangen wird und dessen Pegel innerhalb der Grenzen liegt, die für die DÜE angegeben sind. Der Zustand "geschlossen" zeigt an, daß das Empfangssignal mit dem geforderten Pegel anliegt. Der Zustand "offen" signalisiert, daß das empfangene Signal nicht innerhalb der entsprechenden Grenzen liegt bzw. daß eine Störung oder das Ende der Übertragung vorliegt.
Neben der Möglichkeit, über diese Leitung die Ferneinschaltung des Datenendgerätes zu realisieren, wird sie über den Leitungsempfänger A48 dem Gatter A23 zugeführt und mit dem Stromweg V107 konjunktiv verknüpft. Ausgang A23-11 steuert den Anschluß DCDA des SIO.

**X6 B10 - V111 (Auswahl der Übertragungsgeschwindigkeit)**
Durch die DEE wird diese Leitung verwendet, um eine von zwei Übertragungsgeschwindigkeiten der DÜE auszuwählen. Über DIL-Schalter A61/PIN7-10 wird der Zustand "geschlossen" realisiert und damit die höhere Geschwindigkeit bzw. der höhere Geschwindigkeitsbereich ausgewählt. Ein Schließen des DIL-Schalters A61/PIN8-9 bewirkt die Auswahl der niederen Geschwindigkeit bzw. des niederen Geschwindigkeitsbereiches.

**X6 B12 - V114 (Sendeschrittakt von der DÜE)**
Die Signale auf dieser Leitung beliefern die Dateneindeinrichtung mit der Schrittaktinformation für die Sendedaten. Dies ist in der Regel erforderlich bei synchroner Datenübertragung. Über Leitungsempfänger A48, Brücke W1:6 und DIL-Schalter A46 wird der Takt dem Anschluß TxCA des SIO zugeführt.

**X6 A13 - V115 (Empfangsschrittakt von der DÜE)**
Die Signale auf dieser Leitung beliefern die Dateneindeinrichtung mit der Schrittaktinformation für die Empfangsdaten. Dies ist in der Regel bei synchroner Datenübertragung erforderlich. Über Leitungsempfänger A48 und DIL-Schalter A46 wird der Takt dem Anschluß RxCA des SIO zugeführt.

**X6 B2 - V125 (Anzeige eines kommenden Anrufes)**
Ist der Zustand dieser Leitung "geschlossen", wird angezeigt, daß durch die DÜE ein Anruf von einer entfernt liegenden Endstelle empfangen wird. Bei DFÜ über Wählleitungen wird dieser Stromweg zur Ferneinschaltung des Datenendgerätes benutzt.

#### 2.3.3. Realisierung der DFÜ über die IFSS-Schnittstelle

IFSS bedeutet:
Interface zum sternförmigen Anschluß von Ein- und Ausgabegeräten mit serieller Informationsübertragung.
Entsprechend der "Entwicklungsrichtlinie DEKK - Interface IFSS" nach KROS-R 5006 erfolgt der Informationsaustausch durch Übertragung serieller binärer Signale mittels zweier Stromschleifen in stufenförmig wählbaren Geschwindigkeiten. Bei Datenfernübertragung über IFSS ist bei Sender und Empfänger die galvanische Trennung zwischen den Leitungsschleifen und der Steuerelektronik durch Einsatz von Optokopplern von Vorteil.
Die logischen Bedingungen sind so festgelegt, daß die Stromkreise zwischen den Zeichen den Zustand "high" einnehmen. Die Zustände "high" oder "low" müssen sich für eine ganze Bitzeit halten.
IFSS erlaubt asynchrone Übertragung (Start-Stop-Verfahren). Deshalb setzt sich der Signalablauf eines Zeichens zusammen aus:
- 1 Startbit (Zustand "low")
- wahlweise 5, 7 oder 8 Datenbits
- 1 Paritätsbit (Kontrolle auf Geradzahligkeit bzw. Ungeradzahligkeit oder ohne Parität)
- 1, 1 1/2 oder 2 Stopbits (Zustand "high")

Der erforderliche Strom wird im Aktivmodus über Festwiderstände eingespeist. Dies erfolgt für die Sendeeinrichtung über die Widerstände R10:1/R10:2 und DIL-Schalter A61/PIN6-11 und PIN4-13.
Die Sendedaten gelangen vom Ausgang TxDB des SIO A33 über Nand A24-11 zum Optokoppler V5:2. In Abhängigkeit vom Potential der Sendedaten wird über den Optokoppler Transistor V1:8 gesteuert. Ist V1:8 geöffnet (Zustand "high"), kommt es zu einem Stromfluß über SD+1, der Stromschleife, dem Empfänger der Gegenstelle zurück zu SD-1. Bei Senden im Passivmodus sind die o. g. DIL-Schalter-Verbindungen A61 nicht herzustellen. Dafür muß DIL-Schalter A61/PIN5-12 geschlossen sein.
Der Kondensator C1 verhindert ein Übersprechen in die Nachbarleitungen des Kabels.
Für den Datenempfang dient die Empfangsstromschleife ED+1, DIL-Schalter A61, Optokoppler V5:1, ED-1. Wird im Aktivmodus gearbeitet, erfolgt die Stromeinspeisung analog zum Senden über R10:3/R10:4 und DIL-Schalter A61/PIN3-14 und PIN1-16. Datenempfang im Passivmodus erfordert das Einschalten des DIL-Schalters A61/PIN2-15.

Fließt ein Strom von ca. 20 mA (≙ Zustand "high"), gelangt über Optokoppler V5:5 high-Potential an die Eingänge des Schmitt-Triggers A45. Am Ausgang A45-06 steht ein exakter high-Impuls zur Verfügung, welcher zu RxDB des SIO A33 gelangt.

#### 2.3.4. Festlegung bestimmter Übertragungsparameter

Mit Hilfe des auf der Steckeinheit ASS K 8025 befindlichen Registers A31 können bestimmte Übertragungsparameter wie beispielsweise Auswahl der physischen Blocklänge und der Übertragungsgeschwindigkeit über die DIL-Schalter A41 frei programmiert werden. Die konkreten Festlegungen betr. der Einstellmöglichkeiten sind im Punkt 3.2. ausführlicher beschrieben.

#### 2.3.5. Netzteil - Ferneinschaltung

Eine Datenübertragung im unbesetzten Betrieb wird durch Ferneinschaltung der Anlage über die V.24-Schnittstelle ermöglicht. Bedingung dafür ist eine ständige Verbindung der Dateneindeinrichtung zum Netz, damit die Hilfsspannung 5 PH immer zur Verfügung steht, ein betriebsbereites MODEM sowie ein verriegelter Datenträger.
Die Ferneinschaltung wird über die Verbindungswege V109 oder V125 der V.24-Schnittstelle realisiert. Werden der Datenträger mit dem geforderten Pegel bzw. ein Ruf empfangen, ist der Zustand der entsprechenden Leitung "geschlossen". Über die jetzt leitende Diode V2 steuert Transistor V1:1 auf, wodurch über A24-06 das Signal `/SA` nach low schaltet und das Netzteil aktiviert. Ist die Betriebsspannung 5 P vom Netzteil aufgebaut, wird das FF A35-05 in Grundstellung geschaltet. Damit liegt die Leitung `/SA` wieder auf high.
Die Abschaltung des Gerätes erfolgt mit der Beendigung der Datenübertragung programmmäßig durch Befehl "OFF".

### 2.4. Druckersteuerung

Die Steckeinheit ASS K8025 besitzt zwei Kanäle mit IFSS-Schnittstelle, über die der Anschluß von je einem Seriendrucker möglich ist. Die dafür erforderliche Steuerung wird zentral durch den SIO A32 übernommen.
Kanal A des SIO A32 dient der Steuerung des Zusatzdruckers über Steckverbinder X4; Kanal B steuert den Hauptdrucker. Die Taktierung für Sender und Empfänger in beiden Kanälen wird grundsätzlich vom eigenen CTC-Baustein A34, Kanal 0 übernommen. Die Übertragungsgeschwindigkeit liegt in der Regel, entsprechend der standardmäßig bei Druckerhersteller vorgenommenen Einstellung, bei 9600 Bit je Sekunde.

Da ständig im Aktivmodus gearbeitet wird, erfolgt die Stromeinspeisung über die Festwiderstände R8:1/R2:1 (Empfangsstromschleife Hauptdrucker), R8:2 (Sendestromschleife Hauptdrucker), R8:3/R2:2 (Empfangsstromschleife Zusatzdrucker) und R8:4 (Sendestromschleife Zusatzdrucker).
Der Informationsaustausch mit den Druckern geschieht analog zum DFÜ-IFSS und ist unter Punkt 2.3.3. beschrieben.

## 3. Einstellungsmöglichkeiten auf der Steckeinheit ASS K 8025

Die Anordnung der Brücken und DIL-Schalter auf der Steckeinheit ist dem jeweiligen Belegungsplan zu entnehmen.

### 3.1. Einstellungsmöglichkeiten über Brücken und Wickelverbindungen

Die im Belegungsplan eingezeichneten Brücken entsprechen der im Stromlaufplan gezeichneten Verbindung. Während die Brücken der Adreßdecodierung in Abhängigkeit von der PIN-Belegung der 1 aus 8-Decoder A13 und A21 eingelötet werden, unterscheidet man bei den übrigen Brücken eine "gezeichnete" und eine "nichtgezeichnete" Stellung. Das Einlöten einer Brücke in die "nichtgezeichnete" Stellung bedeutet das Versetzen derselben um ein Raster.

**Adreßdecodierung**
- **W1:1** - Festlegung des H-Teiles der Ein- und Ausgabekanäle
- **W1:2** - Auswahl Register A31
- **W1:3** - Auswahl SIO (Drucker)
- **W1:4** - Auswahl SIO (DFÜ)
- **W1:5** - Auswahl CTC

**Taktauswahl**
- **W1:6** - Auswahl des Sendeschrittaktes im Synchronbetrieb
*gezeichnete Stellung:* Zuführung des Sendeschrittaktes von der DÜE über Leitung V114
*nichtgezeichnete Stellung:* Zuführung des Sendeschrittaktes vom CTC der ZRE bzw. CTC A34 (abhängig von Brücke W1:7) über FF A35-09 zu Testzwecken

- **W1:7** - Auswahl Sende-/Empfangsschrittakt für DFÜ über das V24-Interface
*gezeichnete Stellung:* Taktzuführung vom CTC der ZRE
*nichtgezeichnete St.:* Taktzuführung von Kanal 2 des CTC A34

**Wickelverbindung**
- **X7-X8** - Auswahl Sende-/Empfangsschrittakt für DFÜ über das IFSS-Interface
*gezeichnete Stellung:* Taktzuführung vom CTC der ZRE
- **X8-X9** - *nichtgezeichnete St.:* Taktzuführung von Kanal 1 des CTC A34

**Umschaltung der Interruptprioritäten**
- **W1:8** bis **W1:11**
*gezeichnete Stellung:* Arbeitsweise der Drucker im zeitunkritischen Betrieb (eingeordnet in der 2. Interruptkette)
*nichtgezeichnete St.:* Arbeitsweise der Drucker im zeitkritischen Betrieb (eingeordnet in der 1. Prioritätenkette unmittelbar nach DFÜ)

### 3.2. Einstellungsmöglichkeiten über DIL-Schalter

Wie aus dem Stromlaufplan zu ersehen ist, sind drei DIL-Schalter-Pakete (A41, A46 und A61) auf der Steckeinheit K 8025.50 zu finden. Jedes Paket besteht aus mehreren Einzelschaltern und kann in Abhängigkeit von der Ausstattung der Steckeinheit variieren.
Die Zählweise ist analog der integrierten Bausteine, d. h. die auf dem Belegungsplan gekennzeichnete Ecke bestimmt PIN1 des Schalterpaketes.

"EIN" bedeutet: Der Einzelschalter ist zu schließen, um eine Verbindung zwischen den sich gegenüber liegenden PIN's herzustellen.
"AUS" bedeutet: Der Einzelschalter ist zu öffnen, um die Verbindung zwischen den sich gegenüberliegenden PIN's zu trennen.

Die Abfrage der DIL-Schalter-Einstellungen ist über Register A31 möglich. Demzufolge wird allein vom jeweils verwendeten Betriebssystem festgelegt, welche Bedeutung die verschiedenen Schaltereinstellungen haben. Im Betriebssystem SIOS 1526 gelten die unter Punkt 3.2.1. ... 3.2.4. beschriebenen Festlegungen.

#### 3.2.1. Einstellung der Übertragungsgeschwindigkeit für die DFÜ

| Bit pro Sekunde | A41 PIN2-15 | A41 PIN8-9 | A41 PIN6-11 |
| :--- | :--- | :--- | :--- |
| 50 | EIN | EIN | EIN |
| 200 | EIN | EIN | AUS |
| 300 | EIN | AUS | EIN |
| 600 | EIN | AUS | AUS |
| 1200 | AUS | EIN | EIN |
| 2400 | AUS | EIN | AUS |
| 4800 | AUS | AUS | EIN |
| 9600 | AUS | AUS | AUS |

#### 3.2.2. Einstellung der physischen Blocklänge für die DFÜ

| Byte | A41 PIN3-14 | A41 PIN5-12 | A41 PIN7-10 |
| :--- | :--- | :--- | :--- |
| 64 | EIN | EIN | EIN |
| 128 | EIN | EIN | AUS |
| 256 | EIN | AUS | EIN |
| 512 | EIN | AUS | AUS |
| 1024 | AUS | EIN | EIN |
| 2048 | AUS | EIN | AUS |
| 4096 | AUS | AUS | EIN |
| 8192 | AUS | AUS | AUS |

#### 3.2.3. Einstellungen zur IFSS-Schnittstelle DFÜ

| | DIL-Schalter | PIN | Schalterstellung |
| :--- | :--- | :--- | :--- |
| SchirmmErdung der Masseleitung des Übertragungskabels | A41 | 1-16 | EIN |
| Empfänger arbeitet im Aktivmodus | A61 | 1-16 | EIN |
| | | 2-15 | AUS |
| | | 3-14 | EIN |
| Empfänger arbeitet im Passivmodus | A61 | 1-16 | AUS |
| | | 2-15 | EIN |
| | | 3-14 | AUS |
| Sender arbeitet im Aktivmodus | A61 | 4-13 | EIN |
| | | 5-12 | AUS |
| | | 6-11 | EIN |
| Sender arbeitet im Passivmodus | A61 | 4-13 | AUS |
| | | 5-12 | EIN |
| | | 6-11 | AUS |

#### 3.2.4. Einstellungen zur V.24-Schnittstelle DFÜ

**Asynchronarbeit**
| | DIL-Schalter | PIN | Schalterstellung |
| :--- | :--- | :--- | :--- |
| Empfangsschrittakt vom CTC | A46 | 1-8 | AUS |
| | | 2-7 | EIN |
| Sendeschrittakt vom CTC | A46 | 3-6 | EIN |
| | | 4-5 | AUS |

**Synchronarbeit**
| | DIL-Schalter | PIN | Schalterstellung |
| :--- | :--- | :--- | :--- |
| Empfangsschrittakt von der DÜE über Leitung V115 | A46 | 1-8 | EIN |
| | | 2-7 | AUS |
| Sendeschrittakt von der DÜE über Leitung V114 bzw. vom FF A35-09 | A46 | 3-6 | AUS |
| | | 4-5 | EIN |
| Auswahl der niederen Übertragungsgeschwindigkeit der DÜE * | A61 | 7-10 (1-4) | AUS |
| | | 8-9 (2-3) | EIN |
| Auswahl der höheren Übertragungsgeschwindigkeit der DÜE * | A61 | 7-10 (1-4) | EIN |
| | | 8-9 (2-3) | AUS |

* PIN-Belegung der Ausstattungsvariante K 8025.60

---

## 4. Besonderheiten der ASS K 8025.70

Durch das Nichtbestücken der V.24-Schnittstelle auf der STE K 8025.70 (1.62.518442.4; 083-4-710-037) sind die Eingänge 22 und 23 des SIO A33 offen. Dies führt bei einem Teil der Steckeinheiten zu Störungen. Zur Beseitigung evtl. auftretender Störungen werden o. g. SIO-Eingänge über R13.1 auf 1-Potential gelegt (siehe Abb.)

![[Pasted image 20260515172425.png]]
## 5. Kontaktbelegung der Steckverbinder

**Steckverbinder X1 und X2:** siehe Betriebsdokumentation ZRE K 2526

**Steckverbinder X3/X4/X5:**
| A | | B |
| :--- | :---: | :--- |
| SD- | 1 | |
| | 2 | SD+ |
| ED+ | 3 | |
| | 4 | ED- |
| 00 | 5 | |

**Steckverbinder X6:**
| A | | B |
| :--- | :---: | :--- |
| V102 | 1 | |
| | 2 | V125 |
| V103 | 3 | |
| | 4 | V104 |
| V105 | 5 | |
| | 6 | V106 |
| V107 | 7 | |
| | 8 | V108 |
| V109 | 9 | |
| | 10 | V111 |
| V113 | 11 | |
| | 12 | V114 |
| V115 | 13 | |

**Schalter- und Steckverbinderanordnung**

![[Pasted image 20260515172455.png]]
**Varianten:**
- **K 8025.50** - Bestückung (siehe Abb.)
- **K 8025.60** - ohne Steckverbinder X5, Schalter A61 hat nur 2 Schließer
- **K 8025.70** - ohne Steckverbinder X6, ohne Schalter 46
- **K 8025.80** - ohne Steckverbinder X5, X6, ohne DIL-Schalter, keine Einstellung notwendig

---

## VI. Kurzzeichenübersicht

Die Signale an den Steckverbindern X1 und X2 sind in der Betriebsdokumentation ZRE K 2526 zu finden.

- **V102:** Betriebserde
- **V103:** Sendedaten
- **V104:** Empfangsdaten
- **V105:** Sendeaufforderung
- **V106:** Sendebereitschaft
- **V107:** Betriebsbereitschaft
- **V108:** Anschalten der Datenendstelle an den Übertragungsweg
- **V109:** Empfangssignalpegel
- **V111:** Auswahl der Übertragungsgeschwindigkeit
- **V113:** Sendeschrittakt von der DEE
- **V114:** Sendeschrittakt von der DÜE
- **V115:** Empfangsschrittakt von der DÜE
- **V125:** Anzeige eines kommenden Anrufes

- **SD:** Sendedaten
- **ED:** Empfangsdaten
- **00:** SchirmmErdung der Masseleitung

---

**robotron**

VEB Robotron
Buchungsmaschinenwerk
Karl-Marx-Stadt

Annaberger Straße 93
PSF 129
DDR-9010 Karl-Marx-Stadt

Exporteur:
Robotron - Export/Import
Volkseigener Außenhandelsbetrieb der Deutschen Demokratischen Republik
Allee der Kosmonauten 24
PSF 11
Berlin
DDR-1140

1.62.540111.2 (GER)
083-3-000
832.53.01.013
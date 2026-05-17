# Schaltkreis für parallele Ein- und Ausgabe PIO U 855 D

Mikroprozessorsystem
der II. Leistungsklasse

— Technische Beschreibung —

*KI generierte Interpretation der Abbildung: Die Abbildung zeigt ein schematisches Blockschaltbild eines Mikroprozessorsystems. In der Mitte ist die CPU dargestellt, die über Adreß-, Daten- und Steuerbusse mit dem Programmspeicher und dem Arbeitsspeicher verbunden ist. Unterhalb der Busse sind vier Peripherie-Bausteine in blauen Boxen gezeichnet: PIO, SIO, CTC und DMA. Der PIO-Baustein ist orange hervorgehoben. Eine graue horizontale Linie stellt die Interrupt-Kaskade dar, welche die Peripherie-Bausteine miteinander verbindet. Pfeile zeigen die Datenflüsse zwischen den Komponenten und zur externen Peripherie.*

Schaltkreis für parallele Ein- und Ausgabe
PIO U 855 D

Technische Beschreibung

Schaltkreis für parallele Ein- und Ausgabe
PIO U855 D

**veb mikroelektronik 'karl marx' erfurt**
**im veb kombinat mikroelektronik**

DDR - 5010 Erfurt, Rudolfstraße 47 . Telefon 5 80 . Telex 061 306

Die technischen Angaben dieses Handbuches tragen reinen Informationscharakter. Verbindliche technische Liefer- und Reklamationsgrundlage sind ausschließlich die Typstandards. Die vorliegende Dokumentation gibt keine Auskunft über Liefermöglichkeiten und beinhaltet keine Verbindlichkeiten zur Produktion.

**Inhaltsübersicht**

| Kap. | Titel | Seite |
| :--- | :--- | :--- |
| 1. | Einleitung | 5 |
| 2. | Aufbau des PIO-Schaltkreises U855 | 6 |
| 3. | Beschreibung der Anschlüsse | 8 |
| 4. | Rücksetzen | 12 |
| 5. | Programmierung des PIO-Schaltkreises U855 | 13 |
| 5.1. | Interruptvektor | 13 |
| 5.2. | Wahl der Betriebsart | 13 |
| 5.3. | Interruptsteuerwort | 15 |
| 6. | Beschreibung der Betriebsarten | 18 |
| 6.1. | Byte-Ausgabe (Mode 0) | 18 |
| 6.2. | Byte-Eingabe (Mode 1) | 21 |
| 6.3. | Bidirektionale Byte-Ein-/Ausgabe (Mode 2) | 23 |
| 6.4. | Bit-Ein-/Ausgabe (Mode 3) | 24 |
| 7. | Interruptbearbeitung | 25 |
| 7.1. | Ablauf der Bearbeitung | 25 |
| 7.2. | Zeitprobleme | 28 |
| 8. | Anwendungsbeispiele | 30 |
| 8.1. | Ein-/Ausgabe-Interface | 30 |
| 8.2. | Steuer-Interface | 31 |
| 9. | Zusammenfassung der Programmierung | 33 |
| 10. | Technische Daten | 34 |
| 10.1. | Zuverlässigkeit | 34 |
| 10.2. | Elektrische Kennwerte | 34 |
| 10.3. | Gehäuse | 39 |

**1. Einleitung**

Der integrierte Schaltkreis U855 ist ein in n-Kanal-Silicon-Gate-Technologie gefertigter Ein-/Ausgabe-Baustein (Parallel I/O Controller, PIO) mit zwei TTL-kompatiblen Kanälen (Ports).
Er stellt die Verbindung zwischen der CPU und peripheren Geräten her, ohne daß zusätzliche Logik erforderlich ist.
Die U855 ist ein Schaltkreis innerhalb des Systems der II. Leistungsklasse und wird in einem 40poligen DIL-Plastgehäuse gefertigt.
Sie ist vorwiegend für den Einsatz in Datenverarbeitungsanlagen und Anlagen der Steuerungs- und Regelungstechnik vorgesehen.

Die wichtigsten Merkmale des Schaltkreises U855 sind:
- Interruptmöglichkeit im Quittungsbetrieb für schnelle Anforderungsbearbeitung, beide 8bit-bidirektionalen Ports sind mit Einrichtungen für Quittungsbetrieb ("handshaking") versehen.
- Folgende Betriebsarten sind möglich:
    - Byte-Ausgabe (Betriebsart 0)
    - Byte-Eingabe (Betriebsart 1)
    - Byte-Ein/Ausgabe (bidirektionaler Betrieb, nur für Port A möglich) (Betriebsart 2)
    - Bit-Ein/Ausgabe (Betriebsart 3)
- Interruptbearbeitung kann den Bedingungen des peripheren Gerätes angepaßt programmiert werden.
- Automatische Interruptvektorerzeugung und Prioritätskodierung durch Kaskadierung der Bausteine (Daisy-chain priority interrupt logic)
- Ausgänge des Ports B für den Anschluß von Darlington-Transistoren geeignet
- alle Ein- und Ausgänge sind TTL-kompatibel

Die Stromversorgung erfolgt über eine 5-V-Spannungsquelle. Es ist außerdem nur ein 5-V-Einphasen-Takt erforderlich.

Eine weitere wesentliche Eigenschaft der PIO U855 ist, daß der gesamte Datentransfer zwischen der Peripherie und der CPU unter vollständiger Interruptüberwachung durchgeführt werden kann. Die Interruptlogik der PIO gestattet die volle Anwendung der Interruptmöglichkeiten der CPU U880 bei Ein- und Ausgabetransfers.

Außerdem besteht die Möglichkeit, daß die PIO dahingehend programmiert wird, auf einen bestimmten peripheren Zustand hin einen Interrupt auszulösen, so daß sie auch Überwachungsfunktionen für periphere Zustände übernehmen kann.

Die gesamte Logik zur Abarbeitung auch ineinander verschachtelter Interrupts ist in der PIO enthalten, so daß keine zusätzlichen Schaltkreise erforderlich sind, falls das System nicht eine bestimmte Größenordnung überschreitet.

**2. Aufbau des PIO-Schaltkreises U855**

In Bild 1 sind die wichtigsten Teile der internen Schaltung im Blockschaltbild dargestellt.

*KI generierte Interpretation der Abbildung: Bild 1 zeigt die innere Struktur des U855D. Ein „CPU INTERFACE“ verarbeitet den Daten-Bus und die Steuerleitungen der CPU. Über einen „Interner Bus“ ist dieses Interface mit der „Internen Steuerlogik“, der „Interrupt Steuerung“ (die über 3 Leitungen nach außen verfügt) und den beiden Kanälen „Kanal A I/O“ und „Kanal B I/O“ verbunden. Von den Kanälen führen jeweils 8 Leitungen für Daten oder Steuerung sowie zwei Leitungen für Quittierungssignale zum „Peripheren Interface“. Die Spannungsversorgung erfolgt über Uss und Ucc, der Takt über CP.*

**Bild 1: Blockschaltbild der inneren Struktur der U855D**

Es sind enthalten:
- Interface zur CPU
- Logik für I/O-Port A und B
- interne Steuerlogik
- Interrupt-Steuerlogik

Die CPU-Bus-Interfacelogik gestattet es, die CPU ohne zusätzliche externe Logik mit der PIO zu koppeln. Für große Systeme machen sich jedoch Treiberstufen mit Logik zur Richtungsumschaltung, Adreßdekoder und Logik zum schnellen Durchschalten von Signalveränderungen durch die Interrupt-Prioritäten-Kette erforderlich.
Die interne Steuerlogik synchronisiert den Transfer zwischen der CPU und den beiden PIO-Ports. Mit Ausnahme in Bezug auf Betriebsart 2, die Interrupt-Priorität sowie die Art der Ausgangsstufen sind Port A und B im wesentlichen identisch.

In Bild 2 ist die Kanal-Ein-/Ausgabe-Logik im Blockschaltbild dargestellt.

Das 2bit-Betriebsarten-Register wird von der CPU zur Festlegung auf eine der 4 Betriebsarten geladen. Der gesamte Datentransfer von einem peripheren Gerät zur CPU erfolgt über das 8bit-Eingabe-Register eines PIO-Kanals für die umgekehrte Richtung über das 8bit-Ausgabe-Register. Die Quittierungsleitungen jedes Ports dienen der Steuerung des Datentransfers zwischen PIO und dem peripheren Gerät.

*KI generierte Interpretation der Abbildung: Bild 2 zeigt das Blockschaltbild eines einzelnen Kanals. Der „Interne Bus“ versorgt das „Betriebsarten-register (2 bit)“, das „Ein-/Ausgabe-Selekt-Register (8 bit)“ und das „Datenausgabe-register (8 bit)“. Ein „Dateneingabe-register (8 bit)“ nimmt Daten von der Peripherie entgegen. Die „Maskierungs-Steuer-Register“ und das „Maskierungs-Register (8 bit)“ sind mit der „Interrupt Anforderung“ verbunden. Ein Block „Quittierungs-betriebslogik“ steuert die „Quittungs-signale“ zur Peripherie und empfängt Signale von ihr.*

**Bild 2: Blockschaltbild eines Kanals**

Das 2bit-Maskierungssteuerregister, das 8bit-Ein-/Ausgabe-Auswahlregister und das 8bit-Maskierungsregister werden in der Betriebsart 3 benötigt. Das Maskierungssteuerregister wird von der CPU geladen, um für eine Interruptanmeldung festzulegen, welcher Zustand der peripheren Schaltung als aktiv erkannt werden soll (Low oder High) und welche Verknüpfungsbedingung die einflußnehmenden Anschlüsse erfüllen sollen (UND-Bedingung, wenn alle einflußnehmenden Anschlüsse aktiv werden müssen bzw. ODER-Bedingung, wenn mindestens einer der einflußnehmenden Anschlüsse aktiv werden muß).
Das Ein-/Ausgabe-Auswahlregister wird von der CPU geladen, um zu definieren, welche Anschlüsse des PIO-Ports Ausgänge und welche Eingänge sein sollen.
Das Maskierungsregister wird von der CPU geladen, um festzulegen, welche Port-Anschlüsse (entsprechend den durch die Belegung des Maskierungssteuerregisters definierten Bedingungen) auf die Erzeugung einer Interrupt-Anforderung Einfluß nehmen.
Das bietet dem Anwender die Möglichkeit, die PIO so zu programmieren, daß sie bei einem bestimmten peripheren Zustand einen Interrupt anfordert, so daß z. B. auf zyklische Statuskontrollen des peripheren Zustands durch die CPU verzichtet werden kann.

Die Interruptsteuerlogik behandelt alle CPU-Interruptroutinen entsprechend der gegebenen Interruptprioritätenstruktur. Die Priorität jedes Schaltkreises ist abhängig von seiner Stellung in der Prioritätskette.

**3. Beschreibung der Anschlüsse**

Im Bild 3 sind die Anschlußbelegung und das Schaltungskurzzeichen der U855 dargestellt.

*KI generierte Interpretation der Abbildung: Bild 3 zeigt links das physische 40-polige DIL-Gehäuse mit der Nummerierung der Pins. Rechts ist das logische Schaltsymbol des U855-Bausteins zu sehen. In der Mitte des Logiksymbols steht „U855“. Die Anschlüsse sind funktional gruppiert: Datenbus D0-D7, Steuereingänge (B/A SEL, C/D SEL, CS, M1, IORQ, RD, CP), Interrupt-Daisy-Chain (IEI, IEO, INT) und die Peripherie-Anschlüsse für Kanal A (A0-A7, ASTB, ARDY) sowie Kanal B (B0-B7, BSTB, BRDY, wobei BRDY am Logiksymbol als Ausgang gekennzeichnet ist).*

| Pin | Signal | Pin | Signal |
| :--- | :--- | :--- | :--- |
| 1 | D2 | 40 | D3 |
| 2 | D7 | 39 | D4 |
| 3 | D6 | 38 | D5 |
| 4 | $\overline{\text{CS}}$ | 37 | $\overline{\text{M1}}$ |
| 5 | C/$\overline{\text{D}}$ SEL | 36 | $\overline{\text{IORQ}}$ |
| 6 | B/$\overline{\text{A}}$ SEL | 35 | $\overline{\text{RD}}$ |
| 7 | A7 | 34 | B7 |
| 8 | A6 | 33 | B6 |
| 9 | A5 | 32 | B5 |
| 10 | A4 | 31 | B4 |
| 11 | Uss | 30 | B3 |
| 12 | A3 | 29 | B2 |
| 13 | A2 | 28 | B1 |
| 14 | A1 | 27 | B0 |
| 15 | A0 | 26 | Ucc |
| 16 | $\overline{\text{ASTB}}$ | 25 | CP |
| 17 | $\overline{\text{BSTB}}$ | 24 | IEI |
| 18 | ARDY | 23 | $\overline{\text{INT}}$ |
| 19 | D0 | 22 | IEO |
| 20 | D1 | 21 | BRDY |

**Bild 3: Anschlußbelegung und logisches Schaltbild**

Die Anschlüsse haben folgende Funktion:

**D0...D7** (Data Bus)
Datenbus (bidirektional, tri-state)
Dieser Bus wird für den Transfer aller Daten und Steuerworte zwischen der CPU und der PIO verwendet. D0 ist das niederwertigste Bit des Busses.

**B/$\overline{\text{A}}$ SEL** (Port B or A Select)
Auswahl Kanal B oder A (Eingang)
Dieser Anschluß definiert, welcher der beiden PIO-Kanäle angesprochen ist.
Low $\hat{=}$ Kanal A
High $\hat{=}$ Kanal B
Im praktischen Einsatz wird häufig die Adresse A0 der CPU für diese Auswahlfunktion verwendet.

**C/$\overline{\text{D}}$ SEL** (Control or Data Select)
Auswahl Steuerwort/Datenwort (Eingang)
Dieser Anschluß definiert die Art der Information bei einem Transfer zwischen CPU und PIO.
High-Pegel an C/$\overline{\text{D}}$ SEL während einer CPU-Schreiboperation an die PIO hat zur Folge, daß die PIO den Inhalt des Datenbusses während dieser Zeit als Steuerwort interpretiert.
Low-Pegel an diesem Anschluß bedeutet, daß der Datenbus zum Transfer von Daten zwischen CPU und PIO benutzt wird. Im praktischen Einsatz wird häufig die Adresse A1 für die angegebene Auswahlfunktion verwendet.

**$\overline{\text{CS}}$** (Chip Select)
Bausteinauswahl (Eingang, low aktiv)
Low-Pegel an diesem Anschluß versetzt die PIO in die Lage, Steuerworte oder Daten von der CPU während einer CPU-Schreiboperation zu empfangen bzw. während einer CPU-Leseoperation Daten an die CPU zu senden. Im praktischen Einsatzfall wird dieses Signal meist durch Dekodierung der Adressen A2...A7 gewonnen, wenn A0 und A1 bereits für B/$\overline{\text{A}}$ SEL und C/$\overline{\text{D}}$ SEL verwendet wurden.

**$\overline{\text{M1}}$** (Machine Cycle one Signal)
Maschinenzyklus 1-Signal (Eingang, low aktiv)
Dieses von der CPU ausgesandte Signal dient der Synchronisierung innerer Operationen in der PIO. Sind die Signale $\overline{\text{M1}}$ und $\overline{\text{RD}}$ aktiv, dann befindet sich die CPU im Befehlsholezyklus (dieser Fall ist für die RETI-Erkennung durch die PIO von Bedeutung).
Sind $\overline{\text{M1}}$ und $\overline{\text{IORQ}}$ aktiv, quittiert die CPU eine Interruptanforderung.
$\overline{\text{M1}}$ hat in der PIO zwei weitere Funktionen:
1. Das Signal $\overline{\text{M1}}$ synchronisiert die Interruptlogik der PIO.
2. Ist $\overline{\text{M1}}$ aktiv ohne aktives $\overline{\text{RD}}$- oder $\overline{\text{IORQ}}$-Signal, geht die PIO in den RESET-Zustand.

**CP** (Clockpulse)
Systemtakt (Eingang)
Die U855 benutzt den Standardsystemtakt (Einphasentakt) des U880-Systems. Dieser sichert die Synchronisation der Signale untereinander ab.

**$\overline{\text{IORQ}}$** (Input/Output Request)
Ein/Ausgabeanforderung (Eingang, low aktiv)
Das Signal $\overline{\text{IORQ}}$ wird in Verbindung mit den Signalen B/$\overline{\text{A}}$ SEL, C/$\overline{\text{D}}$ SEL, $\overline{\text{CS}}$, $\overline{\text{RD}}$ und $\overline{\text{M1}}$ zum Transport von Befehlen und Daten zwischen CPU und PIO verwendet. Sind $\overline{\text{CS}}$, $\overline{\text{RD}}$ und $\overline{\text{IORQ}}$ aktiv, dann überträgt der mit B/$\overline{\text{A}}$ selektierte Kanal Daten zur CPU (Leseoperation).
Sind $\overline{\text{CS}}$ und $\overline{\text{IORQ}}$ aktiv, $\overline{\text{RD}}$ und $\overline{\text{M1}}$ jedoch nicht aktiv, dann schreibt die CPU Daten- oder Befehlsworte (je nach Signal-Vorgabe am C/$\overline{\text{D}}$-Eingang) in den selektierten Kanal ein (Schreiboperation).
Sind $\overline{\text{M1}}$ und $\overline{\text{IORQ}}$ aktiv, bedeutet das, daß die CPU eine Interruptanforderung quittiert, und derjenige interruptanfordernde Kanal, der die höchste Priorität hat, legt seinen Interruptvektor auf den Datenbus.

**$\overline{\text{RD}}$** (Read)
Lesezyklus-Signal (Eingang, low aktiv)
Mit aktivem $\overline{\text{RD}}$-Signal wird seitens der CPU eine Speicher- oder I/O-Leseoperation gekennzeichnet. Um Daten von der PIO zur CPU zu übertragen, wird das $\overline{\text{RD}}$-Signal in Verbindung mit den Signalen B/$\overline{\text{A}}$ SEL, C/$\overline{\text{D}}$ SEL, $\overline{\text{CS}}$ und $\overline{\text{IORQ}}$ verwendet.

**IBI** (Interrupt Enable In)
Interrupt-Freigabe-Eingang (Eingang, high aktiv)
Dieses Signal wird benötigt, um eine Interruptprioritätskette zu bilden (IEI ist mit IEO des nächsthöherpriorisierten peripheren Schaltkreises zu verbinden).
High-Pegel bedeutet, daß momentan kein Interrupt höherer Priorität abgearbeitet wird oder angemeldet wird. (Ausnahme bei noch nicht bestätigtem Interrupt des aktuell höchstpriorisierten peripheren Schaltkreises und RETI-Dekodierung).

**IEO** (Interrupt Enable Out)
Interrupt-Freigabe-Ausgang (Ausgang, high aktiv)
Das IEO-Signal ist ein weiteres für die Funktion der Interruptprioritätskette erforderliches Signal.
IEO führt nur dann High-Pegel, wenn der Eingang IEI desselben Schaltkreises High-Pegel erhält und kein eigener Interrupt abgearbeitet oder angemeldet wird (Ausnahme bei noch nicht bestätigtem Interrupt des aktuell höchstpriorisierten peripheren Schaltkreises und RETI-Dekodierung).
Auf diese Weise kann verhindert werden, daß Funktionseinheiten mit niederer Priorität einen Interrupt auslösen können, solange von einer Funktionseinheit mit höherer Priorität bei der CPU eine Interruptbehandlung angefordert wird.

**$\overline{\text{INT}}$** (Interrupt Request)
Interruptanforderung (Ausgang, Open-drain, low aktiv)
Aktivierung des Ausgangs signalisiert der CPU die Anmeldung eines Interrupts.

**A0...A7** (Port A Bus)
Kanal-A-Bus (bidirektional, Tri-state)
Dieser 8bit-Bus wird zum Transfer von Daten und/oder Status- oder Steuerinformationen zwischen Kanal A der PIO und externen angeschlossenen Geräten benutzt. A0 ist das niederwertigste Bit des Busses.

**$\overline{\text{ASTB}}$** (Port A Strobe Pulse)
Kanal A-Strobe (Eingang, low aktiv)
Die Bedeutung dieses Signals hängt von der Betriebsart ab, die für Kanal A gewählt wurde:
1. MODE 0: Der Strobeimpuls wird von der Peripherie abgegeben, um die Daten aus dem Ausgaberegister zu übernehmen. Das Ende des Strobeimpulses (positive Flanke) gilt als Quittierung der erfolgten Übernahme.
2. MODE 1: Der Strobeimpuls wird von der Peripherie abgegeben, um Daten von der Peripherie in das Eingaberegister des Kanals zu laden. Die Daten werden in die U855 geladen, wenn das Signal aktiv ist.
3. MODE 2: Wenn das Signal aktiv ist, werden Daten vom Ausgaberegister des Kanals A auf den bidirektionalen Bus des Kanals A gelegt. Das Ende des Strobeimpulses (positive Flanke) gilt als Bestätigung für den Empfang der Daten.
4. MODE 3: Der Strobeimpuls ist intern verboten.

**ARDY** (Register A Ready)
Quittung Kanal A (Ausgang, high aktiv)
Bedeutung ist abhängig von Betriebsart:
1. MODE 0: Das Signal wird aktiv, um anzuzeigen, daß das Ausgaberegister des Kanals geladen ist und daß die Daten abgerufen werden können. Nach Übernahmequittierung (steigende Flanke des Strobeimpulses) durch die periphere Schaltung wird das Signal inaktiv.
2. MODE 1: Das Signal ist aktiv, wenn das Eingaberegister des Kanals gelesen und es bereit ist, Daten vom peripheren Gerät zu übernehmen.
3. MODE 2: Das Signal ist aktiv, wenn Daten ins Ausgaberegister vom Kanal A geladen und für einen Transfer zum peripheren Gerät verfügbar sind. In dieser Mode liegen die Daten am Kanal-A-Datenbus nicht an, sofern nicht $\overline{\text{ASTB}}$ aktiv ist.
4. MODE 3: Das Signal ist nicht verwendbar und liegt auf Potential "low".

**B0...B7** (Port B Bus)
Kanal-B-Bus (bidirektional, Tri-state)
Dieser 8bit-Bus wird zum Transfer von Daten und/oder Status- oder Steuerinformationen zwischen Kanal B der PIO und externen angeschlossenen Geräten benutzt. Der Kanal-B-Bus ist zur Ansteuerung von Darlingtonstufen ausgelegt (minimal 1,5 mA bei $U_{OH} = 1,5\ V$). B0 ist das niederwertigste Bit des Busses.

**$\overline{\text{BSTB}}$** (Port B Strobe Pulse)
Kanal B-Strobe (Eingang, low aktiv)
Bedeutung entsprechend $\overline{\text{ASTB}}$ mit der folgenden Ausnahme:
In der bidirektionalen Betriebsart des Kanals A werden mit diesem Signal Daten vom peripheren Gerät in das Eingaberegister des Kanals A eingeschrieben.

**BRDY** (Register B Ready)
Quittung Kanal B (Ausgang, high aktiv)
Bedeutung entspricht ARDY mit der folgenden Ausnahme:
In der bidirektionalen Betriebsart des Kanals A ist das Signal "high" wenn das Eingaberegister des Kanals A gelesen und bereit ist, Daten vom peripheren Gerät zu übernehmen.

**4. Rücksetzen**

Nach Einschalten der Betriebsspannung geht die PIO U855 automatisch in den RESET-Zustand über (power-on-clear).
Dieser Zustand löst folgende Aktivitäten aus:
- Beide Kanalmaskierungssteuerregister werden zurückgesetzt.
- Die Kanaldatenleitungen werden in den hochohmigen Zustand geschaltet. Die Quittierungsleitungen — Ready — werden inaktiv (Low-Zustand). Die PIO geht automatisch in die Betriebsart Byte-Eingabe (Mode 1).
- Die Interrupt-Freigabe-Flipflops beider Kanäle werden zurückgesetzt.
- Beide Kanalausgaberegister werden zurückgesetzt.
- Das Vektoradressenregister wird nicht zurückgesetzt.

Die PIO besitzt keinen RESET-Eingang, da die Anzahl der Anschlüsse auf 40 begrenzt ist. Außer dem automatischen Rücksetzen bei Zuschalten der Betriebsspannung gibt es jedoch eine weitere Möglichkeit, die PIO rückzusetzen. Diese besteht darin, daß ein aktives $\overline{\text{M1}}$-Signal mindestens 2 Taktperioden lang angelegt wird, die Signale $\overline{\text{RD}}$ und $\overline{\text{IORQ}}$ jedoch inaktiv bleiben. Nach Beendigung des aktiven $\overline{\text{M1}}$-Signals geht die PIO in den RESET-Zustand über. Damit läßt sich beispielsweise mittels einer zusätzlichen externen Gatterschaltung ein hardware-mäßiges Rücksetzen zusätzlich zum "power-on-clear" realisieren. Eine mögliche Schaltung dafür zeigt Bild 4.

*KI generierte Interpretation der Abbildung: Bild 4 zeigt eine kleine Logikschaltung. Ein UND-Gatter (beschriftet mit „&“) verknüpft das Signal „von CPU-RESET“ mit dem Signal „von CPU-M1“. Der Ausgang des UND-Gatters führt zu einem rechteckigen Block (Puffer oder Verzögerungsglied), der wiederum an den Eingang „zu PIO-M1“ angeschlossen ist. Diese Schaltung erzeugt am M1-Eingang der PIO die für einen Reset notwendige Signalbedingung.*

**Bild 4: Gatterschaltung zum Rücksetzen der U855D mittels CPU-RESET**

Wenn die PIO den Rücksetz-Zustand erreicht hat, bleibt sie solange in diesem Zustand, bis sie ein an sie gerichtetes Steuerwort von der CPU empfängt.

**5. Programmierung des PIO-Schaltkreises U855**

**5.1. Interruptvektor**

Die PIO U855 wurde für den Einsatz mit der CPU U880 in Interruptmode 2 ausgelegt. In dieser leistungsfähigen Interruptbetriebsart wird innerhalb des von der CPU eingeschobenen Interruptquittierungszyklus (INTA-Cycle) von der PIO der sogenannte Interruptvektor auf den Datenbus gelegt. Aus dem Interruptvektor und dem I-Register der CPU bildet diese einen Pointer, der auf eine Speicheradresse zeigt. Aus den Inhalten dieses und des nachfolgenden Speicherplatzes wird die Startadresse des durch die Interruptanmeldung aufzurufenden Unterprogrammes gebildet. Zur Speicherung der Startadresse werden jeweils zwei Speicherplätze benötigt.
Auf dem geradzahligen Speicherplatz (Bit 0 der Speicherplatzadresse = 0) wird das niederwertige Byte der Startadresse abgelegt, auf dem nachfolgenden das höherwertige. Daher sind für das Laden des eigentlichen Interruptvektors in die PIO nur sieben Bits erforderlich (Bit 1...Bit 7), Bit 0 muß mit "0" belegt werden und dient zur Identifikation, daß das gerade geladene Steuerwort der Interruptvektor ist. Laden geschieht mittels eines OUT-Befehls (mit C/$\overline{\text{D}}$ = 1 zur Kennzeichnung, daß es sich um ein Steuerwort handelt) an den entsprechenden Kanal der PIO.

Das Steuerwort hat folgendes Format:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| V7 | V6 | V5 | V4 | V3 | V2 | V1 | 0 |

Durch L-Signal auf der Leitung D0 wird das Steuerwort als Interruptvektor identifiziert.

**5.2. Wahl der Betriebsart**

Mit der PIO U855 sind 4 Betriebsarten (Modes) realisierbar:
- Mode 0: Byte-Ausgabe
- Mode 1: Byte-Eingabe
- Mode 2: Byte-Ein/Ausgabe
- Mode 3: Bit-Ein/Ausgabe

Der Kanal A kann in jeder der vier Betriebsarten benutzt werden, Kanal B in den Betriebsarten 0, 1 und 3. Die Auswahl dazu wird in der Kanallogik im 2bit-Betriebsartenregister gespeichert. Das Laden dieses Registers und damit die Auswahl der Betriebsart geschieht mittels eines OUT-Befehls der CPU an den betreffenden Kanal der PIO. Das dafür notwendige Steuerwort hat folgendes Format:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| M1 | M0 | X | X | 1 | 1 | 1 | 1 |

Definition der Betriebsart:
| M1 | M0 | Mode |
| :---: | :---: | :--- |
| 0 | 0 | 0 |
| 0 | 1 | 1 |
| 1 | 0 | 2 |
| 1 | 1 | 3 |

Die Belegung der Bits D0, D1, D2 und D3 mit "1" kennzeichnet das Steuerwort als Betriebsartenauswahlwort. Die Bits D4 und D5 werden ignoriert.

Ist die Betriebsart Byte-Ausgabe (Mode 0) gewählt, so können durch einen OUT-Befehl der CPU Daten in das gewünschte PIO-Ausgaberegister geschrieben werden. Diese werden unmittelbar auf die zugehörigen Portleitungen A0...A7 bzw. B0...B7 durchgeschaltet. Unabhängig vom Zustand der Quittierungssignale STROBE und READY kann durch entsprechende OUT-Operationen der CPU der Inhalt der Ausgaberegister jederzeit verändert werden.
Der jeweilige Inhalt der Ausgaberegister kann außerdem von der CPU mittels einer IN-Operation gelesen werden. Die Ausgabe (OUT-Operation) eines Datenwortes veranlaßt das READY-Signal des betreffenden Kanals in den aktiven Zustand überzugehen (high), um einem externen Gerät anzuzeigen, daß ein neu geschriebenes Datenwort aus dem Ausgaberegister der PIO gelesen werden kann. Das READY-Signal bleibt aktiv bis die Übernahme der Daten durch die externe Schaltung abgeschlossen ist. Die steigende Flanke von STROBE gilt als Quittung für die erfolgte Übernahme der Daten durch das externe Gerät und veranlaßt die READY-Leitung, wieder in den inaktiven Zustand überzugehen (nach interner Synchronisation mit dem Systemtakt). Außerdem wird nach der steigenden Flanke von STROBE im internen Interruptspeicher eine Interruptanmeldung abgespeichert. Diese löst unter Voraussetzung, daß das Interruptfreigabe-Flipflop gesetzt ist und das anfordernde Port im betrachteten Zeitpunkt die höchste Priorität besitzt, ein aktives $\overline{\text{INT}}$-Signal aus.

Ist die Betriebsart 1 (Byte-Eingabe) gewählt, so kann durch einen IN-Befehl der CPU das im Eingaberegister des jeweiligen Kanals gespeicherte Datenbyte zur CPU übertragen werden. Für Operationen im Hand-shake-Betrieb ist zunächst die READY-Leitung in den aktiven Zustand zu bringen (IN-Befehl der CPU), um dem externen Gerät anzuzeigen, daß dem PIO-Eingaberegister Daten zugeführt werden sollen. Das Laden der an den Kanalleitungen A0...A7 bzw. B0...B7 anliegenden Daten in das betreffende Eingaberegister erfolgt während des Low-Zustandes des STROBE-Signals, das vom externen Gerät ausgesendet wird. Die steigende Flanke des STROBE-Impulses bewirkt eine Interruptanforderung (falls die Voraussetzungen dafür gegeben sind) und überführt nach interner Synchronisation mit dem Systemtakt das READY-Signal wieder in den inaktiven Zustand, falls dies erforderlich ist. Das Laden der Eingaberegister kann auch unabhängig vom Zustand der READY-Leitung erfolgen.

In der Betriebsart 2 (Byte-Ein-/Ausgabe, bidirektional) werden alle 4 Quittungsleitungen verwendet. Daher kann nur 1 Kanal (Kanal A) in dieser Betriebsart benutzt werden, der andere Kanal (Kanal B) kann dann nur noch in Betriebsart 3 betrieben werden. In Betriebsart 2 werden die Quittungsleitungen des Kanals A zur Ausgabesteuerung des Kanals A, die des Kanals B zur Eingabesteuerung ebenfalls Kanals A benutzt. Damit stehen die Signale an ARDY und BRDY gleichzeitig zur Information über den aktuellen Zustand des Kanals A zur Verfügung. Die Daten des Ausgaberegisters des Kanals A werden nur dann auf den Kanalleitungen A0...A7 ausgegeben, wenn das Signal $\overline{\text{ASTB}}$ aktiv ist.
Werden in Betriebsart 2 Leseoperationen der CPU durchgeführt, die den Kanal A betreffen, so werden in Abhängigkeit vom Zustand des Signals $\overline{\text{ASTB}}$ entweder Daten aus dem Eingaberegister (bei $\overline{\text{ASTB}}$ = H) oder Daten aus dem Ausgaberegister (bei $\overline{\text{ASTB}}$ = L) gelesen.

In der Betriebsart 3 (Bit-Ein-/Ausgabe) können alle Bits des jeweiligen Kanals (A0...A7 bzw. B0...B7) unabhängig voneinander als Ein- oder Ausgänge definiert werden. Daher muß nach Auswahl der Betriebsart 3 eines Kanals mit dem nächstfolgenden Steuerwort für diesen Kanal definiert werden, welche Kanalleitungen als Eingänge bzw. als Ausgänge fungieren sollen. Das erforderliche Steuerwort hat folgendes Format:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| I/$O_7$ | I/$O_6$ | I/$O_5$ | I/$O_4$ | I/$O_3$ | I/$O_2$ | I/$O_1$ | I/$O_0$ |

Einer "0" entspricht dabei die Zuordnung eines Ausganges, einer "1" wird ein Eingang zugeordnet.

Führt die CPU eine IN-Operation mit einem in Betriebsart 3 arbeitenden Kanal aus, so setzen sich die der CPU zugeführten Daten bitweise entsprechend der vorgewählten Funktion der Kanalleitungen aus dem Inhalt des Eingaberegisters und dem Inhalt des Ausgaberegisters zusammen. Waren die entsprechenden Kanalleitungen als Eingänge definiert, werden die zugehörigen Bits dem Eingaberegister, im anderen Falle dem Ausgaberegister entnommen. In der Betriebsart 3 werden die Quittungsleitungen nicht benutzt, der READY-Ausgang führt Low-Pegel. Eine Ausnahme bildet dabei der Fall, daß Kanal A in Betriebsart 2 betrieben wird.

**5.3. Interruptsteuerwort**

Die Auslösung von Interruptanmeldungen beim U855 kann auf 2 Arten erfolgen. In den byte-orientierten Betriebsarten 0, 1 und 2 wird die Auslösung jeweils von der steigenden Flanke des STROBE-Impulses veranlaßt. In der Betriebsart 3 hingegen besteht für den Anwender die Möglichkeit, die Bedingungen für die Auslösung eines Interrupts im Programm selbst festzulegen. In dieser Betriebsart kommt es zur internen Interruptanmeldung, wenn eine bestimmte vorher per Programm vorgegebene Datenkonfiguration an den Kanalleitungen eintritt. Für alle Betriebsarten gilt jedoch, daß die interne Interruptanmeldung (im internen Interruptspeicher abgespeichert) nur dann zur CPU weitergegeben wird, wenn das zu jedem PIO-Kanal gehörige Interrupt-Freigabe-Flipflop gesetzt ist und der betreffende Kanal die höchste aktuelle Priorität innerhalb der Interruptkaskade besitzt.
Das Interrupt-Freigabe-Flipflop kann durch zwei Interruptsteuerworte beeinflußt werden. Das im folgenden zuerst beschriebene Interruptsteuerwort hat die Funktion, das Interrupt-Freigabe-Flipflop des jeweiligen Kanals zu laden und die Auswahlkriterien zur Auslösung eines Interrupts in Mode 3 zu definieren. Das Steuerwort hat folgendes Format:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :--- | :--- | :--- | :--- | :---: | :---: | :---: | :---: |
| Interrupt-freigabe | UND/ODER | High/Low | nächstes Steuerwort ist Maske | 0 | 1 | 1 | 1 |

Mit der angegebenen Belegung der Bits D0...D3 wird das Steuerwort als Interruptsteuerwort identifiziert.
Ist das Bit D7 mit "1" belegt, so wird das Interrupt-Freigabe-Flipflop gesetzt und die Weitergabe von im internen Interruptspeicher anhängigen oder zukünftigen Interruptanmeldungen ermöglicht. Ist D7 = 0, so wird das Interrupt-Freigabe-Flipflop rückgesetzt und die Weitergabe einer anhängigen oder zukünftigen Interruptanmeldung verhindert.
Besteht eine interne anhängige Interruptanmeldung und wird in der weiteren Programmabarbeitung das Interrupt-Freigabe-Flipflop gesetzt, so wird diese Anmeldung weitergegeben, unabhängig davon, ob das Ereignis welches die interne Interruptanmeldung hervorgerufen hat noch aktuell ist oder nicht.
Befindet sich ein Kanal bereits in der Interruptbearbeitung und treten für diesen Kanal nach dem Interruptbestätigungszyklus weitere interruptauslösende Ereignisse ein, so wird die entstehende Interruptanmeldung intern abgespeichert, auch wenn diese Anmeldung später nicht mehr aktuell ist (außer in Mode 3).
Mit den Bits D6, D5 und indirekt mit D4 werden in der Betriebsart 3 die Bedingungen definiert, unter denen eine Interruptanmeldung ausgelöst werden soll. Ist D6 = 1, so gilt für alle auf die Interruptbildung einflußnehmenden Leitungen die UND-Verknüpfung, für D6 = 0 die ODER-Verknüpfung. Dies bedeutet, daß im Falle D6 = 1 **alle** auf die Interruptbildung einflußnehmenden Leitungen aktiv (im Sinne des mit D5 definierten logischen Pegels) sein müssen, im Falle D6 = 0 jedoch genügt aktives Signal an **einer** beteiligten Kanalleitung.
Ist D5 = 1, so gilt als aktives Signal der High-Pegel, bei D5 = 0 der Low-Pegel.
Ist D4 = 1, so wartet der PIO-Kanal das nächste an diesen Kanal gerichtete Steuerwort als Maske. Mittels dieses Maskierungswortes wird definiert, welche der Kanalleitungen an einer beteiligten Interruptbildung Einfluß nehmen sollen (mit den von D6 und D5 gemachten logischen Vorgaben).
Das Steuerwort zur Maskierung hat folgendes Format:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| MB7 | MB6 | MB5 | MB4 | MB3 | MB2 | MB1 | MB0 |

Nur diejenigen Kanalleitungen $A_n$ bzw. $B_n$ nehmen auf die Bildung eines Interrupts Einfluß, deren entsprechendes Bit im Maskierungssteuerwort mit $MB_n$ = 0 geladen wurde.
Bit D4 = 0 im Interruptsteuerwort bedeutet, es folgt keine Maske.
Bit 4 hat jedoch eine weitere Bedeutung. Wird das oben beschriebene Interruptsteuerwort mit D4 = 1 von der CPU an einen PIO-Kanal ausgegeben, so wird eine intern anhängige, aber noch nicht von der CPU bestätigte Interruptanmeldung rückgesetzt, unabhängig davon, in welcher Betriebsart gearbeitet wird. Nach Laden dieses Interruptsteuerwortes mit D4 = 1 ist das Maskierungssteuerwort zu laden, unabhängig davon, ob es in der gerade benutzten Betriebsart verwendet wird.
Wie bereits erwähnt, kann das Interrupt-Freigabe-Flipflop auch durch ein anderes Steuerwort beeinflußt werden. Es hat folgendes Format:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Inter-rupt-frei-gabe | X | X | X | 0 | 0 | 1 | 1 |

Mittels dieses Steuerwortes wird ausschließlich das Interrupt-Freigabe-Flipflop beeinflußt, es eignet sich daher besonders zur Beeinflussung der Interrupt-Freigabe während des laufenden Programms.
Mit D7 = 0 wird wie beim zuerst angeführten Interruptsteuerwort die Weitergabe einer intern vorhandenen Interruptanmeldung verhindert. Mit D7 = 1 kann die Weitergabe erfolgen.
Auf zwei unterschiedliche Anwendungsprobleme soll hier bezüglich der Interrupt-Freigabe eingegangen werden.
Es wurde angenommen, ein externer asynchroner Interrupt erscheint von einem PIO-Kanal zu dem Zeitpunkt, da gerade von der CPU das Steuerwort zur Rücksetzung des Interrupt-Freigabe-Flipflops (03H) genau dieses Kanals ausgesendet wird. Die CPU leitet den Interrupt-Bestätigungszyklus ein und erwartet das Aussenden des Interruptvektors vom entsprechenden Kanal der PIO. Die PIO ist aber durch das an sie gerichtete Interruptsteuerwort inzwischen "interruptunfähig" geworden und sendet nicht ihren Interruptvektor zur CPU. Die CPU liest einen fehlerhaften Interruptvektor und arbeitet mit falschem Programm weiter.
Eine Möglichkeit, diesen Fehler zu vermeiden, besteht darin, die CPU ihrerseits vor Ausgabe des Interruptsteuerwortes an die PIO "interruptunfähig" zu machen. Dies kann mit einem DI-Befehl leicht erreicht werden. Anschließend wird das Interrupt-Freigabe-Flipflop des PIO-Kanals rückgesetzt. Als letzter Schritt wird ein EI-Befehl abgearbeitet und die CPU ist wieder interruptfähig.
Die Befehlsfolge wäre dann:
```asm
LD A, 03H
DI      ; DISABLE CPU
OUT PIO ; DISABLE PIO
EI      ; ENABLE CPU
```
Als weiteres Anwendungsproblem werde folgendes angenommen:
Während des Programmablaufes soll ein Kanal seine Betriebsart wechseln z. B. von Betriebsart 1 in die Betriebsart 3 und später zurück zur Betriebsart 1. Aufgrund der internen Schaltung der PIO muß jedoch damit gerechnet werden, daß sich bei einem Wechsel von und nach Betriebsart 3 interne Interruptanmeldungen ergeben, die sich auswirken, sobald eine Interrupt-Freigabe erfolgt. Es wird daher empfohlen, bei einem Betriebsartenwechsel in die Mode 3 folgende Reihenfolge einzuhalten:
1. PIO-Kanal "interruptunfähig" machen,
2. Modewechsel vollziehen. Dabei Interruptsteuerwort mit D4 = 1 verwenden und Maske laden.
3. Freigabe des Interrupt-Freigabe-Flipflops kann erfolgen.

Eine mögliche Befehlsfolge wäre:
```asm
LD A, 03H
DI
OUT PIO
EI

LD A, 0CFH
OUT PIO   ; MODE 3
LD A, XX
OUT PIO   ; I/O-Steuerwort
LD A, Y7H ; Y = 1, 3, 5, 7 je nach Verknüpfungsbedingungen
OUT PIO   ; Interruptsteuerwort mit D4 = 1
LD A, Maske
OUT PIO   ; Maske
LD A, 87H
OUT PIO   ; Interrupt-Freigabe-Flipflop setzen
```
Für den Wechsel von Mode 3 in eine andere Betriebsart wird folgende Reihenfolge empfohlen:
1. PIO-Kanal "interruptunfähig" machen
2. Modewechsel vollziehen
3. Unabhängig von der Betriebsart Interruptsteuerwort mit D4 = 1 an den PIO-Kanal ausgeben und anschließend "Maske".
4. Freigabe des Interrupt-Freigabe-Flipflops kann erfolgen.

Die Befehlsfolge wäre dann:
```asm
LD A, 03H
DI
OUT PIO
EI

LD A, 4FH
OUT PIO   ; MODE 1
LD A, 17H
OUT PIO   ; Interruptsteuerwort mit D4 = 1
LD A, 83H ; "Maske"
OUT PIO
LD A, 87H
OUT PIO   ; Interrupt-Freigabe-Flipflop setzen
```

**6. Beschreibung der Betriebsarten**

**6.1. Byte-Ausgabe (Mode 0)**

Bei der Ausführung einer Daten-Ausgabe-Operation durch die CPU werden diese direkt in das Ausgaberegister des entsprechenden PIO-Kanals geschrieben. In Bild 5 ist das Zeitdiagramm eines solchen Vorganges bei Quittierung durch ein externes Gerät dargestellt. Das intern erzeugte Schreibsignal $\overline{\text{WR}}^x$ wird gebildet aus der logischen Kombination:
$\overline{\text{WR}}^x = \text{RD} \cdot \overline{\text{CS}} \cdot \overline{\text{C}/\overline{\text{D}}} \cdot \overline{\text{IORQ}} \cdot \overline{\text{M1}}$

*KI generierte Interpretation der Abbildung: Bild 5 zeigt das Timing für Byte-Ausgabe (Mode 0) mit Quittierung. Der CPU-Takt CP taktet die Operation. Wenn das interne Schreibsignal $\overline{\text{WR}}^*$ aktiv (low) wird, stabilisieren sich die Daten am „Kanalbus (8-bit)“. Nach der steigenden Flanke von $\overline{\text{WR}}^*$ geht READY auf high, was der Peripherie die Gültigkeit der Daten anzeigt. Die Peripherie antwortet mit einem STROBE-Signal (low). Während STROBE low ist, bleibt READY high. Erst nach der steigenden Flanke von STROBE (Datenübernahme durch Peripherie) geht READY auf low und das Interrupt-Signal $\overline{\text{INT}}$ wird aktiviert.*

**Bild 5: Zeitablauf in Mode 0 mit Quittierung**

In dieser Kombination sind folgende Komponenten enthalten:
(auf eine Unterscheidung der Kanäle A und B soll in der Beschreibung verzichtet werden)
1. daß der Schaltkreis angesprochen ist: $\overline{\text{CS}}$
2. daß eine I/O-Operation ausgeführt wird: $\overline{\text{IORQ}}$
3. daß die durchgeführte Operation eine Schreiboperation ist: RD (da die PIO keinen Eingang für "Schreiben" besitzt, wird "Nichtlesen" als "Schreiben" interpretiert)
4. daß es sich um ein Datenwort handelt: $\overline{\text{C}/\overline{\text{D}}}$
5. daß ein zufälliges Ansprechen des $\overline{\text{CS}}$-Einganges eines PIO-Schaltkreises während eines Interrupt-Anerkennungszyklus nicht zu Fehlinterpretationen und damit zu unbeabsichtigtem Einschreiben führt: $\overline{\text{M1}}$

Nachdem der interne Einschreibvorgang abgeschlossen ist (Ende des $\overline{\text{WR}}^x$-Impulses) wird nach der nächsten fallenden Flanke des Systemtaktes das READY-Signal in den aktiven Zustand gebracht (High-Pegel). Mit der steigenden Flanke von READY wird nach außen hin angezeigt, daß sich neu geladene Daten im Ausgaberegister des entsprechenden Kanals befinden und gelesen werden können. Die positive Flanke des vom externen Gerät seinerseits erzeugten STROBE-Impulses gilt als Quittung für die erfolgte Übernahme der Ausgaberegisterdaten durch das externe Gerät. Sie löst, falls die entsprechenden Randbedingungen gegeben sind (Interrupt-Freigabe-Flipflop des Kanals gesetzt und höchste aktuelle Priorität innerhalb der Interrupt-Kaskade) die Anmeldung für einen Interrupt aus, in dessen Abarbeitung beispielsweise wieder neue Daten ins Ausgaberegister geladen werden können. Außerdem bewertet die steigende Flanke vom STROBE, daß nach der nächsten fallenden Flanke des Systemtaktes das READY-Signal wieder inaktiv geschaltet wird. Dies gilt für das externe Gerät als Zeichen dafür, daß zum aktuellen Zeitpunkt noch keine neu eingeschriebenen Daten im Ausgaberegister bereitstehen.
Bild 6 zeigt das Zeitdiagramm für den Fall, daß das externe Gerät nicht mit einem STROBE-Signal quittiert, sondern daß der STROBE-Eingang des PIO-Kanals auf einen festen Pegelwert gelegt ist. In dieser Anordnung kann das READY-Signal also nicht von außen her durch das Ende eines STROBE-Signals in den inaktiven Zustand versetzt werden, ebenso erfolgt keine Interruptanmeldung. Um dem externen Gerät dennoch mitteilen zu können, daß aktuelle Daten in das Ausgaberegister geladen worden sind, wird der READY-Ausgang durch die interne Schaltung der PIO am Ende des Beschreibens des PIO-Kanals mit Daten nach Low geschaltet.

*KI generierte Interpretation der Abbildung: Bild 6 zeigt Mode 0 ohne Quittierung. Der STROBE-Eingang wird fest auf Low gehalten. Sobald $\overline{\text{WR}}^*$ inaktiv wird, geht READY für eine kurze Dauer (etwa eine Taktperiode) auf high und kehrt dann automatisch wieder nach low zurück, ohne auf eine Flanke von STROBE zu warten. INT bleibt inaktiv.*

**Bild 6: Zeitablauf in Mode 0 ohne Quittierung**

Die steigende Flanke von $\overline{\text{IORQ}}$ in dieser Operation bewirkt, daß nach der nächsten fallenden Flanke des Systemtaktes READY wieder in den aktiven Zustand (High) übergeht. Die steigende Flanke von READY zeigt damit wiederum nach außen hin an, daß soeben aktuelle Daten ins Ausgaberegister des Kanals geladen worden sind. Eine weitere Möglichkeit des Einsatzes des Schaltkreises U855 in dieser Betriebsart ergibt sich beim Zusammenschalten der STROBE- und READY-Leitungen eines Kanals. Das sich dabei ergebende Zeitdiagramm ist in Bild 7 dargestellt.

*KI generierte Interpretation der Abbildung: Bild 7 zeigt Mode 0 mit Kopplung von READY und STROBE. Die beiden Anschlüsse sind extern miteinander verbunden. Wenn READY durch den CPU-Schreibzugriff high wird, zieht es gleichzeitig STROBE high. Diese steigende Flanke an STROBE triggert den PIO-internen Interrupt und setzt READY kurze Zeit später wieder zurück. Es entsteht ein kurzer Nadelimpuls an der READY/STROBE-Leitung.*

**Bild 7: Zeitablauf in Mode 0 bei Kopplung von READY und STROBE**

Nach Ausgabe von Daten in das Ausgaberegister wird die READY-Leitung aktiv geschaltet. Dies bedeutet aber infolge der Zusammenschaltung von READY und STROBE eine steigende Flanke am STROBE-Eingang. Aufgrund dessen wird nach der nächsten fallenden Flanke des Systemtaktes das READY-Signal wieder inaktiv geschaltet. So ergibt sich am READY-Ausgang nach jeder Daten-OUT-Operation der CPU an dem PIO-Kanal ein positiver Impuls, der etwa eine Taktperiode lang ist. Damit kann das externe Gerät die Ausgabedaten sowohl flankengesteuert (positive oder negative Flanke) als auch zustandsgesteuert (positiver Impuls) übernehmen. Interruptanmeldungen werden nicht ausgelöst, da in dieser Betriebsweise aufgrund der internen Schaltung des PIO-Kanals sowohl der Zustand $\overline{\text{M1}}$ = 0 als auch STROBE = 0 eine Übernahme einer Interruptforderung in den internen Interruptspeicher (Slave) verhindert. Die Bedingung, daß ein $\overline{\text{M1}}$-Zyklus nach Abarbeitung des OUT-Befehls erfolgt, ist ohnehin erfüllt, da für jede neue Befehlsbearbeitung zunächst ein Befehlsholezyklus erforderlich ist und auch ein möglicherweise von anderer Stelle ausgelöster Interrupt-Anerkennungszyklus ein $\overline{\text{M1}}$-Zyklus ist.

Die PIO wird durch ein Steuerwort, das an einen ihrer Kanäle gerichtet wird, aus dem Rücksetzzustand entlassen. Befindet sich die PIO nicht mehr im Rücksetzzustand, so ist es möglich, z. B. schon während der Kanal im Mode 1 arbeitet, die Ausgaberegister mit Daten zu laden.
Wünscht der Anwender, daß die Ausgänge zu einem von ihm festgelegten Zeitpunkt mit der vorgegebenen Belegung aktiv werden, so genügt es, zu diesem Zeitpunkt in den Mode 0 überzugehen.

**6.2. Byte-Eingabe (Mode 1)**

Die Betriebsart Byte-Eingabe ermöglicht die 8-bit-parallele Eingabe der an den Kanalleitungen (A0...A7 bzw. B0...B7) anliegenden Daten in das Eingaberegister des betreffenden PIO-Kanals. Die Übernahme erfolgt während des Low-Zustandes von STROBE. Im Bild 8 ist das Zeitdiagramm für den Hand-shaking-Betrieb dargestellt. (Das interne $\overline{\text{RD}}^x$-Signal wird wiederum aus mehreren logischen Komponenten zusammengesetzt, analog dem $\overline{\text{WR}}^x$-Signal bei Betriebsart 0.)

*KI generierte Interpretation der Abbildung: Bild 8 zeigt das Timing für Byte-Eingabe (Mode 1) mit Quittierung. READY ist initial high (Eingabebereit). Die Peripherie legt Daten an den Bus und zieht STROBE auf low. Solange STROBE low ist, werden die Daten abgetastet („SAMPLE“). Mit der steigenden Flanke von STROBE werden die Daten im PIO-Register verriegelt, READY geht auf low (nicht mehr bereit) und ein Interrupt $\overline{\text{INT}}$ wird ausgelöst. READY bleibt solange low, bis die CPU die Daten über das Signal $\overline{\text{RD}}^*$ ausliest.*

**Bild 8: Zeitablauf in Mode 1 mit Quittierung**

Die steigende Flanke des STROBE-Impulses löst eine Interruptanmeldung aus, falls das Interrupt-Freigabe-Flipflop gesetzt ist und höchste Priorität vorliegt. Die nächste fallende Flanke des Systemtaktes bringt das READY-Signal in den inaktiven Zustand. Damit wird nach außen hin angezeigt, daß sich im Eingaberegister Daten befinden, die noch nicht von der CPU gelesen wurden und ein weiteres Laden des Eingaberegisters unterbleiben soll, bis die CPU die Daten gelesen hat. Werden die Daten durch eine den Kanal betreffende IN-Operation von der CPU gelesen (meist geschieht dies in der durch die steigende STROBE-Flanke ausgelösten Interruptroutine), so wird nach der Ende des internen $\overline{\text{RD}}^x$-Impulses folgenden fallenden Flanke des Systemtaktes die READY-Leitung in den aktiven Zustand geschaltet. Dies ist das Zeichen dafür, daß die CPU die Daten des Eingaberegisters gelesen hat und neue Daten in das Eingaberegister eingeschrieben werden sollen.
Im Falle, daß vor Beginn des Hand-shaking-Betriebes die READY-Leitung auf Low befindet (z. B. nach RESET) ist es erforderlich, mittels eines "Blind-Lesebefehls" die READY-Leitung in den aktiven Zustand zu bringen (IN-Operation der CPU mit dem betreffenden Kanal ohne Auswertung der Daten).
Soll in der Betriebsart Byte-Eingabe ohne Quittierung vom externen Gerät gearbeitet werden (externes Gerät sendet keinen STROBE-Impuls), so ist der STROBE-Eingang auf Low-Pegel zu legen (Zeitdiagramm siehe Bild 9). Damit werden die an den Kanalleitungen anliegenden Daten direkt in das Eingaberegister durchgeschaltet. Da hierbei keine steigende Flanke von STROBE auftreten kann, wird keine Interruptanmeldung ausgelöst, und auch das Rücksetzen des READY-Signals als Folge von STROBE entfällt. Um dennoch das externe Gerät darüber zu informieren, wenn ein in das Eingaberegister gegebenes Wort bereits von der CPU gelesen worden ist, wird der READY-Ausgang zeitweilig auf Low geschaltet. Dies beginnt ca. 1 1/2 Taktperioden nach der fallenden Flanke von $\overline{\text{IORQ}}$ und reicht bis nach der fallenden Flanke von CP, die der steigenden Flanke von $\overline{\text{IORQ}}$ folgt. Verändert der Anwender nur dann Daten an den Eingaberegister der PIO, wenn READY High-Pegel führt, so wird eine Datenveränderung in der Übernahmephase einer an den PIO-Kanal gerichteten Lese-Operation der CPU von vornherein ausgeschlossen und damit verbundene Fehleingabe vermieden. Die steigende Flanke von READY zeigt nach außenhin an, daß die Daten des Eingaberegisters bereits von der CPU gelesen wurden, und daß ins Eingaberegister neue Daten eingegeben werden können.

*KI generierte Interpretation der Abbildung: Bild 9 zeigt Mode 1 ohne Quittierung (STROBE fest auf Low). READY ist high, wird aber während eines CPU-Lesezugriffs ($\overline{\text{RD}}^*$) kurzzeitig für etwa zwei Taktzyklen auf low gezogen. In diesem Zeitfenster erkennt die Peripherie, dass die Daten abgeholt wurden und kann für den nächsten Zyklus neue Daten anlegen.*

**Bild 9: Zeitablauf in Mode 1 ohne Quittierung**

**6.3. Bidirektionale Byte-Ein-/Ausgabe (Mode 2)**

Diese Betriebsart kombiniert die Betriebsarten Byte-Eingabe und Byte-Ausgabe. Dabei werden alle 4 Quittungsleitungen des Schaltkreises U855 benutzt. Aus diesem Grunde ist diese Betriebsart nur mit 1 Kanal, dem Kanal A, möglich. (Kanal B muß daher in der Betriebsart 3 benutzt werden, bei der die Quittungsleitungen nicht benötigt werden.) Die Quittungsleitungen des Kanals A werden zur Steuerung des Datentransfers in Richtung zum externen Gerät (Ausgabesteuerung) verwendet. Die Quittungsleitungen des Kanals B übernehmen die Steuerung des Datentransports vom externen Gerät zur U855 (Eingabesteuerung). Im Bild 10 ist das Zeitdiagramm für diese Betriebsart dargestellt.

*KI generierte Interpretation der Abbildung: Bild 10 zeigt das Zeitdiagramm für Mode 2 (bidirektional). Der Takt CP koordiniert die Zugriffe. 
Im Ausgabe-Teil: Das interne Schreibsignal $\overline{\text{WR}}^*$ schaltet Daten auf den Kanalbus A. ARDY (Ausgabebereit) wird high. Wenn die Peripherie $\overline{\text{ASTB}}$ auf low zieht, werden die Daten auf den Bus geschaltet („Datenausgabe“). 
Im Eingabe-Teil: Wenn die Peripherie Daten anlegt und $\overline{\text{BSTB}}$ (low) aktiviert, werden diese eingelesen („Dateneingabe“). BRDY (Eingabebereit) signalisiert den Status an die Peripherie. $\overline{\text{RD}}^*$ zeigt an, wann die CPU Daten vom Bus einliest.*

**Bild 10: Zeitablauf in Mode 2**

Die zeitlichen Abläufe sind im wesentlichen mit den schon für die Betriebsarten 0 und 1 beschriebenen identisch, wenn berücksichtigt wird, daß die Quittungsleitungen vom Kanal A für die Ausgabesteuerung und die vom Kanal B für die Eingabesteuerung verwendet werden. Die hauptsächliche Abweichung besteht darin, daß Datenausgabe an den Kanalleitungen A0...A7 nur während $\overline{\text{ASTB}}$ = 0 erfolgt. Die steigende Flanke von $\overline{\text{ASTB}}$ kann zur Übernahme der Daten durch das externe Gerät benutzt werden. Die Logik des externen Gerätes hat abzusichern, daß während $\overline{\text{ASTB}}$ = 0 nicht gleichzeitig von außen her Daten auf den Kanalbus gelegt werden.
Um interruptgesteuerten bidirektionalen Datentransfer durchzuführen, ist es erforderlich, daß die Interrupt-Freigabe-Flipflops von Kanal A **und** Kanal B gesetzt sind. Da für die Eingabesteuerung die Quittungsleitungen des Kanals B verwendet werden, löst die steigende Flanke von $\overline{\text{BSTB}}$ nach einer Dateneingabe in das Eingaberegister des Kanals A eine Interruptanmeldung aus, für deren Abarbeitung der Interruptvektor des Kanals B benutzt wird. Falls der in Mode 3 arbeitende Kanal B ebenfalls eine Interruptanmeldung auslöst, kann dies zu Zweideutigkeiten führen. Es empfiehlt sich daher, das Maskierungsregister des Kanals B so zu laden, daß keine der Kanalleitungen B0...B7 zur Bildung eines Interrupts herangezogen wird. Es ist in diesem Fall sinnvoll, den Kanal B im Pollingbetrieb von der CPU zu bedienen.

**6.4. Bit-Ein-/Ausgabe (Mode 3)**

In dieser Betriebsart wird nicht mit Quittierungssignalen gearbeitet. Der READY-Ausgang führt Low-Pegel, mit Ausnahme von BRDY, wenn Kanal A im Mode 2 arbeitet. Der Inhalt des Ein-/Ausgabewahlregisters definiert, welche Kanalleitungen als Eingänge und welche als Ausgänge wirken sollen. Schreibt die CPU Daten in das Ausgaberegister des PIO-Kanals ein, so erfolgt dies nach den gleichen Zeitschema wie in Betriebsart Byte-Ausgabe. Führt die CPU eine IN-Operation (Leseoperation) aus, die an einen im Mode 3 arbeitenden Kanal gerichtet ist, so setzen sich die der CPU zugeführten Daten bitweise entsprechend der vorgewählten Funktion der Kanalleitungen aus dem Inhalt des Eingaberegisters und dem Inhalt des Ausgaberegisters zusammen. (dies gilt für die Bits, die im Ein-/Ausgabewahlregister als Eingänge definiert wurden und den Daten des Ausgaberegisters für diejenigen Bits, die im Ein-/Ausgabewahlregister als Ausgänge definiert wurden). Dabei werden diejenigen Daten ins Eingaberegister des PIO-Kanals übernommen, und damit von der CPU gelesen, die unmittelbar vor der fallenden Flanke von $\overline{\text{RD}}$ an den Kanalleitungen anliegen (siehe Bild 11).

*KI generierte Interpretation der Abbildung: Bild 11 zeigt den asynchronen Zugriff im Mode 3. Der Takt CP läuft. Der Kanalbus führt ein „1. Datenwort“. Wenn $\overline{\text{IORQ}}$ und $\overline{\text{RD}}$ gleichzeitig aktiv (low) werden, erfolgt die „Dateneingabe“. Das Datenwort wird auf den CPU-Datenbus D0-D7 gelegt. Währenddessen kann sich am Kanalbus bereits ein „2. Datenwort“ stabilisieren. INT zeigt eine Unterbrechungsanforderung an, die durch eine Pegeländerung ausgelöst wurde.*

**Bild 11: Zeitablauf in Mode 3 (dargestellt "Lesen")**

Die Auslösung einer Interruptanmeldung erfolgt dann, wenn das Interrupt-Freigabe-Flipflop gesetzt ist und diejenigen logischen Bedingungen erreicht werden, die mit D6 und D5 im 2bit-Maskierungssteuerregister abgespeichert für diejenigen Kanalleitungen definiert wurden, die auf die Interruptauslösung Einfluß nehmen sollen (durch Laden des Maskierungsregisters festgelegt). Dies ist unabhängig davon, ob die Anschlüsse als Eingänge oder als Ausgänge definiert wurden. (Die auf die Interruptauslösung einflußnehmenden Pins werden als "unmaskiert", die davon ausgeschlossenen als "maskiert" bezeichnet). Die interne Interruptanmeldung erfolgt, wenn ein **Wechsel** der Logikpegel an den ausgesuchten Kanalleitungen in die (für die Interruptauslösung) vorgegebene logische Konfiguration vonstatten geht. (Durch die Programmierung des 2bit-Maskierungssteuerregisters kann somit festgelegt werden, ob die logische Funktion eine AND-, NAND-, OR- oder NOR-Verknüpfung sein soll. Bei Bestehenbleiben der vorgewählten logischen Konfiguration wird kein neuer Interrupt ausgelöst, entscheidend ist also das **Erreichen** der Konfiguration.
Soll im gleichen Kanal erneut ein Interrupt angemeldet werden, und die von diesem Kanal ausgelöste Interruptroutine dauert noch an, so wird die Anmeldung intern abgespeichert. Voraussetzung für die Auslösung eines weiteren Interrupts ist jedoch, daß die vorgegebene logische Konfiguration nach Verlassen des Interruptbearbeitungszustands (RETI-Befehl) noch erfüllt ist. Falls die vorgegebene logische Konfiguration innerhalb eines Zeitbereiches erreicht wird, in dem das Signal $\overline{\text{M1}}$ = Low ist, so kann die interne Anmeldung eines Interrupts erst nach der steigenden Flanke von M1 erfolgen, wobei Voraussetzung ist, daß die logische Konfiguration dann noch gegeben ist.

Abschließend sei zur Betriebsart 3 noch ein kurzes Beispiel angeführt:
Zwei Leitungen werden betrachtet, die logische Operation sei das "Oder". Wenn also wenigstens eine der betrachteten Kanalleitungen von High-Potential zu den aktiven Zustand übergeht, vorausgesetzt das Interrupt-Freigabe-Flipflop ist gesetzt und es liegt höchste Priorität vor, wird ein Interrupt ausgelöst. Wird, während die eine Kanalleitung aktiv ist, auch die zweite aktiv, so wird kein weiterer Interrupt ausgelöst. Das heißt, daß der Übergang von "Nichterfüllung" in "Erfüllung" der logischen Konfiguration den Interrupt auslöst.

**7. Interruptbearbeitung**

**7.1. Ablauf der Bearbeitung**

Im System U880 wird die Interruptprioritätskette ("daisy chain") durch Reihenschaltung der peripheren Bauelemente des Systems, also PIO, CTC und SIO, aufgebaut. Dazu dienen die Anschlüsse IEI und IEO an diesen Bauelementen. Durch die körperliche Einordnung der Bauelemente in diese Reihenschaltung wird von vornherein die Prioritätsreihenfolge festgelegt. Dabei ist der Interrupt-Freigabe-Eingang IEI eines Bauelements jeweils mit dem Interrupt-Freigabe-Ausgang IEO des prioritätsmäßig unmittelbar höher liegenden Bauelements gekoppelt (der Interrupt-Freigabe-Eingang des Bauelements mit der allerhöchsten Priorität in der gesamten Kette ist dabei auf High-Potential zu legen, da dies dem Freigabezustand entspricht). Meldet ein in der Kette angeordnetes Bauelement einen Interrupt an (die von allen Bauelementen gemeinsam benutzte Leitung $\overline{\text{INT}}$ wird von dem anmeldenden Bauelement auf Low-Potential gebracht), so geht auch sein Interrupt-Freigabe-Ausgang IEO in den Low-Zustand über. Damit empfängt der Interrupt-Freigabe-Eingang des nachfolgend angeschlossenen Bauelements Low-Signal, das seinen zur Anmeldung eines eigenen Interrupts vorhandenen inneren Mechanismus blockiert. Außerdem schaltet sein eigener Interrupt-Freigabe-Ausgang seinerseits auf Low, so daß die Blockierung in der Kette weitergereicht wird. (In dieses Kettenschema sind intern auch beide Kanäle der U855 einbezogen, wobei Kanal A die höhere Priorität gegenüber Kanal B besitzt.)
Wird also von einem peripheren Bauelement ein Interrupt angemeldet und die CPU befindet sich im interruptfähigen Zustand, so gibt sie die Interrupt-Bestätigung (Interrupt-Quittierung) aus. Dies geschieht im Interrupt-Bestätigungs-Zyklus (Interrupt-Acknowledge-Cycle, INTA-Zyklus), der gekennzeichnet ist durch Auftreten aktiven $\overline{\text{IORQ}}$-Zustandes während aktivem $\overline{\text{M1}}$-Signals (siehe Bild 12). Während dieses Zyklus wird der Kanal mit der höchsten aktuellen Priorität innerhalb der Kette ermittelt und der Interrupt-Vektor dieses Kanals an die CPU gesendet. Dies wird damit erreicht, daß die Interruptzustände von peripheren Bauelementen aufgrund interner schaltungstechnischer Maßnahmen während aktiven $\overline{\text{M1}}$-Signals sich nicht verändern. Damit steht die Zeit vom Beginn des aktiven $\overline{\text{M1}}$-Signals bis zum Aktivwerden des $\overline{\text{IORQ}}$-Signals zum Einschwingen der Prioritätskette zur Verfügung. Die höchste Priorität hat dasjenige Bauelement, dessen Interrupt-Freigabe-Eingang IEI High-Pegel und dessen Interrupt-Freigabe-Ausgang IEO Low-Pegel führt (das Bauelement ist also selbst freigegeben und blockiert die nachfolgenden). Durch das Auftreten des aktiven $\overline{\text{IORQ}}$-Signals während aktiven $\overline{\text{M1}}$-Zustandes, gelangt der ermittelte Kanal mit der höchsten Priorität in den Interruptbearbeitungszustand und sendet noch während des aktiven $\overline{\text{IORQ}}$-Signals seinen Interruptvektor über den Datenbus zur CPU.

*KI generierte Interpretation der Abbildung: Bild 12 zeigt den Zeitablauf des Interrupt-Bestätigungs-Zyklus (INTA). Der M1-Zyklus beginnt mit der fallenden Flanke von M1 bei T1. In der Mitte von T2 wird IORQ aktiv (low). Das Diagramm hebt hervor, dass M1 und IORQ gleichzeitig aktiv sein müssen („IORQ . M1 zeigen interruptbestätigung an (INTA)“). Zwischen T2 und T3 sind zwei Wait-Zyklen (Tw*) eingefügt. Während dieser Zeit stabilisieren sich die Daisy-Chain-Signale IEI und IEO. Erst nach dieser Stabilisierung wird am Ende des Zyklus der Interrupt-Vektor von der PIO auf den Datenbus gelegt.*

**Bild 12: Interrupt-Bestätigungs-Zyklus**

Die $\overline{\text{INT}}$-Leitung wird wieder freigegeben. Die CPU bildet aus dem Interruptvektor des Kanals den niederwertigen, aus dem Inhalt des eigenen I-Registers den höherwertigen Teil eines 16bit-breiten Zeigers. Auf dem Speicherplatz, dessen Adresse dem Wert des Zeigers entspricht, steht wiederum der niederwertige, auf dem nachfolgenden Speicherplatz der höherwertige Teil der ersten Adresse der Interrupt-Service-Routine. Diese beiden Bytes liest die CPU, bildet hieraus die Startadresse der Interrupt-Service-Routine (ISR) und beginnt mit der darin vorgesehenen Programmabarbeitung. Durch die Bestätigung eines Interrupts wird die CPU automatisch interruptunfähig, d. h. sie bestätigt keine weiteren Interrupt-Anmeldungen. Erst durch den Befehl EI (enable interrupt) wird sie wieder in die Lage versetzt, Interruptbestätigungen auszuführen. Der Interrupt-Freigabe-Ausgang IEO der U855 führt beginnend von seiner Interrupt-Anmeldung und andauernd über den Zeitraum der Abarbeitung der Interrupt-Service-Routine bis zum RETI-Befehl innerhalb seiner ISR Low-Signal. (Eine Ausnahme bildet der unten beschriebene Fall der nicht bestätigten Anmeldung eines gegenüber dem in Bearbeitung befindlichen Kanals höherpriorisierten Schaltkreises). Damit werden Interrupt-Anmeldungen niedriger priorisierter peripherer Bauelemente von vornherein unterbunden. Meldet jedoch ein höherpriorisiertes Bauelement (bzw. Kanal) während der Abarbeitung der ISR eines niederpriorisierten Kanals einen Interrupt an, so wird, falls die CPU bereits mittels eines EI-Befehl interruptfähig gemacht worden war, die ISR des niederpriorisierten Kanals unterbrochen und die ISR des höherpriorisierten Kanals eingeschoben. Nach Abschluß dieser ISR wird dann die Bearbeitung der ISR des niederpriorisierten Kanals an der Stelle wieder aufgenommen, an der sie unterbrochen worden war. (Auf diese Weise können je nach Programm mehrere ISR ineinander geschachtelt sein. Siehe Bild 13). Im Falle, daß die Interruptanmeldung eines höherpriorisierten Kanals während der ISR-Bearbeitung eines niedriger priorisierten Kanals erfolgt, und die CPU ist noch nicht interruptfähig, kann diese nicht mit einem INTA-Zyklus reagieren. Dies bedeutet, daß der Interrupt-Freigabe-Eingang IEI des in ISR-Bearbeitung befindlichen Kanals das Low-Signal des Interrupt-Freigabe-Ausgangs IEO des noch nicht bestätigten höherpriorisierten Kanals (gegebenenfalls über mehrere dazwischenliegende Bauelemente) empfängt.

*KI generierte Interpretation der Abbildung: Bild 13 zeigt fünf Zustände der Prioritätskette mit vier beispielhaften Elementen (Kanal 1A, Kanal 1B, Kanal 2A, Kanal 2B). 
1) Zeigt die Hierarchie im Ruhezustand (alle IEI/IEO sind high). 
2) Kanal 2A meldet Interrupt an. Sein IEO geht auf low (LO), wodurch alle nachfolgenden Elemente (Kanal 2B) gesperrt werden. 
3) Während Kanal 2A bedient wird („Bedienung unterbrochen“), meldet Kanal 1B (höhere Priorität) einen Interrupt an. Sein IEO geht auf low, was auch Kanal 2A sperrt. 
4) Nachdem Kanal 1B seine Routine mit RETI beendet hat, wird sein IEO wieder high, und die Bedienung von Kanal 2A wird fortgesetzt. 
5) Kanal 2A beendet ebenfalls mit RETI. Alle Signale sind wieder high.*

**Bild 13: Interruptbedienung in der Prioritätskette**

Zum Abschluß einer ISR ist für den U855D die Dekodierung eines an ihn gerichteten RETI-Befehls erforderlich. Der RETI-Befehl ist die "Return from Interrupt"-Instruktion (Rückkehr vom Interrupt). Er besteht aus 2 Teilen, EDH und 4DH, die beide Opcode-Charakter haben. Die während der zwei Opcode-Leseoperationen der CPU auf dem Datenbus anliegenden hexadezimalen Werte "ED" und später "4D" werden vom peripheren Bauelement "mitgelesen" und dekodiert. Für die CPU hat dieser Befehl die Bedeutung eines "Return"-Befehls. Für einen in Bearbeitung seiner ISR befindlichen Kanal bedeutet er den Abschluß der ISR. Es ist jeweils der Kanal angesprochen, dessen Eingang IEI, bei Dekodierung des zum RETI-Befehl gehörenden "4D", High-Pegel und dessen Ausgang IEO Low-Pegel führt.

Um eine ordnungsgemäße RETI-Dekodierung zu gewährleisten, ist also am IEI-Eingang des betreffenden Kanals High-Signal zu gewährleisten. Dies wird dadurch ermöglicht, daß der IEO-Ausgang eines Kanals, der zwar eine Anmeldung abgibt, sie jedoch noch nicht bestätigt bekommen hat, nach der Erkennung eines "OP-Code ED" (EDH = erstes RETI-Byte) für einen $\overline{\text{M1}}$-Zyklus nach High geht, vorausgesetzt, sein IEI-Eingang führt High-Pegel. Damit ist IEI = High für die RETI-Dekodierung des Kanals gegeben, dessen ISR-Bearbeitung abgeschlossen werden soll (siehe Bild 14).

*KI generierte Interpretation der Abbildung: Bild 14 zeigt die Signalverläufe während des zweistufigen RETI-Zyklus. Zuerst erscheint das Byte „ED“ auf dem Datenbus D0-D7 während M1 und RD aktiv sind. Kurze Zeit später folgt das Byte „4D“. Das Diagramm zeigt, wie das IEI-Signal vor der Dekodierung des zweiten Teils stabilisiert wird, damit der korrekte Kanal das Signal erkennt und seinen IEO-Status von low auf high umschaltet.*

**Bild 14: RETI-Zyklus**

**7.2. Zeitprobleme**

Die bisherigen grundsätzlichen Darlegungen wurden annähernd ohne Beachtung der internen Verzögerungszeiten gemacht. Betrachtet man jedoch Schaltungsanordnungen mit mehreren peripheren Bauelementen, so ist unter Berücksichtigung der verwendeten Betriebsfrequenz und der Geschwindigkeitskategorie der verwendeten Bauelemente gegebenenfalls Zusatzlogik erforderlich.
Für den Fall der Feststellung desjenigen peripheren Bauelements, das die höchste Priorität in der Kette hat, ist die Geschwindigkeit des Durchschaltens des H/L-Übergangs durch die Kette von Bedeutung. Der zeitlich kritische Fall tritt auf, wenn ein in der Kette hinten liegendes peripheres Bauelement eine Interruptanmeldung abgibt ("Low"-Schalten des $\overline{\text{INT}}$-Ausgangs und des IEO-Ausgangs) und von der CPU ein INTA-Zyklus eingeleitet wird, jedoch kurz vor dem $\overline{\text{M1}}$-Signal ein in der Kette vorn liegendes peripheres Bauelement ebenfalls einen Interrupt anmeldet. Dieses schaltet ebenfalls seine Ausgänge $\overline{\text{INT}}$ und IEO nach "Low". Im INTA-Zyklus ist dasjenige periphere Bauelement angesprochen, das eine Interruptanmeldung abgibt und dessen Eingang IEI auf "High" liegt. Damit also nicht beide im angeführten Beispiel einen Interrupt fordernden Bauelemente angesprochen werden, muß noch vor Erscheinen des $\overline{\text{IORQ}}$-Signals das "Low" von IEO des in der Kette vorn liegenden Bauelements am IEI-Eingang des in der Kette hinten liegenden Bauelements eintreffen. Aufgrund der beim "Weiterreichen" der "Low"-Information durch die Bauelemente eintretenden Verzögerungen, sind z. B. bei einer Taktfrequenz von 2,5 MHz und unmittelbarer Aufreihung vom U855, Kategorie B, nur vier derartige Bauelemente in der Prioritätskette einsetzbar. Ist eine größere Zahl von Bauelementen erforderlich, so machen sich Schaltungen zum schnellen Durchschalten der "Low"-Information erforderlich. Eine Möglichkeit ist im Bild 15 dargestellt.

*KI generierte Interpretation der Abbildung: Bild 15 zeigt eine Beschleunigungsschaltung für die Daisy-Chain. Mehrere PIO-Blöcke sind nebeneinander angeordnet. Die IEO-Ausgänge werden nicht nur direkt zum nächsten IEI geführt, sondern zusätzlich über externe UND-Gatter verknüpft. Jedes UND-Gatter empfängt die IEO-Signale mehrerer vorangegangener Bausteine. Dadurch muss das Signal nicht seriell durch alle internen Logiken wandern, sondern kann über die schnellere externe TTL-Logik „abgekürzt“ werden, um nachfolgende Bausteine schneller zu sperren.*

**Bild 15: Mögliche Schaltung zum schnellen Durchschalten des H/L-Übergangs durch die Prioritätskette**

Die "Low"-Information eines IEO-Ausgangs eines peripheren Bauelements wird entweder über eine beschränkte Anzahl von peripheren Bauelementen und dann über TTL, oder direkt über TTL an die IEI-Eingänge der in der Kette hinten liegenden Bauelemente geschaltet. Eine ähnliche Problematik ergibt sich im Falle des L/H-Schaltens des Ausganges IEO eines PIO-Bauelements mit nicht bestätigtem Interrupt während der RETI-Abarbeitung, wie sie im Abschnitt 7.1. erläutert und im Bild 14 dargestellt ist. Eine einfache, jedoch möglicherweise zeitaufwendigere Lösung, ist das Einfügen von WAIT-Zuständen, wenn mittels Dekoder ein "OP-Code ED" erkannt worden ist. Eine andere, keine zusätzliche Zeit erfordernde Lösung ist es, mittels einer Dekoderschaltung, die beim Erkennen des "OP-Code ED 1. Byte" aktiv wird, die IEI-Eingänge aller peripheren Bauelemente auf "High"-Potential bringt (Siehe Bild 16).

*KI generierte Interpretation der Abbildung: Bild 16 illustriert eine Schaltung, die einen „Dekoder“ am Datenbus der CPU einsetzt. Dieser Dekoder erkennt das erste Byte des RETI-Befehls („ED“). Der Ausgang des Dekoders steuert eine Reihe von Logikgattern über den PIO-Bausteinen an. Diese Logik erzwingt bei Erkennung von „ED“, dass alle IEI-Eingänge der Kette gleichzeitig auf High-Potential gelegt werden. Dies verkürzt die Einschwingzeit für die L/H-Flanke massiv und erlaubt den Einsatz von wesentlich längeren Prioritätsketten auch bei hohen Taktraten.*

**Bild 16: Mögliche Schaltung zum schnellen Durchschalten des H/L-Übergangs und des L/H-Übergangs bei RETI durch die Prioritätskette**

Zur Feststellung, welches periphere Bauelement innerhalb der Kette für die RETI-Anweisung bestimmt ist, ist es erforderlich, die zwangsweise "High"-Ansteuerung der IEI-Eingänge so rechtzeitig abzubrechen, daß die ohnehin notwendige H/L-Beschleunigungsschaltung die IEI-Eingänge hinter dem RETI-bedienten Bauelement rechtzeitig noch vor der "4D"-Dekodierung mit "Low" belegt. Damit können die Voraussetzungen zur ordnungsgemäßen RETI-Dekodierung auch in langen Prioritätsketten gesichert werden. Werden die PIO-Bauelemente in Systemen mit niedriger Taktfrequenz eingesetzt, so daß sich damit von vornherein die Zeitprobleme entschärfen, ist jeweils zu prüfen, ob die genannten Umgehungslösungen überhaupt erforderlich sind.

**8. Anwendungsbeispiele**

**8.1. Ein-/Ausgabe-Interface**

In diesem Beispiel steht der PIO-Schaltkreis U855 über einen 8bit breiten bidirektionalen Bus mit einem Ein-/Ausgabe-Terminal in Verbindung (siehe Bild 17).

*KI generierte Interpretation der Abbildung: Bild 17 zeigt das Blockschaltbild eines Interfaces. Eine „U 880 CPU“ ist über Adreß-, Daten- und Steuerbus mit einem „Adreß-Dekoder“ und dem „U 855 PIO“ verbunden. Der Adreß-Dekoder selektiert den PIO über das $\overline{\text{CS}}$-Signal. Der PIO wiederum kommuniziert über den „Kanal-Daten-Bus“ mit dem „I/O Terminal“. Die Quittungssignale sind wie folgt verschaltet: ARDY (PIO Ausgang) steuert den ASTB (Terminal Eingang). Das Terminal antwortet über ARDY zurück zum ASTB-Eingang des PIO. Analog dazu ist die Verschaltung für den B-Kanal (BRDY/BSTB) gezeichnet, wobei hier Puffer bzw. Inverter in die Leitungen eingefügt sind.*

**Bild 17: Beispiel für ein Ein-/Ausgabe-Interface**

Die Betriebsart 2 (bidirektionale Byte-Ein-/Ausgabe) wird durch das Aussenden des folgenden Steuerwortes an den Kanal A gewählt:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 1 | 0 | X | X | 1 | 1 | 1 | 1 |

Das Laden des Interruptvektors geschieht wie bereits beschrieben mittels eines Steuerwortes folgenden Formats:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| V7 | V6 | V5 | V4 | V3 | V2 | V1 | 0 |

Interrupts werden dann durch die steigende Flanke des ersten $\overline{\text{M1}}$-Signals freigegeben, nachdem das Interrupt-Steuerwort eingeschrieben wurde, es sei denn, daß dieses $\overline{\text{M1}}$-Signal einem Interrupt-Bestätigungszyklus zugeordnet ist. (Wenn einem Interrupt-Steuerwort eine Maske folgt, dann sind Interrupts nach der steigenden Flanke des ersten $\overline{\text{M1}}$-Signals, das dem Setzen der Maske folgt, möglich). Die Daten können nun zwischen dem externen Gerät und dem PIO-Schaltkreis transferiert werden.

**8.2. Steuer-Interface**

Ein typischer Einsatzfall des U880-Systems mit in Betriebsart 3 betriebenem PIO-Schaltkreis U855 ist in Bild 18 dargestellt.

*KI generierte Interpretation der Abbildung: Bild 18 zeigt ein praktisches Anwendungsbeispiel zur Prozesssteuerung. Die CPU ist mit dem PIO verbunden. Der „Kanal A“ des PIO ist über acht Leitungen (A0-A7) mit einem „Industrieller Prozeß“-Block verbunden. Den Leitungen sind spezifische Funktionen zugewiesen: A7: Spezialtest (Ausgang), A6: Netz ein (Ausgang), A5: Netzausfall (Eingang), A4: HALT (Ausgang), A3: Temp. Alarm (Eingang), A2: Heizung ein (Ausgang), A1: Druck Syst. (Ausgang), A0: Druck Alarm (Eingang). Eingänge werden durch Kreise an den Leitungen markiert, Ausgänge durch Pfeilspitzen. Die Signale B/A SEL und C/D SEL adressieren den Port.*

**Bild 18: Beispiel für ein Steuer-Interface**

Es wird angenommen, daß ein industrieller Prozeß überwacht werden soll. Das mögliche Auftreten einer abnormalen Funktionsbedingung soll mit dem System kontrolliert werden und gegebenenfalls entsprechende Maßnahmen zur Gegensteuerung eingeleitet werden.
Den einzelnen Bits des vom Kanal A der U855 zur externen Anlage führenden Busses sind folgende Funktionen zugeordnet:

| Pin | Funktion | Richtung |
| :--- | :--- | :--- |
| A7 | Spezieller Test | Ausgang |
| A6 | Netz einschalten | Ausgang |
| A5 | Netzausfall | Eingang |
| A4 | Halt | Ausgang |
| A3 | Temperatur Alarm | Eingang |
| A2 | Heizung einschalten | Ausgang |
| A1 | Druck System | Ausgang |
| A0 | Druck Alarm | Eingang |

Die PIO kann wie folgt benutzt werden. Zuerst muß abgesichert werden, daß der benutzende Kanal (in unserem Falle Kanal A) interruptunfähig gemacht wird, da ein Wechsel in den Mode 3 bzw. ein Bedingungswechsel innerhalb des Modes 3 vollzogen werden soll. Es ist daher sinnvoll, den gesamten Vorgang mit der Sperrung des Interrupt-Freigabe-Flipflops des Kanals einzuleiten (falls von vornherein sichergestellt ist, daß das Interrupt-Freigabe-Flipflop sich im rückgesetzten Zustand befindet, ist dies natürlich nicht erforderlich).
Das entsprechende Steuerwort hat folgendes Format:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 0 | X | X | X | 0 | 0 | 1 | 1 |

Nun wird mit dem folgenden Steuerwort die Betriebsart 3 für den Kanal A festgelegt:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 1 | 1 | X | X | 1 | 1 | 1 | 1 |

Das nächste zum Kanal A gesendete Steuerwort muß nun das Steuerwort zur Festlegung der Eingänge bzw. Ausgänge sein. In unserem Beispiel sollen die Kanalleitungen A0, A3 und A5 Eingänge sein, alle anderen Kanalleitungen werden als Ausgänge definiert:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 0 | 0 | 1 | 0 | 1 | 0 | 0 | 1 |

Anschließend wird der Interrupt-Vektor geladen:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| V7 | V6 | V5 | V4 | V3 | V2 | V1 | V0 |

Danach wird das Interrupt-Steuerwort geladen:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :--- | :--- | :--- | :--- | :---: | :---: | :---: | :---: |
| 1 | 0 | 1 | 1 | 0 | 1 | 1 | 1 |
| Interrupts freigegeben | Log. ODER | Aktiv "High" | Maske folgt | | | | |

Das nachfolgende Steuerwort wird als Maske interpretiert. In unserem Beispiel sollen A0, A3 und A5 auf eine Interruptauslösung Einfluß haben. Daher hat das Steuerwort folgendes Format:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 1 | 1 | 0 | 1 | 0 | 1 | 1 | 0 |

Wenn an mindestens einer der Leitungen A0, A3 oder A5 High-Pegel auftritt, wird eine Interrupt-Anforderung ausgelöst.
Mit dem Maskierungswort kann der Anwender eine beliebige Kombination von Ein- oder Ausgängen festlegen, die auf eine Interruptanforderung Einfluß nehmen sollen. Wenn das Maskierungswort im obigen Beispiel die Form

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 0 | 1 | 0 | 1 | 0 | 1 | 1 | 0 |

gehabt hätte, dann wäre auch eine Interruptanforderung ausgelöst worden, wenn das Bit A7 (Spezialtest) des Ausgaberegisters gesetzt worden wäre.

In den meisten Anwendungsfällen werden die Adressen A0 und A1 für die Belegung der Eingänge B/$\overline{\text{A}}$ SEL und C/$\overline{\text{D}}$ SEL verwendet, aus den Adressen A2...A7 wird die Belegung für den Eingang $\overline{\text{CS}}$ dekodiert.
Wenn nur wenige periphere Bauelemente eingesetzt werden, so ist eine derartige Dekodierung des Signals für $\overline{\text{CE}}$ nicht erforderlich, da die Adreßbits mit höherer Wertigkeit direkt verwendet werden können.

**9. Zusammenfassung der Programmierung**

**Laden des Interruptvektors**

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| V7 | V6 | V5 | V4 | V3 | V2 | V1 | 0 |

**Auswahl der Betriebsart**

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| M1 | M0 | X | X | 1 | 1 | 1 | 1 |

| Nr. der Mode | M1 | M0 | Mode |
| :---: | :---: | :---: | :--- |
| 0 | 0 | 0 | Byte-Ausgabe |
| 1 | 0 | 1 | Byte-Eingabe |
| 2 | 1 | 0 | Byte-Ein/Ausgabe |
| 3 | 1 | 1 | Bit-Ein/Ausgabe |

Wenn Mode 3 gewählt wurde, dann muß das folgende an den betreffenden Kanal gerichtete Steuerwort das Steuerwort zur Festlegung der Eingänge bzw. Ausgänge sein:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| I/$O_7$ | I/$O_6$ | I/$O_5$ | I/$O_4$ | I/$O_3$ | I/$O_2$ | I/$O_1$ | I/$O_0$ |

I/O = 1 $\rightarrow$ Kanalanschluß wird als Eingang definiert
I/O = 0 $\rightarrow$ Kanalanschluß wird als Ausgang definiert

**Setzen des Interrupt-Steuerwortes für Mode 3**

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :--- | :--- | :--- | :--- | :---: | :---: | :---: | :---: |
| Interrupt-Freigabe $\hat{=}$ 1 | UND $\hat{=}$ 1 ODER $\hat{=}$ 0 | High $\hat{=}$ 1 Low $\hat{=}$ 0 | Maske folgt $\hat{=}$ 1 | 0 | 1 | 1 | 1 |

Wenn das Bit "Maske folgt" "high" ist, wird das nächste an den betreffenden Kanal gerichtete Steuerwort als Maske interpretiert:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| MB7 | MB6 | MB5 | MB4 | MB3 | MB2 | MB1 | MB0 |

MB = 0 $\rightarrow$ Kanalanschluß wird zur Interrupterzeugung herangezogen
MB = 1 $\rightarrow$ Kanalanschluß wird **nicht** zur Interrupterzeugung herangezogen (Anschluß ist "maskiert")

**Allgemeines Interruptsteuerwort**
Mit diesem Steuerwort kann unabhängig von der verwendeten Betriebsart ausschließlich das Interrupt-Freigabe-Flipflop des betreffenden Kanals beeinflußt werden:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Interrupt Freigabe | X | X | X | 0 | 0 | 1 | 1 |

Die Freigabe des Interrupt-Freigabe-Flipflops erfolgt mit D7 = 1.

**10. Technische Daten**

Gesetzliche Grundlage für die technischen Daten des Bauelementes U855 bildet der Fachbereichsstandard TGL 35 837.

**10.1. Zuverlässigkeit**

Bei mittlerer elektrischer Belastung (vgl. TGL 35 837), normaler klimatischer Beanspruchung (vgl. TGL 24 951) und vernachlässigbarer mechanischer Belastung, kann eine Betriebsausfallrate (zu erwartende Betriebsausfallrate)

$\lambda_{BE} \leq 1 \cdot 10^{-6} h^{-1}$

bezogen auf die durch den Schaltkreis verursachten Funktionsausfälle von Geräten und Anlagen erwartet werden.
Dieser Kennwert der Kenngröße $\lambda_{BE}$ gilt für die totale Bauelementegesamtheit des Schaltkreises, wobei eine expotentielle Lebensdauerverteilung des Schaltkreises Voraussetzung sein muß.

**10.2. Elektrische Kennwerte**

Alle Spannungen sind bezogen auf $U_{SS} = 0\ V$

**Grenzwerte:**

| Kenngröße | Kurzzeichen | Einheit | Kleinstwert | Größtwert | Bemerkung |
| :--- | :---: | :---: | :---: | :---: | :--- |
| Betriebsspannung | $U_{CC}$ | V | -0,5 | 7 | $\vartheta_a = 0 \dots 70 ^\circ C$ |
| Eingangsspannung | $U_I$ | V | -0,5 | 7 | $\vartheta_a = 0 \dots 70 ^\circ C$ |
| Betriebsumgebungstemperatur | $\vartheta_a$ | $^\circ C$ | 0 | 70 | |
| Lagerungstemperatur | $\vartheta_{stg}$ | $^\circ C$ | -55 | 125 | |
| Verlustleistung ($\vartheta_a = 25 ^\circ C$) | $P_V$ | W | | 1,1 | bei $\vartheta_a = 25 ^\circ C$ |

**Statische Betriebsbedingungen (bei $\vartheta_a = 0 \dots 70 ^\circ C$)**

| Kenngröße | Kurzzeichen | Einheit | Kleinstwert | Größtwert |
| :--- | :---: | :---: | :---: | :---: |
| Betriebsspannung | $U_{CC}$ | V | 4,75 | 5,25 |
| Eingangsspannung Low | $U_{IL}$ | V | -0,5 | 0,8 |
| Eingangsspannung High | $U_{IH}$ | V | 2 | $U_{CC}$ |
| Takteingangsspannung Low | $U_{ILC}$ | V | -0,5 | 0,45 |
| Takteingangsspannung High | $U_{IHC}$ | V | $U_{CC}-0,2$ | $U_{CC}$ |

**Statische Kennwerte (bei $\vartheta_a = 0 \dots 70 ^\circ C$)**

| Kenngröße | Kurzzeichen | Einheit | Kleinstwert | Größtwert | Bemerkung |
| :--- | :---: | :---: | :---: | :---: | :--- |
| Ausgangsspannung Low | $U_{OL}$ | V | | 0,4 | $I_O = 1,8\ mA$ |
| Ausgangsspannung High | $U_{OH}$ | V | 2,4 | | $I_O = -0,25\ mA$ |
| Eingangsreststrom | $I_{LI}$ | $\mu A$ | | 10 | $U_I = 0\ und\ 5,25\ V$ |
| Reststrom der Drei-Zustands-Ein- und Ausgänge und des Eintransistor-Ausgangs (INT) im hochohmigen Zustand | $I_{LO}$, $I_{LINT}$ | $\mu A$ | | 10 | $U_I = 0\ und\ 5,25\ V$ |
| Reststrom des Datenbusses bei Eingabe | $I_{LD}$ | | | | |
| Stromaufnahme | $I_{CC}$ | mA | | 100 | |
| Laststrom der Darlington-Treiber-Ausgänge (nur Kanal B) | $I_{OHD}$ | mA | 1,5 | 3,8 | $U_{OH} = 1,5\ V, R_{EXT} = 390 \Omega$ |
| Taktkapazität | $C_{CP}$ | pF | | 14 | |
| Eingangskapazität | $C_I$ | pF | | 7 | $\vartheta_a = 25 ^\circ C, f = 0,5 \dots 2\ MHz$ |
| Ausgangskapazität | $C_O$ | pF | | 10 | |

**Dynamische Kennwerte (bei $\vartheta_a = 0 \dots 70 ^\circ C$)**

| Kenngröße | Kurzzeichen | Einheit | Kleinstwert | Größtwert |
| :--- | :---: | :---: | :---: | :---: |
| Taktperiode | $t_c$ | | 400 | +) |
| High-Breite des Taktes | $t_{w(CH)}$ | | 180 | 2000 |
| Low-Breite des Taktes | $t_{w(CL)}$ | | 180 | 2000 |
| Anstiegs- und Abfallzeit des Taktes | $t_r, t_f$ | | | 30 |
| D0 bis D7; Datensetzzeit zu L/H des Taktes während eines Schreib- oder M1-Zyklus | $t_{sc}(D)$ | | 50 | |
| A0...A7, B0...B7; Kanaldatensetzzeit zu L/H von $\overline{\text{STROBE}}$ (Mode 1) | $t_s(PD)$ | ns | 200 | |
| $\overline{\text{CS}}$, C/$\overline{\text{D}}$; B/$\overline{\text{A}}$ Setzzeit der Steuersignale zu L/H des Taktes während eines Lese- oder Schreibzyklus | $t_{sc}(CS)$ | | 280 | |
| IEI-Setzzeit zu H/L von $\overline{\text{IORQ}}$ während eines INTA-Zyklus | $t_s(IEI)$ | | 140 | |
| $\overline{\text{RD}}$-Setzzeit zu L/H von CP während eines Lese- oder M1-Zyklus | $t_{sc}(RD)$ | | 240 | |
| $\overline{\text{IORQ}}$-Setzzeit zu L/H von CP während eines Lese- oder Schreibzyklus | $t_{sc}(IR)$ | | 250 | |
| $\overline{\text{M1}}$-Setzzeit zu L/H von CP während eines INTA oder M1-Zyklus | $t_{sc}(M1)$ | | 210 | |
| alle Haltezeiten für spezifizierte Setzzeiten | $t_H$ | | 0 | |
| $\overline{\text{ASTB}}, \overline{\text{BSTB}}$: Impulsbreite, $\overline{\text{STROBE}}$ | $t_w(ST)$ | | 150 | |
| Impulsbreite, $\overline{\text{STROBE}}$ Mode 2 | $t_w(ST)$ | | $> t_s(PD)$ | |
| Dauer von $\overline{\text{M1}}$ für einen RESET-Impuls | $t_{\overline{M1}}(RESET)$ | | $2\ t_c$ | |

+) $t_c = t_{w(CH)} + t_{w(CL)} + t_r + t_f$

**Verzögerungszeiten**
(bei: $U_{CC} = 4,75\ V; U_{IL} = 0,8\ V; U_{IH} = 2\ V; U_{ILC} = 0,45\ V; U_{IHC} = 4,55\ V; C_L = 100\ pF\ und\ \vartheta_a = 70 ^\circ C$)

| Kenngröße | Kurzzeichen | Einheit | Kleinstwert | Größtwert |
| :--- | :---: | :---: | :---: | :---: |
| Datenausgabe im Lesezyklus zur H/L-Flanke von $\overline{\text{RD}}$ | $t_{DR}(D)$ | | | 440 |
| Datenausgabe während eines INTA-Zyklus zur H/L-Flanke von $\overline{\text{IORQ}}$ | $t_{DI}(D)$ | | | 350 |
| Floaten des Datenbusses | $t_F(D)$ | | | 170 |
| IEO, L/H-Flanke von IEI | $t_{DH}(IO)$ | | | 220 |
| IEO, H/L-Flanke von IEI | $t_{DL}(IO)$ | | | 200 |
| IEO, H/L-Flanke von $\overline{\text{M1}}$ | $t_{DM}(IO)$ | ns | | 310 |
| Kanaldatenausgang zur H/L-Flanke von $\overline{\text{STROBE}}$ in Mode 2 | $t_{DS}(PD)$ | | | 240 |
| Floaten des Kanals zur L/H-Flanke von $\overline{\text{STROBE}}$ in Mode 2 | $t_F(PD)$ | | | 210 |
| Kanaldatenstandzeit bezogen auf L/H-Flanke von $\overline{\text{IORQ}}$ während des Schreibzyklus (Mode 0) | $t_{DI}(PD)$ | | | 210 |
| $\overline{\text{INT}}$ zur L/H-Flanke von $\overline{\text{STROBE}}$ | $t_D(IT)$ | | | 500 |
| $\overline{\text{INT}}$ bei Datenübertragung in Mode 3 | $t_D(IT3)$ | | | 660 |
| READY zur L/H-Flanke von $\overline{\text{STROBE}}$ | $t_{DL}(RY)$ | | | $t_c + 410$ |
| READY zur L/H-Flanke von $\overline{\text{IORQ}}$ | $t_{DH}(RY)$ | | | $t_c + 470$ |

*KI generierte Interpretation der Abbildung: Bild 19 ist eine umfassende grafische Darstellung der Zeitabläufe (Timing Diagram) für den U855. Über acht Taktzyklen (CP) hinweg werden die Signalverhältnisse für verschiedene Operationen gezeigt. In den horizontalen Zeilen werden die Signale CS, RD, der Datenbus D0-D7, IORQ, M1, IEI, IEO sowie die kanalspezifischen Signale (Port A/B Busse, READY-Signale, STROBE-Signale und INT) abgebildet. Zahlreiche Bemaßungspfeile zwischen den Flanken der Signale referenzieren die in der Tabelle auf Seite 37 spezifizierten Parameter, wie z.B. $t_{SC}(CS)$, $t_{DR}(D)$, $t_{DM}(IO)$ oder $t_{D}(IT)$. Das Diagramm verdeutlicht z.B. die zeitliche Verschiebung zwischen dem Abfall von IEI und dem verzögerten Abfall von IEO bei einer Interruptanmeldung.*

**Bild 19: Zeitverhalten der U855**

**10.3. Gehäuse**

Der Schaltkreis wird in einem 40poligen DIL-Plastgehäuse nach TGL 26713 geliefert.

Bauform 21.2.3.2.40 nach TGL 26 713
Masse ca. 5,4 g

*KI generierte Interpretation der Abbildung: Bild 20 zeigt drei Ansichten des Gehäuses. Die Draufsicht (Mitte links) stellt den 52 mm langen Baustein mit den 40 Pins und der Markierung für Pin 1 dar. Die Detailansicht einer Pinreihe (unten links) zeigt den standardmäßigen Abstand von 2,54 mm zwischen den Pins. Die Stirnansicht (oben rechts) zeigt die Breite des Bausteins von 15 mm und den Neigungswinkel der Pins von bis zu 15 Grad.*

**Bild 20: Gehäuseabmessungen der U855D**

Rs 01/84-05 V 71 329 Ko

**elektronik**
**export-import**
Volkseigener Außenhandelsbetrieb der
Deutschen Demokratischen Republik
DDR - 1026 Berlin, Alexanderplatz 6
Telex BLN 114721 elei, Telefon: 2180

**veb mikroelektronik 'karl marx' erfurt**
**im veb kombinat mikroelektronik**
DDR - 5010 Erfurt, Rudolfstraße 47
Telefon: 5 80, Telex: 061 306

Rs 01-84-02 W-V-2-1
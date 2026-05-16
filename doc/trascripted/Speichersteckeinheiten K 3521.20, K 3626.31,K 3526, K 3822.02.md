# robotron

Speichersteckeinheiten

K 3521.20, K 3626.31,
K 3526, K 3822.02

Betriebsdokumentation

5. Auflage
Karl-Marx-Stadt 1988

© VEB Kombinat Robotron

Inhaltsverzeichnis

7. Ergänzungen zur Betriebsdokumentation Operativspeicher OPS K 3525
8. Operativspeicher OPS K 3521.20
8.1. Kurzcharakteristik
8.2. Technische Daten
8.3. Einsatzbedingungen für den Stütz-Akkumulator
8.4. Programmierung der STE
8.5. Auswahl des Speichersperrsignals MEMDI
8.6. Funktionsbeschreibung
8.6.1. Verwendungszweck
8.6.2. Funktion
9. Operativspeicher OPS K 3626.31
9.1. Kurzcharakteristik
9.2. Technische Daten
9.3. Programmierung der STE
9.4. Funktionsbeschreibung
9.4.1. Verwendungszweck
9.4.2. Funktion
9.4.3. Refresh-Zyklus
10. Programmierbarer Festwertspeicher PFS K 3822.02
10.1. Kurzcharakteristik
10.2. Technische Daten
10.3. Programmierung der STE
10.3.1. Programmierfelder der STE
10.3.2. Adressenzuordnung
10.3.3. Belegung der EPROM-Schaltkreise auf der STE
10.3.4. Auswahl des Speichersperrsignals MEMDI
10.3.5. Bildung von WAIT
10.3.6. Abgerüstete Variante mit 8 KByte Speicherkapazität
10.3.7. Programmierung der Speicherschaltkreise
10.3.8. Löschen der Speicherschaltkreise
10.4. Funktionsbeschreibung
10.4.1. Verwendungszweck
10.4.2. Funktion
10.4.3. Adreßdecodierung durch 4 Bit-Volladder PS83
10.4.4. Programmierung der STE
11. Operativspeicher OPS K 3526
11.1. Kurzcharakteristik
11.2. Technische Daten
11.3. Programmierung der STE
11.4. Funktionsbeschreibung
11.4.1. Verwendungszweck
11.4.2. Funktion
11.4.3. Speicherzugriff durch die ZVE
11.4.4. RFSH-Steuerung durch die ZVE
11.4.5. Interne RFSH-Steuerung bei Stromausfall
11.4.6. Varianten

12. Ergänzungen zur Betriebsdokumentation Operativspeicher OPS K 3525

16 K dyn. RAM - Typ 012-7012 (1.12.517021.0 - 083-4-710-018)

Bestellindex 3 oder Kennzeichnung auf STE L 4 (Leiterbildseite)
B 5 (Bestückungsseite)

Um eine uneingeschränkte Zusammenarbeit mit der ZRE K 2526 zu gewährleisten, wurde die Speichersteckeinheit K 3525.0 konstruktiv geändert.
Mit dieser Änderung ist die K 3525 auch im Adreßbereich $0000_H$ - $3FFF_H$ funktionstüchtig (geänderte RFSH-Steuerung).

Im folgenden werden nur die Änderungen beschrieben. Ansonsten gilt die Funktionsbeschreibung der K 3525 (Betriebsdokumentation, Punkt 5.).

Beachte! Geänderte Zuordnung der Bauelemente.

- Das Schreibsignal $\overline{\text{WR}}$ wird am Nand A4/06 mit dem Signal $\overline{\text{MEMDI}}$ verknüpft und bildet das Signal $\overline{\text{WRA}}$, das als Steuersignal $\overline{\text{WE}}$ am Speicherchip liegt.
- Die Auswahl der 4 Speicherblöcke beim Schreib-/Leseaufruf erfolgt über einen 1 aus 8-Decoder (A5) durch Bildung der Signale CE1 ... CE4 (Bausteinaktivierung) in Abhängigkeit der umgeschlüsselten Adreßbits AB12 ... AB15.
Die Ansteuerbedingungen sind:
Schreib-/Leseaufruf: $\overline{\text{MREQ}} \cdot \text{RFSH}$
Auffrischaufrunf: $\overline{\text{RFSH}} \cdot \overline{\text{MREQ}}$

Während des Auffrischzyklus werden über A6/01/12 und A9/08 durch das Signal $\overline{\text{RFSH}} = 0$ die CE-Eingänge der Speicherchips auf high geschaltet. Damit bleiben die Datenausgänge hochohmig und es tritt ausgangsseitig keine Beeinflussung durch die ausgewählte STE auf. Beim RFSH-Aufruf werden alle 4 Blöcke (4 x 4 KByte) gleichzeitig angesteuert, jeweils 64 Speicherzellen in jedem Baustein (Matrix 64 x 64).

- Das Steuersignal $\overline{\text{RD}}$ steuert dabei die Datenflußrichtung der Datentreiber A3/01 und A3/02. Beim Lesen ist $\overline{\text{RD}} = 0$ und die Daten sind vom Speicher auf den Bus geschaltet. Aktiviert werden diese Ausgangstreiber durch $\overline{\text{CS}} = 0$, das gebildet wird am A9/11 durch MEMDI und die Aussage am 1 aus 8-Decoder A7 (umgeschlüsselte Adreßbits).

8. Operativspeicher OPS K 3521.20

4 KByte CMOS RAM - Typ 045-8063 (1.45.518063.1)

8.1. Kurzcharakteristik

Im Schreib-Lese-Speicher (Operationsspeicher) OPS K 3521.20 werden während des Programmablaufs im Mikrorechner K 1520 variable Daten gespeichert. Im Gegensatz zum OPS K 3520 bleiben die Daten auch nach einer zwischenzeitlichen Programmunterbrechung durch Netzabschaltung am Rechner für eine vorgegebene Haltezeit für die weitere Programmabarbeitung erhalten.
Der OPS K 3521.20 besteht aus dem STE-Typ 045-8063 mit indirektem Steckverbinder. Er ist ein 4 KByte statischer Halbleiterspeicher, bestückt mit CMOS-Bausteinen und den zur Entkopplung, Auswahl und Ansteuerung erforderlichen bipolaren Schaltkreisen.
Zusätzlich befindet sich auf der STE die zur Stützung der Speicherbetriebsspannung gehörende Logik einschließlich der Stützspannungsquelle.

Die Steckeinheit Typ 045-8063 ist analog der 012-7012 (siehe Betriebsdokumentation Speichersteckeinheit K 3521). Eine Änderung ergibt sich durch die Verwendung der Schaltkreise K 537 RU 1A.

8.2. Technische Daten

| Parameter | Wert |
| :--- | :--- |
| Speicherkapazität: | 4 KByte (Anordnung von 4 x 8 Speicherchips) |
| Speicherschaltkreistyp: | K 537 RU 1A nach TU II-78 (UdSSR) bei STE-Typ 045-8063 |
| | 1 K x 1 Bit; CMOS |
| Zugriffszeit: | $\leq$ 1340 ns |
| | Durch die STE werden bei Adressierung 2 WAIT-Zyklen generiert. |
| Betriebsarten: | "Lesen" und "Schreiben" als abgeschlossene Zyklen in beliebiger Reihenfolge. |
| | Die STE-Typ 045-8063 gestattet keine Abarbeitung von Befehlslese - (M1) - Zyklen von dieser Speicher-STE. |
| Datenerhalt: | Durch STE-interne Stützung der Betriebsspannung für die Speicherschaltkreise durch gepufferte NK-Akkumulatoren wird der Datenerhalt bei Abschaltung der externen Betriebsspannung gewährleistet. |
| | Datenhaltezeit $\geq$ 200 Stunden |
| Spannungsquelle: | Reihenschaltung von 4 NK-Knopfzellen mit je 1,2 V und 225 mAh, Typ KBL 0,225 vom VEB GLZ nach TGL 22807 |
| Stromversorgung: | 5 P = 5 V $\pm$ 5 %, typ. 0,65 A für Steuerelektronik |
| | 12 P = 12 V $\pm$ 5 %, typ. 0,17 A für Speicherschaltkreise und Akkuladestrom |
| | 5 N = -5 V $\pm$ 5 %, typ. 0,01 A für Komparatoren |
| Stützspannungsüberwachung: | Vor dem Zuschalten der Systemspannungen bewertet eine Kontrollschaltung den Spannungszustand der Batterie und speichert das Ergebnis ab. |
| | Im Betriebszustand kann die Aussage: |
| | - Stützspannung war größer als die minimale |
| | - Stützspannung war gleich Schlafspannung der |
| | - Stützspannung war kleiner Speicherschaltkreise |
| | angezeigt werden (V9) oder logisch auf den Bus ausgewertet werden - über das Steuersignal SUE. |
| | LED-Anzeige ein bzw. $\overline{\text{SUE}} = 0 \hat{=}$ der Aussage: Datenerhalt war gesichert. |

8.3. Einsatzbedingungen für den Stütz-Akkumulator

Die NK-Knopfzellen werden in Einzelgehäusen gehalten, die an der Griffseite der Steckeinheit angeordnet sind. Das Wechseln der Knopfzellen ist im gestreckten Zustand der STE möglich und kann auch im Betriebszustand des Rechners erfolgen.
Während der Lagerung und des Transportes sind die Knopfzellen auf der STE nicht zu bestücken. Es wird davon ausgegangen, daß bei einer Neubestückung grundsätzlich geladene Zellen zum Einsatz kommen.
Im Betriebszustand werden die Zellen mit einem mittleren Ladestrom von 5 mA geladen (ständige Pufferung). Dieser Strom führt auch bei Grenztemperaturen nicht zu Überladungserscheinungen an den Zellen. Der maximal vorkommende Entladestrom bei abgeschaltetem Rechner beträgt 800 µA. Der reale Wert hängt von den konkreten Typen und der Qualität der CMOS-Schaltkreise ab und kann zwischen wenigen µA bis zu 800 µA bei 5 V Betriebsspannung streuen. Aus dieser Vorgabe ergibt sich als Richtwert, daß der Ladezustand der Zellen erhalten wird, wenn die Ladezeit allgemein 1/7 der Entladezeit beträgt.
Die Lebensdauer der Akkus wird durch die nutzbare mAh-Kapazität der Zellen bestimmt. Angaben dazu sind in der Einsatzvorschrift des Akku-Herstellers und in der TGL 22807 festgelegt. Da die Einsatztemperatur im K 1520 bis zu 60 °C betragen kann, entstehen hohe Belastungen für die NK-Elemente. Temperaturen über 35 °C bewirken zunehmende chemische Umsetzungen der aktiven Masse, die die Kapazität und damit die Lebensdauer erheblich reduzieren. Es gelten daher laut Qualitätsvereinbarungen folgende zusätzliche Einsatzbedingungen:
Erfolgt der Betrieb der Zellen im Temperaturbereich bis 45 °C bei zusätzlich insgesamt einer Woche Spitzentemperatur bis 60 °C, so ergibt sich eine garantierte Lebensdauer von einem Jahr, wobei die Lebensdauergrenze bei einer nutzbaren Kapazität von 100 mAh definiert ist.
Besteht die Grenztemperatur von 60 °C über einen langen Zeitraum, so verringert sich die Lebensdauer auf 3 Monate.
Aus diesen Garantiewerten ist abzuleiten, daß die Zellen bei Erreichen der angegebenen 100 mAh-Grenze auszuwechseln sind, wenn eine Datenhaltezeit von 200 Stunden sicher gewährleistet werden muß.
Reduziert man die Anforderungen an die langen Datenhaltezeiten, so lassen sich die Zellen noch nutzen, wenn die Kapazität von 100 mAh unterschritten ist. Die reale thermische Belastung über die Zeit ist aber in der Praxis schwer erfaßbar, so daß der Kapazitätszustand der Zellen nicht exakt vorhersagbar ist.
Ein ökonomischer Einsatz der Zellen wird ermöglicht, wenn im Betrieb unter konkreten Einsatzbedingungen im Endprodukt praktische Werte für den Akkutausch abgeleitet werden.
Als Kriterium für die nutzbare Grenzkapazität und den dabei erreichbaren Ladezustand kann die Anzeige der Batteriespannungsüberwachungsschaltung genutzt werden. Wird im Zustand des Datenerhalts die Batteriespannung auf der STE wiederholt gemessen und sinkt bei Einhaltung normaler Lade- und Entladezyklen unter 4,8 V, dann sind die Zellen auszutauschen.

Neben den zusätzlichen, bereits erwähnten Qualitätsvereinbarungen bei Temperaturen über 35 °C, gelten die Festlegungen der TGL 22807 und die Behandlungsvorschrift des Akku-Herstellers über Lagerung und Einsatz der Zellen.
Ist im Rechner eine Totalentladung von Zellen aufgetreten, sind diese außerhalb des Rechners mittels Ladegerät nach Vorschrift des Herstellers zu laden. Der Ladezustand bei der Lagerung von Zellen ist durch regelmäßige Erhaltungsladungen zu sichern (siehe auch Wartungsvorschrift).
Eine Ladung von vollständig entladenen Zellen auf der STE mit dem geringeren Strom von ca. 5 mA führt nicht zur vollen Ausnutzung der Nennkapazität der Zellen.
Eine Lagerung von entladenen Zellen ist bis zu einer Lagerzeit von einem Jahr ohne Einschränkung der elektrischen Parameter möglich, wenn die Umgebungstemperatur 20 °C $\pm$ 5 °C und die relative Luftfeuchte 60 % $\pm$ 15 % eingehalten werden. Danach müssen sie unbedingt mittels Ladegerät zwei- bis dreimal mit Nennstrom geladen werden und entladen werden, bis sie im Rechner eingesetzt werden können.
Knopfzellen anderer Hersteller mit vergleichbaren elektrischen und konstruktiven Daten können eingesetzt werden, wenn die Behandlungsvorschriften dieser Erzeugnisse entsprechende Beachtung finden. Es können sich dabei Einschränkungen in den technischen Daten der Speicher-STE bezüglich Einsatzbedingungen und Datenhaltezeit ergeben.

Beim Einsatz der Zellen auf der STE muß eine Zellen-Summenspannung von $\geq$ 4,6 V vorliegen, da die auf dieser STE eingesetzten Speicherschaltkreise ständig, auch während der Stützung mit einer Betriebsspannung von 5 V $\pm$ 0,5 V versorgt werden müssen. Unterhalb dieser Spannung spricht die Spannungsüberwachung an und sperrt die CE-Signal-Bildung und damit den Zugriff zum Speicher, während des Betriebszustandes werden die Zellen über einen Parallelregler mit ca. 20 mA geladen, solange die Zellenspannung unter ca. 5,3 V bleibt. Bei Überschreiten der Schwelle wird der Ladestrom abgeschaltet und bei Unterschreiten wieder zugeschaltet. Da die Akkumulatorspannung unmittelbar Betriebsspannung der RAM-Schaltkreise ist, ist beim Wechsel der Zellen während des Betriebes Vorsicht geboten, um ein Kurzschließen der Zelle bei Einbringen in die Halterung und in Folge dessen ein Ansprechen der Spannungsüberwachung und eventuellen Datenverlust zu vermeiden. Zum Betrieb der STE ohne Zellen muß der Schalter S1.2 (01-02) geschlossen werden.

8.4. Programmierung der STE Typ 045-8063

![[Pasted image 20260516145511.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt die Bestückungsseite der Leiterplatte OPS K 3521.20 (Abb. 3). Markiert sind die Positionen der Schaltergruppen S1.1, S1.2, S1.3 und S1.4 sowie die Steckverbinder X1 und X2 am unteren Platinenrand.*

Abb. 3

Über die 4 Codierbrücken S1:1 wird der STE ein wählbarer zusammenhängender Adreßbereich von 4 KByte-Adressen zugeordnet. Die programmierte Auswahleinrichtung erhält binär verschlüsselt die Anfangsadresse des gewünschten Adreßbereiches. Die Adresse ist ein ganzzahliges Vielfaches von 4 K.
Die 4 Schalter von S1:1 sind wie folgt einzustellen:

| Adreßbereich | 07 - 08 | 05 - 06 | 04 - 03 | 02 - 01 |
| :--- | :---: | :---: | :---: | :---: |
| 0000 - 0FFF | x | x | x | x |
| 1000 - 1FFF | x | x | x | - |
| 2000 - 2FFF | x | x | - | x |
| 3000 - 3FFF | x | x | - | - |
| 4000 - 4FFF | x | - | x | x |
| ... | | | | |
| F000 - FFFF | - | - | - | - |
| den Brücken sind folgende Wertigkeiten zugeordnet: | $\hat{=}$ 32 K | $\hat{=}$ 16 K | $\hat{=}$ 8 K | $\hat{=}$ 4 K |

x = Schalter geschlossen
- = Schalter geöffnet

![[Pasted image 20260516145553.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung 1 zeigt das Blockschaltbild der K 3521.20 Logik. Zu sehen sind Eingangs- und Steuerpuffer, Blockauswahl, Adressumschlüssler, Akku-Kontrolle mit Ladeschaltung, Datentreiber, Speichermatrix (4x1K Byte) und diverse Logikgatter zur WAIT-Bildung und RESET-Verarbeitung. Die Signale AB0...15, WR, RD, MREQ, RFSH, RESET und TAKT werden als Eingänge geführt.*

Abb. 1
Blockschaltbild K 3521.20 - Logik

![[Pasted image 20260516145617.png]]
*KI generierte Interpretation der Abbildung: Abbildung 2 zeigt das Blockschaltbild der K 3521.20 Sonderbaustufen. Dies ist ein detaillierter Schaltplan der Stromversorgung und Batterie-Backup-Logik. Er enthält Akkubewertung, Spannungsbewertung, einen Nachführregler, Schaltregler, Pufferkondensatoren und die Anbindung der Speichermatrix über verschiedene Versorgungsspannungen (5P, 12P, 5PGW).*

Abb. 2
Blockschaltbild K 3521.20 - Sonderbaustufen

Der Schalter ist geschlossen, wenn der Punkt auf dem DIL-Schalter sichtbar ist.
Der Kontakt S1:2 (01-02) ist zu schließen, wenn die STE abweichend vom Normalfall ohne Stützakkumulatoren betrieben werden soll. Damit wird die Erzeugung der Betriebsspannung für die Speicherchips aus der Spannung 12 P unabhängig von der Spannungsüberwachungseinrichtung. Die Pegel vom Steuersignal SUE und für die LED-Anzeige in dieser Betriebsart sind undefiniert.
Ist die STE mit Stützzellen bestückt, ist der Kontakt unbedingt wieder zu öffnen, um eine zusätzliche Belastung der Zellen im Stützbetrieb zu verhindern (Verkürzung der Stützzeit).

8.5. Auswahl des Speichersperrsignals MEMDI

| Signal | Schalter | Kontakt | Einstellung |
| :--- | :---: | :---: | :--- |
| $\overline{\text{MEMDI}}$ | Schalter S1:3 | Kontakt 01-02 | geschlossen |
| $\overline{\text{MEMDI1}}$ | Schalter S1:3 | Kontakt 03-04 | geschlossen |
| $\overline{\text{MEMDI2}}$ | Schalter S1:3 | Kontakt 05-06 | geschlossen |

Die verbleibenden Schalterkontakte dienen der Prüfung der STE und müssen im Betriebsfall geöffnet sein.

8.6. Funktionsbeschreibung

8.6.1. Verwendungszweck

Der OPS K 3521.20 wird im Erzeugnisprogramm "Dezentrale Datentechnik" als Operativspeicher eingesetzt (statischer Schreib-Lese-Speicher), dessen Informationen bei Systemspannungsabschaltung gesichert werden.
Beachte! Die Variante K 3521.20 ist nicht zur Abarbeitung von M1-Zyklen geeignet.

8.6.2. Funktion

Aufgrund des Einsatzes unterschiedlicher CMOS-RAM-Typen existieren im VEB Robotron Buchungsmaschinenwerk die Steckeinheitentypen STE-Typ 012-7012 (Betriebsdokumentation K 3521, Punkt 4) und die STE Typ 045-8063 (RAM - Matrix aufgebaut auf dem Schaltkreis K 537 RU 1A).
Die STE beinhaltet die Funktionsgruppen:
- Speichermatrix
- Ein- und Ausgabepuffer
- Auswahl- und Steuerelektronik
- Akkumulatorlade-, Kontroll- und Auswerteschaltung

Die Schaltung der Variante K 3521.20 wird nachfolgend beschrieben, da sie Unterschiede zum OPS K 3521 aufweist.
Das Blockschaltbild (Abb. 1) zeigt die Wirkungsweise der Logiksteuerung, Abb. 2 ergänzt das Blockschaltbild in Bezug auf einen akkumulatorgestützten Datenerhalt.

Die Speichermatrix besteht aus 4 Gruppen zu je 8 Speicherchips. Jedes Chip enthält 1 KBit. Eine Gruppe von 8 Chips bildet einen Speicherbereich von 1 KByte. Jede der 4 vorhandenen Blöcke wird durch ein gesondertes CE-Signal aktiviert.
Alle 10 Adreßeingänge der Speicherchips und der Steuereingang $\overline{\text{WE}}$ (Schreib-Lese-Steuerung) sind miteinander verbunden und werden über low-power-Schottky-TTL-Pufferschaltkreise A2:1 bis A2:3 gespeist, die ihrerseits die Ansteuerung der open-collector-Treiber T 106 für die Speicher übernehmen.
Diese Treiber gewährleisten im Zusammenhang mit Lastwiderständen, die mit einer gesondert erzeugten Spannung 5 PGW versorgt werden, die besonderen Forderungen der Speicherschaltkreise des OPS K 3521.20 in Bezug auf Eingangspegel, die nur $0,4 < 5 \text{ PGI}$ (Speicherbetriebsspannung), jedoch nicht $< 4,6 \text{ V}$ sein dürfen.
Alle übrigen Speichereingangssignale des OPS K 3521.20 sind ebenfalls über open-collector-Stufen geführt (A3:3 bis A3:5).
Bei den Datenausgangs- und -eingangsleitungen sind jeweils die gleichen Bits der 4 Speicherblöcke parallel geschaltet und mit bidirektional arbeitenden Datenpufferschaltkreisen SE16 (A1:1, A1:2) verbunden, die die Verbindung mit dem Systembus herstellen. Ist die STE nicht angesteuert, sind die Datenpuffer hochohmig und belasten den Systembus nicht.
Da zwischen Dateneingang und -ausgang der Speicherchips ein Polaritätswechsel stattfindet, werden die Eingangsdaten negiert (A3:1, A3:4/08, 10).
Der 1 aus 8-Decoder A11 decodiert die Adreßsignale AB10 und AB11 und es wird eines der 4 Speicheransteuersignale CE über nachgeschaltete open-collector-Stufen (A3:5/04, 06, 10, 12) aktiviert. Voraussetzung für die Decodierung dieser beiden Adreßleitungen sind die Steuersignale:
- $\overline{\text{MREQ}} = 0$ = Speicheranforderung
- $\overline{\text{MEMDI}} = 1$ = Speichersperre inaktiv
- $\overline{\text{RFSH}} = 1$ = kein Refresh-Zyklus

und die Adreßsignale AB12 ... AB15 entsprechend der Anfangscodierung der Wickelbrücken.
Die Byteanwahl innerhalb der 4 Blöcke erfolgt direkt durch die Adreßbits AB0 ... AB9.
Die Erkennung der Anfangsadresse der STE erfolgt durch den Exklusiv-Oder-Baustein T 186 (A4).
Die Adreßbits AB12 ... AB15 werden mit der an der Schaltergruppe S1:1 eingestellten (negierte) Anfangsadressen verknüpft.
Ein geöffneter Schalter legt high-Pegel an den Eingang des entsprechenden Exklusiv-Oder-Bausteins. Bei programmierter Anfangsadresse ist die Adreßleitung low, so daß am Nand T 130 (A5) bei $\overline{\text{MEMDI}} = 1$ und $\overline{\text{MREQ}} = 0$ am Ausgang A5/08 das Freigabesignal low bildet.
A5/08 = 0 aktiviert den 1 aus 8-Decoder A11, die Ausgangsdatentreiber A1:1 und A1:2 durch den Steuereingang $\overline{\text{CS}}$, wobei das Signal $\overline{\text{RD}}$ die Wirkungsrichtung des Datenaustausches zwischen Bus und Speicher vorgibt. Außerdem wird das Quittungssignal $\overline{\text{RDY}}$ erzeugt (A3:2) und durch Freigabe der Schiebekette A10:2 und A10:1 die Bildung des Signals $\overline{\text{WAIT}}$ ausgelöst. Dieses Signal bedingt die Einblendung von zwei WAIT-Verlängerungszyklen durch die ZVE. Eine Schiebekette, bestehend aus 4 D-Flip-Flops, schaltet das Signal $\overline{\text{WAIT}}$ so lange auf low, bis die ZVE zwei WAIT-Zyklen eingeblendet hat. Gleichzeitig gibt der Ausgang 05 das FF A10:1 den 1 aus 8-Decoder A11 frei, der CE1 ... CE4 bildet.
Am Nand A7/12 wird das Steuersignal WR verknüpft mit dem aus der Schiebekette gebildeten Signal und bildet das Schreibsignal WRI für die Speicherschaltkreise.

Sonderbaugruppen der STE OPS K 3521.20 sind:
- Akkumulatorladeschaltung
- Spannungsregel- und -auswerteschaltung

Sie gewährleisten bei externer Stromversorgung den unterbrechungsfreien Übergang zum Stützbetrieb bei Abschalten der Betriebsspannungen und umgekehrt einen Übergang in den Normalbetrieb ohne Informationsverlust.
Die Betriebsspannung 5 PGI für die Speicherschaltkreise wird aus 12 P über den Transistor V8:1, dem Begrenzungswiderstand R9, der von einer Auswerteschaltung gesteuert wird (Akkumulatorstrombegrenzung) und der Diode V1:2 gebildet.
Im Stützbetrieb ist die Diode V1:2 zur Entkopplung in Sperrichtung betrieben.
Die Betriebsspannung der Speicherschaltkreise wird mit dem Schwellwertschalter A13:2 überwacht.
Durch den Regelwiderstand R14 wird die Spannung am invertierenden Eingang 04 des A13:2 auf 5,2 V eingestellt. Über den Widerstand R6:2 gelangt die zu kontrollierende Betriebsspannung an den Eingang 03. Das RC-Glied R2 parallel C5:1 ist das Rückkoppelglied des Schaltreglers. Übersteigt die Betriebsspannung für die Speicherschaltkreise einen bestimmten Wert, öffnet der Transistor V2:1. Der Spannungspegel sinkt.
Fällt die Betriebsspannung unter den Sollwert, z. B. durch Änderung der Stromaufnahme im dynamischen Betrieb, sperrt der Schalttransistor V2:1, so daß die Spannung steigen kann.
Der Schaltkreis A14 erzeugt für die beiden Komparatoren A13:1 und A13:2 eine hochstabile Referenzspannung und zwar für den A13:2 ca. 5,2 V und durch den Spannungsabfall am Innenwiderstand der geöffneten Diode V3:1, ca. 4,6 V für den A13:1.
Die Spannungsregelschaltung A14 wird über R4 von der Systemspannung 12 P gespeist und regelt in Abhängigkeit von der Ausgangsspannung der Betriebsspannungserzeugung 5 PGI über V9:2 und V7:2 die Anschlagspannung für die Widerstandsnetzwerke A17, R19 direkt und A16:2 indirekt über R11:2 und V8:2, die eine Potentialanhebung der TTL-Ausgangspegel auf die geforderten Werte sichern.
Das Widerstandsnetzwerk A16:2 ist über einen elektronischen Schalter V2:2/V8:2 mit dieser durch A14 nachgeführten Anschlagspannung verbunden und liefert am A14/06 die Spannung 5 PCE.
Der elektronische Schalter wird von einer Spannungsüberwachungsschaltung gesteuert, die den Pegel der Hilfsspannung 12 P und der Logikspannung 5 P kontrolliert. Liegt eine der Spannungen außerhalb der Toleranzgrenze, wird die Speichermatrix durch Abschaltung der 5 PCE (über R16:2) sofort in den inaktiven Zustand gebracht und der Informationsinhalt bleibt erhalten.
Das Signal $\overline{\text{RESET}}$ beeinflusst den elektronischen Schalter über A12/03 und A3:5/02. Er ist erst wirksam, wenn $\overline{\text{RESET}}$ wieder inaktiv geworden ist.
Der A12/06 verknüpft das Signal $\overline{\text{RESET}}$ mit dem Auswertesignal vom Komparator A13:1. Dieser Komparator überwacht den Spannungspegel 5 PGI. Die Vergleichsspannung 4,6 V ist durch den Spannungsteiler R12 ... R14 einstellbar.
Bei $\overline{\text{RESET}} = 0$ wird über das Nand A12/03 und dem elektronischen Schalter V2:2 und V8:2 die Spannung 5 PCE zugeschaltet.
Die Aussage der Komparatorschaltung "Batterieladespannung $\geq 4,6 \text{ V}$ bzw. $< 4,6 \text{ V}$" wird durch das FF A12/08/11 gespeichert. Es kann aber jederzeit durch den Zustand "Speicherbetriebsspannung unterhalb der Toleranzgrenze" gekippt werden.
Er erfolgt:
1. Bildung des Signals SUE Fehlerfall
2. Bildung eines optischen Kontrollsignals durch V7
3. Aktivierung des elektronischen Schalters V8:1 (über V10, V4, R8 und R5:1)

Dieser Schalter läßt erst dann einen Ladestrom in die Akkumulatoren fließen, wenn die Auswertung des Batterieladezustandes (während $\overline{\text{RESET}}$) positiv verlaufen ist.

SUE = 1 und LED-Anzeige leuchtet: 5 PGI $\geq$ min. Schlafspannung. Datenerhalt gesichert.
SUE = 0 und LED-Anzeige aus: 5 PGI < min. Schlafspannung bzw. Akkus sind nicht bestückt. Datenerhalt ist nicht garantiert.

Die Prioritätsketten $\overline{\text{BAI}}$, $\overline{\text{BAO}}$
$\overline{\text{IEI}}$, $\overline{\text{IEO}}$
$\overline{\text{IEIT}}$, $\overline{\text{IEOT}}$ sind auf der Steckeinheit gebrückt.
Kurz- und langseitige Störungen auf der Betriebsspannung werden durch Sieb- und Stützkondensatoren gesiebt.

9. Operativspeicher OFK K 3626.31

32 KByte RAM - Typ 062-8331 (1.62.518331.0 - 083-4-710-052)

9.1. Kurzcharakteristik

Mit dem Schreib-Lese-Speicher (Operativspeicher) OFK K 3626.31 werden variable Daten während des Programmablaufs im Mikrorechner K 1520 gespeichert. Er besteht aus einem 32 KByte großen dynamischen RAM-Speicher mit folgenden Funktionsgruppen:
- Matrix 32 KByte RAM
- Ein- und Ausgabepuffer bzw. Spalten- und Zeilenauswahl der Speicherchips
- Aussteuerelektronik

9.2. Technische Daten

| Parameter | Wert |
| :--- | :--- |
| Steckeinheitenabmessungen: | 215 mm x 170 mm |
| Speicherkapazität: | 32 KByte (Matrix von 2 x 8 Speicherchips) - U 256 |
| Speicherschaltkreistyp: | K 565 RU 3G (MK 411 6P-3) 16384 x 1 Bit |
| Zugriffszeit: | $\leq$ 200 ns |
| Steckverbinder: | 2 x 58-polig, indirekt Bauform: 304-58, TGL 29331/03 für System- und Koppelbussteckverbinder |
| Stromversorgung: | + 5 V $\pm$ 5 % max. 750 mA |
| | + 12 V $\pm$ 5 % max. 100 mA |
| | - 5 V $\pm$ 5 % max. 15 mA |
| Betriebsarten: | Lese- und Schreibzyklen in beliebiger Reihenfolge |
| Datenerhalt: | Die Information geht bei Abschaltung der Betriebsspannung verloren. Alle $\leq$ 2 ms muß durch eine Refresh-Steuerung die Speicherzelle regeneriert werden. Sie erfolgt durch die ZVE des MR K 2526. |

9.3. Programmierung der STE

Das Programmierfeld ist als Wickelstiftpaare ausgeführt. Abbildung 3 zeigt die Belegung der Wickelstifte für die Festlegung der Anfangscodierung der STE. Entsprechend der Zuordnung werden die Brücken gewickelt.

![[Pasted image 20260516145703.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt die Bestückungsseite der OFK K3626.31 (Abb. 3). Dargestellt sind die Positionen der Wickelbrückenfelder X8 und X9 sowie die Steckverbinder X1 und X2.*

Abb. 3

Innerhalb des Adreßraumes von 64 KByte kann der RAM-Speicherbereich von 32 KByte in 4 KByte-Schritten verschoben werden.
Mit den 4 Wickelbrücken X8:1 ... X8:4; X9:1 ... X9:4 kann in binärer Verschlüsselung die Anfangsadresse des gewünschten Adreßbereiches durch entsprechende Brücken festgelegt werden.

| Adreßbereich | X8:4 - X9:4 | X8:1 - X9:1 | X8:2 - X9:2 | X8:3 - X9:3 |
| :--- | :---: | :---: | :---: | :---: |
| 0000 ... 7FFF | - | - | - | - |
| 1000 ... 8FFF | - | Brücke | - | - |
| 2000 ... 9FFF | - | - | Brücke | - |
| 3000 ... AFFF | - | Brücke | Brücke | - |
| : | : | : | : | : |
| 8000 ... FFFF | Brücke | - | - | - |

Adressenzuordnung bei beschalteter Brücke:
$\hat{=}$ 32 K (X8:4-X9:4)
$\hat{=}$ 4 K (X8:1-X9:1)
$\hat{=}$ 8 K (X8:2-X9:2)
$\hat{=}$ 16 K (X8:3-X9:3)

9.4. Funktionsbeschreibung

9.4.1. Verwendungszweck

Der OFK K 3626.31 wird im Erzeugnisprogramm "Dezentrale Datentechnik" als Operativspeicher (dynamischer Schreib-Lese-Speicher) verwendet.

9.4.2. Funktion

Die Adreßleitungen AB0 ... AB6 werden über den Schaltkreis A3 geführt und liegen an den Speicherschaltkreisen als Eingänge A0 ... A6 an. Mit den sieben Adreßbits können 128 Zeilen adressiert werden. Der Übernahmetakt für diese Zeilenadresse $\overline{\text{RAS}}$ (row adress strobe) wird durch $\overline{\text{MREQ}}$, $\overline{\text{F3}}$ (A25/08) oder $\overline{\text{RFSH}}$ (A12/08; A25/11) gebildet und ist mit low aktiv. Da $\overline{\text{MREQ}}$ mit einer H-L-Flanke des Taktes aktiv wird (siehe Taktdiagramm ZVE - Bausteinübersicht), wird auch das Zeilenansteuersignal $\overline{\text{RAS}}$ mit dieser Flanke ausgelöst. Mit Bildung von $\overline{\text{RAS}} = 0$ wird über A25/06 mit der nächsten L-H-Flanke des Taktes das FF A20 gekippt und das Register A3 wird gesperrt (hochohmiger Zustand). Da das Register erst mit der L-H-Flanke des Taktes gesperrt wird, bleibt ausreichend Zeit, um die Zeilenadresse in die RAM's zu übernehmen.
Zur gleichen Zeit wird der Adreßtreiber A4 freigegeben. An den Dateneingängen liegen die Adreßleitungen AB7 ... AB11 und bilden zusammen mit den Adreßbits AB12 und AB13 am 4-Bit-Volladder T183 A9 (Wirkungsweise siehe Betriebsdokumentation Speichersteckeinheiten, Punkt 6.4.2.2.) gebildeten Signalen F0 und F1 die Spaltenadresse für die RAM-Bausteine. Die Blockauswahl (0. bis 15. KByte oder 16. bis 31. KByte) bewirkt das Adreßbit AB14. Über A9 wird mit dem Signal F2 oder $\overline{\text{F2}}$ die Spaltenansteuersignale $\overline{\text{CAS1}}$ und $\overline{\text{CAS2}}$ (column adress strobe) für die beiden 16 KByte RAM-Bereiche gebildet. $\overline{\text{CAS1}}$ wird nur aktiv, wenn $\overline{\text{RFSH}}$ inaktiv ist (A30/03; A30/06; A30/08 und 11).
Der Übernahmetakt für die Spaltenadresse bewirkt nach entsprechender Zugriffszeit, in der die Datenausgänge der RAM's hochohmig werden, die Ausgabe der Dateninformation, wenn $\overline{\text{WE}}$ inaktiv ist (high-Pegel) = Lesezyklus.
Das Datenbyte aus den adressierten RAM-Bausteinen gelangt über den internen Datenbus und wird über 2 bidirektionale Treiberschaltkreise A13/17 auf den Datenbus des Systems geschaltet. Diese Treiber werden aktiviert ($\overline{\text{CS}} = 0$), wenn $\overline{\text{RFSH}}$ und $\overline{\text{MEMDI}}$ inaktiv sind (high-Pegel) und $\overline{\text{RAS}} = 0$ (aktiv) über A25/06; A16/12; A16/08 und A21 Eingang 02.
Die Richtungssteuerung erfolgt durch das Steuersignal $\overline{\text{RD}}$ auf den Eingängen DIEN der Treiber A13/A17. Das Beschreiben der RAM-Bausteine verläuft analog dem Lesen. Vor der Bildung des Spaltenaktivierungssignals muß das Steuersignal $\overline{\text{WE}} = 0$ werden.
Das $\overline{\text{WE}}$-Signal für die RAM-Bausteine wird aus $\overline{\text{MEMDI}}$ und $\overline{\text{WR}}$ (A21/08) gebildet. Die Schreibdaten gelangen über die bidirektionalen Treiber A13/A17 auf den internen Datenbus und liegen jeweils an den bitorientierten DI-Eingängen der entsprechenden RAM-Bausteine. Das Steuersignal $\overline{\text{RD}} = 1$.
Der Treiber A13/A15 steuert die Daten von DB nach DO. Für die Bildung von $\overline{\text{CS}}$ dieser Treiber gilt wie beim Lesezyklus, die Steuersignale $\overline{\text{RFSH}}$, $\overline{\text{MEMDI}}$ und $\overline{\text{RAS}}$ sind low.
Wird $\overline{\text{MREQ}}$ inaktiv (high), d. h. keine Ansteuerung des Speichers, wird das Zeilenansteuersignal $\overline{\text{RAS}}$ abgeschaltet und entsprechend zeitverzögert, auch das entsprechende Spaltenansteuersignal $\overline{\text{CAS1}}$ oder $\overline{\text{CAS2}}$. Gleichzeitig wird das FF A20/06 rückgesetzt, das Register A4 für die Adreßleitungen AB0 ... AB6 freigegeben und das Register A5 gesperrt (Spalte).
Damit ist der Lese- bzw. Schreibzyklus abgeschlossen.
Die Ansteuerbedingungen sind:
- Schreib-/Leseaufruf: $\overline{\text{MREQ}} \cdot \overline{\text{MEMDI}} \cdot \overline{\text{RFSH}} < \frac{\overline{\text{RD}}}{\overline{\text{WR}}}$
- Auffrischaufrunf: $\overline{\text{RFSH}} \cdot \overline{\text{MREQ}}$

Gleichzeitig mit der Bildung des Signals $\overline{\text{CS}}$ für die Datentreiber A13/A17 wird über die open-collector-Baustufe A21/06 das Signal $\overline{\text{RDY}} = 0$ als Quittungssignal für die ZVE bei entsprechendem Adreßbereich gebildet.
Die Prioritätskette $\overline{\text{BAI}}$, $\overline{\text{BAO}}$
$\overline{\text{IEI}}$, $\overline{\text{IEO}}$
$\overline{\text{IEIT}}$, $\overline{\text{IEOT}}$ sind auf der STE nur gebrückt.
Kurz- und langseitige Störungen auf der Betriebsspannung werden durch Sieb- und Stützkondensatoren gesiebt.

9.4.3. Refresh-Zyklus

Eine eigene Regenerierungssteuerung, die bei Verwendung von dynamischen RAM-Schaltkreisen erforderlich ist, ist auf der STE K 3626.31 nicht vorgesehen.
Das Auffrischen des Dateninhaltes der RAM-Speicherbausteine erfolgt durch eine "Refresh"-Ansteuerung von außen; über die Adreßleitungen AB0 ... AB6 und das Signal $\overline{\text{RFSH}}$ aktiv low (über den Bus).
Der Speicher besteht aus 2 x 8 Speicherbausteinen U 212 mit einer Kapazität pro Chip von 2 x 16 KByte $\hat{=}$ 32 KByte.

Das Auffrischen erfolgt in der sogenannten RAS-only-Betriebsart, d. h. alle 128 Zeilen sämtlicher RAM-Bausteine müssen nacheinander adressiert werden, um den gesamten Speicherinhalt aufzufrischen. Das muß mindestens einmal innerhalb von 2 ms erfolgen, um den Dateninhalt zu erhalten.
In jedem M1-Zyklus wird eine Zeile aufgefrischt. Die Zeilenadresse wird von der ZVE (Refreshregister) über den Adreßbus AB0 ... AB6 (max. 128 Byte) bereitgestellt.

![[Pasted image 20260516145744.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt das Blockschaltbild K 3626.31 (Abb. 4). Es ist ein hierarchisches Schema der Speicherelektronik. Enthalten sind Pufferstufen (Puffer 1-4) für Adressen und Steuersignale, ein Adressenumschlüssler, Multiplexer-Steuerung, RAM-Steuerung, RDY-Bildung, Datenausgangspuffer sowie zwei Speichermatrix-Blöcke (8x16k bit). Diverse Steuersignale wie MREQ, RFSH, MEMDI, RD, WR und M1 werden verarbeitet.*

Abb. 4
Blockschaltbild K 3626.31

10. Programmierbarer Festwertspeicher PFS K 3822

16 K EPROM - Typ 062-8450 (1.62.518450.0 - 083-4-710-050)

10.1. Kurzcharakteristik

Die programmierbare Speichersteckeinheit PFS K 3822 ermöglicht die Speicherung von Festdaten.
Ein Datenerhalt ist auch nach Abschalten bzw. Ausfall der Betriebsspannung gewährleistet.
Auf der STE Typ 062-8450 befindet sich ein 16 KByte großer programmierbarer Festwertspeicher (EPROM-Speichermatrix) mit den Funktionsgruppen:
- Ein- und Ausgabepuffer
- Schaltung zur Auswahl und Ansteuerung der Speicherschaltkreise
- 16 KByte EPROM-Speichermatrix
- Schaltung zum logischen Trennen der Speichermatrix von der Ansteuerelektronik während des Programmiervorganges
- $\overline{\text{RDY}}$- und $\overline{\text{WAIT}}$-Bildung

Die EPROM-Speicherchips sind fest eingelötet. Das Programmieren der Schaltkreise erfolgt auf der STE über die Steckverbinder X1 und X2 in einem speziellen Programmiergerät. Eine Änderung des Speicherinhaltes ist durch Löschen der gesamten Speichermatrix oder einzelnen Speicherschaltkreise in einem speziellen Löschgerät und anschließendem Neuprogrammieren möglich.
Zur Prüfung der STE dient der direkte Steckverbinder X3.
Die STE K 3822 ist vollständig kompatibel zur STE K 3820 (Typ 012-7041) und kann an ihrer Stelle eingesetzt werden. Eine spezielle Variante mit 8 KByte-Bestückung ist möglich.

10.2. Technische Daten

| Parameter | Wert |
| :--- | :--- |
| Speicherkapazität: | 16 KByte (Anordnung von 16 Speicherchips); abrüstbar auf 8 KByte |
| Speicherschaltkreistyp: | Q 260 |
| | 1 x 8 KBit; nMOS EPROM |
| Zugriffszeit: | $\sim$ 350 ns |
| Betriebsarten: | - Lesen als abgeschlossener Zyklus |
| | - Programmieren im Programmiergerät |
| | - Löschen der gesamten Speichermatrix oder einzelner Speicherschaltkreise (im Löschgerät) |
| Stromversorgung: | 5 P = 5 V $\pm$ 5 %, I = 0,9 A |
| | 5 N = -5 V $\pm$ 5 %, I = 0,5 A |
| | 12 P = 12 V $\pm$ 5 %, I = 0,9 A |
| Beachte! | Die Spannung 5 V darf nicht später als 10 ms vor Wegfall der 5 P bzw. 12 P abschalten. |

10.3. Programmieren der STE

10.3.1. Programmierfelder der STE

Die Auswahlfelder bestehen aus Wickelstiftpaaren. Die Codierung erfolgt durch das Wickeln von Brücken.

![[Pasted image 20260516145807.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt die Bestückungsseite der PFS K 3822 (Abb. 5). Markiert sind die Wickelbrückenfelder X6 bis X13 sowie die drei Steckverbinder X1, X2 und X3.*

Abb. 5

10.3.2. Adressenzuordnung

Die 16 Adreßleitungen des Systembusses haben folgende Zuordnung:

AB0 ... AB9 - interne Chipadressierung
AB12 ... AB15 - Durch Umschlüsselung wird im Speicher die Adresse AB12K ... AB15K wirksam. Sie ergibt sich aus der stellenrichtigen Subtraktion der im Programmierfeld X8 - X9 ausgewählten Anfangsadressen der STE und der angelegten Adresse AB12 ... AB15.
AB10, AB11 - Auswahl einer der 16 x 1 KByte-Blöcke der STE (Chipauswahl)
AB14K, AB15K - STE wird ausgewählt, wenn beide Signale low-Potential besitzen.

Festlegung der Anfangsadresse der STE

Die 4 Wickelbrücken erhalten in binärer Verschlüsselung die Anfangsadresse der STE innerhalb des Gesamtspeichers des MR. Die Programmierung erfolgt durch entsprechende Brücken des Programmierfeldes X8:1 ... X8:4; X9:1 ... X9:4. Diese Adresse ist ein ganzzahliges Vielfaches von 4 K.

Wickelbrücken:

| Adreßbereich | X8:4 - X9:4 | X8:3 - X9:3 | X8:2 - X9:2 | X8:1 - X9:1 |
| :--- | :---: | :---: | :---: | :---: |
| 0000 - 3FFF | - | - | - | - |
| 1000 - 4FFF | - | - | - | Brücke |
| 2000 - 5FFF | - | - | Brücke | - |
| 3000 - 6FFF | - | - | Brücke | Brücke |
| 4000 - 7FFF | - | Brücke | - | - |
| : | : | : | : | : |
| C000 - FFFF | Brücke | Brücke | - | - |
| Wertigkeit der Wickelbrücke | $\hat{=}$ 32 K | $\hat{=}$ 16 K | $\hat{=}$ 8 K | $\hat{=}$ 4 K |

10.3.3. Belegung der EPROM-Schaltkreise auf der STE

Die EPROM-Speicherschaltkreise sind fest eingelötet. Die Adressen sind relativ zur programmierten Anfangsadresse der STE zu werten.

| | | | | | | | |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 0400-07FF | 0C00-0FFF | 1400-17FF | 1C00-1FFF | 2400-27FF | 2C00-2FFF | 3400-37FF | 3C00-3FFF |
| 0000-03FF | 0800-0BFF | 1000-13FF | 1800-1BFF | 2000-23FF | 2800-2BFF | 3000-33FF | 3800-3BFF |

10.3.4. Auswahl des Speichersperrsignals MEMDI

Wickelbrücken

| Signal | X6:1 - X7:1 | X6:2 - X7:2 | X6:3 - X7:3 |
| :--- | :---: | :---: | :---: |
| $\overline{\text{MEMDI}}$ (X1:B09) | Brücke | - | - |
| $\overline{\text{MEMDI1}}$ (X2:A21) | - | Brücke | - |
| $\overline{\text{MEMDI2}}$ (X2:B21) | - | - | Brücke |

10.3.5. Bildung von WAIT

Von der Zugriffszeit der verwendeten Speicherbauelemente hängt es ab, ob während des Befehlszyklus im MR eine Zeitverlängerung über "WAIT" vorgenommen werden muß.
Es gilt folgendes:
- Bildung von "WAIT" im M1-Zyklus: Brücke X10 - X11 offen
- keine "WAIT"-Bildung: Brücke X10 - X11 geschlossen

10.3.6. Abgerüstete Variante mit 8 KByte Speicherkapazität

Entsprechend der vorgegebenen Speicherkapazität ist folgende Wickelverbindung zu realisieren:
- 16 KByte-Bestückung: Brücke X12 - X13 offen
- 8 KByte-Bestückung: Brücke X12 - X13 geschlossen

10.3.7. Programmierung der Speicherschaltkreise

Die Programmierung der STE erfolgt im vollständig bestückten Zustand mit Hilfe eines speziellen Programmiergerätes. Das Beschreiben der Speicherchips erfolgt von einer Master-STE. Dabei ist die Programmierung der gesamten STE oder auch einzelner Speicherschaltkreise möglich.

10.3.8. Löschen der Speicherschaltkreise

Die Löschung der Speicherschaltkreise erfolgt durch ein spezielles Löschgerät. Es sind möglich:
- Gesamtlöschung der EPROM-Matrix oder
- Löschung einzelner Speicherchips.
(Löschgerät für PROM - STE K 3822 78401-0834710050)

10.4. Funktionsbeschreibung

10.4.1. Verwendungszweck

Der PFS K 3822 wird im Erzeugnisprogramm "Dezentrale Datentechnik" als programmierbarer Festwertspeicher eingesetzt. Er enthält die festen Programme oder Daten.

10.4.2. Funktion

Auf der STE befinden sich folgende Funktionsgruppen:
- Speichermatrix
- Ein- und Ausgangstreiber
- Auswahlelektronik
- Steuerelektronik

Die Wirkungsweise der Schaltung ist aus dem Blockschaltbild ersichtlich.

![[Pasted image 20260516145858.png]]
*KI generierte Interpretation der Abbildung: Die Abbildung zeigt das Blockschaltbild K 3822.02 (Abb. 6). Zu sehen sind Funktionsblöcke wie Eingangspuffer für Adresse und Steuersignale, Adressenumschlüssler, programmierte STE-Adresse, Blockauswahl, Speichermatrix (16 KByte EPROM), Steuerelektronik, Datenpuffer, RDY-Bildung, WAIT-Bildung und Auswahleinrichtung. Bussignale wie AB0...15, WR, RD, MREQ, RFSH, M1 und TAKT sind eingezeichnet.*

Abb. 6
Blockschaltbild K 3822.02

Die Speichermatrix besteht aus 16 Speicherbausteinen Q 260 zu je 1 KByte Speicherkapazität.
Die Adreßeingänge AB0 ... AB9 liegen parallel an allen Eingängen der Speicherchips an, sie werden über Schottky-TTL-Pufferschaltkreise SE12 (A11/12) verstärkt. Die Datenausgänge D00 ... D07 sind über die Datenpufferschaltkreise SE16 (A21/31) auf den Systembus geführt. Diese Ausgänge sind "3-state"-Ausgänge.
Die Auswahl und der Aufruf der 1 K-Speicherbereiche erfolgt über 16 CS-Signale CS1...CS16. Liegt ein Speicheraufruf vor, wird über zwei 1 aus 8-Decoder SE05 (A19/20) eines der 16 CS-Signale aktiv mit low. Der adressierte Speicherplatz wird gelesen. Die Umcodierung der über den Bus angelegten Adreßbits AB12 ... AB15 wird durch den Adderbaustein PS83 (A16) vorgenommen (siehe Punkt 10.4.2.). Dieser Adderbaustein verknüpft die angelegte Adresse AB12 ... AB15 mit der im Auswahlfeld der STE codierten Anfangsadresse. Es entstehen am Ausgang die internen STE-Adressen AB12K ... AB15K.
Die Adreßbits AB10, AB11 und die umcodierten Bits AB12K und AB13K (FO/F1) bilden durch die Decoder SE05 (A19/20) die Auswahlsignale CS1 ... CS16 der Speicherchips, vorausgesetzt, die vom Adderbaustein PS83 (A16) gebildeten Adreßbits AB14K und AB15K (F2/F3) geben die Decoder über das Nand A18/6 frei ($\overline{\text{MEMDI}}$ und $\overline{\text{RFSH}} = \text{high}$).
Abgeleitet vom Signal RD wird über A17/2 das Signal A15/3 = low für die Datentreiber A21/31 gebildet, welches die Daten aus dem Speicher auf den Bus schaltet.
Die Schaltung zur Bildung des Signals "WAIT" wird ebenfalls freigegeben (A17/8 = high). Ist die Brücke X10 - X11 nicht gesetzt, wird aus der mit den Signalen M1 und TAKT gesteuerten Schiebekette, bestehend aus zwei FF's A14, über A15/8 das Signal $\overline{\text{WAIT}}$ mit low gebildet und auf den Bus geschaltet.

Bei der Bestückung der Speichermatrix sind zwei Varianten möglich:
- 8 KByte-Bestückung oder
- 16 KByte-Bestückung

Die Bildung des Signals $\overline{\text{RDY}} = \text{low}$ wird deshalb vom Signal $\overline{\text{MREQ}}$ abgeleitet, abhängig von der Wickelbrücke X12 - X13 und von den internen STE-Adressen AB14K und AB15K. Die logische Verknüpfung dieser Bedingungen erfolgt durch A18/8.
Wird eine außerhalb des 16 KByte-Adreßraumes der STE liegende Adresse zugesprochen, so verbindet A18/6 mit high die Freigabe der Decoder und demzufolge die Bildung des RDY-Signals (A17/8 = low).
Bei 8 KByte-Bestückung (Brücke X12 - X13 gesetzt) wird die Bildung des RDY-Signals durch A17/4 = low verhindert, wenn eine Adresse außerhalb des 8 KByte-Adreßraumes angesprochen wird. Der Decoder A19 wird durch A17/4 = low gesperrt. Die RDY-Bildung ist auch durch RDYEX möglich.
Zur Durchschaltung der Prioritätskette für die INT-Bearbeitung sind die Anschlüsse
$\overline{\text{IEI}}$, $\overline{\text{IEO}}$
$\overline{\text{BAI}}$, $\overline{\text{BAO}}$
$\overline{\text{IEI1}}$, $\overline{\text{IEO1}}$ am System- und Koppelbus gebrückt.

10.4.3. Adreßdecodierung durch 4 Bit-Volladder PS83

Siehe Betriebsdokumentation Speichersteckeinheiten, Punkt 6.4.2.2.

10.4.4. Programmierung der STE

Die Programmierung der STE erfolgt mit Hilfe eines speziellen Programmiergerätes für 1 K EPROM (79291.0000080667) und der Aufnahmevorrichtung zur Programmierung der STE K 3822 (77301.0834710050).

Die Blockierspannung BLP 4,5 V verhindert unzulässiges Ansteigen der Spannung an den Ausgängen der Decoder A19 und A20 beim Anlegen der Programmierspannung CSP = 12 V. Mit CSP werden alle 1 K-Speicherbereiche gleichzeitig aufgerufen. Adresse und Daten liegen über AB0 ... AB9 bzw. D00 ... D07 gleichzeitig an allen Speicherchips an, die Auswahl und Programmierung des entsprechenden 1 K-Speicherbereiches erfolgen durch die Programmiersignale PR1 ... PR16.
Die Programmierung erfolgt im Programmiergerät gemäß Bedienungsanweisung. Eine Umprogrammierung des EPROM-Speichers kann durch Löschen im Löschgerät und anschließendem Neuprogrammieren erfolgen.
Dabei ist das Umprogrammieren des gesamten Speichers oder einzelner Speicherschaltkreise möglich.
Bei der Programmierung des Speichers ist zu beachten:

1. Das Betriebssystem ist, beginnend mit Segment "Grundmodul 0", ab der niederwertigsten EPROM-Speicheradresse und in fortlaufender Reihenfolge einzuspeichern. Unbelegte Speicherschaltkreise innerhalb der Segmente sind nicht zulässig.
2. Wird der vorhandene Speicher durch das Betriebssystem nicht voll ausgenutzt, sind die adreßmäßig höher liegenden unbelegten Speicherschaltkreise mit dem Segment "Füllmodul" zu beschreiben.
3. Da stets alle Speicherschaltkreise bestückt sind, steht im EPROM-Speicheradreßbereich kein Adreßraum für andere Zwecke zur Verfügung (z. B. für Bildschirmadressen, indem in einigen Gerätevarianten bei der bisherigen Steckeinheit K 3820 der 1. EPROM-Speicherschaltkreis nicht bestückt wurde).

4. Operativspeicher OPS K 3526

64 KByte dynamisch RAM

| Bezeichnung | Beschreibung | Bestellnr. |
| :--- | :--- | :--- |
| OPS K 3526.00 | interne RFSH-Steuerung-64 KByte RAM | 083-4-710-064 / 1.62.518600.8/00 |
| OPS K 3526.10 | keine interne RFSH-Steuerung-64 KByte RAM | 083-4-710-065 / 1.62.518601.6/00 |
| OPS K 3526.20 | interne RFSH-Steuerung-48 KByte RAM | 083-4-710-066 / 1.62.518602.4/00 |
| OPS K 3526.30 | keine interne RFSH-Steuerung-48 KByte RAM | 083-4-710-067 / 1.62.518603.2/00 |
| OPS K 3526.40 | interne RFSH-Steuerung-32 KByte RAM | 083-4-710-068 / 1.62.518604.0/00 |
| OPS K 3526.50 | keine interne RFSH-Steuerung-32 KByte RAM | 083-4-710-069 / 1.62.518605.7/00 |

11.1. Kurzcharakteristik

Mit dem 64 KByte dynamischen RAM STE (Operativspeicher) OPS K 3526.00 können variable Daten während des Programmablaufes im MR K 1520 gespeichert werden.
Die Besonderheit der STE besteht darin, daß ihr Speicherinhalt bei Netzausfall durch eine interne RFSH-Steuerung erhalten werden kann, wenn im MR ein AKU-Modul vorhanden ist (Notstrom).

11.2. Technische Daten

| Parameter | Wert |
| :--- | :--- |
| Steckeinheitenabmessungen: | 215 mm x 170 mm |
| Speicherkapazität: | 64 KByte; abrüstbar auf 48 KByte und 32 KByte (Matrix von 4 x 16 KByte) |
| Speicherschaltkreistyp: | Typ U 256 RU3A 16384 x 1 Bit |
| Zugriffszeit: | $\leq$ 400 ns |
| Steckverbinder: | 2 x 58 polig, indirekt Bauform: 304-58, TGL 29331/03 für System- und Koppelbussteckverbinder |
| Stromversorgung: | 5 P = + 5 V $\pm$ 5 %, max. 0,3 A |
| | 5 PG = + 5 V $\pm$ 5 %, max. 1,0 A |
| | $12 \text{ PG}^x = + 12 \text{ V} \pm 5 \text{ %, max. 0,5 A}$ |
| | $5 \text{ NG}^x = - 5 \text{ V} \pm 5 \text{ %, max. 10 mA}$ |
| | x: Die Signale 12 PG und 5 NG sind vom AKU-Modul gestützte Spannungen (auch 5 PG) und werden über die beiden STE Regelschaltung Typ 062-8845 und Ladeschaltung Typ 062-8850 zugeführt. Ohne AKU sind die Brücken W1:1, W1:2, W1:3 vorgesehen. |
| Betriebsarten: | Lese- und Schreibzyklen in beliebiger Reihenfolge |
| Signalpegel: | Ein- und Ausgangsleitungen zum Systembus K 1520 TTL-kompatibel entsprechend TGL 37271 |
| Betriebsart: | Dauerbetrieb, bei Netzausfall Speicherrettung bei separatem AKU-Modul und interner RFSH-Steuerung möglich |

11.3. Programmierung der STE

Die STE ist abrüstbar auf 48 K, 32 K und 16 KByte. Innerhalb des Adreßraumes von 64 K kann der RAM-Speicherbereich in 16 KByte Schritten verschoben werden durch die DIL-Schalter A5, A7 und A10 (siehe Abb. 7).

![[Pasted image 20260516145935.png]]
*KI generierte Interpretation der Abbildung: Abbildung 7 zeigt die Bestückungsseite der OFS K 3526 00. Markiert sind drei DIL-Schaltergruppen (A10, A7, A5) sowie die Speicherchip-Gruppen IV, III, II und I. Die Steckverbinder X1 und X2 befinden sich unten.*

Abb. 7
Schaltergruppen, Speichergruppen

Adressenzuordnung

| Gruppe | Adreßbereich | DIL-Schalter geschlossen |
| :--- | :--- | :--- |
| I | $0000_H$ - $3FFF_H$ | Grundzustand (ohne Schalterstellung) |
| | abgeschaltet | A7 PIN 6-11 |
| II | $4000_H$ - $7FFF_H$ | $A10 4-13^x$ |
| | $8000_H$ - $BFFF_H$ | A10 3-14 |
| | abgeschaltet | A7 7-10 |
| III | $4000_H$ - $7FFF_H$ | A7 4-13 |
| | $8000_H$ - $BFFF_H$ | $A7 3-14^x$ |
| | $C000_H$ - $FFFF_H$ | A7 2-15 |
| | abgeschaltet | A10 6-11 |
| IV | $0000_H$ - $3FFF_H$ | A5 6-11 |
| | $4000_H$ - $7FFF_H$ | A5 5-12 |
| | $C000_H$ - $FFFF_H$ | $A5 4-13^x$ |
| | abgeschaltet | A10 5-12 |

x Vorzugsbescbhaltung

| Brücke | K 3526.00 083-4-710-064 | K 3526.10 083-4-710-065 | Bemerkung |
| :--- | :---: | :---: | :--- |
| W1:1 | - | Brücke | 5 P - 5 PG |
| W1:2 | - | Brücke | 12 P - 12 PG |
| W1:3 | - | Brücke | 5 N - 5 NG |
| W1:4 | - | Brücke | Abschalten RFSH-Adresse |
| W1:5 | Brücke | Brücke | A23/13 |
| W1:6 | Brücke | Brücke | A23/04 |
| W1:7 | Brücke | Brücke | A23/12 |
| W1:8 | Brücke | - | Zuschalten RFSH-Adresse |

Beachte!
Wird die STE K 3526.00 ohne AKU-Modul betrieben (im Reparaturfall z. B. bei Signaturanalyse) sind die Brücken X10 zu wickeln. Bei Einsatz (mit AKU-Modul) sind diese Verbindungen unbedingt zu trennen.

Auswahl des Speichersperrsignals MEMDI

| Gruppe | Signal | Schalter | Kontakt | Gruppe | Signal | Schalter | Kontakt |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| I | MEMDI | A7 | $5-12^x$ | III | MEMDI | A7 | $1-16^x$ |
| | MEMDI1 | } keine Verwendung | | | MEMDI1 | A5 | 8-9 |
| | MEMDI2 | | | | MEMDI2 | A5 | 7-10 |
| II | MEMDI | A10 | $2-15^x$ | IV | MEMDI | A5 | $3-14^x$ |
| | MEMDI1 | A10 | 1-16 | | MEMDI1 | A5 | 2-15 |
| | MEMDI2 | A7 | 8-9 | | MEMDI2 | A5 | 1-16 |

x Vorzugsbeschaltung

Beachte!
Es ist jeweils nur ein MEMDI-Signal zuzuschalten. Bei Abschaltung einer Gruppe entsprechend Adressenzuordnung müssen alle MEMDI-Signale dieser Gruppe abgeschaltet sein (Kurzschlußgefahr).

11.4. Funktionsbeschreibung

11.4.1. Verwendungszweck

Der OPS K 3526.00 wird als Operativspeicher (dynamischer Schreib-Lese-Speicher) für das MR-System K 1520 verwendet.
Bei Verwendung eines AKU-Moduls kann der Speicherinhalt bei Ausfall der Systemspannung durch eine interne RFSH-Steuerung erhalten werden.

11.4.2. Funktion

Die STE hat folgende Funktionsgruppen:
- Speichermatrix
- Ein- und Ausgabepuffer
- Auswahl- und Steuerelektronik
- Ausfallerkennung, Taktgenerator, interne RFSH-Steuerung

Die Matrix besteht aus 4 Gruppen zu je 16 KByte, die wahlweise durch die entsprechende Adressenzuordnung durch die DIL-Schalter zugeschaltet werden können (siehe Punkt 11.3.).
Verwendet wurde der Baustein U256 (RU3A) mit einem Speichervolumen von 16 KBit. Da die Speicherarbeit byteorientiert erfolgt, sind jeweils 8 dieser Bausteine eine Gruppe. Die Gruppenauswahl wird durch die Adreßbits AB14 und AB15 am FF A2 und dem Baustein A4 decodiert.
Die Schreib- und Lesedaten werden durch 2 bidirektionale Datentreiber A9 und A17 zum Systembus der MR gepuffert. Diese Datentreiber sind durch $\overline{\text{CS}} = \text{low}$ ständig aktiv und werden durch das Steuersignal $\overline{\text{RD}}$ über den Treiber A16, A18/6, verknüpft mit dem internen Signal $\text{CAS}_x$ (A15/6), richtungsgesteuert.
Das Signal bildet über Nand A20 das Bereitschaftssignal $\overline{\text{RDY}}$ für die ZVE.
Die speicherspezifischen Steuersignale $\overline{\text{MREQ}}$, $\overline{\text{RFSH}}$, $\overline{\text{WR}}$, $\overline{\text{RD}}$, $\overline{\text{MEMDI}}$, TAKT werden vom System- bzw. Koppelbus über den Treiber A16 geführt.
Die Signale $\overline{\text{MEMDI1}}$ und $\overline{\text{MEMDI2}}$ werden auf der STE ausgewertet (siehe Punkt 11.3.).
Über die Eingänge 12 PG, 5 NG und 5 PG können die AKU-gestützten Spannungen zugeführt werden.

11.4.3. Speicherzugriff durch die ZVE

Die Adreßleitungen AB0 ... AB6 liegen über dem Schaltkreis A3 als Zeilenadresse, an den Speicherschaltkreisen als Eingänge A0 ... A6 an. Das Übernahmesignal für diese Zeilenadresse (max. 128 Zeilen) ist das Signal $\overline{\text{RAS}}$ (row adress strobe), das mit low aktiv ist. Es wird durch das Speicherzugriffssignal $\overline{\text{MREQ}}$ gebildet. (A21/3; A19/11 oder im Notstromfall durch INT = low vom Register A8).
Über das FF A22/9, die Schaltkreise A21/11 und A18/10 wird der Adreßtreiber A3 aktiviert und der Spaltenadreßtreiber A6 zunächst gesperrt.
Gleichzeitig erfolgt durch Decodierung der Adreßleitungen AB14 und AB15 über die FF's A2 die Gruppenauswahl auf der Speichermatrix.
Etwa 200 ns nach der Aktivierung des Signals $\overline{\text{RAS}}$ wird die Zeilenadresse am A3 gesperrt ($\overline{\text{CS1}} = \text{high}$; $\text{CS2} = \text{low}$ bei high-Pegel des Signals $\overline{\text{RFSH}}$) und der Adreßtreiber für die Adreßleitungen AB7 ... AB13 freigegeben und damit die Spaltenadresse an die RAM-Bausteine geschaltet.

Über das Nor A18/12 wird das Spaltenansteuersignal $\overline{\text{CAS}}$ (column adress strobe) gebildet. Durch eine UND-Verknüpfung von CAS mit den aus AB14 und AB15 decodierten Signalen wird diese Spaltenadresse nur an einer der 16 KByte-RAM-Gruppen wirksam (A11 und A13/12).
Beim Lesezyklus steuert das Signal $\overline{\text{CAS}}$ über die Schaltkreise A15/6; A21/6 die Umschaltung der bidirektionalen Treiber A9 und A17 in Abhängigkeit vom Steuersignal $\overline{\text{RD}}$. Während des Schreibzyklus liegt das Signal $\overline{\text{WR}}$ direkt als $\overline{\text{WE}}$ aktiv mit low an den RAM-Bausteinen an.

11.4.4. RFSH-Steuerung durch die ZVE

Zur Sicherung der gespeicherten Daten muß innerhalb von 2 ms jedes Bits des dynamischen RAM-Speichers aufgefrischt werden.
Das Auffrischen erfolgt in der sogenannten RAS-only-Betriebsart, d. h. alle 128 Zeilen sämtlicher RAM-Bausteine müssen nacheinander adressiert werden, um den gesamten Speicherinhalt aufzufrischen.
Dazu erzeugt die ZVE in jedem M1-Zyklus einen $\overline{\text{RFSH}}$-Impuls, d. h. in jedem M1-Zyklus werden 128 Zeilen jedes RAM-Chips aufgefrischt. Bei der STE K 3526.10/30/50 entfällt der Zähler A12/A14, der Taktgenerator A13/A23 und das Register A8.
Die RFSH-Ansteuerung erfolgt von außen, d. h. über die Adreßleitungen AB0 ... AB6 vom RFSH-Register der ZVE und das Signal $\overline{\text{RFSH}}$ aktiv low (über den BUS) über die Register A3 und A16.

11.4.5. Interne RFSH-Steuerung bei Stromausfall

Die Brücke W1:6 ist geschlossen.
Beim Einschalten der Anlage wird für 2 ms das Signal $\overline{\text{RESET}}$ auf low geschaltet und dann wieder auf high. Das FF A20 wird gesetzt. Ausgang 5 des FF's gibt den Steuersignaltreiber A16 am Eingang 13 (CS2) frei und sperrt mit dem Ausgang 6 (low) den Taktgenerator A13.
Im Normalfall erfolgt die RFSH-Steuerung in jedem M1-Zyklus durch die Zentraleinheit.
Durch das Signal $\overline{\text{RFSH}} = \text{low}$ werden über das Nand A21/11 der Spaltenadreßtreiber A6, über den Schaltkreis A18/10 der Zeilenadreßtreiber gesperrt (siehe Punkt 11.4.4.) und der Treiber A8 freigegeben.
Der Zählerstand von A12/A14 wird als RFSH-Adresse über A8 (Interrupt vom A8 bewirkt das Signal $\overline{\text{RAS}}$) der Speichermatrix zur Verfügung gestellt. Mit jedem von der ZRE kommenden RFSH-Impuls wird der Zähler um 1 weitergezählt.
Ist in der Anlage ein AKU-Modul vorhanden, wird bei Stromausfall die STE durch batteriegestützte Spannungen versorgt.
Der Netzausfall wird durch das Signal $\overline{\text{RESET}} = \text{low}$ signalisiert.
Das FF A22/5/6 wird zurückgesetzt und der Taktgenerator über A13/10/11 freigegeben. Gleichzeitig sperrt low am Eingang A16/13 den Steuersignaltreiber.
Die Länge des Ausgangsimpulses des monostabilen Multivibrators A23, der mit einer ansteigenden Flanke des Taktgenerators kippt, wird bestimmt durch das RC-Glied C1:2 und R4. Der U-Impuls liegt am Eingang 12 vom Nand A19 und sperrt die Treiber A3 und A6 und aktiviert das Register A8.
Dieser Impuls taktiert auch den Zähler A12 und A14 über den Takteingang A12/05.
Der Zähler bildet jetzt die RFSH-Adresse, nicht die ZRE über den Adreßbus. Er zählt ständig alle 128 Zeilen durch, mit jedem Zähltakt um eine Stelle weiter.
Über das Register A8 gelangt die jeweils gültige Zeilenadresse an die RAM-Bausteine (die Zeilen-, Spalten- und Steuersignaltreiber sind gesperrt).
Gleichzeitig wird der Ausgang A8/23 = low und bildet das Zeilenansteuersignal $\overline{\text{RAS}}$ an den RAM-Bausteinen (A21/3 - A21/6 - A19/3 - A18/12); (A20/3 - A20/6 - A21/3 - A19/11).

11.4.6. Varianten

Diese Steckeinheit kann sowohl in den Geräten mit als auch ohne Notstromversorgung eingesetzt werden.
Außerdem ist die Abrüstung auf 48 K- und 32 K-Speicherinhalt möglich. Die Ansteuersignale für nicht bestückte Speichergruppen können durch DIL-Schalter blockiert werden.
Durch die Verwendung der Steuersignale MEMDI, MEMDI1 und MEMDI2 sind auch adreßgleiche Varianten möglich. So können zwei gleiche Gruppen mit unterschiedlichen MEMDI-Signalen den gleichen Byte Adreßraum belegen.
Mit Hilfe der DIL-Schalter ist die Verschiebung der 4 Gruppen in 16 KByte-Schritten möglich.

![[Pasted image 20260516150059.png]]
*KI generierte Interpretation der Abbildung: Das Blockschaltbild K 3526 (Abb. 8) zeigt ein komplexes Zusammenspiel aus Decodern, Adresstreibern, einer Matrix aus vier 16 KByte Gruppen, Refresh-Logik und Busumschaltung. Zentrale Elemente sind die Segmentauswahl, die RAS/CAS-Bildung sowie ein interner Zähler für die Regenerierung.*

Abb. 8
Blockschaltbild K 3526

Beachte: STE OPS K 3526.XX - LP-Index L1/B1
Bei Einsatz der Speicher-IS K 565 RU3A (statt RU3G) muß gewährleistet sein, daß die Zeit der ansteigenden Flanke des Signals MREQ bis zur nächsten fallenden Flanke mind. 200 ns vergehen, da der K 565 RU3A diese 200 ns als Zeilenerholzeit benötigt.
Entsprechend Impulsverhalten U 880 kann MREQ minimal 180 ns inaktiv sein, damit kann kein RFSH abgearbeitet werden.
Der IS A23/12 verhindert über A19/13 ein Durchschalten von MREQ vor Ablauf der 200 ns.

robotron

VEB Robotron
Buchungsmaschinenwerk
Karl-Marx-Stadt
PSF 129
Annaberger Straße 93
Karl-Marx-Stadt
DDR - 9010

Exporteur:
Robotron - Export/Import
Volkseigener
Außenhandelsbetrieb
der Deutschen
Demokratischen Republik
PSF 11
Allee der Kosmonauten 24
Berlin
DDR - 1140

831.53.01.003
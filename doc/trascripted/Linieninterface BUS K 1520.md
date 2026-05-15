
DK 681.322-181.4
Fachbereichstandard
Juli 1980

**Deutsche Demokratische Republik**
**System Mikrorechner**
**Linieninterface BUS K 1520**
**Systembus**

**TGL 37271/01**
**Gruppe 138 210**

Microcomputer System
Bus line Interface BUS K 1520
System Bus

Deskriptoren: Mikrorechner; Interface; Bus

**STANDARDSTELLE**
Verbindlich ab 1.4.1981

Dieser Standard gilt nur für die funktionelle, elektrische und konstruktive Gestaltung des Busses des Mikrorechners (MR) K 1520. Er dient als Grundlage für die Entwicklung und den Aufbau von Baugruppen, die als zentrale Steuer- und Verarbeitungseinheiten (ZVE) des MR K 1520 dienen oder mit ihnen verbunden werden sollen.

**Inhaltsverzeichnis** | **Seite**
--- | ---
1. Allgemeine Grundlagen | 2
2. Systembus, funktionelle und elektrische Bedingungen | 2
2.1. Signalbezeichnungen und Bedeutung der Signale | 2
2.2. Prioritätsbestimmende Ketten | 8
2.3. Funktionelle Anschlußbedingungen | 11
2.3.1. Bussignalspiele | 11
2.3.2. Refresh-Steuerung auf dem Systembus | 19
2.3.3. Benutzung und Generierung des Signals /RDY | 20
2.3.4. Adressierung von Ein- und Ausgabegeräten (E/A-Geräten) | 21
2.4. Elektrische Bedingungen | 21
2.4.1. Signalpegel | 21
2.4.2. Signallastbedingungen | 21
2.4.3. Zeitbedingungen | 24
2.5. Konstruktive Bedingungen | 27

Fortsetzung Seite 2 bis 30

Verantwortlich/bestätigt: 25.7.1980, VEB Kombinat Robotron, Dresden

*berichtigte Nachauflage 11.87 in der Fassung der ab 1. 6. 1988 verbindlichen 1. Änderung (GBl. SDr. ST 1103)

Seite 2 TGL 37271/01

**1. Allgemeine Grundlagen**

Der Bus des MR K 1520 wird durch 2 Bündel Signalleitungen sowie die Leitungen zur Stromversorgung der Baugruppen gebildet.

Das erste Leitungsbündel umfaßt die zum Systemaufbau unbedingt erforderlichen Hauptsignale und wird als Systembus bezeichnet. Er bildet die gemeinsame Verbindung zwischen ZVE, Speichern (statische und dynamische RAM, ROM) und E/A-Einheiten zur Durchführung des Informationsaustausches zwischen diesen.

Er ist geeignet für durch die ZVE gesteuerte (prozessorgesteuerte) Operationen und für den direkten Speicherverkehr (DMA-Operationen). An den Bus anschließbar sind eine ZVE und mehrere DMA-Einheiten.

Das zweite Leitungsbündel wird als Koppelbus bezeichnet und umfaßt Signale, die abhängig von der Spezifik der Baugruppen vorhanden sind und die Kopplung mehrerer Rechner zu einem Mehrrechnersystem ermöglichen, sowie die Zeittakt- und Steuersignale der Echtzeituhr und Stromversorgungs- und Überwachungsleitungen. Seine freien Steckverbinderanschlüsse (Pins) können in der Rückverdrahtung zur Herstellung spezifischer Verbindungen zwischen den Steckeinheiten benutzt werden. Die Signale des Koppelbusses werden in diesem Standard nicht festgelegt.

**2. Systembus, funktionelle und elektrische Bedingungen**

**2.1. Signalbezeichnung und Bedeutung der Signale**

Der Systembus des MR K 1520 wird durch die in Tabelle 1 zusammengestellten Leitungsgruppen gebildet.

Bedeutung der einzelnen Signale siehe Tabelle 2

**Tabelle 1**

Leitungsgruppe | Anzahl Pins | Bezeichnungen
--- | --- | ---
Datenbus | 8 | DB0 bis DB7
Adreßbus | 16 | AB0 bis AB15
Steuerbus | 19 | /MREQ, /IORQ, /RD, /WR, /RFSH, /M1, /HALT, /BUSRQ, /INT, /NMI, /WAIT, /RDY, /RESET, /MEMDI, /IODI, /IEI, /IEO, /BAI, /BAO
Takt | 1 | TAKT
Stromversorgung | 14 | 5 P, 5 PG, 5 N, 12 P, 00

TGL 37271/01 Seite 3

**Tabelle 2**

Signalneme | Signalbedeutung
--- | ---
DB0 bis DB7 | Datenbus: Leitungen führen beim Datenaustausch die Befehls- bzw. Dateninformation.
AB0 bis AB15 | Adreßbus: Leitungen führen die Adresse für den Informationsaustausch mit dem Speicher bzw. E/A-Gerät.<br><br>AB0 bis AB15 sind mit /MREQ als Speicheradresse gültig (64 KByte).<br><br>AB0 bis AB7 sind mit /IORQ als E/A-Geräteadresse gültig (256 Eingabe- und 256 Ausgebeadressen).<br><br>AB8 bis AB15 sind mit /IORQ gültig und enthalten bei Ein- und Ausgabebefehlen den Inhalt vom ZVE-Register A bzw. B (je nach Befehlsart) bzw. den Inhalt des oberen Byteregisters des DMA-Kanals.<br><br>AB0 bis AB6 sind mit /RFSH als Refreshadresse für das Auffrischen dynamischer RAM gültig.
/MREQ | Speichertransfergesuch: Signal zeigt an, daß der Adreßbus eine gültige Adresse für eine Speicherlese- bzw. -schreiboperation führt.
/IORQ | Ein-/Ausgabetransfergesuch: Signal zeigt an, daß der Adreßbus im unteren Byte (AB0 bis AB7) eine gültige E/A-Geräteadresse führt. Das Signal wird von der ZVE auch erzeugt, wenn ein Interruptgesuch von der ZVE akzeptiert wurde und der neue Befehl bzw. Interruptvektor auf den Datenbus gelegt werden darf. (siehe /M1)
/RD | Lesen: Signal zeigt an, daß durch die ZVE bzw. den DMA-Kanal Informationen (Daten oder Befehle) vom Speicher bzw. E/A-Gerät gelesen werden sollen.
/WR | Schreiben: Signal zeigt an, daß von der ZVE bzw. von der DMA-Einheit gültige Daten auf den Datenbus gelegt wurden, die im Speicher einzutragen bzw. vom E/A-Gerät zu übernehmen sind.

Fortsetzung der Tabelle Seite 4

Seite 4 TGL 37271/01

**Fortsetzung der Tabelle 2**

Signalname | Signalbedeutung
--- | ---
/RFSH | Auffrischen: Signal zeigt an, daß die unteren 7 Bit des Adreßbusses eine Refreshadresse zum Auffrischen dynamischer RAM bilden. In Verbindung mit /MREQ kann dieses Signal benutzt werden, jeweils eine Refresh-Leseoperation durchzuführen.
/M1 | Befehlslesezyklus: Signal zeigt an, daß der laufende ZVE-Zyklus ein Befehlslesezyklus des auszuführenden Befehls ist. In Verbindung mit dem Signal /IORQ zeigt es an, daß ein Interruptgesuch akzeptiert wurde und der Interruptvektor bzw. Interruptbefehl auf den Datenbus zu legen ist.
/HALT | ZVE-Halt: Das Signal zeigt an, daß sich die ZVE im Halt-Zustand befindet und zur weiteren Arbeit auf ein Interrupt wartet.
/BUSRQ | Gesuch auf direkten Speicherverkehr: Das Signal zeigt der ZVE an, daß ein Gesuch auf direkten Speicherverkehr durch eine DMA-Einheit gestellt wurde. Das Signal besitzt bezüglich der ZVE-Unterbrechung höchste Priorität.
/INT | Maskierbares Interruptgesuch: Signal kennzeichnet ein Interruptgesuch eines peripheren Gerätes an die ZVE.
/NMI | Nichtmaskierbares Interruptgesuch: Signal kennzeichnet ein Interruptgesuch an die ZVE. Es besitzt höhere Priorität als /INT und wird unabhängig vom Zustand des ZVE-internen Interrupt-Enable-Flip-Flops akzeptiert. (Diese Leitung sollte vorzugsweise für Netzausfallinterrupt benutzt werden)

Fortsetzung der Tabelle Seite 5

TGL 37271/01 Seite 5

**Fortsetzung der Tabelle 2**

Signalname | Signalbedeutung
--- | ---
/WAIT | Warten: Signal zeigt der ZVE an, daß der Speicher bzw. das E/A-Gerät nicht für einen Datenaustausch "bereit" ist. (Die ZVE tritt solange in einen Wartezustand ein, wie das Signal aktiv ist.)
/RDY | Bereit: Signal zeigt an, daß der angesprochene Speicher oder das angesprochene E/A-Gerät am Bus vorhanden ist und für Lese- oder Schreiboperationen zur Verfügung steht.
/RESET | Rücksetzen: Signal dient als zentrales Rücksetzsignal im Rechner.
/MEMDI | Speichersperrung: Das Signal dient zur Sperrung aller Speichermodule für Lese- und Schreiboperationen. Die Signale /MREQ und /M1 werden davon nicht beeinflußt. Das Signal wird bei Simulation des Speichers durch z. B. eine Bedieneinheit bzw. einen externen Speicher oder zur ZVE-unabhängigen Beendigung eines Speicherzyklusses benutzt.
/IODI | Ein-/Ausgabesperre: Das Signal dient zur Sperrung aller Ein-/Ausgabe-Baugruppen für Schreib- und Leseoperationen. Die Signale /IORQ und /M1 werden davon nicht beeinflußt. Das Signal wird bei der Simulation des E/A-Systems verwendet. Die Verbindung des Signales auf der Steckeinheit der höchsten Prioritätsstufe mit IEI verhindert zusätzlich die automatische Eingabe von Interruptvektoren bzw. -befehlen nach Interruptanerkennung und erlaubt ihre Handeingabe.
/IEI | Interrupt-Freigabe-Eingang: Das Signal kennzeichnet, daß die Funktionseinheiten mit der höheren Priorität sich nicht im Interrupt-Behandlungsstatus befinden oder bei Interruptanerkennung kein Interrupt anfordern.

Fortsetzung der Tabelle Seite 6

Seite 6 TGL 37271/01

**Fortsetzung der Tabelle 2**

Signalname | Signalbedeutung
--- | ---
/IEO | Interrupt-Freigabe-Ausgang: Bedeutung wie /IEI, jedoch mit Einbeziehung der vorliegenden Steckeinheit. Die Leitung ist direkt mit /IEI der nachfolgenden Steckeinheit zu verbinden.
/BAI und /BAO (/BUSAK) | Anerkennung des direkten Speicherverkehrs (Eingang/Ausgang): Die Signale /BAI und /BAO bilden am Bus eine prioritätsbestimmende Kette zur Durchschaltung des Signals /BUSAK. Dabei bildet /BAI das Eingangssignal (/BUSAK-Input) und /BAO (/BUSAK-Output) das weitergeleitete Anerkennungssignal am Ausgang der Steckeinheit (siehe Bild 1). /BUSAK ist ein von der ZVE erzeugtes Signal, mit dem sie anzeigt, daß /BUSRQ akzeptiert wurde und Datenbus, Adreßbus und 3-state-Steuerausgänge in den hochohmigen Zustand geschaltet sind.
TAKT | Systemtakt: Die zugeordnete Leitung führt den zentralen Systemtakt. Er entspricht dem Grundtakt der ZVE und steht den Busteilnehmern zur zeitlichen Synchronisation interner Vorgänge zur Verfügung.
5P | +5V ±0,25V; Versorgungsspannung ungestützt
5PG | +5V ±0,25V im Betriebsfall; ≥ 2V bei externer Batteriestützung von RAM-speichern im Zustand Datensicherung nach Netzspannungsausfall; wenn keine externe Speicherstützung erfolgt, ist "5PG" mit 5P zu verbinden.
5N | -5V ±0,25V; Versorgungsspannung ungestützt
12P | +12V ±0,6V; Versorgungsspannung ungestützt
00 | Zentrales Bezugspotential (Masse)

TGL 37271/01 Seite 7

**Bemerkungen zu den Signalen des Systembusses**

Die in Tabelle 2 angegebenen Signalnamen entsprechen der Positivlogik ("1"=High, "0"=Low), die Signalbedeutungen beziehen sich auf die folgenden Pegel (aktive Pegel):

- Daten- und Adreßbus: High
- Negiert auf den Bus geführte Steuersignale: "Low"
- Systemtakt: TTL-Pegel

Der Datenbus ist bidirektional: Am Datenbus angeschlossene Sender müssen 3-state-Ausgänge besitzen.

Der Adreßbus sowie die Leitungen /MREQ, /IORQ, /RD, /WR sind unidirektional (vorausgesetzt, sie werden nicht mit bidirektionalen Leitungen - z. B. in DMA-Baugruppen - kombiniert). Die die Busherrschaft besitzende Einheit (z. B. die ZVE oder eine der DMA-Einheiten) ist Sender. Die Sender müssen 3-state-Ausgänge besitzen.

Die Leitungen /RFSH, /M1, /HALT sind unidirektional, die die Busherrschaft besitzende Einheit (z. B. die ZVE) kann Sender sein; die Sender müssen 3-state-Ausgänge besitzen.
Leitungen von 3-state-Ausgängen müssen auch bei hochohmigen Sendern definierte Zustände aufweisen (durch z. B. Widerstandsbeschaltung). Die Leitungen /BUSRQ, /INT, /NMI, /WAIT, /RDY, /RESET, /MEMDI, /IODI sind Sammelleitungen. Die Sender müssen Open-Kollektor-Stufen besitzen.

Empfänger sind:
- für /INT, /NMI die ZVE,
- für /WAIT, /BUSRQ die ZVE oder die DMA-Einheiten,
- für /RDY die auswertenden Funktionseinheiten, wie Buszeitüberwacher, Steuerungen der Übertragungseinrichtung o. ä.
- für /RESET alle Funktionseinheiten,
- für /MEMDI die Speichereinheiten,
- für /IODI die Ein- bzw. Ausgabeeinheiten.

Bei der Anschaltung mehrerer Einheiten, die /MEMDI und /IODI erzeugen, an den Systembus müssen zusätzliche Maßnahmen in diesen Einheiten getroffen werden, die gewährleisten, daß bezüglich der Bildung dieser Signale und der Beschaltung des Datenbusses nur eine Einheit aktiv ist.

Die Leitung TAKT ist unidirektional und nur ein Sender ist zulässig. Die Ketten /IEI und /IEO und /BAI und /BAO sind unidirektional (siehe Abschnitt 2.2.).

Das Signal /BAO ist auf dem ZVE-Modul und auf solchen Baugruppen, die durch Aktivierung von /BAO die Busherrschaft herstellen, durch Open-Kollektor-Stufen zu erzeugen. Derartige Baugruppen sind zwischen dem ZVE-Modul und der ersten DMA-Einheit anzuordnen, da die Weiterleitung von /BAO über die DMA-Einheit und unidirektional erfolgt.

Seite 8 TGL 37271/01

Einheit und unidirektional erfolgt.

Folgende Signale können durch Gegentakt-Ausgangsstufen (z. B. TTL-Bausteine der Serie D100/D200) erzeugt werden:
/IEO,
/BAO (/BAO nicht auf dem ZVE-Modul).

Die Toleranzangaben der Versorgungsspannungen sind auf die Verbraucher (Bauelemente) bezogen.

**2.2. Prioritätsbestimmende Ketten**

**2.2.1. Allgemeines**

Zur Festlegung der Priorität der gemeinsam über die Leitungen /INT bzw. /BUSRQ durch die einzelnen Busteilnehmer zur ZVE gesendeten Gesuche besitzt der Systembus 2 prioritätsbestimmende Kaskadierungeleitungen: /IEI und /IEO für die Prioritätszuordnung zur Leitung /INT; /BAI und /BAO für die Prioritätszuordnung zur Leitung /BUSRQ. Diese Leitungen sind entsprechend dem im Bild 1 (Blick auf Rückverdrahtungsseite) gezeigten Anschlußschema zu führen.

![[Pasted image 20260515160532.png]]
**Bild 1**

**2.2.2. Prioritätskette /IEI und /IEO**

Durch die im Bild 1 gezeigte kettenförmige Verdrahtung der Signale /IEI und /IEO wird erreicht, daß, sofern die für die Signale geltenden Wirkungspegel gewährleistet werden, mit wachsendem Abstand von der Baugruppe höchster Priorität die Priorität eines Busteilnehmers, das ist jede am Systembus anschließbare Baugruppe, sinkt.

37271/01 Seite 9

Zur Sicherung der Funktion der Kette ist dazu durch jeden Businterruptteilnehmer, das ist jeder am Interruptverkehr beteiligter Busteilnehmer, folgendes zu gewährleisten.

**a) Sicherung des Wirkungsablaufes für Interruptanerkennung**

Anmeldung eines Interruptgesuches durch Aktivierung der Leitung /INT. Dies kann durch jeden Businterruptteilnehmer erfolgen, sofern er ein aktives /IEI empfängt. Die ZVE signalisiert die Anerkennung durch die Ausführung eines Interrupt-Anerkennungszyklusses. Dabei erwartet die ZVE die Bereitstellung eines 8-bit-Interruptvektors bzw. Interruptbefehls auf dem Datenbus.

Dieser Interruptvektor bzw. Interruptbefehl darf jedoch nur durch den Interrupt-Anmelder mit der momentan höchsten Priorität gesendet werden. Sie wird über die Leitung /IEI und /IEO ermittelt. Dazu gilt die Bedingung, daß durch jeden (um Unterbrechung ersuchenden) Interrupt-Anmelder gleichzeitig mit dem Aktivieren des Signals /INT das Signal /IEO zu passivieren (/IEO -> High) ist.

Ein Interrupt-Anmelder besitzt dann die höchste Priorität, wenn sein Eingangssignal /IEI aktiv (/IEI = Low) und sein Ausgangssignal /IEO passiv ist. Dabei sind die unter Abschnitt 2.2.2. b) bzw. 2.3. festgelegten Zeitbedingungen einzuhalten.

**b) Schnelles Durchschalten der Low- nach High-Flanke von /IEI nach /IEO**

Zur Sicherung, daß die Prioritätserermittlung zwischen den Businterruptteilnehmern abgeschlossen ist, bevor ein Interruptvektor auf dem Bus gelegt wird, ist zu gewährleisten, daß das Signal /IEI anteilig durch jeden Businterruptteilnehmer innerhalb von max. 20 ns an seinem Ausgang /IEO auf High geschaltet wird (Gesamtzahl Businterruptteilnehmer ≤ 20). Es wird empfohlen, dazu die Schaltung nach Bild 2 zu nutzen.

Durch die Signale /IEI1 und /IEO1 wird auf Bild 2 mit Bezug auf Tabelle 12 die Möglichkeit angedeutet, eine zweite Prioritätenkette zu realisieren, die mit der ersten Prioritätenkette (mit /IEI und /IEO) in Reihe zu schalten ist und die Unterbringung von Businterruptteilnehmern unterschiedlicher Priorität in einer Baugruppe ermöglicht.

![[Pasted image 20260515160624.png]]

**Bild 2**

Seite 10 TGL 37271/01

**c) Erkennen des RETI-Befehls und Aktivieren des Signals /IEO**

Bei Anerkennung eines Interruptgesuches ist das Signal /IEO durch den bedienten Businterruptteilnehmer solange auf High zu halten, bis durch einen RETI-Befehl (Rückkehr vom Interrupt) das Unterbrechungsbehandlungsprogramm abgeschlossen wird.
Dieser für die ZVE gültige Befehl ist durch den Businterruptteilnehmer zu erkennen und als Folge sein Ausgang /IEO zu aktivieren, falls es der Zustand der vorangehenden Kette zuläßt. Dabei sind die unter Abschnitt 2.2.2.d) bzw. Abschnitt 2.3. festgelegten Zeitbedingungen einzuhalten.

**d) Schnelles Durchschalten der High- nach Low-Flanke von /IEI nach /IEO zwischen 1. und 2. Byte des RETI-Befehls**

Es ist zu gewährleisten, daß die Durchschaltung für alle Businterruptteilnehmer in einer Gesamtzeit (falls diese Zeit nicht durch eingefügte WAIT-Zyklen verlängert ist) von $4 T_{cp} - t_{DRD} (/IEO) - t_{SRD} (/IEI)$ erfolgt, mit:

- $t_{DRD} (/IEO)$ = Verzögerung /IEO -> Low des ersten Businterruptteilnehmers nach /RD -> High beim Erkennen des 1. RETI-Bytes,
- $t_{SRD} (/IEI)$ = Voreinstellzeit von /IEI des letzten Businterruptteilnehmers vor /RD -> High beim Erkennen des 2. RETI-Bytes,

wobei die beiden zu subtrahierenden Zeiten schaltkreisspezifisch zu ermitteln sind. Bei Ketten ab im allgemeinen 4 Businterruptteilnehmern wird daher empfohlen, die Durchschaltung durch schaltungstechnische Maßnahmen (aus dem 1. Byte des RETI-Befehls gebildetes Signal /IEP, siehe Bild 2 und Tabelle 12) zu beschleunigen.

Der /IEO-Anschluß der letzten Baugruppe der Kette bleibt offen. Auf Baugruppen, die die Signale /IEI und /IEO nicht benutzen, sind die Anschlüsse durch Kurzschlußbrücken auf der Steckeinheit zu verbinden.

**2.2.3. Prioritätskette /BAI und /BAO**

Zur Prioritätszuweisung bei DMA-Verkehr dienen die Signale /BAI und /BAO. Sie werden am Bus kettenförmig verdrahtet.

Die Kette schaltet das Anerkennungssignal /BUSAK aus der ZVE (Anfang der Prioritätskette) zur DMA-Einrichtung der momentan höchsten Priorität durch.
Es ist folgendes Regime durch die DMA-Einheiten zu gewährleisten:

Jede DMA-Einheit kann ein Gesuch auf Busherrschaft zeitlich unabhängig an die ZVE stellen unter den Bedingungen, daß momentan kein durch die ZVE akzeptierter DMA-Verkehr (/BUSRQ und /BAI schon aktiv) auf dem Bus stattfindet.

TGL 37271/01 Seite 11

Die ZVE akzeptiert das Gesuch durch Senden des Signals /BUSAK über /BAI und /BAO.
Dieses Signal ist durch jede DMA-Einheit an ihren Ausgang /BAO durchzuschalten, sofern sie nicht selbst /BUSRQ gesetzt hat. Im anderen Fall ist die Weiterleitung zu unterbrechen.
Es sind die unter Abschnitt 2.3. festgelegten Zeitbedingungen einzuhalten.

Werden Anschlußsteuerungen oder Speicher räumlich am Bus zwischen DMA-Einheiten eingefügt, sind auf diesen die Anschlüsse /BAI und /BAO kurzzuschließen.

**2.3. Funktionelle Anschlußbedingungen**

**2.3.1. Bussignalspiele**

**2.3.1.1. Allgemeines**

Bei den Bussignalspielen wird zwischen ZVE-gesteuerten und DMA-gesteuerten Transfers sowie einem Busgesuchs-/Busanerkennungszyklus unterschieden. In den Bildern 3 bis 10 sind die prinzipiell einzuhaltenden Zeitabläufe dargestellt. Gestrichelte Linien in den Bildern bedeuten, daß das Signal so verlaufen kann, jedoch nicht ausgewertet wird. Bild 11 enthält in Verbindung mit Tabelle 10 die genauen Zeitbedingungen.
Es werden folgende ZVE-gesteuerte Zyklen unterschieden:

Befehlslesezyklus, Speicherdatenschreib- oder -Lesezyklus, Ein/Ausgabezyklus, Interruptzyklen.
Im DMA-Verkehr sind zu unterscheiden: Speicher - Speicher-Transferzyklus, Speicher - E/A-Gerät-Transferzyklus, E/A-Gerät - E/A-Gerät-Datentransfer.

Die Zeitbedingungen sind auf der Taktzykluszeit von 407 ns aufgebaut. Zwischen Systemtakt und Schaltkreissteuertakt auftretende Verzögerungen sind in den Zeitbedingungen berücksichtigt.

**2.3.1.2. ZVE-gesteuerte Bussignalspiele**

**2.3.1.2.1. Befehlslesezyklus (Bild 3)**

Ein Befehlslesezyklus besteht aus minimal 4 Taktperioden. In den Perioden T1 und T2 erfolgt der Aufruf des zugeordneten Befehlsbytes im über den Adreßbus adressierten Speicherplatz und Einlesen in die ZVE. Die ZVE kennzeichnet diesen Zyklus durch Aktivierung der Signale /M1, /MREQ und /RD in der Periode T1. In Periode T2 wird über die Leitung /WAIT die Bereitstellung der Daten auf dem Datenbus abgefragt und unter der Bedingung, daß kein "WAIT" vorliegt, die Datenbusinformation in die ZVE eingelesen.

Entsprechend Bild 9 ist über die Leitung /WAIT eine Zyklusverlängerung zur Zeitsynchronisation zwischen Speicher und ZVE möglich. Unter der Bedingung /WAIT aktiv während des Abfragezeitraums fügt die ZVE automatisch zusätzliche Wartezyklen ein, bis die Bereitstellung der Daten mit Rücksetzen des Signals /WAIT auf dem Bus angezeigt wird.

Seite 12 TGL 37271/01

In den Taktperioden T3 und T4 stellt die ZVE auf dem Adreßbus eine Refresh-Adresse zum Auffrischen dynamischer RAM zur Verfügung.
Der Refresh-Zeitraum wird durch das Signal /RFSH gekennzeichnet, das Signal /MREQ kennzeichnet die Operation als Speicheroperation (siehe Abschnitt 2.3.2.).

Während des Lesens des Befehlsbytes wird beim adressierten Speicher das Signal /RDY erzeugt (siehe Abschnitt 2.3.3.).

![[Pasted image 20260515160735.png]]
*(Abbildung: Bild 3 Befehlslesezyklus mit Signalen TAKT, AB0-AB15, /M1, /MREQ, /RD, /WAIT, DB0-DB7, /RFSH, /RDY)*
**Bild 3**

**2.3.1.2.2. Speicherdaten-Schreib- oder -Lesezyklus (Bild 4)**

Speicherschreib- bzw. Speicherlesezyklen bestehen minimal aus je 3 Taktperioden. Sie sind gekennzeichnet durch die Aktivierung der Signale /MREQ und /RD beim Lesezyklus bzw. /MREQ und /WR beim Schreibzyklus.
Wie beim Befehlslesezyklus wird beim Speicherlesezyklus während der Taktperiode T2 über die Leitung /WAIT die Datenbereitstellung auf dem Bus geprüft. Die Übernahme der Daten in die ZVE erfolgt in der Taktperiode T3.

TGL 37271/01 Seite 13

Beim Speicherdatenschreibzyklus erfolgt in der Taktperiode T2 über die Leitung /WAIT die Abfrage auf Schreibbereitschaft.

![[Pasted image 20260515160805.png]]
*(Abbildung: Bild 4 Speicherlesezyklus und Speicherschreibzyklus)*
**Bild 4**

**2.3.1.2.3. Ein/Ausgabezyklus (Bild 5)**

Ein/Ausgabezyklen bestehen minimal aus 4 Taktperioden (einschließlich eines durch die ZVE automatisch eingeführten Wartezyklusses).

Sie sind gekennzeichnet durch Aktivierung der Signale /IORQ und /RD bei Ausführung eines Lesezyklusses bzw. /IORQ und /WR bei Ausführung eines Schreibzyklusses. Die Abfrage der Leitung /WAIT erfolgt in der Taktphase TW. Unter der Bedingung, daß /WAIT nicht aktiv ist, übernimmt die ZVE in der Taktperiode T3 die auf dem Datenbus anliegenden Daten beim Eingabezyklus. Wie bei Speichertransfers kann über die Leitung /WAIT erreicht werden, daß die ZVE zusätzliche WAIT-Taktperioden TW in den Zyklus einfügt.
(Bezüglich E/A-Adressierung siehe Abschnitt 2.3.4.).

Seite 14 TGL 37271/01

![[Pasted image 20260515160841.png]]
*(Abbildung: Bild 5 E/A-Datentransferzyklus Lesen/Schreiben)*
**Bild 5**

**2.3.1.2.4. Interruptanerkennungszyklen**

Nach Abschnitt 2.2. sind über den Systembus 2 Interruptarten realisierbar. Die zeitliche Reaktion der ZVE ist dargestellt für:

- maskierbares Interrupt: Bild 6
- nichtmaskierbares Interrupt: Bild 7

**a) Maskierbares Interrupt**

Der Interruptanerkennungszyklus wird von der ZVE durch die Akzeptierung eines am Ende des letzten Zyklusses einer Befehlsabarbeitung von einem Businterruptteilnehmer gesendeten Signals /INT eingeleitet. Die Interruptanerkennung wird über die Leitungen /M1 und /IORQ auf dem Bus angezeigt. Die ZVE erwartet die Eingabe eines Bytes über den Datenbus. Um ausreichende Schaltzeiten für die Prioritätskette /IEI und /IEO zu sichern, fügt die ZVE automatisch nach den Taktperioden T1 und T2 2 WAIT-Zyklen ein. Im 2. WAIT-Zyklus wird über die Leitung /WAIT geprüft, ob die Daten auf dem Datenbus anliegen. Unter der Bedingung, daß /WAIT nicht aktiv ist, werden Daten in die ZVE am Ende des 2. WAIT-Zyklus übernommen.

TGL 37271/01 Seite 15

Über die prioritätsbestimmende Interruptkette ist zu sichern, daß bis zur Aktivierung von /IORQ der Pegel des Signals /IEO bis zum Kettenende durchgeschaltet ist.
Während einer Interruptanerkennung (/M1 aktiv) darf von den Businterruptteilnehmern kein neues Interruptgesuch gestellt werden.
Das einzulesende Byte ist durch den momentan höchstpriorisierten um Interrupt ersuchenden Businterruptteilnehmer auf den Bus zu legen.

Während der Taktperioden T3 und T4 führt die ZVE einen Refresh-Zyklus wie beim Befehlslesezyklus durch.

![[Pasted image 20260515160922.png]]
*(Abbildung: Bild 6 Interruptanerkennungszyklus)*
**Bild 6**

**b) Nichtmaskierbares Interrupt**

Ein nichtmaskierbares Interrupt wird durch Aktivierung der Leitung /NMI eingeleitet. Es wird von der ZVE am Ende des letzten Zyklusses eines Befehls anerkannt.
Der nachfolgend ablaufende 1. ZVE-Zyklus entspricht einem Befehlslesezyklus (Abschnitt 2.3.1.2.1.), wobei die Datenbusinformation ignoriert wird und die ZVE ein Unterprogramm auf der Adresse 0066H aufruft.

1 Der gesamte Zyklus bis zur Abarbeitung des ersten Befehls der INT-Routine dauert 19 Taktzyklen.

Seite 16 TGL 37271/01

Eine NMI-Interruptbehandlung darf durch ein Busgesuch (BUSRQ) unterbrochen werden.

![[Pasted image 20260515160959.png]]
*(Abbildung: Bild 7 Interruptanerkennungszyklus NMI)*
**Bild 7**

**2.3.1.2.5. Eintrittszyklus/Austrittszyklus aus dem HALT-Zustand (Bild 8)**

Nach Empfangen eines HALT-Befehls (Befehlslesezyklus) tritt die ZVE am Ende des Zyklusses in den HALT-Zustand ein.
Der HALT-Zustand auf dem Systembus ist durch das Signal "HALT" gekennzeichnet. Die ZVE führt laufend Befehlslesezyklen auf der nach dem HALT-Befehl folgende Adresse durch, ohne jedoch den Befehl weiter abzuarbeiten.
Dieser Zustand bleibt erhalten bis ihn die ZVE nach Empfang eines Interrupts am Ende eines Befehlslesezyklusses aufhebt oder /RESET aktiv wird.
Als Quittung wird das mit dem Eintritt in den HALT-Zustand aktivierte Statussignal /HALT gelöscht.

**2.3.1.2.6. Zeitsynchronisation auf dem Bus durch WAIT-Zyklen (Bild 9)**

Der Systembus bietet die Möglichkeit, ZVE-Geschwindigkeit und Speicher- bzw. E/A-Zugriffsgeschwindigkeiten miteinander zu synchronisieren.
Die Synchronisation hat mit Hilfe der Leitung /WAIT zu erfolgen. In den beschriebenen Zyklen (Abschnitt 2.3.1.2. bis 2.3.1.5.)

TGL 37271/01 Seite 17

![[Pasted image 20260515161031.png]]
*(Abbildung: Bild 8 Halt-Zyklus und Austritt aus dem Halt)*
**Bild 8**

![[Pasted image 20260515161055.png]]
*(Abbildung: Bild 9 Zyklus mit WAIT)*
**Bild 9**

Seite 18 TGL 37271/01

wird der Zustand dieser Leitung durch die ZVE jeweils zu einem bestimmten Zeitpunkt abgefragt.
Solange das Signal /WAIT zum Abfragezeitpunkt aktiv ist, fügt die ZVE automatisch zusätzliche Taktzyklen TW ein. Erst mit Rücksetzen des Signals /WAIT erwartet die ZVE die Bereitstellung von Informationen auf dem Datenbus bzw. schließt einen Schreibzyklus ab.
Die Synchronisation gelingt somit, wenn die adressierte Baugruppe /WAIT solange aktiviert, bis die Übergabe- bzw. Übernahmebereitschaft erreicht ist.

**2.3.1.3. Signalspiele bei Busgesuch, Busanerkannung und Busrückgabe**

Für den DMA-Verkehr auf dem Systembus ist zu sichern, daß Transfers sowohl zwischen Speicherbereichen als auch zwischen Speicher und E/A-Gerät möglich sind.
Ein Gesuch auf Busherrschaft (BUSRQ) kann unabhängig vom Buszustand unter den Bedingungen /BUSRQ und /BAI nicht aktiv (durch anderen DMA-Modul noch nicht aktiviert) und unter Einhaltung der in Tabelle 10 angegebenen Voreinstell- und Haltezeit gestellt werden.
Ein über die Leitung /BUSRQ gestelltes Gesuch an die ZVE wird am Ende eines ZVE-Zyklusses (auch innerhalb eines Befehls) durch Aktivierung der Leitung /BUSAK bzw. /BAI und /BAO anerkannt. Der die Busanforderung stellende DMA-Modul unterdrückt die Weiterleitung des Anerkennungssignals nach /BAO.
Als Folge werden gleichzeitig die Adreß-, Daten- und Steuerleitungen der ZVE in den hochohmigen Zustand geschaltet. Der Bus steht dem DMA-Modul zur Verfügung, sobald er auf seine gestellte Busanforderung das aktivierte Anerkennungssignal /BAI empfängt.
Der Zustand bleibt erhalten, solange /BUSRQ aktiv ist. Nach Deaktivierung von /BUSRQ wird BUSAK zurückgenommen und die Programmabarbeitung fortgesetzt. Dabei ist durch die Wahl des Abschaltzeitpunktes des Signals /BUSRQ und des Zeitpunktes für die Durchschaltung der Kette /BAI-/BAO zu gewährleisten, daß sich zum möglichen Zuschaltzeitpunkt der ZVE bzw. eines weiteren DMA-Moduls die Adreß-, Daten- und Steuerleitungen des die Busherrschaft abgebenden DMA-Moduls im hochohmigen Zustand befinden. Den Zeitpunkt der Busrückgabe bestimmt allein der die Bus-

![[Pasted image 20260515161150.png]]
*(Abbildung: Bild 10 DMA Busherrschaft Übergabe)*
**Bild 10**

TGL 37271/01 Seite 19

herrschaft besitzende DMA-Modul.
Bei Vorhandensein dynamischer Speicher am Bus muß durch hardware-technische oder software-technische Mittel gesichert werden, daß jede Speicherzeile eines solchen RAM alle 2 ms aufgefrischt wird (siehe Abschnitt 2.3.2.).
Werden mehrere DMA-Module am Bus betrieben, so sind diese über die Leitung /BAI und /BAO in ihrer Priorität zu verketten. Dies sichert bei gleichzeitiger Busanforderung mehrerer DMA-Module eine prioritätsgerechte Arbeit. Im Gegensatz zur Interruptprioritätenkette erlaubt die /BAI-/BAO-Kette keine Busanforderung eines höherpriorisierten DMA-Moduls, wenn ein niederpriorisierter DMA-Modul die Busherrschaft besitzt.

**2.3.1.4. Bussignalspiele bei DMA-Operationen**

Für die Durchführung eines DMA-Betriebes ist Bedingung, daß die um Busherrschaft ersuchende DMA-Steuereinheit /BUSRQ gesetzt hat und eine Zuweisung über /BUSAK bzw. /BAI und /BAO durch die ZVE erfolgt ist. Die auf dem Systembus möglichen Signalspiele entsprechen den unter Abschnitt 2.3.1.1. für den DMA-Verkehr unterschiedenen Transferszyklen.

Für die Gewinnung der Busherrschaft (Aktivieren /BUSRQ) gelten die unter Abschnitt 2.3.1.3. genannten Bedingungen.
Bei Vorhandensein dynamischer RAM-Baugruppen am Systembus ist zu gewährleisten, daß alle 2 ms ein Auffrischen aller Speicherzellen erfolgt. Als Regime ist dazu z. B. softwaretechnisch zu sichern, daß mindestens 400 µs von 2 ms ZVE-gesteuerte Busoperationen ablaufen (64 Befehlslesezyklen). Die Schachtelung der DMA-Zyklen und ZVE-Zyklen unterliegt keinen besonderen Bedingungen.

**2.3.1.4.1. Speicher-Speicher-Transferszyklus (Bild 4)**

Der durch die DMA-Steuereinheit auf dem Bus zu realisierende Zeitablauf muß genau dem entsprechen, der bei ZVE-gesteuerten Speicherdatentransfers realisiert wird. Ein Speicher-Speicher-Transfer soll ein Einzelzyklus (Suchoperationen) oder die Hintereinanderreihung eines Speicherlese- und Speicherschreibzyklusses sein, wobei jeder Zyklus aus mindestens 3 Taktperioden bestehen muß. Für die Zyklen gelten die in Tabelle 10 genannten Zeitbedingungen. Wie unter Abschnitt 2.3.1.2.6. beschrieben, ist zu sichern, daß über /WAIT eine Zeitsynchronisation zwischen Speicher und DMA-Steuereinheit möglich ist.

**2.3.1.4.2. Speicher-E/A-Geräte-Transferzyklus**

Das durch die DMA-Steuereinheit auf dem Bus zu realisierende Signalspiel soll in seinem Zeitablauf ein Lesezyklus oder die Hintereinanderreihung eines Speicherlese- bzw. -schreibzyklusses mit einem Ein/Ausgabezyklus sein. Für die Aktivierung der Leitungen AB0 bis AB15, /IORQ, /MREQ, /RD, /WR, den Abfragezeitpunkt des Signals /WAIT und den Datenausgabe/-übernahmezeitpunkt sind die in Tabelle 10 festgelegten Bedingungen einzuhalten.

**2.3.2. Refresh-Steuerung auf dem Systembus**

Die Zeitbedingungen der Systembusbelegung sehen vor, daß im ZVE-gesteuerten Betrieb der Informationsinhalt am Bus angeschlossener dynamischer Speichermodule durch Refresh-Zyklen (innerhalb von Befehlslesezyklen) in solchen Zeitabständen aufgefrischt wird, daß der Datenerhalt gewährleistet ist. Unter den Bedin-

Seite 20 TGL 37271/01

gungen

- ZVE im WAIT-Zustand
- DMA-Operation auf dem Bus

wird das ZVE-gesteuerte Auffrischen unterbrochen. Zum funktionssicheren Betrieb dynamischer RAM am Bus ist für die obigen Bedingungen folgendes zu gewährleisten:

**- ZVE im WAIT-Zustand**

/WAIT darf insgesamt maximal solange aktiviert bleiben, daß in 2 ms (bei 4K x 1 bit DRAM-Elementen) mindestens 64 M1-Zyklen ablaufen.
Wird /WAIT länger aktiviert, muß der /WAIT erzeugende Busteilnehmer das Auffrischen selbst ausführen.

Dazu gilt:
Sofern /WAIT aktiv und /BUSRQ inaktiv sind, können durch Aktivierung der Leitung /BAO in der ZVE dort die Signale AB0 bis AB6, /MREQ und /RFSH hochohmig geschaltet werden.

Danach ist ein Auffrischen durchführbar. Zeitablauf und zu aktivierende Signale müssen dem eines Befehlslesezyklusses (Taktperioden T3 und T4) entsprechen. Ein Rücksetzen von /WAIT und Rückgabe der Steuerung an die ZVE darf nur nach kompletten Auffrischperioden (64/128 Refresh-Zyklen) erfolgen.

Es ist zu beachten, daß bei manchen Busteilnehmern ein durch /BAO unterbrochener Zyklus nicht normal beendet werden kann.

**- DMA-Operationen auf dem Bus**

Zur Sicherung des Auffrischens dynamischer Speicher am Systembus ist eine Zeitteilung zwischen ZVE und DMA-Steuereinheit zu gewährleisten oder die DMA-Steuereinheit muß selbst Refresh-Zyklen erzeugen. Bei Zeitteilung sind der ZVE durch programmtechnische Mittel mindestens 64 M1-Zyklen innerhalb von 2 ms für die Ausführung von Refresh-Zyklen zuzuordnen.

**2.3.3. Benutzung und Generierung des Signales /RDY**

/RDY dient als Kennungssignal, daß ein adressierter Speicher oder ein adressiertes E/A-Gerät am Bus vorhanden ist und zum Datentransfer zur Verfügung steht. Es ermöglicht zusammen mit /WAIT den Aufbau eines Shake-hand-Spiels beim Datentransfer mit der ZVE.

/RDY ist aus folgenden Bedingungen zu bilden bzw. entsprechend zu kombinieren:

- durch Speicher: RDY = "Adresse erkennt" . /MEMDI . /MREQ . /RFSH;
- durch E/A-Einheiten: RDY = "Adresse erkennt" . /IODI . /IORQ . /M1;
- durch INT liefernde Teilnehmer: RDY = /IEI . /IEO . /IORQ . /M1

Auch /MEMDI und /IODI bildende Einheiten müssen /RDY generieren.

TGL 37271/01 Seite 21

Ein Shake-hand-Spiel ergibt z. B. sich mit

WAIT-"ein" = /MREQ . /RFSH ∨ /IORQ (vorderflankengesteuert)
WAIT-"aus" = /RDY ∨ TIMEOUT (zustandsgesteuert);
TIMEOUT - erzwungene Rückstellung von WAIT.

**2.3.4. Adressierung von E/A-Geräten**

Bei Ein/Ausgabezyklen führt der Adreßbus 2 gültige Adreßbytes auf den Leitungen AB0 bis AB15. Im unteren Adreßbyte (AB0 bis AB7) gibt die ZVE die E/A-Adresse aus, im oberen Byte (AB8 bis AB15) wird, abhängig vom Befehl, der Inhalt der Register A bzw. B ausgegeben.

Bei der Adressierung von Anschlußsteuereinheiten bzw. E/A-Geräten ist vorzugsweise von den in Tabelle 3 aufgeführten Bedingungen auszugehen:

**Tabelle 3**

| Adreßbit auf | Funktion |
| --- | --- |
| AB0 | vorzugsweise Portauswahl |
| AB1 | für Schaltkreisfunktionen |
| AB2 | für Subadresse auf der Steckeinheit |
| AB3 | für Kassettenauswahl (Steckeinheitensatz) |
| AB4 bis AB7 | für Moduladresse |
| Oberes Adreßbyte | Bei Bedarf als Modulsubadresse |

**2.4. Elektrische Bedingungen**

**2.4.1. Signalpegel**

Für alle logischen Bussignale müssen Sender bzw. Empfänger Pegel nach Tabelle 4 bereitstellen bzw. verarbeiten (im Betriebstemperaturbereich 0 bis 60 °C).

**Tabelle 4**

| | High | Low |
| --- | --- | --- |
| Sender | 2,4 bis 5,25 V | 0 bis 0,45 V |
| Empfänger | 2,0 bis 5,25 V | -0,3 bis +0,8 V |

**2.4.2. Signallastbedingungen**

In den universell einsetzbaren Baugruppen müssen die am Systembus als Sender und Empfänger arbeitenden Baustufen bei Einhaltung der unter Abschnitt 2.4.1. genannten Signalpegel die folgenden statischen Bedingungen garantieren:

Seite 22 TGL 37271/01

**2.4.2.1. Open-Kollektor-Stufen**

Die zugehörigen Kollektorwiderstände sind auf der ZVE-Baugruppe anzuordnen.

**Tabelle 5**

Leitungen | Stufe | Pegel | Bedingungen
--- | --- | --- | ---
| /INT und /BUSRQ | Sender | Low | $U_{OL} \leq 0,45\text{ V}$ bei $I_{OL} = 1,8\text{ mA}$
| | | High | $I_{OH} \leq 30\text{ µA}$ bei $U_{OH} = 2,4\text{ V}$
| | Empfänger | Low | $I_{IL} \geq -0,25\text{ mA}$ bei $U_{IL} = 0,45\text{ V}$
| | | High | $I_{IH} \leq 50\text{ µA}$ bei $U_{IH} = 2,4\text{ V}$
| | Widerstand | Low | $I_{RL} \geq -1,55\text{ mA}$ bei $U_{RL} = 0,45\text{ V}$
| | | High | $U_{RH} \geq 2,4\text{ V}$ bei $I_{RH} = -0,65\text{ mA}$
alle übrigen | Sender | Low | $U_{OL} \leq 0,45\text{ V}$ bei $I_{OL} = 16\text{ mA}$
| | | High | $I_{OH} \leq 50\text{ µA}$ bei $U_{OH} = 2,4\text{ V}$
| | Empfänger | Low | $I_{IL} \geq -0,4\text{ mA}$ bei $U_{IL} = 0,45\text{ V}$
| | | High | $I_{IH} \leq 50\text{ µA}$ bei $U_{IH} = 2,4\text{ V}$
| | Widerstand | Low | $I_{RL} \geq -5,5\text{ mA}$ bei $U_{RL} = 0,45\text{ V}$
| | | High | $U_{RH} \geq 2,4\text{ V}$ bei $I_{RH} = -2,2\text{ mA}$

**Maximale Belastung der Leitungen:**
/INT, /BUSRQ - 20 Sender und 1 Empfänger oder mehrere Empfänger mit einer Gesamtlast, die der in Tabelle 5 angegebenen entspricht.
Übrigen Leitungen - zusammen 45 Sender und Empfänger

**2.4.2.2. 3-State-Stufen**

**Tabelle 6**

Leitungen | Stufe | Pegel | Bedingungen
--- | --- | --- | ---
DB0 bis DB7 | Sender | Low | $U_{OL} \leq 0,45\text{ V}$ bei $I_{OL} = 15\text{ mA}$
| | | High | $U_{OH} \geq 2,4\text{ V}$ bei $I_{OH} = -5\text{ mA}$
| | | hochohmig | $|I_V| \leq 100\text{ µA}$ bei $U_O = 0,45 / 2,4\text{ V}$
| | Empfänger | Low | $I_{IL} \geq -0,4\text{ mA}$ bei $U_{IL} = 0,45\text{ V}$
| | | High | $I_{IH} \leq 40\text{ µA}$ bei $U_{IH} = 2,4\text{ V}$

Fortsetzung der Tabelle Seite 23

TGL 37271/01 Seite 23

**Fortsetzung der Tabelle 6**

Leitungen | Stufe | Pegel | Bedingungen
--- | --- | --- | ---
AB0 bis AB9 | Sender | Low | $U_{OL} \leq 0,45\text{ V}$ bei $I_{OL} = 15\text{ mA}$
| | | High | $U_{OH} \geq 3,65\text{ V}$ bei $I_{OH} = -1\text{ mA}$
| | | hochohmig | $|I_V| \leq 20\text{ µA}$ bei $U_O = 0,45 / 3,65\text{ V}$
| | Empfänger | Low | $I_{IL} \geq -0,4\text{ mA}$ bei $U_{IL} = 0,45\text{ V}$
| | | High | $I_{IH} \leq 20\text{ µA}$ bei $U_{IH} = 3,65\text{ V}$
AB10 bis AB15 Steuerbus | Sender | Low | $U_{OL} \leq 0,45\text{ V}$ bei $I_{OL} = 15\text{ mA}$
| | | High | $U_{OH} \geq 2,4\text{ V}$ bei $I_{OH} = -5\text{ mA}$
| | | hochohmig | $|I_V| \leq 100\text{ µA}$ bei $U_O = 0,45 / 2,4\text{ V}$
| | Empfänger | Low | $I_{IL} \geq -0,4\text{ mA}$ bei $U_{IL} = 0,45\text{ V}$
| | | High | $I_{IH} \leq 40\text{ µA}$ bei $U_{IH} = 2,4\text{ V}$

**Maximale Belastung der Leitungen:**
DB0 bis DB7 - 35 Sender und 35 Empfänger
AB0 bis AB9 - 15 Sender und 35 Empfänger
AB10 bis AB15, Steuerbus - 10 Sender und 35 Empfänger

**2.4.2.3. Gegentakt - Ausgangsstufen**

**Tabelle 7**

Stufe | Pegel | Bedingungen
--- | --- | ---
Sender | Low | $U_{OL} \leq 0,45\text{ V}$ bei $I_{OL} = 4\text{ mA}$
| | High | $U_{OH} \geq 2,4\text{ V}$ bei $I_{OH} = -100\text{ µA}$
Empfänger | Low | $I_{IL} \geq -4\text{ mA}$ bei $U_{IL} = 0,45\text{ V}$
| | High | $I_{IH} \leq 100\text{ µA}$ bei $U_{IH} = 2,4\text{ V}$

**2.4.2.4. Systemtakt**

**Tabelle 8**

Stufe | Pegel | Bedingungen
--- | --- | ---
Sender | Low | $U_{OL} \leq 0,45\text{ V}$ bei $I_{OL} = 15\text{ mA}$
| | High | $U_{OH} \geq 2,4\text{ V}$ bei $I_{OH} = -5\text{ mA}$
Empfänger | Low | $I_{IL} \geq -0,4\text{ mA}$ bei $U_{IL} = 0,45\text{ V}$
| | High | $I_{IH} \leq 50\text{ µA}$ bei $U_{IH} = 2,4\text{ V}$

**Maximale Belastung der Leitung:** 35 Empfänger

Seite 24 TGL 37271/01

**2.4.2.5. Kapazitive Busbelastung**

Jede Baugruppe (jede Steckeinheit) darf eine Busleitung mit maximal 30 pF belasten.
Zur Gewährleistung der Zeitbedingungen darf die je Signalleitung wirkende Gesamtkapazität die in Tabelle 9 angegebenen Werte nicht übersteigen.

**Tabelle 9**

| Open Kollektor-Stufen /INT, /BUSRQ | übrige Leitungen | Sonstige Stufen |
| --- | --- | --- |
| 300 pF | 400 pF | 480 pF |

**2.4.3. Zeitbedingungen**

Systembus-Zeitbedingungen (siehe Bild 11 und Tabelle 10)
Angaben aller Zeiten in ns, ohne Busverstärkerbaugruppe zur evtl. Busverlängerung.

**Tabelle 10**

Signal | Symbol | Parameter | Zeitbedingungen ns min. | max.
--- | --- | --- | --- | ---
TAKT | $t_{CP}$ | Taktperiode | 407 ns -0,407 ns | 407 ns +0,407 ns
| | $t_W(CP,H)$ | Taktimpulsbreite Takt=High | 180 | -
| | $t_W(CP,L)$ | Taktimpulsbreite Takt=Low | 180 | -
AB0 bis AB15 | $t_D(AD)$ | Adressenausgangsverzögerungszeit | - | 230
| | $t_F$ | Verzögerung zu 3-state | - | 270
| | $t_{am}$ | Adresse stabil vor /MREQ | 95 | -
| | $t_{aIO}$ | Adresse stabil vor /IORQ | 300 | -
| | $t_{aRD}$ | Adresse noch stabil nach /RD oder /WR | 130 | -
| | $t_{aRDF}$ | Adresse noch stabil nach /RD oder /WR bei 3-state-Übergang | 140 | -
DB0 bis DB7 | $t_D(D)$ | Datenausgangsverzögerung | - | 330
| | $t_{SCP}(D)$ | Voreinstellzeit vor Vorderflanke TAKT bei M1-Zyklus | 75 | -
| | $t_{SCP}(D)$ | Voreinstellzeit vor Rückflanke TAKT bei Speicherzyklen (außer M1) | 85 | -

Fortsetzung der Tabelle Seite 25

TGL 37271/01 Seite 25

**Fortsetzung der Tabelle 10**

Signal | Symbol | Parameter | min. | max.
--- | --- | --- | --- | ---
DB0 bis DB7 | $t_{dm}$ | Daten stabil vor /WR bei Speicherzyklus | 160 | -
| | $t_{dIO}$ | Daten stabil vor /WR bei E/A-Zyklus | 20 | -
| | $t_{DDAM1}(D)$ | Verzögerung Daten bei Eingabe gegenüber Adresse (M1-Zyklus) | - | 480
| | $t_{DDAIO}(D)$ | Verzögerung Daten bei Eingabe gegenüber Adresse | |
| | | - bei Speicherzyklus | - | 640
| | | - bei E/A-Zyklus | - | 1000
| | $t_H(D)$ | Nachwirkzeiten | 0 | -
| | $t_{dWR}$ | Daten noch stabil nach /WR | 120 | -
/MREQ | $t_{DLCP}(MR)$ | Verzögerung /MREQ -> Low nach Rückflanke TAKT | - | 160
| | $t_{DHCP}(MR)$ | Verzögerung /MREQ -> High nach Vorderflanke TAKT | - | 170
| | $t_{DHCP}(MR)$ | Verzögerung /MREQ -> High nach Rückflanke TAKT | - | 170
| | $t_W(/MR,L)$ | Pulsweite, /MREQ=Low | 360 | -
| | $t_W(/MR,H)$ | Pulsweite, /MREQ=High | 170 | -
/IORQ | $t_{DLCP}(IO)$ | Verzögerung /IORQ -> Low nach Vorderflanke TAKT | - | 120
| | $t_{DLCP}(IO)$ | Verzögerung /IORQ -> Low nach Rückflanke TAKT | - | 170
| | $t_{DHCP}(IO)$ | Verzögerung /IORQ -> High nach Vorderflanke TAKT | - | 170
| | $t_{DHCP}(IO)$ | Verzögerung /IORQ -> High nach Rückflanke TAKT | - | 180
/RD | $t_{DLCP}(RD)$ | Verzögerung /RD -> Low nach Vorderflanke TAKT | - | 120
| | $t_{DLCP}(RD)$ | Verzögerung /RD -> Low nach Rückflanke TAKT | - | 190
| | $t_{DHCP}(RD)$ | Verzögerung /RD -> High nach Vorderflanke TAKT | - | 170
| | $t_{DHCP}(RD)$ | Verzögerung /RD -> High nach Rückflanke TAKT | - | 180
/HALT | $t_D(HT)$ | Verzögerung /HALT -> Low nach Rückflanke TAKT | - | 365

Fortsetzung der Tabelle Seite 26

Seite 26 TGL 37271/01

**Fortsetzung der Tabelle 10**

Signal | Symbol | Parameter | min. | max.
--- | --- | --- | --- | ---
/WR | $t_{DLCP}(WR)$ | Verzögerung /WR -> Low nach Vorderflanke TAKT | - | 140
| | $t_{DLCP}(WR)$ | Verzögerung /WR -> Low nach Rückflanke TAKT | - | 150
| | $t_{DHCP}(WR)$ | Verzögerung /WR -> High nach Rückflanke TAKT | - | 170
| | $t_W(/WR)$ | Pulsweite /WR=Low | 350 | -
/M1 | $t_{DL}(M1)$ | Verzögerung /M1 -> Low nach Vorderflanke TAKT | - | 190
| | $t_{DH}(M1)$ | Verzögerung /M1 -> High nach Vorderflanke TAKT | - | 200
/RFSH | $t_{DL}(RF)$ | Verzögerung /RFSH -> Low nach Vorderflanke TAKT | - | 245
| | $t_{DH}(RF)$ | Verzögerung /RFSH -> High nach Vorderflanke TAKT | - | 220
/WAIT | $t_s(WT)$ | Voreinstellzeit /WAIT -> Low vor Rückflanke TAKT | 55 | -
| | $t_H(WT)$ | Haltezeit /WAIT=Low nach Rückflanke TAKT | 15 | -
/INT | $t_s(INT)$ | Voreinstellzeit /INT vor Vorderflanke TAKT | 65 | -
| | $t_{DIEI}(INT)$ | Verzögerungszeit /INT -> Low nach /IEI -> Low | 0 | -
| | $t_H(INT)$ | Haltezeit /INT nach Vorderflanke TAKT | 45 | -
/NMI | $t_W(/NMI)$ | Pulsweite /NMI=Low | 80 | -
/IEO /IEI | $t_{DM1}(IEO)$ | Verzögerung /IEO -> High während INT-Anerkennung:<br>• nach /M1 -> Low am unterbrechendenden Modul | - | 325
| | $t_{DH}(IEO)$ | • nach /IEI -> High je Businterruptteilnehmer innerhalb der Kette | - | 20
| | $t_s(IEI)$ | Voreinstellzeit /IEI -> High vor /IORQ -> Low während INT-Anerkennung: am letzten Businterruptteilnehmer | - | 155
| | $t_{DRETI}(IEO)$ | Verzögerung /IEO -> Low nach /RD -> High während RETI-Befehl am Kettenende | 1600 | -

Fortsetzung der Tabelle Seite 27

TGL 37271/01 Seite 27

**Fortsetzung der Tabelle 10**

Signal | Symbol | Parameter | min. | max.
--- | --- | --- | --- | ---
/BUSRQ | $t_s(BQ)$ | Voreinstellzeit /BUSRQ vor Vorderflanke TAKT | 65 | -
| | $t_H(BQ)$ | Haltezeit /BUSRQ nach Vorderflanke TAKT | 15 | -
/BAO | $t_{DL}(BA)$ | Verzögerung /BAO -> Low nach Vorderflanke TAKT (am Ausgang ZVE) | - | 195
| | $t_{DH}(BA)$ | Verzögerung /BAO -> High nach Rückflanke TAKT (am Ausgang ZVE) | - | 195
/RESET | $t_s(RS)$ | Voreinstellzeit /RESET Low vor Vorderflanke Takt | 105 | -
| | $t_W(/RS,L)$ | Pulsweite /RESET = Low | 1220 | -
/MEMDI | $t_s(MEMDI)$ | Voreinstellzeit /MEMDI -> Low vor /MREQ -> Low | 30 | -
| | $t_H(MEMDI)$ | Haltezeit /MEMDI=Low nach /MREQ -> High | 30 | -
/IODI | $t_s(IODI)$ | Voreinstellzeit /IODI -> Low vor /IORQ -> Low | 90 | -
| | $t_H(IODI)$ | Haltezeit /IODI=Low nach /IORQ -> High | 30 | -
/RDY | $t_s(RDY)$ | Voreinstellzeit /RDY -> Low vor /WAIT-auswertender Rückflanke TAKT | 165 | -
| | $t_H(RDY)$ | Haltezeit /RDY=Low nach /MREQ oder /IORQ | 0 | -

**Anmerkung:** Die /MEMDI, /IODI und /RDY betreffenden Zeiten sind auf Bild 11 nicht dargestellt. Die Voreinstellzeit $t_s(MEMDI)$ ist nur dann notwendig, falls es vom Speichertyp oder der RDY-Auswerteschaltung gefordert wird.

**2.5. Konstruktive Bedingungen**

Auf jeder an den Bus anzuschließenden Baugruppe, (Steckeinheit mit Leiterplatte der Abmessungen 215 mm x 170 mm) sind dem Systembus und dem Koppelbus konstruktiv zwei 58-polige Steckverbinder zugeordnet (siehe Bild 12 Steckverbinder X1 und X2).
Zur Sicherung definierter elektrischer Bedingungen ist der Systembus am Gefäß zur Aufnahme der Steckeinheiten in Form einer gedruckten Rückverdrahtung auszuführen. Der Koppelbus dient zur Herstellung spezifischer Verbindungen zwischen den Baugruppen, er kann als kombinierte Rückverdrahtung, z. B. Stromversorgungszuführung gedruckt und die spezifischen Verbindungen zwischen den Steckverbinderanschlüssen (Pins) gewickelt, ausgeführt werden. Ohne Einsatz eines Busverstärkermoduls ist der Systembus bis zu einer Länge von 800 mm aufbaubar. Stichleitungen auf den Steckeinheiten sind in ihrer Länge zu minimieren ($L_{max} \approx 100\text{ mm}$).

Seite 28 TGL 37271/01

**Zeitmessungen bei den Spannungen 2 V und 0,8 V**

![[Pasted image 20260515161645.png]]
*(Hier ist Bild 11 mit allen zeitlichen Verläufen der Bus-Signale abgebildet)*
**Bild 11**

TGL 37271/01 Seite 29

Bei Verwendung eines Busverstärkermoduls kann der Systembus um maximal 2,5 m verlängert werden. Zur Systembusverlängerung (Anschluß eines 2. Paneels) ist Flachbandkabel BY 19 x 0,3 TGL 24451/20 einzusetzen. Im Kabel sind Signaladern beidseitig durch geerdete Masseadern zu trennen.

Die Baugruppen (Steckeinheiten) können, falls das die Einordnung in die Prioritätsketten nicht einschränkt, an einen beliebigen Steckplatz an den Systembus angeschlossen werden. Baugruppen, die zu den Prioritätsketten (/IEI und /IEO bzw. /BAI und /BAO) gehören, sind vom festgelegten Kettenanfang aus in fallender Priorität aufgereiht anzuordnen. Speicherbaugruppen können zwischen diesen oder seitlich gesteckt sein.

Die Systembussignale sind dem Steckverbinder X1 zugeordnet. In der Rückverdrahtungsleiterplatte (RVLP) befindet sich die Buchsenleiste. Es werden Steckverbinder des Typs XX4-59 TGL 29331/03 eingesetzt. Es sind auch direkte 58-polige Steckverbinder (gedruckter Leiterkamm auf der Steckeinheit) nach TGL 29331/01 möglich. Es wird empfohlen, auf der Rückverdrahtungsleiterplatte die Signalleitungen auf der Lötseite als durchgehende Leiterzüge zu führen.

Die Taktleitung ist in Masseleitungen einzubetten. Auf Steckeinheiten die die Interruptkettensignale nicht benutzen, sind diese durch Kurzschlußbrücken zu verbinden. Die Bestückungsseite ist mit einem Massegitter zu versehen bzw. ist als Massefläche auszubilden. Die Anordnung der Steckverbinder zeigt Bild 12. Tabelle 11 enthält die Anschlußbelegung des Systembusses. Tabelle 12 enthält eine Empfehlung zur Belegung des Koppelbusses.

![[Pasted image 20260515161722.png]]
*(Abbildung: Bild 12 Mechanische Anordnung der Steckverbinder X1 und X2)*
**Bild 12**

2 siehe Abschnitt Hinweise (Werkstandard des VEB Kombinat Robotron)

Seite 30 TGL 37271/01

**Tabelle 11 Anschlußbelegung Steckverbinder Systembus**

Signalname | B/C | A | Signalname
--- | --- | --- | ---
5 P | 29 | 29 | 5 P
12 P | 28 | 28 | 12 P
/BAI | 27 | 27 | /BAO
/HALT | 26 | 26 | /M1
/RDY | 25 | 25 | /RFSH
/IORQ | 24 | 24 | /WAIT
/INT | 23 | 23 | /NMI
00 | 22 | 22 | /IODI
00 | 21 | 21 | TAKT
/BUSRQ | 20 | 20 | /RESET
AB1 | 19 | 19 | AB0
AB3 | 18 | 18 | AB2
AB5 | 17 | 17 | AB4
AB7 | 16 | 16 | AB6
5 N | 15 | 15 | 5 N
AB9 | 14 | 14 | AB8
AB11 | 13 | 13 | AB10
AB13 | 12 | 12 | AB12
AB15 | 11 | 11 | AB14
/IEI | 10 | 10 | /IEO
/MEMDI | 09 | 09 | /MREQ
/RD | 08 | 08 | /WR
DB0 | 07 | 07 | DB1
DB2 | 06 | 06 | DB3
DB4 | 05 | 05 | DB5
DB6 | 04 | 04 | DB7
5 PG | 03 | 03 | 5 PG
00 | 02 | 02 | 00
00 | 01 | 01 | 00

**Hinweise**

Im vorliegenden Standard ist auf folgende Standards Bezug genommen:
TGL 24451/20; TGL 29331/01; TGL 29331/03;
System Mikrorechner; Linieninterface Bus K 1520; Koppelbus siehe KROS 0314, Werkstandard des VEB Kombinat Robotron
# Zentrale Recheneinheit K 2526 / K 2527

**robotron**

**Zentrale Recheneinheit**
**K 2526 / K 2527**

### Betriebsdokumentation

6. Auflage  
    Karl-Marx-Stadt 1988
    

© VEB Kombinat Robotron

# Inhaltsverzeichnis

1. Kurzcharakteristik
    
2. Technische Daten  
    2.1. Allgemeine Daten  
    2.2. Takterzeugung  
    2.3. Zentrale Verarbeitungseinheit  
    2.4. Speicher  
    2.5. Zähler/Zeitgeber  
    2.6. Programmierbarer DMA-Kanal  
    2.7. Universelles E/A-Tor (U-Bus)  
    2.8. Speicher- und E/A-Schutz  
    2.9. Zentrale Baugruppensteuerung (BS-PIO)
    
3. Bezeichnung und Bedeutung der Signale
    
4. Aufbau des Rechnerbusses  
    4.1. Systembus  
    4.1.1. Datenbus  
    4.1.2. Adreßbus  
    4.1.3. Steuer- und Kontrollbus  
    4.1.4. Stromversorgungsleitungen  
    4.2. Koppelbus  
    4.3. Anschlußbelegung der Steckverbinder X1 und X2
    
5. Prioritätenzuordnung
    
6. Technische Beschreibung  
    6.1. Blockschaltbilder  
    6.2. Takterzeugung  
    6.3. Ein- und Ausschaltung Lade-ROM  
    6.3.1. Allgemeines  
    6.3.2. Funktionsbeschreibung  
    6.4. Rücksetzschaltung  
    6.5. WAIT-Einblendung für ZVE2  
    6.5.1. Allgemeines  
    6.5.2. Funktionsbeschreibung  
    6.6. RDY-Bildung  
    6.6.1. Allgemeines  
    6.6.2. RDY-Bildung bei Ansteuerung  
    6.6.3. RDY-Bildung durch die Zentrale Baugruppensteuerung  
    6.6.4. RDY-Bildung bei Interruptquittungszyklen  
    6.7. Programmierbarer DMA-Kanal  
    6.7.1. Allgemeines  
    6.7.2. Arbeitsweise der ZVE-Umschaltung  
    6.8. Bustreiber und -steuerung für den vollständigen ZRE- und Universalbus  
    6.8.1. Bussystem  
    6.8.2. Treibersteuerung  
    6.9. ZRE interne E/A-Tore  
    6.9.1. Allgemeines  
    6.9.2. Adressenbelegung für interne E/A-Tore der ZRE K 2526  
    6.9.3. Ansteuerung des BS-PIO  
    6.9.4. Ansteuerung des Zähler/Zeitgebers  
    6.9.5. Universelles Ein-/Ausgabe-Tor  
    6.9.5.1 Struktur und Anschlußbedingungen  
    6.9.5.2 Funktion  
    6.10. Zentrale Baugruppensteuerung  
    6.10.1. Struktur  
    6.10.2. Funktion  
    6.11. Speicher- und E/A-Schutz  
    6.11.1. Schutzaufgabe  
    6.11.2. Beschreibung des RAM  
    6.11.3. Funktion des Speicherschutzes  
    6.12. Bildung des Steuersignals **/IEP**  
    6.12.1. Allgemeines  
    6.12.2. Funktion  
    Serviceschaltpläne
    

# 1. Kurzcharakteristik

Die Zentrale Recheneinheit ZRE K 2526/27 kann an das realisierte BUS-System des MR K 1520 als zentrale Baugruppe angeschlossen werden. Diese ZRE-Variante berücksichtigt die Forderungen nach hoher Leistungsfähigkeit, insbesondere bei der Simultanarbeit mehrerer E/A-Einheiten. Sie besitzt die Funktionen zum Speicher- und E/A-Schutz und zum Betriebssystem.  
Nach Betriebsbeginn gibt ein abschaltbarer Anfangslader den gesamten Adreßbereich des Rechners wieder frei. Ein zentraler Zähler/Zeitgeber dient als Zeitnormal und übernimmt Überwachungsfunktionen im System. Die ZRE besitzt ein universelles E/A-Tor, was vorzugsweise zum Anschluß einer Tastatur benutzt werden kann.  
Es werden Bauelemente auf der Systembasis des Schaltkreises Q 300 eingesetzt.  
Die ZRE K 2526 umfaßt die Funktionsgruppen:

- Zentrale Verarbeitungseinheit (ZVE)
    
- programmierbarer DMA-Kanal mit einer 2. ZVE (DMA-ZVE)
    
- Speicher (ROM)
    
- Speicher- und E/A-Schutz (RAM)
    
- Zähler/Zeitgeber (CTC)
    
- universelles E/A-Tor (U-Bus)
    
- Zentrale Baugruppensteuerung mit einem Parallel-interface-Baustein (BS-PIO)
    
- Systembustreiber
    
- quarzstabilisierter Taktgenerator.
    

Die ZRE K 2527 entspricht der ZRE K 2526 ohne programmierbaren DMA-Kanal. Beide Zentralen Recheneinheiten sind nicht als "Single-board-computer" verwendbar, ihren Anfangszustand erhalten sie über die **/RESET**-Leitung des Systembusses.

# 2. Technische Daten

## 2.1. Allgemeine Daten

|   |   |
|---|---|
|Eigenschaft|Wert|
|Steckeinheitenabmessung:|215 mm x 170 mm|
|Steckraster:|20 mm|
|Steckverbinder:|2 x 58-polig, indirekt Bauform 304-58, TGL 29331/03|
||1 x 26-polig, indirekt Bauform 102-26, TGL 29331/04-7 PdAu|
|Einsatzklasse:|5/60/30/95/10-1E|
|Stromaufnahme (max.):|wie K 2521|
||5 P = + 5 V ± 5 %, 1,5 A|
||5 N = - 5 V ± 5 %, 0,07 A|
||12 P = + 12 V ± 5 %, 0,12 A|

## 2.2. Takterzeugung

|   |   |
|---|---|
|Eigenschaft|Wert|
|Quarztyp:|Q 51/E2-010, 9832 kHz TGL 33584|
|Quarznennfrequenz:|9,832 MHz ± 0,1 %|
|Systemtaktfrequenz:|2,4576 MHz ± 0,1 %|
|Systemtaktzyklus:|407 ns ± 0,1 %|
|Elektrische Parameter:|Pegel TTL-kompatibel|

## 2.3. Zentrale Verarbeitungseinheit

|   |   |
|---|---|
|Eigenschaft|Wert|
|Schaltkreistyp:|Q 300|
|Befehlsanzahl:|158 Basisbefehle|
|Befehlslänge:|1, 2, 3 und 4 Byte|
|Verarbeitungsbreite:|1 Byte parallel|
|Wortlänge Daten:|1 oder 2 Byte|
|Adressierbarer Speicher:|64 K Byte|
|E/A-Adreßbereich:|256 Ein-/256 Ausgabeadressen (erweiterbar)|
|Unterbrechungsarten:|1. maskierbare Unterbrechung (3 verschiedene Behandlungsmodi)|
||2. nichtmaskierbare Unterbrechung|
|Wartesteuerung:|vorhanden|
|Refreshsteuerung:|vorhanden|

## 2.4. Speicher

|   |   |
|---|---|
|Eigenschaft|Wert|
|Schaltkreistyp:|Y 708|
|Kapazität:|1 K Byte PROM (ROM) programmiert abschaltbar über zentrale Baugruppensteuerung|
|Adressierung:|fest|
|Adressen:|0000_H ... 03FF_H|
|Bemerkungen:|- nach dem Systemsignal **/RESET** zugeschaltet|
||- bei aktiviertem Lade-ROM, Bildung des Speichersperrsignals **/MEMDI**|
||- bei Speicherschreibzyklen wird der Lade-ROM nicht angesprochen|

## 2.5. Zähler/Zeitgeber

|   |   |
|---|---|
|Eigenschaft|Wert|
|Schaltkreistyp:|Q 302|
|Anzahl der Kanäle:|4|
|Adressierung:|über 8 Bit E/A-Adressen|
||Adreßgruppe AB4 ... AB7:|
||O_H (2_H, 4_H, 6_H, 8_H, A_H, C_H, E_H)|
||Adreßgruppe AB0 ... AB3:|
||C_H, D_H, E_H, F_H|
|Ausgangssignale:|MOS, TTL-kompatibel (max. 1,8 mA)|
|Eingangssignal:|MOS, TTL-kompatibel|
|Betriebsarten:|1. Zeitgeber|
||2. Zähler|
|**Zeitgeber**||
|Erzeugbare Intervalle:|programmierbar (16 ... 256^2) * t_z|
||t_z - Systemtaktzyklus|
|**Zähler**||
|Zählbereich:|programmierbar, 1 ... 256 externe Ereignisse|
|Max. Zählbereich:|256^4 externe Ereignisse erreichbar durch Reihenschaltung von 4 Kanälen|
|Bemerkungen:|- Der Zeitgeberausgang ZC/TO2 ist mit dem Zählereingang CLK/TRG3 fest verdrahtet; vorzugsweise zur Bildung einer vom Systemtakt abgeleiteten Systemzeit.|
||- Zähler/Zeitgeber am Ende der 1. Prioritätenkette angeordnet, d. h. an IEI (Systembus) und ausgangsseitig an IEO1 (Koppelbus).|

## 2.6. Programmierbarer DMA-Kanal

|   |   |
|---|---|
|Eigenschaft|Wert|
|Schaltkreistyp:|Q 300|
|Arbeitsweise:|- komplementär zum Zentralprozessor auf den Systembus aufschaltbar|
||- Befehlsanzahl, Befehlslänge, Verarbeitungsbreite, adressierbarer Speicher, E/A-Adreßbereich - siehe ZVE1|
||- Ab- und Zuschaltung über **/BUSRQ**-Signal|
||- vorzugsweise Anwendung für FD-Anschluß|
||- niedrigste Priorität in der BAI/BAO-Kette|
||- programmiert rücksetzbar durch **/RES-ZVE2**|
||- Bedienung mehrerer Steckeinheiten für schnelle E/A-Geräte im DMA-Betrieb möglich (nicht simultan)|
|Unterbrechungsarten:|keine|
|Refresh- und Wartesteuerung:|vorhanden|
|Übertragungsgeschwindigkeit:|entsprechend der Programmierung und der angeschlossenen Peripherie (wie ZVE)|

## 2.7. Universelles E/A-Tor

|   |   |
|---|---|
|Eigenschaft|Wert|
|Schaltkreistyp:|2 x U 216; 1 x U 212|
|Datenleitungen:|8 Bit - bidirektional, TTL-kompatibel (max. 15 mA)|
|Auswahlleitungen:|4, TTL-kompatibel (max. 10 mA)|
|Adressierung der Auswahlleitungen:|AB4 ... AB7: O_H (2_H, 4_H, 6_H, 8_H, A_H, C_H, E_H)|
||AB0 ... AB3: 1_H, 3_H, 5_H, 6_H|
||Anschluß über einen 26-poligen zweireihigen Steckverbinder|

## 2.8. Speicher- und E/A-Schutz

|   |   |
|---|---|
|Eigenschaft|Wert|
|Schaltkreistyp:|Q 240|
|Arbeitsweise:|- Speicherbereichsaufteilung in 1024 Segmente zu 64 Byte|
||Zuordnung der Schutzfunktion für die Segmente durch Programmierung des Q 240|
||- Aktivierung des Speichersperrsignals bei unerlaubtem Zugriff auf ein geschütztes Segment (nur bei Speicherschreibzyklen)|
||- Aktivieren einer nichtmaskierten Unterbrechungsanforderung bei unerlaubtem E/A-Befehl aus einem ungeschützten Speicherbereich (abschaltbar über Lötbrücke)|
||- Abschaltung der Schutzfunktion bei DMA-Betrieb|

## 2.9. Zentrale Baugruppensteuerung

|   |   |
|---|---|
|Eigenschaft|Wert|
|Schaltkreistyp:|Q 301|
|Funktionen:|- Einzelbefehlsabarbeitung|
||- Speicherseitenauswahl|
||- Spannungsüberwachung für CMOS-Speicher|
||- Speicher- und E/A-Schutzanzeige|
||- Abschaltung des Speichers auf der ZRE (Lade-ROM)|
||- Programmiersteuerung des Speicherschutz-RAM|
||- Sonderfunktion (z. B. programmiertes Netzausschalten)|
||- Aufruf Betriebssystemebene|
||- Konfigurationstest|
||- 3 Bit wahlfreier Codeschlüssel (Ausrüstungsvariante)|
||- RD4-Auswertung für Konfigurationstest (außerhalb der normalen Arbeitsweise)|

Die zentrale Baugruppensteuerung ist am Ende der 2. Prioritätenkette eingeordnet, d. h. an **/IEI1** (Koppelbus).

# 3. Bezeichnung und Bedeutung der Signale

|   |   |   |   |   |
|---|---|---|---|---|
|Signalname|Signalbedeutung|Aktiv. Pegel|Wirkungsrichtung bez. auf ZRE/DMA|Sonstige Bedingungen|
|DB0 ... DB7|Datenbus: Leitungen führen beim Datenaustausch auf dem Bus die Befehls- bzw. Dateninformationen zum Speicher und E/A-Geräten.|high|bidirektional|am Bus angeschlossene Sender müssen 3-state-Ausgänge besitzen|
|AB0 ... AB15|Adreßbus: 16 Leitungen führen die Adresse des Speicherplatzes oder des E/A-Gerätes.|high|unidirektional ZRE bzw. DMA ist Sender|ZRE bzw. DMA müssen 3-state-Ausgänge besitzen|
|**/MREQ**|Speicheranforderung (memory request): Das Signal zeigt an, daß der Adreßbus eine gültige Adresse für eine Speicherlese- bzw. -schreiboperation hat.|low|unidirektional|ZRE bzw. DMA müssen 3-state-Ausgänge besitzen|
|**/IORQ**|Ein-/Ausgabeanforderung (input/output request): Das Signal zeigt an, daß der Adreßbus im unteren Byte (AB0 ... AB7) eine gültige E/A-Adresse führt. AB8 ... AB15 ≙ gültige Daten. Die ZRE erzeugt **/IORQ**, wenn ein INT-Gesuch von der ZRE akzeptiert wurde und der neue Befehl bzw. INT-Vektor auf den Bus gelegt werden kann (siehe **/M1**).|low|unidirektional|ZRE bzw. DMA (Sender) müssen 3-state-Ausgänge besitzen|
|**/RD**|Lesen (read): Das Signal zeigt an, daß durch die ZRE bzw. DMA Informationen (Daten oder Befehle) vom Speicher oder E/A-Gerät gelesen werden können.|low|unidirektional|Sender muß 3-state-Ausgänge besitzen|
|**/WR**|Schreiben (write): Das Signal zeigt an, daß von der ZRE gültige Daten auf den Datenbus gelegt wurden, die im Speicher einzutragen bzw. vom E/A-Gerät zu übernehmen sind.|low|unidirektional|Sender müssen 3-state-Ausgänge besitzen|
|**/RFSH**|Speicherauffrischung (refresh): Das Signal zeigt an, daß die unteren 7 Bit des Adreßbusses eine Refreshadresse zum Auffrischen dynamischer RAM's bilden. **/RFSH** wird während der Zeit T3 und T4 bereitgestellt.|low|unidirektional|Sender muß 3-state-Ausgänge besitzen|
|**/M1**|Befehlslesezyklus (Maschinenzyklus 1): Das Signal zeigt an, daß der laufende Prozessorzyklus ein Befehlslesezyklus des auszuführenden Befehls ist. Mit **/IORQ** zeigt es an, daß ein Interruptgesuch akzeptiert wurde und der INT-Vektor auf den Datenbus zu legen ist.|low|unidirektional|Sender muß 3-state-Ausgänge besitzen|
|**/WAIT**|Warten (wait): Das Signal zeigt an, daß Speicher oder E/A-Gerät für Datenaustausch nicht "bereit" sind. Er hält solange an, wie **/WAIT** aktiv ist. (Beachte: **/RFSH** muß in bestimmter Zeit erfolgen)|low|unidirektional ZRE ist Empfänger|Sender müssen Open-Kollektor-Stufen besitzen|
|**/INT**|Maskiertes Unterbrechungsgesuch (interrupt request): Das **/INT**-Anforderungssignal wird durch ein E/A-Gerät erzeugt und ist ein Gesuch an die ZRE zur Unterbrechung. Die Anforderung wird am Ende des gültigen Befehls beachtet. Wird **/INT** akzeptiert, so wird das INT-Annahmesignal bei Beginn des nächsten Befehlszyklus ausgesandt (**/M1** . **/IORQ**).|low|unidirektional ZRE ist Empfänger|Sender müssen Open-Kollektor-Stufen besitzen|
|**/NMI**|Nichtmaskierter Interrupt (non maskable interrupt): Unterbrechungsgesuch an die ZRE. Es besitzt höhere Priorität als **/INT** und wird sofort akzeptiert. **/NMI** zwingt den Prozessor bei der Adresse 0066_H zu starten. Der Befehlszähler wird gerettet, so daß der Anwender zu dem unterbrochenen Programm zurückkehren kann.|low|unidirektional ZRE ist Empfänger|Sender müssen Open-Kollektor-Stufen besitzen|
|**/HALT**|ZRE-Halt (halt state): Das Signal zeigt an, daß sich der Prozessor im Halt-Zustand befindet und zur weiteren Arbeit auf einen Interrupt wartet.|low|unidirektional|Sender muß 3-state-Ausgang besitzen|
|**/RDY**|Bereit (ready): Das Signal zeigt an, daß der angesprochene Speicher oder das angesprochene E/A-Gerät am Bus vorhanden ist und für Lese- oder Schreiboperationen zur Verfügung steht.|low|unidirektional|Sender müssen Open-Kollektor-Stufen besitzen|
|**/RESET**|Rücksetzen: Das Signal dient als zentrales Rücksetzsignal im Rechner.|low|unidirektional ZRE ist Empfänger und Sender|Sender müssen Open-Kollektor-Stufen besitzen|
|**/BUSRQ**|Busanforderung (bus request): Das Signal zeigt an, daß ein Gesuch auf direktem Speicherverkehr durch einen DMA-Kanal gestellt wurde. **/BUSRQ** hat bezüglich der Busanforderung höchste Priorität.|low|unidirektional ZRE ist Empfänger|Sender müssen Open-Kollektor-Stufen besitzen|
|**/BUSAK**|Anerkennung der Busanforderung (bus acknowledge): Die ZVE zeigt mit dem Signal an, daß sie **/BUSRQ** akzeptiert hat. Datenbus, Adreßbus und 3-state Steuerausgänge sind in den hochohmigen Zustand geschaltet. Der Bus steht dem DMA-Kanal zur Verfügung.|
|**/IEI**|Interrupt-Freigabe-Eingang (interrupt enable input): Es kennzeichnet, daß die am Bus näher zur ZRE gesteckte Stecke keinen INT angemeldet hat.|low|unidirektional||
|**/IEO**|Interrupt-Freigabe-Ausgang (interrupt enable output): Das Signal sagt aus, daß die Stecke keinen Interrupt fordert. Die Leitung wird direkt mit **/IEI** der nachfolgenden Stecke verbunden.|low|unidirektional||
|**/MEMDI**|Speichersperren (memory disable): Das Signal dient der Sperrung der Speicher für alle Lese- und Schreiboperationen.|low|unidirektional|Sender muß Open-Kollektor-Stufen besitzen|
|**/IODI**|E/A-Gerätesperrung (input output disable): Das Signal dient der Sperrung von E/A-Operationen der ZRE mit den E/A-Geräten.|low|unidirektional|Sender am Bus muß Open-Kollektor-Stufen besitzen|
|TAKT|Takt für das System: Das Signal dient zur zeitlichen Synchronisation interner Vorgänge.|TTL|unidirektional||
|**/BAI**|Anerkennung einer Busanforderung (busak input): **/BAI** bildet am Bus eine prioritätsbestimmende Kette zur Durchschaltung von **/BUSAK**. **/BAI** ist dabei das Eingangssignal der Kette (siehe **/IEI**).|low|||
|**/BAO**|Anerkennung einer Busanforderung (busak output): Dieses Signal ist das Ausgangssignal obiger Kette.|low|||

# 4. Aufbau des Rechnerbusses

Die Schnittstelle zwischen der ZRE und den Anschlußeinheiten ist der Rechnerbus K 1520, der durch die Systembusrichtlinie MR K 1520 charakterisiert wird.  
Der Rechnerbus besteht aus 2 Leitungsbündeln, die konstruktiv 2 58-poligen Steckverbindern jeder Steckeinheit zugeordnet sind. Es handelt sich um den Systembus (Stecker X1) und den Koppelbus (Stecker X2).

## 4.1. Systembus

Er umfaßt die zum Systemaufbau unbedingt erforderlichen Hauptsignale. Er bildet die gemeinsame Verbindung zwischen ZRE, Speicher und E/A-Einheiten zur Durchführung des Datentransfers zwischen diesen. Der Systembus ist durch eine gedruckte Rückverdrahtung gekennzeichnet. Lediglich die Signale **/IEI**, **/IEO**, **/BAI** und **/BAO** können wahlweise gebrückt werden (siehe Prioritätenzuordnung). Der Bus wird durch folgende Leitungsbündel gebildet:

### 4.1.1. Datenbus (DB0 ... DB7)

Beim Datenaustausch führen diese Leitungen die Befehls- bzw. Dateninformation. Der Bus besteht aus 8 Leitungen und ist bidirektional nutzbar.

### 4.1.2. Adreßbus (AB0 ... AB15)

Die 16 Adreßleitungen ermöglichen die Adressierung eines Speicherbereiches von 64 K Byte bzw. von Adressen der E/A-Tore. Er ist unidirektional.  
AB0 ... AB15 sind mit **/MREQ** als Speicheradresse, AB0 ... AB7 mit **/IORQ** als E/A-Geräteadresse (max. 256 Eingabe- oder 256 Ausgabeadressen möglich) und AB0 ... AB6 mit **/RFSH** als Refreshadresse für das Auffrischen dynamischer RAM-Speicher gültig.  
AB8 ... AB15 sind mit **/IORQ** gültig und enthalten bei Ein- und Ausgabebefehlen den Inhalt vom ZVE-Register A bzw. B (je nach Befehlsart).

### 4.1.3. Steuer- und Kontrollbus

Er beinhaltet alle Steuer- und Kontrollsignale, die zur Steuerung des aufgebauten Systems unbedingt erforderlich sind.

|   |   |   |
|---|---|---|
||||
|**/MREQ**|**/RD**|**/NMI**|
|**/MEMDI**|**/WR**|**/INT**|
|**/IORQ**|**/WAIT**|**/IEI**|
|**/IODI**|**/RDY**|**/IEO**|
|**/M1**|**/HALT**|**/BAI**|
|**/RFSH**|**/RESET**|**/BAO**|
|**/BUSRQ**|**/BUSAK**|TAKT|

### 4.1.4. Stromversorgungsleitungen

Sie führen die Leitungen: 5 P, 12 P, 5 N, 5 PH und 00.

## 4.2. Koppelbus

Der zur Mehrrechnerkopplung benutzte Signalbündelteil wird als Koppelbus bezeichnet. Seine freien Steckverbinderanschlüsse können in der Rückverdrahtung zur Herstellung spezifischer Verbindungen zwischen den Steckeinheiten benutzt werden. Bei diesem Bus sind nur die Masse- und Stromversorgungsanschlüsse gedruckt ausgeführt. Alle weiteren Verbindungen sind gewickelt. Der Koppelbus ist somit speziell auch für die Verbindung der Anschlüsse anwenderspezifischer Steckeinheiten geeignet.

## 4.3. Anschlußbelegung der Steckverbinder X1 und X2

|   |   |   |   |   |   |   |   |
|---|---|---|---|---|---|---|---|
|Systembus (X1)||||Koppelbus (X2)||||
|**Spannung Name**|**C**|**A**|**Spannung Name**|**Spannung Name**|**C**|**A**|**Spannung Name**|
|5 P|29|29|5 P|00|29|29|00|
|12 P|28|28|12 P|00|28|28|00|
|**/BAI**|27|27|**/BAO**|12 N|27|27|12 N|
|**/HALT**|26|26|**/M1**|**/IEI1**|26|26|**/IEO1**|
|**/RDY**|25|25|**/RFSH**|CLK/TRGO|25|25|ZC/TO|
|**/IORQ**|24|24|**/WAIT**|CLK/TRG1|24|24|ZC/TO1|
|**/INT**|23|23|**/NMI**|CLK/TRG2|23|23|ZC/TO2|
|00|22|22|**/IODI**|**/SUE**|22|22||
|00|21|21|TAKT|**/MEMDI2**|21|21|**/MEMDI1**|
|**/BUSRQ**|20|20|**/RESET**|SA|20|20||
|AB1|19|19|AB0||19|19||
|AB3|18|18|AB2||18|18||
|AB5|17|17|AB4||17|17||
|AB7|16|16|AB6||16|16||
|(5 N)|15|15|5 N||15|15||
|AB9|14|14|AB8||14|14||
|AB11|13|13|AB10||13|13||
|AB13|12|12|AB12||12|12||
|AB15|11|11|AB14||11|11||
|**/IEI**|10|10|**/IEO**||10|10||
|**/MEMDI**|9|9|**/MREQ**||9|9||
|**/RD**|8|8|**/WR**||8|8||
|DB0|7|7|DB1|**/IEP**|7|7||
|DB2|6|6|DB3||6|6||
|DB4|5|5|DB5|/UINT|5|5|00|
|DB6|4|4|DB7|X-TAKT|4|4|00|
|5 PG|3|3|5 PG|SSp2 (5PH)|3|3|SSp3|
|00|2|2|00|SSp1|2|2|SSp1|
|00|1|1|00|5 P|1|1|5 P|

# 5. Prioritätenzuordnung

Jede Steckeinheit ist im Gestell ESE 083-6-050-002 (Paneel) prinzipiell an eine beliebige Stelle steckbar.  
Die Entfernung der STE mit peripheren Schaltkreisen von der ZRE-Steckeinheit K 2526/27 bestimmt die Prioritäten dieser Steckeinheit bei der Bedienung von Interruptanforderung (**/IEI**-**/IEO**-daisy-chain-Kette) oder bei Anforderung der Busherrschaft (**/BAI**-**/BAO**-daisy-chain-Kette). Je kleiner die Entfernung von der ZRE-Steckeinheit ist, um so höher ist ihre Priorität, d. h. je eher wird eine Interruptanforderung von der ZRE anerkannt und bearbeitet.  
Nichtbelegte Steckplätze im Gestell sind entweder mit Brücken **/IEI**-**/IEO** oder **/BAI**-**/BAO** (Kurzschlußstecker oder Wickelbrücken an entsprechender Stelle der Rückverdrahtung) zu versehen. Eine Ausnahme bilden die Speichersteckeinheiten:

- OPS K 3520 - 4 K Byte statischer Schreib-Lese-Speicher (sRAM) nMOS
    
- PFS K 3820 - 16 K Byte programmierbarer Festwertspeicher (EPROM)
    
- OPS K 3525 - 16 K Byte dynamischer Schreib-Lese-Speicher
    
- OPS K 3521 - 4 K Byte statischer Schreib-Lese-Speicher CMOS
    

Auf ihnen sind die daisy-chain-Ketten bereits gebrückt. Sind mehrere periphere Schaltkreise auf einer Steckeinheit, z. B. auf der ZRE-Steckeinheit der CTC- und der Betriebssystem-PIO, so wird die Priorität dieser Schaltkreise durch die interne Reihenschaltung der Interrupt-Enable-Ein-/Ausgänge festgelegt.  
Bei der ZRE K 2526 hat der CTC eine höhere Priorität als der BS-PIO (siehe Abb. 1).
![[Pasted image 20260510220845.png]]
![Bildunterschrift: Abb. 1 Prioritätenkette (zeitkritisch und zeitunkritisch) zwischen ZRE und Peripherie über Systembus und Koppelbus]

# 6. Technische Beschreibung

## 6.1. Blockschaltbilder

![[Pasted image 20260510220934.png]]
![Bildunterschrift: Abb. 2 ZRE K 2526 Blockschaltbild]

![[Pasted image 20260510221030.png]]
![Bildunterschrift: Abb. 3 Treiber - Blockschaltbild der Adreß- und Datentreiber]

## 6.2. Takterzeugung

Der Quarzgenerator Q 51/E2 schwingt mit einer Resonanzfrequenz von 9,832 MHz ± 0,1 % (T1).

![[Pasted image 20260510221211.png]]
![Bildunterschrift: Abb. 4 Quarzgenerator]

Der am A23/6 erzeugte Rechteckimpuls von 9,832 MHz wird durch die nachfolgenden FFs A15/05 und A15/09 zwei Mal untersetzt, so daß der Systemtakt T3 von 2,458 MHz erzeugt wird und den Systembus speist. Dieser Takt wird durch die beiden Transistorstufen V1 und V2 verstärkt und definiert den "high"-Pegel und eine geforderte Flankensteilheit von ≦ 30 ns.

![[Pasted image 20260510221246.png]]
![Bildunterschrift: Abb. 5 Takterzeugung]

## 6.3. Ein- und Ausschaltung Lade-ROM

### 6.3.1. Allgemeines

Der Lade-ROM A26 ist ein 1 K-Speicherchip, in dem sich der 1. Teil des Startprogramms befindet. Er belegt die absoluten Adressen 0000 ... 03FF_H.  
Der 1. Teil des Startprogramms hat folgende Aufgaben:

- Löschen der ersten 2 K-RAM
    
- Rücksetzen Speicherschutz-FF und Austasten des Speichers
    
- Abfrage: ROM- oder RAM-Variante?
    
- bei RAM-Variante: Einlesen des Systemladers und Kontrolle
    
- Adressen **AAWA** (Anfangsadresse-Anwender-RAM) und **EBSA** (Ende-Betriebssystem-RAM) eintragen
    
- bei ROM-Variante: **AGM** abspeichern (Anfangsadresse-Grundmodul im EROM)
    

Der Lade-ROM wird nur beim Lesezyklus (/RD = 0) angesprochen, um auch während des Zustandes LD-ROM aktiv ein Beschreiben des Hintergrund RAM (Speicher außerhalb der ZRE) gleichen Adreßbereiches zu ermöglichen.  
Die Schaltung "Ein- und Ausschaltung Lade-ROM" verhindert durch die Bildung /MEMDI = 0 im aktiven Zustand des LD-ROM (/CS = 0) das Lesen des Adreßbereiches 0000 ... 03FF_H im Hintergrundspeicher.

### 6.3.2. Funktionsbeschreibung

Nach der Einschaltlöschung wird durch das Signal /RESET = 0 die ZRE K 2526 auf einen definierten Anfangszustand gesetzt. Alle rücksetzbaren Bauelemente besitzen den Ausgang "low", der BS-PIO-Ausgang 3-state bzw. der Eingang A23/06 durch den Ziehwiderstand R3:4 "high"-Potential, d. h. **/LD-ROM** zugeschaltet.  
Der Programmcounter der ZVE1 A3 steht auf der Adresse 00, d. h. 1. Speicherplatz im Lade-ROM angesteuert.

Daten LD-ROM ———> interner Datenbus der ZRE

Da im Adreßbereich des Lade-ROM die Adreßleitungen AB11 ... AB15 = 0 sind und **/MEMDI = 1**, decodiert der 1 aus 8-Decoder A 22 ein low am Ausgang A22/15. A21 bildet **/CS-ROM = 0** bei **/LD-ROM = 1** (vom BS-PIO). (Bei Speicheranforderung ist **/MREQ = 0**, AB10 = 0, **/RD = 0**).  
Über A28 geht **/CS-ROM = 0** in Selbsthaltung und über A10/06 und A5/08 wird **/MEMDI = 0** (externes Speichersperrsignal) und sperrt den Hintergrundspeicher 0. bis 63. K Byte RAM/ROM außerhalb der ZRE. Der 1. Teil des Startprogramms wird abgearbeitet.

Dieser Teil des Startprogramms wird beendet mit einem Sprung zum 2. Teil des Startprogramms (1. GM des Hintergrundspeichers). Damit werden die Adreßleitungen AB10 ... AB13, AB15 = 1 und **/MEMDI = 1**.  
Im 2. Teil des Startprogramms wird der Lade-ROM abgeschaltet, d. h. **/LD-ROM = 0** durch das BS-PIO. Damit ist der Lade-ROM inaktiv und kann bei den Sprungadressen 0000 ... 03FF_H nicht angesprochen werden.  
Der 2. Teil des Startprogramms hat u. a. die Aufgaben:

- Tabellen für Anfangsadressen laden
    
- Merkplätze und Stack, Tabellen, Anzeigefelder ect. im 0. K (K1) festlegen und laden
    
- Aufruf der Betriebsbeginnroutinen
    
- INT-Vektor BS-PIO und CTC laden
    
- Chiptest mit Fehleranzeige
    
- Sprung in den Monitor (Funktionsauswahlprogramm)
    

![Bildunterschrift: Abb. 6 Ein- und Ausschaltung des Lade-ROM]

Soll während des Lesevorgangs des Lade-ROM in den gleichen Adreßbereich des Hintergrundspeichers eine Information geschrieben werden, wird durch **/RD = 1** der A 21/15 = 1 und **/MEMDI = 1**. Der Speicher wird freigegeben solange **/RD = 1** ist und kann beschrieben werden.

## 6.4. Rücksetzschaltung

Sind nach dem Betätigen der Netztaste alle erforderlichen Spannungen gemäß Ausstattungsvariante im Netzteil aufgebaut worden, wird durch die Ablaufsteuerung das Signal **/RESET** (aktiv low) für 2 ms gebildet und setzt die ZRE zurück (entspricht einer Anfangslöschung).  
Unter anderem erfolgen folgende Reaktionen in der ZVE:

- Rücksetzen des internen Interrupt-FFs
    
- Löschen des Befehlszählers im Programmcounter
    
- Löschen des Interrupt-Vektor-Registers I
    
- Löschen des Refresh-Adreß-Registers R
    
- Setzen INT-Mode 0
    

## 6.5. WAIT-Einblendung für ZVE2

### 6.5.1. Allgemeines

Bei der derzeitigen Speicherkonzeption mit ROM-Bausteinen von 450 ns Zugriffszeit, ist ein **/WAIT**-Takt im **/M1**-Zyklus erforderlich. Das Signal **/WAIT** wird im **/M1**-Zyklus von den Speichersteckeinheiten in Abhängigkeit vom Signal **/M1** gebildet.  
Die 2. ZVE darf das **/M1**-Signal nicht für den Systembus liefern, da sonst die Interruptprioritätenkette der Peripheriebausteine durcheinander käme.  
Würde die Umschaltung der ZVE1 und ZVE2 beispielsweise nach dem 1. Byte eines **RETI**-Befehls (Rücksprung vom Interrupt) durch **/BUSRQ** vom Floppy erfolgen, so würde das nächstfolgende Signal **/M1** in dem Falle von der ZVE2 als 2. Byte des **RETI** erkannt werden.  
**/WAIT-ZVE2** wird also nicht auf den Speichersteckeinheiten gebildet, sondern es erfolgt eine automatische **/WAIT**-Einblendung für die **/M1**-Zyklen der 2. ZVE. Werden schnellere Speicher verwendet, ist diese **/WAIT**-Einblendung programmiert abschaltbar über den BS-PIO Tor B/3 (**/WAIT-ZVE2**) auf der ZRE.

### 6.5.2. Funktionsbeschreibung

Über den Betriebssystem-PIO A36/B3 erfolgt durch das Signal **/WAIT-ZVE2 = 0** über den A37/10 die Freigabe des FF A11 (Eingang 01) und das NAND A 5/02 ist vorbereitet.  
Bei **/M1-ZVE2** inaktiv (high) wird das FF A11 über A10/02 gesetzt. Am D-Eingang liegt low vom getriggert geschalteten Ausgang A11/06.  
Wird beim Befehlsaufruf **/M1 = 0**, wird der Eingang S des FF A11 freigegeben.  
Mit der Vorderflanke des Taktes T2 wird low am D-Eingang durchgeschaltet. A11/06 = 1 setzt über A5/03 **/WAIT** auf low. Die ZVE2 erkennt die **/WAIT**-Anforderung und quittiert mit der Einblendung eines **/WAIT**-Taktes, solange das Signal aktiv ist.  
Mit der nächstfolgenden aufsteigenden Flanke des Systemtaktes wird "high" durchgeschaltet und A11/06 = 1, d. h. **/WAIT = 1**. Die ZVE2 geht wieder aus dem **/WAIT**-Zustand heraus.

![[Pasted image 20260510222819.png]]
![Bildunterschrift: Abb. 7 WAIT-Bildung (Logikschaltbild und Zeitdiagramm für Lese- und Schreibzyklus)]

## 6.6. RDY-Bildung

### 6.6.1. Allgemeines

Das Signal **/RDY** zeigt auf dem BUS an, daß der angesprochene Speicher oder das angesprochene E/A-Gerät am BUS vorhanden ist und für Lese- und Schreiboperationen (Datentransfer) zur Verfügung steht.  
Es wird aus folgenden Bedingungen, die entsprechend der BUS-Richtlinie K 1520 vorgegeben sind, gebildet:

- durch Speicher:  
    **/RDY** = "Adresse erkannt" + **/MEMDI** + **/MREQ** + **/RFSH**
    
- durch E/A-Einheiten:  
    **/RDY** = "Adresse erkannt" + **/IODI** + **/IORQ** + **/M1**
    
- durch den INT liefernden Teilnehmer:  
    **/RDY** = **/IEI** + **/IEO** + **/IORQ** + **/M1**
    

Die Bildung vom Signal **/RDY** erfolgt auf der ZRE selbst durch die Baugruppen

- Anfangslader (LD-ROM)
    
- CTC
    
- zentrale Baugruppensteuerung (BS-PIO)  
    } auch bei INT-Quittungszyklen
    

Das Signal **/RDY** wird entsprechend der Systembusrichtlinie K 1520 durch Open-Kollektor-Stufen (D 103) gebildet. Ausgewertet wird es nur im Konfigurationstest des Startprogramms in Verbindung mit BS-PIO A36, **/WR** (A5) und **/RDY** (A6).  
Von den 16 Adreßleitungen werden die niederen 8 Bit AB0 ... AB7 zur Adressierung der E/A-Tore benutzt (A29/15). Die ZRE internen E/A-Tore haben die Codierung 00 ... 0F_H, d. h. für diese Torbelegung ist der Ausgang 15 des 1 aus 8-Decoders A29 = 0 (A23/08 = 1). Ist **/IORQ** aktiv (**/IORQ = 0**), wird **/CS-E/A** gebildet.

![[Pasted image 20260510222852.png]]
![Bildunterschrift: Abb. 8 Bildung des Signals RDY]

### 6.6.2. RDY-Bildung bei Ansteuerung des LD-ROM (A26)

Bei Freigabe des LD-ROM wird durch das Signal **/LD-ROM = 1** und **/MEMDI** noch high (vor Ansprechen des LD-ROM) bei einem Speicherlesezyklus (**/RD** und **/MREQ = 0**) das Signal **/CS-ROM = 0** ("Adresse erkannt") gebildet.  
Über die interne Treibersteuerung (siehe Punkt 6.8.2.) wird - nur bei einem Lesezyklus, bei dem **/RD = 0** ist - das Signal **/RQ RDY** mit low aktiv (A38/6, A31/8, A32/8). Durch das NAND A14/6 erfolgt die Verknüpfung mit **/CS-E/A** und die Bildung von **/RDY = 0** --> aktiv (A6/6).

### 6.6.3. RDY-Bildung durch die zentrale Baugruppensteuerung

Bei E/A-Zyklen vor ZRE internen E/A-Befehlen wird mit dem Anlegen der entsprechenden Adressen am 1 aus 8-Decoder A29/15 (z. Zt. gebrückt) sofort /CS-E/A = 0 gebildet.  
Mit /IORQ = 0 erfolgt die Bildung von /RDY = 0 über das NAND A6/6 für alle auf der ZRE befindlichen Peripheriebaugruppen, wie CTC, PIO.

### 6.6.4. RDY-Bildung bei Interruptquittungszyklen

Das Signal /RDY = 0 wird auch gebildet, wenn ein Interruptquittungszyklus des CTC oder der zentralen Baugruppensteuerung auftritt. Hier macht es sich erforderlich, die Signale /IEI, /IEO1 und /IEI1 mit auszuwerten, da der CTC beispielsweise am Ende der Systembus-Prioritätenkette und die zentrale Baugruppensteuerung am Ende der Koppelbus-Prioritätenkette liegt. Dazu wird die Treibersteuerlogik A31, A32, A38, A17 mitbenutzt, indem das Signal /RQ RDY über A14/6 und A6/6 /RDY aktiviert wird.

![[Pasted image 20260510223222.png]]
![Bildunterschrift: Abb. 9 logische Struktur des programmierbaren DMA-Kanals der ZRE K 1526]

## 6.7. Programmierbarer DMA-Kanal

### 6.7.1. Allgemeines

Die ZRE K 2526 verfügt über einen programmierbaren DMA-Kanal, der die Abarbeitung von Befehlsfolgen vorzugsweise zur schnellen Datenübertragung (z. B. durch Floppy-Disk) gewährleistet. Die 2. ZVE (A8) arbeitet komplementär zur 1. ZVE (A3). Sie sind mit dem Steuersignal /BUSRQ umschaltbar, was bei Busanforderung durch das entsprechende externe Gerät (Floppy-Disk) auf low geschaltet wird.  
Die ZVE1 quittiert diese Anforderung mit dem Quittungssignal /BUSAK = low und wird inaktiv; die 2. ZVE (A8) wird aktiv geschaltet durch das Verknüpfen des Signals /BUSRQ und /BAI. Die Signale /BAI//BAO bilden am Bus nur die prioritätsbestimmende Kette zur Durchschaltung des Signals /BUSAK. /BAI ist das Eingangssignal (**/BUSAK input**) und /BAO das entsprechende Ausgangssignal (**/BUSAK output**).

### 6.7.2. Arbeitsweise der ZVE-Umschaltung

Beim Einschalten der Anlage wird für 2 ms vom Netzteil das Signal /RESET = low gebildet, das über den Treiber A18 am Rücksetzeingang des FF A11/13 liegt, das FF rücksetzt (Ausgang A11/09 liegt am Eingang /RESET der ZVE2 und setzt diese in den Grundzustand zurück). Gleichzeitig wird auch die ZVE1 zurückgesetzt.

#### - ZVE1 aktiv, ZVE2 inaktiv

Durch das Rücksetzen des FF A11 liegt am Eingang A14/10 low-Potential. A14/08 = 1 und verknüpft mit /BUSRQ = 1 (keine Busanforderung) wird /BUSRQ-ZVE1 = 1, die ZVE1 ist aktiv. A3/23 ist als Quittungssignal ebenfalls low.  
Am Exklusiv OR-Gatter A4/08 werden die Signale /BAI und /BAO ausgewertet. Liegt keine externe Busanforderung auf der /BAI//BAO-Kette vor, ist A4/08 = low und als /CS der Datentreiber A6/A7 und des Steuerbustreibers A1 wird dieser aktiviert. Die Richtungssteuerung erfolgt durch das Steuersignal /RD und /M1 (A14/11).

**Lesen**  
/RD = 0 } DIEN des Datentreiber A6 und A7 = 1  
/M1 = 1 } Datenfluß: DB ---> DO ≙ Lesen vom Systembus

**Schreiben**  
/RD = 1 } DIEN des Datentreiber A6 und A7 = 0  
/M1 = 1 } Datenfluß: DI ---> DB ≙ Schreiben auf dem Systembus

Das Exklusiv OR-Gatter A4/06 wertet die Signale /BUSRQ und /BAI aus. /BUSRQ-ZVE2 am Ausgang des A4/06 wird low, d. h. ZVE2 erkennt eine Busanforderung durch die ZVE1 und schaltet seinen Adreß- und Datenbus und die Steuersignale in den hochohmigen Zustand.

#### - ZVE1 inaktiv, ZVE2 aktiv

Erfolgt beispielsweise vom Floppy-Disk die Busanforderung durch das Signal /BUSRQ = 0 auf den Eingang 09 des NAND A9, so wird /BUSRQ-ZVE1 = 0. Die ZVE1 quittiert diese Anforderung mit dem Steuersignal /BUSAK-ZVE1 = 0 nach Beenden des Maschinenzyklus. /BAO und /BAI sind ebenfalls low und über Daten-, Adreß- und Steuerbustreiber (A1, A12, A13, A6, A7) werden freigegeben (A4/08). Sie sind aber zunächst noch hochohmig, da die 1. ZVE schon abgeschaltet und die 2. ZVE noch nicht zugeschaltet hat.  
Mit /BUSAK-ZVE1 = 1 (A4/06) wird die 2. ZVE freigegeben und mit dem letzten Takt zugeschaltet. Gleichzeitig wird das /RESET-FF (A4/09) auf low gehalten, durch low auf den Setzeingang.  
Ein Rücksetzen der 2. ZVE während ihres aktiven Zustandes ist nicht möglich und nicht erforderlich, da sich das /BUSRQ = 0 im Fehlerfall wieder abschaltet (siehe FD-Adapter).  
Die 2. ZVE ist aktiv, solange /BUSRQ = low bleibt.  
Die Anschlußsteuerung des Floppy-Disk schaltet /BUSRQ = 1, wenn der Datentransfer beendet ist. Über A4/06 wird /BUSRQ-ZVE2 = 0. Erst wenn die 2. ZVE die Abschaltanforderung mit /BUSAK-ZVE2 = 0 quittiert, wird die 1. ZVE über A9/08 wieder freigegeben. Nachfolgend auch die entsprechenden Treiber.  
Da die Eingänge /NMI und /INT der 2. ZVE fest auf "high" liegen, ist die 2. ZVE nicht interruptfähig.

#### - ZVE1 inaktiv, ZVE2 inaktiv

Erfolgt die Busanforderung durch ein externes Gerät (z. B. BUSSIAN), wird /BUSRQ = 0. /BUSRQ-ZVE1 = 0 fordert Busfreigabe der ZVE1 (A9/08). Die ZVE1 schaltet sich nach dem letzten Takt des laufenden Maschinenzyklus ab und aktiviert das Busfreigabesignal /BUSAK-ZVE1 = 0. Da die /BAI//BAO-Kette aber in diesem Fall durch ein externes Gerät gesperrt wird, liegt an den Eingängen A4/01 und A4/13 /BAO = 0 und an A4/10 /BAI = 0. /CS der Datentreiber A6/A7 und des Steuerbustreibers A1 ist high, d. h. die Treiber werden hochohmig und geben den entsprechenden BUS frei. Entsprechend wird /CS2 der Adreßbustreiber low. A12 und A13 sind ebenfalls hochohmig.  
Die 2. ZVE wird über das Exklusiv OR A4/06 durch /BAI = 1 und /BUSRQ = 0 inaktiv geschaltet (/BUSRQ-ZVE2 = 0). Eine externe Benutzung des Systembusses kann erfolgen.

## 6.8. Bustreiber und Bustreibersteuerung für den vollständigen ZRE- und Universalbus

### 6.8.1. Bussystem

16 Adreßleitungen bilden den Systembus (SA-Bus), der von den Adreßbustreibern A12/13 getrieben wird. Es werden 2 SE 8212 verwendet (unidirektional).  
Der Systemdatenbus (SD-Bus) ist bidirektional und wird über 2 4-Bit-bidirektionale Bustreiber SE 8216 getrieben, die in Abhängigkeit der Treibersteuerung der ZRE richtungsgesteuert werden.  
Die Peripheriebausteine auf der ZRE-STE sind durch einen internen Datenbustreiber (ID-Bus) - 2 SE 8216 A24/25 - vom Datentreiber A6/7 entkoppelt. Sie belasten damit den Datenbus nur mit einer Lasteinheit. An diesem Treiber sind angeschlossen:

- 1 K Byte LD-ROM
- CTC
- BS-PIO

Der Treiber für den Universalbus (U-Bus), ebenfalls 2 SE 8216 A40/41, arbeiten aus Lastgründen auch auf dem internen Datenbus. Die Treiber sind bidirektional steuerbar. Die Ausgabesteuersignale der ZVE1 und ZVE2 (/MREQ, /IORQ, /RD, /WR) werden über 4 Zwei-Eingang-AND A2 und dem Bustreiber A1 SE 8212 getrieben.  
Die Ausgänge dieser Treiber sind nachfolgend auf dem Systembus zusammengeführt.

Durch den SE 8212 A1 werden auch die Signale  
/RFSH-ZVE1 . /RFSH-ZVE2  
/M1-ZVE1  
/HALT-ZVE1 verstärkt.

Die Signale /RESET und /BUSRQ sind über den ständig aktivierten Treiber A18 angeschlossen. Zusätzlich wird über diesen Treiber noch der Systemtakt und der Ausgang des Speicherschutz-RAM geführt. (Siehe Blockschaltbild Abb. 3 und Punkt 4.).

### 6.8.2. Treibersteuerung

Die Treibersteuerung muß in Abhängigkeit vom Arbeitszustand des Systems die Richtung und die Zu- bzw. Abschaltung der Bustreiber steuern.  
Folgende Bedingungen sind möglich:

#### - ADT (Adreßdatentreiber A12/13)

Im normalen Betrieb sind die Treiber auf Ausgabe geschaltet (DI ———> DO). Sie schalten ihre Ausgänge hochohmig, wenn der Adreßbus von einem externen Baustein oder Gerät benötigt wird (z. B. DMA-Anforderung durch ein Bus-Simulations-Gerät). Die Anforderung erfolgt mit dem Signal /BUSRQ = low. Die ZVE1 bestätigt nachfolgend die Anforderung mit dem Signal /BUSAK (A3), das auf low geht (≙ ZVE1 hochohmig).  
Dieses Signal schaltet unter der Voraussetzung, daß die /BAI-/BAO-Kette nicht bis zur ZRE durchgeschaltet ist (Anmeldung einer Busanforderung durch ein externes Gerät), den Adreßbustreiber in den hochohmigen Zustand (A4/08 ———> A10/08).

#### - DT (Datentreiber A6/7)

Sie sind bidirektional. Je nach Art der Operation werden sie richtungsgesteuert in Abhängigkeit des Signals /RD (A14/12) und /M1 (A14/13).

/RD = low ———> Lesen zur ZVE  
/RD = high ———> Schreiben von ZVE  
/M1 = low ———> Lesen des INT-Vektors im Interruptquittungszyklus

TS2 = /M1 . /RD

Gleichermaßen werden auch die Adreßbustreiber aktiviert.

TS3 = **TST**

#### - SST 1; 2 (Steuersignaltreiber A1)

Der Steuersignaltreiber wird entsprechend den Datentreibern zu- bzw. abgeschaltet. Es entspricht TS6 = TS3.

Die Steuersignale /WR, /RD, /IORQ und /MREQ der 1. und 2. ZVE werden über das AND A2 (T108) jeweils verknüpft und über den Steuerbustreiber A1 auf den Systembus geführt. Über diesen Treiber sind auch die Steuersignale /M1-ZVE1, /HALT und /RFSH zum Steuerbus geschaltet. Das Aktivieren dieses Treibers erfolgt über das Auswahlsignal /CST (A1/01).

#### - /DTI (Datentreiber intern A24/25)

Der Datentreiber für den internen Datenbus wird nur in seiner Richtung umgeschaltet, d. h. /CS = low.  
Seine Arbeitsweise ergibt sich aus folgenden Bedingungen:

- Ausgabe auf Tastatur (out)  
    Lampenansteuerung out 05_H  
    Ansteuerung Fehlerlampe out 03_H
    
- Eingabe von Tastatur  
    Abfrage Tastencode inp 06_H  
    Abfrage Schlüsselschalter inp 01_H
    
- Lesen LD-ROM
    
- INT-Quittungszyklus des CTC
    
- INT-Quittungszyklus des PIO
    
- externe Speicher- oder E/A-Zyklen der 1. Prioritätenkette
    
- externe Speicher- oder E/A-Zyklen der 2. Prioritätenkette
    

![[Pasted image 20260510223719.png]]
![Bildunterschrift: Abb. 10 Logik der internen Treibersteuerung]

#### - /DTU (Datentreiber Universalbus A40/41)

Die Datentreiber für den Universalbus UB0 ... UB7 werden durch die Adressen 00_H bis 07_H, die vom universellen E/A-Tor (siehe auch Punkt 6.9.5.2.) aus den auf den Adreßleitungen bereitgestellten Toradressen decodiert werden, ausgewählt (/CS = low).  
Das erfolgt über die internen Signale /CST1 und /CST2 (A28).

**/CS_DTU** = TS8 = /CST1 . /CST2

Die Richtungsumschaltung wird durch die interne Bustreiberumschaltung gesteuert. Sie ist identisch mit der des internen Datenbustreibers.

TS7 = /TS9

Die Auswahlsignale für den Uni-Bus /UCS1 ... /UCS5 werden direkt über den 1 aus 8-Decoder A27 getrieben und auf den Steckverbinder X5 der ZRE geführt.

- **SST 3 (Steuersignaltreiber A18)**
    

Der Signaltreiber A18 ist als Eingangsverstärker für die Signale /BUSRQ und /RESET geschaltet. Die Eingänge liegen permanent auf low.

- **BST (Bussignaltreiber)**
    
![[Pasted image 20260510223955.png]]
![Bildunterschrift: Abb. 11 Blockschaltbild Treibersteuerung (komplett)]

## 6.9. ZRE interne E/A-Tore

### 6.9.1. Allgemeines

Die unteren 8 Bit des Adreßbusses AB0 ... AB7 sind mit /IORQ als E/A-Geräteadresse gültig. Bei den ZRE internen E/A-Adressen ist die Gruppenadresse AB4 ... AB7 low.  
Die Adreßbits AB0 und AB1 werden für die direkte Ansteuerung der peripheren Schaltkreise verwendet, z. B. beim BS-PIO A36 für die Portauswahl AB0 und AB1 für die C/D-Auswahl; beim CTC A35 zur Auswahl der Kanäle 0 bis 3 (siehe Adressenbelegung für interne E/A-Tore).  
Sie besitzen die gleichen Adreßwahlgruppen wie die 4 Auswahlleitungen /UCS1 ... /UCS4 (siehe universelles E/A-Tor).

### 6.9.2. Adressenbelegung für interne E/A-Tore der ZRE K 2526

|                    |           |            |               |
| ------------------ | --------- | ---------- | ------------- |
| Adressen AB3/2/1/0 | Codierung | Bedeutung  |               |
| 0000               | 00_H      | /CST1      | /INT-BS       |
| 0001               | 01_H      | /CST1      | /UCS2         |
| 0010               | 02_H      | /CST1      | /RES-SPA      |
| 0011               | 03_H      | /CST1      | /UCS4         |
| 0100               | 04_H      | /CST2      | **/RES-ZVE2** |
| 0101               | 05_H      | /CST2      | /UCS3         |
| 0110               | 06_H      | /CST2      | /UCS1         |
| 0111               | 07_H      | /CST2      |               |
| 1000               | 08_H      | /CS-PIO-BS |               |
| 1001               | 09_H      | /CS-PIO-BS |               |
| 1010               | 0A_H      | /CS-PIO-BS |               |
| 1011               | 0B_H      | /CS-PIO-BS |               |
| 1100               | 0C_H      | /CS-CTC    | Kanal 0       |
| 1101               | 0D_H      | /CS-CTC    | Kanal 1       |
| 1110               | 0E_H      | /CS-CTC    | Kanal 2       |
| 1111               | 0F_H      | /CS-CTC    | Kanal 3       |

Die Adreßgruppe AB4 ... AB7 = 0 spezifiziert die Adressierung eines ZRE internen Tores. Diese 4 Adreßleitungen werden am 1 aus 8-Decoder A29 decodiert und bilden bei obiger Codierung am Ausgang A29/15 = low. Dadurch werden Auswahl-Decoder A20 und A27 freigegeben (Eingänge /E1 und /E2).

![[Pasted image 20260510224129.png]]
![Bildunterschrift: Abb. 12 ZRE internes E/A-Tor]

### 6.9.3. Ansteuerung des BS-PIO A36

Die Auswahl des Betriebssystem-PIO erfolgt mit dem Signal /CS-PIO (aktiv low). Dieses Signal wird lt. Torbelegung aus den Adressen AB2 und AB3 am 1 aus 8-Decoder A20 (Eingang 01 und 02) gebildet und mit dem Systemsignal /MREQ = 1 (inaktiver Zustand) bereitgestellt. Durch die Adressen 08_H, 09_H, 0A_H und 0B_H (unteren 8 Bits) wird bei Decodierung eines ZRE internen E/A-Befehls, d. h. /CS-E/A = 0 durch A29, dieses Signal /CS-PIO = 0 am Ausgang A20/13 aktiviert und der BS-PIO angesteuert.

### 6.9.4. Ansteuerung des Zähler/Zeitgebers A35

Die Auswahl des CTC A35 erfolgt durch das Signal /CS-CTC (aktiv low). Gebildet wird es aus den Toradressen

|      |         |
| ---- | ------- |
| 0C_H | Kanal 0 |
| 0D_H | Kanal 1 |
| 0E_H | Kanal 2 |
| 0F_H | Kanal 3 |

der unteren 8 Adreßleitungen des Adreßbusses. Es ist zu beachten, daß /CS-CTC genau wie 
/CS-PIO mit dem Systemsignal /MREQ = 1 bereitgestellt wird, da das Signal zeitlich vor 
/IORQ = 0 am Baustein anliegen muß.

### 6.9.5. Universelles Ein-/Ausgabe-Tor

#### 6.9.5.1. Struktur und Anschlußbedingungen

Das universelle E/A-Tor besteht aus den bidirektionalen Treibern für die 8 Datenleitungen des Systembusses und 4 Auswahlleitungen zur Übergabe oder Übernahme der Dateninformation.

![[Pasted image 20260510224306.png]]
![Bildunterschrift: Abb. 13 logische Struktur des universellen E/A-Tores]

#### 6.9.5.2. Funktion

Das universelle E/A-Tor ist komplett an den Steckverbinder X5 der ZRE geführt. Über diesen erfolgt die Ankopplung der Tastatur.  
Folgende Leitungen gehen auf diesen Steckverbinder:

- UB0 ... UB7: 8 Datenleitungen
    
- /UCS1 ... /UCS4: 4 Auswahlleitungen
    
- /UINT: universelle Interruptleitung
    
- /SA: Sondersignalleitung
    
- Stromversorgungsleitungen für 5 P, 5 N, 12 P, 5 PH und 00
    

Die 8 Datenleitungen UB0 ... UB7 werden über die Treiber A40/41 geführt. Sie sind bidirektional betreibbar und werden durch das Signal /DIEN richtungsgesteuert. /CS wird gebildet aus den Signalen /CST1 und /CST2 (A28), entsprechend der auf dem Adreßbus bereitgestellten Toradresse. Die Decodierung erfolgt am A20.  
Es gilt für das universelle E/A-Tor die Adressenbelegung:

|   |   |   |   |   |
|---|---|---|---|---|
|AB0 ... AB7|A3... A0|A22|A29|Bedeutung|
|06_H|0110|/CST2|/UCS1|Abfrage Tastencode ———> input|
|01_H|0001|/CST1|/UCS2|Abfrage nach einer gültigen Tasteninformation bzw. n. der Bedienberechtigung ———> input|
|05_H|0101|/CST2|/UCS3|Lampenansteuerung (Selektoren) ———> output|
|03_H|0011|/CST1|/UCS4|Ansteuerung Fehlerlampe bzw. der Anzeige für /INS-MODE ———> output|

Die Adreßgruppe AB4 ... AB7 ist bei ZRE internen E/A-Befehlen low.  
Die 4 Auswahlleitungen /UCS1 ... /UCS4 werden also aus den unteren 8 Bit des Adreßbusses gebildet und mit dem Systemsignal /IORQ bereitgestellt. Die Daten müssen 800 ns nach /UCS_n = 0 am Peripheriesteckverbinder bereitstehen bzw. 850 ns danach abgeholt sein.  
/UCS1 bis /UCS4 sind aktiv low, MOS/TTL-kompatibel (max. 10 mA).  
/CST1 und /CST2 werden am 1 aus 8-Decoder A20 mit /MREQ freigegeben, da über diesen Baustein auch die Toradressen /CS-PIO und /CS-CTC aktiviert werden können. Diese internen E/A-Tore müssen angesteuert werden, bevor /IORQ z. B. am BS-PIO anliegt. Die Signale /UINT und SA sind vom Steckverbinder X5 auf den Steckverbinder X2 durchgezogen.  
/UINT kann im System als Unterbrechungsleitung der angeschlossenen Peripherie weiterverarbeitet werden.  
SA auf dem Koppelbus wird als Ein- bzw. Ausschalsignal des Rechners benutzt.

**Stecker X5:**

|   |   |   |
|---|---|---|
|Pin|A|B|
|1|00|00|
|2|5 PH|5 N|
|3|UB1|UB0|
|4|UB3|UB2|
|5|UB5|UB4|
|6|UB7|UB6|
|7|5 P|/UINT|
|8|/UCS4|SA|
|9|/UCS2|/UCS1|
|10|5 P|/UCS3|
|11|5 P|5 P|
|12|12 P|5 P|
|13|00|5 P|

## 6.10. Zentrale Baugruppensteuerung

### 6.10.1. Struktur

Die zentrale Baugruppensteuerung erfolgt mit dem Schaltkreis M 301, einem Parallel-I/O-Interfacebaustein (PIO), der den Datenverkehr zwischen dem Mikroprozessor und der Umwelt (≙ "Peripherie") gewährleistet. In der K 2526/27 ist es der Betriebssystem-PIO (BS-PIO).  
In der Prioritätenkette des Rechnersystems ist er an niedrigster Stelle des Koppelbusses angeordnet (siehe Abb. 1). Er erfüllt Steuer- und Überwachungsfunktionen der ZRE.

|                   |                 |     |            |                                     |
| ----------------- | --------------- | --- | ---------- | ----------------------------------- |
|                   |                 |     |            |                                     |
|                   |                 | A0  | /M1        | Einzelbefehlsabarbeitung ——— IS     |
|                   |                 | A1  | /SUE       | Spannungsüberwachung ——— X2, C22    |
|                   |                 | A2  | /NMI       | Netzausfall/E/A-Schutz ——— X1, A23  |
| **Daten**         | PIO M 301 / A36 | A3  | SPS-Ind.   | Speicherschutzindikator ——— IS      |
|                   |                 | A4  | /EBF       | Einzelbefehlsabarbeitung ——— IS     |
| **Steuersignale** |                 | A5  | /WR        | Speicherausstattung ——— X1, A08     |
|                   |                 | A6  | /RDY       | Peripherie und Speicher ——— X1, C25 |
|                   |                 | A7  | /MEMDI1/2  | Speicherbereichsumschaltung ——— IS  |
|                   |                 |     |            |                                     |
|                   |                 | B0  | /LD-ROM    | LD-ROM Abschaltung ——— IS           |
|                   |                 | B1  | /INT-BS    | Aufruf Betriebssystemebene ——— IS   |
|                   |                 | B2  | /SPS-ESR   | Speicherschutz-Einschreiben ——— IS  |
|                   |                 | B3  | /WAIT-ZVE2 | WAIT-Einblendung ZVE2 ——— IS        |
|                   |                 | B4  | /SA        | Sonderausgang Netz aus ——— X5 B08   |
|                   |                 | B5  |            |                                     |
|                   |                 | B6  |            | Gerätekonfiguration                 |
|                   |                 | B7  |            |                                     |

IS = ZRE internes Signal

![[Pasted image 20260510224511.png]]
![Bildunterschrift: Abb. 14 Betriebssystem-PIO]

### 6.10.2. Funktion

Tor A und B des BS-PIO arbeiten im Bitmode. Dabei wird der Interruptvektor vom Tor A für die Einzelbefehlsabarbeitung von Anwenderbefehlen benutzt und der von Tor B zum Aufruf der Betriebssystemebene.  
Wird durch den Adreßdecoder (A29) bei entsprechender Adresse ein interner E/A-Befehl decodiert (/CS-E/A), aktiviert nachfolgend über den 1 aus 8-Decoder A27 der out 00 (/INT-BS) den Eingang B1 des PIO und löst einen Interrupt aus.

**A0 - /M1 (Eingang - aktiv low):**  
/M1 löst bei gewünschter Einzelbefehlsabarbeitung einen Interrupt aus. Der Interruptvektor Tor A dient zur Adressierung der vom Anwender durch die Makrobefehle (Ein-/Ausgabebefehle) bestimmbare Einzelbefehlsabarbeitungsroutinen. Wird keine Einzelbefehlsabarbeitung gewünscht, muß dieser PIO-Eingang maskiert werden, so daß er keinen Interrupt auslösen kann.

**A1 - /SUE (Eingang - aktiv low):**  
Das Signal /SUE wird beim Unterschreiten einer bestimmten Ladespannung der Batterie bei CMOS-Speichersteckeinheiten verwendet. Es löst einen Interrupt aus. Da derselbe Interruptvektor bei der Einzelbefehlsabarbeitung verwendet wird, muß als erstes in der Unterbrechungsroutine eine Abfrage von A0 und A1 erfolgen. Die Interruptbedingung an A0 ist flüchtig, die an A1 permanent.

**A2 - /NMI (Eingang - aktiv low):**  
Durch eine Sonderbedingung (z. B. Netzeinbruch) oder durch einen unerlaubten E/A-Befehl (siehe Punkt 6.11.) wird die ZVE durch einen nichtmaskierten Interrupt unterbrochen. Der Eingang A2 ermöglicht ein Unterscheiden dieser beiden Fälle und wird in der /NMI-Routine abgefragt.  
A2 = low ———> Sonderbedingung (permanent)  
A2 = high ———> unerlaubter E/A-Befehl (flüchtig)

**A3 - SPS-Indikator (Eingang - aktiv low):**  
Bei unerlaubtem Zugriff auf einen geschützten Speicherbereich (siehe Speicher- und E/A-Schutz Punkt 6.11.) löst dieser Eingang eine Unterbrechung am Tor A aus. Der Interruptvektor ist der gleiche wie bei der Erfüllung der Interruptbedingungen für A0 und A1, d. h. vor Abarbeitung der adressierten Interruptroutine muß die Leitung A3 abgefragt werden. Wird dieser Eingang nicht benötigt, muß er bei der Programmierung maskiert werden.

**A4 - /EBF (Eingang - aktiv low):**  
Nach jedem Befehl niederer Ebene (z. B. Anwenderebene) als der des Tores A des BS-PIO kann eine Unterbrechung ausgelöst werden.  
Bei entsprechender Programmierung des PIO löst das Signal /M1 (A36/15) und /EBF (A36/10) einen Interrupt aus. Anschließend wird zur Routine der Einzelbefehlsabarbeitung gesprungen und diese abgearbeitet.

**A5 - /WR (Eingang - aktiv low):**  
**A6 - /RDY (Eingang - aktiv low):**  
Diese 2 Eingänge können zum Speicher- und Peripherietest während des Startprogramms verwendet werden. Bei angesprochenem Speicher oder Peripheriebaustein (/CS = aktiv) wird von der betreffenden Steckeinheit das Systemsignal /RDY gebildet, wenn die Tor-Adressen einen Anschluß adressieren, der vorhanden ist. In der Anfangsroutine - nach dem Einschalten des Gerätes - wird der BS-PIO außerhalb seiner normalen Arbeitsweise programmiert, d. h.

Tor A ———> geladen mit dem Interruptvektor, der die Testroutine adressiert.  
Bitmode  
Das Tor A ist so programmiert, daß für den Speichertest z. B. /WR = 0 und /RDY = 0 eine Unterbrechung auslösen kann.  
Bei diesem Test müssen die anderen Eingänge maskiert sein.  
Tor B ———> bleibt im Three-State-Zustand

In der normalen Betriebsweise der ZRE müssen diese Eingänge maskiert sein.

**A7 - MEMDI1/2 (Ausgang):**  
Der Ausgang dient zur Speicherbereichsumschaltung durch die Systemsignale /MEMDI1 und /MEMDI2. Dabei gilt:

A7 = 0 ———> /MEMDI1 = 0 (aktiv)  
/MEMDI2 = 1 (inaktiv)

A7 = 1 ———> /MEMDI1 = 1 (inaktiv)  
/MEMDI2 = 0 (aktiv) unter der Bedingung, daß /MEMDI = 1 ist (A38/3/11, A39/3/11)

Bei /MEMDI = 0 wird der Speicherbereich 0. bis 63. K, bei /MEMDI2 = 0 wird der Adreßbereich 64. bis 127. K Byte adressiert. Ist /MEMDI = 0, sind beide Speicherbereiche gesperrt.

**B0 - /LD-ROM (Ausgang - aktiv low):**  
Mit diesem Signal kann der Lade-ROM (siehe Punkt 6.3.) nach dem 1. Teil des Anfangsladeprogramms programmiert abgeschaltet werden. Damit ist der Adreßbereich 0. K für den Hintergrundspeicher frei (RAM). Im Einschaltmoment ist der Ausgang hochohmig, bzw. besitzt durch den Ziehwiderstand R3:4 das Potential high. Der Lade-ROM ist in diesem Zustand zugeschaltet.

**B1 - /INT-BS (Eingang - aktiv low):**  
Der Eingang B1 löst am Tor B die Unterbrechung zum Sprung in die Betriebssystemebene aus. Die Leitung wird durch einen E/A-Befehl aus der Makroebene durch den out 00_H aktiviert. Dafür wird der Interruptvektor des Tores B benutzt.

**B2 - /SPS-ESR (Ausgang - aktiv low):**  
Das Signal ist im Einschaltmoment logisch high (R3/5) und gibt den Speicherschutz-RAM A19 zum Beschreiben frei. Um die Speicher- und E/A-Schutzfunktion freizugeben, muß dieser Ausgang auf low programmiert werden.

**B3 - /WAIT-ZVE2 (Ausgang - aktiv high):**  
Bei Verwendung von Speicherbausteinen mit einer Zugriffszeit ≧ 450 ns, ist dieses Signal low (siehe WAIT-Einblendung ZVE2, Punkt 6.5.) ehe ein Zugriff auf diese Speicher erfolgt.

**B4 - /SA (Ausgang - aktiv low):**  
Sonderausgang zum programmierten Ausschalten der Anlage. Mehrmalige Übergänge auf low-Potential in einem bestimmten Zeitintervall, führen über die Open-Kollektor-Stufe des Netzteils zum "Netz aus".

**B5, B6, B7 - (Eingänge)**  
Über die Brücken W6:2 1 bis 3 kann eine bestimmte Gerätekonfiguration codiert werden (z. B. Ausstattungsvariante der Peripherie). Über die 3 PIO-Eingänge kann diese Konfiguration abgefragt werden.

| Tor B | keine BAB | BAB1 | BAB2 | BAB3 |  
| :--- | :--- | :--- | :--- | :--- | :--- |  
| B5 | - | **x** | - | **x** | |  
| B6 | - | - | **x** | **x** | |  
| B7 | - | - | - | - | **x** ≙ "Brücke" |

## 6.11. Speicher- und E/A-Schutz

### 6.11.1. Schutzaufgabe

Die Schutzschaltung hat zwei wesentliche Aufgaben. Einmal sollen unbefugte Schreiboperationen in geschützte Speicherbereiche (Betriebssystem im RAM) verhindert werden, zum anderen sollen unerlaubte E/A-Operationen, die nicht unter Kontrolle des Betriebssystems verlaufen (E/A-Steuerung, RST1-Routine) abgebrochen werden.  
Um die Befehle auf ihre Zugehörigkeit zum BS oder Anwender testen zu können, werden sie in 2 Bereiche eingeteilt:

Bereich 1 ≙ Betriebssystem  
Bereich 2 ≙ Anwenderbereich

Diese Zugehörigkeit eines Befehls zum Bereich 1 oder 2 wird in dem 1 K RAM (A19) programmiert. Bei einer Speicherkapazität von max. 64 K kann mit einem Bit des RAM-Speichers ein Bereich von 64 Bytes im Hintergrundspeicher geschützt werden.  
Der Adreßbereich von 0. bis 3. K kann also in 64 Byte Schritten für die Schutzfunktion aufgeteilt werden.

"1" ≙ geschützter Bereich (BS)  
"0" ≙ ungeschützter Bereich (Anwender)

Bei einem unberechtigten Schreibzyklus auf den Speicher wird durch das Signal /MEMDI = 0 abgeschaltet. Bei einer unerlaubten E/A-Operation wird ein nichtmaskierter Interrupt (/NMI) gebildet. Er wird erkannt durch die zentrale Baugruppensteuerung. Durch sie ist auch eine Unterscheidung zwischen einem permanent anliegenden /NMI und dem flüchtigen /NMI des unerlaubten E/A-Befehls möglich.

![[Pasted image 20260510225330.png]]
![Bildunterschrift: Abb. 15 logische Struktur des Speicher- und E/A-Schutzes]

### 6.11.2. Beschreiben des RAM

Das Beschreiben des RAM kann nur aus dem Bereich 1 erfolgen. Durch Tor B/3 des BS-PIO (A36) wird das Beschreiben des RAM freigegeben (A33/13).  
Zu Betriebsbeginn oder nach einem /RESET-Signal ist die Schutzschaltung automatisch in Stellung "Schreiben". Die Schutzfunktionen sind unwirksam. Dadurch wird ein Speichersperren des Anfangsladers (Lade-ROM) bei noch undefinierbarem RAM-Inhalt verhindert. Über AB0 des Adreßbusses erfolgt die Programmierung des Speicherschutz-RAM. /CS ist low. Zeitlich geschieht das im 2. Teil des Startprogramms und wird durch den BS-PIO A36 wieder abgeschaltet.

### 6.11.3. Speicherschutz

Befehle aus dem Bereich 1 (Betriebssystemstatus) können mit ihrem Operandenteil den Bereich 2 und 1 adressieren, Befehle aus dem Bereich 2 können nur im Bereich 2 arbeiten. (Gilt nur für Befehle mit Speicherzugriff nicht für Sprungbefehle).  
Im Befehlsaufrufzyklus tritt noch keine Schutzfunktion auf. Mit der Auswertung des Systemsignals /M1 wird über die Zugehörigkeit für jeden Befehlsaufruf zum Bereich 1 oder Bereich 2 gespeichert. Es erfolgt noch keine Reaktion (über A30/8 gesperrt durch /M1 oder /RFSH).  
Über A30/8 wird für Refresh-Zugriffe (/RFSH = 0) und DMA-Betrieb (/BAO) die Schutzschaltung außer Betrieb gesetzt (A28/11 ———> A39/8 ———> /MEMDI = 1). Mit der letzten abfallenden Flanke des Systemtaktes während /M1 = aktiv ist (A30/6), wird das FF "Befehlsaufruf" auf seinen Wert entsprechend dem am Eingang A34/2 liegenden Wert gesetzt (≙ SSA).  
Ist /M1 aktiv, kann /MEMDI nicht low werden (A39/9 = 0). Ebenfalls nicht bei einem folgenden Speicher-Lesezyklus (/RD aktiv) durch A28/11 = 0 ———> A39/8 = 1 ———> /MEMDI unabhängig vom Schutzbereich.

Folgt ein Speicherschreibzyklus, wird /MEMDI aktiviert in Abhängigkeit vom Inhalt des FF "Befehlsaufruf" A34/6 und vom Ausgang A19/13 des RAM und bei unerlaubtem Speicherzugriff zum Sperren des Speichers benutzt. Das Signal /MEMDI muß eher am Speicher wirksam sein als /CS.

Beispiel:

1. Befehlsaufruf Bereich 1 SSA = 1    
2. Speicherschreibzyklus Bereich 2 SSA = 0 ———> /MEMDI = inaktiv

Beispiel:

1. Befehlsaufruf Bereich 2 SSA = 0
2. Speicherschreibzyklus Bereich 1 SSA = 1 ———> /MEMDI = aktiv

Um einen unerlaubten Speicherschreibzyklus zu erkennen, wird im A33/3 der Zustand des FF "Befehlsaufruf" A34/6 über A30/8 ———> A23/12 ———> A28/11 ≙ A33/1 mit dem Signal SSA verknüpft. Dieser Zustand wird mit der Rückflanke von /MREQ in das FF "SPS-Indikator" A34/3 eingeschrieben und löst über das Signal SPS-Ind am Tor A des BS-PIO (A3) einen Interrupt aus (bei entsprechender Programmierung des PIO). Der nachfolgende /M1-Zyklus wird noch ausgeführt (/MEMDI = 0 wird aufgehoben).  
Die Interruptanforderung wird im nächstfolgenden Zyklus erkannt und eine Fehlerbehandlungsroutine ausgeführt als Folge des verbotenen Speicherzugriffs. Der Ausgang A34/9 wird rückgekoppelt auf A33/9, um zu verhindern, daß ein nachfolgender Zyklus erneut Interrupt auslöst (/MEMDI = 0 wird aufgehoben).  
Das Merkmal für unerlaubten Speicherzugriff (A34/9) wird mit /RES-SPA zurückgesetzt. Dieses Signal wird vom BS-PIO gebildet und entspricht einem E/A-Befehl mit der Adresse 2_H und der entsprechenden Gruppenadresse (ZRE vorzugsweise 02_H).  
Bei unerlaubtem E/A-Befehl ist die Funktion der Schaltung analog, nur daß mit dem Signal /IORQ über A39/6 ein nichtmaskierter Interrupt (/NMI) ausgelöst wird. Dabei bildet sich /MEMDI = 0. Die Codierung der Adreßleitungen AB0 ... AB7 entsprechen der Toradressen, AB8 ... AB15 sind undefiniert. Das ergibt einen undefinierten Wert am Ausgang des RAM. /MEMDI wird mit der Vorderflanke des /M1 im /NMI-Quittungszyklus zurückgesetzt.

## 6.12. Bildung des Steuersignals /IEP

### 6.12.1. Allgemeines

Ein Rücksprung aus der Interruptroutine erfolgt mit dem 2 Byte-Befehl RETI (Codierung ED, 4D). Dieser Befehl holt mit dem 1. Byte den vor Beginn der UP-Routine gültigen PC-Stand aus dem Stack in den Befehlszähler und mit dem 2. Byte wird der gerade im Bearbeitung befindliche Baustein in den Grundzustand gesetzt.  
Nur der Baustein, dessen /IEI-Eingang auf high und /IEO-Ausgang auf low (/INT = 1) liegt, bezieht diesen RETI-Befehl auf sich und schaltet in den Grundzustand zurück, d. h. /IEI und /IEO = 1. Die nachfolgenden Bausteine in der Prioritätenkette werden damit für eine /INT-Anmeldung wieder freigegeben.  
Meldet ein Baustein höhere Priorität zu einem Zeitpunkt einen Interrupt an, wo der gerade in Bearbeitung befindliche die UP-Routine noch nicht beendet hat (EI-Befehl steht am Ende der INT-Routine), wird der Ausgang des prioritätsmäßig höherem Bausteins low, obwohl seine /INT-Anforderung nicht bearbeitet werden kann. Das bedeutet gleichzeitig, daß bei /IEI = low der in Bearbeitung befindliche Baustein den RETI nicht interpretieren könnte. Um das zu verhindern, schaltet der Baustein höherer Priorität mit nicht quittiertem Interrupt beim 1. Byte des RETI-Befehls seinen /IEO-Ausgang kurzfristig auf high. Damit ist die Kette bis zum bedienten Baustein freigegeben und dieser kann seine INT-Routine durch das 2. Byte des RETI-Befehls beenden.  
Die kurzfristige Durchschaltung des high-Pegels beim 1. Byte des RETI-Befehls von einem prioritätsmäßig höher gelegenen Baustein durch alle interruptberechtigten Bausteine erfordert eine gewisse Durchschaltzeit. Diese Durchschaltzeit vom ersten bis zum letzten Baustein muß kleiner sein als die Zeit des Auftretens des 1. und 2. Byte des RETI-Befehls (≦ 7 Bausteine). Um die Prioritätenkette auf > 7 Bausteine verlängern zu können, wird bei jedem Zyklus das Zusatzsignal /IEP (interrupt enable parallel) gebildet. Es liegt parallel an allen Bausteinen an, die einen Interrupt anmelden können.

### 6.12.2. Funktion

![[Pasted image 20260510225728.png]]
![Bildunterschrift: Abb. 16 Bildung des Steuersignals /IEP]

Bei jedem Zyklus wird durch /RFSH das Signal /IEP = 0 gebildet und ermöglicht ein sicheres Erkennen des RETI-Befehls durch den sich in Behandlung befindlichen Baustein und damit den Rücksprung aus der Interruptroutine.  
Bei /RFSH = 0 (T3, T4) wird mit der Rückflanke des Systemtaktes (T3) der Ausgang A16/08 = 1. Das nachfolgende FF wird freigegeben und mit der Rückflanke des Steuersignals /MREQ (T4) wird das Signal /IEP = low aktiv. Dieses Signal liegt parallel am Eingang /IEI aller interruptfähiger Peripheriebausteine und legt deren Eingänge kurzfristig auf high. Nach > 200 µs werden beide FF A16 mit der fallenden Flanke des Systemtaktes T1 wieder zurückgekippt.

# **robotron**

**VEB Robotron**  
**Buchungsmaschinenwerk**  
**Karl-Marx-Stadt**  
PSF 129  
Annaberger Straße 93  
Karl-Marx-Stadt  
DDR 9010

Exporteur:  
**Robotron - Export/Import**  
Volkseigener Außenhandelsbetrieb  
der Deutschen Demokratischen Republik  
PSF 11  
Allee der Kosmonauten 24  
Berlin  
DDR 1140

831.53.01.001

---
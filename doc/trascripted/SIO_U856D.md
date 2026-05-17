# Schaltkreis für serielle Ein- und Ausgabe SIO U 856 D

Mikroprozessorsystem
der II. Leistungsklasse

— Technische Beschreibung —

*KI generierte Interpretation der Abbildung: Die Abbildung zeigt das schematische Blockschaltbild eines Mikroprozessorsystems. Im oberen Teil ist die CPU über einen 16-Bit Adreßbus, einen 8-Bit Datenbus und einen Steuerbus mit dem Programmspeicher und dem Arbeitsspeicher verbunden. Unterhalb der Busse sind vier Peripheriebausteine in blauen Boxen dargestellt: PIO, SIO, CTC und DMA. Der SIO-Baustein ist orange hervorgehoben. Eine horizontale graue Linie verbindet alle vier Peripheriebausteine und ist als „Interrupt Kaskade“ beschriftet. Pfeile zeigen die bidirektionale Verbindung der Bausteine zu den Systembussen sowie nach unten zur „Peripherie“.*

Schaltkreis für serielle Ein- und Ausgabe
SIO U 856 D

Die technischen Angaben dieses Handbuches tragen reinen Informationscharakter. Verbindliche technische Liefer- und Reklamationsgrundlage sind ausschließlich die Typstandards. Die vorliegende Dokumentation gibt keine Auskunft über Liefermöglichkeiten und beinhaltet keine Verbindlichkeiten zur Produktion.

**Inhaltsübersicht**

| Kap. | Inhalt | Seite |
| :--- | :--- | :--- |
| 1. | Einführung | 5 |
| 2. | Aufbau der SIO U 856 D | 6 |
| 3. | SIO-Zeitabläufe | 13 |
| 4. | E/A-Betriebsarten | 16 |
| 4.1. | Funktionsbeschreibung | 16 |
| 4.2. | Asynchrone Betriebsart | 19 |
| 4.2.1. | Asynchrones Senden | 19 |
| 4.2.2. | Asynchroner Empfang | 20 |
| 4.3. | Synchrone Betriebsart | 23 |
| 4.3.1. | Synchrones Senden | 25 |
| 4.3.2. | Synchroner Empfang | 30 |
| 4.4. | SDLC (HDLC) Betriebsart | 34 |
| 4.4.1. | SDLC-Senden | 34 |
| 4.4.2. | SDLC-Empfang | 39 |
| 5. | Programmierung des Schaltkreises U 856 D | 43 |
| 5.1. | Schreibregister | 44 |
| 5.2. | Leseregister | 55 |
| 6. | Technische Daten | 60 |
| 6.1. | Elektrische Kennwerte | 60 |
| 6.2. | Dynamische Kennwerte | 62 |
| 6.3. | Gehäuse | 65 |

**1. Einführung**

Der serielle Ein-Ausgabeschaltkreis U 856 D ist ein in n-Kanal-Silicon-Gate-Technologie gefertigter programmierbarer, zweikanaliger Baustein, der Daten in das für serielle Datenübertragung erforderliche Format umsetzt.
Der SIO ist in der Lage, asynchrone und synchrone byteorientierte Formate wie IBM-Bisync und synchrone bit-orientierte Formate wie HDLC und IBM SDLC zu verarbeiten. Der U 856 D kann durch die Systemsoftware in seiner Funktion in großer Variationsbreite an die Bedingungen eines speziellen seriellen Datenprotokolls angepaßt werden (z. B. Kassetten- oder Floppy-Disk-Interface). Der SIO kann CRC-Kodes in jeder synchronen Betriebsart erzeugen und prüfen. Desweiteren ist er als Modemsteuergerät in beiden Kanälen geeignet. In Anwendungsfällen, in denen diese Kontrollfunktionen nicht benötigt werden, kann die Modemsteuerung für allgemeine Ein-/Ausgabezwecke verwendet werden. Der U 856 D ist ein Schaltkreis innerhalb des Systems der 2. Leistungsklasse. Er ist vorwiegend für den Einsatz in Datenverarbeitungsanlagen und Anlagen der Steuerungs- und Regelungstechnik vorgesehen.

**Eigenschaften**

- 4 unabhängige serielle Ports: zwei Sender- sowie Empfängerports
- Datenübertragungsrate: 0 bis 550 kBit/s
- Empfänger-Datenregister vierfach gepuffert, Sender doppelt gepuffert
- Asynchrone Betriebsart:
    - 5, 6, 7 oder 8 Bits/Zeichen
    - 1, 1 1/2 oder 2 Stoppbits
    - gerade, ungerade oder keine Parität
    - Taktvarianten: x1, x16, x32, x64
    - Break-Erzeugung und -Erkennung
    - Paritäts-, Überlauf- und Rahmenfehlererkennung
- Synchrone Betriebsart:
    - Interne oder Externe Zeichensynchronisation
    - Ein oder zwei Synchronisationszeichen in getrennten Registern
    - Automatisches Einfügen von Synchronisationszeichen
    - CRC-Erzeugung und Kontrolle
- HDLC- und IBM-SDLC-Betriebsart:
    - Null-Einfügung und -Ausblendung
    - Automatisches Flag-Einfügen
    - Adreßfeld-Erkennung
    - I-Feld Residuum-Behandlung
    - gültige empfangene Daten vor Überschreiben geschützt
    - CRC-Erzeugung und Kontrolle
- 8 Modem-Steuer-Ein- und Ausgänge
- CRC-16- oder CRC-CCITT-Blockkontrolle
- Die Interrupt-Logik (Daisy-Chain-Priorisierung) bietet eine automatische Interrupt-Vektorbildung ohne externe Logik.
- Der Modemstatus kann überwacht werden.

**2. Aufbau der SIO U 856 D**

Der SIO U 856 befindet sich in einem standardisierten Dual-In-Line-Gehäuse mit 40 Anschlüssen.
Bild 1 zeigt die Anschlußbelegung und das logische Schaltbild des U 856 D.

*KI generierte Interpretation der Abbildung: Bild 1 zeigt die physische Pinbelegung eines 40-poligen DIL-Gehäuses für die Bondvariante U 8560. Auf der linken Seite befinden sich Datenleitungen (D1, D3, D5, D7), Interrupt (INT), Prioritätskette (IEI, IEO), M1-Zyklus, Versorgungsspannung (Ucc), Wait/Ready A, Sync A, Empfangsdaten A (RxDA), Empfangstakt A (RxCA), Sendetakt A (TxCA), Sendedaten A (TxDA), Terminalbereitschaft A (DTRA), Sendeaufforderung A (RTSA), Sendebereitschaft A (CTSA), Trägererkennung A (DCDA) und Systemtakt (C). Rechts befinden sich weitere Datenleitungen (D0, D2, D4, D6), I/O-Anforderung (IORQ), Bausteinauswahl (CE), Kanalauswahl (B/A), Steuer/Daten-Auswahl (C/D), Lese-Signal (RD), Masse (Uss), Wait/Ready B, Sync B, Empfangsdaten B (RxDB), ein kombinierter Empfangs/Sendetakt B (RxTxCB), Sendedaten B (TxDB), Terminalbereitschaft B (DTRB), Sendeaufforderung B (RTSB), Sendebereitschaft B (CTSB), Trägererkennung B (DCDB) und Reset. Das logische Schaltsymbol daneben gruppiert diese Signale in Bus-Schnittstelle, Kanal A und Kanal B Funktionen.*

**Bild 1: Anschlußbelegung und logisches Schaltbild des U 856 D (Bondvariante U 8560)**

Die Beschränkung auf ein 40poliges Gehäuse erlaubt es nicht, Empfangstakt, Sendetakt, Terminalbereitschaft und Synchronisationssignale für beide Kanäle herauszuführen. Daher muß Kanal B auf ein Signal verzichten oder zwei Signale werden zusammengebondet. Da die Forderungen der Nutzer unterschiedlich sind, werden zwei Bondmöglichkeiten angeboten.
SIO/0 (U 8560 D) hat alle vier Signale, wobei jedoch TxCB und RxCB zusammengebondet sind (Bild 1). SIO/1 (U 8561 D) verzichtet auf $\overline{\text{DTRB}}$ und behält $\overline{\text{TxCB}}$, $\overline{\text{RxCB}}$ und $\overline{\text{SYNCB}}$ (Bild 2).

*KI generierte Interpretation der Abbildung: Bild 2 zeigt die Bondvariante U 8561 D. Der wesentliche Unterschied zu Bild 1 liegt an den Pins 22 bis 24. Pin 23 ist hier ausschließlich als TxCB und Pin 24 als RxCB ausgeführt. Pin 22, der zuvor DTRB war, ist in dieser Variante als SYNCB belegt. Das logische Schaltsymbol rechts spiegelt diese geänderte Pin-Belegung für Kanal B wider.*

**Bild 2: Anschlußbelegung und logisches Schaltbild des U 856 D (Bondvariante U 8561 D)**

**PIN-Beschreibung**

| Bezeichnung | Funktion | Kommentar |
| :--- | :--- | :--- |
| D0-D7 | Datenbus | Bidirektionale Tri-State-Datenleitungen zum Anschluß an den Systemdatenbus |
| B/$\overline{\text{A}}$ | Kanal "B/A-Select" | Eingang zur Kanalauswahl (High bedeutet "Kanal B selektiert") |
| C/$\overline{\text{D}}$ | "Control/Data-Select" | Eingang Steuerwort/Datenübertragung (High bedeutet Steuerwort übertragen) |
| $\overline{\text{CE}}$ | "Chip enable" | Eingang Low aktiv: Chip-Freigabe |
| $\overline{\text{M1}}$ | "Machine Cycle 1" | Eingang Low aktiv: Maschinenzyklus M1 oder CPU-Steuerbussignal |
| $\overline{\text{IORQ}}$ | "I/O-Request" | Eingang Low aktiv: Ein/Ausgabeanforderung von der CPU |
| $\overline{\text{RD}}$ | "Read" | Eingang Low aktiv: Lesezyklus der CPU (Steuerbussignal) |
| C | "Clock" | Eingang: Systemtakt |
| $\overline{\text{INT}}$ | "Interruptrequest" | Eingang, Low aktiv: Interruptanforderung von der Peripherie |
| IEI | "Interrupt enable In" | Eingang, High aktiv: Verbindung von IEI mit IEO des nächst höher priorisierten E/A-Schaltkreises ermöglicht Interruptprioritäts-Kaskadierung. High-Pegel an IEI bedeutet, daß momentan kein Interrupt höherer Priorität abgearbeitet wird. |
| IEO | "Interrupt enable Out" | Ausgang, High aktiv: IEO führt nur High-Pegel, wenn vom betreffenden und allen höher priorisierten Schaltkreisen der Interrupt-Prioritätskette kein Interrupt in Abarbeitung befindlich oder angemeldet ist. (Ausnahme: noch nicht bestätigte Interruptanmeldung eines höher priorisierten E/A-Schaltkreises während ED-Dekodierung) |
| $\overline{\text{RESET}}$ | "Reset" | Eingang, Low aktiv: Sperrt sowohl Sender als Empfänger. TxDA und TxDB werden in den aktiven Zustand gebracht, Modem Control in den High-Zustand. Nach dem Rücksetzen müssen die Steuerregisterinformationen neu eingeschrieben werden, bevor irgendeine Datenübertragung stattfindet. Sämtliche Interrupts werden gesperrt. |
| $\overline{\text{W}}/\overline{\text{RDY A}}$, $\overline{\text{W}}/\overline{\text{RDY B}}$ | "Wait/Ready" | Ausgang programmierbar Open-Drain oder Gegentaktausgang. 1 Anschluß pro Kanal, der als "Ready-Leitung" für den Anschluß von DMA-Controllern oder als "Wait-Leitung" zur Synchronisation der CPU mit der Datenübertragungsrate programmiert werden kann. |
| $\overline{\text{CTS A}}$, $\overline{\text{CTS B}}$ | "Clear to Send" | 1 Anschluß pro Kanal, Schmitt-Trigger-Eingänge, Low aktiv: In der Betriebsart "Auto-Enable" sperrt dieses Signal den Sender seines Kanals. Wird der Anschluß nicht zur Senderfreigabe verwendet, steht er als allgemeiner Eingang zur freien Verfügung. Die beiden Leitungen haben Schmitt-Trigger-Eingänge, so daß auch Eingangssignale geringer Flankensteilheit einwandfrei verarbeitet werden. |
| $\overline{\text{DCD A}}$, $\overline{\text{DCD B}}$ | "Data Carrier Detect" | 1 Anschluß pro Kanal, Schmitt-Trigger-Eingänge, Low aktiv: Im "Auto-Enable"-Mode Eingang zur Empfängerfreigabe sonst frei verfügbarer Eingang |
| RxDA, RxDB | "Receive Data" | 1 Anschluß pro Kanal, Eingänge high aktiv: serielle Dateneingänge |
| TxDA, TxDB | "Transmit Data" | 1 Anschluß pro Kanal, Ausgänge high aktiv: Serielle Datenausgänge |
| $\overline{\text{RxC A}}$, $\overline{\text{RxC B}}$ | "Receiver Clock" | 1 Anschluß pro Kanal, Schmitt-Trigger-Eingänge, Low aktiv: Empfängertakteingang, als Taktgeschwindigkeit in der asynchronen Betriebsart können x1, x16, x32 oder x64 programmiert werden. |
| $\overline{\text{TxC A}}$, $\overline{\text{TxC B}}$ | "Transmitter Clock" | 1 Anschluß pro Kanal, Schmitt-Trigger-Eingänge Sendertakteingang; als Taktgeschwindigkeit sind hier ebenfalls x1, x16, x32 oder x64 möglich. |
| $\overline{\text{RTS A}}$, $\overline{\text{RTS B}}$ | "Request to Send" | 1 Anschluß pro Kanal, Ausgänge low aktiv: Sobald das RTS-BIT eines Kanals gesetzt ist, geht die zugehörige RTS-Leitung in den Low-Zustand über. Wird das RTS-BIT in der asynchronen Betriebsart rückgesetzt, geht die zugehörige RTS-Leitung in den High-Zustand, sobald das Senderregister leer ist. In der synchronen Betriebsart fungiert der Anschluß einfach als Ausgang, an dem dauernd der Wert des RTS-Bits liegt. |
| $\overline{\text{DTR A}}$, $\overline{\text{DTR B}}$ | "Data Terminal Ready" | 1 Kontakt pro Kanal, Ausgänge Low aktiv: Ausgang, an dem der Wert des DTR-Bits liegt. |
| $\overline{\text{SYNC A}}$, $\overline{\text{SYNC B}}$ | "External Charakter Synchronisation" | 2 Ein/Ausgabe-Anschlüsse, Low aktiv: Funktion des Anschlusses ist abhängig vom programmierten Übertragungsmodus: |
| | | **1. Externe Synchronisation:** Eingang; Aufbauen des Zeichens beginnt mit der steigenden Flanke von RxC, die auf die fallende Flanke des SYNC-Signals folgt. Eingang darf frühestens 2 Taktzyklen nach der steigenden Flanke von RxC aktiviert werden. Empfehlung: SYNC-Aktivierung nur mit der fallenden Flanke von RxC erfüllt diese Forderung. |
| | | **2. Interne Synchronisation:** Ausgang; aktiv in dem Teil eines Taktzyklus, in dem ein SYNC-Zeichen (8- oder 16-Bit-Folge) erkannt wurde. SYNC-Bedingung wird nicht zwischengespeichert, d. h. jede SYNC-Bitfolge (unabhängig von Wortgrenzen) aktiviert SYNC-Ausgang. |
| | | **3. Asynchroner Modus:** Eingang, für allgemeine Eingabezwecke in Read-Register 0. |

**Struktur des Bausteins**

Die interne Struktur des Bausteins umfaßt CPU-Interface, interne Steuerung, Interrupt-Logik und zwei Vollduplexkanäle (Bild 3). Jeder Kanal hat Lese- und Schreibregister und eine diskrete Steuer- und Statuslogik, die das Interface für Modems oder andere externe Geräte enthalten.

*KI generierte Interpretation der Abbildung: Bild 3 zeigt das funktionale Blockschaltbild des SIO U 856 D. Ein zentraler „Interner Bus“ verbindet alle Hauptkomponenten. Links ist die Schnittstelle zur CPU („CPU Bus I/O“) dargestellt, die Eingänge für Daten, Steuerung und Spannungsversorgung besitzt. Das Zentrum bilden die „Interne Steuerlogik“ und die „Interrupt-Steuer-Logik“. Diese sind über den internen Bus mit den „Lese/Schreib-Registern“ von Kanal A und Kanal B verbunden. Jeder Kanal hat eine eigene „Diskrete Steuerung und Status“-Einheit für Modem- oder andere Steuerleitungen sowie serielle Schnittstellen („Kanal A“ und „Kanal B“) für serielle Daten, Empfangs-/Sendetakt, SYNC und Wait/Ready.*

**Bild 3: Blockschaltbild der SIO U 856 D**

Die Lese- und Schreibregistergruppe umfaßt fünf 8-Bit-Steuerregister, zwei Sync-Zeichen-Register und zwei Statusregister. Der Interrupt-Vektor wird in ein 8-Bit-Zusatzregister (Schreibregister 2) im Kanal B eingeschrieben, der über Leseregister 2 im Kanal B gelesen werden kann.

Die Register für beide Kanäle werden im Text wie folgt bezeichnet:
- WR0-WR7: Schreibregister 0 bis 7
- RR0-RR2: Leseregister 0 bis 2

Die Bitzuordnung und funktionelle Gruppierung jedes Registers ist so ausgelegt, daß die Programmierarbeit erleichtert wird. Tabelle 1 veranschaulicht die Funktionen, die jedem Lese- oder Schreibregister zugeordnet sind.

| Register | Funktion |
| :--- | :--- |
| **WR0** | Registerzeiger, CRC-Initialisierung, Initialisierungsbefehle für die verschiedenen Betriebsarten, usw. |
| **WR1** | Sende/Empfangs-Interrupt und Festlegung der Datenübertragungsbetriebsart |
| **WR2** | Interrupt-Vektor (nur Kanal B) |
| **WR3** | Empfangsparameter und Steuerbits |
| **WR4** | Parameter für Senden und Empfang und die dazugehörigen Betriebsarten |
| **WR5** | Sendeparameter und Steuerbits |
| **WR6** | SYNC-Zeichen oder SDLC-Adressenfeld |
| **WR7** | SYNC-Zeichen oder SDLC-Flag |
| | |
| **RR0** | Pufferstatus Senden/Empfangen, Interruptstatus und externer Status |
| **RR1** | Status für spezielle Empfangsbedingungen |
| **RR2** | Modifizierter Interrupt-Vektor (nur Kanal B) |

**Tabelle 1: Funktionen der Lese- und Schreibregister**

Die Logik beider Kanäle liefert Formate, Synchronisation und gültige Daten, die zum und vom Kanalinterface übertragen werden. Die Modemsteuereingänge Clear to Send (CTS) und Data Carrier Detect (DCD) werden durch die diskrete Steuerlogik unter Programmkontrolle überwacht. Alle Modemsteuersignale sind dem Wesen nach Mehrzwecksignale und können auch für andere Funktionen verwendet werden.
Für die automatische Interrupt-Vektorbildung legt die Interruptsteuerlogik fest, welcher Kanal die höchste Priorität hat. Die Priorität ist so festgelegt, daß Kanal A eine höhere Priorität hat als Kanal B; Empfänger-, Sender- und externe bzw. Status-Interrupts sind in jedem Kanal in dieser Reihenfolge priorisiert.

**Datenweg**

Der Sende- und Empfangsdatenweg für jeden Kanal ist in Bild 4 dargestellt. Der Empfänger hat drei 8-Bit-Pufferregister in einer FIFO-Anordnung (um eine 3-Byte-Verzögerung zu erzeugen) zusätzlich zu dem 8-Bit-Empfangs-Schieberegister. Auf diese Weise erhält die CPU zusätzlich Zeit, um einen Interrupt am Anfang eines schnellen Datenblocks zu bedienen. Der Empfangsfehler - FIFO speichert Paritäts- und Rahmenfehler und andere Arten von Statusinformationen für jedes der drei Bytes in dem Empfangedaten - FIFO.

*KI generierte Interpretation der Abbildung: Bild 4 zeigt den detaillierten Sende- und Empfangsdatenfluss innerhalb eines Kanals. In der Mitte oben befindet sich der „I/O Datenpuffer“ zum „Interner Datenbus“. Davon gehen Pfade zum Empfänger (links) und Sender (rechts) ab. Der Empfängerpfad beginnt am Pin „RxDA“, führt über eine „1 BIT Verzögerung“, ein „Synchr. Reg“, ein „Empf.-Schieberegister“ und einen dreistufigen „Empf.-Daten-FIFO“ zum Bus. Parallel dazu existiert eine „Empf.-Fehler-FIFO“ und eine CRC-Prüfung („CRC Kontrolle“). Der Senderpfad erhält Daten vom Bus, führt sie über WR6/WR7 oder das Sendedatenregister in das „20 BIT Sendeschieberegister“ (inkl. Start-Bit-Logik). Ein „Sendemultiplexer“ steuert den Ausgang am Pin „TxDA“. Zusätzlich sind Blöcke für „Nulleinfügung“, „SDLC-CRC“ und einen „CRC Generator“ integriert. Die gesamte Taktung wird über die „Empfangs-Taktlogik“ (RxCA) und „Sendetaktlogik“ (TxCA) koordiniert.*

**Bild 4: Sende- und Empfangsdatenweg**

Ankommende Daten werden je nach Betriebsart und Zeichenlänge über einen von mehreren Wegen geleitet. In der asynchronen Betriebsart werden die seriellen Daten in einen 3-Bit-Puffer eingeschrieben, wenn ihre Zeichenlänge 7 oder 8 Bits beträgt, oder sie gelangen direkt in das 8-Bit-Schieberegister, wenn sie 5 oder 6 Bits lang sind. Bei der synchronen Betriebsart wird der Datenweg durch die Phase des Empfangsprozesses bestimmt, die gerade abläuft.

Eine synchrone Empfangsoperation beginnt der Empfänger mit der Suchphase (Hunt-Phase), dabei wird der ankommende Datenstrom auf ein Bitmuster abgesucht, welches den vorprogrammierten Sync-Zeichen (oder Flags in der SDLC-Betriebsart) entspricht. Ist der Baustein für Monosync programmiert, erfolgt ein Vergleich mit einem einzelnen Sync-Zeichen, das in WR7 gespeichert ist. Im Bisync-Betrieb wird mit einem Doppelsynchronisationszeichen verglichen (gespeichert in WR6 und WR7). In jedem Fall passieren die ankommenden Daten das Empfangs-Sync-Register und werden mit dem programmierten Sync-Zeichen in WR6 oder WR7 verglichen. In der SDLC-Betriebsart durchlaufen die ankommenden Daten zuerst das Empfangs-Sync-Register, welches ständig den Empfangsdatenstrom überwacht und die Nullausblendung ausführt, wenn sie notwendig ist. Beim Empfang von fünf aufeinanderfolgenden Einsen wird das sechste Bit geprüft. Ist das sechste Bit eine Null, wird es aus dem Datenstrom gestrichen. Wenn das sechste Bit eine 1 ist, wird das siebente Bit geprüft. Ist dieses Bit eine Null, so wurde eine Flag-Folge empfangen, ist es eine Eins, so lag eine Abbruch (Abort)-Folge vor. Die so geänderten Daten gelangen in den 3-Bit-Puffer und werden zum Empfangsschieberegister weitergeleitet. Die SDLC-Empfangsoperation beginnt ebenfalls mit der Suchphase, in der der SIO versucht, das Zeichen im Empfangsschieberegister mit dem Flagmuster in WR7 zu vergleichen. Ist das erste Flagzeichen einmal erkannt, werden alle darauffolgenden Daten über denselben Weg geführt, unabhängig von der Zeichenlänge.

Der CRC-Prüfer wird sowohl für die SDLC- als auch die synchronen Daten verwendet, jedoch ist der eingeschlagene Datenweg für jede Betriebsart unterschiedlich. Im Bisync-Format beinhaltet die byte-orientierte Arbeitsweise, daß die CPU entscheidet, ob das Datenzeichen in die CRC-Berechnung einzubeziehen ist. Um der CPU genügend Zeit zu geben, diese Entscheidung zu treffen, besitzt der SIO eine 8-Bit-Verzögerung für synchrone Daten. In der SDLC-Betriebsart ist keine Verzögerung vorgesehen, da der SIO eine Logik enthält, welche die Bytes bestimmt, bei denen CRC berechnet wird. Der Sender besitzt ein 8-Bit-Datenregister, das vom internen Datenbus geladen wird und ein 20-Bit-Sendeschieberegister, das seine Informationen von WR6, WR7 und dem Sendedatenregister erhält. WR6 und WR7 enthalten Sync-Zeichen in den Betriebsarten Monosync oder Bisync oder ein Adressenfeld (im Zeichen lang) bzw. ein Flag in der SDLC-Betriebsart. Während der synchronen Betriebsarten werden die in WR6 und WR7 enthaltenen Informationen in das Sendeschieberegister am Anfang der Nachricht geladen oder als Zeitfüller in der Mitte der Nachricht, wenn eine Transmit-Underrun (Sender - leer)-Bedingung auftritt. In der SDLC-Betriebsart werden die Flags in das Sendeschieberegister am Beginn und Ende der Nachricht eingeschrieben. Die asynchronen Daten im Sende-Schieberegister werden mit Start- und Stopp-Bits versehen und mit der ausgewählten Taktrate zum Sende-Multiplexer geschoben. Synchrone (Monosync oder Bisync) - Daten gelangen zum Sendemultiplexer und auch zum CRC-Generator bei einfacher Taktrate.
SDLC/HDLC-Daten werden zur Nulleinfügungslogik weitergeleitet, die unwirksam ist, wenn die Flags gesendet werden. Für alle anderen Felder (Adresse, Steuerbits und Rahmenkontrolle) wird eine Null eingefügt, die auf fünf aufeinanderfolgende Einsen im Datenstrom folgt. Das Ergebnis des CRC-Generators für die SDLC-Daten wird auch über die Nulleinfügungslogik geleitet.

**3. SIO-Zeitabläufe**

**Read-Zyklus (CPU-E/A-Lesezyklus)**

*KI generierte Interpretation der Abbildung: Bild 5 zeigt die Wellenformen für einen CPU-Lesezugriff. Das Taktsignal $\Phi$ dient als Referenz. Wenn $\overline{\text{CE}}$ (Chip Enable) und $\overline{\text{IORQ}}$ (I/O Request) sowie $\overline{\text{RD}}$ (Read) gleichzeitig low sind, schaltet der SIO die Daten auf den Bus („DATEN“), markiert durch eine sechseckige Form im Diagramm. $\overline{\text{M1}}$ bleibt während dieses Zyklus passiv (high).*

**Bild 5: Read-Zyklus**

Diese Signalfolge (Bild 5) tritt auf, wenn die CPU Daten oder Statusregisterinhalte vom SIO liest. Bei aktivierter WAIT-Funktion kann der SIO weitere WAIT-Zyklen anfordern. U 880-Eingabebefehle entsprechen diesem Zeitablauf.

**WRITE-Zyklus (CPU-E/A-Schreibzyklus)**

*KI generierte Interpretation der Abbildung: Bild 6 zeigt den Schreibzugriff. Ähnlich wie beim Lesezyklus müssen $\overline{\text{CE}}$, $\overline{\text{IORQ}}$ und $\overline{\text{M1}}$ aktiv sein. Der wesentliche Unterschied ist, dass $\overline{\text{RD}}$ high (passiv) bleibt. Die Daten müssen von der CPU stabil auf dem Bus bereitgestellt werden, bevor die Signale wieder inaktiv werden.*

**Bild 6: Write-Zyklus**

Beim Schreiben eines Daten- oder Steuerbytes von der CPU zum SIO tritt diese Signalfolge (Bild 6) auf. Bei aktivierter WAIT-Funktion kann der SIO weitere WAIT-Zyklen anfordern. U 880-Ausgabebefehle entsprechen diesem Zeitablauf.

**Interrupt-Bestätigungs-Zyklus**

*KI generierte Interpretation der Abbildung: Bild 7 zeigt das Timing für die Interrupt-Bestätigung. Hier werden $\overline{\text{M1}}$ und $\overline{\text{IORQ}}$ gleichzeitig von der CPU auf low gezogen. Der SIO prüft seinen IEI-Eingang (Interrupt Enable In). Wenn dieser high ist (kein höherer Interrupt aktiv), legt der SIO seinen Interrupt-Vektor auf den Datenbus.*

**Bild 7: Interrupt-Bestätigungs-Zyklus**

Nach Erhalt eines Interrupt-Request-Signales ($\overline{\text{INT}}$ wird auf Low gezogen) sendet die CPU ein Interrupt-Bestätigungssignal (Bild 7) ($\overline{\text{M1}}$ und $\overline{\text{IORQ}}$ beide Low).
Die Daisy-Chain-Interrupt-Kette bestimmt den Interrupt-Anforderer mit der höchsten Priorität. Das IEI des peripheren Gerätes mit der höchsten Priorität ist auf High zu legen. Bei jedem peripheren Gerät, das keinen Interrupt anstehen hat oder gerade abarbeitet, ist IEO=IEI. Jedes periphere Gerät, das einen Interrupt anstehen oder in Bearbeitung hat, bringt seinen IEO auf Low. Um das Einschwingen der Prioritätskette zu garantieren, können die Kanäle während $\overline{\text{M1}}$ ihren Interrupt-Aktivierungszustand nicht ändern. Ist der SIO der höchstpriorisierte Schaltkreis, plaziert er den entsprechenden Interruptvektor während $\overline{\text{IORQ}}$ auf den Datenbus.

**Rückkehr vom Interruptzyklus**

*KI generierte Interpretation der Abbildung: Bild 8 zeigt die zeitlichen Abläufe beim Auslesen des RETI-Befehls (Return from Interrupt). Die CPU holt nacheinander die Bytes „ED“ und „4D“ vom Speicher. Der SIO überwacht den Datenbus. Wenn er das erste Byte „ED“ erkennt, bereitet er die Freigabe der Daisy-Chain vor. Erst nach dem zweiten Byte „4D“ wird der interne „Under Service“-Zustand gelöscht und IEO geht wieder auf high (bzw. folgt IEI).*

Normalerweise gibt die CPU einen RETI-(Return from Interrupt) Befehl am Ende einer Interrupt-Service-Routine aus. RETI ist ein 2-Byte Operationskode (ED4D), welcher die Interrupt-Under-Service-Verriegelung zurücksetzt, um den Interrupt abzuschließen, der gerade bearbeitet wurde. Dies geschieht unter Benutzung der Daisy Chain auf die folgende Art und Weise. Der normale Daisy-Chain-Betrieb kann verwendet werden, um einen anstehenden Interrupt zu ermitteln; aber er kann nicht zwischen einem Interrupt in Abarbeitung und einem anstehenden nicht bestätigten Interrupt von höherer Priorität unterscheiden. Falls ein Interrupt angemeldet, aber noch nicht bestätigt ist, liegt IEO auf "Low" bis der Befehlskode "ED" auf dem Datenbus dekodiert wird. Zu diesem Zeitpunkt liegt IEO des angemeldeten Schaltkreises auf "High" bis zur Dekodierung des nächsten Datenbytes, woraufhin IEO wieder auf "Low" geht. Ist das auf "ED" folgende Befehlsbyte ein "4D", entsprach der Befehlskode einem "RETI"-Befehl. Nach der Dekodierung des Befehlsbytes "ED" hat nur der Peripherieschaltkreis IEI auf "High" und IEO auf "Low", dessen Interrupt in Abarbeitung befindlich ist (momentan höchstpriorisierte in Abarbeitung befindliche IR). Alle anderen Peripherieschaltkreise haben IEI=IEO. Folgt auf "ED" ein "4D"-Befehlscode, setzt eben dieser Peripherieschaltkreis seine Interruptaktivierung zurück. WAIT-Zyklen sind während der $\overline{\text{M1}}$-Zyklen erlaubt, können aber nicht zur Verkürzung der High-Low-Durchlaufzeit genutzt werden.

**Interrupt-Priorisierung innerhalb des SIO**

Bild 9 veranschaulicht die Daisy-Chain-Anordnung von Interruptketten und ihr Verhalten bei geschachtelten Interrupts (ein Interrupt, der durch einen anderen mit einer höheren Priorität unterbrochen wird).

*KI generierte Interpretation der Abbildung: Bild 9 zeigt in fünf Schritten den Zustand einer Interrupt-Kette bestehend aus sechs Elementen (Empfänger, Sender und Extern/Status für jeweils Kanal A und B). Jedes Element ist eine Box mit IEI-Eingang und IEO-Ausgang. 
1. „Ruhezustand“: Alle IEI/IEO sind High.
2. „Kanal B Sender aktiv“: Das Element „Kanal B Sender“ zieht seinen IEO auf Low, alle nachfolgenden Elemente (Kanal B Extern/Status) erhalten Low an ihrem IEI und sind gesperrt.
3. „Kanal A Extern unterbricht“: Ein Element weiter vorne in der Kette („Kanal A Extern“) meldet einen Interrupt an und zieht seinen IEO auf Low. Jetzt sind alle Elemente ab diesem Punkt (inklusive des bereits aktiven Kanal B Senders) gesperrt.
4. „Kanal A fertig“: Nach dem RETI für Kanal A wird dessen IEO wieder High. Die CPU setzt die Arbeit an der Serviceroutine für „Kanal B Sender“ fort.
5. „Kanal B fertig“: Nach dem RETI für Kanal B kehren alle Signale in den Ruhezustand (High) zurück.*

**Bild 9: Typische Interrupt-Folge**

Jedes Kästchen in der Darstellung könnte eine gesonderte externe periphere Schaltung sein, mit einem vom Nutzer angegebenen Reihenfolge der Interrupt-Prioritäten. Eine ähnliche Daisy-Chain-Struktur existiert auch im SIO, der sechs Interruptebenen mit einer festgesetzten Prioritätsreihenfolge hat. Der dargestellte Fall tritt ein, wenn der Sender des Kanals B ein Interrupt auslöst. Während dieser Interrupt bedient wird, wird er durch einen Interrupt höherer Priorität von Kanal A unterbrochen. Der zweite Interrupt wird bedient und - nach Beendigung - durch einen RETI-Befehl abgeschlossen. Jetzt wird die Service-Routine für Kanal B fortgeführt. Ist diese Routine beendet, bewirkt ein anderer RETI-Befehl den Abschluß des Interrupt-Service.

**4. E/A-Betriebsarten**

**4.1. Funktionsbeschreibung**

Die Funktionen des SIO können von zwei unterschiedlichen Standpunkten beschrieben werden: als Gerät zur Datenübertragung sendet und empfängt der SIO-Baustein Daten und erfüllt die Forderungen der verschiedenen Datenübertragungsformate; als peripherer Schaltkreis tritt er mit der CPU und anderen peripheren Schaltkreisen in Wechselwirkung und ist an den Daten-, Adreß- und Steuerbus angeschlossen, ebenso wie der SIO auch ein Glied in der Interruptsstruktur des Systems ist. Als peripherer Schaltkreis zu anderen Mikroprozessoren hat der SIO wertvolle Eigenschaften, wie z. B. Interrupts (ohne Vektorbildung), Polling- und einfacher Handshake-Betrieb.

**Ein-Ausgabe-Möglichkeiten**

Der SIO bietet die Auswahl zwischen den Betriebsarten Polling (Abfragen), Interrupts (mit und ohne Vektoren) und Blockübertragung, um Daten-, Status- und Steuerinformationen zur und von der CPU zu übertragen. Die Betriebsart Blockübertragung kann unter CPU- oder DMA-Steuerung durchgeführt werden.

**Polling**

In dieser Betriebsart gibt es keine Interrupts. Die Statusregister RR0 und RR1 werden zu bestimmten Zeiten für jede ausgeführte Funktion aktualisiert (z. B. CRC-Fehlerstatus wird am Ende der Nachricht bestätigt). Alle Interrupt-Arten des SIO müssen unwirksam gemacht werden, um den Schaltkreis im Abfragebetrieb zu betreiben. Im Polling-Betrieb untersucht die CPU den Status, der für jeden Kanal in RR0 enthalten ist, die RR0-Status-Bits dienen als eine Bestätigung der Abrufanfrage. Die zwei RR0-Status-Bits $D_0$ und $D_2$ zeigen an, daß das Senden oder Empfangen von Daten erforderlich ist. Der Status zeigt auch Fehler oder andere besondere Statusbedingungen an (siehe SIO-Programmierung). Der in RR1 enthaltene spezielle Empfangsbedingungszustand muß nicht abgerufen werden, da die Statusbits in RR1 mit einem gültigen Empfangszeichen-Zustand in RR0 einhergehen.

**Interrupts**

Der SIO bietet eine komplexe Interrupt-Struktur, um eine schnelle Interrupt-Bearbeitung in Echtzeitanwendungen zu gewährleisten. Wie bereits erwähnt, enthalten die Register WR2 und RR2 von Kanal B den Interrupt-Vektor, der auf eine Interrupt-Service-Routine im Speicher zeigt. Um Operationen in beiden Kanälen auszuführen, und um die Notwendigkeit einer Status-Analyse-Routine auszuschalten, kann der SIO den Interrupt-Vektor in RR2 verändern und somit zwischen acht verschiedenen Interrupt-Service-Routinen wählen. Dies geschieht unter Programmsteuerung durch Setzen eines Bits (WR1, $D_2$) in Kanal B, das als "Status Affects Vector" bezeichnet wird. Wenn dieses Bit gesetzt ist, wird der Interruptvektor in WR2 verändert gemäß der zugeordneten Priorität der verschiedenen Interrupt-Bedingungen. Die Tabelle in der Beschreibung des Schreibregisters 1 zeigt die Einzelheiten der Veränderung.

*KI generierte Interpretation der Abbildung: Bild 10 illustriert die hierarchische „Interruptstruktur“ des SIO. Die drei Hauptquellen für Interrupts sind: 
1. „Empfänger Interrupt“: Dieser kann ausgelöst werden bei „jedem Empfangszeichen“, beim „ersten Zeichen“ oder bei einer „speziellen Empfangsbedingung“ (wie Paritätsfehler, Empfängerüberlauf, Rahmenfehler oder Ende des Rahmens in SDLC).
2. „Sender Interrupt“: Wird ausgelöst, wenn der „Puffer wird leer“.
3. „Extern / Status Interrupt“: Überwacht Änderungen an den Pins DCD, CTS und SYNC sowie Bedingungen wie „Sendepuffer leer / Ende der Daten“ oder „Unterbrechung/Abbruch-Erkennung“.
Alle diese Quellen führen zum zentralen Block „SIO Interrupt“, der dann die CPU anfordert.*

**Bild 10: Interruptstruktur**

Sende-Interrupts, Empfangs-Interrupts und Extern/Status-Interrupts sind die Hauptquellen für Interrupts (Bild 10). Jede Interrupt-Möglichkeit wird unter Programmsteuerung gewählt, wobei Kanal A eine höhere Priorität hat als Kanal B, und Empfänger-, Sender- und Extern/Status-Interrupts sind in dieser Reihenfolge in jedem Kanal priorisiert. Bei aktiviertem Sende-Interrupt wird die CPU durch den Sendepuffer unterbrochen, der leer wird. (Dies bedeutet, daß der Sender zuvor ein Datenzeichen ausgegeben hat, um die Bedingung des Leerwerdens zu erfüllen.)
Bei Empfangsbetrieb kann die CPU auf 3 verschiedenen Wegen unterbrochen werden:
- Interrupt beim ersten empfangenen Zeichen
- Interrupt bei jedem empfangenen Zeichen
- Interrupt bei einer speziellen Empfangsbedingung

Interrupt beim ersten empfangenen Zeichen wird gewöhnlich bei Blockübertragung verwendet. Interrupt bei jedem Empfangszeichen ermöglicht die Modifizierung des Interruptvektors im Falle eines Paritätsfehlers. Der Interrupt bei einer speziellen Empfangsbedingung kann auf Grundlage eines Zeichens oder von Daten erfolgen (z. B. Rahmenende-Interrupt in SDLC). Die spezielle Empfangsbedingung kann einen Interrupt nur verursachen, wenn entweder der Interrupt beim ersten Empfangszeichen oder der Interrupt bei jedem Empfangszeichen ausgewählt wurde. Im Falle des Interrupts beim ersten Empfangszeichen kann ein Interrupt bei speziellen Empfangsbedingungen (ausgenommen Paritätsfehler) nach dem ersten Empfangszeichen-Interrupt auftreten (Beispiel: Empfänger-Überlauf-Interrupt).
Die Hauptfunktion des Extern/Status-Interrupts besteht darin, die Signalübergänge der Pins $\overline{\text{CTS}}$, $\overline{\text{DCD}}$ und $\overline{\text{SYNC}}$ zu überwachen; aber ein Extern/Status-Interrupt wird ebenso durch eine Sender-leer-Bedingung oder durch die Feststellung einer Break- (Asynchrone Betriebsart) oder Abort-Folge (SDLC-Betriebsart) im Datenstrom verursacht. Der durch die Break/Abort-Folge verursachte Interrupt hat ein besonderes Merkmal, das dem SIO zu unterbrechen erlaubt, wenn die Break/Abort-Folge festgestellt wird oder beendet ist. Diese Eigenschaft erleichtert den richtigen Abschluß der laufenden Datenübertragung, korrekte Initialisierung der nächsten Daten und die genaue Zeitgebung der Break/Abort-Bedingung in der externen Logik.

**CPU/DMA-Blockübertragung**

Der SIO bietet eine Block-Transfer-Betriebsart, um die CPU-Blockübertragungsfunktionen und die DMA-Steuerung zu ermöglichen. Die Block-Transfer-Betriebsart benutzt den $\overline{\text{WAIT}}/\overline{\text{READY}}$-Ausgang in Verbindung mit den Wait/Ready-Bits des Schreibregisters 1. Der $\overline{\text{WAIT}}/\overline{\text{READY}}$-Ausgang kann unter Software-Kontrolle als Warte-Leitung in der CPU-Block-Transfer-Betriebsart oder als READY-Leitung in der DMA-Block-Transfer-Betriebsart festgelegt werden.
Der DMA-Steuerung zeigt der READY-Ausgang der SIO an, daß der SIO bereit ist, Daten zum oder vom Speicher zu übertragen. Für die CPU zeigt der WAIT-Ausgang an, daß der SIO nicht bereit ist, Daten zu übertragen, wobei die CPU aufgerufen wird, den I/O-Zyklus zu verlängern. Die Programmierung der Bits 5, 6 und 7 des Schreibregisters 1 und die logischen Zustände der $\overline{\text{WAIT}}-\overline{\text{READY}}$-Leitung sind in der Beschreibung des Schreibregisters 1 festgelegt (Abschnitt über Programmierung der SIO).

**Datenübertragungsmöglichkeiten**

Zusätzlich zu den Ein-/Ausgabemöglichkeiten, die bereits erörtert worden sind, hat der SIO zwei unabhängige Vollduplexkanäle, welche in den Betriebsarten Asynchron, Synchron und SDLC (HDLC) programmiert werden können. Diese unterschiedlichen Betriebsarten geben die Möglichkeit, die gewöhnlich verwendeten Datenübertragungsformate zu implementieren. Die besonderen Merkmale dieser Betriebsarten werden in den folgenden Abschnitten beschrieben. Um die Unabhängigkeit und Vollständigkeit jedes Abschnittes zu erhalten, werden einige Informationen wiederholt, die allen Betriebsarten gemeinsam sind.

**4.2. Asynchrone Betriebsart**

Für den Empfang oder das Senden von Daten in asynchroner Betriebsart benötigt der SIO folgende Parameter: Zeichenlänge, Taktrate, Anzahl der Stoppbits, gerade oder ungerade Parität, Interrupt-Betriebsart, WAIT/READY-Mode und Empfänger- oder Senderfreigabe. Durch das Systemprogramm werden diese Parameter in die entsprechenden Schreibregister geladen. Die WR4-Parameter müssen vor den WR1-, WR3- und WR5-Parametern oder Befehlen eingeschrieben werden. Wenn die Daten über ein Modem oder ein RS232C-Interface übertragen werden, so müssen die Ausgänge REQUEST TO SEND ($\overline{\text{RTS}}$) und DATA TERMINAL READY ($\overline{\text{DTR}}$) gemeinsam mit dem Senderfreigabe-Bit gesetzt sein. Die Übertragung beginnt erst, wenn das Senderfreigabe-Bit gesetzt ist.
Die Auto-Enable-Betriebsart gestattet dem Programmierer, das erste Datenzeichen zur SIO zu senden ohne auf $\overline{\text{CTS}}$ zu warten. Ist das Auto-Enable-Bit gesetzt, wartet der SIO bis das Signal am $\overline{\text{CTS}}$-Pin Low wird, bevor er mit der Signalübertragung beginnt. $\overline{\text{CTS}}$, $\overline{\text{DCD}}$ und $\overline{\text{SYNC}}$ sind im allgemeinen Ein-/Ausgabe-Leitungen, die für andere Funktionen außer dem bezeichneten Zweck verwendet werden können. Wird $\overline{\text{CTS}}$ anderweitig genutzt, so muß das Auto-Enable-Bit auf "0" programmiert werden.

*KI generierte Interpretation der Abbildung: Bild 11 skizziert das „Asynchrone Datenformat“. Eine Nachricht beginnt im Zustand „MARKING“ (high). Es folgt ein „START“-Bit (low). Danach werden N Datenbits ($N=5, 6, 7$ oder $8$) übertragen ($D_0$ bis $D_n$). Optional folgt ein „PARITÄT“-Bit (gerade, ungerade oder keine). Abgeschlossen wird das Zeichen durch $1, 1 1/2$ oder $2$ „STOP“-Bits (high), wonach die Leitung wieder in den „MARKING“-Zustand geht. Die Übergänge erfolgen synchron zur fallenden Flanke des Sendetaktes TxC.*

**Bild 11: Asynchrones Datenformat**

Bild 11 zeigt die asynchronen Übertragungsformate; Tabelle 2 enthält die WR3-, WR4- und WR5-Schreibregister mit den dazugehörigen Bits, die gesetzt werden müssen, um verwendete Betriebsarten, Parameter und Befehle im asynchronen Betrieb zu kennzeichnen. WR2 (nur Kanal B) speichert den Interruptvektor; WR1 definiert die Interrupt- und Datenübertragungsarten. WR6 und WR7 werden in der asynchronen Betriebsart nicht verwendet. Tabelle 3 zeigt die typischen Programmschritte, die zu einem Vollduplex-Empfangs-/Sendebetrieb in einem der Kanäle gehören.

**4.2.1. Asynchrones Senden**

Der Datenausgang des Senders (TxD) wird auf High-Pegel gehalten, solange der Sender keine Daten zum Senden hat. Unter Programmsteuerung kann der Break-Befehl (WR5, $D_4$) erteilt werden, um den Ausgang auf Low zu halten bis der Befehl wieder gelöscht ist. Der SIO faßt automatisch das Startbit, das programmierte Paritätsbit (ungerade, gerade oder keine Parität) und die programmierte Anzahl der Stoppbits für die zu übertragenden Datenzeichen zusammen. Wenn die Zeichenlänge sechs oder sieben Bits beträgt, werden die nichtbenutzten automatisch vom SIO ignoriert. Für den Fall, daß die Zeichenlänge fünf oder weniger Bits beträgt, verweisen wir auf die Beschreibung des Schreibregisters 5 (Abschnitt Programmierung der SIO).
Serielle Daten werden von TxD entsprechend der Taktrate mit dem Faktor 1, 1/16, 1/32 oder 1/64 der Taktfrequenz übertragen, die an den Takteingang des Senders ($\overline{\text{TxC}}$) angelegt wird. Serielle Daten werden mit der fallenden Flanke von $\overline{\text{TxC}}$ weitergeschoben. In der Betriebsart Extern/Status-Interrupt wird der Zustand von $\overline{\text{DCD}}$, $\overline{\text{CTS}}$ und $\overline{\text{SYNC}}$ während des Sendens einer Nachricht überwacht. Verändern sich diese Signale während eines Zeitraumes, der größer ist als die angegebene Mindestimpulsbreite, so wird ein Interrupt hervorgerufen. Im Sendebetrieb wird diese Eigenschaft zur Überwachung des Modemsteuersignales $\overline{\text{CTS}}$ verwendet.

**4.2.2. Asynchroner Empfang**

Eine asynchrone Empfangsoperation beginnt, wenn das Empfangs-Enable-Bit gesetzt ist. Wurde der Auto-Enable-Betrieb ausgewählt, so muß $\overline{\text{DCD}}$ ebenfalls Low sein. Ein Low (Spacing) am Dateneingang des Empfängers (RxD) kennzeichnet ein Startbit. Wenn dieses Low mindestens eine halbe Bitdauer lang ist, wird das Startbit als gültig angesehen und das Dateneingangssignal wird in der Bitmitte abgetastet bis das vollständige Zeichen empfangen ist. Diese Erkennungsmethode für ein Startbit verbessert die Fehlerunterdrückung, wenn Störspitzen auf der Leitung vorhanden sind.
Wenn die Betriebsart Taktx1 gewählt wird, muß die Bitsynchronisation extern erfolgen. Die empfangenen Daten werden mit der steigenden Flanke von $\overline{\text{RxC}}$ abgetastet. Der Empfänger fügt Einsen ein, wenn eine andere Zeichenlänge als 8 Bit benutzt wird. Wurde die Parität gebildet, so wird das Paritätsbit nicht vom empfangenen Zeichen abgetrennt (außer für eine Zeichenlänge von 8 Bit). Für Zeichenlängen kleiner 8 Bit wird die erforderliche Zahl Datenbits empfangen zuzüglich eines Paritätsbits und Einsen für jedes nichtbenutzte Bit.
Ein 5-Bit-Zeichen hätte folgendes Format:
$1\ 1\ P\ D_4\ D_3\ D_2\ D_1\ D_0$.

Da der Empfänger durch 3 8-Bit-Register zusätzlich zum Empfangsschieberegister gepuffert ist, hat die CPU genügend Zeit, einen Interrupt zu bedienen und das durch die SIO empfangene Datenzeichen zu übernehmen. Der Empfänger besitzt ebenfalls 3 Puffer, die Fehler-Flags für jedes Datenzeichen im Empfänger-Puffer speichern. Diese Fehler-Flags werden zur gleichen Zeit wie die Datenzeichen geladen.

**Tabelle 2: Inhalt der Schreibregister 3, 4 und 5 in der asynchronen Betriebsart**

| Register | BIT7 | BIT6 | BIT5 | BIT4 | BIT3 | BIT2 | BIT1 | BITO |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **WR3** | 00=Rx5 Bits/Zeich. | 10=Rx6 Bits/Zeich. | Auto | 0 | 0 | 0 | 0 | Empfänger- |
| | 01=Rx7 Bits/Zeich. | 11=Rx8 Bits/Zeich. | Enables | | | | | Freigabe |
| **WR4** | 00=x1 Taktrate | 01=x16 Taktrate | 0 | 0 | 00=keine | 01=1 Stoppbit/Zeichen | gerade, | Parität |
| | 10=x32 Taktrate | 11=x64 Taktrate | | | 10=1 1/2 Stoppbit/Zeichen | 11=2 Stoppbit/Zeichen | ungerade Parität | |
| **WR5** | DTR | 00=Tx5 Bits (od.weniger)/Zeichen | 10=Tx6 Bits/Zeich. | Break- | Sender- | 0 | RTS | 0 |
| | | 01=Tx7 Bits/Zeich. | 11=Tx8 Bits/Zeich. | erzeug. | Freigabe | | | |

Nachdem ein Zeichen empfangen wurde, wird es auf folgende Fehlerbedingungen geprüft:
- Wurde die Parität gebildet, so wird das Paritätsfehlerbit (RR1, $D_4$) immer dann gesetzt, wenn das Paritätsbit des Zeichens nicht mit der programmierten Parität übereinstimmt. Dieses Bit bleibt so lange gesetzt, bis der Fehler-Rücksetz-Befehl (WR0) gegeben wird.
- Das Rahmen-Fehler-Bit (RR1, $D_6$) wird gesetzt, wenn das empfangene Zeichen keine Stoppbits enthält, d. h. an der Stelle des Stoppbits wurde Low-Pegel erkannt. Im Gegensatz zum Paritätsfehler-Bit wird dieses Bit nur für das Zeichen gesetzt, bei dem der Fehler aufgetreten ist. Bei Erkennen des Rahmenfehlers fügt die SIO eine weitere halbe Bitzeit zur Zeichendauer hinzu; somit wird der Rahmenfehler nicht als neues Startbit angesehen.
- Wenn die CPU ein Datenzeichen nicht lesen kann, nachdem mehr als drei Zeichen empfangen wurden, wird das Empfänger-Überlauf (Receive-Overrun)-Bit (RR1, $D_5$) gesetzt. Wenn dies auftritt, ersetzt das vierte empfangene Zeichen das dritte Zeichen in den Empfängerpuffern. In diesem Fall wird nur das Zeichen, welches überschrieben wurde, mit dem Empfänger-Überlauffehler-Bit gekennzeichnet. Dieses Bit kann genau wie das Paritätsfehlerbit durch den Error-Reset-Befehl von der CPU zurückgesetzt werden. Sowohl der Rahmenfehler als auch der Empfänger-Überlauffehler rufen einen Interrupt hervor, mit einem Interruptvektor, der eine spezielle Empfangsbedingung anzeigt (Voraussetzung: der statusabhängige Int-Vektor wurde ausgewählt).

Da die Paritätsfehler- und Empfängerüberlauffehler-Flags gespeichert werden, widerspiegelt der gelesene Fehlerstatus einen Fehler im laufenden Wort im Empfängerpuffer und irgendwelche Paritäts- oder Überlauffehler, die nach dem letzten Fehlerrücksetz-Befehl aufgetreten sind. Um eine Korrespondenz zwischen dem Zustand der Fehlerpuffer und dem Inhalt der Empfangsdatenpuffer zu erhalten, muß das Fehlerstatueregister vor den Daten gelesen werden.

**Tabelle 3: Asynchrone Betriebsart**

| Funktion | Typische Programmschritte | Bemerkungen |
| :--- | :--- | :--- |
| **Initialisierung** | **Register: geladene Information** | |
| | WR0: Kanal-RESET | Reset SIO |
| | WR0: Zeiger 2 | |
| | WR2: Interruptvektor | nur Kanal B |
| | WR0: Zeiger 4, RESET Extern/Status-Interrupt | |
| | WR4: Asynchrone Betriebsart, Paritätsinformation, Anzahl der Stoppbits, Taktrate | Parameterausgabe |
| | WR0: Zeiger 3 | |
| | WR3: Empfängerfreigabe, Auto Enables, Empfängerzeichenlänge | |
| | WR0: Zeiger 5 | |
| | WR5: REQUEST TO SEND, Senderfreigabe, Senderzeichenlänge, DATA TERMINAL READY | Empfänger und Sender voll initialisiert, AUTO ENABLES gibt den Sender frei, wenn $\overline{\text{CTS}}$ aktiv ist und den Empfänger, wenn $\overline{\text{DCD}}$ aktiv ist. |
| | WR0: Zeiger 1, RESET Extern/Status-Interrupt | |
| | WR1: Sendeinterruptfreigabe, statusabhängiger Vektor, Interrupt bei jedem Empfangszeichen, Sperrung der WAIT/READY-Funktion, Extern-Interrupt-Freigabe | Sende/Empfangs-Interruptarten ausgewählt. Externer Interrupt überwacht den Status von den $\overline{\text{CTS}}$, $\overline{\text{DCD}}$ und $\overline{\text{SYNC}}$-Eingängen und erkennt die Breakfolge. Statusabhängiger Vektor nur in Kanal B. |
| | **Übertrage erstes Datenbyte an die SIO** | Dieses Datenbyte muß übertragen werden, da sonst keine Interrupts auftreten |
| **Wartezustand** | Ausführung des Haltebefehls oder irgendeines anderen Programmes | Das Programm wartet auf einen Interrupt der SIO |
| **Datenübertragung und Fehlerüberwachung** | Während des Interrupt-Anforderungs-/Annahme-Zyklus wird RR2 zur CPU übertragen. | Sobald der Interruptvektor erscheint, wird er modifiziert durch: 1. Verfügbare Empfangszeichen, 2. Leerer Sendepuffer, 3. Veränderung Extern/Status, 4. Spezielle Empfangsbedingungen |
| | **Wenn ein Zeichen empfangen wird:** | |
| | - Übertragung des Datenzeichens zur CPU | |
| | - Aktualisierung der Zeiger und Parameter | |
| | - Rückkehr vom Interrupt | |
| | **Wenn der Sendepuffer leer ist:** | |
| | - Übertragung eines Datenzeichens zur SIO | Programmsteuerung wird an eine der acht Interrupt-Service-Routinen übertragen. |
| | - Aktualisierung der Zeiger und Parameter | |
| | - Rückkehr vom Interrupt | |
| | **Wenn der externe Status sich ändert:** | |
| | - Übertragung von RR0 zur CPU | Wenn andere Prozessoren als der U 880 verwendet werden, sollte der modifizierte Interruptvektor (RR2) im Interrupt-Anforderungs-/Annahme-Zyklus zur CPU gelangen. |
| | - Ausführen von Fehlerroutinen (einschließlich Breakerkennung) | |
| | - Rückkehr vom Interrupt | |
| | **Wenn eine spezielle Empfangsbedingung auftritt:** | |
| | - Übertragung von RR1 zur CPU | |
| | - Ausführung einer speziellen Fehlerroutine (z. B. Rahmenfehler) | |
| | - Rückkehr vom Interrupt | |
| | **Neubestimmung der Empfänger/Sender-Interruptarten** | Wenn die Sender- oder Empfänger-Datenübertragung vollständig ist. |
| **Ende der Datenübertragung** | Sperrung der Sende-/Empfangs-Betriebsarten | |
| | Aktualisierung der Modemsteuerausgänge (z. B. RTS OFF) | In der Sendebetriebsart zeigt das "Sender leer" - Statusbit $D_0, RR1$ an, daß der Sendevorgang abgeschlossen ist. |

Diese Aufgabe kann leicht ausgeführt werden, indem man den Vektorinterrupt nutzt, da ein spezieller Interruptvektor für diese Bedingungen erzeugt wird.
Ist der Extern/Status-Interrupt freigegeben, dann verursacht die Breakerkennung einen Interrupt und das Breakerkennungs-Statusbit (RR0, $D_7$) wird gesetzt. Als Reaktion zum ersten Breakerkennungsinterrupt, welcher einen Breakstatus von 1 (RR0, $D_7$) besitzt, sollte der "Reset Extern/Status Interrupt"-Befehl zur SIO ausgesendet werden. Der SIO überwacht den Empfänger-Daten-Eingang, um das Ende der Break-Folge zu erkennen und die CPU nach dem Nullstellen des Breakstatus zu unterbrechen. Die CPU muß in ihrer Interruptserviceroutine wieder den Reset-Befehl für den Extern/Status-Interrupt aussenden, um die Breakerkennungslogik neu zu initialisieren.
Der Extern/Status-Interrupt überwacht auch den Status von $\overline{\text{DCD}}$. Wenn das $\overline{\text{DCD}}$-Pin für einen Zeitraum inaktiv wird, der iänger ist als die minimal spezifizierte Impulsbreite, dann wird ein Interrupt hervorgerufen, wobei das DCD-Statusbit (RR0, $D_3$) auf 1 gesetzt wird. Es ist zu beachten, daß der $\overline{\text{DCD}}$-Eingang im RR0-Statusregister invertiert wird. Wird der Status nach den Daten gelesen, sind die Fehlerdaten für das nächste Wort auch mit eingeschlossen, wenn es im Puffer gespeichert worden ist. Wenn die Operationen schnell genug ausgeführt werden, so daß das nächste Zeichen noch nicht vollständig empfangen wurde, bleibt das Statueregister für das letzte empfangene Byte gültig. Eine Ausnahme tritt auf bei der Betriebsart "Interrupt nur beim ersten Zeichen". Ein spezieller Interrupt in dieser Betriebsart speichert die Fehlerdaten und das Zeichen (selbst wenn sie vom Puffer gelesen werden) bis der Fehlerrücksetz-Befehl erteilt wird. Dies verhindert, daß weitere Daten im Empfänger zur Verfügung gestellt werden, bis der Reset-Befehl erteilt ist. Gleichzeitig kann die CPU auf das fehlerhafte Zeichen reagieren, auch wenn DMA- oder Blockübertragungstechnik angewendet wird.
Wenn die Betriebsart "Interrupt bei jedem Zeichen" ausgewählt wurde, dann ist der Interruptvektor unterschiedlich, falls in RR1 ein Fehlerstatus auftritt. Tritt ein Empfängerüberlauf auf, wird das neueste der erhaltenen Zeichen in den Puffer geladen; das vorausgehende Zeichen geht verloren. Wenn das über die anderen Zeichen geschriebene Zeichen gelesen wird, wird das Empfängerüberlaufbit gesetzt und der Vektor für die spezielle Empfangsbedingung wird ausgesandt, unter der Voraussetzung, daß der statusabhängige Vektor freigegeben ist.
Im Polling-Betrieb muß $D_0$ von RR0 (Zeichen im Empfängerpuffer) überwacht werden, damit die CPU weiß, wann sie ein Zeichen lesen kann. Dieses Bit wird automatisch zurückgesetzt, wenn die Empfängerpuffer gelesen sind. Um das Überschreiben von Daten im Polling-Betrieb zu vermeiden, muß der Senderpufferstatus überprüft werden, bevor in den Sender eingeschrieben wird. Das Senderpuffer-leer-Bit wird immer dann auf 1 gesetzt, wenn der Sendepuffer leer ist.

**4.3. Synchrone Betriebsart**

Bevor das synchrone Senden und Empfangen näher beschrieben wird, sollen die 3 Arten der Zeichensynchronisation - Monosync, Bisync und Extern-Sync - näher erläutert werden. Diese Betriebsarten benutzen den x1 Takt für Sende- und Empfangsoperationen. Die Daten werden an der steigenden Flanke des Empfangstaktes ($\overline{\text{RxC}}$) abgetastet. Die Übergänge der Sendedaten erfolgen jeweils an der fallenden Flanke des Sendetaktes ($\overline{\text{TxC}}$). Monosync, Bisync und Extern-Sync unterscheiden sich im wesentlichen durch die Art des Erreichens der Anfangssynchronisation. Die Betriebsart muß vor dem Laden der Sync-Zeichen ausgewählt werden, da die Register in den verschiedenen Betriebsarten unterschiedlich verwendet werden. Bild 12 zeigt die Formate für alle drei synchronen Betriebsarten.

*KI generierte Interpretation der Abbildung: Bild 12 zeigt drei verschiedene Datenrahmenformate.
1. MONOSYNC (Internsynchronisation): Ein einzelnes 8-Bit SYNC BYTE am Beginn, gefolgt vom DATEN-FELD und zwei 8-Bit CRC Bytes (CRC 1 und CRC 2) am Ende.
2. BISYNC (Internsynchronisation): Zwei 8-Bit SYNC BYTES nacheinander (SYNC BYTE 1, SYNC BYTE 2), gefolgt vom DATEN-FELD und den zwei CRC Bytes.
3. EXTERN-SYNC: Beginnt direkt mit dem DATEN-FELD, gefolgt von den zwei CRC Bytes. Hier wird kein internes Sync-Zeichen benötigt, da die Synchronisation über den physischen SYNC-Pin erfolgt.*

**Bild 12: Synchrone Formate**

**Monosync:**
Das Übereinstimmen eines einzelnen Sync-Zeichens (8-Bit-Sync-Betriebsart) mit dem programmierten Sync-Zeichen, das in WR7 gespeichert ist, bewirkt die Zeichensynchronisation und ermöglicht die Datenübertragung.

**Bisync:**
Die Zeichensynchronisation erfolgt bei Übereinstimmung von zwei aufeinanderfolgenden Sync-Zeichen (16-Bit-Sync-Betriebsart) mit den programmierten Sync-Zeichen, die in WR6 und WR7 gespeichert sind. Sowohl bei der Monosync- als auch der Bisync-Betriebsart wird SYNC als Ausgang verwendet. Dieser Ausgang ist aktiv für den Teil des Empfangstaktes, bei dem die Sync-Zeichen erkannt werden.
Mit Ausnahme von mit "01..." oder "001..." beginnenden Sync-Zeichen ist in WR7 jede beliebige Bit-Kombination verwendbar.

**Extern Sync:**
In dieser Betriebsart erfolgt die Zeichensynchronisation extern; SYNC ist ein Eingang, an den ein Signal zur externen Zeichensynchronisation angelegt wird. Nachdem das Sync-Muster erkannt wurde, muß die externe Logik zwei volle Empfangstakt-Zyklen warten, um den Sync-Eingang zu aktivieren. Der Sync-Eingang muß Low gehalten werden, bis die Zeichensynchronisation beendet ist. Der Zeichenempfang beginnt mit der steigenden Flanke von $\overline{\text{RxC}}$, die der fallenden Flanke von $\overline{\text{SYNC}}$ folgt.
In allen Fällen ist der Empfänger nach dem Rücksetzen in der Suchphase, während dieser Zeit versucht der SIO die Zeichensynchronisation zu erlangen. Die Suche kann nur beginnen, wenn der Empfänger freigegeben ist, und die Datenübertragung beginnt erst dann, wenn die Zeichensynchronisation vorhanden ist. Geht die Zeichensynchronisation verloren, kann die Suchphase (Hunt Phase) von neuem beginnen. Dazu ist ein Steuerwort zum SIO zu senden, in dem das Bit $D_4$, WR3 (Enter hunt Phase) gesetzt ist. Im Senderbetrieb wird immer die programmierte Anzahl von Sync-Bits (8 oder 16) ausgesandt. In der Monosync-Betriebsart benutzt der Sender das Sync-Byte aus WR6, jedoch der Empfänger vergleicht mit WR7.
In den Betriebsarten Monosync, Bisync und Extern Sync werden Daten so lange empfangen, bis der SIO zurückgesetzt wird, oder bis der Empfänger gesperrt wird (durch Befehl oder durch $\overline{\text{DCD}}$ in der Auto Enables-Betriebsart), oder bis die CPU das Enter-Hunt-Phase-Bit setzt. Nachdem erst einmal die Synchronisation erreicht worden ist, sind die Operationen der Betriebsarten Monosync, Bisync und Extern Sync im wesentlichen gleich. Die verbleibenden Unterschiede werden im folgenden näher erläutert.
Tabelle 4 zeigt, wie WR3, WR4 und WR5 in den synchronen Empfangs- und Sendeoperationen verwendet werden. WR0 dient als Zeiger für die anderen Register und enthält noch verschiedene Befehle. WR1 definiert die Interruptarten, WR2 speichert den Interruptvektor und WR6 und WR7 enthalten die Sync-Zeichen. Tabelle 5 zeigt die typischen Programmschritte für eine Halb-Duplex-Bisync-Sendeoperation.

**4.3.1. Synchrones Senden**

**Initialisierung**
Das Systemprogramm muß den Sender mit den folgenden Parametern initialisieren: ungerade oder gerade Parität, x1 Taktrate, 8- oder 16-Bit-Sync-Zeichen, CRC-Polynom, Senderfreigabe, Sendeanforderung (Request To Send), Terminal-Bereitschaft (Data Terminal Ready), Interruptarten und Sendezeichenlänge. Die WR4-Parameter müssen vor den WR1, WR3, WR5, WR6 und WR7-Parametern oder Befehlen ausgesandt werden.

**Tabelle 4: Inhalt der Schreibregister 3, 4 und 5 in den synchronen Betriebsarten**

| Register | BIT7 | BIT6 | BIT5 | BIT4 | BIT3 | BIT2 | BIT1 | BITO |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **WR3** | 00=Rx5 Bits/Zeichen | 10=Rx6 Bits/Zeichen | Auto | Beginne | Empfän- | 0 | Sync- | Empfän- |
| | 01=Rx7 Bits/Zeichen | 11=Rx8 Bits/Zeichen | Enables | Such- | ger CRC- | | Lade- | ger- |
| | | | | Betrieb | Freigabe | | sperre | Freigabe |
| **WR4** | 0 | 0 | 00=8 Bit Sync-Zeich. | 0 | 0 | gerade, | Parität | |
| | | | 01=16 Bit Sync-Zeich. | | | ungerade | | |
| | | | 10=SDLC-Mode | | | Parität | | |
| | | | 11=Ext Sync Mode | | | | | |
| **WR5** | DTR | 00=Tx5 Bits (od.wen.)/Zeichen | 10=Tx6 Bits/Zeichen | Break- | Sender- | 1 | RTS | Sender- |
| | | 01=Tx7 Bits/Zeichen | 11=Tx8 Bits/Zeichen | Senden | Freigabe | CRC-16 | | CRC-Freigabe |

Eins der zwei Polynome - CRC-16 ($x^{16}+x^{15}+x^2+1$) oder SDLC ($x^{16}+x^{12}+x^5+1$) - kann mit synchronen Betriebsarten verwendet werden. In beiden Fällen (SDLC-Betriebsart nicht ausgewählt) werden der CRC-Generator und Tester auf Null zurückgesetzt. Im Sender-Initialisierungsprozeß wird der CRC-Generator initialisiert durch Setzen des Befehls (WR0) Reset Transmit CRC-Generator. Sowohl der Sender als auch der Empfänger benutzen das gleiche Polynom.
Sendeinterruptfreigabe oder Wait/Ready-Freigabe können für die Übertragung der Daten ausgewählt werden. Der Extern/Status-Interrupt wird zur Überwachung des Status des Clear-To-Send-Einganges verwendet, sowie für das Sender-leer/EOM-Catch. Wahlweise kann die Auto-Enable-Betriebsart verwendet werden, um den Sender freizugeben, wenn $\overline{\text{CTS}}$ aktiv ist. Die erste Datenübertragung zum SIO kann beginnen, wenn der Extern/Status-Interrupt auftritt (CTS-Status-Bit gesetzt) oder unmittelbar nach dem Senderfreigabe-Befehl (wenn Auto-Enable-Mode vorliegt). Wenn der Sender nicht freigegeben ist, wird der Senderausgang auf High gehalten. Ein Break kann programmiert werden, um ein Low (Spacing) zu erzeugen, welches beginnt, sobald das Send-Break-Bit gesetzt ist. Werden einem voll initialisierten und freigegebenem Sender keine Daten zugeführt (default condition) so sendet er ständig 8- oder 16-Bit-Sync-Zeichen.

**Datenübertragung und Statusüberwachung**
In dieser Phase gibt es mehrere Kombinationen von Interrupts und Wait/Ready.

**Datenübertragung mit Interruptbetrieb**
Wenn das Transmit-Interrupt-Enable-Bit (WR1, $D_1$) gesetzt ist, wird jedesmal ein Interrupt erzeugt, wenn der Senderpuffer leer wird. Dieser Interrupt kann entweder beantwortet werden durch Einschreiben eines anderen Zeichens in den Sender oder durch Rücksetzen des Transmitter Interrupt Pending Catch mit dem Befehl (WR0, CMD5) Reset Transmitter Pending. Wenn dem Interrupt mit diesem Befehl Genüge getan wurde und nichts mehr in den Sender eingeschrieben wird, kann es keine weiteren Senderpuffer-leer-Interrupte (Transmit Buffer Empty) mehr geben, weil gerade der Vorgang, daß der Puffer leer wird, die Interrupts hervorruft und der Puffer nicht leer werden kann, wenn er bereits leer ist. Diese Situation verursacht eine Sender-leer (Transmit-Underrun)-Bedingung, welche im Abschnitt "Bisync Transmit Underrun" erläutert wird.

**Datenübertragung unter Benutzung von WAIT/READY**
Der CPU zeigt die Aktivierung von $\overline{\text{WAIT}}$ an, daß der SIO nicht bereit ist, Daten anzunehmen und daß die CPU den Ausgabezyklus verlängern muß. Für eine DMA-Steuerung zeigt READY an, daß der Senderpuffer leer ist, und daß der SIO bereit ist, das nächste Datenzeichen anzunehmen. Wenn das Datenzeichen nicht rechtzeitig in den SIO geladen wird, wird das Sendeschieberegister leer und der SIO tritt in die Transmit-Underrun-Bedingung ein.

**Bisync Transmit Underrun**
Im Bisync-Format werden Füllzeichen eingesetzt, um die Synchronisation aufrecht zu erhalten, wenn der Sender keine Daten zum Senden hat (Transmit Underrun Condition). Der SIO hat zwei programmierbare Möglichkeiten für die Lösung dieser Situation: er kann Sync-Zeichen einfügen, oder er kann die bisher erzeugten CRC-Zeichen senden, gefolgt von den Sync-Zeichen. Diese Möglichkeiten werden durch den Befehl Reset Transmit Underrun/EOM in WR0 gesteuert. Nach einem Baustein- oder Kanal-Reset ist das Transmit Underrun/EOM Status-Bit (RR0, $D_6$) gesetzt und erlaubt das Einfügen von Sync-Zeichen, wenn es keine Daten zu senden gibt. CRC wird für die automatisch eingefügten Sync-Zeichen nicht berechnet. Wenn die CPU das Ende der Nachricht feststellt, kann ein Reset Transmit Underrun/EOM-Befehl ausgegeben werden. Das ermöglicht, daß CRC gesendet wird, wenn der Sender keine Daten hat. In diesem Fall sendet der SIO CRC, gefolgt von Sync-Zeichen, um die Nachricht abzuschließen.
Es gibt keine Einschränkung dafür, wann in der Nachricht das Transmit-Underrun/EOM-Bit zurückgesetzt werden kann. Wenn EOM-Reset ausgegeben wird, nachdem das erste Datenzeichen geladen worden ist, wird das 16-Bit-CRC gesendet, gefolgt von Sync-Zeichen zu dem Zeitpunkt, wenn der Sender keine Daten zum Senden hat. Auf Grund der Transmit Underrun-Bedingung wird ein Extern/Status-Interrupt erzeugt, wenn das Transmit Underrun/EOM-Bit gesetzt wird.
Im Falle der Sync-Zeichen-Einfügung wird ein Interrupt erst erzeugt, nachdem das erste automatisch eingefügte Sync-Zeichen geladen worden ist. Der Status zeigt an, daß das Transmit-Underrun/EOM-Bit und das Transmit-Buffer-Empty-Bit gesetzt sind.
Im Falle der CRC-Einfügung, wird das Transmit Underrun/EOM-Bit gesetzt und das Transmit-Buffer-Empty-Bit ist rückgesetzt, während CRC gesendet wird. Wenn CRC vollständig gesendet worden ist, wird das Transmit-Buffer-Empty-Status-Bit gesetzt und ein Interrupt erzeugt, um der CPU anzuzeigen, daß ein anderer Datenstrom beginnen kann (dieser Interrupt tritt auf, weil CRC gesendet und Sync geladen worden ist). Wenn keine weiteren Daten mehr zu senden sind, kann das Programm die Übertragung abschließen, indem $\overline{\text{RTS}}$ rückgesetzt und der Sender gesperrt (WR5, $D_3$) wird.
Auffüllzeichen können gesendet werden, indem der SIO auf 8 Bits/Zeichen programmiert wird und FF in den Sender eingeschrieben wird, während CRC am Ausgang des Senders erscheint. Als Alternative können die Sync-Zeichen während dieser Zeit als Auffüllzeichen (pad characters) umdefiniert werden.
Das folgende Beispiel wurde angeführt, um diesen Punkt zu verdeutlichen. Der SIO erzeugt einen Interrupt (durch das Setzen des Transmit-Buffer-Empty-Bits). Die CPU erkennt, daß das letzte Zeichen (ETX) der Nachricht bereits zur SIO gesendet wurde, indem sie den internen Programmstatus überprüft.
Um den SIO zum Senden von CRC zu veranlassen, sendet die CPU den Reset Transmit Underrun/EOM Latch-Befehl (WR0) aus und genügt dem Interrupt mit dem Reset-Transmit-Interrupt-Pending-Befehl. (Dieser Befehl verhindert, daß der SIO mehr Daten anfordert.) Da durch diesen Befehl ein Transmit-Underrun hervorgerufen wird, beginnt der SIO CRC zu senden. Der SIO ruft auch einen Extern/Status-Interrupt hervor durch das Setzen des Transmit-Underrun/EOM Latch.
Die CPU genügt diesem Interrupt, indem sie Füllzeichen (pad characters) in den Senderpuffer lädt und den Befehl Reset Extern/Status-Interrupt aussendet. Bei diesem Ablauf folgen dem CRC Füllzeichen an der Stelle von Sync-Zeichen. Man beachte, daß der SIO mit einem Transmit-Buffer-Empty-Interrupt reagiert, wenn CRC komplett gesendet wurde und die Füllzeichen in das Senderschieberegister geladen wurden. Von diesem Punkt an kann die CPU mehr Füllzeichen oder Sync-Zeichen senden.

**Bisync CRC-Erzeugung**
Durch Setzen des Sender-CRC-Freigabe-Bits (WR5, $D_0$) beginnt die CRC-Berechnung, wenn das Programm das erste Datenzeichen zur SIO sendet. Der SIO sendet zwar automatisch bis zu zwei Sync-Zeichen (16-Bit-Sync), dennoch ist es ratsam, ein paar Sync-Zeichen mehr der Nachricht vorauszuschicken (bevor Sende-CRC wirksam gemacht wird), um die Synchronisation an der Empfangsseite zu sichern. Das Sender-CRC-Freigabe-Bit kann jederzeit während der Datenübertragung verändert werden, um bestimmte Datenzeichen in die CRC-Berechnung einzubeziehen oder sie auszuschließen. Das Sender-CRC-Freigabe-Bit muß in dem gewünschten Zustand sein, wenn das Datenzeichen vom Sende-Daten-Puffer in das Sendeschieberegister geladen wird. Um zu sichern, daß sich dieses Bit im richtigen Zustand befindet, muß zuerst das Sender-CRC-Freigabe-Bit ausgesendet werden, bevor das Datenzeichen zur SIO gelangt.

**Sender-Transparent-Betriebsart**
Die Transparent-Betriebsart (Bisync-Protokoll) wird ermöglicht durch die Fähigkeit, die Sender-CRC-Freigabe fliegend zu ändern und durch die zusätzliche Möglichkeit 16-Bit Sync-Zeichen einzufügen. Der Ausschluß von DLE Zeichen aus der CRC-Berechnung kann durch Sperrung der CRC-Berechnung erreicht werden, bevor die DLE-Zeichen zur SIO übertragen werden. Im Fall einer Transmit-Underrun-Bedingung im Transparent-Mode werden ein paar DLE-SYN-Zeichen gesendet. Der SIO kann zum Senden einer DLE-SYN-Folge programmiert werden, indem ein DLE-Zeichen in WR6 und ein Sync-Zeichen in WR7 geladen wird.

**Sendeabschluß**
Der SIO besitzt eine spezielle Abschlußbehandlung, die die Datenzusammengehörigkeit und -gültigkeit bewahrt. Wenn während der Aussendung eines Daten- oder Sync-Zeichens der Sender gesperrt wird, so wird dieses Zeichen wie gewöhnlich gesendet, aber es folgt dann kein CRC- oder Sync-Zeichen, sondern der Ausgang geht auf High (marking line). Bei der Sperrung des Senders bleibt ein im Puffer befindliches Zeichen erhalten. Wenn während der CRC-Aussendung der Sender gesperrt wird, wird die 16-Bit Übertragung abgeschlossen, aber es werden Sync-Zeichen gesendet anstatt CRC. Ein programmierter Abbruch (Break) wird wirksam, sobald er in das Befehlsregister eingeschrieben worden ist; Zeichen im Sendepuffer und im Schieberegister gehen verloren.
In allen Betriebsarten werden Zeichen mit dem niedrigsten Bit zuerst gesendet. Das erfordert ein rechtsbündiges Ausrichten der zu sendenden Daten, wenn die Wortlänge weniger als 8 Bits beträgt. Ist die Wortlänge fünf Bits oder weniger, muß das Sonderverfahren, das in der Diskussion des Schreibregisters 5 beschrieben ist (Programmierung des SIO), für das Datenformat angewendet werden. Die Zustände der unbenutzten Bits in einem Datenzeichen sind irrelevant, ausgenommen bei einem fünf-Bit- oder weniger als fünf Bit-Mode.
Wenn das Extern/Status-Interrupt-Enable-Bit gesetzt ist, verursachen Senderbedingungen wie "beginne CRC-Zeichen zu senden", "beginne Sync-Zeichen zu senden" und ein $\overline{\text{CTS}}$ verändernder Zustand Interrupts, die einen einheitlichen Vektor haben, wenn der statusabhängige Vektor gesetzt ist. Diese Interrupt-Betriebsart kann während Blockübertragungen verwendet werden. Alle Interrupts können gesperrt werden (für Operationen im Polling-Betrieb, oder um Interrupts zu unpassenden Zeiten während der Abarbeitung eines Programms zu unterbinden).

**Tabelle 5: Bisync-Sende-Betriebsart**

| Funktion | Typische Programmschritte | Bemerkungen |
| :--- | :--- | :--- |
| **Initialisierung** | **Register: geladene Information** | |
| | WR0: Kanal Reset, Reset Sender-CRC-Generator | Reset SIO |
| | WR0: Zeiger 2 | |
| | WR2: Interruptvektor | nur Kanal B |
| | WR0: Zeiger 3 | |
| | WR3: Auto Enables | Übertragung beginnt nur nachdem $\overline{\text{CTS}}$ erkannt wurde |
| | WR0: Zeiger 4 | |
| | WR4: Paritätsinformation, Information über Sync-Modes, x1-Taktrate | Senderparameter ausgegeben |
| | WR0: Zeiger 6 | |
| | WR6: Sync-Zeichen 1 | |
| | WR0: Zeiger 7, Reset Extern/Status-Interrupts | |
| | WR7: Sync-Zeichen 2 | |
| | WR0: Zeiger 1, Reset Extern/Status-Interrupts | |
| | WR1: Statusabh. Vektor, Extern-Interrupt-Freigabe, Sender-Interrupt-Freigabe oder WAIT/READY-Mode freigegeben | Die Extern-Interrupt-Betriebsart überwacht den Status der $\overline{\text{CTS}}$- und $\overline{\text{DCD}}$-Eingänge sowie den Status der Tx-Underrun/EOM-Speicherzelle. Der Senderinterrupt wird wirksam, wenn der Senderpuffer leer wird; die Wait/Ready-Betriebsart kann zum Datentransfer mit CPU oder DMA genutzt werden. |
| | WR0: Zeiger 5 | Statusabh. Vektor (nur Kanal B) |
| | WR5: Request To Send, Senderfreigabe, Bisync CRC, Sendezeichenlänge | Die Sender-CRC-Freigabe sollte erfolgen, wenn das erste Nicht-Sync-Zeichen zur SIO gesendet wird. |
| | **Erstes Sync-Byte zur SIO** | Benutze mehrere Sync-Zeichen am Beginn der Datenübertragung. Der Sender ist voll initialisiert. |
| **Wartezustand** | Ausführung einer Halt-Operation oder eines anderen Programmteils | Es wird auf einen Interrupt gewartet oder den Wait/Ready-Ausgang, um Daten zu übertragen. |
| **Datentransfer und Statusüberwachung** | **Wenn ein Interrupt (Wait/Ready) auftritt:** | Ein Interrupt tritt auf (Wait/Ready wird aktiv), wenn das erste Datenbyte gesendet wurde. |
| | - Einbeziehung/Ausschließung eines Datenbytes von der CRC-Berechnung. | Die Wait-Betriebsart erlaubt der CPU einen Blocktransfer vom Speicher zur SIO, die Ready-Betriebsart erlaubt der DMA einen Blocktransfer vom Speicher zur SIO. Der DMA-Baustein kann auf das Erkennen spezieller Steuerzeichen programmiert werden (es werden nur die Bits überprüft, die ASCII- oder EBCDIC-Steuerzeichen spezifizieren). Das Vorhandensein der Zeichen löst einen Interrupt aus. |
| | - Übertragung eines Datenbytes von der CPU (oder Speicher) zur SIO | |
| | - Erkennung und Setzen zugeordneter Flags für Steuerzeichen (in der CPU). | |
| | - Reset Tx Underrun/EOM Latch (WR0), wenn das letzte Zeichen der Daten erkannt wurde | |
| | - Aktualisierung der Zeiger und Parameter (CPU). | |
| | - Rückkehr vom Interrupt. | |
| | **Wenn das letzte Zeichen des I-Feldes gesendet wurde, führt die SIO folgendes aus:** | Tx Underrun/EOM zeigt entweder eine Sender-leer-Bedingung an (Sync-Zeichen wurden gesendet), oder das Ende der Datenübertragung (CRC-16 wurde gesendet). |
| | - Sendet CRC | |
| | - Sendet das Endeflag | |
| | - Unterbricht die CPU mit einem Buffer Empty-Status | |
| | **Wenn Fehlerbedingungen oder Status-Veränderungen auftreten:** | |
| | - Übertragung von RR0 zur CPU | |
| | - Ausführung einer Fehlerroutine | |
| | - Rückkehr vom Interrupt | |
| **Ende der Datenübertragung** | **Neubestimmung der Interruptarten** | Das Programm soll die Datenübertragung ordnungsgemäß abschließen. |
| | Aktualisierung der Modem-Steuer-Ausgänge (z. B. RTS ausschalten) | |
| | Sperrung Sendebetriebsart | |

**4.3.2. Synchroner Empfang**

**Initialisierung**
Das Systemprogramm initialisiert die synchronen Empfangsoperationen mit den folgenden Parametern: ungerade oder gerade Parität, 8- oder 16-Bit-Sync-Zeichen, x1-Takt-Betriebsart, CRC-Polynom, Empfangszeichenlänge usw.. Sync-Zeichen müssen in die Register WR6 und WR7 geladen werden. Die Empfänger können nur freigegeben werden, nachdem alle Empfangsparameter gesetzt worden sind. Die WR4-Parameter müssen vor den WR1-, WR3-, WR5-, WR6- und WR7-Parametern oder Befehlen ausgegeben werden.
Nach der Freigabe befindet sich der Empfänger in der Suchphase (Hunt-Phase). Er bleibt in dieser Phase, bis die Zeichensynchronisation erreicht worden ist. Es ist zu beachten, daß unter Programmsteuerung alle führenden Synczeichen der Daten vom Laden in die Empfangspuffer ausgeschlossen werden können, indem das Sync-Character-Load-Bit in WR3 gesetzt wird.

**Datenübertragung und Statusüberwachung**
Nachdem die Zeichensynchronisation erreicht worden ist, werden die empfangenen Zeichen in den Empfangsdaten-F/FP übertragen. Die folgenden vier Interrupt-Betriebsarten stehen für die Übertragung der Daten und des dazugehörigen Statusses an die CPU zur Verfügung.

**Keine Interrupts freigegeben**
Diese Betriebsart wird für reine Polling-Operationen oder für off-line-Bedingungen benutzt.

**Interrupt nur beim ersten Zeichen**
Diese Betriebsart wird normalerweise benutzt zum Starten einer Polling-Schleife oder eines Block-Transfer-Befehls, indem Wait/Ready genutzt wird, um die CPU oder die DMA auf die ankommende Datenrate zu synchronisieren. In dieser Betriebsart unterbricht der SIO nur beim ersten Zeichen und danach entsteht nur ein Interrupt, wenn eine spezielle Empfangsbedingung erkannt wurde. Die Betriebsart wird von neuem mit dem Befehl "Enable-Interrupt-On-Next-Receive-Character" initialisiert, damit das nächste Zeichen empfangen werden kann, um einen Interrupt zu erzeugen. Paritätsfehler rufen in dieser Betriebsart keine Interrupts hervor. Dies ist nicht der Fall bei Rahmenende (SDLC-Betriebsart) und Empfangsüberlauf. In der Betriebsart Extern/Status-Interrupt können $\overline{\text{DCD}}$-Status-Veränderungen jederzeit einen Interrupt hervorrufen.

**Interrupt bei jedem Zeichen**
Immer wenn ein Zeichen in den Empfangspuffer gelangt, wird ein Interrupt erzeugt. Fehler und spezielle Empfangsbedingungen erzeugen einen speziellen Vektor, wenn der statusabhängige Vektor ausgewählt wurde. Wahlweise kann eine Betriebsart benutzt werden, wo der Paritätsfehler keinen speziellen Interruptvektor erzeugt.

**Interrupts bei speziellen Empfangsbedingungen**
Der Interrupt bei spezieller Empfangsbedingung kann nur dann auftreten, wenn entweder die Betriebsart "Interrupt nur beim ersten Zeichen" oder "Interrupt bei jedem Empfangszeichen" ausgewählt wurde. Der Interrupt bei spezieller Empfangsbedingung wird durch die Empfängerüberlaufbedingung hervorgerufen. Da die Empfangsüberlauf- und Paritätsfehlerstatusbits gespeichert sind, widerspiegelt der Fehlerstatus - wenn er gelesen wird - einen Fehler im laufenden Wort im Empfangspuffer zusätzlich zu allen Paritäts- oder Überlauffehler, die seit dem letzten Fehler-Rücksetz-Befehl empfangen wurden. Diese Status-Bits können nur durch den Fehler-Rücksetz-Befehl zurückgesetzt werden.

**CRC-Fehlerkontrolle und Abschluß**
Eine CRC-Fehlerkontrolle der Empfangenachricht kann unter Programmsteuerung pro Zeichen erfolgen. Das Empfangs-CRC-Freigabe-Bit (WR3, $D_3$) muß durch das Programm gesetzt/rückgesetzt werden, bevor das nächste Zeichen vom Empfangsschieberegister in das Empfangs-Puffer-Register übertragen wird. Dies sichert das richtige Ein- oder Ausschließen von Datenzeichen in die CRC-Kontrolle.
Um der CPU genügend Zeit zu lassen, die CRC-Kontrolle an einem besonderen Zeichen wirksam oder unwirksam zu machen, berechnet der SIO-CRC acht Bit-Zeiten nachdem das Zeichen zum Empfangspuffer übertragen worden ist. Wenn die CRC-Berechnung freigegeben ist, bevor das nächste Zeichen übertragen wird, dann wird CRC für das übertragene Zeichen berechnet. Wenn CRC gesperrt wird vor der nächsten Übertragung, dann wird die Berechnung für das anstehende Wort fortgesetzt, aber das Wort, das gerade in den Puffer übertragen wird, wird nicht einbezogen. Wenn diese Forderungen erfüllt werden müssen, kann der 3 Byte-Empfangsdatenpuffer für eine byteorientierte Bisync-Operation nicht in voller Tiefe genutzt werden. CRC kann freigegeben oder gesperrt werden so oft es für eine gegebene Berechnung notwendig ist.
In den Monosync-, Bisync- und Extern-Sync-Betriebsarten enthält das CRC/Rahmenfehlerbit (RR1, $D_6$) das Vergleichsresultat der CRC-Kontrolle 16 Bitzeiten (acht Bits Verzögerung und acht Bits Verschiebung für CRC) nachdem das Zeichen vom Empfangsschieberegister in den Puffer übertragen wurde. Für eine fehlerfreie Übertragung sollte das Resultat Null sein. (Beachte, daß das Resultat nur am Ende der CRC-Berechnung gültig ist. Wenn das Ergebnis vor diesem Zeitpunkt überprüft wird, zeigt es gewöhnlich einen Fehler an.) Der Vergleich wird mit jeder Übertragung hergestellt und ist nur so lange gültig, so lange das Zeichen im Empfangs-P/PO verbleibt.
Es folgt ein Beispiel für die CRC-Kontroll-Operation, wenn 4 Zeichen (A, B, C und D) in folgender Ordnung empfangen werden.
1. Zeichen A wird in den Puffer geladen.
2. Zeichen B wird in den Puffer geladen.
3. Wenn CRC gesperrt ist, bevor C sich im Puffer befindet, dann wird CRC nicht für B berechnet.
4. Zeichen C wird in den Puffer geladen.
5. Nachdem C geladen ist, zeigt das CRC-Rahmenfehlerbit das Ergebnis des Vergleiches durch Zeichen A.
6. Zeichen D wird in den Puffer geladen.
7. Nachdem D im Puffer ist, zeigt das CRC-Fehlerbit das Ergebnis des Vergleiches durch Zeichen B, ob nun B in die CRC-Berechnung einbezogen wurde oder nicht.

Infolge der seriellen Natur der CRC-Berechnung muß der Empfangstakt ($\overline{\text{RxC}}$) 16 mal (8-Bit Verzögerung plus 8 Bit CRC-Verschiebung) erscheinen, nachdem das zweite CRC-Zeichen in den Empfangspuffer geladen wurde, oder 20 mal (die erwähnten 16 plus 3 Bit Puffer-Verzögerung und 1 Bit Eingangsverzögerung) nachdem das letzte Bit im RxD Eingang ist, bevor die CRC-Berechnung komplett ist. Ein schneller externer Takt kann an dem Empfangstakt-Eingang angeschlossen werden, um die geforderten 16 Zyklen zu liefern.

**Tabelle 6: Bisync-Betriebsart (Empfang)**

| Funktion | Typische Programmschritte | Bemerkungen |
| :--- | :--- | :--- |
| **Initialisierung** | **Register: geladene Information** | |
| | WR0: Kanal Reset, Reset Empfangs-CRC-Prüfer | Reset SIO; Initialisierung Empfangs-CRC-Prüfer |
| | WR0: Zeiger 2 | |
| | WR2: Interruptvektor | nur Kanal B |
| | WR0: Zeiger 4 | |
| | WR4: Paritätsinformation, Sync-Betriebsart, x1 Taktrate | Ausgabe Empfangsparameter |
| | WR0: Zeiger 5, Reset Extern Status Interrupt | |
| | WR5: Bisync CRC-16, Data Terminal Ready | |
| | WR0: Zeiger 3 | |
| | WR3: Sync-Ladesperre, Empfänger-CRC-Freigabe, Beginne Suchbetrieb (Hunt Mode), Empfängerzeichenlänge | Die Sync-Ladesperre verhindert das Laden der führenden Sync-Zeichen am Beginn der Nachricht. Im Auto-Enables Mode werden die Daten erst dann angenommen, wenn der $\overline{\text{DCD}}$-Eingang aktiv ist. |
| | WR0: Zeiger 6 | |
| | WR6: Sync-Zeichen 1 | |
| | WR0: Zeiger 7 | |
| | WR7: Sync-Zeichen 2 | |
| | WR0: Zeiger 1, Reset Extern/Status-Interrupt | |
| | WR1: Statusabh. Vektor, Extern-Interrupt-Freigabe, Empfangsinterrupt nur beim ersten Zeichen | In dieser Interrupt-Betriebsart wird nur das erste Datenzeichen zur CPU übertragen. Alle folgenden Daten werden auf der Basis eines DMA übertragen; jedoch unterbrechen Interrupts bei einer speziellen Empfangsbedingung die CPU. Der statusabhängige Vektor wird nur in Kanal B genutzt. |
| | WR0: Zeiger 3, Interruptfreigabe für das nächste Empfangszeichen | Das Rücksetzen dieser Interrupt-Betriebsart liefert eine einfache Programmschleife für den Beginn weiterer Datenübertragungen. |
| | WR3: Empfängerfreigabe, Sync-Ladesperre, Beginn Suchbetrieb (Hunt Mode), Auto Enable, Empfängerwortlänge | WR3 wird erneut ausgegeben, um den Empfänger freizugeben. Die Empfänger-CRC-Freigabe muß gesetzt werden, nachdem SOH- oder STX-Zeichen empfangen wurden. |
| **Wartezustand** | Ausführung des Haltbefehls oder anderer Programmteile | Die Empfänger-Betriebsart ist voll initialisiert und das System wartet auf einen Interrupt beim ersten Zeichen. |
| **Datenübertragung und Statusüberwachung** | **Wenn ein Interrupt beim ersten Zeichen auftritt, führt die CPU folgendes aus:** | Während der Suchphase (Hunt Mode) erkennt der SIO zwei aufeinanderfolgende Zeichen für die Herstellung der Synchronisation. Die CPU stellt die DMA-Betriebsart her und alle nachfolgenden Datenzeichen werden durch die DMA übertragen. |
| | - Übertragung des Datenbytes zur CPU | |
| | - Erkennen und Setzen der zugehörigen Flags für die Steuerzeichen (in der CPU) | |
| | - Ein-/Ausschließen von Datenbytes in die CRC-Kontrolle | |
| | - Aktualisierung der Zeiger und anderen Parameter | |
| | - Freigabe von Wait/Ready für die DMA-Operation | |
| | - Freigabe des DMA-Controllers | |
| | - Rückkehr vom Interrupt | |
| | **Wenn Wait/Ready aktiv wird, dann führt der DMA-Controller folgendes aus:** | Das DMA-Steuergerät wird auch dazu programmiert, spezielle Zeichen zu erkennen (durch Überprüfung nur solcher Bits die ASCII- oder EBCDIC-Steuerzeichen entsprechen) und die CPU bei ihrer Feststellung zu unterbrechen. Als Reaktion überprüft die CPU den Status oder die Steuerzeichen und leitet entsprechende Handlungen ein (z. B. CRC-Freigabe aktualisieren). |
| | - Übertragung des Datenbytes zum Speicher | |
| | - Unterbrechung der CPU, wenn ein spezielles Zeichen durch den DMA-Controller erkannt wurde | |
| | - Unterbrechung der CPU, wenn das letzte Zeichen der Nachricht erkannt wurde | |
| | **Zur Beendigung der Datenübertragung führt die CPU folgendes aus:** | Der SIO ruft bei einer Fehlerbedingung in der CPU einen Interrupt hervor und die Fehlerroutine unterbricht die laufende Nachrichtenübertragung, klärt die Fehlerbedingung und wiederholt die Operation. |
| | - Übertragung von RR1 zur CPU | |
| | - Setzen des ACK/NAK-Antwort-Flags basierend auf dem CRC-Resultat | |
| | - Aktualisierung der Zeiger und Parameter | |
| | - Rückkehr vom Interrupt | |
| **Ende der Datenübertragung** | Neubestimmung der Interrupt- und Sync-Betriebsarten | |
| | Aktualisierung der Modem-Steuersignale | |
| | Sperrung der Empfangsbetriebsart | |

**4.4. SDLC (HDLC) Betriebsart**

Der SIO kann sowohl das Format "High-level Synchronous Data Link Control" (HDLC) als auch das Format "IBM Synchronous Data Link Control" (SDLC) übertragen. Da sich SDLC und HDLC sehr ähnlich sind, wird in dem folgenden Text nur auf SDLC Bezug genommen. Die SDLC-Betriebsart unterscheidet sich beträchtlich von dem synchronen Bisync-Format, weil es bitorientiert und nicht zeichenorientiert ist, wodurch natürlich auch eine transparente Operation ausgeführt werden kann. Die Bitorientierung macht SDLC zu einem flexiblen Format hinsichtlich Nachrichtenlänge und Bitmuster. Der SIO besitzt mehrere Einrichtungen für die Bearbeitung verschiedener Nachrichtenlängen.
Die SDLC-Nachricht, die man auch als Rahmen (Bild 13) bezeichnet, wird durch Flags eröffnet und beendet ähnlich den Sync-Zeichen im Bisync-Protokoll. Der SIO überträgt und erkennt Flagzeichen, die den Anfang und das Ende des Rahmens kennzeichnen.

*KI generierte Interpretation der Abbildung: Bild 13 zeigt das „Sende/Empfangs-SDLC/HDLC-Datenformat“. Der Rahmen ist wie folgt aufgebaut:
- Beginn: Ein 8-Bit FLAG (01111110)
- ADRESSE: 8 Bits
- DATEN-FELD: Ein variables Feld („I“ für Information)
- Rahmenprüfung: Zwei 8-Bit CRC Bytes (CRC 1 und CRC 2)
- Ende: Ein abschließendes 8-Bit FLAG (01111110).*

**Bild 13: Sende/Empfangs-SDLC/HDLC-Datenformat**

Es ist zu beachten, daß der SIO Null-Flags empfangen kann, aber nicht aussenden. Das 8-Bit-Adressenfeld eines SDLC-Rahmens enthält die sekundäre Stationsadresse. Der SIO besitzt eine Adressen-Such-Betriebsart, die die sekundäre Stationsadresse erkennt und so den Rahmen annehmen oder ablehnen kann.
Da das Steuerfeld des SDLC-Rahmens für den SIO transparent ist, ist es einfach, ihn zur CPU zu übertragen. Der SIO bearbeitet die Rahmen-Kontroll-Folge so, daß das Programm vereinfacht wird durch die Einbeziehung von folgenden Arbeitsweisen, wie die Initialisierung des CRC-Generators auf alles Einsen, das Rücksetzen des CRC-Prüfers, wenn das Anfangsflag in der Empfangsbetriebsart erkannt wurde und das Senden der Rahmen-Kontroll/Flag-Folge in der Sendebetriebsart. Die Hardware für die Steuerung wird dadurch vereinfacht, daß eine automatische Null-Einfügungs- und Ausblendlogik im SIO enthalten ist.
Tabelle 7 zeigt den Inhalt von WR3, WR4 und WR5 während der SDLC-Empfangs- und Sendebetriebsarten. WR0 dient als Zeiger für die anderen Register und enthält verschiedene Befehle. WR1 definiert die Interrupt-Betriebsarten. WR2 enthält den Interruptvektor. WR7 speichert das Flag-Zeichen und WR6 die sekundäre Adresse.

**4.4.1. SDLC-Senden**

**Initialisierung**
Wie im synchronen Betrieb muß die SDLC-Sendebetriebsart mit den folgenden Parametern initialisiert werden: SDLC-Betriebsart, SDLC-Polynom, Sendeanforderung (Request To Send), Terminalbereitschaft (Data Terminal Ready) Senderzeichenlänge, Senderinterrupt-Betriebsarten (oder Wait/Ready-Funktion), Senderfreigabe, Selbstfreigabe (Auto Enables) und Extern/Status-Interrupt.
Die Auswahl der SDLC-Betriebsart und des SDLC-Polynoms veranlaßt den SIO, den CRC-Generator auf lauter Einsen zu initialisieren. Dies geschieht durch Ausgabe des Befehls "Reset-Sende-CRC-Generator" (WR0). Ausführliche Informationen über die Interruptbetriebsarten sind im Abschnitt "Synchrone Betriebsart" zu finden. Nach einem Reset oder wenn der Sender nicht freigegeben ist, befindet sich der Senderausgang auf "High" (Marking). Ein Break kann programmiert werden, um am Ausgang ein ständiges "Low" (Spacing) zu erzeugen. Bei vollständig initialisiertem und freigegebenem Sender erscheinen am Senderdatenausgang ständig Flags.
Eine Abbruch-(Abort)Folge kann gesendet werden, indem der Send-Abort-Befehl (WR0, CMD1) ausgegeben wird. Dies bewirkt, daß mindestens acht, aber weniger als vierzehn Einsen gesendet werden, ehe wieder ständig Flags erscheinen. Es ist möglich, daß die Abortfolge (bestehend aus acht Einsen) auf bis zu 5 aufeinanderfolgende Einsen (gestattet durch die Nulleinfügungslogik) folgen kann und folglich bis zu 13 Einsen gesendet werden. Alle Daten, die gesendet werden und alle Daten im Sendepuffer gehen verloren, wenn ein Abort ausgegeben wird. Eine zusätzliche Null wird automatisch eingefügt, wenn im Datenstrom fünf aufeinanderfolgende Einsen auftreten. Dies gilt nicht für Aborts oder Flags.

**Datenübertragung und Statusüberwachung**
Es gibt mehrere Kombinationen von Interrupts und der Wait/Ready-Funktion in der SDLC-Betriebsart.

**Datenübertragung mittels Interrupts**
Wenn das Sende-Interrupt-Freigabe-Bit gesetzt ist, wird jedesmal ein Interrupt erzeugt, wenn der Puffer leer wird. Dem Interrupt kann Genüge getan werden, entweder durch das Einschreiben eines anderen Zeichens in den Sender oder durch Rücksetzen des Transmit Interrupt Pending-Speichers mit einem Reset Transmitter-Pending-Befehl (WR0, CMD5). Wurde der Interrupt mit diesem Befehl beantwortet und nichts mehr in den Sender eingeschrieben, dann treten keine weiteren Senderinterrupts auf. Das Ergebnis ist eine Sender-leer-Bedingung (Transmit Underrun). Wenn ein anderes Zeichen eingeschrieben und ausgesendet wird, dann kann der Sender erneut leer werden und die CPU unterbrechen. Im Anschluß an die Flags in einer SDLC-Operation können unter Benutzung der Sende-Interrupt-Betriebsart das 8-Bit-Adressenfeld, das Steuerfeld und das Informationsfeld zum SIO gesendet werden. Der SIO sendet die Rahmenkontrollfolge (Frame check), indem die Transmit Underrun-Bedingung verwendet wird. Wenn der Sender das erste Mal freigegeben wird, ist er bereits leer und kann dann nicht mehr leer werden. Daher können keine Transmit-Buffer-Empty-Interrupts auftreten, bevor das erste Datenzeichen geschrieben ist.

**Datenübertragung unter Benutzung von Wait/Ready**
Wenn die Wait/Ready-Funktion ausgewählt worden ist, zeigt Wait der CPU an, daß der SIO nicht zu einer Datenübertragung von oder zur CPU bereit ist und die CPU den I/O-Zyklus verlängern muß. Einem DMA-Steuergerät zeigt Ready an, daß der Senderpuffer leer ist, und daß der SIO bereit ist, das nächste Zeichen aufzunehmen. Wenn das Datenzeichen nicht rechtzeitig in den SIO geladen wird, dann wird das Sendeschieberegister leer und der SIO tritt in die Transmit-Underrun-Bedingung ein. Adreß-, Steuer- und Informationsfeld können in dieser Betriebsart zur SIO übertragen werden, indem die Wait/Ready-Funktion benutzt wird. Der SIO sendet die Rahmen-Kontroll-Folge mittels der Transmit Underrun-Bedingung.

**SDLC Transmit Underrun/Ende der Nachricht**
SDLC-ähnliche Formate haben keine Einrichtungen für das Auffüllen von Zeichen in einer Nachricht. Der SIO schließt daher einen SDLC-Rahmen automatisch ab, wenn der Sendedatenpuffer und das Ausgangsschieberegister keine Bits mehr zum Senden haben. Zuerst werden die zwei CRC-Bytes ausgesendet, gefolgt von einem oder mehreren Flags. Dieses Verfahren erlaubt eine sehr schnelle Übertragung unter DMA- oder CPU-Block-I/O-Steuerung, wobei es nicht notwendig ist, daß die CPU schnell auf das Ende der Datenübertragung reagiert.
Die Arbeitsweise des SIO in der Underrun-Situation hängt von dem Zustand des Transmit-Underrun/EOM-Befehls ab. Nach einem Reset befindet sich das Transmit-Underrun/EOM-Statusbit im gesetzten Zustand und verhindert das Einfügen von CRC-Zeichen in der Zeit, in der keine Daten zu senden sind. Folglich werden Flagzeichen gesendet. Der SIO beginnt, den Rahmen zu senden, sobald die Daten in den Senderpuffer eingeschrieben worden sind. In der Zeit zwischen dem Einschreiben des ersten Datenbytes und dem Ende der Nachricht muß der Reset-Transmit-Underrun/EOM-Befehl ausgegeben werden. Folglich ist das Transmit-Underrun/EOM-Statusbit am Ende der Datenübertragung (wenn Underrun auftritt) im rückgesetzten Zustand, wo die CRC-Zeichen automatisch gesendet werden. Das Senden von CRC setzt wieder das Transmit Underrun/EOM-Status-Bit. Obgleich es keine Einschränkung gibt, wann das Transmit-Underrun/EOM-Bit während einer Datenübertragung zurückgesetzt werden kann, geschieht es gewöhnlich, nachdem das erste Datenzeichen (Sekundäradresse) zum SIO gesendet wird. Das Zurücksetzen dieses Bits erlaubt das Senden von CRC und Flags, wenn keine Daten vorhanden sind. Dies gibt der CPU zusätzliche Zeit für das Erkennen des Fehlers und der Reaktion mit einem Abort-Befehl. Bei einem frühzeitigen Rücksetzen in der Datenübertragung hat die CPU maximal Zeit, auf eine unbeabsichtigte Transmit-Underrun-Situation zu reagieren.

**Tabelle 7: Inhalt der Schreibregister 3, 4 und 5 in den SDLC-Betriebsarten**

| Register | BIT7 | BIT6 | BIT5 | BIT4 | BIT3 | BIT2 | BIT1 | BITO |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **WR3** | 00=Rx5 Bits/Zeichen | 10=Rx6 Bits/Zeichen | Auto | Beginne | RxCRC- | Adreß | 0 | Empfänger- |
| | 01=Rx7 Bits/Zeichen | 11=Rx8 Bits/Zeichen | Enables | Such- | Freigabe | Suchbe- | | Freigabe |
| | | | | betrieb | | triebsart | | |
| **WR4** | 0 | 0 | 1 (wählt SDLC-Betriebsart aus) | 0 | 0 | 0 | 0 | 0 |
| **WR5** | DTR | 00=Tx5 Bits (oder weniger)/Zeich. | 10=Tx6 Bits/Zeich. | 0 | Sender- | 0 | RTS | Sender- |
| | | 01=Tx7 Bits/Zeich. | 11=Tx8 Bits/Zeich. | | Freigabe | (SDLC-CRC) | | CRC-Freigabe |

Ist der Extern/Status-Interrupt freigegeben, wird während des Sendens von CRC das Transmit Underrun/EOM-Bit gesetzt und das Transmit-Buffer-Empty-Bit wird rückgesetzt, um anzuzeigen, daß das Senderegister mit CRC-Daten gefüllt ist. Wurde CRC vollständig gesendet, dann wird das Transmit-Buffer-Empty-Status-Bit gesetzt und ein Interrupt erzeugt, um der CPU anzuzeigen, daß eine andere Datenübertragung beginnen kann. Dieser Interrupt erfolgt, wenn CRC gesendet und das Flag geladen wurde. Wenn keine Daten mehr zu senden sind, kann das Programm die Übertragung durch Rücksetzen von RTS beenden und den Sender sperren.
In der SDLC-Betriebsart ist es üblich, das Transmit-Underrun/EOM-Statusbit sofort zurückzusetzen, nachdem das erste Zeichen zum SIO gesendet worden ist. Wenn ein Transmit-Underrun erkannt wird, garantiert dies, daß die folgende Übertragungszeit mit CRC-Zeichen gefüllt wird, um der CPU genügend Zeit zu geben, Abort-Befehl zu senden, falls dies notwendig ist. Dies verhindert, daß die Flags vorzeitig ausgegeben werden und schaltet die Möglichkeit aus, daß der Empfänger den Rahmen als gültige Daten akzeptiert. Die Situation kann auftreten, weil es möglich ist, daß am Empfangsende das Datenmuster, welches der automatischen Flageinfügung unmittelbar vorausgeht, auf die CRC-Kontrolle paßt und ein falsches CRC-Kontrollergebnis ergibt. Der Extern/Status-Interrupt wird immer dann hervorgerufen, wenn das Transmit-Underrun/EOM-Bit gesetzt wird, weil eine Transmit-Underrun-Bedingung existiert.
Die Transmit-Underrun-Logik liefert einen zusätzlichen Schutz gegen die vorzeitige Flag-Einfügung, wenn durch die CPU-Interrupt-Service-Routine eine entsprechende Antwort zur SIO gegeben wird. Das folgende Beispiel erläutert dieses Problem:
- Der SIO erzeugt einen Interrupt mit dem Setzen des Transmit-Buffer-Empty-Statusbits
- Die CPU antwortet nicht rechtzeitig und ruft eine Transmit-Underrun-Bedingung hervor.
- Der SIO beginnt CRC-Zeichen (zwei Bytes) zu setzen.
- Die CPU befriedigt schließlich den Transmit-Buffer-Empty-Interrupt mit einem Datenzeichen, welches den gesendeten CRC-Zeichen folgt.
- Der SIO erzeugt einen Extern/Status-Interrupt mit dem Setzen des Transmit-Underrun/EOM-Statusbits.
- Die CPU erkennt den Transmit Underrun/EOM-Status und entscheidet aufgrund ihres internen Programmstatus, daß der Interrupt nicht für das "Ende der Datenübertragung" erzeugt wurde.
- Die CPU gibt sofort den "Sende-Abort-Befehl"(WR0) zur SIO.
- Der SIO sendet die Abort-Folge, dadurch gehen alle Daten (CRC, Daten oder Flags), die gesendet werden, verloren.

Dieser Ablauf zeigt, daß die CPU einen Schutz von minimal 32 und maximal 80 Sendertaktzyklen besitzt.

**SDLC-CRC-Erzeugung**
Der CRC-Generator muß am Beginn jedes Rahmens, bevor die CRC-Berechnung beginnen kann, auf alles Einsen zurückgesetzt werden. Die CRC-Berechnung beginnt, wenn das Programm das Adressenfeld (acht Bits) zur SIO sendet. Obwohl der SIO automatisch nach der Senderfreigabe ein Flagzeichen sendet, ist es günstig zu Beginn der Datenübertragung ein paar Flags mehr auszugeben, um die Zeichensynchronisation am Empfangsende zu sichern. Dies kann durch eine externe Zeitsteuerung nach der Freigabe des Senders und vor dem Laden des ersten Zeichens erfolgen. Die Sender-CRC-Freigabe (WR5, $D_0$) sollte vor dem Aussenden des Adreßfeldes erfolgen. In der SDLC-Betriebsart werden alle Zeichen zwischen dem Anfangs- und Endflag in die CRC-Berechnung einbezogen und das erzeugte CRC wird invertiert ausgesendet.

**Abschluß des Sendevorgangs**
Erfolgt die Sperrung des Senders während der Ausgabe eines Zeichens, dann wird dieses Zeichen in der üblichen Form gesendet, aber es folgt ein "High" (marking line) und nicht CRC oder Flag-Zeichen. Im Puffer befindliche Zeichen bleiben dort, wenn der Sender gesperrt wird, jedoch wird eine programmierte Abort-Folge sofort wirksam, sobald sie in das Steuerregister eingeschrieben wird. Zeichen, die gerade gesendet werden, gehen verloren. Im Fall des CRC wird die 16-Bit Übertragung abgeschlossen, wenn der Sender gesperrt ist, aber anstelle von CRC werden Flags gesendet.
In allen Betriebsarten werden die Zeichen mit den niedrigststelligen Bits zuerst gesendet. Das erfordert ein rechtsbündiges Ausrichten der zu übertragenden Daten, wenn die Wortlänge weniger als 8 Bits beträgt. Bei einer Wortlänge von fünf oder weniger Bits muß ein Sonderverfahren angewendet werden (siehe Abschnitt Programmierung). Da die Anzahl der Bits/Zeichen fliegend geändert werden kann, können die Daten mit jeder Anzahl von Bits aufgefüllt werden. Der SIO kann in Verbindung mit dem Empfänger-Residuen-Kode eine Nachricht empfangen, die ein variables I-Feld hat und sie exakt zurücksenden ohne vorherige Information über die Zeichenstruktur des I-Feldes. Eine Veränderung in der Anzahl der Bits hat keinen Einfluß auf das Zeichen, das gerade ausgegeben wird. Die Zeichen werden mit der Anzahl der Bits gesendet, die in der Zeit programmiert werden, in der das Zeichen vom Senderpuffer in den Sender geladen wird. Wenn der Extern/Status-Interrupt freigegeben ist, dann rufen solche Senderbedingungen wie "Beginn CRC-Zeichen zu senden", "Beginne Flagzeichen zu senden" und CTS-Zustandsänderung einen einheitlichen Vektor hervor, wenn der statusabhängige Vektor gesetzt ist. Alle Interrupts können gesperrt werden für Operationen im Pollingbetrieb. Tabelle 8 zeigt die typischen Programmschritte für eine Halb-Duplex-SDLC-Sendebetriebsart.

**Tabelle 8: SDLC-Sendebetriebsart**

| Funktion | Typische Programmschritte | Bemerkungen |
| :--- | :--- | :--- |
| **Initialisierung** | **Register: geladene Information:** | |
| | WR0: Kanal Reset | Reset SIO |
| | WR0: Zeiger 2 | |
| | WR2: Interruptvektor | nur Kanal B |
| | WR0: Zeiger 3 | |
| | WR3: Auto Enables | Sender gibt nur Daten aus, nachdem $\overline{\text{CTS}}$ erkannt wurde. |
| | WR0: Zeiger 4, Reset Extern/Status-Interrupts | |
| | WR4: Paritätsinformation, SDLC-Betriebsart, x1 Takt-Betriebsart | Senderparameter ausgegeben |
| | WR0: Zeiger 1, Reset Extern/Status-Interrupts | |
| | WR1: Extern-Interrupt-Freigabe, statusabh. Vektor, Sender-Interrupt-Freigabe oder Wait/Ready-Betriebsart-Freigabe | Die Extern-Interrupt-Betriebsart überwacht den Status der $\overline{\text{CTS}}$ und $\overline{\text{DCD}}$-Eingänge sowie den Status des Tx-Underrun/EOM-Speichers. Sendeinterrupts unterbrechen, wenn den Sendepuffer leer wird; die Wait/Ready-Betriebsart kann zur Datenübertragung auf DMA- oder Blocktransferbasis genutzt werden. Der erste Interrupt erscheint, wenn $\overline{\text{CTS}}$ aktiv wird, zu diesem Zeitpunkt werden die Flags durch die SIO gesendet. Das erste Datenbyte (Adreßfeld) kann nach diesem Interrupt in den SIO geladen werden. Flags können nicht als Daten zum SIO gesendet werden. Der statusabhängige Vektor wird nur in Kanal B genutzt. |
| | WR0: Zeiger 5 | |
| | WR5: Sender-CRC-Freigabe, Sendeanforderung, SDLC-CRC, Senderfreigabe, Senderwortlänge, Terminalbereitschaft | Die SDLC-CRC-Betriebsart muß definiert werden, bevor der Sender CRC-Generator initialisiert wird. |
| | WR0: Reset Sender-CRC-Generator | CRC-Generator wird auf alles "1" initialisiert. |
| **Wartezustand** | Ausführung des Haltebefehls oder anderer Programmschritte | Es wird auf einen Interrupt oder den Wait/Ready-Ausgang gewartet, um Daten zu übertragen. |
| **Datentransfer und Statusüberwachung** | **Wenn ein Interrupt (Wait/Ready) auftritt, führt die CPU folgendes aus:** | Die Flags werden durch die SIO gesendet, sobald die Senderfreigabe erfolgt und $\overline{\text{CTS}}$ aktiv wird. Die $\overline{\text{CTS}}$-Statusänderung ist der erste Interrupt, der auftritt und ihm folgt ein Transmit-Buffer-Empty-Interrupt für nachfolgende Datenübertragungen. |
| | - Verändert die Senderwortlänge (wenn notwendig) | |
| | - Überträgt ein Datenbyte von der CPU (Speicher) zur SIO | |
| | - Reset Tx Underrun/EOM-Latch (WR0) | |
| | **Wenn das letzte Zeichen des I-Feldes gesendet wurde, führt die SIO folgendes aus:** | Die Wortlänge kann laufend für eine variable I-Feldlänge verändert werden. Das Datenbyte kann Adreß-, Steuer- oder I-Feld-Information (niemals ein Flag) enthalten. In der Praxis ist es üblich, den Tx-Underrun/EOM-Speicherplatz am Anfang der Nachricht zurückzustellen, um eine falsche Rahmenende-Feststellung am Empfangsende zu vermeiden. Wenn ein Underrun auftritt, ist damit gesichert, daß CRC gesendet wird und ein Underrun-Interrupt (Tx Underrun/EOM-Speicher wird aktiv) auftritt. Es ist zu beachten, daß der Befehl "Send Abort" als Antwort auf jeden Interrupt zur SIO ausgegeben werden kann, um die Übertragung abzubrechen. |
| | - Sendet CRC | |
| | - Sendet das Endeflag | |
| | - Unterbricht die CPU mit einem Buffer-Empty-Status | |
| | **Die CPU führt folgendes aus:** | |
| | - Aussenden des Reset-Tx-Interrupt-Pending-Befehls zur SIO | |
| | - Aktualisierung des NS-Zählers | |
| | - Wiederholung des Prozesses für die nächste Übertragung usw. | |
| | **Wenn der Vektor einen Fehler anzeigt, führt die CPU folgendes aus:** | |
| | - Sendet Abort-Folge | |
| | - Ausführung einer Fehlerroutine | |
| | - Aktualisierung der Parameter, Betriebsarten usw. | |
| | - Rückkehr vom Interrupt | |
| **Ende der Datenübertragung** | Neubestimmung der Interruptbetriebsarten, Aktualisierung der Modem-Steuersignale, Sperrung der Sendebetriebsart | |

**4.4.2. SDLC-Empfang**

**Initialisierung**
Die SDLC-Empfangs-Betriebsart wird durch das System mit den folgenden Parametern initialisiert: SDLC-Betriebsart, x1 Takt-Betriebsart, SDLC-Polynom, Empfänger-Wortlänge, usw.. Das Flag muß ebenfalls in WR7 und das sekundäre Adreßfeld in WR6 geladen werden. Der Empfänger wird nur freigegeben, nachdem alle Empfangsparameter gesendet worden sind. Nachdem das geschehen ist, befindet sich der Empfänger in der Suchphase und bleibt in dieser Phase bis das erste Flag empfangen wird. Während des SDLC-Betriebs kommt der Empfänger niemals erneut in die Such-Phase (Hunt-Phase), sofern das nicht durch das Programm angewiesen wurde. Die WR4-Parameter müssen vor den WR1, WR3, WR5, WR6 und WR7-Parametern ausgegeben werden.
Unter Programmsteuerung kann der Empfänger in die Adreß-Such-Betriebsart gelangen. Wenn das Adreß-Such-Bit (WR3, $D_2$) gesetzt ist, wird ein Zeichen, welches dem Flag folgt (erstes Zeichen, welches kein Flag ist), mit der programmierten Adresse in WR6 und der globalen Adresse (1111 1111) verglichen. Wenn das SDLC-Rahmen-Adressenfeld zu irgendeiner der Adressen paßt, beginnt die Datenübertragung. Da der SIO nur ein Adressenzeichen auf Übereinstimmung prüfen kann, muß die erweiterte Adressenfelderkennung von der CPU vorgenommen werden. In diesem Fall überträgt der SIO einfach die zusätzlichen Adressenbytes zur CPU, als wären sie Datenzeichen. Wenn die CPU feststellt, daß der Rahmen nicht das richtige Adressenfeld hat, kann sie das Such-Bit (Hunt) setzen. Der SIO beendet den Empfang und sucht nach einer neuen Nachricht, die von einem Flag angeführt wird. Da das Steuerfeld des Rahmens gegenüber dem SIO transparent ist, wird es an die CPU als Datenzeichen übertragen.

**Datenübertragung und Statusüberwachung**
Nach dem Erhalt eines gültigen Flags werden die empfangenen Zeichen zum Empfangsdaten-FIFO übertragen. Die folgenden vier Interrupt-Betriebsarten stehen für die Übertragung dieser Daten und ihres entsprechenden Statusses zur Verfügung.

- **Keine Interrupts freigegeben:** Diese Betriebsart wird für reine Polling-Operationen oder für "off-line" -Bedingungen verwendet.
- **Interrupt nur beim ersten Zeichen:** Diese Betriebsart wird normalerweise zum Start einer Software-Polling-Schleife benutzt oder zu Beginn eines Blocktransfers, der Wait/Ready nutzt, um die CPU oder DMA auf die ankommende Datenrate zu synchronisieren. In dieser Betriebsart unterbricht der SIO nur beim ersten Zeichen und später entstehen nur noch Interrupts, wenn eine spezielle Empfangsbedingung erkannt wurde. Die Betriebsart wird erneut initialisiert mit dem Enable-Interrupt-On-Next-Receive-Character-Befehl. Das erste empfangene Zeichen, welches nach diesem Befehl ausgesandt wurde, ruft einen Interrupt hervor. Wenn der Extern/Status-Interrupt freigegeben ist, kann dieser jederzeit unterbrechen, wenn der $\overline{\text{DCD}}$-Eingang den Zustand ändert. Spezielle Empfangsbedingungen wie "das Ende des Rahmens" und "Empfängerüberlauf" rufen auch Interrupts hervor. Der "Rahmen-Ende"-Interrupt kann benutzt werden, um die Block-Transfer-Betriebsart zu beenden.
- **Interrupt bei jedem Zeichen:** Es wird ein Interrupt erzeugt, immer wenn der Empfangs-FIFO ein Datenwort enthält. Fehler und spezielle Empfangsbedingungen erzeugen einen speziellen Vektor, wenn der statusabhängige Vektor ausgewählt wurde.
- **Interrupts bei spezieller Empfangsbedingung:** Der Interrupt bei spezieller Empfangsbedingung als solcher, ist keine gesonderte Interrupt-Betriebsart. Ehe die spezielle Empfangsbedingung einen Interrupt hervorrufen kann, muß entweder ein Interrupt beim ersten Empfangszeichen oder ein Interrupt bei jedem Empfangszeichen ausgewählt worden sein. Der Interrupt bei spezieller Empfangsbedingung wird durch einen Empfängerüberlauf oder durch das Erkennen des Rahmenendes hervorgerufen. Da das Empfängerüberlauf-Statusbit gespeichert wird, spiegelt der gelesene Fehlerstatus einen Fehler im laufenden Wort im Empfangspuffer wieder, zusätzlich zu den Fehlern, die seit dem letzten Error-Reset-Befehl empfangen wurden. Das Empfänger-Überlauf-Status-Bit kann nur durch den Error-Reset-Befehl zurückgesetzt werden. Das Rahmenende-Status-Bit zeigt an, daß ein gültiges Endflag empfangen wurde und daß der CRC-Fehler und der Residuen-Kode auch gültig sind.

Die Zeichenlänge kann laufend verändert werden. Wenn die Adreß- und Steuerbytes als 8-Bit-Zeichen verarbeitet werden, kann der Empfänger während der Zeit, in der das erste Informations-Zeichen empfangen wird, auf eine kürzere Zeichenlänge umschalten. Diese Änderung muß so schnell erfolgen, daß sie wirksam wird, ehe die Anzahl der Bits, die für die Zeichenlänge festgelegt wurde, empfangen worden ist. Wenn z. B. die Änderung vom 8-Bit Steuerfeld zu einem 7-Bit-Informationsfeld erfolgt, muß die Änderung vorgenommen werden, ehe die ersten sieben Bits des I-Feldes empfangen wurden.

**SDLC-Empfangs-CRC-Prüfung**
Die Steuerung des Empfangs-CRC-Prüfers erfolgt automatisch. Er wird durch das führende Flag zurückgesetzt und CRC wird bis zum Endflag berechnet. Das Byte, bei dem das Rahmenende-Bit gesetzt ist, ist das Byte, welches das Ergebnis der CRC-Kontrolle enthält. Wenn das CRC/Rahmen-Fehler-Bit nicht gesetzt ist, liegt eine gültige Nachricht vor. Eine spezielle Prüffolge wird für die SDLC-Prüfung benutzt, da das gesendete CRC-Ergebnis invertiert ist. Die Endprüfung muß 0001110100001111 ergeben. Die 2-Byte CRC-Prüfzeichen müssen durch die CPU gelesen und abgelegt werden, da der SIO, während er sie für die CRC-Prüfung benutzt, sie als gewöhnliche Daten behandelt.

**Abschluß des SDLC-Empfanges**
Es wird ein spezieller Vektor erzeugt, wenn das Endflag empfangen wurde. Dies signalisiert, daß das Byte mit dem gesetzten Rahmenende-Bit empfangen worden ist. Zusätzlich zu den Ergebnissen der CRC-Prüfung enthält RR1 drei Bits des Residuen-Kodes, die zu diesem Zeitpunkt gültig sind. Für solche Fälle, in denen die Anzahl der I-Feld-Bits kein ganzzahliges Vielfaches der Zeichenlänge ist, zeigen diese Bits die Grenze zwischen den CRC-Prüf-Bits und den I-Feld-Bits an. Eine detaillierte Beschreibung dieser Bits findet man im Abschnitt "SIO-Programmierung".
Jeder Rahmen kann vorzeitig durch eine Abortfolge abgebrochen werden. Aborts werden festgestellt, wenn sieben oder mehr Einsen auftreten und einen Extern/Status-Interrupt verursachen (sofern gewählt), wobei das Break/Abort-Bit in RR0 gesetzt wird. Nachdem der Reset-Befehl für den Extern/Status-Interrupt ausgegeben worden ist, tritt ein zweiter Interrupt auf, wenn das Zustandekommen der aufeinanderfolgenden Einsen geklärt worden ist. Dieser kann verwendet werden, um zwischen den Abort- und Idle-line-Bedingungen zu unterscheiden.
Im Gegensatz zur synchronen Betriebsart hat die CRC-Berechnung in SDLC keine 8-Bit-Verzögerung, da alle Zeichen in der CRC-Berechnung enthalten sind. Wenn das zweite CRC-Zeichen in den Empfangspuffer geladen worden ist, ist die CRC-Berechnung abgeschlossen.

**Tabelle 9: SDLC-Empfangsbetriebsart**

| Funktion | Typische Programmschritte | Bemerkungen |
| :--- | :--- | :--- |
| **Initialisierung** | **Register: geladene Information:** | |
| | WR0: Kanal Reset | Reset SIO |
| | WR0: Zeiger 2 | |
| | WR2: Interruptvektor | nur Kanal B |
| | WR0: Zeiger 4 | |
| | WR4: Paritätsinformation, Sync-Betriebsart, SDLC-Betriebsart, x1 Taktrate, | |
| | WR0: Zeiger 5, Reset Extern/Status-Interrupts | |
| | WR5: SDLC-CRC, Terminalbereitschaft | |
| | WR0: Zeiger 3 | |
| | WR3: Empfänger-CRC-Freigabe, Beginn Suchphase, Auto Enables, Empfänger-Zeichenlänge, Adreßsuch-Betriebsart | Auto Enables gibt den Empfänger frei, um Daten zu empfangen nur nachdem $\overline{\text{DCD}}$ aktiv wird. Die Adreßsuch-Betriebsart gibt den SIO frei, um die Datenadresse mit der programmierten Adresse oder der globalen Adresse zu vergleichen. |
| | WR0: Zeiger 6 | |
| | WR6: Sekundäres Adressenfeld | Diese Adresse wird mit der Datenadresse in einer SDLC-Abfrageoperation verglichen. |
| | WR0: Zeiger 7 | |
| | WR7: SDLC-Flag 01111110 | Dieses Flag kennzeichnet den Start und das Ende des Rahmens in einer SDLC-Operation. |
| | WR0: Zeiger 1, Reset Extern/Status-Interrupts | |
| | WR1: Statusabhängiger Vektor, Extern-Interrupt-Freigabe, Empfangsinterrupt nur beim ersten Zeichen | In dieser Interrupt-Betriebsart wird nur das Adressenfeld (nur 1 Zeichen) zur CPU übertragen. Alle nachfolgenden Felder (Steuerinformation, usw.) werden auf DMA-Basis übertragen. Der statusabhängige Vektor befindet sich nur in Kanal B. |
| | WR0: Zeiger 3, Freigabe des Interrupts beim nächsten Empfangszeichen | Dieser Befehl liefert eine einfache Rückschleife zum Anfang für die nächste Übertragung. |
| | WR3: Empfänger-Freigabe, Empfänger-CRC-Freigabe, Beginne Suchphase, Auto Enables, Empfänger-Zeichenlänge, Adreß-Such-Betriebsart | Es erfolgt erneut die Ausgabe von WR3, um den Empfänger freizugeben. |
| **Wartezustand** | Ausführung des Haltbefehls oder anderer Programmteile | Die SDLC-Empfangsbetriebsart ist voll initialisiert und der SIO wartet auf das Anfangsflag, gefolgt von einem Vergleichsadressenfeld, um die CPU zu unterbrechen. |
| **Datenübertragung und Statusüberwachung** | **Wenn ein Interrupt beim ersten Zeichen auftritt, führt die CPU folgendes aus:** | Während der Suchphase unterbricht der SIO die CPU, wenn die programmierte Adresse mit der Datenadresse übereinstimmt. Die CPU legt die DMA-Betriebsart fest und alle nachfolgenden Datenzeichen werden durch die DMA zum Speicher übertragen. |
| | - Übertragung des Datenbytes (Adreßbyte) zur CPU | |
| | - Erkennen und Setzen des zugeordneten Flags für das erweiterte Adressenfeld | |
| | - Aktualisierung der Zeiger und Parameter | |
| | - Freigabe des DMA-Steuergerätes | |
| | - Freigabe der Wait/Ready-Funktion im SIO | |
| | - Rückkehr vom Interrupt | |
| | **Wenn der Ready-Ausgang aktiv wird, führt die DMA folgendes aus:** | Während der DMA-Operation überwacht der SIO den $\overline{\text{DCD}}$-Eingang und die Abortfolge im Datenstrom, um die CPU mit einem externen Statusfehler zu unterbrechen. Der Interrupt bei spezieller Empfangsbedingung wird durch einen Empfängerüberlauffehler hervorgerufen. |
| | - Übertragung des Datenbytes zum Speicher | |
| | - Aktualisierung der Zeiger | |
| | **Bei Rahmenende erscheint ein Interrupt, wobei die CPU folgendes ausführt:** | Das Erkennen des Rahmenendes (Flag) ruft einen Interrupt hervor und sperrt die Wait/Ready-Funktion. Die Residuen-Kodes kennzeichnen die Bitstruktur der letzten zwei Bytes der Daten, die zum Speicher unter DMA-Kontrolle übertragen wurden. Der "Error Reset"-Befehl wird ausgegeben, um die spezielle Bedingung zu löschen. |
| | - Beendet DMA-Betriebsart (Sperrung Wait/Ready) | |
| | - Übertragung von RR1 zur CPU | |
| | - Überprüfung des CRC-Fehlerbit-Statusses und des Residuen-Kodes | |
| | **Wenn die Abort-Folge erkannt wurde, wird ein Interrupt hervorgerufen, wobei die CPU folgendes tut:** | Eine Abort-Folge wird erkannt, wenn sieben oder mehr Einsen im Datenstrom gefunden werden. |
| | - Übertragung von RR0 zur CPU | |
| | - Beendigung der DMA-Betriebsart | |
| | - Ausgabe des "Reset Extern Status-Interrupt"-Befehls zur SIO | |
| | - Eintritt in den Wartezustand | CPU wartet auf eine Abortfolge, um die Übertragung zu beenden. Die Beendigung löscht das Break/Abort-Statusbit und ruft einen Interrupt hervor. |
| | **Wenn ein Interrupt aufgrund einer zweiten Abortfolge auftritt, führt die CPU folgendes aus:** | Zu diesem Zeitpunkt geht das Programm weiter bis zur Beendigung dieser Datenübertragung. |
| | - Aussenden des "Reset-Extern-Status-Interrupt"-Befehls zur SIO | |
| **Abschluß der Datenübertragung** | Neubestimmung der Interruptbetriebsarten, Sync- und SDLC-Betriebsarten, Sperrung der Empfangsbetriebsart | |

**5. Programmierung des Schaltkreises U 856 D**

Zur Programmierung des SIO gibt das Systemprogramm zuerst eine Serie von Befehlen aus, welche die Hauptbetriebsart initialisieren und dann andere Befehle, welche die Bedingungen in der ausgewählten Betriebsart näher bestimmen. Zum Beispiel werden asynchrone Betriebsart, Zeichenlänge, Taktrate, Anzahl der Stoppbits, gerade oder ungerade Parität zuerst gesetzt, dann die Interrupt-Betriebsart und schließlich Empfänger- oder Sender-Freigabe. Die WR4-Parameter müssen vor allen anderen Parametern innerhalb der Initialisierungsroutine ausgegeben werden. Beide Kanäle enthalten Befehlsregister, welche über das Systemprogramm vor dem Betrieb programmiert werden. Erst nach Programmierung aller Befehlsregister (Schreibregister) ist der Inhalt der Statusregister (Leseregister) definiert. Durch den CPU-Adressenbus werden die entsprechenden Kanaleingänge (B/$\overline{\text{A}}$) bzw. Steuer/Daten-Eingänge (C/$\overline{\text{D}}$) vorgewählt.

| C/$\overline{\text{D}}$ | B/$\overline{\text{A}}$ | Funktion |
| :--- | :--- | :--- |
| 0 | 0 | Kanal A Daten |
| 0 | 1 | Kanal B Daten |
| 1 | 0 | Kanal A Befehle/Status |
| 1 | 1 | Kanal B Befehle/Status |

**5.1. Schreibregister**

Der SIO enthält acht Register (WR0-WR7) in jedem Kanal, die getrennt durch das Systemprogramm programmiert werden können, um die funktionelle Besonderheit des Kanals herzustellen. Mit Ausnahme von WR0 erfordert die Programmierung der Schreibregister zwei Bytes. Das erste Byte enthält drei Bits (D0-D2), die als Zeiger für das auszuwählende Register dienen; das zweite Byte ist das aktuelle Steuerwort, das in das Register eingeschrieben wird, um den SIO zu programmieren.
Es ist zu beachten, daß der Programmierer völlige Freiheit hat, nach der Auswahl des Registers, dieses zu lesen (z. B. Test eines Leseregisters) oder zu schreiben (Laden eines Schreibregisters für die Initialisierung). Bei der Software-Erstellung zur Initialisierung des SIO in einer modularen oder strukturierten Form, kann der Programmierer die leistungsfähigen Block-I/O-Befehle verwenden.
WR0 ist ein Spezialfall, bei dem alle Basis-Befehle ($CMD_0 - CMD_2$) mit einem einfachen Byte ausgegeben werden können. Ein Reset (intern oder extern) initialisiert die Zeigerbits D0-D2 auf WR0 zu weisen.
Die Grundbefehle ($CMD_0 - CMD_2$) und die CRC-Steuerbits ($CRC_0, CRC_1$) sind im ersten Byte jedes Schreibregisterzugriffs enthalten. Dies erlaubt eine maximale Flexibilität und Systemsteuerung. Jeder Kanal enthält die folgenden Steuerregister. Diese Register werden als Befehle (keine Daten) adressiert.

**Schreibregister 0**
WR0 ist ein Befehlsregister; es wird jedoch auch für den CRC-Reset-Kode und als Zeiger für die anderen Register benutzt. Wenn dieses Schreibregister erstmals programmiert wird, ist stets der Befehl CMD 1 zu programmieren ($D_5=0, D_4=0, D_3=1$).

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| CRC Reset Code 1 | CRC Reset Code 0 | CMD 2 | CMD 1 | CMD 0 | PTR 2 | PTR 1 | PTR 0 |

**Zeiger Bits (D0-D2)**
Die Bits D0-D2 sind Zeigerbits, die festlegen, in welches andere Schreibregister das nächste Byte eingeschrieben oder aus welchem Leseregister das nächste Byte ausgelesen wird. Das erste Byte, das in jedem Kanal nach einem Reset (entweder durch einen Reset-Befehl oder durch externe Reset-Eingabe) eingeschrieben wird, geht in WR0. Nach einem Schreiben oder Lesen zu irgendeinem Register (außer WR0), weist der Zeiger auf WR0.

**Befehlsbits (D3-D5)**
Die drei Bits D3-D5 werden genutzt, um die sieben Grundbefehle auszugeben.

| Befehl | CMD2 | CMD1 | CMD0 | Funktion |
| :---: | :---: | :---: | :---: | :--- |
| 0 | 0 | 0 | 0 | Null-Befehl (keine Wirkung) |
| 1 | 0 | 0 | 1 | Sende Abortfolge (SDLC-Betriebsart) |
| 2 | 0 | 1 | 0 | Reset Extern/Status-Interrupt |
| 3 | 0 | 1 | 1 | Kanal-Reset |
| 4 | 1 | 0 | 0 | Reset Interrupt beim nächsten Empfangszeichen |
| 5 | 1 | 0 | 1 | Reset Sender-Interrupt-Pending |
| 6 | 1 | 1 | 0 | Error-Reset |
| 7 | 1 | 1 | 1 | RETI (nur Kanal A) |

**Befehl 0 (Null)**
Der Null-Befehl hat keine Wirkung. Seine Verwendung besteht darin, den Zeiger für das folgende Byte zu setzen.

**Befehl 1 (Sende Abortfolge)**
Dieser Befehl wird nur im SDLC-Betrieb verwendet, um eine Folge von acht bis dreizehn Einsen zu erzeugen.

**Befehl 2 (Reset Extern/Status-Interrupt)**
Nach einem Extern/Status-Interrupt (z. B. eine Änderung auf einer Modemleitung oder eine Break-Bedingung) werden die Statusbits von RR0 gespeichert. Dieser Befehl gibt sie erneut frei und ermöglicht, daß erneut Interrupts auftreten. Durch das Zwischenspeichern der Statusbits werden auch kurze Impulse eingefangen, so daß die CPU genügend Zeit hat, die Änderung zu lesen.

**Befehl 3 (Kanal-Reset)**
Dieser Befehl führt die gleiche Funktion aus wie ein externes Reset, aber nur in einem Kanal. Ein Reset auf Kanal A setzt außerdem die Interrupt-Prioritätslogik zurück. Alle Steuerregister des entsprechenden Kanals müssen neu initialisiert werden. Ein CPU-Zugriff zum rückgesetzten Kanal darf frühestens 4 Systemtaktzyklen nach einem Reset erfolgen. Dies kann normalerweise die Zeit sein, die eine CPU für einen OP-Kode-Fetch benötigt.

**Befehl 4 (Interruptfreigabe beim nächsten Empfangszeichen)**
Wenn die Betriebsart "Interrupt beim ersten empfangenen Zeichen" ausgewählt wurde, reaktiviert dieser Befehl diese Betriebsart, nachdem eine vollständige Nachricht empfangen wurde, um den SIO für die nächste Übertragung vorzubereiten.

**Befehl 5 (Reset Sender-Interrupt-Pending)**
Bei freigegebenem Senderinterrupt unterbricht dieser, wenn der Senderpuffer leer wird. In den Fällen, wo keine Zeichen mehr gesendet werden (z. B. Ende der Datenübertragung), verhindert die Ausgabe dieses Befehls weitere Senderinterrupts bis das nächste Zeichen in den Senderpuffer geladen oder CRC vollständig gesendet wurde.

*KI generierte Interpretation der Abbildung: Bild 14 stellt grafisch das Bit-Mapping für das Schreibregister 0 (WR0) dar. Ein 8-Bit Block (D7-D0) ist oben gezeichnet. Von den Bits D2, D1, D0 führen Linien zu einer Tabelle, welche die Zielregister (Register 0 bis 7) definiert. Von den Bits D5, D4, D3 führen Linien zu einer Liste der acht Grundbefehle (Null, Sende Abort, Reset Ext/Status etc.). Von D7, D6 führen Linien zu den CRC-Reset-Befehlen (Null, Reset Empf. CRC, Reset Sender CRC, Reset Tx Underrun Latch).*

**Bild 14: Schreibregister - Bitfunktionen**

**Schreibregister 1 (Write-Register 1)**

*KI generierte Interpretation der Abbildung: Das Diagramm für WR1 zeigt die Steuerung der Interrupt- und Wait/Ready-Optionen.
- D0: Extern-Interrupt-Freigabe.
- D1: Sender-Interrupt-Freigabe.
- D2: Status Affects Vector (nur Kanal B).
- D4, D3: Kombinationslogik für Empfangs-Interrupts (Sperre, erstes Zeichen, jedes Zeichen mit/ohne Paritätseinfluss).
- D7, D6, D5: Steuerung der Wait/Ready-Funktion (Freigabe, Funktion Ready vs. Wait, Zuordnung zu Empfang oder Senden).*

**Schreibregister 2 (nur Kanal B) (Write-Register 2)**
Dieses Register speichert den 8-Bit Interrupt-Vektor ($V_0 - V_7$).

**Schreibregister 3 (Write-Register 3)**
Hier werden Empfängeroptionen festgelegt: Empfänger-Freigabe (D0), Sync-Ladesperre (D1), Adreß-Such-Betrieb (D2), Empfänger-CRC-Freigabe (D3), Hunt-Mode (D4), Auto-Enables (D5) und Empfänger-Wortlänge (D7, D6: 5, 6, 7 oder 8 Bits).

**Schreibregister 4 (Write-Register 4)**
Parameter für beide Richtungen: Paritäts-Freigabe (D0), Paritätstyp (D1), Stoppbits (D3, D2: Sync-Mode, 1, 1 1/2 oder 2 Bits), Synchron-Mode-Auswahl (D5, D4: Monosync, Bisync, SDLC, Extern) und Taktrate (D7, D6: x1, x16, x32, x64).

**Schreibregister 5 (Write-Register 5)**
Senderoptionen: Sender-CRC-Freigabe (D0), RTS (D1), CRC-Polynom (D2), Sender-Freigabe (D3), Sende-Break (D4), Sender-Wortlänge (D6, D5: 5 oder weniger, 6, 7, 8 Bits) und DTR (D7).

**Schreibregister 6 und 7**
Diese Register enthalten die Synchronisationszeichen (8 oder 16 Bit) oder im SDLC-Modus die sekundäre Adresse (WR6) und das Flag-Byte 01111110 (WR7).

**Schreibregister 1 (Fortsetzung)**

**Extern/Status Interrupt Freigabe ($D_0$)**
Die Extern/Status-Interrupt-Freigabe gestattet Interrupts als Ergebnis von Änderungen auf den $\overline{\text{DCD}}$, $\overline{\text{CTS}}$ oder $\overline{\text{SYNC}}$-Eingängen, sowie als Ergebnis einer Break/Abort-Erkennung und Beendigung oder am Anfang der CRC oder Sync-Zeichen-Übertragung, wenn das Sender Underrun/EOM latch gesetzt wird.

**Sender-Interrupt-Freigabe ($D_1$)**
Bei Leerwerden des Sendepuffers wird ein Interrupt ausgelöst.

**Statusabhängiger Vektor ($D_2$)**
Dieses Bit wirkt nur im Kanal B. Bei nicht gesetzten Bit wird der programmierte Interruptvektor unverändert an die CPU gegeben. Bei gesetztem Bit wird der Interruptvektor in Abhängigkeit der folgenden Bedingungen verändert:

| $V_3$ | $V_2$ | $V_1$ | Bedingung |
| :---: | :---: | :---: | :--- |
| 0 | 0 | 0 | Kanal B Senderpuffer leer |
| 0 | 0 | 1 | Kanal B Extern/Status-Veränderung |
| 0 | 1 | 0 | Kanal B Empfangszeichen vorhanden |
| 0 | 1 | 1 | Kanal B Spezielle Empfangsbedingung * |
| 1 | 0 | 0 | Kanal A Senderpuffer leer |
| 1 | 0 | 1 | Kanal A Extern/Status-Veränderung |
| 1 | 1 | 0 | Kanal A Empfangszeichen vorhanden |
| 1 | 1 | 1 | Kanal A Spezielle Empfangsbedingung * |

*Spezielle Empfangsbedingungen: Paritätsfehler, Rx Überlauffehler, Rahmenfehler, Rahmenende (SDLC)

**Empfangs-Interrupt-Mode 0 und 1 ($D_2$ und $D_4$)**
Diese beiden Bits spezifizieren die verschiedenen Empfangszeichen-Bedingungen. Im Empfangs-Interrupt-Mode 1, 2 und 3 kann eine spezielle Empfangs-Bedingung einen Interrupt hervorrufen und den Interruptvektor modifizieren.

| $D_4$ (Mode 1) | $D_3$ (Mode 0) | Nr. | Bedeutung |
| :---: | :---: | :---: | :--- |
| 0 | 0 | 0. | Empfangsinterrupts gesperrt |
| 0 | 1 | 1. | Empfangsinterrupt nur beim ersten Zeichen |
| 1 | 0 | 2. | Interrupt bei jedem Empfangszeichen - Paritätsfehler ist eine spezielle Empfangs-Bedingung |
| 1 | 1 | 3. | Interrupt bei jedem Empfangszeichen - Paritätsfehler ist keine spezielle Empfangsbedingung |

**Wait Ready Funktionsauswahl ($D_5-D_7$)**
Die Wait- und Ready-Funktionen werden durch die Steuerbits $D_5$, $D_6$ und $D_7$ festgelegt. Die Wait/Ready-Funktion wird freigegeben durch Setzen von Wait/Ready Enable (WR1, $D_7$) auf 1. Die Ready-Funktion wird durch Setzen von $D_6$ (Wait/Ready-Funktion) auf 1 ausgewählt. Bei gesetztem Bit ($D_6$) geht der Wait/Ready-Ausgang von High auf Low, wenn der SIO zur Datenübertragung bereit ist. Die Wait-Funktion wird durch Setzen von $D_6$ auf 0 ausgewählt. In diesem Fall ist der Wait/Ready-Ausgang im Open-Drain-Zustand. Im aktiven Zustand wird er Low. Sowohl die Wait- als auch die Ready-Funktion kann entweder im Sender- oder Empfängerbetrieb genutzt werden, aber nicht gleichzeitig. Wenn $D_5$ (Wait/Ready on Recive/Transmit) auf 1 gesetzt ist, entspricht die Wait/Ready-Funktion dem Zustand des Empfangspuffers (leer oder voll). Ist $D_5$ auf 0 gesetzt, entspricht die Wait/Ready-Funktion dem Zustand des Sendepuffers (leer oder voll).

**Zusammenfassung der Wait/Ready Kombinationen bei $D_7 = 1$:**

| Bedingung | $D_5 = 0$ (Senden) | $D_5 = 1$ (Empfang) |
| :--- | :--- | :--- |
| **$D_6 = 1$ (Ready)** | Ready ist High, wenn der Sendepuffer voll ist. | Ready ist High, wenn der Empfangspuffer leer ist. |
| | Ready ist Low, wenn der Sendepuffer leer ist. | Ready ist Low, wenn der Empfangspuffer voll ist. |
| **$D_6 = 0$ (Wait)** | Wait ist Low, wenn der Sendepuffer voll ist und ein SIO-Datenport ausgewählt wurde. | Wait ist Low, wenn der Empfangspuffer leer ist und ein SIO-Datenport ausgewählt wurde. |
| | Wait ist floatend, wenn der Sendepuffer leer ist. | Wait ist floatend, wenn der Empfangspuffer voll ist. |

Der High-Low-Übergang des Wait-Ausganges erscheint mit der Verzögerungszeit $t_{DIC}$ (WR) nach dem I/O-Request. Der Low-High-Übergang tritt mit der Verzögerung $t_{DHO}$ (WR) nach der fallenden Flanke von $\Phi$ auf. Der High-Low-Übergang des Ready-Ausganges erscheint mit der Verzögerung $t_{DLO}$ (WR) nach der steigenden Flanke von $\Phi$. Der Low-High-Übergang kommt dagegen mit der Verzögerung $t_{DIC}$ (WR) nachdem IORQ Low wird.
Die Ready-Funktion kann jederzeit auftreten, auch wenn der SIO nicht angesprochen wird. Wenn der Ready-Ausgang aktiv wird (Low), sendet das DMA-Steuergerät IORQ und die entsprechenden B/$\overline{\text{A}}$ und C/$\overline{\text{D}}$ Eingänge zur SIO, um Daten zu übertragen. Der Ready-Ausgang wird inaktiv, sobald IORQ und CS aktiv werden. Da die Ready-Funktion intern im SIO auftreten kann, egal ob er adressiert ist oder nicht, wird der Ready-Ausgang inaktiv, wenn eine Übertragung von CPU-Daten oder Befehlen erfolgt. Das verursacht keine Probleme, weil das DMA-Steuergerät nicht freigegeben ist, wenn die CPU sendet.
Die Wait-Funktion andererseits ist nur aktiv, wenn die CPU versucht, SIO-Daten zu lesen, die noch nicht empfangen worden sind, was häufig auftritt, wenn Blockübertragungsbefehle verwendet werden. Die Wait-Funktion kann auch aktiv werden (unter Programmsteuerung), wenn die CPU versucht, Daten zu schreiben, während der Senderpuffer noch voll ist. Die Tatsache, daß der Wait-Ausgang eines Kanals aktiv werden kann, wenn der andere Kanal adressiert wird, beeinflußt nicht die Softwareschleifen oder Blockbewegungsbefehle.

**5.2. Leseregister**

Der SIO enthält drei Register RR0-RR2 (Bild 15), die gelesen werden können, um die Statusinformation für jeden Kanal (ausgenommen für RR2 - nur Kanal B) zu erhalten. Die Statusinformation umfaßt Fehlerbedingungen, Interrupt-Vektor und Standard-Kommunikations-Interface-Signale. Um den Inhalt eines ausgewählten Leseregisters außer RR0 zu lesen, muß das Systemprogramm zuerst das Zeigerbyte für WR0 in genau derselben Art und Weise wie bei einer Schreibregisteroperation schreiben. Dann kann der Inhalt des adressierten Leseregisters durch einen Eingabebefehl der CPU gelesen werden.

**Leseregister 0**
Dieses Register enthält den Status des Empfänger- und Senderpuffers, die $\overline{\text{DCD}}-$, $\overline{\text{CTS}}-$ und $\overline{\text{SYNC}}$-Eingänge, den Transmit-Underrun/EOM-Latch und den Break/Abort-Latch.

*KI generierte Interpretation der Abbildung: Bild 15 zeigt das Bit-Layout von Leseregister 0 (RR0).
- D0: Zeichen im Empfängerpuffer (gesetzt bei mindestens einem Zeichen).
- D1: Interrupt Pending (nur Kanal A, zeigt an ob irgendein Interrupt im SIO ansteht).
- D2: Sender-Puffer-leer (gesetzt wenn Sendepuffer bereit für neue Daten).
- D3: DCD (Status des Trägererkennungs-Eingangs).
- D4: Sync/Hunt (Status des Sync-Eingangs bzw. Suchbetriebs).
- D5: CTS (Status des Sendebereitschafts-Eingangs).
- D6: Tx Underrun/EOM (Sende-Ende oder Underrun Status).
- D7: Break/Abort (Erkennung einer Unterbrechung oder eines Abruchs im Datenstrom).
Die Bits D3 bis D7 sind mit einer Klammer markiert, die besagt, dass diese für den Extern/Status-Interrupt-Betrieb benutzt werden.*

**Bild 15 Leseregister - Bitfunktionen**

**Zeichen im Empfängerpuffer ($D_0$)**
Dieses Bit wird gesetzt, wenn mindestens ein Zeichen im Empfangspuffer ist; es wird rückgesetzt, wenn der Empfangs-FIFO vollständig leer ist.

**Interrupt Pending ($D_1$)**
Jede Interrupt-Bedingung im SIO veranlaßt, daß dieses Bit zu setzen ist; jedoch ist dieses Bit nur in Kanal A lesbar. Dieses Bit wird hauptsächlich dort verwendet, wo keine Vektorinterrupts möglich sind. Während der Interrupt-Service-Routine zeigt dieses Bit in diesen Anwendungen an, ob irgendwelche Interrupt-Bedingungen im SIO vorhanden sind. Dadurch müssen nicht alle Bits von RR0 sowohl in Kanal A als auch B analysiert werden. Bit $D_1$ wird rückgesetzt, wenn alle Interrupt-Bedingungen erfüllt sind. Dieses Bit ist immer 0 in Kanal B.

**Transmit Buffer Empty ($D_2$) (Senderpuffer leer)**
Dieses Bit wird immer gesetzt, wenn der Senderpuffer leer wird, ausgenommen, wenn ein CRC-Zeichen in einer synchronen oder SDLC-Betriebsart gesendet wird. Dieses Bit wird rückgesetzt, wenn ein Zeichen in den Sender-Puffer geladen wird. Dieses Bit ist nach einem Reset gesetzt.

**Data Carrier Detect ($D_3$) (Datenträgererkennung)**
Das DCD-Bit zeigt den Zustand des $\overline{\text{DCD}}$-Einganges während der letzten Änderung eines der fünf externen/Status-Bits ($\overline{\text{DCD}}$, $\overline{\text{CTS}}$, Sync/Hunt, Break/Abort oder Transmit Underrun/EOM). Jede Veränderung des $\overline{\text{DCD}}$-Einganges bewirkt, daß das DCD-Bit gespeichert wird und verursacht einen Extern/Status-Interrupt. Um den laufenden Zustand des DCD-Bits zu lesen, muß dieses Bit sofort nach einem Reset-Befehl für Extern/Status-Interrupt gelesen werden.

**Sync/Hunt ($D_4$)**
Da dieses Bit in der asynchronen, synchronen und SDLC-Betriebsart unterschiedlich benutzt wird, ist eine umfangreiche Erläuterung der Funktion notwendig. In den asynchronen Betriebsarten ist die Funktion dieses Bits dem DCD-Statusbit ähnlich, ausgenommen, daß Sync/Hunt den Zustand des Sync-Einganges anzeigt. Jeder High-Low-Übergang an dem Sync-Pin setzt dieses Bit und verursacht einen Extern/Status-Interrupt. Der Reset-Befehl für den Extern/Status-Interrupt wird ausgegeben, um den Interrupt zu löschen. Ein Low-High-Übergang löscht dieses Bit und setzt den Extern/Status-Interrupt. Wenn der Extern/Status-Interrupt durch die Zustandsänderung irgendeines anderen Eingangs oder einer Bedingung gesetzt wird, zeigt dieses Bit den invertierten Zustand des Sync-Pins zur Zeit der Änderung an. Dieses Bit muß sofort nach einem Reset-Befehl für einen Extern/Status-Interrupt gelesen werden.

Im externen - Sync - Betrieb wirkt das Sync-Hunt-Bit in einer der asynchronen Betriebsart ähnlichen Weise, ausgenommen, das Enter-Hunt-Mode-Steuerbit gibt die externe Synchronisationserkennungslogik frei. Wenn die Bits für Extern-Sync- und Enter-Hunt-Betrieb gesetzt sind (z. B. wenn der Empfänger nach einem Reset freigegeben ist), muß der Sync-Eingang durch die externe Logik High gehalten werden, bis die externe Zeichensynchronisation erreicht ist. Ein High am Sync-Eingang hält das Sync/Hunt-Status-Bit in der Reset-Bedingung. Wenn die externe Synchronisation erreicht ist, muß Sync mit der zweiten steigenden Flanke von $\overline{\text{RxC}}$ Low werden, und zwar nach der steigenden Flanke von $\overline{\text{RxC}}$, mit der das letzte Bit des Sync-Zeichens empfangen wurde. Mit anderen Worten, nachdem das Syncmuster ermittelt worden ist, muß die externe Logik zwei volle Empfänger-Takt-Zyklen warten, um den Sync-Eingang zu aktivieren. Wenn SYNC einmal Low geworden ist, wird es üblicherweise Low gehalten, bis die CPU die externe Logik informiert, daß die Synchronisation verlorengegangen ist oder eine neue Nachricht im Begriff ist zu starten. Der High-Low-Übergang des Sync-Einganges setzt das Sync/Hunt-Bit, welches seinerseits den Extern/Status-Interrupt setzt. Die CPU muß den Interrupt löschen, indem sie den Reset-Befehl für den Extern/Status-Interrupt ausgibt. Wenn der Sync-Eingang wieder High wird, wird ein anderer Extern/Status-Interrupt erzeugt, welcher auch gelöscht werden muß. Das Enter-Hunt-Mode-Steuerbit wird immer dann gesetzt, wenn die Zeichensynchronisation verlorengegangen ist oder das Ende der Nachricht festgestellt wurde. In diesem Fall wartet der SIO wieder auf einen High-Low-Übergang am Sync-Eingang.

In den Betriebsarten Monosync- und Bisync-Empfang wird das Sync/Hunt-Status-Bit anfangs durch das Enter-Mode-Bit auf 1 gesetzt. Das Sync/Hunt-Bit wird zurückgesetzt, wenn der SIO die Zeichensynchronisation herstellt. Der High-Low-Übergang des Sync/Hunt-Bits verursacht ein Extern/Status-Interrupt, der von der CPU gelöscht werden muß. Wenn die CPU das Ende der Nachricht erkennt oder erkennt, daß die Zeichensynchronisation verlorengegangen ist, setzt sie das Enter-Hunt-Mode-Steuer-Bit, das seinerseits das Sync/Hunt-bit auf 1 setzt. Der Low-High-Übergang des Sync/Hunt-Bits bewirkt einen Extern/Status-Interrupt, der auch durch den Reset-Befehl für den Extern/Status-Interrupt gelöscht werden muß. Es ist zu beachten, daß das Sync-Pin in dieser Betriebsart als Ausgang wirkt und jedesmal auf Low geht, wenn im Datenstrom ein Sync-Muster erkannt wurde.

In der SDLC-Betriebsart wird das Sync/Hunt-Bit anfangs durch das Enter-Hunt-Bit gesetzt oder wenn der Empfänger gesperrt ist. In jedem Fall wird es auf 0 zurückgesetzt, wenn das Anfangsflag des ersten Rahmens vom SIO erkannt wird. Ein Extern/Status-Interrupt wird ebenfalls erzeugt. Im Gegensatz zum Monosync- und Bisync-Betrieb braucht das Sync/Hunt-Bit, wenn es einmal im SDLC-Betrieb zurückgesetzt worden ist, nicht gesetzt zu werden, wenn das Ende der Nachricht erkannt worden ist. Der SIO erhält die Synchronisation automatisch aufrecht.

**Clear to Send ($D_5$)**
Dieses Bit ist dem DCD-Bit ähnlich, nur daß es den invertierten Zustand des $\overline{\text{CTS}}$-Pins anzeigt.

**Transmit Underrun/End of Message ($D_6$)**
Nach einem Reset (intern oder extern) ist dieses Bit in einem gesetzten Zustand. Der einzige Befehl, der dieses Bit zurücksetzen kann, ist der Reset-Befehl für das Transmit-Underrun/EOM Latch (WR0, $D_6$ und $D_7$). Wenn die Transmit-Underrun-Bedingung auftritt, wird dieses Bit gesetzt, sein Setzen verursacht einen Extern/Status-Interrupt, der durch Ausgabe des Reset-Extern/Status-Interrupt-Befehls (WR0) zurückgesetzt werden muß.

**Break/Abort ($D_7$)**
In der Betriebsart "Asynchroner Empfang" wird dieses Bit gesetzt, wenn im Datenstrom eine Breakfolge (Null-Zeichen plus Rahmenfehler) erkannt wird. Der freigegebene Extern/Status-Interrupt wird gesetzt, wenn ein Break erkannt wird. Die Interrupt-Service-Routine muß den Reset-Befehl für den Extern/Status-Interrupt (WR0, CMD2) zur Break-Erkennungs-Logik ausgeben, damit das Break-Folgen-Ende erkannt werden kann. Das Break/Abort-Bit wird zurückgesetzt, wenn das Ende der Breakfolge im ankommenden Datenstrom ermittelt worden ist. Das Ende der Breakfolge bewirkt auch, daß ein Extern/Status-Interrupt hervorgerufen wird.
Im SDLC-Empfangsbetrieb wird dieses Statusbit durch die Erkennung einer Abortfolge gesetzt (sieben oder mehr Einsen). Der Extern/Status-Interrupt wird auf die gleiche Weise behandelt wie im Fall eines Break. Das Break/Abort-Bit wird im synchronen Empfangsbetrieb nicht verwendet.

**Leseregister 1 (Read-Register 1)**

*KI generierte Interpretation der Abbildung: Bild „Leseregister 1“ zeigt die Detail-Bits für Fehler- und Residuen-Codes.
- D0: All Sent (Sender leer, nur in asynchronen Modes aktiv).
- D3, D2, D1: Residuen-Codes 0, 1 und 2. Diese drei Bits zeigen die Bit-Länge des Informationsfeldes im letzten empfangenen Byte an (wichtig für SDLC). Eine Tabelle daneben zeigt die Kodierung für eine 8-Bit Empfangslänge: von 1 0 0 (0 Bits im letzten, 3 im vorletzten Byte) bis 0 0 0 (8 Bits im letzten Byte).
- D4: Paritätsfehler.
- D5: Empfänger-Überlauffehler (Receive Overrun).
- D6: CRC/Rahmenfehler (zeigt an ob der berechnete CRC mit dem empfangenen übereinstimmt).
- D7: Rahmenende (End of Frame, SDLC spezifisch).*

**All Sent ($D_0$) (Sender leer)**
In den asynchronen Betriebsarten wird dieses Bit gesetzt, wenn alle Zeichen den Sender vollständig verlassen haben. Änderungen dieses Bits verursachen keine Interrupts. In der synchronen Betriebsart ist es immer gesetzt.

**Residuen Codes 0, 1 und 2 ($D_1-D_3$)**
In Fällen des SDLC-Empfang-Betriebes, in denen das I-Feld nicht ein ganzzahliges Vielfaches der Zeichenlänge ist, zeigen diese drei Bits die Länge des I-Feldes an. Diese Kodes sind nur für die Übertragung bedeutungsvoll, in der das Rahmenende-Bit gesetzt ist (SDLC). Für eine Empfangszeichenlänge von acht Bits pro Zeichen bedeuten die Kodes folgendes:

| Residuen Code 2 | Residuen Code 1 | Residuen Code 0 | I-Feld-Bits im letzten Byte | I-Feld-Bits im vorletzten Byte |
| :---: | :---: | :---: | :---: | :---: |
| 1 | 0 | 0 | 0 | 3 |
| 0 | 1 | 0 | 0 | 4 |
| 1 | 1 | 0 | 0 | 5 |
| 0 | 0 | 1 | 0 | 6 |
| 1 | 0 | 1 | 0 | 7 |
| 0 | 1 | 1 | 0 | 8 |
| 1 | 1 | 1 | 1 | 8 |
| 0 | 0 | 0 | 2 | 8 |

Die I-Feld-Bits sind in allen Fällen rechtsbündig. Wenn eine Empfangs-Zeichenlänge abweichend von acht Bit für das I-Feld benutzt wird, kann eine ähnliche Tafel für jede unterschiedliche Zeichenlänge aufgestellt werden. Wenn kein Reset vorhanden ist (d. h., die letzte Zeichengrenze stimmt mit der Grenze des I-Feldes und des CRC-Feldes überein) ergeben sich folgende Residuen-Kodes:

| Bits pro Zeichen | Residuen Code 2 | Residuen Code 1 | Residuen Code 0 |
| :--- | :---: | :---: | :---: |
| 8 Bits pro Zeichen | 0 | 1 | 1 |
| 7 Bits pro Zeichen | 0 | 0 | 0 |
| 6 Bits pro Zeichen | 0 | 1 | 0 |
| 5 Bits pro Zeichen | 0 | 0 | 1 |

**Paritäts-Fehler ($D_4$)**
Bei freigegebener Parität wird dieses Bit für die Zeichen gesetzt, deren Parität nicht mit der programmierten (gerade/ungerade) übereinstimmt. Das Bit wird gespeichert, so daß wenn ein Fehler auftritt, es gesetzt bleibt, bis der Fehler-Rücksetz-Befehl (WR0) gegeben wird.

**Empfänger-Überlauffehler ($D_5$)**
Dieses Bit zeigt an, daß mehr als drei Zeichen empfangen wurden, ohne daß sie von der CPU gelesen worden sind. Nur das Zeichen, welches überschrieben worden ist, wird als fehlerhaft gekennzeichnet, aber wenn dieses Zeichen gelesen wird, bleibt die Fehlerbedingung gespeichert, bis eine Rücksetzung durch den Error-Reset-Befehl erfolgt. Wenn der statusabhängige Vektor freigegeben ist, unterbricht das Zeichen, das übergelaufen ist, mit einem speziellen Empfangsbedingungs-Vektor.

**CRC/Rahmenfehler ($D_6$)**
Wenn ein Rahmenfehler auftritt (asynchrone Betriebsart), wird dieses Bit für das Empfangszeichen gesetzt (aber nicht gespeichert), in welchem der Rahmenfehler aufgetreten ist. In den synchronen und SDLC-Betriebsarten zeigt dieses Bit das Ergebnis des Vergleiches zwischen CRC-Prüfer und dazugehörigem Prüfwert an. Dieses Bit wird durch Ausgabe eines Error-Reset-Befehles zurückgesetzt. Das Bit wird nicht gespeichert, so enthält es immer den aktuellen Wert, wenn das nächste Zeichen empfangen wird.

**Rahmenende ($D_7$)**
Dieses Bit wird nur im SDLC-Betrieb verwendet und zeigt an, daß ein gültiges Endflag empfangen worden ist, und daß CRC-Fehler und Residuen-Code gültig sind.

**Leseregister 2 (Read Register 2) (nur Kanal B)**
Dieses Register enthält den Interrupt-Vektor, der in WR2 eingeschrieben ist, wenn das statusabhängige Vektor-Steuerbit nicht gesetzt worden ist. Wenn das Steuerbit gesetzt ist, enthält es den modifizierten Vektor.

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| $V_7$ | $V_6$ | $V_5$ | $V_4$ | $V_3$ | $V_2$ | $V_1$ | $V_0$ |

$V_1, V_2, V_3$ sind variabel, wenn statusabhängiger Vektor freigegeben ist.

**6. Technische Daten**

**6.1. Elektrische Kennwerte**

**Grenzwerte**
bei $\vartheta_a = 0$ bis 70 °C, alle Spannungen bezogen auf $U_{SS} = 0\ V$

| Kenngröße | Einheit | Kleinstwert | Größtwert | Bemerkung |
| :--- | :---: | :---: | :---: | :--- |
| Betriebsspannung $U_{CC}$ | V | -0,5 | 7 | |
| Eingangsspannung $U_I$ | V | -0,5 | 7 | |
| Betriebstemperaturbereich $\vartheta_a$ | °C | 0 | bis 70 | |
| Lagerungstemperaturbereich $\vartheta_{stg}$ | °C | -55 | bis 125 | |
| Verlustleistung P | W | - | 1,1 | bei $\vartheta_a = 25\ ^\circ C$ |

**Statische Kennwerte**
bei $\vartheta_a = 0$ bis 70 °C

| Kenngröße | Kurzzeichen | Einheit | Kleinstwert | Größtwert | Bemerkung |
| :--- | :---: | :---: | :---: | :---: | :--- |
| Betriebsspannung | $U_{CC}$ | V | 4,75 | 5,25 | - |
| Eingangsspannung | $U_{IL}$ | V | -0,5 | 0,8 | - |
| | $U_{IH}$ | V | 2 | $U_{CC}$ | - |
| Takteingangsspannung | $U_{ILC}$ | V | -0,5 | 0,45 | - |
| | $U_{IHC}$ | V | $U_{CC}-0,2$ | $U_{CC}$ | - |
| Ausgangsspannung | $U_{OL}$ | V | - | 0,4 | bei $I_{OL} = 1,8\ mA$ |
| | $U_{OH}$ | V | 2,4 | - | bei $I_{OH} = -0,25\ mA$ |
| Eingangsreststrom | $I_{LI}$ | $\mu A$ | - | 10 | |
| Reststrom der SYNC-Anschlüsse | $I_{LSY}$ | $\mu A$ | -40 | 10 | |
| Reststrom INT im hochohmigen Zustand | $I_{LINT}$ | $\mu A$ | - | 10 | |
| Reststrom W/RDY im hochohmigen Zustand | $I_{L W/R}$ | $\mu A$ | - | 40 | |
| Reststrom des Datenbusses bei Eingabe | $I_{LD}$ | $\mu A$ | - | 10 | |
| Taktkapazität | $C_{CP}$ | pF | - | 50 | bei $\vartheta_a = 25\ ^\circ C$ |
| Eingangskapazität | $C_I$ | pF | - | 5 | und f = 0,5...2 MHz |
| Ausgangskapazität | $C_O$ | pF | - | 10 | |
| Stromaufnahme | $I_{CC}$ | mA | - | 100 | bei $U_{CC} = 5,25\ V$ |

**6.2. Dynamische Kennwerte**

*KI generierte Interpretation der Abbildung: Seite 61 zeigt eine Sammlung von Timing-Diagrammen, welche die Phasenbeziehungen zwischen dem Systemtakt $\Phi$ und allen Ein-/Ausgangssignalen des SIO visualisieren. Es werden Verzögerungszeiten ($t_{DL}, t_{DH}, t_{DR}, t_{DI}, t_{FI}$), Voreinstellzeiten ($t_{SC}, t_{SW}$) und Haltezeiten ($t_H$) für die Adress- und Steuersignale (CE, B/A, C/D, IORQ, RD, M1), den Datenbus (D0-D7), die Interrupt-Daisy-Chain (IEI, IEO, INT), Wait/Ready sowie die modem- und kanalspezifischen Leitungen (CTS, DCD, SYNC, TxD, RxD, TxC, RxC) grafisch definiert. Vertikale gestrichelte Linien markieren die relevanten Taktflanken, an denen Signale abgetastet oder stabilisiert sein müssen.*

**Bild: Zeitverhalten - Impulsformen**

| Signal | Symbol | Parameter | Ein-heit | Kleinstwert | Größtwert |
| :--- | :---: | :--- | :---: | :---: | :---: |
| **"$\Phi$"** | $t_c$ (C) | Taktperiode | ns | 400 | |
| | $t_w$ (CH) | Taktimpulsweite High | ns | 170 | 2000 |
| | $t_w$ (CL) | Taktimpulsweite Low | ns | 170 | 2000 |
| | $t_r, t_f$ | Taktanstiegs- und Abfallzeiten | ns | 0 | 30 |
| **$\overline{\text{CE}}$, B/$\overline{\text{A}}$, C/$\overline{\text{D}}$, $\overline{\text{IORQ}}$** | $t_H$ (CS) | Steuersignal-Haltezeit von steigender C-Flanke | ns | 0 | |
| | $t_{SC}$ (CE) | Steuersignal-Voreinstellzeit vor steigender C-Flanke | ns | 160 | |
| **D0-D7** | $t_{DR}$ (D) | Daten-Ausgangsverzögerung von steigender C-Flanke während READ-Zyklus | ns | | 490 |
| | $t_{SC}$ (D) | Daten-Voreinstellzeit vor steigender C-Flanke während WRITE- oder $\overline{\text{M1}}$-Zyklus | ns | 50 | |
| | $t_{HC}$ (D) | Daten-Haltezeit von steigender C-Flanke während WRITE- oder $\overline{\text{M1}}$-Zyklus | ns | 0 | |
| | $t_{DI}$ (D) | Daten-Ausgangsverzögerung von fallender $\overline{\text{IORQ}}$-Flanke während INTA-Zyklus | ns | | 350 |
| | $t_{FIM}$ (D) | Verzögerung bis zum floatenden Bus von steigender $\overline{\text{IORQ}}$-Flanke während INTA-Zyklus | ns | | 240 |
| | $t_{FR}$ (D) | Verzögerung bis zum floatenden Bus von steigender $\overline{\text{RD}}$-Flanke während INTA-Zyklus | ns | | 240 |
| | $t_{FI}$ (D) | Verzögerung bis zum floatenden Bus von fallender IEI-Flanke während INTA-Zyklus | ns | | 240 |
| **IEO** | $t_{DL}$ (IO) | IEO-Verzögerungszeit von fallender IEI-Flanke | ns | | 210 |
| | $t_{DH}$ (IO) | IEO-Verzögerungszeit von steigender IEI-Flanke | ns | | 210 |
| | $t_{DO}$ (IO) | IEO-Verzögerungszeit von fallender $\overline{\text{M1}}$-Flanke (wenn Interrupt genau vor $\overline{\text{M1}}$-Flanke erfolgte) | ns | | 310 |
| **$\overline{\text{M1}}$** | $t_{SWC}$ (M1) | M1-Voreinstellzeit vor steigender C-Flanke während READ- oder WRITE-Zyklus | ns | 210 | |
| | $t_{SRC}$ (M1) | M1-Voreinstellzeit vor steigender C-Flanke während INTA- oder $\overline{\text{M1}}$-Zyklus | ns | 210 | |
| | $t_{HC}$ (M1) | M1-Haltezeit von steigender Flanke | ns | 0 | |
| **$\overline{\text{RD}}$** | $t_{SRC}$ | RD-Voreinstellzeit vor steigender C-Flanke | ns | 240 | |
| **$\overline{\text{INT}}$** | $t_{DRx}$(IT) | INT-Verzögerungszeit von der Mitte des Empfangsdatenbits | C-Perio-den | 10 | 13 |
| | $t_{DTx}$(IT) | INT-Verzögerungszeit von der Mitte des Sendedatenbits | C-Perio-den | 5 | 9 |
| | $t_{DC}$ (IT) | INT-Verzögerung von der steigenden C-Flanke | ns | | 210 |
| **WAIT/READY** | $t_{DIC}$(W/R) | W/R-Verzögerungszeit von $\overline{\text{IORQ}}$ oder $\overline{\text{CE}}$ im WAIT-Mode | ns | | 190 |
| | $t_{DHC}$ (W/R) | W/R-Verzögerungszeit von der fallenden C-Flanke, WAIT/READY-High, WAIT-Mode | ns | | 160 |
| | $t_{DRx}$(W/R) | W/R-Verzögerungszeit von der Mitte des Empfangsdatenbits, READY-Mode | C-Perio-den | 10 | 13 |
| | $t_{DTx}$(W/R) | W/R-Verzögerungszeit von der Mitte des Sendedatenbits, READY-Mode | C-Perio-den | 5 | 9 |
| | $t_{DLC}$ (W/R) | W/R-Verzögerungszeit von der steigenden C-Flanke, WAIT/READY-Low, READY-Mode | ns | | 130 |
| **$\overline{\text{CTSX}}, \overline{\text{DCDX}}, \overline{\text{SYNCX}}$** | $t_W$ (RH) | Minimale Impulslänge zum Einspeichern des Status im Register und Erzeugen eines Interrupts | ns | 200 | |
| | $t_W$ (PL) | Minimale Low-Impulslänge zum Einspeichern des Status im Register und Erzeugen eines Interrupts | ns | 200 | |
| **$\overline{\text{SYNCX}}$** | $t_{DL}$ (SY) | Sync-Impuls-Verzögerungszeit von der Mitte des Empfangsdatenbits, Ausgang | C-Perio-den | 4 | 7 |
| | $t_{SL}$ (SY) | Sync-Impuls-Voreinstellzeit von der steigenden RxC-Flanke, Eingang (Ext.-Sync-Mode) | ns | 100 | |
| | $t_W$ (SY) | SYNC-Impulslänge (Ext.-Sync-Mode) | RxC-Perio-den | 1 | |
| **$\overline{\text{TxCX}}$** | $t_C$ (TxC) | Sendertaktperiode | ns | 400 | $\infty$ |
| | $t_W$ (TCH) | Sendertaktimpulsweite High | ns | 180 | $\infty$ |
| | $t_W$ (TCL) | Sendertaktimpulsweite Low | ns | 180 | $\infty$ |
| **$\overline{\text{TxDX}}$** | $t_D$ (TxD) | TxD-Ausgangsverzögerung von fallender TxC-Flanke (x1-Taktfaktor) | ns | | 410 |
| **$\overline{\text{RxCX}}$** | $t_C$ (RxC) | Empfänger-Taktperiode | ns | 400 | $\infty$ |
| | $t_W$ (RCH) | Empfängertaktimpulsweite High | ns | 180 | $\infty$ |
| | $t_W$ (RCL) | Empfängertaktimpulsweite Low | ns | 180 | $\infty$ |

1) Wird die SIO-WAIT-Funktion genutzt, muß $\overline{\text{CE}}$, $\overline{\text{IORQ}}$, C/$\overline{\text{D}}$ und $\overline{\text{M1}}$ während der gesamten WAIT-Dauer gültig sein.
2) X = A oder B
3) In allen Modes muß der Systemtakt mindestens das 4,5fache der maximalen Datenrate sein.
Das RESET-Signal muß mindestens einen kompletten System-Taktzyklus aktivieren.
Für alle Verzögerungszeiten gilt: $\vartheta_a = 70\ ^\circ C; U_{CC} = 4,75\ V; U_{IL} = 0,8\ V; U_{IH} = 2\ V; U_{ILC} = 0,45\ V; U_{IHC} = 4,55\ V$
Für alle übrigen dynamischen Kennwerte gilt: $\vartheta_a = 0 \dots 70\ ^\circ C; U_{CC} = 4,75 \dots 5,25\ V$

**6.3. Gehäuse**

Bauform 21.2.3.2.40 nach TGL 26 713
Masse ca. 5,4 g

*KI generierte Interpretation der Abbildung: Bild 16 zeigt die technischen Zeichnungen des 40-poligen DIL-Gehäuses. Oben ist die Seitenansicht mit einer Gesamtlänge von 52 mm dargestellt. Die 40 Pins haben einen Abstand von 2,54 mm zueinander. Die Breite des Gehäuses beträgt 15 mm. Unten ist die Draufsicht mit der Pin-Nummerierung von 1 bis 40 abgebildet. Eine Kerbe an der linken Stirnseite markiert Pin 1.*

**VEB FUNKWERK ERFURT**
**im VEB Kombinat Mikroelektronik**
DDR - 5010 Erfurt, Rudolfstraße 47
Telefon: 5 80
Telex: 061 306
Kabel: FUNKWERK ERFURT.

**elektronik**
**export-import**
VOLKSEIGENER AUSSENHANDELSBETRIEB DER DEUTSCHEN DEMOKRATISCHEN REPUBLIK
DDR 1026 BERLIN, ALEXANDERPLATZ
HAUS DER ELEKTROINDUSTRIE

Rs 01-82-31 W-V-2-1
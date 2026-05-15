# Serielle Tastatur K 7637.XX

## robotron

## Betriebsdokumentation

3. Auflage
Karl-Marx-Stadt, 1987

(C) VEB Kombinat Robotron, 1987

Inhaltsverzeichnis
Seite
I. Verwendung und Einordnung .................................................... 2
1. Allgemeine Angaben ........................................................... 2
2. Anwendungstechnische Parameter ................................................ 2
II. Technische Daten ............................................................ 3
III. Konstruktiver Aufbau ....................................................... 5
IV. Bauelementebasis ............................................................ 5
V. Funktionsbeschreibung ........................................................ 6
3. Programmprinzip .............................................................. 6
4. Allgemeiner Ablauf ........................................................... 7
2.1. Einschalten der Tastatur ................................................... 7
2.2. Tastatur im Betriebszustand ................................................ 9
2.3. Spezielle Funktionstasten und Auswahlfunktionen ............................. 12
2.4. Speicheraufteilung ......................................................... 13
5. Bediensicherungsbaugruppe .................................................... 13
3.1. Allgemeine Beschreibung .................................................... 13
3.2. Kennung und Codierung des Bedienelementes und Zuordnung der Drahtbruecken ...... 14
6. Kontaktbelegung der Trennstellen mit Kurzzeichenuebersicht ................... 16
4.1. Systemtrennstellenbelegung ................................................. 16
4.2. Sondertrennstellenbelegung ................................................. 16
7. Besonderheiten der Tastaturadaptierung ....................................... 17
8. Chiffre der Tastaturen und Bestellbezeichnungen ............................... 18
VI. Reparaturanleitung / Wartungsvorschrift ..................................... 19
9. Reparaturanleitung ........................................................... 19
1.1. Unterlagen, Hilfsmittel, Werkzeuge, Messmittel ............................. 19
1.2. Reparaturausfuehrung ....................................................... 19
10. Wartungsvorschrift ........................................................... 20

### I. Verwendung und Einordnung
#### 1. Allgemeine Angaben
Die Tastaturen robotron K 7637.XX ermoeglichen die manuelle Eingabe von alphanumerischen und numerischen Zeichen, von Ruf- und Steuerinformationen sowie von Startbedingungen. Ueber optische Anzeigen und akustischen Signalgeber werden dem Bediener Zustandsinformationen des Systems vermittelt.
Sie sind als Einbau- und als Auftischtastaturen lieferbar. Bei beiden Typen handelt es sich um intelligente Tastaturen fuer Geraete der Klein- und Mikrorechentechnik, welche aus einer speziellen Mikrorechnerkonfiguration auf der Basis der CPU U 880 bestehen. Wesentliche Funktionselemente sind die kontaktlosen Tastenschalter TSH 19 F sowie ein 2K Byte-Speicherschaltkreis, der neben dem Mikroprogramm die Codetabellen der Tasten enthaelt. Die Ausgabe des 8-bit-Codes erfolgt seriell ueber die IFSS-aehnliche Systemtrennstelle X1. An der Trennstelle X2 ist der Anschluss von max. 4 Sonderleitungen der Bediensicherungsbaugruppe moeglich.

Die Tastaturen robotron K 7637.XX bieten folgende Vorteile:
- Anschluss ueber ein duennes, flexibles Kabel mit geringer Leitungsanzahl (serielle Schnittstelle)
- Vereinheitlichung und damit Reduzierung konstruktiver Tastaturvarianten
- Nutzung der Betriebssysteme SIOS 1526 oder SCPX 1526 fuer Geraete der dezentralen Datentechnik.

Die datenuebertragungsmaessige Adaptierung der Tastatur erfolgt geraetespezifisch an einem aktiven IFSS - Tor der Steckeinheit K 8025.XX. In Sonderfaellen ist der Einsatz der Steckeinheit K 6028.50 moeglich. Die Tastaturen robotron K 7637 sind wartungsfrei und im Dauerbetrieb einsetzbar.

#### 2. Anwendungstechnische Parameter
Die Tastaturen K 7637 weisen gegenueber den bisherigen Tastaturen folgende Veraenderungen auf:
- Generelle Bestueckung mit der CTRL - Taste, so dass ueber CTAB 2 der Kommandoumfang fuer SCPX 1526 zur Verfuegung steht
- Vereinheitlichung der Funktionsbezeichnungen der Tastaturen K 7634 / K 7636 im Betriebssystem SIOS wie folgt:

| Funktion | K 7634 | K 7636 | K 7637 |
| :--- | :--- | :--- | :--- |
| Starttasten X | PF XX | SX | PF XX |
| Loeschen Fehleranzeige | RESET | CI | RESET |
| MONITOR - Taste | OFF | M | M |
| Ende - Text 1 | ENTER | ET1 | ET1 |
| Ende - Text 2 | CNCL | ET2 | ET2 |
| Starttaste S | --- | S | ENTER |

- Die Fehlerindikation erfolgt durch eine im Fehlerfall blinkende LED (Position G53) und durch ein einmaliges kurzes akustisches Signal beim Einschalten der Anzeige
- Ansteuerung des akustischen Signalgebers durch das Anwenderprogramm ueber das Kommando BELL moeglich
- Anzeige der Betriebsbereitschaft der Tastatur (Betriebsspannung 5P) durch die LED auf Position E54.
Die danebenliegende Tastenposition E53 ist in den Einbautastaturen mit der Geraeteeinschalttaste bestueckt, waehrend in den Auftischtastaturen diese Position abgedeckt ist, da sich die Geraeteeinschalttaste im Geraetesockel befindet.
- Anordnung der Punkt- und Kommataste im numerischen Bereich der Tastatur, wodurch die 000 - Taste entfallen ist.
Diese Anordnung kann fuer einige Laendervarianten abweichen.
- Veraenderte Anordnung der Funktionstasten CTRL, PRINT, HLT und ESC bei Einsatz der Tastatur K 7637 fuer den Mikrocomputer BC 25.

### II. Technische Daten
Hersteller: VEB Robotron Elektroschaltgeraete Auerbach
Tastelemente: Tastenschalter TSH 19 F (kontaktlos)
Signalelemente:
- optisch: Baustein mit Lichtemitterdiode
- akustisch: akustischer Signalgeber
Interface: Systemtrennstelle X1 mit 10-poliger Buchsenleiste

**Mechanische Kennwerte**
Die Abmessungen und die Tastenfeldaufteilung mit einem Grundraster von 4,75 mm in Zeilenrichtung und einem Zeilenabstand von 19 mm entsprechen internationalem Standard.
- Betaetigungsfrequenz einer Taste: ≤ 10 Hz
- zeitlicher Abstand zwischen der Betaetigung zweier unterschiedlicher Tasten: ≥ 30 ms
- Betaetigungsgeschwindigkeit: 3 mm/s ... 500 mm/s
- Betaetigungskraft (1- und 1 1/2- fach Tasten): ≤ 1,0 N
- Betaetigungskraft (Mehrfachtasten): ≤ 1,5 N
- Tastenhub: 4 mm + 0,3 mm / - 0,2 mm
- Lage des Schaltpunktes (Einschaltpunkt): 1,3 mm ... 3,2 mm nach oberem Anschlag
- Lage des Schaltpunktes (Ausschaltpunkt): ≥ 0,8 mm vor oberem Anschlag
- zulaessiger Hoehenunterschied (benachbarter Tastenknoepfe): ≤ 0,5 mm
- zulaessiger Hoehenunterschied (ueber gesamte Tastaturebene): ≤ 2,5 mm
- Einbaulage (Neigung zur horizontalen Ebene): ≤ 7 Grad
- Summe aller in Betaetigungsrichtung einwirkenden Kraefte: ≤ 30 N
- dabei je Tastelement: max. 10 N fuer die Dauer von max. 30 s
- Masse mit Tastaturrahmen und Bediensicherung: ca. 2,6 kg
- Masse mit Auftischgehaeuse: ca. 4,4 kg
- Abmessungen (analog Tastatur K 7634): 524 mm x 250 mm x 63 mm fuer Auftischtastatur

**Elektrische Anschlusswerte**
- Stromversorgung
  typ. Stromaufnahme: ca. 0,5 A
  Betriebsspannung: 12P oder 5P
  **12P bei Auftischtastaturen**
  erforderliche Eingangsspannung: 8 V ... 12,6 V
  **5P bei Einbautastaturen**
  erforderliche Eingangsspannung: 4,75 V ... 5,25 V
- Signalpegel
  prinzipiell TTL - kompatibel, betreffs der Leitungen der Sondertrennstelle und des Signales `/SA`
  20 mA - Sende- und Empfangsstromschleifen mit gemeinsamem Rueckleiter ueber Massepotential
  Zustand "high": 15 mA ... 25 mA
  Zustand "low": 0 mA ... 3 mA
- Die Uebertragung erfolgt im Asynchronbetrieb mit einer Uebertragungsgeschwindigkeit von 9600 bit/s, wobei die Anschlusssteuerung (IFSS-Gegenstelle) sende- und empfangsseitig im Aktivmodus arbeiten muss.
- Die Uebertragung eines Zeichens beginnt mit einem Startbit; es folgen acht Daten- und ein Stopbit.

**Tast- und Signalelemente**
- max. Anzahl Tastenschalter TSH 19 F: 107
- besondere Funktionstasten (Tastenposition):
  - SHIFT - Taste: B99 und B11
  - LOCK - Taste: C00
  - CTRL - Taste: B00
  - ALT - Taste: --->
  - Steuertaste: waehlbar, z.Zt. nicht bestueckt
  - Triggertaste: --->
- optische Anzeigen (LED - Position):
  - Betriebsspannungsanzeige: E54
  - Zustandsanzeige LOCK - Taste: C99
  - Fehleranzeige (blinkend): G53
  - Selektor- bzw. Zustandsanzeigen: G00, G01, G02, G03, G04
- akustischer Signalgeber H2: fuer Tastenklick bzw. Erzeugung eines akustischen Signals von ca. 1 s Dauer

**Einsatzbedingungen**
- Einsatzklasse EKL 3: +5/+40/+30/95//11-1 B
- Transportklasse TKL 3: -50/+50/+30/95//12-1 LT
- Lagerungsklasse LKL 2: -30/+40/+30/95//12-1 LT
- Stoerinduktion in unmittelbarer Tastaturnaehe: ≤ 0,01 T
- Schutzguete: Schutzgrad IP 20; Schutzklasse III nach TGL RGW 1110 (Betrieb mit Schutzkleinspannung)
- Funkentstoerung: Die Tastatur einschliesslich Anschlusskabel muss in Verbindung mit dem Gesamtgeraet die geltenden Funkstoerforderungen erfuellen.

### III. Konstruktiver Aufbau
Die Tastatur robotron K 7637 mit einer Aufreihlaenge von max. 20 Tastelementen entspricht in den konstruktiven Rahmenbedingungen der bisherigen Tastatur K 7634, besitzt aber eine veraenderte Anordnung, Beschriftung und Codierung der Funktionstasten. Sie besteht aus einer bestueckten Leiterplatte, welche mittels Befestigungsblechen mit einem Montagerahmen mechanisch verbunden ist. Der Montagerahmen enthaelt als tragendes Element Aufreihstreifen und Stabilisierungsschienen und dient der Aufnahme der Tastenschalter, der Anzeigeelemente und der Abdeckbausteine. Er wird mit dem Tastaturrahmen und der Bediensicherungsbaugruppe verschraubt.
Ausserhalb des Montagerahmens sind auf der Leiterplatte die restlichen elektrischen und elektronischen Bauelemente einschliesslich zusaetzlicher Anzeige-LED's und der akustische Signalgeber eingeloetet.
Als Auftischtastatur wird die oben beschriebene Baugruppe mit der Tastenabdeckung komplettiert und in die Tastaturverkleidung eingebaut.

Der elektrische Anschluss der Tastatur erfolgt ueber ein Anschlusskabel, welches an der Systemtrennstelle X1 der Leiterplatte angeloetet wird und auf der Gegenseite mit einer 10-poligen Buchsenleiste versehen ist. Verwendung findet bei Tastaturen mit 12V Betriebsspannung Fm-Plastschlauchleitung HYY 4x1x0,14 gr bzw. bei Tastaturen mit 5V Betriebsspannung Fm-Plastschlauchleitung HYY 10x1x0,14 gr. Die Kabellaenge betraegt fuer Auftischtastaturen in der Regel 1,5 m bzw. 3,0 m; fuer Einbautastaturen 0,5 m.
Bei Nachweis der Funktionssicherheit kann bei 12V - Tastaturen die Anschlussleitung verlaengert werden.
Die Bediensicherungsbaugruppe ist analog der Tastaturen K 7634 / K 7636; sie wird ueber eine vieradrige Anschlussleitung an die Sondertrennstelle X2 der Leiterplatte angeloetet.

### IV. Bauelementebasis
Fuer die Tastatur werden handelsuebliche elektronische Bauelemente der Standardtypenreihen (Transistoren, TTL- und LSI-Schaltkreise) eingesetzt, die im Heft "Bausteinuebersicht" erlaeutert sind. Es folgt deshalb lediglich eine Kurzbeschreibung des Tastenschalters TSH 19 F.
Das Arbeitsprinzip des TSH 19 F ist das eines magnetisch betaetigten Schalters, wodurch kontaktgebende Elemente ausgeschlossen sind. Er besitzt einen integrierten Schaltkreis, bestehend aus Referenzspannungsquelle, Hallgenerator, Differenzverstaerker, Trigger und Ausgangstransistorstufe. Bei Betaetigung der Taste wird ein Magnetsystem dem Hallelement genaehert, wobei die Hallspannung proportional mit dem Magnetfeld ansteigt. Ist ein definierter Hall - Spannungswert erreicht, erfolgt das Umschalten der Ausgangsspannung von high nach low. Voraussetzung fuer diesen Schaltvorgang ist das Anliegen eines high - Pegels am Freigabeeingang. Liegt dieser auf low, ist das Umschalten des Ausgangspegels nicht moeglich.

![[Pasted image 20260515223321.png]]
Abbildung 1: Blockschaltbild des Hall - Schaltkreises B 461 G

![[Pasted image 20260515223343.png]]
Abbildung 2: Tastenschalter TSH 19 F

### V. Funktionsbeschreibung
#### 1. Programmprinzip
Die zentrale Verarbeitungseinheit auf der Tastatur (CPU U880) organisiert nach dem Durchlauf einer Betriebsbeginnroutine eine zyklische Abfrage der in einer Matrix zusammengefassten Tasten. Dabei wird jede Gruppe der Tastaturmatrix komplett eingelesen und sofort ausgewertet. Ist eine Taste als gueltig erkannt, wird ihr Tastencode aus der Codetabelle eines 2K-Byte-ROM-Schaltkreis in den Mikroprozessor eingeschrieben und ueber ein Flip-Flop seriell ausgegeben.
Die Signaleingabe zur Tastatur erfolgt mit seriellen 1- und 2-Byte-Kommandos, die immer von der Tastatur mit dem Zeichen TYP quittiert werden.

![[Pasted image 20260515223420.png]]
Abbildung 3: Grobflussbild

Da die Tastatur mit verschiedenen IFSS-Kanaelen zusammenarbeiten kann, stellt der Tastaturmodul des Betriebssystems fest, an welchem IFSS-Kanal die serielle Tastatur angeschlossen ist. Dazu wird auf jedes in Frage kommende IFSS-Tor ein RESET - Kommando ausgegeben und die Kennung der Tastatur (Zeichen TYP, in der Regel mit Codierung 80H) als Antwort erwartet.
Der Datenaustausch selbst wird sowohl sende- als auch empfangsseitig zwischen Tastaturelektronik und dem SIO-Schaltkreis der IFSS-Anschlusssteuerung realisiert. Dabei ist es nicht erforderlich, den Tastencode nach der Ausgabe aus der CPU zu puffern, da diese Moeglichkeit im SIO gegeben ist.
Die Steuerung aller Ablaeufe uebernehmen die fuer die Tastatur K 7637 geschaffenen Betriebssystemmoduln fuer SIOS bzw. SCPX in Verbindung mit dem Mikroprogramm der tastaturinternen CPU.

#### 2. Allgemeiner Ablauf
#### 2.1. Einschalten der Tastatur
Die Geraeteeinschalttaste mit der Tastenposition E53,5 ist nur bei den Einbauvarianten (K7637.00 bis .49) zu finden, weil hier ueber die Tastaturtrennstelle X1 die Hilfsspannung 5PH anliegt und ueber Sonderausgang `/SA` mit dem Einschalten das Netzteil der Anlage aktiviert werden kann. Dazu ist es erforderlich, die Bruecken E1:3 und E1:5 zu bestuecken. Wegen der kurzen Zuleitung erfolgt die Stromversorgung der Einbautastaturen direkt durch die Betriebsspannung 5P.
Bei allen Auftischgeraeten ist das Einschalten der Anlage durch eine separate Taste am Sockel des Grundgeraetes moeglich. Damit entfaellt diese Taste in den Auftischtastaturen (K7637.50 bis .99). Nach dem Einschalten wird die Tastatur mit der Spannung 12P versorgt; ueber den integrierten Spannungsregler D10 steht eine stabile und TTL-gerechte Spannung 5P zur Verfuegung.
Die Betriebsbereitschaft wird bei beiden Tastaturen ueber den LED-Baustein auf Position E54 angezeigt.
Durch eine RC-Kombination mit nachgeschaltetem Transistor V2:1 wird mit dem Zuschalten der Betriebsspannung ein Hardware-RESET (t > 0,2 s) ausgeloest.
Der dadurch erreichte Grundzustand der Tastatur ist wie folgt charakterisiert:
- SHIFT aus
- alle LED-Funktionsanzeigen aus (ausser Pos. E54)
- Einschalten der Codetabelle 1 (CTAB 1a), sofern Bruecke E2:2 offen ist
- akustischer Tastenklick ein (wird nur bei Tastaturen des ESS A 5310 genutzt)
- Ausgabe eines ungueltigen Zeichens, welches beim Einschalten in der Stromschleife entsteht (meist 00H), danach Ausgabe des Zeichens TYP
- Ausgabe des Codes der Sonderleitungseingaenge SL1 bis SL4 (bei BWK-Tastaturen SL1 bis SL3), die durch das Bedienelement der Bediensicherungsbaugruppe mit low - Pegel belegt werden.

Alle oben erwaehnten Ausgaben wertet das Betriebssystem der Anlage nicht aus, da das Austesten der moeglichen IFSS-Kanaele, an welchem die Tastatur angesteckt ist, erst mit dem Aussenden des Kommandos 00H (= Software-RESET) erfolgt. Damit wird ebenfalls der oben beschriebene Grundzustand der Tastatur erreicht und es werden dann das Zeichen TYP sowie die Codes der SL-Leitungen ausgegeben. Diese "Antwort" dient der Identifikation fuer die Tastaturarbeit gewaehlten IFSS-Kanais.
Nach Ausschalten der Tastatur ist zur Gewaehrleistung eines gesicherten Arbeitsbeginns der Tastaturelektronik das Wiedereinschalten nicht sofort moeglich. Diese notwendige Verzoegerung wird durch die Stromversorgungsbaugruppe der Anlage garantiert.

**2.1.1. Das Zeichen TYP**
Das fuer die Tastatur ausgewaehlte Zeichen TYP ist im EPROM der Tastatur auf der Adresse 7F1H eingetragen. Es ist frei waehlbar (Code 00H bis FFH moeglich), wobei im Betriebssystem SIOS nur die Codierung 80H ausgewertet wird. Zu den niederwertigsten beiden bit dieses Zeichens werden programmaessig zwei bit addiert, deren Wert die Bruecken E2:1 und E2:2 bestimmen.

| Bruecke | Name | Addition | Wert | Wirkung (SHIFT unbeeinflusst) |
| :--- | :--- | :--- | :--- | :--- |
| E2:1 | Konf. | auf bit 1 | 0 | TYP 1 |
| | | | 1 | TYP 2 |
| E2:2 | Code | auf bit 0 | 0 | CTAB 1 in Grundstellung, bei betaetigter CTRL-Taste erfolgt Umschaltung auf CTAB 2, ALT-Taste ist wirksam |
| | | | 1 | CTAB 2 in Grundstellung, bei betaetigter CTRL-Taste bleibt CTAB 2, ALT-Taste ist nicht benutzbar |

0 = offene Bruecke (≙ high-Pegel)
1 = geschlossene Bruecke (≙ low-Pegel)

![[Pasted image 20260515223450.png]]
Abbildung 4: Lage der Bruecken auf der Tastaturleiterplatte

**2.1.2. Sonderleitungseingaenge**
Die Bediensicherungsbaugruppe ist ueber die Sonderleitungseingaenge der Trennstelle X2 mit der Tastatur verbunden. Jeder SL-Eingang wirkt aehnlich einem Tastelement in der Matrix, d.h. die Abfrage erfolgt zyklisch mit jedem Matrixdurchlauf bei Aufsteuerung der Adressleitung A15 ueber das NAND D6:4. Eine Ausgabe der Codes der Sonderleitungen erfolgt jedoch nur bei jeder Pegelaenderung, welche durch Veraenderung am Bedienelement der Bediensicherungsbaugruppe entsteht, d.h. alle SL-Eingaenge haben Triggerverhalten.
Der Code von SL1 bis SL4 wird im Tastaturspeicher auf die Adressen
648H ... 64BH und 6D8H ... 6DBH fuer CTAB 1 bzw.
758H ... 75BH und 7E8H ... 7EBH fuer CTAB 2
eingetragen. Ist auf diesen Adressen 00H programmiert, wird die Zeichenausgabe fuer den betreffenden SL-Eingang unterdrueckt.

#### 2.2. Tastatur im Betriebszustand
**2.2.1. Aenderungstest**
Der tastaturinterne Mikroprozessor organisiert eine zyklische Abfrage der Tastenmatrix (IORQ aktiv), indem die 16 Matrixspalten der Reihe nach vom Adressbus A0...A15 angesteuert werden. Damit schalten die Freigabeeingaenge der in einer Spalte angeordneten TSH 19 F nach high. Indem vor jedem Weiterschalten der Adressleitungen ueber den Datenbus der CPU die Zeilenleitungen Z0...Z7 (Ausgaenge TSH 19 F) abgefragt werden, ist es moeglich, betaetigte Tasten innerhalb jeder Gruppe zu erkennen. Es wird also jede Gruppe zu 8 bit komplett eingelesen und ausgewertet.
Der vom Schwingquarz G8:1 erzeugte Takt wird ueber Zaehler D9:1 im Verhaeltnis 1:10 untersetzt, so dass der Mikroprozessor mit einer Taktfrequenz von ca. 982 kHz taktiert wird. Damit liegt im Grundzustand der Tastatur die Dauer einer vollstaendigen Matrixabfrage bei $T_M$ ca. 5,45 ms.
Diese Zeit ist der minimale Abstand zwischen der Bearbeitung von Kommandos, da erst nach Austestung der letzten Gruppe der Matrix in die Routine der Schnittstellenbehandlung gesprungen wird. Eine Taste gilt als betaetigt, wenn sie mindestens fuer die Zeit zweier aufeinanderfolgender Matrixdurchlaeufe gedrueckt wurde. Demzufolge wird dieser gueltige Tastencode nach max. $3 T_M$ ausgegeben. $3 T_M$ entspricht gleichzeitig dem minimalen Abstand zwischen der Betaetigung zweier unterschiedlicher Tasten.

**2.2.2. Tastenverarbeitung**
Bei einer erkannten Aenderung waehrend der gruppenweisen Matrixaustestung erfolgt eine sofortige Behandlung derselben im Programmabschnitt "Tastenverarbeitung". Dadurch wird der Abfragezyklus $T_M$ verlaengert.
Die Gruppe mit der betaetigten Taste wird in der CPU zur weiteren Auswertung zwischengespeichert. Insgesamt sind vier CPU-Register als "Tastenbetaetigungsspeicher" reserviert, wobei ein Speicher fest fuer die letzte Spalte der Matrix (Sonderleitungen, CTRL- und Umschalttasten) vergeben ist. Damit ermoeglicht die Tastatur als Folgetaetigung 3-key-roll-over.

Zur Bestimmung betaetigter Tasten wird eine Rechenadresse gebildet, welche eine Relativadresse zur Anfangsadresse der Codetabelle darstellt. Sie wird wie folgt ermittelt:

X   XXX   X   XXX (binaere Darstellung)
            |--Zeilennummer 0...7
        |-- Matrixnummer 0: A0...A7, 1: A8...A15
     |-- Spaltennummer 0...7
|-- 0 in Grundstellung (CTAB a)
|-- 1 waehrend der Programmabarbeitung bei Umschaltstellung (CTAB b)

Wird diese Rechenadresse zur Anfangsadresse der Codetabelle 1 (5D0H) addiert, erhaelt man den ROM-Speicherplatz, auf welchem der Code der betaetigten Taste programmiert ist. Nachdem die betaetigte Taste nach bestimmten Kriterien wie "Sondertaste", "SHIFT", "Doppelsetzung" u.a. untersucht wurde und als gueltig erkannt ist, erfolgt die Tastencodeausgabe aus der Codetabelle in die CPU.
In jeder Codetabelle koennen bis zu 16 Tasten als Dauerfunktionstasten festgelegt werden. Entspricht der aktuelle Tastencode einem solchen Dauerfunktionscode, so wird nach einer ersten Zeitschwelle von ca. 500 ms (festgelegt im ROM-Byte mit der Adresse 480H) das Zeichen im Abstand von ca. 100 ms (festgelegt im ROM-Byte mit der Adresse 481H) ausgegeben, solange die Taste betaetigt ist. Jede weitere zu einer Dauerfunktion gedrueckte Taste beendet die Dauerfunktion.
Nach der Verarbeitung der betaetigten Taste erfolgt entweder die Fortsetzung der Matrixabfrage bzw. nach Beendigung derselben die Schnittstellenbehandlung.

**2.2.3. Schnittstellenbehandlung**
Die Tastaturschnittstelle ist logisch-funktional eine IFSS-Schnittstelle, die aber wegen der notwendigen Stromversorgung der Tastatur eine Reduzierung der Datenuebertragungsleitungen durch die tastaturseitige Zusammenschaltung der Rueckleitungen beider IFSS-Schleifen und Nutzung dieser Leitung als Massepotential aufweist.
Die Signaleingabe zur Tastatur erfolgt mit seriellen Kommandos von 1- bzw. 2 Byte Laenge. Die in die IFSS-Sendestromschleife eingespeiste Information in Form von Stromimpulsen gelangt tastaturintern auf die Leitung ED und schaltet in Abhaengigkeit von der Anzahl der Impulsflanken das Flip-Flop D8:1 ueber die statischen Eingaenge 01 und 04. Ausgang 05 bewirkt ein Rueckwaertzaehlen des auf den Wert 15 voreingestellten Binaerzaehlers D7:1. Die Auswertung des dem empfangenen Kommandos entsprechenden Zaehlerstandes erfolgt unter der Bedingung A11 ^ MREQ ueber die Gatter D6:5 und die Datenbusleitungen DB0...DB3.
Man erkennt, dass lediglich die Anzahl der Einzelimpulse (low/high-Flanken) eines Kommandos entscheidend ist, um im Zaehlerschaltkreis einen bestimmten Zaehlerstand einzustellen. Der negierte Zaehlerstand wird als Halbbyte von der CPU eingelesen und decodiert.
Der tastaturinterne Mikroprozessor fragt mindestens zweimal ueber seine niederwertigen Datenbusleitungen den negierten Zaehlerstand ab und wertet erst dann das Kommando als gueltig, wenn sich im zeitlichen Abstand von 1,1 ms im Zaehlerschaltkreis die gleiche Information befindet. Eine Ausnahme bildet das Vorkommando 55H, weil der zeitliche Abstand zwischen den Bytes eines 2-Byte-Kommandos beliebig ist.
Ein als gueltig erkanntes Kommando wird von der Tastatur immer mit dem Zeichen TYP quittiert. Ein neues Kommando kann erst nach Quittung des vorangegangenen an die Tastatur gegeben werden. Die dafuer maximal benoetigte Zeit betraegt etwa 10 ms.

Folgende Kommandos koennen von der Tastatur verarbeitet werden:

| Kommando | Zaehlerstand | Wirkung |
| :--- | :---: | :--- |
| 00H | 14 | **Software - RESET** |
| 55H, 00H | 9 | Einstellen des Grundzustandes der Tastatur entsprechend Pkt. 2.1. |
| 20H | 13 | Blinken der optischen **Fehleranzeige** ein- oder ausschalten (vorheriger Zustand wird negiert) |
| | | Beim Einschalten Erzeugung eines akustischen Signals von ca. 1 s Dauer. |
| 44H | 12 | ca. 1 s **akustisches Signal** |
| 52H | 11 | **Ein- bzw. Ausschalten von LED-Anzeigen** |
| 55H, 20H | 8 | (vorheriger Zustand wird negiert) |
| 55H, 44H | 7 | Die Kommandos sind in der aufgefuehrten Reihenfolge den ROM-Adressen 4A0H...4A4H zugeordnet. Ihre Wirkungsweise ist demzufolge von den Eintragungen auf diesen Adressen abhaengig. |
| 55H, 52H | 6 | |
| 55H, 55H | 5 | |

Folgende Zuordnungstabelle gilt:

| LED-Position | eingetragener Code |
| :--- | :---: |
| G00 | 01H |
| G01 | 02H |
| G02 | 04H |
| G03 | 08H |
| G04 | 10H |
| G53 | 20H |
| keine | 00H |

Fuer im BWK z. Zt. zum Einsatz kommende Tastaturen sind folgende LED - Positionen festgelegt:

| Kommando | Tastatur Standardvarianten | Tastatur f. PRT K 8927 | Tastatur f. ESS A 5310 |
| :--- | :---: | :---: | :---: |
| 52H | G00 | --- | --- |
| 55H, 20H | G01 | G01 | --- |
| 55H, 44H | G02 | --- | --- |
| 55H, 52H | G03 | --- | --- |
| 55H, 55H | G04 | G00 | --- |

Bei der Ausgabebehandlung wird der Tastencode der als gueltig erkannten Taste aus der gewaehlten Codetabelle des Speicherschaltkreises herausgelesen und in einem internen Register der CPU zwischengespeichert. Ohne weitere Zwischenpufferung erfolgt eine sofortige serielle Ausgabe des Zeichens ueber Datenbusleitung DB0 entsprechend der Vorschriften des Asynchronbetriebes, d.h. einschliesslich Start- und Stopbit. Unter der Bedingung A12 ^ WR wird die Information bit fuer bit in das Ausgabe - FF D8:1 uebernommen. Ausgang D8:1-08 steuert Transistor V4:1 und regelt damit den Stromfluss in der IFSS-Empfangsstromschleife (tastaturintern: Leitung SD+).
Im SIO-Schaltkreis des zur Tastatursteuerung benutzten Adapters erfolgt eine vierfache Datenpufferung, so dass der Zeitpunkt der Abholung des Tastencodes durch die ZRE unkritisch ist.

**2.3. Spezielle Funktionstasten und Auswahlfunktionen**
**Umschalttasten (SHIFT), Umschaltfeststeller (LOCK)**
Die Tasten bewirken die Umschaltung innerhalb einer Codetabelle durch Setzen / Ruecksetzen des bit 7 in der Rechenadresse. Der Umschalttaste und dem Feststeller werden in der Matrix feste Plaetze zugeordnet (Tastenposition B11 / B99 und C00, Rechenadresse 7FH und 77H). Bei einer Betaetigung der LOCK-Taste wird fest auf die obere Codeebene umgeschaltet; die LED auf Position C99 leuchtet. Durch Betaetigen der SHIFT-Taste erfolgt das Rueckschalten.
Fuer diese Tasten ist der Code 00H eingetragen (keine Zeichenausgabe).

**CTRL- bzw. ALT-Taste**
Solange die CTRL-Taste betaetigt ist, wird von Codetabelle 1 auf Codetabelle 2 umgeschaltet. Das Zeichen TYP bleibt dabei unbeeinflusst. Die Tastenposition hat die Koordinate B00; die Rechenadresse ergibt sich mit 7EH. Als Tastencode ist 00H eingetragen (keine Zeichenausgabe).
Analog der LOCK-Taste laesst die Tastatur K 7637 eine ALT-Taste fuer die Festumschaltung der Codetabellen 1 und 2 zu. Sie ist einschliesslich der zugehoerigen LED-Anzeige frei waehlbar. Die gewaehlten Positionen muessen auf den Speicherplaetzen 470H bzw. 490H programmiert werden. Derzeit ist eine Benutzung der ALT-Taste nicht vorgesehen (eingetragen ist 01H auf Adresse 470H).

**Triggertaste**
Der Code dieser frei waehlbaren Taste wird von der Tastatur beim Betaetigen und beim Loslassen ausgegeben. Die Rechenadresse der gewuenschten Tastenposition muss auf Speicherplatz 472H eingetragen werden. Da die Triggertaste z.Zt. nicht benutzt wird, ist auf diesem eine 01H eingeschrieben.

**Steuertaste**
Diese Taste ermoeglicht das Ab- und Zuschalten des Tastenklicks, sofern diese Funktion generell fuer die jeweilige Tastaturvariante vorgesehen ist. Lediglich die Tastatur des Schreibsystems ESS A 5310 arbeitet mit Tastenklick. Aber auch dort ist keine Steuertaste programmiert, so dass ein Abschalten dieses akustischen Signals nicht moeglich ist. Demzufolge ist derzeit bei allen BWK-Tastaturen auf ROM-Speicherplatz 471H der Code 01H eingetragen.

**2.4. Speicheraufteilung**
Fuer Programm- und Tastencodespeicherung wird ein 2K-Byte-Speicherschaltkreis (ROM, P oder EPROM) eingesetzt, dessen wesentlichster Speicherinhalt aus nachfolgender Aufstell ersichtlich ist:

| ADDR (Beginn) | Name |
| :--- | :--- |
| 000H | PRx Programm, konstanter Teil |
| 400H | Auswahlfunktionen |
| 4C0H | **CTAB0a** Codetabelle 0, unshift |
| 540H | Dauerfunktionscode CTAB0 |
| 550H | **CTAB0b** Codetabelle 0, shift |
| 5D0H | **CTAB1a** Codetabelle 1, unshift |
| 650H | Dauerfunktionscode CTAB1 |
| 660H | **CTAB1b** Codetabelle 1, shift |
| 6E0H | **CTAB2a** Codetabelle 2, unshift |
| 760H | Dauerfunktionscode CTAB2 |
| 770H | **CTAB2b** Codetabelle 2, shift |
| 7F0H | Bereich TYP |
| 7F8H | Kennzeichnung des Bitmusters |

a = unshift (untere Codiersebene)
b = shift (obere Codiersebene)

### 3. Bediensicherungsbaugruppe
#### 3.1. Allgemeine Beschreibung
Die Bediensicherungsbaugruppe befindet sich zusaetzlich in der Tastatur und dient dem Schutz der Anlage vor unbefugter Benutzung. Sie gewaehrleistet einen differenzierten Zugriff auf die Anwenderprogramme und die Funktionen des Kommunikationssystems (MONITOR).
Von der im Bedienelement durch 8 Codierstopfen verschluesselbaren Information dienen 3 bit der Bedienerkennung. Sie liefern die Aussage, ob es dem Bediener gestattet ist, das aufgerufene Programm oder die gewuenschte Monitor-Funktion zu starten. Ausserdem geben sie eine durch das Programm auswertbare Information ueber den Bediener, falls mehrere Bedienkraefte Zugriff zu gleichen Programmen haben.
Die restlichen 5 bit des Bedienelementes dienen der Geraetekennung, d.h. es wird hardwareseitig ausgewertet, ob der Bediener an der richtigen, fuer ihn zugelassenen Anlage arbeitet. Die Hardware der Bediensicherungsbaugruppe besteht aus der Steckeinheit BES mit den Mikrotastern S1...S8 und dem zum Betaetigen der Taster notwendigen codierbaren Bedienelement.
Wie der Stromlaufplan der Steckeinheit BES zeigt, kann eine programmaessige Auswertung der Bedienerkennung ueber die Leitungen SL1...SL3 nur dann erfolgen, wenn die Auswertung der Geraetekennung positiv war, d.h. wenn die betaetigten Mikrotaster identisch mit den auf der Steckeinheit BES gewickelten Drahtbruecken sind.
Die Abfrage der Leitungen SL1...SL3 geschieht wie im Punkt 2.1.2. beschrieben.

#### 3.2. Kennung und Codierung des Bedienelementes und Zuordnung der Drahtbruecken
**3.2.1. Kennung des Bedienelementes**
Positionen der Codierstopfen: 8 7 6 5 4         3 2 1
							        |-- Bedienerkennung YY
                            |-- Geraetekennung XX

**3.2.2. Codierung der Bedienerkennung**

| Bedienerkennung | Codierung mittels Codierstopfen (3 2 1) | Funktion |
| :--- | :---: | :--- |
| XX / 01 | --- --- o | Chefschalter |
| XX / 02 | --- o --- | 1. Bedienebene |
| XX / 03 | --- o o | 2. Bedienebene |
| XX / 04 | o --- --- | 3. Bedienebene |
| XX / 05 | o --- o | 4. Bedienebene |
| XX / 06 | o o --- | 5. Bedienebene |

**3.2.3. Codierung der Geraetekennung**

| Geraetekennung | Codierung mittels Codierstopfen (8 7 6 5 4) | Bestueckung der Bruecken mittels Drahtstueck (81/80 | 71/70 | 61/60 | 51/50 | 41/40) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| 01 / YY | --- --- --- --- o | X | | | | X |
| 02 / YY | --- --- --- o --- | | X | | | X |
| 03 / YY | --- --- --- o o | X | X | | | X |
| 04 / YY | --- --- o --- --- | | | X | | X |
| 05 / YY | --- --- o --- o | X | | X | | X |
| 06 / YY | --- --- o o --- | | X | X | | X |
| 07 / YY | --- --- o o o | X | X | X | | X |
| 08 / YY | --- o --- --- --- | | | | X | X |
| 09 / YY | --- o --- --- o | X | | | X | X |
| 10 / YY | --- o --- o --- | | X | | X | X |
| 11 / YY | --- o --- o o | X | X | | X | X |
| 12 / YY | --- o o --- --- | | | X | X | X |
| 13 / YY | --- o o --- o | X | | X | X | X |
| 14 / YY | --- o o o --- | | X | X | X | X |
| 15 / YY | --- o o o o | X | X | X | X | X |
| 16 / YY | o --- --- --- --- | X | | | | |
| 17 / YY | o --- --- --- o | | X | | | |
| 18 / YY | o --- --- o --- | X | X | | | |
| 19 / YY | o --- --- o o | | | X | | |
| 20 / YY | o --- o --- --- | X | | X | | |
| 21 / YY | o --- o --- o | | X | X | | |
| 22 / YY | o --- o o --- | X | X | X | | |
| 23 / YY | o --- o o o | | | | X | |
| 24 / YY | o o --- --- --- | X | | | X | |
| 25 / YY | o o --- --- o | | X | | X | |
| 26 / YY | o o --- o --- | X | X | | X | |
| 27 / YY | o o --- o o | | | X | X | |
| 28 / YY | o o o --- --- | X | | X | X | |
| 29 / YY | o o o --- o | | X | X | X | |
| 30 / YY | o o o o --- | X | X | X | X | |

### 4. Kontaktbelegung der Trennstellen mit Kurzzeichenuebersicht
#### 4.1. Systemtrennstellenbelegung
Die Belegung des 10-poligen Steckverbinders (Buchsenleiste 222-10) am Tastaturkabel ist wie folgt:

**Einbautastatur**

| Kontakt | A | B |
| :--- | :--- | :--- |
| 1 | | Mp |
| 2 | | SD+ |
| 3 | | `/SA` |
| 4 | 5PH | ED- |
| 5 | | 5P |

**Auftischtastatur**

| Kontakt | A | B |
| :--- | :--- | :--- |
| 1 | | Mp |
| 2 | 12P | SD+ |
| 3 | | |
| 4 | | ED- |
| 5 | | |

Bedeutung der Symbole:
SD+ : Sendedaten (der Anschlusssteuerung)
ED- : Empfangsdaten (der Anschlusssteuerung)
SA : Sonderausgang
5P oder 12P : Betriebsspannung
5PH : Hilfsspannung zum Einschalten der Anlage
Mp : Massepotential

**Hinweis!**
Die Kontakte A1, A3 und A5 duerfen seitens der Tastatur K 7637 nicht belegt werden, da diese fuer die Tastatur K 7633 bzw. fuer die Kundentastatur reserviert sind.

#### 4.2. Sondertrennstellenbelegung
Die Sondertrennstelle dient dem Anschluss der Bediensicherungsbaugruppe. Ueber Steckloetoesen ist der Anschluss wie folgt realisiert:

| Anschlussnummer | Symbol des Anschlusses |
| :--- | :--- |
| X2:1 | SL1 |
| X2:2 | SL2 |
| X2:3 | SL3 |
| X2:7 | Uss |

Bedeutung der Symbole:
SL1...SL3 : Sonderleitungseingaenge
Uss : Masse der Tastatur

### 5. Besonderheiten der Tastaturadaptierung
Der datenuebertragungsmaessige Anschluss der Tastatur K 7637 erfolgt an einem aktiven IFSS-Tor der Steckeinheit K 8025.XX bzw. in Ausnahmefaellen am IFSS-Tor der Steckeinheit K 6028.50.
Die Leiterplatte der Steckeinheit K 8025.XX muss den Aenderungsindex 1 haben (Kennzeichen: PIO-Schaltkreis ist nicht mehr vorhanden). Fuer die Tastaturen wird standardmaessig der Steckverbinder X4 (Zusatzdrucker - Anschluss) benutzt. Findet Steckverbinder X5 (IFSS-DFÜ) Verwendung, ist zu beachten, dass wegen der Taktversorgung vom eigenen CTC statt der Wickelverbindung X7 - X8 die Verbindung X8 - X9 hergestellt wird. Ausserdem muessen ueber die DIL - Schalter A41 und A61 sowohl der IFSS - Sender als auch der IFSS - Empfaenger in den Aktivmodus versetzt werden.
In besonderen Einsatzfaellen ist die Verwendung der Steckeinheit K 6028.50 moeglich, weil hier der Sende- bzw. Empfangstakt vom eigenen CTC ueber Bruecke W2:18 dem SIO-Schaltkreis zugefuehrt wird. Abweichend von der Einstellvorschrift (Pkt. 3.2. der Funktionsbeschreibung K 6028) ist ausserdem zu beachten, dass am DIL-Schalter A41 neben der Einstellung "Sender im Aktivmodus" zusaetzlich die Verbindung 9-10 (PIN 05-12) geschaltet wird (schliessen der Empfaengerstromschleife gegen Masse).

Der Anschluss der Tastatur K 7637 erfolgt bei allen Auftischgeraeten (A 5120, K 8924/27/31) ueber Steckverbinder X26 am Sockel. Die Leiterplatte Typ-Nr. 062-8825 (fuer K 8924) bzw. 062-8826 (fuer A 5120, K 8927/31) im Sockel dient als Zwischenadapter und realisiert ausserdem Funkentstoermassnahmen fuer die Datenuebertragung zwischen Geraet und Tastatur. Die Einbautastatur des Buerocomputers A 5130 wird mit einem Steckverbinder im Tastaturtraeger links verbunden, an welchem ueber Kabelbaeume die Daten bzw. Spannungsleitungen zugefuehrt werden. Damit ist ein servicefreundlicher Austausch der Tastatur gewaehrleistet.

### 6. Chiffre der Tastaturen und Bestellbezeichnungen
Die Tastaturen werden durch die zusaetzliche Angabe vollstaendiger Chiffre praezisiert.
Folgende Bezeichnung gilt:
**Tastatur robotron K 7637.XX**
XX = 00...49 Einbauvariante ohne Gehaeuse
XX = 50...99 Auftischvariante mit Gehaeuse

Bestellbezeichnung (ausgewaehlte Beispiele)

| Bezeichnung | Variante | BWK - Nr. | KROS - Nr. |
| :--- | :--- | :---: | :---: |
| **a) Einbautastatur fuer A 5130 (Tastaturbaugruppe 3)** | | | |
| K 7637.01 | stand.-lat. | 083-6-705-003 | 1.49.780128.1 |
| .02 | lat./kyr. | -004 | .780131.2 |
| .04 | ungarisch | -006 | .780136.1 |
| .12 | bulgarisch | -007 | .780145.8 |
| .06 | polnisch | -008 | .780137.8 |
| .13 | deutsch | -009 | .780146.6 |
| **b) Auftischtastatur fuer A 5120 und K 8931** | | | |
| K 7637.50 | stand.-lat. | 083-6-705-023 | 1.49.780126.5 |
| .53 | lat.-kyr. | -024 | .780132.0 |
| .58 | serbokroat. | -025 | .780149.0 |
| .55 | ungarisch | -026 | .780138.6 |
| .57 | polnisch | -028 | .780140.0 |
| .64 | deutsch | -029 | .780147.4 |
| **c) Auftischtastaturen fuer Sondervarianten** | | | |
| K 7637.60 | russisch/ESS | 083-6-705-032 | 1.49.780143.3 |
| .61 | lat./BC 25 | -083 | .780144.1 |
| **d) Auftischtastaturen fuer K 8924 und K 8927 (3 m Kabel)** | | | |
| K 7637.59 | deutsch/PRT | 083-6-705-042 | 1.49.780142.5 |
| .50 | stand.-lat. | -043 | .780130.4 |
| .53 | lat./kyr. | -044 | .780133.7 |
| .55 | ungarisch | -046 | .780139.4 |
| .57 | polnisch | -048 | .780141.7 |
| .64 | deutsch | -049 | .780148.2 |

### VI. Reparaturanleitung / Wartungsvorschrift
#### 1. Reparaturanleitung
Die Reparaturanleitung gilt fuer Tastaturen robotron K7637 mit Auftischgehaeuse und umfasst vorzugsweise Hinweise zur Reparaturausfuehrung. Waehrend der Reparatur sind die entsprechenden Bestimmungen des Arbeitsschutzes sowie die Bestimmungen fuer die eingesetzten Hilfsmittel, Werkzeuge und Messmittel zu beachten.

**1.1. Unterlagen, Hilfsmittel, Werkzeuge, Messmittel**
- Betriebsdokumentation
- Bauelementuebersicht
- Reparaturloetplatz mit Ausloetvorrichtung fuer IS
- Einloetdraht ESD 1,5
- Loettinktur SK 10 SW 31
- Spezialwerkzeuge wie Tastenknoepfabzieher 1.49.280153.4
- Reparaturwerkzeugsatz
- Messmittel wie Oszillograf, Vielfachmesser u.a.

**1.2. Reparaturausfuehrung**
**1.2.1. Tastenknoepfe**
Zum Abziehen des Tastenknoepfes ist der Tastenknoepfabzieher des Tastaturherstellers ESA zu verwenden. Ist kein Abzieher vorhanden, so ist beim Wechseln des Tastenknoepfes der Tastenschalter TSH 19 F auszuloeten, der Tastenknoepf auszutauschen und der Tastenschalter wieder einzuloeten.

**1.2.2. Parallelfuehrung**
- Herausnehmen des Fuehrungsbuegels
- Abziehen des Tastenknoepfes
- Ausloeten der Fuehrungsbausteine
Der Einbau erfolgt in umgekehrter Reihenfolge. Bei Lockerung der Betaetigungsfuehrung im Tastenknoepf ist ein neuer Tastenknoepf mit neuen Betaetigungsfuehrungen einzusetzen.

**1.2.3. Abdeckbaustein**
Das Herausnehmen der Abdeckbausteine erfolgt mit der Hand, indem der Baustein in Spaltenrichtung gekippt und dann herausgenommen wird. Sollte das Kippen in Spaltenrichtung behindert sein, ist vorher freier Raum zu schaffen. Die Montage des Abdeckbausteines erfolgt durch Eindruecken, wobei dieser im Montagerahmen einrastet.

**1.2.4. Baustein mit Lichtemitterdiode bzw. LED komplett**
Die Reparatur erfolgt durch Ausloeten des defekten Bausteines bzw. der LED komplett und Einsatz eines bzw. einer neuen unter Beachtung der Lichtstaerke im visuellen Vergleich mit gleichartigen Bausteinen der Tastatur.

**1.2.5. Tastaturcode**
Der Tastaturcode ist festgelegt in der Fertigungsvorschrift fuer den Schaltkreis Y716-IZZ (ZZ ≙ Variantenkennzeichen) und ist grundsaetzlich nicht zu veraendern. Eine Codeaenderung der Tastatur ist durch Wechseln des Speicherschaltkreises moeglich. Nur im Ausnahmefall duerfen Bruecken veraendert werden.

**1.2.6. Schaltung**
Die Schaltung ist fuer jede Variante im Bestueckungsplan festgelegt und grundsaetzlich nicht zu veraendern.

**1.2.7. Wechseln von defekten Bauelementen**
Die Bauelemente sind auszuloeten und neue einzusetzen.
Faellt ein Tastenschalter TSH 19 F aus, so ist dieser komplett zu wechseln.
Bei Tastenschalter TSH 19 F, LED-Bausteinen und LED-komplett muss die Plusmarkierung in Gebrauchslage der Tastaturen unten sein.
Der Austausch des Schaltkreises MA 7805 erfolgt komplett als Baugruppe (Schaltkreis, Kuehlkoerper und Loetoese vernietet). Ist ein Austausch als Baugruppe nicht moeglich, sind die Hohlniete aufzubohren. Das Befestigen des neuen Schaltkreises mit dem Kuehlkoerper erfolgt mit zwei Zylinderschrauben BM 3x10, Scheiben 3,2 und Federring 3 (Schraubenkopf auf Leiterplattenseite).
Vor dem Einloeten des Schaltkreises sind seine Anschluesse auf 6 mm von der Gehaeuseunterkante aus gesehen zu kuerzen.
Bei defekter Leiterplatte (z.B. Bruch) oder defektem Montagerahmen ist eine neue Tastatur K 7637 ohne Auftischgehaeuse zu verwenden.
Bei Reparaturen darf die Summe der zusaetzlichen Verbindungen, die sich aus fehlenden oder unterbrochenen Leiterbildelementen sowie konstruktiven Aenderungen ergibt, max. 2 % betragen. Dabei ist eine max. Bauelementehoehe L-seitig von 2,5 mm nicht zu ueberschreiten.
Die Muttern des Montagerahmens duerfen nicht geloest werden!

**1.2.8. Tastaturgehaeuse**
Bei defektem Tastaturgehaeuse ist die Baugruppe Tastaturverkleidung komplett auszuwechseln. Durch Oeffnen des Gehaeusebodens und Loesen der Befestigungswinkel wird die Tastatur aus dem Gehaeuse genommen. Um ein Verbiegen der Bauelementeanschluesse zu verhindern, ist die Tastatur auf einer geeigneten Unterlage (z.B. Schaumstoff) abzusetzen.

**1.2.9. Anschlusskabel**
Ein Auswechseln des Anschlusskabels erfolgt als Baugruppe "Kabel komplett". Ist diese Baugruppe nicht vorhanden, kann dem Kabel analoge Plastschlauchleitung HYY 4x1x0,14 mm² bzw. HYY 10x1x0,14 mm² Verwendung finden. Das Oeffnen des Gehaeuses erfolgt dabei entsprechend Pkt. 1.2.8. Das defekte Kabel wird von den Steckloetoesen und aus der Leiterplatte geloetet. Die Montage erfolgt sinngemaess unter Beachtung der Kontaktbelegung in umgekehrter Reihenfolge.

#### 2. Wartungsvorschrift
Die Tastatur ist wartungsfrei und im Dauerbetrieb einsetzbar.

**robotron**

VEB Robotron
Buchungsmaschinenwerk
Karl-Marx-Stadt
Annaberger Straße 93
PSF 129
Karl-Marx-Stadt DDR-9010

Exporteur:
Robotron - Export/Import
Volkseigener Außenhandelsbetrieb der Deutschen Demokratischen Republik
Allee der Kosmonauten 24
PSF 11
Berlin DDR-1140

Kv 1085/87 V 7 1 1047 N3

1.62.540027.3 (GER)
833.53.01.003
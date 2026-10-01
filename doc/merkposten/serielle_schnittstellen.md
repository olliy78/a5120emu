<!-- Angelegt 2026-10-01 (AP-S8, doc/design/19_serielle_schnittstellen.md §12).  Diese
     Datei gilt WIE CLAUDE.md, sobald an den seriellen Schnittstellen gearbeitet wird —
     sie ist nur nicht in jeder Anfrage geladen.  Begruendung: doc/merkposten/README.md -->

# Serielle Schnittstellen nach außen — Merkposten

Die seriellen Kanäle der Schnittstellenkarten (A5120: ASS K8025.50 — DFÜ/V.24, DFÜ/IFSS,
Drucker; K8915: ATS K7028.30 — Drucker/IFSS1, V.24, DFÜ/IFSS2) sind über **Telnet**, **RFC 2217**
(jeweils Client oder Server) oder als **Datei** (nur Senden) nach außen geführt. Die
Tastatur bleibt fest verdrahtet. Entwurf, Belegung, Stand der APs:
**`doc/design/19_serielle_schnittstellen.md`** (§12.1); Kartenbefunde in
`doc/design/06_k8025_ass.md` und `16_k8915.md` §3.2; Bedienung im Handbuch
(`app/help/handbuch.md`, „Schnittstellen").

```
app/ui/serial_widget.py   Reiter „Schnittstellen" im Einstellungen-Kasten (AP-S10), Statuszeile (SeriellFeld), Konfiguration
app/core_binding/k1520.py K1520Emulator.serial_* (Datenklassen SerialInfo/Konfig/Status)
core/api/k1520_api.*      k1520_serial_*  (K1520SerInfo/Konfig/Status, groesse-Regel)
core/serial/hub.*         SerialHub: einer je Maschine, I/O-Faden, Transporte
core/serial/wandler.*     Wandler: Maschinenzeit-Seite, Puffer, Leitungen, Loop
core/serial/{telnet,rfc2217}_codec.*   reine Codecs ohne Socket
core/serial/net/          Socket/Adresse (POSIX + Winsock2), Portsuche, Wecker
core/cards/k8025, k7028   je Schnittstelle ein SerialAnschluss (Kartenlogik dort)
```

Maschinen-API: `K1520Machine::serialHub()` (Index = Anmeldereihenfolge = C-ABI-Index),
`serielleAnschluesse()`, `festeSchnittstellen()`. Reihenfolge A5120: DFÜ/V.24, DFÜ/IFSS,
Drucker; K8915: Drucker/IFSS1 (X3, SIO1-B), V.24 (X4, SIO1-A), DFÜ/IFSS2 (X5, SIO2-A) —
nach den Steckern am Gerät (AP-S12; bis dahin „V.24, IFS 1, IFS 2" in SIO-Reihenfolge).
Konfigurationen sind über den NAMEN verschlüsselt; alte Namen bildet
`Programmprofil.alte_schnittstellen` beim Laden ab (`app/profil.py`).

## Was man nicht aufweichen darf

- **Maßstab ist die Maschinenzeit.** Der Wandler taktet jedes Zeichen in Maschinentakten
  nach der vom Gast programmierten Baud (SIO-Teiler × CTC-Takte); läuft der Gast 10×,
  ist die Leitung 10× schnell, Unterschiede fangen Puffer auf. Kein Uhrbezug im Wandler.
  Wächter `SerialWandler.SendetImZeichentakt`/`EmpfaengtImZeichentakt`,
  `SerielleKopplung.*` (1× gegen 10×).
- **Verlustfrei durch Rückstau — nie ein Überlauf im SIO.** Alle Puffer begrenzt (je
  4 KiB); voller Empfangspuffer → Socket wird nicht gelesen → TCP-Fenster zu; voller
  Sendepuffer → `senderNimm` unterbleibt → der Gast wartet. Bewusste Abweichung vom
  Gerät. Wächter `SerialWandler.LangsamerGastBekommtNieEinenUeberlauf`,
  `…RueckstauBeimSendenHaeltDenGastAn`, `…RueckstauBeimEmpfangenOhneUeberlauf`,
  `SerielleKopplung.*` (64 KiB, Muster Byte für Byte geprüft); bricht die Gegenseite
  mitten im Rückstau ab (FIN), kommt trotzdem alles an, erst danach lauscht der Server
  wieder — `SerialHubGegenseite.AbbruchImRueckstauVerliertNichtsUndDerServerLauschtWeiter`,
  `…ResetImRueckstauTrenntSauber` (RST).
- **Der Gast ist maßgeblich (Leitsatz 4).** Baud/Bits/Parität/Stopp stehen in SIO/CTC;
  RFC 2217 *meldet* sie, eine Anfrage der Gegenseite ändert sie **nie** und wird mit
  dem Gastwert beantwortet, ein Unterschied nur angezeigt (`baud_abweichend`). pyserial
  lehnt daraufhin ab (`ValueError: remote rejected value`) — das ist richtig so.
  Wächter `SerialHub.Rfc2217RundlaufMitNullmodemLeitungenUndBaudhinweis`,
  `SerielleKopplung.UnterschiedlicheBaudWirdAngezeigt`,
  `py_serial_pyserial::test_baud_der_gast_ist_massgeblich`,
  `SerialHubGegenseite.Rfc2217ServerBeantwortetJedenBefehlMitDemGastwert` (jeder
  COM-PORT-Befehl einzeln über einen rohen Client, auch SUSPEND/RESUME, PURGE, Break).
- **Kein Netz im Emulationsfaden, die Karte kennt kein Netz.** Sockets, Namensauflösung,
  Dateischreiben nur im I/O-Faden des Hubs (je Client-Versuch ein abgekoppelter Faden,
  `getaddrinfo` ist nicht abbrechbar). Der Emulationsfaden nimmt nur den Wandler-Mutex;
  Sperrreihenfolge immer Hub → Wandler. Die Kartenlogik (/CTSA = V106 ∧ V107 usw.)
  steckt in der Karte, nicht im Wandler (K7028: siehe unten). Wächter
  `SerialHubNebenlaeufig.StatusAusDrittemFadenWaehrendAufUndAbbau` (in AP-T1b mit
  `-fsanitize=thread` gefahren: ohne Befund).
- **RTS-Halt erst, nachdem der Gast RTS einmal gesetzt hat.** CP/A und SCPX am A5120
  setzen RTS auf A33-A nie, SCPX 8915 schreibt WR5 = 68H — wörtlich genommen empfinge so
  ein Gast nie etwas. Zurückgesetzt bei Anbinden, Abbinden, Maschinen-Reset. Danach gilt
  der Halt **sofort** (auch für das, was schon unterwegs ist): ein Gast, der RTS
  weggenommen hat, bekommt auch das Byte nicht, mit dem man ihn bitten wollte, es
  wieder zu setzen. Wächter `SerialWandler.RtsNieGesetztHaeltNicht`,
  `…GastResetLoestXoffUndRtsHalt`,
  `…RtsWegHaeltDenEmpfangAnNurBeiV24`, `py_serial_pyserial::test_steuerleitungen_nullmodem`.
- **Nullmodem-Kreuzung nur in der Rolle Server.** Client-RTS → unser CTS, Client-DTR →
  DSR + DCD; unser RTS → NOTIFY CTS, unser DTR → DSR + CD. Rolle Client ist gerade
  belegt (wir = DEE am fernen COM-Port, `ser2net`). Zwei Emulatoren (Server + Client)
  ergeben so ein richtiges Nullmodemkabel. Fernleitungen gelten als aktiv bis zum ersten
  SET-CONTROL bzw. NOTIFY-MODEMSTATE. Wächter
  `SerialWandler.Rfc2217ServerKreuztDieLeitungenWieEinNullmodem`,
  `…Rfc2217ClientBelegtDieLeitungenGerade`, `SerielleKopplung.Rfc2217RtsCts_*`.
- **„XON/XOFF beachten" hält den EIGENEN Empfang an** (sendet der eigene Gast 13H, stellt
  der Wandler bis 11H nichts zu; das Zeichen geht trotzdem hinaus). Für Binärdaten aus.
  Wächter `SerialWandler.XoffHaeltDenEmpfangAnBisXon`, `SerielleKopplung.TelnetXonXoff_*`.
- **Loop und Verbindung schließen sich aus.** Loop setzen beendet eine aktive Verbindung,
  Start ist bei Loop gesperrt. K8915 startet mit Loop an (der ROM-Selbsttest braucht das
  Echo, das BIOS nicht) — `serial_start` liefert dort `false`, bis der Loop aus ist.
  Wächter `SerialHub.LoopBeendetDieVerbindungUndSperrtDenStart`,
  `K8915Seriell.BiosLaeuftOhneLoopWeiter`, `test_button_is_locked_while_loop_is_set_and_says_why`.
- **Datei meldet VERBUNDEN.** Die Statuszeile schließt Datei über `betriebsart` aus;
  wer neu auf `Zustand::Verbunden` prüft, muss das wissen. Beim Wiederaufnehmen wird
  angehängt, nicht überschrieben. Wächter `SerialDatei.SchreibtUeberschreibtUndHaengtBeimWiederaufnehmenAn`;
  ein Schreibfehler beim Beenden bleibt als FEHLER stehen
  (`SerialDatei.SchreibfehlerBeimBeendenBleibtSichtbar`).
- **Portwahl:** Start von Hand sucht ab dem eingestellten Port aufwärts; die
  Wiederaufnahme beim Programmstart startet NUR auf dem eingestellten Port und trägt sonst
  einen Vorschlag ein (ein Gegenüber, das fest auf 5000 verbindet, soll keinen fremden
  Emulator erreichen). Gebunden wird Dual-Stack auf allen Schnittstellen — der Gast ist
  **gewollt** im LAN erreichbar. Wächter
  `SerialHub.BelegterPortVonHandSuchtWeiterAutomatischKommtNurEinVorschlag`,
  `test_resume_with_a_busy_port_does_not_start_and_proposes_another`.
- **Server: eine Verbindung zur Zeit**, jede weitere wird angenommen und geschlossen
  (Telnet: Zeile „belegt"), das Lauschen bleibt. Client: Dauerversuch alle 1000 ms Uhrzeit
  bis „Trennen". Wächter `SerialClientDauerversuch.*`.
- **Namen und Fähigkeiten liefert der Kern** — kein `if machine == …` in `app/`
  (`test_a_block_per_program_but_no_machine_specific_names_in_the_module`). Kein
  Tastenkürzel im Reiter (Kürzeltabelle des Handbuchs ist ein Vertrag).
- **C-ABI:** Strukturen mit `groesse` vorn, neue Felder nur hinten; `configure` weist
  Port 0 ab (Port 0 = „vom System" gibt es nur in C++-Tests). `test_c_api.py` vergleicht
  Felder und Aufzählungen mechanisch.
- **Alter Unterbau** `k1520_serial_set_rx_cb`/`k1520_serial_send`: DFU/PRINTER → A5120
  DFÜ/V.24 bzw. Drucker, K8915 DFÜ/IFSS2 (SIO2-A) bzw. Drucker/IFSS1 (SIO1-B); der Rückruf ist
  `void (*)(void* ctx, uint8_t byte)` — **Kontext zuerst** (die ctypes-Erklärung stand
  bis AP-T1b vertauscht). Wächter `py_serial_api::test_old_callback_and_send_go_through_the_guest_and_come_back`
  (Echo-Gast, Kontextzeiger, Belegung durch einen Transport, Abmelden),
  `…test_k8915_old_callback_pulls_the_loop_of_its_own_channel`.

## SERTEST — Prüfprogramm für Gerät und Emulator (Entwurf 19 §14)

`tools/sertest/` (`src/sertest.mac`, `build.py`, eingecheckte `sertest.com`, README mit
Bedienung, Kabelbelegung und **Checkliste der Geräteprüfung**). Z80 unter CP/A und
SCPX 8915: Prüfstecker (DATEN-LOOP, LEITUNGEN-LOOP) und Test gegen eine zweite Maschine
(LEITUNGEN, ECHO, FLUSS-HW, FLUSS-XON). Wächter `Sertest.*` (eine Maschine, Loop),
`SertestKopplung.*` (zwei Maschinen über RFC 2217; ein Fall in `tools/dev.sh test`, die
übrigen in `test-format`), `cli_sertest_com_passt_zur_quelle` (`build.py --check`, ohne
CPA_Workbench übersprungen).

- **Ergebniszeilen sind ein Vertrag** (`SERTEST <name> <TEIL>: OK|FEHLER …|ENTFAELLT`,
  `SERTEST ENDE …`); gelesen von `SertestProtokoll` (`tests/system/sertest_hilfen.h`), das
  eine Zeile erst nach 500 000 Takten ununterbrochen im Bild gelten lässt — zeichenweise
  Ausgabe und Rollen erzeugen zerrissene Zwischenbilder.
- **`sertest.com` ist eingecheckt** (die CI hat M80/LINKMT nicht): nach jeder Änderung an
  der Quelle `python3 tools/sertest/build.py` und die `.com` mitcommitten.
- **K7028 /CTSA = V107 ∧ (¬RTSA ∨ V106), /DCDA = V109** (Stromlaufplan, §14.5) — anders als
  K8025 (CTS = V106 ∧ V107). RTSA ist der EIGENE Ausgang: `K7028::bildeCtsA` läuft auch nach
  jedem Schreiben in SIO 1. Mit Loop CTS = DTR, RTS am Prüfstecker unsichtbar. Darum
  spiegelt die K8915-Gegenstelle „CTS bei gesetztem RTS" per kurzer RTS-Probe. Wächter
  `K7028Seriell.CtsANachPlanlogik`, `…LoopLiefertCtsGleichDtr`.
- **`Z80SIO`-Interrupts nach Datenblatt** (ST3, `test_sio.cpp`): RR2 B **lesen quittiert
  nicht** (`rr2Vektor()`); bei „jedes Zeichen" bleibt die Anforderung stehen, solange der
  FIFO Zeichen hält; Überlauf = RR1 **D5**.
- **Jede Karte reicht `onRETI` an ihre Bausteine weiter** — der K8025 fehlte das bis ST5
  (ein quittierter DFÜ-Interrupt blieb ewig „under service"; CP/A pollt, darum unbemerkt).
  Interruptempfang mit **mehr Zeichen als FIFO + 1** prüfen. Wächter
  `K8025.RetiGibtDenSioWiederFrei`, `Sertest.*EmpfaengtImInterrupt*` (8 Zeichen).
  Offen: RETI bei anstehendem Interrupt weiter oben in der Kette (§14.10 Punkt 6).
- **FLUSS-Teile prüfen die Bremse, nicht nur die Daten:** ohne Beobachtung blieben sie auch
  ohne Auto Enables grün (der Rückstau landet verlustfrei in den 4-KiB-Puffern).
  `SertestKopplung.*` sieht deshalb dem Wandler des Testers zu (CTS aus ⇒ 0 Bytes
  abgegeben; Gegenprobe ohne Auto Enables: 1361) und dem der Gegenstelle (`xoffHalt`).
  „XON/XOFF beachten" am Wandler der Gegenstelle **nur** während Abschnitt X — ECHO und
  FLUSS-HW übertragen 13H als Daten.
- **Offene Gerätefragen** (X14, P184-Polarität, 40H–43H am A5120, freie Vektoren, Kabel,
  RTS-Probe) stehen in Entwurf 19 **§14.10** und gesammelt mit den übrigen Anwenderfragen in
  `doc/design/16_k8915.md` **§6 Punkt 24**. Befund dort UND in §14.10 nachtragen.
- **Zwei Maschinen in einem Test** (`test_sertest_kopplung.cpp`):
  in gleichen Scheiben abwechselnd laufen lassen und auf ein Vielfaches der Echtzeit
  drosseln — die Steuerleitungen laufen über die I/O-Fäden (Uhrzeit), nicht durch den
  Rückstau; ungedrosselt sind Fristen von 2 s Maschinenzeit unter `ctest -j` zu kurz.
- **Ohne Gegenstelle endet `T n /A` mit `SERTEST ENDE FEHLER`** — das ist richtig
  (K8915 mit Loop: die eigene Ankündigung kommt zurück und gilt nicht).

## Tests schreiben

- **Nie feste Ports** (`ctest -j`): Port 0 in C++ (`SerialKonfig::port = 0`, tatsächlicher
  Port aus `status().port_aktiv`), in Python ein freier Port vom System. Nur Loopback.
- **Einen Gast zum Senden bringen:** C++ — Z80-Programm per `memWriteDebug` in den RAM,
  `cpuDebug().PC` setzen, kein Disketten-Boot nötig (`test_serielle_kopplung.cpp` hat einen
  Kleinstassembler mit Marken; ZRE-CTC K0 = Port 0CH, Steuerwort 05H + ZK 1 → 9600 Bd bei
  SIO ×16). Python hat keinen PC-Zugriff: CP/A bis `A>` booten und CONST/CONIN der
  BIOS-Sprungleiste (`[0001H] + 3` / `+ 6`) auf das Programm biegen, eine Taste
  drücken — fertig als Echo-Gast in `tests/python/serial_gast.py`. **Achtung:** bei „Bitte Uhrzeit eingeben!" ist die
  Seite 0 noch leer (`[0001H] = 0`) — erst am Prompt patchen.
- **1× gegen 10×** abwechselnd in einem Faden (je Runde 1 ms gegen 10 ms Maschinenzeit),
  nicht mit Uhrdrosselung — unter Last verlöre die das Verhältnis.
- **Fenster in GUI-Tests mit `_zu()` schließen** (`test_serial_gui.py`): sonst laufen
  Laufwerks-Zeitgeber und Fokusfilter weiter und der Prozess wird mit jedem Fenster
  langsamer.
- **Eine benehmende Gegenseite prüft wenig:** Fehlerwege (zerrissene IAC, Abbruch im
  Rückstau, einzelne COM-PORT-Befehle) mit einem rohen `net::Socket` im Testfaden
  (`RohClient` in `test_serial_hub.cpp`), nicht Hub gegen Hub.
- `py_serial_pyserial` wird ohne `pyserial` übersprungen (es steht in
  `requirements-dev.txt`, ist keine Laufzeitabhängigkeit).

## Gegenstellen (Handtest 2026-10-01)

- **`telnet 127.0.0.1 <port>`** gegen einen Telnet-Server: Echo-Gast antwortet,
  BINARY wird ausgehandelt (kein CR NUL). Ein zweites `telnet` bekommt „belegt".
- **Zwei Emulatoren:** einer Server, einer Client, gleiche Betriebsart — über die
  Python-Bindung (zwei Prozesse, RFC 2217) und über zwei `app/main.py`
  (A5120-Server ↔ K8915-Client, Wiederaufnahme aus der Konfiguration) erprobt.
- **`socat`** spricht kein Telnet: gegen einen *Telnet*-Server sieht man zu Beginn die
  Verhandlung (`FF FB 01 FF FB 03 FF FB 00 FF FD 00`) und, weil BINARY nie zustande
  kommt, CR als `CR NUL`. Erwartet (§7.4), kein Fehler. Brücke zu einem Pseudo-Terminal:
  `socat TCP:127.0.0.1:5000 PTY,link=/tmp/ttyA5120,raw,echo=0`.
- **pyserial:** `serial.serial_for_url("rfc2217://127.0.0.1:5000", baudrate=<Gastbaud>)`
  — mit einer anderen Baud scheitert schon das Öffnen (Leitsatz 4).
- **`ser2net`** (nicht im Testumfang, nicht installiert): echtes Gerät am Host-COM-Port,
  Emulator als **RFC-2217-Client**. ser2net-4-YAML etwa
  `connection: &a5120 {accepter: telnet(rfc2217),tcp,3000, connector: serialdev,/dev/ttyUSB0,9600n81,local}`;
  Leitungen kommen gerade an (Rolle Client). Unter Windows `com0com` + `hub4com` mit
  RFC 2217.

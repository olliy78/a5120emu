# Feinentwurf 19: Serielle Schnittstellen nach außen (Telnet / RFC 2217 / Datei)

**Stand:** 2026-10-01, **abgeschlossen** — S1–S12, T1a, T1b erledigt (§12.1); Bildschirm- und
Geräteprüfung durch den Anwender bestanden (2026-10-01). **Neu:** Testprogramm `SERTEST.COM`
(§14, AP-ST1 … ST7) — spezifiziert, offen.
**Gilt für:** A5120 (K8025.50) und K8915 (ATS K7028.30), beide Programme (`a5120emu`, `k8915emu`).
**Bezug:** `doc/design/06_k8025_ass.md`, `doc/design/16_k8915.md` §3.2/§6.6/§6.10,
`doc/design/10_c_api.md`, `doc/design/11_python_app.md` §10,
`doc/trascripted/Anschlußsteuerung K 8025.50 und K 8025.80.md`.
**Zweck dieses Dokuments:** Vorlage für Agenten, die das Feature in Arbeitspaketen (§12)
umsetzen. Was mit **[?]** markiert ist, muss das jeweilige AP vor dem Code klären.

---

## 1. Ziel

Die seriellen Schnittstellen der Schnittstellenkarten werden **praktisch nutzbar**: ein
Gastsystem kann über eine TCP-Verbindung mit einem Terminalprogramm, einem zweiten
Emulator, einem echten seriellen Gerät (über einen RFC-2217-Server wie `ser2net`) oder einer
Datei (Druckerausgabe) Daten austauschen.

- Protokolle **Telnet** (RFC 854/856/858, nur Nutzdaten) und **RFC 2217** (Telnet +
  COM-PORT-OPTION, mit Steuerleitungen und Leitungsparametern), jeweils als **Client und
  Server**, dazu die Betriebsart **Datei** (nur Senden).
- Umsetzung in **C++ im Kern** (`libk1520core`), **plattformunabhängig** (POSIX-Sockets und
  Winsock2 hinter einer dünnen Schicht, `core/util/os_compat.h`).
- Bedienung in der Oberfläche beider Emulatoren über einen Kasten **„Schnittstellen"**, je
  Schnittstelle ein kompakter Block (wie im Laufwerkskasten).
- **Alles zur Laufzeit** — kein Neustart des Gastsystems für irgendeine Einstellung.

## 2. Leitsätze (nicht aufweichen)

1. **Maßstab ist die Maschinenzeit, nicht die Uhr.** Der Wandler zwischen SIO und Socket
   taktet jedes Zeichen in *Maschinentakten* nach der Baudrate, die der Gast in CTC und SIO
   programmiert hat. Läuft der Gast zehnfach schnell, ist die Schnittstelle in der echten
   Welt zehnfach schnell. Unterschiede zwischen den Enden fangen **Puffer** auf.
2. **Verlustfrei durch Rückstau.** Alle Puffer sind begrenzt. Ist ein Puffer voll, wartet
   die Stufe davor: Gegenstelle → TCP-Fenster → Empfangspuffer → SIO-Empfänger; Gast-Sender →
   SIO-Sender → Sendepuffer → TCP. **Der Wandler erzeugt nie einen Überlauf** im SIO — eine
   bewusste Abweichung vom Gerät, das bei zu langsamem Lesen Zeichen verliert.
3. **Unterschiedliche Baudraten an beiden Enden funktionieren trotzdem** (die Verbindung ist
   byteweise, nicht bitweise). Bei RFC 2217 wird der Unterschied **angezeigt**, nicht
   verhindert. Parität/Rahmenfehler werden nicht nachgebildet.
4. **Der Gast ist maßgeblich.** Die Leitungsparameter (Baud, Bits, Parität, Stoppbits)
   stehen in den SIO-/CTC-Registern. RFC 2217 *meldet* sie der Gegenstelle; eine Anfrage
   der Gegenstelle ändert sie **nie**, sie wird mit dem tatsächlichen Wert beantwortet.
5. **Kein Netzzugriff im Emulationsfaden.** Sockets, Namensauflösung und Dateischreiben
   laufen in einem eigenen Faden; der Lauf der Maschine blockiert nie.
6. **Die Karte kennt kein Netz.** Karten stellen je Schnittstelle einen *Anschluss* bereit
   (Bytes, Steuerleitungen, Leitungsparameter); was daran hängt, entscheidet eine Schicht
   darüber (§5). Gleiches Prinzip wie beim Greaseweazle (`doc/design/14_physische_diskette.md`).
7. **Die Tastatur ist fest verdrahtet** und erscheint nicht als einstellbare Schnittstelle.
8. **Namen und Fähigkeiten der Schnittstellen liefert der Kern** (C-ABI), nicht die
   Oberfläche — kein `if machine == …` in `app/` (vgl. `app/profil.py`).

## 3. Schnittstellen je Maschine

### 3.1 A5120 — ASS K8025.50

Laut Kartendokumentation (§2.3, §5 der Transkription): SIO A33 Kanal A = V.24 (X6),
Kanal B = IFSS DFÜ (X5); SIO A32 Kanal A = Zusatzdrucker (X4), Kanal B = Hauptdrucker (X3).
Im Emulator trägt SIO A32 Kanal A die **Tastatur K7637** (bestehende Verdrahtung, bleibt).

| Name (UI) | Stecker | SIO / Kanal | Ports | Art | Steuerleitungen | Takt (Brücke) |
|-----------|---------|-------------|-------|-----|-----------------|---------------|
| **DFÜ/V.24** | X6 | A33 / A | 50H/51H | V.24 | ja (s. u.) | W1:7: ZRE-CTC K0 (gezeichnet) / CTC A34 K2 |
| **DFÜ/IFSS** | X5 | A33 / B | 52H/53H | IFSS | nein | X7–X8: ZRE-CTC K0 (gezeichnet) / X8–X9: CTC A34 K1 |
| **Drucker** | X3 | A32 / B | 5EH/5FH | IFSS | nein | fest CTC A34 K0 |
| Tastatur K7637 | X4 | A32 / A | 5CH/5DH | IFSS | — | fest verdrahtet, **nicht einstellbar** |

Hinweis: `core/cards/k8025/k8025.h` nennt für DFÜ „connector X1" und führt Kanal B der A33 als
„unused" — beides ist nach der Dokumentation falsch und wird in AP-S5 berichtigt (X1/X2 sind
der Rechnerbus).

V.24-Leitungen am SIO A33 Kanal A (Transkription §2.3.2):

| V.24 | Richtung | SIO-Anschluss |
|------|----------|---------------|
| V105 Sendeaufforderung (RTS) | aus | RTSA |
| V108 DEE betriebsbereit (DTR) | aus | DTRA |
| V106 Sendebereitschaft (CTS) | ein | /CTSA = V106 ∧ V107 |
| V107 Betriebsbereitschaft (DSR) | ein | in /CTSA und /DCDA verknüpft, **zusätzlich DCDB** |
| V109 Empfangssignalpegel (DCD) | ein | /DCDA = V109 ∧ V107 |
| V125 Ankommender Ruf (RI) | ein | nur Ferneinschaltung, nicht am SIO → **nicht nachgebildet** |
| V111, V113–V115 | — | Geschwindigkeitswahl / Synchrontakte → **nicht nachgebildet** |

Für die IFSS-Kanäle gibt es keine Steuerleitungen; der Zustand ihrer /CTS- und /DCD-Eingänge
auf der Karte (DCDB = V107, s. o.; CTSB, CTS/DCD der A32) **[?]** klärt AP-S5 am Stromlaufplan
und hält ihn fest, unabhängig von Verbindungen.

Die ZRE-CTC K0 (K2526, Ports 0CH) muss als Taktquelle der K8025 **abfragbar** sein (§6.2);
ob ihr ZC/TO0 im Emulator schon über den Koppelbus zur K8025 geführt ist **[?]**.

### 3.2 K8915 — ATS K7028.30

Namen und Stecker nach der **Gerätebeschriftung** (Anwender, 2026-10-01, AP-S12); die
Kanalzuordnung nach Stromlaufplan 1.45.518732 (`k8915schaltung.pdf` S. 5, 11, 16) und BIOS
(Drucker = SIO1-B). Reihenfolge = C-ABI-Index:

| Index | Name (UI) | Stecker (Gerät) | SIO / Kanal | Ports | Art | Steuerleitungen | Takt |
|-------|-----------|-----------------|-------------|-------|-----|-----------------|------|
| 0 | **Drucker/IFSS1** | X3 | SIO 1 / B | 42H/43H | nur 103/104 — **Drucker des BIOS** („V24 XON/XOFF") | nein | CTC 1 K2 |
| 1 | **V.24** | X4 (am Gerät bestätigt) | SIO 1 / A | 40H/41H | volle V.24 (Empfänger D17, Treiber D14) | ja | über Multiplexer D13; nachgebildet CTC 1 K0 |
| 2 | **DFÜ/IFSS2** | X5 | SIO 2 / A | 50H/51H | 103/104 + die IFSS-Stromschleife der Karte | nein | CTC 2 K0 |
| — | Tastatur K7672 | (Karte X6?) | SIO 2 / B | 52H/53H | — | — | fest verdrahtet, **nicht einstellbar** |

**Widerspruch Plan ↔ Gerät (AP-S12):** auf der *Karte* liegt die volle V.24 an X3 und
SIO1-B (TxDB über X16:1–3, D12:02, D14:01) an X4; das Gerät beschriftet X3 als
„Drucker/IFSS1". Wahrscheinlich nummeriert die Rückwand ihre Buchsen unabhängig von der
Karte (X5 stimmt in beiden Zählungen). **Für die Namen gilt die Gerätebeschriftung.** SIO1-B
hat auf der Karte keine eigene Stromschleife (die einzige liegt an Karten-X5) — „IFSS1" am
Gerät meint daher einen externen Wandler oder nur die Verwendung **[?]**. CTC 1 CLK/TRG0–2
liegen auf Masse (reine Zeitgeber); CTSB/DCDB nur über Wickelbrücke X15:3/4 (offen =
inaktiv), RTSB/DTRB an keinem Treiber; alle drei Kanäle fest getaktet (keine Taktquellenwahl).

Frühere Namen „V.24"/„IFS 1"/„IFS 2" (S5, 2026-09-30): alte `k8915emu.yaml` werden beim
Laden über `Programmprofil.alte_schnittstellen` (`app/profil.py`) abgebildet — „IFS 1" →
„Drucker/IFSS1", „IFS 2" → „DFÜ/IFSS2". Die alte Verdrahtung `K8915Machine` (Drucker =
SIO1-B, DFÜ = SIO2-A) geht in diesem Modell auf; `Config::pruefstecker` ist die Vorgabe des
**Rx/Tx-Loop** (§4).

## 4. Einstellungen je Schnittstelle

| Einstellung | Werte | Vorgabe | Wirkung / Hinweis |
|-------------|-------|---------|-------------------|
| **Betriebsart** | `Telnet`, `RFC2217`, `Datei` | `Telnet` | Datei: Dateiauswahl erscheint sofort beim Umschalten |
| **Rolle** | `Client`, `Server` | `Server` | nur Telnet/RFC2217 |
| **Host/IP** | Text | `127.0.0.1` | nur Client; im Server deaktiviert; Klassifikation §7.3 |
| **Port** | 1–65535 | `5000` | Server: bei Belegung nächster freier darüber (§7.2) |
| **Rx/Tx-Loop** | an/aus | aus (K8915: s. u.) | Prüfstecker: TxD→RxD am Stecker; siehe §6.5 |
| **RTS/CTS-Brücke** | an/aus | aus | nur V.24: RTS→CTS am Stecker (und DTR→DSR/DCD); §6.4 |
| **XON/XOFF beachten** | an/aus | aus | Wandler hält den Empfang an, solange der Gast XOFF gesendet hat; §6.3 |
| **Taktquelle** | je Karte | Brücke „gezeichnet" | nur wo es eine Brücke gibt (K8025 V.24, IFSS DFÜ) |
| Datei | Pfad | — | nur Betriebsart Datei |

„XON/XOFF beachten" steht in der Aufgabenstellung nicht ausdrücklich, ist aber nötig,
damit XON/XOFF bei unterschiedlich schnellen Enden trägt (§6.3); bei Binärübertragungen
muss es aus sein (0x13 im Datenstrom).

K8915-Vorgabe: der ROM-Selbsttest verlangt das Echo auf SIO1-A, SIO1-B, SIO2-A. Die heutige
Vorgabe bleibt Maschinenvorgabe (`K8915Machine::Config::pruefstecker`, **Loop an** für alle
drei) und erscheint in der UI als gesetzter Rx/Tx-Loop. Geklärt in AP-S5: nur der
ROM-Selbsttest braucht das Echo, das BIOS nicht (`K8915Seriell.BiosLaeuftOhneLoopWeiter`) —
der Anwender kann den Loop also nach dem Kaltstart abschalten und verbinden.

**Bewusst nicht einstellbar:** IFSS aktiv/passiv (A61, keine Entsprechung), Adressdekoder
(W1:1–5, X24/X25), Interruptprioritäten (W1:8–11 — verändern die Kette der ganzen Maschine,
gehören nicht in einen Schnittstellenblock), Synchronbetrieb (W1:6, A46), V111 (A61 7–10),
die DIL-Schalter A41 (Baud/Blocklänge für SIOS, liegen weiter in `K8025::A5120Config`).

**Sperren im Betrieb:** Solange eine Schnittstelle aktiv ist (Server lauscht/verbunden,
Client verbunden, Datei offen), sind Betriebsart, Rolle, Host, Port und Datei gesperrt;
Loop, RTS/CTS-Brücke, XON/XOFF und Taktquelle wirken sofort. Beenden/Trennen berührt den
Gast nicht (Leitungen fallen ab wie beim Ziehen des Kabels).

## 5. Aufbau im Kern

```
app/ (GUI)  ── pollt Status, setzt Konfiguration ──►  C-ABI k1520_serial_*   (§8)
                                                          │
core/serial/                                              ▼
  SerialHub        (einer je Maschine) ── besitzt ──► Wandler × N          (§6)
     │ I/O-Faden: poll()/WSAPoll, Namensauflösung,        │ Maschinenzeit: Takten,
     │ Sockets, Datei                                      │ Puffer, Leitungen, Loop
     ▼                                                     ▼
  Transport: TelnetTransport | Rfc2217Transport | DateiTransport | (keiner)
     └─ TelnetCodec / Rfc2217Codec (rein, ohne Socket, einzeln testbar)
  net/Socket, net/Adresse  (POSIX / Winsock2)
                                                          ▲
core/cards/k8025, k7028  ── je Schnittstelle ein SerialAnschluss (Schnittstelle zur SIO)
core/primitives/z80_sio, z80_ctc  ── Leitungsparameter + Steuerleitungen abfragbar (§6.2)
```

Neue Dateien (Vorschlag): `core/serial/{anschluss.h, wandler.{h,cpp}, hub.{h,cpp},
telnet_codec.{h,cpp}, rfc2217_codec.{h,cpp}, transport.{h,cpp}}`,
`core/serial/net/{socket.{h,cpp}, adresse.{h,cpp}}`. Winsock: `WSAStartup` einmal im Hub,
`ws2_32` im MinGW-/MSVC-Zweig des `CMakeLists.txt` linken.

### 5.1 `SerialAnschluss` (Karte → Wandler)

Abstrakte Schnittstelle, die eine Karte je einstellbarer Schnittstelle liefert:

```cpp
struct SerialFormat { uint32_t baud_nenn; uint8_t daten, paritaet /*0 N,1 O,2 E*/;
                      uint8_t stopp_halbe /*2,3,4*/; uint64_t zeichen_takte; bool gueltig; };
struct SerialAnschluss {
    virtual const char* name() const = 0;          // "DFÜ/V.24"
    virtual const char* stecker() const = 0;       // "X6"
    virtual bool v24() const = 0;                  // Steuerleitungen vorhanden
    virtual std::vector<Taktquelle> taktquellen() const = 0; // leer = fest
    virtual SerialFormat format() const = 0;        // aus WR3/4/5 + CTC (§6.2)
    virtual bool senderHatZeichen() const = 0;      // SIO-Tx-Puffer voll
    virtual uint8_t senderNimm() = 0;               // Zeichen verlässt Schieberegister
    virtual bool empfaengerFrei() const = 0;        // SIO-Rx-FIFO hat Platz
    virtual void empfange(uint8_t) = 0;
    virtual bool rts() const; virtual bool dtr() const;      // Ausgänge (nur V.24)
    virtual void setzeEingaenge(bool cts, bool dsr, bool dcd); // Kartenlogik verknüpft
    virtual void breakSenden(bool); virtual bool breakGesendet() const; // optional
};
```

Die Kartenlogik (z. B. /CTSA = V106 ∧ V107) steckt **in der Karte**, nicht im Wandler.

## 6. Der Wandler (Maschinenzeit-Seite)

Je Schnittstelle ein `Wandler`, bedient aus dem Lauf der Maschine
(`A5120Machine::run`/`K8915Machine::run`, dort wo heute `K7028::service()` bzw. der
K8025-Takt laufen) mit der aktuellen Taktzahl.

### 6.1 Puffer und Rückstau

- **Sendepuffer** (Gast → Netz) und **Empfangspuffer** (Netz → Gast): Ringpuffer, je
  4 KiB, zwischen Emulations- und I/O-Faden mit Mutex (oder SPSC lock-free) geteilt.
- **Senden:** Hat die SIO ein Zeichen und ist seit dem letzten *Zeichenende*
  `zeichen_takte` vergangen, nimmt der Wandler es ab (`senderNimm`) — damit wird der
  SIO-Sender erst nach einer Zeichenzeit wieder frei, der Gast sieht TxEmpty in seinem
  Takt. Ist der Sendepuffer voll, nimmt er **nicht** ab → der Gast wartet.
- **Empfangen:** Liegt ein Byte im Empfangspuffer, ist seit der letzten Zustellung
  `zeichen_takte` vergangen, ist `empfaengerFrei()` und kein Halt aktiv (§6.3/6.4), wird
  zugestellt. Ist der Empfangspuffer voll, liest der I/O-Faden den Socket nicht weiter →
  TCP-Fenster schließt → die Gegenseite wartet.
- **Nichts angeschlossen** (inaktiv): gesendete Zeichen verfallen nach je einer
  Zeichenzeit (Kabel ab), Empfang leer, Eingänge CTS/DSR/DCD inaktiv — außer die
  RTS/CTS-Brücke ist gesetzt.
- Ist `format().gueltig == false` (CTC noch nicht programmiert, Fremdtakt), gilt
  9600 Bd 8N1 bei Nenntakt als Ersatz; die UI zeigt dann „—".

### 6.2 Baudrate aus den Registern

- SIO: Teiler ×1/×16/×32/×64 (WR4 D7–6), Stoppbits (WR4 D3–2), Parität (WR4 D1–0),
  Tx-Bits (WR5 D6–5), Rx-Bits (WR3 D7–6). `Z80SIO::Channel` hält das bereits
  (`clock_multiplier`, `tx_bits_per_char`, …); nötig ist ein öffentlicher, konstanter
  Abfrageweg.
- CTC: Betriebsart (Zeitgeber: Vorteiler 16/256 × Zeitkonstante, bezogen auf φ;
  Zähler: Zeitkonstante × Periode des CLK/TRG-Eingangs, bei Kaskaden rekursiv), TC = 0
  bedeutet 256. `Z80CTC` braucht dafür eine Abfrage `teilerTakte(kanal)` → Takte je
  ZC/TO-Impuls oder 0 = unbekannt.
- `zeichen_takte = (1 + daten + (parität?1:0) + stopp) × SIO-Teiler × CTC-Takte je Impuls`
  (1,5 Stoppbits halbzahlig gerechnet).
- `baud_nenn = φ_nenn / (SIO-Teiler × CTC-Takte)`, auf eine Normrate gerundet, wenn sie
  weniger als 2 % abweicht (φ_nenn = Nenntakt der Maschine, 2,4576 MHz). Das ist der Wert
  für Anzeige und RFC 2217 — **unabhängig** von der eingestellten Emulationsgeschwindigkeit.
- Welche CTC-Ausgabe welchen SIO-Kanal taktet, legt die Karte nach Taktquelle fest.

### 6.3 XON/XOFF

In-band, läuft ohne Zutun durch. Problem bei ungleich schnellen Enden: zwischen dem XOFF
des Empfängers und dem Anhalten des Senders liegen Sendepuffer, TCP und Empfangspuffer —
bis zu mehreren KiB, die ein Gast mit 64-Byte-Ringpuffer nicht mehr aufnimmt. Deshalb mit
„XON/XOFF beachten": sendet der **eigene** Gast 13H, stellt der Wandler nichts mehr zu, bis
der Gast 11H sendet (das Zeichen selbst geht trotzdem hinaus). Was unterwegs ist, wartet im
Empfangspuffer — verlustfrei. Auch über Telnet nutzbar.

### 6.4 Steuerleitungen

- **RFC 2217, Rolle Client** (wir = DEE am fernen COM-Port, z. B. `ser2net` mit echtem
  Gerät): Gast-RTS/DTR → `SET-CONTROL` 11/12 bzw. 8/9; `NOTIFY-MODEMSTATE` CTS/DSR/CD →
  `setzeEingaenge`. Gerade Belegung.
- **RFC 2217, Rolle Server** (wir spielen den COM-Port, der Client ist die andere DEE):
  **Nullmodem-Kreuzung** — Client-`SET-CONTROL` RTS → unser CTS, DTR → unser DSR und DCD;
  unser RTS → `NOTIFY-MODEMSTATE` CTS, unser DTR → DSR + CD. So ergeben zwei Emulatoren
  (einer Server, einer Client) ein richtiges Nullmodemkabel.
- **Telnet:** keine Leitungen. Eingänge CTS/DSR/DCD = „verbunden".
- **RTS/CTS-Brücke** (nur V.24) überstimmt: CTS := eigenes RTS, DSR = DCD := eigenes DTR.
- **Halt bei RTS:** nimmt der eigene Gast RTS weg, stellt der Wandler nichts mehr zu
  (sofort, nicht erst nach Zeichen unterwegs) — dasselbe Argument wie §6.3. **Erst, nachdem
  der Gast RTS einmal gesetzt hat** (AP-S5): SCPX 8915 schreibt WR5 = 68H (RTS/DTR aus), CP/A
  und SCPX am A5120 setzen RTS auf A33-A nie — wörtlich genommen empfinge so ein Gast auf
  der V.24 nie etwas. Zurückgesetzt beim Anbinden, Abbinden und Maschinen-Reset
  (`SerialHub::gastZurueckgesetzt`); Wächter `SerialWandler.RtsNieGesetztHaeltNicht`. Gegenüber
  sieht zusätzlich CTS fallen; mit SIO-*Auto Enables* (WR3 D5) hält dessen Sender an —
  `Z80SIO` muss das nachbilden (Sender gibt bei inaktivem CTS nichts ab).
- Änderungen an CTS/DCD lösen im SIO den Ext/Status-Interrupt aus (vorhandene Latches
  `cts_latch`/`dcd_latch`); der Abfrageweg dafür wird in AP-S3 öffentlich.
- Break (optional, niedrige Priorität): Gast WR5 D4 → `SET-CONTROL` 5/6; empfangenes
  Break → RR0 D7.

### 6.5 Rx/Tx-Loop

Entspricht einem gesteckten Prüfstecker: gesendete Zeichen kommen nach einer Zeichenzeit am
selben Kanal an. **Loop und Verbindung schließen sich aus:** Setzen des Loops beendet eine
aktive Verbindung (Server hört auf zu lauschen), der Umschalter ist gesperrt, solange Loop
gesetzt ist. Bei V.24 schließt der Prüfstecker zugleich RTS→CTS und DTR→DSR/DCD. Das
ersetzt die heutige Rückschleife in `K7028::service()`.

### 6.6 Datei

Beim Wählen der Betriebsart erscheint ein Speichern-Dialog (Überschreiben fragt Qt).
„Starten" öffnet die Datei (neu anlegen/überschreiben), jedes Byte wird in seiner
Zeichenzeit geschrieben (der Wandler taktet wie immer; der I/O-Faden puffert und schreibt
spätestens alle 0,5 s bzw. beim Beenden). Empfang bleibt leer, Eingänge wie „verbunden".
Schreibfehler → Zustand Fehler, Text im Block.

## 7. Netz

### 7.1 Faden und Sockets

- Ein I/O-Faden je Maschine (`SerialHub`), nur solange mindestens eine Schnittstelle aktiv
  ist. `poll()` bzw. `WSAPoll()` über alle Sockets plus ein **Weck-Socketpaar** (POSIX
  `socketpair`, Windows: verbundenes Loopback-TCP-Paar; als Helfer in `os_compat`).
- Nicht blockierende Sockets, `TCP_NODELAY` an (Terminalbetrieb).
- Namensauflösung (`getaddrinfo`, blockierend) im I/O-Faden, nicht in der GUI; der Client
  probiert alle gelieferten Adressen der Reihe nach (IPv6 und IPv4).
- **Server:** eine Verbindung zur Zeit. **Ist ein Client verbunden, wird jede weitere
  Anfrage abgelehnt**: angenommen und sofort geschlossen (bei Telnet mit einer kurzen
  Textzeile „belegt"), die bestehende Verbindung bleibt unberührt. Das Lauschen wird dafür
  nicht beendet — sonst könnte ein anderes Programm den Port in der Zwischenzeit belegen.
  Trennt der Client, lauscht der Server weiter.
- **Client — Dauerversuch (festgelegt 2026-09-30):** Nach „Verbinden" ist der Client
  **aktiv**, bis „Trennen" gedrückt wird; nur der Knopf beendet das. Solange er aktiv und
  nicht verbunden ist, versucht er **alle 1000 ms** (Uhrzeit, nicht Maschinenzeit) eine
  Verbindung aufzubauen: nach einem gescheiterten Versuch, nach einem Abbruch und nach
  einem Trennen durch den Server gleichermaßen. Jeder Versuch löst den Namen neu auf
  (DHCP, geänderte Einträge). Ein Versuch, der länger als 1000 ms hängt (nicht
  blockierendes `connect`), wird abgebrochen und zählt als gescheitert.
  Der Knopf zeigt während der Versuche **„Trennen"**; der Block zeigt „verbindet …" mit dem
  letzten Grund (z. B. „Verbindung abgewiesen") und der Zahl der Versuche. Zustand
  `K1520_SER_VERBINDET`; `K1520_SER_FEHLER` gibt es beim Client nur für Dinge, die ein
  neuer Versuch nicht behebt (ungültiger Host).

### 7.2 Portwahl im Server

Binden an Port P; bei `EADDRINUSE` P+1 … bis 65535, danach Fehler. Der **tatsächliche**
Port wird im Block angezeigt („lauscht auf 5001"); das Eingabefeld behält den eingestellten
Wert. POSIX: `SO_REUSEADDR` (sonst schiebt ein alter TIME_WAIT den Port weiter),
Windows: `SO_EXCLUSIVEADDRUSE` (dort bedeutet `SO_REUSEADDR` etwas anderes und würde einen
belegten Port „erfolgreich" doppelt binden).
Diese Portsuche gilt für den **Start von Hand**; beim automatischen Start gilt §7.4a.

**Bindeadresse (festgelegt 2026-09-30):** Dual-Stack auf allen Schnittstellen (`::` mit
`IPV6_V6ONLY=0`, Rückfall `0.0.0.0`). Der Gast ist damit **gewollt** im LAN erreichbar, ohne
Anmeldung; das Handbuch weist darauf hin.

### 7.3 Adressklassifikation (Host-Feld)

Reihenfolge: `inet_pton(AF_INET)` → **IPv4**; sonst `[…]` abstreifen, `%zone` abtrennen,
`inet_pton(AF_INET6)` → **IPv6**; sonst gültiger Hostname nach RFC 1123 (Labels 1–63
Zeichen `[A-Za-z0-9-]`, nicht mit `-` beginnend/endend, gesamt ≤ 253) → **Hostname**;
sonst **ungültig** (Knopf gesperrt). Ergänzt in AP-S1: ist das **letzte Label rein
numerisch**, ist der Name ungültig (RFC 1123 §2.1) — `300.1.1.1`, `1.2.3`, `12345` gelten
damit nicht als Hostname; ein Schlusspunkt (`host.org.`) ist erlaubt, `host:port` ungültig. Die Klassifikation sitzt im Kern
(`k1520_serial_classify_host`), die UI zeigt sie als kleines Etikett neben dem Feld.

### 7.4 Telnet (RFC 854/856/858)

`TelnetCodec`: Zustandsautomat über IAC/SB/SE, verträgt über Pakete zerrissene Sequenzen.
0xFF in den Nutzdaten → `IAC IAC`. Verhandelt werden nur BINARY (0), SGA (3) und ECHO (1):
Server sendet `WILL ECHO`, `WILL SGA`, `WILL BINARY`, `DO BINARY` (Gast macht das Echo,
Terminal im Zeichenbetrieb); Client stimmt diesen zu. Alles andere → `DONT`/`WONT`. Ohne
BINARY: empfangenes `CR NUL` → `CR`, gesendetes `CR` → `CR NUL`. Spricht die Gegenseite
kein Telnet (roher TCP-Strom ohne IAC), funktioniert es trotzdem transparent bis auf 0xFF.

### 7.4a Wiederaufnahme beim Programmstart (festgelegt 2026-09-30)

Der Aktivzustand jeder Schnittstelle wird beim Beenden gespeichert (`aktiv: true`, §9) und
beim nächsten Start **nach dem Einschalten der Maschine** wiederhergestellt:

- **Server:** startet automatisch auf dem **eingestellten** Port. Ist der belegt, startet er
  **nicht**; stattdessen wird der nächste freie Port darüber gesucht (dieselbe Suche wie
  §7.2, aber nur Prüfen, nicht Lauschen), **ins Port-Feld eingetragen** und im Block gemeldet
  („Port 5000 belegt — 5001 ist frei, bitte von Hand starten"). Zustand AUS; Starten von Hand.
  Grund: ein Gegenüber, das fest auf 5000 verbindet, soll nicht stillschweigend einen
  anderen Emulator erreichen oder ins Leere laufen.
  Der eingetragene Port wird erst mit dem nächsten Speichern der Konfiguration dauerhaft.
- **Client:** wird wieder aktiv und geht in den Dauerversuch (§7.1), bis er verbunden ist
  oder „Trennen" gedrückt wird.
- **Datei:** wird wieder geöffnet — **anhängend**, nicht überschreibend (ein Neustart soll
  eine Druckerausgabe nicht löschen); fehlt die Datei, wird sie angelegt.
- **Loop gesetzt** → kein automatischer Start (Loop und Verbindung schließen sich aus, §6.5).

Kern-Unterstützung: `k1520_serial_start_auto(h, i)` — wie `start`, aber ohne Portsuche beim
Binden; bei Belegung liefert sie `false` und im Status `port_vorschlag` (0 = keiner frei).
Die Oberfläche ruft sie aus `_apply_config` (nur wenn der Abschnitt `schnittstellen` da ist).

### 7.5 RFC 2217

`Rfc2217Codec` auf dem TelnetCodec, Option 44 (COM-PORT-OPTION), Client sendet
`WILL 44`, Server `DO 44`; Antworten des Servers = Befehl + 100.

| Befehl | Client sendet | Server |
|--------|---------------|--------|
| 1 SET-BAUDRATE (4 B, Netzreihenfolge) | bei Änderung von `baud_nenn` (entprellt 100 ms Maschinenzeit) | antwortet mit **Gast**-Baud; Abweichung → Hinweis |
| 2/3/4 DATASIZE/PARITY/STOPSIZE | bei Änderung | antwortet mit Gastwert |
| 5 SET-CONTROL | RTS/DTR/Break; Flussart 1/2/3 aus den Wandler-Einstellungen | Leitungen gekreuzt (§6.4); Flussart: Antwort = aktuelle Einstellung |
| 6/7 NOTIFY-LINE/MODEMSTATE | empfängt | sendet bei Änderung, Maske aus 10/11 beachten |
| 8/9 FLOWCONTROL-SUSPEND/RESUME | beachtet (nicht mehr senden) | beachtet |
| 12 PURGE-DATA | — | leert den jeweiligen Wandlerpuffer |
| 0 SIGNATURE | beantwortet mit „k1520emu <Version>" | ebenso |

**Hinweis Baudunterschied:** Client: Antwort des Servers ≠ eigener Wert. Server: Anfrage
des Clients ≠ Gastwert. Anzeige im Block „Gegenseite: 1200 Bd" in Warnfarbe. Bei Telnet
nicht erkennbar — dort keine Anzeige.

## 8. C-ABI (`core/api/k1520_api.h`)

Feste Strukturen mit `groesse`-Feld vorn (ABI-Erweiterung ohne Bruch), Zeichenketten als
feste `char[]`, UTF-8. Kein Rückruf aus dem I/O-Faden nach Python — die GUI **fragt ab**
(im Takt der Statuszeile).

```c
typedef enum { K1520_SER_TELNET=0, K1520_SER_RFC2217=1, K1520_SER_DATEI=2 } K1520SerBetriebsart;
typedef enum { K1520_SER_SERVER=0, K1520_SER_CLIENT=1 } K1520SerRolle;
typedef enum { K1520_SER_AUS=0, K1520_SER_VERBINDET, K1520_SER_LAUSCHT,
               K1520_SER_VERBUNDEN, K1520_SER_FEHLER } K1520SerZustand;
typedef enum { K1520_HOST_UNGUELTIG=0, K1520_HOST_IPV4, K1520_HOST_IPV6,
               K1520_HOST_NAME } K1520HostArt;

typedef struct { uint32_t groesse; char name[32]; char stecker[8]; bool v24;
                 int taktquellen; char taktquelle_name[4][32]; } K1520SerInfo;
typedef struct { uint32_t groesse; int betriebsart, rolle; char host[256]; uint16_t port;
                 bool loop, rtscts_bruecke, xonxoff; int taktquelle; char datei[1024]; } K1520SerKonfig;
typedef struct { uint32_t groesse; int zustand; uint16_t port_aktiv; char gegenstelle[96];
                 char meldung[160]; uint32_t baud_nenn; uint8_t daten, paritaet, stopp_halbe;
                 bool format_gueltig; uint32_t baud_gegenseite; bool baud_abweichend;
                 bool rts, cts, dtr, dsr, dcd; uint64_t bytes_gesendet, bytes_empfangen;
                 uint32_t puffer_senden, puffer_empfangen;
                 uint16_t port_vorschlag; int rolle, betriebsart;
                 uint32_t versuche; /* Client: Versuche seit dem letzten Verbinden */ } K1520SerStatus;

K1520_API int  k1520_serial_count(K1520Handle h);              /* einstellbare */
K1520_API bool k1520_serial_info(K1520Handle h, int i, K1520SerInfo* out);
K1520_API bool k1520_serial_fixed_name(K1520Handle h, int i, char* buf, int n); /* Tastatur-Zeilen */
K1520_API bool k1520_serial_configure(K1520Handle h, int i, const K1520SerKonfig* k);
K1520_API bool k1520_serial_get_config(K1520Handle h, int i, K1520SerKonfig* out); /* ergänzt in S6 */
K1520_API bool k1520_serial_start(K1520Handle h, int i);       /* Server: Portsuche §7.2 */
K1520_API bool k1520_serial_start_auto(K1520Handle h, int i);  /* Wiederaufnahme §7.4a */
K1520_API void k1520_serial_stop(K1520Handle h, int i);
K1520_API bool k1520_serial_status(K1520Handle h, int i, K1520SerStatus* out);
K1520_API int  k1520_serial_classify_host(const char* host);
```

`k1520_serial_configure` während aktivem Betrieb: gesperrte Felder (§4) geändert → `false`,
nichts übernommen. Die alte `k1520_serial_set_rx_cb`/`k1520_serial_send` bleibt als
Unterbau für Tests erhalten; ist ein Transport aktiv, geht sie ins Leere (dokumentieren).
`app/core_binding/k1520.py` bekommt die ctypes-Gegenstücke; `tests/python/test_c_api.py`
prüft Header ↔ Bibliothek ↔ Bindung automatisch mit.

`K1520Machine` (core/machines/machine.h) bekommt `serielleAnschluesse()` (Liste) und den
`SerialHub`; `setDFUECallback`/`setPrinterCallback` werden intern auf Wandler umgestellt.

## 9. Oberfläche

Reiter **„Schnittstellen"** im Einstellungen-Kasten (`app/ui/serial_widget.py`, eingehängt in
`settings_widget.py` zwischen „Laufwerke" und „CRT"; **seit AP-S10** — vorher eigener Dock,
auf Anwenderwunsch verlegt). Format in der üblichen Schreibweise `8N1`/`7E1`/`8O1` mit
erklärendem Tooltip, Leitungen als LEDs (grün aktiv, dunkel inaktiv, Umriss unbekannt), das
Port-Feld zeigt im Betrieb den **tatsächlichen** Port (gespeichert bleibt der eingestellte),
eine Zeile „Gegenseite" mit Baud, Format und — bei V.24 — deren Leitungen (AP-S11).
Blöcke im Stil von `drive_widget.py` (Skizze des ersten Entwurfs):

```
┌ DFÜ/V.24  (X6) ─────────────────────────────── ● verbunden 192.168.1.5:40122 ┐
│ [RFC2217 ▾] [Client ▾]  Host [127.0.0.1          ] IPv4   Port [5000 ]  [ Trennen ] │
│ ☐ Rx/Tx-Loop   ☐ RTS/CTS-Brücke   ☐ XON/XOFF   Takt [ZRE-CTC K0 (W1:7) ▾]          │
│ Gast 9600 Bd 8N1   RTS● CTS● DTR● DSR○ DCD○    Gegenseite 1200 Bd ⚠               │
└──────────────────────────────────────────────────────────────────────────────┘
┌ Drucker  (X3) ────────────────────────────────────────────────── ○ aus ┐
│ [Datei ▾]  druck.txt …                                          [ Starten ] │
│ ☐ Rx/Tx-Loop   ☐ XON/XOFF                                                   │
│ Gast 9600 Bd 7E1                                                            │
└─────────────────────────────────────────────────────────────────────────────┘
  Tastatur K7637 (X4) — fest verdrahtet
```

- Knopf: Server „Starten"/„Beenden", Client „Verbinden"/„Trennen", Datei
  „Starten"/„Beenden". Gesperrt bei Loop oder ungültigem Host.
- Host-Feld im Server deaktiviert (Inhalt bleibt). RTS/CTS-Brücke und Leitungsanzeige nur
  bei V.24; Leitungsanzeige bei Telnet mit Vermerk „nicht übertragen". Rolle/Host/Port bei
  Datei ausgeblendet, stattdessen Dateiname + „…"-Knopf.
- Knopf „Trennen" beim Client, sobald er aktiv ist — auch während er noch versucht (§7.1).
- Zustandspunkt: grau aus, gelb lauscht/verbindet, grün verbunden, rot Fehler;
  Fehlertext als Zeile im Block (kein Meldungsfenster — vgl. `diskNotice`).
- Aktualisierung per `QTimer` (≈ 4 Hz) über `k1520_serial_status`.
- **Statuszeile** (`app/ui/status_bar.py`, festgelegt 2026-09-30) — zwei Felder, die
  jeweils **nur erscheinen, wenn sie etwas zu sagen haben** (ausgeblendet, nicht leer):
  - **Server:** `Telnet/RFC2217 Server Port: 5000, 5001, 5002` — die *tatsächlichen* Ports
    (§7.2) aller Schnittstellen im Zustand LAUSCHT oder VERBUNDEN in Rolle Server,
    aufsteigend nach Schnittstellenreihenfolge. Kein Server aktiv ⇒ Feld weg.
  - **Verbindungen:** `V.24 verbunden, Drucker verbunden` — alle Schnittstellen im Zustand
    VERBUNDEN (Server mit Client wie Client mit Server), in Schnittstellenreihenfolge, mit
    dem Namen aus dem Kern. Getrennte, lauschende und gerade versuchende Schnittstellen
    erscheinen **nicht** — erfolglose Versuche tauchen in der Statuszeile nie auf (nur im
    Block). Keine Verbindung ⇒ Feld weg.
  - Tooltip je Feld: Protokoll und Gegenstelle (`V.24: RFC2217-Client → 10.0.0.5:5000`).
  - Betriebsart **Datei** zählt nicht als Verbindung und erscheint in keinem Feld **[?]**.
  - Quelle ist dieselbe Statusabfrage wie im Dock; beide Programme, gleiche Darstellung
    (kein Profileintrag nötig).
- **Kein Tastenkürzel** (Kürzeltabelle des Handbuchs ist ein Vertrag).
- Handbuch `app/help/handbuch.md`: Abschnitt „Schnittstellen" mit Beispielen
  (`telnet 127.0.0.1 5000`, zwei Emulatoren koppeln, `ser2net`/`socat`, pyserial
  `rfc2217://`).
- **Konfiguration:** Abschnitt `schnittstellen:` in `a5120emu.yaml`/`k8915emu.yaml`,
  Schlüssel = Schnittstellenname aus dem Kern, Inhalt = `K1520SerKonfig`-Felder **plus
  `aktiv`** (Zustand beim Beenden, LAUSCHT/VERBUNDEN/VERBINDET bzw. Datei offen ⇒ `true`).
  Wiederaufnahme beim Start: §7.4a.
  Die Auslieferungsvorgabe (`data/default_config_*.yaml`) trägt keinen Abschnitt
  (fehlend = nicht anfassen, vgl. CLAUDE.md).

## 10. Nicht im Umfang

Synchronbetrieb (SDLC/Bisync) über das Netz, Parität-/Rahmenfehler-Nachbildung,
Überlauf-Nachbildung, V125/Ferneinschaltung, mehrere
Clients je Server, TLS/Anmeldung, echte Host-COM-Ports ohne Umweg (geht über `ser2net`
bzw. `com0com`+`hub4com` mit RFC 2217), Schnittstellen in `boot_trace`/`k1520dbg`
(dort bleibt alles unverbunden), Einbeziehung in Savestates (Verbindungen sind
Host-Ressourcen; Pufferinhalte gehen beim Laden verloren).

## 11. Prüfung

| Ebene | Wächter (Vorschlag) | Was |
|-------|---------------------|-----|
| unit/util | `SerialAdresse.*` | IPv4/IPv6 (auch `[::1]`, `fe80::1%eth0`), Hostnamen, ungültige |
| unit/util | `TelnetCodec.*` | IAC-Verdopplung, zerrissene Sequenzen, Verhandlung, CR NUL |
| unit/util | `Rfc2217Codec.*` | alle Befehle, +100-Antworten, Baud in Netzreihenfolge, Masken |
| unit/primitives | `Z80SIO.Format*`, `Z80SIO.AutoEnables*`, `Z80CTC.TeilerTakte*` | Parameterabfrage, CTS-Halt, Ext/Status-IRQ bei Leitungswechsel |
| unit/util | `SerialWandler.*` (Attrappe statt Socket) | Zeichentakt, Rückstau beidseitig, nie Überlauf, XOFF-Halt, RTS-Halt, Loop, Nullmodem-Kreuzung |
| unit/util | `SerialClientDauerversuch.*` | Server erst nach 2,5 s gestartet → Client verbindet beim nächsten 1000-ms-Takt; Server trennt → Client verbindet erneut; „Trennen" beendet die Versuche sofort; zweiter Client am belegten Server abgewiesen, erster bleibt verbunden |
| unit/util | `SerialNetz.*` | Loopback-Server/Client v4 und v6 (v6 übersprungen, wenn nicht verfügbar), Port belegt → +1 (Hand) bzw. kein Start + Vorschlag (automatisch), `SO_EXCLUSIVEADDRUSE` unter Windows |
| unit/cards | `K8025Seriell.*`, `K7028Seriell.*` | Kanalzuordnung, Taktquelle, V.24-Verknüpfungen, Tastatur unberührt |
| integration | `SerielleKopplung.*` | zwei Maschinen im selben Prozess über echtes TCP, eine mit 1×, eine mit 10× Takt: 64 KiB mit XON/XOFF und mit RTS/CTS verlustfrei, unterschiedliche Baud → Hinweis |
| python | `py_serial_api`, `py_serial_gui` | ctypes-Bindung; Blockzustände, Knopftexte, Host-Feld im Server aus, Klassifikationsetikett, Sperren im Betrieb, Wiederaufnahme (`aktiv` → Server lauscht; Port belegt → nicht gestartet, Vorschlag im Feld; Client geht in den Dauerversuch; Datei anhängend); Statuszeile: Serverfeld mit den tatsächlichen Ports bzw. ausgeblendet, Verbindungsfeld nur mit VERBUNDEN, nie mit versuchenden Clients; Client-Knopf „Trennen" während der Versuche |
| python | `py_serial_pyserial` (übersprungen ohne `pyserial`) | Interop: pyserial `rfc2217://` als Client gegen den Emulator-Server |

**Nie feste Ports in Tests** (parallel `ctest -j`): Port 0 bzw. die +1-Suche benutzen und
den tatsächlichen Port aus dem Status lesen. Die Standardregression muss ohne Netz nach
außen laufen (nur Loopback). `tools/dev.sh win` (wine) für Winsock-Pfad.

## 12. Arbeitspakete

Reihenfolge durch Abhängigkeiten; bauberührende APs **nacheinander** oder je eigenes
Worktree (CLAUDE.md, Bau-Kollision). Jedes AP: Tests grün (`tools/dev.sh test`), dieses
Dokument im AP-Abschnitt nachführen („erledigt JJJJ-MM-TT", Abweichungen), Commit.

| AP | Inhalt | hängt ab von | Umfang |
|----|--------|--------------|--------|
| **S1** | `core/serial/net/`: Socket-Hülle POSIX/Winsock, Weck-Socketpaar, Adressklassifikation, Server-Portsuche, CMake (`ws2_32`) + `SerialAdresse.*`, `SerialNetz.*` | — | M |
| **S2** | `TelnetCodec`, `Rfc2217Codec` (rein, ohne Socket) + Tests | — | M |
| **S3** | `Z80SIO`: Formatabfrage, RTS/DTR-Ausgänge, CTS/DCD-Eingänge mit Ext/Status-IRQ, Auto Enables, Break; `Z80CTC::teilerTakte` + Tests; keine Verhaltensänderung für bestehende Pfade (volle Regression inkl. `test-format`) | — | M |
| **S4** | `SerialAnschluss`, `Wandler` (§6), `SerialHub` mit I/O-Faden, Transporte Telnet/RFC2217/Datei + `SerialWandler.*` | S1, S2, S3 | L |
| **S5** | Karten: K8025 (Kanal B der A33 anschließen, V.24-Verknüpfungen, Taktquellen W1:7/X7–X9, ZRE-CTC-Abfrage), K7028 (Stromlaufplan klären §3.2, Rückschleife → Loop), `K1520Machine::serielleAnschluesse()`, Lauf-Anbindung; Doku 06/16 nachführen | S4 | L |
| **S6** | C-ABI §8 + `app/core_binding/k1520.py` + `py_serial_api` | S5 | S |
| **S7** | Dock „Schnittstellen", Konfiguration, Handbuch + `py_serial_gui` | S6 | M |
| **S8** | `SerielleKopplung.*`, `py_serial_pyserial`, Handtest mit `telnet`/`ser2net`/zweitem Emulator; CLAUDE.md-Absatz + `doc/merkposten/serielle_schnittstellen.md` | S6 (S7 für Handtest) | M |
| **S9** | A5120: Leertaste wiederholt am Gerät, im Emulator nicht (Buchstaben wiederholen beide nicht) — Ursache klären (K7637-Modell, BIOS, SIO-Betriebsart 01 seit `216dc14`?), beheben, Wächter | — | M |
| **S10** | Oberfläche nach Anwenderbefund 2026-10-01: Schnittstellen als Reiter IM Einstellungen-Kasten (neben Allgemein/Laufwerke/CRT) statt eigenem Dock; Format in der üblichen Schreibweise `8N1`/`7E1`/`8O1` mit Erklärung (Tooltip); Leitungen als LEDs; Port-Feld zeigt im Betrieb den TATSÄCHLICHEN Port; Gegenseite (RFC 2217) mit Format und Leitungen | S11 | M |
| **S11** | Kern + C-ABI: Format (Datenbits/Parität/Stoppbits) und Steuerleitungen der **Gegenseite** bei RFC 2217 im Status (Felder hinten an `K1520SerStatus`), Python-Bindung | — | S |

S1, S2, S3 laufen parallel (S3 berührt `build/` — S1/S2 dann im Worktree).

Zusätzlich **AP-T1b** (Regressionsabdeckung, `doc/design/16_k8915.md` §8a AP-T1) nach S8:
Abdeckung von S4–S8 messen, Wächter der Festlegungen prüfen, Lücken schließen.

### 12.1 Stand der Arbeitspakete

**AP-S1 — erledigt 2026-09-30** (`79b811e`). `core/serial/net/{socket,adresse}.{h,cpp}`,
Bibliothek `k1520_serial_net` (`ws2_32` bei `WIN32`), noch nicht an `libk1520core` gebunden —
das macht S4. Namespace `k1520::serial::net`: `netzStarten()` (WSAStartup einmalig),
`Socket` (RAII, nur verschiebbar), `lauschen(port)` / `lauschenMitSuche(ab)` (§7.2),
`portPruefen(port)` → `{frei, vorschlag}` (§7.4a; „frei" = wirklich bindbar, die Probe lauscht
kurz), `annehmen`, `aufloesen(host, port)` (alle v6+v4, bewusst ohne `AI_ADDRCONFIG`, sonst
fiele `::1` weg), `verbinden`/`verbindenAlle(ziele, fristMs)` (Frist gilt für alle Adressen
zusammen → passt zum 1000-ms-Dauerversuch), `senden`/`empfangen` → `IoStatus{Ok, Warten,
Geschlossen, Fehler}` (ohne SIGPIPE; RESET = Geschlossen), `warten(vector<PollEintrag>, ms)`
(poll/WSAPoll), `Wecker` (liegt in `net/socket.cpp`, nicht in `os_compat.h`),
`adresseZerlegen`/`adresseKlassifizieren`/`hostnameGueltig`. Wächter `SerialAdresse.*` (9),
`SerialNetz.*` (19). Abweichung: numerisches letztes Label ungültig (§7.3). Unter wine laufen
beide Binaries grün; der MSVC-Zweig und `SO_EXCLUSIVEADDRUSE` auf echtem Windows sind erst mit
`windows-ci.yml` geprüft. Bekannt: `WSAPoll` meldet ein gescheitertes `connect` vor Win10 2004
nicht — dann läuft die Frist ab, für den Dauerversuch unschädlich.

**AP-S2 — erledigt 2026-09-30** (`ab411ba`). `core/serial/{telnet_codec,rfc2217_codec}.{h,cpp}`,
Bibliothek `k1520_serial_codec`, Namespace `serial`. Beide Codecs gleich zum Transport hin:
`eingabe(bytes)`, `nimmNutzdaten()`, `sende(...)`, `nimmAusgabe()`/`hatAusgabe()`.
`TelnetCodec(TelnetRolle::{Client,Server})`: `start()` (Server: `WILL ECHO/SGA/BINARY, DO
BINARY`), Q-Methode RFC 1143 (kein Pingpong), `erlaube/bieteAn/verlange`,
`lokalAktiv/entferntAktiv`, Unterverhandlung als `nimmSub()` → `TelnetSub{option, daten}`
(statt Rückruf; `SUB_MAX` 1024). CR NUL je Richtung nur ohne BINARY; gesendetes CR wird ohne
Vorgriff zu CR NUL. `Rfc2217Codec(TelnetRolle)`: `nimmEreignisse()` → `Rfc2217Ereignis{art,
antwort, wert, text}` (Signatur, Baud, Datenbits, Paritaet, Stoppbits, Steuerung, LineState,
ModemState, …Maske, FlussHalt/Weiter, Leeren); `sendeBaud(wert, alsAntwort)` usw. — **der
Codec entscheidet nichts**, der Server antwortet mit dem Gastwert (Leitsatz 4).
`sendeLineState/ModemState` beachten die Masken aus 10/11 (Vorgabe Line 00H, Modem FFH —
nicht gegen die RFC geprüft). COM-PORT-Befehle gehen ohne Warten auf das Verhandlungsende
hinaus. Umrechnungen `paritaetNetz/Sio`, `stoppNetz/Halbe`. Wächter `TelnetCodec.*` (19),
`Rfc2217Codec.*` (16).

**AP-S3 — erledigt 2026-09-30** (`5711d6c`). `Z80SIO::Channel`: `format()` →
`Format{teiler, stopp_halbe (0=sync), paritaet, tx_bits, rx_bits}`, `rts()`, `dtr()`,
`breakSenden()`, `autoEnables()`, `setzeCTS/DCD(bool aktiv)`, `setzeBreakEmpfang`,
`cts()/dcd()/breakEmpfangen()`; Flanke → RR0 eingefroren + Ext/Status-IRQ bei WR1 D0,
Freigabe mit WR0-Befehl 2. Wandlerseite: `senderHatZeichen()`, `empfaengerFrei()` (FIFO < 3
und bei Auto Enables /DCD aktiv); Zeichen weiter über `txGet()`/`rxByte()`. **Auto Enables**
wirkt jetzt datenblattgetreu (kein bekannter Gast setzt WR3 D5 — mit Protokollfalle über beide
Testrunden belegt). `Z80CTC::teilerTakte(k)` (0 = unbekannt), Eingangsperiode über
`setzeEingangsQuelle(k, std::function)` (Kaskade) oder `setzeEingangsPeriode(k, takte)`;
Verdrahtung, überlebt `reset()`, nicht im Savestate. Vorgabe aller Eingänge: **inaktiv** —
RR0 bleibt für jeden bestehenden Pfad 04H. Savestate-Version 5 → 6 (SIO-Block +3 Felder).
Befunde für S4/S5: **Tx/Rx-Enable (WR5 D3 / WR3 D0) wird nirgends ausgewertet**; ein
**getriggerter CTC-Zeitgeber** hält nach dem ersten ZC/TO an (`fireZCTO` löscht `running`,
am Chip läuft er weiter) → `teilerTakte` liefert danach 0; `rxIntEnabled()` liest WR1 D3–2
statt D4–3; RR0 D6/D1 nie gesetzt; RTS wirkt ohne Verzögerung. **S5 muss die CTS/DCD-Pegel je
Karte bewusst setzen** — aktiv getrieben wird RR0 D5/D3 für den Gast sichtbar 1 (offener
Punkt 2).

**AP-S4 — erledigt 2026-10-01** (`d4f67b6`). `core/serial/{anschluss,wandler,transport,hub}.{h,cpp}`,
`sio_format.h`, Bibliothek `k1520_serial` (auf `_net`, `_codec`, Threads; `libk1520core`
bindet sie erst in S5). Namespace `k1520::serial` (Codecs weiter in `::serial`).
`core/version.h` (`K1520_VERSION_TEXT`) ist jetzt die gemeinsame Quelle für `k1520_version()`
und die RFC-2217-Signatur. `net::gegenstelle(fd)` ergänzt.
- **Für die Karten (S5):** `SerialAnschluss` — Pflicht `name/stecker/v24/format/
  senderHatZeichen/senderNimm/empfaengerFrei/empfange`, optional `taktquellen()/
  waehleTaktquelle(i)/rts/dtr/setzeEingaenge(cts,dsr,dcd)` (Pegel am Stecker, Verknüpfung
  macht die Karte), `breakGesendet/breakEmpfang(bool)`. Formathilfe
  `serialFormatAusSio(kanal, ctc, ctcKanal)` bzw. `(kanal, ctcTakte)`, `PHI_NENN`,
  `ersatzFormat()`.
- **Für die Maschine:** `SerialHub(phi)`, `registriere(anschluss)` beim Aufbau, `takt(zyklus)`
  im Lauf — **je Aufruf höchstens ein Zeichen je Richtung**, also je Instruktion oder in
  kleinen Batches rufen (arbeitet ohnehin nur alle 1/16 Zeichenzeit).
- **Für die C-ABI (S6):** `info/konfig/konfigurieren/start/startAuto/stop/stopAlle/status`;
  `SerialKonfig/Status/Info` 1:1 zu §8, Aufzählungen mit den ABI-Zahlenwerten.
- **Faden-Modell:** Emulationsfaden nimmt nur den Wandler-Mutex; GUI den Hub-Mutex (kurz);
  I/O-Faden nur solange aktiv, Reihenfolge Hub → Wandler. Jeder Client-Versuch in eigenem
  abgekoppelten Faden (`getaddrinfo` ist nicht abbrechbar) → „Trennen" wirkt sofort.
  `stop` weckt den I/O-Faden, sonst ginge das FIN bis 1 s später hinaus (Wächter < 500 ms).
- **Abweichungen:** (1) Break heißt `breakGesendet()`/`breakEmpfang(bool)`. (2) SET-CONTROL
  Flussart nur 1 oder 2 (XON/XOFF), nie 3 — die Brücke ist ein Stecker, ser2net ohne CTS am
  Gerät bliebe sonst stehen. (3) Fernleitungen gelten als aktiv bis zum ersten SET-CONTROL
  bzw. NOTIFY-MODEMSTATE; IFSS meldet als Server CTS/DSR/CD fest an. (4) PURGE 1 = unser
  Sende-, 2 = Empfangspuffer (Sicht des Zugangsservers). (5) Kabel ab verwirft den
  Sendepuffer, der Empfangspuffer wird noch zugestellt. (6) **Datei meldet VERBUNDEN** —
  die Statuszeile schließt Datei über `betriebsart` aus (§9). (7) Port 0 in `SerialKonfig`
  erlaubt (Tests) — **S6 weist ihn an der C-ABI ab**. (8) Statusformat = rohes Gastformat
  (`format_gueltig`), RFC-2217 meldet das tatsächlich getaktete (notfalls Ersatz). (9) Das
  Eintragen von `port_vorschlag` ins Port-Feld macht die Oberfläche (S7).
- **Offen für S5:** RTS-Halt ist wörtlich umgesetzt — ein Gast, der auf V.24 RTS nie setzt,
  empfängt nichts; am BIOS prüfen. Umstellung `setDFUECallback`/`setPrinterCallback` und
  `k1520_serial_send`/`set_rx_cb` steht aus.
- Wächter: `SerialWandler.*`, `SerialClientDauerversuch.*`, Hub-Rundläufe Telnet/RFC2217 über
  Loopback, Datei (31 Fälle, 10× bei `-j48` stabil).

**AP-S5 — erledigt 2026-10-01** (`7b7b75c`). K8025 und K7028 liefern je einstellbarer
Schnittstelle einen `SerialAnschluss`; `SerialHub` je Maschine, `libk1520core` bindet
`k1520_serial`. Belegung und Klärungen: §3.1/§3.2/§4/§13, Befunde mit Quelle in
`06_k8025_ass.md` (§2, §6, §8, §10) und `16_k8915.md` §3.2.
- **Maschinen-API für S6:** `K1520Machine::serialHub()` (Index = Anmeldereihenfolge =
  C-ABI-Index), `serielleAnschluesse()`, `festeSchnittstellen()` (A5120 „Tastatur K7637
  (X4)", K8915 „Tastatur K7672") für `k1520_serial_fixed_name`. Reihenfolge A5120: DFÜ/V.24,
  DFÜ/IFSS, Drucker; K8915 (seit S12): Drucker/IFSS1, V.24, DFÜ/IFSS2.
- **Alter Unterbau:** `setDFUECallback`/`dfueSend` = A5120 DFÜ/V.24 bzw. K8915 IFS 2,
  `setPrinterCallback`/`printerSend` = A5120 Drucker (geht jetzt — war vorher leer) bzw.
  K8915 IFS 1. Bytes in ihrer Zeichenzeit; belegt Loop oder Transport den Stecker, ins Leere.
  K8915: ein gesetzter Rückruf zieht den Loop seines Kanals, ein leerer steckt ihn wieder.
  `k1520_serial_set_rx_cb(…, NULL)` meldet ab.
- **Kern-Ergänzungen:** `SerialAnschluss::leitungBelegt(bool)`; `Wandler/SerialHub::takt`
  liefern den nächsten fälligen Takt (die Maschine ruft nur dann); Leerlaufpfad;
  `gastZurueckgesetzt()`; RTS-Halt erst nach erstem RTS (§6.4).
- **Verhaltensänderung:** Sendebytes auf K8025 A33-A/B und A32-B holt jetzt der Wandler ab
  (unverbunden verfallen sie in ihrer Zeichenzeit) — vorher blieb ein sendender Gast mit
  vollem Puffer stehen. Die Leitung trägt das **programmierte** Format (eine unprogrammierte
  SIO sendet 5 Bit).
- **Laufzeit** (Thread-CPU, 300 M Takte): A5120 +3,6 % (28,3× → 27,3× Echtzeit), K8915 ± 0.
- Wächter: `K8025Seriell.*` (5), `K7028Seriell.*` (6), `K8915Seriell.*` (4, u. a. `LIST` über
  Telnet mit XON/XOFF), `A5120Seriell.GastSendetUeberDfueV24InEineDatei`,
  `SerialWandler.RtsNieGesetztHaeltNicht`/`MeldetDieBelegungDesSteckers`. `test` und
  `test-format` grün.

**AP-T1a — erledigt 2026-10-01** (Regressionsabdeckung bis S3, Ergebnis in `16_k8915.md` §8a
AP-T1). Abdeckung der geänderten Zeilen C++ 91,2 → 93,3 %, Python 90,0 → 93,8 %; zwei schwache
Wächter verschärft. Befund bestätigt: `Z80SIO::rxIntEnabled()` liest WR1 D3–2 statt D4–3
(WR1 = 13H gibt keinen Rx-Interrupt) — nicht behoben. Für T1b: die Befehle des Abdeckungsbaus
stehen im Ergebnisabschnitt, `gcovr` braucht `--gcov-suspicious-hits-threshold 0`.

**AP-S6 — erledigt 2026-10-01** (`1f3c585`). C-ABI §8 vollständig, dazu
**`k1520_serial_get_config`** (Lesezugriff, den die Oberfläche braucht). `groesse`-Regel
(Header + `10_c_api.md` §7): Ausgabe schreibt höchstens `groesse` Bytes und trägt die
geschriebene Zahl zurück; Eingabe lässt Felder jenseits `groesse` unverändert; `< 4` →
`false`; neue Felder nur hinten. `configure` weist ab: Index, **Port 0**, ungültige
Aufzählung/Taktquelle, gesperrte Felder im Betrieb. Python (`app/core_binding/k1520.py`):
`serial_count/info/fixed_names/config/configure(i, **felder)/start/start_auto/stop/status`
(Datenklassen `SerialInfo/SerialKonfig/SerialStatus`, `None` bei ungültigem Index),
Konstanten `SER_*`/`HOST_*`, `classify_host(s)`. **K8915 startet mit `loop=True`** —
`serial_start` liefert dort `false`, bis der Loop aus ist (die Oberfläche muss das zeigen).
Wächter `py_serial_api` (beide Maschinen, ~2 s), `test_c_api.py` vergleicht jetzt auch Felder
der `K1520Ser*`-Strukturen und die Enum-Werte.

**AP-S7 — erledigt 2026-10-01** (`dc36b94`). `app/ui/serial_widget.py` (`SerialWidget`,
`SerialBlock` je Schnittstelle), Dock hinter *Einstellungen*, *Ansicht ▸ Sch&nittstellen*,
Kastenschalter `dock_serial` in `actions.REIHENFOLGE` (kein Kürzel), Statuszeile
`SeriellFeld` ×2 (`MachineStatus.set_seriell`, Knopfdruck zieht sofort nach), Handbuch
„Schnittstellen", `11_python_app.md` §10.10. Festlegungen:
- **Konfiguration:** Betriebsart/Rolle als Wörter (`telnet`/`rfc2217`/`datei`,
  `server`/`client`), `taktquelle` als **Name** aus dem Kern; Unbrauchbares wird einzeln
  übergangen. Speichern in `closeEvent` VOR dem Beenden (sonst wäre `aktiv` immer falsch).
- **Maschinenwechsel** (`_apply_drive_types` baut Maschine + Hub neu): Stand und `aktiv`
  werden gemerkt und an der neuen Maschine wieder aufgenommen — ein Laufwerkswechsel kostet
  keine Verbindung.
- Knopf gesperrt bei Loop, ungültigem Host (nur Client) und fehlender Datei, nie solange er
  etwas beenden kann; K8915-Grund als Meldungszeile + Tooltip. Datei-Dialog abgebrochen →
  Betriebsart springt zurück. „Gast nicht programmiert" statt Format vor dem BIOS-Init.
- **Testfalle:** geschlossene `MainWindow` lassen Laufwerks-Zeitgeber und
  `ScreenFocusGuard` weiterlaufen — bei ~70 Fenstern je Prozess scheinbarer Stillstand;
  `deleteLater()` stürzt über den Fokusfilter ab. `test_serial_gui.py::_zu()` legt beides
  still — für weitere Fensterfälle übernehmen.
- Wächter `py_serial_gui` (81 Fälle, ~12 s, beide Programme).

**AP-S8 — erledigt 2026-10-01** (`1a7a899`, `ec5e42e`). Kein Fehler in Kern/Wandler/Hub/GUI
gefunden. `test` (1478), `test-format` (26), `win` (1449) grün; neue Fälle 5× stabil.
- **`SerielleKopplung.*`**: zwei A5120 in einem Prozess, abwechselnd in einem Faden
  (1 ms : 10 ms Maschinenzeit je Runde — das Verhältnis hält auch unter `ctest -j`),
  Testgäste als Z80-Programme direkt im RAM (kleiner Assembler mit Marken im Test).
  `TelnetXonXoff_4KiB`, `Rfc2217RtsCts_4KiB` (Auto Enables beim Sender), 
  `UnterschiedlicheBaudWirdAngezeigt` (~0,3–1,8 s) in der Standardregression; die
  64-KiB-Fassungen (je ~29 s) als eigenes Binary `k1520_test_serielle_kopplung_voll` unter
  `format_integration`. Gegenprobe: falsches Muster färbt alle rot; geprüft wird auch, DASS
  die Flusssteuerung griff. Gekoppelt wird nur A5120↔A5120 (K8915-Weg: `K8915Seriell.*`).
- **`py_serial_pyserial`** (~4 s, übersprungen ohne `pyserial`; `pyserial>=3.5` in
  `requirements-dev.txt`): Bytes beidseitig, Baud (pyserial-Wunsch 1200 abgelehnt, Gast
  bleibt 9600), Nullmodem-Kreuzung beidseitig, RTS-Halt. Gast = CP/A bis `A>`, CONST/CONIN
  der BIOS-Sprungleiste auf ein Echo umgebogen (die Bindung kann den PC nicht setzen).
- **Handtest** (automatisiert): `telnet` → Echo, BINARY verhandelt; zwei Prozesse über die
  Bindung (RFC 2217); zwei GUIs offscreen mit `aktiv: true` nehmen die Verbindung beim Start
  selbst auf, ein dritter Client bekommt „belegt"; `socat` → PTY geht, zeigt aber die
  Telnet-Verhandlung und `CR NUL` (kein Telnet — §7.4, im Merkposten als Falle).
  `ser2net` nicht installiert, nur dokumentiert.
- **Merkposten** `doc/merkposten/serielle_schnittstellen.md`, Absatz in `CLAUDE.md`.
- **Offen — nur am Bildschirm prüfbar** (Anwender): Aussehen von Dock/Statuszeile/Tooltips,
  Warnfarbe „Gegenseite … Bd", echtes Terminalprogramm (PuTTY) gegen ein Gast-Terminal-
  programm, echtes Gerät über `ser2net`, Erreichbarkeit im LAN (Firewall, Dual-Stack).

**AP-T1b — erledigt 2026-10-01** (`2aa7830`, `04c8e67`, `03ae045`, `c8bf589`; Ergebnis in
`16_k8915.md` §8a AP-T1). Abdeckung der geänderten Zeilen seit `4b3685d`: C++ 91,4 → 96,4 %,
Python 96,3 → 98,4 %. 20 Festlegungen durch Zurückdrehen geprüft, alle rot — drei davon erst
durch die neuen Wächter (Datenbits-Antwort, Schreibfehler beim Beenden, DFÜ/Drucker-Weiche).
TSan über Hub, Wandler und Kopplung ohne Befund. **Fehler behoben:** `K1520SerialRxCb` in der
ctypes-Bindung hatte die Parameter vertauscht (schon auf `main`; die GUI benutzt den Rückruf
nicht). Befunde ohne Änderung: ein `k1520_serial_send` unmittelbar nach `start` erreicht den
Gast noch (die Karte erfährt die Belegung erst beim nächsten Wandlerblick, ≤ 1/16
Zeichenzeit); `SerialHub::rueckstau` wird nie gefüllt (Schutzcode); unter Winsock verwirft
ein RST ungelesene Daten im Socketpuffer (OS-Verhalten).

**Nachtrag 2026-10-01 — SIO-Empfangsinterrupt nach Datenblatt** (`216dc14`; Befund aus
S3/T1a, auf Anwenderwunsch in diesem AP behoben). `Z80SIO::Channel::rxIntEnabled()` las WR1
D3–D2 statt D4–D3; WR1 = 10H/13H lieferten keinen Empfangsinterrupt. Jetzt entscheidet für
alle Empfangswege `rxIntFaellig()` über `rx_int_mode`: 10/11 = jedes Zeichen, **01 = nur das
erste Zeichen nach dem Setzen bzw. nach WR0-Befehl 4** (vorher: jedes Zeichen in leeren
FIFO). Channel Reset löscht jetzt auch die aus WR1 abgeleiteten Freigaben.
**Protokollfalle** über `test` + `test-format`: die Gäste schreiben 00H, 04H, 08H, 0CH, 13H,
17H, 18H, 1CH — der **A5120-Tastaturkanal (K8025 A32-A) bekommt 0CH**, läuft also jetzt in
Betriebsart 01 statt „jedes Zeichen"; alle Boots, Tastaturtests und Formatierrunden bleiben
grün. Zwei RETI-Tests und `RX_Interrupt_AllReceivedMode` hatten die falsche Lesart
festgeschrieben (08H mit Erwartung „jedes Zeichen") und sind auf 10H umgestellt. Neue Wächter
`Z80SIO.RX_Interrupt_WR1_JedeBetriebsartNachDatenblatt`,
`…ErstesZeichen_EinmalJeScharfmachen`, `…KanalResetLoeschtDieFreigabe`. Volle Regression:
`test` 1494, `test-format` 26, `test-matrix` 94, `win` 1465 — alle grün. Offen: Handprobe
schnelles Tippen/Tastenwiederholung am A5120.

**AP-S11 — erledigt 2026-10-01.** `K1520SerStatus` hinten ergänzt: `daten_gegenseite`,
`paritaet_gegenseite` (`K1520_SER_PAR_*`: keine/ungerade/gerade/mark/space),
`stopp_halbe_gegenseite`, `format_gegenseite_bekannt` (erst wenn alle drei da sind),
`format_abweichend` (neben `baud_abweichend`), `leitungen_gegenseite` +
`leitungen_gegenseite_bekannt` (Masken `K1520_SER_L_RTS/DTR/CTS/DSR/DCD/RI`). Je Rolle:
**Server** sieht Formatwunsch (SET-DATASIZE/PARITY/STOPSIZE) und RTS/DTR des Clients — je
Leitung erst ab ihrem ersten SET-CONTROL (die intern angenommene „aktiv"-Vorgabe zählt nicht
als bekannt); **Client** sieht die Formatantworten und CTS/DSR/DCD/RI aus NOTIFY-MODEMSTATE.
Telnet/Datei: alles unbekannt. Python: `format_gegenseite_text` („8N1"/„7E1", `None`),
`leitung_gegenseite(SER_L_*)` → True/False/None, `leitungen_gegenseite_text`. Hinweis: das
Clientformat kommt erst nach der 100-ms-Entprellung an. Wächter
`SerialHub.Rfc2217ZeigtFormatUndLeitungenDerGegenseite`,
`test_rfc2217_status_shows_format_and_lines_of_the_far_side`.

**AP-S9 — erledigt 2026-10-01** (`203e7a8`). Die **Tastatur K7637 entscheidet selbst**, was
wiederholt: nur ihre bis zu 16 „Dauerfunktionstasten" je Codetabelle
(`doc/trascripted/Serielle Tastatur K 7637.XX.md` §2.2.2; ROM
`robotron-k7637_50-2716.bin` 650H/760H: `20 95 96 94 97 5F` = Leertaste, vier Kursortasten,
5FH; Vergleichsschleife ab 0171H), nach ca. 500 ms alle ca. 100 ms; jede weitere Taste beendet
die Dauerfunktion. Im Emulator wiederholte **gar nichts**: die pauschale Wiederholung des
Modells hing an `K7637::tick()`, das nie gerufen wurde, und die Host-Wiederholung verwirft die
Oberfläche bewusst. **Nicht** durch `216dc14` (SIO-Betriebsart 01) verursacht — der Wächter
läuft damit grün. Behoben: Wiederholung in Maschinenzeit in `service()`, nur für
`K7637::isRepeatCode()`. Wächter `KeyboardIntegration.HeldSpaceRepeatsHeldLetterDoesNot`
(CP/A bis `A>`, Leertaste 1,5 s → 8–14 Leerzeichen, „A" 1,5 s → genau eins; ohne Fix rot),
`K7637.Dauerfunktion_*`. Doku `doc/design/08_k7637_keyboard.md` §2.2a. Offen: welche Taste
5FH sendet (im Modell nur der PC-Unterstrich, der jetzt wiederholt); ROM-Zeiten 480H/481H nicht
in ms umgerechnet (Handbuchwerte benutzt).

**AP-S10 — erledigt 2026-10-01** (`edbf8cc`). Reiter statt Dock (§9); `serial_dock`,
`act_dock_serial` und *Ansicht ▸ Schnittstellen* entfallen; ein `dock_serial` in
`window.toolbar` bzw. ein `serial_dock` in einer alten `dock_state` wird übergangen (Wächter).
Das `SerialWidget` gehört dem Hauptfenster, sein 4-Hz-Takt läuft auch bei verdecktem Reiter
(Statuszeile bleibt aktuell). Format `8N1`/`1.5`, LEDs `LeitungsLed`/`LeitungsReihe`
(„Aus:" RTS/DTR, „Ein:" CTS/DSR/DCD), Port-Feld im Betrieb = `port_aktiv` (Tooltip
„Eingestellt: X, benutzt: Y"), Gegenseite-Zeile mit ⚠/Warnfarbe bei `baud_abweichend` oder
`format_abweichend`, fehlt solange nichts bekannt ist. `py_serial_gui` 105 Fälle.

**AP-S12 — erledigt 2026-10-01** (`6b10a01`, `cb84cfc`). K8915-Namen nach Gerätebeschriftung
(§3.2), `K7028::Kanal` jetzt Sio1B=0, Sio1A=1, Sio2A=2 (Reihenfolge = Stecker = C-ABI-Index),
alte Konfigurationsnamen werden abgebildet. **LEDs:** kein Datenfehler — „inaktiv" war
`#3a3d3a`, praktisch schwarz, und unverbunden ist alles inaktiv (CP/A/SCPX setzen RTS/DTR nie).
Jetzt aktiv `#3cf03c`, inaktiv gedimmtes Grün `#2a6a2e` mit grünem Rand, unbekannt grauer
Umriss; Gruppen „Ausgänge →"/„Eingänge ←" mit Tooltip („vom Rechner getrieben/empfangen").
Ende-zu-Ende geprüft: Telnet-Verbindung → CTS/DSR/DCD grün, nach dem Trennen wieder aus.
**Einschränkung:** die Eingänge werden nur nachgeführt, solange die Maschine läuft — angehalten
behalten die LEDs ihren Stand (im Handbuch vermerkt). `test` 1499, `test-format` 26 grün.

**Abnahme 2026-10-01:** der Anwender hat am Bildschirm und am Gerät geprüft — Reihenfolge und
Namen der K8915-Schnittstellen (V.24 an X4 bestätigt), LED-Zustände bei Telnet-Verbindung,
Unterscheidbarkeit gedimmt/Umriss, Leertasten-Wiederholung (S9) und die Punkte der
Checkliste aus S8/S10: **alles stimmt.** `win` nach S12: 1470 grün.

## 13. Offene Punkte

Entschieden am 2026-09-30: Bindeadresse = alle Schnittstellen (§7.2), Wiederaufnahme beim
Start (§7.4a); UI-Namen der K7028 nach Gerätebeschriftung = „Drucker/IFSS1" (X3), „V.24" (X4),
„DFÜ/IFSS2" (X5) — 2026-10-01, §3.2.

1. ~~**K7028:** Belegung, Taktkanäle, V.24-Leitungen, Brücken~~ — geklärt in AP-S5 (§3.2);
   Rest: Rückwandbeschriftung, Stecker der K7672. Der Weg von /CTSA und /DCDA ist seit
   2026-10-01 geklärt (§14.5: ohne D13, CTS = V107 ∧ (¬RTS ∨ V106)).
2. **K8025:** Weg ZRE-CTC K0 → K8025 geklärt (AP-S5, `K8025::setzeZreTakt`). Die Pegel der
   CTS/DCD-Eingänge der IFSS-Kanäle und der A32 bleiben **vorläufig inaktiv** — es gibt
   keinen Stromlaufplan der K8025 (das PDF hat nur das Blockschaltbild S. 8).
3. ~~**K8915 Loop-Vorgabe**~~ — geklärt in AP-S5: BIOS läuft ohne Echo, Vorgabe bleibt an (§4).
4. **Statuszeile und Datei:** Soll eine offene Druckdatei in der Statuszeile erscheinen
   (etwa „Drucker → druck.txt")? Entwurf: nein, nur Netzverbindungen (§9).

---

## 14. Testprogramm „Serial Test" (`SERTEST.COM`)

**Stand:** 2026-10-01, **AP-ST1 – AP-ST6 erledigt** (Gerüst `tools/sertest/`, V0.1; Testgerüst
`tests/system/test_sertest.cpp` + `sertest_hilfen.h`; SIO-/CTC-Schicht; Prüfsteckertest +
K7028-/CTSA nach Stromlaufplan; Protokoll, Gegenstelle, LEITUNGEN + ECHO mit zwei gekoppelten
Maschinen, K8025 reicht RETI weiter; Flusssteuerung FLUSS-HW/FLUSS-XON, gekoppelt `SERTEST
ENDE OK`); weiter mit ST7.
Arbeitspakete §14.9 (AP-ST1 … AP-ST7).

### 14.1 Ziel

Ein Z80-Programm unter CP/M 2.2, das die seriellen Schnittstellen eines **A5120** (K8025)
und eines **K8915** (K7028) prüft — am echten Gerät wie im Emulator. Derselbe Ablauf, der
am Gerät mit Prüfstecker und Nullmodemkabel von Hand läuft, läuft im Testsystem automatisch
gegen den Rx/Tx-Loop (§6.5) bzw. gegen einen zweiten Emulator über RFC 2217 (Nullmodem-
Kreuzung §6.4). Das Programm ist damit zugleich Abnahmewerkzeug für Geräte und End-zu-End-
Wächter der Schnittstellenemulation (SIO, CTC, Wandler, Steuerleitungen, Flusssteuerung).

- Name **„Serial Test"**, Version **0.1**, Datei **`SERTEST.COM`**.
- Kopfzeile beim Start, genau so: `Serial Test V0.1  (c) 2026 Olaf Krieger`
- Zwei Rollen: **Tester (Aktiv)** und **Gegenstelle (Passiv)**.
- Läuft unter **CP/A (A5120)** und **SCPX 8915 V5.3 (K8915)**; benutzt vom Betriebssystem
  nur das BDOS (Funktionen 0, 6, 9). Alles andere geht direkt auf die Hardware.
  Umgesetzt (ST1): **auch die Ausgabe zeichenweise über BDOS 6**, BDOS 9 bleibt ungenutzt —
  BDOS 9 läuft durch die Abbruchprüfung des BDOS (`conbrk`), die eine während der Ausgabe
  gedrückte Taste in ihren eigenen Puffer holt; für BDOS 6 wäre sie (auch ein Ctrl+C) verloren.
- Bildschirmtexte **ohne Umlaute** (ae/oe/ue/ss) — der Zeichensatz der Geräte kennt sie
  nicht sicher.

### 14.2 Bedienablauf

**Gemeinsamer Anfang:**

```
Serial Test V0.1  (c) 2026 Olaf Krieger
Rechner: A5120 (K8025)
Schnittstellen:
  1  DFUE/V.24      SIO A33 Kanal A   V.24
  2  DFUE/IFSS      SIO A33 Kanal B   IFSS
  3  Drucker        SIO A32 Kanal B   IFSS
  -  Tastatur K7637 SIO A32 Kanal A   (Tastatur)
Tester (Aktiv) oder Gegenstelle (Passiv)? T/G
```

Die Tastatur wird angezeigt und mit „(Tastatur)" gekennzeichnet, aber **nie** zum Test
angeboten (ihre SIO umzuprogrammieren nähme dem Programm die Eingabe und damit Ctrl+C).
Die Liste zeigt nur die Schnittstellen, die die Erkennung (§14.4) gefunden hat.

**Tester:** Die Schnittstellen werden der Reihe nach abgefragt:
`Test der DFUE/V.24? J/N`. Nur `j`/`J` wählt; alles andere überspringt. Für jede gewählte
Schnittstelle:

1. `Test mit Pruefstecker? J/N` → §14.5.
2. `Test mit Gegenstelle? J/N` → §14.6/§14.7. Davor der Hinweis, das Kabel zu stecken und
   auf der Gegenstelle dieselbe Schnittstelle zu wählen; weiter mit beliebiger Taste.

Jeder Teilschritt endet mit `OK` oder `FEHLER: <Grund>`. Am Ende eine Zusammenfassung je
Schnittstelle und Rückkehr ins Betriebssystem.

**Gegenstelle:**

```
Gegenstelle (Passiv): Dieses Geraet mit einem passenden Kabel mit einem
anderen Rechner verbinden, auf dem Serial Test als Tester (Aktiv) laeuft.
Beenden mit Ctrl+C.
Test der DFUE/V.24? J/N
```

Die Abfrage läuft wie beim Tester der Reihe nach; die **erste** mit `J` bestätigte
Schnittstelle wird benutzt (weitere Fragen entfallen). Danach läuft die Gegenstelle auf
**genau dieser einen** Schnittstelle, bis sie mit **Ctrl+C** beendet wird. Wird keine
gewählt, endet das Programm.

**Ctrl+C** beendet in beiden Rollen an jeder Stelle (auch mitten in einer Übertragung):
erst Aufräumen (§14.4 „Wiederherstellen"), dann BDOS 0. Die Tastatur wird mit BDOS 6
(direkte Konsol-E/A) abgefragt, nicht mit BDOS 1/11 — dort griffe die eigene Ctrl+C-/
Ctrl+S-Behandlung des BDOS dazwischen.

### 14.3 Kommandozeile (Grundlage des automatischen Tests)

```
SERTEST                      interaktiv wie §14.2
SERTEST T n [/P] [/G] [/A]   Tester an Schnittstelle n (Nummer aus der Liste)
SERTEST G n                  Gegenstelle an Schnittstelle n
```

- `/P` nur Prüfsteckertest, `/G` nur Gegenstellentest, ohne beide: beide.
- `/A` = **automatisch**: keine Rückfragen, kein „beliebige Taste"; jede Bestätigung gilt
  als gegeben. Für die Gegenstelle bedeutet `G n` bereits „ohne Rückfragen".
- Fehlerhafte Kommandozeile → Kurzhilfe, Ende.

**Ergebniszeilen** (für den Test vom Bildschirm gelesen; Format ist ein **Vertrag**):

```
SERTEST <name> <TEIL>: OK
SERTEST <name> <TEIL>: FEHLER <grund>
SERTEST ENDE OK | SERTEST ENDE FEHLER
```

`<TEIL>` ∈ `DATEN-LOOP`, `LEITUNGEN-LOOP`, `LEITUNGEN`, `ECHO`, `FLUSS-HW`, `FLUSS-XON`;
ein Teil, der für die Schnittstelle nicht gilt (Leitungen/FLUSS-HW bei IFSS), erscheint als
`ENTFAELLT`. Die Gegenstelle gibt nach dem ersten Empfangsinterrupt einmal
`SERTEST INTERRUPT OK` aus.

### 14.4 Hardwareschicht

**Schnittstellentabelle** (Ports nach §3; Datenport = Basis, Steuerport = Basis+1):

| Rechner | Nr | Name | SIO/Kanal | Daten/Steuer | Art | Baudtakt | Bemerkung |
|---------|----|------|-----------|--------------|-----|----------|-----------|
| A5120 | 1 | DFUE/V.24 | A33 A | 50H/51H | V.24 | ZRE-CTC K0 (0CH) | Brücke W1:7 gezeichnet angenommen |
| A5120 | 2 | DFUE/IFSS | A33 B | 52H/53H | IFSS | ZRE-CTC K0 (0CH) | Brücke X7–X8 gezeichnet angenommen; teilt den Takt mit 1 |
| A5120 | 3 | Drucker | A32 B | 5EH/5FH | IFSS | CTC A34 K0 (58H) | **CTC nicht anfassen** — Tastatur hängt am selben Takt |
| A5120 | – | Tastatur K7637 | A32 A | 5CH/5DH | — | CTC A34 K0 | nur Anzeige |
| K8915 | 1 | Drucker/IFSS1 | SIO1 B | 42H/43H | IFSS | CTC1 K2 (4AH) | |
| K8915 | 2 | V.24 | SIO1 A | 40H/41H | V.24 | CTC1 K0 (48H) | über Multiplexer D13 |
| K8915 | 3 | DFUE/IFSS2 | SIO2 A | 50H/51H | IFSS | CTC2 K0 (58H) | |
| K8915 | – | Tastatur K7672 | SIO2 B | 52H/53H | — | CTC2 K2 | nur Anzeige |

**Nicht anfassen:** ZRE-CTC K2/K3 (CP/A-Uhr, `bios.mac`: „Kanal 0,1 steht fuer den
Anwender frei"), CTC A34 K0 (Tastatur + Drucker), CTC1 K3 / CTC2 K3 (Zeitgeber K8915),
CTC2 K2 (Tastatur K8915), den Tastaturkanal jeder SIO.

**Baudrate 9600, Format 8N1:** Zeitkonstante und SIO-Teiler je Kanal so, wie das jeweilige
BIOS sie für 9600 setzt (CP/A: `portpr` in `src/bc_a5120/bioscsio.mac`/`bioscsib.mac` der
CPA_Workbench; SCPX 8915: BIOS-Disassemblat, `doc/design/16_k8915.md`). Beim A5120-Drucker
wird **nur** das SIO-Format gesetzt; der Takt ist dort durch die Tastatur auf 9600 festgelegt.
Die Annahme „Brücke gezeichnet" steht im README des Programms.
Umgesetzt (ST3), je Kanal: CTC-Steuerwort wie das BIOS — CP/A **17H** (`bioscpb.mac`,
„Vorteiler 16"), SCPX 8915 **07H** (BIOS DE82H/D9C7H) — und Zeitkonstante **1**
(2,4576 MHz / 16 = 153,6 kHz), dann Kanalreset 18H, WR4 **44H** (×16, 1 Stoppbit, keine
Parität), WR3 **C1H**, WR5 **EAH** (8 Bit, Sender ein, DTR + RTS), WR1 00H. Am A5120 bestätigt
der Kaltstart CTC A34 K0 = 05H/01H (9600 × 16 für Tastatur und Drucker).

**Maschinenerkennung** (festgelegt ST1): A5120 und K8915 überlappen in 50H–5FH; unterschieden
wird an der SIO 1 des K8915 bei 40H–43H (am A5120 im Emulator ohne Gerät → FFH). Ablauf unter
DI: SIO bei 40H gefunden → SIO bei 50H gefunden → **K8915**; keine SIO bei 40H → SIO bei 50H
(A33) gefunden und RR0 bei 5DH/5FH (A32, **nur gelesen**) ≠ FFH → **A5120**; sonst „Rechner
nicht erkannt" mit Hinweis auf `/M:`. Im Emulator auf beiden Maschinen ohne Überstimmung
richtig (CP/A `cpa_cpa780_k5601_noclock.img`, SCPX `k8915scpx_cpa800_k5601_bios55k-disk900.hfe`).
Am Gerät offen **[?]**: ob 40H–43H an einem A5120 in jeder Ausbaustufe frei ist, und ob die vom
BIOS ungenutzte SIO 1 des K8915 einen Vektor ≠ FFH trägt (RR2 ist dort nicht programmiert). Grundsatz: **die Erkennung schreibt nur in Steuerregister von Bausteinen, die
sie selbst gefunden hat** — nie in einen Port, der auf der anderen Maschine ein CTC ist
(5CH–5FH ist am K8915 ein Spiegel von CTC2, K3 = Zeitgeber). Gefunden gilt eine SIO, wenn
das Lesen von RR2 über Kanal B einen Wert ≠ FFH liefert bzw. ein geschriebener Vektor
zurückkommt (nur an SIOs ohne Tastatur). Überstimmbar mit `/M:A` bzw. `/M:K`.

**SIO-Zugriff:** Kanal programmieren (WR4/WR3/WR5/WR1), RTS (WR5 D1) und DTR (WR5 D7)
setzen, CTS (RR0 D5) und DCD (RR0 D3) lesen, polled Senden/Empfangen mit Zeitüberlauf,
Fehler aus RR1 (Parität D4, Überlauf D5, Rahmen D6) zählen und mit `Error Reset` löschen.
**Zeitbasis** für Zeitüberläufe und Pausen: geeichte Zählschleife (φ = 2,4576 MHz an beiden
Maschinen) — keine Uhr des Betriebssystems.

**Interrupt (nur Gegenstelle):** Mode 2 bleibt, wie das BIOS ihn eingerichtet hat. Die
Vektortabelle liegt bei `I·256`; den Vektor der SIO liefert RR2 von Kanal B. Das Programm
**hängt sich ein und reicht weiter**: es ersetzt nur die Einträge des geprüften Kanals
(Empfangszeichen, Sonderempfang) und springt, wenn der Interrupt nicht von seinem Kanal
kommt, in den gesicherten alten Eintrag. Teilt der Kanal die SIO mit der Tastatur (A5120
Drucker, K8915 DFUE/IFSS2), darf WR1 von Kanal B nur so geschrieben werden, dass
„Status affects Vector" (D2) erhalten bleibt — WR1 ist nicht lesbar, der Wert muss aus dem
BIOS stammen (Tabelle je Maschine). Ist WR2 noch nicht gesetzt (SIO ohne BIOS-
Interrupt), setzt das Programm einen eigenen Vektor in eine freie Tabellenzeile.

Geklärt (ST3) aus den BIOS-Quellen und gegengeprüft am Kaltstart im Emulator
(`k1520dbg` mit `iow` auf alle SIO-/CTC-Steuerports, danach `dev sio`/`ivt all`):

| SIO | BIOS | WR1 B / WR2 | Interrupt | SERTEST |
|-----|------|-------------|-----------|---------|
| A5120 A32 (Tastatur A, Drucker B) | CP/A 25.09.89 | WR1 A = 00H (`bioskbdc.mac` `k37par`: K7637 **gepollt**), WR1 B = 00H (`bios.mac` `lstlpt`: `@wr1 = 0`), WR2 nie geschrieben | keiner | **eigener Vektor E4H**, D2 bleibt aus |
| A5120 A33 (DFÜ) | CP/A | `lsttty` WR1 = 00H; nur ein benutzter UC1:-Treiber schreibt WR1 A = 14H, WR1 B = 04H, WR2 = D0H (`bioscsio.mac` `uc1i2`, `ivsio0 = intvuc = D0H`) | nur UC1: | **eigener Vektor E4H**, D2 aus (WR1 B := 00H bei Kanal A) |
| K8915 SIO 1 (Drucker B, V.24 A) | SCPX 8915 V5.3 | Drucker-Init DE82H schreibt kein WR1/WR2 (nach Kanalreset 00H) | keiner (16_k8915 §AP-B1) | **eigener Vektor C0H**, D2 aus |
| K8915 SIO 2 (DFÜ A, Tastatur B) | SCPX 8915 V5.3 | **WR1 B = 17H** (D2 gesetzt), **WR2 = D0H** (`scpx8915_v53_bios.prn` D9C7H) | Tastatur D0–D6H | **Vektor des BIOS**: Basis = RR2 B ∧ F1H, Einträge Kanal A **DCH/DEH**; WR1 B/WR2 nie geschrieben |

- **Freie Tabellenzeilen:** CP/A `intvsy+04h` = **E4H** („frei" laut `bios.mac`; der Bereich
  `intvsy`…`+1FH` = E0–FFH gehört in jedem CP/A-BIOS zur Tabelle — D0–DFH dagegen nur,
  wenn UC1: eingebunden ist, darunter liegen BIOS-Arbeitszellen). SCPX 8915: **C0H**
  (FFC0H; das BIOS belegt nur FFD0–FFD7H und FFF0–FFFEH, FF00–FFCFH steht auf FFH).
- **Eigener Vektor = „Status affects Vector" aus**: eine Zeile für alle Anlässe der SIO,
  die Empfangsroutine prüft RR0 D0/RR1 und reicht Fremdes an den alten Eintrag weiter
  (0000H/FFFFH = keiner → nur `RETI`). Am Kanal A setzt SERTEST dafür WR1 B := 00H — B ist
  dort nie die Tastatur, und ein D2 von früher (UC1:) verböge sonst den Vektor.
- WR1 des eigenen Kanals im Interruptbetrieb: **10H** (jedes Zeichen).

**Wiederherstellen** (bei Ende und bei Ctrl+C, in dieser Reihenfolge): DI, Tabelleneinträge
zurück, Kanal auf die BIOS-Vorgabe (Format, RTS/DTR, Interruptfreigaben, CTC-Zeitkonstante —
nichts davon ist rücklesbar, daher eine Tabelle je Maschine), EI. Ein Warmstart nach dem
Programm muss ein voll bedienbares System hinterlassen — auch nach Ctrl+C mitten im Empfang.

BIOS-Vorgaben (ST3, Tabellen `V_A1`…`V_K3` in `sertest.mac`, Format des CP/A-`portpr`):

| Schnittstelle | Vorgabe | Quelle |
|---------------|---------|--------|
| A5120 1 DFUE/V.24 (A33 A) | ZRE-CTC K0 17H/01H; 18H, WR4 44H, WR1 00H, WR3 C1H, WR5 EAH (9600 8N1, DTR + RTS) | CP/A TTY: an 50H (`iobtty 01811`, `bioscpb.mac`) |
| A5120 2 DFUE/IFSS (A33 B) | ZRE-CTC K0 17H/01H (geteilt mit 1); Kanalreset | vom BIOS nicht benutzt |
| A5120 3 Drucker (A32 B) | 18H, WR4 45H, WR1 00H, WR3 41H, WR5 28H (9600 7O1); CTC A34 bleibt | CP/A LPT: an 5EH (`ioblpt 11710`) |
| K8915 1 Drucker/IFSS1 (SIO1 B) | CTC1 K2 07H/01H; 18H, WR4 45H, WR3 41H, WR5 28H (9600 7O1) | Drucker-Init der Fassung „55 K" (Diskette 900); die Fassung „V24 XON/XOFF" (901) setzt 05H/01H, 44H/C1H/68H (8N1) — §AP-B2-Tabelle in `16_k8915.md` |
| K8915 2 V.24 (SIO1 A) | CTC1 K0 Reset 03H; Kanalreset | vom BIOS nicht benutzt |
| K8915 3 DFUE/IFSS2 (SIO2 A) | CTC2 K0 Reset 03H; Kanalreset | vom BIOS nicht benutzt |

CP/A programmiert TTY:/LPT:/UC1: **erst bei der ersten Benutzung** (Statusbit `ltpst`,
`cdsini`); SERTEST stellt die Werte her, die das BIOS dabei selbst schriebe — ein schon
benutzter Kanal arbeitet so weiter, ein noch nicht benutzter wird vom BIOS ohnehin neu
programmiert. Grenze: ein **aktiver UC1:-Treiber** an 50H (Interrupt) wird durch einen Test von
Schnittstelle 1 abgelöst; danach steht A33 A auf der TTY:-Vorgabe ohne Interrupt.

### 14.5 Prüfsteckertest

Prüfstecker = TxD→RxD, RTS→CTS, DTR→DSR+DCD (am Emulator: Rx/Tx-Loop, der laut §6.5 bei V.24
genau das schließt).

**DATEN-LOOP:** 256 Zeichen (alle Werte 00H–FFH), je Zeichen senden, Echo mit Zeitüberlauf
abwarten, vergleichen; Fehler = abweichend, fehlend, RR1-Fehler. Ohne Stecker (kein Echo) muss
**FEHLER** herauskommen, nicht OK.

**LEITUNGEN-LOOP** (nur V.24): RTS/DTR in allen vier Kombinationen setzen, CTS/DCD lesen
und mit der **Erwartungstabelle der Maschine** vergleichen. Die Z80-SIO hat keinen
DSR-Eingang; was „DTR→DSR" bewirkt, hängt an der Karte:

| RTS | DTR | A5120 CTS = V106∧V107 | A5120 DCD = V109∧V107 | K8915 CTS = V107∧(¬RTS∨V106) | K8915 DCD = V109 |
|-----|-----|-----------------------|-----------------------|------------------------------|------------------|
| 0 | 0 | 0 | 0 | 0 | 0 |
| 1 | 0 | 0 | 0 | 0 | 0 |
| 0 | 1 | 0 | 1 | **1** | 1 |
| 1 | 1 | 1 | 1 | 1 | 1 |

**K8915-Logik aus dem Stromlaufplan** (1.45.518732 Blatt 1, `k8915schaltung.pdf` S. 11;
ausgewertet 2026-10-01 mit einem Linienverfolger auf dem 568-dpi-Scan, Wege überlagert
gegengeprüft): /CTSA und /DCDA laufen **nicht** über den Multiplexer D13 (der schaltet nur
RxDA und die Takte RxCA/TxCA) und über **keine** Wickelbrücke.
- /DCDA = D17:01 1Y (Empfänger von **V109**, X3:A09) — direkt.
- /CTSA = D3:02 (T500 = 74S00) Pin 11 = NAND( NAND(¬/RTSA über D9:02, D17:01 3Y = V106),
  ¬D17:01 2Y = V107 über D9:03 ).
- Mit invertierenden Empfängern (P184, V.24 EIN → TTL L; offener Eingang = AUS) ergibt das:
  **CTS aktiv ⇔ V107 (DSR) EIN ∧ (RTS nicht gesetzt ∨ V106 EIN)**, **DCD aktiv ⇔ V109 EIN**.
  Sinn: „Senden erlaubt, wenn die DÜE bereit ist und — falls gesendet werden soll — CTS
  meldet". Die Polarität der P184 ist **angenommen** (wie bei jedem V.24-Empfänger), nicht
  aus einem Datenblatt belegt.
- Folge für den Prüfstecker: RTS ist an CTS **nicht** beobachtbar (Zeile 0/1 → CTS = 1, weil
  ohne RTS-Wunsch V106 nicht zählt). Ein klemmender RTS-Treiber (immer EIN) fällt am
  Prüfstecker nicht auf; ein ausgefallener (immer AUS) oder ein toter V106-Empfänger schon
  (Zeile 1/1 → CTS = 0).
- **Emulatormodell berichtigt (AP-ST4):** `K7028` setzte /CTSA ← V106 und ließ V107 weg
  (mit Loop CTS = RTS). Jetzt merkt sich die Karte V106/V107 aus `setzeEingaenge` und bildet
  /CTSA = V107 ∧ (¬RTSA ∨ V106) in `K7028::bildeCtsA` — **auch bei jedem Schreiben in SIO 1**,
  denn RTSA ist ihr eigener Ausgang und ändert sich ohne Wechsel am Stecker; /DCDA = V109.
  Mit Loop also CTS = DTR. Wächter `K7028Seriell.CtsANachPlanlogik` (alle 16 Kombinationen
  RTS/V106/V107/V109, RTS-Wechsel allein, Reset), `K7028Seriell.LoopLiefertCtsGleichDtr`,
  `Sertest.K8915_PruefsteckerMitLoopAnAllenSchnittstellenOk` (Rohzeilen).
- **Ausgabe von SERTEST** (ST4): je Kombination eine Rohzeile
  `  RTS=r DTR=d  CTS=c DCD=d  erwartet  CTS=c DCD=d  RR0=xxH` (bei Abweichung `  FALSCH`
  dahinter), danach die Ergebniszeile, im Fehlerfall `FEHLER RTS=r DTR=d` (erste
  abweichende Kombination). Zwischen Setzen und Lesen liegen 10 ms.

### 14.6 Ablaufprotokoll Tester ↔ Gegenstelle

Leitungen und Daten teilen sich dieselben Steuerleitungen (RTS ist erst Quittung, später
Bremse). Die Gegenstelle braucht deshalb Abschnitte, die der Tester ankündigt:

- **Ruhezustand der Gegenstelle = Leitungsspiegel** (§14.7 LEITUNGEN) bei gleichzeitig
  scharfem Empfang (9600 8N1, Interrupt).
- **Ankündigung** (Tester → Gegenstelle): `1BH 'S' m nL nH s` — `m` = `E` (Echo), `H`
  (Fluss über Leitungen), `X` (Fluss XON/XOFF); `n` = Anzahl der folgenden Nutzbytes; `s` =
  Summe der vier Bytes davor mod 256. Kaputte Ankündigung → verworfen, die Gegenstelle bleibt
  im Ruhezustand.
- **Bestätigung** (Gegenstelle → Tester): `1BH 'A' m s`.
- Danach genau `n` Nutzbytes — durch die Länge ist ein 1BH in den Daten kein Problem.
- **Bericht** (Gegenstelle → Tester) nach dem letzten Echo: `1BH 'B' m ueL ueH s` —
  `ue` = Empfangsfehler der Gegenstelle (RR1 + Pufferüberlauf).
- Danach zurück in den Ruhezustand. Zeitüberlauf auf beiden Seiten (Tester: FEHLER;
  Gegenstelle: Ruhezustand nach einigen Sekunden ohne Byte).

Nutzdaten: Pseudozufallsfolge (LFSR mit festem Startwert), im Modus `X` ohne 11H/13H.

**Festgelegt in ST5:**
- **Prüfsumme `s`** = Summe (mod 256) aller Bytes **zwischen 1BH und `s`** — bei der
  Ankündigung also `'S' + m + nL + nH` („die vier Bytes davor"), bei der Bestätigung
  `'A' + m`, beim Bericht `'B' + m +` alle Felder.
- **Bericht, erweiterbar:** `1BH 'B' m <Felder> s`, jedes Feld 16 Bit (L, H). **Wie viele
  Felder folgen, legt der Modus fest** — der Tester liest genau so viele, wie der von ihm
  angekündigte Modus vorsieht. Kein Längenbyte: der Tester kennt den Modus, und ein falsch
  gezählter Bericht fällt an der Summe auf. **Festgelegt (ST6):**

  | Modus | Bericht | Felder |
  |-------|---------|--------|
  | `E` | `1BH 'B' 'E' ueL ueH s` | `ue` |
  | `H`, `X` | `1BH 'B' m ueL ueH bzL bzH s` | `ue`, `bz` = wie oft die Gegenstelle gebremst hat (RTS weggenommen bzw. XOFF gesendet) |

  `s` = `'B' + m + ueL + ueH (+ bzL + bzH)` mod 256.
- **Weckzeichen:** der Tester schickt vor der Ankündigung ein 00H und wartet 200 ms. Die
  Gegenstelle meldet den ersten Empfangsinterrupt auf dem Bildschirm, und SCPX 8915 rollt
  das Bild unter DI (≈ 16 ms, §14.9 ST3) — am Gerät liefe dabei der 3-Zeichen-FIFO der SIO
  über, die Ankündigung käme verstümmelt an. Alles vor 1BH verwirft die Gegenstelle.
- **Keine Bildschirmausgabe während einer Übertragung** (aus demselben Grund): die
  Gegenstelle schreibt nur **vor** der Bestätigung (`Abschnitt E: 1000H Bytes`) und **nach**
  dem Bericht (`Abschnitt fertig, Empfangsfehler 0000H`), der Tester erst nach dem Bericht.
- **Fristen:** Tester — Bestätigung 3 s, Echo 2 s ohne Byte, Bericht 3 s; Gegenstelle —
  Ankündigung 500 ms je Byte, Abschnitt 3 s ohne Byte (`Zeitueberlauf, zurueck in den
  Ruhezustand.`). Fremdes vor `1BH 'A'` bzw. `1BH 'B'` übergeht der Tester — auch die
  eigene Ankündigung, die an einem Prüfstecker zurückkommt (dann: Zeitüberlauf).
- **LFSR:** 16-Bit-Galois, x¹⁶+x¹⁴+x¹³+x¹¹+1 (Maske B400H), Startwert **ACE1H**, je Nutzbyte
  8 Schritte, Ausgabe = niederes Byte. Nur der Tester kennt die Folge (er vergleicht das
  Echo); die Gegenstelle schickt zurück, was kommt.
- **Modi:** `E`, `H` (nur an V.24 — an IFSS verwirft die Gegenstelle die Ankündigung),
  `X` (seit ST6). Unbekannte Modi verwirft die Gegenstelle (`Ankuendigung verworfen.`).
- **Ergebnisgründe ECHO:** `SENDER BLOCKIERT`, `ZEITUEBERLAUF BESTAETIGUNG`, `BESTAETIGUNG
  FALSCH`, `ZEITUEBERLAUF ECHO BEI nnnnH` (so viele kamen zurück), `ZEITUEBERLAUF BERICHT`,
  `BERICHT FALSCH`, `FALSCH nnnnH` (abweichende Echos), `RR1 nnnnH` (eigene Empfangsfehler),
  `GEGENSTELLE nnnnH` (`ue` laut Bericht); bei `H`/`X` zusätzlich `NICHT GEBREMST` (`bz` = 0).
  Dieselben Gründe gelten für FLUSS-HW und FLUSS-XON (gleicher Ablauf, anderer Modus).

**Kabel:** Nullmodem — TxD↔RxD gekreuzt, RTS→CTS gekreuzt, DTR→DSR+DCD gekreuzt, Masse.
IFSS: Sendeschleife des einen an die Empfangsschleife des anderen (aktiv/passiv nach Gerät).
Die genaue Steckerbelegung je Gerät steht im README des Programms **[?]** (liefert der Anwender).

### 14.7 Test mit Gegenstelle (Tester-Schritte)

1. **LEITUNGEN** (nur V.24): Der Tester setzt nacheinander RTS, nimmt RTS weg, setzt DTR,
   nimmt DTR weg — jeweils nach einer beliebigen Taste (bei `/A` ohne Warten). Die Gegenstelle
   zeigt ihre Eingänge an (`CTS=1 DCD=0`, nur bei Änderung) und spiegelt sie sofort:
   CTS_ein → RTS_aus, DCD_ein → DTR_aus. Der Tester zeigt je Schritt
   `Gegenstelle bestaetigt: JA/NEIN`, Erwartung aus derselben Tabelle wie §14.5 (über das
   Nullmodemkabel ist „eigenes RTS kommt als eigenes CTS zurück" dieselbe Logik wie am
   Prüfstecker, nur über den Umweg).
   **Umgesetzt (ST5), mit drei Präzisierungen:**
   - „CTS_ein → RTS_aus" heißt **Eingang → Ausgang**, nicht „CTS an ⇒ RTS aus": die
     Gegenstelle setzt ihr RTS auf ihr CTS und ihr DTR auf ihr DCD (`LEISPI`).
   - **Schrittfolge** `RTS/DTR` = 00 (Grundstellung), 01 (DTR setzen), 11 (RTS setzen),
     01 (RTS wegnehmen), 00 (DTR wegnehmen): an der A5120 ist CTS = V106 ∧ V107, eine
     RTS-Flanke ist dort nur bei gesetztem DTR sichtbar — die wörtliche Folge „RTS setzen,
     wegnehmen, dann DTR" prüfte RTS gar nicht. Am K8915-Tester bestätigt Zeile 11 den
     Rückweg (CTS = V107 ∧ (¬RTS ∨ V106) = gespiegeltes RTS).
   - **K8915 als Gegenstelle — Widerspruch zur Tabelle §14.5 aufgelöst:** dort ist
     CTS = V107 ∧ (¬RTS ∨ V106), hängt also am **eigenen** RTS. Wörtlich „RTS := CTS"
     schwingt bei V107 = 1, V106 = 0 (RTS aus → CTS 1 → RTS an → CTS 0 → …) — genau im
     Schritt 01. Schlüssig ist: gespiegelt wird **CTS bei gesetztem eigenem RTS**
     (= V106 ∧ V107, wie an der K8025). Mit RTS an wird direkt gelesen; mit RTS aus setzt
     die Gegenstelle alle ~64 ms für einige zehn µs RTS, liest und nimmt es zurück
     („Probe"), dazwischen gilt CTS = 0. Den kurzen RTS-Puls sieht ein A5120-Tester nur bei
     gesetztem DTR — er wertet deshalb erst **drei gleiche Lesungen im Abstand von 10 ms**
     als Bestätigung (Frist 2 s je Schritt).
   - Anzeige der Gegenstelle: `  CTS=c DCD=d` (das gespiegelte Paar, also beim K8915 das
     geprobte CTS), nur bei Änderung. Tester je Schritt
     `  RTS=r DTR=d  CTS=c DCD=d  erwartet  CTS=c DCD=d  Gegenstelle bestaetigt: JA|NEIN`,
     ohne `/A` davor „Weiter mit beliebiger Taste."; Ergebnis `OK` bzw. `FEHLER RTS=r DTR=d`
     (erster unbestätigter Schritt). Danach RTS + DTR wieder an.
2. **ECHO:** Ankündigung `E`, 4096 Nutzbytes, 9600 8N1; der Tester sendet und empfängt
   gleichzeitig (polled), vergleicht das Echo Byte für Byte, wertet den Bericht aus.
   Die Gegenstelle empfängt **per Interrupt** in einen Ringpuffer (256 B), das Hauptprogramm
   schickt zurück; beim ersten Empfangsinterrupt einmal `Empfangsinterrupt ausgeloest`
   (und `SERTEST INTERRUPT OK`).
3. **FLUSS-HW** (nur V.24): Ankündigung `H`. Die Gegenstelle erzeugt Rückstau: sie hält ihr
   Zurückschicken regelmäßig an (Pause ~300 ms je 512 Byte), nimmt bei Puffer > 192 Byte RTS
   weg und setzt es bei < 64 Byte wieder. Der Tester sendet mit **Auto Enables** (WR3 D5),
   sein Sender hält also an, solange CTS fehlt. OK = Echo fehlerfrei **und** die Gegenstelle
   hat mindestens einmal gebremst (Bremszähler `bz` im Bericht, §14.6) **und** kein Überlauf.
4. **FLUSS-XON** (V.24 und IFSS): wie 3., aber die Gegenstelle sendet XOFF (13H) bei
   > 192 Byte und XON (11H) bei < 64; der Tester hält sein Senden bei XOFF an. Auf V.24 sind
   dabei die Auto Enables **aus**, damit nur XON/XOFF wirkt.

   **Umgesetzt (ST6), Präzisierungen zu 3./4.:**
   - **Ein Ablauf, drei Modi:** ECHO, FLUSS-HW und FLUSS-XON sind dieselbe Routine (`ECHOT`,
     Modus `E`/`H`/`X`): Weckzeichen, Ankündigung, 4096 Nutzbytes mit Echovergleich, Bericht.
     Urteil zusätzlich `bz` ≥ 1, sonst `FEHLER NICHT GEBREMST`; „kein Überlauf" = eigene
     RR1-Fehler 0 **und** `ue` der Gegenstelle 0 (RR1 + Ringpufferüberlauf).
   - **Rückstau der Gegenstelle:** vor jedem 512. zurückgeschickten Byte 300 ms Pause; der
     Füllstand des Ringpuffers (256 B) wird nach jedem Byte und in der Pause jede ms geprüft
     (`FLUSTE`): > 192 bremsen, < 64 lösen. In 4096 Bytes ergibt das 7 Bremsungen (die
     achte Pause fällt hinter das letzte Byte des Testers).
   - **FLUSS-HW:** Auto Enables (`AUTOEN`) erst **nach** der Bestätigung — ohne Gegenstelle
     hieße es sonst `SENDER BLOCKIERT` statt Zeitüberlauf; am Ende (auch im Fehlerfall)
     wieder aus. Das eigene RTS bleibt gesetzt: am K8915 ist CTS = V107 ∧ (¬RTS ∨ V106),
     mit gesetztem RTS also V106 ∧ V107 wie an der K8025. Die Gegenstelle nimmt nur RTS
     weg, DTR bleibt (V107 des Testers); am Ende des Abschnitts stellt sie die Leitungen
     von vorher her, der Spiegel des Ruhezustands macht dort weiter. **Keine RTS-Probe
     während eines Abschnitts** (§14.10 Punkt 7): Spiegel und Probe laufen nur im
     Ruhezustand.
   - **FLUSS-XON:** Nutzdaten ohne 11H/13H — das LFSR überspringt sie auf beiden Seiten
     gleich (`NUTZB`), die Folge bleibt also vergleichbar. Der Tester wertet 13H/11H im
     Empfang als Steuerung (zählt nicht als Echo), die Auto Enables bleiben aus. Nach dem
     Bericht schickt die Gegenstelle ein **XON hinterher**: Bericht und Summe können
     zufällig 13H enthalten, und mit „XON/XOFF beachten" hielte das ihren eigenen Empfang
     an, bis zur nächsten Ankündigung (§6.3). Der Tester übergeht das XON (Fremdes vor
     `1BH`).
   - Der Tester zeigt vor dem Urteil `  Gegenstelle hat nnnnH mal gebremst.`, die
     Gegenstelle `Abschnitt fertig, Empfangsfehler nnnnH, gebremst nnnnH`.

Am Ende jedes Teils die Ergebniszeile §14.3.

### 14.8 Einbindung ins Testsystem

- **Quelle im Projekt:** `tools/sertest/` nach dem Muster von `tools/romread/`:
  `src/sertest.mac`, `build.py` (M80 + LINKMT über `cparun` aus `CPA_Workbench/tools`,
  überschreibbar mit `CPA_TOOLS`), `README.md` (Bedienung, Kabel, Annahmen), und die
  gebaute **`tools/sertest/sertest.com` eingecheckt** — die CI hat die CPA_Workbench nicht.
  Ein Wächter prüft, dass die eingecheckte `.com` zur Quelle passt, wenn die Werkzeugkette
  vorhanden ist (sonst übersprungen, mit Meldung) — umgesetzt (ST2) als
  `cli_sertest_com_passt_zur_quelle` = `build.py --check` (baut in ein Temp-Verzeichnis,
  vergleicht bytegleich; ohne Werkzeugkette Exit 77 = ctest `SKIP_RETURN_CODE`).
- **Tests** (`tests/system/`, `k1520_add_test()`): Systemdiskette als `TempDisk`,
  `SERTEST.COM` mit `DiskVolume` darauf, booten, Aufruf per `typeString()`, Ergebniszeilen
  aus dem Bildschirm (`vramText()` bzw. `k1520_screen_char` beim K8915).
  - **Prüfstecker:** eine Maschine, Loop an; alle Schnittstellen beider Maschinen; dazu ein
    Gegenfall ohne Loop (muss FEHLER melden).
  - **Gegenstelle:** zwei Maschinen im selben Prozess wie `SerielleKopplung.*`, RFC 2217
    Server ↔ Client (Nullmodem-Kreuzung, §6.4), „XON/XOFF beachten" an für `FLUSS-XON`.
    Telnet taugt nur für ECHO/FLUSS-XON (keine Leitungen).
  - Schnelle Fälle in die Standardregression (Ziel < 5 s je Fall), lange (Kopplung mit Boot
    beider Maschinen) unter `format_integration` → `tools/dev.sh test-format`.
  - Umgesetzt (ST5): `SertestKopplung.*` (`tests/system/test_sertest_kopplung.cpp`), zwei
    Binaries aus einer Quelle — `A5120_V24_LeitungenUndEcho` in der Standardregression
    (`system;fast`, ~3 s, seit ST6 mit FLUSS-HW/FLUSS-XON ~4,7 s), die übrigen unter
    `format_integration` (`…_lang`).
  - „XON/XOFF beachten" (ST6): **nur** am Wandler der Gegenstelle und **nur** für den
    Abschnitt X — der Test schaltet es ein, sobald die Gegenstelle `Abschnitt X:` zeigt,
    und nach dem Durchgang wieder aus. ECHO und FLUSS-HW übertragen alle Bytewerte; ein
    zurückgeschicktes 13H hielte mit dem Schalter den eigenen Empfang an (Zeitüberlauf).
    Für den Anwender heißt das: für ECHO/FLUSS-HW aus, für FLUSS-XON an (oder aus mit dem
    Risiko eines Überlaufs, wenn das Netz langsam ist — dann sind die 63 Byte Reserve
    über 192 zu wenig).
  - Umgesetzt (ST2): Suite `Sertest.*`, Label `system;fast` — läuft also in der
    Standardregression mit, obwohl sie in `tests/system/` liegt (ein Boot bis zum Prompt
    kostet 1–2 s). Pfad der `.com` über die Definition `K1520_SERTEST_COM`.

### 14.9 Arbeitspakete

Jedes AP: Tests grün (`tools/dev.sh test`, bei ST5/ST6 zusätzlich `test-format`), dieses
Kapitel nachführen („erledigt JJJJ-MM-TT", Abweichungen, geklärte **[?]**), Commit.
Bauberührende APs nacheinander.

| AP | Inhalt | hängt ab von | Umfang |
|----|--------|--------------|--------|
| **ST1** ✔ | Gerüst: `tools/sertest/` (Quelle, `build.py`, README, eingecheckte `.com`), Kopfzeile, Rollenwahl, Kommandozeile §14.3 (inkl. `/A`, `/M:`), Konsole über BDOS 6, Ctrl+C-Pfad mit Aufräumhaken, Maschinenerkennung + Schnittstellenliste §14.4 (Tastatur gekennzeichnet), Abfrage J/N je Schnittstelle, Hinweistext Gegenstelle | — | M |
| **ST2** ✔ | Testgerüst im Emulator: Hilfen „Disk mit SERTEST.COM", „Ergebniszeilen lesen" für A5120 (CP/A) und K8915 (SCPX); erste Fälle: Liste und Erkennung auf beiden Maschinen, Ctrl+C hinterlässt bedienbares System; Wächter „`.com` passt zur Quelle" | ST1 | M |
| **ST3** ✔ | SIO-/CTC-Schicht §14.4: 9600 8N1 je Tabelle, Leitungen, polled E/A mit Zeitüberlauf, Fehlerzählung, Interrupt einhängen/weiterreichen, Wiederherstellen; klärt die **[?]** zu WR1/WR2 und BIOS-Vorgaben aus den BIOS-Quellen | ST1 | M |
| **ST4** ✔ | Prüfsteckertest §14.5 (DATEN-LOOP, LEITUNGEN-LOOP mit Erwartungstabelle je Maschine, gemessene Werte zusätzlich roh ausgeben) + **K7028 berichtigen**: /CTSA nach der Plan-Logik §14.5 (V107 in `setzeEingaenge` auswerten, Loop/Brücke: CTS = DTR), Wächter dafür + Tests: alle Schnittstellen beider Maschinen mit Loop, Gegenfall ohne Loop, Drucker-A5120 lässt Tastatur intakt | ST2, ST3 | M |
| **ST5** ✔ | Protokoll §14.6, Gegenstelle (Auswahl einer Schnittstelle, Leitungsspiegel mit Anzeige, Interrupt-Empfang + einmalige Meldung, Echo), Tester-Schritte LEITUNGEN + ECHO + Tests mit zwei gekoppelten Maschinen (RFC 2217) | ST4 | L |
| **ST6** ✔ | Flusssteuerung §14.7 Schritte 3–4: Rückstau der Gegenstelle, FLUSS-HW (Auto Enables), FLUSS-XON (ohne 11H/13H in den Daten), Berichtsformat mit Bremszähler + Tests (V.24 beide, IFSS nur XON) | ST5 | M |
| **ST7** | Abschluss: README (Bedienung, Kabelbelegung vom Anwender, Annahmen Brücken), Merkposten-Absatz in `doc/merkposten/serielle_schnittstellen.md`, Checkliste für die **Geräteprüfung durch den Anwender** (A5120 ↔ K8915 per Kabel, Prüfstecker an jedem Stecker) | ST6 | S |

**ST6 erledigt 2026-10-01.** Flusssteuerung in `sertest.mac` (Tester `ECHOT` mit Modus,
`NUTZB`; Gegenstelle `ECHOAB` mit `PAUSE`, `FLUSTE`, `FLUEND`), Bericht mit Bremszähler
(§14.6), Präzisierungen §14.7. `.com` jetzt 6,9 KB. Erkenntnisse:
- **Kein Emulatorfehler.** Auto Enables im `Z80SIO` (Sender gibt bei inaktivem /CTS nichts
  ab, Empfänger nur bei /DCD; Wächter `Z80SIO.AutoEnables_*` aus AP-S3), RTS-Halt und
  „XON/XOFF beachten" im Wandler trugen ohne Änderung.
- **Gebremst wird wirklich, und der Sender steht:** der Kopplungstest beobachtet den Wandler
  des Testers während FLUSS-HW — CTS fiel 7×, in ~600 Runden mit CTS aus gab der Tester
  0 Bytes ab. Gegenprobe: mit abgeschalteten Auto Enables im `Z80SIO` waren es 1361, der
  Fall wird rot. Ohne diese Beobachtung bliebe der Test auch ohne Auto Enables grün — der
  Rückstau landete verlustfrei in den Wandlerpuffern (4 KiB je Seite fassen die 4096 Bytes).
- **„XON/XOFF beachten" ist für FLUSS-XON die Absicherung, nicht die Voraussetzung:** ohne
  Schalter lief FLUSS-XON im Einzellauf ebenfalls `OK` (die Latenz XOFF → Tester blieb unter
  den 63 Bytes Reserve); unter Last hinge das an der Weckzeit der I/O-Fäden. Mit dem
  Schalter hält der Wandler der Gegenstelle sofort an — der Test prüft, dass dieser Halt
  griff (`xoffHalt`).
- `TesterAutomatikLiefertErgebniszeilen` (beide Maschinen): FLUSS-XON jetzt
  `FEHLER ZEITUEBERLAUF BESTAETIGUNG`, FLUSS-HW an IFSS `ENTFAELLT`, kein `NICHT EINGEBAUT`
  mehr (geprüft). Das Ende bleibt `FEHLER` — **begründet**: ohne zweiten Rechner können die
  Gegenstellenteile nicht gelingen (A5120 ohne Kabel, K8915 mit Loop: die eigene Ankündigung
  kommt zurück und gilt nicht). `SERTEST ENDE OK` erreichen die Kopplungsfälle.
- Tests (einzeln, ohne Last): `A5120_V24_LeitungenUndEcho` 4,7 s (Standardregression; unter
  `-j16` bis ~12 s), `A5120_Ifss_Echo` 3,6 s, `K8915_V24UndIfss2` 9,6 s,
  `A5120_K8915_V24_BeideRichtungen` 10,1 s, `OhneGegenstelleZeitueberlauf` 4,0 s (ECHO,
  FLUSS-HW, FLUSS-XON je `ZEITUEBERLAUF BESTAETIGUNG`). Die Gegenstellenzeilen werden während
  des Laufs eingesammelt — mit drei Abschnitten rollt das Bild der Gegenstelle.

**ST1 erledigt 2026-10-01.** `tools/sertest/` (Quelle, `build.py`, README, eingecheckte
`sertest.com`, 2,9 KB). Abweichungen/Erkenntnisse:
- Ausgabe über BDOS 6 statt 9 (Begründung §14.1).
- Die Teststeps sind Platzhalter mit Ergebniszeile `FEHLER NICHT EINGEBAUT` (nicht
  `ENTFAELLT` — das bleibt „gilt für diese Schnittstelle nicht"); `ENTFAELLT` für
  LEITUNGEN-LOOP/LEITUNGEN/FLUSS-HW an IFSS ist schon richtig. Zusammenfassung je
  Schnittstelle `OK`/`FEHLER`/`nicht geprueft`.
- `/A` und `/P`/`/G` nur mit `T n`; `/P`/`/G` mit `G n` ist ein Fehler, `/A` dort erlaubt.
  `/M:` in jeder Form. Nummer gegen die Liste geprüft („Diese Schnittstelle gibt es nicht."
  + Kurzhilfe).
- `build.py` bricht ab, wenn M80 nicht „No Fatal error(s)" meldet (M80 endet auch bei
  Fehlern mit 0 und schreibt eine `.ERL`; `tools/romread/build.py` hat diese Lücke noch).
  Ohne `ORG` gebaut — `romread.com` trägt wegen `ORG 100H` + `/p:100` 256 Byte NOPs vorn.
- `k1520dbg keys` am A5120: Steuerzeichen (`\x03`, `\e`, `\t`) wurden von
  `K7637::translateKey` verworfen; gehen jetzt als Rohbyte (`QK_RAW_BASE`) — damit ist
  Ctrl+C im Stapelbetrieb prüfbar. Für ST2: im C++-Test `keyPress('c', false, true)`.
- Im Emulator geprüft: interaktiv T/G mit J/N-Folge, `T n /A`, `G n` + Ctrl+C, Ctrl+C an der
  T/G-Frage, `SERTEST X` und `T 4` → Kurzhilfe, `/M:K` am A5120; danach `DIR` bedienbar.
  K8915-Kaltstart: `<ENTER>` an „Coldstart … --> <ENTER>", dann Autostart `rade` abwarten.

**ST5 erledigt 2026-10-01.** Protokoll §14.6, Gegenstelle und Tester-Schritte LEITUNGEN + ECHO
in `sertest.mac` (`GEGTST` → `LEITG`, `ECHOT`; Gegenstelle `GE_RUH` → `LEISPI`, `ANKLES`,
`ECHOAB`; Pakete `SENDPK`/`EMPPK`, `LFSR`). `.com` jetzt 6,1 KB. Festlegungen in §14.6
(„Festgelegt in ST5") und §14.7 Schritt 1. Erkenntnisse:
- **Emulatorfehler K8025: kein RETI.** `K8025` überschrieb `InterruptSlave::onRETI` nicht —
  das RETI kam bei keinem ihrer Bausteine (SIO A33, SIO A32, CTC A34) an, ein einmal
  quittierter Interrupt blieb für immer „under service". Symptom: die Gegenstelle am A5120
  meldete `SERTEST INTERRUPT OK`, empfing danach aber nur noch, was in den FIFO passte
  (4 Bytes: 1 abgeholt + 3). Der ST3-Fall `A5120_DfueV24EmpfaengtImInterrupt` schickte
  genau 4 Zeichen und bemerkte es deshalb nicht. Behoben (`K8025::onRETI` reicht an alle
  drei weiter), Wächter `K8025.RetiGibtDenSioWiederFrei` (rot ohne den Fix); die beiden
  A5120-Interruptfälle aus ST3 schicken jetzt 8 Zeichen (der Druckerfall wird ohne Fix rot).
  CP/A betreibt die Kanäle gepollt — deshalb fiel es vorher nie auf.
- **Leitungsspiegel ohne Kabel:** an der V.24 spiegelt die Gegenstelle „alles aus" — der
  ST3-Fall `…GegenstelleProgrammiert9600_8N1…` erwartet dort jetzt `rts=0 dtr=0` (A5120,
  kein Kabel) bzw. `rts=1 dtr=1` (K8915, Loop = CTS/DCD an).
- **K8915: Loop beim Koppeln.** Der K8915 startet mit Loop an (§6.5); `start` wird dann
  abgewiesen — der Kopplungstest prüft das ausdrücklich und schaltet den Loop vorher ab.
- **Uhr gegen Maschinenzeit:** Daten laufen mit Rückstau durch die Wandler, die
  Steuerleitungen aber über die I/O-Fäden (Uhrzeit). Die Paarschleife des Tests lässt beide
  Maschinen in gleichen Scheiben (10 000 Takte) abwechselnd laufen und drosselt auf ≤ 8×
  Echtzeit; ohne Drossel lägen die 2-s-Fristen unter `ctest -j` in der Größenordnung einer
  Faden-Weckzeit.
- `SertestA5120`/`SertestK8915`: `lauf(takte)` (gleiche Scheiben trotz verschiedener
  Schrittweiten 5 000 / 100 000), `erfasse()`, `takt()`.
- Tests (Laufzeit einzeln, ohne Last): Standardregression `A5120_V24_LeitungenUndEcho`
  2,8 s; `format_integration`: `A5120_Ifss_Echo` 2,6 s, `K8915_V24UndIfss2` 6,8 s (V.24 mit der
  K8915-Probe, dann DFÜ/IFSS2 an der Tastatur-SIO), `A5120_K8915_V24_BeideRichtungen` 6,5 s
  (A5120-Tester sieht jede RTS-Flanke der K8915-Gegenstelle; dann umgekehrt),
  `OhneGegenstelleZeitueberlauf` 3,2 s (V.24 `FEHLER RTS=…`, ECHO `FEHLER ZEITUEBERLAUF
  BESTAETIGUNG`, danach `DIR`). Je 3× unter `-j16` wiederholt: grün.
  `TesterAutomatikLiefertErgebniszeilen`: ECHO jetzt `FEHLER ZEITUEBERLAUF BESTAETIGUNG`
  (A5120 ohne Kabel; K8915 mit Loop — die eigene Ankündigung kommt zurück und gilt nicht),
  FLUSS-XON weiter `NICHT EINGEBAUT`, Ende FEHLER.

**ST4 erledigt 2026-10-01.** Prüfsteckertest in `sertest.mac` (`PRUEFS` → `DLOOP`, `LLOOP`),
K7028 berichtigt (§14.5). Erkenntnisse/Abweichungen:
- **DATEN-LOOP:** vorher den Empfänger leeren (höchstens 64 Reste à 2 ms), dann je Zeichen
  `SENDE`/`EMPF` mit je 20 ms Frist. Fehlergründe: `KEIN ECHO BEI xxH` (bricht beim ersten
  fehlenden Echo ab — ohne Prüfstecker kostet das 20 ms statt 256 × 20), `FALSCH nnnnH`
  (Anzahl abweichender Echos), `RR1 nnnnH`, `SENDER BLOCKIERT`. Davor die Zeile
  `Daten-Loop: 256 Zeichen 00H-FFH ...`.
- **Ctrl+C im DATEN-LOOP:** ein Echo kommt in ~1 ms, `ZEITK` (und damit `KEYPOL`) liefe dann
  kaum je an; der Loop fragt deshalb **alle 16 Zeichen** selbst die Tastatur ab. Der ganze
  Loop dauert ~0,3 s Maschinenzeit.
- `ERGFE0` = `ERGFEH` ohne Zeilenende (Grund mit angehängten Werten), `PUTHEX`/`PUTHX4`.
- Tests (`Sertest.*`, jetzt 23; Laufzeit unter `-j16`): je Maschine
  `PruefsteckerMitLoopAnAllenSchnittstellenOk` (alle drei Schnittstellen nacheinander mit
  `T n /P /A`, LEITUNGEN-LOOP-Rohzeilen gegen die Tabelle, danach `DIR` — am A5120 teilt
  der Drucker die SIO mit der Tastatur; A5120 5,2 s, K8915 6,2 s — knapp über dem Ziel, ein
  Boot statt drei), `PruefsteckerOhneLoopMeldetFehler` (V.24: `KEIN ECHO BEI 00H`,
  `FEHLER RTS=…`, Ende FEHLER; 1,2/2,2 s), `CtrlCImDatenLoopLaesstSystemBedienbar`
  (Abbruch vor dem Urteil, Kanal auf der BIOS-Vorgabe, `DIR`; 1,8/4,8 s).
  `TesterAutomatikLiefertErgebniszeilen` bleibt bei Ende FEHLER (Gegenstellen-Teile bis ST5
  `NICHT EINGEBAUT`, jetzt ausdrücklich geprüft).
- Hilfe `neuerLauf()` (`sertest_hilfen.h`): mehrere Läufe in einer Sitzung — alte
  `SERTEST …`-Zeilen per CR am Prompt aus dem Bild rollen, Protokoll leeren; sonst fände
  `bisEnde` das alte `SERTEST ENDE` sofort wieder.
- K8915 V.24 nach Ctrl+C (Kanal- + CTC-Reset) meldet der Wandler `0 5N1,5 (ungueltig)` —
  das ist der Einschaltzustand, nicht ein Fehler.

**ST3 erledigt 2026-10-01.** SIO-/CTC-Schicht in `sertest.mac` (Klärungen §14.4). Routinen für
ST4–ST6 (Konvention: `IFZEIG` zeigt auf die Schnittstelle, Kommentarblock vor `SIOPRG`):
`SIOPRG` (9600 8N1, RTS + DTR, merkt `AKTIF`), `SETLEI` (A = `W5_RTS`/`W5_DTR`), `AUTOEN`
(A ≠ 0 = Auto Enables), `LIESLEI` (A = RR0 frisch: `RR0_CTS`, `RR0_DCD`), `SENDE` (A, DE = Frist
ms → CY = Zeitüberlauf), `EMPF` (DE → A, CY), `WARTE` (DE ms), `EINH`/`AUSH` (Interrupt),
`RXHOL` (A aus dem Ringpuffer 256 B, CY = leer), Zähler `FEHLZ` (RR1), `INTZ`, `RXUEB`;
`AUFR` stellt alles her (DI → `AUSH` → `PORTPR` der Vorgabe → EI) und läuft am Ende jeder
Schnittstelle des Testers, bei jedem Ende und bei Ctrl+C. Erkenntnisse/Abweichungen:
- **Zeitbasis gemessen** (A5120, `k1520dbg`, 1000 ms): `WARTE` 2 637 741, `EMPF`-Zeitüberlauf
  2 625 528 Takte statt 2 457 600 — +7 % durch die BIOS-Interrupts und die Tastaturabfrage
  alle 16 ms. Fristen sind Mindestzeiten.
- Der Tester programmiert die Schnittstelle für die Dauer ihrer Prüfung (Teile bleiben bis ST4
  Platzhalter). Die **Gegenstelle** hängt sich schon jetzt ein und meldet den ersten
  Empfangsinterrupt (`Empfangsinterrupt ausgeloest` + `SERTEST INTERRUPT OK`, aus ST5
  vorgezogen, damit die Schicht prüfbar ist); empfangene Zeichen werden verworfen.
- **Drei Fehler im `Z80SIO`** gefunden und behoben (Wächter in `test_sio.cpp`):
  (1) Lesen von **RR2 B quittierte** (rief `getVector()`: IUS gesetzt, Anforderung gelöscht) —
  jetzt `rr2Vektor()` ohne Seitenwirkung, ohne Anforderung V3–V1 = 011 (`RR2_LesenQuittiertNicht`);
  (2) nach dem Abholen eines Zeichens ging die **Anforderung für weitere Zeichen im FIFO
  verloren** — am K8915 blieb die Tastatur stumm, sobald sich während eines langen DI zwei
  Bytes gestaut hatten (`RX_Interrupt_GestauteZeichenUnterbrechenEinzeln`);
  (3) **Überlauf stand in RR1 D3** statt D5 und ließ sich nicht per Error Reset löschen
  (`RX_FIFO_Full`).
- **SCPX 8915 rollt den Bildschirm unter DI** (`LDIR` 0730H Bytes ≈ 39 000 Takte, DE19–DE2BH der
  Fassung 900): vier Tastaturbytes (Strg + C) in dieser Zeit laufen auch am Gerät über, Strg
  bliebe „unten". Tests drücken Ctrl+C deshalb erst, wenn das Bild steht.
- Neue Fälle (`Sertest.*`, jetzt 17): je Maschine `GegenstelleProgrammiert9600_8N1UndStelltDieBiosVorgabeHer`
  (alle drei Schnittstellen nacheinander: Wandlerstatus 9600 8N1, V.24 mit RTS/DTR; nach
  Ctrl+C die Vorgabe — A5120 1 = 9600 8N1, 3 = 9600 7O1, K8915 1 = 9600 7O1, sonst wie vor dem
  Programm; danach `DIR`) und zwei Interruptfälle — an der mit der Tastatur geteilten SIO
  (A5120 Drucker, K8915 DFUE/IFSS2) und an einer mit eigenem Vektor (A5120 DFUE/V.24, K8915
  V.24): vier Zeichen über `Wandler::fernGib` → `SERTEST INTERRUPT OK`, alle zugestellt,
  Ctrl+C, Vektortabelle (I·256) bytegleich wie vorher, `DIR`.
- Kein eigener Fall „Zeitüberlauf ohne Gegenstelle": den liefert ST4 mit DATEN-LOOP ohne Loop.

**ST2 erledigt 2026-10-01.** Suite `Sertest.*` (`tests/system/test_sertest.cpp`, Label
`system;fast`, 11 Fälle) + Wächter `cli_sertest_com_passt_zur_quelle` (`cli;fast`).
Je Maschine: `ErkenntRechnerUndListetSchnittstellen` (Kopf, „Rechner: … " ohne
„(vorgegeben)", alle Einträge + Tastatur, Ctrl+C an T/G), `FalscheKommandozeileGibtKurzhilfe`,
`CtrlCAnDerRollenfrageLaesstSystemBedienbar`, `CtrlCInDerGegenstelleLaesstSystemBedienbar`
(`G 1`, danach `dir sertest.com`), `TesterAutomatikLiefertErgebniszeilen` (`T n /A` an einer
IFSS: alle sechs Teile, LEITUNGEN-LOOP/FLUSS-HW `ENTFAELLT`, Ende `FEHLER` solange Teile fehlen —
**ab ST4 anpassen**); dazu `ZerlegtErgebniszeilen` ohne Maschine. Laufzeit unter `-j16`:
A5120 1,3–2,1 s, K8915 2,2–4,8 s je Fall (die Ctrl+C-Fälle mit `DIR` am längsten); ein
in der Suite geteilter Boot bringt unter ctest nichts (jeder Fall ist ein eigener Prozess).
Hilfen für ST4–ST6 (`tests/system/sertest_hilfen.h`, Namensraum `sertest`):
- `SertestA5120` / `SertestK8915` (gleiche Oberfläche, Schablonen über beide): Konstruktor
  kopiert die Fixture als `TempDisk`, spielt `SERTEST.COM` per `DiskVolume::insert` auf
  (`aufspielen()`, ohne `…~`-Sicherungskopie) und mountet A:; `kaltstart()`, `tippe("…\r")`,
  `ctrlC()`, `bis(text)`, `bisEnde()`, `bisPrompt()`, `kommando()`, `dirFindetSertest()`,
  `bild()`, `lauf()`, `maschine()` (für `serialHub()`). K8915-Kaltstart =
  `k8915test::kaltstartBisPrompt` (Selbsttest übersprungen, `<ENTER>`, Autostart `rade`).
- `SertestProtokoll` sammelt die Ergebniszeilen während des Laufs (auch herausgerollte):
  `wert(name, teil)`, `ende()`, `zeilen()`, `text()`. **Falle:** zeichenweise Ausgabe und
  das Rollen (Blockverschieben über ~40 000 Takte) erzeugen zerrissene Zwischenbilder
  (`SERTEST DFUE/IFSS DATEN-LOOPKanal A   (Tastatur)`); eine Zeile gilt deshalb erst nach
  500 000 Takten ununterbrochen im Bild.
- `hatZeile(bild, text)` vergleicht ganze Zeilen (das VRAM ist mit Leerzeichen aufgefüllt).
- Zwei Maschinen (ST5): Muster `SerielleKopplung.*` (`verbinden()` dort) — Server:
  `k = hub.konfig(i)`, `k.betriebsart = Rfc2217`, `k.rolle = Server`, `k.port = 0`,
  `hub.konfigurieren(i, k)`; gebundenen Port aus `status(i).port_aktiv` dem Client geben, beide Maschinen in EINEM Faden abwechselnd `lauf()`en, am Ende
  `stopAlle()`. Eine gemischte Kopplung A5120 ↔ K8915 geht mit denselben Klassen.

### 14.10 Offene Punkte

1. Maschinenerkennung (Verfahren seit ST1 fest, §14.4): ist 40H–43H an einem A5120 in jeder
   Ausbaustufe frei, trägt die SIO 1 des K8915 am Gerät einen RR2 ≠ FFH? (am Gerät; Abhilfe `/M:`)
2. ~~WR1 D2 / WR2 der mit der Tastatur geteilten SIOs je BIOS~~ — geklärt in ST3 (§14.4):
   A5120 A32 ohne Interrupt (D2 aus, WR2 nie gesetzt), K8915 SIO 2 WR1 B = 17H / WR2 = D0H.
   Offen nur am Gerät: ob E4H (CP/A) bzw. FFC0H (SCPX) in allen BIOS-Fassungen frei sind, und
   die Druckervorgabe der SCPX-Fassung 901 (8N1 statt 7O1 — SERTEST stellt 7O1 her).
3. ~~CTS/DCD-Weg am K8915 über D13~~ — aus dem Stromlaufplan geklärt (§14.5), im Emulator
   seit ST4 so nachgebildet; offen nur die Polarität der P184 (Annahme: invertierend) und die
   Bestätigung am Gerät (SERTEST LEITUNGEN-LOOP gibt die Rohwerte aus), sobald eines läuft.
5. Wickelbrücke **X14** (Auswahl B des Taktmultiplexers D13:02 → RxCA/TxCA der V.24):
   X14:1 = Masse, X14:3 = +5 V über R1:07, X14:2 = Auswahleingang. Welche Stellung steckt,
   bestimmt die Taktquelle der V.24 und damit, ob „CTC1 K0 für 9600" stimmt (Anwender, an der Karte).
4. Steckerbelegung Prüfstecker und Nullmodemkabel je Gerät, IFSS aktiv/passiv (Anwender, ST7).
6. **RETI bei anstehendem Interrupt weiter oben in der Kette** (Emulator, nicht beobachtet):
   `K1520Bus::updateInterruptChain` sperrt das IEI aller nachrangigen Bausteine, sobald ein
   vorrangiger **anfordert**; ihr `onRETI` prüft dieses IEI. Bei Zilog geben anfordernde (nicht
   bediente) Bausteine IEO beim Dekodieren von ED wieder frei, damit der bediente das 4DH
   sieht. Fordert während einer ISR ein vorrangiger Baustein an, ginge im Emulator das RETI
   verloren. In den SERTEST-Fällen liegen die geprüften SIOs vorn in ihrer Kette (A5120:
   K8025 vor der ZRE-CTC, darin A33 zuerst; K8915: SIO 1/2 vor den CTCs der K7028) — dort
   träfe es nur eine Anforderung der K5122. Aufgefallen beim Suchen des K8025-Fehlers (ST5).
7. K8915 als Gegenstelle an der V.24: die RTS-Probe (§14.7 Schritt 1) ist am Gerät ein
   Puls von einigen zehn µs alle ~64 ms — am Gerät gegenprüfen, dass ein Tester damit
   leben kann (er wertet drei gleiche Lesungen). In FLUSS-HW, wo RTS die Bremse ist, läuft
   keine Probe (sie löste die Bremse kurz): Spiegel und Probe laufen nur im Ruhezustand,
   die Gegenstelle stellt am Ende des Abschnitts ihre Leitungen von vorher her — erledigt
   ST6.

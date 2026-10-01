# Feinentwurf 19: Serielle Schnittstellen nach außen (Telnet / RFC 2217 / Datei)

**Stand:** 2026-09-30, S1–S8, T1a und T1b erledigt (§12.1); offen nur die Bildschirm-Checkliste (S8).
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

Belegung nach Belegungsplan und Stromlaufplan 1.45.518732 (`k8915schaltung.pdf` S. 5, 11,
16; geklärt in AP-S5, Befund samt Quelle in `doc/design/16_k8915.md` §3.2):

| Name (UI) | Stecker | SIO / Kanal | Ports | Art | Steuerleitungen | Takt |
|-----------|---------|-------------|-------|-----|-----------------|------|
| **V.24** | X3 | SIO 1 / A | 40H/41H | volle V.24 (Empfänger D17, Treiber D14) | ja | über Multiplexer D13; nachgebildet CTC 1 K0 |
| **IFS 1** | X4 | SIO 1 / B | 42H/43H | nur 103/104 — **Drucker des BIOS** („V24 XON/XOFF") | nein | CTC 1 K2 (sicher) |
| **IFS 2** | X5 | SIO 2 / A | 50H/51H | 103/104 + die IFSS-Stromschleife der Karte | nein | CTC 2 K0 |
| Tastatur K7672 | — | SIO 2 / B | 52H/53H | — | — | fest verdrahtet, **nicht einstellbar** |

**Abweichung vom ersten Entwurf (AP-S5):** die einzige Schnittstelle mit Steuerleitungen ist
SIO1-A — „V.24" heißt deshalb SIO1-A, nicht SIO1-B; der BIOS-Drucker liegt auf **IFS 1**.
CTC 1 CLK/TRG0–2 liegen auf Masse (reine Zeitgeber); CTSB/DCDB nur über Wickelbrücke X15:3/4
(offen = inaktiv), RTSB/DTRB an keinem Treiber. Nur erschlossen, nicht ganz verfolgt:
Kanal↔Stecker für SIO1-A/SIO2-A und der Weg CTSA/DCDA durch D13. Sollte die Rückwand anders
beschriftet sein, sind nur die Namen in `k7028.cpp` zu tauschen.

Die UI-Namen **„V.24", „IFS 1", „IFS 2" sind festgelegt** (Anwender, 2026-09-30) und
bleiben auch dann, wenn der Belegungsplan die Stecker anders beschriftet; welcher IFSS-Kanal
„IFS 1" und welcher „IFS 2" heißt, folgt der SIO-Reihenfolge (SIO 1 vor SIO 2).
Die alte Verdrahtung `K8915Machine` (Drucker = SIO1-B = IFS 1, DFÜ = SIO2-A = IFS 2) geht
in diesem Modell auf; `Config::pruefstecker` ist die Vorgabe des **Rx/Tx-Loop** (§4).

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

Neuer Dock **„Schnittstellen"** (`app/ui/serial_widget.py`), gestapelt mit „Laufwerke" und
„Einstellungen" (`tabifyDockWidget`, `main_window.py` ~Z. 314–329), auch im Menü *Ansicht*
schaltbar (wie die übrigen Kästen). Blöcke im Stil von `drive_widget.py`:

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
  DFÜ/IFSS, Drucker; K8915: V.24, IFS 1, IFS 2.
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

## 13. Offene Punkte

Entschieden am 2026-09-30: Bindeadresse = alle Schnittstellen (§7.2), Wiederaufnahme beim
Start (§7.4a), UI-Namen der K7028 = „V.24"/„IFS 1"/„IFS 2" (§3.2).

1. ~~**K7028:** Belegung, Taktkanäle, V.24-Leitungen, Brücken~~ — geklärt in AP-S5 (§3.2);
   Rest: Rückwandbeschriftung, Stecker der K7672, D13-Weg von CTSA/DCDA.
2. **K8025:** Weg ZRE-CTC K0 → K8025 geklärt (AP-S5, `K8025::setzeZreTakt`). Die Pegel der
   CTS/DCD-Eingänge der IFSS-Kanäle und der A32 bleiben **vorläufig inaktiv** — es gibt
   keinen Stromlaufplan der K8025 (das PDF hat nur das Blockschaltbild S. 8).
3. ~~**K8915 Loop-Vorgabe**~~ — geklärt in AP-S5: BIOS läuft ohne Echo, Vorgabe bleibt an (§4).
4. **Statuszeile und Datei:** Soll eine offene Druckdatei in der Statuszeile erscheinen
   (etwa „Drucker → druck.txt")? Entwurf: nein, nur Netzverbindungen (§9).

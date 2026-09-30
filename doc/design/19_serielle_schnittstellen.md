# Feinentwurf 19: Serielle Schnittstellen nach außen (Telnet / RFC 2217 / Datei)

**Stand:** 2026-09-30, Entwurf — noch nichts umgesetzt.
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

Belegung laut `doc/design/16_k8915.md` §3.2 (ROM- und BIOS-Befund):

| Name (UI) | SIO / Kanal | Ports | Art | Steuerleitungen | Takt |
|-----------|-------------|-------|-----|-----------------|------|
| **IFS 1** | SIO 1 / A | 40H/41H | IFSS **[?]** | nein | CTC 1 K? **[?]** |
| **V.24** | SIO 1 / B | 42H/43H | V.24 (Drucker/V.24 des BIOS) | ja **[?]** welche | CTC 1 K? **[?]** |
| **IFS 2** | SIO 2 / A | 50H/51H | IFSS **[?]** | nein | CTC 2 K0 |
| Tastatur K7672 | SIO 2 / B | 52H/53H | — | — | fest verdrahtet, **nicht einstellbar** |

Die UI-Namen **„V.24", „IFS 1", „IFS 2" sind festgelegt** (Anwender, 2026-09-30) und
bleiben auch dann, wenn der Belegungsplan die Stecker anders beschriftet; welcher IFSS-Kanal
„IFS 1" und welcher „IFS 2" heißt, folgt der SIO-Reihenfolge (SIO 1 vor SIO 2).
Offen und in AP-S5 am Belegungsplan/Stromlaufplan 1.45.518732 zu klären: die Zuordnung
IFSS/V.24 zu SIO1-A/-B, welcher CTC-1-Kanal welchen SIO-Kanal taktet, welche V.24-Leitungen
an welchem SIO-Anschluss liegen und welche Wickelbrücken die Kanäle betreffen. Die heutige Verdrahtung
`K8915Machine` (Drucker = SIO1-B, DFÜ = vorläufig SIO2-A) geht in diesem Modell auf; der
bisherige `Config::pruefstecker` wird zur Einstellung **Rx/Tx-Loop** je Schnittstelle (§4).

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
Vorgabe `K7028::Config::mitPruefstecker` bleibt Maschinenvorgabe; sie erscheint in der UI als
gesetzter Rx/Tx-Loop, solange die Schnittstelle nicht verbunden ist **[?]** — AP-S5 prüft,
ob das BIOS danach ohne Echo weiterläuft (sonst Loop vorgabemäßig an).

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
  (sofort, nicht erst nach Zeichen unterwegs) — dasselbe Argument wie §6.3. Gegenüber
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
- **Server:** eine Verbindung zur Zeit. Weitere Anfragen werden angenommen und sofort
  geschlossen (bei Telnet mit einer kurzen Textzeile „belegt"). Trennt der Client, lauscht
  der Server weiter.
- **Client:** keine Wiederholung im laufenden Betrieb. Scheitert der Aufbau oder bricht
  die Verbindung ab → Zustand Fehler/Getrennt, Knopf springt auf „Verbinden", Grund im Block.
  (Wiederaufnahme beim Programmstart: §7.4a.)

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
sonst **ungültig** (Knopf gesperrt). Die Klassifikation sitzt im Kern
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
- **Client:** baut die Verbindung automatisch auf (ein Versuch, alle Adressen der
  Namensauflösung). Scheitert er → Zustand Fehler mit Grund, Knopf „Verbinden"; kein
  weiterer Versuch.
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
                 uint16_t port_vorschlag; } K1520SerStatus;

K1520_API int  k1520_serial_count(K1520Handle h);              /* einstellbare */
K1520_API bool k1520_serial_info(K1520Handle h, int i, K1520SerInfo* out);
K1520_API bool k1520_serial_fixed_name(K1520Handle h, int i, char* buf, int n); /* Tastatur-Zeilen */
K1520_API bool k1520_serial_configure(K1520Handle h, int i, const K1520SerKonfig* k);
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
- Zustandspunkt: grau aus, gelb lauscht/verbindet, grün verbunden, rot Fehler;
  Fehlertext als Zeile im Block (kein Meldungsfenster — vgl. `diskNotice`).
- Aktualisierung per `QTimer` (≈ 4 Hz) über `k1520_serial_status`.
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
Überlauf-Nachbildung, V125/Ferneinschaltung, automatische Wiederverbindung, mehrere
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
| unit/util | `SerialNetz.*` | Loopback-Server/Client v4 und v6 (v6 übersprungen, wenn nicht verfügbar), Port belegt → +1 (Hand) bzw. kein Start + Vorschlag (automatisch), `SO_EXCLUSIVEADDRUSE` unter Windows |
| unit/cards | `K8025Seriell.*`, `K7028Seriell.*` | Kanalzuordnung, Taktquelle, V.24-Verknüpfungen, Tastatur unberührt |
| integration | `SerielleKopplung.*` | zwei Maschinen im selben Prozess über echtes TCP, eine mit 1×, eine mit 10× Takt: 64 KiB mit XON/XOFF und mit RTS/CTS verlustfrei, unterschiedliche Baud → Hinweis |
| python | `py_serial_api`, `py_serial_gui` | ctypes-Bindung; Blockzustände, Knopftexte, Host-Feld im Server aus, Klassifikationsetikett, Sperren im Betrieb, Wiederaufnahme (`aktiv` → Server lauscht; Port belegt → nicht gestartet, Vorschlag im Feld; Client-Fehlschlag ohne Wiederholung; Datei anhängend) |
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

S1, S2, S3 laufen parallel (S3 berührt `build/` — S1/S2 dann im Worktree).

## 13. Offene Punkte

Entschieden am 2026-09-30: Bindeadresse = alle Schnittstellen (§7.2), Wiederaufnahme beim
Start (§7.4a), UI-Namen der K7028 = „V.24"/„IFS 1"/„IFS 2" (§3.2).

1. **K7028:** Belegung, Taktkanäle, V.24-Leitungen, Brücken (§3.2) — braucht
   Belegungs-/Stromlaufplan (beim Anwender).
2. **K8025:** Pegel der CTS/DCD-Eingänge der IFSS-Kanäle und der A32; Weg ZRE-CTC K0 → K8025
   im Emulator (§3.1).
3. **K8915 Loop-Vorgabe:** läuft das BIOS ohne Prüfstecker-Echo? (§4)

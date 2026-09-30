/**
 * @file socket.h
 * @brief Dünne Socket-Hülle für POSIX und Winsock2 (Entwurf 19 §5, §7).
 *
 * Alles hier ist nicht blockierend und kennt weder Telnet noch die Maschine; es ist die
 * unterste Schicht des späteren `SerialHub`.  Die Kopfdatei zieht bewusst KEINE
 * Systemkopfdateien herein (kein `winsock2.h`/`windows.h` in fremden Übersetzungseinheiten).
 *
 * Fehler werden als `Fehler` (Systemcode + Text) gemeldet; `belegt` unterscheidet
 * „Port schon vergeben" von allem anderen, weil nur das die Portsuche (§7.2) auslöst.
 *
 * @see doc/design/19_serielle_schnittstellen.md
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace k1520::serial::net {

/// Socket-Kennung: `int` unter POSIX, `SOCKET` (UINT_PTR) unter Windows — als
/// `intptr_t` gehalten, `INVALID_SOCKET` (~0) wird dabei zu -1 wie der POSIX-Wert.
using SockFd = std::intptr_t;
constexpr SockFd kUngueltig = -1;

struct Fehler {
    int         code = 0;   ///< errno bzw. WSAGetLastError()
    std::string text;       ///< lesbar (strerror / FormatMessage)
    bool        belegt = false;   ///< Adresse/Port schon vergeben (EADDRINUSE, unter Windows auch WSAEACCES)
};

/// Winsock einmal je Prozess starten (unter POSIX ein No-op).  Mehrfach aufrufbar und
/// threadsicher; der spätere `SerialHub` ruft sie vor dem ersten Socket.  Es gibt
/// bewusst kein `WSACleanup` — der Prozess räumt auf, und ein Paar aus Start/Ende würde
/// mit anderen Nutzern von Winsock im Prozess (Python-`socket`) kollidieren.
bool netzStarten(Fehler* f = nullptr);

/// Besitzender Socket (nur verschiebbar, schließt im Destruktor).
class Socket {
public:
    Socket() = default;
    explicit Socket(SockFd fd) : fd_(fd) {}
    ~Socket() { schliessen(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& o) noexcept : fd_(o.freigeben()) {}
    Socket& operator=(Socket&& o) noexcept {
        if (this != &o) { schliessen(); fd_ = o.freigeben(); }
        return *this;
    }

    SockFd fd() const { return fd_; }
    bool   gueltig() const { return fd_ != kUngueltig; }
    explicit operator bool() const { return gueltig(); }
    SockFd freigeben() { SockFd f = fd_; fd_ = kUngueltig; return f; }
    void   schliessen();

private:
    SockFd fd_ = kUngueltig;
};

// ── Einzelne Sockets ────────────────────────────────────────────────────────

bool nichtBlockierend(SockFd fd, bool an = true);
bool keinVerzoegern(SockFd fd);   ///< TCP_NODELAY (Terminalbetrieb)

/// Ergebnis eines Lausch-Versuchs.
struct Lauscher {
    Socket sock;
    int    port = 0;       ///< TATSÄCHLICHER Port (bei Port 0 vom System gewählt)
    Fehler fehler;         ///< gesetzt (code != 0), wenn `sock` ungültig ist
    bool   dualStack = false;   ///< `::` mit V6ONLY=0 (sonst Rückfall 0.0.0.0)
};

/// Ein Versuch: an Port `port` (0 = beliebig) auf ALLEN Schnittstellen lauschen —
/// Dual-Stack `::` (`IPV6_V6ONLY=0`), Rückfall `0.0.0.0`, wenn IPv6 fehlt.
/// POSIX: `SO_REUSEADDR` (sonst schiebt ein alter TIME_WAIT den Port weiter);
/// Windows: `SO_EXCLUSIVEADDRUSE` (dort würde `SO_REUSEADDR` einen belegten Port
/// „erfolgreich" doppelt binden).  Der Socket ist nicht blockierend.
/// Bei belegtem Port: `fehler.belegt == true`, KEIN Rückfall auf IPv4 (der Dual-Stack-Bind
/// scheitert auch bei einem IPv4-Belegten Port mit EADDRINUSE).
Lauscher lauschen(int port);

/// §7.2 (Start von Hand): ab `ab` aufwärts bis 65535, solange `belegt`; jeder andere
/// Fehler bricht ab.  Ist alles belegt, bleibt `sock` ungültig und `fehler.belegt` gesetzt.
Lauscher lauschenMitSuche(int ab);

/// Ergebnis der reinen Prüfung (§7.4a: Start beim Programmstart, nichts lauschen lassen).
struct PortPruefung {
    bool frei = false;      ///< `port` war frei
    int  vorschlag = 0;     ///< `port` wenn frei, sonst nächster freier darüber; 0 = keiner
    Fehler fehler;          ///< anderer Fehler als „belegt" (dann `vorschlag == 0`)
};

/// Ist Port `port` frei?  Sonst der nächste freie darüber (bis 65535).  Bindet zur Probe
/// und schließt sofort wieder — wie `lauschen`, also mit denselben Sockeloptionen, damit
/// „frei" hier und „bindbar" beim späteren Starten dasselbe heißen.
PortPruefung portPruefen(int port);

/// Eine anstehende Verbindung annehmen; ungültiger Socket, wenn keine da ist.
/// Der neue Socket ist nicht blockierend und hat `TCP_NODELAY`.
Socket annehmen(SockFd lauscher, Fehler* f = nullptr);

/// Adresse der Gegenstelle eines verbundenen Sockets als Text, numerisch:
/// `192.168.1.5:40122` bzw. `[::1]:40122`.  Eine IPv4-abgebildete Adresse
/// (`::ffff:1.2.3.4`, entsteht am Dual-Stack-Lauscher) erscheint als IPv4.
/// Leer, wenn der Socket nicht verbunden ist.
std::string gegenstelle(SockFd fd);

// ── Auflösung und Verbinden ──────────────────────────────────────────────────

/// Eine aufgelöste Adresse (undurchsichtig, `sockaddr_storage`-groß).
struct Ziel {
    unsigned char roh[128] = {};
    int           laenge  = 0;
    int           familie = 0;    ///< AF_INET / AF_INET6
    std::string   text;           ///< numerisch, für Meldungen
    bool ist6() const;
};

/// `getaddrinfo` (BLOCKIEREND — nur im I/O-Faden aufrufen, §2.5): ALLE gelieferten Adressen
/// (IPv6 und IPv4) in der Reihenfolge des Resolvers (RFC 6724).  `host` darf `[::1]` und
/// `fe80::1%eth0` sein (siehe `adresse.h`).  Leere Liste → `f` gesetzt.
std::vector<Ziel> aufloesen(const std::string& host, int port, Fehler* f = nullptr);

/// Nicht blockierendes `connect` mit Zeitgrenze (ms).  Bei Erfolg ein verbundener,
/// nicht blockierender Socket mit `TCP_NODELAY`; sonst ungültig und `f` gesetzt.
Socket verbinden(const Ziel& ziel, int fristMs, Fehler* f = nullptr);

/// Die Adressen der Reihe nach probieren; `fristMs` gilt für ALLE Versuche zusammen
/// (Dauerversuch des Clients, §7.1: „länger als 1000 ms → gescheitert").  Fehler =
/// der des letzten Versuchs.
Socket verbindenAlle(const std::vector<Ziel>& ziele, int fristMs, Fehler* f = nullptr);

// ── Ein-/Ausgabe ────────────────────────────────────────────────────────────

enum class IoStatus {
    Ok,           ///< `n` Bytes übertragen (bei `empfangen` > 0)
    Warten,       ///< würde blockieren (EWOULDBLOCK) — `n == 0`
    Geschlossen,  ///< Gegenstelle hat geschlossen (nur `empfangen`)
    Fehler,       ///< harter Fehler, siehe `fehler`
};

struct IoErgebnis {
    std::size_t n = 0;
    IoStatus    status = IoStatus::Ok;
    Fehler      fehler;
};

IoErgebnis senden(SockFd fd, const void* daten, std::size_t n);   ///< ohne SIGPIPE
IoErgebnis empfangen(SockFd fd, void* puffer, std::size_t max);

// ── Warten (poll / WSAPoll) ──────────────────────────────────────────────────

struct PollEintrag {
    SockFd fd = kUngueltig;
    bool   lesen = false;        ///< Wunsch: auf Lesbarkeit (bzw. eingehende Verbindung) warten
    bool   schreiben = false;    ///< Wunsch: auf Schreibbarkeit (bzw. fertiges connect) warten
    bool   lesbar = false;       ///< Ergebnis (auch bei Hangup, damit `empfangen` es erfährt)
    bool   beschreibbar = false;
    bool   fehler = false;       ///< POLLERR/POLLNVAL
};

/// Wartet höchstens `timeoutMs` (-1 = unbegrenzt) und füllt die Ergebnisfelder.
/// Rückgabe: Zahl der Einträge mit Ereignis, -1 bei Fehler.  EINTR wird als 0 gemeldet.
int warten(std::vector<PollEintrag>& eintraege, int timeoutMs);

// ── Weck-Socketpaar ─────────────────────────────────────────────────────────

/// Weckt den wartenden I/O-Faden aus `warten()`: der Faden nimmt `lesefd()` in seine
/// Liste auf; wer etwas zu melden hat, ruft `wecken()` (aus jedem Faden).  POSIX:
/// `socketpair`; Windows: verbundenes Loopback-TCP-Paar (Winsock kennt keins).
class Wecker {
public:
    Wecker();
    bool   gueltig() const { return lese_.gueltig() && schreib_.gueltig(); }
    SockFd lesefd() const { return lese_.fd(); }
    void   wecken();   ///< schreibt ein Byte; ein volles Paar ist kein Fehler (Wecker steht ohnehin)
    void   leeren();   ///< liest alles weg — im I/O-Faden nach dem Aufwachen

private:
    Socket lese_, schreib_;
};

} // namespace k1520::serial::net

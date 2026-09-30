#include "core/serial/net/socket.h"

#include "core/serial/net/adresse.h"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <system_error>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace k1520::serial::net {

namespace {

// ── Die Stellen, an denen POSIX und Winsock auseinandergehen ────────────────

#if defined(_WIN32)
using SockLen = int;
inline SOCKET roh(SockFd fd) { return static_cast<SOCKET>(fd); }
inline int  letzterFehler() { return ::WSAGetLastError(); }
inline bool istWarten(int e) { return e == WSAEWOULDBLOCK; }
inline bool istLaeuft(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
inline bool istAbbruch(int e) { return e == WSAECONNRESET || e == WSAECONNABORTED; }
inline bool istBelegtCode(int e) {
    // Mit SO_EXCLUSIVEADDRUSE meldet Windows einen vergebenen Port als WSAEACCES,
    // nicht als WSAEADDRINUSE.
    return e == WSAEADDRINUSE || e == WSAEACCES;
}
constexpr int kFehlerUngueltig = WSAEINVAL;
constexpr int kFehlerZeit      = WSAETIMEDOUT;
inline void schliesseRoh(SockFd fd) { ::closesocket(roh(fd)); }
#else
using SockLen = socklen_t;
inline int  roh(SockFd fd) { return static_cast<int>(fd); }
inline int  letzterFehler() { return errno; }
inline bool istWarten(int e) { return e == EWOULDBLOCK || e == EAGAIN; }
inline bool istLaeuft(int e) { return e == EINPROGRESS || e == EINTR; }
inline bool istAbbruch(int e) { return e == ECONNRESET || e == ECONNABORTED; }
inline bool istBelegtCode(int e) { return e == EADDRINUSE; }
constexpr int kFehlerUngueltig = EINVAL;
constexpr int kFehlerZeit      = ETIMEDOUT;
inline void schliesseRoh(SockFd fd) { ::close(roh(fd)); }
#endif

#if defined(MSG_NOSIGNAL)
constexpr int kSendeFlags = MSG_NOSIGNAL;   // Linux: kein SIGPIPE in einen toten Socket
#else
constexpr int kSendeFlags = 0;              // Windows kennt kein SIGPIPE; macOS: SO_NOSIGPIPE unten
#endif

Fehler macheFehler(int code) {
    Fehler f;
    f.code   = code;
    f.text   = std::error_code(code, std::system_category()).message();
    f.belegt = istBelegtCode(code);
    return f;
}

void meldeFehler(Fehler* f, int code) {
    if (f) *f = macheFehler(code);
}

/// Socket anlegen; unter macOS zusätzlich SO_NOSIGPIPE (dort gibt es kein MSG_NOSIGNAL).
SockFd neuerSocket(int familie) {
    const auto s = ::socket(familie, SOCK_STREAM, 0);
#if defined(_WIN32)
    if (s == INVALID_SOCKET) return kUngueltig;
#else
    if (s < 0) return kUngueltig;
#  if defined(SO_NOSIGPIPE)
    int an = 1;
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &an, sizeof an);
#  endif
#endif
    return static_cast<SockFd>(s);
}

bool setzeOption(SockFd fd, int ebene, int name, int wert) {
    return ::setsockopt(roh(fd), ebene, name,
#if defined(_WIN32)
                        reinterpret_cast<const char*>(&wert),
#else
                        &wert,
#endif
                        sizeof wert) == 0;
}

int eigenerPort(SockFd fd) {
    sockaddr_storage ss{};
    SockLen len = sizeof ss;
    if (::getsockname(roh(fd), reinterpret_cast<sockaddr*>(&ss), &len) != 0) return 0;
    if (ss.ss_family == AF_INET6)
        return ntohs(reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port);
    if (ss.ss_family == AF_INET)
        return ntohs(reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
    return 0;
}

/// Ein Bind-Versuch in einer Adressfamilie.  `true` = `r.sock` lauscht.
bool lauschenIn(int familie, int port, Lauscher& r) {
    Socket s(neuerSocket(familie));
    if (!s) { r.fehler = macheFehler(letzterFehler()); return false; }

    if (familie == AF_INET6) {
        // Dual-Stack: ohne das hörte der Socket nur auf IPv6 (Windows-Vorgabe: an).
        if (!setzeOption(s.fd(), IPPROTO_IPV6, IPV6_V6ONLY, 0)) {
            r.fehler = macheFehler(letzterFehler());
            return false;
        }
    }
#if defined(_WIN32)
    // SO_REUSEADDR hieße hier „doppelt binden erlaubt" — das Gegenteil von dem, was die
    // Portsuche braucht.
    setzeOption(s.fd(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
    setzeOption(s.fd(), SOL_SOCKET, SO_REUSEADDR, 1);
#endif

    int rc;
    if (familie == AF_INET6) {
        sockaddr_in6 a{};
        a.sin6_family = AF_INET6;
        a.sin6_addr   = in6addr_any;
        a.sin6_port   = htons(static_cast<unsigned short>(port));
        rc = ::bind(roh(s.fd()), reinterpret_cast<sockaddr*>(&a), sizeof a);
    } else {
        sockaddr_in a{};
        a.sin_family      = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port        = htons(static_cast<unsigned short>(port));
        rc = ::bind(roh(s.fd()), reinterpret_cast<sockaddr*>(&a), sizeof a);
    }
    if (rc != 0) { r.fehler = macheFehler(letzterFehler()); return false; }
    if (::listen(roh(s.fd()), 8) != 0) { r.fehler = macheFehler(letzterFehler()); return false; }
    if (!nichtBlockierend(s.fd())) { r.fehler = macheFehler(letzterFehler()); return false; }

    r.port      = eigenerPort(s.fd());
    r.dualStack = (familie == AF_INET6);
    r.fehler    = Fehler{};
    r.sock      = std::move(s);
    return true;
}

// ── Zeit ────────────────────────────────────────────────────────────────────

using Uhr = std::chrono::steady_clock;

int restMs(Uhr::time_point ende) {
    const auto rest = std::chrono::duration_cast<std::chrono::milliseconds>(ende - Uhr::now()).count();
    return rest > 0 ? static_cast<int>(rest) : 0;
}

} // namespace

// ── Winsock ─────────────────────────────────────────────────────────────────

bool netzStarten(Fehler* f) {
#if defined(_WIN32)
    static std::once_flag einmal;
    static int            ergebnis = 0;
    std::call_once(einmal, [] {
        WSADATA d;
        ergebnis = ::WSAStartup(MAKEWORD(2, 2), &d);   // liefert den Fehlercode selbst
    });
    if (ergebnis != 0) { meldeFehler(f, ergebnis); return false; }
    return true;
#else
    (void)f;
    return true;
#endif
}

// ── Socket ──────────────────────────────────────────────────────────────────

void Socket::schliessen() {
    if (fd_ != kUngueltig) {
        schliesseRoh(fd_);
        fd_ = kUngueltig;
    }
}

bool nichtBlockierend(SockFd fd, bool an) {
#if defined(_WIN32)
    u_long m = an ? 1 : 0;
    return ::ioctlsocket(roh(fd), FIONBIO, &m) == 0;
#else
    const int fl = ::fcntl(roh(fd), F_GETFL, 0);
    if (fl < 0) return false;
    return ::fcntl(roh(fd), F_SETFL, an ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK)) == 0;
#endif
}

bool keinVerzoegern(SockFd fd) {
    return setzeOption(fd, IPPROTO_TCP, TCP_NODELAY, 1);
}

// ── Server ──────────────────────────────────────────────────────────────────

Lauscher lauschen(int port) {
    Lauscher r;
    if (port < 0 || port > 65535) { r.fehler = macheFehler(kFehlerUngueltig); return r; }
    if (!netzStarten(&r.fehler)) return r;

    if (lauschenIn(AF_INET6, port, r)) return r;
    // Belegt heißt belegt: der Dual-Stack-Bind scheitert auch, wenn nur IPv4 den Port
    // hält — ein Rückfall auf 0.0.0.0 würde ihn dann fälschlich als frei melden.
    if (r.fehler.belegt) return r;
    // Sonst fehlt IPv6 (kein Protokoll, kein Adresskonfig., V6ONLY nicht abschaltbar): IPv4.
    Lauscher v4;
    if (lauschenIn(AF_INET, port, v4)) return v4;
    return v4;
}

Lauscher lauschenMitSuche(int ab) {
    Lauscher r;
    if (ab < 1 || ab > 65535) { r.fehler = macheFehler(kFehlerUngueltig); return r; }
    for (int p = ab; p <= 65535; ++p) {
        r = lauschen(p);
        if (r.sock) return r;
        if (!r.fehler.belegt) return r;   // anderer Fehler: weitersuchen würde nichts ändern
    }
    return r;   // alles belegt: letzter Fehler (belegt)
}

PortPruefung portPruefen(int port) {
    PortPruefung r;
    if (port < 1 || port > 65535) { r.fehler = macheFehler(kFehlerUngueltig); return r; }
    // Probe = wirklich lauschen und sofort wieder schließen (der Lauscher zerstört sich
    // am Schleifenende): „frei" heißt dann genau „bindbar".
    for (int p = port; p <= 65535; ++p) {
        Lauscher l = lauschen(p);
        if (l.sock) {
            r.frei      = (p == port);
            r.vorschlag = p;
            return r;
        }
        if (!l.fehler.belegt) { r.fehler = l.fehler; return r; }
    }
    return r;   // nichts frei: vorschlag 0
}

Socket annehmen(SockFd lauscher, Fehler* f) {
    const auto s = ::accept(roh(lauscher), nullptr, nullptr);
#if defined(_WIN32)
    if (s == INVALID_SOCKET) {
#else
    if (s < 0) {
#endif
        const int e = letzterFehler();
        if (!istWarten(e) && e != EINTR) meldeFehler(f, e);
        return Socket();
    }
    Socket neu(static_cast<SockFd>(s));
#if defined(SO_NOSIGPIPE)
    setzeOption(neu.fd(), SOL_SOCKET, SO_NOSIGPIPE, 1);
#endif
    // Ein angenommener Socket erbt O_NONBLOCK nicht überall (Linux: nein) — immer setzen.
    nichtBlockierend(neu.fd());
    keinVerzoegern(neu.fd());
    return neu;
}

// ── Auflösung und Verbinden ──────────────────────────────────────────────────

bool Ziel::ist6() const { return familie == AF_INET6; }

std::vector<Ziel> aufloesen(const std::string& host, int port, Fehler* f) {
    std::vector<Ziel> aus;
    if (!netzStarten(f)) return aus;

    const Adresse a = adresseZerlegen(host);
    if (a.art == AdressArt::Ungueltig) { meldeFehler(f, kFehlerUngueltig); if (f) f->text = "ungültiger Host"; return aus; }
    const std::string name = a.zone.empty() ? a.host : a.host + "%" + a.zone;

    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;       // v6 UND v4 (§7.1)
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_NUMERICSERV;  // bewusst KEIN AI_ADDRCONFIG: es lässt ::1 fallen,
                                         // wenn nur Loopback-IPv6 konfiguriert ist
    addrinfo* erg = nullptr;
    const int rc = ::getaddrinfo(name.c_str(), std::to_string(port).c_str(), &hints, &erg);
    if (rc != 0) {
        if (f) {
            f->code = rc;
#if defined(_WIN32)
            f->text = ::gai_strerrorA(rc);   // gai_strerror würde unter UNICODE zur W-Variante
#else
            f->text = ::gai_strerror(rc);
#endif
        }
        return aus;
    }
    for (const addrinfo* p = erg; p; p = p->ai_next) {
        if (p->ai_family != AF_INET && p->ai_family != AF_INET6) continue;
        if (p->ai_addrlen > sizeof(Ziel::roh)) continue;
        Ziel z;
        std::memcpy(z.roh, p->ai_addr, p->ai_addrlen);
        z.laenge  = static_cast<int>(p->ai_addrlen);
        z.familie = p->ai_family;
        char h[NI_MAXHOST] = {};
        if (::getnameinfo(p->ai_addr, static_cast<SockLen>(p->ai_addrlen), h, sizeof h,
                          nullptr, 0, NI_NUMERICHOST) == 0)
            z.text = h;
        aus.push_back(z);
    }
    ::freeaddrinfo(erg);
    if (aus.empty()) meldeFehler(f, kFehlerUngueltig);
    return aus;
}

Socket verbinden(const Ziel& ziel, int fristMs, Fehler* f) {
    Socket s(neuerSocket(ziel.familie));
    if (!s) { meldeFehler(f, letzterFehler()); return Socket(); }
    if (!nichtBlockierend(s.fd())) { meldeFehler(f, letzterFehler()); return Socket(); }

    int rc = ::connect(roh(s.fd()), reinterpret_cast<const sockaddr*>(ziel.roh), ziel.laenge);
    if (rc != 0) {
        const int e = letzterFehler();
        if (!istLaeuft(e)) { meldeFehler(f, e); return Socket(); }

        std::vector<PollEintrag> pe(1);
        pe[0].fd = s.fd();
        pe[0].schreiben = true;
        const int n = warten(pe, fristMs);
        if (n < 0) { meldeFehler(f, letzterFehler()); return Socket(); }
        // Hinweis Windows: ältere WSAPoll melden ein gescheitertes connect gar nicht
        // (bekannter Fehler bis Win10 2004) — dann läuft die Frist ab und es zählt als
        // gescheitert, was für den Dauerversuch dasselbe ist (nur langsamer).
        if (n == 0 || !(pe[0].beschreibbar || pe[0].fehler || pe[0].lesbar)) {
            meldeFehler(f, kFehlerZeit);
            return Socket();
        }
        int     so = 0;
        SockLen len = sizeof so;
        const int g = ::getsockopt(roh(s.fd()), SOL_SOCKET, SO_ERROR,
#if defined(_WIN32)
                                   reinterpret_cast<char*>(&so),
#else
                                   &so,
#endif
                                   &len);
        if (g != 0) { meldeFehler(f, letzterFehler()); return Socket(); }
        if (so != 0) { meldeFehler(f, so); return Socket(); }
    }
    keinVerzoegern(s.fd());
    return s;
}

Socket verbindenAlle(const std::vector<Ziel>& ziele, int fristMs, Fehler* f) {
    const auto ende = Uhr::now() + std::chrono::milliseconds(fristMs);
    Fehler letzter = macheFehler(kFehlerUngueltig);
    for (const Ziel& z : ziele) {
        Fehler fe;
        Socket s = verbinden(z, restMs(ende), &fe);
        if (s) return s;
        letzter = fe;
        if (restMs(ende) == 0) break;   // Frist verbraucht: nicht weiter durch die Liste
    }
    if (f) *f = letzter;
    return Socket();
}

// ── Ein-/Ausgabe ────────────────────────────────────────────────────────────

IoErgebnis senden(SockFd fd, const void* daten, std::size_t n) {
    IoErgebnis r;
#if defined(_WIN32)
    const int rc = ::send(roh(fd), static_cast<const char*>(daten), static_cast<int>(n), kSendeFlags);
#else
    const auto rc = ::send(roh(fd), daten, n, kSendeFlags);
#endif
    if (rc >= 0) { r.n = static_cast<std::size_t>(rc); return r; }
    const int e = letzterFehler();
    if (istWarten(e) || e == EINTR) { r.status = IoStatus::Warten; return r; }
    r.status = IoStatus::Fehler;
    r.fehler = macheFehler(e);
    return r;
}

IoErgebnis empfangen(SockFd fd, void* puffer, std::size_t max) {
    IoErgebnis r;
#if defined(_WIN32)
    const int rc = ::recv(roh(fd), static_cast<char*>(puffer), static_cast<int>(max), 0);
#else
    const auto rc = ::recv(roh(fd), puffer, max, 0);
#endif
    if (rc > 0) { r.n = static_cast<std::size_t>(rc); return r; }
    if (rc == 0) { r.status = IoStatus::Geschlossen; return r; }
    const int e = letzterFehler();
    if (istWarten(e) || e == EINTR) { r.status = IoStatus::Warten; return r; }
    if (istAbbruch(e)) { r.status = IoStatus::Geschlossen; return r; }   // RST = Ende für uns
    r.status = IoStatus::Fehler;
    r.fehler = macheFehler(e);
    return r;
}

// ── Warten ──────────────────────────────────────────────────────────────────

int warten(std::vector<PollEintrag>& eintraege, int timeoutMs) {
#if defined(_WIN32)
    using PFD = WSAPOLLFD;
#else
    using PFD = pollfd;
#endif
    // Einträge ohne gültigen Socket auslassen (poll würde POLLNVAL melden; WSAPoll
    // verhält sich dort uneinheitlich).
    std::vector<PFD>         pfds;
    std::vector<std::size_t> index;
    for (std::size_t i = 0; i < eintraege.size(); ++i) {
        PollEintrag& e = eintraege[i];
        e.lesbar = e.beschreibbar = e.fehler = false;
        if (e.fd == kUngueltig) continue;
        PFD p{};
        p.fd = roh(e.fd);
        if (e.lesen)     p.events |= POLLIN;
        if (e.schreiben) p.events |= POLLOUT;
        pfds.push_back(p);
        index.push_back(i);
    }
#if defined(_WIN32)
    const int rc = pfds.empty() ? 0 : ::WSAPoll(pfds.data(), static_cast<ULONG>(pfds.size()), timeoutMs);
#else
    const int rc = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeoutMs);
#endif
    if (rc < 0) return (letzterFehler() == EINTR) ? 0 : -1;

    int mit = 0;
    for (std::size_t k = 0; k < pfds.size(); ++k) {
        PollEintrag& e = eintraege[index[k]];
        const short  r = pfds[k].revents;
        // POLLHUP zählt als „lesbar": erst `empfangen` erfährt daraus das Ende.
        e.lesbar       = (r & (POLLIN | POLLHUP)) != 0;
        e.beschreibbar = (r & POLLOUT) != 0;
        e.fehler       = (r & (POLLERR | POLLNVAL)) != 0;
        if (r) ++mit;
    }
    return mit;
}

// ── Weck-Socketpaar ─────────────────────────────────────────────────────────

Wecker::Wecker() {
    if (!netzStarten()) return;
#if defined(_WIN32)
    // Winsock hat kein socketpair: Loopback-Listener auf Port 0, verbinden, annehmen,
    // Listener schließen.  Das Paar ist dann ein gewöhnliches TCP-Paar.
    Socket l(neuerSocket(AF_INET));
    if (!l) return;
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;
    if (::bind(roh(l.fd()), reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) return;
    if (::listen(roh(l.fd()), 1) != 0) return;
    SockLen len = sizeof a;
    if (::getsockname(roh(l.fd()), reinterpret_cast<sockaddr*>(&a), &len) != 0) return;
    Socket c(neuerSocket(AF_INET));
    if (!c) return;
    if (::connect(roh(c.fd()), reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) return;
    const auto s = ::accept(roh(l.fd()), nullptr, nullptr);
    if (s == INVALID_SOCKET) return;
    Socket angenommen(static_cast<SockFd>(s));
    lese_   = std::move(angenommen);
    schreib_ = std::move(c);
    keinVerzoegern(schreib_.fd());
#else
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return;
    lese_    = Socket(fds[0]);
    schreib_ = Socket(fds[1]);
#endif
    // Beide nicht blockierend: `wecken` darf nie hängen (volles Paar), `leeren` liest bis EAGAIN.
    if (!nichtBlockierend(lese_.fd()) || !nichtBlockierend(schreib_.fd())) {
        lese_.schliessen();
        schreib_.schliessen();
    }
}

void Wecker::wecken() {
    if (!gueltig()) return;
    const char b = 1;
    senden(schreib_.fd(), &b, 1);   // Ergebnis gleichgültig: Puffer voll heißt „Wecker steht schon"
}

void Wecker::leeren() {
    if (!gueltig()) return;
    char buf[64];
    while (true) {
        const IoErgebnis r = empfangen(lese_.fd(), buf, sizeof buf);
        if (r.status != IoStatus::Ok) break;
    }
}

} // namespace k1520::serial::net

#include "core/serial/hub.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>

#include "core/serial/net/adresse.h"
#include "core/serial/net/socket.h"
#include "core/serial/transport.h"

namespace k1520::serial {

namespace {

/// Ergebnis eines Verbindungsversuchs, zwischen Verbinder-Faden und I/O-Faden geteilt.
/// Der Verbinder-Faden ist abgekoppelt (getaddrinfo lässt sich nicht abbrechen) und hält
/// selbst einen Anteil — Hub und Eintrag dürfen vor ihm sterben.
struct Verbinder {
    std::mutex  m;
    bool        fertig = false;
    bool        abgebrochen = false;
    net::Socket sock;
    std::string grund, gegenstelle;
};

std::string grundText(const net::Fehler& f) {
    // WSAECONNREFUSED = 10061 (unter Windows steht in `code` der Winsock-Wert).
    if (f.code == ECONNREFUSED || f.code == 10061) return "Verbindung abgewiesen";
    if (f.text.empty()) return "Verbindung gescheitert";
    return "Verbindung gescheitert: " + f.text;
}

void verbinderLauf(std::shared_ptr<Verbinder> v, std::shared_ptr<net::Wecker> wecker,
                   std::string host, int port) {
    net::Fehler f;
    net::Socket s;
    std::string grund, gegen;
    // Jeder Versuch löst den Namen NEU auf (§7.1: DHCP, geänderte Einträge).
    const auto ziele = net::aufloesen(host, port, &f);
    if (ziele.empty()) {
        grund = "Name nicht auflösbar";
        if (!f.text.empty()) grund += ": " + f.text;
    } else {
        bool ab;
        {
            std::lock_guard<std::mutex> l(v->m);
            ab = v->abgebrochen;
        }
        if (!ab) {
            // Frist 1000 ms für alle Adressen zusammen (§7.1).
            s = net::verbindenAlle(ziele, 1000, &f);
            if (s) gegen = net::gegenstelle(s.fd());
            else   grund = grundText(f);
        }
    }
    {
        std::lock_guard<std::mutex> l(v->m);
        v->fertig = true;
        if (!v->abgebrochen) {
            v->sock        = std::move(s);
            v->grund       = std::move(grund);
            v->gegenstelle = std::move(gegen);
        }
    }
    wecker->wecken();
}

constexpr size_t PUFFER = Wandler::PUFFER;

}  // namespace

struct SerialHub::Eintrag {
    SerialAnschluss*         anschluss = nullptr;
    std::unique_ptr<Wandler> wandler;
    SerialKonfig             konfig;
    Zustand                  zustand = Zustand::Aus;
    uint16_t                 portAktiv = 0, portVorschlag = 0;
    std::string              meldung, gegenstelle;
    uint32_t                 versuche = 0;
    // Netz
    net::Socket                    lauscher, verbindung;
    std::unique_ptr<NetzTransport> transport;
    std::vector<uint8_t>           aus;         ///< kodiert, wartet auf einen beschreibbaren Socket
    std::vector<uint8_t>           rueckstau;   ///< dekodiert, vom Wandler noch nicht angenommen
    // Client
    std::shared_ptr<Verbinder> verbinder;
    Uhr::time_point            naechsterVersuch{};
    // Datei
    std::unique_ptr<DateiTransport> datei;
};

// ─── Aufbau ─────────────────────────────────────────────────────────────────

SerialHub::SerialHub(uint64_t phiNenn) : phiNenn_(phiNenn) {
    net::netzStarten();   // WSAStartup einmal (unter POSIX nichts)
    wecker_ = std::make_shared<net::Wecker>();
}

SerialHub::~SerialHub() {
    {
        std::lock_guard<std::mutex> l(m_);
        for (auto& e : eintraege_) stopIntern(*e);
        beenden_ = true;
    }
    wecker_->wecken();
    if (faden_.joinable()) faden_.join();
}

int SerialHub::registriere(SerialAnschluss& anschluss) {
    auto e = std::make_unique<Eintrag>();
    e->anschluss = &anschluss;
    e->wandler   = std::make_unique<Wandler>(anschluss, phiNenn_);
    // Wandler → I/O-Faden.  Kostet einen Schreibzugriff aufs Socketpaar, nur bei
    // Zustandswechseln (Sendepuffer leer → belegt usw.), nicht je Byte.
    e->wandler->setzeWecker([w = wecker_.get()] { w->wecken(); });
    std::lock_guard<std::mutex> l(m_);
    eintraege_.push_back(std::move(e));
    return static_cast<int>(eintraege_.size()) - 1;
}

Wandler& SerialHub::wandler(int i) { return *eintraege_.at(static_cast<size_t>(i))->wandler; }

void SerialHub::takt(uint64_t zyklus) {
    for (auto& e : eintraege_) e->wandler->takt(zyklus);
}

// ─── Bedienung ─────────────────────────────────────────────────────────────

SerialInfo SerialHub::info(int i) const {
    SerialInfo r;
    if (i < 0 || i >= anzahl()) return r;
    const SerialAnschluss& a = *eintraege_[static_cast<size_t>(i)]->anschluss;
    r.name    = a.name();
    r.stecker = a.stecker();
    r.v24     = a.v24();
    for (const Taktquelle& t : a.taktquellen()) r.taktquellen.push_back(t.name);
    return r;
}

SerialKonfig SerialHub::konfig(int i) const {
    std::lock_guard<std::mutex> l(m_);
    if (i < 0 || i >= anzahl()) return {};
    return eintraege_[static_cast<size_t>(i)]->konfig;
}

bool SerialHub::konfigurieren(int i, const SerialKonfig& k) {
    if (i < 0 || i >= anzahl()) return false;
    const auto quellen = eintraege_[static_cast<size_t>(i)]->anschluss->taktquellen().size();
    if (k.taktquelle < 0 || (k.taktquelle > 0 && static_cast<size_t>(k.taktquelle) >= quellen))
        return false;
    if (static_cast<int>(k.betriebsart) < 0 || static_cast<int>(k.betriebsart) > 2) return false;
    if (static_cast<int>(k.rolle) < 0 || static_cast<int>(k.rolle) > 1) return false;

    std::lock_guard<std::mutex> l(m_);
    Eintrag& e = *eintraege_[static_cast<size_t>(i)];
    // §4 „Sperren im Betrieb": Betriebsart, Rolle, Host, Port, Datei.
    if (istAktiv(e.zustand) && !e.konfig.gesperrteGleich(k)) return false;
    e.konfig = k;
    WandlerEinstellung we;
    we.loop           = k.loop;
    we.rtscts_bruecke = k.rtscts_bruecke;
    we.xonxoff        = k.xonxoff;
    we.taktquelle     = k.taktquelle;
    // §6.5: Loop und Verbindung schließen sich aus — Loop setzen beendet sie.
    if (k.loop && istAktiv(e.zustand)) stopIntern(e);
    e.wandler->einstellen(we);
    return true;
}

bool SerialHub::start(int i) { return starten(i, false); }
bool SerialHub::startAuto(int i) { return starten(i, true); }

bool SerialHub::starten(int i, bool automatisch) {
    if (i < 0 || i >= anzahl()) return false;
    std::lock_guard<std::mutex> l(m_);
    Eintrag& e = *eintraege_[static_cast<size_t>(i)];
    if (istAktiv(e.zustand)) return true;
    e.meldung.clear();
    e.portVorschlag = 0;
    e.versuche      = 0;
    e.portAktiv     = 0;
    if (e.konfig.loop) {
        // §6.5: der Umschalter ist gesperrt, solange Loop gesetzt ist.
        e.meldung = "Rx/Tx-Loop gesetzt";
        return false;
    }
    const SerialKonfig& k = e.konfig;
    auto fehler = [&](std::string text) {
        e.zustand = Zustand::Fehler;
        e.meldung = std::move(text);
        return false;
    };

    if (k.betriebsart == Betriebsart::Datei) {
        if (k.datei.empty()) return fehler("keine Datei gewählt");
        auto d = std::make_unique<DateiTransport>();
        std::string grund;
        // Wiederaufnahme hängt an (ein Neustart löscht keine Druckerausgabe, §7.4a).
        if (!d->oeffnen(k.datei, automatisch, &grund)) return fehler(grund);
        e.datei = std::move(d);
        e.wandler->anbinden();   // Eingänge wie „verbunden", Empfang bleibt leer (§6.6)
        e.zustand = Zustand::Verbunden;
    } else if (k.rolle == Rolle::Server) {
        net::Lauscher lau;
        if (k.port == 0) {
            lau = net::lauschen(0);
        } else if (automatisch) {
            // §7.4a: nur der eingestellte Port.  Belegt → nicht starten, Vorschlag melden.
            const net::PortPruefung pp = net::portPruefen(k.port);
            if (pp.frei) lau = net::lauschen(k.port);
            if (!lau.sock) {
                if (!pp.frei && pp.fehler.code != 0 && !pp.fehler.belegt)
                    return fehler(pp.fehler.text);
                // Belegt (oder zwischen Prüfen und Lauschen belegt worden).
                const net::PortPruefung neu = pp.frei ? net::portPruefen(k.port) : pp;
                e.portVorschlag = static_cast<uint16_t>(neu.vorschlag);
                e.zustand = Zustand::Aus;
                e.meldung = "Port " + std::to_string(k.port) + " belegt — " +
                            (neu.vorschlag ? std::to_string(neu.vorschlag) +
                                                 " ist frei, bitte von Hand starten"
                                           : std::string("kein freier Port darüber"));
                return false;
            }
        } else {
            lau = net::lauschenMitSuche(k.port);   // §7.2
        }
        if (!lau.sock) {
            if (lau.fehler.belegt)
                return fehler("kein freier Port ab " + std::to_string(k.port));
            return fehler(lau.fehler.text.empty() ? "Lauschen gescheitert" : lau.fehler.text);
        }
        e.lauscher  = std::move(lau.sock);
        e.portAktiv = static_cast<uint16_t>(lau.port);
        e.zustand   = Zustand::Lauscht;
    } else {
        if (net::adresseKlassifizieren(k.host) == net::AdressArt::Ungueltig)
            return fehler("ungültiger Host");
        if (k.port == 0) return fehler("ungültiger Port");
        e.zustand          = Zustand::Verbindet;
        e.naechsterVersuch = Uhr::now();
    }
    sicherstellenFaden();
    wecker_->wecken();
    return true;
}

void SerialHub::stop(int i) {
    if (i < 0 || i >= anzahl()) return;
    std::lock_guard<std::mutex> l(m_);
    stopIntern(*eintraege_[static_cast<size_t>(i)]);
    // Der I/O-Faden merkt beim nächsten Aufwachen, dass nichts mehr aktiv ist.
    wecker_->wecken();
}

void SerialHub::stopAlle() {
    std::lock_guard<std::mutex> l(m_);
    for (auto& e : eintraege_) stopIntern(*e);
    wecker_->wecken();
}

void SerialHub::stopIntern(Eintrag& e) {
    // Den I/O-Faden IMMER wecken: solange er in poll() auf einem Socket wartet, hält der
    // Kern die Datei fest — ein close() hier schickte das FIN erst nach seinem Aufwachen
    // (bis zu 1 s später), die Gegenseite sähe die Trennung verspätet.
    wecker_->wecken();
    if (e.verbinder) {
        std::lock_guard<std::mutex> l(e.verbinder->m);
        e.verbinder->abgebrochen = true;
        e.verbinder->sock.schliessen();
    }
    e.verbinder.reset();
    const bool warFehler = e.zustand == Zustand::Fehler;
    bool dateiOk = true;
    std::string grund;
    if (e.datei) {
        // Rest aus dem Sendepuffer mitnehmen (VOR dem Abbinden, das ihn verwirft) —
        // der Gast hat ihn schon „gedruckt".
        uint8_t buf[PUFFER];
        const size_t n = e.wandler->fernNimm(buf, sizeof buf);
        e.datei->aufnehmen(buf, n);
        dateiOk = e.datei->schliessen(&grund);
    }
    e.lauscher.schliessen();
    if (e.verbindung || e.datei) e.wandler->abbinden();
    e.datei.reset();
    e.verbindung.schliessen();
    e.transport.reset();
    e.aus.clear();
    e.rueckstau.clear();
    e.gegenstelle.clear();
    {
        const bool ok = dateiOk;
        if (!ok) {
            e.zustand = Zustand::Fehler;
            e.meldung = grund;
            e.portAktiv = 0;
            return;
        }
    }
    e.portAktiv = 0;
    e.versuche  = 0;
    if (!warFehler) e.meldung.clear();
    e.zustand = Zustand::Aus;
}

SerialStatus SerialHub::status(int i) const {
    SerialStatus s;
    if (i < 0 || i >= anzahl()) return s;
    std::lock_guard<std::mutex> l(m_);
    const Eintrag& e = *eintraege_[static_cast<size_t>(i)];
    s.zustand        = e.zustand;
    s.port_aktiv     = e.portAktiv;
    s.gegenstelle    = e.gegenstelle;
    s.meldung        = e.meldung;
    s.port_vorschlag = e.portVorschlag;
    s.rolle          = e.konfig.rolle;
    s.betriebsart    = e.konfig.betriebsart;
    s.versuche       = e.versuche;
    if (e.transport) {
        s.baud_gegenseite = e.transport->baudGegenseite();
        s.baud_abweichend = e.transport->baudAbweichend();
    }
    const WandlerSicht w = e.wandler->sicht();
    s.baud_nenn        = w.format.baud_nenn;
    s.daten            = w.format.daten;
    s.paritaet         = w.format.paritaet;
    s.stopp_halbe      = w.format.stopp_halbe;
    s.format_gueltig   = w.format.gueltig;
    s.rts              = w.rts;
    s.cts              = w.cts;
    s.dtr              = w.dtr;
    s.dsr              = w.dsr;
    s.dcd              = w.dcd;
    s.bytes_gesendet   = w.bytes_gesendet;
    s.bytes_empfangen  = w.bytes_empfangen;
    s.puffer_senden    = w.puffer_senden;
    s.puffer_empfangen = w.puffer_empfangen;
    return s;
}

void SerialHub::setzeVersuchAbstand(std::chrono::milliseconds ms) {
    std::lock_guard<std::mutex> l(m_);
    versuchAbstand_ = ms;
}

// ─── I/O-Faden ─────────────────────────────────────────────────────────────

void SerialHub::sicherstellenFaden() {
    if (laeuft_) return;
    // Ein beendeter Faden hat laeuft_ unter m_ zurückgesetzt und fasst m_ danach nicht
    // mehr an — das join hier unter m_ kann also nicht verklemmen.
    if (faden_.joinable()) faden_.join();
    laeuft_ = true;
    faden_  = std::thread(&SerialHub::lauf, this);
}

void SerialHub::lauf() {
    std::vector<net::PollEintrag> pe;
    while (true) {
        int frist = 1000;
        {
            std::lock_guard<std::mutex> l(m_);
            const bool irgendAktiv = std::any_of(eintraege_.begin(), eintraege_.end(),
                                                 [](const auto& e) { return istAktiv(e->zustand); });
            if (beenden_ || !irgendAktiv) {
                laeuft_ = false;
                return;
            }
            pe.clear();
            net::PollEintrag w;
            w.fd    = wecker_->lesefd();
            w.lesen = true;
            pe.push_back(w);
            const auto jetzt = Uhr::now();
            for (auto& ep : eintraege_) {
                Eintrag& e = *ep;
                if (e.lauscher) {
                    net::PollEintrag p;
                    p.fd    = e.lauscher.fd();
                    p.lesen = true;
                    pe.push_back(p);
                }
                if (e.verbindung) {
                    // Lesen nur, wenn Platz ist — sonst Rückstau über das TCP-Fenster
                    // (und kein Dauerwecken durch Daten, die wir nicht nehmen).
                    net::PollEintrag p;
                    p.fd        = e.verbindung.fd();
                    p.lesen     = e.rueckstau.empty() && e.wandler->fernFrei() > 0;
                    p.schreiben = !e.aus.empty();
                    if (p.lesen || p.schreiben) pe.push_back(p);
                }
                if (e.zustand == Zustand::Verbindet && !e.verbinder) {
                    const auto rest = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          e.naechsterVersuch - jetzt).count();
                    frist = std::min<int>(frist, rest < 0 ? 0 : static_cast<int>(rest));
                }
                if (e.datei) {
                    const int r = e.datei->restMs(jetzt);
                    if (r >= 0) frist = std::min(frist, r);
                }
            }
        }
        net::warten(pe, frist);
        {
            std::lock_guard<std::mutex> l(m_);
            wecker_->leeren();
            const auto jetzt = Uhr::now();
            for (auto& e : eintraege_) bearbeite(*e, jetzt);
        }
    }
}

void SerialHub::bearbeite(Eintrag& e, Uhr::time_point jetzt) {
    Wandler& w = *e.wandler;

    if (e.datei) {
        uint8_t buf[PUFFER];
        size_t n;
        while ((n = w.fernNimm(buf, sizeof buf)) > 0) e.datei->aufnehmen(buf, n);
        if (e.datei->faellig(jetzt)) {
            std::string grund;
            if (!e.datei->schreiben(&grund)) {
                // §6.6: Schreibfehler → Zustand FEHLER, Text im Block.
                std::string egal;
                e.datei->schliessen(&egal);
                e.datei.reset();
                w.abbinden();
                e.zustand = Zustand::Fehler;
                e.meldung = grund;
            }
        }
        return;
    }

    if (e.lauscher) {
        while (true) {
            net::Socket s = net::annehmen(e.lauscher.fd());
            if (!s) break;
            if (!e.verbindung) {
                const std::string gegen = net::gegenstelle(s.fd());
                anbinden(e, std::move(s), gegen);
                continue;
            }
            // §7.1: Server hat eine Verbindung — jede weitere wird angenommen und sofort
            // geschlossen, die bestehende bleibt unberührt.  Das Lauschen bleibt (sonst
            // könnte ein anderes Programm den Port in der Zwischenzeit belegen).
            if (e.konfig.betriebsart == Betriebsart::Telnet) {
                static const char belegt[] = "belegt\r\n";
                net::senden(s.fd(), belegt, sizeof belegt - 1);
            }
            // Schon Angekommenes (Telnet-Verhandlung des Abgewiesenen) wegräumen: ein
            // close mit ungelesenen Daten schickt RST und verwürfe die Zeile „belegt".
            uint8_t weg[512];
            net::empfangen(s.fd(), weg, sizeof weg);
        }
    }

    if (e.konfig.rolle == Rolle::Client && e.zustand == Zustand::Verbindet &&
        e.konfig.betriebsart != Betriebsart::Datei) {
        if (e.verbinder) {
            std::shared_ptr<Verbinder> v = e.verbinder;
            net::Socket s;
            std::string grund, gegen;
            bool fertig;
            {
                std::lock_guard<std::mutex> l(v->m);
                fertig = v->fertig;
                if (fertig) {
                    s     = std::move(v->sock);
                    grund = v->grund;
                    gegen = v->gegenstelle;
                }
            }
            if (fertig) {
                e.verbinder.reset();
                if (s) anbinden(e, std::move(s), gegen);
                else   e.meldung = grund;
            }
        }
        if (!e.verbindung && !e.verbinder && jetzt >= e.naechsterVersuch) {
            e.verbinder = std::make_shared<Verbinder>();
            ++e.versuche;
            // Der Abstand zählt von Versuch zu Versuch (Uhrzeit, §7.1).
            e.naechsterVersuch = jetzt + versuchAbstand_;
            std::thread(verbinderLauf, e.verbinder, wecker_, e.konfig.host,
                        static_cast<int>(e.konfig.port)).detach();
        }
    }

    if (e.verbindung) netzIo(e);
}

void SerialHub::anbinden(Eintrag& e, net::Socket&& sock, const std::string& gegenstelle) {
    const auto rolle = e.konfig.rolle == Rolle::Server ? ::serial::TelnetRolle::Server
                                                       : ::serial::TelnetRolle::Client;
    if (e.konfig.betriebsart == Betriebsart::Rfc2217)
        e.transport = std::make_unique<Rfc2217Transport>(rolle);
    else
        e.transport = std::make_unique<TelnetTransport>(rolle);
    e.verbindung  = std::move(sock);
    e.gegenstelle = gegenstelle;
    e.aus.clear();
    e.rueckstau.clear();
    e.transport->start(*e.wandler);
    const auto a = e.transport->nimmAusgabe();
    e.aus.insert(e.aus.end(), a.begin(), a.end());
    e.zustand = Zustand::Verbunden;
    e.meldung.clear();
    if (e.konfig.rolle == Rolle::Client) e.versuche = 0;   // „seit dem letzten Verbinden"
    netzIo(e);
}

void SerialHub::trennen(Eintrag& e, const std::string& grund) {
    e.verbindung.schliessen();
    e.transport.reset();
    e.aus.clear();
    e.rueckstau.clear();
    e.gegenstelle.clear();
    e.wandler->abbinden();
    if (e.konfig.rolle == Rolle::Server) {
        e.zustand = Zustand::Lauscht;   // Trennt der Client, lauscht der Server weiter.
    } else {
        // Dauerversuch geht weiter; ein Versuch jetzt, wenn der letzte ≥ 1 s her ist.
        e.zustand = Zustand::Verbindet;
        e.meldung = grund;
    }
    wecker_->wecken();
}

void SerialHub::netzIo(Eintrag& e) {
    Wandler& w = *e.wandler;
    NetzTransport& t = *e.transport;
    const net::SockFd fd = e.verbindung.fd();

    // ── Empfangen: Socket → Codec → Empfangspuffer (Rückstau: nicht mehr lesen) ──
    if (!e.rueckstau.empty()) {
        const size_t k = w.fernGib(e.rueckstau.data(), e.rueckstau.size());
        e.rueckstau.erase(e.rueckstau.begin(), e.rueckstau.begin() + static_cast<std::ptrdiff_t>(k));
    }
    while (e.rueckstau.empty()) {
        const size_t frei = w.fernFrei();
        if (frei == 0) break;
        uint8_t buf[PUFFER];
        // Höchstens so viel lesen, wie Platz ist: dekodiert wird es nie mehr.
        const net::IoErgebnis r = net::empfangen(fd, buf, std::min(frei, sizeof buf));
        if (r.status == net::IoStatus::Warten) break;
        if (r.status != net::IoStatus::Ok) {
            trennen(e, r.status == net::IoStatus::Geschlossen ? "Gegenstelle hat getrennt"
                                                             : "Verbindung abgebrochen: " + r.fehler.text);
            return;
        }
        t.eingabe(buf, r.n);
        const std::vector<uint8_t> nd = t.nimmNutzdaten();
        const size_t k = w.fernGib(nd.data(), nd.size());
        if (k < nd.size()) e.rueckstau.assign(nd.begin() + static_cast<std::ptrdiff_t>(k), nd.end());
    }

    t.abgleich(w);

    // ── Senden: Sendepuffer → Codec → Socket ─────────────────────────────────
    if (!t.sendenAngehalten() && e.aus.size() < PUFFER) {
        uint8_t buf[PUFFER];
        const size_t n = w.fernNimm(buf, PUFFER - e.aus.size());
        if (n) t.sende(buf, n);
    }
    const auto a = t.nimmAusgabe();
    e.aus.insert(e.aus.end(), a.begin(), a.end());
    while (!e.aus.empty()) {
        const net::IoErgebnis r = net::senden(fd, e.aus.data(), e.aus.size());
        if (r.status == net::IoStatus::Ok && r.n > 0) {
            e.aus.erase(e.aus.begin(), e.aus.begin() + static_cast<std::ptrdiff_t>(r.n));
            continue;
        }
        if (r.status == net::IoStatus::Warten || r.status == net::IoStatus::Ok) break;
        trennen(e, "Verbindung abgebrochen: " + r.fehler.text);
        return;
    }
}

}  // namespace k1520::serial

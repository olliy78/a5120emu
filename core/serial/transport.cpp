#include "core/serial/transport.h"

#include <cerrno>
#include <cstring>
#include <filesystem>

#include "core/version.h"

namespace k1520::serial {

using ::serial::Rfc2217Ereignis;
using ::serial::TelnetRolle;
namespace r2 = ::serial::rfc2217;

std::string signaturText() { return std::string("k1520emu ") + K1520_VERSION_TEXT; }

// ─── Telnet ─────────────────────────────────────────────────────────────────

void TelnetTransport::start(Wandler& w) {
    codec_.start();
    w.anbinden();   // keine Leitungen: Eingänge = „verbunden" (§6.4)
}

// ─── RFC 2217 ───────────────────────────────────────────────────────────────

Rfc2217Transport::Rfc2217Transport(TelnetRolle rolle, std::string signatur)
    : codec_(rolle), signatur_(std::move(signatur)) {}

uint8_t Rfc2217Transport::flussWert(bool xonxoff) const {
    // Flussart aus den Wandler-Einstellungen (§7.5).  Hardware (3) wird bewusst NICHT
    // gemeldet: die RTS/CTS-Brücke ist ein Stecker, kein Protokoll, und ein ser2net mit
    // Hardware-Fluss hielte an einem Gerät ohne CTS-Treiber an.  Den RTS-Halt leistet
    // der Wandler selbst (§6.4), RTS geht als SET-CONTROL ohnehin hinaus.
    return xonxoff ? r2::FLUSS_XONXOFF : r2::FLUSS_KEINE;
}

void Rfc2217Transport::sendeFormat(const SerialFormat& f) {
    codec_.sendeBaud(f.baud_nenn);
    codec_.sendeDatenbits(f.daten);
    codec_.sendeParitaet(r2::paritaetNetz(f.paritaet));
    codec_.sendeStoppbits(r2::stoppNetz(f.stopp_halbe));
    gesendeteBaud_ = f.baud_nenn;
    gesendetDaten_ = f.daten;
    gesendetParitaet_ = f.paritaet;
    gesendetStopp_ = f.stopp_halbe;
    formatVergleichen(f);
    // Die Antwort des Servers kommt noch; bis dahin gilt der alte Vergleich nicht mehr.
    abweichend_ = baudGegenseite_ != 0 && baudGegenseite_ != gesendeteBaud_;
}

void Rfc2217Transport::formatVergleichen(const SerialFormat& gast) {
    // Client vergleicht mit dem, was er selbst verlangt hat; Server mit dem Gastwert.
    const uint8_t d = client() ? gesendetDaten_ : gast.daten;
    const uint8_t p = client() ? gesendetParitaet_ : gast.paritaet;
    const uint8_t s = client() ? gesendetStopp_ : gast.stopp_halbe;
    fern_.formatAbweichend = fern_.formatBekannt() &&
                             (fern_.daten != d || fern_.paritaet != p || fern_.stoppHalbe != s);
}

GegenseiteStand Rfc2217Transport::gegenseite() const {
    GegenseiteStand g = fern_;
    if (client()) {
        g.leitungenBekannt = fernModemBekannt_ ? (gegenleitung::CTS | gegenleitung::DSR |
                                                  gegenleitung::DCD | gegenleitung::RI) : 0;
        if (fernModemBekannt_) {
            if (fernModem_ & r2::MS_CTS) g.leitungen |= gegenleitung::CTS;
            if (fernModem_ & r2::MS_DSR) g.leitungen |= gegenleitung::DSR;
            if (fernModem_ & r2::MS_CD)  g.leitungen |= gegenleitung::DCD;
            if (fernModem_ & r2::MS_RI)  g.leitungen |= gegenleitung::RI;
        }
    } else {
        // Vor dem ersten SET-CONTROL gelten die Leitungen intern als aktiv (damit der Gast
        // nicht blockiert), sind der Gegenseite aber nicht bekannt.
        if (fernRtsBekannt_) {
            g.leitungenBekannt |= gegenleitung::RTS;
            if (fernRts_) g.leitungen |= gegenleitung::RTS;
        }
        if (fernDtrBekannt_) {
            g.leitungenBekannt |= gegenleitung::DTR;
            if (fernDtr_) g.leitungen |= gegenleitung::DTR;
        }
    }
    return g;
}

uint8_t Rfc2217Transport::modemByte(const WandlerSicht& s, bool v24) const {
    if (!v24) return r2::MS_CTS | r2::MS_DSR | r2::MS_CD;   // keine Leitungen: „bereit"
    uint8_t m = 0;
    if (s.rts) m |= r2::MS_CTS;               // Nullmodem: unser RTS → sein CTS
    if (s.dtr) m |= r2::MS_DSR | r2::MS_CD;   // unser DTR → sein DSR + CD
    return m;
}

void Rfc2217Transport::start(Wandler& w) {
    codec_.start();
    w.anbinden();
    const WandlerSicht s = w.sicht();
    const bool xon = w.einstellung().xonxoff;
    if (client()) {
        sendeFormat(s.gemeldet);
        formatStand_ = s.gemeldetStand;
        codec_.sendeSteuerung(flussWert(xon));
        xonGemeldet_ = xon;
        if (w.v24()) {
            codec_.sendeSteuerung(s.rts ? r2::RTS_EIN : r2::RTS_AUS);
            codec_.sendeSteuerung(s.dtr ? r2::DTR_EIN : r2::DTR_AUS);
        }
        rtsGemeldet_ = s.rts;
        dtrGemeldet_ = s.dtr;
        brkGemeldet_ = false;
    } else {
        // Anfangszustand ungefiltert, damit der Client die Leitungen kennt.
        modemGemeldet_ = modemByte(s, w.v24());
        codec_.sendeModemState(modemGemeldet_, true);
        w.fernLeitungen(fernRts_, fernDtr_, fernDtr_);
    }
}

void Rfc2217Transport::abgleich(Wandler& w) {
    const WandlerSicht s = w.sicht();
    const WandlerEinstellung e = w.einstellung();
    const SerialFormat& gast = s.wirksam;   // Leitsatz 4: der Gast ist maßgeblich

    for (const Rfc2217Ereignis& ev : codec_.nimmEreignisse()) {
        using A = Rfc2217Ereignis::Art;
        if (client()) {
            switch (ev.art) {
                case A::Baud:
                    if (ev.antwort && ev.wert) {
                        baudGegenseite_ = ev.wert;
                        abweichend_ = ev.wert != gesendeteBaud_;
                    }
                    break;
                case A::Datenbits:
                    if (ev.antwort && ev.wert) { fern_.daten = static_cast<uint8_t>(ev.wert); formatVergleichen(gast); }
                    break;
                case A::Paritaet:
                    if (ev.antwort && ev.wert) {
                        fern_.paritaet = ev.wert == 4 ? 3 : ev.wert == 5 ? 4 : r2::paritaetSio(ev.wert);
                        fern_.paritaetBekannt = fern_.paritaet != 255;
                        formatVergleichen(gast);
                    }
                    break;
                case A::Stoppbits:
                    if (ev.antwort && ev.wert) { fern_.stoppHalbe = r2::stoppHalbe(ev.wert); formatVergleichen(gast); }
                    break;
                case A::ModemState:
                    fernModem_ = static_cast<uint8_t>(ev.wert);
                    fernModemBekannt_ = true;
                    w.fernLeitungen(ev.wert & r2::MS_CTS, ev.wert & r2::MS_DSR,
                                    ev.wert & r2::MS_CD);
                    break;
                case A::LineState:
                    w.fernBreak((ev.wert & 0x10) != 0);   // Break-Detect
                    break;
                case A::Signatur:
                    if (!ev.antwort && ev.text.empty()) codec_.sendeSignatur(signatur_, true);
                    break;
                case A::FlussHalt:   angehalten_ = true; break;
                case A::FlussWeiter: angehalten_ = false; break;
                default: break;   // Antworten auf DATASIZE/PARITY/… interessieren nicht
            }
            continue;
        }
        // Server: jede Anfrage wird mit dem TATSÄCHLICHEN Wert beantwortet.
        if (ev.antwort) continue;
        switch (ev.art) {
            case A::Signatur:
                // Leer = Anfrage; mit Text = Signatur des Clients (nur zur Kenntnis).
                if (ev.text.empty()) codec_.sendeSignatur(signatur_, true);
                break;
            case A::Baud:
                if (ev.wert) baudGegenseite_ = ev.wert;
                codec_.sendeBaud(gast.baud_nenn, true);
                break;
            case A::Datenbits:
                if (ev.wert) fern_.daten = static_cast<uint8_t>(ev.wert);
                codec_.sendeDatenbits(gast.daten, true);
                break;
            case A::Paritaet:
                if (ev.wert) {
                    fern_.paritaet = ev.wert == 4 ? 3 : ev.wert == 5 ? 4 : r2::paritaetSio(ev.wert);
                    fern_.paritaetBekannt = fern_.paritaet != 255;
                }
                codec_.sendeParitaet(r2::paritaetNetz(gast.paritaet), true);
                break;
            case A::Stoppbits:
                if (ev.wert) fern_.stoppHalbe = r2::stoppHalbe(ev.wert);
                codec_.sendeStoppbits(r2::stoppNetz(gast.stopp_halbe), true);
                break;
            case A::Steuerung: {
                uint8_t antwort = static_cast<uint8_t>(ev.wert);
                switch (ev.wert) {
                    case r2::RTS_EIN: fernRts_ = true; fernRtsBekannt_ = true; break;
                    case r2::RTS_AUS: fernRts_ = false; fernRtsBekannt_ = true; break;
                    case r2::RTS_ABFRAGE: antwort = fernRts_ ? r2::RTS_EIN : r2::RTS_AUS; break;
                    case r2::DTR_EIN: fernDtr_ = true; fernDtrBekannt_ = true; break;
                    case r2::DTR_AUS: fernDtr_ = false; fernDtrBekannt_ = true; break;
                    case r2::DTR_ABFRAGE: antwort = fernDtr_ ? r2::DTR_EIN : r2::DTR_AUS; break;
                    case r2::BREAK_EIN: w.fernBreak(true); break;
                    case r2::BREAK_AUS: w.fernBreak(false); break;
                    case r2::BREAK_ABFRAGE: antwort = s.brk ? r2::BREAK_EIN : r2::BREAK_AUS; break;
                    case r2::FLUSS_ABFRAGE: case r2::FLUSS_KEINE: case r2::FLUSS_XONXOFF:
                    case r2::FLUSS_HARDWARE: case r2::FLUSS_DCD: case r2::FLUSS_DTR:
                    case r2::FLUSS_DSR:
                        antwort = flussWert(e.xonxoff);   // Einstellung ändert nur der Anwender
                        break;
                    case r2::EINFLUSS_ABFRAGE: case r2::EINFLUSS_KEINE:
                    case r2::EINFLUSS_XONXOFF: case r2::EINFLUSS_HARDWARE:
                        antwort = e.xonxoff ? r2::EINFLUSS_XONXOFF : r2::EINFLUSS_KEINE;
                        break;
                    default: break;
                }
                // Nullmodem-Kreuzung (§6.4): sein RTS → unser CTS, sein DTR → DSR + DCD.
                w.fernLeitungen(fernRts_, fernDtr_, fernDtr_);
                codec_.sendeSteuerung(antwort, true);
                break;
            }
            case A::LineStateMaske:  codec_.sendeLineStateMaske(static_cast<uint8_t>(ev.wert), true); break;
            case A::ModemStateMaske: codec_.sendeModemStateMaske(static_cast<uint8_t>(ev.wert), true); break;
            case A::FlussHalt:
                angehalten_ = true;
                codec_.sendeFlussHalt(true, true);
                break;
            case A::FlussWeiter:
                angehalten_ = false;
                codec_.sendeFlussHalt(false, true);
                break;
            case A::Leeren:
                // Sicht des Zugangsservers: „Empfangspuffer" = was vom Gerät (unserem
                // Gast) kam und zum Netz will = unser Sendepuffer; „Sendepuffer" = was
                // zum Gerät will = unser Empfangspuffer.
                w.leeren(ev.wert & r2::PURGE_EMPFANG, ev.wert & r2::PURGE_SENDEN);
                codec_.sendeLeeren(static_cast<uint8_t>(ev.wert), true);
                break;
            default: break;
        }
    }

    if (client()) {
        if (s.gemeldetStand != formatStand_) {
            formatStand_ = s.gemeldetStand;
            sendeFormat(s.gemeldet);
        }
        if (e.xonxoff != xonGemeldet_) {
            xonGemeldet_ = e.xonxoff;
            codec_.sendeSteuerung(flussWert(e.xonxoff));
        }
        if (w.v24()) {
            if (s.rts != rtsGemeldet_) codec_.sendeSteuerung(s.rts ? r2::RTS_EIN : r2::RTS_AUS);
            if (s.dtr != dtrGemeldet_) codec_.sendeSteuerung(s.dtr ? r2::DTR_EIN : r2::DTR_AUS);
        }
        rtsGemeldet_ = s.rts;
        dtrGemeldet_ = s.dtr;
        if (s.brk != brkGemeldet_) {
            brkGemeldet_ = s.brk;
            codec_.sendeSteuerung(s.brk ? r2::BREAK_EIN : r2::BREAK_AUS);
        }
    } else {
        const uint8_t m = modemByte(s, w.v24());
        if (m != modemGemeldet_) {
            // Deltabits (untere Hälfte) zu den geänderten Zuständen.
            const uint8_t diff = m ^ modemGemeldet_;
            uint8_t delta = 0;
            if (diff & r2::MS_CTS) delta |= r2::MS_DELTA_CTS;
            if (diff & r2::MS_DSR) delta |= r2::MS_DELTA_DSR;
            if (diff & r2::MS_CD)  delta |= r2::MS_DELTA_CD;
            codec_.sendeModemState(m | delta);   // Maske des Clients beachtet der Codec
            modemGemeldet_ = m;
        }
        if (s.brk != lineBrkGemeldet_) {
            lineBrkGemeldet_ = s.brk;
            codec_.sendeLineState(s.brk ? 0x10 : 0x00);
        }
        // Hinweis Baudunterschied (§7.5): Anfrage des Clients ≠ Gastwert.
        abweichend_ = baudGegenseite_ != 0 && baudGegenseite_ != gast.baud_nenn;
        formatVergleichen(gast);
    }
}

// ─── Datei ──────────────────────────────────────────────────────────────────

DateiTransport::~DateiTransport() {
    std::string egal;
    if (offen()) schliessen(&egal);
}

bool DateiTransport::oeffnen(const std::string& pfad, bool anhaengen, std::string* fehler) {
    if (offen()) f_.close();
    puffer_.clear();
    auto modus = std::ios::binary | std::ios::out | (anhaengen ? std::ios::app : std::ios::trunc);
    errno = 0;
    // u8path: der Pfad kommt als UTF-8 aus der Oberfläche; unter Windows öffnet
    // std::ofstream dann über den Breitzeichenweg (Umlaute im Dokumentenordner).
    f_.open(std::filesystem::u8path(pfad), modus);
    if (!f_.is_open()) {
        if (fehler) {
            *fehler = "Datei lässt sich nicht öffnen: " + pfad;
            if (errno) *fehler += std::string(" (") + std::strerror(errno) + ")";
        }
        return false;
    }
    return true;
}

void DateiTransport::aufnehmen(const uint8_t* d, size_t n) {
    if (n == 0) return;
    if (puffer_.empty()) seit_ = Uhr::now();
    puffer_.insert(puffer_.end(), d, d + n);
}

bool DateiTransport::faellig(Uhr::time_point jetzt) const {
    if (puffer_.empty()) return false;
    return puffer_.size() >= SOFORT || jetzt - seit_ >= INTERVALL;
}

int DateiTransport::restMs(Uhr::time_point jetzt) const {
    if (puffer_.empty()) return -1;
    const auto rest = std::chrono::duration_cast<std::chrono::milliseconds>(seit_ + INTERVALL - jetzt);
    return rest.count() < 0 ? 0 : static_cast<int>(rest.count());
}

bool DateiTransport::schreiben(std::string* fehler) {
    if (!offen()) return false;
    if (!puffer_.empty()) {
        errno = 0;
        f_.write(reinterpret_cast<const char*>(puffer_.data()),
                 static_cast<std::streamsize>(puffer_.size()));
        f_.flush();
        puffer_.clear();
    }
    if (!f_) {
        if (fehler) {
            *fehler = "Schreibfehler";
            if (errno) *fehler += std::string(": ") + std::strerror(errno);
        }
        return false;
    }
    return true;
}

bool DateiTransport::schliessen(std::string* fehler) {
    if (!offen()) return true;
    const bool ok = schreiben(fehler);
    f_.close();
    return ok;
}

}  // namespace k1520::serial

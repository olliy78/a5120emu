/**
 * @file telnet_codec.cpp
 * @brief Telnet-Zustandsautomat — siehe telnet_codec.h.
 */
#include "core/serial/telnet_codec.h"

namespace serial {

using namespace telnet;

TelnetCodec::TelnetCodec(TelnetRolle rolle) : rolle_(rolle) {
    // Annahmebereitschaft (§7.4).  Der Server bietet ECHO/SGA/BINARY an und
    // wünscht BINARY vom Client; der Client stimmt dem zu und nimmt auch ein
    // angebotenes ECHO an.  Alles andere bleibt auf "abgelehnt".
    if (rolle == TelnetRolle::Server) {
        for (uint8_t o : {OPT_ECHO, OPT_SGA, OPT_BINARY}) opt_[o].lokalOk = true;
        for (uint8_t o : {OPT_SGA, OPT_BINARY}) opt_[o].entferntOk = true;
    } else {
        for (uint8_t o : {OPT_SGA, OPT_BINARY}) opt_[o].lokalOk = true;
        for (uint8_t o : {OPT_ECHO, OPT_SGA, OPT_BINARY}) opt_[o].entferntOk = true;
    }
}

void TelnetCodec::start() {
    if (gestartet_) return;
    gestartet_ = true;
    if (rolle_ == TelnetRolle::Server) {
        bieteAn(OPT_ECHO);
        bieteAn(OPT_SGA);
        bieteAn(OPT_BINARY);
        verlange(OPT_BINARY);
    }
}

void TelnetCodec::erlaube(uint8_t option, bool lokal, bool entfernt) {
    opt_[option].lokalOk = lokal;
    opt_[option].entferntOk = entfernt;
}

void TelnetCodec::bieteAn(uint8_t option) { wunsch(opt_[option].us, true, WILL, WONT, option); }
void TelnetCodec::verlange(uint8_t option) { wunsch(opt_[option].him, true, DO, DONT, option); }

// Eigener Wunsch, RFC 1143 §7.  Der "Gegenwunsch"-Merker (gegen) fängt einen
// Wunsch ab, der eintrifft, solange die vorige Anfrage noch unbeantwortet ist.
void TelnetCodec::wunsch(Q& q, bool ein, uint8_t jaVerb, uint8_t neinVerb, uint8_t opt) {
    if (ein) {
        switch (q.s) {
            case Q::Nein: q.s = Q::WillJa; dreiByte(jaVerb, opt); break;
            case Q::Ja: break;
            case Q::WillNein: q.gegen = true; break;
            case Q::WillJa: q.gegen = false; break;
        }
    } else {
        switch (q.s) {
            case Q::Ja: q.s = Q::WillNein; dreiByte(neinVerb, opt); break;
            case Q::Nein: break;
            case Q::WillNein: q.gegen = false; break;
            case Q::WillJa: q.gegen = true; break;
        }
    }
}

// Antwort der Gegenseite, RFC 1143 §7.  ja = WILL/DO, sonst WONT/DONT.
// Eine Bestätigung auf eigene Anfrage (WillJa → Ja) bleibt stumm — das ist
// der Schleifenschutz: nie dieselbe Mitteilung zweimal beantworten.
void TelnetCodec::antwort(Q& q, bool ja, bool erlaubt, uint8_t jaVerb, uint8_t neinVerb,
                          uint8_t opt) {
    if (ja) {
        switch (q.s) {
            case Q::Nein:
                if (erlaubt) { q.s = Q::Ja; dreiByte(jaVerb, opt); }
                else dreiByte(neinVerb, opt);
                break;
            case Q::Ja: break;
            case Q::WillNein:  // Fehler der Gegenseite, Zustand beruhigen
                q.s = q.gegen ? Q::Ja : Q::Nein;
                q.gegen = false;
                break;
            case Q::WillJa:
                if (q.gegen) { q.s = Q::WillNein; q.gegen = false; dreiByte(neinVerb, opt); }
                else q.s = Q::Ja;
                break;
        }
    } else {
        switch (q.s) {
            case Q::Ja: q.s = Q::Nein; dreiByte(neinVerb, opt); break;
            case Q::Nein: break;
            case Q::WillNein:
                if (q.gegen) { q.s = Q::WillJa; q.gegen = false; dreiByte(jaVerb, opt); }
                else q.s = Q::Nein;
                break;
            case Q::WillJa: q.s = Q::Nein; q.gegen = false; break;
        }
    }
}

void TelnetCodec::empfangVerb(uint8_t verb, uint8_t opt) {
    Opt& o = opt_[opt];
    switch (verb) {
        case WILL: antwort(o.him, true, o.entferntOk, DO, DONT, opt); break;
        case WONT: antwort(o.him, false, o.entferntOk, DO, DONT, opt); break;
        case DO: antwort(o.us, true, o.lokalOk, WILL, WONT, opt); break;
        case DONT: antwort(o.us, false, o.lokalOk, WILL, WONT, opt); break;
        default: break;
    }
}

void TelnetCodec::nutzbyte(uint8_t b) {
    // CR NUL → CR, solange die Gegenseite nicht BINARY sendet.
    if (!entferntAktiv(OPT_BINARY)) {
        if (crGesehen_ && b == 0) { crGesehen_ = false; return; }
        crGesehen_ = (b == 0x0D);
    } else {
        crGesehen_ = false;
    }
    ein_.push_back(b);
}

void TelnetCodec::sbAbschluss() {
    if (!sbZuLang_) sub_.push_back(TelnetSub{sbOpt_, sbBuf_});
    sbBuf_.clear();
    sbZuLang_ = false;
}

void TelnetCodec::eingabe(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = d[i];
        switch (zst_) {
            case Zst::Daten:
                if (b == IAC) zst_ = Zst::Iac; else nutzbyte(b);
                break;
            case Zst::Iac:
            iac_befehl:
                zst_ = Zst::Daten;
                if (b == IAC) { crGesehen_ = false; ein_.push_back(0xFF); }
                else if (b >= WILL && b <= DONT) { verb_ = b; zst_ = Zst::Verb; }
                else if (b == SB) zst_ = Zst::SbOpt;
                // alles übrige (NOP, AYT, SE ohne SB, …) wird überlesen
                break;
            case Zst::Verb:
                zst_ = Zst::Daten;
                empfangVerb(verb_, b);
                break;
            case Zst::SbOpt:
                sbOpt_ = b;
                sbBuf_.clear();
                sbZuLang_ = false;
                zst_ = Zst::SbDaten;
                break;
            case Zst::SbDaten:
                if (b == IAC) { zst_ = Zst::SbIac; break; }
                if (sbBuf_.size() < SUB_MAX) sbBuf_.push_back(b); else sbZuLang_ = true;
                break;
            case Zst::SbIac:
                if (b == IAC) {
                    zst_ = Zst::SbDaten;
                    if (sbBuf_.size() < SUB_MAX) sbBuf_.push_back(0xFF); else sbZuLang_ = true;
                } else if (b == SE) {
                    zst_ = Zst::Daten;
                    sbAbschluss();
                } else {
                    // Kein gültiges Ende: Unterverhandlung verwerfen, Byte als
                    // gewöhnlichen IAC-Befehl lesen (RFC 855 lässt das offen).
                    sbBuf_.clear();
                    sbZuLang_ = false;
                    goto iac_befehl;
                }
                break;
        }
    }
}

std::vector<uint8_t> TelnetCodec::nimmNutzdaten() {
    std::vector<uint8_t> r;
    r.swap(ein_);
    return r;
}

std::vector<TelnetSub> TelnetCodec::nimmSub() {
    std::vector<TelnetSub> r;
    r.swap(sub_);
    return r;
}

void TelnetCodec::sende(const uint8_t* d, size_t n) {
    const bool binaer = lokalAktiv(OPT_BINARY);
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = d[i];
        aus_.push_back(b);
        if (b == IAC) aus_.push_back(IAC);
        else if (b == 0x0D && !binaer) aus_.push_back(0x00);
    }
}

void TelnetCodec::sendeSub(uint8_t option, const std::vector<uint8_t>& daten) {
    aus_.push_back(IAC);
    aus_.push_back(SB);
    aus_.push_back(option);
    for (uint8_t b : daten) {
        aus_.push_back(b);
        if (b == IAC) aus_.push_back(IAC);
    }
    aus_.push_back(IAC);
    aus_.push_back(SE);
}

std::vector<uint8_t> TelnetCodec::nimmAusgabe() {
    std::vector<uint8_t> r;
    r.swap(aus_);
    return r;
}

}  // namespace serial

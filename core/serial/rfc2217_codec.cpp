/**
 * @file rfc2217_codec.cpp
 * @brief RFC 2217 COM-PORT-Option — siehe rfc2217_codec.h.
 */
#include "core/serial/rfc2217_codec.h"

namespace serial {

using namespace rfc2217;
using Art = Rfc2217Ereignis::Art;

Rfc2217Codec::Rfc2217Codec(TelnetRolle rolle) : telnet_(rolle) {
    // Client: WILL 44 (wir sind der COM-Port-Nutzer), Server: DO 44.
    if (rolle == TelnetRolle::Client) telnet_.erlaube(telnet::OPT_COMPORT, true, false);
    else telnet_.erlaube(telnet::OPT_COMPORT, false, true);
}

void Rfc2217Codec::start() {
    telnet_.start();
    if (rolle() == TelnetRolle::Client) telnet_.bieteAn(telnet::OPT_COMPORT);
    else telnet_.verlange(telnet::OPT_COMPORT);
}

bool Rfc2217Codec::comPortAktiv() const {
    return rolle() == TelnetRolle::Client ? telnet_.lokalAktiv(telnet::OPT_COMPORT)
                                          : telnet_.entferntAktiv(telnet::OPT_COMPORT);
}

void Rfc2217Codec::eingabe(const uint8_t* daten, size_t n) {
    telnet_.eingabe(daten, n);
    for (const TelnetSub& s : telnet_.nimmSub())
        if (s.option == telnet::OPT_COMPORT) verarbeite(s);
}

std::vector<Rfc2217Ereignis> Rfc2217Codec::nimmEreignisse() {
    std::vector<Rfc2217Ereignis> r;
    r.swap(ereignisse_);
    return r;
}

// Dekodieren.  Zu kurze/fremde Befehle werden überlesen (Toleranz gegenüber
// Gegenstellen mit Erweiterungen); Anfragen haben bei 1-Byte-Befehlen den Wert 0.
void Rfc2217Codec::verarbeite(const TelnetSub& sub) {
    const auto& d = sub.daten;
    if (d.empty()) return;
    const bool antwort = d[0] >= ANTWORT;
    const uint8_t cmd = antwort ? d[0] - ANTWORT : d[0];
    const size_t n = d.size() - 1;  // Nutzlast hinter der Befehlsnummer
    Rfc2217Ereignis e;
    e.antwort = antwort;
    auto ein = [&](Art a) { e.art = a; e.wert = d[1]; ereignisse_.push_back(e); };

    switch (cmd) {
        case SIGNATURE:
            e.art = Art::Signatur;
            e.text.assign(d.begin() + 1, d.end());
            ereignisse_.push_back(e);
            break;
        case SET_BAUDRATE:
            if (n < 4) return;
            e.art = Art::Baud;
            e.wert = (uint32_t(d[1]) << 24) | (uint32_t(d[2]) << 16) | (uint32_t(d[3]) << 8) | d[4];
            ereignisse_.push_back(e);
            break;
        case SET_DATASIZE: if (n >= 1) ein(Art::Datenbits); break;
        case SET_PARITY: if (n >= 1) ein(Art::Paritaet); break;
        case SET_STOPSIZE: if (n >= 1) ein(Art::Stoppbits); break;
        case SET_CONTROL: if (n >= 1) ein(Art::Steuerung); break;
        case NOTIFY_LINESTATE: if (n >= 1) ein(Art::LineState); break;
        case NOTIFY_MODEMSTATE: if (n >= 1) ein(Art::ModemState); break;
        case SET_LINESTATE_MASK:
            if (n < 1) return;
            if (!antwort) lineMaske_ = d[1];
            ein(Art::LineStateMaske);
            break;
        case SET_MODEMSTATE_MASK:
            if (n < 1) return;
            if (!antwort) modemMaske_ = d[1];
            ein(Art::ModemStateMaske);
            break;
        case FLOWCONTROL_SUSPEND: e.art = Art::FlussHalt; ereignisse_.push_back(e); break;
        case FLOWCONTROL_RESUME: e.art = Art::FlussWeiter; ereignisse_.push_back(e); break;
        case PURGE_DATA: if (n >= 1) ein(Art::Leeren); break;
        default: break;
    }
}

void Rfc2217Codec::befehl(uint8_t nr, const std::vector<uint8_t>& daten, bool antwort) {
    std::vector<uint8_t> sb;
    sb.reserve(daten.size() + 1);
    sb.push_back(antwort ? uint8_t(nr + ANTWORT) : nr);
    sb.insert(sb.end(), daten.begin(), daten.end());
    telnet_.sendeSub(telnet::OPT_COMPORT, sb);  // verdoppelt 0xFF
}

void Rfc2217Codec::sendeSignatur(const std::string& text, bool a) {
    befehl(SIGNATURE, std::vector<uint8_t>(text.begin(), text.end()), a);
}
void Rfc2217Codec::sendeBaud(uint32_t b, bool a) {
    befehl(SET_BAUDRATE, {uint8_t(b >> 24), uint8_t(b >> 16), uint8_t(b >> 8), uint8_t(b)}, a);
}
void Rfc2217Codec::sendeDatenbits(uint8_t v, bool a) { befehl(SET_DATASIZE, {v}, a); }
void Rfc2217Codec::sendeParitaet(uint8_t v, bool a) { befehl(SET_PARITY, {v}, a); }
void Rfc2217Codec::sendeStoppbits(uint8_t v, bool a) { befehl(SET_STOPSIZE, {v}, a); }
void Rfc2217Codec::sendeSteuerung(uint8_t v, bool a) { befehl(SET_CONTROL, {v}, a); }
void Rfc2217Codec::sendeFlussHalt(bool halt, bool a) {
    befehl(halt ? FLOWCONTROL_SUSPEND : FLOWCONTROL_RESUME, {}, a);
}
void Rfc2217Codec::sendeLeeren(uint8_t v, bool a) { befehl(PURGE_DATA, {v}, a); }
void Rfc2217Codec::sendeLineStateMaske(uint8_t m, bool a) { befehl(SET_LINESTATE_MASK, {m}, a); }
void Rfc2217Codec::sendeModemStateMaske(uint8_t m, bool a) { befehl(SET_MODEMSTATE_MASK, {m}, a); }

bool Rfc2217Codec::sendeLineState(uint8_t wert, bool ungefiltert) {
    if (!ungefiltert && (wert & lineMaske_) == 0) return false;
    befehl(NOTIFY_LINESTATE, {wert}, true);
    return true;
}
bool Rfc2217Codec::sendeModemState(uint8_t wert, bool ungefiltert) {
    if (!ungefiltert && (wert & modemMaske_) == 0) return false;
    befehl(NOTIFY_MODEMSTATE, {wert}, true);
    return true;
}

}  // namespace serial

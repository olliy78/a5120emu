#include "core/serial/net/adresse.h"

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>   // inet_pton (braucht kein WSAStartup)
#else
#  include <arpa/inet.h>
#  include <sys/socket.h>
#endif

namespace k1520::serial::net {

namespace {

// Eigene ASCII-Prädikate statt <cctype>: unabhängig von der Locale, und Bytes
// ≥ 128 (UTF-8 im Feld) sind damit sicher KEIN Hostnamenzeichen.
bool istZiffer(char c) { return c >= '0' && c <= '9'; }
bool istBuchstabe(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool istLeer(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string trim(const std::string& s) {
    std::size_t a = 0, e = s.size();
    while (a < e && istLeer(s[a])) ++a;
    while (e > a && istLeer(s[e - 1])) --e;
    return s.substr(a, e - a);
}

bool istIPv4(const std::string& s) {
    in_addr a{};
    return inet_pton(AF_INET, s.c_str(), &a) == 1;
}

bool istIPv6(const std::string& s) {
    in6_addr a{};
    return inet_pton(AF_INET6, s.c_str(), &a) == 1;
}

} // namespace

bool hostnameGueltig(const std::string& nameIn) {
    std::string name = nameIn;
    if (!name.empty() && name.back() == '.') name.pop_back();   // absoluter Name
    if (name.empty() || name.size() > 253) return false;

    std::size_t von = 0;
    bool        letztesLabelNumerisch = false;
    while (true) {
        std::size_t bis = name.find('.', von);
        if (bis == std::string::npos) bis = name.size();
        const std::size_t len = bis - von;
        if (len < 1 || len > 63) return false;
        if (name[von] == '-' || name[bis - 1] == '-') return false;
        bool numerisch = true;
        for (std::size_t i = von; i < bis; ++i) {
            const char c = name[i];
            if (!(istZiffer(c) || istBuchstabe(c) || c == '-')) return false;
            if (!istZiffer(c)) numerisch = false;
        }
        letztesLabelNumerisch = numerisch;
        if (bis == name.size()) break;
        von = bis + 1;
    }
    return !letztesLabelNumerisch;
}

Adresse adresseZerlegen(const std::string& feld) {
    Adresse r;
    const std::string s = trim(feld);
    if (s.empty()) return r;

    if (istIPv4(s)) {
        r.art  = AdressArt::IPv4;
        r.host = s;
        return r;
    }

    // IPv6: optional in [...], optional mit %zone (inet_pton kennt die Zone nicht).
    std::string v6 = s;
    if (v6.size() >= 2 && v6.front() == '[' && v6.back() == ']')
        v6 = v6.substr(1, v6.size() - 2);
    std::string zone;
    const std::size_t pz = v6.find('%');
    if (pz != std::string::npos) {
        zone = v6.substr(pz + 1);
        v6.resize(pz);
        if (zone.empty()) return r;   // „fe80::1%" ist kein Bereich
    }
    if (istIPv6(v6)) {
        r.art  = AdressArt::IPv6;
        r.host = v6;
        r.zone = zone;
        return r;
    }

    if (hostnameGueltig(s)) {
        r.art  = AdressArt::Hostname;
        r.host = s;
    }
    return r;
}

} // namespace k1520::serial::net

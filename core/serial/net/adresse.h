/**
 * @file adresse.h
 * @brief Klassifikation des Host-Feldes einer seriellen Schnittstelle (Entwurf 19 §7.3).
 *
 * Rein textuell, ohne Netz und ohne Namensauflösung: entschieden wird an der Form.
 * Die Oberfläche zeigt das Ergebnis als kleines Etikett neben dem Feld; „ungültig"
 * sperrt den Verbinden-Knopf.
 *
 * @see doc/design/19_serielle_schnittstellen.md §7.3
 */
#pragma once

#include <string>

namespace k1520::serial::net {

enum class AdressArt {
    Ungueltig,
    IPv4,
    IPv6,
    Hostname,
};

/// Zerlegtes Host-Feld.  `host` ist bei IPv6 ohne `[…]` und ohne `%zone`, sonst
/// das (an den Rändern von Leerraum befreite) Feld selbst.
struct Adresse {
    AdressArt   art = AdressArt::Ungueltig;
    std::string host;
    std::string zone;   ///< nur IPv6 (`fe80::1%eth0` → "eth0"), sonst leer
};

/// Host-Feld klassifizieren und zerlegen.
/// Reihenfolge: IPv4 → (`[…]` abstreifen, `%zone` abtrennen) IPv6 → Hostname → ungültig.
Adresse adresseZerlegen(const std::string& feld);

/// Kurzform: nur die Art.
inline AdressArt adresseKlassifizieren(const std::string& feld) {
    return adresseZerlegen(feld).art;
}

/// Hostname nach RFC 1123: Labels 1–63 Zeichen `[A-Za-z0-9-]`, nicht mit `-` beginnend
/// oder endend, gesamt ≤ 253 (ein einzelner Schlusspunkt ist zulässig).
/// Zusätzlich gilt das letzte Label nicht als rein numerisch (RFC 1123 §2.1) —
/// `300.1.1.1` ist damit ungültig statt ein „Hostname", den kein Resolver kennt.
bool hostnameGueltig(const std::string& name);

} // namespace k1520::serial::net

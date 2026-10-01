/**
 * @file test_serial_adresse.cpp
 * @brief Unit-Tests der Host-Klassifikation (`core/serial/net/adresse.h`, Entwurf 19 §7.3).
 *
 * Rein textuell — kein Netz, keine Namensauflösung.
 */

#include <gtest/gtest.h>

#include <string>

#include "core/serial/net/adresse.h"

using namespace k1520::serial::net;

TEST(SerialAdresse, IPv4) {
    EXPECT_EQ(adresseKlassifizieren("127.0.0.1"), AdressArt::IPv4);
    EXPECT_EQ(adresseKlassifizieren("192.168.1.20"), AdressArt::IPv4);
    EXPECT_EQ(adresseKlassifizieren("  10.0.0.1 "), AdressArt::IPv4);  // Ränder werden getrimmt
}

TEST(SerialAdresse, IPv4ZuGrosseOktettIstKeinHostname) {
    // „300.1.1.1" sähe nach Labelregeln wie ein Hostname aus; das numerische letzte
    // Label (RFC 1123 §2.1) schließt es aus.
    EXPECT_EQ(adresseKlassifizieren("300.1.1.1"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("1.2.3"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("12345"), AdressArt::Ungueltig);
}

TEST(SerialAdresse, IPv6) {
    EXPECT_EQ(adresseKlassifizieren("::1"), AdressArt::IPv6);
    EXPECT_EQ(adresseKlassifizieren("2001:db8::8a2e:370:7334"), AdressArt::IPv6);
    EXPECT_EQ(adresseKlassifizieren("::ffff:192.0.2.1"), AdressArt::IPv6);
}

TEST(SerialAdresse, IPv6InKlammern) {
    const Adresse a = adresseZerlegen("[::1]");
    EXPECT_EQ(a.art, AdressArt::IPv6);
    EXPECT_EQ(a.host, "::1");
    EXPECT_TRUE(a.zone.empty());
}

TEST(SerialAdresse, IPv6MitZone) {
    Adresse a = adresseZerlegen("fe80::1%eth0");
    EXPECT_EQ(a.art, AdressArt::IPv6);
    EXPECT_EQ(a.host, "fe80::1");
    EXPECT_EQ(a.zone, "eth0");

    a = adresseZerlegen("[fe80::1%eth0]");
    EXPECT_EQ(a.art, AdressArt::IPv6);
    EXPECT_EQ(a.host, "fe80::1");
    EXPECT_EQ(a.zone, "eth0");

    EXPECT_EQ(adresseKlassifizieren("fe80::1%"), AdressArt::Ungueltig);   // leere Zone
}

TEST(SerialAdresse, IPv6Ungueltig) {
    EXPECT_EQ(adresseKlassifizieren("[::1"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("::1]"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("1::2::3"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("[1.2.3.4]"), AdressArt::Ungueltig);   // IPv4 nie in Klammern
}

TEST(SerialAdresse, Hostnamen) {
    EXPECT_EQ(adresseKlassifizieren("localhost"), AdressArt::Hostname);
    EXPECT_EQ(adresseKlassifizieren("ser2net.example.org"), AdressArt::Hostname);
    EXPECT_EQ(adresseKlassifizieren("a-b.c1.de"), AdressArt::Hostname);
    EXPECT_EQ(adresseKlassifizieren("host.example.org."), AdressArt::Hostname);   // absoluter Name
    EXPECT_EQ(adresseKlassifizieren("3com.example"), AdressArt::Hostname);   // Ziffer am Anfang erlaubt (RFC 1123)
}

TEST(SerialAdresse, HostnamenGrenzen) {
    const std::string l63(63, 'a'), l64(64, 'a');
    EXPECT_EQ(adresseKlassifizieren(l63 + ".de"), AdressArt::Hostname);
    EXPECT_EQ(adresseKlassifizieren(l64 + ".de"), AdressArt::Ungueltig);

    // gesamt ≤ 253: 4 × (62 + Punkt) = 252 + 1 Zeichen „a" = 253 → gut, mit einem mehr → zu lang
    std::string n;
    for (int i = 0; i < 4; ++i) n += std::string(62, 'b') + ".";
    EXPECT_EQ(adresseKlassifizieren(n + "a"), AdressArt::Hostname);
    EXPECT_EQ(adresseKlassifizieren(n + "ab"), AdressArt::Ungueltig);
}

TEST(SerialAdresse, UngueltigeHostnamen) {
    EXPECT_EQ(adresseKlassifizieren(""), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("   "), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("-host"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("host-"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("a..b"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren(".a"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("ho_st"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("ho st"), AdressArt::Ungueltig);
    EXPECT_EQ(adresseKlassifizieren("host:5000"), AdressArt::Ungueltig);   // Port gehört in ein eigenes Feld
    EXPECT_EQ(adresseKlassifizieren("h\xC3\xA4st"), AdressArt::Ungueltig);  // UTF-8-Umlaut
}

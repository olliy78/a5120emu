/**
 * @file hub.h
 * @brief `SerialHub` — die seriellen Schnittstellen einer Maschine nach außen
 *        (Entwurf 19 §5, §7).
 *
 * @details
 * Einer je Maschine.  Er besitzt je registriertem `SerialAnschluss` einen `Wandler`
 * und betreibt dahinter den Transport der eingestellten Betriebsart (Telnet / RFC 2217 /
 * Datei) in einem **eigenen I/O-Faden** (Leitsatz 5: kein Netz im Emulationsfaden).
 *
 * **Einbindung in eine Maschine (AP-S5):**
 * @code
 *   SerialHub hub_;                               // Member der Maschine
 *   hub_.registriere(k8025_.anschlussV24());      // im Konstruktor, VOR dem ersten Lauf
 *   …
 *   hub_.takt(total_cycles_);                     // im Lauf, dort wo service() läuft
 * @endcode
 * Registrieren nur beim Aufbau (nicht nebenläufig zu `takt`/`status`).  Der Anschluss
 * muss länger leben als der Hub.
 *
 * **Faden-Modell:**
 *   - Emulationsfaden: nur `takt()` → `Wandler::takt` (Wandler-Mutex, kein Hub-Mutex).
 *   - GUI-Faden (oder jeder andere): `konfigurieren`, `start`, `startAuto`, `stop`,
 *     `status`, `info`, `konfig` — unter dem Hub-Mutex, kurz, nie blockierend auf Netz.
 *   - I/O-Faden: läuft nur, solange mindestens eine Schnittstelle aktiv ist; wartet mit
 *     `poll`/`WSAPoll` auf alle Sockets plus ein Weck-Socketpaar, bearbeitet unter dem
 *     Hub-Mutex (nicht blockierende Sockets).  Sperrreihenfolge immer Hub → Wandler.
 *   - Verbinder-Faden (Client): je Verbindungsversuch ein kurzlebiger, abgekoppelter
 *     Faden für `getaddrinfo` + `connect` (beide können blockieren — Namensauflösung
 *     sekundenlang), damit weder der I/O-Faden noch „Trennen" darauf warten.
 *
 * Zustände (§7.1): Server AUS → LAUSCHT ⇄ VERBUNDEN; Client AUS → VERBINDET ⇄ VERBUNDEN
 * (Dauerversuch alle 1000 ms Uhrzeit, bis „Trennen"); Datei AUS → VERBUNDEN (= Datei
 * offen).  FEHLER nur für das, was ein neuer Versuch nicht behebt.
 *
 * @see doc/design/19_serielle_schnittstellen.md
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/serial/anschluss.h"
#include "core/serial/net/socket.h"
#include "core/serial/wandler.h"

namespace k1520::serial {

// Zahlenwerte = C-ABI (§8, K1520SerBetriebsart/K1520SerRolle/K1520SerZustand).
enum class Betriebsart : int { Telnet = 0, Rfc2217 = 1, Datei = 2 };
enum class Rolle : int { Server = 0, Client = 1 };
enum class Zustand : int { Aus = 0, Verbindet = 1, Lauscht = 2, Verbunden = 3, Fehler = 4 };

/// Aktiv im Sinne der Sperrregel (§4): Server lauscht/verbunden, Client versucht/verbunden,
/// Datei offen.
inline bool istAktiv(Zustand z) {
    return z == Zustand::Verbindet || z == Zustand::Lauscht || z == Zustand::Verbunden;
}

/// Feste Angaben einer Schnittstelle (↔ `K1520SerInfo`).
struct SerialInfo {
    std::string name, stecker;
    bool v24 = false;
    std::vector<std::string> taktquellen;   ///< leer = fest
};

/// Einstellungen (↔ `K1520SerKonfig`, §4).
struct SerialKonfig {
    Betriebsart betriebsart = Betriebsart::Telnet;
    Rolle       rolle       = Rolle::Server;
    std::string host        = "127.0.0.1";
    /// 1–65535.  0 = „vom System gewählt" — nur für Tests (nie feste Ports); ohne
    /// Portsuche und ohne Wiederaufnahme.
    uint16_t    port        = 5000;
    bool        loop = false, rtscts_bruecke = false, xonxoff = false;
    int         taktquelle  = 0;
    std::string datei;   ///< UTF-8

    /// Die im Betrieb gesperrten Felder (§4) gleich?
    bool gesperrteGleich(const SerialKonfig& o) const {
        return betriebsart == o.betriebsart && rolle == o.rolle && host == o.host &&
               port == o.port && datei == o.datei;
    }
};

/// Zustand (↔ `K1520SerStatus`).
struct SerialStatus {
    Zustand     zustand = Zustand::Aus;
    uint16_t    port_aktiv = 0;          ///< Server: tatsächlicher Port (§7.2)
    std::string gegenstelle;             ///< "192.168.1.5:40122" / "[::1]:5000"
    std::string meldung;                 ///< Fehler, letzter Grund, Hinweis (§7.1/§7.4a)
    uint32_t    baud_nenn = 0;
    uint8_t     daten = 0, paritaet = 0, stopp_halbe = 0;
    bool        format_gueltig = false;
    uint32_t    baud_gegenseite = 0;     ///< RFC 2217, 0 = unbekannt
    bool        baud_abweichend = false;
    // AP-S11: Format und Leitungen der Gegenseite, Bedeutung je Rolle s. `GegenseiteStand`.
    uint8_t     daten_gegenseite = 0, paritaet_gegenseite = 0, stopp_halbe_gegenseite = 0;
    bool        format_gegenseite_bekannt = false, format_abweichend = false;
    uint8_t     leitungen_gegenseite = 0, leitungen_gegenseite_bekannt = 0;  ///< `gegenleitung::*`
    bool        rts = false, cts = false, dtr = false, dsr = false, dcd = false;
    uint64_t    bytes_gesendet = 0, bytes_empfangen = 0;
    uint32_t    puffer_senden = 0, puffer_empfangen = 0;
    uint16_t    port_vorschlag = 0;      ///< §7.4a: freier Port, wenn der eingestellte belegt war
    Rolle       rolle = Rolle::Server;
    Betriebsart betriebsart = Betriebsart::Telnet;
    uint32_t    versuche = 0;            ///< Client: Versuche seit dem letzten Verbinden
};

class SerialHub {
public:
    explicit SerialHub(uint64_t phiNenn = PHI_NENN);
    ~SerialHub();
    SerialHub(const SerialHub&) = delete;
    SerialHub& operator=(const SerialHub&) = delete;

    // ── Aufbau ───────────────────────────────────────────────────────────────
    /// Anschluss aufnehmen; Rückgabe = Index.  Nur beim Aufbau der Maschine.
    int registriere(SerialAnschluss& anschluss);
    int anzahl() const { return static_cast<int>(eintraege_.size()); }
    Wandler& wandler(int i);   ///< Index muss gültig sein

    // ── Emulationsfaden ─────────────────────────────────────────────────────
    /// @return frühester nächster Blick aller Wandler (UINT64_MAX ohne Anschluss) —
    ///         bis dahin darf die Maschine den Aufruf auslassen.  Springt ihr
    ///         Taktzähler zurück, ruft sie sofort wieder (die Wandler fangen neu an).
    uint64_t takt(uint64_t zyklus);
    /// /RESET der Maschine: `Wandler::gastZurueckgesetzt` für alle (AP-S5).
    void gastZurueckgesetzt();

    // ── Bedienung (jeder Faden) ─────────────────────────────────────────────
    SerialInfo info(int i) const;
    SerialKonfig konfig(int i) const;
    /// Übernehmen.  Während aktiv: geänderte gesperrte Felder → false, NICHTS übernommen.
    /// Loop setzen beendet eine aktive Verbindung (§6.5).  Ungültiger Index/Port → false.
    bool konfigurieren(int i, const SerialKonfig& k);
    /// Start von Hand: Server mit Portsuche (§7.2), Client in den Dauerversuch, Datei
    /// überschreibend.  false bei Loop, Fehler (Zustand FEHLER + Meldung).  Schon aktiv → true.
    bool start(int i);
    /// Wiederaufnahme (§7.4a): Server NUR auf dem eingestellten Port — belegt → false,
    /// Zustand AUS, `port_vorschlag` + Meldung; Datei anhängend.  Loop → false.
    bool startAuto(int i);
    /// Beenden/Trennen — sofort; auch die Versuche eines Clients enden.
    void stop(int i);
    void stopAlle();
    SerialStatus status(int i) const;

    /// Abstand der Client-Versuche (Vorgabe 1000 ms, §7.1).  Nur für Tests verstellen.
    void setzeVersuchAbstand(std::chrono::milliseconds ms);

    using Uhr = std::chrono::steady_clock;
    struct Eintrag;   // intern (hub.cpp)

private:
    bool starten(int i, bool automatisch);
    void stopIntern(Eintrag& e);
    void sicherstellenFaden();   // unter m_
    void lauf();                 // I/O-Faden
    void bearbeite(Eintrag& e, Uhr::time_point jetzt);
    void netzIo(Eintrag& e);
    void anbinden(Eintrag& e, net::Socket&& sock, const std::string& gegenstelle);
    void trennen(Eintrag& e, const std::string& grund);

    const uint64_t phiNenn_;
    std::vector<std::unique_ptr<Eintrag>> eintraege_;
    std::shared_ptr<net::Wecker> wecker_;
    mutable std::mutex m_;
    std::thread faden_;
    bool laeuft_ = false;     // unter m_
    bool beenden_ = false;    // unter m_
    std::chrono::milliseconds versuchAbstand_{1000};
};

}  // namespace k1520::serial

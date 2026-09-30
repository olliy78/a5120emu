/**
 * @file telnet_codec.h
 * @brief Telnet-Zustandsautomat (RFC 854/856/858/857) — rein, ohne Socket.
 *
 * @details
 * Doc: doc/design/19_serielle_schnittstellen.md §7.4.  Der Codec kennt weder
 * Socket noch Faden noch Maschine: Der Transport füttert ihn mit empfangenen
 * Bytes (beliebig zerrissen, auch byteweise) und holt danach
 *   - `nimmNutzdaten()`  die dekodierten Nutzbytes (für den Wandler/SIO),
 *   - `nimmAusgabe()`    alle Bytes, die auf den Draht gehören (Verhandlung,
 *                        Antworten, kodierte Nutzdaten — in richtiger Reihenfolge),
 *   - `nimmSub()`        empfangene Unterverhandlungen (für Option 44 → Rfc2217Codec).
 *
 * Verhandelt wird nur BINARY(0)/ECHO(1)/SGA(3) sowie Optionen, die eine
 * Schicht darüber ausdrücklich zulässt (`erlaube`, Option 44).  Alles andere
 * wird mit DONT/WONT abgelehnt.  Die Verhandlung folgt der Q-Methode
 * (RFC 1143): auf eine Bestätigung folgt nie dieselbe Anfrage noch einmal,
 * damit entsteht kein Verhandlungs-Pingpong — auch nicht gegen eine
 * Gegenseite, die jede Antwort mit der Gegenfrage beantwortet.
 *
 * Gegenüber rohem TCP (kein IAC im Strom) ist der Codec transparent — bis auf
 * 0xFF (wird verdoppelt) und CR NUL (siehe unten).
 *
 * @note CR-Behandlung: Solange BINARY in der jeweiligen Richtung NICHT aktiv
 *       ist, wird beim Senden jedes CR zu CR NUL und ein empfangenes CR NUL
 *       wieder zu CR.  Bewusst ohne Vorgriff (CR LF → CR NUL LF): ein
 *       zurückgehaltenes CR würde ein einzelnes Prompt-CR verzögern.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace serial {

/// Rolle im Telnet-Gespräch.  Sie legt fest, was am Anfang angeboten wird und
/// welche Optionen bereitwillig angenommen werden.
enum class TelnetRolle { Client, Server };

/// Telnet-Bytewerte, die Tests und Schichten darüber gebrauchen.
namespace telnet {
constexpr uint8_t IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240;
constexpr uint8_t OPT_BINARY = 0, OPT_ECHO = 1, OPT_SGA = 3, OPT_COMPORT = 44;
}  // namespace telnet

/// Eine empfangene Unterverhandlung `IAC SB <option> <daten…> IAC SE`
/// (`IAC IAC` in den Daten ist bereits zu 0xFF aufgelöst).
struct TelnetSub {
    uint8_t option = 0;
    std::vector<uint8_t> daten;
};

class TelnetCodec {
public:
    explicit TelnetCodec(TelnetRolle rolle);

    TelnetRolle rolle() const { return rolle_; }

    /// Eröffnet das Gespräch.  Server: `WILL ECHO, WILL SGA, WILL BINARY,
    /// DO BINARY` (Gast macht das Echo, Zeichenbetrieb, §7.4); Client: nichts —
    /// er wartet auf den Server.  Mehrfachaufruf schadet nicht.
    void start();

    // --- Empfang ----------------------------------------------------------
    /// Empfangene Rohbytes verarbeiten.  Der Zustand bleibt zwischen Aufrufen
    /// erhalten, die Stücke dürfen an jeder Stelle enden.
    void eingabe(const uint8_t* daten, size_t n);
    void eingabe(const std::vector<uint8_t>& d) { eingabe(d.data(), d.size()); }

    /// Dekodierte Nutzdaten seit dem letzten Abholen (leert den Puffer).
    std::vector<uint8_t> nimmNutzdaten();
    /// Empfangene Unterverhandlungen seit dem letzten Abholen (leert die Liste).
    std::vector<TelnetSub> nimmSub();

    // --- Senden -----------------------------------------------------------
    /// Nutzdaten zum Senden vormerken (0xFF → IAC IAC; CR → CR NUL ohne lokales BINARY).
    void sende(const uint8_t* daten, size_t n);
    void sende(const std::vector<uint8_t>& d) { sende(d.data(), d.size()); }
    /// Unterverhandlung `IAC SB option daten IAC SE` vormerken (0xFF in den Daten wird verdoppelt).
    void sendeSub(uint8_t option, const std::vector<uint8_t>& daten);

    /// Alles, was auf den Draht gehört, in Reihenfolge (leert den Puffer).
    std::vector<uint8_t> nimmAusgabe();
    bool hatAusgabe() const { return !aus_.empty(); }

    // --- Optionen ---------------------------------------------------------
    /// Zusätzliche Option zulassen: `lokal` = wir dürfen sie einschalten (WILL
    /// auf DO), `entfernt` = die Gegenseite darf (DO auf WILL).
    void erlaube(uint8_t option, bool lokal, bool entfernt);
    /// Wir schalten die Option in unserer Senderichtung ein (sendet WILL, falls nötig).
    void bieteAn(uint8_t option);
    /// Wir bitten die Gegenseite, sie einzuschalten (sendet DO, falls nötig).
    void verlange(uint8_t option);

    /// Läuft die Option in unserer Senderichtung (WILL bestätigt)?
    bool lokalAktiv(uint8_t option) const { return opt_[option].us.s == Q::Ja; }
    /// Läuft sie in der Senderichtung der Gegenseite (DO bestätigt)?
    bool entferntAktiv(uint8_t option) const { return opt_[option].him.s == Q::Ja; }

    /// Obergrenze einer Unterverhandlung; längere werden verworfen (Schutz vor
    /// einer Gegenseite, die nie `IAC SE` schickt).
    static constexpr size_t SUB_MAX = 1024;

private:
    // Q-Methode nach RFC 1143, je Richtung ("us" = WILL/WONT, "him" = DO/DONT).
    struct Q {
        enum S : uint8_t { Nein, Ja, WillNein, WillJa } s = Nein;
        bool gegen = false;  // "OPPOSITE": ein Gegenwunsch wartet auf die Antwort
    };
    struct Opt {
        Q us, him;
        bool lokalOk = false, entferntOk = false;
    };
    enum class Zst { Daten, Iac, Verb, SbOpt, SbDaten, SbIac };

    void dreiByte(uint8_t verb, uint8_t opt) {
        aus_.push_back(telnet::IAC);
        aus_.push_back(verb);
        aus_.push_back(opt);
    }
    // Eigener Wunsch (ein/aus) auf einer Richtung.
    void wunsch(Q& q, bool ein, uint8_t jaVerb, uint8_t neinVerb, uint8_t opt);
    // Antwort der Gegenseite (Ja-Verb = WILL bzw. DO, Nein-Verb = WONT bzw. DONT).
    void antwort(Q& q, bool ja, bool erlaubt, uint8_t jaVerb, uint8_t neinVerb, uint8_t opt);
    void empfangVerb(uint8_t verb, uint8_t opt);
    void sbAbschluss();
    void nutzbyte(uint8_t b);

    TelnetRolle rolle_;
    bool gestartet_ = false;
    Opt opt_[256];
    Zst zst_ = Zst::Daten;
    uint8_t verb_ = 0;  // zuletzt gesehenes WILL/WONT/DO/DONT
    uint8_t sbOpt_ = 0;
    bool sbZuLang_ = false;
    std::vector<uint8_t> sbBuf_;
    bool crGesehen_ = false;  // letztes Nutzbyte war CR (für CR NUL)
    std::vector<uint8_t> ein_, aus_;
    std::vector<TelnetSub> sub_;
};

}  // namespace serial

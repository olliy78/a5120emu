/**
 * @file rfc2217_codec.h
 * @brief RFC 2217 (Telnet COM-PORT-Option, Option 44) auf dem TelnetCodec — rein, ohne Socket.
 *
 * @details
 * Doc: doc/design/19_serielle_schnittstellen.md §7.5, §6.4.  Der Codec hat
 * dieselbe Schnittstelle zum Transport wie der TelnetCodec (`eingabe`,
 * `nimmNutzdaten`, `sende`, `nimmAusgabe`) und zusätzlich:
 *   - `sendeXxx(...)`     kodiert einen COM-PORT-Befehl,
 *   - `nimmEreignisse()`  liefert die empfangenen Befehle dekodiert.
 *
 * **Er entscheidet nichts** (Leitsatz 4: der Gast ist maßgeblich).  Verlangt
 * die Gegenseite „Baud 9600", kommt ein Ereignis `Baud{wert=9600, antwort=false}`;
 * der Aufrufer (Wandler) antwortet mit dem *Gastwert*:
 * `sendeBaud(gastBaud, alsAntwort=true)`.  Im Client kommt die Antwort des
 * Servers als Ereignis mit `antwort=true` zurück — der Vergleich mit dem eigenen
 * Wert (Hinweis „Gegenseite: 1200 Bd") ist Sache des Aufrufers.
 *
 * Antworten und Benachrichtigungen des Servers tragen Befehl + 100 (`antwort`
 * im Ereignis bzw. `alsAntwort` beim Senden).  NOTIFY-LINESTATE/-MODEMSTATE
 * gehen immer als 106/107 hinaus.
 *
 * Anfragen ohne Wert (Abfrage) sind 0: `sendeBaud(0, false)` fragt die Baudrate
 * ab, `sendeSignatur("", false)` die Signatur usw.
 *
 * Die COM-PORT-Befehle gehen sofort nach dem Anstoß hinaus, ohne auf das
 * Verhandlungsende zu warten (auch ser2net/pyserial verhandeln und senden
 * ohne Sperre).  Empfangen wird unabhängig vom Verhandlungszustand.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/serial/telnet_codec.h"

namespace serial {

/// Befehlsnummern der COM-PORT-Option (Client → Server; Antwort = +100).
namespace rfc2217 {
enum Befehl : uint8_t {
    SIGNATURE = 0, SET_BAUDRATE = 1, SET_DATASIZE = 2, SET_PARITY = 3, SET_STOPSIZE = 4,
    SET_CONTROL = 5, NOTIFY_LINESTATE = 6, NOTIFY_MODEMSTATE = 7,
    FLOWCONTROL_SUSPEND = 8, FLOWCONTROL_RESUME = 9,
    SET_LINESTATE_MASK = 10, SET_MODEMSTATE_MASK = 11, PURGE_DATA = 12,
    ANTWORT = 100
};

/// Werte von SET-CONTROL.
enum Steuerwert : uint8_t {
    FLUSS_ABFRAGE = 0, FLUSS_KEINE = 1, FLUSS_XONXOFF = 2, FLUSS_HARDWARE = 3,
    BREAK_ABFRAGE = 4, BREAK_EIN = 5, BREAK_AUS = 6,
    DTR_ABFRAGE = 7, DTR_EIN = 8, DTR_AUS = 9,
    RTS_ABFRAGE = 10, RTS_EIN = 11, RTS_AUS = 12,
    EINFLUSS_ABFRAGE = 13, EINFLUSS_KEINE = 14, EINFLUSS_XONXOFF = 15, EINFLUSS_HARDWARE = 16,
    FLUSS_DCD = 17, FLUSS_DTR = 18, FLUSS_DSR = 19
};

/// Bits des MODEMSTATE-Bytes (obere: Zustand, untere: „hat sich geändert").
enum Modem : uint8_t {
    MS_DELTA_CTS = 0x01, MS_DELTA_DSR = 0x02, MS_TERI = 0x04, MS_DELTA_CD = 0x08,
    MS_CTS = 0x10, MS_DSR = 0x20, MS_RI = 0x40, MS_CD = 0x80
};

/// PURGE-DATA-Werte.
enum Purge : uint8_t { PURGE_EMPFANG = 1, PURGE_SENDEN = 2, PURGE_BEIDE = 3 };

// PARITY-Werte des Netzes (1 N, 2 O, 3 E, 4 Mark, 5 Space) ↔ SerialFormat (0 N, 1 O, 2 E).
inline uint8_t paritaetNetz(uint8_t sio) { return sio == 1 ? 2 : sio == 2 ? 3 : 1; }
/// Liefert 0/1/2 bzw. 255 für Mark/Space/ungültig.
inline uint8_t paritaetSio(uint8_t netz) { return netz == 1 ? 0 : netz == 2 ? 1 : netz == 3 ? 2 : 255; }
// STOPSIZE-Werte des Netzes (1 = 1, 2 = 2, 3 = 1,5) ↔ halbe Stoppbits (2, 4, 3) wie in SerialFormat.
inline uint8_t stoppNetz(uint8_t halbe) { return halbe == 4 ? 2 : halbe == 3 ? 3 : 1; }
inline uint8_t stoppHalbe(uint8_t netz) { return netz == 2 ? 4 : netz == 3 ? 3 : netz == 1 ? 2 : 0; }
}  // namespace rfc2217

/// Ein empfangener COM-PORT-Befehl, dekodiert.
struct Rfc2217Ereignis {
    enum class Art : uint8_t {
        Signatur,         ///< `text` (leer = Abfrage)
        Baud,             ///< `wert` Baud (0 = Abfrage)
        Datenbits,        ///< `wert` 5..8 (0 = Abfrage)
        Paritaet,         ///< `wert` Netzwert (0 = Abfrage), s. rfc2217::paritaetSio
        Stoppbits,        ///< `wert` Netzwert (0 = Abfrage), s. rfc2217::stoppHalbe
        Steuerung,        ///< `wert` = rfc2217::Steuerwert (Fluss, Break, DTR, RTS)
        LineState,        ///< NOTIFY-LINESTATE, `wert` = Byte
        ModemState,       ///< NOTIFY-MODEMSTATE, `wert` = Byte (rfc2217::Modem)
        LineStateMaske,   ///< SET-LINESTATE-MASK, `wert` = Maske (bereits im Codec gespeichert)
        ModemStateMaske,  ///< SET-MODEMSTATE-MASK, `wert` = Maske (bereits im Codec gespeichert)
        FlussHalt,        ///< FLOWCONTROL-SUSPEND: nicht mehr senden
        FlussWeiter,      ///< FLOWCONTROL-RESUME
        Leeren,           ///< PURGE-DATA, `wert` = rfc2217::Purge
    } art = Art::Signatur;
    bool antwort = false;  ///< Befehl trug +100 (Antwort bzw. Benachrichtigung des Servers)
    uint32_t wert = 0;
    std::string text;
};

class Rfc2217Codec {
public:
    explicit Rfc2217Codec(TelnetRolle rolle);
    Rfc2217Codec(const Rfc2217Codec&) = delete;
    Rfc2217Codec& operator=(const Rfc2217Codec&) = delete;

    TelnetRolle rolle() const { return telnet_.rolle(); }
    /// Der darunterliegende Telnet-Automat (nur lesend, z. B. für BINARY-Abfragen).
    const TelnetCodec& telnet() const { return telnet_; }

    /// Gespräch eröffnen: Telnet-Anfangsverhandlung, dazu Client `WILL 44`,
    /// Server `DO 44`.
    void start();
    /// Hat die Verhandlung Option 44 bestätigt (Client: WILL 44 von uns bestätigt, Server: WILL 44 des Clients)?
    bool comPortAktiv() const;

    // --- wie TelnetCodec ---------------------------------------------------
    void eingabe(const uint8_t* daten, size_t n);
    void eingabe(const std::vector<uint8_t>& d) { eingabe(d.data(), d.size()); }
    std::vector<uint8_t> nimmNutzdaten() { return telnet_.nimmNutzdaten(); }
    void sende(const uint8_t* daten, size_t n) { telnet_.sende(daten, n); }
    void sende(const std::vector<uint8_t>& d) { telnet_.sende(d); }
    std::vector<uint8_t> nimmAusgabe() { return telnet_.nimmAusgabe(); }
    bool hatAusgabe() const { return telnet_.hatAusgabe(); }

    /// Empfangene Befehle seit dem letzten Abholen (leert die Liste).
    std::vector<Rfc2217Ereignis> nimmEreignisse();

    // --- Befehle senden ----------------------------------------------------
    // `alsAntwort` = Befehl + 100 (Server antwortet auf eine Anfrage des Clients).
    void sendeSignatur(const std::string& text, bool alsAntwort = false);
    void sendeBaud(uint32_t baud, bool alsAntwort = false);       // 4 Byte, Netzreihenfolge
    void sendeDatenbits(uint8_t bits, bool alsAntwort = false);
    void sendeParitaet(uint8_t netzwert, bool alsAntwort = false);
    void sendeStoppbits(uint8_t netzwert, bool alsAntwort = false);
    void sendeSteuerung(uint8_t steuerwert, bool alsAntwort = false);
    void sendeFlussHalt(bool halt, bool alsAntwort = false);      // SUSPEND / RESUME
    void sendeLeeren(uint8_t purge, bool alsAntwort = false);
    /// Client → Server: SET-LINESTATE-MASK / SET-MODEMSTATE-MASK.  Die Antwort
    /// des Servers (`alsAntwort`) ändert die Masken dieses Codecs nicht.
    void sendeLineStateMaske(uint8_t maske, bool alsAntwort = false);
    void sendeModemStateMaske(uint8_t maske, bool alsAntwort = false);

    /// Server → Client: NOTIFY-LINESTATE/-MODEMSTATE (106/107).  Gesendet wird
    /// nur, wenn `wert & maske != 0` (Maske = die zuletzt vom Client per 10/11
    /// gesetzte; Vorgabe Line 0x00, Modem 0xFF wie bei pyserial); mit
    /// `ungefiltert` immer (z. B. für den Anfangszustand).  Rückgabe: gesendet?
    bool sendeLineState(uint8_t wert, bool ungefiltert = false);
    bool sendeModemState(uint8_t wert, bool ungefiltert = false);

    /// Vom Gegenüber gesetzte Masken (Vorgabe Line 0x00, Modem 0xFF).
    uint8_t lineStateMaske() const { return lineMaske_; }
    uint8_t modemStateMaske() const { return modemMaske_; }

private:
    void befehl(uint8_t nr, const std::vector<uint8_t>& daten, bool antwort);
    void verarbeite(const TelnetSub& sub);

    TelnetCodec telnet_;
    std::vector<Rfc2217Ereignis> ereignisse_;
    uint8_t lineMaske_ = 0x00;
    uint8_t modemMaske_ = 0xFF;
};

}  // namespace serial

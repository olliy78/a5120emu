/**
 * @file transport.h
 * @brief Transporte hinter dem Wandler: Telnet, RFC 2217 (Netz) und Datei —
 *        Entwurf 19 §6.4, §6.6, §7.4, §7.5.
 *
 * @details
 * Ein Netztransport sitzt zwischen Socket und `Wandler` und kennt keinen Socket: der
 * I/O-Faden des `SerialHub` füttert ihn mit empfangenen Rohbytes (`eingabe`), holt die
 * Nutzdaten für den Empfangspuffer (`nimmNutzdaten`), gibt ihm Bytes aus dem Sendepuffer
 * (`sende`) und schreibt, was er in `nimmAusgabe` liefert.  `abgleich` bearbeitet die
 * Protokollereignisse und meldet Leitungs-/Formatänderungen des Gastes.  So sind die
 * Transporte ohne Netz testbar (Attrappe = ein Codec der Gegenrolle).
 *
 * Alle Methoden laufen im I/O-Faden (bzw. im Test); der Wandler wird nur über seine
 * threadsicheren Methoden angefasst.
 *
 * RFC 2217 — Leitungen (§6.4):
 *   - Rolle Client (wir = DEE am fernen COM-Port): Gast-RTS/DTR → SET-CONTROL;
 *     NOTIFY-MODEMSTATE CTS/DSR/CD → Eingänge.  Gerade Belegung.
 *   - Rolle Server (wir spielen den COM-Port): **Nullmodem-Kreuzung** — Client-RTS →
 *     unser CTS, Client-DTR → unser DSR und DCD; unser RTS → NOTIFY CTS, unser DTR →
 *     DSR + CD.  Solange der Client nichts gesetzt hat, gelten seine Leitungen als
 *     aktiv (wie bei Telnet — ein Client ohne SET-CONTROL soll den Gast nicht blockieren).
 *   - Eine Schnittstelle OHNE Steuerleitungen (IFSS) meldet als Server CTS/DSR/CD fest
 *     aktiv und sendet als Client kein RTS/DTR (sonst fiele RTS am fernen Gerät ab).
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/serial/rfc2217_codec.h"
#include "core/serial/telnet_codec.h"
#include "core/serial/wandler.h"

namespace k1520::serial {

/// Signatur für RFC 2217 SIGNATURE (§7.5): "k1520emu <Version>" — dieselbe Quelle wie
/// `k1520_version()` (`core/version.h`).
std::string signaturText();

/// Bits der Leitungen der Gegenseite (↔ `K1520_SER_L_*` der C-ABI).
namespace gegenleitung {
constexpr uint8_t RTS = 0x01, DTR = 0x02, CTS = 0x04, DSR = 0x08, DCD = 0x10, RI = 0x20;
}

/// Was RFC 2217 von der Gegenseite verrät (AP-S11).  Je Rolle:
///   - Server (Gegenseite = Client): Format = zuletzt GEWÜNSCHTE Werte (SET-DATASIZE/-PARITY/
///     -STOPSIZE); Leitungen = RTS, DTR des Clients (SET-CONTROL), bekannt ab dem ersten
///     SET-CONTROL der jeweiligen Leitung.  CTS/DSR/DCD/RI des Clients sind nicht zu sehen.
///   - Client (Gegenseite = Server): Format = Antwortwerte des Servers auf unsere SET-*;
///     Leitungen = CTS, DSR, DCD, RI aus NOTIFY-MODEMSTATE (ab der ersten Meldung).
///     RTS/DTR des Servers sind nicht zu sehen.
/// Telnet und Datei: nichts bekannt.  Ein Wert in `leitungen` zählt nur, wenn sein Bit in
/// `leitungenBekannt` steht.
struct GegenseiteStand {
    uint8_t daten = 0;          ///< 5..8; 0 = unbekannt
    uint8_t paritaet = 0;       ///< 0 N, 1 O, 2 E, 3 Mark, 4 Space (gültig nur mit `paritaetBekannt`)
    uint8_t stoppHalbe = 0;     ///< halbe Stoppbits (2, 3, 4); 0 = unbekannt
    bool paritaetBekannt = false;
    bool formatBekannt() const { return daten != 0 && paritaetBekannt && stoppHalbe != 0; }
    bool formatAbweichend = false;   ///< Format bekannt und ≠ Gastformat (ohne Baud)
    uint8_t leitungen = 0;
    uint8_t leitungenBekannt = 0;
};

class NetzTransport {
public:
    virtual ~NetzTransport() = default;

    /// Gespräch eröffnen und den Wandler anbinden.
    virtual void start(Wandler& w) = 0;
    virtual void eingabe(const uint8_t* daten, size_t n) = 0;
    virtual std::vector<uint8_t> nimmNutzdaten() = 0;
    virtual void sende(const uint8_t* daten, size_t n) = 0;
    virtual std::vector<uint8_t> nimmAusgabe() = 0;
    /// Protokollereignisse bearbeiten, Leitungs-/Formatänderungen des Gastes melden.
    virtual void abgleich(Wandler& /*w*/) {}

    /// Die Gegenseite hat FLOWCONTROL-SUSPEND geschickt: nichts mehr senden.
    virtual bool sendenAngehalten() const { return false; }
    /// Baud der Gegenseite (RFC 2217), 0 = unbekannt; und ob sie vom Gast abweicht.
    virtual uint32_t baudGegenseite() const { return 0; }
    virtual bool baudAbweichend() const { return false; }
    /// Format und Leitungen der Gegenseite (RFC 2217, AP-S11).
    virtual GegenseiteStand gegenseite() const { return {}; }
};

/// Nur Nutzdaten (§7.4).  Eingänge = „verbunden".
class TelnetTransport : public NetzTransport {
public:
    explicit TelnetTransport(::serial::TelnetRolle rolle) : codec_(rolle) {}
    void start(Wandler& w) override;
    void eingabe(const uint8_t* d, size_t n) override { codec_.eingabe(d, n); }
    std::vector<uint8_t> nimmNutzdaten() override { return codec_.nimmNutzdaten(); }
    void sende(const uint8_t* d, size_t n) override { codec_.sende(d, n); }
    std::vector<uint8_t> nimmAusgabe() override { return codec_.nimmAusgabe(); }

private:
    ::serial::TelnetCodec codec_;
};

/// Telnet + COM-PORT-OPTION (§7.5).
class Rfc2217Transport : public NetzTransport {
public:
    Rfc2217Transport(::serial::TelnetRolle rolle, std::string signatur = signaturText());
    void start(Wandler& w) override;
    void eingabe(const uint8_t* d, size_t n) override { codec_.eingabe(d, n); }
    std::vector<uint8_t> nimmNutzdaten() override { return codec_.nimmNutzdaten(); }
    void sende(const uint8_t* d, size_t n) override { codec_.sende(d, n); }
    std::vector<uint8_t> nimmAusgabe() override { return codec_.nimmAusgabe(); }
    void abgleich(Wandler& w) override;

    bool sendenAngehalten() const override { return angehalten_; }
    uint32_t baudGegenseite() const override { return baudGegenseite_; }
    bool baudAbweichend() const override { return abweichend_; }
    GegenseiteStand gegenseite() const override;

private:
    void formatVergleichen(const SerialFormat& gast);
    bool client() const { return codec_.rolle() == ::serial::TelnetRolle::Client; }
    void sendeFormat(const SerialFormat& f);                   // Client
    uint8_t modemByte(const WandlerSicht& s, bool v24) const;  // Server (gekreuzt)
    uint8_t flussWert(bool xonxoff) const;

    ::serial::Rfc2217Codec codec_;
    std::string signatur_;
    bool angehalten_ = false;
    uint32_t baudGegenseite_ = 0;
    bool abweichend_ = false;
    // Client: was zuletzt gemeldet wurde.
    uint32_t formatStand_ = 0;
    uint32_t gesendeteBaud_ = 0;
    // AP-S11: Format der Gegenseite (Server: Wunsch des Clients; Client: Antwort des Servers),
    // dazu bei Client das zuletzt gesendete Format zum Vergleich.
    GegenseiteStand fern_;
    uint8_t gesendetDaten_ = 0, gesendetParitaet_ = 0, gesendetStopp_ = 0;
    bool fernRtsBekannt_ = false, fernDtrBekannt_ = false;
    uint8_t fernModem_ = 0;       ///< Client: letztes MODEMSTATE
    bool fernModemBekannt_ = false;
    bool rtsGemeldet_ = false, dtrGemeldet_ = false, brkGemeldet_ = false, xonGemeldet_ = false;
    // Server: Leitungen des Clients und was wir gemeldet haben.
    bool fernRts_ = true, fernDtr_ = true;
    uint8_t modemGemeldet_ = 0;
    bool lineBrkGemeldet_ = false;
};

/// Betriebsart Datei (§6.6): puffern, spätestens alle 0,5 s (Uhrzeit) schreiben.
class DateiTransport {
public:
    using Uhr = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds INTERVALL{500};
    /// Ab dieser Menge wird sofort geschrieben (ein schneller Gast soll den Speicher
    /// nicht füllen, bis die halbe Sekunde um ist).
    static constexpr size_t SOFORT = 64 * 1024;

    ~DateiTransport();
    /// `anhaengen` = Wiederaufnahme beim Programmstart (§7.4a), sonst überschreiben.
    /// Der Pfad ist UTF-8.
    bool oeffnen(const std::string& pfad, bool anhaengen, std::string* fehler);
    bool offen() const { return f_.is_open(); }
    void aufnehmen(const uint8_t* d, size_t n);
    bool faellig(Uhr::time_point jetzt) const;
    /// Restzeit bis zum nächsten fälligen Schreiben (ms), -1 = nichts gepuffert.
    int restMs(Uhr::time_point jetzt) const;
    /// Gepuffertes schreiben (und flushen).  false = Schreibfehler, Text in `fehler`.
    bool schreiben(std::string* fehler);
    /// Rest schreiben und schließen.
    bool schliessen(std::string* fehler);

private:
    std::ofstream f_;
    std::vector<uint8_t> puffer_;
    Uhr::time_point seit_{};   ///< erstes noch nicht geschriebenes Byte
};

}  // namespace k1520::serial

/**
 * @file anschluss.h
 * @brief `SerialAnschluss` — die Schnittstelle zwischen einer Karte und dem Wandler
 *        (Entwurf 19 §5.1), dazu `SerialFormat` und seine Berechnung (§6.2).
 *
 * @details
 * Die Karte kennt kein Netz (Leitsatz 6): sie stellt je einstellbarer Schnittstelle
 * einen Anschluss bereit — Bytes, Steuerleitungen, Leitungsparameter.  Was daran haengt
 * (Telnet, RFC 2217, Datei, nichts), entscheidet der `Wandler` bzw. der `SerialHub`.
 *
 * **Faden:** Alle Methoden, die Kartenzustand lesen oder aendern (`format`, `sender*`,
 * `empfaenger*`, `empfange`, `rts`, `dtr`, `setzeEingaenge`, `break*`,
 * `waehleTaktquelle`), ruft der Wandler **nur aus dem Emulationsfaden** (`Wandler::takt`).
 * `name`, `stecker`, `v24` und `taktquellen` muessen feste Werte liefern — sie werden
 * auch aus dem GUI-Faden abgefragt.
 *
 * Die Kartenlogik der Leitungen (z. B. /CTSA = V106 ∧ V107 an der K8025) steckt in der
 * KARTE (`setzeEingaenge`), nicht im Wandler.
 *
 * @see doc/design/19_serielle_schnittstellen.md §5.1, §6.2
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace k1520::serial {

/// Nenntakt φ der Maschinen (A5120 und K8915): 2,4576 MHz.  Massstab fuer `baud_nenn`
/// und fuer die Maschinenzeit-Fristen des Wandlers (Entprellen 100 ms).
constexpr uint64_t PHI_NENN = 2457600;

/// Zeichenformat einer Schnittstelle, wie es der Gast programmiert hat (§5.1, §6.2).
struct SerialFormat {
    uint32_t baud_nenn     = 0;   ///< Baud bei Nenntakt, ggf. auf Normrate gerundet; 0 = unbekannt
    uint8_t  daten         = 8;   ///< Datenbits 5..8
    uint8_t  paritaet      = 0;   ///< 0 keine, 1 ungerade, 2 gerade
    uint8_t  stopp_halbe   = 2;   ///< Stoppbits in halben Bits: 2 = 1, 3 = 1½, 4 = 2
    uint64_t zeichen_takte = 0;   ///< Maschinentakte je Zeichen (Start + Daten + Paritaet + Stopp)
    bool     gueltig       = false;

    bool operator==(const SerialFormat& o) const {
        return baud_nenn == o.baud_nenn && daten == o.daten && paritaet == o.paritaet &&
               stopp_halbe == o.stopp_halbe && zeichen_takte == o.zeichen_takte &&
               gueltig == o.gueltig;
    }
    bool operator!=(const SerialFormat& o) const { return !(*this == o); }
};

/// Ersatzformat bei ungueltigem Format (§6.1): 9600 Bd 8N1 bei Nenntakt.
/// `gueltig` ist hier TRUE — es ist das tatsaechlich benutzte Format.
SerialFormat ersatzFormat(uint64_t phiNenn = PHI_NENN);

/// Baudrate auf eine Normrate runden, wenn sie weniger als 2 % abweicht; sonst ganzzahlig
/// gerundet.  0 bleibt 0.
uint32_t normBaud(double baud);

/**
 * @brief Format aus den Registerwerten rechnen (§6.2).
 *
 * `zeichen_takte = (1 + daten + (paritaet ? 1 : 0) + stopp) × sioTeiler × ctcTakte`
 * (1½ Stoppbits halbzahlig gerechnet), `baud_nenn = phiNenn / (sioTeiler × ctcTakte)`,
 * auf eine Normrate gerundet (`normBaud`).
 *
 * @param sioTeiler   SIO-Taktteiler 1/16/32/64 (WR4 D7–6)
 * @param daten       Datenbits (die SENDE-Bits, WR5 D6–5 — der Wandler taktet den Sender
 *                    und den Empfaenger mit derselben Zeichenzeit)
 * @param paritaet    0 keine, 1 ungerade, 2 gerade
 * @param stopp_halbe 2/3/4; 0 = Synchronbetrieb → ungueltig
 * @param ctcTakte    Maschinentakte je ZC/TO-Impuls (`Z80CTC::teilerTakte`), 0 = unbekannt
 *                    → ungueltig (CTC noch nicht programmiert, Fremdtakt)
 * @return Format; `gueltig == false`, wenn eine Groesse unbekannt ist.  Die uebrigen Felder
 *         sind dann so weit gefuellt, wie sie bekannt sind (Anzeige).
 */
SerialFormat serialFormatRechnen(uint8_t sioTeiler, uint8_t daten, uint8_t paritaet,
                                 uint8_t stopp_halbe, uint64_t ctcTakte,
                                 uint64_t phiNenn = PHI_NENN);

/// Wie serialFormatRechnen, aber mit gebrochener CTC-Periode in Q16 (Takte × 65536,
/// `Z80CTC::teilerTakteQ16`); `zeichen_takte` ist auf < 1 Takt genau (P8000 P5d).
SerialFormat serialFormatRechnenQ16(uint8_t sioTeiler, uint8_t daten, uint8_t paritaet,
                                    uint8_t stopp_halbe, uint64_t ctcTakteQ16,
                                    uint64_t phiNenn = PHI_NENN);

/// Merker vor serialFormatRechnenQ16: dieselbe Rechnung (reine Funktion der Eingaben, Ergebnis
/// bitgleich), aber nur bei geaenderten Eingaben neu — Teiler, Format und CTC-Periode aendern
/// sich nur, wenn der Gast SIO/CTC umprogrammiert; abgefragt wird je Befehl (Terminalkopplung,
/// Wandler).  Nicht fadensicher: wie jedes format() nur aus dem Emulationsfaden.
class SerialFormatMerker {
public:
    const SerialFormat& rechnenQ16(uint8_t sioTeiler, uint8_t daten, uint8_t paritaet,
                                   uint8_t stopp_halbe, uint64_t ctcTakteQ16,
                                   uint64_t phiNenn = PHI_NENN) {
        if (!gueltig_ || k_[0] != sioTeiler || k_[1] != daten || k_[2] != paritaet ||
            k_[3] != stopp_halbe || q16_ != ctcTakteQ16 || phi_ != phiNenn) {
            k_[0] = sioTeiler; k_[1] = daten; k_[2] = paritaet; k_[3] = stopp_halbe;
            q16_ = ctcTakteQ16; phi_ = phiNenn;
            f_ = serialFormatRechnenQ16(sioTeiler, daten, paritaet, stopp_halbe, ctcTakteQ16, phiNenn);
            gueltig_ = true;
        }
        return f_;
    }
private:
    uint8_t      k_[4] = {0, 0, 0, 0};
    uint64_t     q16_ = 0, phi_ = 0;
    SerialFormat f_;
    bool         gueltig_ = false;
};

/// Eine waehlbare Taktquelle (Bruecke auf der Karte), z. B. "ZRE-CTC K0 (W1:7)".
struct Taktquelle {
    std::string name;
};

/// Abstrakte Schnittstelle Karte → Wandler (§5.1).
class SerialAnschluss {
public:
    virtual ~SerialAnschluss() = default;

    // ── feste Angaben (jeder Faden) ─────────────────────────────────────────
    virtual const char* name() const = 0;      ///< UI-Name, z. B. "DFÜ/V.24"
    virtual const char* stecker() const = 0;   ///< z. B. "X6"
    virtual bool v24() const = 0;              ///< Steuerleitungen vorhanden
    /// Waehlbare Taktquellen; leer = fest verdrahtet.
    virtual std::vector<Taktquelle> taktquellen() const { return {}; }

    // ── Emulationsfaden ─────────────────────────────────────────────────────
    /// Taktquelle waehlen (Index in `taktquellen()`); wirkt sofort (§4).
    virtual void waehleTaktquelle(int /*index*/) {}
    /// Aktuelles Format aus WR3/4/5 + CTC (§6.2) — typisch ueber `sio_format.h`.
    virtual SerialFormat format() const = 0;
    /// Der SIO-Sender haelt ein Zeichen (Tx-Puffer belegt; bei Auto Enables nur mit /CTS).
    virtual bool senderHatZeichen() const = 0;
    /// Das Zeichen verlaesst das Schieberegister (Tx-Puffer wird frei → TxEmpty).
    virtual uint8_t senderNimm() = 0;
    /// Der SIO-Empfaenger hat Platz (FIFO nicht voll).  Der Wandler liefert NUR dann —
    /// deshalb gibt es nie einen Ueberlauf (Leitsatz 2).
    virtual bool empfaengerFrei() const = 0;
    virtual void empfange(uint8_t byte) = 0;

    /// Ausgaenge des Gastes (nur V.24 von Bedeutung); true = aktiv.
    virtual bool rts() const { return false; }
    virtual bool dtr() const { return false; }
    /// Eingaenge am STECKER (true = aktiv); die Karte verknuepft sie zu den SIO-Pins.
    virtual void setzeEingaenge(bool /*cts*/, bool /*dsr*/, bool /*dcd*/) {}

    /// Break (optional): der Gast sendet Break (WR5 D4) bzw. ein Break kommt an (RR0 D7).
    virtual bool breakGesendet() const { return false; }
    virtual void breakEmpfang(bool /*aktiv*/) {}

    /// Belegung des Steckers (AP-S5): true, solange ein Transport angebunden ist ODER
    /// der Rx/Tx-Loop (Pruefstecker) gesetzt ist.  Der Wandler meldet jeden Wechsel
    /// (und einmal beim ersten `takt`).  Die Karten schalten damit ihren alten
    /// Test-Unterbau (Rueckruf/Einspeisen, `K1520Machine::setDFUECallback` …) stumm:
    /// „ist ein Transport aktiv, geht er ins Leere" (Entwurf 19 §8).
    virtual void leitungBelegt(bool /*belegt*/) {}

    /// Pruefstecker (Rx/Tx-Loop, §6.5) gesteckt bzw. gezogen; der Wandler meldet jeden
    /// Wechsel (und einmal beim ersten `takt`).  Vorgabe: nichts — der Wandler bildet im
    /// Loop RTS→CTS und DTR→DSR/DCD nach, mit seinem Blickabstand (1/16 Zeichenzeit).
    /// Eine Karte, deren Stecker ANDERS gebrueckt ist (PC 1715: Drucker X4 hat nur
    /// 102/103/106 → 103→106; V.24 X5 brueckt 111→109 und 108→107, und 107 liegt an der
    /// SIO von Kanal A) oder deren Gast die Leitungen schneller nachliest, als der Wandler
    /// blickt (PCTEST: ein Lesezugriff ≈ 40 Takte nach dem Setzen), bildet die Bruecken
    /// selbst und SOFORT nach und uebergeht dann die Eingaenge aus `setzeEingaenge`.
    virtual void pruefstecker(bool /*gesteckt*/) {}
};

}  // namespace k1520::serial

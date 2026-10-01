/**
 * @file wandler.h
 * @brief Der Wandler zwischen SIO-Kanal (Maschinenzeit) und Transport (Uhrzeit) —
 *        Entwurf 19 §6.
 *
 * @details
 * Je Schnittstelle ein Wandler.  Er hat zwei Seiten:
 *
 * - **Maschinenseite** (`takt`, nur Emulationsfaden): taktet jedes Zeichen in
 *   Maschinentakten nach dem Format, das der Gast in SIO und CTC programmiert hat
 *   (Leitsatz 1), hält den Empfang bei XOFF (§6.3) bzw. fallendem RTS (§6.4) an, bildet
 *   Rx/Tx-Loop (§6.5), RTS/CTS-Brücke und „nichts angeschlossen" (§6.1) nach.
 * - **Gegenseite** (`fern*`, `anbinden`, `leeren`, …): der I/O-Faden des `SerialHub`
 *   (bzw. im Test eine Attrappe) nimmt Bytes aus dem Sendepuffer und legt empfangene in
 *   den Empfangspuffer.
 *
 * Beide Puffer sind Ringe zu 4 KiB mit **Rückstau statt Verlust** (Leitsatz 2): ist der
 * Sendepuffer voll, nimmt der Wandler dem SIO-Sender nichts mehr ab (der Gast wartet auf
 * TxEmpty); ist der Empfangspuffer voll, nimmt `fernGib` nichts mehr an (der I/O-Faden
 * liest den Socket nicht weiter, das TCP-Fenster schließt sich).  Zugestellt wird nur bei
 * `empfaengerFrei()` — **nie ein SIO-Überlauf**.
 *
 * Geteilter Zustand liegt unter EINEM Mutex; `takt` nimmt ihn höchstens einmal je
 * „Blick" (1/16 Zeichenzeit), sonst kehrt es sofort zurück — `takt` darf also je
 * Instruktion gerufen werden.  Der Mutex wird nie gehalten, während die Gegenseite
 * etwas anderes als diesen Wandler anfasst; `SerialHub` sperrt immer Hub → Wandler.
 *
 * @see doc/design/19_serielle_schnittstellen.md §6
 */
#pragma once

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

#include "core/serial/anschluss.h"

namespace k1520::serial {

/// Einstellungen, die im Betrieb sofort wirken (§4).
struct WandlerEinstellung {
    bool loop           = false;   ///< Rx/Tx-Loop (Prüfstecker), §6.5
    bool rtscts_bruecke = false;   ///< RTS→CTS, DTR→DSR/DCD am Stecker (nur V.24), §6.4
    bool xonxoff        = false;   ///< XON/XOFF beachten, §6.3
    int  taktquelle     = 0;       ///< Index in `SerialAnschluss::taktquellen()`
};

/// Momentaufnahme für Status und Transport (jeder Faden).
struct WandlerSicht {
    SerialFormat format;     ///< wie der Gast es programmiert hat (`gueltig` ggf. false)
    SerialFormat wirksam;    ///< tatsächlich getaktet (bei ungültigem Format: 9600 8N1)
    SerialFormat gemeldet;   ///< `wirksam`, entprellt 100 ms Maschinenzeit (RFC 2217)
    uint32_t gemeldetStand = 0;   ///< zählt jede Änderung von `gemeldet`
    bool rts = false, dtr = false, brk = false;     ///< Ausgänge des Gastes (nur V.24; brk alle)
    bool cts = false, dsr = false, dcd = false;     ///< Eingänge, wie zuletzt an die Karte gegeben
    bool xoffHalt = false;        ///< Gast hat XOFF gesendet (nur mit „XON/XOFF beachten")
    bool angebunden = false;      ///< ein Transport hängt dran
    uint64_t bytes_gesendet = 0;  ///< vom Gast gesendet und weitergegeben (auch Loop)
    uint64_t bytes_empfangen = 0; ///< dem Gast zugestellt
    uint32_t puffer_senden = 0, puffer_empfangen = 0;   ///< Füllstände
};

class Wandler {
public:
    static constexpr size_t PUFFER = 4096;   ///< je Richtung (§6.1)

    explicit Wandler(SerialAnschluss& anschluss, uint64_t phiNenn = PHI_NENN);
    Wandler(const Wandler&) = delete;
    Wandler& operator=(const Wandler&) = delete;

    SerialAnschluss& anschluss() { return a_; }
    bool v24() const { return v24_; }   ///< fest, jeder Faden

    // ── Maschinenseite (NUR Emulationsfaden) ────────────────────────────────
    /// Aus dem Lauf der Maschine mit der aktuellen Taktzahl (monoton; springt sie
    /// zurück — Reset des Zählers —, beginnen die Fristen neu).
    /// @return Taktzahl des nächsten „Blicks" — vorher tut `takt` nichts; die
    ///         Maschine darf den Aufruf bis dahin auslassen (AP-S5, Laufzeit).
    uint64_t takt(uint64_t zyklus);

    // ── Einstellungen (jeder Faden, wirken sofort) ──────────────────────────
    void einstellen(const WandlerEinstellung& e);
    WandlerEinstellung einstellung() const;
    WandlerSicht sicht() const;

    // ── Gegenseite (I/O-Faden bzw. Test) ────────────────────────────────────
    /// Transport hängt sich an: Eingänge gelten als „verbunden" (alle aktiv, so bei
    /// Telnet und Datei, §6.4); RFC 2217 überschreibt sie mit `fernLeitungen`.
    void anbinden();
    /// Transport weg (Kabel ab): Sendepuffer verworfen, Eingänge inaktiv.  Der
    /// Empfangspuffer wird noch zugestellt — die Bytes sind schon angekommen.
    void abbinden();
    bool angebunden() const;

    /// Bytes aus dem Sendepuffer (Gast → Netz) holen; Rückgabe = Anzahl.
    size_t fernNimm(uint8_t* ziel, size_t max);
    size_t fernBelegt() const;   ///< Füllstand Sendepuffer
    /// Bytes in den Empfangspuffer (Netz → Gast) legen; Rückgabe = angenommen (Rückstau).
    size_t fernGib(const uint8_t* daten, size_t n);
    size_t fernFrei() const;     ///< freier Platz im Empfangspuffer
    /// Eingänge am Stecker, wie die Gegenseite sie treibt (nach der Nullmodem-Kreuzung).
    void fernLeitungen(bool cts, bool dsr, bool dcd);
    void fernBreak(bool aktiv);
    /// PURGE (§7.5): Sende- und/oder Empfangspuffer leeren.
    void leeren(bool sendepuffer, bool empfangspuffer);

    /// Der Gast wurde zurückgesetzt (/RESET der Maschine, Netz-Ein): ein XOFF- oder
    /// RTS-Halt des alten Gastes gilt nicht weiter (AP-S5).
    void gastZurueckgesetzt();

    /// Weckruf zum I/O-Faden (Sendepuffer leer → belegt, Empfangspuffer voll → frei,
    /// Leitungen oder gemeldetes Format geändert).  Wird unter dem Wandler-Mutex
    /// gerufen und darf daher nichts sperren (`net::Wecker::wecken` ist nicht blockierend).
    void setzeWecker(std::function<void()> wecken);

private:
    /// Fester Ring ohne Allokation.
    struct Ring {
        uint8_t d[PUFFER];
        size_t  kopf = 0, n = 0;
        bool   leer() const { return n == 0; }
        bool   voll() const { return n == PUFFER; }
        size_t frei() const { return PUFFER - n; }
        void   rein(uint8_t b) { d[(kopf + n) % PUFFER] = b; ++n; }
        uint8_t raus() { uint8_t b = d[kopf]; kopf = (kopf + 1) % PUFFER; --n; return b; }
        void   leeren() { kopf = n = 0; }
    };

    SerialAnschluss& a_;
    const bool       v24_;
    const uint64_t   entprellTakte_;   ///< 100 ms Maschinenzeit
    const SerialFormat ersatz_;

    // Nur Emulationsfaden (ohne Sperre).
    uint64_t letzterZyklus_  = 0;
    uint64_t naechsterBlick_ = 0;
    /// Leerlauf (AP-S5, Laufzeit): nichts angebunden, kein Loop/keine Brücke, beide
    /// Puffer leer.  Dann prüft ein Blick nur „hat der Sender ein Zeichen?" und macht
    /// den vollen Durchlauf (Format, Leitungen, Sperre) nur jeden 16. Blick — also
    /// einmal je Zeichenzeit, damit der Status nicht veraltet.  Ohne das kosteten die
    /// drei Wandler des A5120 rund 13 % Laufzeit.
    std::atomic<bool> ruhig_{false};
    uint64_t ztLetzte_   = 0;   ///< zuletzt getaktete Zeichenzeit (Emulationsfaden)
    unsigned ruhZaehler_ = 0;

    mutable std::mutex m_;
    // ── unter m_ ──
    WandlerEinstellung einst_;
    int      taktquelleGesetzt_ = -1;
    Ring     send_, empf_;
    bool     angebunden_ = false;
    bool     fernCts_ = false, fernDsr_ = false, fernDcd_ = false, fernBrk_ = false;
    bool     xoff_ = false;
    /// Der Gast hat RTS seit dem letzten Anbinden/Reset einmal gesetzt — erst dann
    /// gilt der RTS-Halt (§6.4; Befund AP-S5: SCPX 8915 und CP/A setzen RTS nie).
    bool     rtsBenutzt_ = false;
    int      belegtGemeldet_ = -1;   ///< zuletzt an `leitungBelegt` gemeldet (-1 = nie)
    uint64_t naechstesSenden_ = 0, naechsteZustellung_ = 0;
    SerialFormat format_, wirksam_, kandidat_, gemeldet_;
    uint64_t kandidatSeit_ = 0;
    uint32_t gemeldetStand_ = 0;
    bool     rts_ = false, dtr_ = false, brk_ = false;
    bool     eingCts_ = false, eingDsr_ = false, eingDcd_ = false, eingGesetzt_ = false;
    bool     brkEmpf_ = false;
    uint64_t bytesGesendet_ = 0, bytesEmpfangen_ = 0;
    std::function<void()> wecker_;
};

}  // namespace k1520::serial

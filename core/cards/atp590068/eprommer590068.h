/**
 * @file eprommer590068.h
 * @brief EPROMmer der ATP 590068 (PRG 710) mit **virtuellem Sockel** — AP-P7b.
 *
 * Befund aus `PROG` (UDOS, V3.1) und `PROG.COM` (SCPX, V1.1), ausführlich in
 * `doc/prg710/eprommer.md`, Plan `doc/design/20_prg710.md` §3.4/AP-P7:
 *
 * @code
 *   D0H  PIO Port A Daten     Datenleitungen D0–D7 des PROM (Lesen: Eingang, Brennen: Ausgang)
 *   D1H  PIO Port B Daten     A0–A7
 *   D2H  PIO Steuerwort A     Betriebsart 3, Maske FFH (lesen) / 00H (brennen)   (6DC3/6DCA)
 *   D3H  PIO Steuerwort B     Betriebsart 3, Maske 00H
 *   D4H  Steuerregister [?]   Bit 0+1 Programmierspannung, Bit 2 Impuls, Bit 3 Versorgung 1
 *                              (U555; U2716: Programmierbetrieb), Bit 4 Versorgung, Bit 5–7 A8–A10
 *   84H  ZRE-PIO Port A Bit 0 Typ: 0 = U555 (1 KB, 2708-artig), 1 = U2716 (2 KB)
 * @endcode
 *
 * Die PIO ist wie die der K2521 adressiert: **B/A an AB0, C/D an AB1** (belegt durch die
 * Steuerwörter an D2H/D3H und 86H).  **Ein** Sockel, **kein** Löschen über Software
 * (Löschen = Bedienung, UV-Gerät).
 *
 * **Nur Belegtes wirkt:**
 * - Lesen (`IN (D0H)`): PROM steckt und Bit 4 → Byte an der Adresse (auf die Größe des
 *   PROM begrenzt), sonst FFH.
 * - Brennen an der **fallenden Flanke von Bit 2**, wenn PROM steckt, Bit 4, Bit 0 und
 *   Bit 1 gesetzt und der eingestellte Typ zum PROM passt: `Zelle &= Daten` (nur 1 → 0
 *   wie am echten EPROM).  Bits von Port A, die auf Eingang stehen, gelten als 1 (offen)
 *   `[?]`.
 * - Die Impulsbreite wird gemessen und protokolliert, aber **nicht bewertet**: wie ein
 *   PROM auf einen zu kurzen Impuls reagiert, belegt die Software nicht.  Außerhalb der
 *   Datenblattwerte (U555 0,1–1 ms, U2716 45–55 ms) gibt es einen Hinweis im Protokoll.
 * - Falscher Typ: Lesen geht, Brennen bleibt ohne Wirkung — beides mit Protokolleintrag
 *   `[?]` (Plan §8.6).
 *
 * **Protokoll** (Oberfläche, Debugger): Textzeilen mit Maschinentakt in einem Ringpuffer;
 * Spannungswechsel, Typwahl, Sockel, je Lese- bzw. Programmierphase eine Zusammenfassung,
 * Fehlbedienung.  Kein Eintrag je Byte (U555: 100 × 1024 Impulse).
 *
 * Sperre: der Sockel wird aus dem Oberflächenfaden bedient, der Bus aus dem Lauffaden.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_pio.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class Eprommer590068 : public BusDevice {
public:
    enum class Typ : uint8_t { Keiner = 0, U555 = 1, U2716 = 2 };

    struct Config {
        uint8_t  basis  = 0xD0;          ///< PIO D0H–D3H, Steuerregister basis+4
        uint32_t takt_hz = 2'457'600;    ///< für die Impulsbreite in ms (Protokoll)
    };

    /// Steuerregister D4H (Bitbedeutung aus PROG geschlossen, doc/prg710/eprommer.md §2).
    static constexpr uint8_t BIT_VPP     = 0x03;   ///< Bit 0+1: Programmierspannung
    static constexpr uint8_t BIT_IMPULS  = 0x04;   ///< Bit 2: Programmierimpuls
    static constexpr uint8_t BIT_VERS1   = 0x08;   ///< Bit 3: U555 erste Versorgung / U2716 Programmierbetrieb
    static constexpr uint8_t BIT_VERS    = 0x10;   ///< Bit 4: Betriebsspannung des Sockels

    static constexpr size_t PROTOKOLL_MAX = 1000;

    Eprommer590068() : Eprommer590068(Config{}) {}
    explicit Eprommer590068(const Config& cfg);

    static size_t      groesse(Typ t) { return t == Typ::U555 ? 1024 : t == Typ::U2716 ? 2048 : 0; }
    static const char* name(Typ t);

    /** @brief Ports basis…basis+4 am Systembus anmelden. */
    void attachToBus(K1520Bus& bus);
    /** @brief /RESET: PIO in den Einschaltzustand, Steuerregister 00H [?]; der Sockel bleibt. */
    void reset();

    /// Anschlüsse von ZRE-PIO Port A (Bit 0 = Typ).  Leer = immer 2 KB.
    void setTypwahl(std::function<uint8_t()> f) { typwahl_ = std::move(f); }
    /// Maschinentakte (Impulsbreite, Protokoll).  Leer = 0.
    void setZeit(std::function<uint64_t()> f)  { zeit_ = std::move(f); }

    // ─── BusDevice ───────────────────────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "ATP 590068 EPROMmer"; }

    // ─── Virtueller Sockel (Bedienung) ───────────────────────────────────────
    /**
     * @brief PROM einlegen.  @p typ Keiner = aus der Größe (≤ 1 KB U555, ≤ 2 KB U2716);
     *        kürzere Daten werden mit FFH aufgefüllt.  false bei zu großen Daten.
     */
    bool einlegen(const std::vector<uint8_t>& daten, Typ typ, const std::string& datei,
                  std::string* fehler = nullptr);
    /// Rohes `.bin` einlegen (Typ wie @ref einlegen).  Die Datei bleibt die Bindung für speichern().
    bool einlegenDatei(const std::string& pfad, Typ typ = Typ::Keiner, std::string* fehler = nullptr);
    /// Fabrikneues (gelöschtes) PROM einlegen: alles FFH, ohne Datei.
    void einlegenLeer(Typ typ);
    /// Nach dem Hinübertragen auf eine neue Maschine: „geändert“ wiederherstellen.
    void markiereGeaendert() { std::lock_guard<std::mutex> lk(sperre_); geaendert_ = true; }
    void entnehmen();
    /// Inhalt als rohes `.bin` schreiben; leerer Pfad = an die gebundene Datei.  Bindet neu.
    bool speichern(const std::string& pfad, std::string* fehler = nullptr);
    /// UV-Löschen (Bedienung): alles FFH.
    void uvLoeschen();

    // ─── Zustand ─────────────────────────────────────────────────────────────
    bool                 steckt() const;
    Typ                  typ() const;                ///< Typ des gesteckten PROM (Keiner = leer)
    std::vector<uint8_t> inhalt() const;
    std::string          datei() const;
    bool                 geaendert() const;          ///< seit Einlegen/Speichern gebrannt oder gelöscht
    uint8_t              steuerregister() const;
    Typ                  eingestellterTyp() const;   ///< aus ZRE-PIO 84H Bit 0
    uint16_t             adresse() const;            ///< A0–A10 wie gerade angelegt
    uint64_t             leseZugriffe() const;
    uint64_t             brennImpulse() const;       ///< alle Impulse (auch wirkungslose)

    /// Alle Protokollzeilen im Ringpuffer ("takt: text").
    std::vector<std::string> protokoll() const;
    /// Nur die Zeilen seit dem letzten Aufruf (ein Abnehmer: die Oberfläche).
    std::vector<std::string> protokollNeu();

    Z80PIO& pio() { return pio_; }

private:
    uint8_t  pioPort(uint8_t rel) const { return uint8_t(((rel & 1) << 1) | ((rel >> 1) & 1)); }
    uint64_t jetzt() const { return zeit_ ? zeit_() : 0; }
    uint16_t adresseLocked() const;
    uint8_t  datenleitungen() const;     ///< Port A wie am Sockel (Eingangsbits offen = 1)
    Typ      eingestelltLocked() const;
    void     log(const std::string& text);
    void     steuerregisterSchreiben(uint8_t neu);
    void     impulsEnde();
    void     lesephaseEnde();
    void     brennphaseEnde();
    void     typwahlPruefen();
    double   ms(uint64_t takte) const { return takte * 1000.0 / cfg_.takt_hz; }

    Config cfg_;
    Z80PIO pio_{"ATP EPROMmer-PIO"};
    std::function<uint8_t()>  typwahl_;
    std::function<uint64_t()> zeit_;

    mutable std::mutex sperre_;
    // Sockel
    Typ                  typ_ = Typ::Keiner;
    std::vector<uint8_t> zellen_;
    std::string          datei_;
    bool                 geaendert_ = false;
    // Steuerung
    uint8_t  d4_ = 0;
    uint64_t impuls_ab_ = 0;
    Typ      typ_gemeldet_ = Typ::Keiner;
    // Lesephase (Bit 4 an … aus)
    uint64_t lese_n_ = 0;
    uint16_t lese_min_ = 0xFFFF, lese_max_ = 0;
    bool     lese_typ_gewarnt_ = false;
    // Programmierphase (Bit 0+1 an … aus)
    bool     brennen_aktiv_ = false;
    uint64_t brenn_n_ = 0, brenn_wirksam_ = 0, brenn_min_ = ~0ull, brenn_max_ = 0;
    std::vector<uint16_t> impulse_je_adresse_;
    std::vector<uint8_t>  vorher_;
    bool     brenn_gewarnt_ = false, breite_gewarnt_ = false;
    // Zähler
    uint64_t lese_gesamt_ = 0, impulse_gesamt_ = 0;
    // Protokoll
    std::deque<std::string> protokoll_;
    uint64_t protokoll_nr_ = 0, abgeholt_ = 0;
};

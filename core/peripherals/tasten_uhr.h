/**
 * @file tasten_uhr.h
 * @brief Zeitbasis der Tastenwiederholung (K7637, K7672): Maschinenzeit oder Echtzeit.
 *
 * Die echten Tastaturen haben ihren eigenen Quarz — Verzögerung und Abstand der
 * Wiederholung hängen NICHT vom Takt des Rechners ab.  Im Emulator ist der Rechnertakt
 * einstellbar (bis 10 × und unbegrenzt); in Maschinentakten gezählt, schrumpften die
 * ≈ 0,5 s bis zur ersten Wiederholung bei 10 × auf 50 ms, und jeder normale Anschlag
 * käme mehrfach an.  Die Oberfläche schaltet deshalb auf **Echtzeit** um
 * (`k1520_set_key_repeat_realtime`).
 *
 * **Vorgabe bleibt die Maschinenzeit**: Tests und Werkzeuge laufen ungebremst und
 * müssen wiederholbar sein (`KeyboardIntegration.HeldSpaceRepeatsHeldLetterDoesNot`,
 * `K8915Scpx.GehalteneTasteWiederholtAmPrompt`).
 *
 * Gezählt wird in beiden Fällen in **Nenntakten** der Tastatur-Konstanten: in
 * Echtzeit wird die verstrichene Wirtszeit mit dem Nenntakt in Takte umgerechnet, die
 * Konstanten der Tastaturen bleiben also unverändert gültig.  Die Uhr liefert nur
 * DIFFERENZEN (@ref schritt); ein Wechsel der Zeitbasis setzt den Bezug neu, statt
 * Maschinen- und Wirtszeit zu vermischen.
 *
 * Die Wirtsuhr wird nur gelesen, solange eine Wiederholung läuft (@ref start /
 * @ref schritt) — der Service-Pfad wird je Befehl gerufen.
 */
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

class TastenUhr {
public:
    /// Wirtsuhr in Nanosekunden (monoton).  Austauschbar für Tests.
    using Quelle = uint64_t (*)();

    explicit TastenUhr(double nenntakt_hz) : hz_(nenntakt_hz) {}
    TastenUhr(const TastenUhr& o)
        : hz_(o.hz_), echtzeit_(o.echtzeit_.load(std::memory_order_relaxed)),
          quelle_(o.quelle_) {}
    TastenUhr& operator=(const TastenUhr& o) {
        hz_ = o.hz_;
        echtzeit_.store(o.echtzeit_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        quelle_ = o.quelle_;
        bezug_gueltig_ = false;
        return *this;
    }

    /// Fadensicher (Oberflächenfaden); wirkt beim nächsten @ref schritt.
    void setEchtzeit(bool an) { echtzeit_.store(an, std::memory_order_relaxed); }
    bool echtzeit() const     { return echtzeit_.load(std::memory_order_relaxed); }
    void setQuelle(Quelle q)  { quelle_ = q ? q : &wirtsuhrNs; bezug_gueltig_ = false; }

    /// Bezugspunkt setzen (Beginn einer Wiederholung); @p maschine = aktueller Takt.
    void start(uint64_t maschine) {
        modus_ = echtzeit();
        maschine_ = maschine;
        if (modus_) { ns_ = quelle_(); rest_ns_ = 0.0; }
        bezug_gueltig_ = true;
    }

    /// Verstrichene Nenntakte seit dem letzten Aufruf bzw. @ref start.
    uint64_t schritt(uint64_t maschine) {
        if (!bezug_gueltig_ || modus_ != echtzeit()) { start(maschine); return 0; }
        if (!modus_) {
            const uint64_t dt = maschine > maschine_ ? maschine - maschine_ : 0;
            maschine_ = maschine;
            return dt;
        }
        maschine_ = maschine;
        const uint64_t jetzt = quelle_();
        // Bruchteile eines Takts mitnehmen, sonst liefe die Uhr bei häufigem Abfragen
        // (je Befehl ein paar hundert ns) zu langsam.
        const double takte = (static_cast<double>(jetzt - ns_) + rest_ns_) * hz_ * 1e-9;
        const uint64_t ganz = static_cast<uint64_t>(takte);
        rest_ns_ = (takte - static_cast<double>(ganz)) * 1e9 / hz_;
        ns_ = jetzt;
        return ganz;
    }

    static uint64_t wirtsuhrNs() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

private:
    double            hz_;
    std::atomic<bool> echtzeit_{false};
    Quelle            quelle_ = &wirtsuhrNs;
    // Nur im Lauffaden:
    bool     bezug_gueltig_ = false;
    bool     modus_   = false;   ///< Zeitbasis des Bezugs
    uint64_t maschine_ = 0;
    uint64_t ns_       = 0;
    double   rest_ns_  = 0.0;
};

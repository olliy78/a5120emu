// Tastatur des PC 1715/1715W: eigener U880 mit EPROM S600 (doc/pc1715/tastatur.md).
//
// Echter Nachbau statt Protokollmodell: das ROM läuft auf dem Z80-Kern, die Matrix
// 13 Spalten x 8 Zeilen wird per IN (A15 = 1, genau ein Bit A0..A12) abgefragt,
// ausgegeben wird per OUT (A15 = 0): Bit = D0, Takt = /WR.  Die Klasse setzt die
// OUT-Folge zu 8N1-Rahmen zusammen (kein Bitmodell der SIO, Plan §12) und gibt
// das fertige Byte über byteOut aus; Empfänger ist SIO-A der ZRE.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "core/primitives/z80.h"

class Tastatur1715 {
public:
    static constexpr int SPALTEN = 13;
    static constexpr int ZEILEN  = 8;
    static constexpr uint32_t TAKT_TASTATUR_HZ = 700000;  ///< RC-Takt ± 10 %
    static constexpr uint32_t TAKT_RECHNER_HZ  = 2457600;
    /// Ein Abfragedurchlauf über alle 13 Spalten (gemessen, Takte der Tastatur-CPU).
    static constexpr uint32_t DURCHLAUF_TAKTE  = 5865;

    /// Tastenposition samt Umschaltung, wie sie der Rechner für ein Zeichen braucht.
    struct Taste { int spalte; int zeile; bool shift; };

    /// @param rom        2-KB-EPROM (nullptr = S600); wird über den ganzen Adressraum gespiegelt
    /// @param tastaturHz Takt der Tastatur-CPU
    /// @param rechnerHz  Takt, in dem run() aufgerufen wird (Rechnertakt)
    /// Tastatur-ROM-Fassung (AP-6): S600 = QWERTY (Vorgabe), TAST_618 = QWERTZ.
    enum class Rom : uint8_t { S600, Tast618 };
    /// Abzug der Fassung (2 KB).
    static const uint8_t* romFuer(Rom r);

    explicit Tastatur1715(const uint8_t* rom = nullptr,
                          uint32_t tastaturHz = TAKT_TASTATUR_HZ,
                          uint32_t rechnerHz = TAKT_RECHNER_HZ);

    /// Wie oben, aber für eine bestimmte ROM-Fassung.
    Tastatur1715(Rom art, uint32_t tastaturHz = TAKT_TASTATUR_HZ, uint32_t rechnerHz = TAKT_RECHNER_HZ);
    Rom art() const { return art_; }

    /// Fertiges Zeichen (Statusbyte E0H..EFH bzw. Code); aufgerufen, sobald das
    /// Stoppbit gesendet wurde.  Der Zeitpunkt steht in zeitLetztesByte().
    std::function<void(uint8_t)> byteOut;
    /// Optional, nur Diagnose/Tests: jedes /WR mit D0 und Takt der Tastatur-CPU.
    std::function<void(bool bit, uint64_t tastaturTakt)> bitTrace;

    // ── Matrix (gedrückt = 1) ─────────────────────────────────────────────
    void press(int spalte, int zeile);
    void release(int spalte, int zeile);
    void releaseAll();
    bool isPressed(int spalte, int zeile) const;

    // ── Zeit ──────────────────────────────────────────────────────────────
    /// Lässt die Tastatur-CPU um hostTakte Rechnertakte weiterlaufen.
    void run(uint64_t hostTakte);
    /// Dasselbe in Takten der Tastatur-CPU (für Tests).
    void runTastaturTakte(uint64_t takte);
    uint64_t tastaturTakte() const { return cpuTakte_; }
    uint64_t hostTakte() const { return hostTakte_; }
    /// Rechnertakt, zu dem das zuletzt ausgegebene Byte fertig war (Stoppbit gesendet).
    uint64_t zeitLetztesByte() const { return zeitLetztesByte_; }

    // ── Leuchten (nur Anzeige) ────────────────────────────────────────────
    bool ledSiSo() const { return ledSiSo_; }   ///< A13 des IN mit A15 = 0
    bool ledLock() const { return ledLock_; }   ///< A14 des IN mit A15 = 0

    // ── Zeichen → Taste ───────────────────────────────────────────────────
    /// Position der Taste, die @p c erzeugt (S600-Tabelle); false, wenn es keine gibt.
    /// Zeichen ohne Umschaltung werden bevorzugt.  '\r' = Taste ET (9EH, CP/A macht daraus CR; <-' ist eine Cursortaste), '\x1b' = ESC, '\x7f' = DEL.
    static bool tasteFuer(char c, Taste& out) { return tasteFuer(c, out, Rom::S600); }
    /// Dasselbe für eine ROM-Fassung.  S600: feste Tabelle; TAST_618: einmal aus dem ROM
    /// abgeleitet (jede Taste unverschoben und mit Shift durch das ROM gefahren), so bleibt die
    /// Tabelle nie hinter dem Abzug zurück.
    static bool tasteFuer(char c, Taste& out, Rom art);
    /// Die Taste dieser Instanz (ihre ROM-Fassung).
    bool tasteFuerDiese(char c, Taste& out) const { return tasteFuer(c, out, art_); }
    /// Drückt die Taste für @p c (bei Shift erst Shift, einzeln, mit Entprellzeit);
    /// gehalten wird sie bis zu releaseAll().  false, wenn es keine Taste gibt.
    bool pressKeyFor(char c);
    /// Tippt die Zeichenkette mit Entprellzeiten und Pausen (Tastaturzeit läuft mit).
    /// Unbekannte Zeichen werden übersprungen.
    void tippe(const std::string& text);

    // ── Zustand ───────────────────────────────────────────────────────────
    /// Hardware-Reset der Tastatur-CPU; die Matrix (Finger auf den Tasten) bleibt.
    void reset();
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    uint8_t lesePort(uint16_t port);
    void bit(bool b);

    Z80 cpu_;
    const uint8_t* rom_;
    Rom art_ = Rom::S600;
    uint32_t tastaturHz_, rechnerHz_;
    uint8_t matrix_[SPALTEN] = {};   ///< je Spalte ein Zeilenbyte, gedrückt = 1
    bool ledSiSo_ = false, ledLock_ = false;
    uint64_t cpuTakte_ = 0, hostTakte_ = 0, zeitLetztesByte_ = 0, hostSumme_ = 0;
    // Rahmenzusammensetzung: 0 = Leerlauf (auf Startbit warten), 1..8 = Datenbit, 9 = Stoppbit
    int rahmenZustand_ = 0;
    uint8_t rahmenByte_ = 0;
    bool letztesBit_ = true;   ///< ein Startbit gilt nur nach vorausgegangener 1
};

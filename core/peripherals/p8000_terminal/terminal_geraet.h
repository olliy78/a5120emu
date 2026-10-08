/**
 * @file terminal_geraet.h
 * @brief Gemeinsame Außenschnittstelle der P8000-Terminals (Entwurf 28 §6): das vereinfachte
 *        Kern-Terminal (P6) und das Originalterminal Typ 2 (P20) sind gegeneinander austauschbar.
 *
 * Die Maschine kennt nur `TerminalGeraet`: `takt()` im Emulationsfaden koppelt das Terminal an
 * den `SerialAnschluss` des Rechnerkanals, Bild und Cursor sind lesbar, Tasten gehen hinein.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/peripherals/p8000_terminal/terminal.h"
#include "core/peripherals/p8000_terminal/terminal_anschluss.h"

namespace k1520::p8000 {

class TerminalGeraet {
public:
    virtual ~TerminalGeraet() = default;
    /// @p maschinenTakte Takte der Rechnerseite sind vergangen (Emulationsfaden).
    virtual void takt(uint64_t maschinenTakte) = 0;
    virtual std::string text(int z) const = 0;
    virtual TerminalZelle zelle(int z, int s) const = 0;
    /// An (z, s) wirksames Feldattribut (ATTR_*-Bits, Feldregel bis Zeilenende).
    virtual uint8_t attribut(int z, int s) const = 0;
    virtual int zeile() const = 0;    ///< Cursor, 0-basiert
    virtual int spalte() const = 0;
    /// Zeichentaste (wie `Terminal::zeichenTaste`).  Rückgabe false = auf diesem Terminal nicht tippbar.
    virtual bool zeichenTaste(uint8_t c, bool ctrl = false) = 0;
    virtual bool taste(TerminalTaste t) = 0;
    virtual unsigned klingel() const = 0;
    virtual bool baudAbweichend() const = 0;
    virtual void serialize(std::vector<uint8_t>& out) const = 0;
    virtual bool deserialize(const uint8_t*& p, const uint8_t* end) = 0;
};

/// Das vereinfachte Kern-Terminal (P6) hinter der gemeinsamen Schnittstelle.
class KernTerminalGeraet : public TerminalGeraet {
public:
    explicit KernTerminalGeraet(serial::SerialAnschluss& karte, uint64_t phiNenn = 4'000'000)
        : anschluss_(karte, term_, phiNenn) {}
    Terminal& terminal() { return term_; }
    const Terminal& terminal() const { return term_; }
    void takt(uint64_t n) override { anschluss_.takt(n); }
    std::string text(int z) const override { return term_.text(z); }
    TerminalZelle zelle(int z, int s) const override { return term_.zelle(z, s); }
    uint8_t attribut(int z, int s) const override { return term_.wirksamesAttribut(z, s); }
    int zeile() const override { return term_.zeile(); }
    int spalte() const override { return term_.spalte(); }
    bool zeichenTaste(uint8_t c, bool ctrl) override { term_.zeichenTaste(c, ctrl); return true; }
    bool taste(TerminalTaste t) override { term_.taste(t); return true; }
    unsigned klingel() const override { return term_.klingel(); }
    bool baudAbweichend() const override { return anschluss_.baudAbweichend(); }
    void serialize(std::vector<uint8_t>& out) const override { term_.serialize(out); anschluss_.serialize(out); }
    bool deserialize(const uint8_t*& p, const uint8_t* end) override {
        return term_.deserialize(p, end) && anschluss_.deserialize(p, end);
    }

private:
    Terminal term_;
    TerminalAnschluss anschluss_;
};

}  // namespace k1520::p8000

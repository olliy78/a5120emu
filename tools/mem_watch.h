/**
 * @file mem_watch.h
 * @brief Speicher-Watchpoint fuer k1520dbg: Adressbereich + optionale Wert-Bedingung.
 *
 * Ein Watchpoint feuert bei einem Speicherzugriff, wenn (a) die Adresse im Bereich
 * [lo,hi] liegt, (b) die Richtung passt (write/read) und (c) die Wert-Bedingung gilt:
 *   ANY  – immer · EQ/NE – Datum == / != `val` · CHG – Wert hat sich gegenueber dem
 *   zuletzt gesehenen Wert dieser Adresse *tatsaechlich geaendert*.
 *
 * Reine Logik (mit Zustand fuer CHG) → ohne Maschine unit-testbar.
 * Die Adressbreite ist ein Parameter: `MemWatch` (16 Bit, Z80) und
 * `MemWatch32` (U8001: Schluessel seg·2¹⁶+off bzw. DRAM-Zelle des EM).
 *
 * @license MIT
 */
#pragma once
#include <cstdint>
#include <map>

namespace memwatch {

template <class Addr>
struct BasicMemWatch {
    Addr lo = 0, hi = 0;                     ///< Adressbereich (Einzelbyte → lo==hi)
    bool wr = false, rd = false, brk = false;///< bei Schreiben / Lesen ; brk=anhalten sonst drucken
    enum Cond { ANY, EQ, NE, CHG } cond = ANY;
    uint8_t val = 0;                         ///< fuer EQ/NE
    std::map<Addr, uint8_t> last;        ///< fuer CHG: zuletzt gesehener Wert je Adresse
    long hits = 0;                           ///< Trefferzaehler

    /**
     * Prueft, ob ein Zugriff diesen Watchpoint ausloest.
     * @param isRead  true = Lesezugriff, false = Schreibzugriff.
     * @param addr,data  Adresse und (bei Schreiben: neuer) Wert.
     * @return true, wenn der Watchpoint feuert. Aktualisiert bei CHG den last-Wert.
     *         (Der Trefferzaehler `hits` wird vom Aufrufer erhoeht.)
     */
    bool matches(bool isRead, Addr addr, uint8_t data) {
        if (addr < lo || addr > hi) return false;
        if (isRead ? !rd : !wr) return false;
        switch (cond) {
            case EQ:  return data == val;
            case NE:  return data != val;
            case CHG: { auto it = last.find(addr);
                        bool changed = (it == last.end()) || (it->second != data);
                        last[addr] = data;
                        return changed; }
            default:  return true;   // ANY
        }
    }
};

using MemWatch   = BasicMemWatch<uint16_t>;
using MemWatch32 = BasicMemWatch<uint32_t>;

} // namespace memwatch

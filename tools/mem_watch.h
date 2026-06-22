/**
 * @file mem_watch.h
 * @brief Speicher-Watchpoint für k1520dbg: Adressbereich + optionale Wert-Bedingung.
 *
 * Ein Watchpoint feuert bei einem Speicherzugriff, wenn (a) die Adresse im Bereich
 * [lo,hi] liegt, (b) die Richtung passt (write/read) und (c) die Wert-Bedingung gilt:
 *   ANY  – immer · EQ/NE – Datum == / != `val` · CHG – Wert hat sich gegenüber dem
 *   zuletzt gesehenen Wert dieser Adresse *tatsächlich geändert*.
 *
 * Reine Logik (mit Zustand für CHG) → ohne Maschine unit-testbar.
 *
 * @license MIT
 */
#pragma once
#include <cstdint>
#include <map>

namespace memwatch {

struct MemWatch {
    uint16_t lo = 0, hi = 0;                 ///< Adressbereich (Einzelbyte → lo==hi)
    bool wr = false, rd = false, brk = false;///< bei Schreiben / Lesen ; brk=anhalten sonst drucken
    enum Cond { ANY, EQ, NE, CHG } cond = ANY;
    uint8_t val = 0;                         ///< für EQ/NE
    std::map<uint16_t, uint8_t> last;        ///< für CHG: zuletzt gesehener Wert je Adresse
    long hits = 0;                           ///< Trefferzähler

    /**
     * Prüft, ob ein Zugriff diesen Watchpoint auslöst.
     * @param isRead  true = Lesezugriff, false = Schreibzugriff.
     * @param addr,data  Adresse und (bei Schreiben: neuer) Wert.
     * @return true, wenn der Watchpoint feuert. Aktualisiert bei CHG den last-Wert.
     *         (Der Trefferzähler `hits` wird vom Aufrufer erhöht.)
     */
    bool matches(bool isRead, uint16_t addr, uint8_t data) {
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

} // namespace memwatch

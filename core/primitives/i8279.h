/**
 * @file i8279.h
 * @brief Intel 8279 / КР580ВВ79 — Tastatur-/Anzeigecontroller (nur der belegte Teil).
 *
 * Gebraucht von der ATP 590068 des PRG 710 (Ports C8H Daten / C9H Befehl+Status,
 * doc/design/20_prg710.md §3.4, doc/prg710/resident.md §5).  Nachgebildet, was Boot-ROM
 * (0157H), UDOS-Resident (0633H) und SCPX-BIOS `B15x` (E082H/E097H) benutzen:
 *
 * - **Befehle** an C9H: `02H` Betriebsart (kodierte Abtastung, N-Tasten-Rollover),
 *   `C1H` Clear (FIFO und Zustandsbits).  Die übrigen Befehle (Taktteiler, Anzeige-RAM,
 *   Sperren, Fehlerbetrieb) werden gespeichert/ignoriert und mit `LOG_DEBUG` protokolliert —
 *   der Anzeigeteil fehlt absichtlich (auf der ATP hängt keine Anzeige am 8279 [?]).
 * - **Status** (Lesen C9H): Bit 0–2 = Zeichenzahl im FIFO, Bit 3 = FIFO voll, Bit 4 =
 *   Unterlauf (Lesen aus leerem FIFO), Bit 5 = Überlauf (Eintrag in vollen FIFO).
 * - **Daten** (Lesen C8H) holt den ältesten Eintrag; aus dem leeren FIFO FFH.
 * - FIFO 8 Einträge.  Die Tasten kommen von außen (@ref pushKey) als fertiger Code
 *   `S C RRR CCC`; die Abtastung selbst ist nicht nachgebildet.
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>

class I8279 {
public:
    static constexpr size_t FIFO_TIEFE = 8;
    enum Status : uint8_t {
        ST_ANZAHL = 0x07, ST_VOLL = 0x08, ST_UNTERLAUF = 0x10, ST_UEBERLAUF = 0x20,
    };

    /** @brief /RESET: FIFO leer, Zustandsbits gelöscht, Betriebsart = Vorgabe (0). */
    void reset();

    /** @brief Port 1 (C9H) schreiben: Befehl. */
    void writeCommand(uint8_t cmd);
    /** @brief Port 1 (C9H) lesen: Status. */
    uint8_t readStatus() const;
    /** @brief Port 0 (C8H) lesen: nächster FIFO-Eintrag (FFH bei leerem FIFO). */
    uint8_t readData();
    /** @brief Port 0 (C8H) schreiben: Anzeige-RAM — nicht nachgebildet, nur protokolliert. */
    void writeData(uint8_t d);

    /**
     * @brief Eine Taste in den FIFO (von der Tastatur).  Voller FIFO: Überlauf, der
     *        Code geht verloren.
     * @return true, wenn eingetragen.
     */
    bool pushKey(uint8_t code);

    size_t  count() const { return fifo_.size(); }
    bool    full()  const { return fifo_.size() >= FIFO_TIEFE; }
    /// IRQ-Ausgang: high, solange der FIFO nicht leer ist (nicht verdrahtet [?]).
    bool    irq()   const { return !fifo_.empty(); }
    uint8_t lastMode() const { return mode_; }
    uint8_t lastCommand() const { return last_cmd_; }

private:
    std::deque<uint8_t> fifo_;
    uint8_t mode_ = 0;           ///< zuletzt gesetzte Betriebsart (Befehl 000x xxxx)
    uint8_t last_cmd_ = 0;
    bool    unterlauf_ = false;
    bool    ueberlauf_ = false;
};

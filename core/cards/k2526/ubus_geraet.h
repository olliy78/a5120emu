/**
 * @file ubus_geraet.h
 * @brief Steckplatz X1 der K2526 (U-Bus, E/A 00H–07H) für ein Gerät mit eigenem Verhalten.
 *
 * Ohne Gerät bleibt der U-Bus der K2526 so, wie er immer war (zwei Register, die ein
 * Test/die Maschine über `setKbdCode/setKbdValid` füllt) — der A5120 mit K7637 ändert sich
 * nicht.  Ein angeschlossenes Gerät (heute: die parallele Tastatur K7636,
 * `core/peripherals/k7636/`) bekommt dagegen die Zugriffe auf /UCS1, /UCS2, /UCS3, /UCS4
 * selbst und liefert die Lesewerte.
 *
 * Anschließen VOR `K2526::powerOn()`; `powerOn()` ruft dann @ref ubusReset (die Tastatur
 * wird mit dem Rechner eingeschaltet).
 */
#pragma once
#include <cstdint>

class UBusGeraet {
public:
    virtual ~UBusGeraet() = default;

    /**
     * @brief Lesezugriff auf Port 00H–07H.
     * @return true, wenn das Gerät den Port bedient (dann steht der Wert in @p wert);
     *         false = die K2526 antwortet wie ohne Gerät.
     */
    virtual bool ubusRead(uint8_t port, uint8_t& wert) = 0;

    /// Schreibzugriff auf Port 00H–07H (das Gerät sieht alle; die K2526 merkt sich 03H/05H weiter).
    virtual void ubusWrite(uint8_t port, uint8_t data) = 0;

    /// System-/RESET bzw. Einschalten.
    virtual void ubusReset() = 0;
};

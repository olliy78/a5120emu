/**
 * @file memory.h
 * @brief Speichersubsystem des Robotron A5120 Emulators
 *
 * Emuliert den 64 KByte großen Adressraum des Z80-Prozessors als
 * zusammenhängendes RAM-Array. Der Bildschirmspeicher liegt am oberen
 * Ende des Adressraums (ab 0xF800) und umfasst den sichtbaren Bereich
 * von 80×24 Zeichen.
 *
 * Speicheraufteilung:
 * - 0x0000–0xF7FF: Programm- und Datenspeicher (62 KByte)
 * - 0xF800–0xFFFF: Bildschirmspeicher (2 KByte, davon 1920 Bytes sichtbar)
 *
 * Die Klasse bietet Einzel- und Blockzugriffe sowie Dateien laden.
 *
 * @author Olaf Krieger
 * @license MIT License
 * @see https://opensource.org/licenses/MIT
 */
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/**
 * @class Memory
 * @brief Emuliert den 64-KByte-Adressraum des Robotron A5120.
 *
 * Stellt einfachen Byte- und Wortzugriff sowie Massenladeoperationen
 * bereit. Bietet zusätzlich spezialisierte Zugriffsmethoden für den
 * Bildschirmspeicher, der von der Terminal-Darstellung verwendet wird.
 */
class Memory {
public:
    // =========================================================================
    /// @name Speicherlayout-Konstanten
    // =========================================================================
    ///@{
    static constexpr size_t SIZE = 65536;            ///< Gesamtgröße: 64 KByte
    static constexpr uint16_t SCREEN_START = 0xF800; ///< Startadresse des Bildschirmspeichers
    static constexpr uint16_t SCREEN_END   = 0xFFFF; ///< Endadresse des Bildschirmspeichers
    static constexpr size_t SCREEN_SIZE    = 2048;   ///< Gesamtgröße Bildschirmbereich (0x800)
    static constexpr int SCREEN_COLS       = 80;     ///< Spalten pro Zeile
    static constexpr int SCREEN_ROWS       = 24;     ///< Anzahl sichtbarer Zeilen
    static constexpr size_t SCREEN_CHARS   = 1920;   ///< Sichtbare Zeichen (80×24)
    ///@}

    /** @brief Konstruktor – initialisiert den gesamten Speicher mit Nullen. */
    Memory();

    /**
     * @brief Liest ein Byte aus dem Speicher.
     * @param addr 16-Bit-Adresse (0x0000–0xFFFF).
     * @return Gelesenes Byte.
     */
    uint8_t read(uint16_t addr) const;

    /**
     * @brief Schreibt ein Byte in den Speicher.
     * @param addr 16-Bit-Adresse.
     * @param val Zu schreibendes Byte.
     */
    void write(uint16_t addr, uint8_t val);

    // =========================================================================
    /// @name Massenoperationen
    // =========================================================================
    ///@{

    /**
     * @brief Lädt einen Datenblock in den Speicher.
     *
     * Die Länge wird automatisch begrenzt, damit der Speicherbereich
     * nicht überschritten wird.
     *
     * @param addr Startadresse.
     * @param data Zeiger auf die Quelldaten.
     * @param len Länge in Bytes.
     */
    void load(uint16_t addr, const uint8_t* data, size_t len);

    /** @brief Lädt einen Vektor in den Speicher. */
    void load(uint16_t addr, const std::vector<uint8_t>& data);

    /**
     * @brief Lädt eine Binärdatei in den Speicher.
     * @param filename Dateipfad.
     * @param addr Startadresse im Speicher.
     * @return true bei Erfolg, false bei Dateifehler.
     */
    bool loadFile(const std::string& filename, uint16_t addr);

    /** @brief Löscht den gesamten Speicher (füllt mit 0x00). */
    void clear();

    /**
     * @brief Füllt einen Speicherbereich mit einem konstanten Wert.
     * @param start Startadresse (inklusive).
     * @param end Endadresse (inklusive).
     * @param val Füllwert.
     */
    void fill(uint16_t start, uint16_t end, uint8_t val);

    ///@}

    // =========================================================================
    /// @name Direktzugriff
    // =========================================================================
    ///@{

    /** @brief Konstanter Zeiger auf den gesamten Speicher. */
    const uint8_t* data() const { return ram_; }

    /** @brief Schreibbarer Zeiger auf den gesamten Speicher. */
    uint8_t* data() { return ram_; }

    ///@}

    // =========================================================================
    /// @name Bildschirmspeicher-Zugriff
    // =========================================================================
    ///@{

    /** @brief Zeiger auf den Bildschirmspeicher (ab SCREEN_START). */
    const uint8_t* screenBuffer() const { return &ram_[SCREEN_START]; }

    /**
     * @brief Liest ein einzelnes Zeichen aus dem Bildschirmspeicher.
     *
     * Gibt das ASCII-Zeichen (Bits 0–6) ohne Inversmarker zurück.
     * Bei ungültigen Koordinaten wird ein Leerzeichen zurückgegeben.
     *
     * @param row Zeile (0–23).
     * @param col Spalte (0–79).
     * @return ASCII-Zeichen (7 Bit).
     */
    uint8_t getScreenChar(int row, int col) const;

    ///@}

private:
    uint8_t ram_[SIZE];  ///< Hauptspeicher-Array (64 KByte)
};

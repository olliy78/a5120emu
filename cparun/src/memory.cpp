/**
 * @file memory.cpp
 * @brief Speichersubsystem – Implementierung
 *
 * Implementiert den emulierten 64-KByte-Adressraum des Robotron A5120.
 * Alle Zugriffe erfolgen direkt auf ein zusammenhängendes Array ohne
 * Bank-Switching oder Memory-Mapping, da der A5120 einen flachen
 * Adressraum verwendet.
 *
 * @author Olaf Krieger
 * @license MIT License
 * @see https://opensource.org/licenses/MIT
 */
#include "memory.h"
#include <fstream>

/**
 * @brief Konstruktor – initialisiert den gesamten Speicher mit Nullen.
 */
Memory::Memory() {
    clear();
}

/**
 * @brief Liest ein Byte aus dem Speicher.
 * @param addr 16-Bit-Adresse.
 * @return Gelesenes Byte.
 */
uint8_t Memory::read(uint16_t addr) const {
    return ram_[addr];
}

/**
 * @brief Schreibt ein Byte in den Speicher.
 * @param addr 16-Bit-Adresse.
 * @param val Zu schreibendes Byte.
 */
void Memory::write(uint16_t addr, uint8_t val) {
    ram_[addr] = val;
}

/**
 * @brief Lädt einen Datenblock in den Speicher ab der angegebenen Adresse.
 *
 * Die Länge wird automatisch auf den verfügbaren Platz begrenzt,
 * um einen Pufferüberlauf zu verhindern.
 *
 * @param addr Startadresse im Speicher.
 * @param data Zeiger auf die Quelldaten.
 * @param len Anzahl zu kopierender Bytes.
 */
void Memory::load(uint16_t addr, const uint8_t* data, size_t len) {
    size_t maxLen = SIZE - addr;
    if (len > maxLen) len = maxLen;
    memcpy(&ram_[addr], data, len);
}

/**
 * @brief Lädt einen std::vector in den Speicher.
 * @param addr Startadresse.
 * @param data Quelldaten als Vektor.
 */
void Memory::load(uint16_t addr, const std::vector<uint8_t>& data) {
    load(addr, data.data(), data.size());
}

/**
 * @brief Lädt eine Binärdatei in den Speicher.
 *
 * Die Datei wird binär geöffnet und vollständig eingelesen.
 * Die Dateigröße wird auf den ab addr verfügbaren Platz begrenzt.
 *
 * @param filename Pfad zur Binärdatei.
 * @param addr Startadresse im Speicher.
 * @return true wenn die Datei erfolgreich gelesen wurde.
 */
bool Memory::loadFile(const std::string& filename, uint16_t addr) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    size_t maxLen = SIZE - addr;
    if (fileSize > maxLen) fileSize = maxLen;

    file.read(reinterpret_cast<char*>(&ram_[addr]), fileSize);
    return file.good() || file.eof();
}

/**
 * @brief Löscht den gesamten Speicher (füllt mit 0x00).
 */
void Memory::clear() {
    memset(ram_, 0, SIZE);
}

/**
 * @brief Füllt einen Speicherbereich mit einem konstanten Wert.
 * @param start Startadresse (inklusive).
 * @param end Endadresse (inklusive).
 * @param val Füllwert.
 */
void Memory::fill(uint16_t start, uint16_t end, uint8_t val) {
    for (uint32_t i = start; i <= end && i < SIZE; i++) {
        ram_[i] = val;
    }
}

/**
 * @brief Liest ein einzelnes Zeichen aus dem Bildschirmspeicher.
 *
 * Berechnet die Adresse als SCREEN_START + row*80 + col und gibt
 * das Zeichen ohne Inversmarker (Bit 7 maskiert) zurück.
 * Bei ungültigen Koordinaten wird ein Leerzeichen zurückgegeben.
 *
 * @param row Zeile (0–23).
 * @param col Spalte (0–79).
 * @return ASCII-Zeichen (7 Bit).
 */
uint8_t Memory::getScreenChar(int row, int col) const {
    if (row < 0 || row >= SCREEN_ROWS || col < 0 || col >= SCREEN_COLS)
        return ' ';
    return ram_[SCREEN_START + row * SCREEN_COLS + col] & 0x7F;
}

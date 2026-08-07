/**
 * @file fixtures.h
 * @brief Zugriff auf die Testdisketten — und beschreibbare Kopien davon.
 *
 * Die committeten Disketten unter `tests/fixtures/disks/` dürfen NIE direkt
 * gemountet werden: der Emulator öffnet sie schreibend, ein Testlauf kann sie
 * also verändern.  `TempDisk` nimmt einem die Kopie samt Aufräumen ab — bis
 * 2026-08-07 stand dieses `temp_directory_path()/copy_file/remove`-Ritual in
 * jedem Test einzeln (und der `remove`-Teil fiel bei einem fehlgeschlagenen
 * ASSERT still aus).
 */
#pragma once

#include <string>

namespace k1520test {

/// Vollständiger Pfad einer Fixture-Diskette (Verzeichnis kommt aus CMake).
std::string diskPath(const std::string& name);

/// Dateiinhalt als Bytes (leer, wenn nicht lesbar).
std::string readFileBytes(const std::string& path);

/**
 * @brief Beschreibbare Temp-Kopie einer Fixture — räumt sich selbst weg.
 *
 * ```
 * k1520test::TempDisk a("scpx17_cpa780_k5601.hfe");   // Kopie der Fixture
 * k1520test::TempDisk b = TempDisk::empty("ziel.hfe"); // nur ein freier Pfad
 * machine.mountDisk(0, a.path(), "cpa780", false);
 * ```
 * Der Destruktor löscht die Datei — auch wenn der Test per ASSERT abbricht.
 */
class TempDisk {
public:
    /// Kopiert die Fixture @p fixture_name nach /tmp und merkt sich den Pfad.
    explicit TempDisk(const std::string& fixture_name);

    /// Wie oben, aber mit eigenem Dateinamen im Temp-Verzeichnis — nötig, wenn
    /// ein Test DIESELBE Fixture zweimal gleichzeitig braucht (A: und B:).
    TempDisk(const std::string& fixture_name, const std::string& temp_name);

    /// Nur einen freien Temp-Pfad reservieren (für createDisk-Ziele).
    static TempDisk empty(const std::string& file_name);

    ~TempDisk();
    TempDisk(TempDisk&&) noexcept;
    TempDisk(const TempDisk&) = delete;
    TempDisk& operator=(const TempDisk&) = delete;

    const std::string& path() const { return path_; }
    operator const std::string&() const { return path_; }

private:
    TempDisk() = default;
    std::string path_;
};

}  // namespace k1520test

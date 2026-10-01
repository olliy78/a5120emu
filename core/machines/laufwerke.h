/**
 * @file laufwerke.h
 * @brief Laufwerksverwaltung einer K1520-Maschine mit K5122 — der gemeinsame Baustein
 *        von A5120 und K8915 (doc/design/16_k8915.md §7.1, zweiter Absatz).
 *
 * Alles, was die C-ABI an Disketten braucht (Einlegen, Anlegen, Speichern unter,
 * Formaterkennung, Schreibschutz, Anzeigen), hängt nicht an der Maschine, sondern an
 * der K5122 und ihren vier Laufwerken.  Bis AP-E3 stand es in `A5120Machine`; mit dem
 * K8915 als zweitem Nutzer ist es hierher gewandert — **unverändert**, die Maschinen
 * reichen nur noch durch.  Die Methoden sind in `A5120Machine` dokumentiert
 * (core/machines/a5120/a5120.h), die Doku gilt hier wörtlich.
 *
 * Fadensicherheit: Einlegen/Auswerfen/Speichern laufen unter @ref mutex, den auch die
 * Laufschleife beim Autosave nimmt (@ref autoFlush).
 */

#pragma once
#include "core/cards/k5122/k5122.h"
#include "core/peripherals/floppy_drive/disk_format.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Laufwerke {
public:
    /**
     * @param afs      die K5122 der Maschine (gehört der Maschine)
     * @param profile  Bestückung der vier Anschlüsse
     * @throws std::runtime_error, wenn der Formatkatalog fehlt oder kaputt ist
     */
    Laufwerke(K5122& afs, const std::array<DriveProfile, 4>& profile);

    bool mountDisk(int drive, const std::string& path,
                   const std::string& format_name, bool write_protect);
    bool mountDiskImage(int drive, std::unique_ptr<DiskImage> img, bool write_protect);
    bool createDisk(int drive, const std::string& path,
                    const std::string& format_name, bool write_protect);
    bool saveDiskAs(int drive, const std::string& path, const std::string& format_name);
    bool unmountDisk(int drive);
    bool flushDisks();

    bool isDiskRawCompatible(int drive) const;
    std::string diskPath(int drive) const;
    std::string diskContainer(int drive) const;
    std::string diskNotice(int drive) const;
    DiskGeometry diskGeometry(int drive) const;
    bool isDiskFormatted(int drive) const;
    std::string detectedFormatName(int drive) const;
    std::string defaultFormatName(int drive) const;
    std::vector<std::string> compatibleFormats(int drive) const;
    std::string formatDescription(const std::string& format_name) const;
    const FormatCatalog& formatCatalog() const { return katalog_; }

    bool isDiskActive(int drive) const;
    bool isDiskWriteProtected(int drive) const;
    bool isDiskLedOn(int drive) const;
    bool isMotorOn(int drive) const;
    bool isHeadLoaded() const;
    void setDiskWriteProtect(int drive, bool wp);

    /**
     * @brief Verzögertes Zurückschreiben aus der Laufschleife (doc/design/09 §6.1).
     *
     * Sieht nur alle @ref kPruefAbstand Takte nach — `run()` wird von Werkzeugen auch
     * befehlsweise gerufen, die Sperre soll dort nicht ins Gewicht fallen.
     */
    void autoFlush(uint64_t total_cycles);

    const std::string& lastError() const { return last_error_; }

private:
    /// Abstand zweier Autosave-Prüfungen in Maschinentakten (≈ 40 ms @ 2,45 MHz).
    static constexpr uint64_t kPruefAbstand = 100'000;

    K5122&                      afs_;
    FormatCatalog               katalog_;   // aus data/formats.yaml (§8.6)
    std::array<DriveProfile, 4> profile_;   // Bestückung je Anschluss (create-Vorgabe)
    mutable std::mutex          mutex_;
    uint64_t                    naechste_pruefung_ = kPruefAbstand;
    std::string                 last_error_;
};

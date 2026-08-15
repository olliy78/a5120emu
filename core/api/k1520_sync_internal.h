/**
 * @file k1520_sync_internal.h
 * @brief C++-Innenseite des @ref k1520_sync_api.h — nur für die beiden Bibliotheken.
 *
 * Das Handle hält die noch **nicht angemeldete** Diskette (`image`) und den
 * Synchronisierer (`sync`).  Beim Mounten wandert `image` zum Laufwerk bzw. zum
 * `DiskVolume`; `sync` bleibt beim Handle, damit der Arbeitsfaden weiterarbeiten kann
 * und die Anzeige weiter Zahlen bekommt.
 *
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/api/k1520_sync_api.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_sync.h"

#include <memory>
#include <string>

/// @brief Innenleben eines @ref K1520Sync.
struct K1520SyncHandle {
    std::unique_ptr<DiskImage> image;   ///< bis zum Mounten hier; danach nullptr
    std::shared_ptr<TrackSync> sync;    ///< überlebt die Diskette (detach macht es sicher)
    std::string                last_error;
};

/// @brief Handle auflösen; nullptr bei ungültigem Zeiger.
K1520SyncHandle* k1520s_handle(K1520Sync h);

/**
 * @brief Die Diskette aus dem Handle nehmen (zum Anmelden an Laufwerk/Datenträger).
 * @return nullptr, wenn sie schon angemeldet ist.
 */
std::unique_ptr<DiskImage> k1520s_take_image(K1520Sync h);

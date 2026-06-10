/**
 * @file track_image.cpp
 * @brief Implementierung von TrackImage::nextMark.
 *
 * @see core/peripherals/floppy_drive/track_image.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/track_image.h"
#include <climits>

size_t TrackImage::nextMark(size_t pos, MarkType type) const {
    const size_t n = marks.size();
    if (n == 0) return SIZE_MAX;

    // Zyklische Vorwärtssuche ab pos (inklusive pos selbst).
    // Ein vollständiger Umlauf wird abgebrochen, sobald eine passende Marke
    // gefunden oder die Startposition erneut erreicht wird.
    for (size_t i = 0; i < n; ++i) {
        const size_t idx = (pos + i) % n;
        const MarkType m = marks[idx];
        if (m == MarkType::None) continue;               // kein Markenbyte
        if (type == MarkType::None || m == type) return idx;
    }
    return SIZE_MAX;
}

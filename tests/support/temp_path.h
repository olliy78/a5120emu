/**
 * @file temp_path.h
 * @brief Ein Temp-Dateiname, den kein zweiter Testprozess gleichzeitig benutzt.
 *
 * **Warum das nötig ist:** `gtest_discover_tests` registriert JEDEN Testfall als
 * eigenen ctest-Fall, und ctest startet ihn als eigenen **Prozess**.  Mit `ctest -j`
 * laufen also mehrere Testprozesse gleichzeitig — und wenn zwei davon denselben
 * festen Namen unter `/tmp` benutzen (`k1520_test_img_codec.img`), schreibt der eine
 * hinein, während der andere liest.
 *
 * Unter Linux fällt das nicht auf: dort darf man eine geöffnete Datei löschen, der
 * Inhalt lebt bis zum letzten Griff weiter.  Unter **Windows** ist es ein harter
 * Fehler — `remove` scheitert mit „Sharing violation", und der Test stirbt an einer
 * Ausnahme, die nichts mit seinem Gegenstand zu tun hat.  Gefunden am 2026-08-12 mit
 * `tools/dev.sh win` (MinGW + wine, `ctest -j8`): sechs Tests rot, seriell alle grün.
 *
 * Header-only und ohne Abhängigkeiten — `k1520_testsupport` zieht die ganze
 * Maschinenbibliothek mit und wird von den Unit-Tests bewusst nicht gelinkt.
 *
 * ```
 * const auto pfad = k1520test::tempPath("k1520_test_img_codec.img");
 * ```
 */
#pragma once

#include <filesystem>
#include <string>

#include "core/util/os_compat.h"

namespace k1520test {

/**
 * @brief `<temp>/<pid>_<name>` — im Temp-Verzeichnis des Systems, prozesseindeutig.
 *
 * Die Prozessnummer steht **vorn**: so bleibt die Dateiendung erhalten (Codecs
 * wählen danach aus, `…​.img` vs. `…​.hfe`) und gleichartige Dateien eines Laufs
 * stehen im Verzeichnis beieinander.
 */
inline std::string tempPath(const std::string& name) {
    return (std::filesystem::temp_directory_path()
            / (std::to_string(k1520::os::processId()) + "_" + name)).string();
}

}  // namespace k1520test

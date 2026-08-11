/**
 * @file os_compat.h
 * @brief Die drei Stellen, an denen der Kern das Betriebssystem direkt fragt.
 *
 * Der Kern ist bis auf `format_catalog.cpp` (Modulpfad) portabel — was blieb, sind
 * drei Kleinigkeiten aus Werkzeugen und Tests: die eigene Prozessnummer (eindeutige
 * Namen für Kopien in `/tmp`), „hängt an einem Terminal?" (interaktiver Zeileneditor)
 * und das Setzen einer Umgebungsvariablen im Test.  Statt in jeder Datei ein
 * `#ifdef _WIN32` zu wiederholen, stehen sie hier **einmal**.
 *
 * Bewusst header-only und ohne `windows.h`: `<process.h>`/`<io.h>` sind winzig, und
 * `windows.h` würde `min`/`max` als Makro in jeden Übersetzungsvorgang schleppen.
 *
 * @see doc/design/13_distribution.md §6.1
 */

#pragma once

#include <string>

#if defined(_WIN32)
#  include <cstdlib>
#  include <io.h>        // _isatty
#  include <process.h>   // _getpid
#else
#  include <cstdlib>
#  include <unistd.h>    // getpid, isatty
#endif

namespace k1520::os {

/// Prozessnummer — für eindeutige Namen von Arbeitskopien.
inline long processId() {
#if defined(_WIN32)
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

/// Hängt der Dateideskriptor an einem Terminal?  (0 = Standardeingabe)
inline bool isTerminal(int fd) {
#if defined(_WIN32)
    return ::_isatty(fd) != 0;
#else
    return ::isatty(fd) != 0;
#endif
}

/// Umgebungsvariable setzen (überschreibt).  `true` bei Erfolg.
inline bool setEnv(const char* name, const char* value) {
#if defined(_WIN32)
    return ::_putenv_s(name, value) == 0;
#else
    return ::setenv(name, value, 1) == 0;
#endif
}

/// Umgebungsvariable löschen.  `true` bei Erfolg.
inline bool unsetEnv(const char* name) {
#if defined(_WIN32)
    // Unter Windows löscht ein LEERER Wert den Eintrag — `_unsetenv` gibt es nicht.
    return ::_putenv_s(name, "") == 0;
#else
    return ::unsetenv(name) == 0;
#endif
}

}  // namespace k1520::os

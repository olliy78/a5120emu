# ─────────────────────────────────────────────────────────────────────────────
# Cross-Bau nach Windows x64 mit MinGW-w64 — LOKALE VORPRÜFUNG, nicht die Zusage
# ─────────────────────────────────────────────────────────────────────────────
#
# Wofür das gut ist: der Entwicklungsrechner ist Linux, MSVC gibt es hier nicht.
# Jede Portierungsfrage über die CI zu klären kostet 10 Minuten je Runde.  MinGW
# übersetzt hier in Sekunden gegen die WINDOWS-Kopfdateien und findet damit alles,
# was plattformabhängig ist: fehlende `#include <windows.h>`-Zweige, POSIX-Aufrufe,
# `_WIN32`-Pfade, Pfadtrennzeichen, Zeilenenden.  Die Tests laufen anschließend
# unter `wine`.
#
# Was es NICHT findet — MinGW ist GCC, MSVC ist ein anderer Übersetzer:
#   * fehlende Ausfuhrkennzeichnung (MinGW exportiert wie Linux per Vorgabe alles!)
#   * MSVC-eigene Strenge (Zwei-Phasen-Auflösung, C2065 bei abhängigen Namen)
#   * /utf-8, statische CRT, Sektionslimits (/bigobj)
# Die verbindliche Prüfung bleibt der Windows-Workflow (.github/workflows/windows-ci.yml).
#
# Benutzung:   tools/dev.sh win  [ctest-args]
# Vorbedingung: sudo apt install g++-mingw-w64-x86-64 wine

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_triple x86_64-w64-mingw32)

# Debian liefert zwei Threadmodelle.  Der Kern benutzt std::mutex/std::atomic —
# die gibt es nur im POSIX-Modell; das win32-Modell übersetzt <mutex> gar nicht.
find_program(_mingw_cxx NAMES ${_triple}-g++-posix ${_triple}-g++)
find_program(_mingw_cc  NAMES ${_triple}-gcc-posix ${_triple}-gcc)
if(NOT _mingw_cxx)
    message(FATAL_ERROR
        "MinGW-w64 nicht gefunden.  Installieren:  sudo apt install g++-mingw-w64-x86-64")
endif()
set(CMAKE_CXX_COMPILER "${_mingw_cxx}")
set(CMAKE_C_COMPILER   "${_mingw_cc}")
find_program(CMAKE_RC_COMPILER NAMES ${_triple}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${_triple})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Statisch binden: sonst braucht jede .exe libstdc++-6.dll/libgcc_s_seh-1.dll/
# libwinpthread-1.dll neben sich, und wine findet sie nicht.
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")

# ctest (und gtest_discover_tests beim Bauen) starten die .exe hierüber.
find_program(_wine NAMES wine64 wine)
if(_wine)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${_wine}")
else()
    message(WARNING "wine nicht gefunden — es wird nur gebaut, nicht getestet")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# CLI-/Blackbox-Tests: die gebauten Werkzeuge als Prozess
# ─────────────────────────────────────────────────────────────────────────────
#
# Werkzeug starten, Sitzung einspielen, Ausgabe prüfen.  Deckt die Schicht ab,
# die die Header-Tests unter tests/debugtools/ nicht erreichen: Kommando-
# zerlegung, Argumentbehandlung, Exit-Codes, Zusammenbau der Maschine.
#
# Die Fälle stehen als Daten in cases/*.cli und werden von run_case.py
# ausgeführt — je Datei ein ctest-Fall, Name `cli_<dateiname ohne .cli>`.
# Einen Fall hinzufügen heißt: eine .cli-Datei anlegen, sonst nichts.
#
# Warum nicht mehr im CMake (Stand bis 2026-08-07): die Fälle waren escapte
# Shell-Einzeiler mit CMake-Regex als Erwartung.  Sonderzeichen überlebten das
# doppelte Escaping nicht — im Code stand deshalb der Hinweis „regex uses '.'
# for literal []()*+".  Dazu 15 Kopien desselben mktemp/cp/rm-Rituals, das bei
# einem Abbruch Diskettenkopien liegenließ.  Format und Direktiven: Kopf von
# run_case.py.

find_package(Python3 COMPONENTS Interpreter QUIET)

if (NOT Python3_Interpreter_FOUND)
    message(STATUS "CLI-Tests NICHT registriert — kein python3 gefunden")
    return()
endif()

file(GLOB _cli_cases CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cases/*.cli")

# Beim Cross-Bau (tools/dev.sh win: MinGW nach Windows) sind die Werkzeuge
# `.exe` und laufen auf dem Linux-Rechner nur unter `wine`.  CMake weiß das
# bereits — gtest_discover_tests benutzt denselben Wert; ohne die Weitergabe an
# run_case.py starteten alle 59 CLI-Fälle die .exe direkt und scheiterten mit
# „Exec format error".  Leer im Normalfall, dann ändert sich nichts.
set(_cli_emulator "")
if (CMAKE_CROSSCOMPILING_EMULATOR)
    set(_cli_emulator --emulator "${CMAKE_CROSSCOMPILING_EMULATOR}")
endif()

foreach(_case IN LISTS _cli_cases)
    get_filename_component(_name "${_case}" NAME_WE)
    add_test(NAME cli_${_name}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_case.py"
                "${_case}"
                --fixtures "${K1520_FIXTURE_DISKS}"
                ${_cli_emulator}
                --tool "k1520dbg=$<TARGET_FILE:k1520dbg>"
                --tool "boot_trace=$<TARGET_FILE:boot_trace>"
                --tool "k1520disktool=$<TARGET_FILE:k1520disktool>")
    set_tests_properties(cli_${_name} PROPERTIES TIMEOUT 120 LABELS "cli;fast")
endforeach()

list(LENGTH _cli_cases _cli_count)
message(STATUS "CLI-Tests: ${_cli_count} Fälle aus cases/*.cli")

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
                --tool "k1520disktool=$<TARGET_FILE:k1520disktool>"
                --tool "z8kasm=$<TARGET_FILE:z8kasm>")
    set_tests_properties(cli_${_name} PROPERTIES TIMEOUT 120 LABELS "cli;fast")
endforeach()

list(LENGTH _cli_cases _cli_count)
message(STATUS "CLI-Tests: ${_cli_count} Fälle aus cases/*.cli")

# Wächter „eingecheckte .com passt zur Quelle" für die eigenen CP/M-Programme
# (SERTEST, ROMREAD, A5120.16-Prüfprogramme; Entwurf 19 §14.8): baut die Quelle in
# ein temporäres Verzeichnis neu (M80 + LINKMT über cparun aus der CPA_Workbench,
# CPA_TOOLS; tools/cpm_bau.py) und vergleicht bytegleich.  Ohne Werkzeugkette — in
# der CI immer — Exit 77 = übersprungen, mit Meldung.
# em256 braucht für die U8001-Firmware das z8kasm DIESES Baus — im Cross-Bau
# (MinGW) wäre das eine .exe, die der Bau nicht ausführen kann; dort fehlt der
# Abgleich (der native Lauf hat ihn).
set(_cpm_programme sertest romread)
if(NOT CMAKE_CROSSCOMPILING)
    list(APPEND _cpm_programme em256)
endif()
foreach(_prog IN LISTS _cpm_programme)
    add_test(NAME cli_${_prog}_com_passt_zur_quelle
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/${_prog}/build.py" --check)
    set_tests_properties(cli_${_prog}_com_passt_zur_quelle PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 LABELS "cli;fast")
endforeach()
if(NOT CMAKE_CROSSCOMPILING)
    set_tests_properties(cli_em256_com_passt_zur_quelle PROPERTIES
        ENVIRONMENT "Z8KASM=$<TARGET_FILE:z8kasm>")
endif()

# Wächter „die Bootdisketten in disks/ tragen die aktuellen eigenen Programme"
# (SERTEST, ROMREAD, EM256ADR/EM16ABL/EM256FUL) und die Prüflinge unter
# tests/fixtures/cpm/ sind dieselbe Fassung: die Disketten gehen als
# Beispieldisketten ins Paket; nach einem Neubau einer .com liefert es sonst eine
# alte Fassung aus.  Nachziehen: python3 tools/disketten_beigaben.py --tool <k1520disktool>
set(_beigaben_emulator "")
foreach(_e IN LISTS CMAKE_CROSSCOMPILING_EMULATOR)   # Cross-Bau: .exe unter wine
    list(APPEND _beigaben_emulator --emulator "${_e}")
endforeach()
add_test(NAME cli_beigaben_auf_den_disketten
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/disketten_beigaben.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_beigaben_auf_den_disketten PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Wächter „die kommentierten Boot-ROM-Listings des PRG 710/710-1 sind die Ausgabe des
# Generators" (AP-P0d, doc/design/20_prg710.md): tools/gen_prg710_zre_prn.py --check
# erzeugt beide Listings im Speicher, prüft MD5 der ROM-Abzüge, dass JEDES der 1024 Byte
# genau einmal und mit dem richtigen Wert dargestellt ist, dass jede benannte Routine als
# Label vorkommt, und vergleicht bytegleich mit doc/EPROMS/PRG710/*_zre.prn.
# Neu erzeugen: python3 tools/gen_prg710_zre_prn.py
add_test(NAME cli_prg710_zre_prn_passt_zur_quelle
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_prg710_zre_prn.py" --check)
set_tests_properties(cli_prg710_zre_prn_passt_zur_quelle PROPERTIES
    TIMEOUT 60 LABELS "cli;fast")

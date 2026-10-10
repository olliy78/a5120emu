# k1520_add_test() — eine Zeile je GoogleTest-Binary.
#
# Ersetzt den bis 2026-08-07 üblichen Block aus add_executable +
# target_link_libraries + target_include_directories + eine SEPARAT gepflegte
# gtest_discover_tests-Zeile an anderer Stelle der Datei.  Zwei Listen, die
# auseinanderlaufen können, werden damit zu einer.
#
#   k1520_add_test(<name>
#       SRC     <quelle.cpp> [...]        # Pflicht, relativ zum aufrufenden Verzeichnis
#       LIBS    <lib> [...]               # ohne GTest::gtest_main (kommt automatisch)
#       LABELS  <label> [...]             # ctest -L / -LE
#       DEFS    <NAME=wert> [...]         # zusätzliche Compile-Definitionen
#       TIMEOUT <sekunden>                # Vorgabe 60, wird mit K1520_TEST_ZEIT_FAKTOR malgenommen
#       [RUN_SERIAL])                     # Fälle nie parallel (gemeinsame Dateien)
#
# Feste Zusagen für jeden so erzeugten Test:
#   * Zielname          k1520_test_<name>
#   * Binary landet in  ${CMAKE_BINARY_DIR}  (also build/k1520_test_<name>) —
#     NICHT in build/tests/…, damit eingespielte Aufrufe und die Dokumentation
#     („./build/k1520_test_k2526 --gtest_filter=…") gültig bleiben.
#   * Include-Wurzel    ${CMAKE_SOURCE_DIR}  → #include "core/…" / "tools/…"
#   * Fixture-Pfade als Compile-Definition (siehe unten)

# Zeitgrenzen: EIN Faktor für alle Tests (2026-10-10).  Die Grenzen in den
# CMakeLists sind auf einem leeren Rechner gemessen; mit ~2900 Fällen bei
# `-j16` und nebenher laufenden Emulatoren reichten sie nicht mehr
# (cli_dbg_k8915_all_commands_smoke: 120 s überschritten, einzeln ~30 s).
# Statt jede Zahl einzeln zu verdoppeln, gilt dieser Faktor — für
# k1520_add_test() hier, für die übrigen add_test()-Fälle über
# k1520_test_zeiten_skalieren() am Ende von tests/CMakeLists.txt.
set(K1520_TEST_ZEIT_FAKTOR 2 CACHE STRING
    "Faktor auf alle ctest-Zeitgrenzen (ganze Zahl)")

function(k1520_add_test name)
    cmake_parse_arguments(T "RUN_SERIAL" "TIMEOUT" "SRC;LIBS;LABELS;DEFS" ${ARGN})

    if(NOT T_SRC)
        message(FATAL_ERROR "k1520_add_test(${name}): SRC fehlt")
    endif()
    if(T_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "k1520_add_test(${name}): unbekannte Argumente: ${T_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT T_TIMEOUT)
        set(T_TIMEOUT 60)
    endif()
    math(EXPR T_TIMEOUT "${T_TIMEOUT} * ${K1520_TEST_ZEIT_FAKTOR}")

    set(target k1520_test_${name})

    add_executable(${target} ${T_SRC})
    target_link_libraries(${target} PRIVATE ${T_LIBS} GTest::gtest_main)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR})

    # Testdisketten liegen an EINEM Ort.  Beide historischen Makronamen werden
    # gesetzt, damit die Tests nicht alle gleichzeitig umgeschrieben werden
    # müssen: A5120_TEST_DISK_DIR (Integrations-/Systemtests, diskPath()) und
    # FIXTURE_DIR (Floppy-Unit-Tests).
    target_compile_definitions(${target} PRIVATE
        A5120_TEST_DISK_DIR="${K1520_FIXTURE_DISKS}"
        FIXTURE_DIR="${K1520_FIXTURE_DISKS}"
        ${T_DEFS})

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    # ACHTUNG: gtest_discover_tests reicht PROPERTIES als flache Liste weiter.
    # Ein unmaskiertes "unit;fast" zerfällt dabei in zwei Argumente — gesetzt
    # wird dann NUR "unit", der Rest verschwindet lautlos.  Das ist genau die
    # Falle, die am 2026-08-07 die drei System-Tests trotz Label
    # "format_integration" in der schnellen Runde mitlaufen ließ.  Die
    # maskierten Semikola halten die Labelliste als EINEN Wert zusammen.
    string(REPLACE ";" "\\;" _labels "${T_LABELS}")

    # Beim CROSS-Bau (tools/dev.sh win) das Einsammeln der Fälle auf die Testzeit
    # verschieben.  Vorgabe ist POST_BUILD: CMake startet JEDES Testprogramm
    # einmal während des Bauens, um seine TESTs aufzuzählen — im Cross-Bau also
    # unter `wine`, und bei `cmake --build -j` gehen dutzende wine-Starts
    # gleichzeitig los.  Einer fällt dabei regelmäßig um, und der Bau meldet
    # „Error running test executable", obwohl nichts kaputt ist (dreimal am
    # 2026-08-12 passiert; ein zweiter Aufruf lief jedes Mal durch).  Ein roter
    # Bau, der nicht rot ist, macht das Werkzeug unbrauchbar.
    #
    # PRE_TEST sammelt stattdessen erst ein, wenn ctest den Fall braucht — dann
    # einzeln statt in einem Sturm.  Nur beim Cross-Bau, damit der native Bau
    # sein eingespieltes Verhalten behält (dort ist die Vorgabe schneller, weil
    # sie einmal statt je ctest-Aufruf läuft).  DISCOVERY_MODE gibt es ab
    # CMake 3.18; darunter bleibt es bei der Vorgabe.
    set(_discovery "")
    if (CMAKE_CROSSCOMPILING AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
        set(_discovery DISCOVERY_MODE PRE_TEST)
    endif()

    # RUN_SERIAL: die Fälle laufen nie neben anderen (gemeinsame Zwischenstände
    # außerhalb des Baums, z. B. p8000_wega_install).
    set(_serial "")
    if (T_RUN_SERIAL)
        set(_serial RUN_SERIAL TRUE)
    endif()
    gtest_discover_tests(${target} ${_discovery}
        PROPERTIES TIMEOUT ${T_TIMEOUT} LABELS "${_labels}" ${_serial})
endfunction()

# Skaliert die TIMEOUT-Eigenschaft aller mit add_test() angelegten Tests des
# AUFRUFENDEN Verzeichnisses mit K1520_TEST_ZEIT_FAKTOR — als letzte Zeile jeder
# tests/<ebene>/CMakeLists.txt (aus dem eigenen Verzeichnis, weil der Zugriff auf
# Tests eines anderen Verzeichnisses erst ab CMake 3.28 geht).  Die Fälle aus
# k1520_add_test() stehen NICHT in der Verzeichnis-Eigenschaft TESTS (sie
# entstehen erst beim gtest-Einsammeln) und sind oben schon skaliert — doppelt
# wird also nichts.  Tests ohne eigene Grenze bleiben bei der ctest-Vorgabe.
macro(k1520_test_zeiten_skalieren)
    get_property(_k1520_tests DIRECTORY PROPERTY TESTS)
    foreach(_k1520_t IN LISTS _k1520_tests)
        get_test_property(${_k1520_t} TIMEOUT _k1520_alt)
        if(_k1520_alt)
            math(EXPR _k1520_neu "${_k1520_alt} * ${K1520_TEST_ZEIT_FAKTOR}")
            set_tests_properties(${_k1520_t} PROPERTIES TIMEOUT ${_k1520_neu})
        endif()
    endforeach()
endmacro()

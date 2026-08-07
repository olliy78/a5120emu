# ─────────────────────────────────────────────────────────────────────────────
# Testsystem — einziger Ort der Testregistrierung
# ─────────────────────────────────────────────────────────────────────────────
#
# Gliederung nach Testebene, ein Unterverzeichnis je Ebene:
#
#   unit/         isolierte Klasse, keine Diskette, kein Boot   (µs–ms)
#     primitives/   Z80, PIO, CTC, SIO + RETI-Daisy-Chain
#     bus/          K1520Bus
#     cards/        K2526, K3526, K5122, K7024, K8025
#     peripherals/  K7637 + der Floppy-Stack (TrackCodec/BitCodec/DiskImage/…)
#     util/         yaml_lite
#   debugtools/   header-only Bausteine der Werkzeuge (tools/*.h)
#   integration/  ganze Maschine, echter Kaltboot                (0,2–2 s)
#   cli/          Werkzeuge als Prozess, Blackbox                (< 1 s)
#   system/       FORMAT/CPABCGEN/SCPX/HARDY                     (Minuten)
#   python/       pytest: C-ABI + GUI
#
# Auswahl über Labels statt über handgepflegte Ausschlusslisten:
#
#   ctest -L unit            nur Unit-Tests
#   ctest -L integration     nur Maschinen-Integrationstests
#   ctest -LE format_integration   alles außer den langsamen (= tools/dev.sh test)
#
# Jeder Test trägt sein Ebenen-Label plus `fast` oder `slow`.  Die langsamen
# tragen ZUSÄTZLICH das historische Label `format_integration`, weil dev.sh und
# die Dokumentation darauf verweisen.

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/K1520AddTest.cmake)

# Einziger Fundort der Testdisketten (siehe tests/fixtures/README.md).
# k1520_add_test() reicht ihn jedem Test als Compile-Definition herein.
set(K1520_FIXTURE_DISKS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/disks")

add_subdirectory(unit)
add_subdirectory(debugtools)
add_subdirectory(integration)
add_subdirectory(cli)
add_subdirectory(system)
add_subdirectory(python)

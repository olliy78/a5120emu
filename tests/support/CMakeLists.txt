# ─────────────────────────────────────────────────────────────────────────────
# k1520_testsupport — gemeinsame Test-Infrastruktur
# ─────────────────────────────────────────────────────────────────────────────
#
# „Boote bis zum Prompt", „warte auf Text im Bildschirm", „tippe ein Kommando",
# „kopiere die Diskette nach /tmp" war bis 2026-08-07 in
# test_boot_integration.cpp, test_scpx_init.cpp und test_hardy.cpp jeweils
# eigenständig implementiert.  Diese Bibliothek ist die eine Fassung davon.
#
# Benutzung in einem Test:
#     k1520_add_test(hardy SRC test_hardy.cpp
#                          LIBS k1520_a5120 k1520_floppy k1520_logger k1520_testsupport
#                          LABELS "system;slow")
#
# Bewusst NICHT automatisch an jeden Test gelinkt: die Unit-Tests kommen ohne
# A5120Machine aus und sollen die Maschinenbibliothek nicht mitziehen.

add_library(k1520_testsupport STATIC
    fixtures.cpp
    screen.cpp
    machine_run.cpp
    keyboard.cpp
)
target_link_libraries(k1520_testsupport PUBLIC k1520_a5120 k1520_floppy k1520_logger)
target_include_directories(k1520_testsupport PUBLIC ${CMAKE_SOURCE_DIR})

# Fundort der Testdisketten — dieselbe Definition, die k1520_add_test() den
# Tests hereinreicht (fixtures.cpp wertet sie aus).
target_compile_definitions(k1520_testsupport
    PRIVATE A5120_TEST_DISK_DIR="${K1520_FIXTURE_DISKS}")

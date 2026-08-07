# Integrationstests: die ganze Maschine (A5120Machine), echter Kaltboot von
# einer Fixture-Diskette.  Laufzeit je Fall 0,2–2 s — bleiben deshalb in der
# schnellen Runde.  LOG_LEVEL=2 (WARN) hält den Millionen-Takte-Lauf lesbar.

set(_lbl "integration;fast")
set(_libs k1520_a5120 k1520_floppy k1520_logger)

k1520_add_test(boot_integration SRC test_boot_integration.cpp LIBS ${_libs}
                                LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 60)
k1520_add_test(machine_snapshot SRC test_machine_snapshot.cpp LIBS ${_libs} LABELS "${_lbl}")
k1520_add_test(a5120_disk_api   SRC test_a5120_disk_api.cpp   LIBS ${_libs} LABELS "${_lbl}")

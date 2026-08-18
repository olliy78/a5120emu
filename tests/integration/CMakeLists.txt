# Integrationstests: die ganze Maschine (A5120Machine), echter Kaltboot von
# einer Fixture-Diskette.  Laufzeit je Fall 0,2–2 s — bleiben deshalb in der
# schnellen Runde.  LOG_LEVEL=2 (WARN) hält den Millionen-Takte-Lauf lesbar.

set(_lbl "integration;fast")
set(_libs k1520_a5120 k1520_floppy k1520_logger k1520_testsupport)

k1520_add_test(boot_integration SRC test_boot_integration.cpp LIBS ${_libs}
                                LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 60)
k1520_add_test(machine_snapshot SRC test_machine_snapshot.cpp LIBS ${_libs} LABELS "${_lbl}")
k1520_add_test(a5120_disk_api   SRC test_a5120_disk_api.cpp   LIBS ${_libs} LABELS "${_lbl}")

# Physische Diskette (Greaseweazle): Kaltstart von einer SPURWEISE gelesenen Quelle.
# Braucht KEINE Hardware — ein Ersatz-Arbeitsfaden bedient die Auftraege aus einer
# .hfe-Fixture (doc/design/14_physische_diskette.md §14).
k1520_add_test(physical_boot    SRC test_physical_boot.cpp    LIBS ${_libs}
                                LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# k1520DiskTool ↔ echtes CP/A: geschrieben wird mit CpmFileSystem, gelesen vom
# laufenden Betriebssystem (doc/design/13_k1520disktool.md §15).
k1520_add_test(disktool_cpm_roundtrip SRC test_disktool_cpm_roundtrip.cpp
                                      LIBS ${_libs} k1520_fs
                                      LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# Dieselbe Kreuzprobe auf FRISCH ANGELEGTEN Disketten: 192 Verzeichnisplaetze (cpa800,
# vom Werkzeug SELBST ausgerechnet) und die Doppelschritt-Abbildung (`step: 2`).
k1520_add_test(disktool_neue_disketten SRC test_disktool_neue_disketten.cpp
                                       LIBS ${_libs} k1520_fs
                                       LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# Bootfaehige Diskette: Systemspuren herausholen, neue Diskette damit anlegen,
# Systemdateien einfuegen — und davon kalt starten.
k1520_add_test(disktool_bootdiskette SRC test_disktool_bootdiskette.cpp
                                     LIBS ${_libs} k1520_fs
                                     LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

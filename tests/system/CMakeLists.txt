# ─────────────────────────────────────────────────────────────────────────────
# Systemtests: echte DDR-Programme unter dem Emulator (langsam)
# ─────────────────────────────────────────────────────────────────────────────
#
# Diese Ebene fährt vollständige Anwenderabläufe mit den originalen Programmen
# (FORMAT.COM, CPABCGEN.COM, SCPX INIT/MODF/SYSP, HARDY) — je Fall ein oder
# mehrere komplette Kaltstarts, Laufzeit Minuten statt Millisekunden.
#
# Labels: "system;slow" plus das historische "format_integration", auf das
# tools/dev.sh (test / test-all / test-format) und die Dokumentation verweisen.
# `tools/dev.sh test` schließt sie via `-LE format_integration` aus.

set(_libs k1520_a5120 k1520_floppy k1520_logger k1520_testsupport)
set(_lbl  "system;slow;format_integration")

# Langsamer SCPX-INIT.COM-Formatier-Guard (~35 s, 160 Spuren): INIT formatiert B:,
# MODF/SYSP erzeugen ein 5x1024-System, PIP kopiert, davon wird gebootet.
k1520_add_test(scpx_init SRC test_scpx_init.cpp LIBS ${_libs}
                         LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# HARDY-Hardware-Testprogramm: der „Rechner"-Test laeuft clean durch (Waechter
# fuer den /MEMDI- + BS-PIO-Bus-Strobe-Fix).
k1520_add_test(hardy     SRC test_hardy.cpp     LIBS ${_libs}
                         LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 180)

# ─── Boot-Disk-Erstellung: leere Diskette → formatieren → bootfähig → boot ──
# Voller Anwender-Workflow (Tastatur-getrieben über format_driver): eine leere,
# als Systemformat formatierte Diskette wird mit CPABCGEN.COM bootfähig gemacht
# und der Rechner davon kalt gebootet (make_bootdisk.py, Exit 0 = "BOOTDISK OK").
# Diese Tests dauern LANGE (voller Boot je Schritt) → LABEL "format_integration";
# sie laufen bei den Standard-Regressionstests NICHT mit (tools/dev.sh test schließt
# sie via `-LE format_integration` aus; `tools/dev.sh test-format` führt nur sie aus).
# FORMAT_DRIVER/MK_DISK_TEMPLATE zeigen auf die Tools DIESES Build-Verzeichnisses.
set(_mkboot "${CMAKE_SOURCE_DIR}/tools/cpa_tools/make_bootdisk.py")
# Ein Boot-Disk-Test je Preset: erstellen+formatieren (mk_disk_template bzw.
# committete cpa780-Vorlage) → CPABCGEN → Kaltboot-Verify.
macro(add_bootdisk_test name preset)
    add_test(NAME ${name} COMMAND sh -c
        "FORMAT_DRIVER=$<TARGET_FILE:format_driver> MK_DISK_TEMPLATE=$<TARGET_FILE:mk_disk_template> python3 ${_mkboot} --preset ${preset} --quiet")
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 600 PASS_REGULAR_EXPRESSION "BOOTDISK OK"
        LABELS "system;slow;format_integration")
endmacro()

add_bootdisk_test(bootdisk_cpa780        cpa780)         # 5¼″-MFM   cpa780
add_bootdisk_test(bootdisk_mf3200_fmt7   mf3200_fmt7)    # 8″-SD/FM  26×128+16×256
add_bootdisk_test(bootdisk_mf6400_fmt1   mf6400_fmt1)    # 8″-DD Mischd. 26×128FM+8×1024MFM (C:)
add_bootdisk_test(bootdisk_k5600_10_fmt1 k5600_10_fmt1)  # 5¼″-40-SS 26×128+5×1024
add_bootdisk_test(bootdisk_k5600_20_fmt1 k5600_20_fmt1)  # 5¼″-80-SS 26×128+5×1024 (C:)
# HINWEIS: Preset mf3200_fmt1 (8″-SD/FM 26×128+4×1024) ist bewusst NICHT als Test
# registriert — die 1024-B-FM-Datensektoren werden korrekt geschrieben (CRC gültig),
# aber das laufende OS liest sie nicht (256-B-FM funktioniert): offener 1024-B-FM-SD-
# Lesepfad, seltenes Format.  Preset bleibt für manuelle Analyse verfügbar (§8.6).

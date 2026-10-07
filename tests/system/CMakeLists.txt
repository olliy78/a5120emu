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

# UDOS-4.3-Schreibpfad (origin/main 69b7445): 8212-Laufwerksauswahl und
# Sektorkontrollblock — langsam, daher dasselbe Label.
k1520_add_test(udos_format SRC test_udos_format.cpp LIBS ${_libs}
                           LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 600)

# k1520DiskTool <-> echtes UDOS: geschrieben wird mit UdosFileSystem, gelesen vom
# laufenden Betriebssystem (CAT / PRINT / STATUS).  Doc: doc/design/13_k1520disktool.md §15
k1520_add_test(disktool_udos_roundtrip SRC test_disktool_udos_roundtrip.cpp
                                       LIBS ${_libs} k1520_fs
                                       LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 600)

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
# FORMAT_DRIVER zeigt auf das Tool DIESES Build-Verzeichnisses.
set(_mkboot "${CMAKE_CURRENT_SOURCE_DIR}/drivers/make_bootdisk.py")
# Ein Boot-Disk-Test je Preset: Leerdiskette → FORMAT → CPABCGEN → Kaltboot-Verify.
# (Bis 2026-08-07 wurde hier zusätzlich MK_DISK_TEMPLATE gereicht — der Treiber legt
# die Diskette seit dem Medium-Umbau selbst unformatiert an und liest die Variable
# nicht mehr; mk_disk_template bleibt als Handwerkzeug bestehen.)
macro(add_bootdisk_test name preset)
    add_test(NAME ${name} COMMAND sh -c
        "FORMAT_DRIVER=$<TARGET_FILE:format_driver> python3 ${_mkboot} --preset ${preset} --quiet")
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

# ─── Format-Matrix: jeder FORMAT.COM-Menüeintrag jedes Laufwerkstyps ────────
# Die Matrix kommt per `--list-matrix` aus format_all.py selbst — dort stehen die
# Formattabellen, also wächst der Testsatz beim nächsten `cmake` automatisch mit.
# Eigenes LABEL "format_matrix": weder in der Standard-Regression noch in
# `dev.sh test-format` (das nur die Boot-Disk-Kette meint) → `dev.sh test-matrix`.
set(_fmtall "${CMAKE_CURRENT_SOURCE_DIR}/drivers/format_all.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_fmtall}")
execute_process(COMMAND python3 "${_fmtall}" --list-matrix
                OUTPUT_VARIABLE _fmt_matrix
                RESULT_VARIABLE  _fmt_matrix_rc
                OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _fmt_matrix_rc EQUAL 0)
    message(WARNING "format_all.py --list-matrix fehlgeschlagen (rc=${_fmt_matrix_rc})"
                    " — die Format-Matrix-Tests werden NICHT registriert")
else()
    string(REPLACE "\n" ";" _fmt_rows "${_fmt_matrix}")
    foreach(_row IN LISTS _fmt_rows)
        string(REPLACE "|" ";" _f "${_row}")
        list(GET _f 0 _boot)
        list(GET _f 1 _drv)
        list(GET _f 2 _geo)
        list(GET _f 3 _key)
        set(_geoarg "")
        set(_geotag "DS")
        if(NOT _geo STREQUAL "")
            set(_geoarg "--geo ${_geo}")
            set(_geotag "${_geo}")
        endif()
        add_test(NAME format_matrix_${_boot}_${_drv}_${_geotag}_${_key} COMMAND sh -c
            "D=$(mktemp -d); \
             FORMAT_DRIVER=$<TARGET_FILE:format_driver> python3 ${_fmtall} ${_key} \
               --boot ${_boot} --drive ${_drv} ${_geoarg} --outdir \$D; \
             r=\$?; rm -rf \$D; exit \$r")
        set_tests_properties(format_matrix_${_boot}_${_drv}_${_geotag}_${_key}
            PROPERTIES TIMEOUT 300
            FAIL_REGULAR_EXPRESSION "SPUR DEFEKT|FEHLER"
            LABELS "system;slow;format_matrix")
    endforeach()
    list(LENGTH _fmt_rows _fmt_n)
    message(STATUS "Format-Matrix: ${_fmt_n} Tests registriert (LABEL format_matrix)")
endif()


# ─── K8915 (doc/design/16_k8915.md §8a AP-E4f): FORMAT.COM + DISGEN.COM im /WAIT-Betrieb ──
# Die Kette Leerdiskette → FORMAT (80 Zylinder) → DISGEN → Kaltstart ist die TIEFE
# (format_integration, ≈ 500 Mio. Takte); jedes zweiseitige 80-Zylinder-Verfahren des
# FORMAT-Menüs auf Zylinder 00–02 die BREITE (format_matrix, wie die A5120-Matrix).
k1520_add_test(k8915_format SRC test_k8915_format.cpp
                            LIBS k1520_k8915 k1520_fs k1520_logger k1520_testsupport
                            LABELS "system;slow;format_integration" DEFS LOG_LEVEL=2 TIMEOUT 600)
k1520_add_test(k8915_format_verfahren SRC test_k8915_format_verfahren.cpp
                            LIBS k1520_k8915 k1520_logger k1520_testsupport
                            LABELS "system;slow;format_matrix" DEFS LOG_LEVEL=2 TIMEOUT 300)

# ─── SERTEST.COM (Entwurf 19 §14.8, AP-ST2): Serial Test unter CP/A und SCPX 8915 ──
# Erkennung + Schnittstellenliste, Kurzhilfe, Ergebniszeilen, Ctrl+C → bedienbar.
# Gerüst: sertest_hilfen.h.  Die eingecheckte .com kommt über K1520_SERTEST_COM;
# ob sie zur Quelle passt, prüft cli_sertest_com_passt_zur_quelle.
k1520_add_test(sertest SRC test_sertest.cpp
                       LIBS k1520_a5120 k1520_k8915 k1520_fs k1520_logger k1520_testsupport
                       LABELS "system;fast" DEFS LOG_LEVEL=2
                       K1520_SERTEST_COM="${CMAKE_SOURCE_DIR}/tools/sertest/sertest.com")
# AP-ST5: Tester gegen Gegenstelle auf zwei gekoppelten Maschinen (RFC 2217, Loopback).
# Ein schneller Fall (A5120 ↔ A5120 V.24) in der Standardregression, die übrigen
# (je zwei Kaltstarts, K8915, gemischt, Gegenfall) unter format_integration.
k1520_add_test(sertest_kopplung SRC test_sertest_kopplung.cpp
                       LIBS k1520_a5120 k1520_k8915 k1520_fs k1520_logger k1520_testsupport
                       LABELS "system;fast" DEFS LOG_LEVEL=2
                       K1520_SERTEST_COM="${CMAKE_SOURCE_DIR}/tools/sertest/sertest.com")
k1520_add_test(sertest_kopplung_lang SRC test_sertest_kopplung.cpp
                       LIBS k1520_a5120 k1520_k8915 k1520_fs k1520_logger k1520_testsupport
                       LABELS "system;slow;format_integration" DEFS LOG_LEVEL=2 SERTEST_KOPPLUNG_LANG=1
                       K1520_SERTEST_COM="${CMAKE_SOURCE_DIR}/tools/sertest/sertest.com" TIMEOUT 300)

# ─── PRG 710 / 710-1 (doc/design/20_prg710.md AP-P5f): FORMAT → bootfähige Diskette ──
# Die TIEFE: UDOS-FORMAT beider Seiten mit SYSTEMDISK? Y, Systemdateien kopieren, Kaltstart
# von der neuen Diskette; SCPX-FORMAT.COM 5 × 1024.  Die schnellen Fälle:
# tests/integration/test_prg710_format.cpp.
k1520_add_test(prg710_format_boot SRC test_prg710_format_boot.cpp
                            LIBS k1520_prg710 k1520_fs k1520_logger k1520_testsupport
                            LABELS "system;slow;format_integration" DEFS LOG_LEVEL=2 TIMEOUT 300)

# ─── PC 1715 (doc/design/21_pc1715.md AP-4f): Werkstest PCTEST.COM unter SCP 1715 mit
# Prüfsteckern (Loop am Hub), wie HARDY — Testzeit 1 min Maschinenzeit.
k1520_add_test(pc1715_pctest_scp SRC test_pc1715_pctest_scp.cpp
                            LIBS k1520_pc1715 k1520_fs k1520_logger k1520_testsupport
                            LABELS "system;slow;format_integration" DEFS LOG_LEVEL=2 TIMEOUT 600)

# ─── PC 1715 (doc/design/21_pc1715.md AP-5b): FORMAT unter SCP 1715 / CP/A 1715 ──
k1520_add_test(pc1715_format SRC test_pc1715_format.cpp
                            LIBS k1520_pc1715 k1520_fs k1520_logger k1520_testsupport
                            LABELS "system;slow;format_integration" DEFS LOG_LEVEL=2 TIMEOUT 600)

# ─── P8000 M3-Anfang (doc/design/25_p8000.md §10.11 AP P13d): WDC 4.2 an der 16-Bit-PIO2,
# sa.format + sa.verify von der WEGA-Startdiskette formatieren und prüfen eine TempPlatte
# (K5504.50, 5120 Spuren) — Minuten, daher format_integration.
set(_p8000_dir "P8000_FIXTURE_DIR=\"${CMAKE_SOURCE_DIR}/tests/fixtures/p8000\"")
k1520_add_test(p8000_sa_format SRC test_p8000_sa_format.cpp
                            LIBS k1520_p8000 k1520_fs k1520_logger k1520_testsupport
                            LABELS "system;slow;format_integration" DEFS LOG_LEVEL=2 ${_p8000_dir} TIMEOUT 3600)

# ─── P8000 M3 (doc/design/25_p8000.md AP P15): WEGA 3.0 auf die Platte installieren und von ihr
# starten.  Braucht die WEGA-Disketten außerhalb des Repos (sonst übersprungen) und rechnet
# Milliarden Takte — eigenes Label `wega_install` (NICHT in test/test-format; tools/dev.sh test-wega).
# Zwischenstände in ~/.cache/k1520emu/p8000_wega (tests/support/p8000_wega.h).
k1520_add_test(p8000_wega_install SRC test_p8000_wega_install.cpp
                            LIBS k1520_p8000 k1520_fs k1520_logger k1520_testsupport
                            LABELS "system;slow;wega_install" DEFS LOG_LEVEL=2 ${_p8000_dir} TIMEOUT 14400 RUN_SERIAL)

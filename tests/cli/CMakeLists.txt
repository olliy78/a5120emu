# ─────────────────────────────────────────────────────────────────────────────
# CLI-/Blackbox-Tests: die gebauten Werkzeuge als Prozess
# ─────────────────────────────────────────────────────────────────────────────
#
# Werkzeug starten, Sitzung einspielen, Ausgabe pruefen.  Deckt die Schicht ab,
# die die Header-Tests unter tests/debugtools/ nicht erreichen: Kommando-
# zerlegung, Argumentbehandlung, Exit-Codes, Zusammenbau der Maschine.
#
# Die Faelle stehen als Daten in cases/*.cli und werden von run_case.py
# ausgefuehrt — je Datei ein ctest-Fall, Name `cli_<dateiname ohne .cli>`.
# Einen Fall hinzufuegen heisst: eine .cli-Datei anlegen, sonst nichts.
#
# Warum nicht mehr im CMake (Stand bis 2026-08-07): die Faelle waren escapte
# Shell-Einzeiler mit CMake-Regex als Erwartung.  Sonderzeichen ueberlebten das
# doppelte Escaping nicht — im Code stand deshalb der Hinweis „regex uses '.'
# for literal []()*+".  Dazu 15 Kopien desselben mktemp/cp/rm-Rituals, das bei
# einem Abbruch Diskettenkopien liegenliess.  Format und Direktiven: Kopf von
# run_case.py.

find_package(Python3 COMPONENTS Interpreter QUIET)

if (NOT Python3_Interpreter_FOUND)
    message(STATUS "CLI-Tests NICHT registriert — kein python3 gefunden")
    return()
endif()

file(GLOB _cli_cases CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cases/*.cli")

# Beim Cross-Bau (tools/dev.sh win: MinGW nach Windows) sind die Werkzeuge
# `.exe` und laufen auf dem Linux-Rechner nur unter `wine`.  CMake weiss das
# bereits — gtest_discover_tests benutzt denselben Wert; ohne die Weitergabe an
# run_case.py starteten alle 59 CLI-Faelle die .exe direkt und scheiterten mit
# „Exec format error".  Leer im Normalfall, dann aendert sich nichts.
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
                --tool "z8kasm=$<TARGET_FILE:z8kasm>"
                --tool "k1520tape=$<TARGET_FILE:k1520tape>")
    set_tests_properties(cli_${_name} PROPERTIES TIMEOUT 120 LABELS "cli;fast")
endforeach()

list(LENGTH _cli_cases _cli_count)
message(STATUS "CLI-Tests: ${_cli_count} Faelle aus cases/*.cli")

# Waechter „eingecheckte .com passt zur Quelle" fuer die eigenen CP/M-Programme
# (SERTEST, ROMREAD, A5120.16-Pruefprogramme; Entwurf 19 §14.8): baut die Quelle in
# ein temporaeres Verzeichnis neu (M80 + LINKMT ueber cparun aus der CPA_Workbench,
# CPA_TOOLS; tools/cpm_bau.py) und vergleicht bytegleich.  Ohne Werkzeugkette — in
# der CI immer — Exit 77 = uebersprungen, mit Meldung.
# em256 braucht fuer die U8001-Firmware das z8kasm DIESES Baus — im Cross-Bau
# (MinGW) waere das eine .exe, die der Bau nicht ausfuehren kann; dort fehlt der
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

# SCPX-Pruefprogramm KMBGTEST (AP K8, mit KMBG.REL gebunden): .com passt zur Quelle.
add_test(NAME cli_kmbgtest_com_passt_zur_quelle
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/kassette/kmbgtest/build.py" --check)
set_tests_properties(cli_kmbgtest_com_passt_zur_quelle PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 LABELS "cli;fast")

# Kassetten-Bootprogramm BANDTEST (AP K7): .bin passt zur Quelle, das eingecheckte Bootband
# passt zur .bin (k1520tape bootband, bytegleich).
add_test(NAME cli_bandtest_bin_passt_zur_quelle
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/kassette/bandtest/build.py" --check)
set_tests_properties(cli_bandtest_bin_passt_zur_quelle PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 LABELS "cli;fast")
add_test(NAME cli_bandtest_bootband
    COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:k1520tape>
            -DBIN=${CMAKE_SOURCE_DIR}/tools/kassette/bandtest/bandtest.bin
            -DSOLL=${CMAKE_SOURCE_DIR}/tests/fixtures/kassette/bandtest_boot.k52
            -DTMP=${CMAKE_CURRENT_BINARY_DIR}/bandtest_boot_neu.k52
            -P ${CMAKE_CURRENT_SOURCE_DIR}/bandtest_bootband.cmake)
set_tests_properties(cli_bandtest_bootband PROPERTIES TIMEOUT 60 LABELS "cli;fast")
if(NOT CMAKE_CROSSCOMPILING)
    set_tests_properties(cli_em256_com_passt_zur_quelle PROPERTIES
        ENVIRONMENT "Z8KASM=$<TARGET_FILE:z8kasm>")
endif()

# Waechter „die Bootdisketten in disks/ tragen die aktuellen eigenen Programme"
# (SERTEST, ROMREAD, EM256ADR/EM16ABL/EM256FUL) und die Prueflinge unter
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

# Waechter „die beiden PRG-UDOS-Lieferdisketten in disks/ sind der Inhalt von
# tools/prg_disketten/“ (doc/udos_programme.md): Dateiliste, Seite, Inhalt und Systemspuren
# beider Disketten gegen den gemeinsamen Ordner.  Neu bauen:
#   python3 tools/prg_disketten/build.py --tool build/k1520disktool
add_test(NAME cli_prg_disketten
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/prg_disketten/build.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_prg_disketten PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Waechter „LBREAD.COM/LBPUNCH.COM sind aus tools/lochband/src gebaut“ (nur mit CP/M-Werkzeugkette,
# sonst uebersprungen, Rueckgabe 77).  Neu bauen: python3 tools/lochband/build.py
add_test(NAME cli_lochband_com
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/lochband/build.py" --check)
set_tests_properties(cli_lochband_com PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 LABELS "cli;fast")

# Waechter „die UDOS-Systemdiskette des A5120 in disks/ ist der Inhalt von tools/udos_a5120/“
# (doc/udos_programme.md §7).  Neu bauen:
#   python3 tools/udos_a5120/build.py --tool build/k1520disktool
add_test(NAME cli_udos_a5120
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/udos_a5120/build.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_udos_a5120 PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Waechter „die CP/A-Systemdiskette des A5120 in disks/ ist der Inhalt von tools/cpa_a5120/“
# (doc/disketten_bestand.md).  Zieht auch nach, wenn eine Beigabe (SERTEST, EM256*, RAF*, LB*)
# neu gebaut wurde.  Neu bauen:
#   python3 tools/cpa_a5120/build.py --tool build/k1520disktool
add_test(NAME cli_cpa_a5120
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/cpa_a5120/build.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_cpa_a5120 PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Waechter „die CP/A-Systemdiskette des PC 1715 in disks/ ist der Inhalt von tools/cpa_pc1715/“.
# Neu bauen: python3 tools/cpa_pc1715/build.py --tool build/k1520disktool
add_test(NAME cli_cpa_pc1715
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/cpa_pc1715/build.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_cpa_pc1715 PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Waechter „die SCPX-Systemdiskette des A5120 in disks/ ist der Inhalt von tools/scpx_a5120/“ (doc/scp_inventur.md).
# Neu bauen: python3 tools/scpx_a5120/build.py --tool build/k1520disktool
add_test(NAME cli_scpx_a5120
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/scpx_a5120/build.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_scpx_a5120 PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Waechter „die BCU-880-Disketten in disks/a5120_bcu/ sind die Wandlung ihrer Belege doc/a5120_alt/bcu/,
# das Demo-Bootband die Fixture, die BCU-Kassette formatiert“ (doc/a5120_alt/bcu.md §10).
# Neu bauen: python3 tools/bcu_a5120/build.py --tool build/k1520disktool
add_test(NAME cli_bcu_a5120
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/bcu_a5120/build.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_bcu_a5120 PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Waechter „die SCPX-8915-Systemdiskette des K8915 in disks/ ist der Inhalt von tools/scpx_k8915/“ (doc/scp_inventur.md).
# Neu bauen: python3 tools/scpx_k8915/build.py --tool build/k1520disktool
add_test(NAME cli_scpx_k8915
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/scpx_k8915/build.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_scpx_k8915 PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Waechter „die SCPX-Systemdisketten des PRG 710 und 710-1 in disks/ ist der Inhalt von tools/scpx_prg/“ (doc/scp_inventur.md).
# Neu bauen: python3 tools/scpx_prg/build.py --tool build/k1520disktool
add_test(NAME cli_scpx_prg
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/scpx_prg/build.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_scpx_prg PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Waechter „die SCP-/CP/Z-Systemdisketten des PC 1715 in disks/ ist der Inhalt von tools/scp_pc1715/“ (doc/scp_inventur.md).
# Neu bauen: python3 tools/scp_pc1715/build.py --tool build/k1520disktool
add_test(NAME cli_scp_pc1715
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/scp_pc1715/build.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_scp_pc1715 PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Waechter „die SCP-3.0-Systemdiskette des PC 1715W in disks/ ist der Inhalt von tools/scp_pc1715w/“ (doc/scp_inventur.md).
# Neu bauen: python3 tools/scp_pc1715w/build.py --tool build/k1520disktool
add_test(NAME cli_scp_pc1715w
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/scp_pc1715w/build.py"
            --check --tool "$<TARGET_FILE:k1520disktool>" ${_beigaben_emulator})
set_tests_properties(cli_scp_pc1715w PROPERTIES
    TIMEOUT 180 LABELS "cli;fast")

# Waechter „die kommentierten Boot-ROM-Listings des PRG 710/710-1 sind die Ausgabe des
# Generators" (AP-P0d, doc/design/20_prg710.md): tools/gen_prg710_zre_prn.py --check
# erzeugt beide Listings im Speicher, prueft MD5 der ROM-Abzuege, dass JEDES der 1024 Byte
# genau einmal und mit dem richtigen Wert dargestellt ist, dass jede benannte Routine als
# Label vorkommt, und vergleicht bytegleich mit doc/EPROMS/PRG710/*_zre.prn.
# Neu erzeugen: python3 tools/gen_prg710_zre_prn.py
add_test(NAME cli_prg710_zre_prn_passt_zur_quelle
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_prg710_zre_prn.py" --check)
set_tests_properties(cli_prg710_zre_prn_passt_zur_quelle PROPERTIES
    TIMEOUT 60 LABELS "cli;fast")

# Waechter „das kommentierte Listing des Betriebssystems MC80.21/22 ist die Ausgabe des
# Generators" (AP M10, doc/design/32_arbeitspakete_kassette_a5120alt_mc80.md): prueft MD5 der
# Abzuege doc/EPROMS/MC80/mc8020/, dass jedes der 4096 Byte (0000-0BFFH, 2000-23FFH) genau einmal
# und mit dem richtigen Wert dargestellt ist, dass jede Marke/jeder Kommentar der Abschrift
# tools/mc80/bsys_2x_abschrift.txt an einem Befehlsanfang liegt, und vergleicht bytegleich mit
# doc/mc80/bsys_2x.prn.  Neu erzeugen: python3 tools/gen_mc8020_bsys_prn.py
add_test(NAME cli_mc8020_bsys_prn_passt_zur_quelle
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_mc8020_bsys_prn.py" --check)
set_tests_properties(cli_mc8020_bsys_prn_passt_zur_quelle PROPERTIES
    TIMEOUT 60 LABELS "cli;fast")

# Waechter „die kommentierten Listings der K8915-Gen-2-Abzuege (ZRE 175/176/177 und die zwei
# belegten Chips der 2708-Karte PFS K3820) sind die Ausgabe des Generators" (AP-V3a,
# doc/design/24_k8915_varianten.md): tools/gen_k8915g2_prn.py --check prueft die MD5 aller
# Abzuege gegen doc/EPROMS/K8915G2/MD5SUMS (auch die abgeleiteten Verkettungen), dass JEDES
# der 1024 Byte je Baustein genau einmal und mit dem richtigen Wert dargestellt ist, die
# 24-Bit-Pruefsummen der Chips, dass jede benannte Routine als Label vorkommt, und vergleicht
# bytegleich mit doc/EPROMS/K8915G2/k8915g2_{zre,pfs3820}.prn.  Wird ein Abzug veraendert, wird
# der Waechter rot.  Neu erzeugen: python3 tools/gen_k8915g2_prn.py
add_test(NAME cli_k8915g2_prn_passt_zur_quelle
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_k8915g2_prn.py" --check)
set_tests_properties(cli_k8915g2_prn_passt_zur_quelle PROPERTIES
    TIMEOUT 60 LABELS "cli;fast")

# Waechter „das kommentierte EGOS-Listing des MC80.3x ist die Ausgabe des Generators" (AP M1,
# doc/mc80/egos.md): tools/gen_mc80_egos_prn.py --check prueft die MD5 der sechs MAME-Abzuege
# (doc/EPROMS/MC80/mc8030_mame/), dass JEDES der 10240 Byte genau einmal und mit dem richtigen
# Wert dargestellt ist, dass jeder Einsprung der EGOS-Softwareschnittstellen (Sprungleisten
# 0BB5H und 2004H) benannt ist, die ROM-Kette ab 0AFEH, und vergleicht bytegleich mit
# doc/mc80/egos.prn.  Neu erzeugen: python3 tools/gen_mc80_egos_prn.py
add_test(NAME cli_mc80_egos_prn_passt_zur_quelle
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_mc80_egos_prn.py" --check)
set_tests_properties(cli_mc80_egos_prn_passt_zur_quelle PROPERTIES
    TIMEOUT 60 LABELS "cli;fast")

# Zeitgrenzen der add_test()-Faelle × K1520_TEST_ZEIT_FAKTOR (tests/cmake/K1520AddTest.cmake)
k1520_test_zeiten_skalieren()

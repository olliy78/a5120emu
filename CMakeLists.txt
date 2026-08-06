cmake_minimum_required(VERSION 3.16)
project(a5120emu VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default-Build-Type: OHNE dies baut GCC mit -O0 (keine Optimierung) — für einen
# Z80-Interpreter der mit Abstand größte Performance-Verlust (~5-8x langsamer).
# Wird nur gesetzt, wenn der Aufrufer keinen expliziten Typ vorgibt.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Release/RelWithDebInfo/Debug)" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE nicht gesetzt → Release (-O3)")
endif()

if(NOT LOG_LEVEL)
    set(LOG_LEVEL 3)
endif()
add_compile_definitions(LOG_LEVEL=${LOG_LEVEL})
message(STATUS "Global LOG_LEVEL=${LOG_LEVEL}")

# Auto-detect SDL2 (can be overridden with -DUSE_SDL=OFF)
option(USE_SDL "Build with SDL2 graphical terminal support" ON)

# Core emulator sources
set(SOURCES
    src/z80.cpp
    src/memory.cpp
    src/floppy.cpp
    src/cpa_bios.cpp
    src/terminal_ansi.cpp
    src/a5120emu.cpp
)

set(HEADERS
    src/z80.h
    src/memory.h
    src/floppy.h
    src/cpa_bios.h
    src/terminal.h
    src/font8x8.h
)

# Main executable
add_executable(a5120emu ${SOURCES} ${HEADERS})

if(USE_SDL)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        target_sources(a5120emu PRIVATE src/terminal_sdl.cpp)
        target_compile_definitions(a5120emu PRIVATE USE_SDL)
        target_link_libraries(a5120emu PRIVATE SDL2::SDL2)
        message(STATUS "SDL2 found - graphical terminal enabled")
    else()
        message(STATUS "SDL2 not found - building without graphical terminal")
    endif()
else()
    message(STATUS "SDL2 disabled by user")
endif()

# cparun: CP/M command-line runner (no SDL, no disk images, direct host filesystem)
add_executable(cparun
    src/cparun.cpp
    src/cpm_bdos.cpp
    src/z80.cpp
    src/memory.cpp
)
target_include_directories(cparun PRIVATE src)

# Test executable
add_executable(a5120emu_test
    src/z80.cpp
    src/memory.cpp
    src/floppy.cpp
    tests/test_main.cpp
)
target_include_directories(a5120emu_test PRIVATE src)

# Install (legacy)
install(TARGETS a5120emu cparun RUNTIME DESTINATION bin)

# ─────────────────────────────────────────────────────────────────────────────
# K1520 core emulator (new modular implementation)
# ─────────────────────────────────────────────────────────────────────────────

option(BUILD_K1520 "Build new K1520 modular emulator" ON)

if(BUILD_K1520)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    add_library(k1520_logger STATIC
        core/logger.cpp
    )
    target_include_directories(k1520_logger PUBLIC ${CMAKE_SOURCE_DIR})

    # Bus library
    add_library(k1520_bus STATIC
        core/bus/k1520_bus.cpp
    )
    target_include_directories(k1520_bus PUBLIC ${CMAKE_SOURCE_DIR})
    target_link_libraries(k1520_bus PUBLIC k1520_logger)

    # Minimaler YAML-Subset-Parser (Konfigurationsdateien des Kerns, §8.6.2)
    add_library(k1520_util STATIC
        core/util/yaml_lite.cpp
    )
    target_include_directories(k1520_util PUBLIC ${CMAKE_SOURCE_DIR})

    # Floppy peripheral: Diskettenformat-Datenmodell (DiskFormat/TrackFormat)
    add_library(k1520_floppy STATIC
        core/peripherals/floppy_drive/disk_format.cpp
    )
    target_include_directories(k1520_floppy PUBLIC ${CMAKE_SOURCE_DIR})

    # Neue Peripherie-Schicht: Track-Bild-Abstraktion + FM/MFM-Codec + Drive/Image
    add_library(k1520_floppy2 STATIC
        core/peripherals/floppy_drive/track_image.cpp
        core/peripherals/floppy_drive/track_codec.cpp
        core/peripherals/floppy_drive/bit_codec.cpp
        core/peripherals/floppy_drive/drive_profile.cpp
        core/peripherals/floppy_drive/disk_medium.cpp
        core/peripherals/floppy_drive/img_codec.cpp
        core/peripherals/floppy_drive/hfe_codec.cpp
        core/peripherals/floppy_drive/dmk_codec.cpp
        core/peripherals/floppy_drive/image_codec.cpp
        core/peripherals/floppy_drive/disk_image.cpp
        core/peripherals/floppy_drive/floppy_drive2.cpp
        core/peripherals/floppy_drive/format_catalog.cpp
    )
    target_link_libraries(k1520_floppy2 PUBLIC k1520_floppy k1520_util)
    target_include_directories(k1520_floppy2 PUBLIC ${CMAKE_SOURCE_DIR})

    # Fallback-Fundort des Diskettenformat-Katalogs (§8.6.4, Priorität 1).  Zeigt im
    # Entwicklungsbaum auf die Quelldatei, damit ctest und die Tools den Katalog ohne
    # gesetzte Umgebungsvariable finden.
    set(K1520_FORMATS_DEFAULT "${CMAKE_SOURCE_DIR}/data/formats.yaml"
        CACHE FILEPATH "Fallback-Pfad der formats.yaml")
    target_compile_definitions(k1520_floppy2
        PRIVATE K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")

    # PIO primitive
    add_library(k1520_pio STATIC
        core/primitives/z80_pio.cpp
    )
    target_include_directories(k1520_pio PUBLIC ${CMAKE_SOURCE_DIR})

    # CTC primitive
    add_library(k1520_ctc STATIC
        core/primitives/z80_ctc.cpp
    )
    target_include_directories(k1520_ctc PUBLIC ${CMAKE_SOURCE_DIR})

    # SIO primitive
    add_library(k1520_sio STATIC
        core/primitives/z80_sio.cpp
    )
        # Z80 CPU primitive for the modular core
        add_library(k1520_z80 STATIC
            core/primitives/z80.cpp
        )
        target_include_directories(k1520_z80 PUBLIC ${CMAKE_SOURCE_DIR})

    target_link_libraries(k1520_sio PUBLIC k1520_bus)
    target_include_directories(k1520_sio PUBLIC ${CMAKE_SOURCE_DIR})

    # K3526 OPS – Operationsspeicher (64KB DRAM card)
    add_library(k1520_k3526 STATIC core/cards/k3526/k3526.cpp)
    target_include_directories(k1520_k3526 PUBLIC ${CMAKE_SOURCE_DIR})

    # K7637 serial keyboard peripheral
    add_library(k1520_k7637 STATIC
        core/peripherals/k7637/k7637.cpp
    )
    target_link_libraries(k1520_k7637 PUBLIC k1520_sio)
    target_include_directories(k1520_k7637 PUBLIC ${CMAKE_SOURCE_DIR})

    # K7024 ABS – Abbildsteuerung Bildschirm (screen controller card)
    add_library(k1520_k7024 STATIC core/cards/k7024/k7024.cpp)
    target_link_libraries(k1520_k7024 PUBLIC k1520_bus)
    target_include_directories(k1520_k7024 PUBLIC ${CMAKE_SOURCE_DIR})

    # K2526 ZRE – Zentrale Recheneinheit (CPU card, enthält jetzt ZVE1)
    add_library(k1520_k2526 STATIC core/cards/k2526/k2526.cpp)
    target_link_libraries(k1520_k2526 PUBLIC k1520_z80 k1520_pio k1520_ctc k1520_bus)
    target_include_directories(k1520_k2526 PUBLIC ${CMAKE_SOURCE_DIR})

    # K8025 ASS – Anschlußsteuerung Seriell (serial interface card)
    add_library(k1520_k8025 STATIC core/cards/k8025/k8025.cpp)
    target_link_libraries(k1520_k8025 PUBLIC k1520_sio k1520_ctc k1520_pio k1520_bus)
    target_include_directories(k1520_k8025 PUBLIC ${CMAKE_SOURCE_DIR})

    # K5122 AFS – Anschlusssteuerung Floppy-disk Speicher (formatagnostischer
    # Streaming-Controller, TrackImage/DiskImage-basiert; bootet CP/A inkl. HFE)
    add_library(k1520_k5122 STATIC core/cards/k5122/k5122.cpp)
    target_link_libraries(k1520_k5122 PUBLIC k1520_pio k1520_floppy2 k1520_bus k1520_logger)
    target_include_directories(k1520_k5122 PUBLIC ${CMAKE_SOURCE_DIR})

    # A5120 machine (wires all cards + Z80 CPU)
    add_library(k1520_a5120 STATIC
        core/machines/a5120/a5120.cpp
    )
    target_link_libraries(k1520_a5120 PUBLIC
        k1520_z80
        k1520_k2526 k1520_k3526 k1520_k7024 k1520_k8025 k1520_k5122
        k1520_k7637 k1520_floppy k1520_floppy2 k1520_bus k1520_logger
    )
    target_include_directories(k1520_a5120 PUBLIC ${CMAKE_SOURCE_DIR})

    # libk1520core.so — stable C-API shared library
    add_library(k1520core SHARED
        core/api/k1520_api.cpp
    )
    target_link_libraries(k1520core PRIVATE k1520_a5120 k1520_logger)
    target_include_directories(k1520core PUBLIC ${CMAKE_SOURCE_DIR})
    set_target_properties(k1520core PROPERTIES
        CXX_STANDARD 17
        POSITION_INDEPENDENT_CODE ON
    )

    option(BUILD_K1520_TESTS "Build K1520 GoogleTest unit tests" ON)

    if(BUILD_K1520_TESTS)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)

        enable_testing()

        add_executable(k1520_test_pio
            tests/cpp/test_pio.cpp
        )
        target_link_libraries(k1520_test_pio PRIVATE k1520_pio GTest::gtest_main)
        target_include_directories(k1520_test_pio PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_bus
            tests/cpp/test_bus.cpp
        )
        target_link_libraries(k1520_test_bus PRIVATE k1520_bus GTest::gtest_main)
        target_include_directories(k1520_test_bus PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_yaml_lite tests/cpp/test_yaml_lite.cpp)
        target_link_libraries(k1520_test_yaml_lite PRIVATE k1520_util GTest::gtest_main)
        target_include_directories(k1520_test_yaml_lite PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_format_catalog tests/cpp/test_format_catalog.cpp)
        target_link_libraries(k1520_test_format_catalog PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_format_catalog PRIVATE ${CMAKE_SOURCE_DIR})
        # Katalogpfad auch dem Test bekannt machen (prüft die ausgelieferte data/formats.yaml)
        target_compile_definitions(k1520_test_format_catalog
            PRIVATE K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")

        add_executable(k1520_test_track_codec tests/cpp/test_track_codec.cpp)
        target_link_libraries(k1520_test_track_codec PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_track_codec PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_bit_codec tests/cpp/test_bit_codec.cpp)
        target_link_libraries(k1520_test_bit_codec PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_bit_codec PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_drive_profile tests/cpp/test_drive_profile.cpp)
        target_link_libraries(k1520_test_drive_profile PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_drive_profile PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_disk_medium tests/cpp/test_disk_medium.cpp)
        target_link_libraries(k1520_test_disk_medium PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_disk_medium PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_img_codec tests/cpp/test_img_codec.cpp)
        target_link_libraries(k1520_test_img_codec PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_img_codec PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_dmk_codec tests/cpp/test_dmk_codec.cpp)
        target_link_libraries(k1520_test_dmk_codec PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_dmk_codec PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_disk_image tests/cpp/test_disk_image.cpp)
        target_link_libraries(k1520_test_disk_image PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_disk_image PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_floppy_drive2 tests/cpp/test_floppy_drive2.cpp)
        target_link_libraries(k1520_test_floppy_drive2 PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_floppy_drive2 PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_hfe_codec tests/cpp/test_hfe_codec.cpp)
        target_link_libraries(k1520_test_hfe_codec PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_hfe_codec PRIVATE ${CMAKE_SOURCE_DIR})
        target_compile_definitions(k1520_test_hfe_codec PRIVATE
            FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

        add_executable(k1520_test_ctc
            tests/cpp/test_ctc.cpp
        )
        target_link_libraries(k1520_test_ctc PRIVATE k1520_ctc GTest::gtest_main)
        target_include_directories(k1520_test_ctc PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_sio
            tests/cpp/test_sio.cpp
        )
        target_link_libraries(k1520_test_sio PRIVATE k1520_sio GTest::gtest_main)
        target_include_directories(k1520_test_sio PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_k3526 tests/cpp/test_k3526.cpp)
        target_link_libraries(k1520_test_k3526 PRIVATE k1520_k3526 k1520_bus GTest::gtest_main)
        target_include_directories(k1520_test_k3526 PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_k7637 tests/cpp/test_k7637.cpp)
        target_link_libraries(k1520_test_k7637 PRIVATE k1520_k7637 k1520_sio k1520_bus GTest::gtest_main)
        target_include_directories(k1520_test_k7637 PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_k7024 tests/cpp/test_k7024.cpp)
        target_link_libraries(k1520_test_k7024 PRIVATE k1520_k7024 k1520_bus GTest::gtest_main)
        target_include_directories(k1520_test_k7024 PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_k2526 tests/cpp/test_k2526.cpp)
        target_link_libraries(k1520_test_k2526 PRIVATE k1520_k2526 k1520_k3526 k1520_k5122 k1520_floppy GTest::gtest_main)
        target_include_directories(k1520_test_k2526 PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_k8025 tests/cpp/test_k8025.cpp)
        target_link_libraries(k1520_test_k8025 PRIVATE k1520_k8025 GTest::gtest_main)
        target_include_directories(k1520_test_k8025 PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_k5122 tests/cpp/test_k5122.cpp)
        target_link_libraries(k1520_test_k5122 PRIVATE k1520_k5122 GTest::gtest_main)
        target_include_directories(k1520_test_k5122 PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_z80 tests/cpp/test_z80.cpp)
        target_link_libraries(k1520_test_z80 PRIVATE k1520_z80 GTest::gtest_main)
        target_include_directories(k1520_test_z80 PRIVATE ${CMAKE_SOURCE_DIR})

        # Header-only MACRO-80 .prn listing parser (tools/prn_listing.h)
        add_executable(k1520_test_prn_listing tests/cpp/test_prn_listing.cpp)
        target_link_libraries(k1520_test_prn_listing PRIVATE GTest::gtest_main)
        target_include_directories(k1520_test_prn_listing PRIVATE ${CMAKE_SOURCE_DIR})

        # Header-only MACRO-80 .MAC/.ASM source assembler + offset matcher (tools/mac_listing.h)
        add_executable(k1520_test_mac_listing tests/cpp/test_mac_listing.cpp)
        target_link_libraries(k1520_test_mac_listing PRIVATE GTest::gtest_main)
        target_include_directories(k1520_test_mac_listing PRIVATE ${CMAKE_SOURCE_DIR})

        # Header-only call-stack tracker for the history backtrace (tools/callstack_tracker.h)
        add_executable(k1520_test_callstack_tracker tests/cpp/test_callstack_tracker.cpp)
        target_link_libraries(k1520_test_callstack_tracker PRIVATE GTest::gtest_main)
        target_include_directories(k1520_test_callstack_tracker PRIVATE ${CMAKE_SOURCE_DIR})

        # Header-only coverage CSV parser + run-diff (tools/coverage_diff.h)
        add_executable(k1520_test_coverage_diff tests/cpp/test_coverage_diff.cpp)
        target_link_libraries(k1520_test_coverage_diff PRIVATE GTest::gtest_main)
        target_include_directories(k1520_test_coverage_diff PRIVATE ${CMAKE_SOURCE_DIR})

        # Header-only k1520dbg command list + tab-completion matcher (tools/dbg_commands.h)
        add_executable(k1520_test_dbg_commands tests/cpp/test_dbg_commands.cpp)
        target_link_libraries(k1520_test_dbg_commands PRIVATE GTest::gtest_main)
        target_include_directories(k1520_test_dbg_commands PRIVATE ${CMAKE_SOURCE_DIR})

        # Header-only expression evaluator for k1520dbg (tools/expr_eval.h)
        add_executable(k1520_test_expr_eval tests/cpp/test_expr_eval.cpp)
        target_link_libraries(k1520_test_expr_eval PRIVATE GTest::gtest_main)
        target_include_directories(k1520_test_expr_eval PRIVATE ${CMAKE_SOURCE_DIR})

        # Header-only boot_trace --until condition parser/evaluator (tools/until_cond.h)
        add_executable(k1520_test_until_cond tests/cpp/test_until_cond.cpp)
        target_link_libraries(k1520_test_until_cond PRIVATE GTest::gtest_main)
        target_include_directories(k1520_test_until_cond PRIVATE ${CMAKE_SOURCE_DIR})

        # Header-only event-breakpoint classifier for k1520dbg (tools/event_bp.h)
        add_executable(k1520_test_event_bp tests/cpp/test_event_bp.cpp)
        target_link_libraries(k1520_test_event_bp PRIVATE GTest::gtest_main)
        target_include_directories(k1520_test_event_bp PRIVATE ${CMAKE_SOURCE_DIR})

        # Header-only memory-watchpoint matcher for k1520dbg (tools/mem_watch.h)
        add_executable(k1520_test_mem_watch tests/cpp/test_mem_watch.cpp)
        target_link_libraries(k1520_test_mem_watch PRIVATE GTest::gtest_main)
        target_include_directories(k1520_test_mem_watch PRIVATE ${CMAKE_SOURCE_DIR})

        # Machine snapshot / reverse-debugging (A5120Machine::captureState/restoreState)
        add_executable(k1520_test_machine_snapshot tests/cpp/test_machine_snapshot.cpp)
        target_link_libraries(k1520_test_machine_snapshot
            PRIVATE k1520_a5120 k1520_floppy k1520_logger GTest::gtest_main)
        target_include_directories(k1520_test_machine_snapshot PRIVATE ${CMAKE_SOURCE_DIR})

        # Disketten-API auf Maschinenebene (Formatkatalog-Verdrahtung, §8.6)
        add_executable(k1520_test_a5120_disk_api tests/cpp/test_a5120_disk_api.cpp)
        target_link_libraries(k1520_test_a5120_disk_api
            PRIVATE k1520_a5120 k1520_floppy k1520_logger GTest::gtest_main)
        target_include_directories(k1520_test_a5120_disk_api PRIVATE ${CMAKE_SOURCE_DIR})

        # Full-machine boot integration test (ZVE1+ZVE2 + K5122 DMA end-to-end).
        # Quiet logging (WARN) keeps the multi-million-cycle boot run readable.
        add_executable(k1520_test_boot_integration tests/cpp/test_boot_integration.cpp)
        target_link_libraries(k1520_test_boot_integration
            PRIVATE k1520_a5120 k1520_floppy k1520_logger GTest::gtest_main)
        target_include_directories(k1520_test_boot_integration PRIVATE ${CMAKE_SOURCE_DIR})
        target_compile_definitions(k1520_test_boot_integration PRIVATE
            LOG_LEVEL=2
            A5120_TEST_DISK_DIR="${CMAKE_SOURCE_DIR}/disks")

        include(GoogleTest)
        gtest_discover_tests(k1520_test_pio)

        gtest_discover_tests(k1520_test_bus)
        gtest_discover_tests(k1520_test_yaml_lite)
        gtest_discover_tests(k1520_test_format_catalog)
        gtest_discover_tests(k1520_test_track_codec)
        gtest_discover_tests(k1520_test_bit_codec)
        gtest_discover_tests(k1520_test_drive_profile)
        gtest_discover_tests(k1520_test_disk_medium)
        gtest_discover_tests(k1520_test_img_codec)
        gtest_discover_tests(k1520_test_dmk_codec)
        gtest_discover_tests(k1520_test_disk_image)
        gtest_discover_tests(k1520_test_floppy_drive2)
        gtest_discover_tests(k1520_test_hfe_codec)
        gtest_discover_tests(k1520_test_ctc)
        gtest_discover_tests(k1520_test_sio)
        gtest_discover_tests(k1520_test_k3526)
        gtest_discover_tests(k1520_test_k7637)
        gtest_discover_tests(k1520_test_k7024)
        gtest_discover_tests(k1520_test_k2526)
        gtest_discover_tests(k1520_test_k8025)
        gtest_discover_tests(k1520_test_k5122)
        gtest_discover_tests(k1520_test_z80)
        gtest_discover_tests(k1520_test_prn_listing)
        gtest_discover_tests(k1520_test_mac_listing)
        gtest_discover_tests(k1520_test_callstack_tracker)
        gtest_discover_tests(k1520_test_coverage_diff)
        gtest_discover_tests(k1520_test_dbg_commands)
        gtest_discover_tests(k1520_test_expr_eval)
        gtest_discover_tests(k1520_test_until_cond)
        gtest_discover_tests(k1520_test_event_bp)
        gtest_discover_tests(k1520_test_mem_watch)
        gtest_discover_tests(k1520_test_machine_snapshot)
        gtest_discover_tests(k1520_test_a5120_disk_api)
        gtest_discover_tests(k1520_test_boot_integration PROPERTIES TIMEOUT 60)

        # Langsamer SCPX-INIT.COM-Formatier-Guard (~35 s, 160 Spuren) in eigenem
        # Executable → per Label "format_integration" aus dem Default-Lauf (via
        # `-LE format_integration`) ausgeschlossen; läuft mit `dev.sh test-format`.
        add_executable(k1520_test_scpx_init tests/cpp/test_scpx_init.cpp)
        target_link_libraries(k1520_test_scpx_init
            PRIVATE k1520_a5120 k1520_floppy k1520_logger GTest::gtest_main)
        target_include_directories(k1520_test_scpx_init PRIVATE ${CMAKE_SOURCE_DIR})
        target_compile_definitions(k1520_test_scpx_init PRIVATE
            LOG_LEVEL=2
            A5120_TEST_DISK_DIR="${CMAKE_SOURCE_DIR}/disks")
        gtest_discover_tests(k1520_test_scpx_init
            PROPERTIES TIMEOUT 120 LABELS "format_integration")

        # UDOS-FORMAT-Guard (Boot + 77 Spuren formatieren/verifizieren, ~20 s) —
        # sichert die 8212-Laufwerksauswahl und den Sektorkontrollblock im
        # Schreibpfad ab.  Ebenfalls Label „format_integration".
        add_executable(k1520_test_udos_format tests/cpp/test_udos_format.cpp)
        target_link_libraries(k1520_test_udos_format
            PRIVATE k1520_a5120 k1520_floppy k1520_logger GTest::gtest_main)
        target_include_directories(k1520_test_udos_format PRIVATE ${CMAKE_SOURCE_DIR})
        target_compile_definitions(k1520_test_udos_format PRIVATE
            LOG_LEVEL=2
            A5120_TEST_DISK_DIR="${CMAKE_SOURCE_DIR}/disks")
        gtest_discover_tests(k1520_test_udos_format
            PROPERTIES TIMEOUT 600 LABELS "format_integration")

        # HARDY-Hardware-Testprogramm: „Rechner"-Test läuft clean durch (Guard für
        # den /MEMDI- + BS-PIO-Bus-Strobe-Fix).  Ebenfalls langsam (Boot + Testlauf)
        # → Label „format_integration“, läuft mit `dev.sh test-format` / `test-all`.
        add_executable(k1520_test_hardy tests/cpp/test_hardy.cpp)
        target_link_libraries(k1520_test_hardy
            PRIVATE k1520_a5120 k1520_floppy k1520_logger GTest::gtest_main)
        target_include_directories(k1520_test_hardy PRIVATE ${CMAKE_SOURCE_DIR})
        target_compile_definitions(k1520_test_hardy PRIVATE
            LOG_LEVEL=2
            A5120_TEST_DISK_DIR="${CMAKE_SOURCE_DIR}/disks")
        gtest_discover_tests(k1520_test_hardy
            PROPERTIES TIMEOUT 180 LABELS "format_integration")
    endif() # BUILD_K1520_TESTS

    # boot_trace: standalone boot diagnostic tool (LOG_LEVEL=5 for full trace)
    add_executable(boot_trace tools/boot_trace.cpp)
    target_link_libraries(boot_trace PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(boot_trace PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(boot_trace PRIVATE LOG_LEVEL=5)

    # bench_run: fixed-workload Performance-Benchmark (Mcycles/s, x-Echtzeit)
    add_executable(bench_run tools/bench_run.cpp)
    target_link_libraries(bench_run PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(bench_run PRIVATE ${CMAKE_SOURCE_DIR})

    # kbd_test: standalone keyboard smoke test (boot, type a command, dump screen)
    add_executable(kbd_test tools/kbd_test.cpp)
    target_link_libraries(kbd_test PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(kbd_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(kbd_test PRIVATE LOG_LEVEL=3)

    # format_driver: scripted two-disk interactive driver (FORMAT.COM / FORMATB.COM)
    # Für DEBUG/TRACE aus build_trace/ laufen lassen (dort ist der ganze Build inkl.
    # Bibliotheken LOG_LEVEL=5); FD_GATE/FD_PCGATE + FD_LOGLEVEL öffnen zur Laufzeit
    # ein gezieltes Fenster.  In build/ (LOG_LEVEL=3) sind DEBUG/TRACE wegkompiliert.
    add_executable(format_driver tools/format_driver.cpp)
    target_link_libraries(format_driver PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(format_driver PRIVATE ${CMAKE_SOURCE_DIR})

    # mk_disk_template: erzeugt gültig vorformatierte EINSEITIGE Leerdisketten (HFE)
    # für die Boot-Pipeline der einseitigen Formate (MF3200 8″-FM, MF6400 8″-MFM,
    # K5600.10/.20 5″-SS) — FORMAT.COM kann eine gap-leere .hfe nicht direkt formatieren
    # (Gap-Blank-Hänger, docs/format.md §8.2/§8.6).
    add_executable(mk_disk_template tools/mk_disk_template.cpp)
    target_link_libraries(mk_disk_template PRIVATE k1520_floppy2 k1520_logger)
    target_include_directories(mk_disk_template PRIVATE ${CMAKE_SOURCE_DIR})

    # floppy_diag: running-OS floppy read diagnostic
    add_executable(floppy_diag tools/floppy_diag.cpp)
    target_link_libraries(floppy_diag PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(floppy_diag PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(floppy_diag PRIVATE LOG_LEVEL=3)

    # k1520dbg: interactive gdb-style debugger (breakpoints, marker-relative cycles,
    # memory dump/disasm, watchpoints, keystroke injection, screen view)
    add_executable(k1520dbg tools/k1520dbg.cpp)
    target_link_libraries(k1520dbg PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(k1520dbg PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(k1520dbg PRIVATE LOG_LEVEL=5)
    # Optional GNU readline: interactive line editing, history, command tab-completion.
    # If not found, k1520dbg falls back to plain getline (unchanged behaviour).
    find_path(READLINE_INCLUDE_DIR readline/readline.h)
    find_library(READLINE_LIBRARY readline)
    if (READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
        message(STATUS "k1520dbg: GNU readline found — interactive editing/history/completion enabled")
        target_compile_definitions(k1520dbg PRIVATE HAVE_READLINE)
        target_include_directories(k1520dbg PRIVATE ${READLINE_INCLUDE_DIR})
        target_link_libraries(k1520dbg PRIVATE ${READLINE_LIBRARY})
    else()
        message(STATUS "k1520dbg: GNU readline not found — using plain getline (no history/completion)")
    endif()

    # ─── End-to-end (black-box) CLI tests ──────────────────────────────────────
    # Run the built tool with a scripted session and match the output. This exercises
    # the full stack (command dispatch + machine wiring), which the header-level unit
    # tests don't reach. Each disk-using test copies the fixture to a PRIVATE temp file
    # first — the tools mount r/w and a run can otherwise corrupt the committed image.
    if (BUILD_K1520_TESTS)
        set(_bbdisk "${CMAKE_SOURCE_DIR}/disks/cpadisk_autofs_clock_noautoexec.img")
        # add_cli_test(<name> <pass-regex> <shell-command>)
        macro(add_cli_test name regex cmd)
            add_test(NAME ${name} COMMAND sh -c "${cmd}")
            set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}" TIMEOUT 120)
        endmacro()

        # k1520dbg — scripted sessions piped on stdin (temp disk copy).
        # NB: regex uses '.' for literal []()*+ — escaped specials don't survive the macro.
        add_cli_test(cli_dbg_breakpoint "bp ZVE1 : ZVE1 PC=0135"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ng\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_watchpoint "WR .6005.=99"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'wp 0x6005 == 99\\ne 0x6005 99\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_expr_eval "= 10 .0xA."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ndisp 2*3+4\\ng\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # Erster angenommener Interrupt ist die ISR der Speicher-Ausbaumessung des
        # Lade-ROMs (007AH, ausgelöst vom /WR-Strobe des Testschreibens) — lange vor
        # dem Floppy-Index-IRQ (01C7H).
        add_cli_test(cli_dbg_event_bp "ISR 007A"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'bint\\ng\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_reverse_step "restored 1 step"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ng\\ns 1\\nrs\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # feature_request_interactive_debug: screen-konditioniertes Laufen, Screen-Suche,
        # Mehr-Byte-Examine (#4) — Wortlaut = die Meldezeilen der Kommandos.
        add_cli_test(cli_dbg_gscreen "screen matched after [0-9]+ cyc"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'gscreen \"Bootloader\" 8000000\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_screen_find "found at row [0-9]+ col [0-9]+"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'gscreen \"Bootloader\" 8000000\\nscreen find \"Bootloader\"\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # x <A> N (ohne /N-Spec) muss N Bytes dumpen — der behobene #4-Bug (gab nur 1 Byte).
        add_cli_test(cli_dbg_examine_count "6000: 41 42 43 44"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'e 0x6000 41 42 43 44\\nx 0x6000 4\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")

        # feature_request_source_annotation: Fremdquellen-Annotation, Datei↔RAM-Abgleich,
        # IM-2-Vektortabelle, alle drei PIOs, Uhrenwahl.
        # `.MAC`-Quelltext (ORG/Opcode-Längen) wird assembliert und annotiert die Disasm.
        add_cli_test(cli_dbg_lst_mac "1002: E6 0F +AND 0FH +; AND 0FH"
            "D=$(mktemp); M=$(mktemp --suffix=.mac); cp ${_bbdisk} \$D; printf '\\tORG\\t1000H\\n\\tLD\\tA,05H\\n\\tAND\\t0FH\\n' > \$M; printf 'e 0x1000 3E 05 E6 0F\\nlst %s\\nu 0x1000 2\\nq\\n' \$M | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D \$M")
        # dump A N datei  →  verify datei @A  muss 100 % identisch sein (Rundlauf).
        add_cli_test(cli_dbg_verify_roundtrip "64 Bytes @0100, 64 identisch .100.0 %."
            "D=$(mktemp); F=$(mktemp); cp ${_bbdisk} \$D; printf 'g 1000000\\ndump 0x0100 64 %s\\nverify %s @0x0100\\nq\\n' \$F \$F | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D \$F")
        # ivt: IM-2-Tabelle inkl. Fallback-Zeile für den geräteloses-0xFF-Fall.
        add_cli_test(cli_dbg_ivt "0xFF +0x[0-9A-F]+ +[0-9A-F]+ +.Fallback: kein Geraet."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'g 5000000\\nivt\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # dev pio zeigt jetzt auch die beiden K5122-PIOs (vorher nur die BS-PIO).
        add_cli_test(cli_dbg_dev_pio "K5122 ctrl-PIO .Ports 10-13."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'g 1000000\\ndev pio\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # Lauf-Budgets zählen die Maschinenuhr; `clock` zeigt beide Stände.
        add_cli_test(cli_dbg_clock "Lauf-Uhr = Maschine .beide CPUs.   ZVE1=[0-9]+  Maschine=[0-9]+"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'g 1000000\\nclock\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")

        # ── Debugger-Regressionsnetz ───────────────────────────────────────────
        # Der Dispatch in k1520dbg.cpp ist eine ~90-Zweige-if/else-Kette in EINEM
        # main(); die Header-Bausteine (expr_eval, event_bp, mem_watch, prn/mac_listing,
        # callstack_tracker, until_cond) sind unit-getestet, die Kommandos selbst waren
        # es bis auf eine Handvoll nicht. Die folgenden Tests sichern (a) die Existenz
        # ALLER Kommandos (Smoke) und (b) den Wortlaut der Meldungen ab, auf die
        # Skripte/Agenten und die Doku sich stützen.
        #
        # Determinismus-Regel: NICHT auf zyklusabhängigen Maschinenzustand prüfen
        # (das brach schon einmal bei einer Timing-Änderung), sondern auf die
        # Kommando-Semantik — Zustand wird per `e`/`set`/`gu`/Breakpoint gezielt
        # hergestellt statt „nach N Takten".

        # (a) Dispatch-Smoke: jedes Kommando einmal. Schlägt fehl, sobald ein Zweig
        #     verschwindet (Tool meldet das Kommando als unbekannt) oder das Tool
        #     abstürzt. Läuft in einem eigenen Temp-Verzeichnis (das Skript schreibt
        #     Trace-/State-Dateien).
        add_test(NAME cli_dbg_all_commands_smoke COMMAND sh -c
            "T=$(mktemp -d); cd \$T; cp ${_bbdisk} disk.img; $<TARGET_FILE:k1520dbg> disk.img -x ${CMAKE_SOURCE_DIR}/tests/dbg/all_commands_smoke.dbg < /dev/null; r=\$?; cd /; rm -rf \$T; test \$r -eq 0")
        set_tests_properties(cli_dbg_all_commands_smoke PROPERTIES
            FAIL_REGULAR_EXPRESSION "unknown command" TIMEOUT 300)

        # (b0) break-before-execute (k1520dbg.md §2): der Halt greift VOR der Instruktion.
        # Die Haltezeile UND jede Folgeabfrage müssen denselben PC/Taktstand zeigen —
        # vorher lief die Instruktion noch zu Ende und `r`/`rj`/`where` sahen den
        # Folgezustand (PC=0136 statt 0135).
        # Haltezeile, die Register des Halts und ein anschließendes `r` müssen
        # dieselbe Adresse UND denselben Taktstand zeigen (drei Zeilen in Folge).
        # Haltezeile, die Register des Halts und ein anschließendes `r` müssen
        # DIESELBE Adresse zeigen. Hätte die Instruktion (0135: LD A,(HL)) noch
        # ausgeführt, stünde `r` auf 0136 — genau der behobene Fehler.
        add_cli_test(cli_dbg_stop_is_before_instruction "bp ZVE1 : ZVE1 PC=0135\n  ZVE1 PC=0135 .*\n  => 0135: 7E .*\n  state: .*\n.*ZVE1 PC=0135 .*\n  => 0135: 7E "
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ng\\nr\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_stop_json_matches_banner ".pc.:.0x0135."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ng\\nrj\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # Fortsetzen VON einem Breakpoint darf nicht sofort wieder dort halten
        # (sonst käme `g` nie von der Stelle): 3× `g` = 3 Treffer derselben Schleife.
        add_cli_test(cli_dbg_resume_past_breakpoint "0135  hits=3"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ng\\ng\\ng\\nbl\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # `s` muss vorankommen UND (gdb-Semantik) den NÄCHSTEN Befehl zeigen.
        add_cli_test(cli_dbg_step_shows_next_instruction "=> 0136: B7 +OR A\n.*\n.*\n  => 0137: 3E 87 +LD A,87H"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ng\\ns\\ns\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # breti: nach dem Halt auf einem RETI muss `g` das nächste finden, nicht dasselbe.
        add_cli_test(cli_dbg_resume_past_reti "RETI : ZVE1 PC=0090\n.*\n.*\n.*\n.*ran [0-9]+ cyc.*\n.. RETI : ZVE1 PC=0090"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'breti on\\ng\\ng\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")

        # (b1) Neue Diagnose-Ausgaben (feature_request_source_annotation).
        # bint nennt jetzt Vektor + Quellbaustein + aufgelöste Tabellenadresse.
        add_cli_test(cli_dbg_bint_vector_device "ISR 007A  Vektor=B8 von K2526 BS-PIO  Tabelle .00B8."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'bint\\ng\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # itrace schreibt Vektor + Gerät je Quittung mit.
        add_cli_test(cli_dbg_itrace_device "ISR 007A SP=[0-9A-F]+ vec=B8 dev=K2526 BS-PIO"
            "D=$(mktemp); T=$(mktemp); cp ${_bbdisk} \$D; printf 'itrace %s\\ng 300000\\nitrace off\\nq\\n' \$T | $<TARGET_FILE:k1520dbg> \$D >/dev/null 2>&1; cat \$T; rm -f \$D \$T")
        # u bricht in unbeschriebenem Speicher ab statt 40 Zeilen RST 38H zu drucken.
        add_cli_test(cli_dbg_u_unwritten_hint "ab C000 unbeschriebener Speicher .0xFF. — Ausgabe abgebrochen"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'u 0xC000 40\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # Ein zweites u-Argument > 256 ist als ENDadresse gemeint — und wird so gemeldet.
        add_cli_test(cli_dbg_u_end_address "2. Argument als END-Adresse 6003 gelesen"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'e 0x6000 00 00 00 00\\nu 0x6000 0x6003\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # clock schaltet die Lauf-Uhr auf die alte ZVE1-Zählung zurück.
        add_cli_test(cli_dbg_clock_zve1 "Lauf-Uhr = ZVE1   ZVE1=[0-9]+  Maschine=[0-9]+"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'clock zve1\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # verify meldet Quote UND die konkrete Abweichung (hier absichtlich erzeugt).
        add_cli_test(cli_dbg_verify_mismatch "0104  Datei D3   RAM AA"
            "D=$(mktemp); F=$(mktemp); cp ${_bbdisk} \$D; printf 'g 500000\\ndump 0x0100 32 %s\\ne 0x0104 AA\\nverify %s @0x0100\\nq\\n' \$F \$F | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D \$F")
        # @auto darf einen schwachen Treffer NICHT anwenden (sonst lägen die Kommentare
        # auf fremdem Code) — die Verweigerung ist die eigentliche Zusage.
        # Aufbau: 24 Bytes stehen im RAM (findbarer Anker), die folgenden 30 nicht →
        # 44 % Übereinstimmung = „anderer Build" ⇒ Versatz darf NICHT angewandt werden.
        add_cli_test(cli_dbg_lst_auto_reject "44.4 %.*\n.*Versatz NICHT angewandt .zu unsicher."
            "D=$(mktemp); M=$(mktemp --suffix=.mac); cp ${_bbdisk} \$D; printf '\\tORG\\t6000H\\n\\tDB\\t3EH,05H,3EH,05H,3EH,05H,3EH,05H,3EH,05H,3EH,05H,3EH,05H,3EH,05H,3EH,05H,3EH,05H,3EH,05H,3EH,05H\\n\\tDB\\t0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\\n' > \$M; printf 'e 0x6000 3E 05 3E 05 3E 05 3E 05 3E 05 3E 05 3E 05 3E 05 3E 05 3E 05 3E 05 3E 05\\nlst %s@auto\\nq\\n' \$M | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D \$M")

        # (b2) Länger bestehende Kommandos ohne bisherige Absicherung.
        add_cli_test(cli_dbg_cond_bp_list "0135  hits=0 if A==0"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135 if A==0\\nbl\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_bp_delete "ZVE1 breakpoints:\n    .none."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\nbd 0x0135\\nbl\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_snap_diff "RAM 6000..6001 .2 B."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'snap a\\ne 0x6000 11 22\\nsnap b\\nsnap diff a b\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # savestate/loadstate: nach dem Zwischenlauf muss loadstate genau den
        # gesicherten Punkt (PC=013B) wiederherstellen.
        add_cli_test(cli_dbg_savestate_roundtrip "state loaded ← .*\n  => 0139:"
            "D=$(mktemp); S=$(mktemp); cp ${_bbdisk} \$D; printf 'gu 0x0139\\nsavestate %s\\ng 200000\\nloadstate %s\\nq\\n' \$S \$S | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D \$S")
        add_cli_test(cli_dbg_logpoint ".lp. c[0-9]+ +0135: 7E +LD A,.HL.  A="
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'logpoint 0x0135 A\\ng 200000\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_trace_file "T1 c[0-9]+ +013[0-9]:"
            "D=$(mktemp); T=$(mktemp); cp ${_bbdisk} \$D; printf 'trace %s 0x0130 0x0140\\ng 200000\\ntrace off\\nq\\n' \$T | $<TARGET_FILE:k1520dbg> \$D >/dev/null 2>&1; head -3 \$T; rm -f \$D \$T")
        add_cli_test(cli_dbg_hist "ZVE1 top .[0-9]+ instrs, [0-9]+ distinct PCs."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'hist 200000\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_where_json ".zve1_pc.:.0x[0-9A-F]+.,.zve2_pc.:.*.busmaster.:.ZVE1."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'g 200000\\nwhere --json\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # Symbol → Disasm-Annotation UND als Breakpoint-Adresse verwendbar.
        add_cli_test(cli_dbg_symbols "0135 <START>: 7E +LD A,.HL."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'sym add START 0x0135\\nu 0x0135 1\\nb START\\nbl\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # alias + source: das Kürzel muss auch aus einer gesourceten Datei greifen.
        add_cli_test(cli_dbg_alias_source "6000: 3E 05 +LD A,05H"
            "D=$(mktemp); S=$(mktemp); cp ${_bbdisk} \$D; printf 'zeig 0x6000 1\\n' > \$S; printf 'e 0x6000 3E 05\\nalias zeig u\\nsource %s\\nq\\n' \$S | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D \$S")
        # Range-Watchpoint mit Bedingung feuert auf den Schreibzugriff im Bereich.
        add_cli_test(cli_dbg_watch_range ".wp. c[0-9]+ +WR .6006.=42"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'wp 0x6005..0x6007 changed\\ne 0x6006 42\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # I/O-Watch: erster Port-11-Schreibzugriff des Lade-ROMs (Steuerwort 7FH @00E1).
        add_cli_test(cli_dbg_io_watch ".io. c[0-9]+ +OUT .11H.=7F  ZVE1.PC=00E1"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'iow 0x11\\ng 150000\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_dev_ctc "CTC .K2526.  vecBase=[0-9A-F]+  IEI=[01] IEO=[01]"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'g 200000\\ndev ctc\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_dbg_mark_relative "mark: origin=[0-9]+ .now."
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'g 100000\\nmark\\ng 50000\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # disk verify braucht ein selbstbeschreibendes Image (.hfe) — Medien-Health.
        add_cli_test(cli_dbg_disk_verify "164 Spuren, 2560 Sektoren, 0 CRC-Fehler"
            "D=$(mktemp --suffix=.hfe); cp ${CMAKE_SOURCE_DIR}/disks/scpx_boot.hfe \$D; printf 'disk verify\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        # Step-over: `n` über ein 1-Byte-LD hinweg landet auf dem Folgebefehl.
        add_cli_test(cli_dbg_step_over "=> 0137: 3E 87 +LD A,87H"
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ng\\nn\\nn\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")

        # boot_trace — standalone runs.
        add_cli_test(cli_bt_until "MET at cycle [0-9]+ .PC=0x0168."
            "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --until '[0x03F8]==3' \$D; rm -f \$D")
        # feature_request_interactive_debug: --until 'screen ~ "txt"' (Text-VRAM als Bedingung).
        # Deckt zugleich die per-Instruktions-Pollung (sample_count, NICHT cycles_done) ab.
        add_cli_test(cli_bt_until_screen "MET at cycle [0-9]+"
            "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --until 'screen ~ \"Bootloader\"' \$D; rm -f \$D")
        # --itrace: Vektor + Quellgerät je Quittung (feature_request_source_annotation §5).
        add_cli_test(cli_bt_itrace_device "INT,0x[0-9A-F]+,0x[0-9A-F]+,0x[0-9A-F]+,0x[0-9A-F][0-9A-F],K2526"
            "D=$(mktemp); T=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --itrace \$T --until '[0x03F8]==3' \$D >/dev/null 2>&1; cat \$T; rm -f \$D \$T")
        # --watchio zeichnet ab Takt 0 auf und nennt die schreibende CPU (§8).
        add_cli_test(cli_bt_watchio_origin "OUT.11.=[0-9A-F]+  von=ZVE1"
            "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --watchio 0x11 -c 300000 \$D; rm -f \$D")
        # Fremdquelle (.MAC) auch in boot_trace: wird assembliert statt geparst.
        add_cli_test(cli_bt_lst_mac "Quelle: .*[.]mac — 2 Zeile.n. assembliert .1000[.][.]1002."
            "D=$(mktemp); M=$(mktemp --suffix=.mac); cp ${_bbdisk} \$D; printf '\\tORG\\t1000H\\n\\tLD\\tA,05H\\n\\tAND\\t0FH\\n' > \$M; $<TARGET_FILE:boot_trace> -L /dev/null -l \$M -c 100000 \$D; rm -f \$D \$M")
        # Per-Instruktions-CSV (Kopfzeile + erste Instruktion des Lade-ROMs).
        add_cli_test(cli_bt_csv "0,0,ZVE1,0x0000,00,.NOP."
            "D=$(mktemp); C=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --csv \$C --until 'PC==0x0437' \$D >/dev/null 2>&1; head -3 \$C; rm -f \$D \$C")
        # --coverage BEFORE --until so it doesn't consume the disk path as its CSV file.
        add_cli_test(cli_bt_coverage "Code coverage .ZVE1."
            "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --coverage --until 'PC==0x0437' \$D; rm -f \$D")
        # --diff is standalone (no emulation): feed two hand-written coverage CSVs.
        add_cli_test(cli_bt_diff "only-A=1 .*only-B=1"
            "A=$(mktemp); B=$(mktemp); printf 'cpu,pc,hits\\nZVE1,0x0100,1\\nZVE1,0x0200,1\\n' > \$A; printf 'cpu,pc,hits\\nZVE1,0x0200,1\\nZVE1,0x0300,1\\n' > \$B; $<TARGET_FILE:boot_trace> --diff \$A \$B; rm -f \$A \$B")
        # save state at a condition, then resume from it (ROM mapping reproduced).
        # PC = GENAU die Adresse, an der die Bedingung gemeldet wurde (break-before-execute,
        # s. k1520dbg.md §2) — vor dieser Semantik war es die Folgeinstruktion (0x0169).
        add_cli_test(cli_bt_savestate "resuming at PC=0x0168"
            "D=$(mktemp); S=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --until '[0x03F8]==3' --save-state \$S \$D >/dev/null 2>&1; $<TARGET_FILE:boot_trace> -L /dev/null --load-state \$S -c 2000 \$D; rm -f \$D \$S")

        # Exit-code semantics (for scripted/agent branching) — assert the code via `test`.
        add_test(NAME cli_bt_exit_met COMMAND sh -c
            "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --until '[0x03F8]==3' \$D >/dev/null 2>&1; r=\$?; rm -f \$D; test \$r -eq 0")
        add_test(NAME cli_bt_exit_notmet COMMAND sh -c
            "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null -c 50000 --until '[0x03F8]==3' \$D >/dev/null 2>&1; r=\$?; rm -f \$D; test \$r -eq 2")
        add_test(NAME cli_dbg_exit_badisk COMMAND sh -c
            "printf 'q\\n' | $<TARGET_FILE:k1520dbg> /no/such/disk.img >/dev/null 2>&1; test \$? -eq 1")
        set_tests_properties(cli_bt_exit_met cli_bt_exit_notmet cli_dbg_exit_badisk PROPERTIES TIMEOUT 120)

        # machine-readable JSON output (agent ergonomics).
        # Golden-Register-Snapshot bei 5M Zyklen der MASCHINENUHR (beide CPUs; seit
        # FR §7 die Voreinstellung von `g` — `clock zve1` zählt wieder wie früher nur
        # ZVE1).  Die exakte Zeitlage hängt zusätzlich vom Floppy-Timing ab (u. a.
        # Motor-Spin-up, K5122_MOTOR_SPINUP_MS) — bei Timing-Änderungen hier den
        # neuen deterministischen Wert eintragen.
        # Golden-Zustand nach fester Taktzahl — prueft das JSON-Format von `rj`.
        # Die Werte haengen am Boot-Timing und sind nachzuziehen, wenn sich der
        # Laufwerks-Suchlauf des Lade-ROMs aendert (zuletzt: /TRACK 00 meldet auch
        # ein leeres Laufwerk, K5122-Handbuch §4.1).
        add_cli_test(cli_dbg_json "\"pc\":\"0x19CF\".*\"hl\":\"0x1BCF\""
            "D=$(mktemp); cp ${_bbdisk} \$D; printf 'g 5000000\\nrj\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
        add_cli_test(cli_bt_json "\"boot_reached\":false.*\"until\":.\"set\":true,\"met\":true"
            "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --until '[0x03F8]==3' --json \$D; rm -f \$D")
        # --quiet suppresses the narrative: JSON present, but no banner/progress/summary.
        add_test(NAME cli_bt_quiet COMMAND sh -c
            "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --quiet --until '[0x03F8]==3' --json \$D; rm -f \$D")
        set_tests_properties(cli_bt_quiet PROPERTIES TIMEOUT 120
            PASS_REGULAR_EXPRESSION "boot_reached"
            FAIL_REGULAR_EXPRESSION "Boot Trace Complete|PROGRESS|MILESTONE|histogram")

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
                LABELS "format_integration")
        endmacro()

        # ─── FORMAT.COM auf einer ECHTEN LEERDISKETTE (mit Verify) ──────────────
        # Regression fuer den Einfrierer auf unformatierten Spuren.  B: wird als
        # echte Leerdiskette angelegt (leerer Formatname) und FORMAT.COM Format 1
        # ueber ALLE 160 Spuren MIT Vergleichs-Lesen darueber gefahren.
        #
        # Der volle Spurbereich ist noetig: der Fehler ist ein Wettlauf mit dem
        # Umdrehungs-Timeout des BIOS (fl.zto -> headup), der erst nach vielen
        # Spuren zuschlaegt und dessen Trefferpunkt an der Anlaufphase haengt
        # (gemessen: Spur 96 bzw. Spur 18 bei 2 Mio. Takten Unterschied).  Ein
        # kurzer Bereich (Spur 0-4) lief auch VOR dem Fix durch.
        add_test(NAME format_blank_disk_with_verify COMMAND sh -c
            "set -e; \
             A=$(mktemp --suffix=.img); B=$(mktemp --suffix=.hfe); S=$(mktemp); \
             cp ${CMAKE_SOURCE_DIR}/disks/cpadisk_autofs_noclk_noautoexec.img \$A; \
             rm -f \$B; \
             printf 'boot 80\\ntype 12:00:00\\nenter\\nboot 5\\ntype FORMAT\\nenter\\nboot 30\\nenter\\nboot 6\\ntype B\\nenter\\nboot 10\\nenter\\nboot 8\\ntype 1\\nboot 6\\nenter\\nboot 6\\nenter\\nboot 6\\ntype j\\nboot 8000\\n' > \$S; \
             $<TARGET_FILE:format_driver> \$A \$B \$S ''; \
             rm -f \$A \$B \$S")
        set_tests_properties(format_blank_disk_with_verify PROPERTIES
            TIMEOUT 1200 PASS_REGULAR_EXPRESSION "FORMATIEREN beendet"
            FAIL_REGULAR_EXPRESSION "SPUR DEFEKT"
            LABELS "format_integration")

        add_bootdisk_test(bootdisk_cpa780        cpa780)         # 5¼″-MFM   cpa780
        add_bootdisk_test(bootdisk_mf3200_fmt7   mf3200_fmt7)    # 8″-SD/FM  26×128+16×256
        add_bootdisk_test(bootdisk_mf6400_fmt1   mf6400_fmt1)    # 8″-DD Mischd. 26×128FM+8×1024MFM (C:)
        add_bootdisk_test(bootdisk_k5600_10_fmt1 k5600_10_fmt1)  # 5¼″-40-SS 26×128+5×1024
        add_bootdisk_test(bootdisk_k5600_20_fmt1 k5600_20_fmt1)  # 5¼″-80-SS 26×128+5×1024 (C:)
        # HINWEIS: Preset mf3200_fmt1 (8″-SD/FM 26×128+4×1024) ist bewusst NICHT als Test
        # registriert — die 1024-B-FM-Datensektoren werden korrekt geschrieben (CRC gültig),
        # aber das laufende OS liest sie nicht (256-B-FM funktioniert): offener 1024-B-FM-SD-
        # Lesepfad, seltenes Format.  Preset bleibt für manuelle Analyse verfügbar (§8.6).
    endif()

endif() # BUILD_K1520

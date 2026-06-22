cmake_minimum_required(VERSION 3.16)
project(a5120emu VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

    # Floppy peripheral: Disk-Format-Parser (Geometrie/cpaFormates.cfg)
    add_library(k1520_floppy STATIC
        core/peripherals/floppy_drive/format_parser.cpp
    )
    target_include_directories(k1520_floppy PUBLIC ${CMAKE_SOURCE_DIR})

    # Neue Peripherie-Schicht: Track-Bild-Abstraktion + FM/MFM-Codec + Drive/Image
    add_library(k1520_floppy2 STATIC
        core/peripherals/floppy_drive/track_image.cpp
        core/peripherals/floppy_drive/track_codec.cpp
        core/peripherals/floppy_drive/bit_codec.cpp
        core/peripherals/floppy_drive/drive_profile.cpp
        core/peripherals/floppy_drive/raw_sector_image.cpp
        core/peripherals/floppy_drive/hfe_image.cpp
        core/peripherals/floppy_drive/disk_image.cpp
        core/peripherals/floppy_drive/floppy_drive2.cpp
    )
    target_link_libraries(k1520_floppy2 PUBLIC k1520_floppy)
    target_include_directories(k1520_floppy2 PUBLIC ${CMAKE_SOURCE_DIR})

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

        add_executable(k1520_test_format_parser
            tests/cpp/test_format_parser.cpp
        )
        target_link_libraries(k1520_test_format_parser PRIVATE k1520_floppy GTest::gtest_main)
        target_include_directories(k1520_test_format_parser PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_track_codec tests/cpp/test_track_codec.cpp)
        target_link_libraries(k1520_test_track_codec PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_track_codec PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_robotron_track tests/cpp/test_robotron_track.cpp)
        target_link_libraries(k1520_test_robotron_track PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_robotron_track PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_bit_codec tests/cpp/test_bit_codec.cpp)
        target_link_libraries(k1520_test_bit_codec PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_bit_codec PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_drive_profile tests/cpp/test_drive_profile.cpp)
        target_link_libraries(k1520_test_drive_profile PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_drive_profile PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_disk_image_raw tests/cpp/test_disk_image_raw.cpp)
        target_link_libraries(k1520_test_disk_image_raw PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_disk_image_raw PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_floppy_drive2 tests/cpp/test_floppy_drive2.cpp)
        target_link_libraries(k1520_test_floppy_drive2 PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_floppy_drive2 PRIVATE ${CMAKE_SOURCE_DIR})

        add_executable(k1520_test_hfe_image tests/cpp/test_hfe_image.cpp)
        target_link_libraries(k1520_test_hfe_image PRIVATE k1520_floppy2 GTest::gtest_main)
        target_include_directories(k1520_test_hfe_image PRIVATE ${CMAKE_SOURCE_DIR})
        target_compile_definitions(k1520_test_hfe_image PRIVATE
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

        # Machine snapshot / reverse-debugging (A5120Machine::captureState/restoreState)
        add_executable(k1520_test_machine_snapshot tests/cpp/test_machine_snapshot.cpp)
        target_link_libraries(k1520_test_machine_snapshot
            PRIVATE k1520_a5120 k1520_floppy k1520_logger GTest::gtest_main)
        target_include_directories(k1520_test_machine_snapshot PRIVATE ${CMAKE_SOURCE_DIR})

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
        gtest_discover_tests(k1520_test_format_parser)
        gtest_discover_tests(k1520_test_track_codec)
        gtest_discover_tests(k1520_test_robotron_track)
        gtest_discover_tests(k1520_test_bit_codec)
        gtest_discover_tests(k1520_test_drive_profile)
        gtest_discover_tests(k1520_test_disk_image_raw)
        gtest_discover_tests(k1520_test_floppy_drive2)
        gtest_discover_tests(k1520_test_hfe_image)
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
        gtest_discover_tests(k1520_test_callstack_tracker)
        gtest_discover_tests(k1520_test_coverage_diff)
        gtest_discover_tests(k1520_test_dbg_commands)
        gtest_discover_tests(k1520_test_expr_eval)
        gtest_discover_tests(k1520_test_until_cond)
        gtest_discover_tests(k1520_test_machine_snapshot)
        gtest_discover_tests(k1520_test_boot_integration PROPERTIES TIMEOUT 60)
    endif() # BUILD_K1520_TESTS

    # boot_trace: standalone boot diagnostic tool (LOG_LEVEL=5 for full trace)
    add_executable(boot_trace tools/boot_trace.cpp)
    target_link_libraries(boot_trace PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(boot_trace PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(boot_trace PRIVATE LOG_LEVEL=5)

    # kbd_test: standalone keyboard smoke test (boot, type a command, dump screen)
    add_executable(kbd_test tools/kbd_test.cpp)
    target_link_libraries(kbd_test PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(kbd_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(kbd_test PRIVATE LOG_LEVEL=3)

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

endif() # BUILD_K1520

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

# HINWEIS: Der alte monolithische A5120-Emulator (src/, Targets a5120emu / cparun /
# a5120emu_test mit CP/M-BIOS-HALT-Traps) wurde entfernt — der modulare K1520-Kern
# unter core/ ist die einzige Implementierung.  Der eigenständige CP/M-Runner lebt
# unverändert als Unterprojekt in cparun/ (eigenes CMakeLists.txt, eigene Quellen).

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


    # ─── Testsystem ───────────────────────────────────────────────────────────
    # Die gesamte Testregistrierung liegt in tests/ (eine CMakeLists.txt je
    # Testebene, Helfer tests/cmake/K1520AddTest.cmake).  MUSS nach den
    # Werkzeugzielen stehen: die CLI-/Systemtests verweisen per
    # $<TARGET_FILE:…> auf k1520dbg, boot_trace, format_driver, mk_disk_template.
    option(BUILD_K1520_TESTS "Testsystem bauen und in ctest registrieren" ON)
    if (BUILD_K1520_TESTS)
        # enable_testing() MUSS hier oben stehen — nur dann erzeugt CMake im
        # Build-Wurzelverzeichnis eine CTestTestfile.cmake, die in die
        # Unterverzeichnisse absteigt.  Steht es nur in tests/, meldet ctest
        # „No tests were found!!!".
        enable_testing()
        add_subdirectory(tests)
    endif()

endif() # BUILD_K1520
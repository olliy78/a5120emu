/**
 * @file CMakeLists.txt
 * @brief Build configuration for K1520 emulator core library
 * 
 * This CMakeLists.txt builds the complete K1520 emulator core (libk1520core.so)
 * including all bus logic, primitives, cards, peripherals, and the C-API.
 * 
 * Build modes:
 *   BUILD_SHARED_LIBS=ON  - cards as separate shared libs for testing
 *   BUILD_SHARED_LIBS=OFF - static linking, single libk1520core.so
 * 
 * Log level:
 *   LOG_LEVEL=0 (OFF) - no logging
 *   LOG_LEVEL=3 (INFO, default)
 *   LOG_LEVEL=5 (TRACE) - full tracing
 * 
 * Usage:
 *   mkdir build && cd build
 *   cmake -DCMAKE_BUILD_TYPE=Release -DLOG_LEVEL=2 ..
 *   make -j4
 */

cmake_minimum_required(VERSION 3.16)
project(k1520core VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# Log level configuration (default: INFO)
if(NOT LOG_LEVEL)
    set(LOG_LEVEL 3)
endif()
add_compile_definitions(LOG_LEVEL=${LOG_LEVEL})
message(STATUS "K1520 Log Level: ${LOG_LEVEL}")

# Build options
option(BUILD_SHARED_LIBS "Build cards as shared libraries for testing" OFF)
option(BUILD_TESTS "Build C++ unit tests" ON)
option(BUILD_CLI "Build CLI tool" ON)

# ────────────────────────────────────────────────────────────────────────────
# Logger library (always static, header + one source file)
# ────────────────────────────────────────────────────────────────────────────

add_library(k1520_logger STATIC
    logger.cpp
    logger.h
)
target_include_directories(k1520_logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ────────────────────────────────────────────────────────────────────────────
# Bus library (K1520Bus and Koppelbus)
# ────────────────────────────────────────────────────────────────────────────

add_library(k1520_bus
    bus/k1520_bus.cpp
    bus/k1520_bus.h
    bus/koppelbus.h
)
target_link_libraries(k1520_bus PUBLIC k1520_logger)
target_include_directories(k1520_bus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ────────────────────────────────────────────────────────────────────────────
# Primitives library (Z80 chips)
# ────────────────────────────────────────────────────────────────────────────

add_library(k1520_primitives
    primitives/z80.cpp
    primitives/z80.h
    primitives/z80_pio.cpp
    primitives/z80_pio.h
    primitives/z80_sio.cpp
    primitives/z80_sio.h
    primitives/z80_ctc.cpp
    primitives/z80_ctc.h
    primitives/ram_device.h
    primitives/eprom_device.h
)
target_link_libraries(k1520_primitives PUBLIC k1520_bus k1520_logger)
target_include_directories(k1520_primitives PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ────────────────────────────────────────────────────────────────────────────
# Cards library
# ────────────────────────────────────────────────────────────────────────────

add_library(k1520_cards
    cards/k2526/k2526.cpp
    cards/k2526/k2526.h
    cards/k2526/rom_data.h
    
    cards/k3526/k3526.cpp
    cards/k3526/k3526.h
    
    cards/k5122/k5122.cpp
    cards/k5122/k5122.h
    
    cards/k7024/k7024.cpp
    cards/k7024/k7024.h
    cards/k7024/charset_latin.h
    cards/k7024/charset_cyrillic.h
    
    cards/k8025/k8025.cpp
    cards/k8025/k8025.h
)
target_link_libraries(k1520_cards PUBLIC k1520_bus k1520_primitives k1520_logger)
target_include_directories(k1520_cards PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ────────────────────────────────────────────────────────────────────────────
# Peripherals library (floppy, keyboard)
# ────────────────────────────────────────────────────────────────────────────

add_library(k1520_peripherals
    peripherals/floppy_drive/format_parser.cpp
    peripherals/floppy_drive/format_parser.h

    peripherals/k7637/k7637.cpp
    peripherals/k7637/k7637.h
)
target_link_libraries(k1520_peripherals PUBLIC k1520_bus k1520_primitives k1520_logger)
target_include_directories(k1520_peripherals PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ────────────────────────────────────────────────────────────────────────────
# Machines library (machine configurations)
# ────────────────────────────────────────────────────────────────────────────

add_library(k1520_machines
    machines/a5120/a5120.cpp
    machines/a5120/a5120.h
)
target_link_libraries(k1520_machines PUBLIC
    k1520_bus k1520_primitives k1520_cards k1520_peripherals k1520_logger
)
target_include_directories(k1520_machines PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ────────────────────────────────────────────────────────────────────────────
# C-API library (public interface)
# ────────────────────────────────────────────────────────────────────────────

add_library(k1520core
    api/k1520_api.cpp
    api/k1520_api.h
)
target_link_libraries(k1520core PUBLIC
    k1520_bus k1520_primitives k1520_cards k1520_peripherals k1520_machines
    k1520_logger
)
target_include_directories(k1520core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Export C API header
install(FILES api/k1520_api.h DESTINATION include/k1520)
install(TARGETS k1520core DESTINATION lib)

# ────────────────────────────────────────────────────────────────────────────
# CLI Binary (optional)
# ────────────────────────────────────────────────────────────────────────────

if(BUILD_CLI)
    add_executable(k1520cli
        ../cli/k1520cli.cpp
    )
    target_link_libraries(k1520cli PRIVATE k1520core)
    install(TARGETS k1520cli DESTINATION bin)
endif()

# ────────────────────────────────────────────────────────────────────────────
# Unit Tests (C++ with GoogleTest)
# ────────────────────────────────────────────────────────────────────────────

if(BUILD_TESTS)
    enable_testing()
    
    find_package(GTest)
    if(GTest_FOUND)
        # Link against legacy tests too (Z80 instruction tests)
        add_executable(k1520_tests
            ../tests/cpp/test_main.cpp
            ../tests/cpp/test_bus.cpp
            ../tests/cpp/test_k7024.cpp
            # Add more tests as implemented
        )
        target_link_libraries(k1520_tests PRIVATE k1520core GTest::gtest GTest::gtest_main)
        target_include_directories(k1520_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
        
        add_test(NAME K1520Tests COMMAND k1520_tests)
    else()
        message(WARNING "GoogleTest not found. Skipping C++ unit tests.")
    endif()
endif()

# ────────────────────────────────────────────────────────────────────────────
# Summary
# ────────────────────────────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "K1520 Emulator Core Configuration")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Log Level: ${LOG_LEVEL}")
message(STATUS "  Shared Cards: ${BUILD_SHARED_LIBS}")
message(STATUS "  Tests: ${BUILD_TESTS}")
message(STATUS "  CLI: ${BUILD_CLI}")
message(STATUS "")
message(STATUS "Build with: cmake --build . -j4")
message(STATUS "")

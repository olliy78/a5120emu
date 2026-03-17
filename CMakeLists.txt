cmake_minimum_required(VERSION 3.16)
project(a5120emu VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Install
install(TARGETS a5120emu cparun RUNTIME DESTINATION bin)

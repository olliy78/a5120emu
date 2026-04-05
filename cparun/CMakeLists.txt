cmake_minimum_required(VERSION 3.16)
project(cparun VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cparun
    src/cparun.cpp
    src/cpm_bdos.cpp
    src/z80.cpp
    src/memory.cpp
)

target_include_directories(cparun PRIVATE src)

if(MSVC)
    # Enable UTF-8 source and execution character sets (source files contain umlauts)
    target_compile_options(cparun PRIVATE /W3 /utf-8)
    # Suppress CRT deprecation warnings for standard POSIX names (fopen, strcasecmp alias, etc.)
    target_compile_definitions(cparun PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

install(TARGETS cparun RUNTIME DESTINATION bin)

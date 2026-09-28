# MAME-Orakel für die U8001/U8002-Primitive (Arbeitspaket S3).
#
# MAMEs z8000 (BSD-3-Clause, Juergen Buchmueller / Ernesto Corvi) wird zu einem
# FESTEN Stand heruntergeladen und gegen den Framework-Ersatz mame_shim/ übersetzt.
# Nur Vergleich, nichts übernommen: kein MAME-Code liegt im Repository oder in core/.
# 8000dasm.h wird absichtlich NICHT geladen — der Ersatz aus mame_shim/ gilt.

set(_mame_commit 741d827a0b2748bc0892ebb3541799118161d6a7)   # 2026-08-15
set(_mame_dir ${CMAKE_BINARY_DIR}/mame_z8000)
set(_mame_files
    "z8000.cpp=214b82f8f052de6bce396806664b6ba956e3877600b4adcc83f09664df25f619"
    "z8000.h=5cdf50966a07f339733920deed0c6fdbaae1624d8d27a3c9662b62d45189308d"
    "z8000cpu.h=749abeb66af8c2161dd45fb2cc42533df51c11e6b4146195de228318baf51b68"
    "z8000ops.hxx=46d538d7ab7fddcc848e219bebcbb9b74f382f93fe3e65615c38412de986ee6a"
    "z8000tbl.hxx=4250073e2622ff483791819a28c1843944b2e00bdb2d41e5cc355f7b7dfc1ef4")
foreach(_e IN LISTS _mame_files)
    string(REPLACE "=" ";" _p "${_e}")
    list(GET _p 0 _f)
    list(GET _p 1 _h)
    if(NOT EXISTS ${_mame_dir}/${_f})
        file(DOWNLOAD
            https://raw.githubusercontent.com/mamedev/mame/${_mame_commit}/src/devices/cpu/z8000/${_f}
            ${_mame_dir}/${_f}
            EXPECTED_HASH SHA256=${_h}
            STATUS _st)
        list(GET _st 0 _code)
        if(NOT _code EQUAL 0)
            message(FATAL_ERROR "MAME-Orakel: ${_f} nicht ladbar (${_st})")
        endif()
    endif()
endforeach()

add_library(k1520_mame_z8000_oracle STATIC ${_mame_dir}/z8000.cpp)
target_include_directories(k1520_mame_z8000_oracle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mame_shim ${_mame_dir})
if(NOT MSVC)
    target_compile_options(k1520_mame_z8000_oracle PRIVATE -w)
endif()

k1520_add_test(z8000_mame SRC test_z8000_mame.cpp
               LIBS k1520_z8000 k1520_mame_z8000_oracle
               LABELS "mame_oracle;slow" TIMEOUT 600)

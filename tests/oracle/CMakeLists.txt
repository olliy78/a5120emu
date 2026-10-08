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

# ── Z8/UB8840 (AP P19c) gegen MAMEs z8 ───────────────────────────────────────
# Nur die BEFEHLSSEMANTIK wird verglichen: z8ops.hxx (unverändert) wird in eine Hülle
# (mame_shim_z8/) eingebettet; Deklarationen und Opcodetabelle werden beim Konfigurieren
# aus z8.h/z8.cpp herausgeschnitten.  Zähler/UART/Interrupts von MAME bleiben aussen vor
# (dort deckt MAME weniger ab als der Kern; Wächter sind die Unit-Tests).
set(_mame_z8_dir ${CMAKE_BINARY_DIR}/mame_z8)
set(_mame_z8_files
    "z8.cpp=3d13fcf174c1ef45f924d8478e2072dab44acf8103a4c6d332046857079eb1d4"
    "z8.h=d231709deda2920ee5819901b70fae271e2a157996178742185771448b9bc85f"
    "z8ops.hxx=6496a228f413396a7f2c8a7e3d7fce57ce36b055b797cea110803b9f86314877")
foreach(_e IN LISTS _mame_z8_files)
    string(REPLACE "=" ";" _p "${_e}")
    list(GET _p 0 _f)
    list(GET _p 1 _h)
    if(NOT EXISTS ${_mame_z8_dir}/${_f})
        file(DOWNLOAD
            https://raw.githubusercontent.com/mamedev/mame/${_mame_commit}/src/devices/cpu/z8/${_f}
            ${_mame_z8_dir}/${_f}
            EXPECTED_HASH SHA256=${_h}
            STATUS _st)
        list(GET _st 0 _code)
        if(NOT _code EQUAL 0)
            message(FATAL_ERROR "MAME-Orakel: z8/${_f} nicht ladbar (${_st})")
        endif()
    endif()
endforeach()
# Befehlsdeklarationen (INSTRUCTION( … ) aus z8.h) und Z8601_OPCODE_MAP (aus z8.cpp)
file(READ ${_mame_z8_dir}/z8.h _z8h)
string(REGEX MATCHALL "INSTRUCTION\\( [a-zA-Z0-9_]+ \\)" _decl "${_z8h}")
string(REPLACE ";" "\n" _decl "${_decl}")
file(WRITE ${_mame_z8_dir}/z8_decl.inc "${_decl}\n")
file(READ ${_mame_z8_dir}/z8.cpp _z8c)
string(FIND "${_z8c}" "const z8_device::z8_opcode_map z8_device::Z8601_OPCODE_MAP[256] =" _a)
string(SUBSTRING "${_z8c}" ${_a} -1 _rest)
string(FIND "${_rest}" "};" _b)
math(EXPR _b "${_b} + 2")
string(SUBSTRING "${_rest}" 0 ${_b} _tab)
file(WRITE ${_mame_z8_dir}/z8_table.inc "${_tab}\n")

add_library(k1520_mame_z8_oracle STATIC mame_shim_z8/z8_mame.cpp)
target_include_directories(k1520_mame_z8_oracle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mame_shim_z8 ${_mame_z8_dir})
if(NOT MSVC)
    target_compile_options(k1520_mame_z8_oracle PRIVATE -w)
endif()

k1520_add_test(z8_mame SRC test_z8_mame.cpp
               LIBS k1520_z8 k1520_mame_z8_oracle
               LABELS "mame_oracle;slow" TIMEOUT 600)

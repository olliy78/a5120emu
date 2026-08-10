# ─────────────────────────────────────────────────────────────────────────────
# Python-Testebene (pytest): C-ABI + GUI
# ─────────────────────────────────────────────────────────────────────────────
#
# Deckt ab, was die C++-Tests nicht erreichen: die ctypes-Bindung gegen
# libk1520core.so (Signatur-Drift bricht sonst STILL) und die PySide6-GUI
# (headless über QT_QPA_PLATFORM=offscreen).  Je Testmodul ein ctest-Fall.
#
# Voraussetzungen (pytest, PySide6, PyYAML) werden EINMAL beim Konfigurieren
# geprüft; fehlen sie, werden die Tests nicht registriert und CMake sagt warum.
# Installation:  venv/bin/python3 -m pip install -r requirements-dev.txt
#
# Einzelheiten und Grenzen der Ebene: tests/python/README.md

option(BUILD_PYTHON_TESTS "pytest-Ebene (C-ABI + GUI) in ctest registrieren" ON)

if (NOT BUILD_PYTHON_TESTS)
    return()
endif()

# Das Projekt-venv hat PySide6/PyYAML/pytest; ein System-Python meist nicht.
if (EXISTS "${CMAKE_SOURCE_DIR}/venv/bin/python3")
    set(K1520_PYTEST_PYTHON "${CMAKE_SOURCE_DIR}/venv/bin/python3")
else()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    set(K1520_PYTEST_PYTHON "${Python3_EXECUTABLE}")
endif()

execute_process(
    COMMAND "${K1520_PYTEST_PYTHON}" -c "import pytest, PySide6, yaml"
    RESULT_VARIABLE _k1520_pydeps OUTPUT_QUIET ERROR_QUIET)

if (K1520_PYTEST_PYTHON AND _k1520_pydeps EQUAL 0)
    message(STATUS "Python-Tests: ${K1520_PYTEST_PYTHON}")
    # Ein ctest-Fall je Testmodul: klare Fehlerzuordnung + parallelisierbar.
    set(_py_modules c_api disk_c_api binding boot_smoke drive_types config_io keyboard_map gui_smoke disktool_gui)
    foreach(_mod IN LISTS _py_modules)
        add_test(NAME py_${_mod}
            COMMAND "${K1520_PYTEST_PYTHON}" -m pytest -q --no-header
                    "${CMAKE_CURRENT_SOURCE_DIR}/test_${_mod}.py"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        set_tests_properties(py_${_mod} PROPERTIES
            LABELS "python;fast" TIMEOUT 300
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
else()
    message(STATUS
        "Python-Tests NICHT registriert — pytest/PySide6/PyYAML fehlen in "
        "'${K1520_PYTEST_PYTHON}'.  Nachinstallieren: "
        "venv/bin/python3 -m pip install -r requirements-dev.txt")
endif()

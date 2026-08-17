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
# Windows legt es nach venv/Scripts/python.exe statt venv/bin/python3.
if (EXISTS "${CMAKE_SOURCE_DIR}/venv/bin/python3")
    set(K1520_PYTEST_PYTHON "${CMAKE_SOURCE_DIR}/venv/bin/python3")
elseif (EXISTS "${CMAKE_SOURCE_DIR}/venv/Scripts/python.exe")
    set(K1520_PYTEST_PYTHON "${CMAKE_SOURCE_DIR}/venv/Scripts/python.exe")
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
    # gw_physical: physische Diskette OHNE Hardware (Ersatzlaufwerk ueber .hfe).
    # Der Gegenpart test_gw_hardware.py ist NICHT registriert — er braucht einen
    # Greaseweazle und laeuft von Hand (doc/design/14_physische_diskette.md §14).
    set(_py_modules c_api disk_c_api binding boot_smoke drive_types config_io
                    keyboard_map gui_smoke disktool_gui paths packaging
                    gw_physical gw_gui physical_cli)
    foreach(_mod IN LISTS _py_modules)
        # -p no:cacheprovider: ALLE Fälle laufen im selben Arbeitsverzeichnis, und
        # seit die Regression parallel fährt (`ctest -j`) auch gleichzeitig.  Der
        # Cache-Anbieter legt dabei sein Verzeichnis über ein Temp-Verzeichnis plus
        # Umbenennen an (`<rootdir>/pytest-cache-files-XXXX`) — zwei Prozesse
        # geraten sich dabei in die Quere, und der Verlierer bricht schon beim
        # EINSAMMELN ab („FileNotFoundError … pytest-cache-files-…"), also ohne
        # einen einzigen Test gefahren zu haben.  Genau so am 2026-08-12 auf
        # windows-latest passiert.  Der Cache trägt hier ohnehin nichts bei: er
        # speist `--lf`/`--ff`, und jeder ctest-Fall fährt genau ein Modul.
        add_test(NAME py_${_mod}
            COMMAND "${K1520_PYTEST_PYTHON}" -m pytest -q --no-header
                    -p no:cacheprovider
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

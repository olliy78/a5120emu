# Waechter cli_bandtest_bootband (AP K7): das eingecheckte Bootband
# tests/fixtures/kassette/bandtest_boot.k52 ist genau `k1520tape bootband` von
# tools/kassette/bandtest/bandtest.bin.  Aufruf: cmake -DTOOL=… -DBIN=… -DSOLL=… -DTMP=… -P
execute_process(COMMAND "${TOOL}" bootband "${BIN}" "${TMP}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "k1520tape bootband scheiterte (${rc})")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${TMP}" "${SOLL}" RESULT_VARIABLE diff)
file(REMOVE "${TMP}")
if(NOT diff EQUAL 0)
    message(FATAL_ERROR "${SOLL} passt nicht zu ${BIN} - neu erzeugen: "
                        "tools/dev.sh tool k1520tape bootband ${BIN} ${SOLL}")
endif()
message(STATUS "Bootband passt zum Programm")

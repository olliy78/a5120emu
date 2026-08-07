# ─────────────────────────────────────────────────────────────────────────────
# CLI-/Blackbox-Tests: die gebauten Werkzeuge als Prozess
# ─────────────────────────────────────────────────────────────────────────────
#
# Run the built tool with a scripted session and match the output. This exercises
# the full stack (command dispatch + machine wiring), which the header-level unit
# tests don't reach. Each disk-using test copies the fixture to a PRIVATE temp file
# first — the tools mount r/w and a run can otherwise corrupt the committed image.
#
# OFFEN (Schritt 5 des Umbaus, doc/testsystem_rework.md §2 B3/B4): die Fälle
# stecken weiterhin als escapte Shell-Einzeiler samt Erwartungs-Regex hier im
# Build-System.  Ziel ist eine Datentabelle (tests/cli/cases/*.cli) plus ein
# Ausführungsskript, damit Sonderzeichen ausdrückbar sind, das
# mktemp/cp/rm-Ritual nur einmal existiert und Golden-Ausgaben im
# Testverzeichnis liegen statt im CMake.

set(_bbdisk "${K1520_FIXTURE_DISKS}/cpa_cpa780_k5601_clock.img")
# add_cli_test(<name> <pass-regex> <shell-command>)
macro(add_cli_test name regex cmd)
    add_test(NAME ${name} COMMAND sh -c "${cmd}")
    set_tests_properties(${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "${regex}" TIMEOUT 120 LABELS "cli;fast")
endmacro()

# k1520dbg — scripted sessions piped on stdin (temp disk copy).
# NB: regex uses '.' for literal []()*+ — escaped specials don't survive the macro.
add_cli_test(cli_dbg_breakpoint "bp ZVE1 : ZVE1 PC=0135"
    "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ng\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
add_cli_test(cli_dbg_watchpoint "WR .6005.=99"
    "D=$(mktemp); cp ${_bbdisk} \$D; printf 'wp 0x6005 == 99\\ne 0x6005 99\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
add_cli_test(cli_dbg_expr_eval "= 10 .0xA."
    "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ndisp 2*3+4\\ng\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
# Erster angenommener Interrupt ist die ISR der Speicher-Ausbaumessung des
# Lade-ROMs (007AH, ausgelöst vom /WR-Strobe des Testschreibens) — lange vor
# dem Floppy-Index-IRQ (01C7H).
add_cli_test(cli_dbg_event_bp "ISR 007A"
    "D=$(mktemp); cp ${_bbdisk} \$D; printf 'bint\\ng\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
add_cli_test(cli_dbg_reverse_step "restored 1 step"
    "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ng\\ns 1\\nrs\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
# feature_request_interactive_debug: screen-konditioniertes Laufen, Screen-Suche,
# Mehr-Byte-Examine (#4) — Wortlaut = die Meldezeilen der Kommandos.
add_cli_test(cli_dbg_gscreen "screen matched after [0-9]+ cyc"
    "D=$(mktemp); cp ${_bbdisk} \$D; printf 'gscreen \"Bootloader\" 8000000\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
add_cli_test(cli_dbg_screen_find "found at row [0-9]+ col [0-9]+"
    "D=$(mktemp); cp ${_bbdisk} \$D; printf 'gscreen \"Bootloader\" 8000000\\nscreen find \"Bootloader\"\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
# x <A> N (ohne /N-Spec) muss N Bytes dumpen — der behobene #4-Bug (gab nur 1 Byte).
add_cli_test(cli_dbg_examine_count "6000: 41 42 43 44"
    "D=$(mktemp); cp ${_bbdisk} \$D; printf 'e 0x6000 41 42 43 44\\nx 0x6000 4\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")

# boot_trace — standalone runs.
add_cli_test(cli_bt_until "MET at cycle [0-9]+ .PC=0x0168."
    "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --until '[0x03F8]==3' \$D; rm -f \$D")
# feature_request_interactive_debug: --until 'screen ~ "txt"' (Text-VRAM als Bedingung).
# Deckt zugleich die per-Instruktions-Pollung (sample_count, NICHT cycles_done) ab.
add_cli_test(cli_bt_until_screen "MET at cycle [0-9]+"
    "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --until 'screen ~ \"Bootloader\"' \$D; rm -f \$D")
# --coverage BEFORE --until so it doesn't consume the disk path as its CSV file.
add_cli_test(cli_bt_coverage "Code coverage .ZVE1."
    "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --coverage --until 'PC==0x0437' \$D; rm -f \$D")
# --diff is standalone (no emulation): feed two hand-written coverage CSVs.
add_cli_test(cli_bt_diff "only-A=1 .*only-B=1"
    "A=$(mktemp); B=$(mktemp); printf 'cpu,pc,hits\\nZVE1,0x0100,1\\nZVE1,0x0200,1\\n' > \$A; printf 'cpu,pc,hits\\nZVE1,0x0200,1\\nZVE1,0x0300,1\\n' > \$B; $<TARGET_FILE:boot_trace> --diff \$A \$B; rm -f \$A \$B")
# save state at a condition, then resume from it (ROM mapping reproduced).
add_cli_test(cli_bt_savestate "resuming at PC=0x0169"
    "D=$(mktemp); S=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --until '[0x03F8]==3' --save-state \$S \$D >/dev/null 2>&1; $<TARGET_FILE:boot_trace> -L /dev/null --load-state \$S -c 2000 \$D; rm -f \$D \$S")

# Exit-code semantics (for scripted/agent branching) — assert the code via `test`.
add_test(NAME cli_bt_exit_met COMMAND sh -c
    "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --until '[0x03F8]==3' \$D >/dev/null 2>&1; r=\$?; rm -f \$D; test \$r -eq 0")
add_test(NAME cli_bt_exit_notmet COMMAND sh -c
    "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null -c 50000 --until '[0x03F8]==3' \$D >/dev/null 2>&1; r=\$?; rm -f \$D; test \$r -eq 2")
add_test(NAME cli_dbg_exit_badisk COMMAND sh -c
    "printf 'q\\n' | $<TARGET_FILE:k1520dbg> /no/such/disk.img >/dev/null 2>&1; test \$? -eq 1")
set_tests_properties(cli_bt_exit_met cli_bt_exit_notmet cli_dbg_exit_badisk
    PROPERTIES TIMEOUT 120 LABELS "cli;fast")

# machine-readable JSON output (agent ergonomics).
# Golden-Register-Snapshot bei 5M Zyklen.  Die exakte Zeitlage hängt vom
# Floppy-Timing ab (u. a. Motor-Spin-up, K5122_MOTOR_SPINUP_MS) — bei
# Timing-Änderungen hier den neuen deterministischen Wert eintragen.
add_cli_test(cli_dbg_json "\"pc\":\"0x1E52\".*\"hl\":\"0x231A\""
    "D=$(mktemp); cp ${_bbdisk} \$D; printf 'g 5000000\\nrj\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
add_cli_test(cli_bt_json "\"boot_reached\":false.*\"until\":.\"set\":true,\"met\":true"
    "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --until '[0x03F8]==3' --json \$D; rm -f \$D")
# --quiet suppresses the narrative: JSON present, but no banner/progress/summary.
add_test(NAME cli_bt_quiet COMMAND sh -c
    "D=$(mktemp); cp ${_bbdisk} \$D; $<TARGET_FILE:boot_trace> -L /dev/null --quiet --until '[0x03F8]==3' --json \$D; rm -f \$D")
set_tests_properties(cli_bt_quiet PROPERTIES TIMEOUT 120 LABELS "cli;fast"
    PASS_REGULAR_EXPRESSION "boot_reached"
    FAIL_REGULAR_EXPRESSION "Boot Trace Complete|PROGRESS|MILESTONE|histogram")

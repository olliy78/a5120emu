# Unit-Tests der header-only Bausteine, aus denen k1520dbg und boot_trace
# zusammengesetzt sind (tools/*.h).  Kein Emulator, keine Bibliothek nötig —
# die Header werden direkt inkludiert.

set(_lbl "debugtools;fast")

k1520_add_test(expr_eval         SRC test_expr_eval.cpp         LABELS "${_lbl}")  # tools/expr_eval.h
k1520_add_test(until_cond        SRC test_until_cond.cpp        LABELS "${_lbl}")  # tools/until_cond.h
k1520_add_test(event_bp          SRC test_event_bp.cpp          LABELS "${_lbl}")  # tools/event_bp.h
k1520_add_test(mem_watch         SRC test_mem_watch.cpp         LABELS "${_lbl}")  # tools/mem_watch.h
k1520_add_test(dbg_commands      SRC test_dbg_commands.cpp      LABELS "${_lbl}")  # tools/dbg_commands.h
k1520_add_test(coverage_diff     SRC test_coverage_diff.cpp     LABELS "${_lbl}")  # tools/coverage_diff.h
k1520_add_test(callstack_tracker SRC test_callstack_tracker.cpp LABELS "${_lbl}")  # tools/callstack_tracker.h
k1520_add_test(prn_listing       SRC test_prn_listing.cpp       LABELS "${_lbl}")  # tools/prn_listing.h

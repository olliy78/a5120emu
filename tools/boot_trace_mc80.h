/**
 * @file boot_trace_mc80.h
 * @brief `boot_trace --machine mc80[-30|-31]` (doc/design/32_... AP M8).
 *
 * Eine CPU (ZVE), VIS-2a-Bild als Text (25 Zeilen), EGOS-Prompt `>` in Zeile 24.  Optionen wie
 * beim PC 1715 (@ref K8915TraceOpts); zusaetzlich `K8915TraceOpts::spe` (Zahl der SPE 2) und
 * `--tape`/`--tape2`/`--tape-card` (K5020 an 30H).  `--keys`: `<ET>` = Return, Block je Stillstand.
 *
 * @license MIT
 */
#pragma once
#include "tools/boot_trace_k8915.h"

/** @brief Faehrt einen MC80 und berichtet.  Rueckgabe = Exit-Code: 0 = Prompt bei Stillstand bzw.
 *  `--until` erfuellt, 1 = nicht erreicht, 2 = `--until` nicht erfuellt.  @p v31 = MC80.31, sonst .30. */
int bootTraceMc80(const K8915TraceOpts& o, const prnlst::Listing& prn, bool v31 = false);

/** @brief `boot_trace --machine mc80-21|mc80-22` (AP M12, boot_trace_mc80_2x.cpp): Mc80_2xMachine, DPL-Bild
 *  8 x 32 als Text.  Rueckgabe wie @ref bootTraceMc80, nur das Ziel ist das BSYS-Menue: Exit 0 = bei Stillstand
 *  zeigt das Bild eine Zeile mit `TASTENTEST` (die blinkende Marke in Spalte 5 zaehlt nicht als Bildaenderung).
 *  `--keys`: `<ET>` ENTER, `<OFF>`, `<UP>`/`<DN>`/`<LT>`/`<RT>` Kursor, `<CL>`, `<BK>`, `<TAB>`; ein Block endet
 *  mit `<ET>` und wird bei Stillstand getippt.  @p v22 = MC80.22, sonst .21. */
int bootTraceMc80_2x(const K8915TraceOpts& o, const prnlst::Listing& prn, bool v22 = false);

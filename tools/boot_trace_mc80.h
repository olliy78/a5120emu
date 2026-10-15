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

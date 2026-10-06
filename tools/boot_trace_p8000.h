/**
 * @file boot_trace_p8000.h
 * @brief `boot_trace --machine p8000` — Grundform für den P8000 (doc/design/25_p8000.md AP P7c;
 *        Muster tools/boot_trace_pc1715.h).
 *
 * Stand P7c: nur die 8-Bit-Seite (U880 + MON8 + Floppy, Kern-Terminal an tty1).  Optionen wie
 * beim K8915 (@ref K8915TraceOpts); `--keys` tippt blockweise bei Stillstand (`<CR>`/`<ET>` =
 * Return), `--skip-selftest`/`--no-cr`/`--raf`/`--ptape` sind wirkungslos.  Aktive CPU wählbar,
 * `--p8000 <konfig>` und `--hd` (Entwurf §10.10) kommen mit dem 16-Bit-Teil/WDC (P10/P12/P13).
 *
 * @license MIT
 */
#pragma once
#include "tools/boot_trace_k8915.h"

/** @brief Fährt den P8000 (8-Bit-Seite) und berichtet.
 *  Rückgabe = Exit-Code: 0 = Prompt (`>`/`%`) oder „Press RETURN" bei Stillstand bzw. `--until`
 *  erfüllt, 1 = nicht erreicht (Stillstand woanders, Taktgrenze), 2 = `--until` nicht erfüllt. */
int bootTraceP8000(const K8915TraceOpts& o, const prnlst::Listing& prn);

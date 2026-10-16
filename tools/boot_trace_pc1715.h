/**
 * @file boot_trace_pc1715.h
 * @brief `boot_trace --machine pc1715` — Grundform fuer den PC 1715 (doc/design/21_pc1715.md
 *        AP-2; ausgebaut in AP-4b).
 *
 * Wie beim PRG 710 ein eigener Zweig: eine CPU, K5122 im `/WAIT`-Betrieb (Portlage „1715"),
 * Bild ueber den 8275 aus dem Haupt-RAM.  Optionen wie beim K8915 (@ref K8915TraceOpts);
 * `--keys` (`<ET>` = Return) tippt blockweise bei Stillstand; `--skip-selftest`, `--no-cr` sind wirkungslos.
 *
 * @license MIT
 */
#pragma once
#include "tools/boot_trace_k8915.h"

/** @brief Faehrt einen PC 1715 und berichtet.
 *  Rueckgabe = Exit-Code: 0 = Prompt (`A>`) bei Stillstand bzw. `--until` erfuellt,
 *  1 = nicht erreicht (Stillstand woanders, Taktgrenze), 2 = `--until` nicht erfuellt. */
/// @param w true = PC 1715W (`--machine pc1715w`, AP-W3): Urlader S550, U8272 + UA858, Baenke.
int bootTracePc1715(const K8915TraceOpts& o, const prnlst::Listing& prn, bool w = false);

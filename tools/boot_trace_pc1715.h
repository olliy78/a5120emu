/**
 * @file boot_trace_pc1715.h
 * @brief `boot_trace --machine pc1715` — Grundform für den PC 1715 (doc/design/21_pc1715.md
 *        AP-2; ausgebaut in AP-4b).
 *
 * Wie beim PRG 710 ein eigener Zweig: eine CPU, K5122 im `/WAIT`-Betrieb (Portlage „1715"),
 * Bild über den 8275 aus dem Haupt-RAM.  Optionen wie beim K8915 (@ref K8915TraceOpts);
 * `--keys`, `--skip-selftest`, `--no-cr` sind wirkungslos (Tastatur erst AP-3).
 *
 * @license MIT
 */
#pragma once
#include "tools/boot_trace_k8915.h"

/** @brief Fährt einen PC 1715 und berichtet.
 *  Rückgabe = Exit-Code: 0 = Prompt (`A>`) bei Stillstand bzw. `--until` erfüllt,
 *  1 = nicht erreicht (Stillstand woanders, Taktgrenze), 2 = `--until` nicht erfüllt. */
int bootTracePc1715(const K8915TraceOpts& o, const prnlst::Listing& prn);

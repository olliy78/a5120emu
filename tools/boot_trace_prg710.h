/**
 * @file boot_trace_prg710.h
 * @brief `boot_trace --machine prg710|prg710-1` — Grundform für den PRG 710 / 710-1
 *        (doc/design/20_prg710.md AP-P1d; ausgebaut in AP-P5c).
 *
 * Wie beim K8915 (tools/boot_trace_k8915.h) ein eigener Zweig: eine CPU (ZRE K2521),
 * keine ZVE2, kein DMA-Handschlag.  Gemeinsam mit dem A5120-Teil sind Kommandozeile,
 * Listings, `--until`, `--quiet --json` und der COW-Mount; die Optionen kommen in
 * derselben Struktur wie beim K8915 herüber (@ref K8915TraceOpts — dort stehen die
 * Felder, von denen hier `skip_selftest`/`auto_cr` wirkungslos sind).
 *
 * @license MIT
 */
#pragma once
#include "tools/boot_trace_k8915.h"

/** @brief Fährt einen PRG 710 (@p variante_1 = false) bzw. 710-1 und berichtet.
 *  Rückgabe = Exit-Code: 0 = ROM wartet in der Tastaturabfrage bzw. `--until` erfüllt,
 *  1 = nicht erreicht (Stillstand woanders, Taktgrenze), 2 = `--until` nicht erfüllt. */
int bootTracePrg710(const K8915TraceOpts& o, bool variante_1, const prnlst::Listing& prn);

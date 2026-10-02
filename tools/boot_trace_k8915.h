/**
 * @file boot_trace_k8915.h
 * @brief `boot_trace --machine k8915` — eigener Zweig für den K8915 (§8a AP-E4d).
 *
 * Der A5120-Teil von boot_trace ist um die ZVE2, den DMA-Handschlag ([03F8]) und die
 * Meilensteine des A5120-Boot-ROMs gebaut; davon passt am K8915 nichts.  Gemeinsam
 * sind Kommandozeile, Listings, `--until`, `--quiet --json`, COW-Mount — die Optionen
 * reicht main() in @ref K8915TraceOpts herüber.
 *
 * @license MIT
 */
#pragma once
#include "tools/prn_listing.h"
#include "tools/until_cond.h"
#include <cstdint>
#include <string>
#include <vector>

struct K8915TraceOpts {
    std::string disk;            ///< Diskettenpfad (leer = keine)
    std::string mount_path;      ///< tatsächlich zu mountender Pfad (COW-Kopie o. ä.)
    bool        write_protect = false;
    int         drive = 0;
    long long   limit = 250'000'000;   ///< -c (Vorgabe am K8915: 250 Mio. ≈ 100 s Maschinenzeit)
    bool        quiet = false;
    bool        json = false;
    bool        skip_selftest = false;  ///< JP bei 0000H/0005H in Bank 1 (wie ein Warmstart)
    bool        auto_cr = true;         ///< nach „* Coldstart *“ einmal CR tippen
    long long   stall = 30'000'000;     ///< so lange ohne Bildänderung/Ereignis = Stillstand
    std::string keys;                   ///< --keys (PRG): Tasten, `<ET>` = ET1/Return; je Block bei Stillstand
    std::string events_path;            ///< --events <datei> (sonst stderr)
    long        events_cap = 3000;      ///< höchstens so viele Protokollzeilen
    untilcond::UntilCond until;
    bool        coverage = false;
    std::string coverage_path;
    std::string csv_path;
    std::string itrace_path;
    int         win_lo = -1, win_hi = -1, win_cap = 3000;   ///< -w/-W
    std::vector<uint16_t> watch;        ///< --watch
    std::vector<uint16_t> watchio;      ///< --watchio (Portnummern)
    int         dump_lo = -1, dump_hi = -1;  ///< -d
    std::string dump_path;
};

/** @brief Fährt den K8915 und berichtet; Rückgabe = Exit-Code (s. tools/boot_trace.md §8). */
int bootTraceK8915(const K8915TraceOpts& o, const prnlst::Listing& prn);

/**
 * @file boot_trace_k8915.h
 * @brief `boot_trace --machine k8915` — eigener Zweig fuer den K8915 (§8a AP-E4d).
 *
 * Der A5120-Teil von boot_trace ist um die ZVE2, den DMA-Handschlag ([03F8]) und die
 * Meilensteine des A5120-Boot-ROMs gebaut; davon passt am K8915 nichts.  Gemeinsam
 * sind Kommandozeile, Listings, `--until`, `--quiet --json`, COW-Mount — die Optionen
 * reicht main() in @ref K8915TraceOpts herueber.
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
    int         spe = 1;         ///< --spe 1|2: Zahl der SPE 2 am MC80 (AP M8)
    bool        gen2 = false;    ///< --machine k8915-g2: K8915 Generation 2 (K2521 + K3528, AP-V6b)
    std::string raf;             ///< --raf: RAM-Floppy stecken (leer = keine)
    bool        ptape = false;   ///< --ptape: Lochstreifen-Karte K6022 stecken (E0H–E7H)
    bool        tape = false;    ///< --tape/--tape-card: Kassettenkarte K5020 stecken
    std::string tape1, tape2;    ///< --tape/--tape2: .k52 an Geraet 1/2 binden
    std::string disk;            ///< Diskettenpfad (leer = keine)
    std::string mount_path;      ///< tatsaechlich zu mountender Pfad (COW-Kopie o. ae.)
    bool        write_protect = false;
    int         drive = 0;
    long long   limit = 250'000'000;   ///< -c (Vorgabe am K8915: 250 Mio. ≈ 100 s Maschinenzeit)
    bool        quiet = false;
    bool        json = false;
    bool        skip_selftest = false;  ///< JP bei 0000H/0005H in Bank 1 (wie ein Warmstart)
    bool        auto_cr = true;         ///< nach „* Coldstart *“ einmal CR tippen
    long long   stall = 30'000'000;     ///< so lange ohne Bildaenderung/Ereignis = Stillstand
    std::string keys;                   ///< --keys (PRG): Tasten, `<ET>` = ET1/Return; je Block bei Stillstand
    std::string events_path;            ///< --events <datei> (sonst stderr)
    long        events_cap = 3000;      ///< hoechstens so viele Protokollzeilen
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

/** @brief Faehrt den K8915 und berichtet; Rueckgabe = Exit-Code (s. tools/boot_trace.md §8). */
int bootTraceK8915(const K8915TraceOpts& o, const prnlst::Listing& prn);

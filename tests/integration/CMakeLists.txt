# Integrationstests: die ganze Maschine (A5120Machine), echter Kaltboot von
# einer Fixture-Diskette.  Laufzeit je Fall 0,2–2 s — bleiben deshalb in der
# schnellen Runde.  LOG_LEVEL=2 (WARN) hält den Millionen-Takte-Lauf lesbar.

set(_lbl "integration;fast")
set(_libs k1520_a5120 k1520_floppy k1520_logger k1520_testsupport)

k1520_add_test(boot_integration SRC test_boot_integration.cpp LIBS ${_libs}
                                LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 60)
k1520_add_test(machine_snapshot SRC test_machine_snapshot.cpp LIBS ${_libs} LABELS "${_lbl}")
k1520_add_test(a5120_disk_api   SRC test_a5120_disk_api.cpp   LIBS ${_libs} LABELS "${_lbl}")

# Physische Diskette (Greaseweazle): Kaltstart von einer SPURWEISE gelesenen Quelle.
# Braucht KEINE Hardware — ein Ersatz-Arbeitsfaden bedient die Auftraege aus einer
# .hfe-Fixture (doc/design/14_physische_diskette.md §14).
k1520_add_test(physical_boot    SRC test_physical_boot.cpp    LIBS ${_libs}
                                LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# k1520DiskTool ↔ echtes CP/A: geschrieben wird mit CpmFileSystem, gelesen vom
# laufenden Betriebssystem (doc/design/13_k1520disktool.md §15).
k1520_add_test(disktool_cpm_roundtrip SRC test_disktool_cpm_roundtrip.cpp
                                      LIBS ${_libs} k1520_fs
                                      LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# Dieselbe Kreuzprobe auf FRISCH ANGELEGTEN Disketten: 192 Verzeichnisplaetze (cpa800,
# vom Werkzeug SELBST ausgerechnet) und die Doppelschritt-Abbildung (`step: 2`).
k1520_add_test(disktool_neue_disketten SRC test_disktool_neue_disketten.cpp
                                       LIBS ${_libs} k1520_fs
                                       LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# Bootfaehige Diskette: Systemspuren herausholen, neue Diskette damit anlegen,
# Systemdateien einfuegen — und davon kalt starten.
k1520_add_test(disktool_bootdiskette SRC test_disktool_bootdiskette.cpp
                                     LIBS ${_libs} k1520_fs
                                     LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# K8915 Etappe 1+2 (doc/design/16_k8915.md §8a AP-E1/AP-E2): ZRE 045-8762, ATS
# K7028.30 + K7672, K7024 — Selbsttest bis zur Coldstart-Meldung.  Keine Diskette
# (die K5122 ist bestückt, die Laufwerke bleiben leer ⇒ „Drive A: not ready“).
k1520_add_test(k8915_boot SRC test_k8915_boot.cpp
                          LIBS k1520_k8915 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2)

# K8915Machine ohne Boot (AP-T1a): DFÜ/Drucker-Kanal, Fehlerwege von `Laufwerke`,
# Durchreichungen, Konsolenmodus.  Kein CPU-Lauf, < 1 s.
k1520_add_test(k8915_maschine SRC test_k8915_maschine.cpp
                          LIBS k1520_k8915 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2)

# K8915 Etappe 3 (doc/design/16_k8915.md §8a AP-E3): SCPX 8915 von der eigenen Diskette
# bis zum Prompt, dann dir/save/era/Warmstart/RAM-Disk.  Je Fall ≈ 2–3 s.
k1520_add_test(k8915_scpx SRC test_k8915_scpx.cpp
                          LIBS k1520_k8915 k1520_fs k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# K8915 Etappe 4 (doc/design/16_k8915.md §8a AP-E4k): Kaltstart von einer SPURWEISE
# gelesenen Diskette (Ersatzsitzung ohne Greaseweazle) — keine Maschinenzeit waehrend des
# Nachladens, Schreiben mit Zuruecklesen, Laufwerks- und Sitzungsschloss.
k1520_add_test(k8915_physical SRC test_k8915_physical.cpp
                          LIBS k1520_k8915 k1520_floppy k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# Serielle Schnittstellen (Entwurf 19 §11, AP-S8): zwei A5120 im selben Prozess über
# echtes Loopback-TCP, 1× gegen 10× Takt, Telnet+XON/XOFF und RFC 2217+RTS/CTS.
# Standardregression 4 KiB je Fall (~2 s); die volle Fassung mit 64 KiB läuft mit den
# langsamen Fällen (`tools/dev.sh test-format`).
k1520_add_test(serielle_kopplung SRC test_serielle_kopplung.cpp
                                 LIBS k1520_a5120 k1520_logger
                                 LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)
k1520_add_test(serielle_kopplung_voll SRC test_serielle_kopplung.cpp
                                 LIBS k1520_a5120 k1520_logger
                                 LABELS "integration;slow;format_integration"
                                 DEFS LOG_LEVEL=2 KOPPLUNG_VOLL=1 TIMEOUT 600)

# A5120.16, Abnahme S1: Original-BIOS (em256 equ 1) erkennt das EM256, RAM-Floppy M:
# (doc/design/17_a5120_16.md §3 S1).
k1520_add_test(em256_ramfloppy SRC test_em256_ramfloppy.cpp LIBS ${_libs} k1520_fs
                               LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# A5120.16, G1: Prüfprogramm em256adr.com (CPA-Workbench) gegen das EM256 — Portbasis,
# Attributspeicher, Lesart der Leseadresse (doc/design/17_a5120_16.md §3 G1).
k1520_add_test(em256_adr SRC test_em256_adr.cpp LIBS ${_libs} k1520_fs
                         LABELS "${_lbl}" TIMEOUT 180
                         DEFS LOG_LEVEL=2
                              "EM256ADR_COM=\"${CMAKE_SOURCE_DIR}/tests/fixtures/cpm/em256adr.com\"")


# A5120.16, S4: U880 + U8001 auf gemeinsamer Maschinenzeit, Save-State v6 (ohne CP/A).
k1520_add_test(em16_maschine SRC test_em16_maschine.cpp LIBS ${_libs} k1520_z8000
                             LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 60)

# A5120.16, Abnahme S4: Prüfprogramm em16abl.com (CPA-Workbench, U8001-Teil mit z8kasm)
# unter CP/A — alle in §7 belegten Abläufe des 16-Bit-Mode Ende-zu-Ende; dazu em256ful.com
# v2.0 (G2b, nach §7 repariert), Gruppen A–E.
k1520_add_test(em16_abl SRC test_em16_abl.cpp LIBS ${_libs} k1520_fs
                        LABELS "${_lbl}" TIMEOUT 180
                        DEFS LOG_LEVEL=2
                             "EM16ABL_COM=\"${CMAKE_SOURCE_DIR}/tests/fixtures/cpm/em16abl.com\""
                             "EM256FUL_COM=\"${CMAKE_SOURCE_DIR}/tests/fixtures/cpm/em256ful.com\"")

# Erste Eingabe nach der Reset-Taste (Befund S1): Schattenram-Falle beim Warten auf
# den Prompt, Helfer k1520test::wipeVram().
k1520_add_test(kbd_nach_reset SRC test_kbd_nach_reset.cpp LIBS ${_libs}
                              LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# PRG 710 / 710-1 (doc/design/20_prg710.md AP-P1c): beide Varianten bauen, Netz-Ein + Takte,
# Verdrahtung der Speicherverwaltung, Marken-FF-Polarität.  Kein Diskettenlauf.
k1520_add_test(prg710_maschine SRC test_prg710_maschine.cpp
                          LIBS k1520_prg710 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2)

# PRG 710 / 710-1 Etappe 1 (AP-P1d): echte Boot-ROMs bis zur Tastaturabfrage, Starttaste
# ohne Diskette (DISKERROR C2) und ohne Laufwerke (LW0/LW1 DEF), Registerfolge der
# Speicherverwaltung, Marken-FF-Ruhepegel je Variante, Bootsektor von der UDOS-Diskette.
k1520_add_test(prg710_boot SRC test_prg710_boot.cpp
                          LIBS k1520_prg710 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2)

# PRG 710 / 710-1 Etappe 3 (AP-P3): UDOS 4.3 bis zum Prompt.
k1520_add_test(prg710_udos SRC test_prg710_udos.cpp
                          LIBS k1520_prg710 k1520_fs k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2)

# PRG 710 / 710-1 Etappe 4 (AP-P4): K8025-Kanäle über den SerialHub nach außen,
# Namen nach Gerätebeschriftung; UDOS `SET PRI TO V24` in eine Datei.
k1520_add_test(prg710_seriell SRC test_prg710_seriell.cpp
                          LIBS k1520_prg710 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2)

# PRG 710 / 710-1 Etappe 5 (AP-P5e): SCPX 1526 V1.7 am 710-1 bis `A>`, `DIR`/`STAT`/`PIP`
# nach B:, SCPX-Bootdiskette für den PRG 710.
k1520_add_test(prg710_scpx SRC test_prg710_scpx.cpp
                          LIBS k1520_prg710 k1520_fs k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2)

# PRG 710 / 710-1 (AP-P7b): EPROMmer — `PROG` (UDOS) kopiert ein eingelegtes Abbild
# bytegleich in einen leeren virtuellen Sockel (U2716 und U555), falscher Typ brennt nicht.
k1520_add_test(prg710_eprom SRC test_prg710_eprom.cpp
                          LIBS k1520_prg710 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 300)

# PRG 710 / 710-1 (AP-P5f): FORMAT unter UDOS und SCPX auf einer Leerdiskette.
k1520_add_test(prg710_format SRC test_prg710_format.cpp
                          LIBS k1520_prg710 k1520_fs k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# DiskTool (AP-P6): bootfähige PRG-Disketten (UDOS, SCPX aus Modulen) — gebaut mit dem
# Werkzeug, gebootet am PRG beider Varianten; Fassung des Bootabbilds erkannt/abgewiesen.
# AP-P5h: dieselbe Datei prüft auch die ins Paket gelegten Systemdisketten aus disks/.
k1520_add_test(disktool_prg710 SRC test_disktool_prg710.cpp
                          LIBS k1520_prg710 k1520_fs k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2
                          "PRG710_LIEFERDISKETTEN=\"${CMAKE_SOURCE_DIR}/disks\"" TIMEOUT 150)

# PRG 710 / 710-1 (AP-P8b): ADA K6022 — Lochband stanzen und lesen unter UDOS
# (`TWRITE.1215`/`TREAD.1210` mit dem Treiber `PTAPE.6022`).
k1520_add_test(prg710_lochband SRC test_prg710_lochband.cpp
                          LIBS k1520_prg710 k1520_fs k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

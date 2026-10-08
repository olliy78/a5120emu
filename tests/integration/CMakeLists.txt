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
# K8915 Generation 2 (doc/design/24_k8915_varianten.md AP-V6a): K2521 + K3528 statt der
# ZRE 045-8762 — Selbsttest (ROM-Fehler C des Abzugs, geflickte Summe im Test), Kaltstart-
# meldung, V3-Systemdiskette 901 bis zum Prompt.
k1520_add_test(k8915g2_boot SRC test_k8915g2_boot.cpp
                          LIBS k1520_k8915 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

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

# PRG 710-1 (AP-P8c): ASS 590069 — SCPX mit dem Fernschreiber-BIOS B17272FS druckt über
# den Anschluss „Fernschreiber“ in eine Datei (Systemdiskette aus den Modulen).
k1520_add_test(prg710_fernschreiber SRC test_prg710_fernschreiber.cpp
                          LIBS k1520_prg710 k1520_fs k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 120)

# PC 1715 (doc/design/21_pc1715.md AP-1b): Maschine bauen, echter Urlader S502 (kopiert sich,
# blendet das ROM aus), Bildwechsel.  Ohne Floppy; das Verhalten danach ist AP-1c.
k1520_add_test(pc1715_maschine SRC test_pc1715_machine.cpp
                          LIBS k1520_pc1715 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2)

# PC 1715 Etappe 2 (doc/design/21_pc1715.md AP-2): K5122 „1715" im /WAIT-Betrieb, S502 lädt
# Spur 0 Sektor 1, Betriebssysteme bis zum Prompt (ohne Tastatur).
k1520_add_test(pc1715_boot SRC test_pc1715_boot.cpp
                          LIBS k1520_pc1715 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 300)

# PC 1715 Etappe 3 (doc/design/21_pc1715.md AP-3): Tastatur1715 an SIO0-A, DIR/STAT/PIP unter
# SCP 1715, DIR unter CP/A 1715, ein Kommando unter UDOS 1715.
k1520_add_test(pc1715_tastatur SRC test_pc1715_tastatur.cpp
                          LIBS k1520_pc1715 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 600)

# PC 1715 Etappe 4 (AP-4a): Drucker X4 / V.24 X5 am SerialHub, Leitung 107, Drucken unter CP/A,
# V.24-Boot des Urladers S502 ueber Telnet.
k1520_add_test(pc1715_seriell SRC test_pc1715_seriell.cpp
                          LIBS k1520_pc1715 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 300)

# DiskTool (AP-D): CP/A-1715-Disketten ohne Systemspuren (Bootkopf im Verzeichnis), bootfähige
# 1715-/SCP-3.0-Disketten aus Systemspuren, alle pc1715*-Abzüge gegen doc/pc1715/disketten.md.
k1520_add_test(disktool_pc1715 SRC test_disktool_pc1715.cpp
                          LIBS k1520_pc1715 k1520_fs k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 600)

# PC 1715W Etappe 6+7 (AP-W3): S550 → SCP-3.0-Lader → SCP3.SYS → A>, Zeichensatz aus dem
# ZG-RAM, DIR über die Tastatur, INIT.COM formatiert B:.
k1520_add_test(pc1715w_boot SRC test_pc1715w_boot.cpp
                          LIBS k1520_pc1715 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 600)

# PC 1715 AP-4d: Werkstest PCTEST.COM unter CP/A 1715 aus der Workbench.
k1520_add_test(pc1715_pctest SRC test_pc1715_pctest.cpp
                          LIBS k1520_pc1715 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 600)

# RAM-Floppy RAF in allen drei Maschinen (doc/design/22_raf512.md §5.2, AP-R2): Bestückung,
# 88H/89H über den Systembus, Reset erhält / Netz-Ein verwirft, Save-State v8, A5120.16 + RAF.
k1520_add_test(raf_maschinen SRC test_raf_maschinen.cpp
                             LIBS k1520_a5120 k1520_k8915 k1520_prg710 k1520_logger k1520_testsupport
                             LABELS "${_lbl}" DEFS LOG_LEVEL=2)

# Lochstreifen-Karte K6022 als Option in allen Maschinen (doc/design/23_lochstreifen.md AP-L1):
# stecken, zu spät, doppelt, Tor belegt; Z80-Gastcode stanzt über E0H und liest über E4H.
k1520_add_test(k6022_maschinen SRC test_k6022_maschinen.cpp
                               LIBS k1520_a5120 k1520_k8915 k1520_prg710 k1520_logger k1520_testsupport
                               LABELS "${_lbl}" DEFS LOG_LEVEL=2)

# RAM-Floppy RAF, Gastnachweis mit den Original-Treibern (doc/design/22_raf512.md §8 AP-R5):
# RAFCPM.COM (M:) / RAF512.COM (P:) unter CP/A, SCPX 8915 und SCPX 1526 am PRG 710-1 —
# Kapazitätstest, Kennung RAFvalid.SYS, PIP/DIR, RESET erhält, Netz-Ein verwirft.
set(_raf_dir "RAF_FIXTURE_DIR=\"${CMAKE_SOURCE_DIR}/tests/fixtures/raf\"")
k1520_add_test(raf_a5120 SRC test_raf_a5120.cpp LIBS ${_libs} k1520_fs
                         LABELS "${_lbl}" DEFS LOG_LEVEL=2 ${_raf_dir} TIMEOUT 180)
k1520_add_test(raf_k8915 SRC test_raf_k8915.cpp
                         LIBS k1520_k8915 k1520_fs k1520_logger k1520_testsupport
                         LABELS "${_lbl}" DEFS LOG_LEVEL=2 ${_raf_dir} TIMEOUT 180)
k1520_add_test(raf_prg710 SRC test_raf_prg710.cpp
                          LIBS k1520_prg710 k1520_fs k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 ${_raf_dir} TIMEOUT 180)

# CP/A-BIOS mit eingebautem RAF-Treiber (AP-R7, raf = 1, rafpar = 1): M: nach dem Kaltstart,
# RESET erhält, ohne Karte kein M:, DPB-Wahl für RAF128/RAF512/RAF-2M.
k1520_add_test(raf_cpa_bios SRC test_raf_cpa_bios.cpp LIBS ${_libs}
                            LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 300)

# ZWG-Prüfprogramm RAFTEST.COM (AP-R6) gegen die RAF512 am A5120: Vollauf 512 KByte,
# Sperrmuster 90H/10H/00H.  Kartenadresse 88H/89H im Patchbereich der Testkopie.
k1520_add_test(raf_zwg SRC test_raf_zwg.cpp LIBS ${_libs} k1520_fs
                       LABELS "${_lbl}" DEFS LOG_LEVEL=2 ${_raf_dir} TIMEOUT 300)

# P8000 8-Bit-Seite (doc/design/25_p8000.md §10.11 AP P7a): Laufschleife, MON8 am Kern-Terminal,
# RESI, NMI-Weiche, DMA hält die CPU, keine Zusatzkarten.
k1520_add_test(p8000_maschine SRC test_p8000_machine.cpp
                          LIBS k1520_p8000 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 300)

# P8000 Meilenstein M1 (doc/design/25_p8000.md §10.11 AP P7d): UDOS von der WEGA-Startdiskette.
k1520_add_test(p8000_boot SRC test_p8000_boot.cpp
                          LIBS k1520_p8000 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 300)

# P8000 Save-State P8KS v1 (doc/design/25_p8000.md §10.2, AP P7e): Rundreise mitten im Laden von
# Diskette (FDC + DMA aktiv), Konfigurationsfingerabdruck, defekter Zustand lässt alles stehen.
k1520_add_test(p8000_stand SRC test_p8000_stand.cpp
                          LIBS k1520_p8000 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 300)

# P8000 Meilenstein M2 (doc/design/25_p8000.md §10.11 AP P11): Kopplung 8 ↔ 16 in der Maschine,
# U8000-Monitor über tty1, Hardwaretest U8001, `O U` + `boot` von der WEGA-Startdiskette.
# P8000 Kopplung K1–K15 an den echten Karten + Save-State P8KS v2 (AP P11, Entwurf 25 §10.3).
k1520_add_test(p8000_kopplung SRC test_p8000_kopplung.cpp
                          LIBS k1520_p8000 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 300)

k1520_add_test(p8000_monitor16 SRC test_p8000_monitor16.cpp
                          LIBS k1520_p8000 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 600)

# P8000 mit WDC 4.2 an der 16-Bit-PIO2 (doc/design/25_p8000.md §10.11 AP P13d): Hardwaretest U8001
# ohne Fehler, Init-Fehler 39 ohne PAR, Save-State P8KS v3, Plattenschnittstelle.
k1520_add_test(p8000_wdc_maschine SRC test_p8000_wdc_maschine.cpp
                          LIBS k1520_p8000 k1520_logger k1520_testsupport
                          LABELS "${_lbl}" DEFS LOG_LEVEL=2 TIMEOUT 600)

# Originalterminal P8000 Typ 2 + K7673.09 als Einheit (Entwurf 28, AP P20a/b): Tastatur → Leitung,
# Rechner → Bild über einen Kartenkanal, Austauschbarkeit mit dem Kern-Terminal, Telnet am Hub.
k1520_add_test(p8000_terminal_einheit SRC test_p8000_terminal_einheit.cpp
                          LIBS k1520_p8000_terminal_hw k1520_serial
                          LABELS "${_lbl}" TIMEOUT 120)

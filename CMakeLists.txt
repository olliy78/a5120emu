cmake_minimum_required(VERSION 3.16)
# C steht neben CXX, weil der Zeileneditor des Debuggers (third_party/isocline,
# MIT) reines C ist — ohne das meldet CMake beim Erzeugen „Missing variable is:
# CMAKE_C_CREATE_STATIC_LIBRARY". Eigener Kerncode ist unverändert C++.
project(a5120emu VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default-Build-Type: OHNE dies baut GCC mit -O0 (keine Optimierung) — für einen
# Z80-Interpreter der mit Abstand größte Performance-Verlust (~5-8x langsamer).
# Wird nur gesetzt, wenn der Aufrufer keinen expliziten Typ vorgibt.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Release/RelWithDebInfo/Debug)" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE nicht gesetzt → Release (-O3)")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Plattform: Windows/MSVC   (doc/design/13_distribution.md §6.1)
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)
    # NOMINMAX:            windows.h definiert sonst min/max als MAKRO und zerlegt
    #                      jedes std::min/std::max im Kern.
    # WIN32_LEAN_AND_MEAN: spart den halben Win32-Kopfdateibaum (Winsock, OLE, …).
    # UNICODE:             die Modulpfad-Auflösung benutzt GetModuleFileNameW.
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

if(MSVC)
    # /utf-8: die Quellen sind UTF-8 und stecken VOLLER deutscher Umlaute (Kommentare
    #         UND Zeichenketten).  Ohne das liest MSVC sie in der Systemcodepage —
    #         Umlaute in Log-/Fehlertexten kommen als Buchstabensalat heraus, und in
    #         Kommentaren löst ein zerbrochenes Byte Warnung C4819 aus.
    add_compile_options(/utf-8)
    # /permissive-: Zwei-Phasen-Namensauflösung wie GCC/Clang — sonst nimmt MSVC
    #               Vorlagenfehler erst bei der Instanziierung wahr.
    add_compile_options(/permissive- /W3)
    # /bigobj: die Kartenmodelle erzeugen sehr grosse Objektdateien (Vorlagen +
    #          eingebettete EPROM-Felder) und reissen sonst das Sektionslimit.
    add_compile_options(/bigobj)
    # C4996: MSVC hält halb POSIX für „unsicher" (fopen, getenv …).  Wir benutzen
    #        die Standardformen bewusst; der Kern soll auf allen drei Systemen
    #        DIESELBE Quelle sein.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    # Statische CRT — NUR für Auslieferungsbauten.  Der Windows-Installer läuft
    # per-user ohne Administratorrechte und kann deshalb KEIN VC-Redist
    # nachinstallieren (§5.1); die ausgelieferte DLL muss ihre Laufzeit selbst
    # mitbringen.  Vorgabe OFF, weil GoogleTest im selben Baum die dynamische CRT
    # erwartet (gtest_force_shared_crt) — /MT und /MD in einem Bau geben
    # „mismatch detected for RuntimeLibrary" beim Linken.  Der Release-Workflow
    # baut daher mit -DK1520_MSVC_STATIC_CRT=ON UND -DBUILD_K1520_TESTS=OFF.
    option(K1520_MSVC_STATIC_CRT "Auslieferungsbau: CRT statisch einbinden (/MT)" OFF)
    if(K1520_MSVC_STATIC_CRT)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        message(STATUS "MSVC: statische CRT (/MT) — Auslieferungsbau")
    endif()
endif()

if(NOT LOG_LEVEL)
    set(LOG_LEVEL 3)
endif()
add_compile_definitions(LOG_LEVEL=${LOG_LEVEL})
message(STATUS "Global LOG_LEVEL=${LOG_LEVEL}")

# HINWEIS: Der alte monolithische A5120-Emulator (src/, Targets a5120emu / cparun /
# a5120emu_test mit CP/M-BIOS-HALT-Traps) wurde entfernt — der modulare K1520-Kern
# unter core/ ist die einzige Implementierung.  Der eigenständige CP/M-Runner lebt
# unverändert als Unterprojekt in cparun/ (eigenes CMakeLists.txt, eigene Quellen).

# ─────────────────────────────────────────────────────────────────────────────
# K1520 core emulator (new modular implementation)
# ─────────────────────────────────────────────────────────────────────────────

option(BUILD_K1520 "Build new K1520 modular emulator" ON)

if(BUILD_K1520)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    add_library(k1520_logger STATIC
        core/logger.cpp
    )
    target_include_directories(k1520_logger PUBLIC ${CMAKE_SOURCE_DIR})

    # Bus library
    add_library(k1520_bus STATIC
        core/bus/k1520_bus.cpp
    )
    target_include_directories(k1520_bus PUBLIC ${CMAKE_SOURCE_DIR})
    target_link_libraries(k1520_bus PUBLIC k1520_logger)

    # Minimaler YAML-Subset-Parser (Konfigurationsdateien des Kerns, §8.6.2)
    add_library(k1520_util STATIC
        core/util/yaml_lite.cpp
    )
    target_include_directories(k1520_util PUBLIC ${CMAKE_SOURCE_DIR})

    # Serielle Schnittstellen nach aussen: reine Protokoll-Codecs ohne Socket (Entwurf 19, AP-S2)
    add_library(k1520_serial_codec STATIC
        core/serial/telnet_codec.cpp
        core/serial/rfc2217_codec.cpp
    )
    target_include_directories(k1520_serial_codec PUBLIC ${CMAKE_SOURCE_DIR})

    # Netzschicht der seriellen Schnittstellen (Entwurf 19): Sockets POSIX/Winsock2,
    # Adressklassifikation, Portsuche.  Eigene Bibliothek, damit Tests und der spätere
    # SerialHub sie ohne den Rest des Kerns binden.  ws2_32 gilt für MinGW UND MSVC.
    add_library(k1520_serial_net STATIC
        core/serial/net/socket.cpp
        core/serial/net/adresse.cpp
    )
    target_include_directories(k1520_serial_net PUBLIC ${CMAKE_SOURCE_DIR})
    if(WIN32)
        target_link_libraries(k1520_serial_net PUBLIC ws2_32)
    endif()

    # Serielle Schnittstellen nach aussen (Entwurf 19, AP-S4): SerialAnschluss, Wandler,
    # SerialHub mit I/O-Faden, Transporte Telnet/RFC2217/Datei.  Kennt keine Karte —
    # die Karten liefern je Schnittstelle einen SerialAnschluss (AP-S5).
    find_package(Threads REQUIRED)
    add_library(k1520_serial STATIC
        core/serial/anschluss.cpp
        core/serial/wandler.cpp
        core/serial/transport.cpp
        core/serial/hub.cpp
    )
    target_include_directories(k1520_serial PUBLIC ${CMAKE_SOURCE_DIR})
    target_link_libraries(k1520_serial PUBLIC k1520_serial_net k1520_serial_codec Threads::Threads)

    # Floppy peripheral: Diskettenformat-Datenmodell (DiskFormat/TrackFormat)
    add_library(k1520_floppy STATIC
        core/peripherals/floppy_drive/disk_format.cpp
    )
    target_include_directories(k1520_floppy PUBLIC ${CMAKE_SOURCE_DIR})

    # Neue Peripherie-Schicht: Track-Bild-Abstraktion + FM/MFM-Codec + Drive/Image
    add_library(k1520_floppy2 STATIC
        core/peripherals/floppy_drive/track_image.cpp
        core/peripherals/floppy_drive/track_codec.cpp
        core/peripherals/floppy_drive/track_view.cpp
        core/peripherals/floppy_drive/bit_codec.cpp
        core/peripherals/floppy_drive/drive_profile.cpp
        core/peripherals/floppy_drive/disk_medium.cpp
        core/peripherals/floppy_drive/img_codec.cpp
        core/peripherals/floppy_drive/hfe_codec.cpp
        core/peripherals/floppy_drive/dmk_codec.cpp
        core/peripherals/floppy_drive/image_codec.cpp
        core/peripherals/floppy_drive/disk_image.cpp
        core/peripherals/floppy_drive/track_sync.cpp
        core/peripherals/floppy_drive/floppy_drive2.cpp
        core/peripherals/floppy_drive/format_catalog.cpp
    )
    # ${CMAKE_DL_LIBS}: FormatCatalog fragt per dladdr(), in welchem Modul es selbst
    # liegt — davon hängt ab, wo es formats.yaml sucht (siehe format_catalog.h).
    # Ab glibc 2.34 steckt dladdr in der libc, davor braucht es -ldl.
    # k1520_logger: FloppyDriveV2 meldet verworfene Schreibzugriffe (Spurdichte-Anpassung).
    target_link_libraries(k1520_floppy2 PUBLIC k1520_floppy k1520_util k1520_logger ${CMAKE_DL_LIBS})
    target_include_directories(k1520_floppy2 PUBLIC ${CMAKE_SOURCE_DIR})

    # Fallback-Fundort des Diskettenformat-Katalogs (§8.6.4, Priorität 1).  Zeigt im
    # Entwicklungsbaum auf die Quelldatei, damit ctest und die Tools den Katalog ohne
    # gesetzte Umgebungsvariable finden.
    set(K1520_FORMATS_DEFAULT "${CMAKE_SOURCE_DIR}/data/formats.yaml"
        CACHE FILEPATH "Fallback-Pfad der formats.yaml")
    target_compile_definitions(k1520_floppy2
        PRIVATE K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")

    # ─── Dateisystem-Schicht (k1520DiskTool) ──────────────────────────────────
    # Rein logisch: kein Z80, keine Karten.  Setzt auf denselben Container-/Medium-
    # Bausteinen auf wie der Emulator (k1520_floppy2), sieht aber nur DiskMedium.
    # Feinentwurf: doc/design/13_k1520disktool.md
    # Die Geometrie-Erkennung steht ALLEIN, weil sie als einziges Stueck der
    # Dateisystem-Schicht auch der Emulator braucht (er zeigt das erkannte Format
    # in der Statuszeile an).  Sie haengt nur an TrackCodec; k1520_fs mit seinen
    # Dateisystemen in libk1520core.so zu ziehen waere der falsche Tausch
    # (doc/design/13_k1520disktool.md §10: die beiden Bibliotheken sind bewusst
    # getrennt und teilen nur die Container-/Medium-Bausteine).
    add_library(k1520_geometry STATIC
        core/filesystem/geometry_probe.cpp
    )
    target_link_libraries(k1520_geometry PUBLIC k1520_floppy2)
    target_include_directories(k1520_geometry PUBLIC ${CMAKE_SOURCE_DIR})

    add_library(k1520_fs STATIC
        core/filesystem/sector_space.cpp
        core/filesystem/fs_catalog.cpp
        core/filesystem/cpm/cpm_fs.cpp
        core/filesystem/cpm/cpa_dpb.cpp
        core/filesystem/udos/udos_bitmap.cpp
        core/filesystem/udos/udos_fs.cpp
        core/filesystem/udos/udos1715_fs.cpp
        core/filesystem/disk_volume.cpp
        core/filesystem/prg_boot.cpp
        core/filesystem/check/fs_check.cpp
        core/filesystem/check/fs_recover.cpp
        core/filesystem/check/cpm_check.cpp
        core/filesystem/check/cpm_recover.cpp
        core/filesystem/check/udos_check.cpp
        core/filesystem/check/udos_recover.cpp
        core/filesystem/check/udos1715_check.cpp
        core/filesystem/check/udos1715_recover.cpp
    )
    target_link_libraries(k1520_fs PUBLIC k1520_geometry k1520_floppy2 k1520_util)
    target_include_directories(k1520_fs PUBLIC ${CMAKE_SOURCE_DIR})

    # PIO primitive
    add_library(k1520_pio STATIC
        core/primitives/z80_pio.cpp
    )
    target_include_directories(k1520_pio PUBLIC ${CMAKE_SOURCE_DIR})

    # CTC primitive
    add_library(k1520_ctc STATIC
        core/primitives/z80_ctc.cpp
    )
    target_include_directories(k1520_ctc PUBLIC ${CMAKE_SOURCE_DIR})

    # Z80-DMA (UA858, PC 1715W, doc/design/21_pc1715.md AP-W2a)
    add_library(k1520_z80dma STATIC core/primitives/z80_dma.cpp)
    target_link_libraries(k1520_z80dma PUBLIC k1520_bus)
    target_include_directories(k1520_z80dma PUBLIC ${CMAKE_SOURCE_DIR})

    # SIO primitive
    add_library(k1520_sio STATIC
        core/primitives/z80_sio.cpp
    )
        # Z80 CPU primitive for the modular core
        add_library(k1520_z80 STATIC
            core/primitives/z80.cpp
        )
        target_include_directories(k1520_z80 PUBLIC ${CMAKE_SOURCE_DIR})
        # U8001/U8002 (Z8001/Z8002) CPU-Primitive; Tabelle/Dekoder header-only in core/primitives/z8000/
        add_library(k1520_z8000 STATIC
            core/primitives/z8000.cpp
        )
        target_include_directories(k1520_z8000 PUBLIC ${CMAKE_SOURCE_DIR})
        # UB8010/Z8010-MMU (P8000 AP P9a, doc/p8000/z8010_mmu.md)
        add_library(k1520_z8010 STATIC
            core/primitives/z8010.cpp
        )
        target_include_directories(k1520_z8010 PUBLIC ${CMAKE_SOURCE_DIR})

    target_link_libraries(k1520_sio PUBLIC k1520_bus)
    target_include_directories(k1520_sio PUBLIC ${CMAKE_SOURCE_DIR})

    # K3526 OPS – Operationsspeicher (64KB DRAM card)
    add_library(k1520_k3526 STATIC core/cards/k3526/k3526.cpp)
    target_include_directories(k1520_k3526 PUBLIC ${CMAKE_SOURCE_DIR})

    # K7637 serial keyboard peripheral
    add_library(k1520_k7637 STATIC
        core/peripherals/k7637/k7637.cpp
    )
    target_link_libraries(k1520_k7637 PUBLIC k1520_sio)
    target_include_directories(k1520_k7637 PUBLIC ${CMAKE_SOURCE_DIR})

    # K7024 ABS – Abbildsteuerung Bildschirm (screen controller card)
    add_library(k1520_k7024 STATIC core/cards/k7024/k7024.cpp)
    target_link_libraries(k1520_k7024 PUBLIC k1520_bus)
    target_include_directories(k1520_k7024 PUBLIC ${CMAKE_SOURCE_DIR})

    # K2526 ZRE – Zentrale Recheneinheit (CPU card, enthält jetzt ZVE1)
    add_library(k1520_k2526 STATIC core/cards/k2526/k2526.cpp)
    target_link_libraries(k1520_k2526 PUBLIC k1520_z80 k1520_pio k1520_ctc k1520_bus)
    target_include_directories(k1520_k2526 PUBLIC ${CMAKE_SOURCE_DIR})

    # K8025 ASS – Anschlußsteuerung Seriell (serial interface card)
    add_library(k1520_k8025 STATIC core/cards/k8025/k8025.cpp)
    target_link_libraries(k1520_k8025 PUBLIC k1520_sio k1520_ctc k1520_pio k1520_bus k1520_serial)
    target_include_directories(k1520_k8025 PUBLIC ${CMAKE_SOURCE_DIR})

    # K5122 AFS – Anschlusssteuerung Floppy-disk Speicher (formatagnostischer
    # Streaming-Controller, TrackImage/DiskImage-basiert; bootet CP/A inkl. HFE)
    add_library(k1520_k5122 STATIC core/cards/k5122/k5122.cpp core/cards/k5122/k5122_wait.cpp)
    target_link_libraries(k1520_k5122 PUBLIC k1520_pio k1520_floppy2 k1520_bus k1520_logger)
    target_include_directories(k1520_k5122 PUBLIC ${CMAKE_SOURCE_DIR})

    # Laufwerksverwaltung (K5122 + vier Laufwerke + Formatkatalog) — gemeinsamer
    # Baustein von A5120 und K8915 (doc/design/16_k8915.md §7.1)
    add_library(k1520_laufwerke STATIC core/machines/laufwerke.cpp)
    target_link_libraries(k1520_laufwerke PUBLIC k1520_k5122 k1520_floppy2 k1520_geometry k1520_logger)
    target_include_directories(k1520_laufwerke PUBLIC ${CMAKE_SOURCE_DIR})

    # EM064/EM256 – Erweiterungsmodul des A5120.16 mit U8001/U8002 (doc/design/17_a5120_16.md)
    add_library(k1520_em STATIC core/cards/em/em.cpp)
    target_link_libraries(k1520_em PUBLIC k1520_pio k1520_z8000 k1520_bus k1520_logger)
    target_include_directories(k1520_em PUBLIC ${CMAKE_SOURCE_DIR})

    # RAM-Floppy RAF 128/512/2M (doc/design/22_raf512.md)
    add_library(k1520_raf STATIC core/cards/raf/raf.cpp)
    target_link_libraries(k1520_raf PUBLIC k1520_bus k1520_logger)
    target_include_directories(k1520_raf PUBLIC ${CMAKE_SOURCE_DIR})

    # A5120 machine (wires all cards + Z80 CPU)
    add_library(k1520_a5120 STATIC
        core/machines/a5120/a5120.cpp
    )
    target_link_libraries(k1520_a5120 PUBLIC
        k1520_z80
        k1520_k2526 k1520_k3526 k1520_k7024 k1520_k8025 k1520_k5122 k1520_laufwerke k1520_em k1520_raf k1520_k6022
        k1520_k7637 k1520_floppy k1520_floppy2 k1520_geometry k1520_bus k1520_logger k1520_serial
    )
    target_include_directories(k1520_a5120 PUBLIC ${CMAKE_SOURCE_DIR})

    # ZRE 045-8762 (K8915): U880, CTC, 4-KB-Boot-EPROM, 2 × 64 KB DRAM, Register A8H
    add_library(k1520_zre8762 STATIC core/cards/zre8762/zre8762.cpp)
    target_link_libraries(k1520_zre8762 PUBLIC k1520_z80 k1520_ctc k1520_bus k1520_logger)
    target_include_directories(k1520_zre8762 PUBLIC ${CMAKE_SOURCE_DIR})

    # ZRE K2521 (PRG 710/710-1): U880, ROM 1 KB (U555), 1 KB RAM, CTC 80H, PIO 84H
    add_library(k1520_k2521 STATIC core/cards/k2521/k2521.cpp)
    target_link_libraries(k1520_k2521 PUBLIC k1520_z80 k1520_ctc k1520_pio k1520_bus k1520_logger)
    target_include_directories(k1520_k2521 PUBLIC ${CMAKE_SOURCE_DIR})

    # K3528 (K8915 Gen 2): 64 KB DRAM, Register A8H-ABH -> /MEMDI, doc/design/24_k8915_varianten.md R3
    add_library(k1520_k3528 STATIC core/cards/k3528/k3528.cpp)
    target_link_libraries(k1520_k3528 PUBLIC k1520_bus k1520_logger)
    target_include_directories(k1520_k3528 PUBLIC ${CMAKE_SOURCE_DIR})

    # PFS K3820 (012-7040): 16 x 2708, X8/X9 Start, X6/X7 MEMDI — von keiner Maschine benutzt (Entwurf 24 AP-V11)
    add_library(k1520_k3820 STATIC core/cards/k3820/k3820.cpp)
    target_link_libraries(k1520_k3820 PUBLIC k1520_bus k1520_logger)
    target_include_directories(k1520_k3820 PUBLIC ${CMAKE_SOURCE_DIR})

    # Speicherverwaltung E8H-EBH + OPS-RAM (PRG 710/710-1), Arbeitsmodell doc/design/20_prg710.md §4b
    add_library(k1520_prg710_speicher STATIC core/cards/prg710_speicher/prg710_speicher.cpp)
    target_link_libraries(k1520_prg710_speicher PUBLIC k1520_bus k1520_logger)
    target_include_directories(k1520_prg710_speicher PUBLIC ${CMAKE_SOURCE_DIR})

    # P8000, 8-Bit-Karte: ADP-gebankter Speicher + RFF + EPROM/SRAM/DRAM (doc/design/25_p8000.md §10.1, AP P5a)
    add_library(k1520_p8000_speicher8 STATIC core/cards/p8000/speicher8.cpp)
    target_link_libraries(k1520_p8000_speicher8 PUBLIC k1520_bus k1520_logger)
    target_include_directories(k1520_p8000_speicher8 PUBLIC ${CMAKE_SOURCE_DIR})

    # P8000, 8-Bit-Rechnerkarte: U880, E/A-Dekoder, CTC/SIO/PIO, Kette, Reset/NMI (doc/design/25_p8000.md, AP P5e)
    add_library(k1520_p8000_karte8 STATIC core/cards/p8000/karte8.cpp)
    target_link_libraries(k1520_p8000_karte8 PUBLIC k1520_p8000_speicher8 k1520_z80 k1520_ctc k1520_sio k1520_pio k1520_bus k1520_logger k1520_serial)
    target_include_directories(k1520_p8000_karte8 PUBLIC ${CMAKE_SOURCE_DIR})

    # P8000, Floppy-Seite der 8-Bit-Karte: U8272 + UA858 + PIO2-Glue + Laufwerke (AP P5f)
    add_library(k1520_p8000_floppy8 STATIC core/cards/p8000/floppy8.cpp)
    target_link_libraries(k1520_p8000_floppy8 PUBLIC k1520_p8000_karte8 k1520_upd765 k1520_z80dma k1520_laufwerke k1520_k5122)
    target_include_directories(k1520_p8000_floppy8 PUBLIC ${CMAKE_SOURCE_DIR})

    # P8000, 16-Bit-Karte: MMU-Steuerlogik — drei UB8010, Auswahl, NBR, On-Board, TRPL/IF1L (AP P9b)
    add_library(k1520_p8000_mmu_logik16 STATIC core/cards/p8000/mmu_logik16.cpp)
    target_link_libraries(k1520_p8000_mmu_logik16 PUBLIC k1520_z8010)
    target_include_directories(k1520_p8000_mmu_logik16 PUBLIC ${CMAKE_SOURCE_DIR})

    # P8000, 16-Bit-Teil: DRAM-Karten 256 K/1 M am Speicherbus, Moduladresse, Parität (AP P10a)
    add_library(k1520_p8000_dram16 STATIC core/cards/p8000/dram16.cpp)
    target_include_directories(k1520_p8000_dram16 PUBLIC ${CMAKE_SOURCE_DIR})

    # P8000, 16-Bit-Rechnerkarte: U8001, On-Board-Speicher, CTC/SIO/PIO, E/A, NMI/Reset (AP P10b)
    add_library(k1520_p8000_karte16 STATIC core/cards/p8000/karte16.cpp)
    target_link_libraries(k1520_p8000_karte16 PUBLIC k1520_p8000_mmu_logik16 k1520_p8000_dram16 k1520_z8000 k1520_ctc k1520_sio k1520_pio k1520_bus k1520_logger k1520_serial)
    target_include_directories(k1520_p8000_karte16 PUBLIC ${CMAKE_SOURCE_DIR})

    # P8000, Winchesterlaufwerk am WDC: LBA-Abbild, Spursynthese/-zerlegung, Mechanik (AP P13b)
    add_library(k1520_winchester STATIC core/peripherals/winchester/platte.cpp)
    target_link_libraries(k1520_winchester PUBLIC k1520_logger)
    target_include_directories(k1520_winchester PUBLIC ${CMAKE_SOURCE_DIR})

    # P8000, Winchester-Disk-Controller: UA880 + CTC + Original-Firmware, Host-/Disk-Logik (AP P13c)
    add_library(k1520_p8000_wdc STATIC core/cards/p8000/wdc.cpp)
    target_link_libraries(k1520_p8000_wdc PUBLIC k1520_winchester k1520_z80 k1520_ctc k1520_bus k1520_logger)
    target_include_directories(k1520_p8000_wdc PUBLIC ${CMAKE_SOURCE_DIR})

    # P8000, Kopplung 8-Bit ↔ 16-Bit als Leitungsmodell K1–K15 (doc/design/25_p8000.md §10.3, AP P11)
    add_library(k1520_p8000_kopplung STATIC core/cards/p8000/kopplung.cpp)
    target_link_libraries(k1520_p8000_kopplung PUBLIC k1520_p8000_karte8 k1520_p8000_karte16)
    target_include_directories(k1520_p8000_kopplung PUBLIC ${CMAKE_SOURCE_DIR})

    # 8279-Tastaturcontroller, Tastatur K7609, ATP 590068 (PRG 710, doc/design/20_prg710.md AP-P2a)
    add_library(k1520_i8279 STATIC core/primitives/i8279.cpp)
    target_link_libraries(k1520_i8279 PUBLIC k1520_logger)
    target_include_directories(k1520_i8279 PUBLIC ${CMAKE_SOURCE_DIR})
    # 8275-CRT-Controller (PC 1715, doc/design/21_pc1715.md AP-1a)
    add_library(k1520_i8275 STATIC core/primitives/i8275.cpp)
    target_link_libraries(k1520_i8275 PUBLIC k1520_logger)
    target_include_directories(k1520_i8275 PUBLIC ${CMAKE_SOURCE_DIR})
    # U8272 / µPD765 Floppy-Controller auf Sektorebene (PC 1715W, doc/design/21_pc1715.md AP-W2)
    add_library(k1520_upd765 STATIC core/primitives/upd765.cpp)
    target_link_libraries(k1520_upd765 PUBLIC k1520_floppy2 k1520_logger)
    target_include_directories(k1520_upd765 PUBLIC ${CMAKE_SOURCE_DIR})
    add_library(k1520_k7609 STATIC core/peripherals/k7609/k7609.cpp)
    target_link_libraries(k1520_k7609 PUBLIC k1520_i8279)
    target_include_directories(k1520_k7609 PUBLIC ${CMAKE_SOURCE_DIR})
    add_library(k1520_atp590068 STATIC core/cards/atp590068/atp590068.cpp
        core/cards/atp590068/eprommer590068.cpp)
    target_link_libraries(k1520_atp590068 PUBLIC k1520_i8279 k1520_pio k1520_bus k1520_logger)
    target_include_directories(k1520_atp590068 PUBLIC ${CMAKE_SOURCE_DIR})

    # ADA K6022 (Lochband über SIF1000) und ASS 590069 (Fernschreiber) — PRG 710, AP-P8
    # Bandformate Roh / Intel HEX / ASCII-Art (doc/design/23_lochstreifen.md §4)
    add_library(k1520_lochstreifen STATIC core/peripherals/lochstreifen/band_format.cpp)
    target_include_directories(k1520_lochstreifen PUBLIC ${CMAKE_SOURCE_DIR})
    add_library(k1520_k6022 STATIC core/cards/k6022/k6022.cpp)
    target_link_libraries(k1520_k6022 PUBLIC k1520_pio k1520_bus k1520_logger k1520_lochstreifen)
    target_include_directories(k1520_k6022 PUBLIC ${CMAKE_SOURCE_DIR})
    add_library(k1520_ass590069 STATIC core/cards/ass590069/ass590069.cpp)
    target_link_libraries(k1520_ass590069 PUBLIC k1520_sio k1520_ctc k1520_bus k1520_serial)
    target_include_directories(k1520_ass590069 PUBLIC ${CMAKE_SOURCE_DIR})

    # ATS K7028.30 (K8915): 2 × SIO, 2 × CTC, Anzeigelatch; Rückschleife als Option
    add_library(k1520_k7028 STATIC core/cards/k7028/k7028.cpp)
    target_link_libraries(k1520_k7028 PUBLIC k1520_sio k1520_ctc k1520_bus k1520_logger k1520_serial)
    target_include_directories(k1520_k7028 PUBLIC ${CMAKE_SOURCE_DIR})

    # Tastatur K7672 (K8915): Protokollmodell, SCP-Modus
    add_library(k1520_k7672 STATIC core/peripherals/k7672/k7672.cpp)
    target_link_libraries(k1520_k7672 PUBLIC k1520_sio k1520_logger)
    target_include_directories(k1520_k7672 PUBLIC ${CMAKE_SOURCE_DIR})

    # K8915 V3 (doc/design/16_k8915.md) — ZRE + ATS/K7672 + K7024 + K5122 (/WAIT).  Seit
    # AP-E4b in libk1520core.so: k1520_create(K1520_MACHINE_K8915).  Gen 2 (Entwurf 24,
    # AP-V6a): K2521 + K3528 statt der ZRE 045-8762, Config::generation.
    add_library(k1520_k8915 STATIC core/machines/k8915/k8915.cpp)
    target_link_libraries(k1520_k8915 PUBLIC
        k1520_zre8762 k1520_k2521 k1520_k3528 k1520_k7028 k1520_k7672 k1520_k7024 k1520_k5122 k1520_laufwerke k1520_raf k1520_k6022
        k1520_floppy2 k1520_bus k1520_logger k1520_serial)
    target_include_directories(k1520_k8915 PUBLIC ${CMAKE_SOURCE_DIR})

    # PRG 710 / 710-1 (doc/design/20_prg710.md AP-P1c) — K2521 + Speicherverwaltung + K7024 +
    # K5122 (/WAIT) + K8025.  In libk1520core.so: k1520_create(K1520_MACHINE_PRG710).
    add_library(k1520_prg710 STATIC core/machines/prg710/prg710.cpp)
    target_link_libraries(k1520_prg710 PUBLIC
        k1520_k2521 k1520_prg710_speicher k1520_k8025 k1520_k7024 k1520_k5122 k1520_laufwerke
        k1520_atp590068 k1520_k7609 k1520_k7672 k1520_k6022 k1520_ass590069 k1520_raf
        k1520_floppy2 k1520_bus k1520_logger k1520_serial)
    target_include_directories(k1520_prg710 PUBLIC ${CMAKE_SOURCE_DIR})

    # Tastatur des PC 1715: U880 + EPROM S600 auf dem Z80-Kern (doc/design/21_pc1715.md AP-3a)
    # P8000-Terminal (ADM31/VT100-Textschicht + Anbindung an einen SerialAnschluss), Entwurf 25 §10.6
    add_library(k1520_p8000_terminal STATIC
        core/peripherals/p8000_terminal/terminal.cpp
        core/peripherals/p8000_terminal/terminal_anschluss.cpp)
    target_link_libraries(k1520_p8000_terminal PUBLIC k1520_serial)
    target_include_directories(k1520_p8000_terminal PUBLIC ${CMAKE_SOURCE_DIR})

    add_library(k1520_tastatur1715 STATIC core/peripherals/tastatur1715/tastatur1715.cpp)
    target_link_libraries(k1520_tastatur1715 PUBLIC k1520_z80)
    target_include_directories(k1520_tastatur1715 PUBLIC ${CMAKE_SOURCE_DIR})

    # PC 1715 (doc/design/21_pc1715.md AP-1b): ZRE mit ROM-Overlay, CTC0/SIO0, 8275 + Rastern
    add_library(k1520_pc1715_zre STATIC core/cards/pc1715_zre/pc1715_zre.cpp)
    target_link_libraries(k1520_pc1715_zre PUBLIC k1520_z80 k1520_ctc k1520_sio k1520_i8275 k1520_bus k1520_logger k1520_serial)
    target_include_directories(k1520_pc1715_zre PUBLIC ${CMAKE_SOURCE_DIR})
    # Speicher des PC 1715W: BR 24H + 74S287 + Bank 0 (S550/ZG-RAM/Bild-RAM) + Bildquelle (AP-W1)
    add_library(k1520_pc1715w_speicher STATIC core/cards/pc1715w_speicher/pc1715w_speicher.cpp
                                              core/cards/pc1715w_speicher/pc1715w_bild.cpp)
    target_link_libraries(k1520_pc1715w_speicher PUBLIC k1520_i8275 k1520_bus k1520_logger)
    target_include_directories(k1520_pc1715w_speicher PUBLIC ${CMAKE_SOURCE_DIR})
    add_library(k1520_pc1715 STATIC core/machines/pc1715/pc1715.cpp)
    target_link_libraries(k1520_pc1715 PUBLIC k1520_pc1715_zre k1520_pc1715w_speicher k1520_z80dma k1520_upd765 k1520_tastatur1715 k1520_k5122 k1520_laufwerke k1520_floppy2 k1520_bus k1520_logger k1520_serial)
    target_include_directories(k1520_pc1715 PUBLIC ${CMAKE_SOURCE_DIR})

    # P8000 (doc/design/25_p8000.md §10, AP P7a): 8-Bit-Seite (Karte8 + Floppy8 + Kern-Terminal)
    add_library(k1520_p8000 STATIC core/machines/p8000/p8000.cpp)
    target_link_libraries(k1520_p8000 PUBLIC k1520_p8000_floppy8 k1520_p8000_karte8 k1520_p8000_karte16 k1520_p8000_kopplung k1520_p8000_terminal k1520_laufwerke k1520_bus k1520_logger k1520_serial)
    target_include_directories(k1520_p8000 PUBLIC ${CMAKE_SOURCE_DIR})

    # libk1520core.so — stable C-API shared library
    add_library(k1520core SHARED
        core/api/k1520_api.cpp
        core/api/k1520_sync_api.cpp   # physische Diskette — steht in BEIDEN Bibliotheken
    )
    target_link_libraries(k1520core PRIVATE k1520_a5120 k1520_k8915 k1520_prg710 k1520_pc1715 k1520_p8000 k1520_logger)
    target_include_directories(k1520core PUBLIC ${CMAKE_SOURCE_DIR})
    # K1520_BUILD_SHARED schaltet K1520_API auf __declspec(dllexport).  OHNE das
    # exportiert eine MSVC-DLL GAR NICHTS und ctypes findet keine Funktion
    # (core/api/k1520_export.h, doc/design/13_distribution.md §6.1).
    target_compile_definitions(k1520core PRIVATE K1520_BUILD_SHARED)
    set_target_properties(k1520core PROPERTIES
        CXX_STANDARD 17
        POSITION_INDEPENDENT_CODE ON
    )


    # libk1520disk.so — stabile C-ABI des k1520DiskTool (Python/PySide6).
    # BEWUSST getrennt von libk1520core.so: das Werkzeug braucht keinen Z80 und keine
    # Karten, der Emulator keine Dateisysteme; geteilt werden nur die statischen
    # Container-/Medium-Bausteine.  Feinentwurf: doc/design/13_k1520disktool.md §10
    add_library(k1520disk SHARED
        core/api/k1520_disk_api.cpp
        core/api/k1520_sync_api.cpp   # dieselbe Uebersetzungseinheit wie in k1520core
    )
    target_link_libraries(k1520disk PRIVATE k1520_fs)
    target_include_directories(k1520disk PUBLIC ${CMAKE_SOURCE_DIR})
    target_compile_definitions(k1520disk PRIVATE K1520_BUILD_SHARED)
    set_target_properties(k1520disk PROPERTIES
        CXX_STANDARD 17
        POSITION_INDEPENDENT_CODE ON
    )

    # k1520disktool: Kommandozeilenseite desselben Unterbaus (§11.1)
    add_executable(k1520disktool tools/k1520disktool.cpp)
    target_link_libraries(k1520disktool PRIVATE k1520_fs)
    target_include_directories(k1520disktool PRIVATE ${CMAKE_SOURCE_DIR})

    # z8kasm: Assembler/Disassembler fuer U8001/U8002 (header-only tools/z8000/)
    add_executable(z8kasm tools/z8kasm.cpp)
    target_include_directories(z8kasm PRIVATE ${CMAKE_SOURCE_DIR})

    # boot_trace: standalone boot diagnostic tool (LOG_LEVEL=5 for full trace)
    add_executable(boot_trace tools/boot_trace.cpp tools/boot_trace_k8915.cpp tools/boot_trace_prg710.cpp tools/boot_trace_pc1715.cpp tools/boot_trace_p8000.cpp)
    target_link_libraries(boot_trace PRIVATE k1520_a5120 k1520_k8915 k1520_prg710 k1520_pc1715 k1520_p8000 k1520_floppy k1520_logger)
    target_include_directories(boot_trace PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(boot_trace PRIVATE LOG_LEVEL=5)

    # bench_run: fixed-workload Performance-Benchmark (Mcycles/s, x-Echtzeit)
    add_executable(bench_run tools/bench_run.cpp)
    target_link_libraries(bench_run PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(bench_run PRIVATE ${CMAKE_SOURCE_DIR})

    # kbd_test: standalone keyboard smoke test (boot, type a command, dump screen)
    add_executable(kbd_test tools/kbd_test.cpp)
    target_link_libraries(kbd_test PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(kbd_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(kbd_test PRIVATE LOG_LEVEL=3)

    # format_driver: scripted two-disk interactive driver (FORMAT.COM / FORMATB.COM)
    # Für DEBUG/TRACE aus build_trace/ laufen lassen (dort ist der ganze Build inkl.
    # Bibliotheken LOG_LEVEL=5); FD_GATE/FD_PCGATE + FD_LOGLEVEL öffnen zur Laufzeit
    # ein gezieltes Fenster.  In build/ (LOG_LEVEL=3) sind DEBUG/TRACE wegkompiliert.
    add_executable(format_driver tools/format_driver.cpp)
    target_link_libraries(format_driver PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(format_driver PRIVATE ${CMAKE_SOURCE_DIR})

    # mk_disk_template: erzeugt gültig vorformatierte EINSEITIGE Leerdisketten (HFE)
    # für die Boot-Pipeline der einseitigen Formate (MF3200 8″-FM, MF6400 8″-MFM,
    # K5600.10/.20 5″-SS) — FORMAT.COM kann eine gap-leere .hfe nicht direkt formatieren
    # (Gap-Blank-Hänger, doc/format.md §8.2/§8.6).
    add_executable(mk_disk_template tools/mk_disk_template.cpp)
    target_link_libraries(mk_disk_template PRIVATE k1520_floppy2 k1520_logger)
    target_include_directories(mk_disk_template PRIVATE ${CMAKE_SOURCE_DIR})

    # floppy_diag: running-OS floppy read diagnostic
    add_executable(floppy_diag tools/floppy_diag.cpp)
    target_link_libraries(floppy_diag PRIVATE k1520_a5120 k1520_floppy k1520_logger)
    target_include_directories(floppy_diag PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(floppy_diag PRIVATE LOG_LEVEL=3)

    # k1520dbg: interactive gdb-style debugger (breakpoints, marker-relative cycles,
    # memory dump/disasm, watchpoints, keystroke injection, screen view)
    add_executable(k1520dbg tools/k1520dbg.cpp)
    target_link_libraries(k1520dbg PRIVATE k1520_a5120 k1520_k8915 k1520_prg710 k1520_pc1715 k1520_p8000 k1520_floppy k1520_logger)
    target_include_directories(k1520dbg PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(k1520dbg PRIVATE LOG_LEVEL=5)
    # Zeilenbearbeitung der interaktiven Sitzung: Pfeiltasten, History,
    # Tab-Vervollständigung, UTF-8 — über die BEIGELEGTE Quelle isocline
    # (third_party/isocline, MIT, dort steht auch das Warum).
    #
    # Vorgänger war GNU readline. Zwei Gründe für den Wechsel (voll:
    # doc/design/13_distribution.md §10a.3):
    #   * LIZENZ — readline ist GPLv3+, dieses Projekt MIT. Ein ausgeliefertes
    #     Binärabbild mit readline wäre ein Gesamtwerk unter GPLv3. isocline ist
    #     ebenfalls MIT.
    #   * WINDOWS — readline gibt es dort nicht (es wurde gar nicht erst
    #     gesucht), die Sitzung hatte also KEINE Zeilenbearbeitung. isocline
    #     bedient Windows über die Console-API.
    # Nebenbei fällt die Laufzeitabhängigkeit von libreadline.so.8 + libtinfo.so.6
    # weg — ohne die startete das Programm bisher gar nicht.
    #
    # src/isocline.c ist eine Amalgamation (inkludiert die übrigen .c selbst):
    # genau EINE Datei übersetzen, sonst gibt es doppelte Symbole.
    add_library(isocline STATIC third_party/isocline/src/isocline.c)
    target_include_directories(isocline PUBLIC third_party/isocline/include)
    set_target_properties(isocline PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
    if (MSVC)
        # Die Fremdquelle wird nicht auf unsere Warnstufe gehoben; /utf-8 gilt
        # global (die Quellen hier sind voller Umlaute) und stört sie nicht.
        target_compile_options(isocline PRIVATE /w)
    else()
        target_compile_options(isocline PRIVATE -w)
    endif()

    target_compile_definitions(k1520dbg PRIVATE HAVE_ISOCLINE)
    target_link_libraries(k1520dbg PRIVATE isocline)


    # ─── Testsystem ───────────────────────────────────────────────────────────
    # Die gesamte Testregistrierung liegt in tests/ (eine CMakeLists.txt je
    # Testebene, Helfer tests/cmake/K1520AddTest.cmake).  MUSS nach den
    # Werkzeugzielen stehen: die CLI-/Systemtests verweisen per
    # $<TARGET_FILE:…> auf k1520dbg, boot_trace, format_driver, mk_disk_template.
    option(BUILD_K1520_TESTS "Testsystem bauen und in ctest registrieren" ON)
    if (BUILD_K1520_TESTS)
        # enable_testing() MUSS hier oben stehen — nur dann erzeugt CMake im
        # Build-Wurzelverzeichnis eine CTestTestfile.cmake, die in die
        # Unterverzeichnisse absteigt.  Steht es nur in tests/, meldet ctest
        # „No tests were found!!!".
        enable_testing()
        add_subdirectory(tests)
    endif()

endif() # BUILD_K1520
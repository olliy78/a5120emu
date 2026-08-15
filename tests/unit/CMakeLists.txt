# Unit-Tests: isolierte Klassen, keine Diskette, kein Boot.
# Struktur spiegelt core/ — primitives/ bus/ cards/ peripherals/ util/.

set(_lbl "unit;fast")

# ── core/primitives/ ─────────────────────────────────────────────────────────
k1520_add_test(z80   SRC primitives/test_z80.cpp   LIBS k1520_z80 LABELS "${_lbl}")
k1520_add_test(pio   SRC primitives/test_pio.cpp   LIBS k1520_pio LABELS "${_lbl}")
k1520_add_test(ctc   SRC primitives/test_ctc.cpp   LIBS k1520_ctc LABELS "${_lbl}")
k1520_add_test(sio   SRC primitives/test_sio.cpp   LIBS k1520_sio LABELS "${_lbl}")
# RETI/IUS-Verhalten über alle drei Bausteine hinweg (braucht daher den Bus).
k1520_add_test(reti  SRC primitives/test_reti.cpp
                     LIBS k1520_ctc k1520_pio k1520_sio k1520_bus LABELS "${_lbl}")

# ── core/bus/ ────────────────────────────────────────────────────────────────
k1520_add_test(bus   SRC bus/test_bus.cpp          LIBS k1520_bus LABELS "${_lbl}")

# ── core/cards/ ──────────────────────────────────────────────────────────────
k1520_add_test(k2526 SRC cards/test_k2526.cpp
                     LIBS k1520_k2526 k1520_k3526 k1520_k5122 k1520_floppy LABELS "${_lbl}")
k1520_add_test(k3526 SRC cards/test_k3526.cpp      LIBS k1520_k3526 k1520_bus LABELS "${_lbl}")
k1520_add_test(k5122 SRC cards/test_k5122.cpp      LIBS k1520_k5122 LABELS "${_lbl}")
k1520_add_test(k7024 SRC cards/test_k7024.cpp      LIBS k1520_k7024 k1520_bus LABELS "${_lbl}")
k1520_add_test(k8025 SRC cards/test_k8025.cpp      LIBS k1520_k8025 LABELS "${_lbl}")

# ── core/peripherals/ ────────────────────────────────────────────────────────
k1520_add_test(k7637          SRC peripherals/test_k7637.cpp
                              LIBS k1520_k7637 k1520_sio k1520_bus LABELS "${_lbl}")
k1520_add_test(track_codec    SRC peripherals/test_track_codec.cpp    LIBS k1520_floppy2 LABELS "${_lbl}")
k1520_add_test(bit_codec      SRC peripherals/test_bit_codec.cpp      LIBS k1520_floppy2 LABELS "${_lbl}")
# Zeichenbare Sicht auf eine Spur (Diskeditor, §19)
k1520_add_test(track_view     SRC peripherals/test_track_view.cpp     LIBS k1520_floppy2 LABELS "${_lbl}")
k1520_add_test(drive_profile  SRC peripherals/test_drive_profile.cpp  LIBS k1520_floppy2 LABELS "${_lbl}")
# Medium-Umbau (origin/main 7c7f312): DiskMedium + Container-Codecs .img/.hfe/.dmk
k1520_add_test(disk_medium    SRC peripherals/test_disk_medium.cpp    LIBS k1520_floppy2 LABELS "${_lbl}")
k1520_add_test(img_codec      SRC peripherals/test_img_codec.cpp      LIBS k1520_floppy2 LABELS "${_lbl}")
k1520_add_test(hfe_codec      SRC peripherals/test_hfe_codec.cpp      LIBS k1520_floppy2 LABELS "${_lbl}")
k1520_add_test(dmk_codec      SRC peripherals/test_dmk_codec.cpp      LIBS k1520_floppy2 LABELS "${_lbl}")
k1520_add_test(disk_image     SRC peripherals/test_disk_image.cpp     LIBS k1520_floppy2 LABELS "${_lbl}")
# Physische Diskette am Greaseweazle: Spurzustaende + Auftragswarteschlange (§14).
# Braucht KEINE Hardware — ein Ersatz-Arbeitsfaden bedient die Auftraege aus dem RAM.
k1520_add_test(track_sync     SRC peripherals/test_track_sync.cpp     LIBS k1520_floppy2 LABELS "${_lbl}")
k1520_add_test(floppy_drive2  SRC peripherals/test_floppy_drive2.cpp  LIBS k1520_floppy2 LABELS "${_lbl}")
# Prüft den AUSGELIEFERTEN Katalog data/formats.yaml — Pfad muss der Test kennen.
k1520_add_test(format_catalog SRC peripherals/test_format_catalog.cpp LIBS k1520_floppy2 LABELS "${_lbl}"
                              DEFS K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")

# ── core/util/ ───────────────────────────────────────────────────────────────
k1520_add_test(yaml_lite SRC util/test_yaml_lite.cpp LIBS k1520_util LABELS "${_lbl}")

# ── core/filesystem/ (k1520DiskTool) ─────────────────────────────────────────
# Prüft gegen die AUSGELIEFERTE data/formats.yaml und die committeten Disketten.
k1520_add_test(sector_space SRC filesystem/test_sector_space.cpp
                            LIBS k1520_fs LABELS "${_lbl}")
k1520_add_test(fs_catalog    SRC filesystem/test_fs_catalog.cpp
                             LIBS k1520_fs LABELS "${_lbl}"
                             DEFS K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")
k1520_add_test(geometry_probe SRC filesystem/test_geometry_probe.cpp
                              LIBS k1520_fs LABELS "${_lbl}"
                              DEFS K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")
k1520_add_test(cpm_fs        SRC filesystem/test_cpm_fs.cpp
                             LIBS k1520_fs LABELS "${_lbl}"
                             DEFS K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")
k1520_add_test(doppelschritt SRC filesystem/test_doppelschritt.cpp
                             LIBS k1520_fs LABELS "${_lbl}"
                             DEFS K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")
k1520_add_test(cpa_dpb       SRC filesystem/test_cpa_dpb.cpp
                             LIBS k1520_fs LABELS "${_lbl}"
                             DEFS K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")
k1520_add_test(udos_fs       SRC filesystem/test_udos_fs.cpp
                             LIBS k1520_fs LABELS "${_lbl}"
                             DEFS K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")
k1520_add_test(disk_volume   SRC filesystem/test_disk_volume.cpp
                             LIBS k1520_fs LABELS "${_lbl}"
                             DEFS K1520_FORMATS_DEFAULT="${K1520_FORMATS_DEFAULT}")

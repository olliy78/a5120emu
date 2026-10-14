# MC80 — EPROM-Abzüge und Speicherabzüge

Herkunft (abgerufen 2026-10-13, Planung `doc/design/31_mc80.md`):

- `*.bin` im Hauptordner: ycdt.net/mc80.3x (= ycdtotv.de, k1520.com); Namen wie dort
  (`mc80.30e-0000v2.bin` = ZVE 0000H, Fassung 2 usw.).
- `mc8030_mame/`: MAME-Satz `mc8030` (MESS 0.151, Internet Archive „MESS0.151RomCollectionByGhostware“),
  ZVE 0000H–1FFFH (`zve_1…4`, 4×2716) und SPE 2000H–27FFH (`spe_1/2`, 2×2708). **`zve_3/zve_4` = 1000H–1FFFH
  = Magnetbandsoftware (K5020-Treiber)** — auf ycdt.net nicht vorhanden.
- `mc8020/`: MAME-Satz `mc8020` (MC80.21/22): ZRE K2521 `s01–s03` (0000H–0BFFH, drei Fassungen),
  PFS K3820 `mo01, mo03–mo16` (2000H–5FFFH; `mo02` = 2400H fehlt auch am Gerät laut MAME).
- `segmente/`: Web-Archiv-Abzüge von k1520.com/ycdtotv.de (2022): MC80.31E-EPROM-Segmente 10H (a–d) und 20H
  (f–h, e fehlt) je 4 KB bei 4000H, `la80-print.bin` (MC80-LA), Testbild-ROM K7229.25.

Prüfsummen = MAME-Liste, wo dort geführt.

| Datei | Bytes | CRC32 |
|---|---|---|
| `mc80.3-ccd-3000-f10a.bin` | 1024 | 7d220128 |
| `mc80.3-rk-3000-5642.bin` | 1024 | 280b2211 |
| `mc80.3-rk-3400-c6a7.bin` | 1024 | fc5656f3 |
| `mc80.3-rk-3800-1678.bin` | 1024 | 34d7e1cf |
| `mc80.3-x-2c00-c63c.bin` | 1024 | 469be754 |
| `mc80.3-x-3800-7280.bin` | 1024 | 09976efb |
| `mc80.30e-0000.bin` | 2048 | ebdf766f |
| `mc80.30e-0000v2.bin` | 2048 | 259b55e9 |
| `mc80.30e-0800v2.bin` | 2048 | fe7a01a7 |
| `mc80.31e-2400-d0d7.bin` | 1024 | 43c22046 |
| `mc80.31e-3000-0d15.bin` | 1024 | cf6f090b |
| `mc80.31e-3400-9993.bin` | 1024 | 918d2b55 |
| `mc80.31e-3800-7a4c.bin` | 1024 | 62d9f989 |
| `mc80.33-1000.bin` | 2048 | c7e062b1 |
| `mc80.3s-2000.bin` | 1024 | 28f1df56 |
| `mc80.3s-2400.bin` | 1024 | 43c22046 |
| `mc80.3s-2800.bin` | 1024 | 4b52deb1 |
| `mc80.3s-2c00.bin` | 1024 | a13d8302 |
| `mc80.3s-3000.bin` | 1024 | cf6f090b |
| `mc80.3s-3400.bin` | 1024 | 918d2b55 |
| `mc80.3s-3800.bin` | 1024 | 6104646b |
| `mc80.3s-3c00.bin` | 1024 | 2f82d032 |
| `mc8020/mo01.rom` | 1024 | c65a470f |
| `mc8020/mo01_v2.rom` | 1024 | 7e47201c |
| `mc8020/mo03.rom` | 1024 | 29685056 |
| `mc8020/mo04.rom` | 1024 | fd315b73 |
| `mc8020/mo05.rom` | 1024 | 453d6370 |
| `mc8020/mo06.rom` | 1024 | 6357aba5 |
| `mc8020/mo07.rom` | 1024 | a1eb6021 |
| `mc8020/mo08.rom` | 1024 | 8221cc32 |
| `mc8020/mo09.rom` | 1024 | 7ad5944d |
| `mc8020/mo10.rom` | 1024 | 11de8c76 |
| `mc8020/mo11.rom` | 1024 | 370cc672 |
| `mc8020/mo12.rom` | 1024 | a3838f2b |
| `mc8020/mo13.rom` | 1024 | 88b61d12 |
| `mc8020/mo14.rom` | 1024 | 2168da19 |
| `mc8020/mo15.rom` | 1024 | e32f54c4 |
| `mc8020/mo16.rom` | 1024 | 403be935 |
| `mc8020/s01.rom` | 1024 | 0f1c1a62 |
| `mc8020/s02.rom` | 1024 | 93b5811c |
| `mc8020/s02_v2.rom` | 1024 | dd26c90a |
| `mc8020/s02_v3.rom` | 1024 | 40c7a694 |
| `mc8020/s03.rom` | 1024 | 3d32c334 |
| `mc8020/s03_v2.rom` | 1024 | 5b64ee7b |
| `mc8030_mame/spe_1.rom` | 1024 | 826f609c |
| `mc8030_mame/spe_2.rom` | 1024 | 98320040 |
| `mc8030_mame/zve_1.rom` | 2048 | 31ec0159 |
| `mc8030_mame/zve_2.rom` | 2048 | 5104983d |
| `mc8030_mame/zve_3.rom` | 2048 | 4bcfd727 |
| `mc8030_mame/zve_4.rom` | 2048 | f949ae43 |
| `segmente/la80-print.bin` | 2048 | 94a40448 |
| `segmente/seg_10_4000-a.bin` | 4096 | 0ba4f393 |
| `segmente/seg_10_4000-b.bin` | 4096 | 3e684eca |
| `segmente/seg_10_4000-c.bin` | 4096 | 739ca0d9 |
| `segmente/seg_10_4000-d.bin` | 4096 | 1d7f88fc |
| `segmente/seg_20_4000-f.bin` | 4096 | 6c1bb69c |
| `segmente/seg_20_4000-g.bin` | 4096 | 757e6188 |
| `segmente/seg_20_4000-h.bin` | 4096 | ecf38bb2 |
| `segmente/testbild-0000-k7229.25-2.bin` | 2048 | a976d803 |
| `segmente/testbild-0000-k7229.bin` | 2048 | a976d803 |

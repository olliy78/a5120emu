# RAM-Floppy RAF 512 (ZWG der AdW) — Beigaben

Herkunft: Diskette **`PRG710-1_RAF`** des Anwenders (cpa800, Datendiskette ohne
System), gelesen am Greaseweazle 2026-10-02 mit dem k1520DiskTool; Abbild
`PRG710-1_RAF.hfe`, sha256 `99931b5f68b1a749…` (liegt nicht im Baum, nur im
Diskettenarchiv des Anwenders).  Übernommen sind ausschließlich die RAF-Dateien
und das Werkzeug `PRL2COM`, mit dem sie gebaut werden; Fremdsoftware (TURBO,
M80, LINK, PASCAL, PLUS, KONVERT …) bleibt draußen.  Freigabe: Entwurf 22 §9, F5.
Planung und Umsetzung: `doc/design/22_raf512.md`.

| Datei | Inhalt |
|-------|--------|
| `RAFPRUF6.DOK` / **`RAFPRUF6.txt`** | Prüfanleitung RAF 512 des ZWG (WordStar; `.txt` = Bit 7 abgeschnitten, DDR-Umlaute `{|}[\]~` → äöüÄÖÜß, Punktbefehle stehen gelassen) |
| `RAFMAC1.MAC`, `RAFMAC2.MAC` | ZWG-Quellen „Teil von RAF 512 Dokumentation" (1986): Registermodell (19-Bit-Adresse aus B/r-Register) und Installations-Overlay für BIOS 57/59 |
| `RAFAD12`, `RAFASM7` | je 512 B: Abzüge der beiden PROMs der Karte (Adress-PROM, Ablauf-PROM ASM) |
| `RAFCPM.COM` | Original-Treiber „Nachladbare RAF-Installation, 11.10.87 ohne Parity", Laufwerk M:, bis 4 Karten ab 88H |
| `RAFCPMP.COM` + `.MAC`/`.PRN` | dasselbe mit Software-Parität (07.09.87) |
| `RAF2X24O.MAC`/`.PRN`, `RAF2X90O.MAC`/`.PRN` | Reassemblat V. Pohlers (2009/2012), parametrierbar (Kartenzahl, Laufwerk, Port) |
| `RAF512.COM`, `RAF2X88P.COM` | Fassung „(DKt) v.26.07.08": 2 Karten ab 88H, Laufwerk **P:** |
| `RAF2X88O`…`RAF2X98O`, `RAF2X20P`, `RAF2X24O` (`.COM`) | dieselbe für O: bzw. andere Portlagen |
| `RAFQUICK.COM/.PAS/.INC` | ZWG-Schnelltest (Turbo Pascal 3, ADM31) |
| `RAFTEST.COM/.MAC/.REL` | ZWG-Prüfprogramm (M80, 13.2.87) |
| `RAFSCAN.COM/.PAS` | weiterer Abtaster (Turbo Pascal) |
| `PRL2COM.COM/.MAC/.PRN`, `LIESMICH` | Bauwerkzeug und Hinweise von V. Pohlers |
| `diskette_inhalt.txt` | Inhaltsverzeichnis der ganzen Diskette (DiskTool) |

Die für Tests gebrauchten `.COM` liegen zusätzlich in `tests/fixtures/raf/`.

## Prüfsummen (gegen `diskarchive.yaml` der Diskette geprüft)

| Datei | Größe | sha256 |
|-------|------:|--------|
| `LIESMICH` | 896 | `3b2cf14d6d85ad1e…` |
| `PRL2COM.COM` | 640 | `fcb2560264412156…` |
| `PRL2COM.MAC` | 8576 | `d3e1c2fd65889af1…` |
| `PRL2COM.PRN` | 22016 | `9f57c802bdd2df7d…` |
| `RAF2X20P.COM` | 2432 | `699abac71c729eeb…` |
| `RAF2X24O.COM` | 2432 | `da4a865c8c718d5e…` |
| `RAF2X24O.MAC` | 24704 | `fd8c8900de63d6e0…` |
| `RAF2X24O.PRN` | 59136 | `2a909096aa0a1259…` |
| `RAF2X88O.COM` | 2432 | `7581fc8bbe4cb395…` |
| `RAF2X88P.COM` | 2432 | `88455611a44d7ee8…` |
| `RAF2X89O.COM` | 2432 | `6b62dd887ae92300…` |
| `RAF2X8AO.COM` | 2432 | `f98558eb2d4f6e1e…` |
| `RAF2X8BO.COM` | 2432 | `5a8862a9ac04a3d9…` |
| `RAF2X8CO.COM` | 2432 | `437dd129e075cc4c…` |
| `RAF2X8DO.COM` | 2432 | `d61d614319bfd499…` |
| `RAF2X8EO.COM` | 2432 | `355559f853e2ca41…` |
| `RAF2X8FO.COM` | 2432 | `2ba9e9abce052bf6…` |
| `RAF2X90O.COM` | 2432 | `1a8825db19a2fba6…` |
| `RAF2X90O.MAC` | 24704 | `9c1232420d3a4a79…` |
| `RAF2X90O.PRN` | 59136 | `d829c85a11bf1ca6…` |
| `RAF2X91O.COM` | 2432 | `5a56b898e97c6cae…` |
| `RAF2X92O.COM` | 2432 | `50c53e949d2088f5…` |
| `RAF2X93O.COM` | 2432 | `0dfbabf9951f85b4…` |
| `RAF2X94O.COM` | 2432 | `daf9317fe25bc7d1…` |
| `RAF2X95O.COM` | 2432 | `4a6e519226acf80a…` |
| `RAF2X96O.COM` | 2432 | `bf13f9da973771d9…` |
| `RAF2X97O.COM` | 2432 | `ac5a402a9fff2bc9…` |
| `RAF2X98O.COM` | 2432 | `f0800dcff2183869…` |
| `RAF512.COM` | 2432 | `88455611a44d7ee8…` |
| `RAFAD12` | 512 | `f276fb50d896b854…` |
| `RAFASM7` | 512 | `6e31aaa5e3de8cc7…` |
| `RAFCPM.COM` | 2432 | `597655e0bfac7e34…` |
| `RAFCPMP.COM` | 2432 | `a0b6c87f6a87b966…` |
| `RAFCPMP.MAC` | 25600 | `9ce2887ee6a3a475…` |
| `RAFCPMP.PRN` | 60928 | `b6666b9241e36a1a…` |
| `RAFMAC1.MAC` | 12160 | `6193822eebf54ba4…` |
| `RAFMAC2.MAC` | 34816 | `737a13808730ae1c…` |
| `RAFPRUF6.DOK` | 22656 | `3e4d3408d10b6094…` |
| `RAFQUICK.COM` | 13952 | `fef691f906f00709…` |
| `RAFQUICK.INC` | 8704 | `22aa9acc8606ce00…` |
| `RAFQUICK.PAS` | 11648 | `be63286e6279e11c…` |
| `RAFSCAN.COM` | 13568 | `08ebb338aed9565e…` |
| `RAFSCAN.PAS` | 11392 | `b7d787daa648559f…` |
| `RAFTEST.COM` | 7552 | `8046cee869aed7bf…` |
| `RAFTEST.MAC` | 49152 | `2f2bbfdf1fc2ad47…` |
| `RAFTEST.REL` | 9088 | `b5f034884a8a6873…` |

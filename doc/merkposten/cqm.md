<!-- Merkposten wie doc/merkposten/disktool.md: gilt WIE CLAUDE.md, sobald an diesem
     Teilsystem gearbeitet wird.  Entwurf: doc/design/33_cqm.md -->

# CopyQM (`.cqm`) — Merkposten

Vierter Container (`ContainerType::Cqm`), **nur lesbar**; `core/peripherals/floppy_drive/cqm_codec.*`.
Voller Befund an 142 Abbildern: `doc/design/33_cqm.md`.

1. **Der Pruefwert ist CRC-32 mit Tabellenfehler** (`& 0x3F` statt `& 0xFF`, Startwert 0,
   kein Endxor).  Wer "mal eben" auf ein normales CRC-32 umstellt, bekommt 142 abgelehnte
   Abbilder; wer `& 0x3F` "repariert", ebenso.  Folge: Bits 6/7 der Datenbytes sind nicht
   gedeckt.  Wächter: `CqmCodec.PruefwertHatDenTabellenfehler`,
   `KippenderDatenbyteFaelltAmPruefwertAuf`, `CqmEchtdaten.AlleAbbilderDekodierenMitPruefwert`.
2. **RLE-Bloecke laufen ueber Sektor- und Spurgrenzen** — nie je Spur neu beginnen.
   Wächter: `WiederholungLaeuftUeberDieSpurgrenze`, `RohblockUndNullblock`.
3. **Reihenfolge = `.img`-Reihenfolge** (Zylinder → Kopf → Sektor, IDs `secbase+1…`);
   Interleave/Skew des Kopfs werden gemeldet, nicht angewandt.  Wächter:
   `CqmEchtdaten.TarGegenstueckeStimmenByteweise` und `MediumGleichImgAusDerTar` (die
   `.img`-Gleichheit gilt bis auf die Marken).  Einzige bekannte Abweichung: `curspdoc.tar`
   hat einen Bitkipper (1 Byte, CQM-Pruefwert stimmt) — die `.tar` ist dort NICHT die
   Wahrheit.
4. **Nie zurueckschreiben.**  `DiskImage::open` setzt `binding_writable_ = false`,
   `save()` scheitert, `DiskVolume` oeffnet immer schreibgeschuetzt und lehnt
   `setReadOnly(false)` ab.  Wächter: `CqmDiskImage.SchreibtNieInDieCqmZurueck`.
   `saveAs`/`exportTo` in `.img`/`.hfe`/`.dmk` ist der Weg heraus.
5. **Kennung vor Endung** in `ImageCodec::detect` (Kennung `CQ` + Kopfpruefbyte).  Eine
   `.cqm` ohne gueltigen Kopf ist kein CQM.  Wächter: `ImageCodecErkenntUndBenennt`.
6. **`allowsContainer("cqm") == allow_img`** (`fs_profile.h`): UDOS-Gap-Verkettung geht
   nicht in ein CQM, UDOS1715/NDOS und CP/M schon.
7. **Etikettfeld (0x60) hat 11 Byte ohne Pflicht-NUL** ("** NONE ** " mit Leerzeichen
   kommt vor, `w30strt2`); der Kommentar steht erst hinter dem 133-Byte-Kopf
   (Laenge bei 0x6F), das Etikett ist NICHT der Kommentar.
8. **Echtdaten-Tests nur mit `K1520_CQM_DIR`** (Ordner mit `*.cqm` + `*.tar`, z. B.
   `~/projects/robotron/P8000/discs`), sonst uebersprungen (~18 s).  Kein Abbild einchecken.
9. **C-ABI-Zugriff**: `k1520d_container_info(h, key)` (type, description, label, comment,
   interleave, skew, cylinders, crc), Python `DiskTool.container_info()`.  Der Schreibschutz ist
   `readOnlyForced()` (Wächter `CqmDiskVolume.SchreibschutzIstErzwungen`).
10. **84 Abbilder sind rohe tar-Ströme** (kein Dateisystem), erkannt als `tar720`
   (`doc/merkposten/disktool.md`).  Offen: `.cqm` in den GUI-Dateidialogen, nicht
   gespeicherte Zylinder nur synthetisch geprueft.

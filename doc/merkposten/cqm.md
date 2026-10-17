<!-- Merkposten wie doc/merkposten/disktool.md: gilt WIE CLAUDE.md, sobald an diesem
     Teilsystem gearbeitet wird.  Entwurf: doc/design/33_cqm.md -->

# CopyQM (`.cqm`) — Merkposten

Vierter Container (`ContainerType::Cqm`), **lesen und schreiben** (seit 2026-10-17); `core/peripherals/floppy_drive/cqm_codec.*`.
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
4. **Schreiben (2026-10-17, Entwurf §7).**  Grenze = `DiskMedium::rawCompatible()` PLUS
   einheitliche MFM-Geometrie (je Spur gleich viele gleich grosse Sektoren, lueckenlose IDs,
   keine unformatierte Luecke, kein FM); sonst wird mit Grund abgelehnt, Datei unveraendert.
   Geschrieben wird in `<datei>.k1520tmp` und umbenannt, nachdem der Leser das Ergebnis
   gegengeprueft hat.  `SourceInfo::raw_header`/`comment_raw` sind die Vorlage: Kommentar,
   Bezeichnung, Etikett, Interleave/Skew, Zeit/Datum und Unbekanntes ueberleben bei gleicher
   Geometrie.  Nicht darstellbar zur Laufzeit = `.img`-Politik (`saveFailed()`, Hinweis
   `Abbild nicht speicherbar: …` in `FloppyDriveV2::notices()`, Datei bleibt).  **Kein
   erzwungener Schreibschutz mehr** — nur tar-Disketten bleiben unaufhebbar.  Datei1 -> Medium ->
   Datei2 ist NICHT byteidentisch (anderer RLE-Schnitt).  Pruefwert 0 (nur Nullbytes) liest
   sich als "nicht gesetzt".  Aus FM/gemischten Medien (CP/A-Systemspuren) wird kein `.cqm`.
   Waechter: `CqmWrite.*`, `CqmDiskImage.*`, `CqmEchtdaten.SpeichernUndWiederLadenIstVerlustfrei`,
   `DiskVolume.CopyQmAnlegenSchreibenUndNeuLaden`.
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
   interleave, skew, cylinders, crc), Python `DiskTool.container_info()`.  `readOnlyForced()`
   gilt fuer `.cqm` NICHT mehr (Wächter `CqmDiskVolume.SchreibschutzKommtVomTarProfilNichtVomContainer`).
10. **84 Abbilder sind rohe tar-Ströme** (kein Dateisystem), erkannt als `tar720`
   (`doc/merkposten/disktool.md`).  Offen: `.cqm` in den GUI-Dateidialogen, nicht
   gespeicherte Zylinder nur synthetisch geprueft.
11. **Oberflaechen (2026-10-17; Stand VOR dem Schreiben — die Oberflaeche sperrt `.cqm` noch
   selbst, Umbau ist der naechste Auftrag; `py_cqm_*` haben dafuer zwei uebersprungene Faelle).**
   Emulator: der Kern erzwingt keinen Schutz mehr, `k1520_disk_container_info`
   (Metadaten wie `k1520d_container_info`); Laufwerkskasten: Haken fest, Kommentar als Tooltip,
   `.cqm` nie Ziel von Neu/Speichern unter.  DiskTool: `k1520d_read_only_forced`,
   `k1520d_open_tar_satz`, `app/disktool/ui/schreibsperre.py` (Grund an EINER Stelle),
   Aktion „tar-Satz öffnen…" (kein Kuerzel).  Archiv: Abbild bleibt `.hfe`; `.cqm`-Quelle als
   `source_container` (Metadaten + sha256 der Quelldatei).  Testhilfe `tests/python/cqm_bau.py`,
   Waechter `py_cqm_emulator_gui`, `py_cqm_disktool_gui`.  Kein Drag&Drop zum Oeffnen vorhanden.
12. **Sammlung archivieren (2026-10-17).**  `tools/p8000/pofo_archive.py --quelle … --meta … --ziel …`
   macht aus den pofo.de-Abbildern je ein DiskTool-Archiv (+ Satzarchive der tar-Mehrdisketten-
   Saetze, Webseiten-Herkunft als `provenance:`; siehe `disktool.md`, „Archiv mit Herkunft").
   Die `*.crdownload` der Sammlung sind **vollstaendige** CQM-Abbilder (Strom komplett, Pruefwert ok)
   — nur umbenennen; das Skript berichtet es, archiviert sie nicht.

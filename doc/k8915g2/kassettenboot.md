# K8915: gibt es einen Kassettenboot-Weg im ROM? (Befund 2026-10-15)

Reine Analyse (Lesen). Legende: [ROM] aus Listing/Abzug, [Doku] aus Repo-Dokument, [?] Vermutung.

## 1. Befund je ROM

**V3-Boot-ROM** `doc/EPROMS/K8915/k8915_boot_2732.bin` / `k8915_zre.prn` [ROM]
- Alle `IN`/`OUT`-Ports (Direktadressen) des Listings: 10H–18H (K5122), 44H–4EH, 52H–54H, 58H, 5BH/5CH, 61H, 80H/83H, A8H.
  **Kein einziger Zugriff auf D0H–D7H.** `OUT (C)`/`IN H,(C)` (0558–06FC) laufen nur mit C = 10H bzw. 16H
  (`LD C,10H` 0552, `LD C,16H` 05BB/0629/0690/0715); `LD C,A` (0763–077F) ist Tabellenindex, kein Port.
- Bytesuche im 4-KB-Abzug: kein `D3/DB D0..D7`, kein `LD C,D0..D7`.
- Texte (alle Strings des Abzugs): "Coldstart * Disk on A: ready", "Disk-error, change disk", "Drive A: not ready, check",
  "No system disk, change disk", "Loading complete, replace (system|previous) disk", "MROM RAM SIO KEY CTC DIAGNOSTIC ENTER: LADER / #: ZYKL.".
  Kein KASSETTE/TAPE/MK/KMBG (MK in den Quellen = K5122-Markensteuerbit, nicht Magnetkassette).
- Bootauswahl: nur Laufwerk 0 (18H = EEH); Fehlerpfade = "Drive A: not ready" / "No system disk" / "Disk-error" -> Neuversuch. Keine Alternative Band.

**Gen 2, ZRE 175/176/177** `doc/EPROMS/K8915G2/k8915g2_zre.prn` [ROM]
- Gleiche Port-Menge (10H–18H, 44H–4EH, 52H–54H, 58H, 5BH/5CH, 61H, 80H/83H, A8H); **kein D0H–D7H**; gleiche Strings.
  176/177 sind bis auf 2 bzw. 0 Byte identisch mit V3 (`doc/k8915g2/zre_rom.md` §0).
- Kartenchips "3C00"/"3000" (`k8915g2_pfs3820_0000-3FFF.bin`, Gen-1-Urlader): Bytesuche ohne Treffer auf D0H–D7H
  (die Fundstellen 316C/3C04/3E11/3E17 sind `LD A,0EH; OUT (A8H)`-Fehltreffer). Dort: A8H, E0H–E2H (K7634), 61H nicht.

**Repo-Doku:** `doc/design/16_k8915.md` Z.55 und `24_k8915_varianten.md` Z.30/40: Magnetkassette (KOKOS/KOBRA) "nicht Ziel", nur [Web]-Hinweis.
`doc/kassette/k5020.md` §2: D0H ist nur aus dem **Anwenderprogramm FEX** (`padr = D0H`) belegt, nicht aus einem K8915-ROM;
Stiftbelegung der Wickelbrücke für D0H [?]. `doc/kassette/treiber.md` §6.1 (Bootband) beschreibt ausschliesslich das **A5120-Lade-ROM** (zre.prn 027E–03AF).

**Fazit: Kein Kassettenweg in V3-ROM, Gen-2-ROM 175–177 und den Kartenchips.**

## 2. Welche Fassung hätte ihn? [?]
- Nur Hypothese: die Kassetten-Auslieferung der frühen K8915 (V1-Zeit, K7634/K7028.10; `24_k8915_varianten.md` §2) hatte ein
  **eigenes Urlader-ROM**; belegt ist für Gen 1 nur der Satz `anflad.rom` (3 KB, Forum 5713 Beitr. 007/028; `doc/merkposten/k8915_varianten.md` F11),
  nicht öffentlich. Ob dort ein Bandweg steckt, ist unbekannt [?].
- Fehlt: (a) der Abzug (`anflad.rom` bzw. ein Kassetten-ZRE-Satz), (b) Einsprung/Portbild (D0H–D7H laut FEX; Brücken [?]),
  (c) Bootband-Format: Hypothese, dass es dem A5120-Lade-ROM-Band entspricht (4 x 128 B ab 0400H, 'SYL', `treiber.md` §6.1) — **nicht belegt** für K8915.
- Wichtig: Kassettenbetrieb am K8915 ist für **SCPX unter FEX/KMBG** nachgewiesen (`doc/kassette/README.md`: "getestet am realen A5120/K8915 mit K5221"),
  d. h. das Gerät kann die K5020 ansprechen, gebootet wird dabei aber von Diskette.

## 3. Empfehlung
- Kein Kassettenboot für V3/Gen 2 modellieren (ROM hat keinen; Kasettenkarte dort nur als Option für SCPX-Programme: D0H, ohne ROM-Weg).
- Kassetten-K8915 erst nach Beschaffung von `anflad.rom` o. ä. (Anfrage Forum robotrontechnik.de/Rechenwerk Halle, wie F11); bis dahin [?].
- Optional prüfen: FEX/KMBG-Quellen in `doc/kassette/` auf Bootband-/Bandstart-Routine für K8915.

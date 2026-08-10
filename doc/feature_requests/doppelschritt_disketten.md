# Feature-Request: Doppelschritt-Disketten lesen und schreiben

**Anlass:** Beim systematischen Erzeugen aller Diskettenformate der drei Systeme
(2026-08-10, `tests/system/drivers/make_all_formats.py`) blieben **13 Abbilder**
liegen, die der Formatkatalog nicht beschreiben kann — die CP/A-Geometrien `T` und
`U`. Sie sind kein Kuriosum, sondern die **Austauschformate** zwischen Rechnern mit
unterschiedlicher Spurdichte: genau das, was man braucht, wenn man an einem K5601
sitzt und Disketten mit einem K5600.10 oder K5600.20 tauschen will.

**Priorität: mittel.** Nichts ist kaputt — die Erkennung lehnt solche Abbilder
ausdrücklich und mit Begründung ab, statt Datenmüll zu liefern. Aber der praktische
Nutzen ist hoch: wer eine fremde Diskette bekommt, hat gute Chancen, dass sie in
einem dieser Formate vorliegt.

Hintergrund und die drei Austauschfälle: `doc/format.md`, Abschnitt „Wozu einseitig
und Doppelschritt gut sind — der Datenaustausch".

---

## §1 Worum es geht

Das K5600.10 arbeitet mit **48 tpi** (40 Spuren), K5601 und K5600.20 mit **96 tpi**
(80 Spuren). Ein 96-tpi-Laufwerk schreibt eine für 48 tpi lesbare Diskette, indem es
**zwei Schritte je Spur** macht — seine Spuren liegen dann radial genau dort, wo das
kleine Laufwerk sie erwartet.

Im Abbild sieht das so aus:

```
  c0h0  : 16 Sektoren à 256 B, mfm      ← logische Spur 0
  c1h0  : unformatiert                  ← übersprungen
  c2h0  : 16 Sektoren à 256 B, mfm      ← logische Spur 1
  c3h0  : unformatiert
  …
  c78h0 : 16 Sektoren à 256 B, mfm      ← logische Spur 39
```

Physisch ist das **dieselbe Diskette**, die ein K5600.10 nativ erzeugt hätte; nur der
Schreiber war ein anderer. Ein `.hfe` von einem 80-Spur-Laufwerk hat trotzdem 80
Spurplätze, von denen jeder zweite leer ist.

## §2 Was heute passiert

`data/formats.yaml` beschreibt in `tracks:` **zusammenhängende** Zylinderbereiche.
„Jeder zweite Zylinder" lässt sich damit nicht ausdrücken. Der `GeometryProbe` lehnt
solche Abbilder deshalb bewusst ab (Kriterium `gap_tracks`, eingeführt am
2026-08-10):

```
$ k1520disktool measure fmt_clock_B_U_4.hfe
Gemessen:
  Zylinder 0-79, Koepfe 0-0
  c0h0 : 16 Sektoren à 256 B, IDs 1-16, mfm
  c2h0 : 16 Sektoren à 256 B, IDs 1-16, mfm
  …
Kein Format in data/formats.yaml passt.
```

Das ist die richtige Antwort auf die falsche Frage. **Ohne** dieses Kriterium passte
jedes 80-Spur-Format auf eine solche Diskette (die 40 Lücken gingen als „leere Spuren"
durch) und das Dateisystem läse anschließend Datenmüll — der Grund, warum das
Kriterium überhaupt eingeführt wurde.

## §3 Vorschlag: ein Attribut `step:` am Format

```yaml
  - name:        k5601_ss40_16x256_dstep
    description: "40 Spuren einseitig 16×256 im Doppelschritt — Austausch mit K5600.10"
    drives:      [K5601, K5600.20]
    encoding:    mfm
    step:        2          # logische Spur n liegt auf physischem Zylinder 2·n
    tracks:
      - { cyls: 0-39, heads: 0, sectors: 16, size: 256 }
```

`tracks:` bleibt **logisch** (0-39) — so wie das Gastsystem die Diskette sieht. `step`
sagt nur, wie logische auf physische Zylinder abgebildet werden. Vorgabe `1`; jeder
heutige Eintrag verhält sich damit unverändert.

> **Entwurfsfrage, bitte vorab entscheiden:** Gehört `step` an das **Format** oder an
> das **Laufwerksprofil**? Physisch ist der Doppelschritt eine Eigenschaft der
> Ansteuerung. Für den Katalog — der beschreibt, *was auf dem Medium steht* — muss es
> aber am Format hängen: dieselbe Hardware schreibt mit und ohne Doppelschritt.
> Der Vorschlag hier setzt es deshalb ans Format.

## §4 Was zu ändern wäre

Nach Aufwand geordnet; die ersten drei Punkte sind das Kernstück.

1. **`TrackFormat`/`DiskFormat`** (`core/peripherals/floppy_drive/disk_format.h`) —
   Feld `uint8_t step = 1`, dazu `physicalCylinder(logical)` und ein
   `physicalCylinders()` für die Laufwerksprüfung. `FormatCatalog` liest und validiert
   es (`step ≥ 1`, sinnvoll nur 1 oder 2).
2. **`formatFitsDrive`** — muss die **physische** Ausdehnung prüfen: ein
   40-Spur-Format mit `step: 2` braucht ein Laufwerk mit 80 Zylindern. Heute wird
   `fmt.numCylinders()` gegen `prof.num_cyls` gehalten; das wäre mit `step` zu klein.
3. **`SectorSpace`** (`core/filesystem/sector_space.cpp`) — beim Aufbau der
   Slot-Tabelle den physischen Zylinder aus dem logischen ableiten. Das ist der
   einzige Ort, an dem die Dateisysteme Spuren adressieren; CP/M und UDOS bleiben
   dadurch unverändert.
4. **`GeometryProbe`** — das Lückenmuster von „Ausschluss" zu „Treffer" machen:
   bei `step: 2` **müssen** die ungeraden Zylinder leer sein, und die geraden müssen
   passen. Damit wird aus dem heutigen Ablehnungsgrund ein Erkennungsmerkmal, das
   Doppelschritt- von Einzelschritt-Formaten sauber **trennt** (heute liefe eine
   40-Spur-Einzelschritt-Diskette Gefahr, mit dem Doppelschritt-Format verwechselt zu
   werden — sie hat aber keine Lücken).
5. **Schreiben** (`DiskImage::create`, `DiskVolume::create`) — eine neue Leerdiskette
   in einem `step: 2`-Format muss die ungeraden Zylinder **unformatiert lassen**.
   Heute formatiert `create` jeden Zylinder des Bereichs.
6. **`.img`** (`core/peripherals/floppy_drive/img_codec.cpp`) — ein rohes Sektorabbild
   ist logisch: 40 Spuren. Der Codec muss beim Laden/Speichern dieselbe Abbildung
   benutzen wie der `SectorSpace`, sonst liegen die Sektoren um Faktor 2 daneben.
7. **Katalogeinträge** — je Doppelschritt-Format ein eigener Eintrag. Aus den
   vorhandenen Messungen lassen sich sie direkt ableiten (§5).

**Nicht betroffen:** der Emulator. Er folgt der Kopfposition, die das Gastsystem
ansteuert, und schreibt dorthin — Doppelschritt funktioniert dort längst (die 13
Abbilder sind ja von CP/A im Emulator erzeugt worden). Die Lücke ist ausschließlich
auf der Katalog-/DiskTool-Seite.

## §5 Prüfmaterial ist schon da

Die 13 Abbilder erzeugt `tests/system/drivers/make_all_formats.py --system cpa`
(sie liegen unter `out/formats/cpa/`, das ist `.gitignore`d):

| Geometrie | Abbilder | Bedeutung |
|---|---|---|
| `T` — 40 Spuren, doppelseitig, Doppelschritt | `fmt_clock_B_T_{0,3,4,5,6,7}` | 6 Formate |
| `U` — 40 Spuren, einseitig, Doppelschritt | `fmt_clock_B_U_{0,2,3,4,5,6,7}` | 7 Formate |

Gegenprobe gegen Verwechslung liefern die **Einzelschritt**-Pendants derselben
Formatliste: `V` (40 Spuren doppelseitig) und `W` (40 Spuren einseitig) — sie haben
dieselben Sektorlayouts, aber keine Lücken. Ein `step`-fähiger `GeometryProbe` muss
`U` und `W` sicher auseinanderhalten.

## §6 Wann es fertig ist

1. `k1520disktool measure` erkennt die 13 Abbilder, und zwar **nur** mit dem
   Doppelschritt-Format — `W`/`V` werden weiterhin den Einzelschritt-Formaten
   zugeordnet und nicht verwechselt.
2. `k1520disktool ls` mountet ein `U`-Abbild und listet dieselben Dateien, die das
   Gastsystem darauf sieht.
3. **Rundlauf**: das DiskTool schreibt eine Datei auf ein `U`-Abbild, CP/A liest sie
   mit `TYPE` — das ist der einzige Nachweis, dass die Spurabbildung stimmt und nicht
   nur in sich schlüssig ist. Vorbild: `tests/integration/test_disktool_cpm_roundtrip.cpp`.
4. `k1520disktool create --fs <doppelschritt-profil>` erzeugt eine Diskette, deren
   ungerade Zylinder unformatiert sind, und CP/A kann sie ohne Neuformatierung
   beschreiben.
5. Regression grün; `FormatCatalog.Formatnamen_SindEinStabilerVertrag` und
   `FsCatalog.ProfilnamenSindEinStabilerVertrag` um die neuen Namen ergänzt.

## §7 Risiken und Nebenwirkungen

* **Verwechslungsgefahr in beide Richtungen.** Eine 40-Spur-Einzelschritt-Diskette und
  eine 80-Spur-Diskette mit halb leerem Medium sehen einer Doppelschritt-Diskette
  ähnlich. Das Erkennungskriterium muss die Lücken **positiv** verlangen, nicht bloß
  tolerieren.
* **Halbe Wahrheit bei beschädigten Disketten.** Fehlt auf einer normalen Diskette
  jede zweite Spur, weil sie kaputt ist, sähe sie wie Doppelschritt aus. Ein Hinweis
  in der Anzeige („als Doppelschritt gelesen") schadet nicht.
* **`step: 2` mit ungerader Zylinderzahl** am Medium (79 physische Zylinder) — die
  Validierung sollte das abfangen statt still die letzte Spur zu verlieren.

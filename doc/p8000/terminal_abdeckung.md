# Abdeckung Originalterminal Typ 2 (AP P20a/P20b) — Funktion ↔ Test ↔ Annahme

Stand 2026-10-08.  Grundsatz §10.11a (Entwurf 25): jede Funktion des 8275 und der Terminalhardware,
die die Firmware **P8T 5.0** benutzt, hat einen Test.  Entwurf: `doc/design/28_p8000_originalterminal.md`.
Testdateien: `HW` = `tests/unit/peripherals/test_p8000_terminal_hw.cpp`, `DIFF` = `…/test_p8000_terminal_diff.cpp`,
`KB` = `…/test_tastatur_k7673.cpp`, `I8275` = `tests/unit/primitives/test_i8275.cpp`,
`EIN` = `tests/integration/test_p8000_terminal_einheit.cpp`.

## 1. 8275 — von der Firmware benutzt

| Funktion | Firmware | Test | Annahme |
|---|---|---|---|
| Reset-Befehl 00H + 4 Parameter 4F 97 CC 5A (80 Zeichen, 24 Zeilen, 3 VRTC-Zeilen, 13 Linien, Unterstrich Linie 12, nicht transparent, Cursor blinkender Unterstrich, HRTC 22) | RESET_DISPLAY | HW `Programmierung8275WieFirmware`; I8275 `ResetParameterDisplay2_80x24` | — |
| Preset Counters E0H (zweimal) | RESET_DISPLAY | I8275 `PresetCountersSetztDieBlinkzaehlerZurueck`; HW `Programmierung8275WieFirmware` | Wirkung = Blinkzähler zurück (Bildzeitlage nicht) |
| Start Display 20H (Burstabstand 0, 1 Zyklus je Burst) | START_DISPLAY | HW `Programmierung8275WieFirmware` (Burstfelder) | Burstzeit nicht nachgebildet (Zeile als Ganzes) |
| Load Cursor 80H + Spalte + Zeile | LOAD_CURSOR (jede Bewegung) | HW `EinschaltmeldungImBildspeicher`, `ZellenDes8275UndCursor`; DIFF alle `CURSOR(…)` | — |
| DRQ je Zeichenzeile → P31 (IRQ2), Zeilen-DMA mit Zählerladen per LDE und P35 = 0 | IRP31/IRT1 | HW `ZeilenDmaBedientJedeAnforderungUndBildHat62Komma8Hz`, `ZellenDes8275UndCursor` | DRQ Zeile 0 in der letzten VRTC-Zeile, Zeile r in Zeile r−1; erster DMA erst ab dem Bild nach START [A] |
| VRTC → P32 (IRQ0), VSYN-Erzeugung über T1 | IRP32/IRT1 | HW `WatchdogSetztEineHaengendeFirmwareZurueck` (Dauerlauf ohne Reset), `…62Komma8Hz` | P32 = VRTC invertiert, nicht IRQ [U, Befund §2.1] |
| Feldattribut 80H (Spalte 80 jeder Zeile, „Reset to Standard Video") | CLEAR_LINE | HW `ZellenDes8275UndCursor`; DIFF `FeldregelAttributWirktNurInnerhalbDerZeile` | — |
| Feldattribut H (81H) | VT100 SGR 1 | HW `AttributeUndCursorImPixelbild`; DIFF `Vt100_SGR_Tab4310` | Highlight = Punktstufe 2 [U: VIDEO2] |
| Feldattribut B (82H), Zeichenblinken 32 Bilder | ADM31 G2, VT100 SGR 5 | HW `AttributeUndCursorImPixelbild`; DIFF `Adm31_SGR_AlleParameter_Tab439`; I8275 `ZeichenBlinkphase32Bilder` | Blinken = VSP in der Dunkelphase |
| Feldattribut R (90H) | ADM31 G4/G5/G7, SGR 7 | HW `AttributeUndCursorImPixelbild` (alle 13 Linien invertiert); DIFF `Adm31_SGR…`, `Feldregel…` | XOR RVV nach dem Austasten [A] |
| Feldattribut U (A0H) | SGR 4 | HW `AttributeUndCursorImPixelbild` (Linie 12) | — |
| Kombination 92H, 93H | ADM31 G6, SGR 1;5;7 | DIFF `Adm31_SGR…`, `Vt100_SGR_Tab4310` | — |
| Nicht transparent: Attribut belegt eine Zelle (leer) | P4 = 5AH | DIFF `Adm31_SGR…` (`bildZelle(0,0).empty`); I8275 `FeldattributNichtTransparentBelegtEineZelle` | — |
| Cursor blinkender Unterstrich (16 Bilder) | P4 = 5AH | HW `ZellenDes8275UndCursor`, `AttributeUndCursorImPixelbild`; I8275 `CursorBlinktAlle16Bilder` | — |
| 13 Linien je Zeichenzeile, Glyphe aus dem 2716 (Byte n = Linie n, Bit 7 links) | P3 = CCH | HW `PixelbildAusDemZeichengenerator` | — |
| Zeichen 00H–1FH (Programm-Mode) über den ZG | ESC U | DIFF `SteuerzeichenSindUnsichtbarProgrammModeZeigtAlle`, `Adm31_PMN_PMF` | — |

**Nicht benutzt** (Baustein-Tests in I8275, hier ohne Wirkung): Stop Display, Read Light Pen, Enable/Disable
Interrupt (Status-IR), Statuslesen, transparente Attribute, Zeichenattribute 11CCCCBH, Sondercodes F0–F3
(die Firmware maskiert Zeichen auf 7 Bit), versetzter Linienzähler, Zeilenabstand (Bit S), Block-Cursor.

## 2. Terminalhardware

| Funktion | Test | Annahme |
|---|---|---|
| Speicherkarte: EPROM < 1000H, BWS 1000–17FF, Zeilentabelle 1780H, 8275 1C00/1C01 (/DM) | HW `EinschaltmeldungImBildspeicher`, `…62Komma8Hz` (Tabelle), DIFF `RollenNachDer24Zeile` (Tabelle gedreht) | Spiegel in 1000–1FFF nicht dekodiert [U] |
| Strobe 4000H ZG1 / 2000H ZG2 (RS-Flipflop, ganzes Bild) | HW `ZeichensatzWechselGiltFuerDasGanzeBild`; DIFF `ZeichensatzUmschaltungSiSo` | Z61 = P8TEZS, Z62 = P8TDZS [U, Befund §4.3] |
| Strobe 8000H Klingel | HW `HostzeichenKlingelUndSchieberegister`; DIFF `Bel` | Ton nicht nachgebildet (Zähler) |
| Strobe C000H TRES, Schieberegister 2 × 74LS299, P2 = Byte ROL 1, P33 | HW `HostzeichenKlingelUndSchieberegister`, `TastaturLeitungBitweise` | weitere Takte bei vollem Register verworfen [A] |
| Watchdog 74123 an P36 | HW `WatchdogSetztEineHaengendeFirmwareZurueck` | 100 ms [U] |
| UART 9600 Bd 8N2 (Senden), Empfang 8 Bit, Bit 7 gelöscht, 00H/7FH verworfen | DIFF `EmpfangBit7GeloeschtNulUndDelVerworfen`, `Adm31_HVP_ESC_Gleich` (DEL) | — |
| XON/XOFF bei 36 Zeichen, Überlauf bei 45 | DIFF `XoffBei36ZeichenImPufferXonWennLeer`, `OhneXoffBeachtungGehenZeichenVerloren` | — |
| BREAK = 00H mit verdoppeltem T0 | DIFF `BreakTasteSendetNullMitHalberBaudrate`; EIN `BreakTasteMeldetBreakAmKanal` | als Break eine Rahmenzeit gemeldet |
| DSR/CPR `ESC [ 6 n` | DIFF `Vt100_DSR_CursorPositionsReport` | — |
| Einschaltmeldung, „Error Tastatur" bei FC, Warten auf das erste Tastaturbyte | HW `ErrorTastaturBeiFC`, `OhneTastaturBleibtDieFirmwareInDerTastaturwarte` | Tastatur läuft 300 ms nach dem Terminal an [U, B9] |
| Bildfrequenz 62,8 Hz (Zeichentakt 17,998 MHz / 8) | HW `…62Komma8Hz` | N = 8 [U, B4] |
| Save-State | HW `SaveStateSetztGenauFort`; KB `SaveStateSetztGenauFort`; EIN `SaveStateDerKopplungSetztFort` | — |

## 3. Tastatur K7673.09 (Verhaltensmodell gegen die Firmware auf dem Z8)

| Funktion | Test |
|---|---|
| AA beim Einschalten (Zeitpunkt gleich) | KB `EinschaltenSendetAAWieDieFirmware` |
| alle 128 Matrixpositionen: Make/Break, E0, Folgen (gleiche Bytes, Zeit ± 1,5 ms) | KB `JedeMatrixpositionGleicheCodefolge` |
| Wiederholung 50/10 Ticks, nur die zuletzt gedrückte Taste | KB `WiederholungDerZuletztGedrueckt…`, `ZweiTastenWiederholtWirdNurDieLetzte`; DIFF `TastenwiederholungKommtVonDerTastatur` |
| > 3 Tasten verworfen | KB `MehrAlsDreiTastenWerdenVerworfen` |
| PAUSE allein / mit 1DH-Taste, „00"-Folge (Break sendet die Folge mit Bit 7) | KB `PauseMitUndOhne1DTaste` |
| LED-Tasten (einmal je Druck) | KB `LedTastenSchaltenEinmalJeDruck` |
| Leitungsprotokoll, FF-Überlaufmarke | KB `LeitungsprotokollEinesBytes`, `UeberlaufMarkeFF` |
| Zeichen → Taste (NORMAL_Tab/SHIFT_Tab der Firmware), CTRL, CAPS LOCK | DIFF `TastaturCtrlTabelle45`, `TastaturCapsLockNurBuchstaben`; EIN `TasteAWirdAlsAGesendet` |

Teilabdeckung: P3.0 (Sendefreigabe) und P3.1 (Neustart) der Tastaturfirmware werden im Prüfstand fest auf 0
gehalten (Kabel hat nur Takt/Daten, Herkunft [U]); der Neustartpfad ist daher nicht verglichen.
Quarz 8 MHz [U] — alle Zeiten skalieren mit `TastaturK7673Config::quarzHz`.

## 4. Entschiedene Annahmen des Kern-Terminals (Original gewinnt)

| Annahme Kern (P6) | Original P8T 5.0 | Fall |
|---|---|---|
| W2 FF-Taste 0CH | **0CH** (bestätigt) | DIFF `TastaturSteuerzeichenTasten_Tab436` |
| W3 LINE DELETE = ESC R | **ESC R** (bestätigt); eine LINE-ERASE-Taste gibt es nicht | `…Funktionstasten_Tab437` |
| W6 IL vor der Cursorzeile | **bestätigt** | `Vt100_IL_VorDerCursorzeile_W6` |
| NEL rollt nicht am Schirmende | bestätigt | `Vt100_NEL_ESC_E_ohneRollen` |
| CTRL-@ sendet 00H | **nichts** (SIO_OUT unterdrückt NUL) | `TastaturCtrlTabelle45` |
| TAB-Taste VT100 = ESC [ I | **09H** (eine TAB/HT-Taste) | `…Tab437` |
| Zeichensatz je Zeichen, MODE wählt ZG1 | **ganzes Bild**, MODE lässt ihn stehen | `ZeichensatzUmschaltungSiSo` |
| ESC u im Programm-Mode unsichtbar | ESC und u werden **angezeigt** und wirken | `SteuerzeichenSind…`, `Adm31_PMN_PMF` |
| ESC i endet am Bildschirmende | bricht um und **rollt** | `Adm31_CHT_ESC_i` |
| VT100 CBT → voriger Tabstopp | **(Spalte − 8) & F8H** | `Vt100_CBT` |
| ADM31 G1/G3 „leer" | **Leerzeichen**, G5/G7 nur invers | `Adm31_SGR_AlleParameter_Tab439` |
| VT100 SGR kumulativ | **setzt neu** | `Vt100_SGR_Tab4310` |
| CR in einer Folge läuft mit | CR wird ausgeführt, **Rest der Folge verschluckt** | `Vt100_SteuerzeichenInnerhalbEinerFolge` |
| ESC = 7FH 7FH = letzte Position | DEL wird beim Empfang **verworfen** | `Adm31_HVP_ESC_Gleich` |
| kein XOFF (Anschluss liefert nur bei freiem Empfänger) | **XOFF bei 36**, Verlust ohne Beachtung | `XoffBei36…`, `OhneXoffBeachtung…` |

# P8000 Mehrplatz-Abnahme (AP P22)

Stand 2026-10-08.  WEGA 3.0 von der Winchester, Administrator-Konsole am **Originalterminal** (Variante
„P8000 + P8000 Terminal", tty1), dazu drei reine Arbeitsplätze „P8000 Terminal" über Telnet am `SerialHub`
des Rechners.  Test: `tests/system/test_p8000_wega_mehrplatz.cpp`
(`P8000WegaMehrplatz.KonsoleAmOriginalterminalUndArbeitsplaetzeUeberTelnet`, Label `wega_install`,
`tools/dev.sh test-wega`).  Festlegungen: `doc/merkposten/p8000.md` Nr. 40–43.

## 1. Aufbau

| Teil | Im Test | Am Gerät/in der Oberfläche |
|---|---|---|
| Rechner | `P8000Machine`, Vollgerät (16-Bit-Karte, WDC 4.2), `terminal = Original` | `p8000emu`, Modell *P8000 + P8000 Terminal* |
| Platte | Kopie von `~/.cache/k1520emu/p8000_wega/p15_7_sync.platte.img` (WEGA installiert, `sync`) | Plattenkasten ▸ Anschließen |
| Startdiskette (A:) | Kopie von `p15_7_sync.start.hfe` (UDOS mit Koppelsoftware `WEGA`) | Laufwerkskasten |
| Konsole tty1 | Originalterminal Typ 2 + K7673.09 (Firmware P8T 5.0 auf dem Z8) | Bildschirm des Hauptfensters |
| Arbeitsplätze | 3 × `P8000TerminalMachine` als Telnet-Client an tty0, tty2, tty4 | `p8000term` (je Arbeitsplatz ein Prozess, `--instance`) |
| Leitungen | Hub-Kanäle tty0/tty2/tty4 als Telnet-Server, Port 0 (frei), 127.0.0.1 | *Einstellungen ▸ Schnittstellen*, Vorschläge 5000/5002/5004 |

tty0 und tty2 laufen über die SIOs der 8-Bit-Karte und die Koppelsoftware (Ringpuffer → PIO-Kopplung →
WEGA-Kern), tty4 über SIO0 der 16-Bit-Karte direkt im Kern — beide Wege sind abgedeckt.  Getippt wird
überall als Matrixdruck an der K7673 (80 ms halten, 80 ms Pause), gelesen aus dem Bildspeicher des
jeweiligen Terminals.  Gleichschritt: 5 000 Rechnertakte (1,25 ms) ≙ 4 608 Z8-Takte je Arbeitsplatz.

## 2. Ablauf und Zeiten (Maschinenzeit bei 4 MHz)

| Schritt | Eingabe | Gesehen | Zeit |
|---|---|---|---|
| Netz ein | — | Terminal-Vorlauf 1,5 s, dann „P8000 Hardwaretest U880 - Version 3.1", „U880-Softwaremonitor Version 3.1 - Press RETURN" | |
| MON8 | RETURN, RETURN | `>`, dann Koppelsoftware von A:, „U8000-Softwaremonitor Version 3.1 - Press NMI" | |
| MON16 | NMI-Taste | „P8000 Hardwaretest U8001 - Version 3.1", „MAXSEG=<0F>" | |
| AUTOBOOT | **keine** | `boot0.md` aus Block 0 zeigt selbst „> boot", „Boot", „: md(0,16000)wega" | |
| WEGA | — | „WEGA Kernel -- Release 3.2", fsck aller Dateisysteme („** Phase 1 – 5", „/dev/rtmp", „/dev/rz"), „Going multi-user in 30 seconds!" | |
| Anmeldung Konsole | `wega`, `root` | „WEGA login:", „Password:", Tagesmeldung (`/etc/motd`), `#1` | 252 s / 264 s |
| Arbeitsplätze ein | — (Terminal erst nach stehender Verbindung einschalten) | Einschaltmeldung „ADM31/9600 baud …" | |
| Anmeldung tty0, tty2, tty4 | RETURN (getty hatte ins Leere gesendet), `wega`, `root` | „WEGA login:", Tagesmeldung, `#1` | 277 s, 288 s, 298 s |
| `who` (Konsole) | | `wega     tty0    May 30 21:12` / `wega     tty4    May 30 21:11` / `wega     tty2    May 30 21:10` / `wega     console May 30 21:10` | |
| parallele Last | `ls -l /bin` an allen vier gleichzeitig | letzte Zeile überall vollständig: `-rwxr-x--x 1 bin      system     7346 May 30 08:00 write` | |
| | `date`, `who am i` an allen vier | `Tue May 30 21:12:07 MES 1989`, je eigener Kanal | |
| XON/XOFF | `echo xyxy… \| tr xy '\033*' ; echo ABCD…` (30 × Bild löschen) an allen vier | Bild gelöscht, darunter vollständig `ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789`, Prompt | |
| Nachricht | tty0: `echo Gruss von tty0 \| write wega tty4` | tty4: `Message from wega tty0...` / `Gruss von tty0` / `EOF` | |
| Ab-/Anmelden | tty0: `exit`, dann `wega`/`root` | „WEGA login:" kommt von selbst wieder (inittab-Flag `c`); `who` zeigt tty0 mit neuer Zeit | |
| Abschalten | Konsole `sync;sync` | `#8` | 520 s |

Rechenzeit eines Laufs: 3–7 min (drei Läufe nebeneinander je ≈ 445 s).

## 3. Gefundene Fehler (behoben, je mit Wächter)

1. **U880 ohne EI-Sperre** (Merkposten 40).  Symptom: bei gleichzeitigen Eingaben an Konsole, tty0 und tty2
   blieb ≈ jeder zweite Lauf stehen — alle drei Kanäle der 8-Bit-Seite mitten im Wort still, tty4 lief weiter;
   einmal stand an der Konsole MON8 mit „BREAK AD82", einmal ein Bild voller „B".  Gefunden mit dem
   PC-Verlauf des U880 (`K1520_P22_DIAG2=1`): Sender-ISR `S0BS` (4338H) der Koppelsoftware wurde zwischen
   `EI` (43EBH) und `RETI` (43ECH) erneut angesprungen, SP sank je Runde um 2 bis in den Code bei 4300H.
   Behoben in `P8000Karte8::schritt` (Annahme erst nach dem auf `EI` folgenden Befehl).  Wächter
   `P8000Karte8_.NachEiErstNachDemFolgendenBefehlEinInterrupt`.  Danach 3/3 Läufe grün (vorher rund 4 von 10 rot).
2. **Wandler-Vorausblick nach langsamer Rate** (Merkposten 41) — durch die neue Zeitfolge aus 1. sichtbar
   geworden: MON16-Hardwaretest „*** ERROR 61 FF81" (tty4 interruptet nicht) in
   `P8000Monitor16.VonUdosBootetDerU8000VonDerWegaStartdiskette`.  Wächter
   `SerialWandler.NachWechselAufSchnellesFormatKeinVerspaeteterBlick`.

## 4. Geprüft und in Ordnung

- **XON/XOFF:** Die Firmware sendet bei 36 Zeichen im Empfangspuffer DC3; WEGA (`IXON` Vorgabe, `tty.c`)
  hält an.  Gegenprobe `K1520_P22_OHNE_IXON=1` (`stty -ixon` an allen vier): nach dem Löschen bleiben alle
  vier Bilder leer — Prüfzeile und Prompt gehen im Überlauf (45 Zeichen) verloren.
- **Einschalt-00H des Terminals:** trifft getty (NUL = Break ⇒ nächste Rate; GETTY-Typ 2 kennt nur 9600) —
  folgenlos.
- **Software-Parität** von getty (Bit 7): die Firmware löscht Bit 7 beim Empfang — Anmeldezeile lesbar.
- **Terminalart:** `/etc/ttytype` der Auslieferung führt alle Kanäle als `P8` (ADM31), die Betriebsart, in der
  das Terminal einschaltet.  (Das Systemhandbuch zeigt `PV`/VT100 für tty4–7 — nicht in WEGA 3.0.)
- **Baudrate:** getty-Typ 2 = 9600 Bd an allen Kanälen, Terminal fest 9600 8N (Bit 7 maskiert); der Hub
  wandelt Byte für Byte, Rahmenunterschiede (8N1/8N2) spielen keine Rolle.

## 5. Offen

- Die anderen Maschinen (A5120, K8915, PRG 710, PC 1715) nehmen Interrupts ebenfalls ohne EI-Sperre an
  (Merkposten 40) — dort bisher unauffällig, nicht geändert.
- Vollbildprogramme (`vi`) am Original nicht geprüft (termcap-Eintrag `P8`).
- Weitere Konten (`/etc/passwd` kennt nur `wega` als Anmeldekonto mit Kennwort); alle Arbeitsplätze melden
  sich als `wega` an.

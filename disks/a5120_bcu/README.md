# BCU 880 am frühen A5120

Betriebssystem **BCU 880 V1.2** (FZW/BL) für den A5120 der ersten Bauform: Bildschirm K7023
(64 × 16 Zeichen), Tastatur K7636, Laufwerke K5600.10 (5¼″, 40 Spuren, einseitig).

| Datei | Inhalt |
|-------|--------|
| `a5120_bcu12_k5600-10_start-syl.hfe` | **Startdiskette** „SYL auf Spur 1“ (Lader auf Spur 1, System ab Spur 2). Keine Dateidiskette |
| `a5120_bcu12_k5600-10_arbeit-sys1.hfe` | **Arbeitsdiskette** „SYS1“: dieselbe V1.2 ohne Lader, dazu PIP, STAT, SYSGEN, INIT, `KMBG.REL` (Kassettentreiber), LOAD, DDT, DISASS, FORMAT, MBASIC, EDIT80, EPROM, SUBMIT/XSUB u. a. |
| `bcu_kassette_formatiert.k52` | eine von `INIT` formatierte Kassette (Bandmarke am Anfang) |

## Starten (A5120 Emulator)

1. *Einstellungen ▸ Allgemein ▸ Modell*: **A5120 BCU 880 (Lade-ROM V0.6)**.
2. *Einstellungen ▸ Laufwerke*: A: und B: auf **K5600.10**.  Im 80-Spur-Laufwerk K5601 startet die
   Diskette nicht (Doppelschritt — das Lade-ROM findet den Lader auf Spur 1 nicht).
3. Startdiskette nach A:, Arbeitsdiskette nach B:, Kaltstart → `BCU 880 VERS 1.2`, `A>`.
4. Zuerst eine leere Zeile (Taste ↵) — die Tastatur schickt nach dem Einschalten ein Typzeichen,
   das BCU als erstes Zeichen der ersten Zeile liest.  Zeilenende ist die Taste **↵**.
5. Vor `^C` (INS LINE) oder dem Ende eines Programms die **Arbeitsdiskette auch nach A:** legen: der
   Warmstart lädt das System von A: Spur 0–1, auf der Startdiskette liegt dort der Lader.

## Kassette (Laufwerk F:)

*Einstellungen ▸ Allgemein ▸ Kassette* einschalten, im Kasten *Kassette* ein Band in **Gerät 2**
legen (F:; Gerät 1 ist E:), etwa eine Kopie von `bcu_kassette_formatiert.k52`, dann:

```
A>OPEN B:KMBG          kmbg opened
A>B:INIT               (nur fuer eine neue Kassette; Antwort Y)
A>PIP F:=B:STAT.COM
A>PIP B:STAT2.COM=F:STAT.COM
```

`INIT` endet nur mit Return (0DH), den die K7636 nicht hat: während des Formatierens **DEL LINE**,
danach **INS LINE** drücken (Warmstart).

## Herkunft

- Die beiden Disketten sind die Abzüge `doc/a5120_alt/bcu/satz2_SYL_auf_Spur1/disk.img` (Etikett
  „D15 SIOS 1SS Copy1“) und `satz2_OhneSYL.1/disk.img` („SYS1“), mit dem DiskTool unverändert nach
  `.hfe` gewandelt (`tools/bcu_a5120/build.py`; sektorgleich).
- Die Kassette hat BCU 880 im Emulator selbst formatiert (`B:INIT` an Gerät 2, Test
  `BcuKassette.FormatiertSchreibtLiest`).
- Hintergrund, Bedienung im Einzelnen und offene Fragen: `doc/a5120_alt/bcu.md`.

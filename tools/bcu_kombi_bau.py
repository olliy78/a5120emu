#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
bcu_kombi_bau.py  -  EIGENBAU: BCU-880-Diskette, die startet UND ein Dateisystem traegt
=====================================================================================

KEIN Beleg fuer irgendein Originalgeraet: eine solche Diskette gab es nicht.  Sie entsteht aus dem
Bootabbild einer Startdiskette „SYL auf Spur 1“ (disks/bootsektoren/boot_bcu12_syl_*.bin, 5 Zylinder)
durch zwei Aenderungen — beide im Emulator nachgewiesen (Test DisktoolBcu.Kombidiskette*), am Geraet
ungeprueft (doc/a5120_alt/bcu.md §12):

1. **Das Verzeichnis zieht hinter das System.**  Das BIOS sucht es ab Zylinder 2 (OFF = 2), dort liegt
   bei der Startdiskette das System.  Laufwerk A: bekommt deshalb eine EIGENE Parametertabelle mit
   OFF = 5 und DSM = 112 (113 Bloecke zu 1 KB = die 35 Zylinder 5-39): die im Original ungenutzte
   zweite Tabelle EC73H (80 Spuren; kein Laufwerkskopf zeigt darauf) wird dafuer ueberschrieben und
   der Laufwerkskopf A: (EC3DH) darauf umgebogen.  B:-D: behalten OFF = 2 — die Arbeitsdiskette „ohne
   SYL“ in B: bleibt lesbar.
2. **Der Warmstart bekommt sein CCP.**  Der Kaltstart (JP F112H) stellt den Warmstart auf „1 Spur,
   17 Sektoren ab Zylinder 0 Sektor 2 nach D780H“ (LD (F12CH),1101H) — das CCP (D780H-DFFFH) wird von
   A: Zylinder 0 neu geladen.  Auf der Startdiskette ist Zylinder 0 leer (deshalb der Absturz bei ^C);
   hier steht dort eine Kopie von D780H-DFFFH aus dem System selbst.

Mit ``--80`` fuer 80 Spuren (K5600.20, 96 tpi): A: OFF 5 / DSM 242, B:-D: OFF 2 / DSM 247.

Aufruf: bcu_kombi_bau.py [--80] <boot_syl.bin> <aus.bin>
"""
import sys

SPT, SEC = 26, 128
TRK = SPT * SEC
BASIS = 0xD680                       # Ladeadresse des Systems (Zylinder 2 Sektor 1)
DPB_NEU = 0xEC73                     # im Original ungenutzte zweite Tabelle (80 Spuren)
DPB_ALT = 0xEC82                     # die von allen vier Laufwerkskoepfen benutzte (40 Spuren)
DPH_A_DPB = 0xEC3D                   # Laufwerkskopf A: + 0AH = Zeiger auf die Tabelle
def dpb(dsm, off):
    """SPT 26, BSH 3, BLM 7, EXM 0, DSM, DRM 63, AL0/1 C0 00, CKS 16, OFF."""
    return bytes([0x1A, 0x00, 0x03, 0x07, 0x00, dsm, 0x00, 0x3F, 0x00, 0xC0, 0x00, 0x10, 0x00, off, 0x00])


DPB_A = dpb(112, 5)                  # 40 Spuren: 35 Zylinder ab 5 = 113 Bloecke
# 80 Spuren: A: 75 Zylinder ab 5 = 243 Bloecke (DSM 242); B:-D: bleiben OFF 2, aber DSM 247 — der
# Belegungsvektor je Laufwerk ist im BIOS nur 31 Byte (248 Bloecke) gross, 253 (DSM 252) liefen ins Nachbarlaufwerk.
DPB_A_80 = dpb(242, 5)
DPB_BCD_80 = dpb(247, 2)
CCP_VON, CCP_SEKTOREN = 0xD780, 17   # was der Warmstart nach dem Kaltstart nachlaedt


def crc(b):
    c = 0xFFFF
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def bauen(boot, spuren=40):
    assert len(boot) == 5 * TRK, 'Bootabbild muss Zylinder 0-4 umfassen (16640 Byte)'
    lader = boot[TRK:TRK + 768]
    assert lader[:3] == b'SYL' and crc(lader[7:0x2FE]) == int.from_bytes(lader[0x2FE:0x300], 'big'), \
        'kein SYL-Lader mit stimmiger Selbstpruefung'
    assert int.from_bytes(lader[3:5], 'little') == BASIS, 'Ladeadresse ist nicht D680H (V1.2-System erwartet)'
    laenge = int.from_bytes(lader[5:7], 'little')
    sysb = bytearray(boot[2 * TRK:2 * TRK + laenge])

    def at(a, n):
        return bytes(sysb[a - BASIS:a - BASIS + n])

    # Erwartungen an das V1.2-System, bevor etwas verbogen wird
    assert at(0xECAB, 8) == b'BCU 880 ', 'keine BCU-880-V1.2-Kennung bei ECABH'
    assert at(DPH_A_DPB, 2) == DPB_ALT.to_bytes(2, 'little'), 'Laufwerkskopf A: zeigt nicht auf EC82H'
    assert at(DPB_ALT, 15) == bytes([0x1A, 0, 3, 7, 0, 0x7A, 0, 0x3F, 0, 0xC0, 0, 0x10, 0, 2, 0]), \
        'DPB EC82H ist nicht die 40-Spur-Tabelle der V1.2'
    assert at(DPB_NEU, 15)[5] == 0xF2 and at(DPB_NEU, 15)[13] == 2, 'DPB EC73H ist nicht die 80-Spur-Tabelle'
    assert at(0xF12B, 3) == bytes([0x01, 0x02, 0x19]) and at(0xF112, 3) == bytes([0x21, 0x01, 0x11]), \
        'Neuladen/Kaltstart haben nicht die erwartete Form (BC=1902H, LD HL,1101H)'

    sysb[DPB_NEU - BASIS:DPB_NEU - BASIS + 15] = DPB_A if spuren == 40 else DPB_A_80
    if spuren == 80:
        sysb[DPB_ALT - BASIS:DPB_ALT - BASIS + 15] = DPB_BCD_80
    sysb[DPH_A_DPB - BASIS:DPH_A_DPB - BASIS + 2] = DPB_NEU.to_bytes(2, 'little')

    aus = bytearray(boot)
    aus[2 * TRK:2 * TRK + laenge] = sysb
    ccp = bytes(sysb[CCP_VON - BASIS:CCP_VON - BASIS + CCP_SEKTOREN * SEC])
    aus[SEC:SEC + len(ccp)] = ccp                     # Zylinder 0, Sektoren 2-18
    return bytes(aus)


def main():
    args = sys.argv[1:]
    spuren = 80 if '--80' in args else 40
    args = [a for a in args if a != '--80']
    if len(args) != 2:
        sys.exit(__doc__)
    with open(args[0], 'rb') as f:
        boot = f.read()
    with open(args[1], 'wb') as f:
        f.write(bauen(boot, spuren))


if __name__ == '__main__':
    main()

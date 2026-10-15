#!/usr/bin/env python3
"""EIGENBAU-Experiment (AP A7b): BCU-880-V2.1-System auf SYL-Bootaufbau.

KEIN Beleg fuer irgendein Originalgeraet: die Diskette gab es so nie.  Sie kombiniert
  - Zylinder 0/1 und den SYL-Lader (Zyl. 1, Sektoren 1-6) der V1.2-Diskette satz2_SYL_auf_Spur1,
  - den Kaltstartteil D680-D77F derselben Diskette (umgesetzt nach D600-D6FF, Absolutadressen
    gepatcht, Einsprung statt F112H -> D759H = Rest des V2.1-Kopfes: Bannerbyte, SIO-Tafel),
  - das V2.1-System (Zyl. 0-1 von satz2_OhneSYL.2 = 52 Sektoren) ab D700H.
Mit --treiber zusaetzlich (Variante B):
  - den residenten Plattentreiber F100-F47F der V1.2-SYL-Diskette (die V2.1-Diskette traegt ihn
    nicht; vermutlich steckt er im Start-EPROM) hinter dem System, und
  - Umbiegen der 11 Absolutadressen des V2.1-BIOS, die in diesen Treiber zeigen, auf die
    Einsprungadressen der V1.2-Fassung (Tabelle TREIBER_REFS).
Quellabbilder werden nur gelesen.
Aufruf: bcu_v21_eigenbau.py [--treiber] <SYL.img> <OhneSYL2.img> <aus.img>
"""
import sys

SPT, SEC = 26, 128
TRK = SPT * SEC

# (Adresse des Befehls im V2.1-BIOS, alter V2.1-Zielwert, Zielwert in der V1.2-Fassung)
TREIBER_REFS = [
    (0xEC18, 0xF18B, 0xF19C),   # BIOS-Sprungleiste HOME
    (0xEC1B, 0xF156, 0xF154),   # SELDSK
    (0xEC1E, 0xF18D, 0xF19E),   # SETTRK
    (0xEC27, 0xF1DA, 0xF1E7),   # READ
    (0xED2A, 0xF1DF, 0xF1EC),
    (0xEDEB, 0xF2E1, 0xF2E7),   # LD HL,<Tafel>
    (0xEDE1, 0xF2FB, 0xF301),   # LD HL,<Tafel>
    (0xEDA9, 0xF3BC, 0xF3BE),
    (0xEDE6, 0xF3E6, 0xF3E8),
    (0xEDF0, 0xF3E6, 0xF3E8),
    (0xECCA, 0xF11D, 0xF11B),   # WBOOT: CALL Neuladen
]


def main(syl_path, v21_path, out_path, treiber=False):
    syl = bytearray(open(syl_path, 'rb').read())
    v21 = bytearray(open(v21_path, 'rb').read())
    assert len(syl) == len(v21) == 40 * TRK

    # Kaltstartblock der V1.2: D680-D77F = Zylinder 2, Sektoren 1-2
    kalt = bytearray(syl[2 * TRK:2 * TRK + 2 * SEC])
    # erwartete Bytes pruefen, bevor gepatcht wird (Abweichung = falsche Quelle)
    assert kalt[0x1F:0x22] == bytes([0x21, 0x81, 0xD6])      # LD HL,D681H
    assert kalt[0x2B:0x2E] == bytes([0xEA, 0xA8, 0xD6])      # JP PE,D6A8H
    assert kalt[0x34:0x37] == bytes([0x21, 0x07, 0xD7])      # LD HL,D707H (Tafeln)
    assert kalt[0x84:0x87] == bytes([0xC3, 0x12, 0xF1])      # JP F112H
    kalt[0x20:0x22] = bytes([0x01, 0xD6])                    # D681 -> D601 (Block liegt bei D600H)
    kalt[0x2C:0x2E] = bytes([0x28, 0xD6])                    # D6A8 -> D628
    kalt[0x35:0x37] = bytes([0x87, 0xD6])                    # D707 -> D687
    # JP F112H -> D759H: dort beginnt im V2.1-Kopf (D700H) `LD A,50H; LD (ECC2H),A; ... OTIR; ...
    # JP ECD0H`.  Der Kopfteil davor (Stub nach F100H, Fenster F182-F184/F126/F12C/F239) setzt
    # das Start-EPROM voraus und wuerde den V1.2-Treiber zerstoeren.  Die 4 POPs am Ende laufen
    # ohne PUSH (SP 0080H -> 0088H) ins Leere; der Stapelinhalt ist belanglos.
    assert v21[0x59:0x5B] == bytes([0x3E, 0x50]) and v21[0x6A:0x6D] == bytes([0xC3, 0xD0, 0xEC])
    kalt[0x85:0x87] = bytes([0x59, 0xD7])

    system = bytearray(v21[:2 * TRK])                        # 52 Sektoren = Zylinder 0 und 1
    resident = b''
    if treiber:
        for adr, alt, neu in TREIBER_REFS:
            o = adr - 0xD700
            assert int.from_bytes(system[o + 1:o + 3], 'little') == alt, hex(adr)
            system[o + 1:o + 3] = neu.to_bytes(2, 'little')
        # V1.2-System ab D780H = Zylinder 2 Sektor 3; F100H = Sektor 3 + (F100-D780)/128 = 51 Sek.
        lin = 2 * 26 + 2 + (0xF100 - 0xD780) // SEC          # lineare Sektornummer
        resident = bytes(syl[lin * SEC:lin * SEC + 7 * SEC])  # F100-F47F (ab F450 loescht der Kaltstart)
        assert resident[0x1B:0x1D] == bytes([0x0E, 0x00])    # V1.2: F11B LD C,0
    payload = bytes(kalt) + bytes(system) + resident          # ab D600H
    nsec = len(payload) // SEC                               # 54 bzw. 61

    # Lader: Ladeadresse D600H, Laenge nsec*128 (liegt ausserhalb der Lader-CRC 0407-06FD)
    L = 1 * TRK
    assert syl[L:L + 3] == b'SYL' and syl[L + 3:L + 5] == bytes([0x80, 0xD6])
    syl[L + 3:L + 5] = (0xD600).to_bytes(2, 'little')
    syl[L + 5:L + 7] = (nsec * SEC).to_bytes(2, 'little')

    # System ab Zylinder 2 Sektor 1, Rest leer
    start = 2 * TRK
    syl[start:] = b'\xE5' * (len(syl) - start)
    syl[start:start + len(payload)] = payload
    open(out_path, 'wb').write(syl)


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--treiber']
    main(*args[:3], treiber='--treiber' in sys.argv)

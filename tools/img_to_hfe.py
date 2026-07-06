#!/usr/bin/env python3
"""
img_to_hfe.py – Wandelt eine rohe .img-Datei in eine HFE-v1-MFM-Datei.

Hardcodierte Geometrie (Mini-Fixture für Tests):
  - 2 Zylinder (cyl 0..1)
  - 2 Köpfe    (head 0..1)
  - 4 Sektoren × 128 B je Spur (sec 1..4)
  - Gesamtgröße .img: 2×2×4×128 = 2048 B

Sektorlayout in der .img (verschränkt, wie RawSectorImage):
  cyl0/head0 sec1..4, cyl0/head1 sec1..4, cyl1/head0 sec1..4, cyl1/head1 sec1..4

HFE-v1-Bit-Konvention (kompatibel zu BitCodec):
  - MFM, 250 kbit/s, 300 U/min
  - Internes Zellmodell: 16 Zellen/Byte, MSB-first (c0 d0 c1 d1 … c7 d7)
  - A1-Sync-Zellwort: 0x4489 (fehlendes Clock-Bit bei Bit 4 von oben)
  - Ablage im HFE-Byte: LSB-first → bitreverse8 je Byte

CRC-Kompatibilität zu TrackCodec::crc16 und parseTrack:
  - IDCRC  = crc16([A1,A1,A1,FE,cyl,head,sec,sizecode], seed=0xFF,0xFF)
  - DATACRC = crc16([A1,A1,A1,FB]+data, seed=0xFF,0xFF)  (Standard-IBM-MFM/CCITT)

Verwendung:
  python3 tools/img_to_hfe.py tests/fixtures/cpa_mini.img tests/fixtures/cpa_mini.hfe

Autor: Olaf Krieger
Datum: 2026
"""

import sys
import struct

# ─── Geometrie der Mini-Fixture (fest verdrahtet) ────────────────────────────

NUM_CYLS   = 2
NUM_HEADS  = 2
SECS_TRACK = 4
SEC_SIZE   = 128
BITRATE    = 250   # kbit/s
RPM        = 300


# ─── CRC-16-CCITT (byte-genau zu loaderCrc16 / TrackCodec::crc16) ─────────

def crc16(data: bytes, seed_hi: int = 0xFF, seed_lo: int = 0xFF) -> int:
    """
    Byte-genaue Python-Übersetzung von sub_0407/sub_1E44 (Robotron-Z80).
    Liefert (hi<<8)|lo als 16-Bit-Wert.
    """
    def ror(x: int, k: int) -> int:
        return ((x >> k) | (x << (8 - k))) & 0xFF

    b = seed_hi & 0xFF
    c = seed_lo & 0xFF
    for byte in data:
        a = (byte ^ b) & 0xFF
        b = a
        a = ror(a, 4) & 0x0F
        a = (a ^ b) & 0xFF
        b = a
        a = ror(a, 3)
        d = a
        a = ((a & 0x1F) ^ c) & 0xFF
        c = a
        a = ror(d, 1) & 0xF0
        a = (a ^ c) & 0xFF
        c = a
        a = ((d & 0xE0) ^ b) & 0xFF
        b = c
        c = a
    return (b << 8) | c


# ─── MFM-Bitcodierung (kompatibel zu BitCodec) ───────────────────────────────

def bitreverse8(x: int) -> int:
    """Spiegelt die 8 Bits eines Bytes (LSB↔MSB)."""
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1)
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2)
    x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4)
    return x & 0xFF


def mfm_cell_word(data_byte: int, prev_d_bit: bool) -> tuple[int, bool]:
    """
    Erzeugt ein 16-Bit-MFM-Zellwort (MSB-first) für ein Datenbyte.
    Gibt (zellwort, letztes_datenbit) zurück.
    """
    w = 0
    prev = prev_d_bit
    for i in range(7, -1, -1):
        d = (data_byte >> i) & 1
        c = 1 if not (prev or d) else 0
        w = (w << 2) | (c << 1) | d
        prev = bool(d)
    return w & 0xFFFF, prev


class MfmEncoder:
    """
    Akkumuliert MFM-Zellwörter (MSB-first intern) und wandelt am Ende in
    HFE-Bytes (LSB-first, bitreverse8 je Byte).
    """

    def __init__(self):
        # Interner MSB-first-Bitstrom als Bytearray
        self._bits: list[int] = []  # MSB-first Byte-Strom
        self._bitpos: int = 0       # aktuell belegte Bits
        self._prev_d: bool = False

    def _push_word(self, w: int) -> None:
        """Schreibt ein 16-Bit-Zellwort MSB-first in den Puffer."""
        for i in range(15, -1, -1):
            byte_idx = self._bitpos // 8
            bit_idx  = 7 - (self._bitpos % 8)
            if byte_idx >= len(self._bits):
                self._bits.append(0)
            if (w >> i) & 1:
                self._bits[byte_idx] |= (1 << bit_idx)
            self._bitpos += 1

    def sync_a1(self) -> None:
        """A1-Sync-Zellwort 0x4489 (fehlendes Clock-Bit); setzt prev_d=1."""
        self._push_word(0x4489)
        self._prev_d = True

    def sync_c2(self) -> None:
        """C2-IAM-Sync-Zellwort 0x5224 (fehlendes Clock-Bit); setzt prev_d=0."""
        self._push_word(0x5224)
        self._prev_d = False

    def byte(self, b: int) -> None:
        """Reguläres MFM-Datenbyte."""
        w, self._prev_d = mfm_cell_word(b, self._prev_d)
        self._push_word(w)

    def fill(self, b: int, count: int) -> None:
        """Füllt count Bytes mit demselben Wert."""
        for _ in range(count):
            self.byte(b)

    def bitcount(self) -> int:
        return self._bitpos

    def hfe_bytes(self, target_bits: int) -> bytes:
        """
        Liefert den Bitzellen-Strom als HFE-Bytes (LSB-first).
        Wird auf target_bits aufgefüllt (mit MFM-0x4E-Gap) oder abgeschnitten.
        """
        # Erst auf target_bits auffüllen mit 0x4E-Gap
        while self._bitpos < target_bits:
            w, _ = mfm_cell_word(0x4E, False)
            for i in range(15, -1, -1):
                if self._bitpos >= target_bits:
                    break
                byte_idx = self._bitpos // 8
                bit_idx  = 7 - (self._bitpos % 8)
                if byte_idx >= len(self._bits):
                    self._bits.append(0)
                if (w >> i) & 1:
                    self._bits[byte_idx] |= (1 << bit_idx)
                self._bitpos += 1

        # Auf ganze Bytes aufrunden, dann abschneiden
        target_bytes = (target_bits + 7) // 8
        while len(self._bits) < target_bytes:
            self._bits.append(0)
        raw = bytes(self._bits[:target_bytes])

        # MSB-first → LSB-first (bitreverse8 je Byte)
        return bytes(bitreverse8(x) for x in raw)


# ─── Spur bauen ───────────────────────────────────────────────────────────────

def build_side(cyl: int, head: int, sectors: list[bytes]) -> bytes:
    """
    Baut den MFM-Bitzellen-Strom für eine (cyl, head)-Spur.
    Layout je Sektor (wie TrackCodec::buildTrack + gapsFor(MFM)):
      gap4a(16×4E), IAM(C2×3 + FC), gap1(16×4E),
      je Sektor: 12×00, A1×3, FE, cyl, head, sec, sc, IDCRC×2,
                 gap2(11×4E), 12×00, A1×3, FB, data, DATACRC×2, gap3(24×4E)

    Zieldichte: 250 kbit/s, 300 U/min → (250000/300×60) = 50000 Bit/Umdrehung
    → 6250 Bytes/Seite.  Abgerundet auf 256-B-Grenze: 6144 B → aber wir nutzen
    6250 B als Ziel (bitcount), aufgerundet auf 8 = 6250.
    """
    enc = MfmEncoder()

    # ── Spur-Präambel ──────────────────────────────────────────────────────────
    enc.fill(0x4E, 16)          # gap4a
    # IAM (MFM): 3×C2 mit dem Sonder-Sync-Zellwort 0x5224 (fehlendes Clock-Bit),
    # analog zu BitCodec::encode und TrackCodec::buildTrack + BitCodec::encode.
    # Reguläre MFM-Codierung von C2 (0x52A4) erzeugt ein spuriöses 0x5224-Muster
    # an der Grenze 12×00-Sync / A1-Sync, das BitCodec::decode als Sync-Lock
    # missinterpretiert und 5 Bits vor dem A1 einrastet.
    enc.sync_c2()
    enc.sync_c2()
    enc.sync_c2()
    enc.byte(0xFC)              # IAM-Markenbyte (regulär codiert)
    enc.fill(0x4E, 16)          # gap1

    size_code = 0               # 128 B → size_code 0

    for sec_idx, sec_data in enumerate(sectors):
        sec_id = sec_idx + 1    # 1-basiert

        # ── IDAM ──────────────────────────────────────────────────────────────
        enc.fill(0x00, 12)      # 12×00 Sync
        enc.sync_a1()           # A1-Sync 0x4489
        enc.sync_a1()
        enc.sync_a1()           # drittes A1 — Mark-Byte folgt
        enc.byte(0xFE)          # IDAM-Mark

        # ID-CRC über [A1,A1,A1,FE,cyl,head,sec_id,size_code], Seed 0xFF,0xFF
        id_preamble = bytes([0xA1, 0xA1, 0xA1, 0xFE,
                             cyl, head, sec_id, size_code])
        id_crc = crc16(id_preamble, 0xFF, 0xFF)

        enc.byte(cyl)
        enc.byte(head)
        enc.byte(sec_id)
        enc.byte(size_code)
        enc.byte((id_crc >> 8) & 0xFF)
        enc.byte(id_crc & 0xFF)

        enc.fill(0x4E, 11)      # gap2

        # ── DAM ───────────────────────────────────────────────────────────────
        enc.fill(0x00, 12)      # 12×00 Sync
        enc.sync_a1()
        enc.sync_a1()
        enc.sync_a1()
        enc.byte(0xFB)          # DAM-Mark

        # Standard-IBM-MFM-Daten-CRC (CCITT) über [A1,A1,A1,FB] + Daten, Seed 0xFFFF
        # (identisch zu TrackCodec::buildTrack / parseTrack).
        data_crc = crc16(bytes([0xA1, 0xA1, 0xA1, 0xFB]) + sec_data, 0xFF, 0xFF)

        for b in sec_data:
            enc.byte(b)
        enc.byte((data_crc >> 8) & 0xFF)
        enc.byte(data_crc & 0xFF)

        enc.fill(0x4E, 24)      # gap3

    # Ziel: 250 kbit/s bei 300 U/min = 50000 Bits/Umdrehung
    target_bits = 50000
    return enc.hfe_bytes(target_bits)


# ─── HFE-v1-Container schreiben ───────────────────────────────────────────────

def write_hfe(out_path: str, sides: list[list[bytes]],
              num_cyls: int, num_heads: int) -> None:
    """
    Schreibt eine HFE-v1-Datei.

    sides[cyl][head] = Spur-Bytes (de-interleavt, je Seite track_len/2 Bytes).
    Die Spurdaten werden 256-B-verschränkt abgelegt.
    """
    # ── Alle Spurseiten bauen ────────────────────────────────────────────────

    # Wir erwarten, dass alle Seiten gleich lang sind (target_bits/8 aufgerundet).
    # track_len = 2 × Seiten-Bytes (beide Seiten zusammen, muss gerade sein).
    side0 = build_side(0, 0, sides[0][0])  # Dummy — wird gleich ersetzt
    side_len = len(side0)    # Bytes je Seite

    # Alle Spuren aufbauen
    track_data: list[list[bytes]] = []   # [cyl][head] = bytes
    for cyl in range(num_cyls):
        cyl_heads = []
        for head in range(num_heads):
            cyl_heads.append(build_side(cyl, head, sides[cyl][head]))
        track_data.append(cyl_heads)

    # Seitenlänge (einheitlich) sicherstellen
    side_len = len(track_data[0][0])
    track_len_bytes = side_len * num_heads  # Bytes für BEIDE Seiten

    # ── Interleaved Spurdaten erzeugen ────────────────────────────────────────
    def interleave_track(heads_data: list[bytes]) -> bytes:
        """
        Verschränkt die Spurseiten in 256-B-Blöcken: [S0_256][S1_256][S0_256]…
        Kurze Segmente werden mit 0x88 (HFE-Gap) aufgefüllt.
        """
        result = bytearray()
        total_per_side = side_len
        offset = 0
        while offset < total_per_side:
            for head in range(num_heads):
                chunk_start = offset
                chunk_end   = min(offset + 256, total_per_side)
                chunk       = heads_data[head][chunk_start:chunk_end]
                # Auf 256 Bytes auffüllen
                chunk = chunk.ljust(256, b'\x88')
                result += chunk
            offset += 256
        return bytes(result)

    interleaved: list[bytes] = []
    for cyl in range(num_cyls):
        interleaved.append(interleave_track(track_data[cyl]))

    # ── LUT aufbauen ─────────────────────────────────────────────────────────
    # Header = Block 0 (512 B), LUT = Block 1 (512 B),
    # Spurdaten ab Block 2.
    track_list_block = 1  # LUT ab Block 1
    track_data_start_block = 2

    lut_entries: list[tuple[int, int]] = []   # (offset_block, track_len_bytes)
    current_block = track_data_start_block
    for cyl in range(num_cyls):
        lut_entries.append((current_block, track_len_bytes))
        track_blocks = (len(interleaved[cyl]) + 511) // 512
        current_block += track_blocks

    # ── Header (512 B) ────────────────────────────────────────────────────────
    hdr = bytearray(512)
    hdr[0:8]  = b'HXCPICFE'
    hdr[0x08] = 0                       # formatrevision = 0
    hdr[0x09] = num_cyls                # number_of_tracks
    hdr[0x0A] = num_heads               # number_of_sides
    hdr[0x0B] = 0                       # track_encoding: 0 = ISOIBM_MFM
    struct.pack_into('<H', hdr, 0x0C, BITRATE)   # bitrate [kbit/s]
    struct.pack_into('<H', hdr, 0x0E, RPM)       # rpm
    hdr[0x10] = 0                       # iface (IBMPC_HD_FLOPPYMODE = 0)
    hdr[0x11] = 1                       # dnu = 1
    struct.pack_into('<H', hdr, 0x12, track_list_block)
    hdr[0x14] = 0xFF                    # write_allowed
    hdr[0x15] = 0xFF                    # single_step
    # Rest mit 0xFF füllen (ab 0x16)
    for i in range(0x16, 512):
        hdr[i] = 0xFF

    # ── LUT-Block (512 B) ─────────────────────────────────────────────────────
    lut_block = bytearray(512)
    for i in range(512):
        lut_block[i] = 0xFF
    for idx, (off, tlen) in enumerate(lut_entries):
        struct.pack_into('<H', lut_block, idx * 4 + 0, off)
        struct.pack_into('<H', lut_block, idx * 4 + 2, tlen)

    # ── Datei schreiben ────────────────────────────────────────────────────────
    with open(out_path, 'wb') as f:
        f.write(hdr)
        f.write(lut_block)
        for cyl in range(num_cyls):
            # Auf 512-B-Blockgrenze auffüllen
            data = bytearray(interleaved[cyl])
            pad  = (512 - len(data) % 512) % 512
            data += bytes([0x88] * pad)
            f.write(data)


# ─── .img lesen ───────────────────────────────────────────────────────────────

def load_img(path: str, num_cyls: int, num_heads: int,
             secs: int, sec_size: int) -> list[list[list[bytes]]]:
    """
    Liest eine .img-Datei mit verschränktem Layout
    (cyl0/h0, cyl0/h1, cyl1/h0, cyl1/h1; je Spur sec 1..secs).
    Gibt sides[cyl][head] = Liste von sec_data-bytes zurück.
    """
    expected = num_cyls * num_heads * secs * sec_size
    with open(path, 'rb') as f:
        raw = f.read()
    if len(raw) < expected:
        raw = raw + bytes(expected - len(raw))

    sides: list[list[list[bytes]]] = []
    offset = 0
    for cyl in range(num_cyls):
        cyl_heads: list[list[bytes]] = []
        for head in range(num_heads):
            track_secs: list[bytes] = []
            for _sec in range(secs):
                track_secs.append(raw[offset:offset + sec_size])
                offset += sec_size
            cyl_heads.append(track_secs)
        sides.append(cyl_heads)
    return sides


# ─── Blank-HFE (leerer, formatierbarer Container) ─────────────────────────────

# Seiten-Bytes je Spur des K5601-Blank-Templates.  Muss den größten §3-Track
# (5×1024 B MFM ≈ 11 KB HFE-Bytes/Seite) fassen; Wert = wie das funktionierende
# disks/cpadisk_*.hfe (len_bytes 25000 → 12500 B/Seite).
BLANK_SIDE_LEN = 12500
HFE_GAP        = 0x88   # HFE-Gap-Füllbyte (unformatierte Spur)


def write_blank_hfe(out_path: str, num_cyls: int, num_heads: int,
                    side_len: int = BLANK_SIDE_LEN) -> None:
    """
    Schreibt ein leeres, mountbares HFE-v1-MFM-Template (Header + LUT + Gap-Spuren).

    Der Inhalt jeder Spur ist reines HFE-Gap (0x88) — FORMAT.COM überschreibt beim
    Formatieren jede Spur bitgenau, sodass nur die Geometrie (Spuren/Seiten/Kapazität
    pro Spur) und ein gültiger Header/LUT relevant sind.  Damit nimmt EIN Template
    jedes §3-Format der K5601-Geometrie auf.
    """
    track_len_bytes = side_len * num_heads          # beide Seiten zusammen
    track_blocks    = (track_len_bytes + 511) // 512
    track_len_pad   = track_blocks * 512

    # ── Header (512 B) ────────────────────────────────────────────────────────
    hdr = bytearray(512)
    hdr[0:8]  = b'HXCPICFE'
    hdr[0x08] = 0                       # formatrevision = 0 (v1)
    hdr[0x09] = num_cyls                # number_of_tracks
    hdr[0x0A] = num_heads               # number_of_sides
    hdr[0x0B] = 0                       # track_encoding: 0 = ISOIBM_MFM
    struct.pack_into('<H', hdr, 0x0C, BITRATE)   # bitrate [kbit/s]
    struct.pack_into('<H', hdr, 0x0E, RPM)       # rpm
    hdr[0x10] = 0                       # iface (IBMPC_HD_FLOPPYMODE)
    hdr[0x11] = 1                       # dnu
    struct.pack_into('<H', hdr, 0x12, 1)         # track_list_block = 1
    hdr[0x14] = 0xFF                    # write_allowed
    hdr[0x15] = 0xFF                    # single_step
    for i in range(0x16, 512):
        hdr[i] = 0xFF

    # ── LUT-Block (512 B) ─────────────────────────────────────────────────────
    lut_block = bytearray(b'\xFF' * 512)
    current_block = 2                   # Spurdaten ab Block 2
    for cyl in range(num_cyls):
        struct.pack_into('<H', lut_block, cyl * 4 + 0, current_block)
        struct.pack_into('<H', lut_block, cyl * 4 + 2, track_len_bytes)
        current_block += track_blocks

    # ── Datei schreiben ────────────────────────────────────────────────────────
    with open(out_path, 'wb') as f:
        f.write(hdr)
        f.write(lut_block)
        gap_track = bytes([HFE_GAP]) * track_len_pad
        for _ in range(num_cyls):
            f.write(gap_track)


# ─── main ─────────────────────────────────────────────────────────────────────

def main() -> None:
    import argparse
    p = argparse.ArgumentParser(
        description="Erzeugt HFE-v1-MFM-Dateien: aus einer .img ODER als leeres "
                    "formatierbares Template (--blank).")
    p.add_argument('out', help="Ausgabe-.hfe")
    p.add_argument('img', nargs='?',
                   help="Eingabe-.img (Mini-Fixture-Geometrie); entfällt bei --blank")
    p.add_argument('--blank', action='store_true',
                   help="Leeres, formatierbares HFE-Template statt Konvertierung")
    p.add_argument('--cyls', type=int, default=NUM_CYLS,
                   help=f"Zylinder (Default {NUM_CYLS}; K5601: 80)")
    p.add_argument('--heads', type=int, default=NUM_HEADS,
                   help=f"Köpfe/Seiten (Default {NUM_HEADS}; K5601: 2)")
    p.add_argument('--side-len', type=int, default=BLANK_SIDE_LEN,
                   help=f"HFE-Bytes je Spurseite im Blank (Default {BLANK_SIDE_LEN})")

    # Rückwärtskompatibilität: `img_to_hfe.py <in.img> <out.hfe>` (2 Positionals).
    argv = sys.argv[1:]
    if len(argv) == 2 and not argv[0].startswith('-') and not argv[1].startswith('-') \
            and argv[0].endswith('.img'):
        in_path, out_path = argv[0], argv[1]
        print(f"Lese {in_path} ({NUM_CYLS}×{NUM_HEADS}×{SECS_TRACK}×{SEC_SIZE}B) …")
        sides = load_img(in_path, NUM_CYLS, NUM_HEADS, SECS_TRACK, SEC_SIZE)
        print(f"Erzeuge HFE-v1 MFM {BITRATE} kbit/s {RPM} U/min → {out_path} …")
        write_hfe(out_path, sides, NUM_CYLS, NUM_HEADS)
        print("Fertig.")
        return

    args = p.parse_args(argv)

    if args.blank:
        print(f"Erzeuge leeres HFE-Template {args.cyls}×{args.heads} "
              f"(side_len={args.side_len}) → {args.out} …")
        write_blank_hfe(args.out, args.cyls, args.heads, args.side_len)
        print("Fertig.")
        return

    if not args.img:
        p.error("ohne --blank wird eine Eingabe-.img benötigt")

    print(f"Lese {args.img} ({NUM_CYLS}×{NUM_HEADS}×{SECS_TRACK}×{SEC_SIZE}B) …")
    sides = load_img(args.img, NUM_CYLS, NUM_HEADS, SECS_TRACK, SEC_SIZE)
    print(f"Erzeuge HFE-v1 MFM {BITRATE} kbit/s {RPM} U/min → {args.out} …")
    write_hfe(args.out, sides, NUM_CYLS, NUM_HEADS)
    print("Fertig.")


if __name__ == '__main__':
    main()

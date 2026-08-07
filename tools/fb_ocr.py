#!/usr/bin/env python3
"""
fb_ocr.py — Framebuffer-OCR für die K7024-Bildschirmkarte
=========================================================

Zerlegt den 640×288-Pixel-Framebuffer der K7024 (`k1520_framebuffer`) wieder in
die einzelnen 80×24 Zeichen und rekonstruiert je Zelle den ausgegebenen ASCII-Code.

Idee: `K7024::renderChar` malt jede Zelle deterministisch aus dem Zeichen-
generator (Latein: zweistufig zg1=Zeilen 0–7 / zg2=Zeilen 8–11 aus v171/v172,
roh nach code*8 adressiert, mit echten Klein- und Großbuchstaben). Kehrt man das
um — Zelle → 12-Zeilen-Bitmap → Rück-Lookup im selben Modell — bekommt man das
angezeigte Zeichen zurück. Der Cursor (invertiert Zeilen 10–11) wird beim
Matchen rückgängig gemacht. Trifft eine Zelle *keine* Glyphe exakt, ist die
Rendering-Kette (VRAM → Chargen → Framebuffer) an der Stelle kaputt; solche
Zellen werden als `?` markiert und im Report aufgelistet.

Betriebsarten:
  * live booten (lädt libk1520core.so per ctypes) und Framebuffer auslesen (--boot DISK)
  * gedumpten Rohframebuffer (640*288 Bytes) einlesen  (--fb-file F)
  * kompletten Zeichensatz 0x20–0x7F rendern (kein Boot)  (--font-sheet [--png F])

Beispiele:
    tools/fb_ocr.py --boot disks/cpa_cpa780_k5601_clock.img
    tools/fb_ocr.py --font-sheet --png /tmp/font.png
    tools/fb_ocr.py --fb-file /tmp/fb.raw --grid

⚠️ Der Boot mountet die Disk read/write — immer gegen eine Temp-Kopie laufen,
   nie gegen ein committetes Fixture (das Tool legt die Kopie selbst an).
"""

import argparse
import ctypes
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]

# K7024-Geometrie (siehe core/cards/k7024/k7024.{h,cpp})
COLS, ROWS = 80, 24
CELL_W, CELL_H = 8, 12          # Zellraster 8×12
# Zum Matchen werten wir alle 12 Zeilen aus (Zeilen 0–7 aus zg1/v171, 8–11 aus
# zg2/v172) — nötig, weil sich z.B. g/q erst in der Descender-Zeile 10 unter-
# scheiden. Den Cursor (invertiert Zeilen 10–11) behandelt decode_framebuffer.
MATCH_ROWS = 12
FB_W, FB_H = COLS * CELL_W, ROWS * CELL_H   # 640 × 288


# ────────────────────────────────────────────────────────────────────────────
# Zeichengenerator-Modell laden (spiegelt K7024::renderChar)
# ────────────────────────────────────────────────────────────────────────────

def _hbytes(path: Path) -> list[int]:
    b = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", path.read_text())]
    if len(b) < 1024:
        sys.exit(f"{path.name}: nur {len(b)} Bytes (erwartet 1024)")
    return b[:1024]


def build_font():
    """Liefert glyph_of(code) → Tupel(MATCH_ROWS Bytes), exakt wie gerendert.

    Zweistufig (chargen_zg1 = Zeilen 0–7 / chargen_zg2 = Zeilen 8–11, aus den
    EPROMs A103/A123 bzw. v171/v172), roh nach code*8 adressiert. Der A5120 hat
    nur diesen einen (lateinischen) Zeichensatz — kein Kyrillisch.
    """
    kdir = PROJECT_ROOT / "core" / "cards" / "k7024"
    zg1 = _hbytes(kdir / "chargen_zg1.h")
    zg2 = _hbytes(kdir / "chargen_zg2.h")

    def glyph_of(code: int) -> tuple:
        c = code & 0x7F
        if c < 0x20:
            return tuple([0] * MATCH_ROWS)
        rows = [zg1[c * 8 + r] for r in range(8)]
        rows += [zg2[c * 8 + r] for r in range(MATCH_ROWS - 8)]
        return tuple(rows)
    return glyph_of


def build_glyph_map(glyph_of) -> dict[tuple, list[int]]:
    """Glyphe → *alle* Codes mit dieser Glyphe (Codes 0x20..0x7F).

    Ein Zeichengenerator ist nicht zwingend injektiv: mehrere Codes können
    dieselbe Glyphe tragen (leere Glyphen — oder, wie beim reinen Versal-ROM
    zg1/zg2, die auf Großbuchstaben gefalteten Kleinbuchstaben). Für »wird das
    *richtige* Zeichen ausgegeben?« ist genau diese Mehrdeutigkeit relevant,
    darum sammeln wir alle Codes je Glyphe.
    """
    gmap: dict[tuple, list[int]] = {}
    for code in range(0x20, 0x80):
        gmap.setdefault(glyph_of(code), []).append(code)
    return gmap


def ambiguous_codes(gmap: dict[tuple, list[int]]) -> dict[int, list[int]]:
    """Code → andere Codes mit identischer Glyphe (leere Glyphe ausgenommen).

    Statische Eigenschaft des ROMs: Codes, die pixelgleich gerendert werden und
    sich daher aus dem Framebuffer allein nicht unterscheiden lassen.
    """
    blank = tuple([0] * MATCH_ROWS)
    out: dict[int, list[int]] = {}
    for glyph, codes in gmap.items():
        if glyph == blank or len(codes) < 2:
            continue
        for c in codes:
            out[c] = [x for x in codes if x != c]
    return out


# ────────────────────────────────────────────────────────────────────────────
# Framebuffer-Zerlegung
# ────────────────────────────────────────────────────────────────────────────

def cell_glyph(fb: bytes, col: int, row: int) -> tuple:
    """Pixelzeilen 0..MATCH_ROWS-1 einer Zelle als Byte-Bitmap (bit7 = links)."""
    glyph = []
    for pr in range(MATCH_ROWS):
        base = (row * CELL_H + pr) * FB_W + col * CELL_W
        byte = 0
        for pc in range(CELL_W):
            if fb[base + pc]:                 # 0xFF gesetzt, 0x00 gelöscht
                byte |= 1 << (7 - pc)
        glyph.append(byte)
    return tuple(glyph)


def decode_framebuffer(fb: bytes, gmap: dict[tuple, list[int]]):
    """Framebuffer → (Zeichenraster, unbekannte Zellen, mehrdeutige Zellen).

    Pro Zelle wird die Glyphe im ROM-Modell zurückgeschlagen. Trägt eine Glyphe
    mehrere Codes, wird der kleinste als Anzeigezeichen genommen und die Zelle
    als *mehrdeutig* vermerkt — dort lässt sich aus den Pixeln allein nicht
    entscheiden, welches Zeichen der OS eigentlich geschrieben hat.
    """
    blank = tuple([0] * MATCH_ROWS)
    grid = []
    unknown = []       # (col, row, glyph)
    ambiguous = []     # (col, row, [codes])
    for row in range(ROWS):
        line = []
        for col in range(COLS):
            glyph = cell_glyph(fb, col, row)
            codes = gmap.get(glyph)
            if codes is None:
                # Cursor invertiert Zeilen 10–11 → rückgängig machen und erneut suchen
                dec = list(glyph)
                dec[10] ^= 0xFF
                dec[11] ^= 0xFF
                codes = gmap.get(tuple(dec))
            if codes is None:
                if glyph == blank:
                    line.append(" ")
                else:
                    line.append("?")
                    unknown.append((col, row, glyph))
                continue
            code = min(codes)
            if len(codes) > 1:
                ambiguous.append((col, row, sorted(codes)))
            line.append(chr(code) if 0x20 <= code < 0x7F else " ")
        grid.append(line)
    return grid, unknown, ambiguous


# ────────────────────────────────────────────────────────────────────────────
# Ausgabe / Report
# ────────────────────────────────────────────────────────────────────────────

def _cn(code: int) -> str:
    """Zeichencode → druckbare Kurzform, z.B. 0x73 → 's'(0x73)."""
    ch = chr(code) if 0x20 <= code < 0x7F else "·"
    return f"{ch!r}(0x{code:02X})"


def render_report(grid, unknown, ambiguous, gmap, truth=None, *,
                  show_grid: bool) -> str:
    out = []
    border = "+" + "-" * COLS + "+"
    out.append("Rekonstruierter Bildschirm (Framebuffer → ROM-Rückschlag):")
    out.append(border)
    non_space = 0
    for line in grid:
        out.append("|" + "".join(line) + "|")
        non_space += sum(1 for c in line if c != " ")
    out.append(border)

    total = COLS * ROWS
    out.append("")
    out.append(f"Belegte Zellen (nicht-leer):                 {non_space}/{total}")
    out.append(f"Unbekannte Glyphen (kein exakter ROM-Match): {len(unknown)}")
    out.append(f"Mehrdeutige Zellen (Glyphe → mehrere Codes): {len(ambiguous)}")

    if unknown:
        preview = ", ".join(f"({c},{r})" for c, r, _ in unknown[:16])
        out.append(f"  unbekannt bei (col,row): {preview}"
                   + (" …" if len(unknown) > 16 else ""))
        if show_grid:
            out.append("  Roh-Bitmaps der ersten unbekannten Zellen:")
            for c, r, glyph in unknown[:4]:
                out.append(f"    Zelle ({c},{r}):")
                for b in glyph:
                    out.append("      " + "".join(
                        "#" if (b >> (7 - i)) & 1 else "." for i in range(8)))

    # Statische ROM-Eigenschaft: welche Codes sind pixelgleich (nicht injektiv)?
    collisions = ambiguous_codes(gmap)
    if collisions:
        out.append("")
        out.append("ROM-Mehrdeutigkeiten (Codes mit identischer Glyphe — aus dem")
        out.append("Framebuffer prinzipiell nicht unterscheidbar):")
        for code in sorted(collisions):
            twins = ", ".join(_cn(t) for t in collisions[code])
            out.append(f"  {_cn(code)} ≡ {twins}")

    # Grundwahrheits-Vergleich (Console-Mode-Codes vs. Pixel-OCR)
    if truth is not None:
        out.append("")
        out.append("Grundwahrheit (Console-Mode = geschriebene VRAM-Codes) vs. Pixel-OCR:")
        mismatches = []
        wrong_letters: dict[str, str] = {}
        checked = 0
        for r in range(ROWS):
            for c in range(COLS):
                t = truth[r][c]
                if t == "":            # Zelle nie beschrieben → nicht bewerten
                    continue
                checked += 1
                o = grid[r][c]
                if t != o:
                    mismatches.append((c, r, t, o))
                    wrong_letters[t] = o
        out.append(f"  geprüfte (beschriebene) Zellen: {checked}")
        out.append(f"  Abweichungen Pixel≠Code:        {len(mismatches)}")
        if wrong_letters:
            summary = ", ".join(
                f"{k!r}→{v!r}" for k, v in sorted(wrong_letters.items()))
            out.append(f"  falsch gerenderte Zeichen:      {summary}")
            out.append("  → Der Zeichengenerator zeigt diese Codes als fremde"
                       " Glyphe an (Detail siehe ROM-Mehrdeutigkeiten oben).")
        else:
            out.append("  → alle beschriebenen Zellen werden korrekt gerendert.")
    return "\n".join(out)


# ────────────────────────────────────────────────────────────────────────────
# Live-Boot über libk1520core.so
# ────────────────────────────────────────────────────────────────────────────

def find_lib() -> Path:
    for p in (PROJECT_ROOT / "build" / "libk1520core.so",
              PROJECT_ROOT / "build" / "libk1520core.so.1"):
        if p.exists():
            return p
    sys.exit("libk1520core.so nicht gefunden — vorher `tools/dev.sh build`")


def font_sheet_via_lib(png_path: str | None):
    """Rendert jeden Code 0x20–0x7F über die *echte* Lib (VRAM-Write → Render-
    Pfad → Framebuffer) und gibt ihn als ASCII zurück; optional als PNG.

    Beweist den kompletten Render-Pfad K7024::renderChar für den ganzen Zeichen-
    satz, unabhängig davon welche Codes im Boot-Banner vorkommen.
    """
    lib = ctypes.CDLL(str(find_lib()), use_errno=True)
    lib.k1520_create.argtypes = [ctypes.c_int]
    lib.k1520_create.restype = ctypes.c_void_p
    lib.k1520_mem_write.argtypes = [ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint8]
    lib.k1520_framebuffer.argtypes = [ctypes.c_void_p]
    lib.k1520_framebuffer.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.k1520_fb_width.argtypes = [ctypes.c_void_p]
    lib.k1520_fb_width.restype = ctypes.c_int
    lib.k1520_fb_height.argtypes = [ctypes.c_void_p]
    lib.k1520_fb_height.restype = ctypes.c_int
    lib.k1520_destroy.argtypes = [ctypes.c_void_p]

    h = lib.k1520_create(0)
    if not h:
        sys.exit("k1520_create() lieferte NULL")

    # 96 Codes in ein 16×6-Raster schreiben (VRAM 0xF800, 80 Spalten pro Zeile)
    PER = 16
    positions = {}                       # code -> (col, row)
    for i, code in enumerate(range(0x20, 0x80)):
        col = (i % PER) * 3              # 3 Spalten Abstand → Lücke zwischen Glyphen
        row = i // PER
        positions[code] = (col, row)
        lib.k1520_mem_write(h, 0xF800 + row * 80 + col, code)

    w, hgt = lib.k1520_fb_width(h), lib.k1520_fb_height(h)
    fb = bytes(ctypes.string_at(lib.k1520_framebuffer(h), w * hgt))
    lib.k1520_destroy(h)

    # ASCII-Sheet
    out = ["Kompletter Zeichensatz (jeder Code über den echten Render-Pfad):", ""]
    for base in range(0x20, 0x80, PER):
        codes = list(range(base, min(base + PER, 0x80)))
        out.append(" ".join(f"0x{c:02X}  " for c in codes))
        out.append(" ".join(f" {chr(c)!r:<4}" if 0x20 <= c < 0x7F
                             else "  ·  " for c in codes))
        for pr in range(CELL_H):
            cells = []
            for c in codes:
                col, row = positions[c]
                base_px = (row * CELL_H + pr) * w + col * CELL_W
                cells.append("".join(
                    "#" if fb[base_px + pc] else "." for pc in range(CELL_W)))
            out.append(" ".join(cells))
        out.append("")

    if png_path:
        _write_font_png(fb, positions, w, png_path)
        out.append(f"PNG geschrieben: {png_path}")
    return "\n".join(out)


def _write_font_png(fb, positions, w, png_path):
    """Skaliertes PNG des gerenderten Zeichensatzes (grün auf schwarz)."""
    from PIL import Image
    PER, SC, PAD = 16, 6, 4               # Skalierung, Rand
    cell_w, cell_h = CELL_W + 1, CELL_H + 2      # inkl. Zwischenraum
    sheet_w = PER * cell_w
    sheet_h = 6 * cell_h
    img = Image.new("RGB", (sheet_w, sheet_h), (0, 0, 0))
    px = img.load()
    for code, (col, row) in positions.items():
        gi = code - 0x20
        gx, gy = (gi % PER) * cell_w, (gi // PER) * cell_h
        for pr in range(CELL_H):
            base = (row * CELL_H + pr) * w + col * CELL_W
            for pc in range(CELL_W):
                if fb[base + pc]:
                    px[gx + pc, gy + pr] = (0, 255, 70)
    img = img.resize((sheet_w * SC, sheet_h * SC), Image.NEAREST)
    img.save(png_path)


def boot_and_capture(disk: str, cycles: int, chunk: int, *, truth: bool):
    """Bootet eine Temp-Kopie der Disk.

    Liefert (framebuffer_bytes, truth_grid | None). Ist ``truth`` gesetzt, wird
    der Console-Mode aktiviert und die Folge der geschriebenen VRAM-Codes zu
    einem ROWS×COLS-Raster verdichtet (letzter Wert je Zelle gewinnt) — die
    Grundwahrheit dessen, was das OS *ausgeben wollte*.
    """
    lib = ctypes.CDLL(str(find_lib()), use_errno=True)
    lib.k1520_create.argtypes = [ctypes.c_int]
    lib.k1520_create.restype = ctypes.c_void_p
    lib.k1520_power_on.argtypes = [ctypes.c_void_p]
    lib.k1520_run.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.k1520_run.restype = ctypes.c_int32
    lib.k1520_mount_disk.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                     ctypes.c_char_p, ctypes.c_char_p, ctypes.c_bool]
    lib.k1520_mount_disk.restype = ctypes.c_bool
    lib.k1520_framebuffer.argtypes = [ctypes.c_void_p]
    lib.k1520_framebuffer.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.k1520_fb_width.argtypes = [ctypes.c_void_p]
    lib.k1520_fb_width.restype = ctypes.c_int
    lib.k1520_fb_height.argtypes = [ctypes.c_void_p]
    lib.k1520_fb_height.restype = ctypes.c_int
    lib.k1520_set_console_mode.argtypes = [ctypes.c_void_p, ctypes.c_bool]
    lib.k1520_console_poll.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int),
                                       ctypes.POINTER(ctypes.c_int),
                                       ctypes.POINTER(ctypes.c_char)]
    lib.k1520_console_poll.restype = ctypes.c_bool
    lib.k1520_destroy.argtypes = [ctypes.c_void_p]

    h = lib.k1520_create(0)          # K1520_MACHINE_A5120
    if not h:
        sys.exit("k1520_create() lieferte NULL")

    # ⚠️ read/write-Mount → nur gegen Temp-Kopie booten
    tmpdir = tempfile.mkdtemp(prefix="fb_ocr_")
    tmp = os.path.join(tmpdir, os.path.basename(disk))
    shutil.copy(disk, tmp)
    b = tmp.encode()
    if not (lib.k1520_mount_disk(h, 0, b, b"cpa780", False) or
            lib.k1520_mount_disk(h, 0, b, b"cpa800", False)):
        sys.exit(f"Disk {disk} konnte nicht gemountet werden (cpa780/cpa800)")

    truth_grid = [["" for _ in range(COLS)] for _ in range(ROWS)] if truth else None
    cx, cy, cc = ctypes.c_int(), ctypes.c_int(), ctypes.c_char()

    def drain():
        if not truth:
            return
        while lib.k1520_console_poll(h, ctypes.byref(cx), ctypes.byref(cy),
                                     ctypes.byref(cc)):
            x, y = cx.value, cy.value
            if 0 <= x < COLS and 0 <= y < ROWS:
                truth_grid[y][x] = cc.value.decode("latin1")

    if truth:
        lib.k1520_set_console_mode(h, True)
    lib.k1520_power_on(h)
    done = 0
    while done < cycles:
        step = min(chunk, cycles - done)
        ran = lib.k1520_run(h, step)
        drain()
        done += step
        if ran <= 0:
            break
    drain()

    w = lib.k1520_fb_width(h)
    hgt = lib.k1520_fb_height(h)
    if (w, hgt) != (FB_W, FB_H):
        print(f"WARN: Framebuffer {w}×{hgt} ≠ erwartet {FB_W}×{FB_H}",
              file=sys.stderr)
    ptr = lib.k1520_framebuffer(h)
    fb = bytes(ctypes.string_at(ptr, w * hgt))
    lib.k1520_destroy(h)
    shutil.rmtree(tmpdir, ignore_errors=True)
    return fb, truth_grid


# ────────────────────────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--boot", metavar="DISK",
                     help="Disk-Image booten und Framebuffer auslesen")
    src.add_argument("--fb-file", metavar="RAW",
                     help="gedumpten Rohframebuffer (640*288 Bytes) einlesen")
    src.add_argument("--font-sheet", action="store_true",
                     help="kompletten Zeichensatz 0x20–0x7F rendern (kein Boot)")
    ap.add_argument("--png", metavar="FILE",
                    help="mit --font-sheet: PNG des Zeichensatzes hierhin schreiben")
    ap.add_argument("--cycles", type=int, default=60_000_000,
                    help="Zyklen bis zum Framebuffer-Snapshot (default 60M)")
    ap.add_argument("--chunk", type=int, default=1_000_000,
                    help="Zyklen pro run()-Aufruf (default 1M)")
    ap.add_argument("--dump-fb", metavar="FILE",
                    help="gelesenen Rohframebuffer zusätzlich hierhin schreiben")
    ap.add_argument("--grid", action="store_true",
                    help="Roh-Bitmaps unbekannter Zellen mit ausgeben")
    ap.add_argument("--no-verify", action="store_true",
                    help="beim Boot keinen Console-Mode-Grundwahrheitsvergleich")
    args = ap.parse_args()

    if args.font_sheet:
        print(font_sheet_via_lib(args.png))
        return 0

    truth = None
    if args.boot:
        fb, truth = boot_and_capture(args.boot, args.cycles, args.chunk,
                                     truth=not args.no_verify)
    else:
        fb = Path(args.fb_file).read_bytes()
        if len(fb) != FB_W * FB_H:
            sys.exit(f"Framebuffer-Datei hat {len(fb)} Bytes "
                     f"(erwartet {FB_W * FB_H})")

    if args.dump_fb:
        Path(args.dump_fb).write_bytes(fb)

    glyph_of = build_font()
    gmap = build_glyph_map(glyph_of)
    grid, unknown, ambiguous = decode_framebuffer(fb, gmap)
    print(render_report(grid, unknown, ambiguous, gmap, truth,
                        show_grid=args.grid))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

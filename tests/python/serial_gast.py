"""Echo-Gast auf der DFÜ/V.24 eines gebooteten A5120 — geteilt von `py_serial_pyserial`
und `py_serial_api` (AP-T1b).

Die Bindung hat bewusst keinen Zugriff auf den PC.  Deshalb: CP/A bis `A>` booten, ein
kleines Z80-Programm in den RAM legen und CONST/CONIN der BIOS-Sprungleiste darauf
zeigen lassen — der nächste Konsolenaufruf landet dort.  Es programmiert A33-A auf
9600 Bd 8N1 mit RTS/DTR und schickt jedes Byte zurück; F1H nimmt DTR weg, F2H setzt
DTR und RTS, F3H nimmt RTS weg.

**Achtung:** bei „Bitte Uhrzeit eingeben!" ist die Seite 0 noch leer (`[0001H] = 0`) —
erst am Prompt patchen.
"""

from conftest import run_until_text

V24 = 0                 # DFÜ/V.24 = SIO A33 Kanal A, Ports 50H/51H
ORG = 0x8000


def assemble(items, org=ORG):
    """Kleinstassembler: Bytes, ("m", marke), ("jr", opcode, marke)."""
    code, marken, sprung = [], {}, []
    for it in items:
        if isinstance(it, tuple) and it[0] == "m":
            marken[it[1]] = org + len(code)
        elif isinstance(it, tuple) and it[0] == "jr":
            code += [it[1], 0]
            sprung.append((len(code) - 1, it[2]))
        else:
            code += list(it)
    for pos, ziel in sprung:
        d = marken[ziel] - (org + pos + 1)
        assert -128 <= d <= 127
        code[pos] = d & 0xFF
    return code


def echo_gast():
    init = []
    for x in (0x18, 0x04, 0x44, 0x03, 0xC1, 0x05, 0xEA, 0x01, 0x00):
        init += [0x3E, x, 0xD3, 0x51]               # LD A,x / OUT (51H),A
    return assemble([
        [0xF3, 0x31, 0x00, 0xA0],                   # DI / LD SP,0A000H
        [0x3E, 0x05, 0xD3, 0x0C, 0x3E, 0x01, 0xD3, 0x0C],   # ZRE-CTC K0: ×16 → 9600 Bd
        init,
        ("m", "rx"),
        [0xDB, 0x51, 0xE6, 0x01], ("jr", 0x28, "rx"),       # warten auf Rx
        [0xDB, 0x50, 0xFE, 0xF1], ("jr", 0x28, "aus"),
        [0xFE, 0xF2], ("jr", 0x28, "an"),
        [0xFE, 0xF3], ("jr", 0x28, "rts_aus"),
        [0x47],                                     # LD B,A
        ("m", "tx"),
        [0xDB, 0x51, 0xE6, 0x04], ("jr", 0x28, "tx"),       # warten auf TxEmpty
        [0x78, 0xD3, 0x50], ("jr", 0x18, "rx"),     # LD A,B / OUT (50H),A
        ("m", "aus"),                               # WR5 6AH: DTR aus, RTS an
        [0x3E, 0x05, 0xD3, 0x51, 0x3E, 0x6A, 0xD3, 0x51], ("jr", 0x18, "rx"),
        ("m", "rts_aus"),                           # WR5 E8H: DTR an, RTS aus
        [0x3E, 0x05, 0xD3, 0x51, 0x3E, 0xE8, 0xD3, 0x51], ("jr", 0x18, "rx"),
        ("m", "an"),
        [0x3E, 0x05, 0xD3, 0x51, 0x3E, 0xEA, 0xD3, 0x51], ("jr", 0x18, "rx"),
    ])


def bis_zum_prompt(emulator, temp_disk):
    """CP/A von einer Kopie booten, Uhrzeit eingeben, bis `A>`."""
    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert emulator.mount_disk(0, path, "cpa780", False), emulator.last_error()
    emulator.power_on()
    assert run_until_text(emulator, "Bitte Uhrzeit eingeben!", 60_000_000)
    for z in "120000":
        emulator.key_press(ord(z)); emulator.run(300_000)
        emulator.key_release(ord(z)); emulator.run(300_000)
    emulator.key_press(0x01000004); emulator.key_release(0x01000004)
    assert run_until_text(emulator, "A>"), emulator.screen_text()
    emulator.run(2_000_000)                             # Prompt fertig, BIOS in CONIN
    return emulator


def echo_gast_einsetzen(emu):
    """Echo-Gast an CONST/CONIN hängen und laufen lassen, bis er A33-A programmiert hat."""
    bios = emu.mem_read(1) | (emu.mem_read(2) << 8)    # JP WBOOT → Basis + 3
    assert emu.mem_read(0) == 0xC3 and bios > 0xC000, hex(bios)
    basis = bios - 3
    for i, b in enumerate(echo_gast()):
        emu.mem_write(ORG + i, b)
    for eintrag in (basis + 6, basis + 9):              # CONST, CONIN
        for i, b in enumerate((0xC3, ORG & 0xFF, ORG >> 8)):
            emu.mem_write(eintrag + i, b)
    emu.key_press(ord(" ")); emu.run(300_000)          # CONIN kehrt zurück, der
    emu.key_release(ord(" "))                           # nächste Aufruf landet im Gast
    for _ in range(200):
        emu.run(100_000)
        st = emu.serial_status(V24)
        if st.format_gueltig and st.rts:
            break
    st = emu.serial_status(V24)
    assert st.format_gueltig and st.baud_nenn == 9600 and st.rts and st.dtr, st
    return emu

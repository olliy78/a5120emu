# Boot-Analyse: A5120-Emulator

## 1. Symptom

Der Emulator startet nicht: Der Boot-ROM (0x0000–0x03FF) läuft in eine Endlosschleife,
ohne je den Floppy-Ladevorgang (CALL 0x0303) zu erreichen. Der Bildschirm bleibt leer.

---

## 2. Bisherige Erkenntnisse und Korrekturen

### 2.1 K3526 OPS — RAM-Initialisierung (behoben)

**Problem:** Die K3526-Klasse initialisierte ihren 64-KB-Speicher mit `0x00` (NOP-Sled).

**Auswirkung:** Beim ersten `RET M` im Boot-ROM (Adresse 0x0016) wird der Stack-Pointer
SP=0xFFFF dereferenziert. Das Byte an 0xFFFF (Kopplung aus K7024-VRAM und/oder K3526)
bestimmt den Sprungziel-PC zusammen mit dem ROM-Byte 0xEE an Adresse 0x0000.
Mit K3526=0x00 würde das Boot-ROM zu Adresse 0x0000 springen (NOP-Sled) statt zu einem
sinnvollen RST-Ziel.

**Korrektur:** `mem_.fill(0xFF)` in `K3526::K3526()`.

```
Echte DRAM-Hardware: Power-on-Zustand typischerweise 0xFF (oder undefiniert).
0xFF = RST 38h → springt zu 0x0038 (echter Boot-Einstieg im ROM).
```

**Datei:** `core/cards/k3526/k3526.cpp`

---

### 2.2 K7024 ABS — VRAM-Initialisierung und isWritable (behoben)

**Problem 1 — Zufällige VRAM-Initialisierung:**
Der K7024-Konstruktor initialisierte VRAM mit Zufallsdaten (Simulation von Power-on-Rauschen).
Das byte an VRAM-Offset 0x7FF (Adresse 0xFFFF) war zufällig. Das Boot-ROM liest beim
ersten `RET M` an 0x0016 exakt [0xFFFF] als low byte des Rücksprungiels.

**Auswirkung:** Zufälliges Sprungziel → kein deterministischer Boot möglich.

**Problem 2 — isWritable()=false (Fehlkorrektur):**
In einer früheren Diagnose wurde `K7024::isWritable()` auf `false` gesetzt in der Annahme,
dass Schreibzugriffe auf 0xF800-0xFFFF an K3526 gehen sollen. Das ist falsch.

**Korrektes K1520-Busverhalten (K7024 Lesesperre = X15:2–X16:2, Standardstellung):**
- CPU-Schreibzugriffe auf 0xF800–0xFFFF → **K7024 VRAM** (Bildschirmspeicher-Update)
- CPU-Lesezugriffe auf 0xF800–0xFFFF → **K7024 VRAM** (Inhalt des Bildschirmspeichers)

Der Z80-Stack nach Reset (SP=0xFFFF) liegt im VRAM-Bereich. Boot-ROM-RST-Befehle pushen
Return-Adressen dorthin. `LD A,(FFFCh)` liest denselben VRAM-Bereich zurück.
Schreiben (isWritable=false) und Lesen würden auf verschiedene Devices gehen → Inkonsistenz.

**Korrektur:**
1. `isWritable()` auf `true` zurückgesetzt (Original-Verhalten).
2. VRAM mit `0xFF` initialisiert statt Zufallswerten — entspricht dem typischen DRAM-Zustand
   nach dem Einschalten realer Hardware.

**Datei:** `core/cards/k7024/k7024.cpp`, `core/cards/k7024/k7024.h`

---

### 2.3 Boot-ROM-Ablauf (Analyse)

Das Boot-ROM ist eine verschachtelte Trampoline-Struktur aus RST-Befehlen (0xFF = RST 38h,
0xF7 = RST 30h, 0xDF = RST 18h), DJNZ-Schleifen und bedingten Returns.

**Kritischer Pfad (vereinfacht):**

```
Reset → PC=0, SP=0xFFFF, A=0xFF, F=0xFF
0x0000: XOR 0x77     → A=0x88, S=1 (negativ)
0x000A: RET P        → S=1 → NICHT ausgeführt (fällt durch)
0x000B: RET P        → S=1 → NICHT ausgeführt
0x000C: LD DE,5011h
0x0010: DEC DE (x2)
0x0014: DEC A (x2)   → A=0x86, S=1
0x0016: RET M        → S=1 → AUSGEFÜHRT!
  → PCL = [0xFFFF] = 0xFF (K7024 VRAM, init 0xFF)
  → PCH = [0x0000] = 0xEE (ROM[0x0000])
  → PC = 0xEEFF (K3526 RAM, init 0xFF = RST 38h)
0xEEFF: RST 38h      → PC = 0x0038 (echter Boot-Einstieg)

Ab 0x0038: Timing-Schleife via RST 30h/18h/DJNZ
0x003E: PUSH AF (x2) → Stack schreibt in K7024 VRAM (~0xFFFx)
0x0040: LD A,(FFFCh) → liest VRAM-Inhalt (von vorherigen Pushes/RSTs)
...
0x006F: CALL 0303h   → echter Floppy-Bootstrap-Code
```

**Port 0xD3** (mehrfach im Boot-ROM): Adresse liegt außerhalb aller bekannten Karten.
Möglicherweise ein historisches Relikt oder ein Signal auf dem Koppelbus der realen Hardware,
das im Emulator ignoriert werden kann (kein registriertes Gerät → write ignored).

---

## 3. Koppelbus — fehlende Verdrahtung (zu korrigieren)

### 3.1 ZRE CTC ZC/TO → Koppelbus

**Dokumentation (K2526 ZRE):** CTC ZC/TO[2] → CLK/TRG[3] "fest verdrahtet".
Die CTC-Ausgänge ZC/TO[0] und ZC/TO[2] müssen auf den Koppelbus getrieben werden.

**Aktueller Code (`a5120.cpp`):**
```cpp
koppel_.zc_to[2].connect([this](bool lvl) {
    zre_.ctc().clkTrg(3, lvl);   // Koppelbus → ZRE CTC CLK/TRG3
});
```
Fehler: Der Koppelbus-Signal `zc_to[2]` wird nie angetrieben (kein `drive()`-Aufruf).
ZRE CTC liefert kein ZCTO-Callback auf den Koppelbus.

**Korrektur:** ZCTO-Callback auf ZRE CTC setzen, der `koppel_.zc_to[ch].drive()` aufruft.

### 3.2 K8025 ASS — Baudrate-Takt (W1:7)

**Dokumentation (K8025):** Brücke W1:7 in "gezeichneter Stellung" = Baudrate-Takt kommt
vom ZRE CTC ZC/TO[0] über den Koppelbus.

**Aktueller Code:** Koppelbus `zc_to[0]` wird nicht mit K8025 CTC A34 verbunden.
K8025 CTC A34 erhält keine externen Taktimpulse aus dem ZRE CTC.

**Korrektur:** `koppel_.zc_to[0]` → K8025 CTC A34 CLK/TRG-Eingänge verbinden.

### 3.3 K8025 ASS — Interrupt-Kette (W1:8-W1:11)

**Dokumentation (K8025):** Brücken W1:8–W1:11 in "gezeichneter Stellung" = Drucker-SIO
(A32 Ch B) liegt in der **2. Interrupt-Kette (Koppelbus /IEI1–/IEO1)**, nicht im
Systembus.

**Aktueller Code:**
```cpp
bus_.setInterruptChain({&afs_, &ass_, &zre_});
```
K8025 (gesamte Karte) ist in der Systembus-Kette. Das ist teilweise falsch:
- SIO A33 (DFÜ), SIO A32 Ch A (Tastatur), CTC A34: Systembus-Kette — korrekt
- SIO A32 Ch B (Drucker): sollte Koppelbus 2. Kette sein — fehlt

Auswirkung auf Boot: Wahrscheinlich gering (Drucker wird beim Boot nicht genutzt).
Für spätere BIOS-Interrupt-Behandlung relevant.

---

## 4. Aktueller Stand (nach Korrekturen)

| Komponente | Status |
|---|---|
| K3526 RAM init 0xFF | ✅ behoben |
| K7024 VRAM init 0xFF | ✅ behoben |
| K7024 isWritable()=true | ✅ behoben |
| Koppelbus ZRE CTC → zc_to | ✅ behoben |
| Koppelbus zc_to[0] → K8025 CTC | ✅ behoben |
| Boot-ROM läuft bis CALL 0303h | ⬜ zu verifizieren |
| Floppy-Bootstrap lädt | ⬜ zu verifizieren |

---

## 5. Nächste Schritte

1. Build + Lauf mit boot_trace prüfen, ob CALL 0303h (Floppy-Bootstrap) erreicht wird.
2. Falls ja: Floppy-Emulation testen (K5122 AFS).
3. Falls nein: PC-Histogramm aus boot_trace auswerten, um Schleifenkerne zu identifizieren.


The boot trace still shows the same pattern - just more iterations of the same loop. The 0x001E appears only 3 times (DJNZ falls through 3 times in 5M cycles), but we never escape the loop!

Wait, looking at 0x001E appearing 3 times! The DJNZ does fall through 3 times in 5M cycles! But after falling through, we don't see 0x0303 or any other address beyond 0x0038 appearing. Let me look at what happens after 0x001E.

From the disassembly: 0x001E: OUT (D3h),A

0x0020: JR NC, -1 (target 0x0021) 0x0022: RST 38h

So after the OUT instruction, there's a conditional jump that loops back, and then RST 38h instructions. Let me check the actual ROM data at these addresses to understand what's really there. Verifying the DJNZ instruction target address—the offset 0x3E equals +62 decimal, so from PC 0x001E the jump lands at 0x005C as expected.

Now examining the next sequence: a JR NC instruction at 0x0020 with offset 0xFF (which is -1 in signed form), so if no carry flag is set it jumps back to 0x0021, landing on what appears to be a RST 38h instruction.

Looking at the execution paths, after the OUT instruction at 0x001E, the carry flag state becomes critical. The OUT instruction itself doesn't modify carry, so I need to trace back where carry gets set. Following the RST 18h path: there's a CCF instruction at 0x0030 that complements the carry flag, then LD A loads 0xDF without affecting it, and RST 18h jumps to 0x0018 where an ADD HL,BC operation would change the carry flag based on the result.

The key insight is that HL accumulates values through repeated additions of BC in the loop, and eventually when HL overflows, the carry flag gets set. At 0x0020, the JR NC instruction checks this carry state—if no carry is set, it jumps to 0x0021 (RST 38h), otherwise it falls through to 0x0022 (also RST 38h). Either way, both paths lead to the same RST 38h instruction.

Now I'm realizing that when RST 38h executes from 0x0021, it pushes the return address 0x0022 onto the stack and jumps to 0x0038. But this isn't going through the PUSH AF area at 0x003E-0x003F like I initially thought—it's a direct jump to 0x0038. At that address, there's an ADD A,A instruction, though I need to figure out what value A actually contains at that point since the earlier LD A,0xDF might have been modified by the loop iterations. I'm seeing the loop structure more clearly now—after RST 30h, we jump back to 0x0030 where CCF toggles the carry flag, then we load 0xDF into A and call RST 18h, which eventually leads back to DJNZ. The decrement causes B to wrap from 0 to 0xFF, so we jump back to 0x005C and cycle through again, never actually reaching the LD A,(FFFCh) path that would break out of this repetitive sequence. So RST 18h jumps to 0x0018, where it performs some operations with HL and BC, outputs to port D3h, then uses DJNZ to conditionally jump back to 0x005C. When that loop finally falls through with B=0, it outputs again and checks the carry flag—if no carry, it executes RST 38h which pushes the return address 0x0022 and jumps to 0x0038. There the code adds A to itself and conditionally jumps to 0x0048 if the result was zero, otherwise it decrements C at 0x003B.

Now I'm realizing that RST 30h at 0x003C doesn't actually return via an explicit RET instruction—the return address gets pushed onto the stack but then overwritten by subsequent RST calls, so the stack frame is essentially abandoned rather than properly unwound.

The stack here is doing double duty: it's storing both return addresses and timing constants that the code reads back via memory access to determine loop exit conditions. The "returns" are never actually used as returns—they're just values sitting on the stack that get read to control program flow.

Now I'm tracing through the RST instruction targets and their return paths. The RST 18h at 0x0033 pushes 0x0034 as a return address, but execution doesn't immediately return there. Instead, RST 18h jumps to 0x0018, where a DJNZ either branches to 0x005C if B is nonzero or falls through to 0x001E if B reaches zero, eventually triggering another RST 38h.

Following the first path where DJNZ jumps to 0x005C, I see a sequence that loads H with 0, pushes AF twice, then executes RST 38h at 0x0061, which pushes 0x0062 and jumps to 0x0038. From there, ADD A,A operates on 0xDF to produce 0xBE with carry set, the conditional jump is skipped, then RST 30h at 0x003C pushes 0x003D and jumps back to 0x0030. The stack now has multiple return addresses accumulating—0x0034, 0x0062, and 0x003D—and the only way execution can actually return to 0x0034 is through a RET instruction.

Looking at the available RET conditions in the ROM, I see RET P at 0x000A and 0x000B (which return when S=0), RET M at 0x0016, 0x0017, and 0x0026 (which return when S=1). The key is figuring out what value of A after the ADD A,A at 0x0038 would set S=0, allowing RET P to pop and return to the stacked address. If A starts at 0x80, then ADD A,A produces 0x00 with S=0, which would satisfy the RET P condition.

But tracing through the actual execution path: when A=0x40, ADD A,A gives 0x80 with S=1, so the JR Z at 0x0039 doesn't jump, falling through to 0x003B and 0x003C where RST 30h executes. This RST 30h eventually chains through RST 18h at 0x0033, which pushes 0x0034 to the stack and jumps to 0x0018 to run the DJNZ loop. After the loop completes, we hit OUT D3h at 0x001E, then JR NC which leads to RST 38h at 0x0021, pushing 0x0022 and jumping to 0x0038. At that point with A=0xDF, ADD A,A produces 0xBE with C=1 and Z=0, so we'd need A to be 0x00 to satisfy the return condition. Looking at the execution frequency data from the histogram, I can see that 0x0033 (RST 18h) executes 13 times across 5 million cycles, while 0x0061 (RST 38h at that address) shows 10 times, and the DJNZ fall-through at 0x001E only happens 3 times. This suggests the DJNZ loop does eventually break, and when it does, the subsequent RST 38h at 0x0021/0x0022 enters the routine at 0x0038 with a different register state than before.

But 0x0040 never appears in the histogram at all, which means either the code path to reach it requires a very specific state that's not being hit, or the boot ROM is spending most of its time in a different loop entirely—possibly one involving the CTC timer with a clock divider that makes the actual execution time much longer than the cycle count suggests.

Looking at the port writes, 0xD3 is marked as unknown hardware, but it could be an I/O port on the Koppelbus or similar that the emulator isn't handling properly, which might be causing the code to hang or loop unexpectedly.

What's interesting is that the histogram shows the inner loop instructions (0x0018 through 0x001C) all appearing with roughly equal frequency around 814-838 times, suggesting they're executing at the same rate rather than the outer loop running significantly more iterations than the inner one.

The DJNZ at 0x001C is jumping back to 0x005C almost every time, but it does fall through occasionally—only 3 times across the entire 5 million cycle sample. That's roughly once every 1.67 million cycles, which works out to about 256 complete iterations of the DJNZ loop at roughly 6500 cycles per iteration. After those rare fall-throughs at 0x001E, the code writes to the I/O port and then branches based on a condition flag, potentially routing to the interrupt handler at 0x0038.

The critical observation is that the histogram shows zero hits to any 0x030x addresses, meaning the floppy bootstrap routine at 0x0303 never executes during this 5 million cycle window. On real hardware, the boot sequence should complete within 2-3 seconds, so this absence suggests something might still be preventing the loop from exiting properly.

Looking at the exit conditions: the code reaches the CALL 0x0303 instruction only when A has bit 7 clear (so S flag is 0 after OR A) and A is non-zero (so Z flag is 0), which means A must fall in the range 0x01-0x7F.

Tracing back through the execution path, I can see how we arrive at 0x0068: starting from loading a value at 0xFFFC, then hitting RST 38h which jumps to 0x0038, where ADD A,A with A=0 sets the Z flag, triggering the conditional jump to 0x0048 where RLCA executes, followed by the unconditional jump to 0x0068.

The key insight is that at 0x0048, the carry flag determines whether A becomes 0x00 or 0x01 after RLCA—if carry is set, we get 0x01 which clears the Z flag and allows the CALL to 0x0303 to execute, breaking out of the timing loop. So I need to trace back further to see what sets the carry flag before reaching that RLCA instruction. So at 0x0068, the OR instruction with A=0x01 clears the zero flag, which means the code skips both conditional jumps and falls through to call 0x0303 — that's the breakthrough. The exit condition requires (FFFCh) to equal 0x80, but I'm stuck because none of the code paths I've traced actually produce A=0x80 to push there. The carry flag from ADD A,A with A=0x00 is always zero, so that doesn't help either.

Wait, if (FFFCh) contains 0x80 instead, then ADD A,A with A=0x80 produces a carry, RLCA sets A to 0x01, and the OR succeeds — so the ROM definitely needs 0x80 at that VRAM address. Now I'm trying to figure out where 0x80 originates, and the PUSH AF instruction is the key since it writes A to memory, so I need to trace how A gets set to 0x80 in the first place. Now I'm tracing through the stack pointer changes more carefully. After the RET M instruction at 0x0016, SP increments twice (once for each byte popped), moving from 0xFFFF to 0x0001. Then when RST 38h executes, I need to figure out where SP ends up and what value gets pushed to 0xFFFC. I caught an error in my stack pointer arithmetic—0xFFFE minus 1 is 0xFFFD, not 0xFFFC. So after pushing the return address 0x003D, SP lands at 0xFFFD, then the RST 18h instruction at 0x0033 pushes 0x0034 with the high byte going to 0xFFFC and the low byte to 0xFFFB, leaving SP at 0xFFFB before jumping to 0x0018.

Now tracing through the DJNZ loop—it eventually falls through to 0x001E, then 0x0020, where a conditional jump takes us to RST 38h at 0x0021, which pushes the return address 0x0022 onto the stack at 0xFFFA and 0xFFF9 respectively. From there we jump to 0x0038, which leads to another RST 30h at 0x003C that pushes 0x003D, and the stack pointer keeps decrementing as we nest deeper into these interrupt handlers.

Each RST instruction pushes another return address, so the stack grows downward through memory, with 0xFFFC getting repeatedly overwritten. I'm realizing there's something about the PUSH instruction that might be relevant here—

specifically, when PUSH AF executes at 0x005E and 0x005F, it's pushing the A register (0xDF from the earlier load) and the flags register onto the stack, filling positions 0xFFFA through 0xFFF7 with these values.

Then RST 38h at 0x0061 pushes the return address 0x0062 onto the stack, moving SP down to 0xFFF5. From there, nested RST instructions (RST 30h, RST 18h) continue pushing return addresses as the stack grows downward, with the second iteration of RST 18h bringing SP to 0xFFF3 and storing 0x0034 at that location.

The question now is what value ends up at 0xFFFC—whether it gets overwritten or remains untouched by this sequence of operations.

For 0xFFFC to contain 0x80, I need to trace when a PUSH AF instruction executes with SP=0xFFFD and the A register holds 0x80, which would write A to 0xFFFC. However, the PUSH AF at 0x005E uses A=0xDF from the preceding LD A,0xDF instruction, so that's not the source of the 0x80 value.

Looking at the actual VRAM contents after 5M cycles shows 0xFFFC = 0xDF, which matches what PUSH AF wrote when SP was 0xFFFD. Then LD A,(FFFCh) loads 0xDF into A, and at 0x0038 the ADD A,A instruction doubles it to 0xBE with the carry flag set.

Since Z isn't set, the JR Z at 0x0039 doesn't branch, so execution continues to DEC C at 0x003B and then RST 30h. The timing loop keeps cycling through, but 0xDF never produces the 0x80 value needed to trigger the exit condition. The SP must be decreasing as the loop runs, so eventually PUSH AF executes with a different stack pointer and potentially a different value in A, though A keeps getting reloaded as 0xDF at the start of each iteration. I'm getting bogged down in the ROM trace details—the RST 28h instruction jumps to 0x0028, which then calls RST 30h, creating an alternate execution path that I don't need to fully trace right now. I should just increase the cycle count to let the timing loop run longer and see if the boot ROM progresses, since it likely needs tens of millions of cycles to complete at the system's clock speed. Looking at the VRAM contents at the end of memory, I'm seeing some data there that might be relevant to the boot state. I'm noticing there are actually two RST 38h instructions back-to-back at 0x0043 and 0x0044. The first one pushes the return address and jumps to 0x0038, where it shifts the accumulator left and checks if the result is zero—if so, it branches to handle the RLCA and jump to 0x0068, otherwise it decrements C and executes another RST.

Now I'm tracing through the different cases based on what's stored at [FFFCh]. When it's 0x00 or 0x80, the ADD A,A produces zero with different carry flags, which affects the RLCA result and determines whether the code loops back or calls 0x0303. I'm working through the edge case where [FFFCh]=0xDF to see how that shifts.

The critical insight is that 0x80 is the only value that actually breaks out of the loop—specifically when the carry flag gets set during ADD A,A. That means I need to figure out what operation would set [FFFCh] to 0x80, which would be pushing the F register when only the Sign flag is set.

When ADD A,A executes with A=0x40, the result is 0x80, which sets the Sign flag. The overflow flag also gets set because 0x40+0x40 causes a sign change, so the F register becomes 0x84 (Sign and overflow both set, no carry, no zero flag).

Then when ADD A,A runs again with A=0x80, the result wraps to 0x00, setting the Zero and Carry flags instead. After DEC 0x81 to 0x80, the flags would be S=1, Z=0, H=0, P/V=0, N=1, giving F=0x82 or 0x83 depending on the carry flag—still not matching 0x80. Let me try ADD A,A with A=0xC0: that gives 0x180, so A becomes 0x80 with carry set.

With A=0x40 and ADD A,A, I get 0x80 with no carry, and the overflow flag sets because the sign flipped from positive to negative, making F=0x84. Getting F to be exactly 0x80 seems tricky—I'd need S=1 with all other flags clear, which requires a result in the negative range with no carry, no half-carry, and no overflow. Let me explore other approaches.

Actually, I've been confusing the byte order for PUSH AF. On the Z80, A is the high byte and F is the low byte, so when pushing, A goes to the higher address and F to the lower one. With SP starting at 0xFFFD, the first decrement puts A at 0xFFFC, then the second decrement puts F at 0xFFFB. So when DJNZ jumps to 0x005C with SP still at 0xFFFB, the first PUSH AF at 0x005E decrements SP to 0xFFFA and stores A (0xDF) there, then decrements again to 0xFFF9 and stores the flags register.

The second PUSH AF at 0x005F continues from SP=0xFFF9, pushing A and F to 0xFFF8 and 0xFFF7 respectively.

The key insight is that 0xFFFC—which was set to 0x00 by the initial RST 18h push—remains untouched by these PUSH AF operations as the stack grows downward, and subsequent RST pushes in later iterations will interact with different stack locations.

Looking at the VRAM snapshot after 5M cycles, I see 0xFFFC now contains 0xDF, which means a PUSH AF must have written there at some point when SP was 0xFFFD. This would happen after enough RST pushes (like RST 30h at 0x003C bringing SP to 0xFFFD, then RST 18h further adjusting it) before the PUSH AF at 0x005E executes.

But tracing through the actual execution path: RST 30h drops SP to 0xFFFD, then RST 18h at 0x0033 pushes again, bringing SP to 0xFFFB by the time PUSH AF runs. For PUSH AF to execute with SP=0xFFFD instead, I'd need to skip that RST 18h call entirely, which doesn't align with the control flow I'm seeing.

The VRAM data showing [0xFFFC]=0xDF after 5M cycles confirms PUSH AF did write there, so the stack pointer must have been at 0xFFFB at that moment. I should trace through the fall-through path more systematically to understand the exact sequence, or just follow what the emulator trace shows and check whether it eventually reaches CALL 0x0303.

On real hardware at 2.4576 MHz, the boot takes about 2 seconds—that's roughly 5 million cycles, which matches our simulation length. The timing loop exits when [0xFFFC] becomes 0x80, but right now it's 0xDF, so something needs to load A with 0x80 and write it there to break out of the loop.

Looking at how A could become 0x80: it could come from ADD A,A when A=0x40, or through rotation operations like RLCA. I'm tracing through the code at 0x0048 where RLCA appears, and checking if there's an XOR operation at 0x0073 that might be manipulating the accumulator value.

But now I'm wondering if the timing loop isn't actually supposed to exit via [0xFFFC]=0x80 at all—maybe there's a different exit condition I'm missing. Looking back at the disassembly around 0x003E through 0x0049, I see multiple PUSH AF instructions, a load from [0xFFFC], RST calls, and a conditional jump, followed by RLCA. The sequence is confusing because the RST instructions at 0x0043 and 0x0044 seem unusual in this context.

Let me trace through what happens when [0xFFFC]=0xDF: after loading that value and executing RST 38h, we jump to 0x0038 where ADD A,A doubles it to 0xBE with the carry flag set, then a conditional jump on zero isn't taken since the result isn't zero.

Then at 0x003B we decrement C and hit RST 30h, which pushes the return address 0x003D onto the stack and jumps to 0x0030. That target code starts with CCF, loads 0xDF into A, and executes RST 18h—so the code at 0x003E-0x0040 is actually part of the RST 30h handler itself, not a separate path.

When RST 18h runs from 0x0033, it pushes 0x0034 as the return address and jumps to 0x0018. After that returns, we're back at 0x0034, which is where things get interesting: there's a sequence of RST 38h calls and CP (HL) instructions, and then at 0x0038 we hit the ADD A,A instruction we've been tracking. The JR Z at 0x0039 jumps forward 13 bytes to 0x0048 if the zero flag is set.

Looking at what comes after, I see DEC C, then RST 30h calls, and finally PUSH AF instructions at 0x003E-0x003F. The key insight is that 0x0038 sits right in the middle of the RST 30h target code range, and those final PUSH AF instructions are at the very end of it. For execution to actually reach 0x003E-0x003F, the code has to be running within the RST 30h handler and somehow get past the RST 18h call that happens earlier. I'm realizing this could be a trampoline pattern where RST instructions push return addresses onto the stack, allowing the code to return to specific locations and continue execution sequentially. When an RST at one address pushes the next address as a return value, and that return is executed, it lands at the pushed address and can fall through to the next instruction. When RST 30h at 0x003D executes, it pushes the next address 0x003E onto the stack and jumps to 0x0030, where CCF and LD A,0xDF execute before RST 18h. That return address 0x003E stays on the stack as execution continues through DJNZ falling through to 0x001E, then 0x0020 with a conditional jump, eventually reaching RST 38h which jumps to 0x0038 where a Z-conditional branch doesn't trigger since A holds 0xBE.

The execution loops back through 0x003B and 0x003C before hitting RST 30h again, which pushes 0x003D this time. But I'm realizing the code never actually reaches 0x003E directly—there must be a RET instruction somewhere that's pulling from the stack. Looking at the ROM, I see RET M instructions at 0x0016 and 0x0017, though those are in the sequential code path rather than inside the RST 30h target at 0x0030.

Now examining the area around 0x0026-0x0027, which contains RET M instructions within what should be RST 38h padding space. The flow after DJNZ falls through to RST 38h at 0x0021, jumping to 0x0038, where ADD A,A and a conditional jump determine whether execution continues to 0x003B or branches elsewhere. But wait: if the RST 38h at 0x0021 pushes return address 0x0022. Then at 0x0038 → ...

Actually, I realize I need to look at ALL the RET instructions in the ROM:0x000A: RET P (S=0)0x000B: RET P (S=0) 0x0016: RET M (S=1) 0x0017: RET M (S=1)0x0026: RET M (S=1) 0x0027: RET M (S=1)And there are more at other addresses... let me look at the full ROM again:Row 2 (0x0020): 30 FF FF FF FF FF F8 F8 → 0x0026=F8=RET M, 0x0027=F8=RET M

Now I'm scanning through the remaining rows of the ROM to identify all RET instructions. Row 3 has no RET opcodes, Row 4 has none either, and Row 5 contains a POP AF at 0x0057 which isn't a RET. Continuing through Row 6 to see what else is there. I'm going to step back and just run more cycles to see if the boot ROM actually exits this timing loop, since I might be overcomplicating the control flow. Looking at what happens at 0xFFFC after 5M cycles—it's 0xDF, but it needs to reach 0x80. The issue is that A is consistently 0xDF or 0xBE when PUSH AF executes, and the F register doesn't seem to ever equal 0x80 based on my calculations, so I need to reconsider the register state from a different angle, particularly how B and C are decrementing together. So BC stays at 0x00xx since B is locked at 0, and C decrements each loop iteration. Each ADD HL,BC operation adds a small value (0-255) to HL, and since the loop runs ADD HL,BC twice per iteration, I need to figure out how many iterations it takes for HL to overflow past 0xFFFF. Each iteration decrements C and adds BC to HL twice, so HL grows by 2×C per cycle. Since C starts at 0xFF and counts down through 0xFE, 0xFD, and so on, I need to figure out when HL reaches 0x10000 and overflows. 000 (16-bit overflow). HL starts at 0. We add 2×C each iteration.

Sum of 2×C over one full cycle of C (0xFF down to 0x00): Sum = 2 × (0xFF + 0xFE + ... + 0x00) = 2 × (255+254+...+0) = 2 × 255×256/2 = 255×256 = 65280 = 0xFF00.

HL is 16-bit and maxes out at 0xFFFF. After one complete C cycle (256 iterations), HL would increase by 0xFF00 without overflow. But individual ADD HL,BC operations might trigger the carry flag when the sum exceeds 0xFFFF—for instance, when HL=0xFF00 and we add BC=0x0100, we get 0x10000, which overflows. Let me reconsider this by tracking the total accumulation: over 256×256 total DEC C operations across multiple outer loop cycles, HL accumulates roughly 65536 × 2 × 127.5, which is around 16 million—far exceeding 16-bit capacity and causing multiple overflows.

The key insight is that each ADD HL,BC can set the carry flag when overflow occurs. At instruction 0x0020, JR NC checks the carry flag: if carry is clear, it jumps to 0x0021 (RST 38h); if carry is set, it falls through to 0x0022 (also RST 38h). Both paths invoke RST 38h but from slightly different addresses, though both ultimately loop back to 0x0038.

Rather than continue tracing this manually, I should test with a much larger cycle count to see if the ROM ever reaches CALL 0x0303. I'm also noticing that the C register gets set to specific values during the loop—particularly, DEC C at 0x003B only executes when the JR Z at 0x0039 doesn't jump (meaning A≠0 after ADD A,A), which happens when A=0xDF from the LD A,0xDF instruction at 0x0031. So 0x001B is actually the operand for the OUT instruction at 0x001A, not a separate instruction. The real instruction is OUT (0xD3),A which is 2 bytes total.

Looking at the control flow: C decrements with each loop iteration at 0x003B, while B stays at 0. The HL register accumulates 2×C each time through. When ADD HL,BC causes an overflow and sets the carry flag, the conditional jump at 0x0020 will fall through instead of looping back.

Wait, I'm seeing two ADD HL,BC operations in sequence, so the carry flag that matters for the JR NC check comes from the second one. Either way, whether carry is set or not, the code seems to end up at RST 38h. Let me look at the actual byte values to clarify what's at each address.

0x0021 and 0x0022 both contain 0xFF, which is the RST 38h opcode. So whether the JR NC jumps or falls through, execution hits RST 38h—just at different addresses depending on the carry flag state.

The key difference is which return address gets pushed onto the stack: 0x0022 if the jump is taken, or 0x0023 if it's not. Later, when RET M executes with the sign flag set, it could pop whichever address is on the stack and jump back to it. If 0x0023 gets popped, that's another RST 38h instruction, creating a recursive loop through the same interrupt handler. I'm getting tangled up in the conditional branches and stack manipulations here—the RET M instruction keeps popping addresses that lead back into more RET M calls, creating this nested chain. Rather than trace through every cycle manually, I should just run an extended simulation to see how long it actually takes to reach the CALL at 0x0303. Looking at the boot trace output, I'm noticing the DJNZ fall-through at 0x001E appears multiple times within a 5-cycle window, so there's a pattern I can use to estimate the overall timing.

The real constraint is the ADD HL,BC×2 carry condition—I need HL to be large enough that adding twice the BC value overflows 0xFFFF. With BC starting at 0x00FF, that's only 0x1FE added, so HL needs to be quite high already for the carry to trigger.

But here's the key insight: HL accumulates across iterations. Each time DJNZ falls through (roughly every 1.67M cycles), HL grows by around 256 × 2 × the average C value. If C is around 0x80, that's a full 0x10000 increment per cycle—a complete 16-bit overflow. So the carry DOES get produced during each DJNZ fall-through.

The fact that 0x001E appears three times in the trace means three DJNZ fall-throughs occurred, which means three times the ADD HL,BC generated carry. Since JR NC at 0x0020 didn't jump those three times, execution fell through to RST 38h at 0x0022 each time.

This creates a complex stack state with multiple return addresses (0x0023, 0x003D, 0x0034, 0x0062, 0xFFFE after wrapping) being pushed and later popped when RET conditions are met. The nested RST/RET trampoline creates an intricate timing sequence. I should try running with 50M or 100M cycles to see if CALL 0x0303 eventually gets reached, since the ROM might just be taking far longer than expected to complete its initialization loop.
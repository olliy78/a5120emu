# K1520 Emulator - Open Points

Updated: 2026-05-16
Status: most initial architecture blockers are resolved.

## Resolved points

1. Backplane slot order for A5120 is clarified and implemented in machine wiring.
2. Keyboard serial channel mapping is clarified (K8025, SIO A32 channel A).
3. Z80 CPU has been moved into core/primitives and is no longer linked from legacy src for the modular core.
4. Logging system with compile-time log level is available and active.
5. Python GUI, ctypes binding, and integration tests are implemented.
6. Disk formats needed for current boot path are narrowed to cpa780 and cpa800.
7. K7637 EPROM file is available and has been inspected at binary level.

## New technical findings

### K7637 keyboard EPROM quick analysis

File: doc/EPROMS/robotron-k7637_50-2716.bin

- Size: 2048 bytes (2716 class EPROM)
- SHA256: e24c31033ee729a9bef9f73a3f1f6a8c1de7e73b38ea5a31974e1449715e59af
- Byte distribution contains significant code-like patterns (JP/CALL/CB-prefixed opcodes) mixed with data tables.
- High frequency of 0xFF and sparse printable text indicates firmware + lookup tables rather than plain text resources.

Interpretation:
- This is executable Z80 firmware with embedded tables.
- For exact behavioral emulation, a dedicated disassembly pass with code/data separation is still recommended.

## Remaining open points

### 1) Boot sequence still not producing expected screen output

Observed:
- CPU cycles execute.
- Disk mount works.
- Boot image cpa800 mounts.
- Display output still does not show expected boot messages in current run.

Need to verify:
- Is K2526 boot ROM execution path reaching the floppy bootstrap routine?
- Are floppy read attempts issued in the expected drive scan order?
- Are reads targeting correct C/H/S values for the CPA boot tracks?

Action in progress:
- Additional boot logs were added in CPU step trace and K5122 read/write/step paths.

### 2) GUI stability validation in desktop mode

Observed:
- Offscreen smoke test of MainWindow passes.
- User previously reported a Qt layout segfault.

Need to verify:
- Re-test on local desktop session with rebuilt libk1520core.so and updated Python binding path resolution.
- Confirm that no stale app/build symlink target is used after rebuild.

### 3) Documentation coverage gap

Status:
- Several files now include comments, but full English Doxygen/Google-style coverage is still incomplete across the entire codebase.

Need to finish:
- Remaining C++ headers and implementations with missing API-level comments.
- Python tests and helper scripts docstrings.

### 4) K7637 firmware-level behavior extraction

To finalize keyboard emulation accuracy:
- Identify command parser entry points.
- Confirm LED command encoding and repeat timing from ROM logic.
- Cross-check scan code tables against firmware tables.

## Suggested next implementation block

1. Run boot with LOG_LEVEL=4 and capture first 200 instruction PCs plus K5122 access logs.
2. Add a focused boot diagnostic test that asserts:
   - PC enters ROM region early.
   - At least one floppy sector read command is issued.
   - Drive LED activity toggles during boot.
3. Continue documentation pass on remaining high-impact modules:
   - core/cards/k2526
   - core/cards/k8025
   - core/peripherals/floppy_drive
   - app/ui/main_window.py
   - tests/*.py

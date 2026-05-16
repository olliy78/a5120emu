# K1520 A5120 Emulator - GUI Application

A faithful graphical emulation of the **Robotron A5120** computer from the 1980s (East German DDR).

## Prerequisites

- Python 3.8 or later
- PySide6 (Qt6 Python bindings)
- libk1520core.so (built from C++ core)

## Installation

### 1. Build the C++ Core

```bash
cd /path/to/a5120emu
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DLOG_LEVEL=2
make -j4
cd ..
```

This builds `libk1520core.so` which the Python GUI links to via ctypes.

### 2. Install Python Dependencies

```bash
pip install -r requirements.txt
```

This installs PySide6 (Qt6 bindings for Python).

## Running the Emulator

```bash
python3 app/main.py
```

The GUI window will open showing:
- The A5120 80×24 character display with authentic green phosphor color
- Control buttons (Power, Reset)
- Status bar showing cycles and disk activity
- Docked disk drive panel for mounting/unmounting disk images

## Features

### Display
- **Authentic green phosphor** rendering
- 80×24 character mode
- Real-time screen updates (50 Hz refresh)
- Character-based rendering (8×12 pixels per character)

### Disk Management
- Mount/unmount disk images for 4 floppy drives
- Support for multiple disk formats:
  - CPA800 (Robotron native)
  - CPA780
  - UDOS
  - SCP (Symbolic Computer Protocol)
  - MUTOS
  - K7
  - HDOS
- Write-protect control per drive

### Keyboard
- Full German QWERTZ keyboard emulation
- Shift and Ctrl modifier support
- Hardware-accurate scan code translation

### Control
- Power on/off toggle
- Emulator reset
- Speed control (1x, 2x, 10x, unlimited)

## Architecture

### C Core → Python Bridge

```
app/main.py
    ↓
app/ui/main_window.py (Qt6 MainWindow)
    ↓
app/core_binding/k1520.py (ctypes wrapper)
    ↓
libk1520core.so (C++ implementation)
```

### Key Classes

#### `K1520Emulator` (app/core_binding/k1520.py)
Python ctypes wrapper exposing the C-API:
- `power_on()` / `reset()` - Power management
- `run(cycles)` - Execute CPU cycles
- `mount_disk(drive, path, format, wp)` - Disk operations
- `key_press()` / `key_release()` - Keyboard input
- `get_framebuffer()` - Display data

#### `MainWindow` (app/ui/main_window.py)
Qt6 main application window:
- Embeds `ScreenWidget` for display rendering
- Manages emulation loop (50 Hz timer)
- Handles menus and dialogs

#### `ScreenWidget` (app/ui/screen_widget.py)
Qt6 custom widget for framebuffer rendering:
- Converts byte array to QImage
- Renders green phosphor color scheme
- Updates at 50 Hz to match original hardware

#### `DriveWidget` (app/ui/drive_widget.py)
Docked panel for disk management:
- 4 drive panels (one per floppy drive)
- Mount/unmount buttons
- Format selection
- Write-protect toggle

## Troubleshooting

### `libk1520core.so not found`
Make sure you built the C++ core:
```bash
cd build
cmake .. && make -j4
```

### `ModuleNotFoundError: No module named 'PySide6'`
Install the Python dependency:
```bash
pip install PySide6
```

### Display is blank
1. Check that a disk is mounted on drive 0
2. Try pressing Power OFF then ON
3. Check emulator logs: `CMAKE_BUILD_TYPE=Debug cmake ..`

## Booting

To boot an operating system:

1. Locate a disk image (e.g., `disk_b.img`)
2. Launch the GUI: `python3 app/main.py`
3. Mount the disk:
   - Click "Mount" on Drive 0
   - Select the disk image file
   - Choose the appropriate format (usually CPA800)
   - Leave Write-Protect unchecked
4. Power on the emulator
5. The A5120 will boot from the disk

### Test Boot

If disk_b.img is available:
```bash
python3 app/main.py
# Mount disk_b.img on drive 0
# Should see boot banner and CP/M prompt
```

## Performance

- **1x (Original)**: Real-time emulation at original 4 MHz Z80 speed
- **2x / 10x**: Faster-than-real-time execution
- **Unlimited**: Maximum speed (CPU-bound, no throttling)

Typical performance: 50-100 million Z80 cycles per second on modern hardware.

## Configuration

Machine configuration is stored in `app/config/machines/a5120.json`:
- Hardware specifications
- Default disk formats
- Boot ROM behavior
- Machine metadata

## Development Notes

### Adding New Features

1. **UI Components**: Add to `app/ui/`
2. **C-API Binding**: Update `app/core_binding/k1520.py`
3. **Core Features**: Modify C++ in `core/` and rebuild

### Logging

To increase verbosity:
```bash
cd build
cmake .. -DLOG_LEVEL=5 -DCMAKE_BUILD_TYPE=Debug
make clean && make -j4
```

Log levels:
- 0: OFF
- 1: ERROR
- 2: WARN (default)
- 3: INFO
- 4: DEBUG
- 5: TRACE

## References

- **Robotron A5120**: https://en.wikipedia.org/wiki/Robotron_K8920
- **Z80 CPU**: Zilog Z80 instruction set documentation
- **CP/M**: Digital Research CP/M operating system

## License

See LICENSE file in the repository root.

## Authors

Implementation by: K1520 Emulator Contributors

Original Robotron A5120: VEB Robotron, Dresden, DDR (1985)

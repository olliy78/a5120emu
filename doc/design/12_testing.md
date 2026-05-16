# Feinentwurf: Testkonzept

**Modul:** `tests/`

---

## 1. Überblick

Das Testsystem ist zweischichtig:
1. **C++ Unit-Tests** (GoogleTest): Testen einzelne Klassen isoliert
2. **Python Integrationstests** (pytest + ctypes): Testen Karten und Subsysteme über die C-API oder separate Karten-Libs

---

## 2. Testpyramide

```
         ┌─────────────────────────┐
         │  System-Test (Python)   │  ← Bootet vollständigen A5120, prüft Ausgabe
         ├─────────────────────────┤
         │  Integrations-Tests     │  ← K5122 + FloppyDrive, K7024 + K7637
         │  (Python ctypes)        │
         ├─────────────────────────┤
         │  Unit-Tests (C++)       │  ← Z80, PIO, SIO, CTC isoliert
         └─────────────────────────┘
```

---

## 3. C++ Unit-Tests (GoogleTest)

```
tests/cpp/
├── CMakeLists.txt
├── test_z80.cpp          # Z80 Instruktionen (aus src/tests/ übernommen)
├── test_bus.cpp          # K1520Bus: Dispatch, Interrupt-Chain
├── test_pio.cpp          # Z80PIO: Modus-Wechsel, Interrupt
├── test_sio.cpp          # Z80SIO: Senden, Empfangen, Interrupt
├── test_ctc.cpp          # Z80CTC: Timer, Counter, ZC/TO
├── test_k7024.cpp        # K7024: VRAM → Framebuffer
├── test_k5122.cpp        # K5122: PIO-Protokoll, Sektorzugriff
├── test_floppy.cpp       # FloppyDrive: Mount, Read, Write
└── test_format_parser.cpp # FormatParser: cpaFormates.cfg parsen
```

### 3.1 Beispiel-Tests

```cpp
// test_ctc.cpp
#include <gtest/gtest.h>
#include "primitives/z80_ctc.h"

TEST(Z80CTC, TimerMode_InterruptAfterTimeout) {
    Z80CTC ctc;
    // Kanal 0: Timer, Prescaler=16, TimeConst=10 → 160 Takte bis INT
    ctc.ioWrite(0, 0b10100101);  // IE=1, Mode=Timer, Prescaler=16, Trigger=Auto
    ctc.ioWrite(0, 10);          // Time constant = 10
    EXPECT_FALSE(ctc.hasInterrupt());
    for (int i = 0; i < 159; i++) ctc.clockTick();
    EXPECT_FALSE(ctc.hasInterrupt());
    ctc.clockTick();
    EXPECT_TRUE(ctc.hasInterrupt());
}

TEST(Z80CTC, ZCTOCallback) {
    Z80CTC ctc;
    bool zc_fired = false;
    ctc.setZCTOCallback([&](int ch, bool) { if (ch == 0) zc_fired = true; });
    ctc.ioWrite(0, 0b10100101);
    ctc.ioWrite(0, 1);   // TimeConst = 1 → 16 Takte
    for (int i = 0; i < 16; i++) ctc.clockTick();
    EXPECT_TRUE(zc_fired);
}

// test_k7024.cpp
TEST(K7024, VRAMWriteRenderChar) {
    K1520Bus bus;
    K7024 screen(bus, K7024::A5120Config{});

    // Zeichen 'A' (ASCII 0x41) in Zeile 0, Spalte 0
    bus.memWrite(0xF800, 0x41);

    const uint8_t* fb = screen.getFramebuffer();
    // Mindestens ein Pixel in Zeile 0 muss gesetzt sein
    bool any_set = false;
    for (int x = 0; x < 8; x++)
        any_set |= (fb[x] != 0);
    EXPECT_TRUE(any_set);
}

// test_floppy.cpp
TEST(FloppyDrive, MountAndReadBootSector) {
    FloppyDrive drv;
    auto formats = FormatParser::builtinFormats();
    auto cpa780 = *std::find_if(formats.begin(), formats.end(),
                                  [](auto& f){ return f.name == "cpa780"; });
    ASSERT_TRUE(drv.mount("disks/cpadisk.img", cpa780));
    auto data = drv.readSector(0, 0, 1);
    EXPECT_EQ(data.size(), 128u);
    EXPECT_NE(data[0], 0xFF);  // Kein Bus-Float
}
```

---

## 4. Python Integrationstests (pytest)

### 4.1 Struktur

```
tests/python/
├── conftest.py             # Fixtures: k1520_lib, machine, ...
├── test_bus.py             # Bus-Dispatch über Python-ctypes
├── test_k7024.py           # Bildschirm-Karte
├── test_k5122.py           # Floppy-Controller
├── test_k7637.py           # Tastatur
├── test_floppy_drive.py    # FloppyDrive
├── test_format_parser.py   # Disk-Format-Konfiguration
├── test_full_a5120.py      # System-Test: kompletter Boot
└── fixtures/
    ├── cpa780_boot.img      # Kleines Boot-Test-Image
    └── ...
```

### 4.2 conftest.py

```python
# tests/python/conftest.py
import ctypes
import pathlib
import pytest

BUILD_DIR = pathlib.Path(__file__).parent.parent.parent / "build"

@pytest.fixture(scope="session")
def k1520_lib():
    """Lädt libk1520core.so."""
    lib = ctypes.CDLL(str(BUILD_DIR / "libk1520core.so"))
    _setup_sigs(lib)
    return lib

@pytest.fixture
def machine(k1520_lib):
    """Erstellt eine A5120-Instanz, teardown nach Test."""
    h = k1520_lib.k1520_create(0)  # 0 = A5120
    yield h
    k1520_lib.k1520_destroy(h)

@pytest.fixture
def tmp_cpa800_image(tmp_path):
    """Erstellt ein leeres CPA800 Image für Schreib-Tests."""
    path = str(tmp_path / "test.img")
    # 80×2×5×1024 = 819200 Bytes
    pathlib.Path(path).write_bytes(b'\xE5' * 819200)
    return path

def _setup_sigs(lib):
    lib.k1520_create.restype = ctypes.c_void_p
    lib.k1520_create.argtypes = [ctypes.c_int]
    lib.k1520_destroy.restype = None
    lib.k1520_destroy.argtypes = [ctypes.c_void_p]
    lib.k1520_reset.restype = None
    lib.k1520_reset.argtypes = [ctypes.c_void_p]
    lib.k1520_run.restype = ctypes.c_int
    lib.k1520_run.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.k1520_framebuffer.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.k1520_framebuffer.argtypes = [ctypes.c_void_p]
    lib.k1520_fb_width.restype = ctypes.c_int
    lib.k1520_fb_height.restype = ctypes.c_int
    lib.k1520_mount_disk.restype = ctypes.c_bool
    lib.k1520_mount_disk.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                      ctypes.c_char_p, ctypes.c_char_p,
                                      ctypes.c_bool]
    lib.k1520_mem_read.restype = ctypes.c_uint8
    lib.k1520_mem_read.argtypes = [ctypes.c_void_p, ctypes.c_uint16]
    # ... weitere Signaturen
```

### 4.3 Vollständiger System-Test

```python
# tests/python/test_full_a5120.py
import time

def test_boot_cpa_to_prompt(k1520_lib, machine):
    """Bootet CPA von Disk und prüft ob der CCP-Prompt erscheint."""
    assert k1520_lib.k1520_mount_disk(
        machine, 0, b"disks/cpadisk.img", b"cpa780", False)

    k1520_lib.k1520_reset(machine)
    k1520_lib.k1520_set_console_mode(machine, True)

    # 20 Millionen Zyklen ≈ ~8 Sekunden Echtzeit auf 2.4576 MHz
    TOTAL_CYCLES = 20_000_000
    BATCH = 100_000
    output = []
    x_buf = ctypes.c_int(0)
    y_buf = ctypes.c_int(0)
    ch_buf = ctypes.c_char(0)

    for _ in range(TOTAL_CYCLES // BATCH):
        k1520_lib.k1520_run(machine, BATCH)
        while k1520_lib.k1520_console_poll(
                machine, ctypes.byref(x_buf),
                ctypes.byref(y_buf), ctypes.byref(ch_buf)):
            output.append(ch_buf.value.decode("latin-1"))

    text = "".join(output)
    # CPA-Prompt ist "A>"
    assert "A>" in text, f"Kein CCP-Prompt gefunden. Ausgabe: {text[:200]!r}"


def test_reset_clears_screen(k1520_lib, machine):
    """Nach Reset ist der Bildschirm leer."""
    k1520_lib.k1520_reset(machine)
    k1520_lib.k1520_run(machine, 1000)

    fb_ptr = k1520_lib.k1520_framebuffer(machine)
    w = k1520_lib.k1520_fb_width(machine)
    h = k1520_lib.k1520_fb_height(machine)
    fb = bytes(fb_ptr[:w * h])
    # Nach kurzer Ausführung: kaum Pixel gesetzt
    non_zero = sum(1 for b in fb if b != 0)
    assert non_zero < w * h * 0.05  # < 5% gesetzt
```

---

## 5. Test-Runner

```bash
# C++ Tests bauen und ausführen
cmake -B build -DBUILD_TESTS=ON
cmake --build build --target all
cd build && ctest --output-on-failure

# Python Tests
cd tests/python
pytest -v

# Python Tests mit Coverage
pytest --cov=../../app --cov-report=html -v

# Spezifischen Modul-Test ausführen
pytest test_k7024.py -v
pytest test_full_a5120.py::test_boot_cpa_to_prompt -v -s
```

---

## 6. Continuous Integration

```yaml
# .github/workflows/test.yml (oder GitLab CI)
stages:
  - build
  - test

build:
  script:
    - cmake -B build -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
    - cmake --build build

test_cpp:
  script:
    - cd build && ctest --output-on-failure

test_python:
  script:
    - pip install -r app/requirements.txt pytest pytest-cov
    - pytest tests/python/ -v --tb=short
```

---

## 7. Test-Abdeckungsziele

| Modul | Ziel-Coverage |
|-------|--------------|
| Z80CPU | > 90% (aus bestehendem Test-Satz) |
| Z80PIO / SIO / CTC | > 80% |
| K1520Bus | > 90% |
| K7024 (Framebuffer) | > 70% |
| K5122 (FDC) | > 70% |
| FloppyDrive | > 85% |
| FormatParser | > 95% |
| K7637 | > 75% |
| System-Test (Boot) | Manuell / E2E |

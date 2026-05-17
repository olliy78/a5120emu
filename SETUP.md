# K1520 A5120 Emulator - Setup & Schnellstart

## Voraussetzungen

- Python 3.8 oder später
- C++ Compiler (g++, clang)
- CMake 3.16+

## Installation

### 1️⃣ Virtual Environment einrichten

```bash
cd /home/olliy/projects/a5120emu
python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
```

### 2️⃣ C++ Core bauen

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DLOG_LEVEL=2
make -j4
cd ..
```

### 3️⃣ GUI starten

```bash
export LD_LIBRARY_PATH=$PWD/build:$LD_LIBRARY_PATH
source venv/bin/activate
python3 app/main.py
```

**Oder einfacher:**
```bash
bash run_gui.sh
```

---

## Tests ausführen

```bash
export LD_LIBRARY_PATH=$PWD/build:$LD_LIBRARY_PATH
source venv/bin/activate

# Integrationstests (7 Tests)
python3 test_integration.py

# Boot-Diagnostik
python3 test_boot.py
python3 test_boot_detailed.py

# C-API Test
gcc -o test_c_api test_c_api.c -ldl
./test_c_api
```

---

## Virtual Environment deaktivieren

```bash
deactivate
```

---

## Troubleshooting

### `ModuleNotFoundError: No module named 'PySide6'`
**Lösung:** venv ist nicht aktiviert
```bash
source venv/bin/activate
```

### `libk1520core.so not found`
**Lösung:** LD_LIBRARY_PATH nicht gesetzt oder Build fehlgeschlagen
```bash
export LD_LIBRARY_PATH=$PWD/build:$LD_LIBRARY_PATH
```

### GUI startet nicht
**Lösung:** X11/Display nicht verfügbar
```bash
# Headless testen:
python3 test_integration.py
```

---

## Struktur

```
a5120emu/
├── venv/                    # Virtual Environment (nach Setup)
├── build/                   # CMake Build Artefakte
├── core/                    # C++ Kern-Emulator
│   ├── CMakeLists.txt
│   ├── api/                 # C-API (k1520_api.*)
│   ├── bus/                 # K1520 Bus
│   ├── cards/               # CPU, RAM, Display, Serial, Floppy
│   ├── primitives/          # Z80 Chips (PIO, SIO, CTC)
│   └── machines/            # Machine integration
├── app/                     # Python GUI
│   ├── main.py              # Entry point
│   ├── core_binding/        # ctypes Wrapper
│   └── ui/                  # Qt6 Widgets
├── requirements.txt         # Python Abhängigkeiten
├── run_gui.sh              # GUI Launcher Script
└── test_*.py               # Test Suite
```

---

## Nächste Schritte

1. ✅ venv eingerichtet
2. ✅ C++ Core gebaut
3. 🔄 GUI testen und fehlende Boot-Features debuggen
4. 🔄 Disk-Boot-Sequenz trace und fixieren
5. 📊 Performance-Optimierungen

---

**Für Fragen siehe:**
- [APP_README.md](APP_README.md) - GUI Dokumentation
- [doc/open_points.md](doc/open_points.md) - Architektur & TODOs
- [PROJECT_STATUS.md](PROJECT_STATUS.md) - Implementierungs-Status

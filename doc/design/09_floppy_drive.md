# Feinentwurf: FloppyDrive – Diskettenlaufwerk-Emulation

**Modul:** `core/peripherals/floppy_drive/`  
**Dateien:** `floppy_drive.h`, `floppy_drive.cpp`, `format_parser.h`, `format_parser.cpp`  
**Format-Referenz:** `/home/olliy/projects/CPA_Workbench/cpaFormates.cfg`

> **Hinweis (2026-06-10): neue Schicht daneben.** Dieses Dokument beschreibt das
> ursprüngliche `FloppyDrive` (Inline-Sektor-IO über `.img`), das von der alten `K5122`
> genutzt wird und unverändert bleibt.  Der Floppy-Refactor hat im selben Verzeichnis eine
> **neue, DiskImage-basierte Schicht** ergänzt, die die neue Karte `K5122v2` nutzt:
> `track_image.*` (zentrale `TrackImage`-Abstraktion), `track_codec.*` (FM/MFM-Track
> bauen/parsen + CRC), `drive_profile.*` (physische Laufwerksprofile), `disk_image.*` +
> `raw_sector_image.*` (austauschbare Backends) und `floppy_drive2.*` (`FloppyDriveV2` mit
> Profil + Track-Cache).  Architektur/Status: `doc/refactoring_floppy_emulator.md` (§3–§8, §15).

---

## 1. Aufgabe

Die `FloppyDrive`-Klasse emuliert ein physisches Diskettenlaufwerk als Abstraktion über eine `.img`-Datei. Sie ist kein K1520-Bus-Gerät, sondern wird von der K5122-Karte verwendet.

---

## 2. Disk-Format-Definition

Das Format einer Diskette beschreibt die Sektorgeometrie pro Spur und Kopf. Es kann spurabhängig variieren (z.B. cpa780 hat andere Boot-Spuren).

```cpp
struct SectorInfo {
    uint8_t  id;        // Sektor-ID (1-basiert)
    uint16_t size;      // Bytes pro Sektor (128, 256, 512, 1024)
};

struct TrackFormat {
    int head_first;     // Erster Kopf (0 oder 1)
    int head_last;      // Letzter Kopf
    int cyl_first;      // Erste Spur
    int cyl_last;       // Letzte Spur
    int secs_per_track; // Sektoren pro Spur
    int bytes_per_sec;  // Bytes pro Sektor
};

struct DiskFormat {
    std::string name;          // z.B. "cpa780"
    int         num_cyls;      // Anzahl Spuren (z.B. 80)
    int         num_heads;     // Anzahl Köpfe (1 oder 2)
    std::vector<TrackFormat> tracks;  // Spur-Bereiche mit Geometrie

    // Byte-Offset eines Sektors im .img-File
    size_t sectorOffset(int cyl, int head, int sector_id) const;

    // Gesamtgröße in Bytes
    size_t totalSize() const;
};
```

### 2.1 Bekannte Formate (aus cpaFormates.cfg)

| Format | Spuren | Köpfe | Bootspuren | Bootsektor | Datensektor |
|--------|--------|-------|-----------|------------|-------------|
| cpa624 | 80 | 2 | keine (alle gleich) | — | 16×256B |
| cpa640 | 80 | 2 | keine | — | 16×256B |
| cpa780 | 80 | 2 | 0-1.K0, 0.K1 | 26×128B | 5×1024B |
| cpa800 | 80 | 2 | keine | — | 5×1024B |
| cpa200 | 40 | 1 | keine | — | 5×1024B |
| cpa200_boot | 40 | 1 | 0-1 | 26×128B | 5×1024B |
| scpx780 | 80 | 2 | keine | — | 5×1024B |
| scpx780_b | 80 | 2 | keine | — | 10×512B |

---

## 3. Format-Parser

```cpp
class FormatParser {
public:
    // Liest eine .cfg-Datei und gibt alle definierten Formate zurück
    static std::vector<DiskFormat> parseFile(const std::string& path);

    // Parst einen einzelnen Format-Block aus einem String
    static DiskFormat parseFormat(const std::string& name,
                                   const std::string& block);

    // Gibt alle eingebauten Formate zurück (ohne externe Datei)
    static const std::vector<DiskFormat>& builtinFormats();

private:
    static void parseTrackRange(const std::string& line, DiskFormat& fmt);
};
```

**Eingebaute Formate:** Die Standardformate (cpa780, cpa800, etc.) sind im Code eingebaut und funktionieren ohne externe Konfigurationsdatei. Externe `.cfg`-Dateien können zusätzliche Formate definieren.

---

## 4. FloppyDrive-Klasse

```cpp
class FloppyDrive {
public:
    FloppyDrive() = default;

    // ─── Montieren/Auswerfen ──────────────────────────────────────

    bool    mount(const std::string& imagePath,
                  const DiskFormat& format,
                  bool writeProtect = false);
    void    unmount();
    bool    isMounted() const;

    // ─── Disk-Eigenschaften ───────────────────────────────────────

    const DiskFormat& format() const;
    bool    isWriteProtected() const;
    void    setWriteProtect(bool wp);
    bool    isTrack0() const;        // Aktueller Kopf auf Spur 0?

    // ─── Kopf-Positionierung ──────────────────────────────────────

    void    seekTrack(int track);    // Absolute Position
    void    step(bool inward);       // Ein Schritt +/- Spur
    int     currentTrack() const;

    // ─── Sektoroperationen ────────────────────────────────────────

    // Liest einen physikalischen Sektor
    // cyl: Spur, head: Kopf (0/1), sector_id: Sektor-ID (1-basiert)
    std::vector<uint8_t> readSector(int cyl, int head, int sector_id);

    // Schreibt einen physikalischen Sektor
    bool writeSector(int cyl, int head, int sector_id,
                     const std::vector<uint8_t>& data);

    // ─── Image-Verwaltung ─────────────────────────────────────────

    // Erstellt ein neues leeres Image (E5H-gefüllt)
    static bool createBlankImage(const std::string& path,
                                  const DiskFormat& format);

    // Speichert Änderungen (für Nicht-Read-Only-Images)
    bool    flush();

    // ─── Aktivitätsanzeige ────────────────────────────────────────
    bool    isActive() const;        // Laufwerk wird gerade verwendet
    void    setActive(bool active);

private:
    DiskFormat              format_;
    std::vector<uint8_t>    image_;         // Im Speicher gehaltenes Image
    std::string             image_path_;
    bool                    mounted_       = false;
    bool                    write_protect_ = false;
    int                     current_track_ = 0;
    bool                    active_        = false;

    size_t  calcOffset(int cyl, int head, int sector_id) const;
    bool    validateSector(int cyl, int head, int sector_id) const;
};
```

---

## 5. Sektor-Offset-Berechnung

```cpp
size_t FloppyDrive::calcOffset(int cyl, int head, int sector_id) const {
    size_t offset = 0;

    // Summiere alle Spuren vor der gesuchten
    for (const auto& tr : format_.tracks) {
        if (cyl > tr.cyl_last ||
            (cyl == tr.cyl_first && head < tr.head_first))
            continue;

        // Prüfe ob diese TrackFormat-Regel zutrifft
        if (cyl >= tr.cyl_first && cyl <= tr.cyl_last &&
            head >= tr.head_first && head <= tr.head_last) {

            // Offset innerhalb dieser Spur-Gruppe
            int cyls_before = (cyl - tr.cyl_first) * (tr.head_last - tr.head_first + 1);
            int heads_before = head - tr.head_first;
            int track_idx = cyls_before + heads_before;

            offset += track_idx * tr.secs_per_track * tr.bytes_per_sec;
            offset += (sector_id - 1) * tr.bytes_per_sec;
            return offset;
        } else {
            // Komplette Spur-Gruppe addieren
            int num_tracks = (tr.cyl_last - tr.cyl_first + 1) *
                             (tr.head_last - tr.head_first + 1);
            offset += num_tracks * tr.secs_per_track * tr.bytes_per_sec;
        }
    }
    return offset;
}
```

---

## 6. Disk-Format-Validierung

Beim Mounten wird geprüft, ob die Image-Datei zur angegebenen Geometrie passt:

```cpp
bool FloppyDrive::mount(const std::string& path,
                         const DiskFormat& format, bool wp) {
    size_t expected_size = format.totalSize();
    auto file_size = std::filesystem::file_size(path);

    if (file_size != expected_size) {
        // Warnung: Image-Größe passt nicht exakt
        // Trotzdem laden (manche Images haben Toleranz)
        if (file_size < expected_size * 0.9) {
            return false;  // Zu klein, abbrechen
        }
    }
    // Image laden...
}
```

---

## 7. C-API-Integration

```c
// Via C-API (k1520_api.h):
bool k1520_mount_disk(K1520Handle h, int drive,
                       const char* image_path,
                       const char* format_name);

// Implementierung:
bool k1520_mount_disk(K1520Handle h, int drive, ...) {
    auto& machine = *static_cast<Machine*>(h);
    const auto& fmt = FormatParser::builtinFormats()  // oder aus .cfg
        | find(format_name);
    auto floppy = std::make_unique<FloppyDrive>();
    floppy->mount(image_path, fmt);
    machine.k5122().mountDrive(drive, floppy.get());
    machine.storeDrive(drive, std::move(floppy));
    return true;
}
```

---

## 8. Testbarkeit

```python
# tests/python/test_floppy_drive.py
import ctypes, os, tempfile, pytest

def test_mount_cpa780(floppy_lib, cpa780_image):
    drv = floppy_lib.floppy_create()
    assert floppy_lib.floppy_mount(drv, cpa780_image, "cpa780")
    assert floppy_lib.floppy_is_mounted(drv)
    assert floppy_lib.floppy_track0(drv)  # Nach Reset: Spur 0

def test_read_boot_sector(floppy_lib, cpa780_image):
    drv = floppy_lib.floppy_create()
    floppy_lib.floppy_mount(drv, cpa780_image, "cpa780")
    # Spur 0, Kopf 0, Sektor 1 (Bootsektor), 128 Bytes
    data = floppy_lib.floppy_read_sector(drv, 0, 0, 1)
    assert len(data) == 128
    assert data[0] != 0xFF  # Nicht leer

def test_seek_step(floppy_lib, cpa780_image):
    drv = floppy_lib.floppy_create()
    floppy_lib.floppy_mount(drv, cpa780_image, "cpa780")
    floppy_lib.floppy_step(drv, True)   # nach innen
    assert floppy_lib.floppy_current_track(drv) == 1
    floppy_lib.floppy_step(drv, False)  # nach außen
    assert floppy_lib.floppy_current_track(drv) == 0

def test_write_read_roundtrip(floppy_lib, tmp_path):
    # Neues leeres Image erstellen
    img = str(tmp_path / "test.img")
    assert floppy_lib.floppy_create_blank(img, "cpa800")
    drv = floppy_lib.floppy_create()
    floppy_lib.floppy_mount(drv, img, "cpa800")
    # Schreiben und Lesen vergleichen
    test_data = bytes(range(256)) * 4  # 1024 Bytes
    floppy_lib.floppy_write_sector(drv, 2, 0, 1, test_data)
    read_back = floppy_lib.floppy_read_sector(drv, 2, 0, 1)
    assert read_back == test_data
```

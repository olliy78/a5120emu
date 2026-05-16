# K1520 Emulator – Offene Punkte und Fragen

**Aktualisiert:** 16. Mai 2026  
**Status:** Systemische Fragen vor Implementierungs-Abschluss

---

## 1. KRITISCHE ARCHITEKTUR-FRAGEN

### 1.1 Koppelbus-Verdrahtung (A5120)

**Problem:** Die genaue Verdrahtung der Wickelbrücken in der A5120-Backplane ist für korrekte Interrupt- und Signal-Routing essenziell.

**Benötigte Informationen:**

1. **Interrupt-Prioritätskette (Daisy-Chain /IEI–/IEO):**
   - Welche Karte hat die höchste Priorität?
   - Exakte Reihenfolge: K2526 → K8025 → K5122 → K7024 oder anders?

2. **Zweite Interrupt-Kette (/IEI1–/IEO1 via Koppelbus):**
   - Welche Karten sind in dieser sekundären Kette?
   - Dient sie der BS-PIO (U 855D) Prioritätssteuerung?

3. **CTC-Signale (CLK/TRG, ZC/TO Verbindungen):**
   - Welche `ZC/TO`-Ausgänge des K2526 CTC (U 313) gehen zu welchen `CLK/TRG`-Eingängen anderer CTCs?
   - Ist der Bustakt (2.4576 MHz) als `CLK` eingebunden oder muss er durch CTC geteilt werden?

4. **Speicherbereichsumschaltung (/MEMDI, /MEMDI1, /MEMDI2):**
   - Sind `MEMDI1` und `MEMDI2` signifikant oder nur `MEMDI`?
   - Wird Speicher via DMA oder Zubehör umgeschaltet?

5. **Sonderausgänge (/SA, /SUE):**
   - `/SA` (Sonderausgang): Netzschalter-Schaltung?
   - `/SUE` (Spannungsüberwachung): Nur Monitoring oder triggert NMI?

**Lösung:** Beschaffung eines Original-Backplane-Schaltplans oder technischer Verdrahtungsplan.

---

### 1.2 K8025 – SIO-Kanal-Belegung

**Problem:** Welcher SIO-Chip und Kanal (A oder B) ist mit der K7637 Tastatur verbunden?

**Spezifikation sagt:**
- K8025 enthält zwei SIO-Chips: A33 und A32
- A33: Ports 50H–53H, Kanäle A und B
- A32: Ports 5CH–5FH, Kanäle A und B
- Eines ist "DFÜ" (V.24), eines ist "Drucker" (IFSS)

**Frage:**
- Ist K7637-Tastatur (IFSS-Protokoll) mit SIO A33 **Kanal A** oder **Kanal B** verbunden?
- Was ist dann **A33 Kanal B** oder **A32 Kanal B**?

**Lösung:** Schaltplan K8025 oder cparun-Quellcode analysieren.

---

### 1.3 K3526 OPS – Passive DRAM oder mit Logik?

**Problem:** Die K3526 ist spezifiziert als "64 KB DRAM". Hat sie nur passive Speicher-Funktion oder sind Kontrollregister vorhanden?

**Fragen:**
- Gibt es Banking-Register (z.B. für Erweiterung auf 128 KB oder mehr)?
- Ist es möglich, Speicherbereiche schreibgeschützt zu markieren?
- Sind DIP-Schalter vorhanden (z.B. zur Adressumschaltung)?

**Lösung:** Technische Dokumentation K3526 beschaffen.

---

### 1.4 CPU-Speed und Taktverbindungen

**Problem:** Der K1520 verwendet einen externen 2.4576 MHz Taktoszillator. Wie wird dieser verteilt?

**Fragen:**
- Kommt der TAKT direkt vom Oszillator auf jeden Slot, oder via K2526?
- Haben die Karten Takt-In und Takt-Out (zur Verteilung)?
- Wird der Takt irgendwo geteilt (z.B. im CTC)?

**Lösung:** Analyse K2526-Schaltplan.

---

## 2. HARDWARE-SPEZIFIKATION – LÜCKEN

### 2.1 K7637 Tastatur – Scan-Code-Tabellen

**Status:** Drei Scan-Code-Tabellen sind dokumentiert, aber nicht vollständig verifiziert.

**Benötigt:**
- Vergleich mit tatsächlichem Keyboard-EPROM
- Überprüfung der Shift/Ctrl-Modifier-Kombinationen
- Kyrillische vs. lateinische Zeichen

### 2.2 Diskettenformat-Konfiguration

**Status:** Das Format `cpaFormates.cfg` ist teilweise spezifiziert.

**Benötigt:**
- Vollständige Dokumentation aller Standard-Formate (CPA780, CPA800, SCP, UDOS, MUTOS)
- Beispiel-Disk-Images für Tests

### 2.3 K2526 ROM (Lade-ROM)

**Status:** 1 KB Lade-ROM ist in `doc/EPROMS/` nicht vorhanden.

**Benötigt:**
- Binäre oder Assembler-Quellen des Lade-ROMs
- Oder: Dokumentation der ROM-Bootsequenz

---

## 3. IMPLEMENTIERUNGS-LÜCKEN

### 3.1 Z80CPU – Fehlende Anpassungen

**Status:** Wird aus `src/z80.cpp` adaptiert.

**Fragen:**
- Sind alle Z80-Instruktionen korrekt implementiert?
- Ist der HALT-Zustand richtig (CPU pausiert bis RESET/INT/NMI)?
- Werden alle Interrupt-Modi (0, 1, 2) unterstützt?

### 3.2 Logging und Debug-System

**Status:** Nicht vorhanden.

**Benötigt:**
- Compiler-Flag zum Ein-/Ausschalten (`-DENABLE_LOGGING`)
- Log-Level-Skalierung (OFF, ERROR, WARN, INFO, DEBUG, TRACE)
- Logs ohne Overhead bei Deaktivierung
- Ausgabe auf stderr oder in Datei

### 3.3 Integrationstests

**Status:** Unit-Tests vorhanden, aber Integrationstests fehlen.

**Benötigt:**
- Vollständiger Boot-Test (A5120 startet, zugreift auf Floppy)
- Tastatur-Input-Test
- Bildschirm-Output-Test
- Diskettenformat-Parser-Tests

### 3.4 Python-GUI (PySide6)

**Status:** Nicht angefangen.

**Benötigt:**
- `app/main.py` – Haupteinstiegspunkt
- `app/core_binding/k1520.py` – ctypes-Wrapper
- `app/ui/main_window.py` – Hauptfenster
- `app/ui/screen_widget.py` – Framebuffer-Anzeige
- `app/ui/drive_widget.py` – Diskettenlaufwerk-Dialog
- `app/config/machines/a5120.json` – Konfiguration
- `app/requirements.txt` – Abhängigkeiten

### 3.5 CLI-Tool (k1520cli)

**Status:** Nicht angefangen.

**Benötigt:**
- `cli/k1520cli.cpp` – Standalone-Emulator ohne Python
- Kommandozeilen-Argument-Verarbeitung
- Console-I/O (Text-basierter Bildschirm)

---

## 4. DOKUMENTATION – ERFORDERLICHE ERGÄNZUNGEN

### 4.1 Code-Kommentare

**Status:** Bestehender Code ist weitestgehend ohne Doxygen/Google-Style-Kommentare.

**Zu tun:**
- [ ] Alle `.h`-Dateien: Doxygen-Header-Kommentar
- [ ] Alle Funktionen: `///` Doxygen-Kommentare
- [ ] Alle Strukturen/Klassen: Beschreibung des Zwecks
- [ ] Parameter und Rückgabewerte dokumentieren
- [ ] Python-Code: Google-Style Docstrings

### 4.2 Fehlende Design-Dokumente

**Status:** Gerüst existiert, aber nicht alle sind ausgefüllt.

**Zu überprüfen:**
- `03_k2526_zre.md` – vollständig?
- `04_k3526_ops.md` – vollständig?
- `06_k8025_ass.md` – SIO-Kanal-Zuordnung fehlt
- `07_k5122_afs.md` – Protokoll-Details?
- `08_k7637_keyboard.md` – Scan-Code-Tabellen aktuell?

---

## 5. BUILD- UND TEST-SYSTEM

### 5.1 CMakeLists.txt für neuen Kern

**Status:** Nicht vollständig vorhanden.

**Benötigt:**
- `core/CMakeLists.txt` – Defines libk1520core.so
- `core/api/CMakeLists.txt` – C-API
- `core/primitives/CMakeLists.txt` – Primitive
- `core/cards/CMakeLists.txt` – alle Karten
- `core/machines/CMakeLists.txt` – Maschineninstanzen
- `tests/cpp/CMakeLists.txt` – GoogleTest-Integration
- Root `CMakeLists.txt` – Alles zusammenbinden

### 5.2 Python Test-Struktur

**Status:** Nicht vorhanden.

**Benötigt:**
- `tests/python/conftest.py` – pytest-Fixtures
- `tests/python/test_*.py` – alle Integration-Tests

---

## 6. DEPLOYMENT

### 6.1 Packaging

**Status:** Nicht definiert.

**Fragen:**
- Soll `libk1520core.so` systemweit installiert oder in `app/lib/` lokal abgelegt werden?
- Wie soll `app/` als pip-Paket installiert werden?

### 6.2 Betriebssysteme

**Status:** Nur Linux spezifiziert.

**Frage:**
- macOS-Unterstützung gewünscht?
- Windows-Unterstützung (via MSYS2/MinGW) gewünscht?

---

## 7. LEISTUNGS-ANFORDERUNGEN

### 7.1 Zyklus-Genauigkeit

**Problem:** Der Emulator ist transaktionsgenau, nicht zyklus-genau. Ausreichend?

**Frage:**
- Können kritische Sequenzen (z.B. Floppy-Markenerkennung) nur funktional emuliert werden, oder ist exakte Timing nötig?

### 7.2 Echtzeit-Ausführung

**Problem:** Soll sich der Emulator an 2.4576 MHz orientieren (Real-Time), oder schneller laufen?

**Frage:**
- In der Python-GUI: synchronisieren mit Frame-Rate (60 Hz)?
- Oder: vollständige Geschwindigkeit (so schnell wie Host-CPU)?

---

## 8. KÜNFTIGE ERWEITERUNGEN (nicht für MVP)

Diese sollten in der Architektur vorbereitet, aber nicht implementiert sein:

1. **PRG710 / K8915** – weitere Maschinentypen
2. **Speicherexpansion** – Banking-Register
3. **Drucker-Emulation** – Ausgabe-Protokoll
4. **Netzwerk-Emulation** – IFSS über TCP/IP
5. **Disketten-Editor** – Dateiauswahl und Disk-Utils
6. **Assembler-Integration** – Inline-Debugging, Breakpoints

---

## 9. RISIKEN UND ANNAHMEN

| Risiko | Wahrscheinlichkeit | Mitigation |
|--------|-------------------|-----------|
| Koppelbus-Verdrahtung falsch | Mittel | Schaltplan beschaffen, Tests anpassen |
| Z80-Instruktionen unvollständig | Gering | Bestehende Tests aus `src/tests/` verwenden |
| Performance zu langsam | Gering | Profiling nach MVP durchführen |
| K7637 Scan-Codes falsch | Mittel | Boot-Test mit realer CPA zeigen, welche Tasten fehlen |

---

## 10. NÄCHSTE SCHRITTE (für dich)

**Bitte antworte auf folgende Fragen:**

1. **Koppelbus-Verdrahtung:** Kannst du einen A5120-Backplane-Schaltplan bereitstellen?
   - Falls nein: Soll ich eine "Best-Guess"-Verdrahtung basierend auf cparun implementieren?

2. **K8025 SIO-Belegung:** Welcher Kanal ist mit K7637 verbunden?
   - Falls unsicher: Kann ich aus `cparun` reverse-engineeren?

3. **K3526 OPS:** Ist es nur 64 KB DRAM oder gibt es Kontrolllogik?
   - Falls nur DRAM: Dann ist die Implementierung trivial.

4. **Logging-System:**
   - Soll Output auf stderr gehen oder in `~/.k1520emu/logs/` ?
   - Welche Log-Level brauchst du? (OFF, ERROR, WARN, INFO, DEBUG, TRACE)

5. **Disk-Images:**
   - Sind Test-Images (`cpa780_boot.img`, `disk_b.img`) vorhanden?
   - Wo sollten sie im Repo liegen?

6. **Python GUI:**
   - Nur Qt6 / PySide6, oder auch Unterstützung für tkinter?
   - Minimale Features für MVP oder auch Konfigurationsdialog?

7. **Performance:**
   - Real-Time (2.4576 MHz) oder schnellste Simulation?

---

**Sobald diese Fragen geklärt sind, kann die Implementierung ohne Blockers fortfahren.**

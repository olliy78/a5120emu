/**
 * @file cpm_bdos.cpp
 * @brief CP/M 2.2 BDOS-Emulation mit direktem Host-Dateisystem-Zugriff.
 *
 * Implementiert die BDOS-Funktionen fuer cparun. Alle Dateioperationen
 * werden direkt auf das Host-Dateisystem abgebildet:
 * - FCB-Dateinamen (8+3 Grossbuchstaben) → Host-Dateinamen (Kleinbuchstaben)
 * - Case-insensitive Dateisuche im Arbeitsverzeichnis
 * - Sequentielle und Random-Zugriffe ueber stdio FILE-Handles
 *
 * @author Olaf Krieger
 * @license MIT License
 */
#include "cpm_bdos.h"
#include <cstring>
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>

// =============================================================================
// Konstruktor / Destruktor
// =============================================================================

CpmBdos::CpmBdos(Z80& cpu, Memory& mem, const std::string& workDir)
    : cpu_(cpu), mem_(mem), workDir_(workDir),
      dmaAddr_(0x0080), currentDrive_(0), currentUser_(0),
      running_(false), searchIndex_(0) {}

CpmBdos::~CpmBdos() {
    for (auto& [addr, of] : openFiles_) {
        if (of.fp) fclose(of.fp);
    }
}

// =============================================================================
// Setup und Laden
// =============================================================================

/**
 * @brief Richtet Page Zero und Trap-Adressen ein.
 *
 * Schreibt die Sprungvektoren und HALT-Opcodes in den Z80-Speicher:
 * - 0x0000: JP WBOOT_TRAP (Warmstart)
 * - 0x0005: JP BDOS_TRAP  (BDOS-Einsprung)
 * - 0xF000: HALT (BDOS-Trap)
 * - 0xF003: HALT (Warmstart-Trap)
 *
 * Programme lesen Bytes 0x0006-0x0007 als TPA-Obergrenze (0xF000),
 * was ca. 60 KB nutzbaren Speicher ergibt.
 */
void CpmBdos::setup() {
    mem_.clear();

    // Warm boot vector: JP WBOOT_TRAP
    mem_.write(0x0000, 0xC3);
    mem_.write(0x0001, WBOOT_TRAP & 0xFF);
    mem_.write(0x0002, (WBOOT_TRAP >> 8) & 0xFF);

    // IOBYTE and current disk
    mem_.write(0x0003, 0x00);
    mem_.write(0x0004, 0x00);

    // BDOS vector: JP BDOS_TRAP
    mem_.write(0x0005, 0xC3);
    mem_.write(0x0006, BDOS_TRAP & 0xFF);
    mem_.write(0x0007, (BDOS_TRAP >> 8) & 0xFF);

    // HALT traps
    mem_.write(BDOS_TRAP, 0x76);      // HALT
    mem_.write(BDOS_TRAP + 1, 0x00);  // NOP
    mem_.write(BDOS_TRAP + 2, 0x00);  // NOP
    mem_.write(WBOOT_TRAP, 0x76);     // HALT
}

/**
 * @brief Laedt eine .com-Datei in den Z80-Speicher ab 0x0100.
 *
 * Sucht die Datei case-insensitiv im Arbeitsverzeichnis.
 * Haengt ".com" an, falls nicht vorhanden. Initialisiert die CPU-Register
 * und legt die Warmstart-Ruecksprungadresse (0x0000) auf den Stack.
 *
 * @param name Programmname (z.B. "m80" oder "m80.com")
 * @return true bei Erfolg
 */
bool CpmBdos::loadCom(const std::string& name) {
    std::string comName = name;
    if (comName.size() < 4 ||
        strcasecmp(comName.c_str() + comName.size() - 4, ".com") != 0) {
        comName += ".com";
    }

    std::string found = findFileCI(comName);
    if (found.empty()) return false;

    std::string path = hostPath(found);
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0 || size > (BDOS_TRAP - 0x0100)) {
        fclose(fp);
        return false;
    }

    std::vector<uint8_t> buf(static_cast<size_t>(size));
    size_t nread = fread(buf.data(), 1, buf.size(), fp);
    fclose(fp);

    mem_.load(0x0100, buf.data(), nread);

    // Set up CPU
    cpu_.reset();
    cpu_.PC = 0x0100;
    cpu_.SP = BDOS_TRAP;

    // Push return address 0x0000 so RET goes to warm boot
    cpu_.SP -= 2;
    mem_.write(cpu_.SP, 0x00);
    mem_.write(cpu_.SP + 1, 0x00);

    return true;
}

// =============================================================================
// Kommandozeilen-Parsing
// =============================================================================

/**
 * @brief Parst einen Dateinamen-Token in einen FCB an der gegebenen Adresse.
 *
 * Format: [d:]filename[.ext]
 * - Optionaler Laufwerksbuchstabe (A:-P:)
 * - Dateiname (1-8 Zeichen, space-padded)
 * - Extension (0-3 Zeichen, space-padded)
 * - Alles in Grossbuchstaben konvertiert
 */
void CpmBdos::parseFcb(const std::string& token, uint16_t addr) {
    uint8_t fcb[36] = {};
    memset(fcb + 1, ' ', 11);  // Filename + ext = spaces

    const char* p = token.c_str();

    // Check for drive prefix
    if (token.size() >= 2 && p[1] == ':') {
        char d = static_cast<char>(toupper(p[0]));
        if (d >= 'A' && d <= 'P') {
            fcb[0] = static_cast<uint8_t>(d - 'A' + 1);
        }
        p += 2;
    }

    // Parse filename (up to 8 chars before '.')
    int i = 0;
    while (*p && *p != '.' && i < 8) {
        fcb[1 + i++] = static_cast<uint8_t>(toupper(*p++));
    }

    // Skip to extension
    if (*p == '.') p++;

    // Parse extension (up to 3 chars)
    i = 0;
    while (*p && i < 3) {
        fcb[9 + i++] = static_cast<uint8_t>(toupper(*p++));
    }

    // Write FCB to memory
    for (int j = 0; j < 36; j++) {
        mem_.write(addr + j, fcb[j]);
    }
}

/**
 * @brief Parst die Kommandozeile in FCB1, FCB2 und Kommando-Tail.
 *
 * Die Argumente werden in Grossbuchstaben konvertiert (CP/M-Konvention).
 * Der Kommando-Tail wird im CP/M-Format abgelegt:
 * - 0x0080: Laengenbyte (inkl. fuehrendes Leerzeichen)
 * - 0x0081+: " ARGS..." (Grossbuchstaben, max. 127 Bytes)
 * - Abschluss: 0x0D (CR)
 *
 * FCB1 (0x005C) und FCB2 (0x006C) werden mit den ersten beiden
 * Dateiname-Tokens gefuellt (Trennung an Leerzeichen).
 */
void CpmBdos::parseCommandLine(const std::string& args) {
    // Convert to uppercase
    std::string upper;
    for (char c : args) upper += static_cast<char>(toupper(c));

    // --- FCBs first (before command tail, since FCB2 at 0x006C overlaps 0x0080) ---

    // Initialize FCBs with empty entries
    parseFcb("", 0x005C);
    parseFcb("", 0x006C);

    // Extract first two tokens for FCB1 and FCB2
    // Delimiters: space, comma, semicolon, equals, brackets
    auto isDelim = [](char c) {
        return c == ' ' || c == ',' || c == ';' || c == '=' ||
               c == '[' || c == ']' || c == '<' || c == '>';
    };

    const char* p = upper.c_str();
    // Skip leading delimiters
    while (*p && isDelim(*p)) p++;

    // First token → FCB1
    if (*p) {
        std::string tok;
        while (*p && !isDelim(*p)) tok += *p++;
        parseFcb(tok, 0x005C);
    }

    // Skip delimiters
    while (*p && isDelim(*p)) p++;

    // Second token → FCB2
    if (*p) {
        std::string tok;
        while (*p && !isDelim(*p)) tok += *p++;
        parseFcb(tok, 0x006C);
    }

    // --- Command tail LAST (at 0x0080, must be after FCB2 which overlaps) ---

    // Build command tail: leading space + args
    std::string tail = " " + upper;
    if (tail.size() > 127) tail.resize(127);

    mem_.write(0x0080, static_cast<uint8_t>(tail.size()));
    for (size_t i = 0; i < tail.size(); i++) {
        mem_.write(0x0081 + static_cast<uint16_t>(i), static_cast<uint8_t>(tail[i]));
    }
    mem_.write(0x0081 + static_cast<uint16_t>(tail.size()), 0x0D);


}

// =============================================================================
// FCB / Dateinamen-Hilfsfunktionen
// =============================================================================

/**
 * @brief Liest einen FCB aus dem Z80-Speicher und gibt den Dateinamen zurueck.
 *
 * Konvertiert das CP/M 8+3 Format in einen Host-Dateinamen:
 * "BIOS    MAC" → "bios.mac"
 *
 * @param addr FCB-Adresse im Z80-Speicher
 * @return Dateiname in Kleinbuchstaben (z.B. "bios.mac")
 */
std::string CpmBdos::fcbToName(uint16_t addr) {
    char name[13];
    int len = 0;

    // Filename (bytes 1-8), strip trailing spaces, ignore high bit
    for (int i = 0; i < 8; i++) {
        char c = static_cast<char>(mem_.read(addr + 1 + i) & 0x7F);
        if (c != ' ') name[len++] = static_cast<char>(tolower(c));
    }

    // Extension (bytes 9-11), strip trailing spaces, ignore high bit
    char ext[4];
    int extLen = 0;
    for (int i = 0; i < 3; i++) {
        char c = static_cast<char>(mem_.read(addr + 9 + i) & 0x7F);
        if (c != ' ') ext[extLen++] = static_cast<char>(tolower(c));
    }

    if (extLen > 0) {
        name[len++] = '.';
        for (int i = 0; i < extLen; i++) name[len++] = ext[i];
    }
    name[len] = '\0';
    return std::string(name);
}

/**
 * @brief Sucht eine Datei case-insensitiv im Arbeitsverzeichnis.
 *
 * @param name Gesuchter Dateiname
 * @return Tatsaechlicher Dateiname auf dem Host, oder leer wenn nicht gefunden
 */
std::string CpmBdos::findFileCI(const std::string& name) {
    DIR* dir = opendir(workDir_.c_str());
    if (!dir) return "";
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcasecmp(ent->d_name, name.c_str()) == 0) {
            std::string result = ent->d_name;
            closedir(dir);
            return result;
        }
    }
    closedir(dir);
    return "";
}

/**
 * @brief Kombiniert Arbeitsverzeichnis und Dateiname zu einem Host-Pfad.
 */
std::string CpmBdos::hostPath(const std::string& name) {
    if (workDir_.empty() || workDir_ == ".") return name;
    return workDir_ + "/" + name;
}

/**
 * @brief Konvertiert einen Host-Dateinamen in FCB-Format (11 Bytes).
 *
 * @param name Host-Dateiname (z.B. "bios.mac")
 * @param fcb  Zielpuffer, mindestens 12 Bytes (Byte 0 = User, 1-11 = Name+Ext)
 */
void CpmBdos::nameToFcb(const std::string& name, uint8_t* fcb) {
    memset(fcb, ' ', 12);
    fcb[0] = 0; // User 0

    size_t dot = name.rfind('.');
    std::string base = (dot != std::string::npos) ? name.substr(0, dot) : name;
    std::string ext  = (dot != std::string::npos) ? name.substr(dot + 1) : "";

    for (size_t i = 0; i < 8 && i < base.size(); i++)
        fcb[1 + i] = static_cast<uint8_t>(toupper(base[i]));
    for (size_t i = 0; i < 3 && i < ext.size(); i++)
        fcb[9 + i] = static_cast<uint8_t>(toupper(ext[i]));
}

/**
 * @brief Vergleicht ein FCB-Muster (mit '?' als Wildcard) mit einem FCB-Namen.
 *
 * @param pat  11-Byte FCB-Muster (Bytes 1-11 des FCBs)
 * @param name 11-Byte FCB-Name
 * @return true wenn Muster passt
 */
bool CpmBdos::matchPattern(const uint8_t* pat, const uint8_t* name) {
    for (int i = 0; i < 11; i++) {
        uint8_t p = pat[i] & 0x7F;
        uint8_t n = name[i] & 0x7F;
        if (p != '?' && p != n) return false;
    }
    return true;
}

// =============================================================================
// BDOS-Dispatch
// =============================================================================

/**
 * @brief Dispatcht den BDOS-Aufruf anhand der Funktionsnummer in Register C.
 *
 * @return false wenn das Programm beendet werden soll (Funktion 0 oder Warmstart)
 */
bool CpmBdos::dispatch() {
    uint8_t func = cpu_.C;
    uint16_t de = cpu_.DE;



    switch (func) {
        case 0: // P_TERMCPM - System Reset
            running_ = false;
            return false;

        case 1: { // C_READ - Console Input (with echo)
            int ch = fgetc(stdin);
            if (ch == EOF) {
                // stdin exhausted - terminate program
                running_ = false;
                return false;
            }
            uint8_t result = static_cast<uint8_t>(ch);
            // Echo character (CP/M convention)
            if (result >= 0x20 || result == '\r' || result == '\n' || result == '\t') {
                fputc(result, stdout);
                if (result == '\r') fputc('\n', stdout);
                fflush(stdout);
            }
            cpu_.A = result;
            cpu_.L = result;
            break;
        }

        case 2: // C_WRITE - Console Output
            fputc(cpu_.E, stdout);
            fflush(stdout);
            break;

        case 6: // C_RAWIO - Direct Console I/O
            if (cpu_.E == 0xFF) {
                // Input: return 0 (no char available) for batch usage
                cpu_.A = 0;
                cpu_.L = 0;
            } else if (cpu_.E == 0xFE) {
                // Status: return 0 (no char available)
                cpu_.A = 0;
                cpu_.L = 0;
            } else {
                fputc(cpu_.E, stdout);
                fflush(stdout);
            }
            break;

        case 9: { // C_WRITESTR - Print String until '$'
            uint16_t addr = de;
            while (true) {
                uint8_t ch = mem_.read(addr++);
                if (ch == '$') break;
                fputc(ch, stdout);
            }
            fflush(stdout);
            break;
        }

        case 10: { // C_READSTR - Read Console Buffer
            uint8_t maxLen = mem_.read(de);
            if (maxLen == 0) break;
            char buf[256];
            if (fgets(buf, maxLen + 1, stdin) == nullptr) {
                // stdin exhausted (EOF) - terminate program
                running_ = false;
                return false;
            }
            // Remove trailing newline
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
            if (len > maxLen) len = maxLen;
            mem_.write(de + 1, static_cast<uint8_t>(len));
            for (size_t i = 0; i < len; i++) {
                mem_.write(de + 2 + static_cast<uint16_t>(i),
                           static_cast<uint8_t>(toupper(buf[i])));
            }
            break;
        }

        case 11: // C_STAT - Console Status
            cpu_.A = 0x00; // No key available (batch mode)
            cpu_.HL = 0;
            break;

        case 12: // S_BDOSVER - Return Version Number
            cpu_.H = 0x00;  // System type: CP/M
            cpu_.L = 0x22;  // Version 2.2
            cpu_.B = cpu_.H;
            cpu_.A = cpu_.L;
            break;

        case 13: // DRV_ALLRESET - Reset Disk System
            dmaAddr_ = 0x0080;
            currentDrive_ = 0;
            break;

        case 14: // DRV_SET - Select Disk
            currentDrive_ = cpu_.E;
            break;

        case 15: { // F_OPEN - Open File
            uint8_t result = bdosOpenFile();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 16: { // F_CLOSE - Close File
            uint8_t result = bdosCloseFile();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 17: { // F_SFIRST - Search For First
            uint8_t result = bdosSearchFirst();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 18: { // F_SNEXT - Search For Next
            uint8_t result = bdosSearchNext();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 19: { // F_DELETE - Delete File
            uint8_t result = bdosDeleteFile();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 20: { // F_READ - Read Sequential
            uint8_t result = bdosReadSeq();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 21: { // F_WRITE - Write Sequential
            uint8_t result = bdosWriteSeq();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 22: { // F_MAKE - Make File
            uint8_t result = bdosMakeFile();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 23: { // F_RENAME - Rename File
            uint8_t result = bdosRenameFile();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 24: // DRV_LOGINVEC - Return Login Vector
            cpu_.HL = 0xFFFF; // All drives logged in
            break;

        case 25: // DRV_GET - Return Current Disk
            cpu_.A = static_cast<uint8_t>(currentDrive_);
            cpu_.L = cpu_.A;
            break;

        case 26: // F_DMAOFF - Set DMA Address
            dmaAddr_ = de;
            break;

        case 27: // DRV_ALLOCVEC - Get Alloc Address (dummy)
            cpu_.HL = 0;
            break;

        case 28: // DRV_SETRO - Write Protect Disk (ignore)
            break;

        case 29: // DRV_ROVEC - Get Read-Only Vector
            cpu_.HL = 0; // No drives read-only
            break;

        case 30: // F_ATTRIB - Set File Attributes (ignore)
            cpu_.A = 0;
            break;

        case 31: // DRV_DPB - Get DPB Address (dummy)
            cpu_.HL = 0;
            break;

        case 32: // F_USERNUM - Get/Set User Code
            if (cpu_.E == 0xFF) {
                cpu_.A = static_cast<uint8_t>(currentUser_);
            } else {
                currentUser_ = cpu_.E & 0x0F;
            }
            cpu_.L = cpu_.A;
            break;

        case 33: { // F_READRAND - Read Random
            uint8_t result = bdosReadRand();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 34: { // F_WRITERAND - Write Random
            uint8_t result = bdosWriteRand();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        case 35: // F_SIZE - Compute File Size
            bdosFileSize();
            break;

        case 36: // F_RANDREC - Set Random Record
            bdosSetRandRec();
            break;

        case 37: // DRV_RESET - Reset Drive (ignore)
            cpu_.A = 0;
            break;

        case 40: { // F_WRITEZF - Write Random with Zero Fill
            uint8_t result = bdosWriteRand();
            cpu_.A = result;
            cpu_.H = 0;
            cpu_.L = result;
            break;
        }

        default:
            fprintf(stderr, "cparun: unimplemented BDOS function %d (C=%02Xh DE=%04Xh)\n",
                    func, func, de);
            cpu_.A = 0;
            cpu_.L = 0;
            break;
    }
    return true;
}

// =============================================================================
// Hauptschleife
// =============================================================================

/**
 * @brief Fuehrt das .com-Programm aus.
 *
 * Ohne Timing-Beschraenkung: Instruktionen werden so schnell wie moeglich
 * ausgefuehrt. HALT-Opcodes an den Trap-Adressen loesen BDOS-Aufrufe
 * oder Programmende aus.
 *
 * @return 0 bei normalem Programmende, 1 bei Fehler
 */
int CpmBdos::run() {
    running_ = true;

    while (running_) {
        if (cpu_.halted) {
            uint16_t haltAddr = cpu_.PC - 1;
            cpu_.halted = false;

            if (haltAddr == BDOS_TRAP) {
                if (!dispatch()) return 0;
                // Return from BDOS: pop return address
                cpu_.PC = static_cast<uint16_t>(
                    mem_.read(cpu_.SP) | (mem_.read(cpu_.SP + 1) << 8));
                cpu_.SP += 2;
                continue;
            } else if (haltAddr == WBOOT_TRAP) {
                return 0;
            } else {
                fprintf(stderr, "cparun: unexpected HALT at %04Xh\n", haltAddr);
                return 1;
            }
        }

        cpu_.step();
    }

    return 0;
}

// =============================================================================
// BDOS Dateioperationen
// =============================================================================

/**
 * @brief BDOS 15: Open File.
 *
 * Sucht die Datei case-insensitiv im Arbeitsverzeichnis, oeffnet sie
 * zum Lesen und initialisiert den FCB fuer sequentiellen Zugriff.
 *
 * @return 0 bei Erfolg, 0xFF wenn Datei nicht gefunden
 */
uint8_t CpmBdos::bdosOpenFile() {
    uint16_t fcbAddr = cpu_.DE;
    std::string name = fcbToName(fcbAddr);
    if (name.empty()) return 0xFF;

    std::string found = findFileCI(name);
    if (found.empty()) return 0xFF;

    std::string path = hostPath(found);

    // Close if already open at this FCB address
    auto it = openFiles_.find(fcbAddr);
    if (it != openFiles_.end()) {
        if (it->second.fp) fclose(it->second.fp);
        openFiles_.erase(it);
    }

    FILE* fp = fopen(path.c_str(), "r+b");
    if (!fp) {
        // Try read-only
        fp = fopen(path.c_str(), "rb");
    }
    if (!fp) return 0xFF;

    openFiles_[fcbAddr] = {fp, path};

    // Initialize FCB fields for sequential access
    mem_.write(fcbAddr + 12, 0); // EX = 0
    mem_.write(fcbAddr + 13, 0); // S1 = 0
    mem_.write(fcbAddr + 14, 0); // S2 = 0
    mem_.write(fcbAddr + 32, 0); // CR = 0

    // Set RC = records in first extent (max 128)
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    int totalRecords = static_cast<int>((size + 127) / 128);
    mem_.write(fcbAddr + 15, static_cast<uint8_t>(totalRecords > 128 ? 128 : totalRecords));

    return 0; // Directory code 0 = success
}

/**
 * @brief BDOS 16: Close File.
 *
 * Schliesst das FILE-Handle und entfernt den Eintrag aus der Open-File-Map.
 *
 * @return 0 bei Erfolg, 0xFF wenn Datei nicht geoeffnet war
 */
uint8_t CpmBdos::bdosCloseFile() {
    uint16_t fcbAddr = cpu_.DE;
    auto it = openFiles_.find(fcbAddr);
    if (it == openFiles_.end()) return 0; // Not an error to close unopened
    if (it->second.fp) fclose(it->second.fp);
    openFiles_.erase(it);
    return 0;
}

/**
 * @brief BDOS 17: Search For First.
 *
 * Durchsucht das Arbeitsverzeichnis nach Dateien, die zum FCB-Muster passen.
 * Das Ergebnis wird als CP/M-Directory-Eintrag (32 Bytes) an die aktuelle
 * DMA-Adresse geschrieben.
 *
 * @return 0 bei Treffer (Eintrag an DMA-Offset 0), 0xFF wenn nichts gefunden
 */
uint8_t CpmBdos::bdosSearchFirst() {
    uint16_t fcbAddr = cpu_.DE;
    searchResults_.clear();
    searchIndex_ = 0;

    // Read pattern from FCB (bytes 1-11)
    uint8_t pattern[11];
    for (int i = 0; i < 11; i++) {
        pattern[i] = mem_.read(fcbAddr + 1 + i);
    }

    // Scan directory
    DIR* dir = opendir(workDir_.c_str());
    if (!dir) return 0xFF;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        // Skip . and ..
        if (ent->d_name[0] == '.') continue;

        // Skip directories
        std::string fullPath = hostPath(ent->d_name);
        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;

        // Convert to FCB format and match
        uint8_t fcb[12];
        nameToFcb(ent->d_name, fcb);
        if (matchPattern(pattern, fcb + 1)) {
            searchResults_.push_back(ent->d_name);
        }
    }
    closedir(dir);

    return bdosSearchNext();
}

/**
 * @brief BDOS 18: Search For Next.
 *
 * Gibt den naechsten Treffer aus der gespeicherten Suchergebnisliste zurueck.
 *
 * @return 0 bei Treffer, 0xFF wenn keine weiteren Treffer
 */
uint8_t CpmBdos::bdosSearchNext() {
    if (searchIndex_ >= searchResults_.size()) return 0xFF;

    const std::string& name = searchResults_[searchIndex_++];

    // Build directory entry (32 bytes) at DMA
    uint8_t dirEntry[32] = {};
    dirEntry[0] = 0; // User 0

    // Filename in FCB format
    uint8_t fcb[12];
    nameToFcb(name, fcb);
    memcpy(dirEntry + 1, fcb + 1, 11);

    // EX, S1, S2 = 0
    dirEntry[12] = 0;
    dirEntry[13] = 0;
    dirEntry[14] = 0;

    // RC = record count
    std::string path = hostPath(name);
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        int records = static_cast<int>((st.st_size + 127) / 128);
        dirEntry[15] = static_cast<uint8_t>(records > 128 ? 128 : records);
    }

    // Fake allocation map (non-zero to indicate file has blocks)
    for (int i = 16; i < 32; i++) dirEntry[i] = 0x01;

    // Write to DMA at offset 0
    for (int i = 0; i < 32; i++) {
        mem_.write(dmaAddr_ + i, dirEntry[i]);
    }

    return 0; // Found at index 0 in DMA buffer
}

/**
 * @brief BDOS 19: Delete File.
 *
 * Loescht die im FCB angegebene Datei. Unterstuetzt '?'-Wildcards
 * fuer Muster-Loeschung.
 *
 * @return 0 bei Erfolg, 0xFF wenn nicht gefunden
 */
uint8_t CpmBdos::bdosDeleteFile() {
    uint16_t fcbAddr = cpu_.DE;
    std::string name = fcbToName(fcbAddr);
    if (name.empty()) return 0xFF;

    // Check if pattern has wildcards
    bool hasWildcard = false;
    for (int i = 1; i <= 11; i++) {
        if ((mem_.read(fcbAddr + i) & 0x7F) == '?') { hasWildcard = true; break; }
    }

    if (hasWildcard) {
        // Delete matching files
        uint8_t pattern[11];
        for (int i = 0; i < 11; i++) pattern[i] = mem_.read(fcbAddr + 1 + i);

        DIR* dir = opendir(workDir_.c_str());
        if (!dir) return 0xFF;
        bool deleted = false;
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            if (ent->d_name[0] == '.') continue;
            uint8_t fcb[12];
            nameToFcb(ent->d_name, fcb);
            if (matchPattern(pattern, fcb + 1)) {
                std::string path = hostPath(ent->d_name);
                remove(path.c_str());
                deleted = true;
            }
        }
        closedir(dir);
        return deleted ? 0 : 0xFF;
    }

    // Close if open
    auto it = openFiles_.find(fcbAddr);
    if (it != openFiles_.end()) {
        if (it->second.fp) fclose(it->second.fp);
        openFiles_.erase(it);
    }

    std::string found = findFileCI(name);
    if (found.empty()) return 0xFF;

    std::string path = hostPath(found);
    return (remove(path.c_str()) == 0) ? 0 : 0xFF;
}

/**
 * @brief BDOS 20: Read Sequential.
 *
 * Liest 128 Bytes ab der aktuellen Dateiposition in den DMA-Puffer.
 * Position wird aus den FCB-Feldern EX/S2/CR berechnet.
 *
 * @return 0 bei Erfolg, 1 bei EOF (ungelesene Bytes werden mit 0x1A gefuellt)
 */
uint8_t CpmBdos::bdosReadSeq() {
    uint16_t fcbAddr = cpu_.DE;
    auto it = openFiles_.find(fcbAddr);
    if (it == openFiles_.end()) return 1;

    FILE* fp = it->second.fp;

    // Compute file position from FCB
    uint8_t ex = mem_.read(fcbAddr + 12);
    uint8_t s2 = mem_.read(fcbAddr + 14);
    uint8_t cr = mem_.read(fcbAddr + 32);
    uint32_t record = (static_cast<uint32_t>(s2) * 32 + ex) * 128 + cr;
    long offset = static_cast<long>(record) * 128;

    fseek(fp, offset, SEEK_SET);

    uint8_t buf[128];
    memset(buf, 0x1A, 128); // Pre-fill with EOF marker
    size_t nread = fread(buf, 1, 128, fp);

    // Write to DMA
    for (int i = 0; i < 128; i++) {
        mem_.write(dmaAddr_ + i, buf[i]);
    }

    // Advance position
    cr++;
    if (cr >= 128) {
        cr = 0;
        ex++;
        if (ex >= 32) {
            ex = 0;
            s2++;
        }
    }
    mem_.write(fcbAddr + 12, ex);
    mem_.write(fcbAddr + 14, s2);
    mem_.write(fcbAddr + 32, cr);

    return (nread == 0) ? 1 : 0;
}

/**
 * @brief BDOS 21: Write Sequential.
 *
 * Schreibt 128 Bytes vom DMA-Puffer in die Datei an der aktuellen Position.
 *
 * @return 0 bei Erfolg, 5 bei Schreibfehler
 */
uint8_t CpmBdos::bdosWriteSeq() {
    uint16_t fcbAddr = cpu_.DE;
    auto it = openFiles_.find(fcbAddr);
    if (it == openFiles_.end()) return 5; // Write error

    FILE* fp = it->second.fp;

    // Compute position
    uint8_t ex = mem_.read(fcbAddr + 12);
    uint8_t s2 = mem_.read(fcbAddr + 14);
    uint8_t cr = mem_.read(fcbAddr + 32);
    uint32_t record = (static_cast<uint32_t>(s2) * 32 + ex) * 128 + cr;
    long offset = static_cast<long>(record) * 128;

    fseek(fp, offset, SEEK_SET);

    uint8_t buf[128];
    for (int i = 0; i < 128; i++) {
        buf[i] = mem_.read(dmaAddr_ + i);
    }

    size_t written = fwrite(buf, 1, 128, fp);
    fflush(fp);

    // Advance position
    cr++;
    if (cr >= 128) {
        cr = 0;
        ex++;
        if (ex >= 32) {
            ex = 0;
            s2++;
        }
    }
    mem_.write(fcbAddr + 12, ex);
    mem_.write(fcbAddr + 14, s2);
    mem_.write(fcbAddr + 32, cr);

    return (written == 128) ? 0 : 5;
}

/**
 * @brief BDOS 22: Make File.
 *
 * Erstellt eine neue Datei im Arbeitsverzeichnis. Falls die Datei
 * bereits existiert, wird sie geloescht und neu erstellt.
 *
 * @return 0 bei Erfolg, 0xFF bei Fehler
 */
uint8_t CpmBdos::bdosMakeFile() {
    uint16_t fcbAddr = cpu_.DE;
    std::string name = fcbToName(fcbAddr);
    if (name.empty()) return 0xFF;

    // Close if already open
    auto it = openFiles_.find(fcbAddr);
    if (it != openFiles_.end()) {
        if (it->second.fp) fclose(it->second.fp);
        openFiles_.erase(it);
    }

    // Delete existing file (case-insensitive)
    std::string existing = findFileCI(name);
    if (!existing.empty()) {
        remove(hostPath(existing).c_str());
    }

    // Create new file (use lowercase name)
    std::string path = hostPath(name);
    FILE* fp = fopen(path.c_str(), "w+b");
    if (!fp) return 0xFF;

    openFiles_[fcbAddr] = {fp, path};

    // Initialize FCB
    mem_.write(fcbAddr + 12, 0); // EX
    mem_.write(fcbAddr + 13, 0); // S1
    mem_.write(fcbAddr + 14, 0); // S2
    mem_.write(fcbAddr + 15, 0); // RC
    mem_.write(fcbAddr + 32, 0); // CR

    return 0;
}

/**
 * @brief BDOS 23: Rename File.
 *
 * Der FCB enthaelt bei Offset 0 den alten und bei Offset 16 den neuen Namen.
 *
 * @return 0 bei Erfolg, 0xFF bei Fehler
 */
uint8_t CpmBdos::bdosRenameFile() {
    uint16_t fcbAddr = cpu_.DE;

    // Old name from bytes 1-11
    std::string oldName = fcbToName(fcbAddr);

    // New name from bytes 17-27 (offset 16 is the "drive" of new name)
    // Read it as if it were a separate FCB at fcbAddr+16
    std::string newName = fcbToName(fcbAddr + 16);

    if (oldName.empty() || newName.empty()) return 0xFF;

    std::string foundOld = findFileCI(oldName);
    if (foundOld.empty()) return 0xFF;

    // Delete existing target if present
    std::string foundNew = findFileCI(newName);
    if (!foundNew.empty()) {
        remove(hostPath(foundNew).c_str());
    }

    std::string oldPath = hostPath(foundOld);
    std::string newPath = hostPath(newName);
    return (rename(oldPath.c_str(), newPath.c_str()) == 0) ? 0 : 0xFF;
}

/**
 * @brief BDOS 33: Read Random.
 *
 * Liest 128 Bytes von der Position, die durch R0/R1/R2 im FCB angegeben wird.
 *
 * @return 0 bei Erfolg, 1 bei EOF, 6 bei nicht geoeffneter Datei
 */
uint8_t CpmBdos::bdosReadRand() {
    uint16_t fcbAddr = cpu_.DE;
    auto it = openFiles_.find(fcbAddr);
    if (it == openFiles_.end()) return 6;

    uint32_t record = mem_.read(fcbAddr + 33) |
                      (static_cast<uint32_t>(mem_.read(fcbAddr + 34)) << 8);
    long offset = static_cast<long>(record) * 128;

    FILE* fp = it->second.fp;
    fseek(fp, offset, SEEK_SET);

    uint8_t buf[128];
    memset(buf, 0x1A, 128);
    size_t nread = fread(buf, 1, 128, fp);

    for (int i = 0; i < 128; i++) {
        mem_.write(dmaAddr_ + i, buf[i]);
    }

    // Update EX/CR to match random position
    mem_.write(fcbAddr + 12, static_cast<uint8_t>((record / 128) & 0x1F));
    mem_.write(fcbAddr + 14, static_cast<uint8_t>((record / 128) / 32));
    mem_.write(fcbAddr + 32, static_cast<uint8_t>(record & 0x7F));

    return (nread == 0) ? 1 : 0;
}

/**
 * @brief BDOS 34: Write Random.
 *
 * Schreibt 128 Bytes an die Position, die durch R0/R1/R2 angegeben wird.
 *
 * @return 0 bei Erfolg, 5 bei Fehler, 6 bei nicht geoeffneter Datei
 */
uint8_t CpmBdos::bdosWriteRand() {
    uint16_t fcbAddr = cpu_.DE;
    auto it = openFiles_.find(fcbAddr);
    if (it == openFiles_.end()) return 6;

    uint32_t record = mem_.read(fcbAddr + 33) |
                      (static_cast<uint32_t>(mem_.read(fcbAddr + 34)) << 8);
    long offset = static_cast<long>(record) * 128;

    FILE* fp = it->second.fp;
    fseek(fp, offset, SEEK_SET);

    uint8_t buf[128];
    for (int i = 0; i < 128; i++) {
        buf[i] = mem_.read(dmaAddr_ + i);
    }

    size_t written = fwrite(buf, 1, 128, fp);
    fflush(fp);

    // Update EX/CR
    mem_.write(fcbAddr + 12, static_cast<uint8_t>((record / 128) & 0x1F));
    mem_.write(fcbAddr + 14, static_cast<uint8_t>((record / 128) / 32));
    mem_.write(fcbAddr + 32, static_cast<uint8_t>(record & 0x7F));

    return (written == 128) ? 0 : 5;
}

/**
 * @brief BDOS 35: Compute File Size.
 *
 * Setzt R0/R1/R2 im FCB auf die Dateigroesse in 128-Byte-Records.
 */
void CpmBdos::bdosFileSize() {
    uint16_t fcbAddr = cpu_.DE;
    std::string name = fcbToName(fcbAddr);

    std::string found = findFileCI(name);
    uint32_t records = 0;
    if (!found.empty()) {
        std::string path = hostPath(found);
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            records = static_cast<uint32_t>((st.st_size + 127) / 128);
        }
    }

    mem_.write(fcbAddr + 33, static_cast<uint8_t>(records & 0xFF));
    mem_.write(fcbAddr + 34, static_cast<uint8_t>((records >> 8) & 0xFF));
    mem_.write(fcbAddr + 35, static_cast<uint8_t>((records >> 16) & 0xFF));
}

/**
 * @brief BDOS 36: Set Random Record.
 *
 * Berechnet die Random-Record-Nummer aus den sequentiellen FCB-Feldern
 * EX/S2/CR und setzt R0/R1/R2 entsprechend.
 */
void CpmBdos::bdosSetRandRec() {
    uint16_t fcbAddr = cpu_.DE;
    uint8_t ex = mem_.read(fcbAddr + 12);
    uint8_t s2 = mem_.read(fcbAddr + 14);
    uint8_t cr = mem_.read(fcbAddr + 32);

    uint32_t record = (static_cast<uint32_t>(s2) * 32 + ex) * 128 + cr;
    mem_.write(fcbAddr + 33, static_cast<uint8_t>(record & 0xFF));
    mem_.write(fcbAddr + 34, static_cast<uint8_t>((record >> 8) & 0xFF));
    mem_.write(fcbAddr + 35, static_cast<uint8_t>((record >> 16) & 0xFF));
}

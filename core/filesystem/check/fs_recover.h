/**
 * @file fs_recover.h
 * @brief Das gemeinsame Modell der Wiederherstellung — Fund, Guete, Bericht.
 *
 * Gegenstueck zu @ref fs_check.h, und bewusst ein **eigenes** Modell: eine Pruefung
 * sagt, was falsch ist, eine Suche sagt, was noch da ist.  Ein geloeschter Platz ist
 * kein Schaden — die Diskette ist in Ordnung, sie verschweigt nur etwas.
 *
 * Drei Dinge tragen dieses Modell:
 *
 * 1. **Retten geht vor Wiederherstellen** (Entwurf E7).  Der Vorgabeweg ist *in den
 *    Linux-Ordner holen*, und der muss an einer schreibgeschuetzten Diskette
 *    vollstaendig bedienbar sein — der haeufigste Fall ist „einmal alles retten, was
 *    noch da ist, dann die Diskette in Ruhe lassen".  Auf die Diskette
 *    zurueckzuschreiben ist der Ausnahmefall und traegt sein eigenes Kennzeichen
 *    (@ref FsRecoverFind::wiederherstellbar).
 * 2. **Die Guete ist kein Gefuehl, sondern eine Aussage mit Belegen** (§13.2).  Zu
 *    jedem Fund steht in @ref FsRecoverFind::detail im Klartext, WORAN es haengt —
 *    „Block 44 gehoert jetzt zu HELP.DAT".  Eine Sterneskala ohne Begruendung waere
 *    an dreissig Jahre alten Datentraegern wertlos.
 * 3. **Ein Fund ohne Namen ist trotzdem ein Fund.**  Bei UDOS ueberlebt der Name das
 *    Loeschen nicht; bei CP/M ueberleben Rohbereiche ohne jeden Verzeichnisplatz.
 *    Deshalb traegt jeder Fund einen @ref FsRecoverFind::vorschlag, unter dem er sich
 *    ohne Rueckfrage speichern laesst.
 *
 * @see doc/design/15_dateisystempruefung.md §13
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include <cstdint>
#include <string>
#include <vector>

/**
 * @enum FsRecoverLevel
 * @brief Wie tief wird gesucht?
 *
 * - @c Verzeichnis — nur die Verwaltungsstrukturen (CP/M: die geloeschten
 *   Verzeichnisplaetze).  Billig; an einer physischen Diskette kostet es keinen
 *   zusaetzlichen Spurzugriff.
 * - @c Oberflaeche — zusaetzlich jeder freie Bereich der Diskette.  Das findet die
 *   Bruchstuecke ohne Verzeichnisplatz und kostet die ganze Scheibe.
 */
enum class FsRecoverLevel : uint8_t { Verzeichnis = 0, Oberflaeche = 1 };

/**
 * @enum FsRecoverQuality
 * @brief Wie gut ist ein Fund?  (§13.2)
 *
 * Die Reihenfolge ist aufsteigend, damit sich Funde danach sortieren lassen — das
 * Beste zuerst ist die Reihenfolge, in der man sie durchgeht.
 */
enum class FsRecoverQuality : uint8_t {
    Bruchstueck    = 0,  ///< ein Teil ist neu vergeben oder die Struktur bricht ab
    Wahrscheinlich = 1,  ///< vollstaendig, aber mit benannten Vorbehalten (CRC, Streit)
    Sicher         = 2   ///< nichts davon gehoert einer lebenden Datei
};

/// @brief Hoechstens so viele Sektoren fuehrt ein Fund in @ref FsRecoverFind::orte.
///
/// Eine 300-KB-Datei haette sonst 2400 Eintraege, durch die niemand blaettert; die
/// Liste ist eine Bedienhilfe, kein Abbild.  Was darueber hinausgeht, wird gerettet
/// wie eh und je — nur nicht mehr aufgezaehlt.
inline constexpr size_t kFsRecoverMaxOrte = 512;

const char* fsRecoverQualityName(FsRecoverQuality q);  ///< "sicher"|"wahrscheinlich"|"Bruchstueck"

/// @name Gemeinsame Handgriffe der Suchlaeufe
///
/// Sie stehen hier und nicht in `cpm_recover.cpp` bzw. `udos_recover.cpp`, weil der
/// Anwender die Ergebnisse NEBENEINANDER sieht: eine CP/M- und eine UDOS-Diskette
/// duerfen denselben Inhalt nicht verschieden einordnen und ihre Vorbehalte nicht
/// verschieden aufzaehlen.
/// @{

/**
 * @brief Einordnung eines Rohbereichs nach seinem Inhalt (§13.3).
 *
 * @return `"Text"` (mindestens 90 % druckbar, `1A` und die Zeilenenden zaehlen mit),
 *         `"Programm"` (Z80-Einsprungmuster `JP` / `LD SP,nn` / `DI` am Anfang) oder
 *         `"unklar"`.
 */
std::string fsRecoverEinordnung(const std::vector<uint8_t>& daten);

/**
 * @brief Ist der Bereich reines Fuellmuster?
 *
 * Ein Bereich aus EINEM immer gleichen Byte ist kein Inhalt — unabhaengig davon,
 * welches Byte es ist (FORMAT.COM fuellt je nach Menuepunkt mit `0xE5`, `0xF6` oder
 * dem Pruefmuster `0x53`).  Ohne diese Regel meldet eine Oberflaechensuche die halbe
 * Diskette als Bruchstueck.
 */
bool fsRecoverFuellmuster(const std::vector<uint8_t>& daten);

/// @brief Bis zu drei Belege an @p detail anhaengen, den Rest zusammenfassen.
void fsRecoverBelege(std::string& detail, const std::vector<std::string>& belege,
                     const std::string& was);
/// @}

/**
 * @struct FsRecoverOrt
 * @brief Ein Sektor auf der Diskette — Zylinder, Kopf und die Sektor-**Kennung**.
 *
 * Die Kennung, nicht der Versatz in der Spur: `DiskEditorWindow.zeige_ort` sucht den
 * Sektor ueber seine ID.
 */
struct FsRecoverOrt {
    int cyl = -1, head = -1, sector = -1;
};

/**
 * @struct FsRecoverFind
 * @brief Ein Fund: eine geloeschte Datei oder ein Bereich mit Inhalt.
 */
struct FsRecoverFind {
    /// @brief Der ueberlieferte Name; **leer**, wo keiner ueberlebt hat (UDOS, Rohbereich).
    std::string name;
    std::string type;      ///< Dateityp bzw. Einordnung des Inhalts ("Text", "Programm")
    std::string origin;    ///< woher der Fund stammt ("Verzeichnisplatz 37", "freier Bereich")
    /// @brief Vorschlag fuer den Linux-Dateinamen — **nie leer**, auch ohne Namen.
    std::string vorschlag;
    int      volume = 0;   ///< von @ref DiskVolume nachgetragen
    uint64_t size   = 0;   ///< Nutzbytes, so weit sie sich bestimmen lassen

    FsRecoverQuality quality = FsRecoverQuality::Sicher;
    /// @brief Die Belege zur Guete, im Klartext und mit Zahlen ("" = ohne Vorbehalt).
    std::string detail;

    /// @brief Laesst sich der Fund **auf der Diskette** wieder eintragen?
    ///
    /// Nicht dasselbe wie „rettbar": herausholen laesst sich immer etwas, aber ein
    /// Verzeichnisplatz zurueckzuholen, dessen Bloecke inzwischen einer lebenden
    /// Datei gehoeren, erzeugte genau die Kreuzbelegung, die die Pruefung als
    /// @c Gefahr meldet.  @ref warum_nicht nennt den Grund.
    bool        wiederherstellbar = false;
    std::string warum_nicht;

    /// @name Ort auf der Diskette — -1 = ortlos (fuer den Sprung in den Diskeditor)
    /// @{
    int cyl = -1, head = -1, sector_index = -1;
    /// @}

    /**
     * @brief **Alle** Sektoren des Fundes, in Lesereihenfolge.
     *
     * Der Grund, warum das neben @ref teile noch einmal dasteht: `teile` ist
     * absichtlich privat und nur von der Dateisystemklasse zu deuten (Blocknummern,
     * Satzanfaenge, …), die Oberflaeche braucht aber etwas, mit dem sie umgehen kann.
     *
     * Und sie braucht es wirklich.  Beim Retten ist die entscheidende Frage nicht
     * „wo faengt das an", sondern **„ist das ueberhaupt das, was ich suche"** — und
     * bei UDOS gibt es nicht einmal einen Namen, an dem man das ablesen koennte.  Nur
     * mit dieser Liste kann der Dialog den Anwender durch die Sektoren des Fundes
     * fuehren, bevor er irgendetwas tut; die Saetze einer UDOS-Datei sind verkettet
     * und liegen keineswegs hintereinander — von Hand faende sie niemand.
     *
     * @ref cyl / @ref head / @ref sector_index sind der erste Eintrag (bzw. der
     * Kopfsektor).  Begrenzt auf @ref kFsRecoverMaxOrte Eintraege.
     */
    std::vector<FsRecoverOrt> orte;

    /**
     * @brief Woraus der Fund besteht — **vom Dateisystem zu deuten**, wie bei
     *        @ref FsRepair die namenlosen Parameter.
     *
     * CP/M: bei @c a == 0 die Nummern der Verzeichnisplaetze, bei @c a == 1 die
     * Blocknummern des Rohbereichs.
     *
     * UDOS/NDOS: @c d nennt die Art (0 = Kopfsektor-Fund, 1 = Rohbereich), @c a und
     * @c b Spur und Sektorindex des Anfangs, @c c die erreichbaren Saetze bzw. die
     * Sektorzahl; @c teile fuehrt die Satzanfaenge bzw. Sektoren als
     * `Spur * 256 + Sektorindex`.
     */
    std::vector<int> teile;
    int         a = 0, b = 0, c = 0, d = 0;   ///< Ausfuehrungsparameter der Klasse
    std::string s;
};

/**
 * @struct FsRecoverReport
 * @brief Das Ergebnis eines Suchlaufs.
 */
struct FsRecoverReport {
    FsRecoverLevel level = FsRecoverLevel::Verzeichnis;

    /// @brief Wurde alles angesehen, was zu dieser Suchtiefe gehoert?  (wie
    ///        @ref FsCheckReport::vollstaendig — an einer physischen Diskette
    ///        heisst „nichts gefunden" sonst zu viel)
    bool vollstaendig = true;
    int  spuren_gelesen = 0, spuren_gesamt = 0;

    std::vector<FsRecoverFind> funde;

    bool leer() const { return funde.empty(); }
    /// @brief Wie viele Funde dieser Guete?
    int  zaehler(FsRecoverQuality q) const;

    /// @brief Guete absteigend, dann Volume, dann Name.  Reproduzierbar.
    void sortieren();
    /// @brief Einen zweiten Bericht anhaengen (@ref DiskVolume ueber alle Volumes).
    void uebernimm(const FsRecoverReport& anderer);

    /// @brief Eine Zeile je Fund: `NR  GUETE  VOLUME  NAME  GROESSE  HERKUNFT`.
    ///        Greptauglich, wie @ref FsCheckReport::alsText.
    std::string alsText(bool mit_volume = false) const;
    /// @brief Kurzfassung ("3 sicher, 1 Bruchstueck"); "" bei leerem Bericht.
    std::string kurzfassung() const;
};

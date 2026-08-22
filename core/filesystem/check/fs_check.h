/**
 * @file fs_check.h
 * @brief Das gemeinsame Modell der Dateisystempruefung — Befund, Schwere, Reparatur.
 *
 * Eine **Pruefung** sieht sich ein Dateisystem an und liefert eine Liste von
 * @ref FsFinding.  Sie **aendert nichts** (Entwurf E1): jede Aenderung geht
 * ausschliesslich ueber eine ausdruecklich gewaehlte @ref FsRepair, und ausgefuehrt
 * wird die von der Dateisystemklasse selbst (@ref FileSystem::repair) — sie kennt
 * ihre Invarianten (E3).
 *
 * Drei Dinge sind an diesem Modell tragend:
 *
 * 1. **Vier Schweregrade, und der oberste heisst @ref FsSeverity::Gefahr** (E4).  Er
 *    bedeutet nicht „Daten sind verloren", sondern **„der naechste Schreibvorgang
 *    zerstoert Daten"** — der Musterfall ist ein UDOS-Sektor, der zu einer Datei
 *    gehoert und im Belegungsplan als frei steht.  Das ist die einzige Auskunft, die
 *    eine sofortige Handlung nach sich zieht (Schreibschutz drauflassen, sichern,
 *    dann reparieren), und sie geht in einer Liste von Warnungen unter, wenn sie
 *    nicht eigens benannt ist.
 * 2. **Die Kennung ist ein stabiler Vertrag** (E5).  `"cpm.block.doppelt"` steht in
 *    Skripten, in `--json`, in Tests und in der Oberflaeche; sie wird nicht
 *    umbenannt, sondern hoechstens ergaenzt.  Waechter: `FsCheckVertrag.*`.
 * 3. **@ref FsCheckReport::vollstaendig ist keine Nebensache.**  An einer physischen
 *    Diskette wird nur angesehen, was schon gelesen ist; „ohne Befund" hiesse dort
 *    zu viel.  Aus demselben Grund sperrt ein unvollstaendiger Bericht spaeter jede
 *    Reparatur, die etwas freigibt (E8) — aus halbem Wissen einen Belegungsplan zu
 *    bauen ist der eine Fehler, den ein fsck nie machen darf.
 *
 * @see doc/design/15_dateisystempruefung.md §5
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include <cstdint>
#include <string>
#include <vector>

/**
 * @enum FsSeverity
 * @brief Wie dringend ist ein Befund?  (Entwurf §5, E4)
 */
enum class FsSeverity : uint8_t {
    Info    = 0,   ///< bemerkenswert, nicht falsch (z. B. „7 geloeschte Plaetze")
    Warnung = 1,   ///< in sich widerspruechlich, aber ohne Folgen (Zaehler, verlorener Platz)
    Fehler  = 2,   ///< etwas ist bereits unerreichbar oder falsch (Kettenbruch, wilder Zeiger)
    Gefahr  = 3    ///< der NAECHSTE Schreibvorgang zerstoert Daten
};

/**
 * @enum FsLayer
 * @brief Auf welcher Ebene entstand der Befund?  Das legt fest, **was er kostet**.
 *
 * - @c Medium — Adressmarken, CRCs, Nachspann, unformatierte Spuren: eine Spur je Spur.
 * - @c Verwaltung — Verzeichnis und Belegungsplan: wenige Spuren, beim Mounten ohnehin
 *   gelesen.  **Nur diese Ebene laeuft automatisch beim Oeffnen** (E2).
 * - @c Dateien — Kopfsektoren, Ketten, Kreuzbelegung: die ganze Diskette.
 * - @c Erkennung — die Ebene 0 (§11): sie betrachtet gar kein Dateisystem, sondern
 *   sagt, **warum keines erkannt wurde**.  Sie kommt nur an einer roh geoeffneten
 *   Diskette vor, kostet nichts (die Gruende sind beim Oeffnen schon angefallen) und
 *   traegt nie eine Reparatur.
 */
enum class FsLayer : uint8_t { Medium = 0, Verwaltung = 1, Dateien = 2, Erkennung = 3 };

/// @brief Prueftiefe.  @c Schnell = nur @ref FsLayer::Verwaltung (E2).
enum class FsCheckLevel : uint8_t { Schnell = 0, Voll = 1 };

const char* fsSeverityName(FsSeverity s);   ///< "Hinweis"|"Warnung"|"Fehler"|"Gefahr"
const char* fsLayerName(FsLayer l);         ///< "Medium"|"Verwaltung"|"Dateien"|"Erkennung"

/**
 * @struct FsSchritt
 * @brief Ein **Arbeitsschritt** der Pruefung bzw. der Suche — die Checkliste (§5a).
 *
 * Der Grund, warum es das gibt: eine Pruefung, die „ohne Befund" meldet, sagt dem
 * Bediener nicht, WORAUF sie gesehen hat.  Bei einer dreissig Jahre alten Diskette
 * ist das der Unterschied zwischen einer Entwarnung und einem Achselzucken — und
 * ein Lauf, der 70 ms dauert, sieht ohne diese Liste aus, als haette er gar nicht
 * stattgefunden.
 *
 * Die Schritte werden von den Pruefern selbst gemeldet (@ref FsCheckReport::schritt),
 * nicht von der Oberflaeche erfunden: eine Checkliste, die etwas behauptet, was
 * nicht gelaufen ist, waere schlimmer als gar keine.  Gezaehlt wird **eifrig** —
 * jeder Befund landet im gerade offenen Schritt, sobald er entsteht; damit ueberlebt
 * die Zaehlung auch die vorzeitigen Ausstiege und das spaetere Umsortieren.
 */
struct FsSchritt {
    std::string id;                     ///< stabile Kennung, z. B. "cpm.schritt.verzeichnis"
    std::string titel;                  ///< eine Zeile Klartext fuer die Anzeige
    bool        ausgefuehrt = true;     ///< @c false = uebersprungen, Grund in @ref grund
    std::string grund;                  ///< warum uebersprungen ("nur bei der Vollpruefung")
    int         treffer = 0;            ///< Befunde bzw. Funde, die in ihm entstanden
    FsSeverity  hoechste = FsSeverity::Info;  ///< schwerster Befund darin (Pruefung)
};

/**
 * @struct FsRepair
 * @brief Ein Reparaturvorschlag zu genau einem Befund.
 *
 * Beschrieben wird er von der Pruefung, **ausgefuehrt** von der Dateisystemklasse
 * (E3): @ref FileSystem::repair bekommt diese Struktur zurueck und deutet
 * @ref kind samt den Parametern @ref a … @ref s.  Die Parameter sind absichtlich
 * namenlos — sie gehen niemanden ausser der Klasse etwas an, die sie erzeugt hat,
 * und die Oberflaeche reicht sie unveraendert durch.
 */
struct FsRepair {
    std::string kind;                  ///< stabile Kennung, z. B. "udos.karte.sektoren.sperren"
    std::string text;                  ///< was genau geschieht — im Klartext, mit Zahlen
    bool        datenverlust = false;  ///< verwirft Nutzdaten oder einen Verweis darauf
    bool        empfohlen    = false;  ///< hoechstens eine je Befund
    bool        geraten      = false;  ///< stellt einen Wert her, der nicht ableitbar ist
    bool        gesperrt     = false;  ///< nicht ausfuehrbar (E8) — Grund in @ref warum
    std::string warum;                 ///< Begruendung der Sperre

    int         a = 0, b = 0, c = 0, d = 0;   ///< Ausfuehrungsparameter der Klasse
    std::string s;
};

/**
 * @struct FsFinding
 * @brief Ein Befund.
 */
struct FsFinding {
    std::string id;                          ///< stabile Kennung (E5), z. B. "cpm.block.doppelt"
    FsSeverity  severity = FsSeverity::Info;
    FsLayer     layer    = FsLayer::Verwaltung;
    int         volume   = 0;                ///< von @ref DiskVolume nachgetragen
    std::string object;                      ///< "TEST.COM" · "Platz 37" · "c12h0 ID 5"
    std::string text;                        ///< ein Satz, mit den konkreten Zahlen darin

    /// @name Ort auf der Diskette — -1 = ortlos
    ///
    /// Wo es ihn gibt, laesst sich ein Befund im **Diskeditor** oeffnen (E9): ein
    /// Befund, den man nicht ansehen kann, zwingt zum Vertrauen.
    /// @{
    int cyl = -1, head = -1, sector_index = -1;
    /// @}

    std::vector<FsRepair> repairs;           ///< leer = nur zur Kenntnis
};

/**
 * @struct FsCheckReport
 * @brief Das Ergebnis einer Pruefung.
 */
struct FsCheckReport {
    FsCheckLevel level = FsCheckLevel::Schnell;

    /**
     * @brief Wurde **alles** angesehen, was zu dieser Prueftiefe gehoert?
     *
     * `false` heisst: an einer physischen Diskette waren Spuren noch nicht gelesen
     * und wurden uebersprungen (die Pruefung laedt nur nach, wenn sie darf).  Der
     * Unterschied zwischen „ohne Befund" und „**bislang** ohne Befund" gehoert in
     * jede Anzeige — und er sperrt spaeter die freigebenden Reparaturen (E8).
     */
    bool vollstaendig = true;
    /// @name Wie viel wurde angesehen?
    ///
    /// Gezaehlt wird, was DIESE Prueftiefe braucht — nicht, was die Diskette hat:
    /// @ref spuren_gesamt sind die Spuren, in die die Pruefung sehen WOLLTE,
    /// @ref spuren_gelesen die davon verfuegbaren.  Damit steht bei einer
    /// vollstaendigen Pruefung immer `N von N`, und die Differenz ist genau das,
    /// was @ref vollstaendig zu @c false macht.  (Mit „alle Spuren der Diskette"
    /// als Nenner meldete eine tadellose Schnellpruefung „1 von 156" und saehe
    /// unvollstaendig aus.)
    /// @{
    int  spuren_gelesen = 0;
    int  spuren_gesamt  = 0;
    /// @}

    std::vector<FsFinding> findings;   ///< nach @ref sortieren geordnet

    /// @brief Die abgearbeitete Checkliste, in der Reihenfolge der Ausfuehrung (§5a).
    std::vector<FsSchritt> schritte;
    /// @brief Index des offenen Schritts (-1 = keiner) — Buchhaltung, nicht Ergebnis.
    int aktueller_schritt = -1;

    /// @brief Einen Schritt beginnen (und den vorigen damit beenden).
    ///        Ein zweites Mal mit derselben @p id fuehrt den bestehenden fort.
    FsSchritt& schritt(std::string id, std::string titel);
    /// @brief Einen Schritt vermerken, der NICHT gelaufen ist, mit Begruendung.
    void schrittEntfaellt(std::string id, std::string titel, std::string grund);
    /// @brief Keinen Schritt mehr offen halten (alles Weitere zaehlt nirgends).
    void schrittEnde() { aktueller_schritt = -1; }
    /// @brief Einen entstandenen Befund dem offenen Schritt zuschlagen.
    ///        Ruft @ref FsFindings; von Hand braucht es das nicht.
    void zaehleImSchritt(FsSeverity s);

    bool       ohneBefund() const { return findings.empty(); }
    /// @brief Hoechste vorkommende Schwere; bei leerem Bericht @c Info.
    FsSeverity hoechste() const;
    /// @brief Wie viele Befunde dieser Schwere?
    int        zaehler(FsSeverity s) const;
    /// @brief Wie viele Befunde MINDESTENS dieser Schwere?
    int        zaehlerAb(FsSeverity s) const;

    /// @brief Schwere absteigend, dann Ebene, dann Ort, dann Kennung.
    ///        Reproduzierbar — zwei Laeufe muessen dieselbe Reihenfolge liefern.
    void sortieren();

    /// @brief Einen zweiten Bericht anhaengen (@ref DiskVolume ueber alle Volumes).
    void uebernimm(const FsCheckReport& anderer);

    /**
     * @brief Hoechstens @p je_kennung Befunde derselben Art behalten.
     *
     * Eine wirklich kaputte Diskette bringt sonst fuenfhundert Zeilen hervor, und
     * fuenfhundert Zeilen liest niemand — der Bericht waere genau dann wertlos,
     * wenn er am noetigsten ist.  Die uebrigen werden zu EINEM Befund derselben
     * Kennung zusammengezogen („… und 431 weitere dieser Art"), damit weder die
     * Zahl noch die Kennung verlorengeht.  Gezaehlt wird je Kennung UND Volume.
     */
    void begrenzen(size_t je_kennung);

    /// @brief Eine Zeile je Befund: `SCHWERE  EBENE  ORT  KENNUNG  Text`.
    ///        Bewusst greptauglich — das ist das Format der Kommandozeile.
    std::string alsText(bool mit_volume = false) const;

    /// @brief Kurzfassung fuer Streifen und Statuszeile ("2 Gefahr, 1 Fehler"),
    ///        "" bei einem Bericht ohne Befund.
    std::string kurzfassung() const;
};

/**
 * @brief Kleiner Bausatz fuer die Pruefer — spart in jedem Pruefzweig drei Zeilen.
 */
class FsFindings {
public:
    explicit FsFindings(FsCheckReport& an) : an_(an) {}

    /// @brief Befund anlegen und eine Referenz darauf zurueckgeben (fuer Ort/Reparaturen).
    FsFinding& add(std::string id, FsSeverity sev, FsLayer layer,
                   std::string object, std::string text);

    /// @brief Wie @ref add, mit Ort.
    FsFinding& addAt(std::string id, FsSeverity sev, FsLayer layer,
                     std::string object, std::string text,
                     int cyl, int head, int sector_index = -1);

private:
    FsCheckReport& an_;
};

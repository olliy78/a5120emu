/**
 * @file kassetten_medium.h
 * @brief Kassettenband als Medium: zwei Seiten, je eine Folge von Blöcken in Bandlängen
 *        (doc/design/29_kassette.md §5, doc/kassette/treiber.md, doc/kassette/k5200.md).
 *
 * **Was das Medium kann und was nicht.**  Es speichert Bytefolgen *roh*, genau so, wie die
 * Software sie über die Karte auf das Band gegeben hat: `AAH | Daten | CRC lo | CRC hi | AAH`
 * (treiber.md §2.1).  Präambel, Postambel und CRC sind Sache der Software; ein Block mit
 * falscher CRC oder fremder Präambel (`ABH`, treiber.md §5.1) ist deshalb ein gültiger Inhalt.
 * Als Hilfen gibt es @ref crc16_arc, @ref blockBauen und @ref bandmarkeBytes — gerechnet wird
 * damit nur auf Wunsch (CLI, Tests, Bootband).
 *
 * **Längen sind Bandlängen** (µm), nicht Zeiten: die Zeit macht das Laufwerk aus seiner
 * Geschwindigkeit.  Ein Band, mit 38 cm/s beschrieben, liest sich bei 19 cm/s (nur langsamer).
 * Die Länge eines Blocks ist `n · 8 / Dichte`; die Dichte ist Eigenschaft des Bandes
 * (Vorgabe 31,5 Bit/mm = 12 000 Bit/s bei 381 mm/s, k5200.md §3).
 *
 * **Bandaufbau längs einer Seite** (Seitenkoordinate: 0 = physischer Bandanfang dieser Seite):
 * ```
 * 0 ─ Vorspann (AEB aktiv) ─ vorspann ─ Magnetband ─ BOT-Marke (AEB aktiv, kurz) ─ Initiallücke ─
 * Block ─ Lücke ─ Block … ─ EOT-Marke (AEB aktiv, kurz) ─ Magnetband ─ Vorspann (AEB aktiv) ─ laenge
 * ```
 * (treiber.md §3.1; Länge der Marken und ihr Abstand zum Vorspann sind nicht belegt [?] —
 * begründete Vorgaben in @ref Geometrie.)  Seite B ist dasselbe Band von der anderen Seite:
 * ihre Koordinate ist `laenge − Seite-A-Koordinate`; @ref wenden spiegelt die Position.
 *
 * **Faden:** keiner — das Medium ist nicht thread-sicher; der Lauffaden (bzw. die Kernsperre)
 * bedient es.
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kassette {

/// CRC-16/ARC (Polynom 8005H reflektiert = A001H, Start 0000H, kein XOR; treiber.md §2.2).
/// `crc16_arc("123456789") == 0xBB3D`.  @p start erlaubt abschnittsweises Rechnen.
uint16_t crc16_arc(const uint8_t* daten, size_t n, uint16_t start = 0);
inline uint16_t crc16_arc(const std::vector<uint8_t>& d) { return crc16_arc(d.data(), d.size()); }

constexpr uint8_t kPraeambel = 0xAA;
constexpr uint8_t kPostambel = 0xAA;

/// Bytefolge eines regulären Blocks: `Präambel | Daten | CRC lo | CRC hi | Postambel`.
std::vector<uint8_t> blockBauen(const std::vector<uint8_t>& daten, uint8_t praeambel = kPraeambel,
                                uint8_t postambel = kPostambel);
/// Die Bandmarke: ein Block ohne Daten, `AA 00 00 AA` (treiber.md §2.3).
std::vector<uint8_t> bandmarkeBytes();
/// Ist @p bytes eine Bandmarke (genau vier Bytes `xx 00 00 yy`, Daten leer, CRC 0000H)?
bool istBandmarke(const std::vector<uint8_t>& bytes);
/// Hat der Block die Form `P | Daten (≥ 0) | CRC | Q` mit stimmender CRC?  Präambel und
/// Postambel werden nicht verlangt, nur dass je ein Byte davor/dahinter steht.
bool crcStimmt(const std::vector<uint8_t>& bytes);

/// Seite der Kassette (die der Kopf gerade sieht).
enum class Seite : int { A = 0, B = 1 };
inline Seite andere(Seite s) { return s == Seite::A ? Seite::B : Seite::A; }

/// Größter speicherbarer Block: 256 Daten + Präambel + CRC + Postambel (+ Reserve für Fremdes).
constexpr size_t kMaxBlockBytes = 1024;

/// Ein Block auf dem Band.  @p bytes sind die rohen Bytes (Präambel bis Postambel).
struct Block {
    uint32_t luecke_vor_um = 0;   ///< Bandlänge zwischen dem Ende des Vorgängers (bzw. der BOT-Marke) und diesem Block
    std::vector<uint8_t> bytes;   ///< roh, 1 … kMaxBlockBytes

    bool bandmarke() const { return istBandmarke(bytes); }
    bool operator==(const Block& o) const { return luecke_vor_um == o.luecke_vor_um && bytes == o.bytes; }
};

/// Längenmaße des Bandes, in µm.  Vorgaben nach k5200.md (86 m, BOT-Marke 450 mm) bzw. begründet [?].
struct Geometrie {
    uint32_t laenge_um         = 86'000'000 + 2 * 1'000'000;  ///< ganze Seite einschließlich beider Vorspanne
    uint32_t vorspann_um       = 1'000'000;  ///< Klarsichtstück je Ende [?] (belegt ist nur „länger als der Kopfabstand“)
    uint32_t marken_abstand_um = 450'000;    ///< Magnetbandanfang → BOT-Marke (k5200.md §6 „bot_marke_pos“ [?]); EOT symmetrisch
    uint32_t marke_um          = 20'000;     ///< Länge von BOT-/EOT-Marke [?] (nirgends genannt; KMBG entprellt 2 ms)
    uint32_t initialluecke_um  = 40'000;     ///< BOT-Ende → erster Block (≥ 33 mm, k5200.md §3)
    /**
     * @brief Kassette ohne BOT-/EOT-Marke und mit kurzem Vorspann, wie sie der EGOS-Bandtreiber
     *        des MC80 voraussetzt (AP M7, doc/mc80/kassette.md §3): EGOS fährt nach dem Umspulen
     *        20 s vorwärts und hat dabei den AEB-Interrupt nach 250 ms scharf — jede Marke bräche
     *        mit „Bandende“ ab.  **Vorspann 80 mm [?]**: EGOS' Rückspulen (1980H ff.) fährt erst
     *        2 s vorwärts und verlässt sich darauf, danach aus dem Vorspann heraus zu sein, damit
     *        `UMS` an der Vorspannkante hält — am LW 1200 sind 2 s nur 95 mm.  Sonst stünde eine
     *        frisch eingelegte Kassette woanders als eine zurückgespulte, und das Lesen (16,5 s
     *        Vorlauf) begänne hinter dem ersten Block, den das Schreiben (20 s) angelegt hat.
     */
    static Geometrie kompaktkassette() {
        Geometrie g;
        g.vorspann_um = 80'000;
        g.marken_abstand_um = 0;
        g.marke_um = 0;
        return g;
    }
    bool operator==(const Geometrie& o) const {
        return laenge_um == o.laenge_um && vorspann_um == o.vorspann_um && marken_abstand_um == o.marken_abstand_um &&
               marke_um == o.marke_um && initialluecke_um == o.initialluecke_um;
    }
};

class KassettenMedium {
public:
    /// Vorgabedichte in 1/1000 Bit/mm (31,5 Bit/mm).
    static constexpr uint32_t kDichteVorgabe = 31'500;
    /// Vorgabe der Aufnahmegeschwindigkeit in mm/s (nur Angabe im Kopf der `.k52`).
    static constexpr uint32_t kAufnahmeVorgabe = 381;

    explicit KassettenMedium(Geometrie g = {}, uint32_t dichte_milli = kDichteVorgabe,
                             uint32_t aufnahme_mm_s = kAufnahmeVorgabe);

    const Geometrie& geometrie() const { return geo_; }
    uint32_t dichteMilli() const { return dichte_milli_; }
    uint32_t aufnahmeMmS() const { return aufnahme_mm_s_; }

    // ── Maße (µm) ──
    /// Bandlänge von @p n Bytes (auf µm gerundet).
    int64_t laengeVonBytes(size_t n) const;
    int64_t botAnfang() const;        ///< Anfang der BOT-Marke
    int64_t botEnde() const;          ///< Ende der BOT-Marke = frühester beschreibbarer Punkt
    int64_t eotAnfang() const;        ///< Anfang der EOT-Marke
    int64_t eotEnde() const;          ///< Ende der EOT-Marke
    int64_t magnetbandEnde() const;   ///< Ende des Magnetbands (Anfang des Endvorspanns)

    /// Liegt an der Seitenkoordinate @p sensor_um Vorspann oder eine Marke unter der Lichtschranke
    /// (`AEB` aktiv, Bit 7 der Steuer-PIO A = 0; treiber.md §3.1)?
    bool aebAktiv(int64_t sensor_um) const;

    // ── Seiten und Position ──
    Seite aktiveSeite() const { return seite_; }
    /// Kassette wenden: andere Seite, Position spiegelt sich (`laenge − p`).
    void wenden();
    /// Position des Kopfes in Koordinaten der aktiven Seite (0 … laenge).  Gehört zur Kassette,
    /// nicht zur Datei: sie steht nicht in der `.k52`.
    int64_t positionUm() const { return pos_um_; }
    void setPosition(int64_t um);

    // ── Inhalt je Seite ──
    size_t anzahlBloecke(Seite s) const { return seiten_[idx(s)].bloecke.size(); }
    const Block& block(Seite s, size_t i) const { return seiten_[idx(s)].bloecke.at(i); }
    int64_t blockStart(Seite s, size_t i) const;
    int64_t blockEnde(Seite s, size_t i) const;
    /// Index des Blocks, der @p pos_um überdeckt (Start ≤ pos < Ende), sonst −1.
    int blockBei(Seite s, int64_t pos_um) const;
    /// Index des ersten Blocks mit Start ≥ @p pos_um (= anzahl, wenn keiner).
    size_t ersterBlockAb(Seite s, int64_t pos_um) const;
    /// Index des letzten Blocks mit Ende ≤ @p pos_um, sonst −1.
    int letzterBlockBis(Seite s, int64_t pos_um) const;
    /// Ende des letzten Blocks (oder das BOT-Ende, wenn die Seite leer ist).
    int64_t datenEnde(Seite s) const;

    /**
     * @brief Block ab Seitenkoordinate @p start_um schreiben (überschreibt, was darunter liegt).
     *
     * Alle Blöcke, die sich mit `[start, start+Länge)` überlappen, sind danach zerstört und
     * entfallen; spätere Blöcke behalten ihre Lage auf dem Band.  Schreibschutz prüft das
     * Laufwerk, nicht das Medium.  @return false bei leeren/zu langen Bytes, vor dem Ende der
     * BOT-Marke oder über das Ende des Magnetbands hinaus (Medium dann unverändert).
     */
    bool schreibeBlock(Seite s, int64_t start_um, const std::vector<uint8_t>& bytes);
    /// Block mit der Lücke @p luecke_um hinter den letzten anhängen (Aufbau in Werkzeugen/Codec).
    /// false, wenn er nicht mehr aufs Band passt.
    bool haengeAn(Seite s, uint32_t luecke_um, const std::vector<uint8_t>& bytes);
    /// Magnetisiertes Bandstück `[von, bis)` ohne Daten: zerstört überlappende Blöcke.
    /// @return Zahl der entfernten Blöcke.
    size_t loesche(Seite s, int64_t von_um, int64_t bis_um);
    /// Ganze Seite leeren (nur Blöcke; der Schreibschutz bleibt).
    void leereSeite(Seite s);

    bool schreibgeschuetzt(Seite s) const { return seiten_[idx(s)].schreibschutz; }
    void setSchreibschutz(Seite s, bool an);

    /// Zähler, der bei jeder inhaltlichen Änderung steigt (Blöcke, Schreibschutz) — Grundlage
    /// des verzögerten Zurückschreibens.  Position und Wenden zählen nicht.
    uint64_t revision() const { return revision_; }

    /// Gleicher Inhalt, gleiche Geometrie/Dichte/Schreibschutz (nicht die Position).
    bool gleichInhalt(const KassettenMedium& o) const;

private:
    struct SeitenInhalt {
        std::vector<Block> bloecke;
        bool schreibschutz = false;
        mutable std::vector<int64_t> starts;  ///< Cache: Start je Block
        mutable bool cache_gueltig = false;
    };
    static size_t idx(Seite s) { return static_cast<size_t>(s); }
    const std::vector<int64_t>& starts(Seite s) const;
    void aenderung(Seite s);

    Geometrie geo_;
    uint32_t dichte_milli_;
    uint32_t aufnahme_mm_s_;
    Seite seite_ = Seite::A;
    int64_t pos_um_ = 0;
    SeitenInhalt seiten_[2];
    uint64_t revision_ = 0;
};

}  // namespace kassette

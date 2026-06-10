/**
 * @file bit_codec.cpp
 * @brief BitCodec – MFM/FM-Bitzellen-Codec für das HFE-Floppy-Backend.
 *
 * Implementierung von @ref BitCodec::decode und @ref BitCodec::encode.
 * Arbeitet MSB-first intern; HFE-Bytes sind LSB-first und werden an der
 * Byte-Grenze gespiegelt (bytereverse8).
 *
 * Zellenmodell:
 *   - 1 Datenbyte = 16 Zellen, MSB-first als  c0 d0 c1 d1 … c7 d7
 *   - MFM: d_i = Datenbit; c_i = NOT(d_{i-1} OR d_i); A1-Sync = 0x4489
 *   - FM : c_i = 1 immer; Marken-Bytes mit Sondertakt (FE/FB/F8 → C7, FC → D7)
 *
 * @see core/peripherals/floppy_drive/bit_codec.h
 * @see https://github.com/keirf/greaseweazle  (image/hfe.py, codec/ibm/ibm.py)
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/bit_codec.h"
#include <cassert>
#include <cstring>

namespace {

// ─── Hilfsfunktion: Byte bitspiegeln (LSB↔MSB) ───────────────────────────────

/// HFE-Bytes sind LSB-first; diese Funktion wandelt zwischen HFE- und interner
/// MSB-first-Darstellung um (bijektiv: bitreverse8(bitreverse8(x)) == x).
static inline uint8_t bitreverse8(uint8_t x) {
    x = static_cast<uint8_t>(((x & 0xAAu) >> 1) | ((x & 0x55u) << 1));
    x = static_cast<uint8_t>(((x & 0xCCu) >> 2) | ((x & 0x33u) << 2));
    x = static_cast<uint8_t>(((x & 0xF0u) >> 4) | ((x & 0x0Fu) << 4));
    return x;
}

// ─── Bit-Buffer-Hilfsfunktionen ───────────────────────────────────────────────

/// Schreibt @p count Bits aus dem MSB-first-Wert @p val in den Ausgabepuffer.
/// Bits werden MSB-first in aufsteigende Byte-/Bit-Positionen gesetzt.
static void pushBits(std::vector<uint8_t>& buf, uint32_t val, int count,
                     int& bitpos) {
    for (int i = count - 1; i >= 0; --i) {
        int byteIdx = bitpos / 8;
        int bitIdx  = 7 - (bitpos % 8);
        if (byteIdx >= static_cast<int>(buf.size()))
            buf.push_back(0);
        if ((val >> i) & 1u)
            buf[static_cast<size_t>(byteIdx)] |= static_cast<uint8_t>(1u << bitIdx);
        ++bitpos;
    }
}

/// Liest ein Bit aus dem MSB-first-Bitzellenstrom @p stream bei Position @p pos.
static inline bool getBit(const std::vector<uint8_t>& stream, uint32_t pos) {
    uint32_t byteIdx = pos / 8;
    int      bitIdx  = 7 - static_cast<int>(pos % 8);
    return (stream[byteIdx] >> bitIdx) & 1u;
}

/// Liest ein 16-Bit-Zellwort MSB-first aus @p stream ab Bitposition @p pos.
static uint16_t get16(const std::vector<uint8_t>& stream, uint32_t pos,
                      uint32_t total_bits) {
    uint16_t w = 0;
    for (int i = 0; i < 16; ++i) {
        w = static_cast<uint16_t>(w << 1);
        if (pos + static_cast<uint32_t>(i) < total_bits)
            if (getBit(stream, pos + static_cast<uint32_t>(i)))
                w |= 1u;
    }
    return w;
}

// ─── MFM-Zellencodierung ──────────────────────────────────────────────────────

/// Encodiert @p data_byte als reguläres MFM-Zellwort (16 Bit, MSB-first).
/// @p prev_d_bit ist das letzte Datenbit des Vorgänger-Bytes (über Bytegrenzen
/// fortgeführt); wird auf das letzte Datenbit dieses Bytes aktualisiert.
static uint16_t mfm_cell_word(uint8_t data_byte, bool& prev_d_bit) {
    uint16_t w = 0;
    bool prev  = prev_d_bit;
    for (int i = 7; i >= 0; --i) {
        bool d = (data_byte >> i) & 1u;
        bool c = !(prev || d);     // MFM-Clock-Regel
        w = static_cast<uint16_t>(w << 2);
        if (c) w |= 2u;
        if (d) w |= 1u;
        prev = d;
    }
    prev_d_bit = prev;
    return w;
}

/// Decodiert die 8 Daten-Bits aus einem 16-Bit-MFM-Zellwort.
/// Die Daten-Bits liegen an den ungeraden Positionen (Bits 14,12,10,8,6,4,2,0).
static uint8_t mfm_decode_word(uint16_t w) {
    uint8_t b = 0;
    for (int i = 7; i >= 0; --i) {
        b = static_cast<uint8_t>(b << 1);
        if ((w >> (i * 2)) & 1u)
            b |= 1u;
    }
    return b;
}

// ─── FM-Zellencodierung ───────────────────────────────────────────────────────

/// Encodiert @p data_byte als reguläres FM-Zellwort: Clock-Bits immer 1.
static uint16_t fm_cell_word(uint8_t data_byte) {
    uint16_t w = 0;
    for (int i = 7; i >= 0; --i) {
        bool d = (data_byte >> i) & 1u;
        w = static_cast<uint16_t>(w << 2);
        w |= 2u;   // Clock immer 1
        if (d) w |= 1u;
    }
    return w;
}

/// Encodiert ein FM-Marken-Byte mit Sonder-Clock als Zellwort.
/// Verschränkt @p clock_byte und @p data_byte bitweise (MSB-first):
///   c7 d7 c6 d6 … c0 d0
static uint16_t fm_sync_word(uint8_t data_byte, uint8_t clock_byte) {
    uint16_t w = 0;
    for (int i = 7; i >= 0; --i) {
        bool c = (clock_byte >> i) & 1u;
        bool d = (data_byte  >> i) & 1u;
        w = static_cast<uint16_t>(w << 2);
        if (c) w |= 2u;
        if (d) w |= 1u;
    }
    return w;
}

/// Decodiert die Daten-Bits aus einem FM-Zellwort (identisch zu MFM).
static inline uint8_t fm_decode_word(uint16_t w) {
    return mfm_decode_word(w);
}

/// Extrahiert die Clock-Bits (gerade Positionen) aus einem FM-Zellwort.
static uint8_t fm_extract_clock(uint16_t w) {
    uint8_t c = 0;
    for (int i = 7; i >= 0; --i) {
        c = static_cast<uint8_t>(c << 1);
        if ((w >> (i * 2 + 1)) & 1u)
            c |= 1u;
    }
    return c;
}

// ─── Bekannte FM-Sync-Zellwörter (für Sync-Suche in decode) ──────────────────

// FM-Marken-Bytes mit ihren Sonder-Clock-Bytes:
//   FE/FB/F8 → Clock 0xC7   (IDAM / DAM / Gelöschter DAM)
//   FC       → Clock 0xD7   (IAM Index-Adressmarke)
static const uint16_t kFmSyncFE = fm_sync_word(0xFE, 0xC7);  // IDAM
static const uint16_t kFmSyncFB = fm_sync_word(0xFB, 0xC7);  // DAM
static const uint16_t kFmSyncF8 = fm_sync_word(0xF8, 0xC7);  // gelöschter DAM
static const uint16_t kFmSyncFC = fm_sync_word(0xFC, 0xD7);  // IAM

/// Prüft, ob ein 16-Bit-Zellwort ein bekanntes FM-Sync-Wort ist.
static bool isFmSyncWord(uint16_t w) {
    return w == kFmSyncFE || w == kFmSyncFB || w == kFmSyncF8 || w == kFmSyncFC;
}

/// Liefert den MarkType für ein FM-Datenbyte (anhand Sonder-Clock erkannt).
/// @p clock ist das aus dem Zellwort extrahierte Clock-Byte.
static MarkType fmMarkFromClock(uint8_t data, uint8_t clock) {
    if (clock == 0xC7) {
        if (data == 0xFE) return MarkType::Id;
        if (data == 0xFB || data == 0xF8) return MarkType::Data;
    } else if (clock == 0xD7) {
        if (data == 0xFC) return MarkType::Index;
    }
    return MarkType::None;
}

/// Gap-Füllbyte für MFM (encodiertes 0x4E ergibt 0x9254 als Zellwort; als
/// direkter Füller im HFE-Byte-Stream wählen wir das kompakteste Muster).
/// Greaseweazle verwendet 0x88 als rohen Gap-Füller im Zellstrom.
/// Wir kodieren reguläre 0x4E-Bytes (passt zum Roundtrip-Verhalten).

}  // anonymous namespace

namespace BitCodec {

// ─── decode ───────────────────────────────────────────────────────────────────

TrackImage decode(const std::vector<uint8_t>& cells, uint32_t bitcell_count,
                  Encoding enc) {
    TrackImage result;
    result.encoding = enc;
    result.bitcells  = bitcell_count;

    if (cells.empty() || bitcell_count == 0)
        return result;

    // Schritt 1: Baue den internen MSB-first-Bitzellenstrom.
    // HFE-Bytes sind LSB-first → bitreverse8 je Byte.
    const uint32_t needed_bytes = (bitcell_count + 7) / 8;
    std::vector<uint8_t> stream;
    stream.reserve(needed_bytes);
    for (uint32_t i = 0; i < needed_bytes && i < cells.size(); ++i)
        stream.push_back(bitreverse8(cells[i]));
    // Falls cells kürzer als needed_bytes, mit 0 auffüllen
    while (stream.size() < needed_bytes)
        stream.push_back(0x00);

    if (enc == Encoding::MFM) {
        // ── MFM-Decode ────────────────────────────────────────────────────────

        // Schritt 2a: Suche bitweise erstes MFM-Sync-Zellwort (0x4489 oder 0x5224).
        // 0x4489 = A1-Sync (IDAM/DAM), 0x5224 = C2-Sync (IAM).
        // Wir sperren auf das früheste der beiden, damit auch Spuren mit IAM
        // vollständig decodiert werden.
        uint32_t lock_pos = bitcell_count;  // Startposition (noch nicht gefunden)
        uint32_t search_end = (bitcell_count >= 16) ? (bitcell_count - 16) : 0;
        for (uint32_t b = 0; b <= search_end; ++b) {
            uint16_t w = get16(stream, b, bitcell_count);
            if (w == 0x4489u || w == 0x5224u) {
                lock_pos = b;
                break;
            }
        }
        if (lock_pos == bitcell_count) {
            // Kein Sync gefunden — Rohdaten ohne Marken ausgeben
            // (leere Spur zurückgeben; realer HFE-Tracker würde trotzdem decodieren)
            return result;
        }

        // Schritt 2b: Ab lock_pos 16-Zellen-aligned schleifen.
        bool after_sync = false;  // true: nächstes nicht-Sync-Byte wird als Marken-Byte bewertet
        uint32_t pos = lock_pos;

        while (pos + 16 <= bitcell_count) {
            uint16_t w = get16(stream, pos, bitcell_count);
            pos += 16;

            if (w == 0x4489u) {
                // A1-Sync: Byte 0xA1 ohne Marke
                result.bytes.push_back(0xA1);
                result.marks.push_back(MarkType::None);
                after_sync = true;
            } else if (w == 0x5224u) {
                // C2-Sync (IAM): Byte 0xC2 ohne Marke
                result.bytes.push_back(0xC2);
                result.marks.push_back(MarkType::None);
                after_sync = true;
            } else {
                uint8_t b = mfm_decode_word(w);
                MarkType mt = MarkType::None;
                if (after_sync) {
                    // Das erste Nicht-Sync-Byte nach einem Sync-Block ist das Marken-Byte.
                    switch (b) {
                        case 0xFE: mt = MarkType::Id;    break;
                        case 0xFB: mt = MarkType::Data;  break;
                        case 0xF8: mt = MarkType::Data;  break;
                        case 0xFC: mt = MarkType::Index; break;
                        default:   mt = MarkType::None;  break;
                    }
                    after_sync = false;
                }
                result.bytes.push_back(b);
                result.marks.push_back(mt);
            }
        }

    } else {
        // ── FM-Decode ─────────────────────────────────────────────────────────

        // Schritt 3a: Suche erstes FM-Sync-Zellwort als Lock-Punkt.
        uint32_t lock_pos = bitcell_count;
        uint32_t search_end = (bitcell_count >= 16) ? (bitcell_count - 16) : 0;
        for (uint32_t b = 0; b <= search_end; ++b) {
            uint16_t w = get16(stream, b, bitcell_count);
            if (isFmSyncWord(w)) {
                lock_pos = b;
                break;
            }
        }
        if (lock_pos == bitcell_count)
            return result;

        // Schritt 3b: Ab lock_pos 16-Zellen-aligned schleifen.
        uint32_t pos = lock_pos;

        while (pos + 16 <= bitcell_count) {
            uint16_t w = get16(stream, pos, bitcell_count);
            pos += 16;

            uint8_t  d = fm_decode_word(w);
            uint8_t  c = fm_extract_clock(w);
            MarkType mt = fmMarkFromClock(d, c);

            result.bytes.push_back(d);
            result.marks.push_back(mt);
        }
    }

    return result;
}

// ─── encode ───────────────────────────────────────────────────────────────────

std::vector<uint8_t> encode(const TrackImage& track, uint32_t target_bitcells) {
    if (track.bytes.empty() || target_bitcells == 0) {
        const uint32_t out_bytes = (target_bitcells + 7) / 8;
        return std::vector<uint8_t>(out_bytes, 0x00);
    }

    const Encoding enc = track.encoding;
    const size_t n     = track.bytes.size();

    // Zielgröße des Ausgabepuffers (Bits, aufgerundet auf Bytes)
    const uint32_t target_bytes = (target_bitcells + 7) / 8;

    // Intern MSB-first arbeiten; am Ende bitreverse8 je Byte.
    std::vector<uint8_t> internal_buf;
    internal_buf.reserve(target_bytes + 4);
    int bitpos = 0;  // aktuelle Schreibposition in internal_buf

    auto emitWord = [&](uint16_t w) {
        pushBits(internal_buf, w, 16, bitpos);
    };

    if (enc == Encoding::MFM) {
        // Letztes Datenbit des Vorgänger-Bytes (für Clock-Berechnung über Grenzen)
        bool prev_d = false;

        // Hilfsfunktion: ist bytes[i] ein Sync-A1 (d. h. er gehört zur Sync-Gruppe
        // direkt vor einem markierten Byte)?  Sucht vorwärts: falls alle Bytes
        // i..j-1 == 0xA1 und marks[j] != None → ja.  Daten-A1 (innerhalb eines
        // Feldes) haben kein solches Marken-Byte in unmittelbarer Nähe.
        // Greaseweazle/HFE-Konvention: Sync-Gruppen haben max. 3 A1-Bytes.
        auto isSyncA1 = [&](size_t idx) -> bool {
            if (track.bytes[idx] != 0xA1) return false;
            // Vorwärtssuche: alle folgenden 0xA1 der Gruppe
            for (size_t j = idx; j < n && j <= idx + 3; ++j) {
                if (track.bytes[j] != 0xA1) {
                    // j ist das erste Nicht-A1 — ist es markiert?
                    return (track.marks[j] != MarkType::None);
                }
            }
            return false;
        };

        // Analog: ist bytes[i] ein Sync-C2 (Gruppe direkt vor IAM 0xFC)?
        auto isSyncC2 = [&](size_t idx) -> bool {
            if (track.bytes[idx] != 0xC2) return false;
            for (size_t j = idx; j < n && j <= idx + 3; ++j) {
                if (track.bytes[j] != 0xC2) {
                    return (track.bytes[j] == 0xFC &&
                            track.marks[j] == MarkType::Index);
                }
            }
            return false;
        };

        for (size_t i = 0; i < n; ++i) {
            const uint8_t  b  = track.bytes[i];
            const MarkType mt = track.marks[i];

            if (isSyncA1(i)) {
                // A1-Sync-Byte: Sonderzellwort 0x4489 (fehlendes Clock-Bit)
                emitWord(0x4489u);
                // Letztes Datenbit von 0xA1 = Bit0 = 1 → prev_d = true
                prev_d = true;
            } else if (isSyncC2(i)) {
                // C2-Sync-Byte (IAM): Sonderzellwort 0x5224
                emitWord(0x5224u);
                // Letztes Datenbit von 0xC2 = Bit0 = 0 → prev_d = false
                prev_d = false;
            } else {
                // Reguläres MFM-Byte (auch Mark-Bytes FE/FB/FC hinter den Syncs,
                // sowie Daten-0xA1/0xFE/0xFB, die keine Marke tragen sollen)
                (void)mt;
                emitWord(mfm_cell_word(b, prev_d));
            }
        }
    } else {
        // FM
        for (size_t i = 0; i < n; ++i) {
            const uint8_t  b  = track.bytes[i];
            const MarkType mt = track.marks[i];

            if (mt != MarkType::None) {
                // Marken-Byte mit Sonder-Clock
                uint8_t clock = (b == 0xFC) ? 0xD7u : 0xC7u;
                emitWord(fm_sync_word(b, clock));
            } else {
                emitWord(fm_cell_word(b));
            }
        }
    }

    // ── Auf target_bytes bringen ──────────────────────────────────────────────
    // Sicherstellen, dass internal_buf mindestens target_bytes lang ist
    while (internal_buf.size() < target_bytes)
        internal_buf.push_back(0);
    internal_buf.resize(target_bytes);  // bei Überlänge kürzen

    // Wenn internal_buf kürzer als target_bytes: mit Gap-Bytes auffüllen.
    // Wir kodieren Gap-Füllbytes (MFM: 0x4E, FM: 0xFF) regulär und hängen sie
    // hinten an, bis target_bytes erreicht ist.
    if (enc == Encoding::MFM) {
        // MFM-Gap: reguläres 0x4E-Zellwort, mit aktuellem prev_d = false (worst case)
        while (internal_buf.size() < target_bytes) {
            bool dummy = false;
            uint16_t gw = mfm_cell_word(0x4E, dummy);
            // Die nächsten 2 Bytes des Zellwortes anhängen
            if (internal_buf.size() < target_bytes)
                internal_buf.push_back(static_cast<uint8_t>(gw >> 8));
            if (internal_buf.size() < target_bytes)
                internal_buf.push_back(static_cast<uint8_t>(gw & 0xFF));
        }
        internal_buf.resize(target_bytes);
    } else {
        // FM-Gap: 0xFF mit vollem Clock
        while (internal_buf.size() < target_bytes) {
            uint16_t gw = fm_cell_word(0xFF);
            if (internal_buf.size() < target_bytes)
                internal_buf.push_back(static_cast<uint8_t>(gw >> 8));
            if (internal_buf.size() < target_bytes)
                internal_buf.push_back(static_cast<uint8_t>(gw & 0xFF));
        }
        internal_buf.resize(target_bytes);
    }

    // MSB-first → HFE-LSB-first: bitreverse8 je Byte
    for (auto& byte : internal_buf)
        byte = bitreverse8(byte);

    return internal_buf;
}

}  // namespace BitCodec

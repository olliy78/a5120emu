/**
 * @file k1520_export.h
 * @brief Sichtbarkeit der C-ABI-Funktionen — `K1520_API` vor jeder exportierten Funktion.
 *
 * Warum das nötig ist: unter Linux exportiert eine `.so` per Vorgabe **alles**, unter
 * Windows exportiert eine DLL per Vorgabe **nichts**.  Ohne Ausfuhrkennzeichnung baut
 * MSVC also eine leere `k1520core.dll`, und `ctypes` findet auf der Python-Seite keine
 * einzige Funktion — der Fehler fällt erst beim ersten Aufruf auf.
 *
 * Drei Fälle:
 *   - `K1520_BUILD_SHARED`  gesetzt beim Bau der Bibliothek selbst  → ausführen
 *   - `K1520_USE_SHARED`    gesetzt bei einem C/C++-Nutzer der DLL  → einführen
 *   - keins von beiden      statisch mitgebaut / Werkzeuge          → neutral
 *
 * Die Python-Seite braucht `K1520_USE_SHARED` nicht — `ctypes` sucht die Symbole zur
 * Laufzeit.  Der Fall ist für einen künftigen C-Nutzer da.
 *
 * @see doc/design/13_distribution.md §6.1
 */

#pragma once

#if defined(_WIN32)
#  if defined(K1520_BUILD_SHARED)
#    define K1520_API __declspec(dllexport)
#  elif defined(K1520_USE_SHARED)
#    define K1520_API __declspec(dllimport)
#  else
#    define K1520_API
#  endif
#elif defined(__GNUC__) || defined(__clang__)
/* Auch dann richtig, wenn die Bibliothek später mit -fvisibility=hidden gebaut wird. */
#  define K1520_API __attribute__((visibility("default")))
#else
#  define K1520_API
#endif

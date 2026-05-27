# Konfiguration der K1520-Karten im A5120

## K8025 – Serielle Schnittstelle

### DIP-Schalter A61

| Schalter | Position |
|:--------:|:--------:|
| 1 | EIN |
| 2 | AUS |
| 3 | EIN |
| 4 | AUS |
| 5 | EIN |
| 6 | AUS |
| 7 | EIN |
| 8 | AUS |

### DIP-Schalter A41

| Schalter | Position |
|:--------:|:--------:|
| 1 | EIN |
| 2 | AUS |
| 3 | AUS |
| 4 | AUS |
| 5 | EIN |
| 6 | AUS |
| 7 | EIN |
| 8 | AUS |

### DIP-Schalter A46

| Schalter | Position |
|:--------:|:--------:|
| 1 | AUS |
| 2 | EIN |
| 3 | EIN |
| 4 | AUS |

### Wicklelbrücke X7 / X8 / X9

| Wicklelbrücke | geschlossen | Hinweis |
|:------:|:--------:|---------|
| X9 | Nein | offen |
| X8 | Ja | mit X7 gebrückt |
| X7 | Ja | mit X8 gebrückt |

### Resultierende Funktionseinstellungen

| Funktion | Einstellung |
|----------|:-----------:|
| Übertragungsgeschwindigkeit DFÜ | 9600 Bit/s |
| Physische Blocklänge DFÜ | 1024 Bytes |
| Schirmerdung der Masseleitung | AKTIVIERT |
| IFSS Empfänger – Aktivmodus | AKTIVIERT |
| IFSS Sender – Passivmodus | AKTIVIERT |
| Empfangsschrittakt vom CTC | AKTIVIERT |
| Sendeschrittakt vom CTC | AKTIVIERT |
| Empfangsschrittakt von DÜE (V115) | NICHT AKTIVIERT |
| Sendeschrittakt von DÜE (V114 / FF A35-09) | NICHT AKTIVIERT |
| Niedere Übertragungsgeschwindigkeit DÜE | NICHT AKTIVIERT |
| Höhere Übertragungsgeschwindigkeit DÜE | AKTIVIERT |
| Sende-/Empfangsschrittakt DFÜ über IFSS | AKTIVIERT |
| Taktzuführung von Kanal 1 des CTC A34 | NICHT AKTIVIERT |

---

## K7024 – Bildschirmkarte

### Wicklelbrücke X11 / X12 (5-polig)

| Position | geschlossen |
|:--------:|:--------:|
| 1 | Ja |
| 2 | Ja |
| 3 | Ja |
| 4 | Ja |
| 5 | Ja |

### Wicklelbrücke X15 / X16 (2-polig)

| Position | geschlossen |
|:--------:|:--------:|
| 1 | Ja |
| 2 | Nein |

### Wicklelbrücke X13 / X14 (2-polig)

| Position | geschlossen |
|:--------:|:--------:|
| 1 | Ja |
| 2 | Nein |

### Resultierende Funktionseinstellungen

| Funktion | Einstellung |
|----------|:-----------:|
| Adressbereich Bildinhaltsspeicher | F800H – FFFFH |
| Kursor blinkend | NICHT AKTIVIERT |
| Kursor ruhend | AKTIVIERT |
| Lesesperre | AKTIVIERT |

> **Hinweis:** Die Lesesperre ist erforderlich, wenn vom Anwender die letzten 2 KByte des Gesamt-RAM-Speichers genutzt werden sollen.

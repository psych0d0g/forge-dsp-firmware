# Eine Firmware für alle Modelle? (Auswertung zu #31)

Stand 11.11.2026, gemessen an den generierten Dateien `src/flavours/*` (BASCOM 1.11.6000) und am Port-Build.

## Wie verschieden sind die 7 Varianten wirklich?

| Paar | Konstanten | Tabellen | Pins / Init / EEPROM-Layout | Unterschied |
|---|---|---|---|---|
| V40-96ELN ↔ V40-CORED8 | 5 (nur `MODEL_ID` und ungenutzte AES-Werte) | gleich | gleich (nur Zeilennummern im Kommentar) | nur die Kennung `96ELN` / `CORED8` |
| V40-CORED8 ↔ V41-CORED8 | 2 (`BOARD_VERSION`, `EXT_USBAUDIO_FW`) | gleich | gleich | nur die Kennung `4.0` / `4.1` und der Name der XMOS-Firmware (im Port ungenutzt, #42) |
| V40-96ELN-3 ↔ V41-96ELN-3 | 2 (wie oben) | gleich | gleich | wie oben |
| V40-96ELN ↔ V40-96ELN-3 | viele | 1369 Zeilen, u. a. DSP-Programm 2 | Pins gleich, EEPROM-Layout verschieden | DAC ES9018 ↔ ES9038Pro, anderes DSP-Programm |
| V30-96ELN ↔ V40-96ELN | 70 | Si5338-Tabellen, Events | verschieden | V40 mit Taktgenerator Si5338 und USB-Audio |
| V30-96LN ↔ Rest | viele | eigene DSP-Programme | verschieden | SRC43x2 + CS8416 statt CT7302, 8 Presets |

**Ergebnis: Funktional gibt es nur 4 verschiedene Firmwares.** CORED8 ist firmwareseitig ein 96ELN, und V41 ist dasselbe wie V40.

| Firmware | Varianten heute |
|---|---|
| A | V30-96LN |
| B | V30-96ELN |
| C | V40-96ELN, V40-CORED8, V41-CORED8 |
| D | V40-96ELN-3, V41-96ELN-3 |

## Platz für eine einzige Firmware

- Code des Ports (Bring-up, V40-96ELN): rund 69 KB, Tabellen und DSP-Programme rund 56 KB.
- Alle **verschiedenen** Tabelleninhalte der 7 Varianten zusammen: 131 KiB. Die DSP-Programme sind teils gleich: Programm 1 gibt es in 2 Fassungen, Programm 2 in 3.
- Eine gemeinsame Firmware käme auf rund 200–220 KB, wenn die heutigen `#if` zu Laufzeit-Abfragen werden; verfügbar sind 248 KB (256 KB minus Bootloader). **Es passt, aber knapp.** Für die Debug-Texte (#41) bliebe dann kaum Platz.

## Erkennung zur Laufzeit

| Unterscheidung | per Hardware erkennbar? |
|---|---|
| V30-96LN ↔ übrige | ja: SRC43x2/CS8416 statt CT7302, keine Si5338 |
| V30-96ELN ↔ V40-96ELN | ja: Si5338 (0xE0 an Bus 1) nur ab V40 |
| 96ELN ↔ 96ELN-3 | ja: ES9038Pro meldet eine andere Chip-ID als ES9018 (Bus 2, 0x90) |
| 96ELN ↔ CORED8 | **nein**: identische Hardware und Firmware, nur der Name |
| V40 ↔ V41 | **nein** per I2C; der Unterschied (XMOS-Firmware XHRA/XU208) liegt auf der USB-Audio-Platine |

Die Kennung (`system.key.info` = Vendor, Modell, Board) lässt sich also nur teilweise erkennen. Die GUI wählt darüber ihre Seiten, sie muss stimmen.

## Risiko

Eine falsche Erkennung bedeutet falsches EEPROM-Layout (Abstimmung zerstört) oder falsche Register in DAC/DSP. Absicherung wäre nötig: mehrfache Bestätigung, bei Zweifel der sichere Modus (nur Protokoll und Bootloader), und die einmal erkannte Variante im EEPROM festhalten.

## Empfehlung

1. **Jetzt: von 7 auf 4 Builds (A–D).** Kein Risiko, weil Code, Tabellen, Pins und EEPROM-Layout je Gruppe identisch sind. Modell und Board (`96ELN`/`CORED8`, `4.0`/`4.1`) kommen als **Kennung im EEPROM** dazu. Der Flasher schreibt sie beim ersten Flashen aus der Kennung, die die alte Firmware gemeldet hat (`system.key.info`), danach meldet die Port-Firmware genau diese. Fehlt sie, gilt die Vorgabe der Gruppe.
2. **Später, optional: eine Firmware für alle.** Platz reicht, Erkennung für A/B/C/D ist eindeutig per I2C; Modell/Board kommen wie unter 1 aus dem EEPROM. Aufwand: alle `BAS_*`-Konstanten, die heute Array-Größen und `#if` steuern, werden Laufzeitwerte, der Generator erzeugt Tabellen je Gruppe. Lohnt sich vor allem, wenn ein Update ohne Auswahl der Variante wichtig wird.

## Umsetzung (User: „7 → 4 Builds“)

Umgebungen `V30-96LN`, `V30-96ELN`, `V4x-96ELN` (96ELN, CORED8), `V4x-96ELN-3`. Die Kennung wählt `src/identity.c` aus
der Liste des Builds; gespeichert ab EEPROM 0xF00 (Magic 0xA5, Index). Ohne Eintrag gilt die generierte Variante.
Der Upload liest die Kennung der laufenden Firmware (`--expect-device` mit Liste) und setzt sie nach dem Flashen
(`--keep-identity`). *Forge: flash release 1.11.6000* fragt die Kennung ab und wählt die passende Original-HEX.


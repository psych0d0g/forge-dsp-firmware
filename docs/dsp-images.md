# TAS3108-Programme (EEPROM-Boot-Images)

Stand der Analyse zu #43. Werkzeug: `tools/dsp_image.py` (list, extract, check), Test: `tests/test_dsp_image.py`.

## Format

Quelle: TAS3108-Datenblatt SLES152B, Abschnitt 6, Tabelle 6-5. Befehlssatz des DSP: SLEU067A
(TAS3108/TAS3108IA Audio DSP Instruction Set Reference Guide).

Das EEPROM enthält eine Folge von Blöcken:

| Byte | Inhalt |
|---|---|
| 0–1 | Prüfsumme (Big Endian): untere 16 Bit der Bytesumme ab Byte 2 bis vor die Prüfsumme am Ende |
| 2–3 | Kennung `0x001F` |
| 4 | Speicher: 0 = 8051-Programm (oder End-Block), 1 = 8051 XDATA, 2 = DSP-Programm, 3 = DSP-Koeffizienten, 4 = DSP-Daten |
| 5 | 0 |
| 6–7 | Startadresse in Worten des Zielspeichers (Big Endian) |
| 8–9 | Länge des Blocks in Byte (Big Endian) = 12 + Daten + ein Wort Abschluss |
| 10–11 | 0 |
| 12… | Daten, Worte Little Endian: 4 Byte (8051, Koeffizienten, 28 Bit gültig), 6 Byte (Daten, 48 Bit), 7 Byte (Programm, 54 Bit) |
| Ende | ein Wort: Nullen, in den letzten 2 Byte die Prüfsumme noch einmal |

Der End-Block hat Speicher 0, Adresse 0 und Länge 0. In den Forge-Images steht dort die Prüfsumme `0000`, danach
folgen 8 Null-Bytes. Alle 14 Images (7 Varianten × 2 Programme) haben gültige Prüfsummen und lassen sich mit
`dsp_image.py` bitgleich wieder zusammensetzen (CI).

## Aufbau der Programme (V40-96ELN)

| Block | Programm 1 (Kern 1) | Programm 2 (Kern 2 und 3) |
|---|---|---|
| 8051-Programm 0x0000 | 2575 Worte (10 300 Byte) | 2575 Worte |
| 8051-Programm 0x2B00 | 28 Worte | 16 Worte |
| 8051-Programm 0x2C00 | 256 Worte | 256 Worte |
| 8051 XDATA 0x03F8 | 2 Worte | 2 Worte |
| DSP-Programm 0x0000 | 1402 Befehle | 1262 Befehle (96ELN-3: 1282) |
| DSP-Koeffizienten | 148 Blöcke | 197 Blöcke (96ELN-3: 198) |
| DSP-Daten | 1–2 Blöcke | 1–2 Blöcke |

Der 8051-Teil ist in allen 96ELN-/CORED8-Varianten gleich lang. Beim 96LN hat der erste Block 2738 Worte.

## Koeffizienten

- Die meisten Koeffizienten-Blöcke bestehen aus einem einzigen Wort `0x00800000` (1,0 im Format 5.23) im Abstand von
  5 Adressen: b0 = 1 eines Biquads, die übrigen vier Koeffizienten bleiben 0. Die Biquads starten also als
  Durchgang. Programm 1 enthält 143 davon, Programm 2 195.
- Die Firmware kennt 144 Biquad-Register für Kern 1 und je 196 für Kern 2 und 3 (`data_flow_dsp_bq_reg`,
  I2C-Subadressen 0x1F–0xB6 bzw. 0x1F–0xEF). Der jeweils fehlende Biquad steckt vermutlich in einem der Blöcke mit
  mehreren Worten (z. B. 0x0179, 17 Worte, mit echten Filterkoeffizienten).
- Weitere Blöcke mit mehreren Worten: Steuerworte ab 0x0000, Pegel und Mixer (`0x00000017`, `0x00200000` = 0,25,
  `0x00800000` = 1,0).

## Offen

- Abbildung I2C-Subadresse → Koeffizientenadresse (das Register-Mapping des 8051-Programms).
- Disassembler für das DSP-Programm (54-Bit-Befehle, SLEU067A Anhang A) und Signalfluss je Kern.
- Unterschiede zwischen den Fassungen (96ELN ↔ 96ELN-3 Programm 2: 20 Befehle mehr, ein Koeffizienten-Block mehr).
- Der 8051-Teil (Firmware des Mikrocontrollers im TAS3108) ist 8051-Code und lässt sich mit üblichen Werkzeugen
  disassemblieren.

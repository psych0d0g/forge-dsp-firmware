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

## Abbildung Biquad-Register → Koeffizienten (Kern 1, belegt)

Kern 1: **Koeffizientenadresse = 0x16 + 5 × (I2C-Subadresse − 0x1F)**, je 5 Worte (b0, b1, b2, a1, a2 in der
Reihenfolge des I2C-Schreibens).
Beleg: Die Register 0x1F–0x66 und 0x6F–0xB6 bilden zwei zusammenhängende Gruppen mit einer Lücke von 8 Registern
(0x67–0x6E). Die b0-Worte = 1,0 liegen ab 0x16 im Abstand 5. Das letzte der ersten Gruppe (0x66 → 0x179) steckt im
17-Wort-Block, danach fehlen genau 8 Positionen (0x17E–0x19C), und ab 0x1A1 (→ 0x6F) geht es weiter.
Die 8 Plätze dazwischen sind keine Biquads mit Durchgangs-Vorgabe; der 17-Wort-Block enthält dort echte
Filterkoeffizienten.

Kern 2/3 (Programm 2): Die b0-Worte liegen ab 0x11 lückenlos bis 0x3CC (188 Stück), dann folgen 0x3D5, 0x3DA,
0x3DF und 0x3E4. Die Register sind 0x1F–0xC7 (169) und 0xD5–0xEF (27). Diese Aufteilung passt nicht linear zu den
Koeffizienten; die Abbildung steht vermutlich in einer Tabelle des 8051-Programms und ist noch offen.

## Offen

- Abbildung I2C-Subadresse → Koeffizientenadresse für Programm 2 (Register-Tabelle im 8051-Programm suchen).
- Disassembler für das DSP-Programm (54-Bit-Befehle, SLEU067A Anhang A) und Signalfluss je Kern.
- Unterschiede zwischen den Fassungen (96ELN ↔ 96ELN-3 Programm 2: 20 Befehle mehr, ein Koeffizienten-Block mehr).
- Der 8051-Teil (Firmware des Mikrocontrollers im TAS3108) ist 8051-Code und lässt sich mit üblichen Werkzeugen
  disassemblieren.

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
  5 Adressen: b0 = 1 eines Biquads, die übrigen vier Koeffizienten bleiben 0. Alle Biquads, die die MCU per I2C
  erreicht (144 in Kern 1, 196 in Kern 2/3), starten so als Durchgang.
- Weitere Blöcke mit mehreren Worten: Steuerworte ab 0x0000, ein fest eingestellter Filterblock in Programm 1
  (0x0179–0x019D, nicht per I2C erreichbar), Pegel und Mixer (siehe unten).

## Registertabelle (I2C-Subadresse → Speicher)

Die 8051-Firmware im TAS3108 setzt I2C-Zugriffe über eine Tabelle um: Block 8051-Programm 0x2C00, 256 Einträge zu
4 Byte, einer je Subadresse: `[Adresse High, Adresse Low, Länge in Byte, Art]`. Art 1 = DSP-Koeffizientenspeicher
(Adresse in Worten, Länge / 4 Worte). Die anderen Arten behandelt der 8051-Code selbst (Adresse 0).
Ausgabe: `dsp_image.py regs IMAGE.bin`. Der Test prüft für alle Fassungen, dass jedes Biquad- und Pegelregister
der MCU-Firmware dort ein Koeffizienten-Eintrag passender Länge ist.

Der 8051-Code (Block 0x0000) ist in Programm 1 und 2 gleich; die Programme unterscheiden sich im 8051-Teil nur in
dieser Tabelle und im Block 0x2B00.

V40-96ELN (96ELN-3, CORED8 und V30-96ELN gleich, Abweichungen unten):

| Subadresse | Programm 1 (Kern 1) | Programm 2 (Kern 2 und 3) | Verwendung in der MCU-Firmware |
|---|---|---|---|
| 0x00 | Art 5 | Art 5 | |
| 0x02 | Art 6 | Art 6 | Status (`BAS_DSP_SUB_STATUS`) |
| 0x06 / 0x07 | Art 3 / Art 4 (8 Byte) | gleich | |
| 0x08 | Koeffizient 0x010 (Wert 0x0006E1BA) | Koeffizient 0x010 (0) | Version (`BAS_DSP_SUB_VERSION`) |
| 0x0B / 0x0C / 0x0F | Art 7 / 8 / 0x0B | gleich | |
| 0x1F–0x66 | Biquads 0x011 + 5 × (Subadresse − 0x1F) | Biquads 0x011 + 5 × (Subadresse − 0x1F) bis 0xC7 | `data_flow_dsp_bq_reg` |
| 0x67–0x6E | frei | (in der Folge oben) | |
| 0x6F–0xB6 | Biquads, gleiche Formel (0x1A1–0x308) | (in der Folge oben) | |
| 0xC8–0xCF | Art 0x0C | Art 0x0C | |
| 0xD0–0xD3 | Art 0x10 | Art 0x10 | Delay (`data_flow_pair_delay_*_reg`) |
| 0xD4 | Art 0x0D | Art 0x0D | Delay übernehmen (`BAS_DSP_SUB_DELAY_ACK`) |
| 0xD5–0xDA | Eingangspegel 0x35E–0x363, 1 Wort | Biquads 0x35E + 5 × (Subadresse − 0xD5) bis 0xEB | `data_flow_dsp_input_vol_reg` (Kern 1), `bq_reg` (Kern 2/3) |
| 0xDB–0xF6 | Mixer 0x364 + 4 × (Subadresse − 0xDB), je 4 Worte | 0xEC–0xEF: Biquads 0x3D5 + 5 × (Subadresse − 0xEC) | `data_flow_mix_dsp_reg` (Kern 1) |
| 0xF7 | Koeffizient 0x3D4 (Wert 1) | frei | |
| 0xF8 | Koeffizienten 0x3D5–0x3D6 (0,01 / 0,99, ein Glättungspaar) | frei | |
| 0xF9 | Art 0x62, Adresse 0x0420 | frei | |
| 0xFA–0xFD | frei | Ausgangspegel 0x3D1–0x3D4, 1 Wort | `data_flow_dsp_output_vol_reg` |

Die vier Ausgangspegel liegen in Programm 2 zwischen den Biquads (0x3D1–0x3D4); daher die Lücke in der Folge der
b0-Worte (0x3CC → 0x3D5). Die Biquads von Kern 2/3 sind also zwei lineare Folgen: Subadresse 0x1F–0xC7 und 0xD5–0xEF,
beide mit 5 Worten Abstand, die zweite ab 0x35E. Eingangs- und Ausgangspegel haben als Vorgabe `0x00000017`.
Die Mixer-Zeilen 0xDB–0xEE stehen auf 0, 0xEF–0xF6 auf `[1,0, 0, 0, 0,25]`.

Abweichungen: 96ELN-3 (V40/V41) Programm 2 hat zusätzlich 0xF0–0xF3 → 0x3E9–0x3EC (je 1 Wort). V30-96LN hat eine
eigene Aufteilung (Kern 1: 0x1F–0x84, 0x8D–0xA6, 0xAB–0xC4; Kern 2 mit mehreren Teilfolgen), `dsp_image.py regs`
zeigt sie.

Korrektur: Eine frühere Fassung dieses Dokuments leitete für Kern 1 0x16 + 5 × (Subadresse − 0x1F) aus den
b0-Worten ab. Die Tabelle zeigt 0x011 als Start; der erste Biquad beginnt also bei 0x011, nicht bei 0x016.

## Offen

- Bedeutung der vom 8051-Code behandelten Arten (3–0x10, 0x62); dazu den 8051-Code disassemblieren.
- Disassembler für das DSP-Programm (54-Bit-Befehle, SLEU067A Anhang A) und Signalfluss je Kern.
- Unterschiede zwischen den Fassungen (96ELN ↔ 96ELN-3 Programm 2: 20 Befehle mehr, ein Koeffizienten-Block mehr).
- Der 8051-Teil (Firmware des Mikrocontrollers im TAS3108) ist 8051-Code und lässt sich mit üblichen Werkzeugen
  disassemblieren.

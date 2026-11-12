# TAS3108-Programme (EEPROM-Boot-Images)

Stand der Analyse zu #43. Werkzeuge: `tools/dsp_image.py` (list, extract, check, regs) und
`tools/tas3108_dis.py` (Disassembler), `tools/tas3108_flow.py` (Signalfluss), `tools/c51_dis.py` (8051),
Test: `tests/test_dsp_image.py`.

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

## Disassembler

`tools/tas3108_dis.py IMAGE.bin [--regs]` gibt das DSP-Programm Befehl für Befehl in der Schreibweise von SLEU067A
aus: `ALU1 | ALU2 | MOP1 | MOP2 | MOP3`. Mit `--regs` werden Koeffizientenadressen nach der Registertabelle benannt,
z. B. `C_013<s1F.b2>`. Felder des 54-Bit-Worts: ALU1 b53–49, ALU2 b48–42, MOP1/AD1 b41–37/b36–27 (Daten laden),
MOP2/AD2 b26–24/b23–14 (Koeffizient laden), MOP3/AD3 b13–10/b9–0 (Speichern, bei Sprüngen das Ziel, sonst mit
MOP3 = 0 `DLYPTR`).

Alle 14 Programme dekodieren vollständig bis auf einen Code (Test). Abweichungen von SLEU067A, aus den Programmen
belegt:

- ALU2 `CLRACC` ist 100 1101 (0x4D). Das Handbuch nennt 101 1101, das ist dort aber auch `ADD(BR,MR,CLP32,DO7)`.
  Im Ein-/Ausgabeblock jedes Programms steht 0x4D genau dort, wo das Beispiel im Handbuch (Tabelle 5) `CLRACC` hat.
- ALU1 `SHL(2)` ist 01011. Das Handbuch nennt 00111, den Code von `SHR(2)`; die Folge ist SHR(1–4) = 6–9,
  SHL(1–4) = 10–13.
- ALU2 0x4F (100 1111) steht nicht im Handbuch. Er kommt nur im Pegel-Block vor, immer zusammen mit `SHR(4)` direkt
  nach `ADD(ACC,MR,NONE,B)`, 6× in Programm 1, 4× in Programm 2. Die Ausgabe zeigt ihn als `ALU2?0x4f`.
  Vermutung: Die ADD-Codes tragen das Ziel in den unteren 3 Bit (001 MD, 010 B, 011 L, 100 MC, 101 ACC,
  111 DLYI); `CLRACC` = 100 1101 ist dann „Null → ACC“ und 0x4F = 100 1111 „Null → DLYI“. An diesen Stellen
  folgt kein Zugriff auf die Delay-Leitung, der Datenpfad bleibt also unberührt.

Erste Befunde (V40-96ELN):

- Jedes Programm beginnt mit `JMP 0x00C`. Bei 0x004 steht `STOP`, bei 0x008 `JMP 0x004`; dort endet jeder
  Durchlauf und wartet auf das nächste Sample.
- 0x00C–0x015: Ein-/Ausgabe wie Tabelle 5 des Handbuchs: `ST(DI,DATA,0..7)` liest die acht Eingangskanäle,
  `ADD(ACC,LR,CLP32,DO1..8)` gibt die im vorigen Durchlauf berechneten Ausgänge aus (Daten 0x163–0x16A in Programm 1).
- Delay: `DLYPTR(n)` mit `ADD(ACC,LR,NONE,DLYI)`, 14 Befehle später `ST(DLYO,DATA,…)`, wie im Handbuch gefordert.
- Pegel (0x027–0x044): Der Pegel ist eine lineare Verstärkung im Format 5.23. Die MCU nimmt sie aus
  `data_dsp_vol` (256 Werte in 0,5-dB-Schritten: Index 223 = 1,0 = 0 dB, 255 = +16 dB, 2 ≈ −110 dB, 0/1 = aus).
  Die Vorgabe im Image 0x17 entspricht etwa −111 dB, also stumm, bis die MCU den gespeicherten Pegel schreibt.
  Der DSP glättet den Wert gegen Knacken: um 6 Bit nach links (mehr Auflösung), vier Tiefpässe erster Ordnung mit
  α = 1/128 (`C_002` ≈ 0,0078, `C_004` ≈ 0,992 = 1 − α, Zustände D_16F–D_172), wieder 6 Bit nach rechts. Das Ergebnis
  schreibt er selbst als Verstärkung nach `C_008` (`ST(B,COEF,…)`) und multipliziert damit das verzögerte
  Eingangssignal (D_15D → D_14D). Ausgangspegel `sFA`–`sFD` in Kern 2/3 werden ebenso behandelt.
- Biquad: fünf Multiplikationen mit Akkumulation in der Reihenfolge b2, b1, b0, a1, a2 (Direktform I, Zustände im
  Datenspeicher), z. B. ab 0x1D0 für `s1F`.

## Signalfluss

`tools/tas3108_flow.py IMAGE.bin [--dot]` führt ein Programm für ein Sample symbolisch aus. Jedes Register und
Datenwort trägt die Menge seiner Quellen: Eingänge (`in1`–`in8` = SDIN1L–SDIN4R), Delay-Leitungen (`dlyN`),
Datenwörter, die vorher im selben Sample geschrieben wurden, und die Subadressen der Koeffizienten, mit denen
multipliziert wurde. Ausgabe je Datenwort: `D_01D <- D_00C [1F-28]` heißt, D_01D entsteht aus D_00C über die
Subadressen 0x1F–0x28. Werte aus dem vorigen Sample (Biquad-Zustände, `~D_xxx`) blendet das Werkzeug aus, damit die
Rückkopplung den Aufbau nicht verdeckt. Der Test prüft für alle Fassungen, dass die Analyse jedes Biquad-, Pegel-
und Phasenregister der MCU-Firmware erreicht.

V40-96ELN (V40/V41-CORED8 und V30-96ELN identisch), **Kern 1**:

1. Die acht Eingänge laufen je durch eine Delay-Leitung (16–23, Reihenfolge in5, in6, in3, in4, in1, in2, in8, in7).
2. Danach Eingangspegel `sD5`–`sDA` für in5, in6, in3, in4, in1, in2; in7 und in8 haben keinen Pegel.
3. Mixer (`sDB`–`sEE`): aus den acht Signalen entstehen acht Mischungen (D_155–D_15C) und zwei weitere Summen.
4. Die beiden Summen laufen durch je eine Kette aus 72 Biquads: `s1F`–`s66` und `s6F`–`sB6`.
5. Ausgang n = Mischung n und die beiden Ketten, gewichtet mit der Mixer-Zeile `sEF`+n−1 (4 Worte, Vorgabe
   `[1,0, 0, 0, 0,25]`). Die Ausgänge gehen erst im nächsten Sample hinaus (ein Sample Latenz je Kern).
6. Daneben ein Pegeldetektor: Summe der linken und der rechten Eingänge, Betrag, Glättung mit `sF8` (0,01/0,99),
   Ergebnis in D_021/D_022, vermutlich für die VU-Anzeige. Er nutzt den Koeffizienten 0x010 (`s08`); das ist das
   Register, das die MCU als „Version“ liest (`BAS_DSP_SUB_VERSION`, Kern 1: 0x0006E1BA, Kern 2/3: 0).

**Mixer von Kern 1 und Befehl `mixer.C.G`.** Die MCU sendet einen I2C-Puffer rückwärts (`i2c_wbuffer`:
`buffer_bytes[size]` bis `[1]`). Aus Wort 1..n (Little Endian) im Puffer werden so Wort n..1 (Big Endian) im DSP.
Bei Biquads legt die MCU `raw[1..5]` = a2, a1, b2, b1, b0 an (`BAS_DSP_BQ_CACHE_*`). Im Koeffizientenspeicher stehen
dann b0, b1, b2, a1, a2, genau wie der Biquad-Kern sie liest; a1/a2 sind negiert, weil der DSP sie addiert. Für
den Mixer gilt dasselbe je 4-Wort-Register: Speicherindex b (`data_flow_mix_cmd2array`) → Wort w = b − 1 →
Register `data_flow_mix_dsp_reg[w / 4]`, Koeffizient 3 − (w mod 4). Damit ergibt sich (Test für alle Fassungen):

| `mixer.C` | Ziel im DSP | | `mixer.C.G` | Quelle |
|---|---|---|---|---|
| 1 | Summe vor Kette 1 (`s1F`–`s66`) | | G1, G2 | in5, in6 (SDIN3) |
| 2 | Summe vor Kette 2 (`s6F`–`sB6`) | | G3, G4 | in3, in4 (SDIN2) |
| 3, 4 | Ausgang DO7, DO8 | | G5, G6 | in1, in2 (SDIN1) |
| 5, 6 | Ausgang DO5, DO6 | | G7, G8 | in8, in7 (SDIN4, vertauscht) |
| 7, 8 | Ausgang DO3, DO4 | | G9 | Kette 1 in den Ausgang (nur C3–C10) |
| 9, 10 | Ausgang DO1, DO2 | | G10 | Kette 2 in den Ausgang (C1: 0xFF = Eingangswahl) |

Der Anteil der eigenen Mischung am Ausgang (Wort 0 der Zeilen `sEF`–`sF6`) ist fest 1,0
(`data_flow_mix_passthrough`, `data_flow_mix_can_change` = 0). Das vierte Wort dieser Zeilen (Vorgabe 0,25) liest
das DSP-Programm nicht. Die Eingänge des Mixers sind die verzögerten und (außer in7/in8) im Pegel gestellten
Signale.

**Kern 2 und 3** (Programm 2), vier Kanäle, DO5–DO8 bleiben still:

| Eingang | Biquads | Anzahl | Delay | Ausgangspegel | Ausgang |
|---|---|---|---|---|---|
| in1 | `s1F`–`s44` | 38 | 16 | `sFA` | DO1 |
| in2 | `s45`–`s6A` | 38 | 17 | `sFB` | DO2 |
| in3 | `s6B`–`sA6` | 60 | 18 | `sFC` | DO3 |
| in4 | `sA7`–`sC7`, `sD5`–`sEF` | 60 | 19 | `sFD` | DO4 |

Nach `data_flow_dsp_output_vol_*` sind die Ausgangskanäle 1/2 (Kern 3) und 5/6 (Kern 2) die Ketten mit 60 Biquads
(`sFC`/`sFD`), die Kanäle 3/4 und 7/8 die mit 38.

96ELN-3 (V40/V41): Programm 2 hat zwischen Delay und Ausgangspegel je Kanal eine weitere Verstärkung
`sF0`–`sF3`. Das ist die Phasenumkehr (`data_flow_dsp_output_inv_reg`, nur in diesen Fassungen); daher die
20 Befehle und der Koeffizienten-Block mehr. V30-96LN ist anders aufgeteilt (Kern 1 mit eigenen Ketten nach dem
Mixer, Kern 2 mit fünf Ketten und drei Kanälen nur mit Pegel); `tas3108_flow.py` zeigt die Einzelheiten.

## 8051-Programm

`tools/c51_dis.py IMAGE.bin` disassembliert den 8051-Teil, rekursiv ab den Vektoren und den Handlern (Tabellen
bleiben Daten). SLEU067A Abschnitt 2 und Tabelle 1 beschreiben die Register zwischen 8051 und DSP bzw.
Volume-Update-Block (0xA6–0xD7); das Werkzeug benennt sie. SLEU067A nennt sie „ESFR“ im XDATA-Raum, die
Forge-Programme sprechen sie aber als direkte SFRs an (z. B. `MOV micro_addr_lo,R7`, `INC micro_addr_lo` mit
Übertrag nach `micro_addr_hi`). Die übrigen Register (I2C, Delay-Speicher, GPIO) stehen im Microprocessor Reference
Guide SLEU076, den TI nicht mehr veröffentlicht; sie erscheinen als `SFR_xx`.

Belegt aus dem Code:

- Koeffizient/Daten schreiben (Handler Art 1): DSP-Adresse nach `micro_addr_lo/hi` (Bit 10 wählt laut SLEU067A
  Koeffizienten- oder Datenspeicher), Wert nach `data_to_dsp0..`, dann `SETB 0xC0.6`, warten bis Bit 0xC8.5
  (in der Ausgabe `T2CON.5`) gelöscht ist. Bei mehreren Worten zählt der Handler `micro_addr` selbst hoch.
- Lesen: `SETB 0xC0.7`, warten auf 0xC8.5, Wert aus `to_micro0..3` (Big Endian in den Antwortpuffer).
- Den Volume-Update-Block (0xA6–0xAC) nutzt kein Forge-Programm; die Pegel glättet der DSP-Code selbst.

Ablauf einer I2C-Anfrage (V40-96ELN, beide Programme): Der Code bei 0x1272 liest aus der Registertabelle
(0x2C00 + 4 × Subadresse) die Zieladresse und die Art. Über die Art springt er in eine Handler-Tabelle bei 0x03ED
(4 Byte je Art: Lese-Handler, Schreib-Handler, Big Endian). Lese-Handler legen die Antwort in den internen RAM
0x38–0x3B. Der Test prüft für alle Fassungen, dass die Tabelle gefunden wird und dass alle Handler und der von dort
erreichbare Code ohne ungültige Opcodes dekodieren.

| Art | Subadressen | Lesen | Schreiben | Befund |
|---|---|---|---|---|
| 0 | unbenutzte | 0x2732 | 0x1D5D | Lesen reicht die Anfrage an 0x1FD0 weiter (Code 3) |
| 1 | Koeffizienten | 0x1EAC | 0x24C1 | Adresse und Länge aus der Registertabelle |
| 3 | 0x06 | 0x2719 | 0x1000 | |
| 4 | 0x07 (8 Byte) | 0x0BF8 | 0x2788 | Lesen mit Sprungtabelle über 10 Fälle |
| 5 | 0x00 | 0x27EE | 0x275F | Lesen: vier SFR-Bytes (0xA1, 0xA2, 0x96, 0x97), vermutlich Kennung |
| 6 | 0x02 Status | 0x26AD | 0x25E1 | Lesen: XDATA 0x15, 0x14, 0x99, 0x12; die MCU wertet ein Byte > 0 als Fehler |
| 7 | 0x0B | 0x2670 | 0x226C | Lesen: XDATA 0x97/0x96 |
| 8 | 0x0C | 0x23B4 | 0x27AC | Lesen: Zustandsbits aus SFR 0xC5, 0xC6 und Bit SFR_D8.1 sowie RAM 0x35/0x36/0x30 |
| 0x0B | 0x0F | 0x2732 | 0x245A | Lesen wie unbenutzte Subadressen |
| 0x0C | 0xC8–0xCF | 0x26C9 | 0x27DE | Schreiben: Wert 1 setzt ein Flag (RAM-Bit 0x21.4), sonst löschen; Lesen: XDATA je Subadresse |
| 0x0D | 0xD4 Delay-Ack | 0x2749 | 0x27AC | Lesen liefert das Flag 0x21.4 als 0/1; Schreiben reicht an 0x1FD0 weiter (Code 2) |
| 0x10 | 0xD0–0xD3 Delay | 0x26E4 | 0x2225 | Schreiben: Wert nach XDATA 0x7E/0x84–0x86, dann Bit 0 in SFR 0xC7 (Übergabe an die Delay-Hardware); Lesen: gespeicherte Werte |
| 0x62 | 0xF9 (Kern 1) | 0x20A5 | 0x1957 | Schreiben mit Sprungtabelle über 7 Fälle, Adresse 0x0420 |

## Offen

- 8051: Register für I2C, Delay und GPIO (SLEU076, nicht öffentlich) und damit die Arten 3, 4, 7, 8 und 0x62 im
  Einzelnen; Funktion 0x1FD0.
- ALU2 0x4F belegen (Vermutung „Null → DLYI“, siehe Disassembler).
- Die Analyse ist statisch (bedingte Sprünge werden nicht genommen); am Gerät gegenprüfen, z. B. Biquad setzen
  und den Ausgang messen.

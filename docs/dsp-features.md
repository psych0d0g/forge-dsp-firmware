# TAS3108: Funktionsumfang, Auslastung, Spielraum

Grundlage: Datenblatt SLES152B, Befehlssatz SLEU067A, Analyse der Forge-Programme mit `tools/tas3108_dis.py`,
`tools/tas3108_flow.py` und `tools/c51_dis.py` (Details in `docs/dsp-images.md`, #43). Zahlen für V40-96ELN; CORED8
und V30-96ELN sind gleich, 96ELN-3 wie angegeben.

## Was der Chip kann

| Bereich | TAS3108 |
|---|---|
| DSP-Takt | bis 135,2 MHz; bei 96 kHz **1408 Takte je Sample** (48 kHz: 2816, 192 kHz: 704) |
| Rechenwerk | je Takt fünf Operationen: ALU1 (Schieben, NEG/ABS, LOG2/ALOG2, Sprünge), ALU2 (Addieren/Vergleichen, 76-Bit-Akku), Laden Daten, Laden Koeffizient, Speichern; Multiplikation 28 × 48 Bit in jedem Takt |
| Programmspeicher | 3K Befehle zu 54 Bit (Grenze ist aber das Taktbudget, siehe unten) |
| Koeffizientenspeicher | 1022 Worte zu 28 Bit (Format 5.23) |
| Datenspeicher | 1022 Worte zu 48 Bit (Format 25.23) |
| Delay-Speicher | 5,88K Worte zu 24 Bit (bei 96 kHz rund 61 ms insgesamt, auf alle Delay-Leitungen verteilt) |
| Ein-/Ausgänge | 8 Kanäle rein, 8 raus (4 × SDIN, 4 × SDOUT), I2S/TDM, 16–32 Bit, 32–192 kHz |
| Sonderfunktionen | Volume-Update-Block (Hardware-Rampe), Zufallsgenerator (LFSR) für Dither, LOG2/ALOG2-Schätzer, bedingte Sprünge und Ladebefehle, GPIO, Bass/Höhen-Schnittstelle des 8051 |
| 8051 | 12K Code, 256 Byte IRAM, 2K XDATA; bedient I2C (Registertabelle, Handler), Delay-Zeiger, Status |

## Was die Forge-Programme nutzen

| | Kern 1 (Programm 1) | Kern 2 und 3 (Programm 2) |
|---|---|---|
| ausgeführte Takte je Sample | 1384 von 1408 (98,3 %) | 1244 von 1408 (88,4 %); 96ELN-3: 1264 |
| davon reine Wartetakte (alle fünf Felder NOP) | 116, davon 104 = 8 Delays × 13 Takte Wartezeit | 56, davon 52 = 4 × 13 |
| Biquads | 144 per I2C einstellbar + 2 fest (Pegelmesser) | 196 per I2C einstellbar (2 × 38 + 2 × 60) |
| Koeffizienten benutzt | 866 von 1022 | 991 von 1022 (96ELN-3: 995) |
| Datenworte benutzt | 402 von 1022 | 468 von 1022 |
| Delay-Leitungen | 8 (alle Eingänge) | 4 (alle Ausgänge) |
| Ausgänge | DO1–DO8 an Kern 2/3 | DO1–DO4, **DO5–DO8 ungenutzt** |

Funktionen im Signalweg:

- **Kern 1:** Eingangs-Delay je Kanal → Eingangspegel (6 Kanäle) → Mixer 8 × 10 → zwei Biquad-Ketten zu je 72 →
  Ausgangsmischung (Mischung + beide Ketten) → DO1–DO8. Daneben ein Spitzenwert-Pegelmesser: Maximum der vier
  linken bzw. rechten Eingänge nach Betrag (`COMP` + bedingtes Laden), zwei feste Biquads als Bewertungsfilter, Betrag,
  Glättung (`sF8`), `LOG2` → D_021/D_022 (logarithmischer Pegel, vermutlich für die VU-Anzeige/Signalerkennung).
- **Kern 2/3:** je Kanal eine Biquad-Kette (38 bzw. 60) → Delay → (96ELN-3: Phasenumkehr) → Ausgangspegel.
- Pegel: lineare Werte aus der MCU, im DSP geglättet (vier Tiefpässe, α = 1/128), nicht über den Volume-Update-Block.
- Latenz: ein Sample je Kern (Ausgabe am Anfang des nächsten Durchlaufs), dazu die eingestellten Delays.

Nicht genutzt: Volume-Update-Block, LFSR/Dither, ALOG2, bedingte Sprünge (BOC/BNC), GPIO des TAS3108, die
Bass/Höhen-Schnittstelle, DO5–DO8 von Kern 2/3, Abtastraten außer 96 kHz.

## Spielraum für Optimierungen

1. **Wartetakte nach `DLYPTR` füllen.** Zwischen `DLYPTR` und `ST(DLYO)` müssen 13 Takte liegen, die Programme
   füllen sie mit NOPs. Mit anderer Arbeit verschränkt gewinnt man 104 Takte in Kern 1 (≈ 20 Biquads) und 52 in
   Kern 2/3. Kern 1 hätte damit 128 freie Takte statt 24.
2. **Kern 2/3 hat Takte frei, aber kaum Koeffizienten.** 164 freie Takte + 52 Wartetakte ≈ 43 Biquads, der
   Koeffizientenspeicher reicht aber nur noch für 6 (31 freie Worte). Mehr Biquads gingen nur mit gemeinsam
   genutzten Koeffizienten (gleiches Filter auf mehreren Kanälen, z. B. Links/Rechts gekoppelt) oder auf Kosten
   anderer Blöcke.
3. **DO5–DO8 von Kern 2/3 sind frei** (SDOUT3/SDOUT4). Offen: ob diese Pins auf der Platine beschaltet sind
   (Schaltplan V40h). Wenn ja, wären bis zu 8 weitere Ausgangskanäle denkbar, begrenzt durch Takte und
   Koeffizienten.
4. **Kern 1 ist voll.** Neue Funktionen dort (z. B. Kompressor/Limiter, Loudness im DSP) brauchen erst Punkt 1
   oder den Verzicht auf Biquads der Eingangsketten.
5. **Volume-Update-Block statt Software-Rampe:** Die Glättung kostet je Pegel rund 25–30 Takte (Kern 1: 6 Pegel,
   Kern 2/3: 4). Der Hardware-Block (8 Kanäle, Rampe 2048/4096/8192 Samples) könnte das übernehmen; seine
   Ansteuerung durch den 8051 ist in SLEU067A nur teilweise beschrieben (Register 0xA6–0xAC).
6. **Dither (LFSR)** für die 32-Bit-Ausgänge ist vorhanden, aber ungenutzt; bei 24-Bit-Wandlern wäre er am
   Ausgang sinnvoll.
7. **48 kHz** verdoppelt das Budget (2816 Takte), halbiert aber die Bandbreite; mit 96 kHz-Quellen nicht sinnvoll.

Voraussetzungen, bevor man das baut:

- Ein Assembler (Umkehrung von `tas3108_dis.py`, die Kodierung ist vollständig bekannt) und ein Prüfwerkzeug, das
  ein neues Programm mit `tas3108_flow.py` gegen den alten Signalfluss und das Taktbudget prüft.
  Regeln der Hardware, die das Prüfwerkzeug durchsetzen muss: Sprungziele unter 0x400, drei NOPs nach
  JMP/BOC/BNC, `COMP`-Ergebnis erst zwei Takte später, 13 Takte zwischen `DLYPTR` und `ST(DLYO)`.
- Die MCU schreibt `dsp_N_code` beim Start ins EEPROM des TAS3108, wenn die Prüfsumme abweicht. Ein geändertes
  Programm kommt also mit der Port-Firmware aufs Gerät, ohne zusätzlichen Weg.
- Messungen am Gerät (Pegel, Frequenzgang, Latenz) vor und nach jeder Änderung, mit der Lautstärkegrenze für Tests.

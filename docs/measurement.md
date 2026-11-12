# Messaufbau am Gerät (#49)

Werkzeug: `tools/measure/forge_measure.py` (Signale erzeugen, abspielen und aufnehmen mit zeitgesteuerten
Forge-Befehlen, auswerten). Tests der Auswertung: `tests/test_measure.py`.

## Aufbau

```
PC ──USB── S/PDIF-Karte ──Toslink── Forge S/PDIF IN (rechte optische Buchse der Eingangsseite)
PC Line-In (blau, hinten) ──Klinke/Cinch── Forge Ausgangspaar (Ausgangsseite, Paare 1–4)
Forge USB-Steuerung (CP2102) ── PC, /dev/ttyUSB0
```

Die Forge hat außerdem USB-Audio (XMOS an X102); am DEV-Gerät meldete es sich nicht, deshalb der Weg über S/PDIF.

## Einstellungen an der Forge

| Was | Einstellung | Befund |
|---|---|---|
| Eingangswahl optisch | `set.1 mixer.<pos>.1.10=3` | Empfänger CT7302 RX2; eingerastet, wenn Register 122 (0x7A) & 0xE0 = 0 (`get.1 i2c.1.32.122`) |
| Digitaleingang in den Mixer | `mixer.<pos>.C.7` / `.C.8` | in8 / in7 von Kern 1 (G7/G8), wie aus der DSP-Analyse (#43) |
| Mixer-Kanäle | C3–C10 → Ausgänge | jeder Kanal ein Ausgang, C1/C2 die Ketten 35/36 |
| Ausgangsbuchsen | paarweise: Paar 1 = `gain.out.1/2`, Paar 2 = `3/4` … | ein Klinkenkabel nimmt eine Buchse auf |
| Pegel | `gain.out` 223 = 0 dB, `gain.spdif` 255 = 0 dB (0,5 dB je Schritt) | Skalen unterscheiden sich |

## Einstellungen am PC

- **Abtastrate:** Der S/PDIF-Empfänger rastete am DEV-Gerät nur mit 44,1 und 48 kHz ein, nicht mit 88,2/96 kHz
  (Sender oder Empfänger, offen). Das Werkzeug setzt PipeWire für die Messung auf die gewählte Rate
  (`clock.force-rate`) und danach zurück.
- **Line-In an PipeWire vorbei:** PipeWire stellt beim Öffnen des Eingangs seine gespeicherte Verstärkung wieder
  her (am Messplatz Capture +30 dB und Line Boost +20 dB). Das Werkzeug schaltet die PipeWire-Quelle ab
  (`--source`, `pactl suspend-source`), setzt die Hardware auf 0 dB (oder +30 dB mit `--gain 30`) und nimmt direkt
  per `arecord` auf. Die Messkette ist linear (+30 dB Verstärkung ergeben +30,1 dB).
- **Absoluter Pegel:** nicht kalibriert; der gemessene Pegel lag rund 44 dB unter der Erwartung, bei Port und
  Original gleich. Für Funktionstests wird relativ gemessen.

## Auswertung

- `level`: Tonpegel und stärkste Linien. Am Messplatz liegt eine Brummlinie bei 160 Hz und Vielfachen; `multitone`
  verschiebt Töne davon weg.
- `response A.wav B.wav`: Pegel jedes Tons in B relativ zu A, also die Wirkung einer Einstellung unabhängig von der
  Messkette. Ein konstanter Versatz über alle Frequenzen ist ein Pegelunterschied der Aufnahmen, kein Filterfehler.
- `phase A.wav B.wav --tones T`: Phase links minus rechts je Ton, B gegen A. Dazu dasselbe Signal auf beide Ausgänge
  eines Paars legen und nur einen filtern; der Startzeitpunkt der Aufnahme spielt dann keine Rolle. Am Messplatz liegt
  Ausgang 3 rechts, das Vorzeichen ist also umgekehrt. Unter 100 Hz streut die Messung um einige Grad (Brumm,
  geringer Pegel).
- `clicks`: Aussetzer (2-ms-Fenster 12 dB unter dem Pegel; eine Pegelabsenkung um 12 dB löst das ebenfalls aus) und
  Spitzen im Hochpass über 5 kHz, die das Maximum der Ruhephase vor dem ersten Befehl um mehr als 3 dB übersteigen.
  Die Messkette fängt sich gelegentlich Impulsstörungen ein, auch ohne Befehl: Einzelne Treffer nur mit
  Wiederholungen werten.

## Beispiel

```
M=tools/measure/forge_measure.py
python3 $M tone /tmp/t.wav --freq 1000 --db -20 --seconds 20
python3 $M run --play /tmp/t.wav --out /tmp/r.wav --seconds 9 \
  --sink alsa_output.usb-…pro-output-0 --card 1 --source alsa_input.pci-…analog-stereo \
  --at 3 "set.1 gain.out.4=199" --at 6 "set.1 gain.out.4=223"
python3 $M level /tmp/r.wav
python3 $M clicks /tmp/r.wav --events 3,6
```

## Ergebnisse vom 12.11.2026 (DEV V40-96ELN)

- Audioweg der Port-Firmware identisch zum Original 1.11.6000 (−70,9 / −70,7 dBFS an derselben Buchse).
- Clickless-EQ ohne Aussetzer und Knacken (#44), Lautstärke/Phase/Mute knackfrei, Mixer/Delay/Eingangswahl mit
  ca. 2 s Mute (#50), Loudness und dynamische Loudness wie spezifiziert (#51), EQ-Frequenzgang ±0,3 dB (#4).
- Allpässe (#48): Typ 21 (1 kHz), Typ 20 (1 kHz, Q 0,707), Typ 22 (−45° bei 1 kHz; −270° bei 300 Hz, Q 1) über
  100 Hz innerhalb ±1,5° der Theorie, Nullmessung ±1°.

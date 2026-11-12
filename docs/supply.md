# Versorgungsspannung überwachen (#46)

Erweiterung der Port-Firmware, nur in den Bring-up-Builds aktiv (die sichere Variante beantwortet die Befehle,
misst aber nicht).

## Was gemessen wird

- **3,3-V-Schiene der MCU**, immer: Der ADC des XMEGA misst intern VCC/10 gegen die 1-V-Referenz (ADCB Kanal 1).
  Ohne Abgleich je Gerät etwa ±2 % genau.
- **Bordspannung** (12–14 V), optional über einen der beiden GVOL-Eingänge (X-GND-INPUT, gedacht für ein Poti)
  und einen Spannungsteiler im Kabel. Ein Eingriff ins Gerät ist nicht nötig. Der gewählte Eingang steht dann nicht
  mehr für ein Poti (MODC) zur Verfügung.

## Verdrahtung

Im Gerät hängt am GVOL-Eingang ein Pull-up von 5,8 kΩ auf 3,3 V, dazu 120 Ω, Klemmdioden und 1 µF; der ADC
misst bis etwa 2,06 V. Die Masse von X-GND-INPUT ist die Digitalmasse, also die Minusleitung der Versorgung.

Vorschlag: 47 kΩ von der Bordspannung (nach der Sicherung) auf den GVOL-Pin und 3,3 kΩ vom GVOL-Pin nach Masse.
Zusammen mit dem Pull-up liegen dann bei 9–15 V etwa 1,5–1,8 V am Pin, und die Auflösung beträgt rund 25 mV an
der Batterie. Die Werte am Gerät nachmessen, die Kalibrierung gleicht Toleranzen und den Pull-up aus.

## Befehle

| Befehl | Bedeutung |
|---|---|
| `get.1 system.supply` | `[VCC mV, Bordspannung mV]`, −1 ohne Quelle oder Kalibrierung |
| `get.1 system.supply.raw` | `[VCC roh, GVOL1 roh, GVOL2 roh]` (Mittelwerte des ADC) |
| `set.1 system.supply.source=N` | 0 aus, 1 = GVOL1, 2 = GVOL2 |
| `set.1 system.supply.cal=[raw1,mV1,raw2,mV2]` | Zwei Messpunkte, linear |
| `set.1 system.supply.low=mV` | Schwelle für Unterspannung, 0 = aus |
| `set.1 system.supply.mute=1` | Ausgänge stumm, solange die Spannung zu niedrig ist |

Alle Einstellungen liegen im EEPROM (0x0F03–0x0F0E).

Ereignisse: `#.1 system.supply=low` wird gesendet, wenn die Spannung 8 Auswertungen lang unter der Schwelle
liegt, und `#.1 system.supply=ok`, sobald sie wieder 300 mV darüber liegt (Hysterese). Mit `supply.mute=1` werden
die Ausgänge wie beim Übertemperaturschutz stummgeschaltet und nach dem Ende der Unterspannung automatisch wieder
freigegeben.

## Kalibrieren

1. `set.1 system.supply.source=1` (Eingang wählen).
2. Bei einer bekannten Spannung, z. B. 12,00 V am Multimeter, den Rohwert lesen: `get.1 system.supply.raw`, zweiter
   Wert bei GVOL1.
3. Das bei einer zweiten Spannung wiederholen, z. B. 14,00 V mit Motor an oder am Labornetzteil.
4. `set.1 system.supply.cal=[raw1,12000,raw2,14000]`.
5. Gegenprobe mit `get.1 system.supply`.

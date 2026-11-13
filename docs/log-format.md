# Binäres Log auf der microSD (#60)

Kompakte Form des `debug.log` der Original-Firmware (`libs/debug` `debugout` → Datei #99). Die GUI liest die Datei über die Dateiübertragung (`system.sd.open/read`, #59) und dekodiert sie.

## Ein- und Ausschalten

Wie im Original über zwei Register, die im EEPROM gespeichert sind:

| Befehl | Bedeutung |
|---|---|
| `set system.debug.out=N` | Bit 2 (`debugout_file`, Wert 4) = Log auf die Karte. Bit 0/1 sind UART2/UART4 des Originals |
| `set system.debug.mode=L` | Stufe 1–7. Geschrieben werden Ereignisse mit Stufe ≤ L. 0 = aus, wie im Original |

Beispiel: `set system.debug.out=4` und `set system.debug.mode=4` schreiben alles bis Stufe „info“.

## Dateien

- `FORGE.LOG`: wird fortgeschrieben.
- `FORGE.OLD`: Erreicht `FORGE.LOG` 64 KB, wird sie zu `FORGE.OLD` (eine ältere `FORGE.OLD` wird gelöscht) und eine neue `FORGE.LOG` beginnt.
- Jede Datei beginnt mit einem Formatsatz (Modul 6, Ereignis 1).
- Bis die Karte eingebunden ist (beim Start erst nach der DSP-Erkennung), stehen bis zu 16 Sätze im RAM. Weitere zählt der Satz „verloren“.
- Nach jedem Schreiben wird die Datei geschlossen. Ein Stromausfall verliert höchstens die Sätze in der Warteschlange.

## Satz (12 Byte, little endian)

| Byte | Typ | Inhalt |
|---|---|---|
| 0–1 | u16 | Startzähler (`device_bootcycles`) dieses Starts: trennt die Starts. Ein Start, den ein DSP-Neustart abbricht, trägt dieselbe Nummer wie der folgende (der Zähler steigt erst am Ende der Startsequenz); beide beginnen mit einem Satz „Start“ und ms ab 0 |
| 2–5 | u32 | ms seit dem Start dieses Laufs |
| 6 | u8 | Stufe (Bit 7–5) · Modul (Bit 4–0) |
| 7 | u8 | Ereignis |
| 8–9 | u16 | Parameter 1 |
| 10–11 | u16 | Parameter 2 |

Stufen wie `dbglvl_*` des Originals: 1 fatal, 2 error, 3 warn, 4 info, 5 debug, 6 trace.

## Ereignisse (Format 1)

| Modul | Ereignis | Stufe | p1 | p2 |
|---|---|---|---|---|
| 1 System | 1 Start | fatal | Reset-Ursache der MCU (`RST.STATUS`: Bit 0 Einschalten, 1 Pin, 2 Brownout, 3 Watchdog, 5 Software) | 0 |
| 1 System | 2 DSP-Neustart (#45/#57) | error | gefundene Bausteine (`hardware_dsp`) | 1 = ein Kern meldete einen Fehlerzustand |
| 1 System | 3 Hardware weicht ab, Ausgänge stumm | error | `hardware_dsp` | `hardware_other` |
| 2 Fehlerzähler | Fehlertyp (wie `system.errors`) | error, ab Typ 128 warn | Zählerstand danach | 0 |
| 3 DSP | 1 EEPROM neu geschrieben | info (error bei falscher Prüfsumme) | Kern | Prüfung (1 ok, 2 falsch) · Anzahl Schreibvorgänge × 256 |
| 3 DSP | 2 Kern-Status nach dem Laden | debug (error bei Fehlerzustand) | Kern | Status-Byte 1 · 256 (antwortet) |
| 3 DSP | 3 Bus nach der EEPROM-Arbeit freigeräumt (SDA lag low) | warn | Bus | Kern |
| 4 Preset | 1 geladen | info | Preset (ab 0) | 0 |
| 4 Preset | 2 von der Karte geladen | info | N von `preset_N.cfg` | 0 |
| 5 Versorgung | 1 Spannung zu niedrig (#46) | warn | mV | 0 |
| 5 Versorgung | 2 Spannung wieder gut | info | mV | 0 |
| 6 Log | 1 Formatsatz | fatal | Formatversion (1) | Satzlänge (12) |
| 6 Log | 2 Sätze verloren | warn | Anzahl | 0 |

Neue Ereignisse bekommen neue Nummern. Bestehende ändern ihre Bedeutung nicht; sonst steigt die Formatversion.

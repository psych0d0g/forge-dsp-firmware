# forge-dsp-firmware

Freie Firmware in C für die Forge-DSP-Geräte von Mark Duprau (Forge Audio, 2011–2021). Sie portiert die
BASCOM-Firmware 1.11.6000 (bitbucket.org/forge-dsp/firmware) für **alle Varianten** nach avr-gcc/PlatformIO.
Geflasht wird über den vorhandenen Bootloader chip45boot3, direkt aus VS Code.

| Umgebung | Gerät | PCB | `system.key.info` |
|---|---|---|---|
| `V30-96LN` | Forge DSP 96LN | 3.0 | 96LN-3.0 |
| `V30-96ELN` | Forge DSP 96ELN | 3.0 | 96ELN-4.0 (sic, Quelltext) |
| `V40-96ELN` | Forge DSP 96ELN | 4.0 | 96ELN-4.0 |
| `V40-96ELN-3` | Forge DSP 96ELN-3 | 4.0 | 96ELN3-4.0 |
| `V40-CORED8` | ETON CORE D8 | 4.0 | CORED8-4.0 |
| `V41-96ELN-3` | Forge DSP 96ELN-3 | 4.1 | 96ELN3-4.1 |
| `V41-CORED8` | ETON CORE D8 | 4.1 | CORED8-4.1 |

Alle: ATxmega256A3U, interner 32-MHz-Takt.

**Stand: M0 (Bring-up), noch nicht auf echter Hardware getestet.** Takt, Pins, Selbsthaltung, Shutdown und Watchdog
entsprechen dem Original. Am USB-COM (38400 Baud) antwortet M0 auf `<system.firmware`, `<system.key.info` und
`>system.bootloader=1`. Die Peripherie bleibt im Reset, Remote-Out (Endstufen) bleibt aus: **kein Ton**. Fahrplan in
den GitHub-Issues und in `docs/porting-plan.md`.

Lizenz: CC BY-NC-SA 4.0 (`LICENSE.txt`), wie das Original. `tools/c45b3-host` und die Bootloader-Quellen in
`tools/c45b3-host/tests/ctarget/upstream` stehen unter MIT (chip45boot3, Erik Lins).

## VS Code / PlatformIO
1. Projekt öffnen, unten in der Statusleiste die Umgebung (= Variante) wählen.
2. `upload_port` in `platformio.ini` fest auf den CP2102 des Forge setzen (`/dev/serial/by-id/usb-Silicon_Labs_CP2102…`).
3. *PlatformIO: Upload*. Ablauf:
   - `<system.key.info`: Modell und PCB des angeschlossenen Forge müssen zur Umgebung passen, sonst Abbruch.
     V30-96ELN und V40-96ELN melden beide `96ELN-4.0`, hier die Umgebung bitte selbst richtig wählen.
   - `>system.bootloader=1`: Die eigene Firmware springt in den Bootloader. Die Originalfirmware kennt den Befehl
     nicht, dann den Forge während „waiting for bootloader“ (30 s) aus- und einschalten (Remote-In).
   - Flash schreiben (Seiten werden im Bootloader geprüft), zurücklesen, falls der Bootloader-Build das kann, Anwendung starten.
4. Project Tasks → Custom:
   - *Forge: versions*: Version von Bootloader und Firmware.
   - *Forge: backup*: Flash und EEPROM nach `backups/<Datum>/`, soweit lesbar.
   - *Forge: flash release 1.11.6000 (…)*: Rückweg auf die offizielle Firmware der Variante (`release/`, sha256).
5. Monitor: 38400 Baud, CR LF (Textprotokoll, `libs/cmd/v20/commands.txt` im Original).

c45b3 braucht nur pyserial, das liefert PlatformIO mit. CLI: `pio run -e V41-96ELN-3 -t upload`.

## Aufbau
- `src/`: gemeinsamer Code aller Varianten, `include/`: Header.
- `src/flavours/<Variante>/`: **generiert** mit `tools/gen_flavour.py` aus dem BASCOM-Quelltext. `flavour.h` enthält alle
  `const` des aufgelösten Builds als `BAS_*` und die Pin-Aliase als `PIN_*`, `flavour_pins.c` die Pin-Init aus `pins.bas`.
  Prüfen: `python3 tools/gen_flavour.py <forge-dsp/firmware> src/flavours --check`.
- `tools/bas_resolve.py`: löst `$include`/`#IF` eines BASCOM-Builds auf (`docs/inventory-*.md`, `--flat` = nur aktiver Code
  mit Datei:Zeile, Referenz beim Portieren).
- `tools/c45b3-host/`: Linux-Host für chip45boot3, mit Tests gegen ein Modell und gegen den nativ übersetzten Original-
  Bootloader-Code. `tests/pty_bridge.py` emuliert Firmware + Bootloader an einem pty für Upload-Tests ohne Hardware.
- `release/`: offizielle HEX 1.11.6000 aller Varianten, unverändert.

## Sicherheit
- Der Bootloader-Bereich ist laut Original per Lockbits (BLBB) geschützt. Das Tool schreibt nie dorthin und sendet die
  abschließende Leer-Frame der Windows-GUI nicht (sie würde die Seite hinter dem Image löschen).
- Vor dem ersten eigenen Flashen: *Forge: backup*, Abstimmung über die Original-GUI sichern.
- Solange die Audiokette nicht vollständig portiert ist, bleiben die Endstufen aus.

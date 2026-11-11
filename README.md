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

**Stand: M0 (Bring-up) + Kern des Textprotokolls (M1, #1), noch nicht auf echter Hardware getestet.** Takt, Pins,
Selbsthaltung, Shutdown und Watchdog entsprechen dem Original. Der Parser von `libs/cmd/v20` ist Zeile für Zeile portiert
(Prüfsumme, Ziel/Weiterleitung über RS485 Master/Slave, Antwort-Header, Fehler) und läuft auf allen vier seriellen Kanälen.
Befehle bisher: `system.boot`, `system.echo`, `system.firmware` (meldet `1.11.6000` wie das Original, eigene Version unter
`system.firmware.port`), `system.info`, `system.key.info[.board|.type|.vendor]`, `system.ping`. Alles andere antwortet
„unknown command“. Die Peripherie bleibt im Reset, Remote-Out (Endstufen) bleibt aus: **kein Ton**. Fahrplan in
den GitHub-Issues und in `docs/porting-plan.md`.

Lizenz: CC BY-NC-SA 4.0 (`LICENSE.txt`), wie das Original. `tools/c45b3-host` und die Bootloader-Quellen in
`tools/c45b3-host/tests/ctarget/upstream` stehen unter MIT (chip45boot3, Erik Lins).

## VS Code / PlatformIO
1. Projekt öffnen, unten in der Statusleiste die Umgebung (= Variante) wählen.
2. `upload_port` in `platformio.ini` fest auf den CP2102 des Forge setzen (`/dev/serial/by-id/usb-Silicon_Labs_CP2102…`).
3. *PlatformIO: Upload*. Ablauf:
   - `get.1 system.key.info`: Modell und PCB des angeschlossenen Forge müssen zur Umgebung passen, sonst Abbruch.
     V30-96ELN und V40-96ELN melden beide `96ELN-4.0`, hier die Umgebung bitte selbst richtig wählen.
   - `set.1 system.boot=1`: Software-Reset in den Bootloader. Das Original kennt den Befehl (`cmd_set_system_boot`,
     Reset ~1 s nach „ok“, ohne Werkseinstellungen). Antwortet der Forge nicht, während des Wartens aus-/einschalten.
   - Flash schreiben (Seiten werden im Bootloader geprüft), zurücklesen, falls der Bootloader-Build das kann, Anwendung starten.
4. Project Tasks → Custom:
   - *Forge: versions*: Version von Bootloader und Firmware.
   - *Forge: backup*: Flash und EEPROM nach `backups/<Datum>/`, soweit lesbar.
   - *Forge: flash release 1.11.6000 (…)*: Rückweg auf die offizielle Firmware der Variante. Sie wird aus dem
     GitHub-Release [0.0.1116000](https://github.com/psych0d0g/forge-dsp-firmware/releases/tag/0.0.1116000) geladen
     und gegen `scripts/release-0.0.1116000.sha256` geprüft (Cache `.pio/release/`).
5. Monitor: 38400 Baud, CR LF. Format wie die GUI: `get.1 system.ping`, `set.1 <register>=<wert>;<cs>` (`1` = dieses
   Gerät, `cs` = Summe der Zeichen ab dem Register, 2 Hex). Ohne Leerzeichen nach dem Befehlszeichen liest das
   Original das erste Zeichen als Ziel (`<system…` = Slave).

c45b3 braucht nur pyserial, das liefert PlatformIO mit. CLI: `pio run -e V41-96ELN-3 -t upload`.

## Aufbau
- `src/`: gemeinsamer Code aller Varianten (`src/cmd/`: Textprotokoll), `include/`: Header.
- `tests/`: `test_cmd.py` übersetzt das Protokoll nativ für jede Variante (`make -C tests/native`) und prüft die Antworten.
- `src/flavours/<Variante>/`: **generiert** mit `tools/gen_flavour.py` aus dem BASCOM-Quelltext. `flavour.h` enthält alle
  `const` des aufgelösten Builds als `BAS_*` und die Pin-Aliase als `PIN_*`, `flavour_pins.c` die Pin-Init aus `pins.bas`.
  Prüfen: `python3 tools/gen_flavour.py <forge-dsp/firmware> src/flavours --check`.
- `tools/bas_resolve.py`: löst `$include`/`#IF` eines BASCOM-Builds auf (`docs/inventory-*.md`, `--flat` = nur aktiver Code
  mit Datei:Zeile, Referenz beim Portieren).
- `tools/c45b3-host/`: Linux-Host für chip45boot3, mit Tests gegen ein Modell und gegen den nativ übersetzten Original-
  Bootloader-Code. `tests/pty_bridge.py` emuliert Firmware + Bootloader an einem pty für Upload-Tests ohne Hardware.
- Offizielle HEX 1.11.6000 aller Varianten: GitHub-Release `0.0.1116000` (nicht im git).

## Sicherheit
- Der Bootloader-Bereich ist laut Original per Lockbits (BLBB) geschützt. Das Tool schreibt nie dorthin und sendet die
  abschließende Leer-Frame der Windows-GUI nicht (sie würde die Seite hinter dem Image löschen).
- Vor dem ersten eigenen Flashen: *Forge: backup*, Abstimmung über die Original-GUI sichern.
- Solange die Audiokette nicht vollständig portiert ist, bleiben die Endstufen aus.

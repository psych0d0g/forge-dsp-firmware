# c45b3-host

`c45b3`: Linux-Host für den Bootloader
chip45boot3 (Python-Port von `chip45boot3_windows_gui/BootloaderTools.cs`, Upstream
github.com/eriklins/chip45boot3 @ c5a34fa, MIT, siehe `LICENSE.chip45boot3`).

## Benutzung
```
pip install pyserial                     # bzw. Python von PlatformIO
python3 -m c45b3 --port /dev/ttyUSB0 --wait 30 --stay info     # Forge währenddessen aus-/einschalten
python3 -m c45b3 --port /dev/ttyUSB0 --resume --stay read-flash  dumps/forge-flash
python3 -m c45b3 --port /dev/ttyUSB0 --resume read-eeprom dumps/forge-eeprom   # danach Anwendung starten
python3 -m c45b3 --sim --sim-image main.hex info                # Trockenlauf gegen das Modell
```
- Der Bootloader lauscht nur ca. 3 s nach einem Reset auf Autobaud (`U`). `--wait SEK` sendet so lange Bursts.
- Nach `--stay` bleibt er im Befehlsmodus und beantwortet kein Autobaud mehr, dann `--resume`.
- Ohne `--stay` wird am Ende `0x18` (Anwendung starten) gesendet.
- `--enter 'set.1 system.boot=1'` sendet den Befehl vorher mit `--app-baud` (38400) an die laufende Firmware. Original wie
  Port antworten ` *.1 system.boot ok` und springen per Software-Reset (Original nach ~1 s) in den Bootloader.
- `backup DIR`: Versionen, Flash und EEPROM in einer Sitzung, dazu `backup.json` mit sha256.
- `--expect-device MODEL-PCB` fragt vorher `get.1 system.key.info` ab und bricht bei anderem Gerät ab.
- Integration in PlatformIO: `scripts/c45b3_pio.py` im Hauptverzeichnis. Test ohne Hardware: `tests/pty_bridge.py`.
- Dumps: `OUT.bin` + `OUT.hex`, vorhandene Dateien werden nicht überschrieben, sha256 wird ausgegeben.
- Schreiben (`write-flash`, `write-eeprom`) nur mit `--allow-write`.
  Flash wird auf ganze Seiten (512 B) mit 0xFF aufgefüllt, die abschließende Leer-Frame der GUI wird nicht gesendet.

## Tests
```
python3 -m unittest tests.test_c45b3                    # Modell, überall
make -C tests/ctarget && python3 -m unittest tests.test_c45b3   # zusätzlich gegen den C-Code (gcc, Linux)
```
`tests/ctarget/` übersetzt die unveränderten Upstream-Dateien `host.c`, `commands.c`, `crc.c` nativ
(Stubs für USART, NVM, EEPROM, PROGMEM). Ein- und Ausgabe laufen über stdin/stdout. Zwei Builds: `full` (alle PROVIDE_*)
und `example` (Flags aus `Xmega_Example.cproj`, ohne Lesebefehle). `ModelEquivalenceTest` prüft, dass Modell
(`c45b3/sim.py`) und C-Code auf denselben Bytestrom byte-gleich antworten.

## Eigenheiten des Ziel-Codes (aus Quelltext und Harness)
- Die abschließende leere `0x22`-Frame der GUI löscht und schreibt die Seite *hinter* den Daten. Endet das Image an
  `BOOT_SECTION_START` (0x40000), trifft das die erste Bootloader-Seite (beim Forge per Lockbits geschützt, laut forge.md).
- Bei CRC-Fehler sendet das Ziel `hostBufferSend((uint8_t *)"e0", 2)`. Die CRC wird an das Literal im RAM angehängt, dabei
  werden das NUL und 1 Byte dahinter überschrieben. Das Tool warnt dann. Danach nur noch lesen und Ziel neu starten.
- Unbekannte oder nicht einkompilierte Befehle (z. B. `0x13`, Lesen ohne PROVIDE_*_READ) bekommen keine Antwort.
- Empfangszähler wird nur durch STX zurückgesetzt, ohne Grenze nach 131 Byte. Daher nur kurze Autobaud-Bursts (16 × `U`).
- EEPROM-Schreiben ohne Bereichsprüfung, `flashBuffer` ohne Grenzprüfung (Frames müssen die Seite genau füllen).

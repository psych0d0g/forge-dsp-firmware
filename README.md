# forge-dsp-firmware

Freie Firmware in C für die Forge-DSP-Geräte von Mark Duprau (Forge Audio, 2011–2021). Sie portiert die
BASCOM-Firmware 1.11.6000 (bitbucket.org/forge-dsp/firmware) für **alle Varianten** nach avr-gcc/PlatformIO.
Geflasht wird über den vorhandenen Bootloader chip45boot3, direkt aus VS Code.

## Kompatibilität

Welche Firmware passt auf welche Hardware. Die Port-Firmware gibt es je Gerät als sichere Variante (Umgebung
`<name>`, kein Ton) und als vollständige Variante (`<name>-bringup`). CORED8 ist firmwareseitig ein 96ELN und V41
dasselbe wie V40, nur die Kennung unterscheidet sich ([docs/single-firmware.md](docs/single-firmware.md)); sie steht
im EEPROM, der Upload übernimmt sie aus der bisherigen Firmware.

| Gerät | PCB | `system.key.info` | Port-Firmware (Umgebung) | Original 1.11.6000 (Release [0.0.1116000](https://github.com/psych0d0g/forge-dsp-firmware/releases/tag/0.0.1116000)) | Am Gerät getestet |
|---|---|---|---|---|---|
| Forge DSP 96LN | 3.0 | `96LN-3.0` | `V30-96LN` | `forge-dsp-V30-96LN-bascom-1.11.6000.hex` | – |
| Forge DSP 96ELN | 3.0 | `96ELN-4.0` (sic, Quelltext) | `V30-96ELN` | `forge-dsp-V30-96ELN-bascom-1.11.6000.hex` | – |
| Forge DSP 96ELN | 4.0 | `96ELN-4.0` | `V4x-96ELN` | `forge-dsp-V40-96ELN-bascom-1.11.6000.hex` | ja (DEV-Gerät, #39) |
| ETON CORE D8 | 4.0 | `CORED8-4.0` | `V4x-96ELN` | `forge-dsp-V40-CORED8-bascom-1.11.6000.hex` | – |
| ETON CORE D8 | 4.1 | `CORED8-4.1` | `V4x-96ELN` | `forge-dsp-V41-CORED8-bascom-1.11.6000.hex` | – |
| Forge DSP 96ELN-3 | 4.0 | `96ELN3-4.0` | `V4x-96ELN-3` | `forge-dsp-V40-96ELN-3-bascom-1.11.6000.hex` | – |
| Forge DSP 96ELN-3 | 4.1 | `96ELN3-4.1` | `V4x-96ELN-3` | `forge-dsp-V41-96ELN-3-bascom-1.11.6000.hex` | – |

Achtung: Forge 96ELN mit PCB 3.0 und 4.0 melden beide `96ELN-4.0` (#10). Die Firmwares sind verschieden
(PCB 4.0 hat den Taktgenerator Si5338 und USB-Audio). Die Umgebung bitte nach der Platine wählen.

Alle: ATxmega256A3U, interner 32-MHz-Takt.

**Stand:** Der Funktionsumfang des Originals ist portiert: komplettes Textprotokoll, Register und EEPROM-Layout,
Presets, Biquads, Mixer, Delays, MODC-Steuerblöcke, Events, ADC/Temperatur, IR-Empfänger und die Einschaltsequenz
aller Bausteine. Nativ getestet für alle 7 Varianten (128 Protokolltests); auf einem Forge 96ELN (V40) laufen sichere und
Bring-up-Variante: Start auf der Abstimmung des Originals ohne Werks-Reset, alle Bausteine erkannt,
Biquad-Koeffizienten bitgenau, Abschalten über Remote-In, Audioweg gemessen (#49). Erstes Release: **2.0.0**
(bewusst oberhalb der letzten BASCOM-Firmware 1.11.6000). Remote-Out ist seit 2.0.0 wie im Original aktiv, mit
angeschlossener Endstufe aber noch nicht geprüft. Fahrplan in den GitHub-Issues.

## Was die Port-Firmware zusätzlich kann

Ausführlich mit allen Unterschieden: **[docs/port-changes.md](docs/port-changes.md)**.

- **Firmware-Update ohne Taster:** `set.1 system.boot` bringt den Forge direkt in den Bootloader, Updates aus VS Code
  laufen damit ohne Aus-/Einschalten. Grundlage ist eine Analyse des ausgelesenen Bootloaders: Er startet auch über eine
  Sequenz im EEPROM, die das Original vorgesehen, aber nie geschrieben hat ([docs/bootloader.md](docs/bootloader.md), #22).
  Am Gerät geprüft: Sicherung, Flashen und Start mit Kennung komplett ohne Taster.
- **Abstimmung sichern und vergleichen** beim Update (`c45b3 export-tuning / compare-tuning / restore-tuning`,
  Tasks *Forge: export/compare tuning*), im `.cfg`-Format der Original-GUI (#30).
- **Versorgungsspannung überwachen** (`get.1 system.supply`): 3,3-V-Schiene intern, Bordspannung über einen GVOL-Eingang
  mit Spannungsteiler, Ereignis bei Unterspannung und auf Wunsch Stummschalten, Abgleich der 3,3-V-Messung gegen ein
  Multimeter (`set.1 system.supply.vcc=mV`) ([docs/supply.md](docs/supply.md), #46).
- **DSP-Ladevorgang messen** (`get.1 system.hardware.dsp`): wie lange die TAS3108 beim Start ihr Programm laden, je I2C-Bus (#45).
- **Startprotokoll der DSP-Kerne** (`get.1 system.hardware.dsp.1`/`.2`): Status, Version und ob das EEPROM neu geschrieben und richtig zurückgelesen wurde, für diesen Start und den vor einem DSP-Neustart (#57). Geschriebene DSP-Programme werden zurückgelesen; dadurch braucht das Umverteilen der Biquads nur noch einen Neustart.
- **Pegelanzeige** (`get.1 system.vu`): Effektivpegel aller acht Eingänge für ein VU-Meter in der GUI, gemessen nur
  solange jemand fragt (#52).
- **Ausgangspegel** (`get.1 system.vu.out`): Effektivpegel der Ausgänge 1–8 nach Gain und Laufzeit, gleiche Skala wie `system.vu` (#52).
- **Befehlsliste** (`get.1 system.help`): alle `get`/`set system.*`-Befehle der Firmware, erzeugt aus den Verteiltabellen.
- **Fehlerzähler zurücksetzen** (`set.1 system.errors=0`), im Original nur per Werks-Reset.
- **microSD prüfen** (`get.1 system.sd`, `get.1 system.sd.files`): Karte, Dateisystem, Größe und Dateien, nur lesend (#24).
- **Preset auf die microSD exportieren** (`set.1 system.sd.export=N`): schreibt `preset_N.cfg` im GUI-Format, das der Vorlader beim Start wieder liest (#24).
- **Dateien auf die microSD und zurück** (`system.sd.open/create/read/write/close/delete`): in Blöcken mit Prüfsumme über die serielle Verbindung, für die GUI (#59).
- **Binäres Log auf der microSD** (`set.1 system.debug.out=4`, Stufe mit `system.debug.mode`): kleine Sätze statt Text, für die Auswertung in der GUI; Format in [docs/log-format.md](docs/log-format.md) (#60).
- **Namen der Eingänge** im Gerät (`set.1 system.channel.in.N="…"`), wie es sie für die Ausgänge schon gab (#55).
- **Grenzen lesbar** (`get.1 system.limits.*`): Wertebereiche, Filtertypen und Laufzeit-Speicher je DSP, damit eine GUI
  nichts fest einbauen muss (#54).
- **Biquads frei verteilen** (`get.1 system.biquads`, `set.1 system.biquads.K=[…]`): Jeder DSP-Kern hat einen
  gemeinsamen Pool, die Filterzahl je Ausgang bzw. Summe ist einstellbar (z. B. 170 Bänder für einen Ausgang);
  Summe A/B haben zusammen 28 Biquads mehr als im Original (172 statt 144).
  Die MCU erzeugt das passende DSP-Programm selbst; gespeicherte Bänder wandern mit (#47).
- **Robuster Start**: Ein DSP-Kern, der beim Start noch lädt, wird bis zu 2 s nachgefragt; fehlt er dann noch, startet die
  Forge genau einmal neu statt stumm zu bleiben (#45).
- **Bootloader auslesen** (nur lesend, `get.1 system.hardware.boot.N`, `tools/dump_bootsection.py`); das Abbild liegt unter
  [`bootloader/`](bootloader/).
- **Eingangswahl** `mixer.in` (und `mixer.in.max`) für die aktive Mixer-Position, mit Ereignis an die GUI; die
  dokumentierten, im Original nie gesetzten Ereignisse 45–48 (`SOURCE.1–4.MUX`) tragen jetzt die Eingangswahl je
  Position (#23).
- **Startbefehle**: bis zu 8 Befehle im EEPROM, die nach dem Laden des Presets beim Start laufen
  (`set.1 system.startup.1="set.1 gain.out.2:150"`, #40).
- **Clickless** (Vorgabe aus, `set.1 biquad.clickless=1`): Filter- und Mixer-Änderungen werden ohne Aussetzer
  eingeblendet, andere Änderungen (Delay, Eingangswahl) sind nur noch ca. 1 s statt 2 s stumm (#44, #50).
- **Mono je Ausgangspaar** (MODC „CTRL-MONO“ aus der Original-GUI, #14): Stereo, Mono links/rechts oder L+R, schaltbar
  per Event, ohne die Abstimmung zu ändern.
- **Analoger Mischer** (MODC „Analoger Mischer“ aus der Original-GUI, dort ohne Firmware-Gegenstück, #16): Anteil
  der drei analogen Eingangspaare 0–100 % über die gespeicherte Mischung, mit Clickless gleitend.
- **Automatische Eingangswahl** per Events (MODC „CTRL-MUX I“ aus der Original-GUI, #17), z. B. auf USB, sobald
  dort Audio anliegt.
- **IR-Fernbedienung, Taste 2** schaltet den Eingang weiter (#27).
- **Loudness** (gehörrichtige Lautstärke) als neuer MODC-Block: hebt Bass und Höhen an, je leiser gehört wird,
  folgt der Lautstärke in 0,2-dB-Schritten (feiner einstellbar bis 0,01 dB, #51), einstellbar im vorhandenen
  Loudness-Dialog der Original-GUI. Optional **dynamisch**: Die Forge misst den Pegel des Eingangssignals selbst und
  reagiert so auch auf Dämpfung an der Quelle (PC, Android) und leise gemischte Musik (#51)
  (M6, #6; [Konfiguration](docs/port-changes.md#loudness-m6-6)).
- **I2C-Puffer schreiben** zur Diagnose (`set.1 i2c.B.A.buffer=[r,b1,…]`), damit lässt sich z. B. der Speicher der DSPs per
  PEEK auslesen (#51).
- **Biquad-Typ 17 BandPass**, im Original geplant, aber nie umgesetzt (#20).
- **Allpässe für den Phasenabgleich** (#48): Typ 21 Allpass 1. Ordnung (−90° bei f) und Typ 22 „Phase-Shift mit
  Wunschwinkel“ (z. B. −45° bei 80 Hz, die Firmware wählt Ordnung und Eckfrequenz). Am Gerät nachgemessen.
- **microSD wieder aktiv** (im Original aus Lizenzgründen entfernt): In der Original-GUI gespeicherte `.cfg`-Dateien
  als `preset_1.cfg` … auf die Karte kopieren, der Forge übernimmt sie beim nächsten Start in die Presets (#24,
  FatFs statt AVR-DOS; auf dem Gerät noch nicht getestet).
- **Behobene Fehler des Originals**, u. a.: `preset.erase=4` löscht bei 4 Presets nicht mehr Preset 0 (#33),
  Rohwert-Biquads `biquad.….raw` funktionieren (#35), `get event.N.value` antwortet (#36), Delays in ms/cm/inch
  lassen sich lesen (#21), keine Speicherzugriffe hinter Arrays und Tabellen bei ungültigen Werten.
- **Sicherer Werks-Reset**: Abstimmungen werden nur mit `FORGE_FACTORY_RESET=1` gelöscht, nicht bei jedem
  Versionswechsel.
- **Flashen unter Linux** aus VS Code, mit Backup, Gerätecheck und Rückweg auf die Originalfirmware.
- **Vier statt sieben Builds**, die Kennung (Modell, PCB) steht im EEPROM: `get.1 system.key.info.list` zeigt die
  möglichen, `set.1 system.key.info="CORED8-4.1"` wählt eine (#31).
- Rund **39 % kleiner** als das Original (V40-96ELN: 125 KB statt 205 KB, mit microSD; ohne die Debug-Texte des Originals, #41).

## Build-Optionen

In `platformio.ini` unter `[common]`:

| Option | Standard | Wirkung |
|---|---|---|
| `FORGE_BRINGUP` | 0 | 1 = vollständige Peripherie-Init wie das Original. 0 = sichere Variante: Erkennung und Protokoll, Peripherie im Reset, **kein Ton** |
| `FORGE_AUDIO` | 0 (sicher), 1 (`-bringup`, seit 2.0.0) | 1 = Remote-Out (Endstufen) nach dem Laden des Presets wie im Original einschalten |
| `FORGE_FACTORY_RESET` | 0 | 1 = Werks-Reset wie im Original (löscht Abstimmungen und Presets), braucht `FORGE_BRINGUP=1` |
| `FORGE_DSP_OPT` | 0 (sicher), 1 (`-bringup`) | 1 = optimierte DSP-Programme: gleiche Signalverarbeitung, rund 130 bzw. 40 Takte je Sample weniger (Platz für mehr Filter, #47). V30-96LN behält sein Originalprogramm |

Lizenz: CC BY-NC-SA 4.0 (`LICENSE.txt`), wie das Original. `lib/fatfs`: FatFs R0.16 von ChaN (BSD-1-Clause, `lib/fatfs/LICENSE.txt`). `tools/c45b3-host` und die Bootloader-Quellen in
`tools/c45b3-host/tests/ctarget/upstream` stehen unter MIT (chip45boot3, Erik Lins).

## VS Code / PlatformIO
0. Einmalig `tools/get-toolchain.sh`: lädt avr-gcc 16.2.1 mit einem Compiler-Fix (Release
   `toolchain-avr-gcc-16.2.1-p1` dieses Repos, Linux x86_64, sha256 im Skript) und verlinkt ihn nach
   `.toolchain/avr-gcc`. Patch, Testfall und Build-Skript: `tools/toolchain/`. Grund: BASCOM rechnet Biquads in 64-Bit-`double`, das
   PlatformIO-Paket (avr-gcc 7.3) kann nur 32 Bit. Gebaut wird mit `-mdouble=64`.
1. Projekt öffnen, unten in der Statusleiste die Umgebung (= Variante) wählen.
2. `upload_port` in `platformio.ini` fest auf den CP2102 des Forge setzen (`/dev/serial/by-id/usb-Silicon_Labs_CP2102…`).
3. *PlatformIO: Upload*. Ablauf:
   - `get.1 system.key.info`: Modell und PCB des angeschlossenen Forge müssen zur Umgebung passen, sonst Abbruch.
     V30-96ELN und V40-96ELN melden beide `96ELN-4.0`, hier die Umgebung bitte selbst richtig wählen.
   - **Bootloader:** Läuft schon die Port-Firmware, schickt das Tool `set.1 system.boot` und der Forge geht von selbst
     in den Bootloader (kein Taster). Mit der Original-Firmware startet er dabei nur neu: dann während der Wartezeit
     (60 s) den Forge ausschalten, den Taster neben dem USB-Port halten, einschalten, halten bis die orange LED blinkt.
   - Vorher: Abstimmung (geladenes Preset, Kanalnamen, IR-Codes, MODC) nach `backups/<datum>/tuning-before.cfg`
     sichern, im Format der Original-GUI.
   - Flash schreiben (Seiten werden im Bootloader geprüft), zurücklesen, Anwendung starten.
   - Danach: Abstimmung erneut lesen und vergleichen. Mit `[forge] restore_tuning = yes` werden Abweichungen
     zurückgeschrieben (nur die Unterschiede, nur bei gleichem Modell).
4. Project Tasks → Custom:
   - *Forge: versions*: Version von Bootloader und Firmware.
   - *Forge: backup*: Flash und EEPROM nach `backups/<Datum>/`, soweit lesbar.
   - *Forge: flash release 1.11.6000 (…)*: Rückweg auf die offizielle Firmware der Variante. Sie wird aus dem
     GitHub-Release [0.0.1116000](https://github.com/psych0d0g/forge-dsp-firmware/releases/tag/0.0.1116000) geladen
     und gegen `scripts/release-0.0.1116000.sha256` geprüft (Cache `.pio/release/`).
5. Monitor: 38400 Baud, CR LF. Format wie die GUI: `get.1 system.ping`, `set.1 <register>=<wert>;<cs>` (`1` = dieses
   Gerät, `cs` = Summe der Zeichen ab dem Register, 2 Hex). Ohne Leerzeichen nach dem Befehlszeichen liest das
   Original das erste Zeichen als Ziel (`<system…` = Slave).

c45b3 braucht nur pyserial, das liefert PlatformIO mit. CLI: `pio run -e V4x-96ELN-3 -t upload`.

## Aufbau
- `src/`: gemeinsamer Code aller Varianten (`src/cmd/`: Textprotokoll), `include/`: Header.
- `tests/`: `test_cmd.py` übersetzt die Firmware-Module nativ für jede Variante (`make -C tests/native`) und prüft
  Protokoll, EEPROM-Inhalte, Biquads (gegen eine unabhängige Referenz) und die Peripherie auf einem I2C-Modell.
- `docs/port-changes.md`: Ergänzungen und Abweichungen gegenüber dem Original.
- `tools/toolchain/`: Compiler-Patch, Testfall und Build-Skript für die gepinnte Toolchain.
- `src/flavours/<Variante>/`: **generiert** mit `tools/gen_flavour.py` aus dem BASCOM-Quelltext. `flavour.h` enthält alle
  `const` des aufgelösten Builds als `BAS_*` und die Pin-Aliase als `PIN_*`, `flavour_pins.c` die Pin-Init aus `pins.bas`.
  Prüfen: `python3 tools/gen_flavour.py <forge-dsp/firmware> src/flavours --check`.
- `tools/bas_resolve.py`: löst `$include`/`#IF` eines BASCOM-Builds auf (`docs/inventory-*.md`, `--flat` = nur aktiver Code
  mit Datei:Zeile, Referenz beim Portieren).
- `tools/c45b3-host/`: Linux-Host für chip45boot3, mit Tests gegen ein Modell und gegen den nativ übersetzten Original-
  Bootloader-Code. `tests/pty_bridge.py` emuliert Firmware + Bootloader an einem pty für Upload-Tests ohne Hardware.
- Offizielle HEX 1.11.6000 aller Varianten: GitHub-Release `0.0.1116000` (nicht im git).

## Sicherheit
- Der Bootloader-Bereich ist per Lockbits geschützt (BLBB = WLOCK, am Gerät bestätigt: lesbar, nicht beschreibbar). Das Tool schreibt nie dorthin und sendet die
  abschließende Leer-Frame der Windows-GUI nicht (sie würde die Seite hinter dem Image löschen).
- Vor dem ersten eigenen Flashen: *Forge: backup*, Abstimmung über die Original-GUI sichern.
- Remote-Out (Endstufen) schalten die Vollstart-Builds (`-bringup`, Release ab 2.0.0) wie das Original nach dem Laden des
  Presets ein; die sicheren Builds lassen ihn aus.
- Abstimmungen werden ohne `FORGE_FACTORY_RESET=1` nie gelöscht; Rückweg jederzeit über *Forge: flash release 1.11.6000*.

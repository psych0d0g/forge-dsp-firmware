# Ergänzungen und Abweichungen gegenüber BASCOM 1.11.6000

Die Port-Firmware verhält sich nach außen wie die Originalfirmware 1.11.6000: gleiches Textprotokoll, gleiches
EEPROM-Layout, gleiche Antworten an die GUI. Die Abstimmung eines Geräts bleibt beim Wechsel erhalten (am Gerät
geprüft). Dieses Dokument listet alles, was davon abweicht: neue Funktionen, behobene Fehler des Originals und
bewusste Unterschiede. Im Quelltext sind die Stellen mit `PORT:` markiert.

## Neue Funktionen

| Funktion | Befehl / Stelle | Issue |
|---|---|---|
| **Eingangswahl** für die aktive Mixer-Position (Mux), mit Stummschaltung beim Umschalten und Speichern | `set/get mixer.in` (= `mixer.input`), `get mixer.in.max`, Ereignis `#.1 mixer.in=N` | #23 |
| **Ereignisse 45–48** `SOURCE.1–4.MUX` (in `events.txt` dokumentiert, im Original nie gesetzt): Mux je Mixer-Position, nutzbar in MODC-Blöcken | Events 45–48 | #23 |
| **Biquad-Typ 17 BandPass** (im Original geplant, fiel auf Bypass zurück). Formel wie die Vorschau der GUI (`CalculatorBandPass`), Spitzenverstärkung = Q | `biquad.CH.N.calc=[17,f,0,q,0,128]` | #20 |
| **Delay in ms/cm/inch lesen** liefert den Wert (Original: nur der Antwortkopf, kein Zeilenende) | `get delay[.out\|.in].N.ms` | #21 |
| **Einzelne Events lesen** (Original antwortet nie) | `get event.N.value`, `get event.N.init` | #36 |
| **Rohwert-Biquads** werden wirklich übernommen (Original nutzt die Werte des vorherigen Befehls) | `biquad.CH.N.raw=["A2","A1","B2","B1","B0"]` | #35 |
| **microSD wieder aktiv** (im Original für die Lizenz entfernt, AVR-DOS): Beim Start werden `preset_1.cfg` … `preset_N.cfg` von der Karte in die Presets geladen, wenn sich das Dateidatum geändert hat. Die Dateien sind die `.cfg`-Dateien der Original-GUI (Abschnitt mit dem Modellnamen, Zeilen `x = "…"`). FatFs R0.16 statt AVR-DOS. Slot auf der USB-Audio-Platine (V40/V41), SPIE, Kartenschalter PD0 | Bring-up-Variante, Karte FAT12/16/32 | #24 |
| **Vier Builds statt sieben**: Varianten mit identischer Firmware teilen sich einen Build, die Kennung (Modell, PCB) steht im EEPROM (0xF00) und wird beim Upload aus der alten Firmware übernommen | `get system.key.info.list`, `set system.key.info="MODEL-PCB"`, Upload `--keep-identity` | #31 |
| **Loudness** (MODC-Typ 0x6C, im Original deklariert, nie umgesetzt; Feldlayout wie der Loudness-Dialog der Original-GUI): hebt Bass und Höhen per Shelf an, je tiefer die Lautstärke unter einem Referenzwert liegt (gehörrichtige Lautstärke). Belegt die letzten zwei Biquads der gewählten Ausgangspaare, führt in 0,2-dB-Schritten nach, ohne Stummschaltung und ohne EEPROM-Schreiben | Original-GUI (MODC „CTRL-LOUDNESS“) oder `set modc.N=[1,108,…]`, Details unten | #6 |
| **Startbefehle** (im Original angelegt, aber nie ausgeführt und nicht setzbar): bis zu 8 Befehle à 32 Zeichen im EEPROM, laufen einmal nach dem ersten Preset-Laden eines Starts. `=` im Befehl als `:` schreiben (der Parser behält nur den Text nach dem letzten `=`); Neustart/Werks-Reset werden abgelehnt | `set system.startup.N="set.1 gain.out.2:150"`, `get system.startup.N`/`.*`, `""` löscht | #40 |
| **Eingangs-Umschalter** (MODC-Typ 0x60 `mux_I`, im Original deklariert, Dialog in der Original-GUI): bis zu 4 Events als Prioritätenliste, Event 4 am höchsten, Set 1 ist der Rückfall; der Eingang des höchsten aktiven Events (Set k) geht an die aktive Mixer-Position, wenn sie angehakt ist (Flags 1, Bits 0/2/4/6 = Positionen 1–4). Speichert nicht ins EEPROM | Original-GUI (MODC „CTRL-MUX I“) oder `set modc.N=[1,96,01000000,E1,E2,E3,E4,S1S2S3S4]` | #17 |
| **Boot-Sektion lesen** (Diagnose, nur lesend): 64 Byte je Block aus 0x40000, zeigt ob die Lockbits das Auslesen des Bootloaders aus der Anwendung erlauben; Skript `tools/dump_bootsection.py` | `get system.hardware.boot.N` (N 1–128) | #22 |
| **IR-Befehl 2** (im Original frei, „maybe use later“): schaltet zum nächsten Eingang (`mixer.in`) der aktiven Mixer-Position, nach dem letzten wieder auf 1. Die Java-GUI hat für Platz 2 keinen Eintrag; anlernen per Befehl | `set extender.ir.custom.2=[header,adresse,befehl]` | #27 |
| **Bootloader-Versionsstring**: Der Bootloader liest die Firmware-Version (`FW_VERSION:xx.yy`); das Original enthält keinen | chip45boot3 0x12 | |
| **Linux-Flasher** chip45boot3 mit Backup, Gerätecheck und Rückweg auf 1.11.6000, eingebunden in PlatformIO/VS Code | `tools/c45b3-host`, *Forge:*-Tasks | |

## Behobene Fehler des Originals

Alle betreffen Fehlerpfade oder Werte außerhalb des gültigen Bereichs. Gültige Eingaben verhalten sich wie im Original.

| Original | Port | Issue |
|---|---|---|
| `preset.copy/erase/select/loaded` akzeptieren die Preset-Anzahl selbst: mit 4 Presets löscht `preset.erase=4` Preset 0, mit 8 Presets wird hinter den Adresstabellen gelesen | nur 0 … Anzahl−1 | #33 |
| Typwechsel eines MODC-Blocks N löscht die Event-Felder N, N+32, N+64 und N+96, also auch fremde wie Remote-Out (3) oder Shutdown (4) | nur der Ausgang des Blocks (96+N) | #38 |
| V30-96ELN: `chk_hardware_other` erwartet den Taktbaustein (Bit 11), den der Build nicht hat, und nicht DAC/CT7302 #20 (Bits 2, 3), die er erkennt. Die Prüfung am Ende von `systeminit` schlägt immer fehl, die Ausgänge bleiben stumm | erwartet 0x007D (wie V40-96ELN ohne Si5338), auch in `system.feature.attributes.hw` | #32 |
| Nie gespeichertes Preset (Mixer-Position 0xFF): `api_set_mix_reg` liest weit hinter den Mixerdaten und schreibt RAM-Inhalt als Koeffizienten in den DSP | übersprungen | |
| Ungültiger Mux-Wert liest hinter der Mux-Tabelle | geprüft | |
| `gain.in`/`gain.spdif` werden gegen die Zahl der Ausgänge geprüft; Eingang 7 schreibt in den Speicher von Ausgang 1. Kanal 0 schreibt vor das Array | je Gruppe geprüft, 0 abgelehnt | |
| `delay.in` mit allen Werten schreibt Kanal 7/8 über die Ausgangs-Delays 1/2; unbekannte Einheit meldet Fehler **und** „ok“ und setzt einen alten Wert; `delay.out.*` nummeriert falsch | korrigiert (Lesen liefert weiter 8 Werte wie das Original) | #21 |
| `mixer.1.2` (fehlendes Pfadelement) überschreibt die Eingangswahl von Position 1 | alle drei Elemente müssen Zahlen sein | |
| Biquad-Grenzwerttabellen werden für Wert 7/8 hinter dem Tabellenende gelesen; `biquad.CH.0` adressiert den letzten Biquad des vorherigen Kanals | nur Werte 2–6, Biquad 0 abgelehnt | |
| `preset.copy` ohne geladenes Preset kopiert von einer ungültigen Adresse | abgelehnt | |
| MODC/IR/Event-Nummern 0 oder außerhalb des Arrays schreiben bzw. lesen vor/hinter die Arrays | abgelehnt bzw. 0 | |
| Divisionen durch 0 in MODC limiter/calculator/gain und im Si5338-Teiler (Ergebnis undefiniert) | definiertes Ergebnis, Ausgang unverändert | |
| `errorcount` schreibt bei voller Liste Index 0 (Byte vor der Liste im EEPROM) | verworfen | |
| Rücklese-Prüfung der DSP-Schreibzugriffe wiederholt unendlich | höchstens 8 Versuche, Fehler wird gezählt | |
| `i2c.B.A.dump=[r,255]` endet nie; `.buffer` liest hinter den Puffer | begrenzt | |

Bekannt, aber wie im Original belassen (damit die GUI dasselbe sieht): `get modc.enable` liefert für belegte Blöcke 0.

## Bewusste Unterschiede

| Thema | Original | Port |
|---|---|---|
| **Werks-Reset** (leeres/fremdes EEPROM, Versionswechsel, `system.reinit`, `system.hardware.memory.eram`) | löscht immer alle Abstimmungen und Presets | nur mit `FORGE_FACTORY_RESET=1`; sonst Standardwerte nur im RAM, nichts wird gelöscht |
| **Remote-Out** (Endstufen) | nach den Startverzögerungen an | nur mit `FORGE_AUDIO=1` |
| **Peripherie-Init** | immer | nur mit `FORGE_BRINGUP=1`; ohne: sichere Variante (Erkennung, Protokoll, kein Ton) |
| Pin-Interrupts (Masse-Schalter, USB-DTR, Überlauf, Remote-In) | Interrupt je Flanke | in der Hauptschleife abgefragt (gleiche Werte; IR-Abtastung und Timer bleiben Interrupts) |
| I2C | wartet unbegrenzt | Zeitüberschreitung nach ~10 ms, als Fehler gemeldet; `bitwait sda` beim DSP-Start höchstens 2 s |
| EEPROM | jedes Schreiben | gleiche Bytes werden nicht neu geschrieben (weniger Verschleiß) |
| SD-Presets: `store_preset` nach dem Laden | bleibt auf der zuletzt geladenen Datei; ist das zufällig das gewählte Preset, lädt MODC es nicht in die DSPs | zurückgesetzt, MODC lädt das gewählte Preset immer |
| SD-Presets: Datum der Dateien | nur V30-96LN hat Platz dafür im EEPROM | V30-96LN wie im Original, die übrigen Varianten ab EEPROM 0xE00 (vom Original ungenutzt) |
| Debug-Texte (`system.debug.mode`) auf UART2/UART4 | ja | nicht portiert (#41) |
| XMOS-Firmware-Update über SPI | vorhanden, im Betrieb ungenutzt | nicht portiert (#42) |

## Build-Optionen

| Option | Standard | Wirkung |
|---|---|---|
| `FORGE_BRINGUP` | 0 | 1 = vollständige Peripherie-Init wie das Original (DAC, DSP, S/PDIF, Preset, MODC, IR, ADC) |
| `FORGE_AUDIO` | 0 | 1 = Remote-Out (Endstufen) wie im Original einschalten |
| `FORGE_FACTORY_RESET` | 0 | 1 = Werks-Reset wie im Original (braucht `FORGE_BRINGUP=1`) |

## Toolchain

- avr-gcc 16.2.1 mit eigenem Patch (Release `toolchain-avr-gcc-16.2.1-p1`): behebt einen internen Compilerfehler in
  `lower-subreg`, der auch in GCC 17 noch vorhanden ist. Patch, Testfall und Build-Skript in `tools/toolchain/`.
- `-mdouble=64` wird auch an den Linker gegeben (`scripts/linkflags.py`). Ohne das bindet der Linker die
  32-Bit-Mathematik der avr-libc ein (`sin`, `cos`, `sqrt` als Float-Versionen). `scripts/check_math.py` bricht den Build in
  diesem Fall ab.
- libgcc hat kein 64-Bit-`lround`; der Port rundet selbst (`bas_round_i32`).

## Prüfstand

- Nativ: 99 Tests, alle 7 Varianten (Protokoll, EEPROM-Layout gegen die Release-HEX, Biquads gegen eine unabhängige
  Referenz, Peripherie auf einem I2C-Modell, SD-Karte als FAT-Image).
- Am Gerät (DEV-Forge V40-96ELN): Start auf dem EEPROM des Originals ohne Werks-Reset, alle Bausteine erkannt,
  Biquad-Koeffizienten aus dem DSP bitgenau gleich der Referenz, Abschalten über Remote-In (#39).

## Loudness (M6, #6)

MODC-Block vom Typ 108 (0x6C). Die Felder folgen dem Loudness-Dialog, den die Original-GUI schon enthält
(`CTRL_LOUDNESS_Settings`), damit sie den Block einstellen kann. Konfiguration per Befehl:
`set.1 modc.N=[an,108,FLAGS,E1,E2,E3,E4,SETS]` (FLAGS und SETS je 4 Bytes hex).

| Feld | Bedeutung | 0 = Vorgabe |
|---|---|---|
| Flags 1, Bits 0–3 | Ausgangspaare 1/2, 3/4, 5/6, 7/8: deren letzte zwei Biquads werden belegt (vorletzter Bass-Shelf, letzter Höhen-Shelf). Port: Bit 4 = Eingangsweg 35/36 | keine |
| Flags 3 / Flags 4 | Bass- / Höhen-„Gain“ 1–250 (GUI zeigt /100): Anhebung in dB je dB Dämpfung unter der Referenz | 0,40 / 0,15 |
| Sets 1+2 / Sets 3+4 | Frequenz des Bass- / Höhen-Shelfs in Hz (Low-Byte zuerst) | 100 / 10 000 Hz |
| Event 1 | „Loudness Input“: Lautstärke 0–255 in der Skala von `gain.*` (0,5 dB je Schritt, 223 = 0 dB), z. B. ein Lautstärke- oder IR-Block | `gain.spdif.1` |
| Flags 2 (Port, in der GUI ausgeblendet) | Referenz: ab diesem Wert keine Anhebung | 223 (0 dB) |
| Ausgang (Event 96+N) | aktuelle Bass-Anhebung in 0,5-dB-Schritten | |

Höchstens 12 dB Anhebung. Beispiel: `set.1 modc.9=[1,108,01DF280F,70,0,0,0,5000A00F]` = Ausgänge 1/2, folgt Event 70, Bass
80 Hz × 0,40, Höhen 4 kHz × 0,15; 20 dB unter der Referenz ergibt Bass +8 dB und Höhen +3 dB.

Auslegung: Die GUI nennt die Flags 3/4 „Gain“ (Anzeige x,xx dB). Als feste Anhebung wären 0,01–2,5 dB zu wenig, deshalb
gilt der Wert hier als Anstieg je dB Dämpfung. Die Bedeutung von Event 1 („de/aktiviert über Event“ im Dialog,
„Loudness Input“ in der Liste) ist im Original nicht festgelegt; hier ist es die Lautstärke, ein- und ausgeschaltet wird
über den Block selbst.

Hinweise: Die belegten Biquads überschreibt der Block; Filter, die die GUI dort anzeigt, wirken nicht. Wird ein
Ausgangspaar abgewählt, gehen seine Shelves auf 0 dB. Nach dem Laden eines Presets setzt der Block die Shelves neu.
Offen: ob der TAS3108 einen Koeffizientenblock ohne Knacken übernimmt (Messung am Gerät).


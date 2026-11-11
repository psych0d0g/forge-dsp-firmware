# Portierungsplan BASCOM 1.11.6000 → C (alle Varianten)

Umfang je Variante (`docs/inventory-*.md`): V30-96LN 11 493, V30-96ELN 11 757, V40-96ELN 12 431, V40/V41-CORED8 12 428,
V40/V41-96ELN-3 12 501 aktive Zeilen. Größte Module: cmd, dsp, ctrl. V40 und V41 unterscheiden sich nur in
`board_version` und `ext_usbaudio_fw`. CORED8 und 96ELN weichen von 96ELN-3 um rund 430 Zeilen ab, V30-96ELN um
rund 1150, V30-96LN um rund 3200 (andere DSP-Ausstattung BQ4x42_4x30_2x26SYM). Referenz: `ref/active-<Variante>.bas`
(`tools/bas_resolve.py --flat`) und die Release-HEX.

Varianten-Technik
- Ein gemeinsamer Quelltext. Jede BASCOM-Bedingung (`#IF modul_… `) wird zu `#if BAS_MODUL_…` aus dem generierten
  `src/flavours/<Variante>/flavour.h`. Damit bleiben die Schalter aller Varianten exakt wie im Original.
- Unterschiede in `devices/*/systeminit.bas`, `interrupts.bas` und `processflows/` (DSP-Adressen, Kanaltabellen) werden
  ebenfalls generiert oder je Variante übersetzt. Ziel ist, dass `gen_flavour.py --check` alles Variantenabhängige abdeckt.
- Jede Stufe gilt erst als fertig, wenn alle sieben Umgebungen bauen und die Host-Tests für alle Varianten laufen.

Grundsätze
- Verhalten nach außen gleich: Textprotokoll (commands.txt, GUI, forge-control), Init-Reihenfolge und Stummschaltung,
  Speicherlayout im EEPROM (ERAM mapped, 0x1000–0x1FFF), damit die vorhandene Abstimmung erhalten bleibt.
- Jedes Modul bekommt native Host-Tests (wie `forge-tools/tests/ctarget`): Protokoll-Parser, Koeffizientenrechnung
  (BASCOM single ↔ C float numerisch vergleichen), EEPROM-Layout.
- Flashen auf den Forge nur mit Freigabe des Users; Rückweg `flash_release`.
- Solange die Audiokette nicht vollständig portiert ist, bleibt Remote-Out (Endstufen) aus.

Stand und Aufgaben: GitHub-Issues und Milestones M1–M6 (#1–#6), Hardware- und Sicherheitsfragen #7–#13.

| Stufe | Inhalt | Prüfung |
|---|---|---|
| M0 ✔ (Build, alle 7) | Takt, Pins (generiert), Selbsthaltung/Shutdown, Watchdog, USB-COM, `<system.key.info`, `>system.bootloader=1`, FW_VERSION für 0x12 | am Gerät: LED, `<system.firmware`, Upload ohne Aus/Ein, Shutdown über Remote-In |
| M1 (läuft) | Kern portiert: Parser, Header, Fehler, Routing, 4 Kanäle, system.{boot,echo,firmware,info,key,ping}. Offen: übrige Module. Textprotokoll komplett (cmd/v20: Parser, Prüfsumme, Header, Fehler, Events), 4 serielle Kanäle (USB-COM, Mediaport, RS485 Master/Slave, Weiterleitung), Baudraten `system.baud.*`, Debug-Ausgabe | Host-Tests gegen Antworten aus dem Quelltext; am Gerät Vergleich mit Mitschnitt der Originalfirmware (Schritt 2) |
| M2 | Register/Store: EEPROM-/ERAM-Layout (store/, eeprom/), Presets, `api_store`-Logik | Layout-Test gegen EEPROM-Dump bzw. Export über das Textprotokoll |
| M3 | Peripherie: I2C-Busse 1/2, PCA9554/9555, Takt (Si5338), CT7302 (SRC/USB-I2S), AD/DA, TAS3108 ×3 aus AT24C512, Reset-/Mute-Sequenz | I2C-Mitschnitt Original ↔ eigene Firmware (Logic Analyzer), Ton erst nach Freigabe, leise |
| M4 | API/ctrl: gain, mute, mixer/mux, biquads (`dsp_bq_calcto5coeff`), delay/phase, modc, events (Grounds, Quad, ADC, Temperatur), IR-Empfänger, Remote-Out | Protokoll-Parität, Frequenzgang-Messung über S/PDIF-OUT |
| M5 | Parität/Abnahme: alle Befehle der GUI, forge-control gegen echtes Gerät | Abnahme durch User |
| M6 | Loudness in der Firmware (Issue) | Messung + Hörtest |

Offene Punkte: siehe Issues #9 (Bootloader-Start), #10 (V30/V40-96ELN), #11 (Generator), #12 (Baudrate), #13 (chip45boot3).

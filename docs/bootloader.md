# Bootloader des Forge (chip45boot3, Version 1.0)

Ausgelesen am 11.11.2026 auf einem Forge DSP 96ELN (PCB 4.0) mit der Port-Firmware über `get.1 system.hardware.boot.N`
(nur lesend, `tools/dump_bootsection.py`): 8 KiB ab 0x40000, davon 3774 Byte belegt, zweimal gelesen und identisch.
Die Lockbits lassen das Lesen aus der Anwendung zu (Boot-Sektion `BLBB` = WLOCK, nicht RWLOCK). Abbild unter
[`bootloader/`](../bootloader/) (chip45boot3, MIT).

## Startbedingung

Fuse `BOOTRST = BOOTLDR`: Jeder Reset beginnt im Bootloader. Er bleibt dort, wenn

1. die Anwendung leer ist (erstes Wort des Flash = 0xFF), **oder**
2. **PB6 High** ist (Taster neben dem USB-Port; der Bootloader schaltet dafür kurz einen Pull-down zu), **oder**
3. **EEPROM-Byte 0 = 0xAA und Byte 1 = 0x55**.

Sonst springt er in die Anwendung. Beim Verlassen des Bootloaders setzt er EEPROM 0 und 1 auf 0xFF, das Magic wirkt
also nur einmal. Die Firmware des Originals legt diese Bytes an (`eeprom_bootloader_seq_1/2 = &HAA/&H55`,
`libs/declare.bas`), schreibt sie aber nie. Deshalb startete `system.boot` dort nur neu.

**Port:** `set.1 system.boot` schreibt 0xAA/0x55 und startet neu, der Forge landet im Bootloader, ohne Taster
(am DEV-Gerät geprüft, 11.11.2026: orange LED blinkt, `c45b3 info` antwortet, nach `start` läuft die Anwendung normal,
das Magic ist also gelöscht). Der Bootloader wartet ohne Zeitlimit auf den Host. Er kalibriert die Baudrate am ersten
Zeichen; Bursts, die noch während der letzten Sekunde der Anwendung kommen, verstellen sie. `c45b3 --enter` wartet
deshalb 2 s nach dem Befehl.

## Wartezeit (Disassembly, 14.11.2026)

Ist eine Startbedingung erfüllt, startet der Bootloader die RTC mit 1,024 kHz (Vergleichswert 60000) und wartet auf das
erste Zeichen des Hosts zur Baudraten-Erkennung. Kommt bis zum Vergleich (58,6 s) nichts, setzt er EEPROM 0/1 auf 0xFF und
startet die Anwendung. Dabei blinkt die orange LED (PA1) aus der Timer-Interrupt-Routine. PB6 liest er direkt nach dem
Zuschalten des Pull-downs ohne Wartezeit. Die RTC hält er beim Start der Anwendung nur an, der Zähler bleibt: Die
Port-Firmware gibt ihn als letzten Wert von `get system.startup.time` aus (ms im Bootloader, 0 = direkt gesprungen).

## Weitere Befunde

- Host-Schnittstelle: USARTC0 (USB-COM über CP2102). Die RS485-Ports kennt der Bootloader nicht; Geräte hinter dem
  Slave-Port lassen sich also nicht durch den Master flashen (#22).
- Die Boot-Sektion enthält Atmels `sp_driver` (Seitenpuffer laden, App-Seite löschen/schreiben, Boot-Seite schreiben,
  CRC) mit festen Adressen. Schreiben in die Boot-Sektion verhindert WLOCK; ändern lässt sich das nur per Chip-Erase
  über PDI (Header `X-PDI` auf der Platine).
- Fuses laut Original-Doku: `BOOTRST = BOOTLDR`, `RSTDISBL = ON`, BOD 2,9 V.

# Bootloader des Forge (chip45boot3, Version 1.0)

Ausgelesen am 11.11.2026 auf einem Forge DSP 96ELN (PCB 4.0) mit der Port-Firmware über `get.1 system.hardware.boot.N`
(nur lesend, `tools/dump_bootsection.py`): 8 KiB ab 0x40000, davon 3774 Byte belegt, zweimal gelesen und identisch.
Die Lockbits lassen das Lesen aus der Anwendung zu (Boot-Sektion `BLBB` = WLOCK, nicht RWLOCK). Das Binary liegt nicht im
Repository (Build des Herstellers; chip45boot3 selbst steht unter MIT).

## Startbedingung

Fuse `BOOTRST = BOOTLDR`: Jeder Reset beginnt im Bootloader. Er bleibt dort, wenn

1. die Anwendung leer ist (erstes Wort des Flash = 0xFF), **oder**
2. **PB6 High** ist (Taster neben dem USB-Port; der Bootloader schaltet dafür kurz einen Pull-down zu), **oder**
3. **EEPROM-Byte 0 = 0xAA und Byte 1 = 0x55**.

Sonst springt er in die Anwendung. Beim Verlassen des Bootloaders setzt er EEPROM 0 und 1 auf 0xFF, das Magic wirkt
also nur einmal. Die Firmware des Originals legt diese Bytes an (`eeprom_bootloader_seq_1/2 = &HAA/&H55`,
`libs/declare.bas`), schreibt sie aber nie. Deshalb startete `system.boot` dort nur neu.

**Port:** `set.1 system.boot` schreibt 0xAA/0x55 und startet neu, der Forge landet im Bootloader, ohne Taster.

## Weitere Befunde

- Host-Schnittstelle: USARTC0 (USB-COM über CP2102). Die RS485-Ports kennt der Bootloader nicht; Geräte hinter dem
  Slave-Port lassen sich also nicht durch den Master flashen (#22).
- Die Boot-Sektion enthält Atmels `sp_driver` (Seitenpuffer laden, App-Seite löschen/schreiben, Boot-Seite schreiben,
  CRC) mit festen Adressen. Schreiben in die Boot-Sektion verhindert WLOCK; ändern lässt sich das nur per Chip-Erase
  über PDI (Header `X-PDI` auf der Platine).
- Fuses laut Original-Doku: `BOOTRST = BOOTLDR`, `RSTDISBL = ON`, BOD 2,9 V.

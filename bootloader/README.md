# Bootloader des Forge (chip45boot3 1.0)

Abbild der Boot-Sektion (8 KiB ab 0x40000) eines Forge DSP 96ELN (PCB 4.0), am 11.11.2026 über die Port-Firmware
ausgelesen (`tools/dump_bootsection.py`), zweimal gelesen und identisch.

| Datei | Inhalt |
|---|---|
| `chip45boot3-forge-1.0.bin` | Rohabbild, Offset 0 = Flash-Adresse 0x40000 |
| `chip45boot3-forge-1.0.hex` | dasselbe als Intel-HEX an 0x40000 |
| `SHA256SUMS` | Prüfsummen |
| `LICENSE.chip45boot3` | Lizenz von chip45boot3 (MIT) |

Analyse (Startbedingung, Bootloader-Sequenz im EEPROM, `sp_driver`): [docs/bootloader.md](../docs/bootloader.md).

Nur zur Referenz und Sicherung. Einspielen geht ausschließlich per PDI-Programmer (Header `X-PDI`); die Boot-Sektion
ist gegen Schreiben aus der Anwendung gesperrt.

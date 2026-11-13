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
| **Biquad-Typ 21 Allpass 1. Ordnung**: Betrag 1, Phase 0 bis −180°, −90° genau bei f (bilinear mit Vorverzerrung, H = (c + z⁻¹)/(1 + c·z⁻¹), c = (tan(ω0/2) − 1)/(tan(ω0/2) + 1)). Nur f wird verwendet. Gedacht für den Phasenabgleich, z. B. Sub gegen Front an der Trennfrequenz | `biquad.CH.N.calc=[21,f,0,0,0,128]` | #48 |
| **Biquad-Typ 22 Phase-Shift mit Wunschwinkel**: Phasenverzögerung von b Grad (1–359) genau bei f. Unter 180° wird ein Allpass 1. Ordnung gewählt (q ohne Wirkung), ab 180° einer 2. Ordnung mit der Güte q; die Firmware berechnet dafür die Eckfrequenz. b = 0 oder ≥ 360 = Bypass. Clickless gleitet innerhalb einer Ordnung, der Wechsel über 180° (1. ↔ 2. Ordnung) schaltet kurz stumm | `biquad.CH.N.calc=[22,80,0,707,45,128]` = −45° bei 80 Hz | #48 |
| **Biquad-Typ 20 Phase-Shift** (aus dem Original, Allpass 2. Ordnung nach RBJ, −180° bei f) jetzt gegen den Frequenzgang getestet statt nur gegen dieselbe Formel. Am Gerät gemessen (12.11.2026, Typen 20, 21, 22): Phasengang innerhalb ±1,5° der Theorie über 100 Hz (Phasendifferenz zweier Ausgänge, `forge_measure.py phase`) | – | #48 |
| **Delay in ms/cm/inch lesen** liefert den Wert (Original: nur der Antwortkopf, kein Zeilenende) | `get delay[.out\|.in].N.ms` | #21 |
| **Einzelne Events lesen** (Original antwortet nie) | `get event.N.value`, `get event.N.init` | #36 |
| **Rohwert-Biquads** werden wirklich übernommen (Original nutzt die Werte des vorherigen Befehls) | `biquad.CH.N.raw=["A2","A1","B2","B1","B0"]` | #35 |
| **microSD wieder aktiv** (im Original für die Lizenz entfernt, AVR-DOS): Beim Start werden `preset_1.cfg` … `preset_N.cfg` von der Karte in die Presets geladen, wenn sich das Dateidatum geändert hat. Die Dateien sind die `.cfg`-Dateien der Original-GUI (Abschnitt mit dem Modellnamen, Zeilen `x = "…"`). FatFs R0.16 statt AVR-DOS. Slot auf der USB-Audio-Platine (V40/V41), SPIE, Kartenschalter PD0 | Bring-up-Variante, Karte FAT12/16/32 | #24 |
| **Vier Builds statt sieben**: Varianten mit identischer Firmware teilen sich einen Build, die Kennung (Modell, PCB) steht im EEPROM (0xF00) und wird beim Upload aus der alten Firmware übernommen | `get system.key.info.list`, `set system.key.info="MODEL-PCB"`, Upload `--keep-identity` | #31 |
| **I2C-Puffer schreiben** (Diagnose): mehrere Bytes in ein Register, z. B. TAS3108 PEEK/POKE (Register 0x06: `[0,Speicher,Adr-High,Adr-Low]`, Speicher 1 = Koeffizienten, 2 = Daten; danach `get i2c.B.A.buffer=[7,8]`). EEPROMs gesperrt wie beim Lesen | `set.1 i2c.B.A.buffer=[r,b1,…,bn]` (n = 1–7, b1 zuerst auf dem Bus) | #51 |
| **Loudness** (MODC-Typ 0x6C, im Original deklariert, nie umgesetzt; Feldlayout wie der Loudness-Dialog der Original-GUI): hebt Bass und Höhen per Shelf an, je tiefer die Lautstärke unter einem Referenzwert liegt (gehörrichtige Lautstärke). Belegt die letzten zwei Biquads der gewählten Ausgangspaare, führt in 0,2-dB-Schritten nach, ohne Stummschaltung und ohne EEPROM-Schreiben | Original-GUI (MODC „CTRL-LOUDNESS“) oder `set modc.N=[1,108,…]`, Details unten | #6 |
| **Startbefehle** (im Original angelegt, aber nie ausgeführt und nicht setzbar): bis zu 8 Befehle à 32 Zeichen im EEPROM, laufen einmal nach dem ersten Preset-Laden eines Starts. `=` im Befehl als `:` schreiben (der Parser behält nur den Text nach dem letzten `=`); Neustart/Werks-Reset werden abgelehnt | `set system.startup.N="set.1 gain.out.2:150"`, `get system.startup.N`/`.*`, `""` löscht | #40 |
| **Clickless EQ** (abschaltbar, Vorgabe aus): `biquad.CH.N.calc` speichert wie bisher sofort, der DSP bekommt aber Zwischenfilter statt Mute + hartem Wechsel. Gleicher Typ: Frequenz/Q geometrisch, Gain linear, Schritte ≤ 0,5 dB, ≤ 1/12 Oktave, Q-Faktor ≤ 1,1, höchstens 64 Schritte, 2 Biquads je Durchlauf (64 Hz). Typwechsel zwischen EQ, Shelfs, EQ-Bandbreite und Bypass über 0 dB; andere Typwechsel und `raw` wie bisher mit Mute. Der letzte Schritt ist bitgleich zum direkten Schreiben. Am Gerät gemessen (12.11.2026): ohne Aussetzer und ohne messbares Knacken, Original-Verhalten dagegen ca. 2 s stumm und Knackser beim Unmute (#44). Dieselbe Option schaltet außerdem (#50): **Mixer-Werte** laufen in Schritten von 0,0125 (64 Hz, 0→1 in 1,25 s) zum Ziel statt Mute; am Gerät kein Aussetzer, Pegelübergang 0,6 s für −6 dB. **DAC-Mute parallel**: Das Original schaltet die acht DAC-Kanäle nacheinander mit je 100 ms (aus) bzw. 130 ms (an) Wartezeit, 1,8 s, in denen die Hauptschleife steht; mit der Option alle Kanäle auf einmal und eine Wartezeit. Nicht überblendbare Änderungen (Delay, Eingangswahl, Mixer-Position, `raw`) sind damit nur noch ca. 1 s stumm statt 2,1 s | `set biquad.clickless=1` (EEPROM 0x0F02), `get biquad.clickless` | #44, #50 |
| **Mono je Ausgangspaar** (MODC-Typ 0x6A `mono`, im Original deklariert, Dialog in der Original-GUI): Flags 1 Bits 0–3 = Ausgangspaare 1/2…7/8 (0 = Paar 7/8 wie der ältere Dialog „Kanal 7+8“), Set 1 = Modus (1 Stereo, 2 Mono rechts, 3 Mono links, 4 Stereo zu Mono), Event 1 = an/aus. Wirkt beim Schreiben auf die Zeilen des Eingangs-Mischers (`mixer.P.C.G`, C 3–10 = Ausgänge 1–8), die gespeicherte Abstimmung bleibt unverändert; Umschalten mit kurzem Mute wie jede Mixer-Änderung | Original-GUI (MODC „CTRL-MONO“) oder `set modc.N=[1,106,08000000,E1,0,0,0,04000000]` | #14 |
| **Analoger Mischer** (MODC-Typ 0x6D `mixer`, im Original deklariert, aber ohne Code; Dialog „Analoger Mischer“ der Original-GUI): Set 1–3 = Anteil der analogen Eingangspaare 1–3 (GUI „Kanal 1–3“ = `mixer.P.C.G` mit G 1/2, 3/4, 5/6) in 0–100 %, wirkt auf alle Zeilen des Eingangs-Mischers zusätzlich zur gespeicherten Mischung (die Abstimmung bleibt unverändert, digitale Eingänge und Ketten unberührt). Block aus = 100 %. Mit `clickless=1` gleitender Übergang wie bei Mixer-Änderungen (#50), sonst kurzes Mute | Original-GUI (MODC „Analoger Mischer“) oder `set modc.N=[1,109,00000000,0,0,0,0,32644B00]` (50 %, 100 %, 75 %) | #16 |
| **Eingangs-Umschalter** (MODC-Typ 0x60 `mux_I`, im Original deklariert, Dialog in der Original-GUI): bis zu 4 Events als Prioritätenliste, Event 4 am höchsten, Set 1 ist der Rückfall; der Eingang des höchsten aktiven Events (Set k) geht an die aktive Mixer-Position, wenn sie angehakt ist (Flags 1, Bits 0/2/4/6 = Positionen 1–4). Speichert nicht ins EEPROM | Original-GUI (MODC „CTRL-MUX I“) oder `set modc.N=[1,96,01000000,E1,E2,E3,E4,S1S2S3S4]` | #17 |
| **Boot-Sektion lesen** (Diagnose, nur lesend): 64 Byte je Block aus 0x40000, zeigt ob die Lockbits das Auslesen des Bootloaders aus der Anwendung erlauben; Skript `tools/dump_bootsection.py` | `get system.hardware.boot.N` (N 1–128) | #22 |
| **DSP-Ladevorgang messen** (Diagnose): pro I2C-Bus die Wartezeit bis die TAS3108 ihr Programm laden, gezählte SDA-Flanken, Wartezeit bis Ruhe, ob die 2-s-Grenze des Ports erreicht wurde, zusätzliche Abfragen der Kerne; am Ende, ob dieser Start auf einen Neustart wegen fehlender Kerne folgte | `get system.hardware.dsp` = `[ms,Flanken,ms,Grenze,Wiederholungen]` je Bus 1/2, dann `Neustart` | #45 |
| **Startprotokoll der DSP-Kerne** (Diagnose): je Kern, ob er antwortete, ob sein EEPROM in diesem Start neu geschrieben wurde und ob das Zurücklesen stimmte, dazu Status (Subadresse 0x02) und Version (0x08); dazu die Messwerte je Bus wie oben. `.2` hält den Start vor einem DSP-Neustart (bleibt über den Software-Reset im RAM, nach dem Einschalten leer) | `get system.hardware.dsp.1` (dieser Start) / `.2` = `[gültig, [antwortet,geschrieben,geprüft,Status 4×,Version 4×] je Kern 1–3, [Bus 1], [Bus 2]]` | #57 |
| **Pegelanzeige** für die GUI: Effektivpegel der acht Mixer-Eingänge (G1–G8: 1–6 analog wie `gain.in.N`, 7/8 digital), Skala wie `gain.*` (255 = 0 dBFS, 0,5 dB je Schritt, 0 = kein Signal). Gemessen per TAS3108-PEEK wie die dynamische Loudness, aber je Eingang und nur, solange gefragt wird: jede Abfrage hält die Messung 3 s am Laufen (zwei zusätzliche PEEKs je 64-Hz-Durchlauf, 16 Stichproben je Eingang und Sekunde, Glättung rund 0,5 s). Die Loudness-Messung behält ihren Takt. Effektivwert aus Stichproben, kein Spitzenwert | `get system.vu` = `[v1,…,v8]` | #52 |
| **Ausgangspegel** für die GUI: Effektivpegel der Ausgänge 1–8 nach Gain und Laufzeit, gleiche Skala und Glättung. PEEK der Datenworte, die Kern 3 (Ausgänge 1–4) und Kern 2 (5–8) an DO1–DO4 schicken; die Worte trägt `tools/dsp_opt_gen.py` aus dem Programm ein (96ELN: D_1BE–D_1C1, 96ELN-3: D_1C2–D_1C5). Eigene Haltezeit von 3 s, zwei weitere PEEKs je Durchlauf. Mute wirkt im DAC und ist hier nicht sichtbar. Nur mit `FORGE_DSP_OPT` (Bring-up-Builds), sonst `unknown command` | `get system.vu.out` = `[o1,…,o8]` | #52 |
| **Befehlsliste** `system.help`: alle `get`- und `set system.*`-Befehle dieser Firmware als Liste, mit Platzhaltern (N/K = Zahl, `*` = alle). Die Liste entsteht aus denselben Tabellen, über die die Firmware die Befehle verteilt (eine Zeile je Befehl: Name, Unterpfade, Funktion). Ein neuer Systembefehl erscheint damit automatisch | `get system.help` = `["get system.biquads", …, "set system.supply.vcc"]` | |
| **Fehlerzähler zurücksetzen**: leert die Liste von `get system.errors` (im Original nur per Werks-Reset). Nur belegte Plätze werden geschrieben | `set system.errors=0` | |
| **microSD prüfen** (Diagnose, nur lesend): Karte gesteckt, eingebunden, FAT-Typ (1/2/3 = FAT12/16/32), Größe in MB; Dateien im Hauptverzeichnis mit Größe (8.3-Namen, höchstens 32). Eine später gesteckte Karte wird bei der Abfrage eingebunden. Kein freier Platz, weil FAT32 ohne gültiges FSINFO dafür die ganze FAT lesen müsste | `get system.sd` = `[gesteckt,eingebunden,Typ,MB]`, `get system.sd.files` = `["NAME.EXT:Bytes", …]` | #24 |
| **Preset auf die microSD exportieren** (neu, im Original nicht vorhanden): schreibt das geladene Preset und die Geräteeinstellungen als `preset_N.cfg` im Format der GUI und des Vorladers. Intern laufen dieselben Abfragen wie bei `c45b3 export-tuning`; nie geschriebene Werte fallen weg, gespeicherte Biquads werden als `.calc` geschrieben. Das Dateidatum kommt ins EEPROM, damit der nächste Start die eigene Datei nicht zurücklädt; eine am PC geänderte Datei lädt er wie bisher. DEV-Gerät: 37 KB in 1,7 s | `set system.sd.export=N` (N = 1 … Zahl der Presets) | #24 |
| **Dateien auf der microSD über die serielle Verbindung** (für die GUI und das spätere Log): eine Datei ist offen; Lesen in Blöcken zu 128 Byte mit CRC-16/XMODEM, Sprung zu einem Block zum Wiederholen; Schreiben in Blöcken bis 48 Byte mit Pflicht-CRC im Wert, weil der Befehlspuffer (127 Zeichen) zu lange Zeilen samt Zeilenprüfsumme ohne Fehler abschneidet; Löschen. 8.3-Namen im Hauptverzeichnis, keine Pfade. Eine hochgeladene `preset_N.cfg` lädt der nächste Start (gespeichertes Datum wird gelöscht, ohne Uhr haben alle geschriebenen Dateien dasselbe Datum) | `set system.sd.open/create="NAME.EXT"`, `get system.sd.read` = `[block,"hex",crc]`, `set system.sd.seek=B`, `set system.sd.write="hex:CRC4"`, `set system.sd.close=1`, `set system.sd.delete="NAME.EXT"` | #59 |
| **Namen der Eingänge**: je Mixer-Eingang (1–6 analog wie `gain.in.N`, 7/8 digital) bis 16 Zeichen, `""` löscht. Gespeichert im EEPROM ab 0x0C00 (8 × 17 Bytes), außerhalb des Layouts des Originals; nie beschriebener Speicher liest sich als `""` | `set system.channel.in.N="Radio"`, `get system.channel.in.*` | #55 |
| **Grenzen lesbar** für die GUI, aus den Tabellen des jeweiligen Builds: Bereiche der Biquad-Werte (Frequenz, Gain in 0,1 dB, Q in 1/1000, Bandbreite), die berechenbaren Filtertypen, der Laufzeit-Speicher je DSP mit der Zuordnung der Aus- und Eingänge, die Länge der Kanalnamen | `get system.limits` (Liste), `.biquad`, `.types`, `.out`, `.in`, `.names` | #54 |
| **Biquads frei verteilen**: Jeder DSP-Kern hat einen gemeinsamen Pool (Kern 1: 172 für Summe A/B, davon 28 zusätzliche aus freien Takten und Koeffizienten, Kerne 2 und 3: je 196 für die Ausgänge 5–8 bzw. 1–4). Die Länge je Kanal ist einstellbar, die Summe darf den Pool nicht überschreiten; die L/R-Trennung der Summen entfällt. Die Zusatz-Biquads von Kern 1 bekommen die virtuellen Nummern 537–564 (Speicherplätze im Rahmen der Preset-Bereiche des Originals) und freie Subadressen der Art 0 (0x67–0x6E, 0xB7–0xC7, 0xFA–0xFC); mit voller Nutzung 1395 von 1408 Takten. Mit den Standardlängen bleibt das DSP-Programm unverändert, nur die Registertabelle wächst. Kerne 2/3 hätten nur Koeffizienten für 5 weitere. Die MCU erzeugt das DSP-Programm beim Schreiben des TAS3108-EEPROMs aus Tabellen (`tools/dsp_pool.py` → `dsp_opt.h`, Generator `src/dsp_pool.c`, byte-gleich mit der Python-Referenz geprüft); mit den Standardlängen entsteht genau das optimierte Programm. Beim Umstellen wandern die gespeicherten Bänder aller Presets mit (Band b bleibt Band b, neue Bänder starten als Vorgabe, überzählige entfallen), danach startet die MCU neu und lädt das neue Programm (kurze Stille). Mindestlänge je Kanal 1 bzw. 3 (ein eingeschobener Speicherbefehl des Optimierers muss im ersten Segment liegen). Längen im EEPROM 0x0F20 (Kennung 0x47, dann je Kanal ein Byte); unpassende Werte gelten als Standard. Nur mit `FORGE_DSP_OPT` und nicht auf V30-96LN | `get system.biquads` (= `[[Kanal, Kern, Biquads, Minimum, Pool des Kerns], …]`), `set system.biquads.K=[…]` (Längen der Kanäle von Kern K); `get biquad` liefert die aktuellen Längen | #47 |
| **Versorgungsspannung überwachen**: 3,3-V-Schiene intern (VCC/10), Bordspannung optional über einen GVOL-Eingang mit Teiler und Zwei-Punkt-Kalibrierung, Unterspannungs-Ereignis mit Hysterese, optional Stummschalten (nur Bring-up), Ein-Punkt-Abgleich der 3,3-V-Messung gegen ein Multimeter (die interne Referenz streut je Gerät, DEV-Gerät −6 %). Einstellungen im EEPROM 0x0F03–0x0F10. Belegt den ADCB-Kanal 1, den das Original nicht nutzt | `system.supply`, `.raw`, `.source`, `.cal`, `.low`, `.mute`, `.vcc`; Ereignis `system.supply=low/ok`; `docs/supply.md` | #46 |
| **IR-Befehl 2** (im Original frei, „maybe use later“): schaltet zum nächsten Eingang (`mixer.in`) der aktiven Mixer-Position, nach dem letzten wieder auf 1. Die Java-GUI hat für Platz 2 keinen Eintrag; anlernen per Befehl | `set extender.ir.custom.2=[header,adresse,befehl]` | #27 |
| **Bootloader-Versionsstring**: Der Bootloader liest die Firmware-Version (`FW_VERSION:xx.yy`); das Original enthält keinen | chip45boot3 0x12 | |
| **Abstimmung sichern/vergleichen/zurückschreiben** im Flasher, Format = `.cfg` der Original-GUI (auch vom SD-Lader lesbar). Der Upload sichert vorher und vergleicht danach; Zurückschreiben nur der Unterschiede, nur bei passendem Modell. Am Gerät geprüft (1009 Werte, Export 15 s) | `c45b3 export-tuning / compare-tuning / restore-tuning`, Tasks *Forge: export/compare tuning*, `[forge] restore_tuning` | #30 |
| **Linux-Flasher** chip45boot3 mit Backup, Gerätecheck und Rückweg auf 1.11.6000, eingebunden in PlatformIO/VS Code | `tools/c45b3-host`, *Forge:*-Tasks | |

## Behobene Fehler des Originals

Alle betreffen Fehlerpfade oder Werte außerhalb des gültigen Bereichs. Gültige Eingaben verhalten sich wie im Original.

| Original | Port | Issue |
|---|---|---|
| `preset.copy/erase/select/loaded` akzeptieren die Preset-Anzahl selbst: mit 4 Presets löscht `preset.erase=4` Preset 0, mit 8 Presets wird hinter den Adresstabellen gelesen | nur 0 … Anzahl−1 | #33 |
| Typwechsel eines MODC-Blocks N löscht die Event-Felder N, N+32, N+64 und N+96, also auch fremde wie Remote-Out (3) oder Shutdown (4) | nur der Ausgang des Blocks (96+N) | #38 |
| `system.boot` startet nur neu: Die Bootloader-Sequenz (EEPROM 0/1 = 0xAA/0x55) ist deklariert, wird aber nie geschrieben, der Bootloader startet nur per Taster | schreibt die Sequenz, der Forge geht ohne Taster in den Bootloader ([docs/bootloader.md](bootloader.md)) | #22 |
| V30-96ELN: `chk_hardware_other` erwartet den Taktbaustein (Bit 11), den der Build nicht hat, und nicht DAC/CT7302 #20 (Bits 2, 3), die er erkennt. Die Prüfung am Ende von `systeminit` schlägt immer fehl, die Ausgänge bleiben stumm | erwartet 0x007D (wie V40-96ELN ohne Si5338), auch in `system.feature.attributes.hw` | #32 |
| DSP-Erkennung beim Start: Das Original wartet in `i2cN_sda_wait4silence` unbegrenzt auf den Programmstart der TAS3108 und verlässt sich auf den Watchdog (8 s), der Port hatte dort eine Grenze von 2 s und lief bei einem noch ladenden Kern ohne ihn weiter (stumm durch die Hardwareprüfung, sporadisch nach Kaltstarts) | Ein Kern, der nicht antwortet, wird bis zu 8-mal im Abstand von 250 ms erneut abgefragt (2 s je Kern, Watchdog bedient); fehlen danach Kerne, startet die MCU einmal neu (Software-Reset). Ein Merker im EEPROM (0x0F11) erlaubt nur einen Neustart in Folge, ein defekter DSP führt also nicht zu einer Schleife, sondern wie bisher zu stummen Ausgängen | #45 |
| Das DSP-Programm wird nur nach einem Werks-Reset mit dem EEPROM des TAS3108 verglichen; sonst gilt die gespeicherte Prüfsumme. Eine Firmware mit anderem DSP-Programm würde es deshalb nie schreiben | Die Prüfsumme wird bei jedem Start aus dem Programm der Firmware berechnet (24 KB, wenige ms); weicht das EEPROM ab, wird es neu geschrieben | #47 |
| Meldet ein TAS3108 nach dem Laden einen Fehlerzustand (Programm nicht geladen), läuft das Gerät ohne Ton weiter. Am DEV-Gerät einmal beobachtet, direkt nachdem die EEPROMs der Kerne 2/3 neu geschrieben worden waren | Wie bei fehlenden Kernen (#45) startet die MCU einmal neu; der Merker im EEPROM verhindert eine Schleife | #47 |
| Nach dem Neuschreiben eines DSP-EEPROMs meldete der Kern beim folgenden Laden reproduzierbar einen Fehlerzustand, die MCU startete dafür ein zweites Mal (Umverteilen der Pools dauerte so rund 40 s) | Die Firmware liest das geschriebene Programm zurück und vergleicht die Summe, bei Abweichung schreibt sie es ein zweites Mal. Die gut 2 s dafür reichen dem Kern: am DEV-Gerät 4 von 4 Umverteilungen mit einem Start (eine reine Wartezeit von 2,5 s wirkte genauso, es liegt also an der Zeit, nicht am Lesen) | #57 |
| `delay.left` teilt die Ausgänge gleichmäßig auf `flow_max_dsp` DSPs auf. Bei 96ELN, 96ELN-3 und CORE D8 (3 DSPs, DSP 1 hat nur die Eingangs-Delays) stimmt das nicht mit der Hardware überein, Ausgang 7/8 fehlt. `set delay` begrenzt nicht: Die Summe je DSP kann über `flow_max_samples` (5840) gehen | `delay.left` nach der tatsächlichen Zuordnung (`data_flow_pair_delay(_in)_dsp`), ein Wert je DSP 1…`flow_max_dsp`, Eingänge mitgezählt. Ein Wert, mit dem ein DSP mehr als 5840 Samples bräuchte, wird mit `command out of range` abgelehnt; bei `delay=[…]` gilt das für alle Werte zusammen, dann wird nichts geschrieben. Nie beschriebener Speicher (32767) zählt nicht | #53 |
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
| **Remote-Out** (Endstufen) | nach den Startverzögerungen an | mit `FORGE_AUDIO=1`: in den Vollstart-Builds (`-bringup`, Release ab 2.0.0) wie im Original, in den sicheren Builds aus |
| **Versionsnummer** | `system.firmware` = 1.11.6000 | `system.firmware` bleibt 1.11.6000 (Protokollstand für die GUI, Merker im EEPROM), eigene Version in `system.firmware.port` und im Bootloader (`FW_VERSION:02.00`); erstes Release 2.0.0, bewusst oberhalb der BASCOM-Firmware |
| **Peripherie-Init** | immer | nur mit `FORGE_BRINGUP=1`; ohne: sichere Variante (Erkennung, Protokoll, kein Ton) |
| Pin-Interrupts (Masse-Schalter, USB-DTR, Überlauf, Remote-In) | Interrupt je Flanke | in der Hauptschleife abgefragt (gleiche Werte; IR-Abtastung und Timer bleiben Interrupts) |
| I2C | wartet unbegrenzt | Zeitüberschreitung nach ~10 ms, als Fehler gemeldet; `bitwait sda` beim DSP-Start höchstens 2 s, danach erneute Abfrage der Kerne und höchstens ein Neustart (#45) |
| EEPROM | jedes Schreiben | gleiche Bytes werden nicht neu geschrieben (weniger Verschleiß) |
| SD-Presets: `store_preset` nach dem Laden | bleibt auf der zuletzt geladenen Datei; ist das zufällig das gewählte Preset, lädt MODC es nicht in die DSPs | zurückgesetzt, MODC lädt das gewählte Preset immer |
| SD-Presets: Datum der Dateien | nur V30-96LN hat Platz dafür im EEPROM | V30-96LN wie im Original, die übrigen Varianten ab EEPROM 0xE00 (vom Original ungenutzt) |
| Debug-Texte (`system.debug.mode`) auf UART2/UART4 | ja | nicht portiert (#41) |
| XMOS-Firmware-Update über SPI | vorhanden, im Betrieb ungenutzt | nicht portiert (#42) |

## Build-Optionen

| Option | Standard | Wirkung |
|---|---|---|
| `FORGE_BRINGUP` | 0 | 1 = vollständige Peripherie-Init wie das Original (DAC, DSP, S/PDIF, Preset, MODC, IR, ADC) |
| `FORGE_AUDIO` | 0 (sicher), 1 (`-bringup`, seit 2.0.0) | 1 = Remote-Out (Endstufen) wie im Original einschalten |
| `FORGE_FACTORY_RESET` | 0 | 1 = Werks-Reset wie im Original (braucht `FORGE_BRINGUP=1`) |
| `FORGE_DSP_OPT` | 0 (sicher), 1 (`-bringup`, Standard) | 1 = optimierte DSP-Programme (Wartetakte nach DLYPTR gefüllt, Kern 1 1387 → 1255 und Kern 2/3 1247 → 1205 Takte, im Simulator bitgleich, `tools/dsp_opt_gen.py`); 96ELN, 96ELN-3, CORE D8. Am Gerät gemessen gleichwertig. Voraussetzung für die frei verteilbaren Biquads (#47) |

## Toolchain

- avr-gcc 16.2.1 mit eigenem Patch (Release `toolchain-avr-gcc-16.2.1-p1`): behebt einen internen Compilerfehler in
  `lower-subreg`, der auch in GCC 17 noch vorhanden ist. Patch, Testfall und Build-Skript in `tools/toolchain/`.
- `-mdouble=64` wird auch an den Linker gegeben (`scripts/linkflags.py`). Ohne das bindet der Linker die
  32-Bit-Mathematik der avr-libc ein (`sin`, `cos`, `sqrt` als Float-Versionen). `scripts/check_math.py` bricht den Build in
  diesem Fall ab.
- libgcc hat kein 64-Bit-`lround`; der Port rundet selbst (`bas_round_i32`).

## Prüfstand

- Nativ: 106 Tests, alle 7 Varianten (Protokoll, EEPROM-Layout gegen die Release-HEX, Biquads gegen eine unabhängige
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
| Event 2 (Port, #51) | Schrittweite je Durchlauf (32 Hz) in 0,01 dB. Kleinere Schritte führen langsamer nach (0,05 dB: 1,6 dB/s) | 20 (0,2 dB) |
| Event 3 (Port, #51) | **Dynamische Loudness:** Bitmaske der Eingänge von Kern 1 (Bit 0 = in1 … Bit 7 = in8; Digitaleingang = 0xC0), deren gemessener Effektivpegel die Lautstärke ersetzt. Skala wie `gain.*`: 255 = 0 dBFS effektiv, 0,5 dB je Schritt; die Referenz (Flags 2) gilt dann als Pegel (z. B. 215 = −20 dBFS). Erfasst auch Dämpfung an der Quelle (PC, Android) und leise gemischte Musik | 0 = Lautstärke wie bisher |
| Event 4 (Port, #51) | Zeitkonstante der Pegelmessung in 0,1 s | 30 (3 s) |
| Ausgang (Event 96+N) | aktuelle Bass-Anhebung in 0,5-dB-Schritten | |

Höchstens 12 dB Anhebung.

Pegelmessung (Event 3, `src/level.c`): Die MCU liest 64-mal je Sekunde reihum ein Eingangssample der gewählten Eingänge
per TAS3108-PEEK (Datenspeicher D_000–D_007, vom DSP in jedem Sample geschrieben). Die Lesezeitpunkte sind zum
Audiotakt asynchron, also Zufallsstichproben; ihr mittleres Quadrat ist die Signalleistung. Je 64 Stichproben (1 s)
ergeben einen Blockpegel in dB; Blöcke unter −60 dBFS (Pause) zählen nicht, damit die Anhebung beim Wiedereinsetzen
nicht zu hoch ist. Aus den letzten 5 Blöcken gilt der Median, so bleiben kurze laute Impulse (bis etwa 2 s) ohne
Wirkung. Der Median wird in dB mit der Zeitkonstante aus Event 4 geglättet, der ausgegebene Wert hat 1 dB Hysterese.
Gemessen am Gerät (Referenz 215, Bass 0,40): Ton mit −23/−33/−43 dBFS effektiv → Bass +1,0/+5,0/+9,0 dB (Soll
1,2/5,2/9,2, Ereignis in 0,5-dB-Stufen), in der Pause gehalten; ein Impuls von 0,5 s mit +20 dB ändert nichts.
Eingeschwungen nach etwa 5–15 s. Gains zwischen den 0,1-dB-Stufen der Tabelle `data_power_gain` rechnet die Firmware direkt aus (`10^(g/40)`); ganze 0,1-dB-Stufen kommen weiter aus der Tabelle, bitgleich zum Original. Beispiel: `set.1 modc.9=[1,108,01DF280F,70,0,0,0,5000A00F]` = Ausgänge 1/2, folgt Event 70, Bass
80 Hz × 0,40, Höhen 4 kHz × 0,15; 20 dB unter der Referenz ergibt Bass +8 dB und Höhen +3 dB.

Auslegung: Die GUI nennt die Flags 3/4 „Gain“ (Anzeige x,xx dB). Als feste Anhebung wären 0,01–2,5 dB zu wenig, deshalb
gilt der Wert hier als Anstieg je dB Dämpfung. Die Bedeutung von Event 1 („de/aktiviert über Event“ im Dialog,
„Loudness Input“ in der Liste) ist im Original nicht festgelegt; hier ist es die Lautstärke, ein- und ausgeschaltet wird
über den Block selbst.

Hinweise: Die belegten Biquads überschreibt der Block; Filter, die die GUI dort anzeigt, wirken nicht. Wird ein
Ausgangspaar abgewählt, gehen seine Shelves auf 0 dB. Nach dem Laden eines Presets setzt der Block die Shelves neu.
Am Gerät gemessen (12.11.2026): Anhebung wie spezifiziert (−10 dB → Bass +4,2 dB, −20 dB → +7,9 dB). Ein Knacken beim Nachführen der Shelves war nicht nachweisbar; die Messkette zeigt gelegentlich Impulsstörungen, auch ganz ohne Befehle (#51).


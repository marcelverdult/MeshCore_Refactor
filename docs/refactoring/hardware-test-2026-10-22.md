# Hardwaretest der Refactoring-Serie, Stand integration 07fa9c5c (22./23.10.2026)

Geprüfter Stand: `origin/integration` = `07fa9c5ccd7ca06a1f27ae85ffd72cdd49a4e2a2`
("docs(refactoring): list step 9c in the step table"), enthält Schnitt C, 9b/9c, 10c und D1/D2.
Beide Testboards nutzen das Opt-in `RADIOLIB_TARGET_GLOBALS` (`variants/promicro/target.h:17`,
`variants/heltec_v3/target.h:17`), laufen also über `src/helpers/radiolib/RadioLibTarget.cpp` mit
der geänderten statischen Init-Reihenfolge.

Rohlogs aller Schritte: `docs/refactoring/hardware-test-2026-10-22-logs/` (Dateinamen unten in
Klammern). Die Testskripte (`cli2.py`, `comp.py`, `hold.py`, `advrate.py`) liegen daneben.

## Ergebnis in Kürze

- **Kein Fehler des Refactors gefunden.** Repeater und USB-Companion aus integration booten,
  das Radio arbeitet (Rauschboden, RX, TX, ACKs), die Uhr läuft, Adverts und DMs gehen in beide
  Richtungen, auch gegen offizielle 1.18.0, und Prefs überleben Reboots.
- **Upgrade 1.18.0 → integration und Downgrade zurück** an beiden Boards und beiden Rollen:
  Identität (Public Key), alle 30 gelesenen Repeater-Prefs bzw. Companion-Name, Radio, tx und
  Kontaktliste (mit gelerntem Direktpfad) bleiben erhalten; nur `ver` ändert sich.
- **Advert-Empfangsquoten** (je 10 Adverts) unterscheiden sich nicht zwischen integration und
  offizieller Firmware; Aussetzer gibt es in beiden (siehe Abschnitt 4).
- Alle Boards sind wieder im Vorher-Stand, mit einer Ausnahme beim FakeTec (Abschnitt 7).

## 1. Inventar (Ports nach Umstecken, per ioreg-USB-Seriennummer)

Die meisten "nicht im Inventar"-Ports waren bekannte Boards nach Umnummerierung des Hubs
(t114-4, techo-2, techo-3dcf, techo-b43b, t1000e); deren Ports sind in `_meta/devices.tsv` der KB
korrigiert. Neu eingetragen:

| Name | USB-Seriennummer | Vorher-Stand |
|---|---|---|
| faketec-5095 | 5095333DA2CCCBD9 | kiekr_firmware v0.4.8 (Zephyr), Bootloader OTAFIX 0.9.2, ohne SoftDevice |
| heltecv3-dutch | CP2102 "0001", MAC 34:b7:da:56:91:7c | Repeater "Dutch Embassy", `v1.13.0-evo_0.1.9-9e5dbf2`, Pubkey D6ADD22B… |
| heltecv3-jork | CP2102 "0001", MAC 48:ca:43:59:74:38 | Repeater/Observer "Jork Observer", `1.14.1-letsmesh.net-dev-2026-03-20-abe6e046`, Pubkey E936D957… |
| faketec-6060 | 6060B6CAFA98F588 | nur Bootloader + S140 6.1.1, keine MeshCore-App |
| faketec-0224 | 022496C3BB26E869 | nur Bootloader + S140 6.1.1, keine MeshCore-App |
| faketec-6502 | 65026E116C8E16A8 | App-Modus, antwortet weder auf Text-CLI noch auf Companion-APP_START, nicht angefasst |

Beide Heltec V3 melden dieselbe USB-Seriennummer `0001`; unterscheidbar nur über Port/locationID
und den Public Key, der vor jedem Flash per CLI geprüft wurde. Nicht angefasst: faketec-6e71 und
faketec-64b9 (FUOTA-Session lud währenddessen per `usb_load.py`), faketec-1879, p1-4134,
techo-b43b und alle Companions der a32-Telefone. Getestet wurde mit faketec-5095 und
heltecv3-dutch; heltecv3-jork wurde nur gesichert und gelesen.

## 2. Sicherung des Vorher-Stands

- heltecv3-dutch, heltecv3-jork: kompletter 8-MB-Flash per `esptool read-flash` (bei 921600 und
  460800 Baud brach das Lesen am Hub mit "Invalid head of packet" ab, 115200 ging).
  Prüfung per `verify-flash`: Dutch "Verification successful (digest matched)"; Jork weicht nur in
  der NVS-Partition (0x9000, WLAN/PHY-Daten, die beim Booten neu geschrieben werden) ab, alle
  anderen Partitionen "Verification successful".
  Ablage: `~/Projects/_backup/firmware_dumps/heltecv3-{dutch-34b7da56917c,jork-48ca43597438}-2026-10-22-full8MB.bin`.
- faketec-5095: `CURRENT.UF2` des Bootloaders (0x1000 bis 0xEA000, SHA256 726224e9…).
  Der Rest der kiekr-Speicherpartition (0xEA000 bis 0xF4000, 40 KB) ist über den Bootloader nicht
  lesbar und damit nicht gesichert.
- Offizielle Referenz-Images 1.18.0 (Download freigegeben von Marcel), SHA256 gegen GitHub-Digest
  geprüft, abgelegt in `kiekr-workspace/meshcore_stock_firmware/binaries/v1.18.0/`:
  `Heltec_v3_repeater-…-merged.bin` (396c8e2a…), `Heltec_v3_companion_radio_usb-…-merged.bin`
  (310750aa…), `ProMicro_repeater-….zip` (1d6f4820…), `ProMicro_companion_radio_usb-….zip`
  (08590e12…).

Getestete integration-Images, gebaut mit `PLATFORMIO_BUILD_FLAGS='-D FIRMWARE_BUILD_DATE=\"hw-07fa9c5\"'`
(Marker in jedem Image einmal gefunden, `ver` zeigt `v1.18.0 (Build: hw-07fa9c5)`):

| Image | SHA256 |
|---|---|
| ProMicro_repeater firmware.zip | 950d2db3d702153813cad0dd335bea8b3fd30799e7809e4fab389428ab0fa0ad |
| ProMicro_companion_radio_usb firmware.zip | 89a2763183a7449333bbaa039cbe7cc7ad83a3ece1e431c1a313aae7fda798e7 |
| Heltec_v3_repeater firmware.bin | 70d69d393aa1d7caa9c4dbaf526331671057e8c830addb2a8311032c7ad0e40a |
| Heltec_v3_companion_radio_usb firmware.bin | 4777d96a65adb000d3769c34db2c8cfb853cb866ca867b3b22f363ca1b047586 |

Um nicht im echten Mesh zu funken, liefen alle Tests auf 869,462 MHz / 62,5 kHz / SF8 / CR8
(später 869,475) mit 1 bis 2 dBm; der Heltec wurde vorher komplett gelöscht, damit er nicht mit
der Identität von "Dutch Embassy" sendet.

## 3. Repeater: Upgrade, Boot, Radio, Uhr, Downgrade

**FakeTec** (offizielle 1.18.0 mit Testwerten A, Reboot, dann DFU auf integration):

```
(ft-1-stock-A.txt)              ver => v1.18.0-81dbae5 (Build: 15-Oct-2026)
(ft-2-integ-after-upgrade.txt)  ver => v1.18.0 (Build: hw-07fa9c5)
diff ft-1 ft-2: nur die beiden ver-Zeilen
```

Gleiche Werte in beiden Dateien, u. a. `get public.key => 10C028D3…`, `get radio => 869.461975,62.5,8,8`,
`get tx => 2`, `get af => 2.0`, `get rxdelay => 3.5`, `get int.thresh => 12`, `get cad => on`,
`get radio.rxgain => off`, `get flood.max => 32`, `get owner.info => hwtest-A`.

Radio und Uhr unter integration (ft-3-integ-stats-t0.txt, adv-1-…txt):

```
stats-core  => {"battery_mv":4408,"uptime_secs":38,"errors":0,"queue_len":0}
stats-radio => {"noise_floor":-101,"last_rssi":-104,"last_snr":16.75,"tx_air_secs":1,"rx_air_secs":0}
clock => 10:53 - 15/5/2024 UTC     (uptime 38 s)
clock => 10:57 - 15/5/2024 UTC     (uptime 272 s)
```

Ein Boot-Log ist beim FakeTec nicht beobachtbar (USB-CDC meldet sich erst nach dem Boot an);
Ersatzbeleg ist `errors:0` in `stats-core` und der arbeitende Empfänger. **NICHT VERIFIZIERT:**
Boot-Ausgabe des FakeTec.

Downgrade: unter integration Testwerte B gesetzt, Reboot, DFU auf offizielle 1.18.0
(ft-4-integ-B.txt gegen ft-5-stock-after-downgrade.txt): Unterschied nur in den `ver`-Zeilen;
`name HWT-FT5095b`, `tx 1`, `af 1.5`, `cad off`, `flood.max 48`, `owner.info hwtest-B` usw. bleiben.

**Heltec V3 "Dutch"** (Flash gelöscht, offizielle 1.18.0 merged, Testwerte A, dann nur die
App-Partition 0x10000 mit integration überschrieben):

```
(dutch-4-integ-after-upgrade.txt, Boot-Ausgabe nach dem Upgrade)
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
...
entry 0x403c98d0
Repeater ID: 1FE03AAD38ED17E40BB0F287E026E742364ABE170AAAD63B4408CE1F87E0477E
```

Keine Fehlerzeile im Boot. `diff` dutch-3 (offiziell) gegen dutch-4 (integration): nur `ver`.
Downgrade nach Testwerten B (dutch-5 gegen dutch-6): nur `ver`; `name HWT-HV3Db`, `tx 1`,
`owner.info hwtest-B` erhalten.

## 4. Adverts zwischen den Boards (RSSI/SNR)

Messung mit `advrate.py`: 10 Adverts je Richtung im Abstand von 6 s, Erfolg = Empfänger führt den
Sender in `neighbors` mit höchstens 8 s Alter (SNR-Feld dort ist SNR mal 4).

| Sender → Empfänger | FakeTec integration, Heltec offiziell (rate-1) | FakeTec offiziell, Heltec offiziell (rate-2) | FakeTec offiziell, Heltec integration (rate-3) |
|---|---|---|---|
| FakeTec → Heltec | 6/10, SNR 11,75 bis 12,25 dB | 8/10, SNR 11,75 bis 12,0 dB | 7/10, SNR 11,5 bis 12,25 dB |
| Heltec → FakeTec | 10/10, SNR 12,5 bis 13,25 dB | 10/10, SNR 12,25 bis 13,0 dB | 9/10, SNR 12,5 bis 12,75 dB |

RSSI am Empfänger -3 bis -6 dBm (Boards nebeneinander, 1 bis 2 dBm Sendeleistung), `recv_errors 0`
überall. Die Aussetzer in Richtung Heltec treten auch mit offizieller Firmware auf beiden Seiten
auf; 6/10 gegen 8/10 bei n = 10 ist kein belastbarer Unterschied. Ursache der Aussetzer nicht
untersucht (Vermutung: Kollision mit dem Weiterleiten des eigenen Adverts durch das jeweils andere
Board, beide sind Repeater mit `repeat on`). Einzelbeleg:

```
(adv-1) neighbors => 1FE03AAD:23:49          FakeTec integration hört Heltec offiziell, SNR 12,25 dB
(adv-3) [/dev/cu.usbserial-0001] neighbors => 10C028D3:2:48   Heltec offiziell hört FakeTec integration, SNR 12 dB
```

## 5. Companion (USB): Kontakte, DM, CLI, Prefs

Beide Boards zuerst mit offizieller `companion_radio_usb` 1.18.0. Hinweis: beide übernehmen beim
Wechsel vom Repeater Name, Radio, tx und Position aus den Repeater-Prefs (comp-1), Identität bleibt.

- Adverts → Kontaktlisten in beide Richtungen (comp-2, comp-3), z. B.
  `PUSH_ADVERT pub=10c028d3…` und `CONTACT name=HWT-FT-C type=1 …`.
- DM offiziell ↔ offiziell (comp-4): beide Richtungen mit `PUSH_SEND_CONFIRMED`, Text kommt an.
- FakeTec auf integration (comp-5 gegen comp-6: identisch, d. h. SELF_INFO, Kontakt mit
  `out_path_len=0`, Radio, tx, Name gleich).
- DM FakeTec integration → Heltec offiziell, 6 Versuche (comp-8): 5 mit ACK und empfangenem
  Text; der erste direkt nach dem Öffnen des Heltec-Ports (Heltec bootet beim Öffnen neu) ging
  verloren, wie schon beim Einzelversuch in comp-7. Gegenrichtung (comp-7) mit ACK, rtt 1013 ms.
- Heltec auf integration (comp-9 gegen comp-10: gleicher Inhalt, nur andere Frame-Reihenfolge im
  Log). Uhr läuft: `CURR_TIME 1772323228`, 30 s später `CURR_TIME 1772323260`.
- DM integration ↔ integration, 3 je Richtung (comp-11): 6 von 6 mit `PUSH_SEND_CONFIRMED`
  (rtt 1014 bis 5110 ms), alle Texte kommen an.
- CLI über `CMD_RUN_CLI_COMMAND` (comp-12): `cli ver => v1.18.0 (Build: hw-07fa9c5)`,
  `set radio 869.475,62.5,8,8 => OK - reboot to apply`, `set tx 2 => OK`, `set name … => OK`,
  Reboot, danach an beiden `SELF_INFO … tx=2/22 radio=869.475,62.5,8,8` und
  `get radio => 869.4749755,62.5,8,8`; DM in beide Richtungen auf der neuen Frequenz mit ACK.
- Downgrade beider Companions auf offizielle 1.18.0 (comp-13 gegen comp-14): Heltec identisch;
  FakeTec identisch bis auf `lat=53599999 lon=9799999` gegen `53600000/9800000`. Aufgeklärt:
  `companion_session.py` schreibt die gelesenen Koordinaten per `CMD_SET_ADVERT_LATLON` zurück;
  danach zeigt auch die offizielle Firmware `lat=53599999 lon=9799999`, nach Neuladen aus der Datei
  wieder `53600000`. Gleiches Verhalten in beiden Images, kein Befund des Refactors.
- `companion_session.py` (38 akzeptierte Frames) auf dem FakeTec, einmal integration, einmal
  offiziell (session-ft-integ.txt gegen session-ft-stock.txt): Unterschiede nur in Version/Builddatum
  (`DEVICE_QUERY`, `RUN_CLI_COMMAND ver`) und im Fehlerzähler von `GET_STATS core` (02 gegen 00,
  Zähler seit Boot, die Läufe hatten unterschiedliche Vorgeschichte).

## 6. Was nicht oder nur teilweise geprüft ist

- **NICHT VERIFIZIERT:** BLE-Companion (nur USB-Companion getestet).
- **NICHT VERIFIZIERT:** Boot-Ausgabe des FakeTec (siehe 3.).
- **NICHT VERIFIZIERT:** Empfang durch ein unabhängiges Referenz-Board außerhalb des Testpaars;
  Referenz war jeweils das andere Board mit offizieller 1.18.0.
- Uhr: am FakeTec über `clock` (4 Minuten Abstand), am Heltec über `CURR_TIME` (30 s). Kein
  Langzeitlauf.
- Nur zwei Boardfamilien (nRF52840 ProMicro, ESP32-S3 Heltec V3). Andere D1/D2-Varianten sind nicht
  auf Hardware geprüft.

## 7. Zustand der Boards am Ende

- **heltecv3-dutch:** vollständiger Dump zurückgeschrieben, `verify-flash` "digest matched";
  CLI: `v1.13.0-evo_0.1.9-9e5dbf2`, "Dutch Embassy", Pubkey D6ADD22B…, `869.6179809,62.5,8,5`,
  tx 22, lat/lon wie vorher (dutch-9-restored.txt). Freigegeben.
- **heltecv3-jork:** nicht geflasht; `verify-flash` ab 0x10000 gegen den Dump "digest matched",
  CLI "Jork Observer", Pubkey E936D957… (jork-final.txt). Freigegeben.
- **faketec-5095:** kiekr_firmware zurückgespielt; der Firmware-Bereich 0x1000 bis 0x65000 ist
  blockgleich zum Backup (1600 von 1600 UF2-Blöcken), Konsole meldete danach
  `kiekr_firmware v0.4.8 — USB + BLE NUS (paired)`. **Abweichung:** der Paketspeicher (littlefs in
  0x65000 bis 0xF4000) war nicht vollständig sicherbar; nach dem Rückspielen meldete kiekr
  `PacketStore init failed — continuing without persistence`. Die Partition wurde danach bis 0xE9000
  gelöscht, damit kiekr ein frisches Dateisystem anlegen kann; die 776 Paketdatensätze stehen nur
  noch im Backup (`CURRENT.UF2`, bis 0xEA000). Nach einem Stromzyklus (23.10. vormittags) bootet
  kiekr, formatiert und arbeitet wieder mit Speicher (Konsole):

  ```
  <inf> main: kiekr_firmware v0.4.8 — USB + BLE NUS (paired)
  <wrn> littlefs: can't mount (LFS -84); formatting
  <inf> pktstore: lfs mounted: 143 blocks × 4096 B (141 free)
  <inf> pktstore: flash scan: empty log, next seq=1
  ```

  Verlust gegenüber vorher: die 776 Paketdatensätze des kiekr-Paketspeichers (vorher
  `flash scan: 776 records, next seq=4494`) und, falls sie dort lagen, Daten aus der
  littlefs-Partition. Freigegeben.

## 8. Nebenbefunde (Werkzeug und Bootloader, nicht Firmware)

- OTAFIX-Bootloader 0.9.2 (FakeTec, "Jun 13 2025"): läuft er ohne SoftDevice ("SoftDevice: not
  found", z. B. unter kiekr_firmware), startet er nach vollständigem UF2-Kopieren und auch nach
  `adafruit-nrfutil dfu serial` ("Device programmed") nicht neu; der USB-Teil meldet sich nicht neu
  an (sessionID unverändert). Erst ein Stromzyklus hilft. Mit SoftDevice startete er sofort neu.
  Dreimal beobachtet; Ursache nicht untersucht.
- Das UF2-Laufwerk desselben FakeTec wurde einmal im 1,2-s-Takt eingehängt und sofort wieder
  ausgeworfen (`DiskMountApproval`, 10 ms später `DiskEjectApproval`); nach einem Stromzyklus normal.
- Ein ESP32-Heltec bootet beim Öffnen des CP2102-Ports neu, auch mit DTR/RTS aus; Tests, die auf
  Empfang am Heltec warten, müssen den Port über den ganzen Ablauf offen halten (`hold.py`,
  `comp.py shell …`).

## Reproduktion

```sh
PLATFORMIO_BUILD_FLAGS='-D FIRMWARE_BUILD_DATE=\"hw-07fa9c5\"' pio run -e ProMicro_repeater   # usw.
python3 tools/refactoring/nodetool.py cli 5095333DA2CCCBD9 ver @gets.txt                       # FakeTec
python cli2.py /dev/cu.usbserial-0001 4 ver @gets.txt                                           # Heltec
python advrate.py /dev/cu.usbserial-0001 5095333DA2CCCBD9 10                                    # Advert-Quote
comp.py /dev/cu.usbserial-0001 "shell comp.py /dev/cu.usbmodem1301 send 1fe03aad38ed text ;; listen 4 ;; sync"
```

UF2-Modus ohne Doppel-Reset (aus MeshCore heraus, SoftDevice vorhanden): `uf2entry.c` und
`uf2entry.ld` in den Logs, gebaut mit
`arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -nostdlib -T uf2entry.ld uf2entry.c`, `objcopy -O binary`,
`adafruit-nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0x00B6 --application e.bin`, dann
`dfu serial` nach dem 1200-Baud-Touch. Die App setzt GPREGRET = 0x57 und resettet.

# Upstream-PR vorbereiten: Mindestlänge für 14 Companion-Befehle

Stand: 2026-11-11. Nichts gepusht, kein PR angelegt. Einreichen nur mit Marcels ausdrücklichem Go.

## Branch und Basis

* PR-Branch (lokal): `upstream-pr/companion-cmd-frame-min-len`, zwei signierte Commits auf `upstream/dev` = `09eac55bc3c8891f6ea9df73898d5f43c82ecfc7`:
  * `e7f78af42e63445b2e6c52c93962bbe411e00d2f` Fix (nur `examples/companion_radio/MyMesh.cpp`, +22 / -13), Quelle Fork-Commit `c54cf273`, Commit-Text neu geschrieben.
  * `b58cc946507eddb8c00cf53933365bfb9a177279` Hostbuild `native_companion` und Test `test_cmd_frame_min_len` (19 Dateien, +1385 / -18, davon etwa 1000 Zeilen Mocks).
* Dieser Notizbranch: `docs/upstream-pr-companion-cmd-frame-min-len`, ab `integration` (`446e3afe`).

## Schritt 0: Protokollprüfung (gibt es Sender mit kürzeren Frames?)

**Ergebnis: Nein. Kein Minimum musste gesenkt werden.** Optionale Felder bleiben optional (lat, lon, lastmod bei `ADD_UPDATE_CONTACT`; Repeat-Flag bei `SET_RADIO_PARAMS`; Felder nach `manual_add_contacts` bei `SET_OTHER_PARAMS`; `max_hops` bei `SET_AUTOADD_CONFIG`). Eine Auffälligkeit bei `FACTORY_RESET` (meshcore_py), siehe unten.

Quellen:

* **Doku** `docs/companion_protocol.md`: beschreibt diese Befehle nicht (nur Channel-Data-Datagramme, Abschnitt 6); keine Aussage über Längen.
* **KiekR** (`kiekr_app/core-lib/src/companion/cmd.rs`, selbst gelesen): `build_add_update_contact` setzt `1+32+1+1+1+64+32+4 = 136` fest, lat und lon nur zusammen (+8), lastmod nur mit beiden (+4). `build_remove_contact`, `build_share_contact`, `build_get_contact_by_key`: 33. `build_set_radio_params`: 11 (+1 mit Repeat). `build_set_radio_tx_power`: 2. `build_set_tuning_params`: 9. `build_set_other_params`: 2 bis 5. `build_reboot`: 7. `build_factory_reset`: `"reset"` nach dem Code, 6. `build_set_autoadd_config`: 3. `build_send_path_discovery_req`: 34.
* **meshcore.js, meshcore_py, meshcore-open (Dart)**: gelesen über WebFetch (Zusammenfassungen eines Modells, Zeilennummern nicht verlässlich; die FACTORY_RESET-Zeile von meshcore_py habe ich zusätzlich im Rohtext bestätigt). meshcore-cli baut keine Frames selbst, sondern ruft meshcore_py.
* **Ältere Firmware**: `git log -S` auf `MyMesh.cpp`: das Layout bis `last_advert_timestamp` (136 Bytes) steht seit dem ersten Auftritt von `updateContactFromFrame` (`f58a34f5`) so da; der Guard `len >= 36` ist seit damals unverändert. Eine ältere Firmware mit kürzerem Pflichtlayout gibt es also nicht. (Die allererste Fassung prüfte die optionalen GPS-Felder mit `i + 8 >= len`, also verkehrt herum; das betrifft nur den optionalen Teil.)

| Befehl | Code | Vorher in `upstream/dev` | Neues Minimum | Sender (Länge) |
|---|---|---|---|---|
| SEND_CHANNEL_DATA | 62 | `len >= 4`, data_type hinter dem Pfad ungeprüft | 3 + Pfad + 2 | meshcore.js min 5, meshcore_py 5+, Dart nicht gefunden. Entspricht dem Layout, kürzere Frames hätten kein data_type. |
| ADD_UPDATE_CONTACT | 9 | `len >= 36` | 136 | KiekR 136 bis 148; meshcore.js 144; meshcore_py 144; Dart 136 bis 148 (Dart lässt lat/lon weg: genau am Minimum) |
| REMOVE_CONTACT | 15 | keine | 33 | KiekR 33; js 33; py 33; Dart 33 |
| SHARE_CONTACT | 16 | keine | 33 | KiekR 33; js 33; py 33; Dart 33 |
| GET_CONTACT_BY_KEY | 30 | keine | 33 | KiekR 33; py 33 (kürzer nur, wenn der Aufrufer einen zu kurzen Schlüssel übergibt); Dart 33 |
| SET_RADIO_PARAMS | 11 | keine | 11 | KiekR 11 bis 12; js 11; py 11 bis 12; Dart 11 bis 12 |
| SET_RADIO_TX_POWER | 12 | keine | 2 | KiekR 2; js 2; Dart 2; py 5 (4 Byte Wert) |
| SET_TUNING_PARAMS | 21 | keine | 9 | KiekR 9; py 11; js und Dart nicht gefunden |
| SET_OTHER_PARAMS | 38 | keine | 2 | KiekR 2 bis 5; js 2; py 4 bis 5; Dart 5 |
| SET_PATH_HASH_MODE | 61 | `cmd_frame[1]` vor `len >= 3` | 3 (nur Reihenfolge) | KiekR 3; py 3; Dart 3 |
| REBOOT | 19 | keine | 7 | alle vier: 7 (`"reboot"`) |
| FACTORY_RESET | 51 | keine | 6 | KiekR 6; **meshcore_py 1 (nur `0x33`)**; js und Dart nicht gefunden |
| SET_AUTOADD_CONFIG | 58 | keine | 2 | KiekR 3; Dart 2 bis 3 |
| SEND_PATH_DISCOVERY_REQ | 52 | `cmd_frame[1]` vor `len >= 34` | 34 (nur Reihenfolge) | KiekR 34; py 34 |

**Befund meshcore_py `confirm_factory_reset`**: sendet nur `b"\x33"`, ohne `"reset"`. Der Handler vergleicht `cmd_frame[1..5]` mit `"reset"`; bei einem 1-Byte-Frame sind das Altbytes des Puffers. Der Befehl wirkte upstream also nur, wenn der vorige Frame zufällig mit `"reset"` an dieser Stelle endete. Mit dem Fix wird er immer abgelehnt. Das Minimum bleibt bei 6, weil der Handler das Wort braucht. Das ist der einzige Sender, den der Fix tatsächlich verändert, und er war vorher schon nicht verlässlich. Es lohnt einen Hinweis an die meshcore_py-Maintainer; im PR-Text steht ein Satz dazu.

Grenzen der Prüfung: JS- und Dart-"nicht gefunden" stammt aus Modell-Zusammenfassungen, die Dateien wurden nicht vollständig gelesen; meshcore-cli nur zu 100k von 250k Zeichen. Weitere Clients (z. B. Web-Flasher, Home-Assistant-Integration, andere Apps) nicht geprüft. **NICHT VERIFIZIERT**: ob irgendein Sender in freier Wildbahn kürzere Frames schickt.

## Was und warum

Siehe Commit-Text. Kurz: `handleCmdFrame()` liest Felder an festen Offsets. `cmd_frame` und der Empfangspuffer werden zwischen Frames nicht gelöscht, `checkRecvFrame()` kopiert nur die empfangenen Bytes. Bei 14 Befehlen fehlt die Längenprüfung oder sie steht hinter dem Zugriff auf `cmd_frame[1]`; ein kurzer Frame arbeitet dann mit den Altbytes des Vorgängers. Es wird nicht außerhalb von `cmd_frame` gelesen (größter fester Offset 135, Puffer 177 Bytes). Der Schaden ist falsch verarbeitete Befehle: auf einem T-Echo (Messung aus dem Fork-Commit, hier nicht wiederholt) setzte ein kurzer `SET_TUNING_PARAMS` nach einem CLI-Frame die Werte aus dem CLI-Text, ein 36-Byte-`ADD_UPDATE_CONTACT` legte einen Kontakt aus 100 Bytes des Vorgängerframes an, ein kurzer `REBOOT` startete neu.

## Diffgröße und Alternative

```
Fix (MyMesh.cpp):           +22 / -13
Test, Mocks, platformio.ini, CI (19 Dateien):  +1385 / -18
```

Der Test braucht einen Hostbuild des echten `MyMesh.cpp`; dafür kommen rund 1000 Zeilen Mocks aus dem Fork mit (Dateisystem, Board, Target, Identity, SHA256, ...). Das ist für Maintainer eine große Zugabe zu einem 35-Zeilen-Fix. Alternative: nur den Fix-Commit einreichen und die Hardwaremessung im Text nennen, den Hostbuild später getrennt. **Entscheidung für Marcel**, nicht getroffen.

## Angepasst gegenüber dem Fork

* Fix: `c54cf273` unverändert übernommen (Cherry-Pick ohne Konflikt), Commit-Text neu (ohne Verweis auf Fork-Notizen und ohne den Satz "no host test").
* Test: neu geschrieben (`test_cmd_frame_min_len.cpp`, 3 Tests), nicht die Fork-Suiten `test_commands`, `test_frame_bounds`, `test_frame_sweep`, `test_frame_reader`, die auf dem umgebauten `MyMesh.cpp` und dem `FrameReader` des Forks aufbauen. Übernommen aus dem Fork: `CompanionHarness.h`, `identity_stubs.cpp` (an die ältere Signatur `LocalIdentity::writeTo(uint8_t*, size_t)` ohne `const` angepasst), `test_companion_frames.cpp` (ohne `frame_reader_overruns`), die Mocks.
* `platformio.ini`: Env `native_companion` (ohne `FRAME_READER_COUNT_OVERRUNS`, mit `CommonGpsPrefs.cpp` in der Quellenliste, weil upstream `MyMesh::handleCommand` darauf verweist); `test_companion_frames` in `test_ignore` von `native`.
* `.github/workflows/run-unit-tests.yml`: `-e native_companion` angehängt.
* Mocks `Mesh.h` und `RadioLib.h` des Forks nicht übernommen (der Fork-`Mesh.h` bricht `native_kiss_modem` auf upstream).

## Prüfungen

### Tests

Auf `b58cc946`, je ein Lauf pro Env:

```
native                        64 test cases: 64 succeeded
native_kiss_modem             43 test cases: 43 succeeded
native_rak3401_onboard_radio   7 test cases:  7 succeeded
native_companion               3 test cases:  3 succeeded
```

### Mutation

`MyMesh.cpp` auf `upstream/dev` zurückgesetzt, `pio test -e native_companion -vv`: `4 test cases: 2 failed, 1 succeeded`. Die Spuren (`SCOPED_TRACE`) nennen die roten Befehle: ADD_UPDATE_CONTACT, REMOVE_CONTACT, SHARE_CONTACT, GET_CONTACT_BY_KEY, SET_RADIO_PARAMS, SET_RADIO_TX_POWER, SET_TUNING_PARAMS, SET_OTHER_PARAMS, REBOOT, FACTORY_RESET, SET_AUTOADD_CONFIG, SEND_CHANNEL_DATA = **12 von 14**. Nicht rot: SET_PATH_HASH_MODE und SEND_PATH_DISCOVERY_REQ; dort antwortete schon der alte Code auf kurze Frames mit "unsupported", die Änderung ist nur die Reihenfolge der Bedingungen, ohne beobachtbare Wirkung. Der Test zeigt für sie also keinen Unterschied. Datei danach wiederhergestellt (`git checkout HEAD -- ...`).

### Builds und Delta gegen `upstream/dev`

4 Companion-Envs, je Baseline (`upstream/dev` in eigenem Worktree) und Fix, alle rc=0. Libdeps aus Vorlagen geseedet (Registry-Limit), Baseline und Fix gleich geseedet. Absolute Größen können von frischen Builds abweichen, das Delta nicht.

| Env | Plattform | Flash vorher | Flash nachher | Delta | RAM-Delta |
|---|---|---|---|---|---|
| Heltec_v3_companion_radio_ble | ESP32-S3 | 1676761 | 1676717 | -44 | 0 |
| Heltec_v3_companion_radio_usb | ESP32-S3 | 663221 | 663173 | -48 | 0 |
| RAK_4631_companion_radio_ble | nRF52 | 489036 | 488844 | -192 | 0 |
| RAK_4631_companion_radio_usb | nRF52 | 480028 | 479836 | -192 | 0 |

Die Größe sinkt, obwohl Prüfungen dazukommen. Ursache nicht untersucht (**Vermutung/ungeprüft**: der Compiler spart sich die Pfade, die jetzt eine Länge brauchen; weder Map noch Disassembly verglichen).

### Konflikte und offene PRs

* Quellcode: `git merge-tree` des PR-Branches gegen `upstream-candidate/prefs-json-range-check` (#3797, `31ee36db`): kein Konflikt. Gegen #3799 und #3808 kein Konflikt in `MyMesh.cpp`.
* Testinfrastruktur gegen #3799 (`prefs-extra-sf-disc`): Konflikte in `platformio.ini`, `run-unit-tests.yml` und, weil beide Mocks anlegen, `test/mocks/Adafruit_LittleFS.h`, `Arduino.h`, `target.h` (add/add). Hier überschneiden sich zwei Hostbuilds mit eigenen Mocks; die Reihenfolge der Einreichung entscheidet, wer rebased. Der zweite PR muss die Mocks von Hand zusammenführen.
* Gegen #3808 (`lr-packet-score-sf`): Konflikte nur in `platformio.ini` und `run-unit-tests.yml` (benachbarte Zeilen, beide Einträge behalten).
* #3584 (agessaman, `CMD_SET_RADIO_PARAMS`): Diff gelesen. Es ändert `filterRecvFloodPacket`, `MyMesh.begin()`, den DEVICE_INFO-Zweig und ergänzt eine Zeile `_active_freq_khz = freq;` im Inneren des SET_RADIO_PARAMS-Zweigs. Die Bedingungszeile, die wir ändern, fasst es nicht an. `merge-tree` (PR-Kopf von `refs/pull/3584/head` geholt): Exit 0, kein Konflikt.
* Alle offenen PRs, die `examples/companion_radio/MyMesh.cpp` ändern (Liste per `gh pr list --json files`), auf die 14 Befehlsnamen in den geänderten Zeilen geprüft, soweit ich sie einzeln diffte (#3584, #3447, #3362, #3403, #3195, #3214, #3553, #3526, #3480, #3293): keine Treffer außer #3584. **Die übrigen Treffer der Liste (u. a. #3754, #3734, #3683, #3670, #3638, #3630, #3583, #3576, #3560, #3523, #3472, #3454, #3450, #3438, #3410, #3383, #3361) nicht einzeln gelesen.**
* Suche nach ähnlichen PRs ("frame length", "short frame", "CMD_ADD_UPDATE_CONTACT" u. a.): nur #3643 (gemergt, CHANNEL_TXT_MSG), #3386 (gemergt, Payloadgröße CHANNEL_DATA).

## Ungeprüft

* Hardware: keine Messung auf diesem Stand. Die T-Echo-Messung stammt aus dem Fork-Commit (dev `428a95c6`), hier nicht wiederholt; der PR-Text nennt sie als Messung des Autors an einem früheren Stand.
* Sender-Prüfung nur über KiekR (selbst gelesen) und Modell-Zusammenfassungen der drei Bibliotheken, siehe Grenzen oben.
* Nur 4 Envs gebaut, kein RP2040, STM32, kein WiFi-Companion (die Frame-Logik ist dieselbe Datei).
* Ursache der Flash-Verkleinerung nicht geprüft.
* Der Test läuft auf macOS/arm64 mit clang; Linux-CI (gcc) nicht gelaufen. Ob die Mocks dort ohne Warnungs-Fehler bauen, ist offen.
* Commits lokal signiert (Signaturobjekt vorhanden); `git log --show-signature` kann lokal nicht prüfen (`gpg.ssh.allowedSignersFile` fehlt).
* CI von upstream nicht gelaufen.
* Humanizer: Regeln von Hand auf PR-Text und Commit-Texte angewendet (keine Gedankenstriche, keine Gegenüberstellungen, keine Schlusszeilen); kein eigener Rewrite-Lauf.

## PR-Text-Entwurf (Englisch)

Titel: `Refuse companion command frames shorter than the fields they read`

````
`handleCmdFrame()` reads command fields at fixed offsets. 35 of its branches test the frame length first, 14 do not, or read `cmd_frame[1]` before testing it. `cmd_frame` is not cleared between frames and `checkRecvFrame()` copies only the received bytes, so a short frame is carried out with the bytes the previous frame left behind.

Nothing is read outside `cmd_frame` (the largest fixed offset is 135, the buffer holds 177 bytes), but the node acts on stale data: a short `SET_TUNING_PARAMS` takes its values from the previous command, a 36 byte `ADD_UPDATE_CONTACT` creates a contact from the tail of an earlier frame, and a short `REBOOT` can reboot.

This adds the minimum length to each affected command. A short frame now falls through to the existing `else` and gets `ERR_CODE_UNSUPPORTED_CMD`, like the commands that were already guarded. Optional trailing fields stay optional.

| Command | Minimum |
|---|---|
| ADD_UPDATE_CONTACT | 136 (was 36) |
| REMOVE_CONTACT, SHARE_CONTACT, GET_CONTACT_BY_KEY | 33 |
| SET_RADIO_PARAMS | 11 |
| SET_RADIO_TX_POWER | 2 |
| SET_TUNING_PARAMS | 9 |
| SET_OTHER_PARAMS | 2 |
| SET_AUTOADD_CONFIG | 2 |
| REBOOT | 7 |
| FACTORY_RESET | 6 |
| SET_PATH_HASH_MODE | 3 |
| SEND_PATH_DISCOVERY_REQ | 34 |
| SEND_CHANNEL_DATA | 3 + path + 2 |

The one existing guard that changes is `ADD_UPDATE_CONTACT`: `updateContactFromFrame()` reads through `last_advert_timestamp` (136 bytes) without a length test, and `writeContactRespFrame()` writes the same layout. Only lat, lon and lastmod are optional.

The new minimums match what KiekR, meshcore.js, meshcore_py and meshcore-open send. One exception: meshcore_py's `confirm_factory_reset` sends only the command byte and no `"reset"`, so it only ever worked when the previous frame happened to leave those bytes behind. It is now refused.

Tests: `native_companion` builds the real `MyMesh.cpp` against mocks and sends each command's full frame, then the same frame one byte below its minimum. Without the fix, 12 of the 14 fail. `SET_PATH_HASH_MODE` and `SEND_PATH_DISCOVERY_REQ` already answered short frames the same way, only the order of the conditions changes.

Flash changes by -44 to -192 bytes on four companion targets (Heltec V3 and RAK4631, BLE and USB), RAM unchanged.
````

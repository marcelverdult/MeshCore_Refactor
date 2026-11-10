# Upstream-PR vorbereiten: extra_sf und discovery_mod_timestamp in /prefs.json speichern

Stand: 2026-11-11. Nichts gepusht, kein PR angelegt. Einreichen nur mit Marcels ausdrücklichem Go.

## Branch und Basis

* Upstream-Branch (nur Fix und Tests): `claude/upstream-prefs-extra-sf-disc`, Commit `b9805cc4` (signiert, ein Commit).
* Basis: `upstream/dev` = `ef4477b69de764915c4377f51e8eba8072543e8a` (PR #3798).
* Quelle: Fork-Commit `67b8843d` (`pr/prefs-extra-sf-disc`). Commit-Text neu geschrieben (ohne RCC-Marker, ohne Hardwareteil, der nur im Fork belegt war).
* Dieser Notizbranch: `docs/upstream-pr-prefs-extra-sf-disc`, ab `integration`.

## Was und warum

`NodePrefs::saveSerial()/loadSerial()` (`src/helpers/CommonCLI.h`) lassen zwei Felder weg. Beide gehen bei jedem Neustart eines Knotens verloren, der von `/prefs.json` bootet:

* `discovery_mod_timestamp`: wird von Repeater und Sensor in Discovery-Antworten benutzt. Der Legacy-Binärloader liest es noch (`CommonCLI.cpp`, Offset 162), verloren geht es erst seit dem Wechsel auf `/prefs.json`.
* `extra_sf`: per `extra.sf` gesetzt, in keinem Format gespeichert.

Fix: drei Zeilen in `CommonCLI.h` (`def("disc_mod", ...)`, `def("extra_sf", ...)` als Hex im `radio`-Objekt, `memset(extra_sf, 0, ...)` im Konstruktor). Kein Eingriff in `.cpp`.

## Angepasst gegenüber dem Fork-Commit

* `platformio.ini`: neues Env `native_node_prefs`; `test_node_prefs` in `test_ignore` von `native` ergänzt (an die neue Listenform von upstream angepasst, dort steht jetzt auch `test_rak3401_board_startup`).
* `.github/workflows/run-unit-tests.yml`: `-e native_node_prefs` an die aktuelle Zeile von upstream angehängt (`-e native -e native_kiss_modem -e native_rak3401_onboard_radio -e native_node_prefs`).
* Test: `#include <Arduino.h>` vor `CommonCLI.h` ergänzt. Upstream-Header `LocationProvider.h` nutzt `millis()` ohne eigenen Include, der Test baute sonst nicht.
* Mocks `Adafruit_LittleFS.h`, `target.h` neu, `Arduino.h` um `constrain` und `ltoa` erweitert (unverändert aus dem Fork).
* Diffstat: 7 Dateien, +330 / -1.

## Prüfungen

### Tests (alle Envs aus dem Workflow)

`pio test -e native -e native_kiss_modem -e native_rak3401_onboard_radio -e native_node_prefs` auf `b9805cc4`:

```
native                        test_mesh_tables, test_empty_contact_slots, test_config_serializer,
                              test_utils, test_utf8_helpers, test_rak3401_onboard_radio,
                              test_routing_policy, test_time_series        PASSED
native_node_prefs             test_node_prefs                              PASSED
native_rak3401_onboard_radio  test_rak3401_onboard_radio, test_rak3401_board_startup  PASSED
native_kiss_modem             test_kiss_modem                              PASSED
================ 122 test cases: 122 succeeded ================
```

### Mutation

`src/helpers/CommonCLI.h` auf Stand `upstream/dev` zurückgesetzt, `pio test -e native_node_prefs`:

```
SaveWritesTheDiscoveryTimestamp        [FAILED]
SaveWritesTheExtraSpreadFactors        [FAILED]
LoadRestoresTheDiscoveryTimestamp      [FAILED]
LoadRestoresTheExtraSpreadFactors      [FAILED]
RoundTripKeepsBothFields               [FAILED]
UnknownKeysAreSkipped                  [FAILED]
ExtraSfStartsZeroed, LoadWithoutTheNewKeysLeavesThemAlone  [PASSED]
6 failed, 2 succeeded
```

Datei danach wiederhergestellt, `git status` sauber.

### Builds und Delta gegen upstream/dev

Alle sechs Envs bauen (rc=0) auf Fix und Baseline (`ef4477b6`, eigener Worktree). RAM-Delta überall 0.

| Env | Plattform | Flash vorher | Flash nachher | Delta |
|---|---|---|---|---|
| Heltec_v3_repeater | ESP32-S3 | 1182941 | 1183125 | +184 |
| Heltec_v3_room_server | ESP32-S3 | 1176277 | 1176473 | +196 |
| Heltec_v3_companion_radio_ble | ESP32-S3 | 1676749 | 1676749 | 0 |
| RAK_4631_repeater | nRF52 | 464040 | 464248 | +208 |
| RAK_4631_room_server | nRF52 | 454848 | 455056 | +208 |
| RAK_4631_companion_radio_ble | nRF52 | 489036 | 489036 | 0 |

Companion: Delta 0 ist plausibel, weil der Companion eigene `NodePrefs` (`examples/companion_radio/NodePrefs.h`) nutzt, nicht die aus `CommonCLI.h`. Das ist aus den Zahlen geschlossen, nicht im Linker-Map geprüft.

### Offene PRs und Konflikte (`gh pr list`, `git merge-tree`)

* Suche `extra_sf`: kein Treffer. Suche `discovery_mod_timestamp`: nur #3086 (agessaman, Region-Discovery, anderes Thema).
* Gegen #3797 (unser prefs.json-Range-Check, `31ee36db`): `git merge-tree` ohne Konflikt (rc=0).
* Alle 21 offenen PRs, die `CommonCLI.h` anfassen (#3797 bis #3019), einzeln gegen `upstream/dev` und gegen den Fix-Branch gemerged: die Konfliktliste ist in jedem Fall identisch. Der Fix verursacht keinen neuen Konflikt. Bereits ohne Fix konfliktieren u. a. #3086 und #3027 in `CommonCLI.h` sowie #3638 (viele Dateien); das ist deren Rebase-Bedarf.

## Ungeprüft

* Hardware: kein Gerät. Der Fork-Commit nennt einen Up- und Downgrade-Test auf einem techo-b43b; auf diesem Branch nicht wiederholt, daher im PR-Text nicht behauptet.
* Leser von `discovery_mod_timestamp` (`simple_repeater/MyMesh.cpp`, `simple_sensor/SensorMesh.cpp`) und das Zusammenspiel mit #3086 nur gelesen, nicht getestet.
* `extra.sf` nur für `USE_LR2021` kompiliert; kein Build einer solchen Variante.
* Nur 6 Envs gebaut, kein wio-e5, kein RP2040.
* Test-Output der Mutation gekürzt wiedergegeben (grep auf FAILED/PASSED).
* Commit lokal signiert (SSH-Signatur im Objekt vorhanden); `git log --show-signature` meldet lokal "allowedSignersFile" fehlt, die Prüfung ist hier also nicht gelaufen.
* Humanizer: Texte von Hand gegen die Regeln geprüft (keine Gegenüberstellungen, keine Dreierreihen, keine Gedankenstriche); der Skill selbst wurde nicht aufgerufen.

## Commit-Text (Englisch, `b9805cc4`)

Siehe `git show b9805cc4`. Titel: `Persist extra_sf and discovery_mod_timestamp in /prefs.json`.

## PR-Text-Entwurf (Englisch)

Titel: `Save extra_sf and discovery_mod_timestamp in /prefs.json`

````
`NodePrefs` leaves two fields out of its `/prefs.json` serialisation, so a node that boots from that file loses both on every reboot:

- `discovery_mod_timestamp`, which the repeater and sensor use when they answer discovery requests. The legacy binary loader still reads it (`CommonCLI.cpp`, offset 162), so it was only lost after the move to `/prefs.json`.
- `extra_sf`, the extra spread factors set with `extra.sf`. It was never saved in either format.

Both are now written: `disc_mod` next to the advert intervals, and `extra_sf` inside the `radio` object, hex encoded like the companion's scope key. `extra_sf` had no initialiser and is now written out, so the constructor zeroes it.

Compatibility: a file without the two keys loads as before, because the serializer leaves a field alone when its key is missing. A firmware that does not know the keys skips them like any unknown key.

The fix is three lines in `CommonCLI.h`.

**Evidence**

New env `native_node_prefs` with 8 cases (save, load, round trip, file without the keys, unknown keys, zeroed default). `NodePrefs` sits in `CommonCLI.h`, whose filesystem types exist only per platform, so the env selects the nRF52 branch and takes the types from a mock. It is added to `run-unit-tests.yml`.

6 of the 8 cases fail on `dev` (`ef4477b6`) and pass with the change. `pio test -e native -e native_kiss_modem -e native_rak3401_onboard_radio -e native_node_prefs`: 122 of 122 pass.

Builds (flash delta against `dev`, RAM unchanged in all):

| env | flash |
|---|---|
| Heltec_v3_repeater / room_server / companion_radio_ble | +184 / +196 / 0 B |
| RAK_4631_repeater / room_server / companion_radio_ble | +208 / +208 / 0 B |

**Not tested**

- No device test.
- The `extra.sf` command is compiled only for `USE_LR2021` boards; no such variant was built.
````

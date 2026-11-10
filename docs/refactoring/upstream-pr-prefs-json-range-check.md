# Upstream-PR vorbereiten: Grenzprüfung nach dem Laden von /prefs.json

Stand: 2026-11-10. Nichts gepusht, kein PR angelegt. Einreichen nur mit Marcels ausdrücklichem Go.

## Branch und Basis

* Upstream-Branch (nur der Fix): `claude/upstream-prefs-json-range-check-d22699`, Commit `31ee36db` (signiert, ein Commit).
* Basis: `upstream/dev` = `f678f6ac151c795bd3fb6d519b401acbadd92d17` (PR #3784).
* Quelle: Fork-Commit `bc5708f7` (`pr/prefs-json-range-check`), per `cherry-pick -n` übernommen, Commit-Text neu geschrieben.
* Dieser Notizbranch: `docs/upstream-pr-prefs-json-range-check`, ab `integration` (`f307e08b`).

## Übernommen

* `src/helpers/CommonCLI.cpp`, `CommonCLI.h`: Klemmcode aus `loadPrefsInt()` nach `CommonCLI::sanitisePrefs()`, von beiden Loadern aufgerufen. Log bei Parse-Fehler.
* `src/helpers/ConfigSerializer.cpp`: nicht endliche float/double-Werte (nan, inf, Überlauf) werden verworfen, Feld behält den Default. Bitprüfung, bei float nach dem Verengen.
* `test/test_config_serializer/test_config_serializer.cpp`: 5 neue Fälle (3 rot ohne Fix, 2 Verhalten festgehalten).
* Diffstat: 4 Dateien, +149 / -34.

## Weggelassen

* Alle Fork-Notizen (`docs/fixes/`, `docs/refactoring/`, `tools/refactoring/`): waren im Fix-Commit gar nicht enthalten.
* Keine Änderung an `platformio.ini` oder `run-unit-tests.yml`: `test_config_serializer` läuft in upstream schon im Env `native` (`build_src_filter` enthält `ConfigSerializer.cpp`). Kein neuer Test-Eintrag nötig.
* Der Hinweis "RCC-SKIPPED" und die Fork-internen Commit-Hashes im alten Commit-Text.

## airtime_factor 9 auf 99: gehört zum Fix

Beleg auf `upstream/dev`:

* `CommonRadioPrefs.cpp:88`: `set af` erlaubt 0 bis 9.
* `CommonRadioPrefs.cpp:104-111`: `set dutycycle N` speichert `100/N - 1`; `set dutycycle 1` ergibt 99 (bestätigt auch durch die Messung im Text von PR #3646).
* `CommonCLI.cpp:111`: der Legacy-Loader klemmt bisher auf 9.

Würde die gemeinsame Funktion bei 9 bleiben, schnitte der neue Klemmcode auf dem JSON-Pfad jeden Duty Cycle unter 10 Prozent beim nächsten Boot ab. Der Fix würde also selbst eine Regression einführen. Die 99 sind deshalb im Commit begründet, kein eigener PR nötig. `set af` bleibt bei 0 bis 9 (absichtlich nicht angefasst, im PR-Text erwähnt).

Möglicher eigener Punkt, falls upstream das trennen will: Obergrenze im Legacy-Loader allein anheben. Ohne den JSON-Fix wäre das aber ein reiner Randfall; nicht empfohlen.

## Tests

Auf dem Fix-Branch: `pio test -e native -e native_kiss_modem -e native_rak3401_onboard_radio` (die drei Envs aus `run-unit-tests.yml`):

```
119 test cases: 119 succeeded in 00:00:34.592
native: test_mesh_tables, test_empty_contact_slots, test_config_serializer, test_utils,
        test_utf8_helpers, test_rak3401_onboard_radio, test_routing_policy, test_time_series  PASSED
native_rak3401_onboard_radio: test_rak3401_onboard_radio, test_rak3401_board_startup  PASSED
native_kiss_modem: test_kiss_modem  PASSED
```

### Mutation

`src/helpers/ConfigSerializer.cpp` auf den Stand von `upstream/dev` zurückgesetzt (Fix im Parser rückgängig), `pio test -e native -f test_config_serializer`:

```
test_config_serializer.cpp:340: LoadSerial_KeepsDefaultOnNotANumber        [FAILED]
test_config_serializer.cpp:350: LoadSerial_KeepsDefaultOnInfinity          [FAILED]
test_config_serializer.cpp:362: LoadSerial_KeepsDefaultWhenFloatOverflows  [FAILED]
LoadSerial_AcceptsFiniteNumbers / LoadSerial_StoresZeroForGarbage          [PASSED]
```

Datei danach wiederhergestellt, `git diff` leer.

## Builds und Delta gegen upstream/dev

Alle acht Envs bauen (rc=0) auf Fix und Baseline. RAM-Delta überall 0 Byte.

| Env | Plattform | Flash vorher | Flash nachher | Delta |
|---|---|---|---|---|
| Heltec_v3_repeater | ESP32-S3 | 1182941 | 1182981 | +40 |
| Heltec_v3_room_server | ESP32-S3 | 1176277 | 1176321 | +44 |
| Heltec_v3_companion_radio_ble | ESP32-S3 | 1676749 | 1676773 | +24 |
| RAK_4631_repeater | nRF52 | 464040 | 464088 | +48 |
| RAK_4631_room_server | nRF52 | 454848 | 454896 | +48 |
| RAK_4631_companion_radio_ble | nRF52 | 489036 | 489068 | +32 |
| PicoW_repeater | RP2040 | 528768 | 528816 | +48 |
| wio-e5-mini_repeater | STM32WL | 206508 | 206548 | +40 |

Wio-E5 steht bei 90,0 Prozent Flash (206548 von 229376 Byte), der Fix braucht dort 40 Byte.

## Ungeprüft

* Hardware: kein Gerät, kein Flashen. Kein Test mit einer handgeschriebenen `/prefs.json` auf einem Board.
* `sanitisePrefs()` hat keinen Host-Test (kein natives Env kompiliert `CommonCLI.cpp`); abgedeckt nur durch die Builds.
* `test/test_companion_node_prefs` taucht im Lauf von `native` nicht in der Ergebnisliste auf. Warum, ist nicht untersucht (vermutlich per `test_ignore`/Filter ausgeschlossen). Ob der Fix die Companion-`NodePrefs` über denselben Serializer wie behauptet beeinflusst, ist nur gelesen, nicht getestet.
* Nur diese 8 Envs gebaut, nicht alle Varianten.
* Die Aussage "nRF52 baut mit -Ofast" im Commit-Text stammt aus PR #3646 und dem alten Fork-Text, hier nicht neu geprüft.
* Offene Fremd-PR-Überschneidung: #3322 (ConfigSerializer `rd_len`) fasst dieselbe Datei an; Konflikt nicht geprüft.

## Commit-Text (Englisch, `31ee36db`)

```
Check value ranges after loading /prefs.json

loadPrefs() reads /prefs.json and uses whatever is in it. Only the legacy
binary loader (loadPrefsInt) clamps values, so a hand edited or externally
written file can put freq, bw, sf, cr, tx_power_dbm, the delay factors or the
bridge settings outside the limits the node otherwise enforces. These values
go straight into the radio setup.

Changes: (siehe `git show 31ee36db`)
```

## PR-Text-Entwurf (Englisch)

Titel: `Clamp values loaded from /prefs.json, ignore nan and inf`

````
Nodes that boot from `/prefs.json` use whatever the file contains. Only the legacy binary loader (`loadPrefsInt`) clamps values. A file that was edited by hand or written by another tool can put `freq`, `bw`, `sf`, `cr`, `tx_power_dbm`, the delay factors or the bridge settings outside the limits the node otherwise enforces, and these values go straight into the radio setup.

This moves the clamping into `CommonCLI::sanitisePrefs()` and calls it from both loaders. The fields and limits are the ones the legacy loader already used, with one exception, below.

**airtime_factor.** The limit is now 0..99 instead of 0..9. `set dutycycle 1` stores `100/1 - 1 = 99`, so a limit of 9 would reset any duty cycle below 10 percent at the next boot, and with this change that would now also happen on the `/prefs.json` path. `set af` still accepts 0..9 only; I left that alone.

**nan and inf.** `constrain()` cannot catch a NaN, since both of its comparisons are false. So `ConfigSerializer` no longer stores a float or double that is not finite: a token like `nan`, `inf` or `1e40` leaves the field at the default the firmware set before reading the file. The check uses the bits and not a comparison, because nRF52 builds use `-Ofast`. For floats it runs after the narrowing, since a value that is finite as a double can overflow to infinity as a float. This also applies to the companion `NodePrefs`, which uses the same serializer.

`loadPrefs()` now logs when `loadSerial()` reports a parse error. A partially parsed file is still applied, and now sanitised.

**Evidence**

Five new cases in `test_config_serializer`. Three fail on `dev` (`f678f6ac`) and pass with the change: a file with `nan`, a file with `inf` and `-inf`, and a float that overflows to infinity. Two pin existing behaviour: finite values are stored, garbage still parses as zero.

`pio test -e native -e native_kiss_modem -e native_rak3401_onboard_radio`: 119 of 119 pass. With the parser change reverted, the three new cases fail.

Builds (flash delta against `dev`, RAM unchanged in all):

| env | flash |
|---|---|
| Heltec_v3_repeater / room_server / companion_radio_ble | +40 / +44 / +24 B |
| RAK_4631_repeater / room_server / companion_radio_ble | +48 / +48 / +32 B |
| PicoW_repeater | +48 B |
| wio-e5-mini_repeater | +40 B |

**Not tested**

- No device test with a hand written `/prefs.json`.
- `sanitisePrefs()` has no host test, since no native env compiles `CommonCLI.cpp`. It is covered by the builds only.
````

## Humanizer

Commit-Text und PR-Entwurf geprüft: keine Gegenüberstellungen "nicht X, sondern Y", keine Schlusszeilen, keine Dreierreihen als Stilmittel, keine Gedankenstriche. Der Stil folgt PR #3646 (Fettdruck-Abschnittslabels dort ebenfalls üblich).

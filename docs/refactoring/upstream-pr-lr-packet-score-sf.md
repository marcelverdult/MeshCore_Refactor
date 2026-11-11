# Upstream-PR vorbereiten: Paketscore mit dem echten Spreading Factor (LR1110, LR1121, LR2021)

Stand: 2026-11-11. Nichts gepusht, kein PR angelegt. Einreichen nur mit Marcels ausdrücklichem Go.

## Branch und Basis

* Upstream-Branch (Fix und Tests): `claude/upstream-lr-packet-score-sf-fix`, Commit `2facb916923534cdb8ee907eec7b9a3c3de6bb4a` (signiert, ein Commit).
* Basis: `upstream/dev` = `09eac55bc3c8891f6ea9df73898d5f43c82ecfc7`.
* Quelle: Fork-Commit `26a9bce2` (`upstream-candidate/lr-packet-score-sf`, Notiz 49ed34be). Per `git cherry-pick -n` übernommen, Konflikte in `platformio.ini` und `run-unit-tests.yml` von Hand gelöst, Commit-Text neu geschrieben (ohne den T1000-E-Hardwareteil und ohne Verweis auf Fork-Notizen).
* Dieser Notizbranch: `docs/upstream-pr-lr-packet-score-sf`, ab `integration` (`9dd80b85`).

## Was und warum

`RadioLibWrapper::packetScore()` (`src/helpers/radiolib/RadioLibWrappers.h`) rechnet für jeden Wrapper ohne eigene Überschreibung mit SF10. LR1110, LR1121 und LR2021 haben keine Überschreibung, melden ihren echten Faktor aber über `getSpreadingFactor()`. Die nötige SNR steigt mit fallendem SF (`packetScoreInt()`: -7,5 dB bei SF7 bis -20 dB bei SF12), also ist der Score unter SF10 zu hoch und darüber zu niedrig.

Fix: Die Basisklasse ruft `getSpreadingFactor()`. Die fünf Überschreibungen in SX1262, SX1268, SX1276, LLCC68 und STM32WLx lasen exakt dasselbe Member wie ihr `getSpreadingFactor()` und entfallen. Wrapper ohne eigenen Faktor fallen auf `LORA_SF` zurück.

Wirkung: Der Score geht ins Logging und in `calcRxDelay()`. Alle vier Rollen rechnen dort mit `rx_delay_base`, Default 0. Auf einer Standardkonfiguration ändert sich also nur, was geloggt wird; die Weiterleitungsverzögerung ändert sich nur, wo `rxdelay` über 0 gesetzt ist. Beispielrechnung (64 Byte, 500 ms Airtime, rxdelay 10): SF7 bei -5 dB Score 0,750 auf 0,188, Verzögerung 129 ms auf 1799 ms; SF12 bei -10 dB Score 0,375 auf 0,750, Verzögerung 993 ms auf 129 ms. Bei SF10 keine Änderung. Diese Zahlen stammen aus dem Fork-Commit und sind hier nicht neu gerechnet (die Tests prüfen den Score, nicht die Verzögerung).

## Diffgröße und Alternative

Gebaute Variante (saubere Lösung), `git diff upstream/dev HEAD --shortstat`:

```
src (6 Dateien):            5 insertions(+), 21 deletions(-)
Tests, platformio.ini, CI:  5 Dateien, 227 insertions(+), 1 deletion(-)
gesamt (11 Dateien):        232 insertions(+), 22 deletions(-)
```

Minimale Alternative: Basisklasse unverändert, nur in `CustomLR1110Wrapper.h`, `CustomLR1121Wrapper.h` und `CustomLR2021Wrapper.h` je eine `packetScore`-Überschreibung ergänzen, die `getSpreadingFactor()` nutzt. Geschätzt etwa 4 Zeilen je Datei, also rund +12 / -0 im Quellcode, drei Dateien statt sechs, keine gelöschten Zeilen. **Nicht gebaut, nicht getestet, Größe geschätzt** (Zeilenzahl der entfernten Überschreibungen als Maß). Nachteil: Der Default bleibt SF10, der Fehler wiederholt sich bei jedem neuen Treiber, und der Test müsste die drei Wrapper statt der Basisklasse abdecken. Wechsel nur auf Marcels Entscheidung, falls Maintainer die kleine Fassung bevorzugen.

## Angepasst gegenüber dem Fork-Commit

* `platformio.ini`: Env `native_radio_wrappers` ergänzt; `test_radio_wrappers` in `test_ignore` von `native` ergänzt, an die neue Listenform von upstream angepasst (dort stehen jetzt `test_kiss_modem` und `test_rak3401_board_startup`).
* `.github/workflows/run-unit-tests.yml`: `-e native_radio_wrappers` an die Zeile von upstream angehängt (`-e native -e native_kiss_modem -e native_rak3401_onboard_radio -e native_radio_wrappers`).
* `test/mocks/RadioLib.h`: `RADIOLIB_ERR_LORA_HEADER_DAMAGED (-24)` ergänzt, weil `RadioLibWrappers.cpp` auf upstream/dev diesen Code inzwischen nutzt (Wert aus `TypeDef.h` der gebauten RadioLib).
* Sonst unverändert aus dem Fork: `test/mocks/Arduino.h` (min, max, `random(a, b)`), `test_radio_wrappers.cpp` (7 Testfälle).

## Prüfungen

### Tests (alle nativen Envs des Workflows)

`pio test -e native -e native_kiss_modem -e native_rak3401_onboard_radio -e native_radio_wrappers` auf `2facb916`:

```
native                        8 Suiten (test_mesh_tables ... test_time_series)        PASSED
native_rak3401_onboard_radio  test_rak3401_onboard_radio, test_rak3401_board_startup  PASSED
native_radio_wrappers         test_radio_wrappers                                     PASSED
native_kiss_modem             test_kiss_modem                                         PASSED
================ 121 test cases: 121 succeeded ================
```

### Mutation

`RadioLibWrappers.h` auf Stand `upstream/dev` zurückgesetzt (zweimal ausgeführt, gleiches Ergebnis), `pio test -e native_radio_wrappers`:

```
ScoreFollowsTheReportedSpreadingFactor   [FAILED]
DoesNotAssumeSpreadingFactorTen          [FAILED]
FallsBackToTheBuildsSpreadingFactor      [FAILED]
BelowThresholdScoresZero                 [FAILED]
ScoreMatchesTheFormulaByHand             [FAILED]
AgreesWithTheOldAssumptionAtSpreadingFactorTen  [PASSED]
LongerPacketsScoreLower                         [PASSED]
============= 8 test cases: 5 failed, 2 succeeded =============
```

Die Datei enthält 7 `TEST`-Makros (`grep -c "^TEST"` = 7); pio nennt 8 "test cases", die Zählung ist nicht geklärt. Die zwei bestandenen Fälle sind erwartet: bei SF10 stimmen alt und neu überein, der Längenabzug hängt nicht vom SF ab. Datei danach wiederhergestellt, `git status` sauber.

### Builds und Delta gegen upstream/dev

7 Envs, je Baseline (`upstream/dev` in eigenem Worktree, gleiche Toolchain) und Fix, alle rc=0. RAM-Delta überall 0.

| Env | Chip | Plattform | Flash vorher | Flash nachher | Delta |
|---|---|---|---|---|---|
| t1000e_repeater | LR1110 | nRF52 | 306744 | 306776 | +32 |
| Waveshare_S3_LR1121_repeater | LR1121 | ESP32-S3 | 1089909 | 1089921 | +12 |
| meshnology_w12_repeater | LR2021 | ESP32-S3 | 1192289 | 1192301 | +12 |
| Heltec_v3_repeater | SX1262 | ESP32-S3 | 1182949 | 1182961 | +12 |
| RAK_4631_repeater | SX1262 | nRF52 | 464040 | 464072 | +32 |
| LilyGo_T3S3_sx1276_repeater | SX1276 | ESP32-S3 | 1110509 | 1110521 | +12 |
| wio-e5_repeater | STM32WLx | STM32 | 199316 | 199316 | 0 |

Die Deltas sind pro Plattform gleich (+12 ESP32, +32 nRF52), auch auf SX1262 und SX1276, wo sich der berechnete Score nicht ändert. Das passt zu einem zusätzlichen virtuellen Aufruf in `packetScore()` der Basisklasse. **Vermutung/ungeprüft**: weder Linker-Map noch Disassembly verglichen. Beim STM32 (Wio-E5) ist das Delta 0.

### Offene PRs und Konflikte (`gh pr list`, `git merge-tree`)

* Suche nach `packetScore`, `getSpreadingFactor`, "packet score": nur #3261 (LR11x0/LR2021 Längenlesefehler), #2739 (LR2021-Variante) und #2025 (Linux-Target); keiner ändert den Score.
* Gegen #3797 (`origin/upstream-candidate/prefs-json-range-check`, `31ee36db`): `merge-tree` ohne Konflikt.
* Gegen #3799 (`origin/upstream-candidate/prefs-extra-sf-disc`, `b9805cc4`): zwei Konflikte, beide eine Zeile gegen eine Zeile:
  * `platformio.ini`: `test_radio_wrappers` gegen `test_node_prefs` in der `test_ignore`-Liste von `native`.
  * `.github/workflows/run-unit-tests.yml`: `-e native_radio_wrappers` gegen `-e native_node_prefs` in der `pio test`-Zeile.
  Lösung beim Rebase des zweiten PR: beide Einträge behalten. Der Env-Block in `platformio.ini` und `test/mocks/Arduino.h` mergen sauber.
* Von den offenen PRs, die `RadioLibWrappers.h` oder die Wrapper ändern, hat nur #3638 (dual radio profiles) einen Bezug: Es konfligiert schon mit `upstream/dev` in `RadioLibWrappers.h/.cpp`, `CustomLR2021*.h` und sechs weiteren Dateien. Die Konfliktliste ist mit und ohne den Fix identisch, der Fix schafft keinen neuen Konflikt. #3638 ändert die Zeilen mit `packetScore` nicht (der Diff enthält `getSpreadingFactor` nur als entfernte Zeile in `onSendFinished`).
* Weitere `merge-tree`-Läufe gegen #3714, #3734, #3563, #3530, #3261 und #2739: keine zusätzlichen Konflikte durch den Fix; die vorhandenen Konflikte von #3734, #3563 und #3261 bestehen gleich gegen `upstream/dev`.

## Ungeprüft

* Hardware: kein Gerät, kein Rx-Test. Der Fork-Commit nennt eine Messung auf einem Seeed T1000-E (LR1110: konfigurierter und gemeldeter SF stimmten überein, Score bei SF7 wie berechnet). Auf diesem Branch nicht wiederholt und im PR-Text nicht behauptet. Dass die LR-Treiber zur Laufzeit den konfigurierten SF melden, ist hier nur durch Lesen des Quelltexts gestützt.
* Die Beispielzahlen für die Verzögerung stammen aus dem Fork-Commit.
* Die Minimal-Alternative ist nicht gebaut, ihre Diffgröße geschätzt.
* Nur 7 Envs gebaut, kein RP2040, kein nRF52 mit LR2021, kein LR1121 auf nRF52. Die Prüfung "Ein Env je Wrapper" ist nicht vollständig (SX1268 und LLCC68 nicht gebaut).
* Ursache der Flash-Deltas nicht im Linker-Map geprüft.
* Commit lokal signiert (Signaturobjekt vorhanden); `git log --show-signature` meldet lokal, dass `gpg.ssh.allowedSignersFile` fehlt, die Prüfung lief also nicht.
* CI von upstream nicht gelaufen.
* Humanizer: Skill geladen und auf den PR-Text von Hand angewendet (Gegenüberstellungen, Schlusszeilen, Dreierreihen, Gedankenstriche entfernt). Kein Lauf als eigener Rewrite-Schritt.

## Commit-Text (Englisch, `2facb916`)

Siehe `git show 2facb916`. Titel: `Score packets at the spreading factor the radio reports`.

## PR-Text-Entwurf (Englisch)

Titel: `Score packets at the spreading factor the radio reports`

````
`RadioLibWrapper::packetScore()` assumes spreading factor 10 for every wrapper that does not override it. LR1110, LR1121 and LR2021 have no override, so their packets are always scored as if the radio ran at SF10, although all three report the real factor through `getSpreadingFactor()`.

`packetScoreInt()` holds one SNR threshold per factor, from -7.5 dB at SF7 to -20 dB at SF12. Below SF10 the score therefore comes out too high on these radios, above SF10 too low.

The base class now calls `getSpreadingFactor()`. The overrides in the SX1262, SX1268, SX1276, LLCC68 and STM32WLx wrappers read the same member that their `getSpreadingFactor()` returns, so they are removed and those radios score exactly as before. A wrapper that reports no factor falls back to `LORA_SF`.

The score feeds the logging and `calcRxDelay()`. All four roles compute that delay from `rx_delay_base`, which is 0 by default. On a default configuration only the logged score changes. With `rxdelay` above 0 the rebroadcast delay of LR boards changes: for a 64 byte packet at 500 ms airtime and rxdelay 10, SF7 at -5 dB goes from score 0.750 to 0.188 (delay 129 ms to 1799 ms), and SF12 at -10 dB from 0.375 to 0.750 (993 ms to 129 ms). At SF10 nothing changes.

If you prefer a smaller diff, the alternative is to leave the base class alone and add the missing `packetScore()` override to the three LR wrappers. I went with the base class change because a default that guesses repeats this bug for the next driver. I can switch if you like.

Tests: new env `native_radio_wrappers` (7 cases) builds the real `RadioLibWrappers.cpp` against a small RadioLib stand-in and sets `LORA_SF=9`, so a score that assumes 10 fails. It is added to `run-unit-tests.yml`. With the base class reverted to `dev`, 5 of the 7 cases fail. The 2 that still pass are the SF10 case and the length penalty, which do not depend on the factor. `pio test -e native -e native_kiss_modem -e native_rak3401_onboard_radio -e native_radio_wrappers`: 121 of 121 pass.

Builds against `dev` (flash delta in bytes, RAM unchanged in all):

| Env | Chip | Flash delta |
|---|---|---|
| t1000e_repeater | LR1110 | +32 |
| Waveshare_S3_LR1121_repeater | LR1121 | +12 |
| meshnology_w12_repeater | LR2021 | +12 |
| Heltec_v3_repeater | SX1262 | +12 |
| RAK_4631_repeater | SX1262 | +32 |
| LilyGo_T3S3_sx1276_repeater | SX1276 | +12 |
| wio-e5_repeater | STM32WLx | 0 |

Not tested on hardware.
````

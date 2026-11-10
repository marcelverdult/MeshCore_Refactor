# Triage der Upstream-Kandidaten (10.11.2026)

Stand: `upstream/dev` = f678f6ac (nach `git fetch upstream`, neuer als der Sync-Stand b8ae75d2).
Nur gelesen: kein Build, kein Test, kein Rebase, nichts gepusht. Alle Aussagen zu "kompiliert" oder "Tests laufen" sind **ungeprüft**.

Merge-Basis aller Branches: 428a95c6 (PR #3606, 17.10.2026). Konfliktprüfung deshalb doppelt:
`git merge-tree --write-tree upstream/dev <branch>` (ganzer Branch) und für den einzelnen Fix-Commit
`git merge-tree --write-tree --merge-base=<commit>^ upstream/dev <commit>` (so würde er als Cherry-Pick landen).

## Wichtige Korrekturen zum Auftrag

* Es gibt doch Marcel-PRs bei meshcore-dev (der Autorenfilter hat sie nicht gefunden, die Suche nach Stichworten schon):
  * **#3646 MERGED**: "Refuse nan and inf for the airtime factor" (deckt Kandidat 7 ab).
  * **#3635 OPEN**: "Fix IdentityStore::save() reporting success when the write failed", Text beschreibt das Zurücklesen nach `close()` (deckt Kandidat 9 ab).
  * #3643 MERGED (kurzer `CMD_SEND_CHANNEL_TXT_MSG`-Frame) ist der Vorläufer von Kandidat 1.
* `gh pr list --author marcelverdult --state all --limit 50` zeigt 14 PRs; keiner davon ist einer der Kandidaten 1 bis 6 oder 8.

## Rangliste (Nutzen für upstream, höchster zuerst)

| # | Kandidat | Status upstream | Konflikte (Fix-Commit auf `upstream/dev`) | Schwere | Empfehlung |
|---|----------|-----------------|--------------------------------------------|---------|------------|
| 1 | `pr/prefs-json-range-check` (bc5708f7) | offen, kein PR, kein Issue | keine (Exit 0) | falsches Verhalten: handeditiertes oder fremd geschriebenes `/prefs.json` umgeht alle Grenzen (z. B. `sf`, `freq`, `tx`) | **einreichen**, aber die Änderung `airtime_factor` 9 auf 99 als eigenen Punkt kenntlich machen oder abtrennen |
| 2 | `pr/prefs-extra-sf-disc` (67b8843d) | offen, kein PR, kein Issue | nur Test-Infrastruktur (`platformio.ini`, `run-unit-tests.yml`); Quellcode sauber | falsches Verhalten: `extra_sf` und `discovery_mod_timestamp` gehen beim Neustart verloren | **einreichen** nach Anpassen der Test-Einträge |
| 3 | `upstream-candidate/lr-packet-score-sf` (26a9bce2) | offen; LR1110/LR1121/LR2021 haben kein eigenes `packetScore()`, Basis nimmt SF10 an | nur Test-Infrastruktur (`platformio.ini`, Workflow) | falsches Verhalten: Paketbewertung für LR11x0/LR2021 immer mit SF10 | **einreichen** nach Anpassen der Test-Einträge |
| 4 | `upstream-candidate/companion-cmd-frame-min-len` (c54cf273) | offen; #3643 war dieselbe Klasse (nur ein Befehl) | keine (Exit 0) | Speicherfehler: Lesen über das Frame-Ende hinaus (veraltete Bytes im statischen Puffer), kein Schreibfehler | **einreichen**; vorher Protokollfrage klären (siehe Abschnitt) |
| 5 | `fix/dispatcher-parse-len` (c0a25f58, nur dieser Commit zählt) | offen, aber drei offene Fremd-PRs am selben Code: #3552, #3521, #1666 | keine (Exit 0) | Speicherfehler: Lesen über `len` hinaus beim Parsen (Lesefehler, Paket wird meist trotzdem abgelehnt) | **erst klären / zusammenlegen**: zuerst prüfen, ob #3552 oder #3521 es schon abdecken |
| 6 | `upstream-candidate/gps-millis-rollover` (09b303cf) | **nicht überholt**: 7 Varianten haben `millis() > next_gps_update` noch; Issue #2878 wurde nur vom Stale-Bot geschlossen; #2937 fixte nur den gemeinsamen Sensor-Manager | 7 triviale Konflikte (eine Zeile pro Variante, wegen #3753) | kosmetisch: bis 1 s veraltete GPS-Position alle 49,7 Tage | **einreichen**, niedrige Priorität, vereinfacht (siehe Abschnitt) |
| 7 | `upstream-candidate/espnow-rxgain-return` (f8866037) | offen, kein PR | keine (Exit 0) | Undefiniertes Verhalten: Funktion mit Rückgabetyp `bool` ohne `return` | **zusammenlegen** mit einem anderen Fix, nur der eine Code-Commit, nicht die Doku |
| 8 | `fix/identity-verify-after-close` (7e27a98d) | **bereits eingereicht** als #3635 (offen) | Quellcode: Konflikt in `IdentityStore.cpp`; Tests: upstream hat Mocks gelöscht | falsches Verhalten (Speicher geht ohne Fehler verloren) | **verwerfen** als eigener Kandidat; #3635 weiterverfolgen |
| 9 | `pr/af-nan-bit-check` (5ce76bd1) | **überholt**: #3646 gemerged (`looksLikeANumber`), Zeile `set af` in upstream schon abgesichert | Konflikt in `CommonRadioPrefs.cpp` | kosmetisch | **verwerfen** |
| 10 | LLCC68 `doResetAGC` | **erledigt**: upstream ruft `sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode())` mit zwei Argumenten | kein Branch | war: Compilerfehler, kein Env baut den Wrapper | **verwerfen**, nichts zu tun |

Reihenfolge-Begründung in einem Satz: 1 bis 3 beheben Verhalten, das ein Nutzer oder ein Gerät sieht, bei sauberem Diff; 4 und 5 sind Härtung ohne sichtbaren Fehler; 6 und 7 sind fast kosmetisch; 8 bis 10 sind erledigt.

## Gemeinsame Hinweise

* Konflikte der Kandidaten 2, 3, 8, 9 sitzen in `platformio.ini` und `.github/workflows/run-unit-tests.yml` (neue Test-Envs, die die Fork-Basis hinzufügt). Bei einem Einreichen müssen die Test-Einträge auf den upstream-Stand neu gesetzt werden. Die Test-Mocks (`test/mocks/...`) liegen im Fork auf einem anderen Stand als upstream (z. B. `Adafruit_LittleFS.h` und `test_identity_store` sind upstream gelöscht). **Ungeprüft:** ob die mitgelieferten Tests der Kandidaten 2, 3, 5 auf upstream überhaupt laufen.
* Die Branches von Kandidat 3, 5 und 7 enthalten Notizen unter `docs/fixes/` und `tools/refactoring/*.patch`. Das gehört nicht in einen upstream-PR.
* `fix/dispatcher-parse-len` ist kein einzelner Fix: der Branch trägt die ganze Refactor-Serie (20 Commits laut `git cherry`, 339 Dateien). Nur c0a25f58 (plus optional die Tests 783e89d2, d748d432) zählt.
* Merge-tree sagt nur "Textkonflikt ja/nein", nicht "kompiliert". **Ungeprüft** bei allen.

## 1. `pr/prefs-json-range-check` (bc5708f7)

Befehle und Output:

```
git merge-tree --write-tree --merge-base=bc5708f7^ upstream/dev bc5708f7   -> Exit 0
git merge-tree --write-tree upstream/dev pr/prefs-json-range-check          -> Exit 0
git show upstream/dev:src/helpers/CommonCLI.cpp | grep -n "sanitise\|loadSerial"
  38:      _prefs->loadSerial(file);   // new Serial prefs
  107:    // sanitise bad pref values      (nur im Legacy-Loader loadPrefsInt)
  111:    _prefs->airtime_factor = constrain(_prefs->airtime_factor, 0, 9.0f);
gh pr list -R meshcore-dev/MeshCore --state all --search "prefs.json sanitise" / "ConfigSerializer" / "loadSerial prefs"
  kein PR zum Thema (verwandt: #3322 offen "widen ConfigSerializer rd_len", #3415 gemerged "digits in config keys")
```

* Upstream: `loadPrefs()` liest `/prefs.json` ohne Grenzprüfung, nur der alte Loader klemmt. Fix nicht enthalten.
* Diff: `CommonCLI.cpp` (+80/-34, Klemmcode in `sanitisePrefs()` ausgelagert und von beiden Loadern genutzt), `CommonCLI.h` (+1), `ConfigSerializer.cpp` (+33: NaN/Inf-Token wird per Bitprüfung verworfen), Test `test_config_serializer.cpp` (+69). Insgesamt 149 Zeilen, Tests dabei.
* Schwere: falsches Verhalten, keine Abstürze. Ein Knoten kann mit einer ungültigen Funkkonfiguration starten.
* Achtung: die Obergrenze für `airtime_factor` steigt von 9 auf 99 (der alte Wert passt nicht zu `set dutycycle 1`). Das ist eine inhaltliche Änderung im selben Diff und sollte im PR-Text eigens stehen oder als eigener Commit.

## 2. `pr/prefs-extra-sf-disc` (67b8843d)

```
git merge-tree --write-tree --merge-base=67b8843d^ upstream/dev 67b8843d
  CONFLICT .github/workflows/run-unit-tests.yml, platformio.ini   -> Exit 1 (nur diese zwei Dateien)
git grep -n "extra_sf" upstream/dev
  CommonCLI.cpp:646 (set), :785 (get), CommonCLI.h:78 (Feld)   -> kein def() in RadioPrefs::structure()
git grep -n "discovery_mod_timestamp" upstream/dev -- src
  CommonCLI.cpp:97 (Legacy-Loader), :873 (setzen), CommonCLI.h:69   -> kein def() in der Haupt-structure()
gh pr list ... "extra_sf prefs" / "discovery_mod_timestamp"  -> nur #3086 (agessaman, Region-Discovery, offen, anderes Thema)
```

* Upstream speichert `extra_sf` und `discovery_mod_timestamp` nicht in `/prefs.json`. Fix nicht enthalten.
* Diff Quellcode: 3 Zeilen `def(...)` plus `memset(extra_sf, 0, ...)` im Konstruktor (`extra_sf[4]` hat upstream keinen Initialisierer). Tests: 141 Zeilen, plus Mocks (331 Zeilen insgesamt, 7 Dateien).
* Schwere: falsches Verhalten (Einstellung geht beim Neustart verloren). Zusammenspiel mit #3086 (liest `discovery_mod_timestamp`) ist **ungeprüft**.
* **Ungeprüft:** ob `def(const char*, void*, size_t)` für Byte-Arrays auf `upstream/dev` existiert (merge-tree meldet nur Textfreiheit im Quellcode).

## 3. `upstream-candidate/lr-packet-score-sf` (26a9bce2, Notiz 49ed34be)

```
git merge-tree --write-tree --merge-base=26a9bce2^ upstream/dev 26a9bce2   -> Exit 1, nur platformio.ini + run-unit-tests.yml
git show upstream/dev:src/helpers/radiolib/RadioLibWrappers.h | grep -n packetScore
  80: float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, 10, packet_len); }  // assume sf=10
git grep -n packetScore upstream/dev -- src/helpers/radiolib
  SX1262/SX1268/SX1276/LLCC68/STM32WLx: eigene Überschreibung mit echtem SF; LR1110/LR1121/LR2021: keine
gh pr list ... "packetScore" / "getSpreadingFactor"  -> nichts Passendes (#3261, #2739 betreffen andere Dinge)
```

* Upstream: LR1110, LR1121 und LR2021 liefern `getSpreadingFactor()`, aber `packetScore()` ignoriert es und rechnet mit SF10. Fix nicht enthalten.
* Diff Quellcode: Basisklasse ruft `getSpreadingFactor()`, fünf identische Überschreibungen entfallen (-23 Zeilen, +6). Tests: `test_radio_wrappers` (148 Zeilen) plus Mocks. Binäre auf Boards ändern sich um wenige Bytes (laut Notiz des Forks, nicht nachgemessen).
* Schwere: falsches Verhalten (Bewertung für Routing/Empfangsverzögerung bei SF≠10 auf LR-Chips), kein Absturz.
* Das Entfernen der fünf Überschreibungen vergrößert den Diff über die Fehlerstelle hinaus. Alternative wäre nur das Hinzufügen der drei fehlenden Überschreibungen. **Zu entscheiden**, was die Maintainer lieber lesen.

## 4. `upstream-candidate/companion-cmd-frame-min-len` (c54cf273)

```
git merge-tree --write-tree upstream/dev upstream-candidate/companion-cmd-frame-min-len   -> Exit 0
git show upstream/dev:examples/companion_radio/MyMesh.cpp | grep -nE "CMD_REMOVE_CONTACT|CMD_SHARE_CONTACT|CMD_GET_CONTACT_BY_KEY|CMD_SET_RADIO_PARAMS|CMD_SET_RADIO_TX_POWER|CMD_SET_TUNING_PARAMS|CMD_SET_OTHER_PARAMS|CMD_REBOOT"
  1353/1363/1375/1428/1464/1474/1492/1516: alle ohne Längenprüfung
gh pr list ... "cmd_frame length" / "CMD_REMOVE_CONTACT" / "CMD_SET_RADIO_PARAMS"
  #3386 gemerged (Payloadgröße CMD_SEND_CHANNEL_DATA), #3643 gemerged (CHANNEL_TXT_MSG), #3584 offen (anderer Fix an CMD_SET_RADIO_PARAMS)
```

* Upstream: die 14 Befehle haben weiter keine Mindestlänge (nur ADD_UPDATE_CONTACT mit zu kleinem Minimum, `len >= 1 + 32 + 2 + 1`).
* Diff: 35 Zeilen in einer Datei, **keine Tests im Branch** (Tests liegen im Fork auf `fix/dispatcher-parse-len`, `test_companion_frames`).
* Schwere: Speicherfehler (Lesen alter Puffer-Bytes), also falsch verarbeitete Befehle statt Absturz. Bei CMD_REBOOT mit kurzem Frame können alte Bytes "reboot" ergeben haben: **Vermutung/ungeprüft**.
* Punkt zum Klären: `CMD_ADD_UPDATE_CONTACT` verlangt jetzt `1 + 32 + 3 + 64 + 32 + 4 = 136` Bytes. Falls Apps (oder alte Firmware-Protokolle) kürzere Frames senden, wären sie danach abgelehnt. In der Protokolldokumentation und in einer Beispiel-App gegenprüfen. **Ungeprüft.**
* Konflikte mit #3584 (agessaman) an `CMD_SET_RADIO_PARAMS`: merge-tree zeigt keinen Konflikt, die Änderung dort ist aber nicht gelesen. **Ungeprüft.**

## 5. `fix/dispatcher-parse-len` (c0a25f58)

```
git cherry upstream/dev fix/dispatcher-parse-len | wc -l      -> 20 (ganze Refactor-Serie, 339 Dateien)
git merge-tree --write-tree --merge-base=c0a25f58^ upstream/dev c0a25f58   -> Exit 0
git show upstream/dev:src/Dispatcher.cpp  (tryParsePacket): liest raw[0], dann raw[1..4], path_len, ohne Test von len vorher
git show upstream/dev:src/Packet.cpp      (readFrom): kopiert den Pfad vor dem Test "i >= len"
gh pr list ... "Packet readFrom len":
  #3552 OPEN kim7tin  "bounds-check input length in Packet::readFrom()"   (Packet.cpp + Test)
  #3521 OPEN neilalexander "Protocol parser vulnerability fixes"           (u. a. Dispatcher.cpp, Packet.cpp, Tests)
  #1666 OPEN weebl2000 "Validate buffer length before reading fields in Packet::readFrom"
```

* Upstream: nicht behoben, aber drei offene PRs anderer Autoren am selben Code. #3521 (31.10.) fasst Dispatcher.cpp und Packet.cpp an.
* Diff des Fixes: 10 Zeilen in zwei Dateien; Tests 783e89d2 (402 Zeilen, `test_packet_parse`) und d748d432 getrennt.
* Schwere: Lesen jenseits von `len`; `payload_len` (uint8) läuft bei negativem `len - i` über und wird dann durch `> sizeof(payload)` abgelehnt, also meist nur Lesefehler. Schreibfehler nicht gefunden. **Nur aus dem Lesen des Codes, nicht gemessen.**
* Empfehlung: vor dem Einreichen #3521 und #3552 im Detail lesen. Wahrscheinlich zusammenlegen: einen Review oder Test-Beitrag zu #3521 statt eines vierten PRs.

## 6. `upstream-candidate/gps-millis-rollover` (09b303cf, Test bd1fcf77)

```
git grep -ln "millis() > next_gps_update" upstream/dev -- variants src examples
  heltec_mesh_solar, heltec_tracker, meshadventurer, meshtracker_x1, nano_g2_ultra, t1000-e, thinknode_m1   (7 Dateien, alle noch betroffen)
git grep -n "(long)(millis() - next_gps_update)" upstream/dev -- src/helpers/sensors/EnvironmentSensorManager.cpp
  744-746: wrap-sicher (von PR #2937 gemerged)
git merge-tree --write-tree --merge-base=09b303cf^ upstream/dev 09b303cf
  CONFLICT in allen 7 target.cpp  -> Exit 1; Beispiel t1000-e: nur die if-Zeile daneben (#3753 hat isGpsOn()/hasValidFix() eingeführt)
gh issue view 2878 -> CLOSED 26.10. durch Stale-Bot, nicht durch Fix
```

* **Nicht überholt**: #3753 hat die 7 Varianten nicht angefasst, was die Schleife betrifft (Zeile `millis() > next_gps_update` bleibt).
* Diff: neue Datei `src/helpers/MillisDeadline.h` (40 Zeilen) plus Test (121 Zeilen) plus 5 Zeilen pro Variante.
* Schwere: kosmetisch. Bei einem Zählerüberlauf (alle 49,7 Tage) bleibt `node_lat/lon` höchstens eine Sekunde alt; das vorzeichenbehaftete `long` wirkt wegen der Umwandlung nach unsigned praktisch nicht. Der Umbau nach #3753 (`GpsDriver`) macht das Ganze eher unwichtiger.
* Empfehlung: nicht den neuen Header einreichen, sondern in den 7 Varianten das in `EnvironmentSensorManager.cpp` schon vorhandene Muster `(long)(millis() - next) > 0` verwenden. Das ist kleiner und folgt dem Stil von #2937.

## 7. `upstream-candidate/espnow-rxgain-return` (f8866037, Notiz 173dc322)

```
git merge-tree --write-tree upstream/dev upstream-candidate/espnow-rxgain-return   -> Exit 0
git show upstream/dev:src/helpers/esp32/ESPNOWRadio.h | grep -n setRxBoostedGainMode
  41: virtual bool setRxBoostedGainMode(bool) { }
gh pr list ... "ESPNOWRadio setRxBoostedGainMode" -> #2190 (gemerged, andere Methoden), #2061 (offen, anderes Thema)
```

* Upstream: unverändert, Funktion ohne `return`. Fix nicht enthalten.
* Diff des Fixes: eine Zeile (`{ return false; }`). Der Branch bringt außerdem 260 Zeilen Notiz und einen Diag-Patch, die nicht nach upstream gehören.
* Schwere: undefiniertes Verhalten laut C++; ob eine Build-Kette das wirklich als Absturz zeigt, ist **ungeprüft**.
* Eine einzelne Zeile ist als eigener PR zu klein; mit Kandidat 3 (Rückgabewert- und Wrapper-Thema) oder 6 bündeln.

## 8. `fix/identity-verify-after-close` (e281f0ea + 7e27a98d)

```
gh pr view 3635 -R meshcore-dev/MeshCore --json files
  src/Identity.cpp, src/Identity.h, src/helpers/IdentityStore.cpp, src/helpers/IdentityStore.h   (OPEN, Marcel)
```

* Der Text von #3635 beschreibt das Zurücklesen nach `close()`, den `const`-Umbau von `LocalIdentity::writeTo` und die gleichen vier Dateien wie der Branch. Es ist derselbe Inhalt (Textvergleich des Diffs gegen den PR **ungeprüft**).
* Gegen `upstream/dev` konfligiert der Branch in `IdentityStore.cpp` und bei den Test-Mocks; der PR #3635 steht auf einem älteren Stand und müsste ggf. aktualisiert werden. Mergebarkeit von #3635: **ungeprüft**.
* Empfehlung: verwerfen, nur #3635 verfolgen.

## 9. `pr/af-nan-bit-check` (5ce76bd1)

```
git show upstream/dev:src/helpers/CommonRadioPrefs.cpp  Zeilen 14-18, 88, 106
  looksLikeANumber(): lehnt "nan"/"inf" ab (Zeichen nach Vorzeichen muss Ziffer oder '.' sein)
git log upstream/dev -S looksLikeANumber -- src  -> 83bd824f (Marcel, #3646, gemerged)
```

* `set af nan` und `set dutycycle nan` sind upstream schon abgelehnt. Die Bitprüfung gegen `-Ofast` ist Zusatzschutz, den die Textprüfung überflüssig macht (kein Float-Vergleich nötig).
* Offen bleibt nur das `/prefs.json`-Pfad-Argument; dort greift Kandidat 1 mit seiner eigenen Bitprüfung in `ConfigSerializer`.
* Empfehlung: verwerfen.

## 10. LLCC68 `doResetAGC`

```
git show upstream/dev:src/helpers/radiolib/CustomLLCC68Wrapper.h | grep -n -i agc
  43: void doResetAGC() override { sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode()); }
git log -1 --format='%h %an %ad %s' a94f510f
  a94f510f agessaman 2026-09-23 radiolib: reapply runtime RX boosted gain on SX126x AGC reset
```

* Herkunft: `docs/refactoring/architecture-review-v1.18.md` Zeile 144 (Punkt 2) und KB `meshcore/firmware/meshcore-refactor-fixes-f1-f2.md` (Fix 4, "eine Zeile").
* Upstream hat das Problem schon durch a94f510f (Fremd-Commit) behoben; Fix 4 des Forks ist damit gegenstandslos.
* Empfehlung: verwerfen, den Punkt im Review-Dokument als "upstream erledigt" markieren (nicht Teil dieser Triage).

## Offene Punkte

* Test-Läufe fehlen für alle Kandidaten (Hinweis: kein Build gemacht).
* Kandidat 4: Protokollprüfung für die neue Mindestlänge von `CMD_ADD_UPDATE_CONTACT`.
* Kandidat 5: Vergleich mit #3521 und #3552 im Detail.
* Kandidat 1: Entscheidung über die Trennung der `airtime_factor`-Grenze.
* Kandidat 3: Entscheidung "Basisklasse ändern" gegen "drei Überschreibungen ergänzen".
* #3635: Stand und Mergebarkeit gegen `upstream/dev` nicht geprüft.

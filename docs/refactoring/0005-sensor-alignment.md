# 0005: Align the sensor, part one: multi.acks and RX boosted gain

Status: implemented. The third part of the alignment, the flood hop limits, is
parked on `refactor/sensor-flood-limits` and waits for the `/prefs.json` range
checking; the reason is in "What is deliberately held back" below.

This is a change of behaviour, not a move, so it is a branch of its own with a note
of its own.

## Context

The sensor sketch was copied from the repeater in July 2025 and then missed three
changes that reached the repeater and the room server. The commit by commit history
is in `architecture-review-v1.18.md` under finding F2. None of the deviations has a
role specific reason such as battery life: each one is a change the sensor was left
out of.

Marcel's decision of 19 October is to align the sensor, with the retransmit delay
excepted and the flood limits deferred.

## What changes

- **`multi.acks` takes effect.** The sensor's `getExtraAckTransmitCount()` override
  returned 0 with a comment saying it had never sent extra ACKs. Deleting it lets
  the core report the stored setting, as it does for the other roles since step 2.
  `SensorMesh::sendAckTo()` already had a branch guarded by that count, so the
  branch stops being dead code.
- **RX boosted gain is applied at boot.** The sensor initialises the pref under the
  same `USE_SX1262 || USE_SX1268` and `SX126X_RX_BOOSTED_GAIN` guards as the other
  roles, and `begin()` pushes the stored value to the radio. Before, a value set
  over the CLI took effect at once and was gone after a restart. This mirrors
  upstream's own fix for the room server, `a71cc238`.

Neither change can alter what a node that has not been reconfigured does: an extra
ACK is only sent when `multi.acks` is on, which is off by default, and the gain
value pushed at boot is whatever was already stored.

## What deliberately does not change

The retransmit delay. The sensor draws from six discrete multiples of the airtime
while the other roles draw uniformly from a continuous range. Upstream moved them
in `8cbcd227`, whose subject calls itself experimental, and has neither revisited
nor confirmed it since. Aligning the sensor would adopt that experiment and change
radio timing on battery nodes, so the two overrides stay and the formula becomes the
one role parameter of the shared base. Pinned by a test, so it cannot be lost by
accident.

## Not added on purpose

`setRxBoostedGain()`, which upstream's room server fix also added. The virtual has a
default in `helpers/CommonCLI.h` and overrides in the repeater and the room server,
and **nothing calls it**: the only `radio.rxgain` command handler
(`helpers/CommonRadioPrefs.cpp`) reaches the radio driver directly, and the settings
table lists the field as `CLI_NONE`, which the generic loop skips. Adding it to the
sensor would add dead code. The two existing overrides are vestigial and are left
for a separate clean-up (ledger issue 11).

## What is deliberately held back

The flood hop limits. `allowPacketForward()` still checks `flood_max` only, so
`flood.max.unscoped` and `flood.max.advert` remain without effect on the sensor.
The work exists on `refactor/sensor-flood-limits`, with its tests, and is not merged
because of an upgrade hazard that was measured rather than assumed:

1. A hop limit of zero is written to `prefs.json` as an explicit `0`, not left
   empty.
2. Loading such a file applies the 0, because `/prefs.json` is read through
   `loadSerial()`, which has no range checking (Fix 9 of the F4 session).
3. The limits are compared with `>=`, so 0 means "never forward". A node booting on
   such a file would refuse every un-scoped flood and every advert at hop 0.

The sensor set no default for either limit, so every sensor that has ever saved its
settings carries `f_max_uns:0, f_max_adv:0`. Giving the constructor the values the
other roles use is not enough, because the stored zero beats the default.

How far it reaches: forwarding is off by default on the sensor
(`_prefs.disable_fwd = true` in its constructor, as on the room server; only the
repeater forwards out of the box). The hazard therefore only affects sensors where
an operator switched forwarding on, and those are exactly the ones where it matters.

Marcel's decision of 20 October: wait for Fix 9. Once `/prefs.json` is range
checked, the cause is gone and the flood limits can be aligned without a migration
of their own. Until then only the two changes above land.

A separate observation, worth its own issue either way (ledger issue 12): because a
0 is stored and applied verbatim, `set flood.max.unscoped 0` is persistable on every
role and the constructor default does not protect against it. That is the setting
upstream's own comment describes repeaters as running.

## Verification

The sensor role had no host environment before this. `native_sensor` and
`test/test_sensor_role` were added first, and the behaviour was recorded there
before anything changed (see the preceding commit); the two tests for these changes
now carry the new expectations, so the diff of the test file is the record of what
changed. `OnlyFloodMaxIsAppliedToday` still passes, which is the evidence that the
flood behaviour is untouched on this branch.

- `pio test` over all six environments: 123 of 123 test cases pass. (The branch
  that also carries the flood limits has 127; the four extra cases are its own.)
- Hardware: **not done.** `techo-b43b` is the sensor board and is not to be claimed
  until the F1 to F4 fixes are merged. What it should cover for this change: that a
  stored `radio.rxgain` really survives a reboot on an SX1262 board, which the host
  environment cannot show because it defines neither `USE_SX1262` nor
  `SX126X_RX_BOOSTED_GAIN`.

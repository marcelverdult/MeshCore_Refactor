# 0005: Align the sensor, part one: multi.acks and RX boosted gain

Status: implemented. The third part of the alignment, the flood hop limits, is
[0006](0006-sensor-flood-limits.md); why it is a change of its own is in "What is
deliberately held back" below.

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

Marcel's decision of 20 October was first to wait for Fix 9, on the assumption that
range checking `/prefs.json` would remove the cause. It does not: 0 is a legal value
for these limits, so Fix 9 does not clamp them. The second decision, the same day, is
to lift a stored 0 once on upgrade, behind a version in `/prefs.json`. That is
[0006](0006-sensor-flood-limits.md).

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
- Hardware: done on `techo-b43b` (LilyGo T-Echo, SX1262, sensor role), with marked
  images of the merge into `integration` (`ae0b9f22`, "new") and of its first parent
  (`cc1e6c3b`, "base"), both built with
  `tools/refactoring/diag-sensor-rxgain-acks.patch`. `get radio.rxgain` only reads the
  stored pref, so the patch adds `diag sensor`, which prints the pref, the radio's RX
  gain register (`CustomSX1262::getRxBoostedGainMode()`) and
  `getExtraAckTransmitCount()`. The T-Echo turns boosted gain on during radio init
  (`SX126X_RX_BOOSTED_GAIN=1`), so only a stored `off` shows the difference. Values
  below are pref / register / extra ACKs.
  1. Original 1.17.1 to base: all 30 settings identical, `1 / 1 / 0`.
  2. Base, `set radio.rxgain off` and `set multi.acks 1`: `0 / 0 / 0` at once, and
     `0 / 1 / 0` after a reboot. The stored gain is lost at boot and `multi.acks` has
     no effect: the two defects this change removes.
  3. Base to new: all settings identical, `0 / 0 / 1`.
  4. New, `set radio.rxgain on` and reboot: `1 / 1`. Then `set radio.rxgain off`,
     `set multi.acks 0` and reboot: `0 / 0 / 0`.
  5. New to base (downgrade): all settings identical, `0 / 1 / 0`, the old boot
     behaviour again.
  6. Back to the original 1.17.1 with the original values: 31 of 31 lines identical to
     the reading taken before the check.

  Two findings on the way, neither caused by this change (ledger issues 13 and 14):
  `set multi.acks 2` is accepted, but the range check after loading `/prefs.json`
  clamps the field to 0..1, so it reads back as 1 after a reboot. And a soft reset
  sometimes leaves this board's I2C bus with SDA held low, after which every role
  blocks in the RTC probe before any radio or mesh code runs; the diagnostic patch
  frees the bus before `board.begin()` so the check could run at all.

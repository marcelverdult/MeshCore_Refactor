# Hardware checks for the refactoring series

`nodetool.py` drives the upgrade/downgrade check described in each step's design
note (`docs/refactoring/`): flash a firmware, change settings through the CLI,
reboot, read them back, flash the other firmware, compare. Boards are addressed by
their USB serial number, so the check cannot hit the wrong board when several are
attached. macOS only (it finds ports through `ioreg`); Python 3 standard library
only.

```sh
python3 nodetool.py port <usb-serial>                       # serial port of the board
python3 nodetool.py cli <usb-serial> @scripts/server_gets.txt   # repeater / room server / sensor console
python3 nodetool.py comp <usb-serial> @scripts/comp_gets.txt    # companion: CMD_RUN_CLI_COMMAND per line
python3 nodetool.py compinfo <usb-serial>                   # companion: self info and tuning parameters
python3 nodetool.py dfu <usb-serial> firmware.zip           # 1200 baud touch, then adafruit-nrfutil serial DFU
```

`@file` stands for the lines of that file. `reboot` as a command reboots the board
(companion: `CMD_REBOOT`).

## Telling the images apart

All builds of one release report the same version and build date, so `ver` cannot
show which image is running. Build the images for the check with a marker:

```sh
PLATFORMIO_BUILD_FLAGS='-D FIRMWARE_BUILD_DATE=\"step1-<commit>\"' pio run -e LilyGo_T-Echo_room_server
```

`ver` then answers `v1.18.0 (Build: step1-<commit>)`. Setting
`PLATFORMIO_BUILD_FLAGS` makes PlatformIO clean the whole build folder of the
project, so build the marked images in a separate worktree.

## Companion frame sweep (step 10)

`companion_sweep.py` sends the frames of the host frame sweep
(`test/test_companion_frames/frame_sweep.golden`, the all zero pass) to a USB
companion and checks that the board answers as the host harness does: every
frame the host refuses, every length of the commands that only report, and four
lengths of every code that is not a command. Commands that would store, transmit,
restart or format are never sent at an accepted length; the docstring lists them.
About 6,800 frames, 12 minutes on a T-Echo.

```sh
python3 companion_sweep.py <usb-serial>
```

## Diagnostic build for step 2

`diag-tuning.patch` adds a `diag tuning` command to the room server and the
companion that prints what the running core uses for its tuning hooks (duty cycle
budget, RX delay at score 0.3 and 1000 ms airtime, interference threshold, CAD,
AGC reset interval, extra ACKs). Apply the same patch to `dev` and to the step,
build both with a marker, and compare the output for the same settings:

```sh
git apply tools/refactoring/diag-tuning.patch
```

The patch is for testing only and is not part of any step.

## Diagnostic build for the sensor alignment

`diag-sensor-rxgain-acks.patch` adds `diag sensor` to the sensor role. It prints the
stored RX boosted gain, the gain the radio really runs with (read back from the
SX126x register) and the extra ACK count the core uses, because `get radio.rxgain`
only shows the stored value. It also makes two boot changes that only serve the
check: `setup()` waits up to 15 s for the host to open the serial port, so the boot
output is not lost, and it frees an I2C bus that a soft reset left with SDA held low
(see ledger issue 14), since the board otherwise hangs in the RTC probe. The T-Echo
has no sensor environment in the tree. The check used one like this, appended to
`variants/lilygo_techo/platformio.ini`; the board's own build also sets its admin
password and `DEFAULT_FLOOD_SCOPE_NAME`, which only take effect on a board without
stored prefs:

```ini
[env:LilyGo_T-Echo_sensor]
extends = LilyGo_T-Echo
build_src_filter = ${LilyGo_T-Echo.build_src_filter}
  +<../examples/simple_sensor>
build_flags =
  ${LilyGo_T-Echo.build_flags}
  -D ADVERT_NAME='"T-Echo Sensor"'
  -D ADVERT_LAT=0.0
  -D ADVERT_LON=0.0
  -D ADMIN_PASSWORD='"password"'
```

The patch is for testing only and is not part of any step.

## Restoring a board

Since v1.17 every role reads `/prefs.json`. Flashing the old firmware back keeps
the values set during the check, so set them back to the original values first
(read them before the check with the `get` scripts).

If `~/.platformio/penv` points at a Python that no longer exists, set
`NRFUTIL_PYTHON` to another interpreter, for example the one PlatformIO itself
runs with.

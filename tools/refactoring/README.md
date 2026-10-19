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

## Restoring a board

Since v1.17 every role reads `/prefs.json`. Flashing the old firmware back keeps
the values set during the check, so set them back to the original values first
(read them before the check with the `get` scripts).

If `~/.platformio/penv` points at a Python that no longer exists, set
`NRFUTIL_PYTHON` to another interpreter, for example the one PlatformIO itself
runs with.

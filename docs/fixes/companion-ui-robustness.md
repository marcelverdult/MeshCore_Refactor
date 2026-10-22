# A node without a display faults in the ui-tiny status bar

Status: fixed. NOT verified at runtime, see the limits at the end. Behaviour
change: a companion with no working display starts instead of faulting.

## Symptom

`UITask::begin()` in `ui-tiny` dereferences `_display` one line below the guard
that was written for exactly that pointer
(`examples/companion_radio/ui-tiny/UITask.cpp:447-450`):

```c
  if (_display != NULL) {
    _display->turnOn();
  }
  _statusBar.begin(_display->width());
```

`main.cpp` sets `disp` to NULL and only assigns it when the driver starts
(`examples/companion_radio/main.cpp:143-146`), then passes it in unchanged
(`main.cpp:285`). So on a node whose panel does not answer, `_display` is NULL
and `setup()` faults in the status bar call. The node never joins the mesh at
all, and a user cannot tell a dead display apart from a dead node.

`ui-orig` has a second, much smaller fault in the same function: the `strdup` of
`FIRMWARE_VERSION` at `ui-orig/UITask.cpp:55` is never freed. `begin()` runs once
per boot, so nothing grows; it is a handful of bytes that stay allocated.

## Reachability

The display fault needs `display.begin()` to return false. Measured, not assumed:

- `U8g2Display::begin()` returns what `_u8g2.begin()` returns
  (`src/helpers/ui/U8g2Display.h:45-57`), so an unpopulated, dead or
  wrongly addressed I2C panel is enough.
- `variants/lilygo_techo_card/platformio.ini:20` sets
  `DISPLAY_CLASS=U8g2Display`, and that variant is the only one that compiles
  `ui-tiny`.
- `NullDisplayDriver::begin()` always returns false
  (`src/helpers/ui/NullDisplayDriver.h:8`) and 16 variants use it, which would be
  a second path into the same fault. None of those 16 compiles `ui-tiny`, so that
  path does not reach this line. `ui-orig`, which several of them do compile, has
  the guard right.

So the fault is reachable on a LilyGo T-Echo Card whose panel fails to
initialise, and nowhere else today. What makes it an oversight rather than a
decision is that every other `_display` use in the same file tests the pointer
first: lines 517, 532, 699, 746, 760, 775 and 783.

## Fix

The status bar call moves inside the guard that is already there. Nothing is lost
when there is no display:

- The status bar is only ever drawn through `*_display`, inside
  `if (_display != NULL && _display->isOn())` in `loop()`.
- `ScrollingStatusBar`'s constructor initialises every member, including
  `_display_width(72)` (`ui-tiny/ScrollingStatusBar.h:35`), so the object stays in
  a defined state when `begin()` is not called. Nothing reads uninitialised
  memory in the headless case.

Checked by walking the block depth of all four `_statusBar` uses rather than by
eye: each one now sits under an `if (_display != NULL ...)`.

For `ui-orig`, the `strdup` becomes a local array sized to the field it feeds.
The copy only has to outlive the `sprintf` two lines later. `StrHelper::strzcpy`
bounds it, which also removes the chance of a long `FIRMWARE_VERSION` walking
past `_version_info[32]`.

### Alternatives considered

- **Use `NullDisplayDriver` for the headless case** instead of a NULL pointer,
  and drop the guards. That is the shape the codebase is reaching for; the driver
  exists and already returns false from `begin()`. Rejected here: it changes
  `main.cpp` and every UI task for all 222 companion environments, which is a
  refactor and not a fix, and it would hide this defect rather than close it.
  Worth proposing on its own.
- **Guard inside `ScrollingStatusBar::begin()`** by passing the pointer instead
  of the width. Rejected: it moves the NULL question into a class that otherwise
  knows nothing about the display, and the other three call sites would still
  need the guard.
- **Free the `strdup`** instead of removing it. Rejected: there is no reason to
  allocate at all for a copy that lives for two lines.

## Verification

### Builds

All 26 environments that compile `ui-tiny` or `ui-orig` were built from `dev`
(428a95c6) and from this branch with the same toolchain. That is every
environment that contains the changed code, not a sample: measured with
`pio project config --json-output`, the two files are compiled by 18
`nordicnrf52` environments, 7 `platformio/espressif32@6.11.0` and 1
`ststm32@19.7.1`, and all 26 are in the list below.

All 26 build on both sides, and RAM is unchanged everywhere.

| Environment | Flash `dev` | Flash fix | Flash delta | RAM delta |
|---|---:|---:|---:|---:|
| `LilyGo_T-Echo_Card_companion_radio_ble` | 347,840 | 347,840 | +0 | +0 |
| `LilyGo_T-Echo_Card_companion_radio_usb` | 342,096 | 342,096 | +0 | +0 |
| `MeshTracker_X1_companion_radio_ble` | 323,004 | 322,940 | -64 | +0 |
| `MeshTracker_X1_companion_radio_usb` | 317,324 | 317,276 | -48 | +0 |
| `Minewsemi_me25ls01_companion_radio_ble` | 317,516 | 317,452 | -64 | +0 |
| `Minewsemi_me25ls01_companion_radio_usb` | 311,804 | 311,756 | -48 | +0 |
| `R1Neo_companion_radio_ble` | 383,688 | 383,640 | -48 | +0 |
| `R1Neo_companion_radio_usb` | 376,280 | 376,232 | -48 | +0 |
| `RAK_3112_companion_radio_ble` | 1,649,285 | 1,649,285 | +0 | +0 |
| `RAK_3112_companion_radio_usb` | 638,353 | 638,353 | +0 | +0 |
| `RAK_3112_companion_radio_wifi` | 1,087,681 | 1,087,681 | +0 | +0 |
| `RAK_WisMesh_Tag_companion_radio_ble` | 383,992 | 383,928 | -64 | +0 |
| `RAK_WisMesh_Tag_companion_radio_usb` | 376,568 | 376,520 | -48 | +0 |
| `ThinkNode_M3_companion_radio_ble` | 318,048 | 318,000 | -48 | +0 |
| `ThinkNode_M3_companion_radio_usb` | 312,344 | 312,296 | -48 | +0 |
| `ThinkNode_M4_companion_radio_ble` | 318,416 | 318,368 | -48 | +0 |
| `ThinkNode_M4_companion_radio_usb` | 312,712 | 312,648 | -64 | +0 |
| `ThinkNode_M7_companion_radio_ble` | 1,624,645 | 1,624,649 | +4 | +0 |
| `ThinkNode_M7_companion_radio_ethernet` | 1,057,533 | 1,057,537 | +4 | +0 |
| `ThinkNode_M7_companion_radio_usb` | 584,381 | 584,381 | +0 | +0 |
| `ThinkNode_M7_companion_radio_wifi` | 1,023,861 | 1,023,861 | +0 | +0 |
| `Xiao_nrf52_companion_radio_ble` | 378,152 | 378,088 | -64 | +0 |
| `Xiao_nrf52_companion_radio_usb` | 372,440 | 372,392 | -48 | +0 |
| `t1000e_companion_radio_ble` | 309,180 | 309,116 | -64 | +0 |
| `t1000e_companion_radio_usb` | 303,508 | 303,460 | -48 | +0 |
| `wio-e5-mini_companion_radio_usb` | 208,024 | 207,976 | -48 | +0 |

The sizes split by platform, and the split was chased down rather than waved
away. The 18 `nordicnrf52` environments lose 48 to 64 bytes, the ESP32 ones are
unchanged, and two of them gain 4 bytes. That is not image padding: comparing the
ELF sections instead of the padded image shows `.text` identical on both sides for
`RAK_3112_companion_radio_ble` (1,318,998) and genuinely 4 bytes larger for
`ThinkNode_M7_companion_radio_ble` (1,297,174 to 1,297,178).

`nm` explains it. On `t1000e_companion_radio_ble` the `strdup` symbol is present
on `dev` and gone on this branch: this call site was its only user, so the linker
drops the function and its dependencies. On `RAK_3112_companion_radio_ble` the
symbol is present on both sides, because something else in the ESP32 build still
calls it, so removing one call site frees nothing and the replacement costs a few
bytes. The `ui-tiny` change, which only moves a call inside a guard that is
already there, leaves `.text` identical (346,648 on both sides).

The 18 environments of the review series plus both T-Echo companion environments
were built as well, to show that nothing else moved. None of them compiles
`ui-tiny` or `ui-orig`.

All 20 build on both sides and every one of them reports the same flash and
RAM size as `dev`, with not a single byte of difference. That is the expected
result and it was stated before the builds ran: none of the 20 has `ui-tiny` or
`ui-orig` in its `build_src_filter`, so none of them contains the changed code.
Any deviation would have meant something else moved.

### What is not verified

- **No automated test.** `UITask` pulls in the display driver, the board and the
  mesh, so it does not build in a native environment. A host harness for it would
  be a great deal larger than the change it guards.
- **No hardware run.** The display path needs a LilyGo T-Echo Card whose panel
  fails to initialise. No board within reach of this work runs `ui-tiny` at all:
  the T-Echo used for the other two fixes in this series builds `ui-new`.
  The fix is therefore **NOT verified at runtime**. The evidence is the source
  chain above, the reachability check, and the builds.
- The `ui-orig` change is verified only in the sense that it produces the same
  string: both implementations were run against each other on the host for twelve
  inputs, including a leading dash, several dashes and none, and the resulting
  `_version_info` is identical in every case. A leak of a few bytes once per boot
  has no runtime symptom to observe.

### Observation, not fixed here

`sprintf(_version_info, "%s (%s)", version, FIRMWARE_BUILD_DATE)` at
`ui-orig/UITask.cpp:62` can still write past `_version_info[32]` if
`FIRMWARE_VERSION` and `FIRMWARE_BUILD_DATE` are long enough together. This
change bounds the version part; the format as a whole is unchanged and out of
scope here.

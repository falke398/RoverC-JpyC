# M5StickC PLUS JoyC / RoverC Pro

Two Arduino C++ firmwares for original M5StickC PLUS devices: a JoyC controller
and a RoverC Pro receiver. They communicate directly over encrypted ESP-NOW.
No router, phone or Internet connection is needed while driving.

Source repository: [falke398/RoverC-JpyC](https://github.com/falke398/RoverC-JpyC).

Development root: `T:/projects/m5stack/RoverC/M5_Rover_Joystick`.
See [PLAN.md](PLAN.md) for milestones, decisions and verification status.

**Current status:** the user confirms both full M5Unified application screens:
JOYC / DISARMED / 30% on COM7 and ROVER / DISARMED / Fault:4 / I2C:OK on COM8.
The rover reports successful accessory communication. Radio:OK on both devices
and Link:OK on the controller confirm the provisioned two-way link. The user confirms
Calibration saved on JoyC, and Joy:OK throughout full travel with reported
endpoints within 0..200 and centered readings 97, 98, 103, 102. Keep
AxisMaximum=200. All 10 native control tests pass. Calibration persistence after
reboot and physical motion/safety checks remain pending. The user authorized a
wheels-raised motor test: MotionCommissioned is now true, and both builds and
uploads succeeded for the initial motion test. The requested update now sets
startup speed to 50%, B selection to 30%/50%, and GripperCommissioned to true;
both builds and uploads succeeded. The user confirmed the factory gripper and
original connection. The user confirms 50% operation and gripper movement in
both directions. Closing at 30 degrees moves inward quietly but leaves about
a 3 cm empty-jaw gap; opening at 10 degrees buzzes slightly. Channel 0 is retained;
the user confirms open 15 degrees works but close 35 degrees still leaves about
a 3 cm gap. Following the deployed 70-degree setting, the user requested a
closed target of 80 degrees; open remains 15 degrees. The same update swaps
horizontal stick roles: left turns, right crabs. Both builds/uploads succeeded.
The new mapping, 80-degree closing target and full safe travel remain unverified.
Keep fingers clear and switch off rover power if buzzing or strain persists;
disarming alone leaves the servo holding its position.
With a healthy link and
accessories, expect rover fault 0; confirm both devices remain DISARMED before
the deliberate neutral arm-and-stop test. The user reports arming/disarming and
directional motion work, and confirms the crab left/right correction worked.
InvertSideways remains true. Remaining safety checks are pending. Forward/back
and saved calibration are unchanged; horizontal inputs retain their matching
physical-axis centers when their roles are swapped.

2026-11-08: build directories moved to local disk (see below); cold builds now take
about 50 s per role instead of about 2.5 minutes. Both rebuilt firmwares were
uploaded (COM7, COM8) and both displays work, but the user reports the rover does
not move after arming. This is not yet diagnosed; check rover Fault/I2C/Wheels, the
controller Link/Joy values and the RoverC base power before changing code.

Also 2026-11-08: both displays were redesigned for portrait (135x240, 90 degrees
counter-clockwise from the old landscape view) and uploaded successfully to COM7 and
COM8: a dashboard on the controller and a top-down wheel view on the rover. Only
the build and upload were verified; the physical screen orientation, wheel-to-bar
mapping and sprite allocation are not yet confirmed on the devices.

## Display screens

Both devices use `setRotation(0)` (portrait). If a screen appears upside-down, use
`setRotation(2)` in `src/controller/main.cpp` and `src/rover/main.cpp`. Drawing goes
into a 64 KB off-screen sprite allocated before Wi-Fi starts; if allocation fails the
code draws directly to the screen. Shared colors and helpers are in
[include/DisplayUi.h](include/DisplayUi.h).

**Controller (dashboard):** a colored header shows JOYC and the state (ARMED green,
DISARMED amber, NO LINK/FAULT red). Bars show SPEED (cap, 0..60), FWD, TURN and CRAB
(signed, -100..100). Rows show Link, Radio and Joy status. The footer shows the
calibration message, the radio error text if any, and the rover fault number.

**Rover (wheel view):** a colored header shows ROVER and the speed cap. A top-down car
shows each wheel as a bar with its speed (front-left, front-right, rear-left,
rear-right, matching the mixing order) and the state word at the bottom: ARMED,
DISARMED, LINK LOST, LOCAL STOP, I2C ERROR or CONFIG.

The MAC address, raw joystick values, `Peer`/`Ack` counters and Motion/Grip enabled
flags are no longer shown. The station MAC is printed by esptool during every upload
(`MAC: ...` in the upload log).

Mockups: [docs/display/](docs/display) holds the portrait proposals (A banner,
B dashboard, C two pages, D visual, E minimal HUD; chosen: controller B, rover D),
rendered by `scripts/render_display_mockups.py`. [docs/display/current](docs/display/current)
shows the earlier landscape screens, rendered by `scripts/render_current_displays.py`
with sample values and an approximate font. Both scripts need Pillow
(`requirements-dev.txt`) and run with `.\.venv\Scripts\python.exe scripts\<name>.py`.

## Debug log

Both firmwares write a timestamped one-line log to USB serial (115200 baud). Connect
the device and run one monitor per port (close it before uploading):

```powershell
.\.venv\Scripts\python.exe -m platformio device monitor -p COM7 -b 115200 -f log2file
```

Use COM7 for the controller and COM8 for the rover. `-f log2file` also saves the
output to `platformio-device-monitor-*.log` in the project folder (ignored by Git).
The controller is handheld, so tethering it while driving is practical; monitor the
rover on the bench with the wheels raised, or tether both from two terminals.

Line types (`[ms]` is milliseconds since boot):

| Line | Meaning |
| --- | --- |
| `BOOT` | Reset reason (`POWERON`, `BROWNOUT`, `PANIC`, ...), free heap, display buffer, I2C and radio status. A rover `BROWNOUT` or repeated boots points to power. |
| `ARM requested` / `ARM confirmed` (controller), `ARMED` (rover) | Arming steps. |
| `DISARM reason=...` | Every armed-to-disarmed transition with the cause and the counters at that moment. |
| `ROVER BOOT`, `ROVER FAULT a->b` | The controller saw the rover reboot or change fault. |
| `JOY FAIL err=N`, `JOY recovered after N ms` (controller) | Joystick I2C read failed twice in a row / worked again. Dropouts shorter than 100 ms (`settings::JoyGrace`) send neutral motion and keep the arm; longer ones disarm with `joy read failed`. |
| `I2C FAIL motors/stop/servo err=N` | RoverC write failed. `err` is the Wire status (1 length, 2 NACK on address, 3 NACK on data, 4 other, 5 timeout) or 100 queue, 101 short read, 102 axis range, 103 bad argument. A rover I2C failure latches Fault 3 until reboot. |
| `STAT` (every second) | Link and health counters, see below. |

Disarm reasons: controller `radio not ready`, `motion locked`, `joy read failed`,
`not calibrated`, `calibrating`, `no rover status`, `status timeout` (no rover status
for 300 ms), `ack stalled` (status arrives but the rover stopped acknowledging
commands), `rx mismatch`, `rover fault`, `rover rebooted`, `rover refused`, `button A`,
`calibration`. Rover `button A`, `i2c fault`, `config`, `cmd timeout` (no command for
250 ms), `peer changed`, `cmd disarm` (the controller sent disarm).

`STAT` fields: `stAge`/`ackAge` ms since the last rover status / acknowledgement
progress (limit 300); `cmdAge` ms since the rover accepted a command (limit 250);
`rx`/`tx` packets received/sent; `drop` wrong sender or length; `over` mailbox packet
replaced before it was read; `busy` send skipped because one was outstanding;
`txFail` send call failed; `ackOk`/`ackFail` MAC-level delivery results; `acc`/`rej`
rover accepted/rejected commands; `joyFail`/`i2cFail` + `joyErr`/`i2cErr` I2C failure
count and last code; `gapMax` longest control-loop gap in the last second (ideal 20
ms); `dispMax` longest display redraw; `heap`/`minHeap` free and lowest free heap.

On screen, the controller footer shows `Off: <reason> (count)` for the last disarm.
After a `joy read failed` disarm its last line shows `J:e<err> a b c d`: the I2C
error code (see the `I2C FAIL` table; 102 means an axis above `AxisMaximum` plus
`settings::AxisTolerance`) and the
raw axes at the failure. The rover shows the same `Off:` line plus `rst:<reset reason> up:<seconds>`. A small `up`
value or a `BROWNOUT` reset on the rover means it rebooted.

## Layout

| Location | Purpose |
| --- | --- |
| [platformio.ini](platformio.ini) | Pinned controller, rover and native environments |
| [src/controller/main.cpp](src/controller/main.cpp) | JoyC sampling, calibration, UI and commands |
| [src/rover/main.cpp](src/rover/main.cpp) | Receiver, motor/servo writes and safety loop |
| [lib/ControlCore](lib/ControlCore) | Portable packet codec, mixing, input and safety logic |
| [lib/Hardware](lib/Hardware) | Checked HAT I2C transactions |
| [lib/RadioLink](lib/RadioLink) | ESP-NOW configuration and synchronized mailbox |
| [include/LocalConfig.example.h](include/LocalConfig.example.h) | Nonfunctional provisioning template |
| [include/DisplayUi.h](include/DisplayUi.h) | Portrait display colors and drawing helpers |
| [include/DebugLog.h](include/DebugLog.h) | Timestamped serial log and reset-reason helper |
| [docs/architecture.html](docs/architecture.html) | Interactive architecture, data flows, resource usage and disarm reasons (open in a browser) |
| [scripts/resource_report.py](scripts/resource_report.py) | Prints section and origin sizes of the built firmware ELFs as JSON (feeds the resource figures in architecture.html) |
| [scripts/render_display_mockups.py](scripts/render_display_mockups.py) | Renders portrait display proposals to docs/display |
| [scripts/render_current_displays.py](scripts/render_current_displays.py) | Renders the earlier landscape screens to docs/display/current |
| [test/test_control/test_main.cpp](test/test_control/test_main.cpp) | Native control tests |

## Development setup

Use the PlatformIO IDE extension for VS Code, or the project-local CLI below.
The firmware uses pinned `M5Unified` 0.2.7 and `M5GFX` 0.2.36, the versions used
by the working display diagnostic. Do not substitute PLUS2-specific firmware.
The `m5stick-c` PlatformIO board definition supplies the ESP32 build settings;
M5Unified detects the board and handles its screen, power hardware and buttons.
External accessory detection is disabled, and the external I2C controller is
released before our HAT driver initializes GPIO0/26. The internal power-control
bus remains owned by M5Unified.

From this project directory in PowerShell, with [uv](https://docs.astral.sh/uv/)
installed:

```powershell
uv venv .venv --python 3.12
uv pip install --python .venv\Scripts\python.exe -r requirements-dev.txt
.\.venv\Scripts\python.exe -m platformio test -e native
.\.venv\Scripts\python.exe -m platformio run -e controller -e rover
```

Do not recreate an existing working environment unnecessarily. The local
environment was bootstrapped during development. To keep firmware builds fast on a
network share, PlatformIO's SDKs/packages, build output and library dependencies
live under `%LOCALAPPDATA%/M5RoverTools` (`pio-core`, `pio-build`, `pio-libdeps`;
see `platformio.ini`). Logs and the native compiler cache use `.pio`. `.pio` and
`.venv` are ignored by Git. The first builds need Internet access.

Windows native tests prefer Zig 0.13.0 installed under
`%LOCALAPPDATA%/M5RoverTools/zig-0.13.0/ziglang`, falling back to Zig in the
Python environment that runs PlatformIO. Other hosts use their native compiler.
For this network-backed workspace, Zig could not launch helper processes from
`T:`. The following user-approved local installation resolved that failure:

```powershell
uv pip install --python .venv\Scripts\python.exe --target "$env:LOCALAPPDATA\M5RoverTools\zig-0.13.0" ziglang==0.13.0
.\.venv\Scripts\python.exe -m platformio test -e native
```

No system PATH change is needed. Project source, documentation, build outputs
and the Zig cache stay in the workspace. The compiler override runs at the
BuildProgram boundary, after native platform tool loading and before test
environments are cloned. All 10 native tests passed with this setup; warnings
from Zig's bundled runtime sources did not prevent successful execution.

## First flash: stationary diagnostics

1. Attach the JoyC and RoverC Pro to their respective PLUS devices with power off.
   Use the intended HAT connection, not Grove GPIO32/33. Both external buses use
   SDA 0, SCL 26, address `0x38`, initially at 100 kHz.
2. Charge/power the units according to M5Stack's instructions. Keep the RoverC
   base power switch reachable. Raise all four wheels clear of the surface.
3. Use a USB data cable and enumerate ports:

```powershell
.\.venv\Scripts\python.exe -m platformio device list
```

4. Flash each role to the correct physical device. Replace the example COM ports:

```powershell
.\.venv\Scripts\python.exe -m platformio run -e controller -t upload --upload-port COM5
.\.venv\Scripts\python.exe -m platformio run -e rover -t upload --upload-port COM6
.\.venv\Scripts\python.exe -m platformio device monitor -p COM5 -b 115200
```

5. Record the **STA MAC** of each device (printed as `MAC:` in the esptool upload
   output), including which is controller
   and which is rover. Default builds have no peer keys and cannot arm. The rover
   attempts motor zero at boot and continues zero writes while disarmed. Do not
   assume a failed I2C zero write stopped the hardware.
6. Observe all four raw JoyC axes and the button byte. Expected register order is
   left X, left Y, right X, right Y at `0x60..0x63`; button bits at `0x64` are
   left `0x10`, right `0x01`. Verify that on your physical controller.

If no COM port appears, check the USB data cable and Windows Device Manager and
install the appropriate official M5Stack USB driver for the enumerated device.
Close serial monitors before uploading. Do not flash both roles to one board.

If a terminal command starts with an unexpected `^U`, cancel that input with
Ctrl+C and enter the command without the prefix. For terminal automation that
sends Ctrl+U, this session-only binding makes it clear the line instead:

```powershell
Set-PSReadLineKeyHandler -Chord Ctrl+u -Function BackwardDeleteLine
```

## Local provisioning

Generate the ignored configuration in this project directory using the observed
station MACs. For the current pair:

```powershell
.\scripts\provision.ps1 -ControllerMac 'F0:24:F9:96:A2:CC' -RoverMac 'F0:24:F9:97:2E:00'
```

The script generates two independent random 16-byte keys without displaying
them, enables provisioning, and leaves motion/gripper commissioning disabled.
It refuses to overwrite an existing configuration. If Windows blocks scripts,
`Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force` allows the
script in that terminal only; afterward remove the override with
`Set-ExecutionPolicy -Scope Process -ExecutionPolicy Undefined -Force`.

For subsequent adjustments, edit the local header, never the template:

- Set `ControllerMac` and `RoverMac` to the observed station MAC byte arrays.
- Generate two independent random 16-byte values locally: `PrimaryKey` and
  `LinkKey`. Use the same values for both firmwares. Never use public example
  keys, commit them or paste them into chat. Placeholder/uniform keys are rejected.
- Set `Provisioned = true`. Keep both commissioning switches false initially.
- Keep `Channel` identical for both devices (default 1). Do not join an access
  point while using this firmware. No automatic pairing or channel scanning.
- Choose `AxisMaximum` from observed hardware: default 200, or 255 if appropriate.
  M5Stack's product description and newer library differ about this range.
  Confirm center and both extremes; out-of-range readings prevent driving.
- Configure axis inversions after checking the physical directions.

Build and flash both devices again after changing shared configuration. Replacing
a device requires new MAC settings. The receiver accepts only the configured
controller; encryption is not a substitute for the motion safety checks.
Check green Radio and Link dots on the controller dashboard (the rover shows its
state word at the bottom). Rover fault 4 (CONFIG on the rover screen)
remains expected while `MotionCommissioned` is false, even with a working link.

## Calibration and commissioning

1. With sticks released and the controller disarmed, hold **B for two seconds**.
   Keep both sticks still for another second. The controller checks that centers
   are plausible and samples stable, then saves them in NVS. A failed calibration
   does not overwrite a previously valid one. A storage failure disables arming.
2. Set `MotionCommissioned = true` on both builds only when ready for a supervised
   wheels-raised test. This is the explicit motor-enable gate, not evidence that
   the hardware has passed. Rebuild and reflash. Startup speed is now 50%; select
   30% with a short B press while disarmed for initial safety checks.
3. First verify DISARMED at 30%, healthy radio/input, Calibration loaded and no
   wheel movement at boot. Release A, center sticks, then hold A for one second
   to arm; wheels should still be stationary. Release A, apply a small forward
   input and press A to disarm: all wheels must stop. If they do not, switch off
   rover power immediately. Keep wheels raised and retain 30% for initial checks.
   Deliberately rearm for the rover A local-stop check and each direction check.
   Left stick left/right should turn, left forward/back should translate, and
   right stick left/right should crab without intentional rotation. Correct
   `InvertSideways`, `InvertForward`, `InvertTurn` as needed.
   Stop and reflash before retesting. Mechanical drift is possible without encoders.
4. Gripper output has its own enable gate. Inspect the servo connection and
   select channel 0 or 1. `ServoOpen=10` and `ServoClose=30` are conservative example
   values from M5Stack's demonstration, **not verified travel limits**. Set safe
   initial angles appropriate to your assembly before opting into a stationary
   test with `GripperCommissioned = true`. Adjust gradually without forcing stops.
   With wheels raised and sticks centered, arm and click the left stick once to
   open, then the right stick once to close. Release each click between commands.
   Keep fingers clear; switch off rover power if the servo strains, buzzes or
   hits a stop. Disarming alone retains the last commanded servo position.
5. Complete all hardware acceptance checks in [PLAN.md](PLAN.md) before floor use.

Changing axis range invalidates saved calibration. Recalibrate deliberately;
the firmware never learns a held stick as its center automatically. Only centers
are calibrated; raw endpoints remain the configured range.

## Controls

| Input | Action |
| --- | --- |
| Left stick forward/back | Forward/backward translation |
| Left stick left/right | Rotation |
| Left stick diagonally | Combined forward/backward translation and rotation |
| Right stick left/right | Sideways crab movement, including with other motion |
| Right stick vertical | Unused |
| Left stick click | Set gripper open |
| Right stick click | Set gripper closed |
| Both clicks together | Ignored |
| Controller A held for one second | Arm with neutral sticks and healthy link |
| Controller A press while armed | Disarm immediately |
| Controller B short press | Switch 30% / 50% maximum motor output (boot: 50%) |
| Controller B hold while disarmed | Calibrate neutral |
| Rover A | Local disarm |

A must be released after startup or a fault before a new arm gesture. Neutral
must be observed for at least 300 ms. The controller waits for the rover's armed
acknowledgement before sending nonzero motion. Always release and deliberately
rearm after link loss, a local stop or a reboot. B does not arm the rover.
Both firmware roles must be updated for 50% support. The codec retains acceptance
of legacy 60% packets, but the updated controller selects only 30% or 50%.

Commands are sent every 20 ms and status every 100 ms. Faults and missing commands
zero motor outputs without a ramp; normal motion uses a ramp. Releasing the
sticks uses the normal deceleration ramp, so leave stopping room. Maximum output
applies to every wheel even for diagonals. Full rotation takes priority over
translation, matching the official RoverC mixer.

Gripper commands are absolute, debounced and rate-limited; lost/repeated packets
cannot toggle its state. On disarm/link loss the servo keeps its last position.
No position feedback is available: the displayed gripper value is commanded,
not measured. Reboot does not intentionally reposition the servo.

## Faults and safety

| Code | Meaning | Recovery |
| --- | --- | --- |
| 0 | No fault; may still be disarmed | Neutral and deliberate arm gesture |
| 1 | Command timeout or changed peer session | Restore link, release A and rearm |
| 2 | Rover local stop | Release rover A, then deliberately rearm at controller |
| 3 | Motor/servo I2C fault | Power off, repair connection, reboot; latched |
| 4 | Radio configuration or motion commissioning missing | Correct config and reboot |

Timeout is 250 ms since the last accepted fresh command; the design target is
a zero write within 300 ms total, **not a measured stopping-distance guarantee**.
Duplicates, malformed packets and wrong sessions do not refresh it. The controller
also disarms when status or acknowledgement progress is missing for 300 ms.

An ESP32 hang/reset, failed motor bus or retained STM32 command can defeat a
software stop. This system has no independent safety-rated cutoff. Keep the base
power switch accessible and supervise use. Test bus faults only with wheels
raised. Do not use near stairs, people, pets or fragile objects during development.
After bus errors, zero writes are bounded but cannot guarantee delivery.

## Verification commands

```powershell
.\.venv\Scripts\python.exe -m platformio test -e native
.\.venv\Scripts\python.exe -m platformio run -e controller -e rover
```

The current tests cover axis deadzones/endpoints/inversion, crab/forward/back/turn
wheel signs, cap bounds across combined inputs, packet roundtrip/rejection,
timeout and rearming, duplicate/wrong-session rejection, sequence/time wrap,
latched bus faults, arm gestures, ramp stops and gripper button conflicts.

These portable tests do not exercise actual I2C, Wi-Fi callbacks, NVS persistence,
LCD rendering, pin routing or physical stopping behavior. Those remain bench
checks. Keep [PLAN.md](PLAN.md) current with actual results, not just commands run.

## References

- [Original PLUS hardware](https://docs.m5stack.com/en/core/m5stickc_plus)
- [RoverC Pro](https://docs.m5stack.com/en/hat/hat_roverc_pro)
- [JoyC](https://docs.m5stack.com/en/hat/hat_joyc)
- [Official RoverC registers and wheel mapping](https://github.com/m5stack/M5-RoverC)
- [JoyC register/button reference](https://github.com/m5stack/uiflow-micropython/blob/master/m5stack/libs/hat/joyc.py)
- [ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html)
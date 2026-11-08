# JoyC / RoverC Pro Implementation Plan

Updated: 2026-11-01

## Location and maintenance

All project software, tests and documentation belong in
`T:/projects/m5stack/RoverC/M5_Rover_Joystick`. This file is the canonical living
plan. Update status, decisions and verification after each meaningful milestone.
Do not record provisioning keys here. External SDK/tool installations may live
in their normal tool-managed locations; project artifacts stay here.

Source repository: https://github.com/falke398/RoverC-JpyC.git (`origin`, `main`).
Publish source, tests and documentation only. Keep include/LocalConfig.h,
pairing keys, build outputs, toolchains and machine-local editor settings local.
Git on this network share may require a repository-specific safe.directory
exception; do not disable ownership checks globally with a wildcard.

## USB device mapping

Confirmed by the user's USB-disconnection test on 2026-11-01:

| Role | Current port | CH9102 USB serial |
| --- | --- | --- |
| JoyC controller | COM7 | 58A4004379 |
| RoverC receiver | COM8 | 58A4003076 |

COM7 disappeared when JoyC was unplugged; COM8 remained with the rover connected.
COM4 and COM5 are unrelated devices and must not be used for these uploads.
Use explicit upload ports. Port numbers can change after reconnection; compare
USB serial numbers if necessary. These USB serial numbers are not ESP32 station
MAC addresses and must not be used as ESP-NOW peer addresses.

Rover upload command used:
`.\.venv\Scripts\python.exe -m platformio run -e rover -t upload --upload-port COM8`

2026-11-01: User reports that the rover flash appears successful, following the
earlier JoyC flash report. Final upload logs have not been independently checked.
The legacy full firmware subsequently reset before setup. JoyC now runs the full
M5Unified controller firmware: the user confirms JOYC / DISARMED / 30% on COM7.
The user also confirms ROVER / DISARMED / Fault:4 / I2C:OK after the migrated
rover upload to COM8. Both full application screens now work. After provisioning,
the user confirms Radio:OK on both devices and Link:OK on the controller.
Station MACs supplied by the user: controller F0:24:F9:96:A2:CC and rover
F0:24:F9:97:2E:00. LocalConfig.h has been generated with these addresses and
private random keys. Provisioned is true; MotionCommissioned is now true in
the local source configuration. The user now authorized enabling the factory
gripper at the original connection and increasing speed to 50%. GripperCommissioned
is true locally; startup speed is 50% and B switches 30%/50%. Both builds and
uploads for this new update succeeded. The earlier motion-test builds/uploads
to COM7 and COM8 succeeded. User reports arming/disarming work and directional motion
responds, but crab left/right was reversed. InvertSideways is now true and the
controller-only correction has been built and uploaded successfully to COM7;
the user confirms its physical retest worked. Remaining hardware safety checks
are required.
The earlier radio configuration was uploaded and the two-way encrypted
link is confirmed by the displayed status. The user confirms Calibration saved
on JoyC, verifying stable neutral sampling and a successful NVS write. User
reported raw endpoints fit AxisMaximum=200, with Joy:OK throughout full travel;
centered readings are 97, 98, 103, 102. Explicit calibration persistence checks
and remaining physical safety tests are pending.
Keep wheels raised.

## Agreed design

- Two original M5StickC PLUS devices (ESP32-PICO-D4 / AXP192, not PLUS2).
- PlatformIO with Arduino C++; separate `controller` and `rover` firmware.
- Direct encrypted ESP-NOW unicast, fixed channel, one-time MAC/key setup.
- Left stick: forward/back and rotation, including combined inputs.
- Right stick horizontal: sideways crab movement; vertical unused.
- Left/right stick clicks: open/close gripper; ignore simultaneous clicks.
- Button A: deliberate neutral-stick arming; immediate disarm when armed.
- Button B: 30% / 50% maximum motor output; boot at 50% per user request.
- Rover button A: local disarm. Reconnection or reboot never automatically arms.

## Architecture

JoyC I2C -> checked input -> calibration/deadzone -> command packet -> ESP-NOW ->
validation and safety state -> wheel mixing -> checked RoverC I2C writes.
Rover sends application status acknowledgements to the controller.

Both accessories use I2C address `0x38`, SDA GPIO 0, SCL GPIO 26, on separate
devices. Keep the external bus separate from M5's internal power/sensor bus.
Use M5Unified 0.2.7 and M5GFX 0.2.36 for screen, power and buttons, matching the
successful display diagnostic. Disable external accessory detection and release
the separate external I2C peripheral before the checked HAT Wire driver starts.

### Radio and safety contract

- Commands at 50 Hz; status at 10 Hz; displays at 5 Hz.
- Explicit binary encoding under 250 bytes with magic, version, packet type,
  boot/session identifiers, sequence, bounded axes, speed and gripper target.
- Only provisioned peers; explicit PMK and shared LMK; no broadcast pairing.
- One send outstanding; do not queue old motion. Callbacks only copy bounded
  data to synchronized mailboxes, never operate motors or displays.
- Ignore malformed, wrong-peer, old-session, duplicate and out-of-order data.
- A valid neutral/disarmed handshake is required before arming.
- Rover commands zero at 250 ms without a fresh valid command; target total
  zero-write latency <=300 ms including scheduling and I2C.
- Controller disarms on input failure, missing status, or rover fault/reboot.
- Fault recovery requires release and a fresh deliberate arm action.
- Zero commands bypass ramping. I2C errors latch faults and trigger bounded
  zero-write retries. Servo holds last target on disarm; no automatic opening.
- No servo movement before bench-confirmed channel/travel and explicit command.

## Milestones

| Phase | Deliverable | Status | Gate |
| --- | --- | --- | --- |
| 0 | Workspace plan and confirmed requirements | Complete | This file exists |
| 1 | Pinned dual-build scaffold and nonmoving diagnostics | Both full M5Unified role screens confirmed by user; final build logs not captured | Both builds pass and role screens confirmed |
| 2 | Portable input/motor/protocol/safety logic | All 10 native control tests passed using local-disk Zig | Automated tests pass |
| 3 | Checked I2C and encrypted ESP-NOW adapters | Rover I2C, JoyC full-travel readings and two-way radio link confirmed; fault tests pending | Builds and bench checks |
| 4 | Controls, calibration, gripper and displays | Neutral calibration saved and axis range checked; persistence and control tests pending | Integration tests/builds |
| 5 | Flashing and operation documentation | Written; commands need verification | Reproducible instructions |
| 6 | Hardware acceptance | Pending | User-assisted bench tests |

### Phase 1: Scaffold and diagnostics

Use `board=m5stick-c`, `M5Unified` / `M5GFX`, explicit source filters and
shared libraries. Establish both build environments and portable tests.
Show role, STA MAC, accessory detection and raw joystick values. Missing local
configuration must leave both firmwares nonmoving. Send motor zero at boot.

### Phase 2: Portable control and tests

Implement proportional axes with plausible-center calibration, adjustable
deadzone, inversion, official RoverC wheel ordering and turn priority. Cap final
wheel outputs even for diagonal combinations. Use wide intermediate arithmetic.
Implement strict packet validation, wrap-safe freshness and rover safety state.
Test exact timeout boundaries, sequence/time wrap, rearming, invalid inputs,
wheel signs, cap enforcement and repeated absolute gripper commands.

### Phase 3: Hardware and transport

Use bounded I2C transactions; reject short reads and invalid axes. Upstream
RoverC writes discard error status, so use a small checked adapter with the
documented register layout. Verify JoyC button masks/axes against source and
then hardware. Configure both station MACs, channel and private keys locally.
Check callback APIs against the pinned Arduino core. Provide application status
because ESP-NOW send success alone does not confirm command acceptance.

### Phase 4: Integration

Require neutral for 300 ms and a deliberate one-second A hold before arming.
Require release after startup/fault; keep the rover's local stop effective.
Persist deliberate stable neutral calibration in NVS, never auto-learn a held
stick. Add acceleration limiting, resetting immediately on stop. Gripper clicks
set repeated absolute targets; debounce buttons, ignore conflicting clicks,
limit servo updates and use only calibrated travel. Display link, armed/fault,
speed and input/command state without blocking control. Do not equate reported
M5 battery voltage with the external accessory battery charge.

### Phase 5: Documentation

Document Windows tool setup, USB/COM troubleshooting, per-role build/upload,
one-time peer provisioning, controls, calibration, commissioning gates, safety
limits and upstream sources. Keep secrets in ignored local configuration.

### Phase 6: Hardware acceptance

- Wheels raised: identify wheels/directions, axes/buttons and gripper travel.
- No motion on boot, missing configuration or off-center arming attempts.
- Verify local stop, controller power loss, either-device reboot, channel
  mismatch, JoyC disconnection and fresh deliberate rearming.
- Verify invalid/duplicate traffic cannot retain motion; measure timeout stop.
- Test motor-bus interruption only wheels-raised with power switch accessible.
  Determine whether STM32 retains speed during host reset or bus loss.
- Clear level floor at 30%, then 50%: crab, diagonals and combined rotation.
- Exercise driving/gripper for ten minutes; check brownouts, range and latency.
  Do not claim range, battery life or stopping distance without measurements.

## Scope and limitations

No phone/web UI, router dependency, automatic pairing, OTA, obstacle avoidance,
heading hold, autonomous navigation or motor-controller firmware replacement.

This is not a safety-rated stop system. A stuck/reset ESP32 or broken motor bus
can leave the STM32 executing its last command. Software cannot guarantee a stop
in that case. Keep the base power switch accessible. An independent hardware
cutoff/watchdog is a separate scope decision.

## Verification log

- 2026-11-08: Faster builds. Project sits on a network share, so PlatformIO
  `core_dir`, `build_dir` and `libdeps_dir` now point at
  `%LOCALAPPDATA%/M5RoverTools/{pio-core,pio-build/m5rover,pio-libdeps/m5rover}`
  (core seeded from the old `.pio-core`). Cold builds after `-t clean`: controller
  51.4 s (was 2m28s), rover 50.2 s (was 2m22s); a no-change rebuild is still ~18.7 s.
  Both new builds uploaded successfully (controller COM7, rover COM8; logs in
  `.pio/new-*-upload.log`, the controller log came out empty but the summary shows
  SUCCESS). No firmware source or LocalConfig change. User reports both displays
  work, but the rover does not move after arming. Cause not yet diagnosed: rover
  Fault/I2C/Wheels, controller Link/Joy values and base power are still to be
  read. Keep wheels raised while investigating.
- 2026-11-01: Captured .pio/stick-swap-close80-upload.log confirms both builds
  succeeded (controller 2m28s, rover 2m22s), followed by successful COM7 and COM8
  uploads and the final BOTH STICK-SWAP AND CLOSE80 UPLOADS SUCCEEDED marker.
  Installed mapping: left Y forward/back, left X turn, right X crab, right Y
  unused. Servo targets are open=15, close=80. Saved physical-axis calibration,
  50% startup, inversion settings and gripper clicks are retained. Physical
  direction retest and quiet safe closure at 80 remain unverified.
- 2026-11-01: User requested close=80 degrees and swapped horizontal stick
  controls. Set only local ServoClose 70->80, retaining open=15/channel 0.
  Controller now shapes turn from physical left X (axes[0], calibration[1])
  and sideways from physical right X (axes[2], calibration[3]); forward remains
  left Y. Physical calibration storage, inversion settings, joystick clicks,
  arming and 50% startup are unchanged. Started both-role build then sequential
  COM7/COM8 uploads, aborting on failures, logged to
  .pio/stick-swap-close80-upload.log. Deployment and hardware retests are pending.
  No quiet/full-closure result at 70 degrees was reported; 80 is not yet a
  verified safe mechanical endpoint. Keep jaws empty/fingers clear and stop
  power if the servo strains.
- 2026-11-01: Rover-only build/upload of open=15 and close=70 completed
  SUCCESS on COM8 in 00:02:44.160, with flash hash verified in
  .pio/gripper-close70-upload.log. Physical closure and absence of strain at
  70 degrees remain unverified. Controller, speed and other settings unchanged.
- 2026-11-01: User requested ServoClose=70 degrees. Changed only the local
  closed target from 65 to 70, retaining open=15 and channel 0. Started
  rover-only build/upload on COM8, logged to .pio/gripper-close70-upload.log.
  Build/upload completion and quiet physical closure are pending. No confirmed
  full-closure or strain result at 65 degrees was supplied; do not infer one.
- 2026-11-01: Rover-only build/upload of open=15 and close=65 succeeded on
  COM8 in 00:02:38.232 with flash hash verified, captured in
  .pio/gripper-close65-upload.log. Physical closure and absence of strain at
  65 degrees still need confirmation. No other settings changed.
- 2026-11-01: User confirms the open position at 15 degrees is good, while
  close=35 still leaves about a 3 cm gap, and requests a 30-degree adjustment.
  Interpreted as an additional 30 degrees in the established closing direction:
  changed only local ServoClose 35->65, retaining ServoOpen=15 and channel 0.
  Started rover-only build/upload on COM8, captured in
  .pio/gripper-close65-upload.log; deployment result and physical retest are
  pending. This larger requested step is not a verified safe endpoint. Keep
  empty jaws/fingers clear and power off promptly if the servo strains.
- 2026-11-01: Rover-only build/upload of open=15 and close=35 completed SUCCESS
  on COM8 in 00:02:30.348 with flash hash verified, captured in
  .pio/gripper-travel-upload.log. Physical retest remains pending: check whether
  opening is quiet and whether closing reduces the empty-jaw gap without strain.
- 2026-11-01: User confirms right click moves empty jaws inward and is quiet
  at close=30, leaving about a 3 cm gap; left click buzzes slightly at open=10.
  Opening may be near a mechanical limit, but the cause is not established.
  Changed local ServoOpen 10->15 and ServoClose 30->35, retaining channel 0:
  five degrees inward from the buzzing open position and five degrees farther
  in the confirmed closing direction. Started rover-only build/upload on COM8,
  logged to .pio/gripper-travel-upload.log. Controller firmware, speed and
  calibration are unchanged. Deployment and physical retest are pending. Do
  not force full closure; stop power if strained, since disarm holds the servo.
- 2026-11-01: User confirms 50% operation works and the gripper moves in both
  directions, but does not close fully. Current servo settings are channel 0,
  open 10 and close 30 degrees. Full closing travel is not commissioned. Before
  increasing the close target, confirm the right-stick click moves jaws inward
  and that the servo is not buzzing, straining or meeting a mechanical stop.
  No firmware/angle change made from this report alone; stop power if strained.
- 2026-11-01: Captured .pio/speed50-gripper-upload.log confirms both builds
  succeeded (controller 2m59s, rover 2m58s), followed by successful controller
  COM7 and rover COM8 uploads and the final BOTH SPEED50 AND GRIPPER UPLOADS
  SUCCEEDED marker. 50% startup, B 30/50 selection and gripper enable are now
  deployed. Native suite passed all 10 expanded tests. Physical gripper travel,
  mechanical direction and 50% operation remain unverified; first jaw test must
  be stationary with wheels raised, fingers clear and power switch accessible.
- 2026-11-01: User requested gripper activation and 50% speed, and confirmed
  factory RoverC Pro gripper/original connection with untested travel. Enabled
  GripperCommissioned locally, retaining channel 0/open 10/close 30 for a
  supervised first stationary test; safe physical travel remains unverified.
  Set controller startup cap to 50 and B selection to 30/50. Added 50 to accepted
  command/status packet caps, retaining legacy 30/60 acceptance and existing
  absolute 60 motor bound; current controller cannot select 60. Expanded existing
  tests for 50% command/status round trips, rejected invalid caps, wheel bounds,
  close clicks and held clicks across disarm. All 10 native tests passed in
  14.455 seconds. Started build-both then sequential COM7/COM8 uploads with
  failures aborting later steps, logged to .pio/speed50-gripper-upload.log.
  Deployment result and physical speed/gripper operation are pending. No new
  auto-arm or automatic boot servo movement was introduced.
- 2026-11-01: User confirms the sideways correction worked. Retain
  InvertSideways=true, forward/turn inversions unchanged and gripper locked.
  Reviewed disarm behavior for the user's operating question: controller A
  press or rover A, 250 ms command timeout, controller 300 ms status/ack timeout,
  invalid input/calibration/readiness, peer reboot and reported hardware faults.
  Centering sticks commands zero through the normal ramp but leaves the rover
  armed; releasing A does not disarm. Reconnection never automatically rearms.
  These code checks do not replace physical link-loss and stop-timing tests.
- 2026-11-01: User reports arming and disarming work and all directional motion
  responds, but crab walking is backwards. Interpreted as reversed lateral
  left/right mapping. Controller controls() independently shapes sideways via
  InvertSideways, so changed only that local flag from false to true; forward
  and turn inversions remain false. Controller build/upload on COM7 completed
  SUCCESS in 00:03:06.300 with flash hash verified, captured in
  .pio/sideways-upload.log. No rover firmware or calibration change was needed.
  Gripper remains locked. Physical sideways retest is pending; this report does
  not establish measured stopping time or link-loss safety behavior.
- 2026-11-01: Captured upload log confirms both motion-test uploads succeeded.
  The sequential command required controller success before flashing rover;
  rover finished SUCCESS with flash hash verified and the final
  BOTH MOTION-TEST UPLOADS SUCCEEDED marker was recorded. No recompilation was
  required. Devices now have motion commissioning enabled with the gripper
  still locked. Next confirm DISARMED/30%, Link:OK, rover fault 0, Calibration
  loaded and Motion:enabled / Grip:locked, then perform the neutral-arm and
  small-input A-stop tests with wheels raised. These physical checks have not
  yet been reported. The post-binding terminal log read also succeeded normally.
- 2026-11-01: Read the UTF-16 motion-build.log directly after terminal commands
  failed with a leading Ctrl+U character. Final summary confirms controller
  SUCCESS (00:14:19.122) and rover SUCCESS (00:14:12.012). These were command
  input errors, not firmware build failures. Set a session-only PSReadLine
  Ctrl+U binding to BackwardDeleteLine; the subsequent upload command was
  accepted without the stray character. Started sequential uploads to COM7
  and COM8 using nobuild/upload targets and captured .pio/motion-upload.log.
  Upload completion and physical checks remain pending; gripper stays locked.
- 2026-11-01: User authorized the wheels-raised motor test with gripper locked,
  after confirming wheels clear and after all 10 native tests passed. Changed
  only local MotionCommissioned to true; GripperCommissioned remains false.
  Startup speed remains 30%, and deliberate neutral A-hold arming is unchanged.
  Started both firmware builds with output captured in .pio/motion-build.log;
  uploads and runtime checks are pending. First checks: DISARMED/30%, healthy
  radio/input, Calibration loaded, no boot motion, neutral arm with no wheel
  movement, then small forward input followed by controller A stop. Keep the
  physical power switch accessible; do not use B to select 60% during this test.
- 2026-11-01: User approved a local C: compiler installation while retaining
  project files on T:. Installed ziglang 0.13.0 using uv under
  %LOCALAPPDATA%/M5RoverTools/zig-0.13.0; the native toolchain script now prefers
  this installation and falls back to the project Python environment. No system
  PATH changes, source relocation or firmware uploads. Re-ran the native suite
  with verbose output captured in .pio/native-test.log: 10 Tests, 0 Failures,
  0 Ignored, OK. Covered axes, mixing/caps, packet validation, timeouts and
  rearming, stale sessions, faults/wraparound, arm gesture, ramp and gripper
  logic. Zig bundled-runtime warnings were nonfatal. Compiler selection and
  network-share helper-launch blockers are resolved. Motion/gripper remain
  locked; physical stop timing, direction tests and calibration persistence
  still require hardware verification.
- 2026-11-01: Native tests failed before execution because the native platform
  deletes pre-script CC/CXX settings and loads GCC. A post-script override also
  failed: project/test environments had already been cloned. Updated the pre
  script to apply Zig at the BuildProgram boundary, after platform tool loading
  but before dependency/test environments are cloned. Captured verbose output
  in ignored .pio/native-test.log confirms zig cc / zig c++ are now selected.
  The run then failed launching Zig's helper executable via the UNC path behind
  mapped drive T: (unable to spawn, FileNotFound). No test assertions ran. A
  local-disk compiler installation is the proposed next check, pending user
  approval before placing compiler files outside this workspace. Motion and
  gripper remain locked. User has confirmed the wheels are clear.
- 2026-11-01: User reports Joy:OK throughout full stick travel. Raw endpoints:
  left X 1..190, left Y 0..200, right X 9..199, right Y 3..199. Released raw
  readings in that order are 97, 98, 103, 102. These support keeping
  AxisMaximum=200; no configuration change is needed. Endpoint asymmetry may
  reduce peak response in some directions because current calibration stores
  centers only, not per-axis endpoints. Motion/gripper remain disabled. Native
  control tests and supervised wheels-raised readiness are next prerequisites;
  calibration persistence and physical stop/direction tests remain unverified.
- 2026-11-01: User confirms Calibration saved after the controller B-hold
  procedure. This indicates valid JoyC samples during calibration, stable and
  plausible centers for the three used axes, and a successful NVS write.
  Center values and full axis ranges have not yet been reported; persistence
  after reboot is not yet verified. Next observe centered raw values and full
  travel on both sticks with motion/gripper still locked. No commissioning
  settings were changed.
- 2026-11-01: User reports the provisioned rover upload succeeded, then confirms
  Radio:OK on both devices and Link:OK on the controller. Controller linked()
  requires fresh status, acknowledgement progress and the matching boot ID,
  providing evidence of two-way application traffic over the configured
  encrypted ESP-NOW peers. Fault 4 remains expected with motion commissioning
  disabled. No motion/gripper gate was changed. Next check Joy:OK and calibrate
  neutral sticks using B, then verify raw axis ranges before a supervised motor
  test. Sustained link reliability and disconnect/reboot safety remain untested.
- 2026-11-01: User supplied both station MACs. Added scripts/provision.ps1 and
  executed it successfully to generate ignored include/LocalConfig.h, using
  cryptographic random PMK/LMK values without printing them. The script checks
  unicast/distinct MACs, distinct/nonuniform keys, template contents and disabled
  commissioning gates; it refuses to overwrite an existing file. Provisioned
  is true; motion and gripper remain locked. Both devices must now be rebuilt
  and uploaded with this same file. Link health is not yet verified; require
  Radio:OK on both and Link:OK on JoyC, while rover fault 4 remains expected.
- 2026-11-01: Following the rover upload command targeting COM8, the user
  confirms ROVER / DISARMED and Fault:4 / I2C:OK. This verifies the full rover
  application reaches its screen and reports successful HAT detection and motor
  zero writes; it does not verify physical motor response or fault-stop timing.
  Source inspection confirms Configuration fault 4 is expected when the radio
  is unready or MotionCommissioned is false, and will remain while motion is
  locked even after successful provisioning. Both full role screens are now
  confirmed. Next obtain each displayed station MAC, provision the encrypted
  link with motion/gripper still locked, and verify JoyC accessory readings.
- 2026-11-01: After the controller upload command targeting COM7, the user
  confirms the display changed from DISPLAY CHECK to JOYC / DISARMED / 30%.
  This verifies the full controller reaches its application screen with
  M5Unified, not just the display diagnostic. The terminal did not expose a
  final upload log. Accessory readings, sustained runtime, radio pairing and
  controls are not yet verified. LocalConfig.h remains absent; motion and
  gripper commissioning remain disabled. Next upload the rover role to COM8.
- 2026-11-01: User confirmed the M5Unified diagnostic upload completed and
  DISPLAY CHECK is visible on the controller. Migrated both full firmware
  entry points from M5StickCPlus to M5Unified, pinned M5GFX to the diagnostic's
  resolved 0.2.36, and retained compatible M5.Lcd alias and button APIs after
  inspecting their declarations. Added startup serial/screen labels and explicit
  external I2C ownership handoff; left provisioning/motion/gripper gates unchanged.
  Started a build of controller and rover; the terminal reported dependency
  replacement beginning, not build completion. Editor diagnostics are clear.
  Do not start another build/upload until this command has returned. Full firmware
  upload/runtime, peripheral I2C and button behavior still need verification.
- 2026-11-01: Legacy M5StickCPlus displaycheck reproduces RTC watchdog resets
  without reaching the setup marker, while the serial-only baseline is stable.
  Inspected legacy global constructors: display/buttons perform GPIO operations
  before setup. The configured display control pins (CS 5, DC 23, reset 18)
  match the original PLUS; no incorrect pin mapping was established. The exact
  cause of the pre-setup failure is still unconfirmed.
  Changed only `displaycheck` to pinned M5Unified 0.2.7 and its Display API as a
  controlled alternative-library test, retaining the shared ESP32 platform and
  board definition. Controller/rover firmware dependencies remain unchanged until
  this test succeeds. No watchdog disabling, flash erase or motor commands.
  Next upload displaycheck to COM7 and capture UART plus screen behavior.
- 2026-11-01: User captured stable serial-only BOOTCHECK heartbeats on JoyC
  COM7, with uptime increasing from 79022 to 85022 ms. The shared ESP32/core
  boot path works for that minimal image; this does not prove the full
  application's startup or hardware initialization. Added `displaycheck`,
  reusing the diagnostic source with the pinned M5StickCPlus library and
  flushed serial markers before/after M5.begin and after drawing a blue screen.
  It does not initialize accessory I2C or ESP-NOW or issue motor/servo commands.
  The original `bootcheck` remains unchanged in behavior. Next flash only JoyC
  with `-e displaycheck --upload-port COM7` and capture startup output plus the
  screen result. If no setup marker appears, investigate pre-setup/library
  initialization; if M5.begin hangs, isolate its power/display initialization;
  if displaycheck works, instrument the controller's remaining startup stages.
- 2026-11-01: JoyC serial output confirms repeated RTCWDT_RTC_RESET (0x10)
  after ROM loads the second-stage bootloader and jumps to entry 0x400805e4.
  No M5/application startup messages were captured. This establishes a boot
  reset loop, not a pairing failure, but does not locate the reset within the
  bootloader/core/application startup path. Added a temporary `bootcheck`
  environment with serial-only setup/heartbeat, no M5 library, accessory I2C,
  radio, display or motor commands. Run on JoyC COM7 only; leave rover unchanged.
  Stable BOOTCHECK output isolates the problem to application startup/dependencies;
  the same reset loop points to the shared boot/core/flash/hardware path.
  Build and on-device results are pending. This temporarily replaces controller
  firmware; restore the controller environment after diagnosis. No flash erase
  or changes to flash timing, watchdogs or partition layout have been requested.
- 2026-11-01: User reports both screens are blank after reported flashing.
  Runtime boot/display checks have therefore not passed; pairing and all motion
  commissioning are paused. Source inspection confirms M5.begin() enables AXP192
  display power and initializes the LCD, while HAT I2C uses the other bus.
  These code checks do not prove physical startup or hardware compatibility.
  Next collect JoyC serial diagnostics on COM7 at 115200 baud with DTR/RTS low;
  distinguish a running loop, startup stall, reset loop or no serial output
  before changing firmware or reflashing. No root cause is confirmed yet.
- 2026-11-01: User reports rover flashing appears successful on the identified
  COM8 device. Both firmware roles are now reported flashed, but runtime role
  screens and accessory health have not yet been confirmed. Next collect JoyC
  and rover STA MAC addresses from their displays, plus Joy and I2C status.
  Missing local provisioning and rover configuration fault 4 are expected with
  default settings; no motion should be enabled at this stage.
- 2026-11-01: User reports JoyC appears flashed and has reconnected the rover;
  both boards are now USB-connected. Controller `firmware.bin` and `firmware.elf`
  are present, confirming build artifacts were generated after the fixes.
  Upload success and running JOYC screen still require confirmation. Identify
  each COM port before the next upload and pass the rover port explicitly;
  do not rely on automatic port selection with both devices connected.
- 2026-11-01: User's rebuild passed the previously missing Arduino pin header,
  then failed because Hardware/RadioLink could not resolve ProjectConfig.h and
  M5StickCPlus's display `min` macro expanded the controller's std::min call.
  Added `-I include` to shared firmware build flags and parenthesized both
  standard min/max calls in calibration. Compiler confirmation is pending the
  next controller upload from the user's visible terminal. No upload succeeded
  in the supplied build log.
- 2026-11-01: After the framework repair, the user's process listing showed no
  running PlatformIO processes. Verified that
  `.pio-core/packages/framework-arduinoespressif32/variants/m5stick_c/pins_arduino.h`
  now exists and contains the board pin definitions. The previously missing
  header is restored; a successful controller rebuild/upload is still pending.
  Use the user's visible PowerShell terminal for execution until terminal-tool
  reliability is restored; do not launch competing background builds.
- 2026-11-01: Workspace confirmed empty. PlatformIO and native C++ compiler not
  on terminal PATH; locating tool-managed installations before selecting tests.
- 2026-11-01: Created project-local `.venv` with uv/Python 3.12.13 and installed
  PlatformIO 6.1.18. Native test invocation reached PlatformIO but failed because
  the requested MinGW toolchain was unavailable for windows_amd64. Switched to
  project-local ziglang 0.13.0 with a native SCons toolchain hook.
- 2026-11-01: Subsequent terminal calls returned no output, including simple
  working-directory diagnostics. No successful test/build execution can be
  claimed. Both firmware builds and the native suite must be rerun when terminal
  execution is restored. No uploads were performed.
- 2026-11-01: Editor diagnostic checks reported no errors in source, includes,
  portable tests and toolchain hook. This is not compiler verification.
- Hardware checks, timed stop measurements, LCD inspection and radio/NVS/I2C
  fault injection remain pending. Portable tests currently do not fake I2C;
  hardware-adapter fault injection is an outstanding verification item.
- 2026-11-01: User connected both PLUS devices by USB and requested flashing.
  PlatformIO device enumeration and Windows serial-port enumeration both returned
  no tool output; terminal inspection reported no active terminal instance.
  The project package directory is empty and the build directory contains only
  its checksum, not firmware images. Uploads were not attempted. Restore terminal
  access, enumerate ports and confirm which device has JoyC versus RoverC before
  building/testing and uploading the nonmoving diagnostic firmware. Local peer
  configuration is still absent; obtain station MACs from diagnostics before
  provisioning encrypted communication and commissioning motion.
- 2026-11-01: User's controller upload reached compilation but failed on missing
  `pins_arduino.h`. Confirmed board manifest selects `m5stick_c` correctly;
  installed framework lacks the entire `variants` directory. Framework metadata
  identifies `3.20017.241212+sha.dcc1105b`. Started a force reinstall of that exact
  package with `--no-save`; no board or source workaround was applied. Terminal
  output showed package removal starting, but completion is not yet confirmed.
  Next gate: restore `variants/m5stick_c/pins_arduino.h`, successfully build the
  controller, then upload to the sole attached JoyC board. No successful flash
  has been confirmed. Use absolute project/Python paths: the tool terminal was
  outside the workspace when a relative-path command failed.

## Decision log

- 2026-11-01: User confirmed workspace location, PlatformIO, ESP-NOW and crab
  steering. Created this durable plan before firmware implementation.
- Hardware-dependent commissioning must be explicit and fail closed by default.
- 2026-11-01: Implementation added in this directory: controller/rover entry
  points, checked HAT adapter, encrypted radio mailbox, portable safety/protocol
  logic and ten native tests. Documented operation in README.md.
- Pinned ESP32 platform 6.10.0 and M5StickCPlus 0.1.1 pending compile confirmation.
  Native portable code uses C++14; ESP32 firmware uses C++17.
- Added independent `Provisioned`, `MotionCommissioned` and
  `GripperCommissioned` switches, all false in the template. Missing local
  configuration builds diagnostics rather than enabling motion.
- Official newer JoyC source confirms axis order and button masks, but assumes
  a different raw range from the product page. Range is configurable (200/255)
  and physical orientation/range checks remain mandatory.
- Local calibration uses a two-second B hold, 50 stable samples and NVS storage.
- I2C/configuration faults remain latched across timeout and disarm packets;
  recovery requires repair and reboot. Local stop/link faults require rearming.
- 2026-11-08: PlatformIO core, build and library dependency directories live on
  local disk (`%LOCALAPPDATA%/M5RoverTools`) because small-file I/O on the network
  share dominated build time. Source, logs and the Zig cache stay in the workspace.
  Native tests (~14 s) were left on Zig.

## References

- [RoverC Pro documentation](https://docs.m5stack.com/en/hat/hat_roverc_pro)
- [JoyC documentation](https://docs.m5stack.com/en/hat/hat_joyc)
- [Official RoverC implementation and PLUS examples](https://github.com/m5stack/M5-RoverC)
- [Official JoyC example](https://github.com/m5stack/M5-ProductExampleCodes/tree/master/Hat/JoyC/Arduino/JoyC)
- [PlatformIO board](https://docs.platformio.org/en/latest/boards/espressif32/m5stick-c.html)
- [ESP-NOW documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html)
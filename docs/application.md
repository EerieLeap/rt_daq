# The application

rt_daq is the firmware of the EerieLeap processing module: a data acquisition unit that samples
analog and digital inputs, listens to and sends on CAN, logs to an SD card and is managed over
CAN and Bluetooth. Almost all of its behaviour is rt_core (`modules/eerie_leap_rt_core`), whose
documentation is at [`modules/eerie_leap_rt_core/docs/README.md`](../modules/eerie_leap_rt_core/docs/README.md).
This page covers what the application adds on top: its wiring, its own controllers and its
build configuration.

## Layout

| Path | Contents |
| --- | --- |
| `app/src/main.cpp` | Creates and starts the rt_core controllers in order (see below). |
| `app/src/controllers/logging_controller.*` | Starts and stops logging on a CDMP command, restarts it for a new configuration, stops a failed session; shows a `log` status on the display. |
| `app/src/shell/` | The `logging` bench commands (`APP_LOGGING_SHELL`, see the logging controller below). |
| `app/src/controllers/display_controller.*`, `app/src/views/animations/` | Idle animations and a status bar on the character framebuffer. |
| `app/prj.conf` | Product identity, which rt_core systems are built, heap, stacks, filesystems, CAN FD. |
| `app/boards/<board>.conf`, `.overlay` | Per-board options and device tree; see [boards.md](boards.md). |
| `app/patches/zephyr/` | Patches applied to the Zephyr checkout at configure time, in addition to rt_core's. |
| `app/CMakeLists.txt` | Applies the patches, registers rt_core and its nested modules, and keeps the zcbor generator helper. |
| `app/VERSION` | Zephyr application version (1.0.0). |
| `boards/` | The custom board definitions, `esp32s3_pm_hw_02` and `devebox_stm32h743_hw_20`. |

## Boot sequence

`main()` follows the order rt_core's documentation describes
([boot sequence](../modules/eerie_leap_rt_core/docs/README.md#boot-sequence)); the parts that
are this application's choices:

1. Device tree features requested: internal FS, SD FS, GPIO, ADC, display, CAN.
2. The display is initialized first so the idle animation runs while the rest boots. The display
   controller's work queue runs at priority 6, below sensor processing.
3. The SD service is created when the board has a card slot. Its presence monitor is not started:
   on the SPI card interface the status polling interrupts logging.
4. The configuration work queue is 8192 bytes at priority 5: configuration applies restart sensor
   processing and the log writer there, which writes the MDF header (5728 bytes measured at 6144).
5. System, ADC, GPIO, CAN, sensors, logging (only with an SD card), management, CAN scheduler.
   The sensor processing service is registered with the ADC controller (paused during
   calibration) and with the CAN controller (restarted on CAN reconfiguration), as is the CAN
   scheduler. The log writer is registered with the sensors controller (stopped and started
   again around a sensors or ADC restart).
6. Sensor processing and the CAN scheduler are started last; `main` then sleeps.

`main.cpp` also carries three helpers, `SetupCanbusConfiguration`, `SetupTestSensors` and
`SetupLoggingConfiguration`, that write a test configuration through the managers. The first two
are commented out at their controller's `Initialize()` call, the third runs under
`APP_SETUP_LOGGING_CONFIGURATION` (see the logging controller below). Each persists its
configuration on the unit, so restore the previous configuration afterwards.

## Logging controller

Creates the logging configuration manager (type 5 in the SMP `config` group) and the
`LogWriterService`, then registers a handler for the CDMP `LOGGING` command (0x20, one byte:
0x01 start, 0x00 stop). A broadcast of that command starts or stops logging on every unit on the
bus.

The writer's logger factory is the controller's: every start, including the ones rt_core makes
on its own, builds a new MDF4 logger from the current sensors and logging configurations and the
metadata this application supplies for the file header: device name `CONFIG_BOARD`, tool
`rt_daq` at the software version from `prj.conf` (`EERIE_LEAP_SW_VERSION_*`, the same one the
packed `sw_version` property carries; `app/VERSION` is not used), `EERIE_LEAP_PRODUCT_ID`, the
CDMP device ID (0 until the unit has claimed one), the packed hardware and software versions,
and the CRC of the stored sensors configuration as `sensors_configuration_hash` (0 when that
configuration was rejected at boot and defaults run instead). What the logger writes from them is
in rt_core's [logging.md](../modules/eerie_leap_rt_core/docs/logging.md).

A configuration stored while logging runs takes effect at once. `main()` registers the writer
with the sensors controller, so a sensors or ADC apply stops it before processing restarts and
starts it again on the new sensors: one file ends, the next begins. What is registered is the
controller's wrapper of the writer (`GetDependentService()`), which takes the controller's lock
and starts the writer again only when logging is still wanted: a stop from the CDMP command or
the shell during the restart, or a failed session, is not undone. A logging configuration
(interval, size limit, sensor selection) restarts a running writer from the manager's updated
handler, on the configuration work queue. Starts, stops and these restarts, from the CDMP
command, the shell, the autostart hook or the work queue, run one at a time.

The controller creates rt_core's `LoggingEventBus` and follows the writer's `StatusUpdated`
events on the configuration work queue. The display shows `log` while the last event said active,
so it follows starts, stops, restarts and failures alike. A session that failed (write failed, no
space at a rotation, rotation failed) is stopped there, off the writer thread, which finalizes
what the card still takes and closes the file; the next start, from any source, begins a new
file.

Development aids (`app/Kconfig`, all off by default, bench use only):
`APP_LOGGING_AUTOSTART_DELAY_MS` starts logging that many milliseconds after boot without a CDMP
command; `APP_SETUP_LOGGING_CONFIGURATION` persists the test logging configuration
(`SetupLoggingConfiguration`) at boot; `APP_LOGGING_SHELL` adds the `logging` commands to the
Zephyr shell:

| Command | Effect |
| --- | --- |
| `logging start`, `logging stop` | What the CDMP command does. `start` writes the MDF header on the shell thread, so use a `SHELL_STACK_SIZE` of 4096 or more. |
| `logging status` | Running and failed flags and the writer's counters. |
| `logging config <interval_ms> <max_log_size_mb>` | Stores those two fields of the logging configuration, keeping its sensor entries, and prints the previous values to restore them with; a running writer restarts with it. |
| `logging reapply-sensors` | Applies the stored sensors configuration again: its stored CBOR (`GetCborConfiguration()`) through `ApplyCborConfiguration()`, the path an SMP apply takes, so the restart of processing and of a running writer can be exercised without an SMP client. |

The last two run on the configuration work queue, as an SMP apply does.

## Display controller

Runs one of four idle animations (wave lines, dot path, particles, tapered wave) at 30 frames
per second while no status is shown. `AddStatus()`/`RemoveStatus()` replace the animation with a
right-aligned status bar of short words, alternate ones inverted; when the last status is removed
the animation resumes. `PrintStringLine()` clears the screen and prints one line.

## Build configuration (`app/prj.conf`)

| Area | Setting | Why |
| --- | --- | --- |
| Product identity | `PRODUCT_FAMILY_PROCESSING_MODULE`, `PRODUCT_ID=50177` (0xC401), `FEATURES_CANBUS`, hardware and software versions | Reported over BLE and the SMP `device` group; see rt_core's [system.md](../modules/eerie_leap_rt_core/docs/system.md). |
| Language | C++23, full libc++, exceptions and RTTI | Required by rt_core. |
| Logging | `LOG_MODE_IMMEDIATE`, early console, thread analyzer, runtime stats | Development build defaults; immediate mode costs time on the logging thread. |
| Memory | 64 KiB kernel heap, libc malloc, dynamic thread stacks | Configuration objects and Lua states live on the external heap where the board has one. |
| Filesystems | littlefs on internal flash; FAT on the SD card is enabled per board | `config/` on littlefs, logs and scripts on the card. |
| CAN | `CAN_FD_MODE` | The PM board's MCP2518FD channel runs CAN FD. |
| Threads | SD monitor 3072, log writer 6144, sensor processing priority 5 | Priority 5 puts sampling above the display, CAN scheduler and calibration threads; on the PM board this cut the 50 ms group's period jitter from milliseconds to tens of microseconds. The log writer peaks at 3648 bytes during a file rotation (about 1950 otherwise) on the PM board. |
| rt_core systems | Every subsystem, the ADC, CAN, CAN COM (device type `LOGGER`), script, sensor, system and logging domains, and the system, management, ADC, CAN and sensors controllers | Bluetooth is not enabled in this application. |

## Patches

`app/CMakeLists.txt` applies every `.patch` under `modules/eerie_leap_rt_core/patches/zephyr`
and `.../patches/modules/hal_espressif` to the west checkouts, then `app/patches/zephyr` (an FS
log backend header). A patch already applied is skipped, one that fails to apply stops the
configure step. After `west update` the patches may no longer apply cleanly; re-run the
configure step and fix the ones it reports.

## CBOR code generation

The `generate_zcbor_configuration` function in `app/CMakeLists.txt` runs `zcbor code` on one of
rt_core's CDDL schemas and writes the generated sources to
`src/configuration/cbor/generated/<name>/` (gitignored). The calls are commented out; uncomment
the one for a changed schema, build once, and copy the finished files into rt_core's
`src/configuration/cbor/<name>/`. `zcbor` is installed in the dev container.

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
| `app/src/controllers/logging_controller.*` | Starts and stops logging on a CDMP command; shows a `log` status on the display. |
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
4. The configuration work queue is 6144 bytes at priority 5.
5. System, ADC, GPIO, CAN, sensors, logging (only with an SD card), management, CAN scheduler.
   The sensor processing service is registered with the ADC controller (paused during
   calibration) and with the CAN controller (restarted on CAN reconfiguration), as is the CAN
   scheduler.
6. Sensor processing and the CAN scheduler are started last; `main` then sleeps.

`main.cpp` also carries three commented-out helpers, `SetupCanbusConfiguration`,
`SetupTestSensors` and `SetupLoggingConfiguration`, that write a test configuration through the
managers. Passing one of them to a controller's `Initialize()` persists that configuration on the
unit, so restore the previous configuration afterwards.

## Logging controller

Creates the logging configuration manager (type 5 in the SMP `config` group), the
`LogWriterService` and an MDF4 logger built from the current sensors configuration, then
registers a handler for the CDMP `LOGGING` command (0x20, one byte: 0x01 start, 0x00 stop). A
broadcast of that command starts or stops logging on every unit on the bus. While logging runs
the display shows `log` in its status bar. The logger is built once at initialization; a changed
sensors configuration needs a restart to be reflected in new log files.

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
| Threads | SD monitor 3072, log writer 3072, sensor processing priority 5 | Priority 5 puts sampling above the display, CAN scheduler and calibration threads; on the PM board this cut the 50 ms group's period jitter from milliseconds to tens of microseconds. |
| rt_core systems | Every subsystem, the ADC, CAN, CAN COM (device type `LOGGER`), script, sensor, system and logging domains, and the system, management, ADC, CAN and sensors controllers | Bluetooth is not enabled in this application. |
| Log files | `LOG_METADATA_FILE_VERSION=1`, `LOG_DATA_FILE_VERSION=1` | Version fields of the simple logger's files. |

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

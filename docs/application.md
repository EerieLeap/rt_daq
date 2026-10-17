# The application

rt_daq is the firmware of the EerieLeap processing module: a data acquisition unit that samples
analog and digital inputs, listens to and sends on CAN, logs to an SD card and is managed over
CAN and Bluetooth. Almost all of its behaviour is rt_core (`modules/eerie_leap_rt_core`), whose
documentation is at [`modules/eerie_leap_rt_core/docs/README.md`](../modules/eerie_leap_rt_core/docs/README.md).
This page covers what the application adds on top: its wiring, its event bus, how it uses rt_core's
logging control, its display controller and its build configuration.

## Layout

| Path | Contents |
| --- | --- |
| `app/src/main.cpp` | Creates and starts the rt_core controllers in order (see below). |
| `app/src/event_bus/` | The application bus and `InitializeEventChannels()`: `logging_events` and `sensor_events` on one bus thread, and the channel registry entries for `Sensors` and `Logging`. |
| `app/src/shell/` | The `logging` and `sensor` bench commands (`APP_LOGGING_SHELL`, `APP_SENSOR_SHELL`, see [Development aids](#development-aids)). |
| `app/src/controllers/display_controller.*`, `app/src/views/animations/` | Idle animations and a status bar on the character framebuffer; it follows the logging status (see the display controller below). |
| `app/bench.conf` | The bench build's Kconfig fragment: shell, SMP over the console, stack reports, bench commands (see [Development aids](#development-aids)). |
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

1. Event channels (`InitializeEventChannels()`): the application bus is constructed and `Sensors`
   and `Logging` are registered with `EventChannelRegistry`. Channels drop every publish until a
   bus carries them, so this comes before anything publishes or subscribes.
2. Device tree features requested: internal FS, SD FS, GPIO, ADC, display, CAN.
3. The display is initialized first so the idle animation runs while the rest boots. The display
   controller's work queue runs at priority 6, below sensor processing.
4. The SD service is created when the board has a card slot. Its presence monitor is not started:
   on the SPI card interface the status polling interrupts logging.
5. The configuration work queue is 8192 bytes at priority 5. Every start and stop of the log
   writer runs there, whatever requested it, besides the configuration applies, which restart
   sensor processing and, before `LoggingController::Start()` or for a start requested after an
   update, the writer; a start writes the MDF header (5728 bytes measured at 6144). The
   display controller starts following the logging status on it right away.
6. System, ADC, GPIO, CAN, sensors, the sensor events publisher, logging (only with an SD file
   system; its `Start()` follows the setup hook, so what the unit stores while it starts stops no
   logging), management, CAN scheduler. The sensor processing service is registered with the ADC
   controller (paused during calibration) and with the CAN controller (restarted on CAN
   reconfiguration), as is the CAN scheduler. The log writer is registered with the sensors
   controller (stopped around a sensors or ADC restart, and started again on the new generation
   while logging is still wanted), and only after the
   logging controller initialized: a controller that failed is dropped, and nothing is wired to it.
7. Sensor processing and the CAN scheduler are started last. The autostart, when configured,
   requests a logging start; `main` then sleeps.

`main.cpp` also carries three helpers, `SetupCanbusConfiguration`, `SetupTestSensors` and
`SetupLoggingConfiguration`, that write a test configuration through the managers. The first two
are commented out at their controller's `Initialize()` call, the third runs under
`APP_SETUP_LOGGING_CONFIGURATION` (see [Logging](#logging)) and needs the test sensors, so it logs
an error and stores nothing on a unit without them. Each persists its configuration on the unit,
so restore the previous configuration afterwards.

## Logging

rt_daq builds rt_core's `LoggingController` in the writer role (`EERIE_LEAP_CONTROLLER_LOGGING`
with `EERIE_LEAP_DOMAIN_LOGGING`), with the tool ID `rt_daq`, and only when the board has an SD
file system. How it works is in rt_core's
[logging.md](../modules/eerie_leap_rt_core/docs/logging.md): [Control](../modules/eerie_leap_rt_core/docs/logging.md#control),
[Supervision](../modules/eerie_leap_rt_core/docs/logging.md#supervision),
[Triggers](../modules/eerie_leap_rt_core/docs/logging.md#triggers) and
[Wiring](../modules/eerie_leap_rt_core/docs/logging.md#wiring).

The controller builds:

- the logging configuration (type 5 in the SMP `config` group, `config/logging_config.cbor`, the
  file rt_daq always used, so a stored logging configuration still loads);
- the triggers' configuration (type 8 in the same group, `config/logging_control_config.cbor` on
  the internal file system), created with no triggers at the first boot of a board with an SD file
  system;
- the log writer and its supervisor;
- with the CAN COM service, the handler of the CDMP `LOGGING` command (0x20, one byte: 0x01 start,
  0x00 stop; a broadcast reaches every unit on the bus) and the exporter that shares the status.
  The handler answers `SUCCESS` once the request is accepted; the outcome arrives as the shared
  status.

The application bus carries `logging_events` and `sensor_events` on one thread. rt_core's
`SensorEventBus` is not created, so the unit runs a single bus thread. The sensor events publisher
sends only the sensors a subscriber accepts, which on this unit are the ones triggers name.

### Inputs

Every input is a request through `GetControl()`. The latest request wins, and every start and stop
runs on the configuration work queue, one at a time.

| Input | Source |
| --- | --- |
| Triggers | Stored configuration; see [Triggers](#triggers). |
| The CDMP `LOGGING` command | Another unit, such as the gauge's button. |
| The `logging` shell commands | `APP_LOGGING_SHELL`. |
| The autostart | `APP_LOGGING_AUTOSTART_DELAY_MS`. |

The supervisor starts the writer with a logger it builds each time from the current sensors and
logging configurations. It sets the file header's metadata itself, from the tool ID rt_daq passes
(`rt_daq`): device name `CONFIG_BOARD`, tool `rt_daq` at the software version from `prj.conf`
(`EERIE_LEAP_SW_VERSION_*`, the same one the packed `sw_version` property carries; `app/VERSION` is
not used), `EERIE_LEAP_PRODUCT_ID`, the CDMP device ID (0 until the unit has claimed one), the
packed hardware and software versions, and the CRC of the stored sensors configuration as
`sensors_configuration_hash` (0 when that configuration was rejected at boot and defaults run
instead). What the logger writes from them is in rt_core's
[logging.md](../modules/eerie_leap_rt_core/docs/logging.md).

A configuration stored on a unit stops logging on every unit, whatever its type: sensors, ADC, CAN,
logging, logging triggers, system, UI or display, written over SMP or by a shell command, on this
unit or on another. Here the writer stops for good: the file is finalized, the wanted state is
cleared, and neither the restart after a logging configuration nor the sensors restart starts it
again, so no new file opens. This unit broadcasts the `LOGGING` stop, so every other unit that logs
stops too, and it stops when another unit broadcasts it. The supervisor logs the stop, and the
sender logs "Logging stop command failed." when no other unit is online. Logging starts again with
a new start request (a trigger's change, the `LOGGING` command, the shell). A start requested right
after the update wins as the later request, and then the restart does finalize the file and open the
next on the new sensors: `main()` registers the supervisor's dependent (`GetDependentService()`)
with the sensors controller, and it starts the writer again only while logging is still wanted.

What this unit stores while it starts is no update: the first-boot defaults and the test
configuration (`SetupLoggingConfiguration()`) come before `logging_controller->Start()`, which
`main()` calls before `ManagementController`, so a reboot of this unit does not stop the logging of
another. The rt_daq bench shell can store earlier, on its own thread, and that stops nothing
either. Every other successful store counts, one of unchanged bytes too; a rejected write does
not. A stop during a restart, or a failed session, is not undone. A session that failed (write
failed, no space at a rotation, rotation failed) is stopped by the supervisor on the configuration
work queue, which finalizes what the card still takes and closes the file; the next start, from any
source, begins a new file. The rule and its exceptions are in rt_core's
[logging.md](../modules/eerie_leap_rt_core/docs/logging.md#configuration-updates).

Without an SD file system (the DevKitC and the simulators), or after a failed `Initialize()`, the
unit has no logging, no triggers, no `LOGGING` handler and no shared status. It contributes no
record, so other units see only the units that log, and `Unavailable` when none does. With a CAN
COM service it still broadcasts the `LOGGING` stop when a configuration is stored on it
(`ConfigurationUpdateLoggingStop` with a sender, built in `main()`), since its sensors and messages
feed the files of the units that log.

### Triggers

A trigger starts and stops logging from a sensor's value. On rt_daq it takes two stored
configurations that agree:

1. An indicator sensor in the sensors configuration (type 4). Its `debounce_ms` and `input_mode`
   condition the input (see rt_core's
   [sensors.md](../modules/eerie_leap_rt_core/docs/sensors.md#indicator-conditioning)).
2. A trigger in the logging control configuration (type 8): channel `Sensors` (1), event
   `DataUpdated` (0), payload key `Value` (1) and selector key `SensorId` (0) with the sensor's id
   as the selector value.

Both are written over SMP, in the `config` group (types 4 and 8), over CAN or through a unit that
acts as access point. On a bench without a CAN adapter the gauge's `tools/smp_can_client.py` writes
them from YAML descriptions over the console of a bench build (see [Development aids](#development-aids)).

Behaviour:

- A change of the value from 0 to 1 requests a start and from 1 to 0 a stop.
- At boot a first value of 1 requests a start, so a switch that is on at boot starts logging. After
  a configuration update the first value is a baseline: nothing starts until the next 0 to 1, so
  a switch that is on stays on and starts nothing, and storing the triggers again no longer starts
  logging either (see rt_core's
  [logging.md](../modules/eerie_leap_rt_core/docs/logging.md#triggers)).
- A trigger only requests. A stop from the shell or from another unit stands until the switch
  moves.
- A trigger whose sensor does not exist is logged when the triggers are built, and acts once a
  sensor with that id appears; the first value it reads then is a baseline if an update came
  before. A sensors update that removes, disables or renames the trigger's sensor withdraws the
  trigger: it forgets its value, and the sensor back under that id is a baseline.

The sensor events publisher sends only the bound sensors on `sensor_events`.

| Example | Sensor | Trigger |
| --- | --- | --- |
| A switch on a digital input | `logging_switch`: `PHYSICAL_INDICATOR`, channel 1, no sampling rate (every edge), `debounce_ms` 30, `Level` | `Sensors` / `DataUpdated` / `Value`, selector `logging_switch` |
| Log while the ignition is on | `ignition`: `CANBUS_INDICATOR`, connection string `0/0x3B0/IgnitionOn`, no debounce | The same, selector `ignition` |

The CAN signal of the ignition example must be defined on bus 0 in the CAN configuration: a
client's sensors apply is checked against it. An input that reads 0 while the switch is on, such as
a switch that pulls a pulled-up input to ground, is inverted in the sensor with the expression
`x == 0`; the debounce applies to the expression's result.

### Status

The `StatusUpdated` events the display follows also go to the other units on the CDMP network,
where the gauge shows them: the controller owns rt_core's
`NetworkStateExporter<LoggingNetworkState>`, created on the COM service right after the `LOGGING`
handler, so a command sent once the status is shared always finds a handler. The shared status is
inactive (`Stopped`) until the first event, keeps a failure's reason through the stop that follows
it, and is withdrawn with the controller. Without the CAN COM service the unit neither takes the
command nor shares its status. The record and how units combine it are in rt_core's
[logging.md](../modules/eerie_leap_rt_core/docs/logging.md#status-on-other-units).

### Development aids

`app/Kconfig`, all off by default, bench use only. `APP_LOGGING_AUTOSTART_DELAY_MS` requests a
logging start that many milliseconds after boot, standing in for the CDMP command or a trigger;
`APP_SETUP_LOGGING_CONFIGURATION` persists the test logging configuration
(`SetupLoggingConfiguration`) at boot; `APP_LOGGING_SHELL` adds the `logging` commands to the Zephyr
shell:

| Command | Effect |
| --- | --- |
| `logging start [target_uid]`, `logging stop [target_uid]` | Queue a start or a stop, as the CDMP command does: for every unit that can log without a target (this unit's writer and one broadcast), for the unit with that CDMP UID with one (this unit's own UID means this writer only). `logging status` shows the outcome. No file work runs on the shell thread. |
| `logging status` | Whether logging is active (`IsActive()`), the writer's running and failed flags and its counters. |
| `logging config <interval_ms> <max_log_size_mb>` | Stores those two fields of the logging configuration, keeping its sensor entries, and prints the previous values to restore them with. A configuration update: logging stops on every unit. |
| `logging reapply-sensors` | Applies the stored sensors configuration again: its stored CBOR (`GetCborConfiguration()`) through `ApplyCborConfiguration()`, the path an SMP apply takes, so a configuration update, the restart of processing and the stop of logging that comes with it can be exercised without an SMP client. |
| `logging unmount-sd` | A bench fault: unmounts the card under the writer, as a pulled card does at the file system layer. It runs on the shell thread; reboot to mount the card again. |

`logging config` and `logging reapply-sensors` run on the configuration work queue, as an SMP apply does, and both are configuration updates: logging stops on every unit until it is started again.

`APP_SENSOR_SHELL` adds the `sensor` commands, which stand in for a sensor's source so a bench can drive
triggers and the CAN messages that carry a sensor without wiring an input:

| Command | Effect |
| --- | --- |
| `sensor set <id> <value>` | Commits a processed reading with that value to the readings frame, as the sensor's source would. The sensor events publisher (triggers), the log writer and the CAN scheduler see it. An indicator stores 0 or 1. |
| `sensor get <id>` | Prints the sensor's latest reading: value, status, error and age. |

`sensor set` runs on the configuration work queue, behind any queued sensors apply, processing restart or
CAN reconfiguration, so it never lands inside one. It refuses only while processing is stopped (before boot
starts it, or after a failed start or restart) or paused (an ADC calibration), printing "Sensor processing
is stopped or paused; set the value again once it runs." Every processing restart drops a value set before
it. A restart follows a sensors or ADC configuration write, `logging reapply-sensors` and a CAN configuration
write; the CAN one comes 250 ms after its answer (`EERIE_LEAP_CONTROLLER_CANBUS_RECONFIGURE_DELAY_MS`). Set
the value again after it.

The command bypasses the sensor's own expression and conditioning (`debounce_ms`, `Toggle`), and does not
evaluate the sensors that depend on it when it commits. A virtual sensor that polls it reads the value at
its next period. The sensor's own source replaces the value at its next reading, which a polled sensor
delivers within its sampling period. No enabled sensor kind is without a source, so use one whose source
never writes it: a `CANBUS_INDICATOR` on a signal no other unit sends. A unit does not hear its own frames,
not even on native_sim, so a signal in a message this unit sends is written by nothing but the bench, and
the value travels in that message to the other units. The CAN scheduler fills a sent signal with the value
of the sensor whose id is the signal's name, so the sensor must be named after the signal.

The bench units store the CAN configuration that `SetupCanbusConfiguration()` in `main.cpp` wrote (the call
is commented out now). In it rt_daq sends the signals `sensor_0` to `sensor_2` on frames 0x64 to 0x66 every
20 ms, and the gauge receives them. With that configuration, a sensor `sensor_0` of type `CANBUS_INDICATOR`
with connection string `0/100/sensor_0` (bus 0, frame 100) is written by `sensor set` alone, and the
gauge's remote path sees it. A unit with another CAN configuration needs such a message first: check with
`config-get canbus` and add one with `config-set canbus`. A `VIRTUAL_INDICATOR` that reads such a sensor and
has `debounce_ms` or `Toggle` set exercises the conditioning on the polled value. The option excludes
Zephyr's `SENSOR_SHELL`, which registers a `sensor` command too.

`app/bench.conf` is the Kconfig fragment for a bench build; it is not for a shipped build:

```shell
west build -d build_bench -b esp32s3_pm_hw_02/esp32s3/procpu app -- -DEXTRA_CONF_FILE=bench.conf
```

`EXTRA_CONF_FILE` stays in the CMake cache: build the bench image in a directory of its own, or leave it
with `-p always`, so a later build of the same directory is not a bench build by accident.

| Setting | Why |
| --- | --- |
| `SHELL`, `SHELL_STACK_SIZE=4096` | The Zephyr shell on the console, where the units' USB-Serial-JTAG appears as `/dev/ttyACM*`. The bench commands run C++ at -O0 on this thread. |
| `MCUMGR_TRANSPORT_SHELL`, `BASE64`, `MCUMGR_TRANSPORT_SHELL_RX_BUF_COUNT=16`, `MCUMGR_TRANSPORT_SHELL_INPUT_TIMEOUT` | SMP over the console, which the gauge's `tools/smp_can_client.py --serial` speaks. A configuration request is up to 12 lines of 127 bytes that wait for the shell thread at the lowest priority; the default 2 buffers lose lines. The timeout frees the shell after a request cut off mid-frame. |
| `SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN` | The shell's polling API never hands SMP frames to MCUmgr. It is the default on the S3; native_sim needs it set. |
| `SHELL_LOG_BACKEND=n`, `LOG_BACKEND_UART=y`, `LOG_RUNTIME_FILTERING=n` | Logs keep the shipped path: `uart_poll_out` with no lock. The shell log backend would serialize every thread that logs on the shell's mutex, so the stack peaks and timings measured on the bench build would differ from a shipped one. The shell implies runtime filtering, which would cap every module at INF and drop debug logs. The client skips the log text between SMP lines. |
| `SHELL_BACKEND_SERIAL_FORCE_TX_BLOCKING_MODE` | Shell output is polled too. Interrupt-driven transmit waits for a host to read the USB-Serial-JTAG FIFO and blocks the shell thread while no host has the port open. |
| `THREAD_ANALYZER_AUTO`, `_AUTO_INTERVAL=10`, `_AUTO_STACK_SIZE=3072`, `_PRINT_THREAD_PRIORITY` | A report of every thread's stack peak every 10 s, through the log. `prj.conf` already enables the analyzer; it has no effect on native_sim. |
| `STACK_SENTINEL` | The S3 has no stack guard; a sentinel stops the unit with the thread's name when a stack overflows. |
| `APP_LOGGING_SHELL`, `APP_SENSOR_SHELL` | The bench commands above. |

The fragment costs 14376 B of internal RAM (`dram0_0_seg`, 48.2 % to 51.8 %) and 83552 B of flash on the PM
board. The libc heap is the internal RAM that the image leaves (`COMMON_LIBC_MALLOC_ARENA_SIZE=-1`), so it
shrinks by about the same amount. The stack peaks of a bench build are those of the shipped logging path plus the shell thread; the
shell and the analyzer thread add stacks and a little timing.

## Display controller

Runs one of four idle animations (wave lines, dot path, particles, tapered wave) at 30 frames
per second while no status is shown. `AddStatus()`/`RemoveStatus()` replace the animation with a
right-aligned status bar of short words, alternate ones inverted; when the last status is removed
the animation resumes. `PrintStringLine()` clears the screen and prints one line.

The controller follows `logging_events` itself (`FollowLoggingStatus()`): `log` is shown while the
last `StatusUpdated` said active, so it follows starts, stops, restarts and failures alike. The
display is not thread-safe, so the status is applied on the configuration work queue, and only on
a change. A board without a display does not subscribe.

## Build configuration (`app/prj.conf`)

| Area | Setting | Why |
| --- | --- | --- |
| Product identity | `PRODUCT_ID=50177` (0xC401), hardware and software versions; the unit features follow the domains in the build (`CANBUS`, `LOGGING`) | Reported over BLE and the SMP `device` group, the features also over CDMP (`LOGGING` only while the log volume is available); see rt_core's [system.md](../modules/eerie_leap_rt_core/docs/system.md). |
| Language | C++23, full libc++, exceptions and RTTI | Required by rt_core. |
| Logging | `LOG_MODE_IMMEDIATE`, early console, thread analyzer, runtime stats | Development build defaults; immediate mode costs time on the logging thread. |
| Memory | 64 KiB kernel heap, libc malloc, dynamic thread stacks | Configuration objects and Lua states live on the external heap where the board has one. |
| Filesystems | littlefs on internal flash; FAT on the SD card is enabled per board | `config/` on littlefs, logs and scripts on the card. |
| CAN | `CAN_FD_MODE` | The PM board's MCP2518FD channel runs CAN FD. |
| Threads | SD monitor 3072, log writer 6144, sensor processing priority 5 | Priority 5 puts sampling above the display, CAN scheduler and calibration threads; on the PM board this cut the 50 ms group's period jitter from milliseconds to tens of microseconds. The log writer peaks at 3648 bytes during a file rotation (about 1950 otherwise) on the PM board. |
| rt_core systems | Every subsystem including the event bus, the ADC, CAN, CAN COM, script, sensor, system and logging domains, and the system, management, ADC, CAN, sensors and logging controllers | Bluetooth is not enabled in this application. |

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

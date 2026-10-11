# EerieLeap Real-Time

EerieLeap is an open-source sensor monitoring system built with [Zephyr RTOS](https://github.com/zephyrproject-rtos/zephyr).
rt_daq is the firmware of its processing module: it samples analog and digital inputs, reads and
sends CAN bus messages, logs to an SD card in ASAM MDF 4, and is configured over CAN and
Bluetooth.

### [Please refer to the Wiki for more information, configuration details, and usage examples](https://github.com/Pavllick/EerieLeapRT/wiki).

## Features

- Real-time sensor data collection and processing
- CAN bus data collection and streaming
- Data logging
- Custom expressions for sensor value calculation
- Lua scripts for sensor values and CAN messages custom logic

## Documentation

- [docs/](docs/README.md): building, flashing and debugging, the application's wiring and the
  supported boards.
- [rt_core docs](modules/eerie_leap_rt_core/docs/README.md): how every system works and how it is
  configured (ADC, sensors, CAN, CDMP, SMP, Bluetooth, logging, scripting, ...). The firmware's
  logic lives in the `rt_core` module, shared with the other EerieLeap units.

## Quick start

```shell
git submodule update --init --recursive
```

Open the folder in the dev container (`.devcontainer/`, or `example.docker-compose.yml` copied to
`docker-compose.yml`), then:

```shell
west build -p auto -b native_sim ./app && west build -t run
```

See [docs/getting-started.md](docs/getting-started.md) for hardware targets, flashing and debugging.

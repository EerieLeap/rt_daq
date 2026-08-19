# EerieLeap Real-Time

EerieLeap is an open-source sensor monitoring system built with [Zephyr RTOS](https://github.com/zephyrproject-rtos/zephyr). It provides a robust platform for reading, processing, and managing sensor data, supporting digital, analog, and CAN Bus data inputs.

### [Please refer to the Wiki for more information, configuration details, and usage examples](https://github.com/Pavllick/EerieLeapRT/wiki).

## Features

Core features of the system include:

- Real-time sensor data collection and processing
- CAN Bus data collection and streaming
- Data logging
- Custom expressions for sensor value calculation
- Lua scripts for sensor values and CAN messages custom logic

EerieLeap is highly configurable system, allowing not just collection and streaming of data, but also custom logic for sensor values and CAN messages. Only imagination limits the possibilities.

## Architecture

EerieLeap implements a layered architecture designed for real-time sensor monitoring with clear separation of concerns:

### System Overview

The system follows a pipeline architecture where sensor data flows from hardware inputs through processing stages to independent outputs:

**Hardware Input → SensorsProcessingService → [LogWriterService | Controllers/Views]**

**Hardware Output ← CanbusSchedulerService**

### Core Layers

**Hardware Abstraction Layer**
- Zephyr device drivers provide standardized interfaces to hardware peripherals
- Device tree configuration defines available sensors, buses, and interfaces
- Subsystem adapters (`app/src/subsys`) wrap third-party APIs with project-specific abstractions
- Hardware-agnostic design allows the same codebase to run across different platforms

**Domain Layer** (`app/src/domain`)
- **SensorsProcessingService**: Collects and processes sensor data from all sources (digital inputs, analog inputs, CAN Bus messages). Each sensor operates on an independent schedule with configurable sample rates.
- **CanbusSchedulerService**: Streams processed data to external devices over CAN Bus. Each outgoing message operates on an independent schedule.
- **LogWriterService**: Handles persistent storage of sensor data in ASAM MDF format, operating independently from other outputs
- Domain services coordinate data flow and implement core business logic

**Controller Layer** (`app/src/controllers`)
- Provides interfaces for external control and data access
- Exposes device functionality to network services and user interfaces
- Includes optional display UI (`app/src/views`) for devices with screen support

### Configuration System

The system uses a compact runtime configuration model:
- **CBOR**: Efficient binary format for configuration management and persistence
- Configuration Managers bridge user settings with domain service initialization

### Data Flow

1. **Data Collection**: SensorsProcessingService coordinates sensor readings from all sources (digital, analog, CAN Bus). Each sensor operates on an independent schedule based on its configured sample rate.
2. **Processing**: Raw data is collected from hardware through Zephyr device APIs, then processed and validated by domain services
3. **Parallel Outputs**:
   - LogWriterService writes processed data to persistent storage independently
   - Controllers provide real-time access to current sensor values
   - Optional display and web interfaces enable monitoring capabilities
4. **Data Streaming**: CanbusSchedulerService transmits selected sensor data to external devices over CAN Bus, with each outgoing message operating on its own independent schedule

### Concurrency Model

The system leverages Zephyr RTOS threading primitives:
- Scheduler services run in separate threads
- Data handoff between collection and processing stages is synchronized through semaphores/locks
- Hardware access is synchronized through Zephyr's device API locking mechanisms

## Project Structure

- `app/src` - main application source code
    - `app/src/configuration` - CBOR-based configuration services and schemas
    - `app/src/controllers` - interfaces for device control and data access
    - `app/src/domain` - business logic layer
    - `app/src/subsys` - independent subsystems and adapters
    - `app/src/utilities` - utility functions and classes
    - `app/src/views` - UI for optionally supported display
- `app/boards` - board-specific configuration
- `app/libs` - external libraries
- `boards` - custom board definitions
- `tests` - functional and unit tests
- `modules` - external Zephyr modules

### Configuration

The configuration system uses a CBOR-based runtime model for device settings and data management. The project-specific configuration layer wraps schema-based encoding and decoding with C++-friendly helpers for runtime use.

[zcbor](https://github.com/NordicSemiconductor/zcbor) is used for CBOR parsing and serialization. The **zcbor** library provides helper methods to set up serializers and deserializers, and helper scripts can be used to generate them. The project's main CMake file contains helper scripts to generate CBOR serializers and deserializers. The generated code is used as a reference for project-specific C++ adapters when tighter integration with the application code is required.

### Subsystems

With seldom exceptions, the core codebase does not rely on third-party library APIs. Instead, it provides its own abstractions and interfaces, typically defined in the `app/src/subsys` directory.

### Domain

The domain layer is responsible for business logic and data processing. It is located in the `app/src/domain` directory. Two main components are **SensorsProcessingService** and **CanbusSchedulerService**.

**SensorsProcessingService** is responsible for collection and processing of sensor data. It is located in `app/src/domain/sensor_domain/services` directory.

**CanbusSchedulerService** is responsible for collection and processing of CAN Bus data. It is located in `app/src/domain/canbus_domain/services` directory.

Another key domain component is **LogWriterService**, located in `app/src/domain/logging_domain/services` directory. It is responsible for logging sensor data to a file. The main log file format currently used is [ASAM MDF](https://www.asam.net/standards/detail/mdf/) version 4. Logging format-specific implementations are located in `app/src/domain/logging_domain/loggers` directory.

### Configuration Managers

The domain layer includes **Configuration Managers**, components responsible for loading, saving, and configuring the corresponding domain components. Configuration manager implementations are located in `app/src/domain/domain_component/configuration` directory.

## Getting Started

The project uses submodules for external dependencies. To initialize submodules, run:
```shell
git submodule update --init --recursive
```

The development environment is based on [Docker](https://www.docker.com/). Use `example.docker-compose.yml` as an example. Rename it to `docker-compose.yml` to use it.

### Hardware Access From Container

By default, the container will run with `privileged` mode enabled and `pid` mode set to `host` to allow access to devices connected to the host. Devices must be connected to the host before running the container.

If using Docker for Windows with Docker using the WSL2 engine, make sure to attach the device to the WSL container. This can be done with the help of the [usbipd-win](https://github.com/dorssel/usbipd-win) tool. Example:

```shell
usbipd attach --wsl --busid <busid>
```

Where `<busid>` is the bus ID of the device, which can be found with the `usbipd list` command.

The connected device should be visible in the container as `/dev/ttyACM0`. Test the presence of the device in the container with the `ls /dev/ttyACM0` command.

### Building

For VS Code build setup examples, refer to `.vscode_example/tasks.json`. Alternatively, you can build the application using the following command:

```shell
west build -b $BOARD app
```

where `$BOARD` is the target board.

Currently, the following boards are supported:

#### Simulated Boards:
- `native_sim`
- `qemu_cortex_m3`

#### SoC Boards:
- `esp32s3_devkitc_procpu`

### Running and Debugging in a Simulator

Running and debugging in a simulator can be more convenient and time-efficient. To run the compiled application in a simulator, use:

```shell
west build -t run
```

For VS Code debugging setup examples, refer to `.vscode_example/launch.json`.

### Flashing

Once the application is built, flash it to the target board with:

```shell
west flash
```

### Board-Specific Commands

<details>
<summary>
    Native Simulator
    <a href="https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html">(Reference Docs)</a>
</summary>
<br>

**Build:**  
```shell
west build -p auto -b native_sim ./app
```

</details>

<details>
<summary>
    QEMU Cortex M3
    <a href="https://docs.zephyrproject.org/latest/boards/qemu/cortex_m3/doc/index.html">(Reference Docs)</a>
</summary>
<br>

**Build:**  
```shell
west build -p auto -b qemu_cortex_m3 ./app
```

</details>

<details>
<summary>
    ESP32S3 DevKitC v1.3 / EerieLeap PM v0.2
    <a href="https://docs.zephyrproject.org/latest/boards/espressif/esp32s3_devkitc/doc/index.html">(Reference Docs)</a>
</summary>
<br>

**Build with Bootloader:**  
```shell
west build -p auto -b esp32s3_devkitc/esp32s3/procpu --sysbuild ./app
```

**Simple Build:**  
```shell
west build -p auto -b esp32s3_devkitc/esp32s3/procpu ./app
```

**Serial Monitor:**  
```shell
west espressif monitor
```

**Debugging**

Debugging works to some extent. The <a href="https://github.com/Marus/cortex-debug">Cortex-Debug</a> extension for <a href="https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug">VS Code</a> can be used for this purpose. A config example set up for a Docker container can be found in [.vscode_example/launch.json](.vscode_example/launch.json).

For manual GDB, run either `west debug` or run OpenOCD in one terminal:
```shell
/home/ubuntu/zephyr/workspace/utilities/openocd-esp32/bin/openocd \
-f /home/ubuntu/zephyr/workspace/utilities/openocd-esp32/share/openocd/scripts/board/esp32s3-builtin.cfg \
-c "set ESP32_ONLYCPU 1; set ESP_FLASH_SIZE 0; set ESP_RTOS Zephyr" \
-c "init; halt; esp appimage_offset 0" \
-c "esp32s3.cpu0 configure -rtos Zephyr" \
-c "init" \
-c "reset init"
```

And GDB in another:
```shell
/home/ubuntu/zephyr-sdk-0.17.4/xtensa-espressif_esp32s3_zephyr-elf/bin/xtensa-espressif_esp32s3_zephyr-elf-gdb \
-ex 'target extended-remote :3333' \
-ex 'symbol-file build/zephyr/zephyr.elf' \
-ex 'mon reset halt' \
-ex 'maintenance flush register-cache' \
-ex 'break main' \
-ex 'continue'
```

</details>

<details>
<summary>
    MCUDev DevEBox STM32H7XX_M
    <a href="https://github.com/mcauser/MCUDEV_DEVEBOX_H7XX_M/tree/master">(Reference Docs)</a>
</summary>
<br>

**Required Tools**

STM32CubeProgrammer needs to be installed on the host machine. If using Docker, you can take advantage of the build script expecting to find STM32CubeCLT in `tools/st-stm32cubeclt_1.20.0.sh`. Source the tool from the [ST Website](https://www.st.com/en/development-tools/stm32cubeclt.html) and unpack it to `tools/st-stm32cubeclt_1.20.0.sh`. The Dockerfile will take care of installing it during the build process.

**Build:**  
```shell
west build -p auto -b mcudev_devebox_stm32h743_hw_20 ./app
```

**Debug:**

The <a href="https://github.com/Marus/cortex-debug">Cortex-Debug</a> extension for <a href="https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug">VS Code</a> can be used for debugging. An example configuration valid for a Docker container in combination with ST-Link connected over SWD can be found in `.vscode_example/launch.json`.

</details>

## Development

### Required Tools

#### CBOR Schemas

The project uses [zcbor](https://github.com/NordicSemiconductor/zcbor) to generate CBOR helpers from schemas. To install zcbor:

```shell
pip install zcbor
```

#### HTML Minifier

The project uses [html-minifier-terser](https://github.com/terser/html-minifier-terser) to minify HTML files. To install html-minifier-terser:

```shell
npm install -g html-minifier-terser
```

### Generating CBOR Helpers

Use CBOR Serializer helpers in the core CMakeLists.txt file to generate CBOR helpers for the configuration schemas. Uncomment the corresponding line to generate the base set of serialization classes. Files will appear in the `src/configuration/cbor/generated` directory. Finalized files are expected to be placed in the `src/configuration/cbor/<base_name>` directory.

### Network

#### ESP32S3 DevKitC v1.3

To access the web interface, connect to the WiFi network with SSID `EerieLeap` and use the address `192.168.4.1:8080` in your browser.

#### NativeSim

To access the web interface while running in the simulator, follow the instructions in the [Networking with native_sim board](https://docs.zephyrproject.org/latest/connectivity/networking/native_sim_setup.html) documentation. Run the `./net-setup.sh up` script first, then start the simulator. In a Docker container, `net-tools` can be found in `~/zephyr/net-tools` directory. 

To gain access to the web interface, the port needs to be forwarded from the loopback interface to the TAP IP. The `socat` tool can be used for this purpose. Run the following command:

```shell
socat TCP-LISTEN:8080,fork TCP:192.0.2.1:8080
```

where `192.0.2.1` is the TAP IP address.

Once the port is forwarded, the web interface can be accessed at `http://localhost:8080`.

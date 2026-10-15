# Getting started

## Repository

```shell
git submodule update --init --recursive
```

rt_core and its own nested modules (the Lua interpreter and the expression engine) are git
submodules. Zephyr and its modules come through west: `west.yml` pins Zephyr `main` and
allow-lists only the modules the boards need (CMSIS, MCUboot, the Espressif and ST HALs, zcbor,
FatFs, littlefs, mbedTLS). The dev container runs `west init -l`, `west update`,
`west packages pip --install`, `west zephyr-export`, `west blobs fetch hal_espressif` and
`west sdk install` when its image is built, so a fresh container is ready to build.

## Dev container

Two ways to get the same image (`Dockerfile.dev`, Ubuntu 24.04):

- **VS Code:** open the folder and choose "Reopen in Container". `.devcontainer/` mounts the
  repository and `/dev`, runs privileged with the host PID namespace, puts the Python venv with
  `west`, `cmake` and `zcbor` on `PATH`, and installs the C/C++, CMake, Cortex-Debug, Python and
  devicetree extensions.
- **Plain Docker:** copy `example.docker-compose.yml` to `docker-compose.yml` (gitignored),
  `docker compose up -d`, then `docker compose exec zephyr-dev bash`. Interactive shells activate
  the venv through `.bashrc`.

The image also carries the Zephyr SDK, openocd-esp32 (`~/zephyr/workspace/utilities/openocd-esp32`),
Renode (`/opt/renode`) and Zephyr's `net-tools`. An Xvfb display `:0` with a VNC server on port
5900 (password `zephyr` unless `VNCPASSWD` is set at image build) serves GUI tools.

**STM32CubeCLT.** The STM32 runner needs STM32CubeProgrammer. Download
`st-stm32cubeclt_1.20.0.sh` from [ST](https://www.st.com/en/development-tools/stm32cubeclt.html)
into `tools/` (gitignored) before building the image; the Dockerfile installs it and adds
STM32CubeProgrammer to `PATH`.

**Hardware access.** Devices must be attached to the host before the container starts. With
Docker Desktop on WSL2, attach the USB device to WSL first:

```shell
usbipd attach --wsl --busid <busid>     # busid from `usbipd list`
```

The unit then appears in the container as `/dev/ttyACM0`.

Copy `.vscode_example/` to `.vscode/` for the build tasks and launch configurations referenced
below.

## Building

```shell
west build -p auto -b <board> ./app
```

Board targets are listed in [boards.md](boards.md). Useful variants:

| Need | Command |
| --- | --- |
| Separate build directories per board | `west build -d build_native -b native_sim ./app` |
| ESP32 with the MCUboot bootloader | add `--sysbuild` |
| A Kconfig option for one build | append `-- -DCONFIG_EERIE_LEAP_DOMAIN_SENSOR_PROCESSING_STATS=y`; such values stay in the CMake cache until the next `-p always` |
| The bench build: shell, SMP over the console, stack reports, the `logging` and `sensor` commands | build in its own directory (`-d build_bench`) and append `-- -DEXTRA_CONF_FILE=bench.conf`, which stays in the CMake cache (see [application.md](application.md#development-aids)) |
| ESP32 debugging with `west debug` | append `-- -DOPENOCD=<path>/openocd-esp32/bin/openocd -DOPENOCD_DEFAULT_PATH=<path>/openocd-esp32/share/openocd/scripts` (see the ESP32 tasks in `.vscode_example/tasks.json`) |

The VS Code tasks `Build NativeSim`, `Build EL02` (the PM board), `Build STM32H7`, `Build H753ZI`,
`Build ESP32S3`, `Build ESP32S3 With Bootloader` and `Build QEMU Cortex A9` wrap these.

The configure step applies the patches from `modules/eerie_leap_rt_core/patches/` and
`app/patches/` to the Zephyr and HAL checkouts (see [application.md](application.md#patches)).

## Running in a simulator

```shell
west build -t run
```

runs the native_sim or QEMU build (task `Run Emulator`). On native_sim the simulated flash is kept
in `flash.bin` in the working directory (gitignored), so stored configurations survive restarts;
delete the file for a factory-fresh unit. The `Native Sim Debug` launch configuration debugs
`build/zephyr/zephyr.exe` with gdb.

The Nucleo H753ZI build can run under Renode; `renode/README.md` has the commands and the known
gaps.

## Flashing and console

```shell
west flash                                   # STM32 boards: STM32CubeProgrammer over SWD
west flash --esp-device /dev/ttyACM0         # ESP32 boards
west espressif monitor                       # serial console of an ESP32 board
```

The DevEBox board definition also offers `--runner dfu-util`, `openocd` and `jlink`. The PM
board's console is its USB serial JTAG port; any serial terminal works, and toggling RTS resets
the chip.

## Debugging

- **ESP32-S3:** the `ESP32S3 Debug` launch configuration (Cortex-Debug with openocd-esp32), or
  `west debug` with the OpenOCD arguments above. Manually, start OpenOCD in one terminal:

  ```shell
  <path>/openocd-esp32/bin/openocd \
    -f <path>/openocd-esp32/share/openocd/scripts/board/esp32s3-builtin.cfg \
    -c "set ESP32_ONLYCPU 1; set ESP_FLASH_SIZE 0; set ESP_RTOS Zephyr" \
    -c "init; halt; esp appimage_offset 0" \
    -c "esp32s3.cpu0 configure -rtos Zephyr" \
    -c "init" -c "reset init"
  ```

  and GDB in another:

  ```shell
  <sdk>/xtensa-espressif_esp32s3_zephyr-elf/bin/xtensa-espressif_esp32s3_zephyr-elf-gdb \
    -ex 'target extended-remote :3333' -ex 'symbol-file build/zephyr/zephyr.elf' \
    -ex 'mon reset halt' -ex 'maintenance flush register-cache' -ex 'break main' -ex 'continue'
  ```

  Debugging works to an extent; breakpoints and stepping are fine, some kernel state is not.
- **STM32H743:** the `STM32H743 Cortex Debug` launch configuration with an ST-Link on SWD, or
  `west debug`. The `Run DBG Server` task starts `ninja debugserver` for a manual GDB session.

## Tests

The tests are rt_core's twister suites (task `Run Tests`):

```shell
west twister -c --disable-warnings-as-errors -j 4 -O ./twister-out -T ./modules/eerie_leap_rt_core/tests
```

Narrow with `-p native_sim`, `-t <tag>` or `-T <suite path>`; the platforms and known gaps are in
`modules/eerie_leap_rt_core/README.md`. The application itself has no test suite.

## Profiling the sampling path

Build with `CONFIG_EERIE_LEAP_DOMAIN_SENSOR_PROCESSING_STATS=y` and the firmware logs one line
per rate group every 10 s: ticks, overruns, tick durations, the delivered period (average,
minimum, maximum) and the latest volts of each sampled ADC channel. Capture the console for a
few minutes and compare; the format and the counters are described in rt_core's
[sensors.md](../modules/eerie_leap_rt_core/docs/sensors.md#polled-sensors-rate-groups). Numbers
from the PM board are only comparable between its debug builds, and its analog inputs need the
+5 V rail (see [boards.md](boards.md)). native_sim runs in zero simulated time, so durations
there read 0.

## Updating Zephyr

Change the revision in `west.yml`, run `west update` and `west blobs fetch hal_espressif`, then
reconfigure. The configure step reports any rt_core or app patch that no longer applies; fix
the patch against the new checkout and reconfigure again.

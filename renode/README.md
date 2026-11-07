# Renode Simulation

In effort to test, try and profile the application, [Renode](https://github.com/renode) is used to simulate the Zephyr application in a virtual environment. Two targets have scripts here:

| Target | Build | Run |
| --- | --- | --- |
| Teensy 4.1 (the PM HW 1.0 bench, `teensy41`) | `west build -p auto -b teensy41 -S renode ./app -d build_renode` | `renode renode/teensy41.resc --console` |
| ST Nucleo H753ZI (`nucleo_h753zi`) | `west build -p auto -b nucleo_h753zi ./app` | `renode renode/nucleo_h753zi.resc --console` |

Both scripts put the console UART on a socket (`telnet localhost 3456`) and start a GDB server on port 3333.

## Requirements

1. Renode needs to be installed (`/opt/renode` in the dev container, see [docs/getting-started.md](../docs/getting-started.md)). It can be found on the [official website](https://renode.io/).
2. The Zephyr app needs to be built, resulting in a `zephyr.elf` file.
3. The `*.resc` script of the target, present in the `renode/` directory.

## Teensy 4.1

The Teensy 4.1 build for Renode is the bench configuration (`app/boards/teensy41.overlay`, [docs/boards.md](../docs/boards.md)) with the `renode` snippet (`snippets/renode/`, found through `snippet_root` in `zephyr/module.yml`). Renode 1.16 has no model of the i.MX RT1062 as such; `teensy41.repl` builds on Renode's `platforms/cpus/imxrt1064.repl` (the same core, memories and peripherals; the RT1064 only adds a second FlexSPI flash) and adds what the Teensy and the application need: the FlexRAM OCRAM at 0x20280000, the 8 MB boot flash as plain memory at 0x60000000 for execution in place, 2 MB of RAM at 0x20400000 that only the simulated machine has, the systick at the Teensy's 600 MHz, the board LED and buttons on the bench's input pins.

Renode models the Cortex-M7, the memories, LPUART, GPIO, LPSPI, the internal ADC and the TRNG, but not the NOR flash behind the FlexSPI controller, the uSDHC hosts, LPI2C or FlexCAN, and no model exists for the SPI and I2C devices of the bench. The snippet therefore changes the build:

| Bench device | Under Renode |
| --- | --- |
| littlefs configuration store on the W25Q64 (FlexSPI NOR driver) | `zephyr,sim-flash` (2 MB) in the Renode-only RAM at 0x20400000 (`memory-region`), so it costs no OCRAM and keeps its contents across `machine Reset` while the emulation lives |
| SD card on uSDHC1 (FAT, logging) | none: `sdfs0` and `zephyr,sdhc` are removed, so logging and the CDMP `LOGGING` command are unavailable. A flash disk `SD` on the simulated flash keeps FatFS building, declared but not mounted, as on native_sim |
| `can0` FlexCAN3, `can1` MCP2518FD | `can0` is the CAN loopback controller; there is no `can1` |
| Eight analog channels | the ADC emulator (`ADC_EMUL`), random values |
| SSD1306, RV-3028, PCA9555 on LPI2C1 | none (the LPI2C1 node is disabled) |
| DIO inputs `di_0` .. `di_7`, user button | Renode buttons of the same names (`di_0 Press`, `di_0 Release`, `di_0 Toggle`, `user_button Press`); idle is high (pull-up) |
| Board LED (pin 13) | `board_led` (`board_led State`) |
| ESP32-C6, LIN, Ethernet | not present in the build either way |

Build and run:

```bash
west build -p auto -b teensy41 -S renode ./app -d build_renode
renode renode/teensy41.resc --console
```

The snippet stays in the CMake cache, hence the separate build directory (`build_renode`, the default ELF path of the script). Another ELF:

```bash
renode -e '$bin_path=@/path/to/zephyr.elf' -e 'include @renode/teensy41.resc' --console
```

Attach to the console (LPUART6, Teensy pins 0 and 1) and to GDB:

```bash
telnet localhost 3456
arm-zephyr-eabi-gdb build_renode/zephyr/zephyr.elf -ex 'target remote :3333'
```

The `Renode Teensy 4.1 Debug` launch configuration in `.vscode_example/launch.json` does the GDB part. For a bench build (`bench.conf`, the shell on the console) `scripts/renode_console.py "kernel heap" "mem heaps"` sends shell commands to the socket and prints the replies, which also works headless; the memory profiling procedure in [docs/getting-started.md](../docs/getting-started.md#profiling-memory) uses it. Without a window system (CI, a container without the Xvfb display) add `--disable-xwt`; a headless run of 20 simulated seconds with the console written to a file:

```bash
renode --disable-xwt --console --plain \
  -e 'include @renode/teensy41.resc' -e 'pause' \
  -e 'sysbus.lpuart6 CreateFileBackend @/tmp/teensy41-console.log true' \
  -e 'emulation RunFor "00:00:20"' -e 'quit'
```

The timestamps in the log are simulated time, which the systick keeps at the Teensy's rate (checked: Zephyr's tick counter advances 10000 ticks per simulated second). The wall clock depends on the host and on how busy the firmware is; 20 simulated seconds of the idle application took about 24 s here. The script sets the core's vector table to Zephyr's `_vector_table` after loading the ELF, since there is no ROM to read the boot header in flash.

## Nucleo H753ZI

### Issues

#### Flash Emulation

Project expects to have at least one flash partition, which is used to store configuration data. While renode allows to configure peripherals, it seems to have issues with external flash devices that rely on JEDEC SFDP (Serial Flash Discoverable Parameters) protocol to retrieve device information. Although renode doesn't seem to fully implement it, [this issue](https://github.com/renode/renode/issues/916) describes the problem and workaround.

Alternative solution could be to allocate partition in RAM or in internal flash, though du to limited resources this might not satisfy space requirements. The Teensy 4.1 target takes this route with a RAM-backed `zephyr,sim-flash` (see above).

#### ADC Emulation

Firmware fails to initialize ADC peripheral in renode environment, even if ADC dts node uses Zephyr's ADC emulator, requires further investigation. By default, if no ADC peripheral or emulator is available, app falls back to ADC simulation.

### Usage

Assuming project environment is run in a docker container with renode installed in the same container, following commands can be used to run simulation for Nucleo H753ZI board:

Run simulation:

```bash
renode ./renode/nucleo_h753zi.resc --console
```

Attach to the simulation:

```bash
telnet localhost 3456
```

# Boards

Each supported board has a `<board>.conf` and `<board>.overlay` in `app/boards/`; the two
EerieLeap-specific boards also have full definitions under `boards/`. What rt_core needs from a
board (aliases, chosen nodes, channel properties) is described in rt_core's
[device-tree.md](../modules/eerie_leap_rt_core/docs/device-tree.md).

| Board | `west build -b` | Role |
| --- | --- | --- |
| EerieLeap PM HW 0.2 | `esp32s3_pm_hw_02/esp32s3/procpu` | The product hardware. |
| ESP32-S3 DevKitC | `esp32s3_devkitc/esp32s3/procpu` | ESP32-S3 development without the PM board. |
| MCUDev DevEBox STM32H743 HW 2.0 | `devebox_stm32h743_hw_20` | STM32 development board. |
| ST Nucleo H753ZI | `nucleo_h753zi` | STM32 development board, also the Renode target. |
| native_sim | `native_sim` | Host simulation; the default for development and tests. |
| QEMU Cortex-A9, QEMU x86-64 | `qemu_cortex_a9`, `qemu_x86_64` | Emulated 32-bit ARM and 64-bit x86. |

## What each board provides

| Board | Analog inputs | Digital inputs | CAN | SD card | Display | Internal FS |
| --- | --- | --- | --- | --- | --- | --- |
| PM HW 0.2 | 4 (ESP32-S3 ADC, 12-bit, 0 to 3.1 V) | 8 | `can0` TWAI (classic, 1 Mbit/s transceiver), `can1` MCP2518FD on SPI (CAN FD, 8 Mbit/s transceiver) | SPI slot, FAT | SSD1306 128x32 over I2C | littlefs, 1 MiB at 7 MiB of the 8 MiB flash |
| DevKitC | 8 (two ADCs) | 6 | none | none (`FAT_FILESYSTEM_ELM=n`) | none | littlefs, 1 MiB at 7 MiB |
| DevEBox H743 | 8 emulated (`ADC_EMUL`) | none | none | FAT entry in the overlay, not wired to rt_core (no `sdfs0` alias) | none | littlefs |
| Nucleo H753ZI | simulator (`CONFIG_ADC=n`) | none | none | none | none | littlefs |
| native_sim | 8 emulated | 6 emulated | `can0` loopback | none: a flash disk `SD` is declared but not mounted (no `sdfs0` alias), so no logging | none | littlefs on a 16 MiB simulated flash, kept in `flash.bin` |
| QEMU A9, x86-64 | 4 emulated | 6 emulated | `can0` loopback | none, as native_sim | none | littlefs on simulated flash |

Where the hardware is absent rt_core substitutes a simulator (ADC, GPIO) or runs without the
feature (CAN, SD, display); see rt_core's [device-tree.md](../modules/eerie_leap_rt_core/docs/device-tree.md#simulated-and-emulated-hardware).
Simulators sample random values, so readings on native_sim and QEMU are noise.

## EerieLeap PM HW 0.2 (`boards/eerieleap/esp32s3_pm_hw_02`)

The processing module prototype: ESP32-S3 with 8 MiB flash and 8 MiB octal PSRAM, USB serial
JTAG console. Schematic and bill of materials are in `hardware/`.

- **Analog inputs.** Firmware channel index = input number on the board. The front end divides
  5 V at the input to 3.1 V at the pin (rt_core's `ADC_VOLTAGE_MAX_MV=3100`,
  `SENSOR_VOLTAGE_MAX_MV=5000` defaults) and the channels use `ADC_GAIN_1_4` (12 dB
  attenuation, 0 to 3.1 V).

  | Input | ADC unit, channel | GPIO |
  | --- | --- | --- |
  | 0 | ADC1 ch6 | 7 |
  | 1 | ADC2 ch4 | 15 |
  | 2 | ADC2 ch5 | 16 |
  | 3 | ADC2 ch6 | 17 |

  The front end is powered from the +5 V rail, which USB does not supply: on USB-only power every
  input reads about 0 V. The ESP32 eFuse calibration leaves a small offset (about -1.7 % was
  measured on input 0).
- **Digital inputs.** Eight pull-up inputs `di_0` to `di_7` on GPIO 3, 46, 9, 10, 11, 12, 13, 14.
- **CAN.** `can0` is the SoC's TWAI controller behind a 1 Mbit/s transceiver; `can1` is the
  MCP2518FD on SPI behind an 8 Mbit/s transceiver and is the CAN FD channel. Without a peer on a
  configured bus the log shows "bus state changed" warnings; that is normal. The MCP2518FD
  interrupt thread stack is raised to 2048 in the board `.conf`.
- **Storage.** SD card over SPI (FAT, mounted at `/SD:`, long file names on the heap); the SPI
  host cannot detect the card, so presence is checked with a status command and the monitor is
  left off. Internal littlefs partition for configuration.
- **Memory.** `ESP_SPIRAM` octal at 80 MHz, 2 MiB of it as heap, code and read-only data may be
  fetched from PSRAM.
- **Build.** `Kconfig.sysbuild` selects MCUboot without signing when built with `--sysbuild`. The
  board `.conf` is a debug build (`NO_OPTIMIZATIONS`, `DEBUG_THREAD_INFO`), so timing numbers
  taken on it are pessimistic.
- **Flash and console.** `west flash --esp-device /dev/ttyACM0`; the console is the USB serial
  JTAG port. Pulsing RTS on the port resets the chip.

Measurements taken on this board during the ADC work: a oneshot conversion costs about 31 to 55 µs
including driver overhead; `ADC_ESP32_DMA` does not work here (the second ADC unit rejects the
DMA channel setup); sensor processing at priority 5 holds the 50 ms rate group to about 30 µs of
period jitter.

## MCUDev DevEBox STM32H743 HW 2.0 (`boards/mcudev/devebox_stm32h743_hw_20`)

A full board definition with MPU and hardware stack protection. Runners: STM32CubeProgrammer
over SWD (default), dfu-util, OpenOCD and J-Link. The app overlay uses an emulated 16-channel
ADC (8 configured) and littlefs on the external flash. Flashing needs STM32CubeCLT; see
[getting-started.md](getting-started.md#dev-container).

## ESP32-S3 DevKitC

The stock Zephyr board with the app overlay adding two ADCs (eight channels), six pull-up inputs
and a littlefs partition. The board `.conf` configures 2 MiB quad PSRAM and disables FAT since the
kit has no card slot. CAN is commented out in the `.conf`; the TWAI controller can be enabled
there when a transceiver is attached.

## native_sim and QEMU

Everything the firmware needs is emulated: ADC (`ADC_EMUL`), GPIO (`GPIO_EMUL`), a CAN loopback
controller and a simulated flash with a littlefs partition. The overlays also declare a flash
disk named `SD`, but without the `sdfs0` alias and `zephyr,sdhc` the firmware does not mount it,
so logging is unavailable in simulation. `native_sim` and `qemu_x86_64` get 8 MiB of RAM and a
2 MiB heap. On native_sim code runs in zero simulated time, so tick durations in the processing
statistics read 0.

The Nucleo H753ZI target can also run under Renode; see [`renode/README.md`](../renode/README.md)
for the known limitations (external flash, ADC) and the commands.

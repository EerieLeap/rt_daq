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
| ST Nucleo H753ZI | `nucleo_h753zi` | STM32 development board, also a Renode target. |
| PJRC Teensy 4.1 | `teensy41` | Bench of the PM HW 1.0 board (same i.MX RT1062 die); with the `renode` snippet also a Renode target. |
| native_sim | `native_sim` | Host simulation; the default for development and tests. |
| QEMU Cortex-A9, QEMU x86-64 | `qemu_cortex_a9`, `qemu_x86_64` | Emulated 32-bit ARM and 64-bit x86. |

## What each board provides

| Board | Analog inputs | Digital inputs | CAN | SD card | Display | Internal FS |
| --- | --- | --- | --- | --- | --- | --- |
| PM HW 0.2 | 4 (ESP32-S3 ADC, 12-bit, 0 to 3.1 V) | 8 | `can0` TWAI (classic, 1 Mbit/s transceiver), `can1` MCP2518FD on SPI (CAN FD, 8 Mbit/s transceiver) | SPI slot, FAT | SSD1306 128x32 over I2C | littlefs, 2624 KB at 0x520000 of the 8 MiB flash |
| DevKitC | 8 (two ADCs) | 6 | none | none (`FAT_FILESYSTEM_ELM=n`) | none | littlefs, 2624 KB at 0x520000 |
| DevEBox H743 | 8 emulated (`ADC_EMUL`) | none | none | FAT entry in the overlay, not wired to rt_core (no `sdfs0` alias) | none | littlefs |
| Nucleo H753ZI | simulator (`CONFIG_ADC=n`) | none | none | none | none | littlefs |
| Teensy 4.1 | 4 (internal ADC1, 12-bit, 0 to 3.3 V) until the ADS7038 driver exists | 8 | `can0` FlexCAN3 (CAN FD, 5 Mbit/s transceiver), `can1` MCP2518FD on SPI (CAN FD, 8 Mbit/s transceiver) | uSDHC socket, FAT | SSD1306 128x32 over I2C | littlefs, 2 MiB at 4 MiB of the 8 MiB W25Q64 |
| Teensy 4.1 under Renode (`-S renode`) | 8 emulated | 8 (Renode buttons) | `can0` loopback | none: a flash disk `SD` is declared but not mounted, as on native_sim | none | littlefs on a simulated flash in RAM that only the Renode machine has |
| native_sim | 8 emulated | 6 emulated | `can0` loopback | none: a flash disk `SD` is declared but not mounted (no `sdfs0` alias), so no logging | none | littlefs on a 16 MiB simulated flash, kept in `flash.bin` |
| QEMU A9, x86-64 | 4 emulated | 6 emulated | `can0` loopback | none, as native_sim | none | littlefs on simulated flash |

Where the hardware is absent rt_core substitutes a simulator (ADC, GPIO) or runs without the
feature (CAN, SD, display); see rt_core's [device-tree.md](../modules/eerie_leap_rt_core/docs/device-tree.md#simulated-and-emulated-hardware).
Simulators sample random values, so readings on native_sim and QEMU are noise.

## Flash layout of the ESP32-S3 boards

The PM board and the DevKitC (N8R2) have 8 MiB of flash and one partition table,
`app/boards/partitions/esp32s3_pm_hw_02_procpu.dtsi` (the DevKitC's file includes it). The application's
overlay and MCUboot both include it, and it replaces the stock `partitions_0x0_amp` table completely: no
APP CPU or LP core image is built. The firmware runs on the PRO CPU only (no `appcpu` image, no IPC).

| Partition | Offset | Size | Note |
| --- | --- | --- | --- |
| `boot_partition` (MCUboot) | 0x000000 | 64 KB | |
| `sys_partition` | 0x010000 | 64 KB | |
| `slot0_partition` | 0x020000 | 2560 KB | the running image; the application is linked to run from here |
| `slot1_partition` | 0x2A0000 | 2560 KB | the update candidate, then the backup |
| `intfs0_partition` (LittleFS) | 0x520000 | 2624 KB | the unit's configurations; was 1 MiB at 0x700000 |
| `storage_partition` | 0x7B0000 | 192 KB | unused by rt_daq (no settings, NVS or Bluetooth); kept for layout parity with the other units; was at 0x3B0000 |
| unused | 0x7E0000 | 124 KB | |
| `coredump_partition` | 0x7FF000 | 4 KB | was at 0x3FF000 |

- Every slot starts on a 64 KB boundary, the MMU page of the ESP32.
- The largest signed image is `slot0` minus the swap trailer, `CONFIG_MCUBOOT_UPDATE_FOOTER_SIZE`:
  `scripts/check_image_sizes.py` checks it, and MCUboot's size against `boot_partition`, at the end of
  every sysbuild build.
- The linker does not know the slot: the **FLASH** line of the memory report is measured against the
  whole chip. The slot limit is enforced after the link, by imgtool (`--max-sectors`, from rt_core's signing
  wrapper) and by `scripts/check_image_sizes.py`.
- Under MCUboot the application gets less internal data RAM than with simple boot: `USER_DRAM_END`
  stops at the bootloader's loader segment, so `dram0_0_seg` is 380672 B instead of the 399108 B of a
  simple-boot ESP32-S3 build (186696 B of it used by a PM board build with `app/prj.conf` as of the
  first MCUboot build).
- Moving a unit from the older layout takes a one-time USB flash with a chip erase
  ([getting-started.md](getting-started.md#moving-a-unit-from-simple-boot-to-mcuboot)).

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
- **Build.** Always `--sysbuild`: the board's `Kconfig.sysbuild` selects MCUboot, and
  `app/sysbuild.conf` makes it swap-using-move with ECDSA P-256 signatures (the board's own default is
  no signature). The board `.conf` is a debug build (`NO_OPTIMIZATIONS`, `DEBUG_THREAD_INFO`), so
  timing numbers taken on it are pessimistic.
- **Flash and console.** `west flash --esp-device /dev/ttyACM0` (MCUboot, then the application); the console is the USB serial
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
and the PM board's [flash layout](#flash-layout-of-the-esp32-s3-boards) (the N8R2 kit has the same 8 MiB
flash). Built with `--sysbuild` like the PM board, since Zephyr's own `Kconfig.sysbuild` for the kit also
selects MCUboot. The board `.conf` configures 2 MiB quad PSRAM and disables FAT since the
kit has no card slot. CAN is commented out in the `.conf`; the TWAI controller can be enabled
there when a transceiver is attached.

## PJRC Teensy 4.1 (`teensy41`, the PM HW 1.0 bench)

The stock Zephyr board with the app overlay carrying the devices of the PM HW 1.0 processing module
(the release-candidate PCB, planned in `dev_docs/hw/planning/`, gitignored): the board's pad plan
was laid out so that every bus lands on Teensy header pins, and only the GPIO numbers differ. The
Teensy carries the MIMXRT1062DVJ6B (600 MHz, consumer grade; the board gets the industrial CVJ5B at
528 MHz), 8 MiB of W25Q64 flash on FlexSPI, a micro-SD socket and USB. It has no debug port: PJRC's
bootloader chip programs the flash, so MCUboot and the flash layout of the board cannot be exercised
on it.

| Teensy pins | Function | Bench hardware |
| --- | --- | --- |
| 0 (RX), 1 (TX) | Console, LPUART6, 115200 | 3.3 V UART adapter; or `-S cdc-acm-console` for the USB port |
| 2, 3, 4, 33, 5 | `di_0` .. `di_4` (EMC_04 .. EMC_08) | the comparator channel of the board on a breadboard; pull-up inputs |
| 6, 36, 32 | `di_5` .. `di_7` | |
| 7 | user button (`sw0`) | |
| 8 | PCA9555 INT | |
| 9 | MCP2518FD INT | |
| 10, 11, 12, 13 | LPSPI4 CS, MOSI, MISO, SCK: `can1`, MCP2518FD | the MCP2518FD module of HW 0.2 (20 MHz crystal, 8 MHz SPI); pin 13 is also the board LED |
| 14, 15, 20, 21 | ADC1 inputs 7, 8, 15, 0: analog channels 0 .. 3 | 0 to 3.3 V; on the board these pins sense VIN, 5V_A, CC1, CC2 |
| 18 (SDA), 19 (SCL) | LPI2C1, 400 kHz: SSD1306 0x3C, RV-3028 0x52, PCA9555 0x20 | HW 0.2 display module, Pimoroni RV3028 breakout, PCA9555/TCA9555 module |
| 22, 23, 40 | uSDHC2 CMD, CLK, DATA0: ESP32-C6 over 1-bit SDIO | Seeed XIAO ESP32C6 D10, D8, D9 with 51 kΩ pull-ups; disabled in the overlay until wired |
| 24 (TX), 25 (RX) | LPUART1: the ESP32-C6's UART0 | XIAO D7, D6; disabled, nothing drives it yet |
| 26, 27, 38, 39 | LPSPI3 MOSI, SCK, CS, MISO | the ADS7038 or TLA2518 once its driver exists |
| 28 | RV-3028 INT | |
| 30 (RX), 31 (TX) | FlexCAN3: `can0`, CAN FD | Adafruit CAN Pal (TJA1051T/3) |
| 34 (RX), 35 (TX) | LPUART5: LIN | no driver yet |
| 37 | ESP32-C6 EN, open drain | a wire to the XIAO's TP8 pad |
| SD socket | uSDHC1: SD card, FAT at `/SD:` | |

- **Memory.** The overlay makes the 512 KiB OCRAM2 the main RAM (`zephyr,sram`), as planned for
  the board; the board file would use the 256 KiB FlexRAM OCRAM. ITCM and DTCM stay at 128 KiB.
  The system heap is 128 KiB here (the dynamic work-queue stacks come from it; the 64 KiB of
  `prj.conf` ran out at the sensor processing service), the rest of the RAM is the libc arena.
- **Storage.** The littlefs configuration store is a 2 MiB partition at 4 MiB of the boot flash,
  written through the FlexSPI NOR driver while the code runs from the same flash (the driver runs
  from ITCM with interrupts off during an erase or program). The SD card stands in for the eMMC of
  the board.
- **Analog inputs.** Until the ADS7038 driver exists (milestone M4 of the bench plan) the `adc0`
  alias is the internal ADC1 with the four channels above, so the sampling path can be exercised
  with 3.3 V signals.
- **CAN.** `can0` is FlexCAN3 (the FD controller of the RT1062), `can1` the MCP2518FD. FlexCAN1
  of the board file is disabled: its pins 22 and 23 carry the co-processor's SDIO.
- **Co-processor.** The ESP-Hosted-MCU node on uSDHC2 is declared but disabled; enabling it also
  needs the Wi-Fi and network options (see the comment in `teensy41.conf`).
- **Ethernet** of the Teensy is disabled (the board has none).
- **Build and flash.** `west build -p auto -b teensy41 ./app`, then `west flash` (the `teensy`
  runner, which needs `teensy_loader_cli` on the host) or the Teensy Loader application with
  `build/zephyr/zephyr.hex`. `bench.conf` works here too (the shell on the console UART).
- **Measured** (under Renode, 2026-11-01, idle default configuration): 1.32 MiB of flash and
  185 KiB of static RAM, of which 128 KiB is the kernel heap; at run time the kernel heap peaks at
  78.7 KiB (47 KiB of it dynamic thread stacks), the libc arena at 27.8 KiB, the 17 thread stacks at
  11.1 KiB of their 76 KiB. How to repeat it: [getting-started.md](getting-started.md#profiling-memory).

The same build runs under Renode with the `renode` snippet (`west build -b teensy41 -S renode ./app
-d build_renode`), which replaces the devices Renode's i.MX RT platform does not model (the NOR
flash behind the FlexSPI controller, the SD hosts, LPI2C, FlexCAN and the SPI and I2C devices) with
a simulated flash in RAM that only the Renode machine has, the ADC emulator and the CAN loopback
controller; see [`renode/README.md`](../renode/README.md).

## native_sim and QEMU

Everything the firmware needs is emulated: ADC (`ADC_EMUL`), GPIO (`GPIO_EMUL`), a CAN loopback
controller and a simulated flash with a littlefs partition. The overlays also declare a flash
disk named `SD`, but without the `sdfs0` alias and `zephyr,sdhc` the firmware does not mount it,
so logging, and with it logging triggers and the CDMP `LOGGING` command, is unavailable in
simulation. `native_sim` and `qemu_x86_64` get 8 MiB of RAM and a 2 MiB heap. On native_sim code
runs in zero simulated time, so tick durations in the processing statistics read 0.

The Nucleo H753ZI and Teensy 4.1 targets can also run under Renode; see
[`renode/README.md`](../renode/README.md) for the commands and what each simulation lacks.

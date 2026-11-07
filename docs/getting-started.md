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
| An ESP32-S3 board (PM HW 0.2, DevKitC) | always add `--sysbuild`: MCUboot sits in front of the application (see [ESP32 builds](#esp32-builds-flash-layout-and-signing)) |
| A Kconfig option for one build | append `-- -DCONFIG_EERIE_LEAP_DOMAIN_SENSOR_PROCESSING_STATS=y`; such values stay in the CMake cache until the next `-p always` |
| The bench build: shell, SMP over the console, stack reports, the `logging` and `sensor` commands | build in its own directory (`-d build_bench`) and append `-- -DEXTRA_CONF_FILE=bench.conf`, which stays in the CMake cache and, under `--sysbuild`, reaches the application image only (see [application.md](application.md#development-aids)) |
| ESP32 debugging with `west debug` | append `-- -DOPENOCD=<path>/openocd-esp32/bin/openocd -DOPENOCD_DEFAULT_PATH=<path>/openocd-esp32/share/openocd/scripts` (see the ESP32 tasks in `.vscode_example/tasks.json`) |
| The Teensy 4.1 for Renode | `west build -p auto -b teensy41 -S renode ./app -d build_renode`: the `renode` snippet (`snippets/renode/`) swaps the devices Renode does not model for simulators; its own directory, since the snippet stays in the CMake cache (see [Running in a simulator](#running-in-a-simulator)) |

The VS Code tasks `Build NativeSim`, `Build EL02` (the PM board), `Build EL02 Bench`, `Build STM32H7`,
`Build H753ZI`, `Build Teensy41`, `Build Teensy41 Renode`, `Build ESP32S3` (the DevKitC), `Check image sizes`
and `Build QEMU Cortex A9` wrap these.

The configure step applies the patches from `modules/eerie_leap_rt_core/patches/` and
`app/patches/` to the Zephyr and HAL checkouts (see [application.md](application.md#patches)).

### ESP32 builds, flash layout and signing

**ESP32-S3 builds always use `--sysbuild`.** MCUboot sits in front of the application on both ESP32-S3
boards: the flash holds two image slots, the application is linked for the first and signed, and only
MCUboot starts it. A plain `west build` of one of these boards (no `--sysbuild`) produces an ESP
simple-boot image, linked for and flashed at 0x0: it boots, but it overwrites MCUboot and ignores the
slot layout, so no update can follow.
A sysbuild directory holds the two images in `mcuboot/` and `app/` (the application's ELF is
`build/app/zephyr/zephyr.elf`, the signed image `build/app/zephyr/zephyr.signed.bin`). The other
boards build without it: sysbuild on them builds only the application, since only the ESP32-S3
boards select MCUboot, so do not make `--sysbuild` a default with `west config build.sysbuild true`.

```shell
west build -p auto -b esp32s3_pm_hw_02/esp32s3/procpu --sysbuild ./app
west build -p auto -b esp32s3_devkitc/esp32s3/procpu --sysbuild ./app
```

The bench build (shell, SMP over the console, stack reports; see
[application.md](application.md#development-aids)) adds `app/bench.conf` to the application image only:

```shell
west build -d build_bench -p always -b esp32s3_pm_hw_02/esp32s3/procpu --sysbuild ./app -- -DEXTRA_CONF_FILE=bench.conf
```

(`-Dapp_EXTRA_CONF_FILE=bench.conf` addresses the application image by name and does the same. Do not
give MCUboot a `-Dmcuboot_EXTRA_CONF_FILE`: it replaces `app/sysbuild/mcuboot.conf`.)

The custom board `esp32s3_pm_hw_02` is found through this repository's `zephyr/module.yml`
(`board_root: .`), which west registers when this repository is the west manifest repository (the dev
container's `west init -l`). A workspace whose manifest is another repository passes it itself:
`-- -DBOARD_ROOT=<path to this repository>`.

**Flash layout.** Each ESP32 board's partition table is one file, `app/boards/partitions/<board>.dtsi`
(the DevKitC's includes the PM board's: same 8 MB flash): two 2.5 MB image slots (`slot0` runs, `slot1`
takes the update and then holds the previous image as the backup), LittleFS for the unit's files, and
the `storage` partition (stock layout, unused by rt_daq). The application's overlay includes it and
`app/sysbuild.cmake` hands the same file to MCUboot, so the two cannot disagree. The table is in
[boards.md](boards.md#flash-layout-of-the-esp32-s3-boards). MCUboot swaps the slots in "swap using
move" mode and verifies an ECDSA P-256 signature before every swap (`app/sysbuild.conf`).
`app/sysbuild/mcuboot.conf` switches off the drivers the board enables that MCUboot does not need;
MCUboot must fit its 64 KB boot partition.

**Size checks.** Every sysbuild build ends with `scripts/check_image_sizes.py`, which fails the build
when MCUboot outgrows its boot partition or the signed application outgrows `slot0` minus the swap
trailer, and prints both margins. It also compares the two images' partition tables. Run it by hand on
a build directory with `scripts/check_image_sizes.py build`.

**Signing.** Images are signed with the development key,
`modules/eerie_leap_rt_core/keys/dev-ec-p256.pem`. It is public and protects nothing: it only keeps a
bench unit from accepting images that were not built for this project. Release images are built by
`.github/workflows/release.yml` with the release key, which lives in a GitHub Actions secret and never
in the repository. A unit trusts the key its MCUboot was built with, so a unit flashed with release
artifacts accepts release images only; changing the key, the mode or the partition table takes a USB
flash. To sign a local build with another key, pass an absolute path, quoted because it is a Kconfig
string: `-- '-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="/path/to/key.pem"'`. Each signed image carries the
unit's product (`EERIE_LEAP_PRODUCT_ID`, 0xC401) and board in a signed TLV, so a unit accepts an
update built for it only (rt_core's [dfu.md](../modules/eerie_leap_rt_core/docs/dfu.md)). The version
is `app/VERSION`.

## Running in a simulator

```shell
west build -t run
```

runs the native_sim or QEMU build (task `Run Emulator`). On native_sim the simulated flash is kept
in `flash.bin` in the working directory (gitignored), so stored configurations survive restarts;
delete the file for a factory-fresh unit. The `Native Sim Debug` launch configuration debugs
`build/zephyr/zephyr.exe` with gdb.

Two targets run under Renode (`/opt/renode` in the dev container), each with a script in `renode/`:
the Nucleo H753ZI build, and the Teensy 4.1 build made with the `renode` snippet:

```shell
west build -p auto -b teensy41 -S renode ./app -d build_renode
renode renode/teensy41.resc --console        # console: telnet localhost 3456, GDB: port 3333
```

The task `Run Renode Teensy41` does the second line. [`renode/README.md`](../renode/README.md) has
what each simulation lacks and how to drive the simulated inputs.

## Flashing and console

```shell
west flash                                   # STM32 boards: STM32CubeProgrammer over SWD
west flash --esp-device /dev/ttyACM0         # ESP32 boards
west espressif monitor                       # serial console of an ESP32 board
west flash                                   # Teensy 4.1: teensy_loader_cli over USB (press the Teensy's button first)
```

For an ESP32 sysbuild directory `west flash` writes MCUboot and then the application into `slot0`
(`--domain mcuboot` or `--domain app` writes one of them). esptool erases only the sectors it writes,
so the swap state MCUboot keeps at the end of `slot0` survives a plain `west flash`. If a freshly
flashed image is swapped back or treated as an update, erase the chip first, as in the next section.

The DevEBox board definition also offers `--runner dfu-util`, `openocd` and `jlink`. The PM
board's console is its USB serial JTAG port; any serial terminal works, and toggling RTS resets
the chip.

The Teensy 4.1 is programmed by its own bootloader chip: `west flash` runs `teensy_loader_cli`
(`sudo apt install teensy-loader-cli` on Ubuntu, or the PJRC build; `--teensy <path>` names another
binary), or load `build/zephyr/zephyr.hex` with the Teensy Loader application. The runner does not
wait for the board: press the Teensy's button first, so that it shows up as the HalfKay bootloader
(`lsusb`: `16c0:0478`), then run `west flash`; otherwise it stops with `Unable to open device`.
Under WSL the bootloader is a new USB device, so attach the Teensy with
`usbipd attach --wsl --busid <id> --auto-attach` (in Windows) to have it follow the re-enumeration.
The console is LPUART6 on pins 0 (RX) and 1 (TX) at 115200 through a 3.3 V UART adapter;
`-S cdc-acm-console` moves it to the USB port.

### Moving a unit from simple boot to MCUboot

Units built before MCUboot ran the application straight from the ROM bootloader, on the stock 4 MB
partition table with a 1 MB file system at 0x700000. The new layout moves the file system (to 0x520000,
2624 KB), the `storage` partition (from 0x3B0000 to 0x7B0000; rt_daq has no settings or Bluetooth, so
nothing uses it) and the coredump partition. Moving a unit is a one-time USB flash that erases the
chip, so back up first and restore afterwards. Updates after that go over the air.

1. Back up every configuration with the gauge repository's bench client (`tools/smp_can_client.py`; on
   CAN, with `--target`, or on the console of a bench build, with `--serial`). The internal file system
   holds only the configurations (`config/*.cbor`); Lua scripts and logs are on the SD card, which the
   flash does not touch.

   ```shell
   python3 tools/smp_can_client.py --serial /dev/ttyACM0 config-list
   python3 tools/smp_can_client.py --serial /dev/ttyACM0 config-read <type> backup/<type>.cbor    # for every type listed
   ```

2. Erase the chip and flash the new layout. What the old layout left behind lands in the new one: the
   simple-boot image (at 0x0, about 1.7 MB) and the old NVS (0x3B0000) fall into the new boot, `slot0`
   and `slot1` regions, where MCUboot or `img_mgmt` could take a leftover for an image or its swap
   state, and the old LittleFS (0x700000 to 0x7FFFFF) falls into the tail of the new `intfs0`, `storage`
   and coredump regions, where its blocks could be mounted as the new file system. `--erase` runs once,
   before the first image, and then MCUboot and the application are written:

   ```shell
   west flash --erase --esp-device /dev/ttyACM0
   ```

3. Restore: `config-write <type> backup/<type>.cbor` for every type. Writing the `system` configuration
   back keeps the unit's device ID.

## Debugging

- **ESP32-S3:** the `ESP32S3 Debug` launch configuration (Cortex-Debug with openocd-esp32), or
  `west debug` with the OpenOCD arguments above. Manually, start OpenOCD in one terminal:

  ```shell
  <path>/openocd-esp32/bin/openocd \
    -f <path>/openocd-esp32/share/openocd/scripts/board/esp32s3-builtin.cfg \
    -c "set ESP32_ONLYCPU 1; set ESP_FLASH_SIZE 0; set ESP_RTOS Zephyr" \
    -c "init; halt; esp appimage_offset 0x20000" \
    -c "esp32s3.cpu0 configure -rtos Zephyr" \
    -c "init" -c "reset init"
  ```

  and GDB in another:

  ```shell
  <sdk>/xtensa-espressif_esp32s3_zephyr-elf/bin/xtensa-espressif_esp32s3_zephyr-elf-gdb \
    -ex 'target extended-remote :3333' -ex 'symbol-file build/app/zephyr/zephyr.elf' \
    -ex 'mon reset halt' -ex 'maintenance flush register-cache' -ex 'break main' -ex 'continue'
  ```

  Debugging works to an extent; breakpoints and stepping are fine, some kernel state is not.
- **STM32H743:** the `STM32H743 Cortex Debug` launch configuration with an ST-Link on SWD, or
  `west debug`. The `Run DBG Server` task starts `ninja debugserver` for a manual GDB session.
- **Teensy 4.1:** no debug port on the board. Under Renode the script starts a GDB server on port
  3333: the `Renode Teensy 4.1 Debug` launch configuration attaches to it (build `build_renode`
  first and start `Run Renode Teensy41`), or manually
  `arm-zephyr-eabi-gdb build_renode/zephyr/zephyr.elf -ex 'target remote :3333'`.

## Tests

The tests are rt_core's twister suites (task `Run Tests`):

```shell
west twister -c --disable-warnings-as-errors -j 4 -O ./twister-out -T ./modules/eerie_leap_rt_core/tests
```

Narrow with `-p native_sim`, `-t <tag>` or `-T <suite path>`; the platforms and known gaps are in
`modules/eerie_leap_rt_core/README.md`. The application itself has no test suite.

## Formatting

C/C++ follows `.clang-format` (clang-format 23.1.1; the dev container pins it, elsewhere
`pip install clang-format==23.1.1`; older majors refuse the config).

| Command | Does |
|---|---|
| `scripts/format.sh` | Checks the tracked C/C++ files; exit 1 lists the files that differ. CI runs the same check. |
| `scripts/format.sh --fix` | Reformats them in place. |
| `scripts/format.sh --changed [BASE]` | Only files changed since BASE (default `origin/main`). |
| `scripts/format.sh PATH...` | Limits it to the given paths. |
| `scripts/format.sh --recursive` | Also covers submodules that carry their own `.clang-format`. |

Without the script: `clang-format -i FILE`, or `git clang-format` for the staged lines only.

To keep code out, list files in `.clang-format-ignore`, or wrap a block in `// clang-format off` and
`// clang-format on` with a comment saying why.

`git config blame.ignoreRevsFile .git-blame-ignore-revs` hides the reformat commit from `git blame`
(GitHub does it by itself).

## Linting

C/C++ is checked with `.clang-tidy` (clang-tidy 23; the dev container has `clang-tidy-23`, elsewhere
add LLVM's apt repository `deb http://apt.llvm.org/noble/ llvm-toolchain-noble-23 main` and
`sudo apt-get install clang-tidy-23 clang-tools-23`). It reads the compile database of a
`native_sim` build; `scripts/tidy.sh --build` makes it in `build/tidy`.

| Command | Does |
|---|---|
| `scripts/tidy.sh` | Lints the sources of this repository with the existing database; exit 1 on any warning. |
| `scripts/tidy.sh --build` | Builds `./app` for `native_sim` into `build/tidy` first. |
| `scripts/tidy.sh --changed [BASE]` | Only sources changed since BASE (default `origin/main`). CI runs this on pull requests. |
| `scripts/tidy.sh PATH...` | Limits it to the given files or directories. |
| `scripts/tidy.sh --fix` | Applies the fixes clang-tidy offers; review `git diff` afterwards. |
| `scripts/tidy.sh -p DIR` | Uses the database in DIR, or every one below it (a twister output directory); repeatable. |

To waive a finding, name the check and give the reason: `// NOLINT(check-name): reason` at the end of
the line, `// NOLINTNEXTLINE(check-name): reason` above it, or `NOLINTBEGIN(check-name)` and
`NOLINTEND(check-name)` around a block. Never a bare `NOLINT` and never a wildcard.

## Profiling the sampling path

Build with `CONFIG_EERIE_LEAP_DOMAIN_SENSOR_PROCESSING_STATS=y` and the firmware logs one line
per rate group every 10 s: ticks, overruns, tick durations, the delivered period (average,
minimum, maximum) and the latest volts of each sampled ADC channel. Capture the console for a
few minutes and compare; the format and the counters are described in rt_core's
[sensors.md](../modules/eerie_leap_rt_core/docs/sensors.md#polled-sensors-rate-groups). Numbers
from the PM board are only comparable between its debug builds, and its analog inputs need the
+5 V rail (see [boards.md](boards.md)). native_sim runs in zero simulated time, so durations
there read 0.

## Profiling memory

Three views, from cheap to detailed: the linker's totals, the static footprint by file, and the
heaps and stacks of the running firmware.

**Totals.** Every build ends with the memory region table (`FLASH`, `RAM`, and on the Teensy
`ITCM`, `DTCM`, `OCRAM`). `RAM` is the static image: data, bss, the static thread stacks and the
kernel heap buffer (`HEAP_MEM_POOL_SIZE`). What the image leaves of `RAM` becomes the libc arena
(`COMMON_LIBC_MALLOC_ARENA_SIZE=-1`), so a bigger image means a smaller arena, not a link error.

**Static footprint.** `west build -d <build dir> -t ram_report` (or `rom_report`) prints the image
as a tree by directory, file and symbol and writes `<build dir>/ram.json`; give the existing build
directory and no source argument (with one, west reconfigures the directory from scratch). On the Teensy 4.1 bench build the tree shows
what the static RAM is made of: the 128 KiB kernel heap, the `main` and system work queue stacks
(8 KiB each), the MCUmgr transport buffers (10.5 KiB), littlefs and its file cache (9 KiB), the
CAN and SD drivers (8 KiB, half of it the MCP2518FD interrupt thread). rt_core itself holds under
1 KiB of static RAM: its objects live on the heaps. Of the 1.3 MiB of flash, rt_core is 466 KiB,
Lua 93 KiB, the C++ runtime with libc about 477 KiB, Zephyr 129 KiB and the other modules
(littlefs, FatFs, the NXP HAL) 55 KiB.

**Runtime.** Build the bench variant (`bench.conf`, see
[application.md](application.md#development-aids)); it reports every thread's stack peak every 10 s
through the log and gives the shell commands:

| Command | Shows |
| --- | --- |
| `mem heaps` (`APP_MEMORY_SHELL`) | Size, use and peak use of every heap: the kernel heap, the libc arena, the small heaps of Zephyr subsystems. |
| `kernel heap` | The kernel heap alone (Zephyr's own command). |
| `kernel thread stacks` | Every thread's stack size and peak use; the thread analyzer prints the same every 10 s with the CPU share. |

Which heap holds what: the kernel heap takes `k_malloc`, the dynamic thread stacks
(`k_thread_stack_alloc`: rt_core's work queues and service threads) and, on a board without a
shared multi-heap (the Teensy, and the PM HW 1.0 board unless its PSRAM is fitted), rt_core's PMR
allocations: configurations, sensors, CAN messages, Lua states. The libc arena takes `malloc` and
C++ `new`: `std::function`, strings, node containers. Watch the peaks (`max used`) with a real
configuration loaded and the scenario running (sensors, CAN traffic, logging), not the idle unit.

On the Teensy the shell is on the console UART (pins 0 and 1). Under Renode, with the console on
the socket of `renode/teensy41.resc`, `scripts/renode_console.py` sends the commands and prints the
replies, for a headless run too:

```shell
west build -p auto -b teensy41 -S renode ./app -d build_renode_bench -- -DEXTRA_CONF_FILE=bench.conf
renode --console -e '$bin_path=@build_renode_bench/zephyr/zephyr.elf' -e 'include @renode/teensy41.resc'   # one terminal
scripts/renode_console.py --settle 25 "mem heaps" "kernel thread stacks"                                  # another
```

The same figures come out of GDB on Renode's port 3333 without the shell:
`p *_system_heap.heap.heap` (the kernel heap), `p *z_malloc_heap.heap` and `p/x z_malloc_heap_end`
(the libc arena and its end). `bench.conf` sets `STACK_SENTINEL`, which the Teensy's MPU stack
guard replaces; Kconfig warns about it on that board and the build goes on.

Measured on the Teensy 4.1 under Renode on 2026-11-01, bench build, idle with the default
configuration (no sensors, no SD card, no display), after 40 s:

| Memory | Size | Used | Peak |
| --- | --- | --- | --- |
| Kernel heap | 128 KiB (130760 usable) | 78728 | 78728 |
| Libc arena | 324040 | 27736 | 27816 |
| Thread stacks (17 threads) | 76096 configured | | 11108 |

The kernel heap's 78.7 KiB are 47 KiB of dynamic thread stacks (three 8 KiB work queues, five 4 KiB
threads, the 2 KiB CAN receive thread) plus about 32 KiB of PMR objects and kernel data; that is
why the Teensy configuration has a 128 KiB heap where `prj.conf` has 64 KiB. The largest stack
peaks were `main` (2256 of 8192), the configuration work queue (1720 of 8192) and the shell
(1040 of 4096); the CAN loopback receive thread sits at 404 of its 512 bytes. About 164 KiB of the
512 KiB were in use in all. The Renode variant lacks the SD, display and CAN drivers, so the real
board sits a few KiB higher, and a loaded configuration adds its objects to the kernel heap.

## Updating Zephyr

Change the revision in `west.yml`, run `west update` and `west blobs fetch hal_espressif`, then
reconfigure. The configure step reports any rt_core or app patch that no longer applies; fix
the patch against the new checkout and reconfigure again.

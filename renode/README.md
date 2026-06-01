# Renode Simulation

In effort to test, try and profile the application, [Renode](https://github.com/renode) is used to simulate the Zephyr application in a virtual environment.

## Issues

### Flash Emulation

Project expects to have at least one flash partition, which is used to store configuration data. While renode allows to configure peripherals, it seems to have issues with external flash devices that rely on JEDEC SFDP (Serial Flash Discoverable Parameters) protocol to retrieve device information. Although renode doesn't seem to fully implement it, [this issue](https://github.com/renode/renode/issues/916) describes the problem and workaround.

Alternative solution could be to allocate partition in RAM or in internal flash, though du to limited resources this might not satisfy space requirements.

### ADC Emulation

Firmware fails to initialize ADC peripheral in renode environment, even if ADC dts node uses Zephyr's ADC emulator, requires further investigation. By default, if no ADC peripheral or emulator is available, app falls back to ADC simulation.

## Usage

### Requirements

1. Renode needs to be installed. It can be found on the [official website](https://renode.io/).
2. Zephyr app needs to be built resulting in `zephyr.elf` file.
3. `*.resc` renode configuration file expected to be present in the `renode/` directory.

Assuming project environment is run in a docker container with renode installed in the same container, following commands can be used to run simulation for Nucleo H753ZI board:

Run simulation:

```bash
renode ./renode/nucleo_h753zi.resc --console
```

Attach to the simulation:

```bash
telnet localhost 3456
```

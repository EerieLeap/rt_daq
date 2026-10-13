# rt_daq documentation

rt_daq is the EerieLeap processing module firmware: a Zephyr application that wires the
[rt_core](../modules/eerie_leap_rt_core) module into a data acquisition unit. These pages cover
what is specific to this application and its hardware. How the systems themselves work and how
they are configured is documented in rt_core:
[`modules/eerie_leap_rt_core/docs/README.md`](../modules/eerie_leap_rt_core/docs/README.md).

| Page | Covers |
| --- | --- |
| [getting-started.md](getting-started.md) | Dev container, building, running in simulators, flashing, debugging, tests |
| [application.md](application.md) | What the app adds to rt_core: wiring, logging and display controllers, `prj.conf`, patches |
| [boards.md](boards.md) | Supported boards, what each provides, the PM board's inputs and quirks |

Elsewhere in the repository:

- [`renode/README.md`](../renode/README.md): running the Nucleo H753ZI build under Renode.
- [`scripts/mdf_check/`](../scripts/mdf_check/README.md): asammdf checks of the MDF log files
  (summary, invalid samples, robustness of cut files).
- `hardware/`: schematic and bill of materials of the PM board.
- [Wiki](https://github.com/Pavllick/EerieLeapRT/wiki): configuration details and usage examples
  for end users.

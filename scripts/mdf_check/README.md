# MDF log checks

Python helpers for the MDF 4 files rt_daq writes to `logs/` on the SD card. They need
[asammdf](https://github.com/danielhrisca/asammdf) (`pip install asammdf`).

| Script | Use |
|--------|-----|
| `check_mf4.py file.mf4 ...` | Summary: finalized or not, header properties (device, versions, configuration CRC, interval), and per channel group the record size, record count, samples, invalid samples, time range and step. |
| `check_mf4_truncated.py file.mf4 [tail_bytes]` | Robustness check of a finalized file: turns a copy back into an unfinalized one and cuts it at every byte of its tail, as a power loss or a full card would; asammdf must read exactly the complete records of every cut. |

Reading a file in Python:

```python
from asammdf import MDF

mdf = MDF("log_data_20251022T041030Z_5660.mf4")
sensor_1 = mdf.get("sensor_1")                    # invalid samples (failed readings) dropped
everything = mdf.get("sensor_1", ignore_invalidation_bits=True)
print(everything.invalidation_bits)               # True where a reading failed
frames = mdf.extract_bus_logging({"CAN": [("signals.dbc", 0)]})  # raw CAN frames through a DBC
```

The file format is described in rt_core's [logging.md](../../modules/eerie_leap_rt_core/docs/logging.md).

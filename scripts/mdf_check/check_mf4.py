#!/usr/bin/env python3
"""Summarize an MDF 4 log file written by rt_daq, with asammdf.

Prints the identifier (MDF = finalized, UnFinMF = cut by a power loss or a failure; asammdf
repairs those), the header comment and its device properties, the file history, and per
channel group: record size, cycle count, channels with unit and comment, sample count, time
range and step, and how many samples carry an invalidation bit (a failed reading).

Usage: check_mf4.py file.mf4 [file.mf4 ...]
"""
import struct
import sys
import warnings

import numpy as np
from asammdf import MDF


def summarize(path):
    with open(path, "rb") as f:
        head = f.read(64)

    flags = struct.unpack_from("<H", head, 60)[0]
    print(f"{path}: {head[:8].decode(errors='replace').strip()} {head[8:16].decode(errors='replace').strip()}"
          f", unfinalized flags 0x{flags:04x}")

    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        mdf = MDF(path)

    print(f"  start {mdf.header.start_time}; comment {mdf.header.description!r}")
    for name, value in getattr(mdf.header, "_common_properties", {}).items():
        print(f"    {name} = {value}")

    for index, group in enumerate(mdf.groups):
        channel_group = group.channel_group
        if channel_group.flags & 0x1:
            continue   # the VLSD group holding CAN payloads

        print(f"  group {channel_group.acq_name!r}: {channel_group.samples_byte_nr + channel_group.invalidation_bytes_nr}"
              f" data bytes, {channel_group.cycles_nr} records")

        for channel in group.channels:
            if channel.channel_type == 2:   # the master channel
                continue

            signal = mdf.get(channel.name, group=index, ignore_invalidation_bits=True)
            invalid = int(np.count_nonzero(signal.invalidation_bits)) if signal.invalidation_bits is not None else 0
            timestamps = signal.timestamps
            span = f"t {timestamps[0]:.4f}..{timestamps[-1]:.4f} s" if len(timestamps) else "no samples"
            step = f", step {np.diff(timestamps).mean() * 1000:.1f} ms" if len(timestamps) > 1 else ""

            print(f"    {channel.name}: {len(signal)} samples ({invalid} invalid), {span}{step}"
                  f", unit {signal.unit!r}")

    mdf.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)

    for file_path in sys.argv[1:]:
        summarize(file_path)

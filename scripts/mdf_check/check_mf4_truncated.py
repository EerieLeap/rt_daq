#!/usr/bin/env python3
"""Cut a finalized MDF4 file the way a full card or a power loss would, and check asammdf still reads it.

The copy is turned back into what the writer leaves before StopLogging(): identifier 'UnFinMF ',
standard flags 0x25 (cycle counters, last DT length, VLSD byte counts not updated), DT length 24
and every cycle count and VLSD byte count 0. It is then cut at every byte from the start of the
last tail_bytes to the end. asammdf must open each cut, return every channel, build a dataframe,
and give every group exactly the records that are complete in the cut (a CAN frame record counts
when it is complete; its payload record is written before it).

Usage: check_mf4_truncated.py file.mf4 [tail_bytes]
"""
import os
import struct
import sys
import tempfile
import warnings

from asammdf import MDF

ID_BLOCK_SIZE = 64
BLOCK_HEADER_SIZE = 24
CG_CYCLE_COUNT = BLOCK_HEADER_SIZE + 6 * 8 + 8   # after the links and cg_record_id
CG_FLAGS = CG_CYCLE_COUNT + 8
CG_DATA_BYTES = CG_FLAGS + 2 + 2 + 4             # cg_data_bytes, or with cg_inval_bytes the VLSD total
CG_RECORD_ID = BLOCK_HEADER_SIZE + 6 * 8
DG_RECORD_ID_SIZE = BLOCK_HEADER_SIZE + 4 * 8
RECORD_ID_FORMATS = {1: "<B", 2: "<H", 4: "<I", 8: "<Q"}


def blocks(data):
    offset = ID_BLOCK_SIZE
    while offset + BLOCK_HEADER_SIZE <= len(data):
        block_id = data[offset:offset + 4]
        length = struct.unpack_from("<Q", data, offset + 8)[0]
        yield offset, block_id, length
        if block_id == b"##DT":
            return
        offset += length


def unfinalize(data):
    data = bytearray(data)
    data[0:8] = b"UnFinMF "
    struct.pack_into("<H", data, 60, 0x25)
    dt_address = None
    for offset, block_id, length in blocks(data):
        if block_id == b"##CG":
            struct.pack_into("<Q", data, offset + CG_CYCLE_COUNT, 0)
            flags = struct.unpack_from("<H", data, offset + CG_FLAGS)[0]
            if flags & 0x1:
                struct.pack_into("<Q", data, offset + CG_DATA_BYTES, 0)
        elif block_id == b"##DT":
            dt_address = offset
            struct.pack_into("<Q", data, offset + 8, BLOCK_HEADER_SIZE)
    return bytes(data), dt_address


def layout(data):
    """Record id size and, per record id, the record size (None for a VLSD group), in CG order."""
    record_id_size = None
    groups = []
    for offset, block_id, length in blocks(data):
        if block_id == b"##DG" and record_id_size is None:
            record_id_size = data[offset + DG_RECORD_ID_SIZE]
        elif block_id == b"##CG":
            record_id = struct.unpack_from("<Q", data, offset + CG_RECORD_ID)[0]
            flags = struct.unpack_from("<H", data, offset + CG_FLAGS)[0]
            data_bytes, inval_bytes = struct.unpack_from("<II", data, offset + CG_DATA_BYTES)
            groups.append((record_id, None if flags & 0x1 else data_bytes + inval_bytes))
    return record_id_size, groups


def complete_records(data, dt_address, end, record_id_size, groups):
    """Per record id, how many records end at or before @p end."""
    sizes = dict(groups)
    result = {record_id: 0 for record_id, _ in groups}
    position = dt_address + BLOCK_HEADER_SIZE
    while position + record_id_size <= end:
        record_id = struct.unpack_from(RECORD_ID_FORMATS[record_id_size], data, position)[0]
        size = sizes[record_id]
        if size is None:
            if position + record_id_size + 4 > end:
                break
            size = 4 + struct.unpack_from("<I", data, position + record_id_size)[0]
        position += record_id_size + size
        if position > end:
            break
        result[record_id] += 1
    return result


def counts(path):
    """Samples per channel group index, read through every channel."""
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        mdf = MDF(path)
        result = {}
        for index, group in enumerate(mdf.groups):
            for channel in group.channels:
                if channel.name == "Timestamp":
                    continue
                # Invalid samples are records too; get() would drop them.
                result[index] = len(mdf.get(channel.name, group=index, ignore_invalidation_bits=True))
        mdf.to_dataframe()
        mdf.close()
    return result


def main():
    path = sys.argv[1]
    tail_bytes = int(sys.argv[2]) if len(sys.argv) > 2 else 120

    with open(path, "rb") as f:
        finalized = f.read()

    unfinalized, dt_address = unfinalize(finalized)
    if dt_address is None:
        sys.exit("no DT block")
    record_id_size, groups = layout(finalized)

    failures = 0
    first_cut = max(dt_address + BLOCK_HEADER_SIZE, len(unfinalized) - tail_bytes)
    with tempfile.TemporaryDirectory() as directory:
        cut_path = os.path.join(directory, "cut.mf4")
        for cut in range(first_cut, len(unfinalized) + 1):
            with open(cut_path, "wb") as f:
                f.write(unfinalized[:cut])
            try:
                result = counts(cut_path)
            except Exception as error:   # noqa: BLE001 - every reader failure is a finding here
                failures += 1
                print(f"cut at {cut}: {type(error).__name__}: {error}")
                continue
            complete = complete_records(unfinalized, dt_address, cut, record_id_size, groups)
            expected = {index: complete[record_id] for index, (record_id, size) in enumerate(groups) if size is not None}
            got = {index: result.get(index, 0) for index in expected}
            if got != expected:
                failures += 1
                print(f"cut at {cut}: samples per group {got}, complete records {expected}")

    cuts = len(unfinalized) + 1 - first_cut
    print(f"{path}: {cuts} cuts in the last {len(unfinalized) - first_cut} bytes, {failures} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()

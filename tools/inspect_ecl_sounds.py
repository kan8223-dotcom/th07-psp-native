#!/usr/bin/env python3
import struct
import sys


def decompress(src: bytes, expected_size: int) -> bytes:
    dictionary = bytearray(8192)
    head = 1
    byte_pos = 0
    bit_mask = 0x80
    cur_byte = 0
    out = bytearray()

    def read_bit() -> int:
        nonlocal byte_pos, bit_mask, cur_byte
        if bit_mask == 0x80:
            cur_byte = src[byte_pos] if byte_pos < len(src) else 0
            byte_pos += 1
        value = 1 if cur_byte & bit_mask else 0
        bit_mask >>= 1
        if bit_mask == 0:
            bit_mask = 0x80
        return value

    def read_bits(count: int) -> int:
        value = 0
        for _ in range(count):
            value = (value << 1) | read_bit()
        return value

    while True:
        if read_bit():
            value = read_bits(8)
            out.append(value)
            dictionary[head] = value
            head = (head + 1) & 0x1FFF
            continue
        offset = read_bits(13)
        if offset == 0:
            break
        length = read_bits(4) + 3
        for i in range(length):
            value = dictionary[(offset + i) & 0x1FFF]
            out.append(value)
            dictionary[head] = value
            head = (head + 1) & 0x1FFF
    if len(out) != expected_size:
        raise ValueError(f"decompressed {len(out)} bytes, expected {expected_size}")
    return bytes(out)


def extract_entry(archive_path: str, wanted_name: str) -> bytes:
    with open(archive_path, "rb") as stream:
        archive = stream.read()
    magic, count, header_offset, header_size = struct.unpack_from("<4sIII", archive)
    if magic != b"PBG4":
        raise ValueError(f"bad PBG4 magic: {magic!r}")
    header = decompress(archive[header_offset:], header_size)
    entries = []
    pos = 0
    for _ in range(count):
        end = header.index(0, pos)
        name = header[pos:end].decode("ascii")
        pos = end + 1
        data_offset, size, checksum = struct.unpack_from("<III", header, pos)
        pos += 12
        entries.append((name, data_offset, size, checksum))
    for idx, (name, data_offset, size, _) in enumerate(entries):
        if name.lower() != wanted_name.lower():
            continue
        next_offset = entries[idx + 1][1] if idx + 1 < len(entries) else header_offset
        return decompress(archive[data_offset:next_offset], size)
    raise KeyError(wanted_name)


def inspect_ecl(data: bytes) -> None:
    sub_count, timeline_count = struct.unpack_from("<hh", data)
    timeline_offsets = struct.unpack_from("<16I", data, 4)
    sub_offsets = struct.unpack_from(f"<{sub_count}I", data, 0x44)
    boundaries = sorted(set(sub_offsets + tuple(x for x in timeline_offsets[:timeline_count] if x)))
    boundaries.append(len(data))
    print(f"sub_count={sub_count} timeline_count={timeline_count} size={len(data)}")
    for sub_id, start in enumerate(sub_offsets):
        end = min(x for x in boundaries if x > start)
        pos = start
        while pos + 12 <= end:
            time, opcode, size, unused, difficulty, param_mask = struct.unpack_from(
                "<IhhBBH", data, pos
            )
            if size < 12 or pos + size > end:
                print(f"bad instruction sub={sub_id} off=0x{pos:x} size={size}")
                break
            if opcode == 105:
                arg0 = struct.unpack_from("<i", data, pos + 12)[0] if size >= 16 else None
                print(
                    f"sound sub={sub_id} time={time} off=0x{pos:x} "
                    f"arg0={arg0} mask=0x{param_mask:x} diff=0x{difficulty:x}"
                )
            pos += size
            if opcode == 1:
                break


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} TH07.DAT")
    inspect_ecl(extract_entry(sys.argv[1], "ecldata1.ecl"))

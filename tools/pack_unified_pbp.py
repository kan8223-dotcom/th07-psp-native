#!/usr/bin/env python3
"""Pack one model-routing TH07 PSP release candidate without original artwork.

The outer PBP keeps the launcher's DATA.PSP and replaces DATA.PSAR with a
small, versioned container holding the complete PSP-1000 and PSP-2000+
payload PBPs plus the fixed Slim+ GE4 wrapper used by model 1 and newer.
Every PBP input must have empty PBP media slots. The output has
fixed-length ICON0/PIC1 slots containing fully transparent, tool-generated
neutral PNGs. The PSP may overwrite only those two fixed slots after validating
the owner's original data; the PBP offset table and executables never move.

No generated image or sound file is an input to this tool.
"""

from __future__ import annotations

import argparse
import binascii
import os
from dataclasses import dataclass
from pathlib import Path
import struct
import sys
import tempfile
from typing import Sequence
import zlib


PBP_MAGIC = b"\x00PBP"
PBP_HEADER = struct.Struct("<4sI8I")
PBP_SECTION_NAMES = (
    "PARAM.SFO",
    "ICON0.PNG",
    "ICON1.PMF",
    "PIC0.PNG",
    "PIC1.PNG",
    "SND0.AT3",
    "DATA.PSP",
    "DATA.PSAR",
)
PBP_MEDIA_INDICES = (1, 2, 3, 4, 5)

PSF_MAGIC = b"\x00PSF"
PSF_HEADER = struct.Struct("<4sIIII")
PSF_ENTRY = struct.Struct("<HHIII")
PSF_FORMAT_BINARY = 0x0004
PSF_FORMAT_STRING = 0x0204

CONTAINER_MAGIC = b"TH07UP02"
CONTAINER_VERSION = 2
CONTAINER_HEADER = struct.Struct("<8sII")
CONTAINER_ENTRY = struct.Struct("<IIIIII")
PROFILE_PSP1000 = 0x1000
PROFILE_PSP2000PLUS = 0x2000
COMPANION_GE4 = 0x4734
UINT32_MAX = 0xFFFFFFFF

GE4_WRAPPER_SIZE = 2150
GE4_WRAPPER_CRC32 = 0xDAEBF3F3
GE4_WRAPPER_SHA256 = (
    "3dc5c753497349d6fb0ab5ae2a819b240cc51e8aa412ded10bb52daa540d841d"
)

XMB_MARKER_MAGIC = b"TH07XMB2"
XMB_MARKER = struct.Struct("<8sII")
XMB_SFO_KEY = "TH07_XMB_SLOT"
XMB_SFO_MAX_SIZE = 64 * 1024
XMB_ICON0_SLOT_SIZE = 64 * 1024
XMB_PIC1_SLOT_SIZE = 512 * 1024
XMB_SLOT_CHUNK_TYPE = b"thSb"
XMB_PLACEHOLDER_TAG = b"TH07PLN2"
XMB_WRAPPED_TAG = XMB_MARKER_MAGIC
XMB_ICON0_ROLE = b"I"
XMB_PIC1_ROLE = b"P"
XMB_ICON0_DIMENSIONS = (144, 80)
XMB_PIC1_DIMENSIONS = (480, 272)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

DEFAULT_TITLE = "東方妖々夢 ～ Perfect Cherry Blossom."
TITLE_MIN_CAPACITY = 128


class PackError(ValueError):
    """An input cannot safely be used to produce a unified release PBP."""


@dataclass(frozen=True)
class PbpImage:
    label: str
    raw: bytes
    version: int
    offsets: tuple[int, ...]
    parts: tuple[bytes, ...]


@dataclass(frozen=True)
class _SfoValue:
    key: str
    value_format: int
    value_length: int
    value_capacity: int
    slot: bytes


def _align(value: int, alignment: int = 4) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def parse_pbp(data: bytes, label: str = "PBP") -> PbpImage:
    """Parse a PBP and reject ambiguous or out-of-bounds section tables."""

    if len(data) < PBP_HEADER.size:
        raise PackError(f"{label}: truncated PBP header")
    magic, version, *raw_offsets = PBP_HEADER.unpack_from(data)
    if magic != PBP_MAGIC:
        raise PackError(f"{label}: not a PBP")
    offsets = tuple(raw_offsets)
    if offsets[0] < PBP_HEADER.size:
        raise PackError(f"{label}: PARAM.SFO overlaps the PBP header")
    if tuple(sorted(offsets)) != offsets:
        raise PackError(f"{label}: PBP offsets are not monotonic")
    if offsets[-1] > len(data):
        raise PackError(f"{label}: PBP section offset exceeds file size")

    ends = offsets[1:] + (len(data),)
    parts = tuple(data[start:end] for start, end in zip(offsets, ends))
    return PbpImage(label, data, version, offsets, parts)


def _require_empty_media(pbp: PbpImage) -> None:
    populated = [PBP_SECTION_NAMES[index] for index in PBP_MEDIA_INDICES
                 if pbp.parts[index]]
    if populated:
        raise PackError(
            f"{pbp.label}: bundled XMB media is forbidden: "
            + ", ".join(populated)
        )


def _png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    if len(chunk_type) != 4 or not all(
        ord("A") <= byte <= ord("Z") or ord("a") <= byte <= ord("z")
        for byte in chunk_type
    ):
        raise PackError("invalid PNG chunk type")
    crc = binascii.crc32(chunk_type)
    crc = binascii.crc32(payload, crc) & UINT32_MAX
    return struct.pack(">I", len(payload)) + chunk_type + payload + struct.pack(">I", crc)


def build_neutral_placeholder_png(
    width: int, height: int, slot_size: int, role: bytes
) -> bytes:
    """Return one exact-size, fully transparent, non-original PNG slot."""

    if width <= 0 or height <= 0 or len(role) != 1:
        raise PackError("invalid neutral XMB placeholder geometry")
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    transparent_rows = (b"\x00" * (1 + width * 4)) * height
    idat = zlib.compress(transparent_rows, 9)
    prefix = b"".join(
        (
            PNG_SIGNATURE,
            _png_chunk(b"IHDR", ihdr),
            _png_chunk(b"IDAT", idat),
        )
    )
    iend = _png_chunk(b"IEND", b"")
    fixed_overhead = len(prefix) + 12 + len(iend)
    tag = XMB_PLACEHOLDER_TAG + role
    if slot_size < fixed_overhead + len(tag):
        raise PackError("neutral XMB placeholder does not fit its fixed slot")
    padding = tag + b"\x00" * (slot_size - fixed_overhead - len(tag))
    output = prefix + _png_chunk(XMB_SLOT_CHUNK_TYPE, padding) + iend
    if len(output) != slot_size:
        raise AssertionError("neutral XMB placeholder size mismatch")
    return output


def neutral_xmb_media() -> tuple[bytes, bytes]:
    icon = build_neutral_placeholder_png(
        *XMB_ICON0_DIMENSIONS, XMB_ICON0_SLOT_SIZE, XMB_ICON0_ROLE
    )
    picture = build_neutral_placeholder_png(
        *XMB_PIC1_DIMENSIONS, XMB_PIC1_SLOT_SIZE, XMB_PIC1_ROLE
    )
    return icon, picture


def _require_neutral_outer_media(pbp: PbpImage) -> None:
    icon, picture = neutral_xmb_media()
    expected = (icon, b"", b"", picture, b"")
    if pbp.parts[1:6] != expected:
        raise PackError(
            f"{pbp.label}: outer XMB media is not the exact neutral fixed-slot contract"
        )


def _read_pbp(path: Path, label: str) -> PbpImage:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise PackError(f"{label}: cannot read {path}: {exc}") from exc
    pbp = parse_pbp(data, label)
    _require_empty_media(pbp)
    if not pbp.parts[6]:
        raise PackError(f"{label}: DATA.PSP is empty")
    return pbp


def _parse_sfo(section: bytes, label: str) -> tuple[int, list[_SfoValue]]:
    if len(section) < PSF_HEADER.size:
        raise PackError(f"{label}: truncated PARAM.SFO")
    magic, version, key_offset, data_offset, count = PSF_HEADER.unpack_from(section)
    if magic != PSF_MAGIC:
        raise PackError(f"{label}: invalid PARAM.SFO magic")
    directory_end = PSF_HEADER.size + count * PSF_ENTRY.size
    if count > 1024 or not (directory_end <= key_offset <= data_offset <= len(section)):
        raise PackError(f"{label}: invalid PARAM.SFO tables")

    values: list[_SfoValue] = []
    seen: set[str] = set()
    for index in range(count):
        entry_at = PSF_HEADER.size + index * PSF_ENTRY.size
        key_rel, value_format, value_length, value_capacity, value_rel = (
            PSF_ENTRY.unpack_from(section, entry_at)
        )
        key_at = key_offset + key_rel
        if key_at < key_offset or key_at >= data_offset:
            raise PackError(f"{label}: PARAM.SFO key offset is invalid")
        key_end = section.find(b"\x00", key_at, data_offset)
        if key_end < 0:
            raise PackError(f"{label}: unterminated PARAM.SFO key")
        try:
            key = section[key_at:key_end].decode("ascii")
        except UnicodeDecodeError as exc:
            raise PackError(f"{label}: non-ASCII PARAM.SFO key") from exc
        if not key or key in seen:
            raise PackError(f"{label}: empty or duplicate PARAM.SFO key {key!r}")
        seen.add(key)
        if value_length > value_capacity:
            raise PackError(f"{label}: PARAM.SFO value length exceeds capacity")
        value_at = data_offset + value_rel
        value_end = value_at + value_capacity
        if value_at < data_offset or value_end > len(section):
            raise PackError(f"{label}: PARAM.SFO value is out of bounds")
        values.append(
            _SfoValue(
                key,
                value_format,
                value_length,
                value_capacity,
                section[value_at:value_end],
            )
        )
    return version, values


def _validate_release_title(title: str) -> bytes:
    if not title or "\x00" in title:
        raise PackError("release title must be non-empty and contain no NUL")
    if "beta" in title.casefold() or "tester" in title.casefold():
        raise PackError("release title must not contain Beta/tester markers")
    encoded = title.encode("utf-8") + b"\x00"
    if len(encoded) > TITLE_MIN_CAPACITY:
        raise PackError("release title is longer than the 127-byte XMB limit")
    return encoded


def rebuild_sfo_with_title(
    section: bytes,
    title: str,
    label: str = "launcher",
    *,
    fixed_xmb: bool = False,
) -> bytes:
    """Rebuild PARAM.SFO while preserving all fields except TITLE's value."""

    encoded_title = _validate_release_title(title)
    version, values = _parse_sfo(section, label)
    title_entries = [value for value in values if value.key == "TITLE"]
    if len(title_entries) != 1:
        raise PackError(f"{label}: PARAM.SFO must contain exactly one TITLE")
    if title_entries[0].value_format != PSF_FORMAT_STRING:
        raise PackError(f"{label}: PARAM.SFO TITLE is not a string")
    if any(value.key == XMB_SFO_KEY for value in values):
        raise PackError(f"{label}: input PARAM.SFO already has {XMB_SFO_KEY}")
    if fixed_xmb:
        contract = XMB_MARKER.pack(
            XMB_MARKER_MAGIC, XMB_ICON0_SLOT_SIZE, XMB_PIC1_SLOT_SIZE
        )
        values.append(
            _SfoValue(
                XMB_SFO_KEY,
                PSF_FORMAT_BINARY,
                len(contract),
                len(contract),
                contract,
            )
        )

    keys = bytearray()
    key_offsets: list[int] = []
    for value in values:
        key_offsets.append(len(keys))
        keys.extend(value.key.encode("ascii"))
        keys.append(0)
    keys.extend(b"\x00" * (_align(len(keys)) - len(keys)))

    key_table_offset = PSF_HEADER.size + len(values) * PSF_ENTRY.size
    data_table_offset = key_table_offset + len(keys)
    data = bytearray()
    directory: list[bytes] = []
    for index, value in enumerate(values):
        data.extend(b"\x00" * (_align(len(data)) - len(data)))
        value_offset = len(data)
        if value.key == "TITLE":
            capacity = max(value.value_capacity, TITLE_MIN_CAPACITY)
            slot = encoded_title + b"\x00" * (capacity - len(encoded_title))
            length = len(encoded_title)
        else:
            capacity = value.value_capacity
            slot = value.slot
            length = value.value_length
        directory.append(
            PSF_ENTRY.pack(
                key_offsets[index], value.value_format, length, capacity, value_offset
            )
        )
        data.extend(slot)

    return b"".join(
        (
            PSF_HEADER.pack(
                PSF_MAGIC, version, key_table_offset, data_table_offset, len(values)
            ),
            b"".join(directory),
            bytes(keys),
            bytes(data),
        )
    )


def read_xmb_sfo_contract(section: bytes, label: str = "PARAM.SFO") -> tuple[int, int]:
    """Return fixed ICON0/PIC1 sizes from the owned, valid SFO entry."""

    _version, values = _parse_sfo(section, label)
    matches = [value for value in values if value.key == XMB_SFO_KEY]
    if len(matches) != 1:
        raise PackError(f"{label}: must contain exactly one {XMB_SFO_KEY}")
    value = matches[0]
    if (
        value.value_format != PSF_FORMAT_BINARY
        or value.value_length != XMB_MARKER.size
        or value.value_capacity != XMB_MARKER.size
    ):
        raise PackError(f"{label}: invalid {XMB_SFO_KEY} format or length")
    magic, icon_bytes, picture_bytes = XMB_MARKER.unpack(value.slot)
    if magic != XMB_MARKER_MAGIC:
        raise PackError(f"{label}: invalid TH07XMB2 contract magic")
    return icon_bytes, picture_bytes


def validate_ge4_wrapper(ge4_wrapper: bytes) -> None:
    """Reject anything except the hardware-proven Slim+ GE4 companion."""

    import hashlib

    if len(ge4_wrapper) != GE4_WRAPPER_SIZE:
        raise PackError(
            f"GE4 wrapper size mismatch: expected {GE4_WRAPPER_SIZE}, "
            f"found {len(ge4_wrapper)}"
        )
    crc32 = binascii.crc32(ge4_wrapper) & UINT32_MAX
    if crc32 != GE4_WRAPPER_CRC32:
        raise PackError(
            f"GE4 wrapper CRC32 mismatch: expected {GE4_WRAPPER_CRC32:08X}, "
            f"found {crc32:08X}"
        )
    sha256 = hashlib.sha256(ge4_wrapper).hexdigest()
    if sha256 != GE4_WRAPPER_SHA256:
        raise PackError(
            f"GE4 wrapper SHA-256 mismatch: expected {GE4_WRAPPER_SHA256}, "
            f"found {sha256}"
        )


def build_payload_container(
    psp1000: bytes, psp2000plus: bytes, ge4_wrapper: bytes
) -> bytes:
    """Build the TH07UP02 v2 runtime-plus-companion DATA.PSAR container."""

    validate_ge4_wrapper(ge4_wrapper)
    payload_start = CONTAINER_HEADER.size + 3 * CONTAINER_ENTRY.size
    second_start = payload_start + len(psp1000)
    companion_start = second_start + len(psp2000plus)
    total_size = companion_start + len(ge4_wrapper)
    if not psp1000 or not psp2000plus or not ge4_wrapper:
        raise PackError("unified payloads must be non-empty")
    if total_size > UINT32_MAX:
        raise PackError("unified payload container exceeds the v2 32-bit limit")

    entries = (
        CONTAINER_ENTRY.pack(
            PROFILE_PSP1000,
            0,
            0,
            payload_start,
            len(psp1000),
            binascii.crc32(psp1000) & UINT32_MAX,
        ),
        CONTAINER_ENTRY.pack(
            PROFILE_PSP2000PLUS,
            1,
            UINT32_MAX,
            second_start,
            len(psp2000plus),
            binascii.crc32(psp2000plus) & UINT32_MAX,
        ),
        CONTAINER_ENTRY.pack(
            COMPANION_GE4,
            1,
            UINT32_MAX,
            companion_start,
            len(ge4_wrapper),
            binascii.crc32(ge4_wrapper) & UINT32_MAX,
        ),
    )
    return b"".join(
        (
            CONTAINER_HEADER.pack(CONTAINER_MAGIC, CONTAINER_VERSION, 3),
            *entries,
            psp1000,
            psp2000plus,
            ge4_wrapper,
        )
    )


def build_unified_pbp(
    launcher: PbpImage,
    psp1000: PbpImage,
    psp2000plus: PbpImage,
    ge4_wrapper: bytes,
    title: str = DEFAULT_TITLE,
    fixed_xmb: bool = True,
) -> bytes:
    """Return a validated unified PBP image without writing it."""

    for pbp in (launcher, psp1000, psp2000plus):
        _require_empty_media(pbp)
    if not launcher.parts[6]:
        raise PackError("launcher: DATA.PSP is empty")

    logical_sfo = rebuild_sfo_with_title(
        launcher.parts[0], title, launcher.label, fixed_xmb=fixed_xmb
    )
    data_psp = launcher.parts[6]
    data_psar = build_payload_container(
        psp1000.raw, psp2000plus.raw, ge4_wrapper
    )

    if not fixed_xmb:
        data_psp_start = PBP_HEADER.size + len(logical_sfo)
        data_psar_start = data_psp_start + len(data_psp)
        if data_psar_start + len(data_psar) > UINT32_MAX:
            raise PackError("unified PBP exceeds the 32-bit PBP offset limit")
        offsets = (
            PBP_HEADER.size,
            data_psp_start,
            data_psp_start,
            data_psp_start,
            data_psp_start,
            data_psp_start,
            data_psp_start,
            data_psar_start,
        )
        header = PBP_HEADER.pack(PBP_MAGIC, launcher.version, *offsets)
        output = b"".join((header, logical_sfo, data_psp, data_psar))
        checked = parse_pbp(output, "plain-XMB unified output")
        _require_empty_media(checked)
        if checked.parts[6] != data_psp or checked.parts[7] != data_psar:
            raise AssertionError("plain-XMB PBP assembly error")
        return output

    if len(logical_sfo) > XMB_SFO_MAX_SIZE:
        raise PackError("launcher PARAM.SFO exceeds the 64 KiB XMB contract bound")
    if read_xmb_sfo_contract(logical_sfo, "rebuilt launcher PARAM.SFO") != (
        XMB_ICON0_SLOT_SIZE,
        XMB_PIC1_SLOT_SIZE,
    ):
        raise AssertionError("fixed XMB SFO contract assembly error")
    icon, picture = neutral_xmb_media()
    icon_start = PBP_HEADER.size + len(logical_sfo)
    picture_start = icon_start + len(icon)
    data_psp_start = picture_start + len(picture)
    data_psar_start = data_psp_start + len(data_psp)
    if data_psar_start + len(data_psar) > UINT32_MAX:
        raise PackError("unified PBP exceeds the 32-bit PBP offset limit")

    offsets = (
        PBP_HEADER.size,
        icon_start,
        picture_start,
        picture_start,
        picture_start,
        data_psp_start,
        data_psp_start,
        data_psar_start,
    )
    header = PBP_HEADER.pack(PBP_MAGIC, launcher.version, *offsets)
    output = b"".join((header, logical_sfo, icon, picture, data_psp, data_psar))

    # Keep the constitutional boundary executable: initial media is the exact
    # tool-generated transparent contract and the launcher survives byte-for-byte.
    checked = parse_pbp(output, "unified output")
    _require_neutral_outer_media(checked)
    if checked.parts[6] != data_psp or checked.parts[7] != data_psar:
        raise AssertionError("internal PBP assembly error")
    return output


def pack_unified_pbp(
    launcher_path: Path | str,
    psp1000_path: Path | str,
    psp2000plus_path: Path | str,
    ge4_wrapper_path: Path | str,
    output_path: Path | str,
    title: str = DEFAULT_TITLE,
    fixed_xmb: bool = True,
) -> bytes:
    """Read, validate, pack, and atomically write one unified release PBP."""

    launcher_path = Path(launcher_path)
    psp1000_path = Path(psp1000_path)
    psp2000plus_path = Path(psp2000plus_path)
    ge4_wrapper_path = Path(ge4_wrapper_path)
    output_path = Path(output_path)
    inputs = tuple(path.resolve() for path in (
        launcher_path, psp1000_path, psp2000plus_path, ge4_wrapper_path
    ))
    if len(set(inputs)) != len(inputs):
        raise PackError("launcher and profile payload inputs must be distinct files")
    if output_path.resolve() in inputs:
        raise PackError("output must not overwrite an input PBP")

    launcher = _read_pbp(launcher_path, "launcher")
    psp1000 = _read_pbp(psp1000_path, "PSP-1000 payload")
    psp2000plus = _read_pbp(psp2000plus_path, "PSP-2000+ payload")
    try:
        ge4_wrapper = ge4_wrapper_path.read_bytes()
    except OSError as exc:
        raise PackError(
            f"GE4 wrapper: cannot read {ge4_wrapper_path}: {exc}"
        ) from exc
    output = build_unified_pbp(
        launcher, psp1000, psp2000plus, ge4_wrapper, title,
        fixed_xmb=fixed_xmb
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    temp_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            prefix=f".{output_path.name}.",
            suffix=".tmp",
            dir=output_path.parent,
            delete=False,
        ) as handle:
            temp_name = handle.name
            handle.write(output)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp_name, output_path)
        temp_name = None
    finally:
        if temp_name is not None:
            try:
                os.unlink(temp_name)
            except FileNotFoundError:
                pass
    return output


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Pack one runtime-routed TH07 PSP release-candidate EBOOT"
    )
    parser.add_argument("--launcher", required=True, type=Path,
                        help="plain launcher PBP with empty media slots")
    parser.add_argument("--psp1000", required=True, type=Path,
                        help="PSP-1000 payload PBP with empty media slots")
    parser.add_argument("--psp2000plus", required=True, type=Path,
                        help="PSP-2000+ payload PBP with empty media slots")
    parser.add_argument("--ge4wrap", required=True, type=Path,
                        help="fixed Slim+ ge4wrap_texv1.prx companion")
    parser.add_argument("--output", required=True, type=Path,
                        help="output unified EBOOT.PBP")
    parser.add_argument("--title", default=DEFAULT_TITLE,
                        help=f"release XMB title (default: {DEFAULT_TITLE})")
    parser.add_argument(
        "--plain-xmb-test",
        action="store_true",
        help="omit self-wrap reserve/media for a real-XMB model-routing gate",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    try:
        output = pack_unified_pbp(
            args.launcher,
            args.psp1000,
            args.psp2000plus,
            args.ge4wrap,
            args.output,
            args.title,
            fixed_xmb=not args.plain_xmb_test,
        )
    except PackError as exc:
        parser.error(str(exc))
    xmb_contract = (
        "empty standard XMB media (hardware test)"
        if args.plain_xmb_test else "transparent fixed XMB slots"
    )
    print(f"OK: {args.output} ({len(output)} bytes; {xmb_contract}; "
          "profiles=PSP-1000,PSP-2000+; companion=GE4-Slim+)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

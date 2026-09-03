#!/usr/bin/env python3
"""Reject original TH07 data and locally generated XMB media.

The distributable unified candidate has exact tool-generated, fully transparent
ICON0/PIC1 placeholders and empty ICON1/PIC0/SND0 slots. After the owner supplies
valid original data, the PSP may overwrite the two fixed slots in that *local*
copy. Such a self-wrapped copy must never be committed or redistributed.

Typical pre-commit use::

    git diff --cached --name-only -z --diff-filter=ACMR \
      | xargs -0 python3 tools/check_no_original_assets.py
"""

from __future__ import annotations

import os
from pathlib import Path
import struct
import sys
from typing import Iterable

try:
    from . import pack_unified_pbp as packer
except ImportError:  # Direct `python3 tools/check_no_original_assets.py` use.
    import pack_unified_pbp as packer


PBP_HEADER = struct.Struct("<4sI8I")
PBP_MEDIA_PARTS = (1, 2, 3, 4, 5)
PBP_MEDIA_NAMES = {
    1: "ICON0.PNG",
    2: "ICON1.PMF",
    3: "PIC0.PNG",
    4: "PIC1.PNG",
    5: "SND0.AT3",
}

UNIFIED_HEADER = struct.Struct("<8sII")
UNIFIED_ENTRY = struct.Struct("<IIIIII")
UNIFIED_MAGIC = packer.CONTAINER_MAGIC

BAD_NAMES = {
    "icon0.png",
    "icon0_go_me.png",
    "icon1.pmf",
    "pic0.png",
    "pic1.png",
    "snd0.at3",
    "th07.dat",
    "thbgm.dat",
}


class GuardError(ValueError):
    """A file crosses the repository/distribution asset boundary."""


def _pbp_parts(data: bytes, label: str) -> tuple[bytes, ...]:
    if len(data) < PBP_HEADER.size:
        raise GuardError(f"{label}: truncated PBP")
    magic, _version, *offsets = PBP_HEADER.unpack_from(data)
    if magic != b"\x00PBP":
        raise GuardError(f"{label}: invalid PBP magic")
    if offsets[0] < PBP_HEADER.size or offsets != sorted(offsets):
        raise GuardError(f"{label}: invalid PBP offsets")
    if offsets[-1] > len(data):
        raise GuardError(f"{label}: PBP offset exceeds file size")
    ends = offsets[1:] + [len(data)]
    return tuple(data[start:end] for start, end in zip(offsets, ends))


def _check_pbp(data: bytes, label: str) -> None:
    parts = _pbp_parts(data, label)
    psar = parts[7]
    is_unified = psar.startswith(UNIFIED_MAGIC)
    if is_unified:
        try:
            parsed = packer.parse_pbp(data, label)
            packer._require_neutral_outer_media(parsed)
        except packer.PackError as exc:
            raise GuardError(
                f"{label}: outer XMB media is not the exact neutral placeholder contract"
            ) from exc
    else:
        populated = [PBP_MEDIA_NAMES[index] for index in PBP_MEDIA_PARTS
                     if parts[index]]
        if populated:
            raise GuardError(
                f"{label}: XMB media is populated ({', '.join(populated)})"
            )

    # The unified candidate embeds two complete runtime PBPs and one raw GE4
    # companion in DATA.PSAR. Audit the two nested PBPs for derived media.
    # Audit those payloads too; neutral outer placeholders must not be usable as
    # a wrapper around a derived image in either hidden profile.
    if not psar.startswith(UNIFIED_MAGIC):
        return
    if len(psar) < UNIFIED_HEADER.size:
        raise GuardError(f"{label}: truncated TH07UP02 header")
    magic, version, count = UNIFIED_HEADER.unpack_from(psar)
    if (magic != UNIFIED_MAGIC or version != packer.CONTAINER_VERSION or
            count != 3):
        raise GuardError(f"{label}: unsupported TH07UP02 container")
    table_end = UNIFIED_HEADER.size + count * UNIFIED_ENTRY.size
    if table_end > len(psar):
        raise GuardError(f"{label}: truncated TH07UP02 table")
    expected_profiles = (
        packer.PROFILE_PSP1000,
        packer.PROFILE_PSP2000PLUS,
        packer.COMPANION_GE4,
    )
    expected_offset = table_end
    for index in range(count):
        entry = UNIFIED_ENTRY.unpack_from(
            psar, UNIFIED_HEADER.size + index * UNIFIED_ENTRY.size
        )
        profile, _model_min, _model_max, offset, size, _crc32 = entry
        end = offset + size
        if (profile != expected_profiles[index] or size == 0 or
                offset != expected_offset or end > len(psar)):
            raise GuardError(f"{label}: invalid TH07UP02 payload {index}")
        if profile in (packer.PROFILE_PSP1000, packer.PROFILE_PSP2000PLUS):
            _check_pbp(psar[offset:end], f"{label}/profile-{profile:08x}")
        elif profile != packer.COMPANION_GE4:
            raise GuardError(f"{label}: unknown TH07UP02 member {profile:08x}")
        expected_offset = end
    if expected_offset != len(psar):
        raise GuardError(f"{label}: trailing TH07UP02 data")


def check_path(path: Path) -> str | None:
    if path.name.casefold() in BAD_NAMES:
        return f"forbidden original/generated filename: {path.name}"
    try:
        size = path.stat().st_size
        with path.open("rb") as handle:
            head = handle.read(64 * 1024)
            if head[:4] == b"\x00PBP":
                handle.seek(0)
                data = handle.read()
            else:
                data = b""
    except (FileNotFoundError, IsADirectoryError):
        return None
    except OSError as exc:
        return f"cannot inspect file: {exc}"

    if head[:4] == b"PBG4":
        return "PBG4 archive (th07.dat/original-data derivative)"
    if size > 400 * 1024 * 1024:
        return "file exceeds 400 MiB (thbgm.dat suspected)"
    if head[:4] == b"\x00PBP":
        try:
            _check_pbp(data, str(path))
        except GuardError as exc:
            return str(exc)
    if head[:4] == b"RIFF" and head[8:12] == b"WAVE":
        fmt = head.find(b"fmt ")
        if fmt >= 0 and fmt + 10 <= len(head):
            if struct.unpack_from("<H", head, fmt + 8)[0] == 0x0270:
                return "ATRAC3 media (SND0 derivative suspected)"

    # Do not flag source code which necessarily contains these magic strings.
    try:
        head.decode("utf-8")
    except UnicodeDecodeError:
        if b"THTX" in head:
            return "extracted THTX texture"
    return None


def audit(paths: Iterable[str]) -> list[tuple[str, str]]:
    failures: list[tuple[str, str]] = []
    for raw_path in paths:
        reason = check_path(Path(raw_path))
        if reason is not None:
            failures.append((raw_path, reason))
    return failures


def main(argv: list[str] | None = None) -> int:
    failures = audit(sys.argv[1:] if argv is None else argv)
    if not failures:
        return 0
    print(
        "commit/release rejected: original data or locally generated media found",
        file=sys.stderr,
    )
    for path, reason in failures:
        print(f"  {path}: {reason}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

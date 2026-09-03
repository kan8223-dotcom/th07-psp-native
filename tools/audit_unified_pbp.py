#!/usr/bin/env python3
"""Fail-closed structural audit for a unified TH07 PSP candidate.

Passing this audit proves package structure and exact payload identity only.
It is not a real-hardware or public-release verdict.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
from pathlib import Path
import sys

import pack_unified_pbp as packer


class AuditError(ValueError):
    pass


def _outer_title(sfo: bytes) -> str:
    _version, values = packer._parse_sfo(sfo, "unified PARAM.SFO")
    titles = [value for value in values if value.key == "TITLE"]
    if len(titles) != 1 or titles[0].value_format != packer.PSF_FORMAT_STRING:
        raise AuditError("outer PARAM.SFO does not contain one string TITLE")
    raw = titles[0].slot[:titles[0].value_length]
    try:
        return raw.split(b"\x00", 1)[0].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise AuditError("outer PARAM.SFO TITLE is not UTF-8") from exc


def _payloads(psar: bytes) -> dict[int, bytes]:
    minimum = (packer.CONTAINER_HEADER.size +
               3 * packer.CONTAINER_ENTRY.size)
    if len(psar) < minimum:
        raise AuditError("DATA.PSAR is truncated")
    magic, version, count = packer.CONTAINER_HEADER.unpack_from(psar)
    if (magic, version, count) != (
        packer.CONTAINER_MAGIC, packer.CONTAINER_VERSION, 3
    ):
        raise AuditError("DATA.PSAR is not the supported TH07UP02 v2 container")

    expected_members = (
        (packer.PROFILE_PSP1000, 0, 0),
        (packer.PROFILE_PSP2000PLUS, 1, packer.UINT32_MAX),
        (packer.COMPANION_GE4, 1, packer.UINT32_MAX),
    )
    result: dict[int, bytes] = {}
    expected_offset = minimum
    for index in range(count):
        entry = packer.CONTAINER_ENTRY.unpack_from(
            psar,
            packer.CONTAINER_HEADER.size + index * packer.CONTAINER_ENTRY.size,
        )
        profile, model_min, model_max, offset, size, crc32 = entry
        if (profile, model_min, model_max) != expected_members[index]:
            raise AuditError(f"wrong DATA.PSAR member at index {index}")
        end = offset + size
        if size == 0 or offset < minimum or end > len(psar):
            raise AuditError(f"payload 0x{profile:08X} is out of bounds")
        if offset != expected_offset:
            raise AuditError(
                f"payload 0x{profile:08X} is not canonically contiguous"
            )
        payload = psar[offset:end]
        if (binascii.crc32(payload) & packer.UINT32_MAX) != crc32:
            raise AuditError(f"payload 0x{profile:08X} CRC32 mismatch")
        if profile == packer.COMPANION_GE4:
            try:
                packer.validate_ge4_wrapper(payload)
            except packer.PackError as exc:
                raise AuditError(str(exc)) from exc
        else:
            nested = packer.parse_pbp(payload, f"profile 0x{profile:08X}")
            packer._require_empty_media(nested)
            if not nested.parts[6]:
                raise AuditError(f"payload 0x{profile:08X} has empty DATA.PSP")
        result[profile] = payload
        expected_offset = end
    if expected_offset != len(psar):
        raise AuditError("DATA.PSAR has trailing bytes")
    return result


def audit(
    path: Path,
    expected_title: str,
    expected_psp1000_sha256: str | None = None,
    expected_psp2000plus_sha256: str | None = None,
    plain_xmb_test: bool = False,
) -> tuple[str, dict[int, str]]:
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise AuditError(f"cannot read {path}: {exc}") from exc
    try:
        pbp = packer.parse_pbp(raw, str(path))
        if plain_xmb_test:
            packer._require_empty_media(pbp)
        else:
            packer._require_neutral_outer_media(pbp)
    except packer.PackError as exc:
        raise AuditError(str(exc)) from exc
    if not pbp.parts[6]:
        raise AuditError("outer launcher DATA.PSP is empty")

    title = _outer_title(pbp.parts[0])
    if title != expected_title:
        raise AuditError(
            f"outer TITLE mismatch: expected {expected_title!r}, found {title!r}"
        )
    folded_title = title.casefold()
    if "beta" in folded_title or "tester" in folded_title:
        raise AuditError("outer TITLE still carries Beta/tester branding")

    if plain_xmb_test:
        if len(pbp.parts[0]) >= 4096:
            raise AuditError("plain-XMB PARAM.SFO is unexpectedly large")
        if packer.XMB_MARKER_MAGIC in pbp.parts[0]:
            raise AuditError("plain-XMB PARAM.SFO still contains a self-wrap marker")
    else:
        if len(pbp.parts[0]) >= 4096:
            raise AuditError("fixed-XMB PARAM.SFO is unexpectedly large")
        try:
            slot_sizes = packer.read_xmb_sfo_contract(
                pbp.parts[0], "unified PARAM.SFO"
            )
        except packer.PackError as exc:
            raise AuditError(str(exc)) from exc
        if slot_sizes != (
            packer.XMB_ICON0_SLOT_SIZE,
            packer.XMB_PIC1_SLOT_SIZE,
        ):
            raise AuditError("TH07XMB2 SFO slot sizes do not match the contract")
        expected_icon_start = pbp.offsets[0] + len(pbp.parts[0])
        expected_picture_start = expected_icon_start + packer.XMB_ICON0_SLOT_SIZE
        expected_data_start = expected_picture_start + packer.XMB_PIC1_SLOT_SIZE
        if pbp.offsets[1:7] != (
            expected_icon_start,
            expected_picture_start,
            expected_picture_start,
            expected_picture_start,
            expected_data_start,
            expected_data_start,
        ):
            raise AuditError("TH07XMB2 fixed media offsets do not match the contract")

    payloads = _payloads(pbp.parts[7])
    hashes = {
        profile: hashlib.sha256(payload).hexdigest()
        for profile, payload in payloads.items()
    }
    expected_hashes = {
        packer.PROFILE_PSP1000: expected_psp1000_sha256,
        packer.PROFILE_PSP2000PLUS: expected_psp2000plus_sha256,
    }
    for profile, expected in expected_hashes.items():
        if expected is not None and hashes[profile].casefold() != expected.casefold():
            raise AuditError(
                f"payload 0x{profile:08X} SHA-256 mismatch: {hashes[profile]}"
            )
    return title, hashes


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pbp", type=Path)
    parser.add_argument("--title", default=packer.DEFAULT_TITLE)
    parser.add_argument("--psp1000-sha256")
    parser.add_argument("--psp2000plus-sha256")
    parser.add_argument("--plain-xmb-test", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        title, hashes = audit(
            args.pbp,
            args.title,
            args.psp1000_sha256,
            args.psp2000plus_sha256,
            args.plain_xmb_test,
        )
    except (AuditError, packer.PackError) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    print(f"[OK] structurally valid unified candidate (hardware gate still required): {args.pbp}")
    print(f"  TITLE={title}")
    print(f"  PSP-1000={hashes[packer.PROFILE_PSP1000]}")
    print(f"  PSP-2000+={hashes[packer.PROFILE_PSP2000PLUS]}")
    print(f"  GE4-wrapper={hashes[packer.COMPANION_GE4]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

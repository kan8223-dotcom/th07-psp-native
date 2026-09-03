from __future__ import annotations

import binascii
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import check_no_original_assets as guard  # noqa: E402
import pack_unified_pbp as packer  # noqa: E402


PBP_HEADER = struct.Struct("<4sI8I")
UNIFIED_HEADER = struct.Struct("<8sII")
UNIFIED_ENTRY = struct.Struct("<IIIIII")


def make_pbp(
    data_psp: bytes,
    *,
    icon0: bytes = b"",
    pic1: bytes = b"",
    data_psar: bytes = b"",
) -> bytes:
    parts = [
        b"neutral-param-sfo",
        icon0,
        b"",
        b"",
        pic1,
        b"",
        data_psp,
        data_psar,
    ]
    offsets = []
    offset = PBP_HEADER.size
    for part in parts:
        offsets.append(offset)
        offset += len(part)
    return PBP_HEADER.pack(b"\x00PBP", 0x10000, *offsets) + b"".join(parts)


def make_unified(psp1000: bytes, psp2000plus: bytes) -> bytes:
    ge4 = b"test-ge4-companion"
    payload_offset = UNIFIED_HEADER.size + 3 * UNIFIED_ENTRY.size
    second_offset = payload_offset + len(psp1000)
    companion_offset = second_offset + len(psp2000plus)
    table = b"".join(
        (
            UNIFIED_HEADER.pack(
                packer.CONTAINER_MAGIC, packer.CONTAINER_VERSION, 3
            ),
            UNIFIED_ENTRY.pack(
                0x1000,
                0,
                0,
                payload_offset,
                len(psp1000),
                binascii.crc32(psp1000) & 0xFFFFFFFF,
            ),
            UNIFIED_ENTRY.pack(
                0x2000,
                1,
                0xFFFFFFFF,
                second_offset,
                len(psp2000plus),
                binascii.crc32(psp2000plus) & 0xFFFFFFFF,
            ),
            UNIFIED_ENTRY.pack(
                packer.COMPANION_GE4,
                1,
                0xFFFFFFFF,
                companion_offset,
                len(ge4),
                binascii.crc32(ge4) & 0xFFFFFFFF,
            ),
        )
    )
    icon, picture = packer.neutral_xmb_media()
    return make_pbp(
        b"unified-launcher-data.psp",
        icon0=icon,
        pic1=picture,
        data_psar=table + psp1000 + psp2000plus + ge4,
    )


class NoOriginalAssetsGuardTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write(self, name: str, data: bytes) -> Path:
        path = self.base / name
        path.write_bytes(data)
        return path

    def test_clean_unified_pbp_is_accepted(self) -> None:
        image = make_unified(
            make_pbp(b"psp1000-runtime"),
            make_pbp(b"psp2000plus-runtime"),
        )
        path = self.write("EBOOT.PBP", image)
        self.assertIsNone(guard.check_path(path))
        self.assertEqual(guard.audit([str(path)]), [])

    def test_renamed_xmb_icon_is_rejected(self) -> None:
        path = self.write("ICON0_GO_ME.png", b"\x89PNG\r\n\x1a\nlocal-icon")
        reason = guard.check_path(path)
        self.assertIsNotNone(reason)
        self.assertIn("generated filename", reason)

    def test_outer_icon0_and_pic1_are_rejected(self) -> None:
        populated = {
            "ICON0.PNG": {"icon0": b"\x89PNG\r\n\x1a\nlocal-icon"},
            "PIC1.PNG": {"pic1": b"\x89PNG\r\n\x1a\nlocal-background"},
        }
        for label, media in populated.items():
            with self.subTest(slot=label):
                path = self.write(
                    f"outer-{label}.PBP",
                    make_pbp(b"launcher", **media),
                )
                reason = guard.check_path(path)
                self.assertIsNotNone(reason)
                self.assertIn(label, reason)

    def test_unified_outer_requires_exact_neutral_placeholders(self) -> None:
        nested1000 = make_pbp(b"psp1000-runtime")
        nested2000 = make_pbp(b"psp2000plus-runtime")
        image = bytearray(make_unified(nested1000, nested2000))
        _magic, _version, *offsets = PBP_HEADER.unpack_from(image)
        image[offsets[1] + 64] ^= 0x01
        path = self.write("modified-unified.PBP", bytes(image))
        reason = guard.check_path(path)
        self.assertIsNotNone(reason)
        self.assertIn("neutral placeholder contract", reason)

    def test_nested_profile_media_is_rejected_when_outer_slots_are_empty(self) -> None:
        nested = make_pbp(
            b"psp1000-runtime",
            icon0=b"\x89PNG\r\n\x1a\nlocally-generated-icon",
        )
        image = make_unified(nested, make_pbp(b"psp2000plus-runtime"))
        path = self.write("EBOOT.PBP", image)

        reason = guard.check_path(path)
        self.assertIsNotNone(reason)
        self.assertIn("profile-00001000", reason)
        self.assertIn("ICON0.PNG", reason)

    def test_dangerous_filenames_are_rejected_case_insensitively(self) -> None:
        names = (
            "ICON0.PNG",
            "Icon1.PmF",
            "PIC0.PNG",
            "pic1.png",
            "SND0.AT3",
            "th07.dat",
            "THBGM.DAT",
        )
        for name in names:
            with self.subTest(name=name):
                path = self.write(name, b"otherwise harmless")
                reason = guard.check_path(path)
                self.assertIsNotNone(reason)
                self.assertIn("forbidden original/generated filename", reason)

    def test_textual_source_may_name_magic_without_becoming_an_asset(self) -> None:
        source = b"""#include <string.h>\n
// Parsers intentionally name PBG4, THTX, RIFF/WAVE, TH07UP02, and TH07XMB2.
static const char *formats = "PBG4 THTX RIFF WAVE TH07UP02 TH07XMB2";
static const char *slots = "ICON0.PNG PIC1.PNG SND0.AT3";
"""
        path = self.write("format_signatures.cpp", source)
        self.assertIsNone(guard.check_path(path))


if __name__ == "__main__":
    unittest.main()

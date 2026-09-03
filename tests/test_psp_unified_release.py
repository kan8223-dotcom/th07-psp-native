from __future__ import annotations

import binascii
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import pack_unified_pbp as packer  # noqa: E402
import audit_unified_pbp as auditor  # noqa: E402
import check_no_original_assets as asset_guard  # noqa: E402


PBP_HEADER = struct.Struct("<4sI8I")
PSF_HEADER = struct.Struct("<4sIIII")
PSF_ENTRY = struct.Struct("<HHIII")


def align4(value: int) -> int:
    return (value + 3) & ~3


def make_sfo(title: str = "Touhou 7 PSP Beta") -> bytes:
    fields = (
        ("MEMSIZE", 0x0404, struct.pack("<I", 1), 4),
        ("BOOTABLE", 0x0404, struct.pack("<I", 1), 4),
        ("CATEGORY", 0x0204, b"MG\x00", 4),
        ("TITLE", 0x0204, title.encode("utf-8") + b"\x00", 64),
    )
    keys = bytearray()
    key_offsets = []
    for name, _fmt, _value, _capacity in fields:
        key_offsets.append(len(keys))
        keys.extend(name.encode("ascii") + b"\x00")
    keys.extend(b"\x00" * (align4(len(keys)) - len(keys)))

    key_table = PSF_HEADER.size + len(fields) * PSF_ENTRY.size
    data_table = key_table + len(keys)
    data = bytearray()
    entries = []
    for index, (_name, value_format, value, capacity) in enumerate(fields):
        data.extend(b"\x00" * (align4(len(data)) - len(data)))
        value_offset = len(data)
        slot = value + b"\x00" * (capacity - len(value))
        entries.append(
            PSF_ENTRY.pack(
                key_offsets[index], value_format, len(value), capacity, value_offset
            )
        )
        data.extend(slot)
    return b"".join(
        (
            PSF_HEADER.pack(b"\x00PSF", 0x101, key_table, data_table, len(fields)),
            b"".join(entries),
            bytes(keys),
            bytes(data),
        )
    )


def make_pbp(
    data_psp: bytes,
    *,
    media_index: int | None = None,
    media_bytes: bytes = b"ORIGINAL_XMB_ASSET_DO_NOT_SHIP",
    data_psar: bytes = b"",
) -> bytes:
    parts = [make_sfo(), b"", b"", b"", b"", b"", data_psp, data_psar]
    if media_index is not None:
        parts[media_index] = media_bytes
    offsets = []
    offset = PBP_HEADER.size
    for part in parts:
        offsets.append(offset)
        offset += len(part)
    return PBP_HEADER.pack(b"\x00PBP", 0x10000, *offsets) + b"".join(parts)


def pbp_parts(image: bytes) -> tuple[tuple[int, ...], tuple[bytes, ...]]:
    magic, _version, *offsets = PBP_HEADER.unpack_from(image)
    if magic != b"\x00PBP":
        raise AssertionError("test output is not a PBP")
    ends = tuple(offsets[1:]) + (len(image),)
    return tuple(offsets), tuple(
        image[start:end] for start, end in zip(offsets, ends)
    )


def read_sfo_strings(section: bytes) -> dict[str, str]:
    magic, _version, key_table, data_table, count = PSF_HEADER.unpack_from(section)
    if magic != b"\x00PSF":
        raise AssertionError("not a PARAM.SFO")
    result = {}
    for index in range(count):
        key_rel, value_format, value_length, _capacity, value_rel = (
            PSF_ENTRY.unpack_from(section, PSF_HEADER.size + index * PSF_ENTRY.size)
        )
        if value_format != 0x0204:
            continue
        key_start = key_table + key_rel
        key_end = section.index(0, key_start, data_table)
        key = section[key_start:key_end].decode("ascii")
        value = section[data_table + value_rel:data_table + value_rel + value_length]
        result[key] = value.rstrip(b"\x00").decode("utf-8")
    return result


class PspUnifiedReleasePackerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.launcher_bytes = make_pbp(b"LAUNCHER-DATA.PSP\x00\x01")
        self.psp1000_bytes = make_pbp(
            b"PSP1000-E480-DATA.PSP\x10\x00", data_psar=b"profile-1000-tail"
        )
        self.psp2000_bytes = make_pbp(
            b"PSP2000PLUS-A7-DATA.PSP\x20\x00", data_psar=b"profile-2000-tail"
        )
        self.ge4_bytes = b"TEST-GE4-SLIMPLUS-COMPANION\x00\x01"
        self.launcher = self.base / "launcher.PBP"
        self.psp1000 = self.base / "psp1000.PBP"
        self.psp2000 = self.base / "psp2000plus.PBP"
        self.ge4 = self.base / "ge4wrap_texv1.prx"
        self.output = self.base / "EBOOT.PBP"
        self.launcher.write_bytes(self.launcher_bytes)
        self.psp1000.write_bytes(self.psp1000_bytes)
        self.psp2000.write_bytes(self.psp2000_bytes)
        self.ge4.write_bytes(self.ge4_bytes)
        # Keep the unit fixture small while exercising the same mandatory
        # size/CRC/SHA gates as the fixed production companion.
        self._ge4_contract = (
            packer.GE4_WRAPPER_SIZE,
            packer.GE4_WRAPPER_CRC32,
            packer.GE4_WRAPPER_SHA256,
        )
        packer.GE4_WRAPPER_SIZE = len(self.ge4_bytes)
        packer.GE4_WRAPPER_CRC32 = binascii.crc32(self.ge4_bytes) & 0xFFFFFFFF
        packer.GE4_WRAPPER_SHA256 = hashlib.sha256(self.ge4_bytes).hexdigest()

    def tearDown(self) -> None:
        (
            packer.GE4_WRAPPER_SIZE,
            packer.GE4_WRAPPER_CRC32,
            packer.GE4_WRAPPER_SHA256,
        ) = self._ge4_contract
        self.temp.cleanup()

    def pack(self, title: str = packer.DEFAULT_TITLE) -> bytes:
        return packer.pack_unified_pbp(
            self.launcher, self.psp1000, self.psp2000, self.ge4,
            self.output, title
        )

    def test_outer_pbp_has_one_title_and_exact_neutral_fixed_media(self) -> None:
        output = self.pack()
        self.assertEqual(output, self.output.read_bytes())
        offsets, parts = pbp_parts(output)

        self.assertEqual(offsets[0], PBP_HEADER.size)
        expected_icon, expected_picture = packer.neutral_xmb_media()
        self.assertEqual(parts[1:6], (
            expected_icon, b"", b"", expected_picture, b""
        ))
        self.assertEqual(len(parts[1]), packer.XMB_ICON0_SLOT_SIZE)
        self.assertEqual(len(parts[4]), packer.XMB_PIC1_SLOT_SIZE)
        self.assertEqual(parts[6], b"LAUNCHER-DATA.PSP\x00\x01")

        strings = read_sfo_strings(parts[0])
        self.assertEqual(strings["TITLE"], packer.DEFAULT_TITLE)
        self.assertNotIn("beta", strings["TITLE"].casefold())
        # Rebuilding TITLE must not disturb the high-memory launcher contract.
        self.assertIn(b"MEMSIZE\x00", parts[0])

    def test_plain_xmb_hardware_gate_keeps_small_sfo_and_exact_payloads(self) -> None:
        output = packer.pack_unified_pbp(
            self.launcher,
            self.psp1000,
            self.psp2000,
            self.ge4,
            self.output,
            fixed_xmb=False,
        )
        offsets, parts = pbp_parts(output)
        self.assertLess(len(parts[0]), 4096)
        self.assertEqual(parts[1:6], (b"", b"", b"", b"", b""))
        self.assertEqual(parts[6], b"LAUNCHER-DATA.PSP\x00\x01")
        self.assertEqual(read_sfo_strings(parts[0])["TITLE"], packer.DEFAULT_TITLE)
        self.assertNotIn(packer.XMB_MARKER_MAGIC, output[:offsets[6]])

        magic, version, count = packer.CONTAINER_HEADER.unpack_from(parts[7])
        self.assertEqual(
            (magic, version, count),
            (packer.CONTAINER_MAGIC, packer.CONTAINER_VERSION, 3),
        )
        entries = [
            packer.CONTAINER_ENTRY.unpack_from(
                parts[7], packer.CONTAINER_HEADER.size
                + index * packer.CONTAINER_ENTRY.size
            )
            for index in range(count)
        ]
        for entry, expected in zip(
            entries, (self.psp1000_bytes, self.psp2000_bytes, self.ge4_bytes)
        ):
            _profile, _minimum, _maximum, start, size, crc = entry
            self.assertEqual(parts[7][start:start + size], expected)
            self.assertEqual(crc, binascii.crc32(expected) & 0xFFFFFFFF)

    def test_launcher_uses_ark_user_loadexec_for_both_storage_devices(self) -> None:
        source = (ROOT / "psp" / "unified_launcher" / "main.cpp").read_text(
            encoding="utf-8"
        )
        makefile = (ROOT / "psp" / "unified_launcher" / "Makefile").read_text(
            encoding="utf-8"
        )
        self.assertIn("sctrlKernelLoadExecVSHMs2(path, &parameters)", source)
        self.assertIn("sctrlKernelLoadExecVSHEf2(path, &parameters)", source)
        self.assertIn('parameters.key = "game"', source)
        self.assertNotIn("sceKernelLoadExec(runtime", source)
        self.assertIn("-lpspsystemctrl_user", makefile)
        self.assertIn("--th07-xmb-helper-v2", source)
        self.assertIn("copy_regular_file_verified", source)
        self.assertIn("TH07_UNIFIED_SELFWRAP_DEFERRED", source)
        self.assertIn('"%s/ge4wrap_texv1.prx"', source)
        self.assertIn("container.has_companion", source)
        self.assertIn("GE4 COMPANION skipped model0", source)
        self.assertIn("update_runtime_and_companion(", source)

    def test_first_run_xmb_progress_is_queried_and_not_unconditional(self) -> None:
        launcher = (ROOT / "psp" / "unified_launcher" / "main.cpp").read_text(
            encoding="utf-8"
        )
        selfwrap = (
            ROOT / "psp" / "unified_launcher" / "xmb_selfwrap.cpp"
        ).read_text(encoding="utf-8")
        header = (
            ROOT / "psp" / "unified_launcher" / "xmb_selfwrap.hpp"
        ).read_text(encoding="utf-8")

        query = "th07_unified_selfwrap_needs_generation"
        self.assertIn(query, header)
        self.assertIn(query, selfwrap)
        self.assertIn(query, launcher)
        self.assertIn("if (pbp.wrapped) return 0;", selfwrap)
        self.assertIn("if (needed == 1)", launcher)
        self.assertNotIn("if (needed != 0)", launcher)
        self.assertIn(
            "Generating XMB icon and background...\\n", launcher
        )
        self.assertIn("Do not turn off the PSP.\\n", launcher)

        # The shared conditional is used both before an in-place attempt and
        # after LoadExec into the deferred helper. Already-wrapped launches
        # therefore stay quiet, while either generation path shows the same
        # power-off warning.
        self.assertIn(
            "show_xmb_generation_notice_if_needed(eboot_path, data_root);",
            launcher,
        )
        self.assertIn(
            "show_xmb_generation_notice_if_needed(canonical, data_root);",
            launcher,
        )

    def test_small_valid_sfo_carries_fixed_slot_contract(self) -> None:
        output = self.pack()
        offsets, parts = pbp_parts(output)
        self.assertLess(len(parts[0]), 4096)
        self.assertEqual(
            packer.read_xmb_sfo_contract(parts[0]),
            (packer.XMB_ICON0_SLOT_SIZE, packer.XMB_PIC1_SLOT_SIZE),
        )
        self.assertEqual(offsets[1], PBP_HEADER.size + len(parts[0]))
        self.assertEqual(
            offsets[2], offsets[1] + packer.XMB_ICON0_SLOT_SIZE
        )
        self.assertEqual(
            offsets[6], offsets[2] + packer.XMB_PIC1_SLOT_SIZE
        )

    def test_packer_rejects_oversized_contract_sfo(self) -> None:
        launcher = packer.parse_pbp(self.launcher_bytes, "launcher")
        psp1000 = packer.parse_pbp(self.psp1000_bytes, "PSP-1000")
        psp2000 = packer.parse_pbp(self.psp2000_bytes, "PSP-2000+")
        original = packer.rebuild_sfo_with_title
        try:
            packer.rebuild_sfo_with_title = lambda *_args, **_kwargs: (
                b"S" * (packer.XMB_SFO_MAX_SIZE + 1)
            )
            with self.assertRaisesRegex(packer.PackError, "64 KiB XMB contract"):
                packer.build_unified_pbp(
                    launcher, psp1000, psp2000, self.ge4_bytes
                )
        finally:
            packer.rebuild_sfo_with_title = original

    def test_data_psar_routes_models_and_preserves_both_payloads_exactly(self) -> None:
        output = self.pack()
        _offsets, parts = pbp_parts(output)
        container = parts[7]
        magic, version, count = packer.CONTAINER_HEADER.unpack_from(container)
        self.assertEqual(
            (magic, version, count),
            (packer.CONTAINER_MAGIC, packer.CONTAINER_VERSION, 3),
        )
        entries = [
            packer.CONTAINER_ENTRY.unpack_from(
                container,
                packer.CONTAINER_HEADER.size + index * packer.CONTAINER_ENTRY.size,
            )
            for index in range(count)
        ]
        self.assertEqual(
            [entry[:3] for entry in entries],
            [
                (packer.PROFILE_PSP1000, 0, 0),
                (packer.PROFILE_PSP2000PLUS, 1, 0xFFFFFFFF),
                (packer.COMPANION_GE4, 1, 0xFFFFFFFF),
            ],
        )
        self.assertEqual(entries[0][3], 88)
        self.assertEqual(entries[1][3], 88 + len(self.psp1000_bytes))
        self.assertEqual(
            entries[2][3],
            88 + len(self.psp1000_bytes) + len(self.psp2000_bytes),
        )

        for entry, expected in zip(
            entries, (self.psp1000_bytes, self.psp2000_bytes, self.ge4_bytes)
        ):
            _profile, _model_min, _model_max, offset, size, crc32 = entry
            embedded = container[offset:offset + size]
            self.assertEqual(embedded, expected)
            self.assertEqual(size, len(expected))
            self.assertEqual(crc32, binascii.crc32(expected) & 0xFFFFFFFF)

    def test_ge4_companion_contract_is_fixed_and_bad_input_never_writes(self) -> None:
        expected = self._ge4_contract
        self.assertEqual(
            expected,
            (
                2150,
                0xDAEBF3F3,
                "3dc5c753497349d6fb0ab5ae2a819b240cc51e8aa412ded10bb52daa540d841d",
            ),
        )
        self.ge4.write_bytes(self.ge4_bytes + b"corrupt")
        self.output.unlink(missing_ok=True)
        with self.assertRaisesRegex(packer.PackError, "GE4 wrapper size mismatch"):
            self.pack()
        self.assertFalse(self.output.exists())

    def test_auditor_rejects_malformed_embedded_ge4_companion(self) -> None:
        output = bytearray(self.pack())
        outer_offsets, outer_parts = pbp_parts(output)
        psar = outer_parts[7]
        entry = packer.CONTAINER_ENTRY.unpack_from(
            psar,
            packer.CONTAINER_HEADER.size + 2 * packer.CONTAINER_ENTRY.size,
        )
        companion_offset = outer_offsets[7] + entry[3]
        output[companion_offset] ^= 0x01
        self.output.write_bytes(output)
        with self.assertRaisesRegex(auditor.AuditError, "CRC32 mismatch"):
            auditor.audit(self.output, packer.DEFAULT_TITLE)

    def test_auditor_rejects_noncanonical_order_gaps_and_trailing_data(self) -> None:
        pristine = self.pack()
        outer_offsets, _outer_parts = pbp_parts(pristine)
        psar_start = outer_offsets[7]
        entry0 = psar_start + packer.CONTAINER_HEADER.size
        entry1 = entry0 + packer.CONTAINER_ENTRY.size

        reordered = bytearray(pristine)
        first = bytes(reordered[entry0:entry1])
        second = bytes(
            reordered[entry1:entry1 + packer.CONTAINER_ENTRY.size]
        )
        reordered[entry0:entry1] = second
        reordered[entry1:entry1 + packer.CONTAINER_ENTRY.size] = first

        gapped = bytearray(pristine)
        second_offset = struct.unpack_from("<I", gapped, entry1 + 12)[0]
        struct.pack_into("<I", gapped, entry1 + 12, second_offset + 1)

        cases = (
            (reordered, "wrong DATA.PSAR member"),
            (gapped, "not canonically contiguous"),
            (bytearray(pristine) + b"trailing", "trailing bytes"),
        )
        for image, message in cases:
            with self.subTest(message=message):
                self.output.write_bytes(image)
                with self.assertRaisesRegex(auditor.AuditError, message):
                    auditor.audit(self.output, packer.DEFAULT_TITLE)

    def test_model1_pair_is_staged_before_commit_and_rolls_back_together(self) -> None:
        source = (ROOT / "psp" / "unified_launcher" / "main.cpp").read_text(
            encoding="utf-8"
        )
        start = source.index("int update_runtime_and_companion(")
        end = source.index("int copy_regular_file_verified(", start)
        transaction = source[start:end]
        stage_companion = transaction.index(
            "stage_payload(container, &companion_update"
        )
        stage_runtime = transaction.index(
            "stage_payload(container, &runtime_update"
        )
        commit_companion = transaction.index(
            "commit_staged_payload(&companion_update"
        )
        commit_runtime = transaction.index(
            "commit_staged_payload(&runtime_update"
        )
        self.assertLess(stage_companion, stage_runtime)
        self.assertLess(stage_runtime, commit_companion)
        self.assertLess(commit_companion, commit_runtime)
        self.assertIn("rollback_payload(&runtime_update", transaction)
        self.assertIn("rollback_payload(\n                                                 &companion_update", transaction)

    def test_model0_path_never_extracts_or_removes_ge4_companion(self) -> None:
        source = (ROOT / "psp" / "unified_launcher" / "main.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "const bool has_companion = model.effective_model >= 1u;", source
        )
        branch = source.index("if (container.has_companion) {")
        skipped = source.index('log_line("GE4 COMPANION skipped model0")')
        extraction = source.index("container.companion_entry", branch)
        self.assertLess(branch, extraction)
        self.assertLess(extraction, skipped)
        self.assertNotIn("remove_regular_if_present(companion)", source)

    def test_every_input_and_every_media_slot_is_fail_closed(self) -> None:
        roles = ("launcher", "psp1000", "psp2000")
        for role in roles:
            for slot in range(1, 6):
                with self.subTest(role=role, slot=packer.PBP_SECTION_NAMES[slot]):
                    self.launcher.write_bytes(self.launcher_bytes)
                    self.psp1000.write_bytes(self.psp1000_bytes)
                    self.psp2000.write_bytes(self.psp2000_bytes)
                    target = getattr(self, role)
                    if role == "launcher":
                        replacement = make_pbp(b"LAUNCHER", media_index=slot)
                    elif role == "psp1000":
                        replacement = make_pbp(b"PSP1000", media_index=slot)
                    else:
                        replacement = make_pbp(b"PSP2000", media_index=slot)
                    target.write_bytes(replacement)
                    self.output.unlink(missing_ok=True)
                    with self.assertRaisesRegex(packer.PackError, "bundled XMB media"):
                        self.pack()
                    self.assertFalse(self.output.exists())

    def test_beta_or_tester_release_title_is_rejected(self) -> None:
        for title in ("Touhou 7 PSP Beta", "TH07 tester build"):
            with self.subTest(title=title):
                with self.assertRaisesRegex(packer.PackError, "must not contain"):
                    self.pack(title)

    def test_only_neutral_tool_generated_media_enters_a_clean_release(self) -> None:
        output = self.pack()
        _offsets, parts = pbp_parts(output)
        self.assertIsNone(asset_guard.check_path(self.output))
        for forbidden in (
            b"ORIGINAL_XMB_ASSET_DO_NOT_SHIP",
            b"THTX",
            b"RIFF",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, output)
        neutral_icon, neutral_picture = packer.neutral_xmb_media()
        self.assertEqual(
            parts[1:6],
            (neutral_icon, b"", b"", neutral_picture, b""),
        )

    def test_locally_changed_fixed_media_is_rejected_for_distribution(self) -> None:
        output = bytearray(self.pack())
        offsets, _parts = pbp_parts(output)
        output[offsets[1] + 100] ^= 0x01
        self.output.write_bytes(output)
        reason = asset_guard.check_path(self.output)
        self.assertIsNotNone(reason)
        self.assertIn("neutral placeholder contract", reason)


if __name__ == "__main__":
    unittest.main()

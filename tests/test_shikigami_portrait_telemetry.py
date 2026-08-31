from __future__ import annotations

import importlib.util
import struct
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RECEIVER_PATH = ROOT / "tools" / "shikigami_th07_receiver.py"
SPEC = importlib.util.spec_from_file_location(
    "shikigami_th07_portrait_receiver", RECEIVER_PATH
)
assert SPEC and SPEC.loader
receiver = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = receiver
SPEC.loader.exec_module(receiver)


def identity() -> bytes:
    return receiver.IDENTITY.pack(3, 1, 4, 0xC0A80BC8)


def packet(payload: bytes, *, declared_length: int | None = None) -> bytes:
    return receiver.HEADER.pack(
        receiver.MAGIC,
        receiver.PROTOCOL_VERSION,
        receiver.PacketType.TH07_PORTRAIT_CACHE,
        71,
        12_345,
        len(payload) if declared_length is None else declared_length,
        3,
        0x26082806,
    ) + payload


def complete_values(
    stage_portrait_count: int = 4, *, stage: int = 4
) -> list[int]:
    required_mask = receiver._portrait_required_mask(stage_portrait_count)
    values = [
        receiver.PORTRAIT_CACHE_SCHEMA,
        receiver.PORTRAIT_VALID_KNOWN,
        1,
        1,
        receiver.PORTRAIT_APERTURE_BYTES,
        receiver.PORTRAIT_CACHE_FLAGS_KNOWN,
        9,
        stage,
        required_mask,
        required_mask,
        required_mask,
        0,
        receiver.PORTRAIT_POOL_RAW_BASE,
        receiver.PORTRAIT_POOL_BYTES,
        receiver._portrait_expected_live_bytes(required_mask),
        0,
        0,
        0,
        0,
    ]
    addresses = (
        0x04200000,
        0x04280000,
        0x04300000,
        0x04320000,
        0x04340000,
        0x04360000,
    )
    for index, (expected, address) in enumerate(
        zip(receiver.PORTRAIT_SLOT_EXPECTATIONS, addresses)
    ):
        role, texture_slot, allocation_bytes, width, height = expected
        if required_mask & (1 << index):
            content_hash = 0xA5000000 + index + 1
            values.extend(
                (
                    role,
                    texture_slot,
                    address,
                    allocation_bytes,
                    width,
                    height,
                    receiver.PORTRAIT_PSM_4444,
                    content_hash,
                    content_hash,
                    20 + index,
                    0,
                )
            )
        else:
            values.extend((role, texture_slot, 0, 0, 0, 0, 0, 0, 0, 0, 0))
    assert len(values) == 85
    return values


def encoded(values: list[int]) -> bytes:
    return identity() + receiver.PORTRAIT_CACHE_BODY.pack(*values)


def formatted(values: list[int]) -> str:
    parsed = receiver.parse_packet(packet(encoded(values)))
    assert parsed.portrait_cache
    return receiver.format_packet(parsed, ("192.168.11.200", 9996))[0]


class PortraitTelemetryProtocolTest(unittest.TestCase):
    def test_wire_layout_is_fixed_and_status_layout_is_unchanged(self) -> None:
        self.assertEqual(receiver.PORTRAIT_CACHE_BODY.format, "!HH83I")
        self.assertEqual(receiver.PORTRAIT_CACHE_BODY.size, 336)
        self.assertEqual(receiver.PORTRAIT_CACHE_PAYLOAD_BYTES, 344)
        self.assertEqual(receiver.PERF_LOG_FIXED_PAYLOAD_BYTES, 52)
        self.assertEqual(receiver.MAX_DATAGRAM_BYTES, 1036)
        self.assertEqual(receiver.STATUS_BODY_V1.format, "!HHI30I")
        self.assertEqual(receiver.STATUS_BODY_V2.format, "!HHI47I")
        self.assertEqual(receiver.STATUS_PAYLOAD_BYTES_V1, 136)
        self.assertEqual(receiver.STATUS_PAYLOAD_BYTES_V2, 204)

    def test_each_dynamic_stage_count_passes_without_sample_or_draw_gates(self) -> None:
        for stage_count in range(1, 5):
            with self.subTest(stage_count=stage_count):
                values = complete_values(stage_count)
                values[11] = 0
                required_count = stage_count + 2
                for index in range(required_count):
                    values[19 + index * 11 + 10] = index
                parsed = receiver.parse_packet(packet(encoded(values)))
                assert parsed.portrait_cache
                cache = parsed.portrait_cache
                self.assertEqual(len(cache.slots), 6)
                self.assertEqual(cache.slots[0].raw_address, 0x04200000)
                self.assertEqual(
                    cache.live_bytes,
                    2 * receiver.PORTRAIT_PLAYER_BYTES
                    + stage_count * receiver.PORTRAIT_STAGE_BYTES,
                )
                line = receiver.format_packet(
                    parsed, ("192.168.11.200", 9996)
                )[0]
                self.assertIn("PORTRAIT CACHE=PASS", line)
                self.assertIn("PREWARM=COMPLETE", line)
                self.assertIn("SAMPLED=0x00", line)
                self.assertIn(f"HASH={required_count}/{required_count}", line)
                self.assertIn(
                    f"COUNT=R{required_count}/O{required_count}/V{required_count}",
                    line,
                )

    def test_stage4_5_6_observed_shapes_are_exact_dynamic_passes(self) -> None:
        for stage, stage_count, required_mask, live_kib in (
            (4, 3, 0x1F, 1408),
            (5, 2, 0x0F, 1280),
            (6, 3, 0x1F, 1408),
        ):
            with self.subTest(stage=stage):
                values = complete_values(stage_count, stage=stage)
                line = formatted(values)
                self.assertIn("PORTRAIT CACHE=PASS", line)
                self.assertIn(f"STAGE={stage} PREWARM=COMPLETE", line)
                self.assertIn(f"MASK=R0x{required_mask:02X}", line)
                self.assertIn(f"LIVE={live_kib}KiB", line)

    def test_empty_but_well_formed_snapshot_is_incomplete(self) -> None:
        values = [receiver.PORTRAIT_CACHE_SCHEMA, receiver.PORTRAIT_VALID_KNOWN]
        values.extend([0] * 83)
        values[4] = 2 * 1024 * 1024
        line = formatted(values)
        self.assertIn("PORTRAIT CACHE=INCOMPLETE", line)
        self.assertIn("PREWARM=PENDING", line)
        self.assertIn("GE bridge inactive", line)
        self.assertIn("portrait prewarm is not committed", line)

    def test_hash_mismatch_overlap_and_wrong_range_are_human_readable_failures(self) -> None:
        cases: dict[str, tuple[int, int, str]] = {
            "hash": (19 + 8, 0xDEADBEEF, "source/readback hash mismatch"),
            "overlap": (19 + 11 + 2, 0x04200000, "overlap"),
            "range": (19 + 2, 0x041FF000, "outside the upper pool"),
        }
        for name, (index, value, reason) in cases.items():
            with self.subTest(name=name):
                values = complete_values()
                values[index] = value
                line = formatted(values)
                self.assertIn("PORTRAIT CACHE=FAIL", line)
                self.assertIn(reason, line)

    def test_committed_layout_and_zero_fault_counters_are_required(self) -> None:
        cases: dict[str, tuple[int, int, str]] = {
            "stage": (7, 9, "outside 1..8"),
            "cache-generation": (6, 0, "cache generation is zero"),
            "required-shape": (8, 0x17, "not player + 1..4 contiguous"),
            "owned": (9, 0x1F, "allocated slot 5 is not owned"),
            "live": (
                14,
                receiver.PORTRAIT_MAX_LIVE_BYTES - 4096,
                "live portrait bytes",
            ),
            "fallback": (15, 1, "fallback count is 1"),
            "migration": (16, 1, "migration count is 1"),
            "allocation": (17, 1, "allocation failure count is 1"),
            "invariant": (18, 1, "invariant failure count is 1"),
            "player-size": (19 + 3, 128 * 1024, "slot 0 allocation"),
            "stage-size": (19 + 2 * 11 + 3, 512 * 1024, "slot 2 allocation"),
            "psm": (19 + 6, 3, "not RGBA4444"),
            "texture-slot": (19 + 1, 99, "texture slot is 99"),
            "upload-generation": (19 + 9, 0, "upload generation is zero"),
        }
        for name, (index, value, reason) in cases.items():
            with self.subTest(name=name):
                values = complete_values()
                values[index] = value
                line = formatted(values)
                self.assertIn("PORTRAIT CACHE=FAIL", line)
                self.assertIn(reason, line)

    def test_pending_load_is_incomplete_but_fault_counters_still_fail(self) -> None:
        values = complete_values(1)
        values[8] = 0
        line = formatted(values)
        self.assertIn("PORTRAIT CACHE=INCOMPLETE", line)
        self.assertIn("PREWARM=PENDING", line)
        self.assertNotIn("PORTRAIT CACHE=PASS", line)

        values[17] = 1
        line = formatted(values)
        self.assertIn("PORTRAIT CACHE=FAIL", line)
        self.assertIn("allocation failure count is 1", line)

    def test_committed_missing_slot_is_failure_not_a_smaller_stage(self) -> None:
        values = complete_values(3)
        missing_index = 4
        missing_bit = 1 << missing_index
        values[9] &= ~missing_bit
        values[10] &= ~missing_bit
        values[14] -= receiver.PORTRAIT_STAGE_BYTES
        slot_start = 19 + missing_index * 11
        role = values[slot_start]
        texture_slot = values[slot_start + 1]
        values[slot_start : slot_start + 11] = [
            role,
            texture_slot,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
        ]
        line = formatted(values)
        self.assertIn("PORTRAIT CACHE=FAIL", line)
        self.assertIn("prewarm complete with owned mask", line)
        self.assertIn("required slot 4 is not allocated", line)

    def test_capacity_budget_is_separate_from_dynamic_required_mask(self) -> None:
        self.assertEqual(receiver.PORTRAIT_CAPACITY_MASK, 0x3F)
        self.assertEqual(receiver.PORTRAIT_MAX_LIVE_BYTES, 1536 * 1024)
        self.assertEqual(receiver._portrait_required_mask(1), 0x07)
        self.assertEqual(receiver._portrait_required_mask(2), 0x0F)
        self.assertEqual(receiver._portrait_required_mask(3), 0x1F)
        self.assertEqual(receiver._portrait_required_mask(4), 0x3F)

    def test_malformed_schema_length_masks_and_validity_are_rejected(self) -> None:
        values = complete_values()
        with self.assertRaisesRegex(receiver.ProtocolError, "wrong size|bad payload"):
            receiver.parse_packet(packet(encoded(values)[:-4]))

        values = complete_values()
        values[0] = 2
        with self.assertRaisesRegex(receiver.ProtocolError, "unsupported.*schema"):
            receiver.parse_packet(packet(encoded(values)))

        values = complete_values()
        values[1] |= 1 << 15
        with self.assertRaisesRegex(receiver.ProtocolError, "unknown validity"):
            receiver.parse_packet(packet(encoded(values)))

        values = complete_values()
        values[11] = 1 << receiver.PORTRAIT_SLOT_COUNT
        with self.assertRaisesRegex(receiver.ProtocolError, "exceeds six slots"):
            receiver.parse_packet(packet(encoded(values)))

        values = complete_values()
        values[1] &= ~receiver.PORTRAIT_VALID_CACHE_SNAPSHOT
        with self.assertRaisesRegex(receiver.ProtocolError, "snapshot is nonzero"):
            receiver.parse_packet(packet(encoded(values)))


class PortraitTelemetrySourcePolicyTest(unittest.TestCase):
    def test_getter_is_read_only_from_the_one_hz_observer_path(self) -> None:
        source = (ROOT / "psp" / "shikigami_th07.c").read_text(encoding="utf-8")
        sender = source[
            source.index("static int send_portrait_cache_packet") :
            source.index("static int send_event_packet")
        ]
        self.assertEqual(sender.count("th07_psp_portrait_cache_snapshot(&cache)"), 1)
        self.assertEqual(sender.count("th07_psp_ge4_active()"), 1)
        self.assertEqual(sender.count("th07_psp_ge4_power_lock_held()"), 1)
        self.assertEqual(sender.count("sceGeEdramGetSize()"), 1)
        worker = source[
            source.index("static int observer_worker") :
            source.index("int th07_shikigami_start")
        ]
        one_hz_start = worker.index("if (++ticks >= 10u)")
        one_hz = worker[
            one_hz_start : worker.index("ticks = 0;", one_hz_start)
        ]
        self.assertEqual(worker.count("send_portrait_cache_packet"), 1)
        self.assertIn("send_portrait_cache_packet", one_hz)

    def test_sender_serializes_every_snapshot_word_in_network_order(self) -> None:
        source = (ROOT / "psp" / "shikigami_th07.c").read_text(encoding="utf-8")
        sender = source[
            source.index("static int send_portrait_cache_packet") :
            source.index("static int send_event_packet")
        ]
        for offset, field in (
            (12, "bridge_active != 0"),
            (16, "power_lock_held != 0"),
            (20, "live_aperture_bytes"),
            (24, "cache.flags"),
            (36, "cache.required_mask"),
            (52, "cache.pool_raw_base"),
            (76, "cache.invariant_failure_count"),
        ):
            self.assertIn(f"put_be32(payload + {offset}, {field})", sender)
        slot_writer = source[
            source.index("static void fill_portrait_slot") :
            source.index("static int send_portrait_cache_packet")
        ]
        self.assertEqual(slot_writer.count("put_be32("), 11)
        self.assertIn("slot_index * 44u", sender)


if __name__ == "__main__":
    unittest.main()

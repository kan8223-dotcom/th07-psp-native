from __future__ import annotations

import importlib.util
import struct
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RECEIVER_PATH = ROOT / "tools" / "shikigami_th07_receiver.py"
SPEC = importlib.util.spec_from_file_location("shikigami_th07_receiver", RECEIVER_PATH)
assert SPEC and SPEC.loader
receiver = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = receiver
SPEC.loader.exec_module(receiver)


def header(packet_type: int, sequence: int, payload: bytes) -> bytes:
    return receiver.HEADER.pack(
        receiver.MAGIC,
        receiver.PROTOCOL_VERSION,
        packet_type,
        sequence,
        1234,
        len(payload),
        3,
        0x20260827,
    ) + payload


def identity() -> bytes:
    return receiver.IDENTITY.pack(3, 1, 4, 0xC0A80BC8)


def audio4m_values(*, proven: bool = False, active: bool = False) -> list[int]:
    values = [
        2,
        receiver.VALID_AUDIO_RING
        | receiver.VALID_ME_UPPER_OWNED
        | receiver.VALID_ME_PERF_WINDOW
        | receiver.VALID_AUDIO4M_USAGE,
        receiver.STATUS_AUDIO4M_PROVEN if proven else 0,
    ] + [0] * 47
    values[16] = receiver.AUDIO4M_EXTENT_BASE
    values[17] = receiver.AUDIO4M_EXTENT_BYTES
    values[18] = receiver.AUDIO4M_RING_BYTES
    values[19] = 4096
    values[23] = 100
    if proven or active:
        values[33] = receiver.AUDIO4M_SFX_ATLAS_BYTES
        values[34] = 1_500_000
        values[35] = receiver.AUDIO4M_SFX_ATLAS_BYTES - values[34]
    if active:
        values[2] |= receiver.STATUS_BGM_PLAYING
        values[38] = 2
        values[39] = 1
        values[49] = (
            receiver.AUDIO4M_PROOF_FULL_EXTENT
            | receiver.AUDIO4M_PROOF_ATLAS_EXACT
        )
    if proven:
        values[36] = receiver.AUDIO4M_SFX_REQUIRED_MASK
        values[37] = receiver.AUDIO4M_SFX_REQUIRED_MASK
        values[38] = 1234
        values[39] = 1200
        values[42] = 0
        values[43] = 1
        values[44] = 1
        values[45] = 30
        values[46:49] = [3, 2, 1]
        values[49] = receiver.AUDIO4M_PROOF_REQUIRED
    return values


class ProtocolTest(unittest.TestCase):
    def test_identity_packet(self) -> None:
        packet = receiver.parse_packet(header(1, 7, identity()))
        self.assertEqual(packet.packet_type, receiver.PacketType.HELLO)
        self.assertEqual(packet.identity.local_ip, "192.168.11.200")
        self.assertEqual(packet.build_id, 0x20260827)

    def test_item_me_enabled_packet_is_self_contained(self) -> None:
        values = [
            receiver.ITEM_ME_SCHEMA,
            receiver.ITEM_ME_VALID_DECISION,
            receiver.ItemMeState.ENABLED,
            receiver.ItemMeReason.SELFTEST_PASS,
            1,
            0,
            0,
            0,
            1,
            0,
            0,
        ]
        payload = identity() + receiver.ITEM_ME_BODY.pack(*values)
        packet = receiver.parse_packet(header(12, 17, payload))
        assert packet.item_me
        self.assertEqual(packet.item_me.state, receiver.ItemMeState.ENABLED)
        line = receiver.format_packet(packet, ("192.168.11.200", 9996))[0]
        self.assertIn("ITEM_ME=ENABLED", line)
        self.assertIn("BULLET_ME=ON", line)
        self.assertIn("REASON=SELFTEST-PASS", line)
        self.assertIn("TEST=1/0 RETRY=0/0", line)
        self.assertIn("WAIT=1 STREAM=OK ITEM=OK", line)

    def test_item_me_safe_fallback_names_the_failed_contract(self) -> None:
        values = [
            receiver.ITEM_ME_SCHEMA,
            receiver.ITEM_ME_VALID_DECISION
            | receiver.ITEM_ME_VALID_FAILURE_DETAIL,
            receiver.ItemMeState.SAFE_FALLBACK,
            receiver.ItemMeReason.REJECT_CONTRACT,
            1,
            1,
            1,
            1,
            1,
            0,
            0,
        ]
        payload = identity() + receiver.ITEM_ME_BODY.pack(*values)
        packet = receiver.parse_packet(header(12, 18, payload))
        assert packet.item_me
        line = receiver.format_packet(packet, ("192.168.11.200", 9996))[0]
        self.assertIn("ITEM_ME=SAFE-FALLBACK", line)
        self.assertIn("BULLET_ME=ON", line)
        self.assertIn("REASON=REJECT-CONTRACT", line)
        self.assertIn("TEST=1/1 RETRY=1/1", line)

    def test_item_me_rejects_invalid_wire_contracts(self) -> None:
        base = [
            receiver.ITEM_ME_SCHEMA,
            receiver.ITEM_ME_VALID_DECISION,
            receiver.ItemMeState.ENABLED,
            receiver.ItemMeReason.SELFTEST_PASS,
            1,
            0,
            0,
            0,
            1,
            0,
            0,
        ]
        invalid = []
        values = list(base)
        values[0] = 2
        invalid.append(values)
        values = list(base)
        values[1] |= 0x8000
        invalid.append(values)
        values = list(base)
        values[2] = 99
        invalid.append(values)
        values = list(base)
        values[3] = 99
        invalid.append(values)
        values = list(base)
        values[5] = 2
        invalid.append(values)
        values = list(base)
        values[6:8] = [1, 2]
        invalid.append(values)
        for values in invalid:
            payload = identity() + receiver.ITEM_ME_BODY.pack(*values)
            with self.assertRaises(receiver.ProtocolError):
                receiver.parse_packet(header(12, 19, payload))

        payload = identity() + receiver.ITEM_ME_BODY.pack(*base)
        with self.assertRaisesRegex(receiver.ProtocolError, "bad payload length"):
            receiver.parse_packet(header(12, 20, payload[:-1]))

    def test_a1_move_enabled_packet_is_self_contained(self) -> None:
        values = [
            receiver.A1_MOVE_SCHEMA,
            receiver.A1_MOVE_VALID_DECISION,
            receiver.ItemMeState.ENABLED,
            receiver.A1MoveReason.SELFTEST_PASS,
            1,
            0,
            0,
            0,
            1,
            0,
            0,
            0xFFFF_FFFF,
        ]
        payload = identity() + receiver.A1_MOVE_BODY.pack(*values)
        packet = receiver.parse_packet(header(13, 21, payload))
        assert packet.a1_move
        self.assertEqual(packet.packet_type, receiver.PacketType.TH07_A1_MOVE)
        self.assertEqual(packet.a1_move.state, receiver.ItemMeState.ENABLED)
        self.assertEqual(packet.a1_move.last_poll_result, 1)
        line = receiver.format_packet(packet, ("192.168.11.200", 9996))[0]
        self.assertIn("A1_MOVE=ENABLED", line)
        self.assertIn("ITEM_MOVE=ON", line)
        self.assertIn("BULLET_ME=ON", line)
        self.assertIn("REASON=SELFTEST-PASS", line)
        self.assertIn("TEST=1/0 RETRY=0/0", line)
        self.assertIn("POLL=1 BULLET=OK ITEM=OK MISMATCH=N/A", line)

    def test_a1_move_safe_fallback_names_the_failed_contract(self) -> None:
        values = [
            receiver.A1_MOVE_SCHEMA,
            receiver.A1_MOVE_VALID_DECISION
            | receiver.A1_MOVE_VALID_FAILURE_DETAIL,
            receiver.ItemMeState.SAFE_FALLBACK,
            receiver.A1MoveReason.BIT_MISMATCH,
            1,
            1,
            1,
            1,
            1,
            0,
            0,
            42,
        ]
        payload = identity() + receiver.A1_MOVE_BODY.pack(*values)
        packet = receiver.parse_packet(header(13, 22, payload))
        assert packet.a1_move
        line = receiver.format_packet(packet, ("192.168.11.200", 9996))[0]
        self.assertIn("A1_MOVE=SAFE-FALLBACK", line)
        self.assertIn("ITEM_MOVE=OFF", line)
        self.assertIn("BULLET_ME=ON", line)
        self.assertIn("REASON=BIT-MISMATCH", line)
        self.assertIn("TEST=1/1 RETRY=1/1", line)
        self.assertIn("MISMATCH=42", line)

    def test_a1_move_rejects_invalid_wire_contracts(self) -> None:
        base = [
            receiver.A1_MOVE_SCHEMA,
            receiver.A1_MOVE_VALID_DECISION,
            receiver.ItemMeState.ENABLED,
            receiver.A1MoveReason.SELFTEST_PASS,
            1,
            0,
            0,
            0,
            1,
            0,
            0,
            0xFFFF_FFFF,
        ]
        invalid = []
        for index, value in ((0, 2), (2, 99), (3, 99), (5, 2)):
            values = list(base)
            values[index] = value
            invalid.append(values)
        values = list(base)
        values[1] |= 0x8000
        invalid.append(values)
        values = list(base)
        values[6:8] = [1, 2]
        invalid.append(values)
        values = list(base)
        values[1] |= receiver.A1_MOVE_VALID_FAILURE_DETAIL
        invalid.append(values)

        fallback = [
            receiver.A1_MOVE_SCHEMA,
            receiver.A1_MOVE_VALID_DECISION
            | receiver.A1_MOVE_VALID_FAILURE_DETAIL,
            receiver.ItemMeState.SAFE_FALLBACK,
            receiver.A1MoveReason.BIT_MISMATCH,
            1,
            1,
            1,
            1,
            1,
            0,
            0,
            0xFFFF_FFFF,
        ]
        values = list(fallback)
        values[3] = receiver.A1MoveReason.COMMON_FATAL
        invalid.append(values)
        values = list(fallback)
        values[7] = 0
        invalid.append(values)

        failed = list(fallback)
        failed[2] = receiver.ItemMeState.FAILED
        failed[3] = receiver.A1MoveReason.BULLET_RETRY_FAILED
        invalid.append(failed)
        values = list(failed)
        values[3] = receiver.A1MoveReason.BIT_MISMATCH
        invalid.append(values)

        for values in invalid:
            payload = identity() + receiver.A1_MOVE_BODY.pack(*values)
            with self.assertRaises(receiver.ProtocolError):
                receiver.parse_packet(header(13, 23, payload))

        payload = identity() + receiver.A1_MOVE_BODY.pack(*base)
        with self.assertRaisesRegex(receiver.ProtocolError, "bad payload length"):
            receiver.parse_packet(header(13, 24, payload[:-1]))

    def test_status_packet(self) -> None:
        values = [
            1,
            receiver.VALID_MODEL_CAPACITY
            | receiver.VALID_MAIN_HEAP_API
            | receiver.VALID_GE_APERTURE_API
            | receiver.VALID_GE_PRIOR_EVIDENCE
            | receiver.VALID_FPS
            | receiver.VALID_AUDIO_RING,
            receiver.STATUS_BGM_PLAYING | receiver.STATUS_DEMO,
            600,
            2,
            3,
            599,
            64 * 1024 * 1024,
            20 * 1024 * 1024,
            18 * 1024 * 1024,
            4 * 1024 * 1024,
            2 * 1024 * 1024,
            0x04200000,
            2 * 1024 * 1024,
            4 * 1024 * 1024,
            0,
            0,
            0,
            393216,
            4096,
            2,
            4,
            5,
            0,
            0,
            0,
            0,
            17,
            0,
            0,
            0,
            0,
            0,
        ]
        payload = identity() + receiver.STATUS_BODY.pack(*values)
        packet = receiver.parse_packet(header(8, 8, payload))
        assert packet.status
        self.assertEqual(packet.status.bgm_ring_bytes, 393216)
        self.assertEqual(packet.status.ge_prior_base, 0x04200000)
        self.assertEqual(packet.status.me_upper_bytes, 0)
        self.assertEqual(packet.status.spell_index, 17)

    def test_event_signed_values(self) -> None:
        body = receiver.EVENT_BODY.pack(
            1,
            receiver.EventType.BGM,
            4,
            0xFFFFFFFF,
            3,
            99,
            2,
            4,
            3,
            1,
            0,
        )
        packet = receiver.parse_packet(header(9, 9, identity() + body))
        assert packet.event
        self.assertEqual(packet.event.old_value, -1)
        self.assertEqual(packet.event.bgm_index, 3)

    def test_owned_me_extent_and_perf_window(self) -> None:
        values = [1, receiver.VALID_AUDIO_RING
                  | receiver.VALID_ME_UPPER_OWNED
                  | receiver.VALID_ME_PERF_WINDOW, 0] + [0] * 30
        values[16] = 0x00200000
        values[17] = 393216
        values[18] = 393216
        values[23:27] = [42, 3, 2, 1500]
        payload = identity() + receiver.STATUS_BODY.pack(*values)
        packet = receiver.parse_packet(header(8, 8, payload))
        assert packet.status
        self.assertEqual(packet.status.me_upper_base, 0x00200000)
        self.assertEqual(packet.status.me_upper_bytes, 393216)
        self.assertEqual(packet.status.me_jobs, 42)
        self.assertEqual(packet.status.me_fallbacks, 3)
        self.assertEqual(packet.status.me_timeouts, 2)
        self.assertEqual(packet.status.me_max_wait_us, 1500)

    def test_schema1_wire_constants_remain_frozen(self) -> None:
        self.assertIs(receiver.STATUS_BODY, receiver.STATUS_BODY_V1)
        self.assertEqual(receiver.STATUS_BODY_V1.format, "!HHI30I")
        self.assertEqual(receiver.STATUS_BODY_V1.size, 128)
        self.assertEqual(receiver.STATUS_PAYLOAD_BYTES, 136)
        self.assertEqual(receiver.STATUS_BODY_V2.format, "!HHI47I")
        self.assertEqual(receiver.STATUS_BODY_V2.size, 196)
        self.assertEqual(receiver.STATUS_PAYLOAD_BYTES_V2, 204)
        self.assertEqual(receiver.PORTRAIT_CACHE_BODY.format, "!HH83I")
        self.assertEqual(receiver.PORTRAIT_CACHE_BODY.size, 336)
        self.assertEqual(receiver.PORTRAIT_CACHE_PAYLOAD_BYTES, 344)
        self.assertEqual(receiver.PERF_LOG_BODY.size, 44)
        self.assertEqual(receiver.PERF_LOG_FIXED_PAYLOAD_BYTES, 52)
        self.assertEqual(receiver.ITEM_ME_BODY.format, "!HH9I")
        self.assertEqual(receiver.ITEM_ME_BODY.size, 40)
        self.assertEqual(receiver.ITEM_ME_PAYLOAD_BYTES, 48)
        self.assertEqual(receiver.A1_MOVE_BODY.format, "!HH10I")
        self.assertEqual(receiver.A1_MOVE_BODY.size, 44)
        self.assertEqual(receiver.A1_MOVE_PAYLOAD_BYTES, 52)
        self.assertEqual(receiver.MAX_DATAGRAM_BYTES, 1036)

    def test_audio4m_intermediate_status(self) -> None:
        values = audio4m_values()
        values[33:36] = [1024, 768, 256]
        values[36] = 0b101
        values[37] = 0b010
        payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
        packet = receiver.parse_packet(header(8, 10, payload))
        assert packet.status
        self.assertEqual(packet.status.schema, 2)
        self.assertEqual(packet.status.me_upper_base, receiver.AUDIO4M_EXTENT_BASE)
        self.assertEqual(packet.status.me_upper_bytes, receiver.AUDIO4M_EXTENT_BYTES)
        self.assertEqual(packet.status.bgm_ring_bytes, receiver.AUDIO4M_RING_BYTES)
        self.assertEqual(packet.status.sfx_atlas_bytes, 1024)
        self.assertFalse(packet.status.flags & receiver.STATUS_AUDIO4M_PROVEN)

    def test_audio4m_proven_status_and_format(self) -> None:
        payload = identity() + receiver.STATUS_BODY_V2.pack(*audio4m_values(proven=True))
        packet = receiver.parse_packet(header(8, 11, payload))
        assert packet.status
        self.assertEqual(
            packet.status.audio4m_proof_flags, receiver.AUDIO4M_PROOF_REQUIRED
        )
        line = receiver.format_packet(packet, ("192.168.11.200", 9996))[0]
        self.assertIn("AUDIO4M=PROVEN/0xFF", line)
        self.assertIn("ME_EDRAM=384KiB@0x00010000/LOWER", line)
        self.assertIn("BGM_RING=393216/ME", line)
        self.assertIn("FILL=4096/98304", line)
        self.assertIn("WRAP=3/2/1", line)

    def test_audio4m_normal_game_activity_is_active_not_proven(self) -> None:
        payload = identity() + receiver.STATUS_BODY_V2.pack(
            *audio4m_values(active=True)
        )
        packet = receiver.parse_packet(header(8, 12, payload))
        assert packet.status
        self.assertFalse(packet.status.flags & receiver.STATUS_AUDIO4M_PROVEN)
        line = receiver.format_packet(packet, ("192.168.11.200", 9996))[0]
        self.assertIn("AUDIO4M=ACTIVE-4MiB-IN-USE/0x03", line)
        self.assertNotIn("AUDIO4M=PROVEN", line)

    def test_audio4m_main_ram_bgm_is_active_and_me_edram_is_unused(self) -> None:
        values = audio4m_values()
        values[1] &= ~receiver.VALID_ME_UPPER_OWNED
        values[2] |= (
            receiver.STATUS_BGM_PLAYING
            | receiver.STATUS_SFX_MAIN_RAM
            | receiver.STATUS_BGM_MAIN_RAM
        )
        values[16] = 0
        values[17] = 0
        values[46:49] = [3, 2, 1]
        values[49] = (
            receiver.AUDIO4M_PROOF_BGM_UPLOAD_WRAP
            | receiver.AUDIO4M_PROOF_BGM_FETCH_WRAP
            | receiver.AUDIO4M_PROOF_BGM_OUTPUT_WRAP
            | receiver.AUDIO4M_PROOF_ZERO_FAULTS
        )
        payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
        packet = receiver.parse_packet(header(8, 13, payload))
        assert packet.status
        line = receiver.format_packet(packet, ("192.168.11.200", 9996))[0]
        self.assertIn("AUDIO4M=ACTIVE-BGM-MAINRAM/0xF0", line)
        self.assertIn("ME_EDRAM=UNUSED", line)
        self.assertIn("BGM_RING=393216/MAIN", line)

    def test_schema2_reports_owned_ge_portrait_extent(self) -> None:
        values = audio4m_values()
        values[1] |= receiver.VALID_GE_UPPER_OWNED
        values[2] |= receiver.STATUS_GE_UPPER_PORTRAIT
        values[12] = 0x04200000
        values[13] = 2 * 1024 * 1024
        payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
        packet = receiver.parse_packet(header(8, 13, payload))
        assert packet.status
        line = receiver.format_packet(packet, ("192.168.11.200", 9996))[0]
        self.assertIn("GE_UPPER=2048KiB@0x04200000 OWNED/PORTRAIT", line)
        self.assertIn("GE_PORTRAIT", line)

    def test_schema2_inactive_ge_keeps_prior_no_owner_wording(self) -> None:
        values = audio4m_values()
        values[1] |= receiver.VALID_GE_PRIOR_EVIDENCE
        values[2] |= receiver.STATUS_GE_PRIOR_NOT_RUNTIME_OWNER
        values[12] = 0x04200000
        values[13] = 2 * 1024 * 1024
        payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
        packet = receiver.parse_packet(header(8, 14, payload))
        assert packet.status
        line = receiver.format_packet(packet, ("192.168.11.200", 9996))[0]
        self.assertIn("GE_UPPER=2048KiB@0x04200000 PRIOR/NO-OWNER", line)

    def test_schema2_rejects_contradictory_ge_ownership(self) -> None:
        mutations = (
            (receiver.VALID_GE_UPPER_OWNED, 0, 0x04200000, 2 * 1024 * 1024),
            (0, receiver.STATUS_GE_UPPER_PORTRAIT, 0, 0),
            (
                receiver.VALID_GE_UPPER_OWNED | receiver.VALID_GE_PRIOR_EVIDENCE,
                receiver.STATUS_GE_UPPER_PORTRAIT,
                0x04200000,
                2 * 1024 * 1024,
            ),
        )
        for valid_bits, status_bits, base, size in mutations:
            values = audio4m_values()
            values[1] |= valid_bits
            values[2] |= status_bits
            values[12] = base
            values[13] = size
            payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
            with self.assertRaises(receiver.ProtocolError):
                receiver.parse_packet(header(8, 15, payload))

    def test_audio4m_rejects_schema_size_mismatch(self) -> None:
        values = audio4m_values()
        schema1_sized = identity() + receiver.STATUS_BODY_V1.pack(*values[:33])
        with self.assertRaisesRegex(receiver.ProtocolError, "schema 2 has the wrong size"):
            receiver.parse_packet(header(8, 1, schema1_sized))

        legacy = [1, receiver.VALID_AUDIO_RING, 0] + [0] * 30
        legacy[18] = 393216
        schema2_sized = identity() + receiver.STATUS_BODY_V2.pack(*(legacy + [0] * 17))
        with self.assertRaisesRegex(receiver.ProtocolError, "schema 1 has the wrong size"):
            receiver.parse_packet(header(8, 1, schema2_sized))

    def test_audio4m_rejects_extent_ring_and_fill_changes(self) -> None:
        cases = {
            "extent": (17, receiver.AUDIO4M_EXTENT_BYTES - 64),
            "ring": (18, receiver.AUDIO4M_RING_BYTES - 64),
            "fill": (19, receiver.AUDIO4M_RING_BYTES // 4 + 1),
        }
        for name, (index, value) in cases.items():
            with self.subTest(name=name):
                values = audio4m_values()
                values[index] = value
                payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
                with self.assertRaises(receiver.ProtocolError):
                    receiver.parse_packet(header(8, 1, payload))

    def test_audio4m_rejects_missing_usage_validity(self) -> None:
        values = audio4m_values()
        values[1] &= ~receiver.VALID_AUDIO4M_USAGE
        payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
        with self.assertRaises(receiver.ProtocolError):
            receiver.parse_packet(header(8, 1, payload))

    def test_audio4m_accepts_unowned_fault_diagnostic_without_proof(self) -> None:
        values = audio4m_values()
        values[1] &= ~receiver.VALID_ME_UPPER_OWNED
        values[16] = 0
        values[17] = 0
        values[28] = 1
        values[29] = 0x20AEAB7B
        values[33] = receiver.AUDIO4M_SFX_ATLAS_BYTES
        values[34] = 1_631_524
        values[35] = receiver.AUDIO4M_SFX_ATLAS_BYTES - values[34]
        values[40] = 1
        values[41] = 1
        values[49] = receiver.AUDIO4M_PROOF_ATLAS_EXACT
        payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
        packet = receiver.parse_packet(header(8, 1, payload))
        assert packet.status
        line = receiver.format_packet(packet, ("192.168.11.1", 9996))[0]
        self.assertIn("ME_EDRAM=0KiB UNKNOWN", line)
        self.assertIn("FIFO=1 SFXFATAL=1", line)

    def test_audio4m_rejects_unowned_extent_or_full_extent_claim(self) -> None:
        cases = {
            "address": (16, 64),
            "bytes": (17, receiver.AUDIO4M_EXTENT_BYTES),
            "proof": (49, receiver.AUDIO4M_PROOF_FULL_EXTENT),
        }
        for name, (index, value) in cases.items():
            with self.subTest(name=name):
                values = audio4m_values()
                values[1] &= ~receiver.VALID_ME_UPPER_OWNED
                values[16] = 0
                values[17] = 0
                values[49] = 0
                values[index] = value
                payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
                with self.assertRaises(receiver.ProtocolError):
                    receiver.parse_packet(header(8, 1, payload))

    def test_audio4m_rejects_atlas_sum_and_mask_range(self) -> None:
        mutations = {
            "sum": (35, 1),
            "canonical-mask": (36, 1 << 30),
            "replica-mask": (37, 1 << 31),
            "coverage-buffer": (45, 31),
            "proof-mask": (49, 1 << 8),
        }
        for name, (index, value) in mutations.items():
            with self.subTest(name=name):
                values = audio4m_values()
                values[index] = value
                payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
                with self.assertRaises(receiver.ProtocolError):
                    receiver.parse_packet(header(8, 1, payload))

    def test_audio4m_rejects_false_partial_proof_claims(self) -> None:
        claims = (
            receiver.AUDIO4M_PROOF_ATLAS_EXACT,
            receiver.AUDIO4M_PROOF_CANONICAL_DAC,
            receiver.AUDIO4M_PROOF_REPLICA_DAC,
            receiver.AUDIO4M_PROOF_BGM_UPLOAD_WRAP,
            receiver.AUDIO4M_PROOF_BGM_FETCH_WRAP,
            receiver.AUDIO4M_PROOF_BGM_OUTPUT_WRAP,
        )
        for claim in claims:
            with self.subTest(claim=claim):
                values = audio4m_values()
                values[49] = claim
                payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
                with self.assertRaises(receiver.ProtocolError):
                    receiver.parse_packet(header(8, 1, payload))

    def test_audio4m_proven_revalidates_every_gate(self) -> None:
        mutations = {
            "proof": (49, receiver.AUDIO4M_PROOF_REQUIRED - 1),
            "atlas": (33, receiver.AUDIO4M_SFX_ATLAS_BYTES - 2),
            "canonical-empty": (34, 0),
            "canonical-mask": (36, receiver.AUDIO4M_SFX_REQUIRED_MASK - 1),
            "replica-mask": (37, receiver.AUDIO4M_SFX_REQUIRED_MASK - 1),
            "coverage-active": (42, 1),
            "coverage-incomplete": (43, 0),
            "coverage-failed": (44, 0),
            "coverage-buffer": (45, 29),
            "upload-wrap": (46, 0),
            "fetch-wrap": (47, 0),
            "output-wrap": (48, 0),
            "fifo": (40, 1),
            "sfx-fatal": (41, 1),
            "underrun": (20, 1),
            "fallback": (24, 1),
            "timeout": (25, 1),
            "fatal": (28, 1),
        }
        for name, (index, value) in mutations.items():
            with self.subTest(name=name):
                values = audio4m_values(proven=True)
                values[index] = value
                if name == "atlas":
                    values[35] -= 2
                elif name == "canonical-empty":
                    values[35] = receiver.AUDIO4M_SFX_ATLAS_BYTES
                payload = identity() + receiver.STATUS_BODY_V2.pack(*values)
                with self.assertRaises(receiver.ProtocolError):
                    receiver.parse_packet(header(8, 1, payload))

    def test_rejects_noncanonical_ring(self) -> None:
        values = [1, receiver.VALID_AUDIO_RING, 0] + [0] * 30
        values[18] = 262144
        payload = identity() + receiver.STATUS_BODY.pack(*values)
        with self.assertRaises(receiver.ProtocolError):
            receiver.parse_packet(header(8, 1, payload))

    def test_rejects_unowned_me_address(self) -> None:
        values = [1, 0, 0] + [0] * 30
        values[16] = 0x00200000
        payload = identity() + receiver.STATUS_BODY.pack(*values)
        with self.assertRaises(receiver.ProtocolError):
            receiver.parse_packet(header(8, 1, payload))

    def test_rejects_changed_owned_me_extent(self) -> None:
        values = [1, receiver.VALID_AUDIO_RING | receiver.VALID_ME_UPPER_OWNED, 0] + [0] * 30
        values[16] = 0x00200000
        values[17] = 262144
        values[18] = 393216
        payload = identity() + receiver.STATUS_BODY.pack(*values)
        with self.assertRaises(receiver.ProtocolError):
            receiver.parse_packet(header(8, 1, payload))

    def test_rejects_bad_declared_length(self) -> None:
        datagram = bytearray(header(1, 1, identity()))
        struct.pack_into("!H", datagram, 16, 7)
        with self.assertRaises(receiver.ProtocolError):
            receiver.parse_packet(bytes(datagram))


class SourcePolicyTest(unittest.TestCase):
    def test_observer_scope(self) -> None:
        source = (ROOT / "psp" / "shikigami_th07.c").read_text(encoding="utf-8")
        self.assertIn("sceNetApctlConnect(TH07_SHIKIGAMI_PROFILE)", source)
        self.assertIn("SO_NONBLOCK", source)
        self.assertIn("MSG_DONTWAIT", source)
        self.assertNotIn("LoadExec", source)
        self.assertNotIn("self_update", source)
        self.assertNotIn("sceIoWrite", source)
        self.assertNotIn("meCoreEDRAMAlloc", source)
        self.assertNotIn("0x00200000", source)
        self.assertNotIn("0x00300000", source)
        self.assertNotIn("0x00340000", source)

    def test_worker_does_not_write_memory_stick_log(self) -> None:
        source = (ROOT / "psp" / "shikigami_th07.c").read_text(encoding="utf-8")
        worker = source[
            source.index("static int observer_worker") :
            source.index("int th07_shikigami_start")
        ]
        self.assertNotIn("th07_psp_boot_note", worker)
        self.assertIn("hello_result == 0 || status_result == 0", worker)
        self.assertIn("SHIKIGAMI_NOTICE_SEND_FAILED", worker)

    def test_item_me_decision_is_sent_initially_periodically_and_finally(self) -> None:
        source = (ROOT / "psp" / "shikigami_th07.c").read_text(encoding="utf-8")
        worker = source[
            source.index("static int observer_worker") :
            source.index("int th07_shikigami_start")
        ]
        self.assertEqual(worker.count("send_item_me_packet"), 3)
        self.assertIn("SHIKIGAMI_PACKET_TH07_ITEM_ME", source)
        self.assertIn("SHIKIGAMI_ITEM_ME_PAYLOAD_BYTES = 48", source)

        audio = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        snapshot = audio[
            audio.index("void th07_psp_me_item_render_diag_snapshot") :
            audio.index("static int me_render_stream_item_failure_recoverable")
        ]
        for forbidden in ("sceNet", "sceIo", "malloc", "th07_psp_boot_note"):
            self.assertNotIn(forbidden, snapshot)

        header_source = (ROOT / "psp" / "shikigami_th07.h").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("Th07PspMeItemRenderDiag", header_source)

    def test_a1_move_decision_is_sent_initially_periodically_and_finally(self) -> None:
        source = (ROOT / "psp" / "shikigami_th07.c").read_text(encoding="utf-8")
        worker = source[
            source.index("static int observer_worker") :
            source.index("int th07_shikigami_start")
        ]
        self.assertEqual(worker.count("send_a1_move_packet"), 3)
        self.assertIn("SHIKIGAMI_PACKET_TH07_A1_MOVE = 13", source)
        self.assertIn("SHIKIGAMI_A1_MOVE_PAYLOAD_BYTES = 52", source)

    def test_ge_ownership_is_sampled_only_by_worker_packet_paths(self) -> None:
        source = (ROOT / "psp" / "shikigami_th07.c").read_text(encoding="utf-8")
        publish = source[
            source.index("void th07_shikigami_publish_frame") :
            source.index("void th07_shikigami_record_fatal")
        ]
        status = source[
            source.index("static int send_status_packet") :
            source.index("static void fill_portrait_slot")
        ]
        portrait = source[
            source.index("static int send_portrait_cache_packet") :
            source.index("static int send_event_packet")
        ]
        self.assertNotIn("th07_psp_ge4_active", publish)
        self.assertEqual(status.count("th07_psp_ge4_active()"), 1)
        self.assertEqual(portrait.count("th07_psp_ge4_active()"), 1)
        self.assertIn("SHIKIGAMI_VALID_GE_UPPER_OWNED", status)

    def test_destination_is_part_of_profile_stamp(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("SHIKIGAMI_HOST_STAMP", makefile)
        stamp = next(
            line for line in makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(SHIKIGAMI_HOST_STAMP)", stamp)

    def test_start_is_after_first_render_marker(self) -> None:
        source = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
        marker = source.index('th07_psp_boot_note("first render complete")')
        start = source.index("th07_shikigami_start()")
        self.assertLess(marker, start)

    def test_frame_publish_is_20hz_without_throttling_frame_counter(self) -> None:
        source = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
        self.assertIn("kShikigamiPublishPeriodFrames = 3", source)
        render_loop = source[
            source.index("res = g_GameWindow.Render();") : source.index("cleanup:")
        ]
        increment = render_loop.index("const u32 frameNumber = ++shikigamiFrame;")
        gate = render_loop.index(
            "frameNumber % kShikigamiPublishPeriodFrames == 0u"
        )
        publish = render_loop.index("PublishShikigamiFrame(frameNumber);")
        self.assertLess(increment, gate)
        self.assertLess(gate, publish)
        self.assertNotIn("PublishShikigamiFrame(++shikigamiFrame)", render_loop)

    def test_throttle_preserves_event_path_and_observer_priority(self) -> None:
        source = (ROOT / "psp" / "shikigami_th07.c").read_text(encoding="utf-8")
        changed_events = source[
            source.index("static void send_changed_events") :
            source.index("static int observer_worker")
        ]
        for event in (
            "SHIKIGAMI_EVENT_SUPERVISOR",
            "SHIKIGAMI_EVENT_STAGE",
            "SHIKIGAMI_EVENT_BGM",
            "SHIKIGAMI_EVENT_UNDERRUN",
            "SHIKIGAMI_EVENT_FATAL",
            "SHIKIGAMI_EVENT_GAME_FLAGS",
        ):
            self.assertIn(event, changed_events)
        worker = source[
            source.index("static int observer_worker") :
            source.index("int th07_shikigami_start")
        ]
        self.assertEqual(worker.count("send_changed_events"), 2)
        self.assertIn("previous = current;", worker)
        self.assertIn("observer_worker, 0x30, 0x4000", source)

    def test_audio_ring_is_read_only_and_exact(self) -> None:
        source = (ROOT / "psp" / "SoundPlayerPsp.cpp").read_text(encoding="utf-8")
        self.assertIn("kRingFrames = 96 * 1024", source)
        self.assertIn("== 393216", source)
        getter = source[source.index("th07_psp_audio_shikigami_snapshot") :]
        self.assertNotIn("gBgmRing[", getter)
        self.assertNotIn("meCoreEDRAMAlloc", getter)


if __name__ == "__main__":
    unittest.main()

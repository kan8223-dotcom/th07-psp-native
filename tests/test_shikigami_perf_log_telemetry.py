from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RECEIVER_PATH = ROOT / "tools" / "shikigami_th07_receiver.py"
SPEC = importlib.util.spec_from_file_location(
    "shikigami_th07_perf_receiver", RECEIVER_PATH
)
assert SPEC and SPEC.loader
receiver = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = receiver
SPEC.loader.exec_module(receiver)


def identity() -> bytes:
    return receiver.IDENTITY.pack(3, 1, 4, 0xC0A80BC8)


def perf_packet(
    data: bytes,
    index: int,
    total_data: bytes,
    *,
    run_id: int = 0x1234ABCD,
    snapshot_id: int = 7,
    window_id: int = 15,
    sequence: int = 1,
    retry: bool = False,
    valid: bool = True,
    dropped: int = 0,
    offset_override: int | None = None,
    chunk_crc_override: int | None = None,
) -> bytes:
    count = max(
        1,
        (len(total_data) + receiver.PERF_LOG_CHUNK_BYTES - 1)
        // receiver.PERF_LOG_CHUNK_BYTES,
    )
    flags = 0
    if index == 0:
        flags |= receiver.PERF_LOG_BEGIN
    if index + 1 == count:
        flags |= receiver.PERF_LOG_END
    if retry:
        flags |= receiver.PERF_LOG_RETRY
    if valid:
        flags |= receiver.PERF_LOG_VALID
    offset = index * receiver.PERF_LOG_CHUNK_BYTES
    if offset_override is not None:
        offset = offset_override
    chunk_crc = zlib.crc32(data) & 0xFFFF_FFFF
    if chunk_crc_override is not None:
        chunk_crc = chunk_crc_override
    body = receiver.PERF_LOG_BODY.pack(
        receiver.PERF_LOG_SCHEMA,
        flags,
        run_id,
        snapshot_id,
        window_id,
        len(total_data),
        offset,
        zlib.crc32(total_data) & 0xFFFF_FFFF,
        chunk_crc,
        dropped,
        index,
        count,
        len(data),
        0,
    )
    payload = identity() + body + data
    return receiver.HEADER.pack(
        receiver.MAGIC,
        receiver.PROTOCOL_VERSION,
        receiver.PacketType.TH07_PERF_LOG,
        sequence,
        1234,
        len(payload),
        3,
        0x20260827,
    ) + payload


def chunk(total: bytes, index: int) -> bytes:
    start = index * receiver.PERF_LOG_CHUNK_BYTES
    return total[start : start + receiver.PERF_LOG_CHUNK_BYTES]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class PerfLogProtocolTests(unittest.TestCase):
    def test_type11_has_explicit_offset_and_independent_schema(self) -> None:
        total = b"PERF test\n"
        packet = receiver.parse_packet(perf_packet(total, 0, total))
        assert packet.perf_log
        value = packet.perf_log
        self.assertEqual(packet.packet_type, receiver.PacketType.TH07_PERF_LOG)
        self.assertEqual(value.schema, 1)
        self.assertEqual(value.offset, 0)
        self.assertEqual(value.total_bytes, len(total))
        self.assertEqual(value.data, total)
        self.assertEqual(receiver.STATUS_BODY_V1.size, 128)
        self.assertEqual(receiver.STATUS_BODY_V2.size, 196)
        self.assertEqual(receiver.PORTRAIT_CACHE_BODY.size, 336)

    def test_rejects_bad_offset_chunk_crc_and_valid_drop_claim(self) -> None:
        total = b"x" * 100
        cases = (
            perf_packet(total, 0, total, offset_override=1),
            perf_packet(total, 0, total, chunk_crc_override=0),
            perf_packet(total, 0, total, valid=True, dropped=1),
        )
        for datagram in cases:
            with self.subTest(datagram=datagram[-8:]):
                with self.assertRaises(receiver.ProtocolError):
                    receiver.parse_packet(datagram)

    def test_out_of_order_and_duplicate_chunks_confirm_atomically(self) -> None:
        total = bytes(range(251)) * 9
        packets = [
            receiver.parse_packet(perf_packet(chunk(total, 2), 2, total, sequence=1)),
            receiver.parse_packet(perf_packet(chunk(total, 0), 0, total, sequence=2)),
            receiver.parse_packet(
                perf_packet(chunk(total, 0), 0, total, sequence=3, retry=True)
            ),
            receiver.parse_packet(perf_packet(chunk(total, 1), 1, total, sequence=4)),
        ]
        source = ("192.168.11.200", 43210)
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary) / "TH07PSP_PERF_UDP.LOG"
            merger = receiver.PerfLogReassembler(base)
            lines: list[str] = []
            for packet in packets[:-1]:
                lines.extend(merger.accept(packet, source, now=1.0))
                self.assertEqual(list(Path(temporary).glob("*.LOG")), [])
            lines.extend(merger.accept(packets[-1], source, now=1.1))
            outputs = list(Path(temporary).glob("*.LOG"))
            self.assertEqual(len(outputs), 1)
            self.assertEqual(outputs[0].read_bytes(), total)
            self.assertIn("RID1234ABCD", outputs[0].name)
            self.assertIn("SNAP7", outputs[0].name)
            self.assertIn(f"TOTAL{len(total)}", outputs[0].name)
            self.assertIn(f"CRC{zlib.crc32(total) & 0xFFFF_FFFF:08X}", outputs[0].name)
            complete = next(line for line in lines if " COMPLETE]" in line)
            self.assertIn("DUP=1", complete)
            self.assertIn("PROFILE=VALID", complete)

    def test_incomplete_snapshot_is_explicit_and_never_gets_log_name(self) -> None:
        total = b"z" * 1200
        packet = receiver.parse_packet(perf_packet(chunk(total, 0), 0, total))
        with tempfile.TemporaryDirectory() as temporary:
            merger = receiver.PerfLogReassembler(
                Path(temporary) / "TH07PSP_PERF_UDP.LOG"
            )
            merger.accept(packet, ("192.168.11.200", 1), now=1.0)
            lines = merger.finish("receiver-stop")
            self.assertEqual(list(Path(temporary).iterdir()), [])
            self.assertEqual(len(lines), 1)
            self.assertIn("INCOMPLETE", lines[0])
            self.assertIn("MISSING=1", lines[0])
            self.assertIn("REASON=receiver-stop", lines[0])


class PerfLogSourcePolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.sender = (ROOT / "psp/shikigami_th07.c").read_text()
        cls.fileio = (ROOT / "psp/fileio.cpp").read_text()
        cls.game = (ROOT / "src/GameManager.cpp").read_text()
        cls.main = (ROOT / "src/main.cpp").read_text()
        cls.makefile = (ROOT / "Makefile").read_text()

    def test_request_boundary_seals_then_latches_without_disk_flush(self) -> None:
        deleted = function_body(self.game, "ZunResult GameManager::DeletedCallback")
        finalize = deleted.index("Th07PspPerfFinalizeGameplayWindow();")
        seal = deleted.index("th07_psp_perf_log_seal();")
        request = deleted.index("th07_shikigami_request_perf_log();")
        self.assertLess(finalize, seal)
        self.assertLess(seal, request)
        shikigami_branch = deleted[deleted.index("#if defined(TH07_PSP_SHIKIGAMI)") :]
        self.assertLess(
            shikigami_branch.index("th07_shikigami_request_perf_log();"),
            shikigami_branch.index("#else"),
        )
        self.assertNotIn(
            "th07_psp_perf_log_flush();",
            shikigami_branch[: shikigami_branch.index("#else")],
        )

    def test_worker_is_read_only_and_pauses_during_gameplay(self) -> None:
        request = function_body(
            self.sender, "void th07_shikigami_request_perf_log(void)"
        )
        service = function_body(self.sender, "static void service_perf_log_transfer")
        read = function_body(
            self.fileio, 'extern "C" uint32_t th07_psp_perf_log_snapshot_read'
        )
        self.assertNotIn("sceNet", request)
        self.assertNotIn("malloc", request)
        self.assertNotIn("sceIo", request)
        self.assertIn("th07_psp_perf_log_snapshot_begin", service)
        self.assertIn("send_perf_log_chunk", service)
        self.assertIn("gPerfGameplayActive", read)
        self.assertIn("gPerfStageLoadActive", read)
        self.assertIn("LockBootLog()", read)
        self.assertIn("std::memcpy(destination, gPerfLogBuffer + offset", read)
        self.assertNotIn("sceIo", read)
        self.assertNotIn("sceNet", read)
        self.assertNotIn("malloc", read)

    def test_fallback_flush_runs_only_after_observer_shutdown(self) -> None:
        psp_shutdown = self.main.index("th07_shikigami_shutdown();")
        fallback = self.main.index("th07_psp_perf_log_flush();", psp_shutdown)
        self.assertLess(psp_shutdown, fallback)
        self.assertIn(
            "snapshot->buffer_generation != gPerfLogBufferGeneration",
            self.fileio,
        )

    def test_combined_feature_is_compile_time_gated(self) -> None:
        self.assertIn("CFLAGS += -DTH07_PSP_PERF_DIAG", self.makefile)
        self.assertIn(
            "#if defined(TH07_PSP_SHIKIGAMI) && defined(TH07_PSP_PERF_DIAG)",
            self.fileio,
        )
        self.assertIn("#if defined(TH07_PSP_PERF_DIAG)", self.sender)
        self.assertIn("SHIKIGAMI_PACKET_TH07_PERF_LOG = 11", self.sender)


if __name__ == "__main__":
    unittest.main()

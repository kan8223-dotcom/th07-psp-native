from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class PspMeItemMotionSelftestContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "psp/audio_me.h").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")

    def test_command10_sidecar_is_a_separate_startup_gate(self) -> None:
        self.assertIn(
            "TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED = 1u << 6",
            self.header,
        )
        bounds = body(self.audio, "me_render_stream_bounds_valid(")
        submit = body(self.audio, "th07_psp_me_render_stream_submit(")
        worker = body(self.audio, "process_render_stream_on_me(")
        self.assertIn("if (itemMotionSeed && !itemList)", bounds)
        self.assertIn("gMeItemMotionEnabled", submit)
        self.assertIn("gMeItemMotionSelftestInProgress", submit)
        self.assertIn("const uint32_t itemMotionSidecar", worker)
        self.assertIn("if (itemMotionSidecar)", worker)
        self.assertNotIn("if (itemList)\n        {\n            itemMotionSeed", worker)

    def test_public_admission_cannot_bypass_startup(self) -> None:
        available = body(
            self.audio, "th07_psp_me_item_motion_available("
        )
        begin = body(self.audio, "th07_psp_me_bullet_compact_begin(")
        self.assertIn("gMeItemMotionEnabled", available)
        self.assertIn("gMeItemRenderEnabled", available)
        self.assertIn("gMeItemMotionEnabled", begin)
        self.assertIn("gMeItemMotionSelftestInProgress", begin)

    def test_selftest_has_independent_raw_goldens_for_all_routes(self) -> None:
        golden = body(
            self.audio, "me_item_motion_selftest_golden_matches("
        )
        run = body(self.audio, "me_item_motion_selftest_one(")
        for route in (
            "0x02010101u",  # home
            "0x04030002u",  # interpolate
            "0x05010000u",  # state2 == 60
            "0x06010002u",  # state2 > 60
            "0x03010000u",  # spawn
            "0x01010000u",  # fall
        ):
            self.assertIn(route, golden)
        self.assertIn("0xc00ccccdu", golden)  # canonical -2.2 clamp
        for nontrivial in (
            "0x42f46666u",
            "0x42db3333u",
            "0xc019999bu",
            "0xc04cccccu",
            "0x42210000u",
            "0x42490000u",
        ):
            self.assertIn(nontrivial, golden)
        self.assertIn("me_item_motion_selftest_golden_matches", run)
        self.assertIn("me_render_write_fcr31(0u)", run)
        self.assertLess(
            run.index("const uint32_t effectiveFcr31"),
            run.index("me_item_motion_update_kernel("),
        )
        self.assertIn("restoredFcr31 != originalFcr31", run)
        self.assertIn("memcmp(&actual->slots[slot]", run)

    def test_real_command10_capture_is_proven_before_command12(self) -> None:
        capture = body(
            self.audio, "me_item_motion_selftest_capture_probe("
        )
        suite = body(self.audio, "selftest_item_motion_update(")
        for evidence in (
            "TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED",
            "ME_ITEM_MOTION_CURRENT_POS_OFFSET",
            "ME_ITEM_MOTION_START_POS_OFFSET",
            "ME_ITEM_MOTION_TARGET_POS_OFFSET",
            "ME_ITEM_MOTION_TIMER_SUBFRAME_OFFSET",
            "sceKernelDcacheInvalidateRange(area, sizeof(*area))",
            "me_item_motion_seed_guards_match",
            "me_item_motion_seed_header_valid",
            "seed->header.candidateCount == 1u",
            "word < TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS",
            "seed->candidateBits[word] != 0u",
            "memcmp(slotWords, expectedSlotWords",
            '"A1M COMMAND10 SIDECAR PASS IM01 C1"',
        ):
            self.assertIn(evidence, capture)
        self.assertLess(
            suite.index("me_item_motion_selftest_capture_probe("),
            suite.index("me_item_motion_selftest_one("),
        )

    def test_expected_item_record_proves_segment_local_reject(self) -> None:
        run = body(self.audio, "me_item_motion_selftest_one(")
        self.assertIn("expectItemReject", run)
        self.assertIn(
            "completion.itemResult != TH07_PSP_ME_ITEM_MOTION_RESULT_RECORD",
            run,
        )
        self.assertIn("poll != 1 || !output || !publishedSeed", run)

    def test_rid28_startup_fallback_reproves_base_me17(self) -> None:
        init = body(self.audio, "th07_psp_me_audio_init(")
        self.assertLess(
            init.index("if (!selftest_bullet_compact_update())"),
            init.index("selftest_item_motion_update("),
        )
        for evidence in (
            "ME_ITEM_MOTION_SELFTEST_SAFE_FAIL",
            "me_item_motion_failure_recoverable()",
            "me_item_motion_selftest_cleanup();",
            '"A1M SELFTEST NG -> MOVE OFF; RETRY ME17"',
            "if (selftest_bullet_compact_update())",
            "TH07_PSP_ME_ITEM_MOTION_STATE_SAFE_FALLBACK",
            '"A1M MOVE OFF; ITEM DRAW+ME17 ACTIVE (SAFE FALLBACK)"',
        ):
            self.assertIn(evidence, init)
        recoverable = body(self.audio, "me_item_motion_failure_recoverable(")
        self.assertIn("gMeRenderStreamInFlightSlot", recoverable)
        # Motion downgrade must never close RID29 Item geometry.
        motion_part = init[init.index("selftest_item_motion_update(") :]
        self.assertNotIn("gMeItemRenderEnabled, 0u", motion_part)

    def test_common_timeout_protocol_remain_fatal(self) -> None:
        init = body(self.audio, "th07_psp_me_audio_init(")
        selftest = body(self.audio, "me_item_motion_selftest_one(")
        self.assertIn("ME_ITEM_MOTION_SELFTEST_FATAL", selftest)
        self.assertIn("ME_BULLET_COMPACT_TIMEOUT_US", selftest)
        self.assertIn("TH07_PSP_ME_ITEM_MOTION_REASON_COMMON_FATAL", selftest)
        self.assertIn("A1M COMMON SELFTEST NG -> COLD REBOOT", init)

    def test_runtime_clean_item_reject_closes_motion_only(self) -> None:
        poll = body(self.audio, "th07_psp_me_bullet_compact_poll(")
        self.assertIn("const int itemCleanReject", poll)
        self.assertIn("commonValid && outputValid", poll)
        self.assertIn(
            "local.itemResult != TH07_PSP_ME_ITEM_MOTION_RESULT_PROTOCOL",
            poll,
        )
        reject = poll[poll.index("if (itemCleanReject)") :]
        self.assertIn("gMeItemMotionEnabled, 0u", reject)
        self.assertIn("me_item_motion_reset_arenas_on_sc();", reject)
        self.assertIn("TH07_PSP_ME_ITEM_MOTION_STATE_SAFE_FALLBACK", reject)
        self.assertNotIn("poison_me();", reject.split("#endif", 1)[0])

    def test_diag_snapshot_is_nonblocking_atomic_only(self) -> None:
        snapshot = body(
            self.audio, "th07_psp_me_item_motion_diag_snapshot("
        )
        self.assertIn("__atomic_load_n", snapshot)
        for forbidden in ("malloc", "fopen", "sceIo", "sceKernelDelayThread"):
            self.assertNotIn(forbidden, snapshot)


if __name__ == "__main__":
    unittest.main()

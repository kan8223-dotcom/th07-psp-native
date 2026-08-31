from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "TH07_PSP_ME_RENDER_DIRECT_LIST"
MAKE_FEATURE = "PSP_ME_RENDER_DIRECT_LIST"


def function_body(source: str, signature: str) -> str:
    """Return one C/C++ function body without depending on its return type."""
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


def make_target(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(
        r"\n(?=[A-Za-z0-9_.-]+(?:\s+[^\n:]*)?:)", makefile[start + 1 :]
    )
    return (
        makefile[start:]
        if match is None
        else makefile[start : start + match.start() + 1]
    )


def declaration(source: str, opening: str, closing: str) -> str:
    start = source.index(opening)
    end = source.index(closing, start) + len(closing)
    return source[start:end]


def assert_order(test: unittest.TestCase, source: str, *needles: str) -> None:
    cursor = -1
    for needle in needles:
        found = source.find(needle, cursor + 1)
        test.assertNotEqual(found, -1, f"missing ordered token: {needle}")
        test.assertGreater(found, cursor, f"out-of-order token: {needle}")
        cursor = found


class PspMeRenderWorkerI5Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        cls.audio_h = (ROOT / "psp" / "audio_me.h").read_text(
            encoding="utf-8"
        )
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.window = (ROOT / "src" / "GameWindow.cpp").read_text(
            encoding="utf-8"
        )
        cls.main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")

    def test_profile_is_an_explicit_i4_performance_psp3000_increment(self) -> None:
        self.assertIn(f"{MAKE_FEATURE} ?= 0", self.makefile)
        self.assertIn(f"CXXFLAGS += -D{FEATURE}", self.makefile)
        self.assertIn(f"CFLAGS += -D{FEATURE}", self.makefile)
        self.assertIn(
            "$(error PSP_ME_RENDER_DIRECT_LIST requires "
            "PSP_ME_RENDER_RAW_LIVE=1)",
            self.makefile,
        )
        self.assertIn(
            "$(error PSP_ME_RENDER_DIRECT_LIST is PSP-2000+ only)",
            self.makefile,
        )
        self.assertIn(
            "I-ME5 direct-list traversal requires the I-ME4 raw-live profile",
            self.audio_h,
        )
        recipe = make_target(
            self.makefile, "psp3000-me-render-i5-direct-list-build"
        )
        for setting in (
            "PSP_1000=0",
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_GE_CONSUME=1",
            "PSP_ME_RENDER_PERFORMANCE=1",
            "PSP_ME_RENDER_RAW_LIVE=1",
            "PSP_ME_RENDER_DIRECT_LIST=1",
            "PSP_MECC_AUDIO_4M=1",
            "PSP_AUDIO4M_BUILD_ID=0x26083015u",
        ):
            self.assertIn(setting, recipe)
        self.assertIn("$(PSP_ME_RENDER_DIRECT_LIST)-", self.makefile)
        self.assertIn("MERW I-ME5 LIST-LIVE BULLET", self.main)

    def test_me15_job_carries_exact_six_head_owner_layout(self) -> None:
        for evidence in (
            "TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST = 1u << 3",
            "TH07_PSP_ME_RENDER_STREAM_LIST_VERSION = 0x4d453135u",
            "TH07_PSP_ME_RENDER_LIST_LAYOUT_VERSION = 0x4c4c3031u",
            "Th07PspMeRenderListLayout listLayout",
            "sizeof(Th07PspMeRenderListLayout) == 128u",
        ):
            self.assertIn(evidence, self.audio_h + self.audio + self.bullets)

        layout = declaration(
            self.audio_h,
            "typedef struct Th07PspMeRenderListLayout",
            "} Th07PspMeRenderListLayout;",
        )
        assert_order(
            self,
            layout,
            "listLayoutVersion",
            "listLayoutBytes",
            "bulletBasePhys",
            "bulletStride",
            "bulletCount",
            "generationBasePhys",
            "generationStride",
            "generationCount",
            "activeBitsPhys",
            "activeBitsWordCount",
            "bucketHeadPhys[6]",
            "bulletNextOffset",
            "bulletStateOffset",
            "bulletCollisionTypeOffset",
            "bulletPosXOffset",
            "bulletPosYOffset",
            "bulletRenderAngleOffset",
            "bulletSinOffset",
            "bulletCosOffset",
            "bulletRotationValidOffset",
            "bulletVmOffsets[5]",
            "arcadeLeftBits",
            "arcadeTopBits",
        )

    def test_sc_i5_keeps_side_effect_walk_but_compiles_out_32_byte_staging(self) -> None:
        capture_decl = declaration(
            self.bullets,
            "struct alignas(64) PspMeRenderFusedCapture",
            "};",
        )
        self.assertRegex(
            capture_decl,
            rf"(?s)#if\s+!defined\({FEATURE}\).*?records\[6\]"
            rf".*?#endif",
        )

        capture = function_body(self.bullets, "PspMeRenderCaptureFusedRecord(")
        # These are the canonical Bullet::Draw VM/cache side effects and must
        # remain on SC even though I-ME5 emits no per-bullet input record.
        for side_effect in (
            "vm->pos.x =",
            "vm->pos.y =",
            "vm->pos.z = 0.05f",
            "vm->color.color =",
            "bullet->pspRenderSourceAngle = bullet->angle",
            "bullet->pspRenderAngle = renderAngle",
            "bullet->pspRenderRotationValid = 1u",
            "vm->SetRotationZ(bullet->pspRenderAngle)",
            "vm->updateRotation = 1",
            "++capture.bucketCounts[bucket]",
        ):
            self.assertIn(side_effect, capture)
        self.assertRegex(
            capture,
            rf"(?s)#if\s+!defined\({FEATURE}\).*?"
            r"Th07PspMeRenderRawRecord\s*&record.*?#endif",
        )

        build = function_body(self.bullets, "PspMeRenderBuildFusedSnapshot(")
        self.assertIn(f"#if defined({FEATURE})", build)
        direct_start = build.index(f"#if defined({FEATURE})")
        direct_end = build.index("#else", direct_start)
        self.assertNotIn("std::memcpy", build[direct_start:direct_end])
        self.assertIn("std::memcpy", build[direct_end:])
        self.assertIn("sizeof(Th07PspMeRenderRawRecord)", build[direct_end:])

    def test_sc_publishes_pool_generation_bitmap_and_six_heads(self) -> None:
        build = function_body(self.bullets, "PspMeRenderBuildFusedSnapshot(")
        for evidence in (
            "job->version = TH07_PSP_ME_RENDER_STREAM_LIST_VERSION",
            "TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST",
            "job->listLayout.listLayoutVersion",
            "job->listLayout.listLayoutBytes",
            "job->listLayout.bulletBasePhys",
            "job->listLayout.bulletStride",
            "job->listLayout.bulletCount",
            "job->listLayout.generationBasePhys",
            "job->listLayout.generationStride",
            "job->listLayout.generationCount",
            "job->listLayout.activeBitsPhys",
            "job->listLayout.activeBitsWordCount",
            "job->listLayout.bucketHeadPhys[bucket]",
            "job->listLayout.bulletNextOffset",
            "job->listLayout.bulletStateOffset",
            "job->listLayout.bulletCollisionTypeOffset",
            "job->listLayout.bulletVmOffsets[0]",
            "job->listLayout.bulletVmOffsets[4]",
            "job->listLayout.arcadeLeftBits",
            "job->listLayout.arcadeTopBits",
        ):
            self.assertIn(evidence, build)
        self.assertIn("for (u32 bucket = 0u; bucket < 6u; ++bucket)", build)

    def test_me_validates_the_complete_list_layout_before_dereference(self) -> None:
        layout = function_body(self.audio, "me_render_stream_list_layout_valid(")
        for required in (
            "TH07_PSP_ME_RENDER_LIST_LAYOUT_VERSION",
            "bulletBasePhys",
            "bulletStride",
            "bulletCount",
            "generationBasePhys",
            "generationStride",
            "generationCount",
            "activeBitsPhys",
            "activeBitsWordCount",
            "bucketHeadPhys",
            "bulletNextOffset",
            "bulletStateOffset",
            "bulletCollisionTypeOffset",
            "bulletPosXOffset",
            "bulletPosYOffset",
            "bulletRenderAngleOffset",
            "bulletSinOffset",
            "bulletCosOffset",
            "bulletRotationValidOffset",
            "bulletVmOffsets",
        ):
            self.assertIn(required, layout)
        self.assertRegex(
            layout,
            r"listLayoutBytes\s*!=\s*"
            r"(?:sizeof\(\*layout\)|sizeof\(Th07PspMeRenderListLayout\))",
        )

        physical = function_body(
            self.audio, "me_render_stream_list_bullet_physical("
        )
        self.assertRegex(
            physical,
            r"(?:candidate|phys)\s*<\s*layout->bulletBasePhys",
        )
        self.assertIn("delta % layout->bulletStride", physical)
        self.assertRegex(
            physical,
            r"(?s)(?:delta\s*/\s*layout->bulletStride|slot).*?"
            r"(?:>=|<).*?layout->bulletCount",
        )

    def test_me_rejects_cycles_stale_slots_wrong_state_and_wrong_bucket(self) -> None:
        cursor_decl = declaration(
            self.audio,
            "typedef struct MeRenderStreamListCursor",
            "} MeRenderStreamListCursor;",
        )
        self.assertRegex(cursor_decl, r"(?:seen|visited|duplicate).*\[")

        reconstruct = function_body(
            self.audio, "me_render_stream_reconstruct_list_record("
        )
        combined = cursor_decl + reconstruct
        for proof in (
            "bulletNextOffset",
            "activeBitsPhys",
            "generationBasePhys",
            "bulletStateOffset",
            "bulletCollisionTypeOffset",
            "bucket",
        ):
            self.assertIn(proof, combined)
        self.assertRegex(combined, r"state\s*<\s*1u")
        self.assertRegex(combined, r"state\s*>\s*5u")
        self.assertRegex(combined, r"(?i)(?:seen|visited|duplicate).*bit")
        self.assertRegex(combined, r"generation.*(?:==|!=)\s*0u")
        self.assertRegex(combined, r"(?i)collision.*(?:==|!=).*bucket")
        self.assertGreaterEqual(
            reconstruct.count("me_render_stream_list_bullet_physical("), 2
        )
        self.assertIn("nextPhys != nextPointer", reconstruct)

    def test_me_walks_six_buckets_in_canonical_link_order(self) -> None:
        worker = function_body(self.audio, "process_render_stream_on_me(")
        cursor_init = function_body(
            self.audio, "me_render_stream_list_cursor_init("
        )
        kernel = function_body(self.audio, "me_render_stream_expand_kernel(")
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST", worker)
        self.assertIn("renderStreamListLayout", worker)
        self.assertIn("directList", worker)
        self.assertRegex(
            cursor_init, r"bucket\s*=\s*0u;\s*bucket\s*<\s*6u"
        )
        self.assertIn("layout->bucketHeadPhys[bucket]", cursor_init)
        assert_order(
            self,
            kernel,
            "me_render_stream_list_cursor_init(",
            "for (uint32_t recordIndex = 0u; recordIndex < recordCount;",
            "me_render_stream_reconstruct_list_record(",
        )
        self.assertIn("bucketEnds", kernel)

    def test_i4_raw_record_profile_remains_reproducible(self) -> None:
        for evidence in (
            "TH07_PSP_ME_RENDER_STREAM_RAW_VERSION = 0x4d453134u",
            "TH07_PSP_ME_RENDER_STREAM_RAW_RECORD_BYTES = 32",
            "typedef struct Th07PspMeRenderRawRecord",
            "me_render_stream_reconstruct_raw_record(",
            "PspMeRenderCaptureFusedRecord(",
            "sizeof(Th07PspMeRenderRawRecord)",
        ):
            self.assertIn(evidence, self.audio_h + self.audio + self.bullets)
        i4 = make_target(self.makefile, "psp3000-me-render-i4-raw-build")
        self.assertIn("PSP_ME_RENDER_RAW_LIVE=1", i4)
        self.assertNotIn("PSP_ME_RENDER_DIRECT_LIST=1", i4)

    def test_i5_early_submit_is_gated_ordered_and_cannot_double_submit(self) -> None:
        gate = function_body(self.window, "Th07PspCanCommitBulletWarmQueue()")
        for proof in (
            "!g_PspFixed30Fps",
            "!WAS_PRESSED_RAW(TH_BUTTON_FPS_TOGGLE)",
            "!ReplayManager::MayRestartCalcChainAfterBulletUpdate()",
        ):
            self.assertIn(proof, gate)

        sentinel = function_body(
            self.bullets, "PspMeRenderCalcCompleteSentinel(void *)"
        )
        assert_order(
            self,
            sentinel,
            f"#if defined({FEATURE})",
            "const u32 previousSerial =",
            "++gPspMeRenderShadow.calcCompleteSerial",
            "if (gPspMeRenderFusedCapture.published != 0u)",
            "PspMeRenderCorrectnessAfterCalc(previousSerial, true)",
        )
        call = sentinel.index(
            "PspMeRenderCorrectnessAfterCalc(previousSerial, true)"
        )
        direct_guard = sentinel.rfind(f"#if defined({FEATURE})", 0, call)
        self.assertNotEqual(direct_guard, -1)
        self.assertNotEqual(sentinel.find("#endif", call), -1)

        after_calc = function_body(
            self.bullets, "PspMeRenderCorrectnessAfterCalc(u32 serialBefore"
        )
        acquire = after_calc.index("th07_psp_me_render_stream_acquire(")
        self.assertLess(
            after_calc.index("!gPspMeRenderShadow.managerActive || !nextDraw ||"),
            acquire,
        )
        self.assertLess(after_calc.index("state.pending ||", 0, acquire), acquire)

        render = function_body(self.window, "GameWindow::Render()")
        assert_order(
            self,
            render,
            "Th07PspMeRenderCaptureCalcSerial()",
            "g_Chain.RunCalcChain()",
            "g_SoundPlayer.ProcessQueues()",
            "const bool toggledFixed30",
            "Th07PspMeRenderAfterCalc(meRenderCalcSerialBefore",
        )


if __name__ == "__main__":
    unittest.main()

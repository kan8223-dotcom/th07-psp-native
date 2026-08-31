from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "TH07_PSP_ME_RENDER_CORRECTNESS"
RETIRE_DIAG_FEATURE = "TH07_PSP_ME_RENDER_RETIRE_DIAG"


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


def make_target(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(r"\n(?=[A-Za-z0-9_.-]+:)", makefile[start + 1 :])
    return makefile[start:] if match is None else makefile[start : start + match.start() + 1]


class PspMeRenderWorkerI1Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        cls.audio_h = (ROOT / "psp" / "audio_me.h").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.bullets_h = (ROOT / "src" / "BulletManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.graphics = (
            ROOT / "psp" / "graphics" / "PspGuGraphics.cpp"
        ).read_text(encoding="utf-8")
        cls.main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")

    def test_dedicated_profile_is_worker_only_and_not_m0(self) -> None:
        self.assertIn("PSP_ME_RENDER_CORRECTNESS ?= 0", self.makefile)
        self.assertIn("$(error PSP_ME_RENDER_CORRECTNESS requires PSP_ME_RENDER_WORKER=1)", self.makefile)
        recipe = make_target(self.makefile, "psp3000-me-render-i1-build")
        for setting in (
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_1000=0",
            "PSP_MECC_AUDIO_4M=1",
            "PSP_BULLET_ROTATED_DIRECT=1",
            "PSP_BULLET_UNIFIED_QUADS=1",
            "PSP_BULLET_ONEPASS_ROTATED=1",
        ):
            self.assertIn(setting, recipe)
        self.assertIn("psp3000-me-render-i1-build \\", self.makefile)
        m0 = make_target(self.makefile, "psp3000-me-render-m0-build")
        self.assertIn("PSP_ME_RENDER_CORRECTNESS=0", m0)

    def test_retire_reason_profile_is_isolated_from_i1_and_m0(self) -> None:
        self.assertIn("PSP_ME_RENDER_RETIRE_DIAG ?= 0", self.makefile)
        self.assertIn(
            "$(error PSP_ME_RENDER_RETIRE_DIAG requires PSP_ME_RENDER_CORRECTNESS=1)",
            self.makefile,
        )
        self.assertIn(
            f"CXXFLAGS += -D{RETIRE_DIAG_FEATURE}", self.makefile
        )
        self.assertIn(f"CFLAGS += -D{RETIRE_DIAG_FEATURE}", self.makefile)

        diagnostic = make_target(
            self.makefile, "psp3000-me-render-i1-retire-diag-build"
        )
        for setting in (
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_RETIRE_DIAG=1",
            "PSP_AUDIO4M_BUILD_ID=0x2608300fu",
        ):
            self.assertIn(setting, diagnostic)

        i1 = make_target(self.makefile, "psp3000-me-render-i1-build")
        m0 = make_target(self.makefile, "psp3000-me-render-m0-build")
        self.assertIn("PSP_ME_RENDER_RETIRE_DIAG=0", i1)
        self.assertIn("PSP_ME_RENDER_RETIRE_DIAG=0", m0)
        self.assertIn(
            "$(PSP_ME_RENDER_CORRECTNESS)-$(PSP_ME_RENDER_RETIRE_DIAG)-",
            self.makefile,
        )

    def test_me11_owns_three_fixed_main_ram_slots(self) -> None:
        self.assertRegex(self.audio_h, r"STREAM_VERSION\s*=\s*0x4d453131u")
        self.assertRegex(self.audio_h, r"STREAM_SLOT_COUNT\s*=\s*3")
        self.assertRegex(self.audio_h, r"STREAM_RECORD_BYTES\s*=\s*64")
        self.assertIn("gMeRenderStreamInputAreas[TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT]", self.audio)
        self.assertIn("gMeRenderStreamOutputAreas[TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT]", self.audio)
        self.assertIn("gMeRenderStreamRunAreas[TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT]", self.audio)
        self.assertIn("I-ME1 input pool must remain 64 KiB per slot", self.audio)
        self.assertIn("I-ME1 output pool must remain 96 KiB per slot", self.audio)
        self.assertIn("I-ME1 run pool must remain 32 KiB per slot", self.audio)

    def test_me11_is_compile_isolated_from_the_accepted_m0_path(self) -> None:
        # The new mailbox, pools, kernel, API and selftest are all nested under
        # the correctness define.  M0 therefore retains its already-proven BSS
        # and worker protocol.
        self.assertGreaterEqual(self.audio.count(f"#if defined({FEATURE})"), 10)
        self.assertIn(f"#if defined({FEATURE})\n    initialize_render_stream_slots();", self.audio)
        self.assertIn(
            f"#if defined({FEATURE})\n    int renderStreamSelftestPassed;",
            self.audio,
        )
        self.assertIn(
            "renderStreamSelftestPassed = selftest_render_stream();",
            self.audio,
        )
        self.assertIn(
            f"#if defined({FEATURE})\n        || !me_render_stream_drain_for_shutdown()",
            self.audio,
        )

    def test_low_level_stream_is_shadow_only_and_cannot_reach_ge(self) -> None:
        mark = function_body(self.audio, "th07_psp_me_render_stream_mark_ge_in_flight(")
        release = function_body(self.audio, "th07_psp_me_render_stream_release_after_ge(")
        self.assertRegex(mark, r"return\s+0\s*;")
        self.assertRegex(release, r"return\s+0\s*;")
        self.assertNotIn("sceGu", mark + release)
        self.assertNotIn("th07_psp_me_render_stream_mark_ge_in_flight(", self.bullets)
        self.assertIn("SC DRAW=1 GE CONSUME=0", self.main)

    def test_exact_kernel_owns_mixed_pair_quad_and_effective_state(self) -> None:
        kernel = function_body(self.audio, "me_render_stream_expand_kernel(")
        self.assertIn("me_render_stream_floor(rawLeft + offsetX + 0.5f)", kernel)
        self.assertIn("localX * cosine - localY * sine + posX + offsetX", kernel)
        # Bullet streams retain the callback-wide mixed primitive latch.  An
        # Effect stream instead selects SPRITES/QUADS per record so a rotated
        # Effect cannot silently change the primitive of later axis-aligned
        # Effects in the same atomic segment.
        self.assertRegex(
            kernel,
            r"(?s)if\s*\(\s*!usePairs.*?&&\s*!effectList.*?\)\s*"
            r"generalMode\s*=\s*1\s*;",
        )
        self.assertRegex(
            kernel,
            r"(?s)effectList\s*\?\s*\(usePairs\s*\?\s*"
            r"TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES\s*:\s*"
            r"TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS\s*\)",
        )
        self.assertIn(
            "primitive == TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS",
            kernel,
        )
        self.assertIn("run->sourceFileIndex != sourceFileIndex", kernel)
        self.assertIn("run->renderStateFlags != renderStateFlags", kernel)
        self.assertIn("run->primitive != primitive", kernel)
        self.assertNotRegex(kernel, r"(?:sin|cos)f\s*\(")
        self.assertNotIn("VFPU", kernel)
        self.assertNotIn("eDRAM", kernel)

    def test_boot_selftest_is_independent_and_fail_closed(self) -> None:
        selftest = function_body(self.audio, "selftest_render_stream(void)")
        for evidence in (
            "expectedVertices[14]",
            "expectedRuns[3]",
            "halfWidthBits = float_bits(-4.0f)",
            "TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED",
            "TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK",
            "TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT",
            "ME11 SELFTEST PASS EDRAM0 SHADOW",
        ):
            self.assertIn(evidence, selftest)
        init = function_body(self.audio, "th07_psp_me_audio_init(void)")
        self.assertIn("if (!renderStreamSelftestPassed)", init)
        self.assertIn(
            "renderStreamSelftestPassed = selftest_render_stream();", init
        )
        self.assertIn("ME11 SELFTEST NG -> COLD REBOOT", init)
        self.assertIn("return -1", init)

    def test_snapshot_is_pointer_free_and_commits_only_render_cache(self) -> None:
        build = function_body(self.bullets, "PspMeRenderBuildCorrectnessSnapshot(")
        self.assertIn("Th07PspMeRenderStreamRecord &record", build)
        self.assertNotIn("record.bullet", build)
        self.assertNotIn("record.vm", build)
        for field in (
            "pspRenderSin = sine",
            "pspRenderCos = cosine",
            "pspRenderSourceAngle = bullet->angle",
            "pspRenderAngle = rotation",
            "pspRenderRotationValid = 1",
        ):
            self.assertIn(field, build)
        self.assertNotIn("vm->pos", build)
        self.assertNotIn("vm->SetRotationZ", build)

    def test_visible_draw_remains_sc_and_me_bytes_are_compared_in_place(self) -> None:
        draw = function_body(self.bullets, "BulletManager::OnDraw(BulletManager *arg)")
        self.assertIn("PspMeRenderCorrectnessBeginCapture();", draw)
        self.assertIn("bullet->Draw();", draw)
        self.assertIn("PspMeRenderCorrectnessNoteRecord(", draw)
        self.assertIn("PspMeRenderCorrectnessEndCapture();", draw)
        compare = function_body(self.bullets, "PspMeRenderCorrectnessEndCapture()")
        self.assertIn("state.canonicalStart", compare)
        self.assertIn("th07_psp_me_render_stream_compare(", compare)
        self.assertNotIn("state.ready.vertices", draw + compare)
        self.assertNotIn("sceGu", draw + compare)

    def test_generation_and_raw_draw_globals_are_authority_gates(self) -> None:
        self.assertGreaterEqual(
            self.bullets_h.count("++pspMeRenderSlotGenerations[index]"), 2
        )
        globals_gate = function_body(
            self.bullets, "PspMeRenderCorrectnessGlobalsMatch()"
        )
        for field in (
            "offsetXBits",
            "offsetYBits",
            "viewportLeftBits",
            "viewportTopBits",
            "viewportRightBits",
            "viewportBottomBits",
            "globalColor",
            "configFlags",
            "arcadeLeftBits",
            "arcadeTopBits",
            "viewportMinZBits",
            "viewportMaxZBits",
        ):
            self.assertIn(field, globals_gate)
        retire = function_body(
            self.bullets,
            "PspMeRenderCorrectnessRetire(bool atDeadline, u32 expectedDrawSeq)",
        )
        self.assertIn("const bool globalsValid = PspMeRenderCorrectnessGlobalsMatch()", retire)
        self.assertNotIn("globalSignature == PspMeRenderGlobalSignature()", retire)

    def test_faults_survive_perf_window_reset_and_all_windows_are_logged(self) -> None:
        take = function_body(self.bullets, "Th07PspTakeMeRenderShadowWindow(")
        self.assertIn("const Th07PspMeRenderShadowWindow sticky", take)
        for field in (
            "streamMismatch",
            "streamVertexMismatch",
            "streamRunMismatch",
            "streamHeaderDrop",
            "streamIdentityDrop",
            "streamReleaseFault",
            "streamFirstMismatchWord",
        ):
            self.assertIn(f"sticky.{field}", take)
        self.assertIn("PERF MERW I1 OBS", self.graphics)
        self.assertIn("streamMixedPrimitiveFrames != 0u", self.graphics)
        self.assertIn("MERW I1 FIRST MISMATCH", self.bullets)

        observation_start = self.graphics.index(
            "const bool merwObservationActivity"
        )
        # I-ME2 adds nested GE_CONSUME #else branches inside the correctness
        # profile.  Stop at the outer correctness/M0 branch, not the first
        # nested conditional.
        observation = self.graphics[
            observation_start : self.graphics.index(
                "#else\n        if (denseTarget)", observation_start
            )
        ]
        for field in (
            "sampleOverflow",
            "notReady",
            "lateRetired",
            "signatureDrop",
            "fcrDrop",
            "epochDrop",
            "stageEpochDrop",
            "managerEpochDrop",
            "replayEpochDrop",
            "generationDrop",
            "boundsDrop",
            "busy",
            "timeouts",
            "quarantined",
            "coverageDrop",
            "beginFail",
            "deadlineFault",
            "protocolFault",
            "streamMismatch",
            "streamSizeMismatch",
            "streamVertexMismatch",
            "streamRunMismatch",
            "streamHashMismatch",
            "streamHeaderDrop",
            "streamIdentityDrop",
            "streamReleaseFault",
            "scCopyUs",
        ):
            self.assertIn(f"merw.{field} != 0", observation)
            self.assertIn(f"merw.{field} == 0", observation)


if __name__ == "__main__":
    unittest.main()

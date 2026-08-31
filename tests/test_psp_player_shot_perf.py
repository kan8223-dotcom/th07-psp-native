from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
PLAYER = (ROOT / "src" / "Player.cpp").read_text(encoding="utf-8")
GRAPHICS = (ROOT / "psp" / "graphics" / "PspGuGraphics.cpp").read_text(
    encoding="utf-8"
)
GRAPHICS_HEADER = (
    ROOT / "psp" / "graphics" / "PspGuGraphics.hpp"
).read_text(encoding="utf-8")


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


def target_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    tail = makefile[start + len(target) + 1 :]
    match = re.search(r"^[A-Za-z0-9_.-]+:", tail, re.MULTILINE)
    return (
        makefile[start:]
        if match is None
        else makefile[start : start + len(target) + 1 + match.start()]
    )


class PspPlayerShotPerfTests(unittest.TestCase):
    def test_profile_is_default_off_guarded_and_stamped(self) -> None:
        self.assertIn("PSP_PERF_PLAYER_SHOT ?= 0", MAKEFILE)
        self.assertIn("-DTH07_PSP_PERF_PLAYER_SHOT", MAKEFILE)
        self.assertIn("PSP_PERF_PLAYER_SHOT requires PSP_PERF_DIAG=1", MAKEFILE)
        self.assertIn(
            "PSP_PERF_PLAYER_SHOT requires PSP_PERF_PROFILE=PERF_ACCEPT",
            MAKEFILE,
        )
        self.assertIn("PSP_PERF_PLAYER_SHOT is PSP-2000+-only", MAKEFILE)
        stamp = next(
            line
            for line in MAKEFILE.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_PERF_PLAYER_SHOT)", stamp)

    def test_dedicated_profile_is_rid30_with_c1_and_c2_off(self) -> None:
        recipe = target_body(MAKEFILE, "psp3000-player-shot-perf-build")
        for setting in (
            "PSP_1000=0",
            "PSP_PERF_DIAG=1",
            "PSP_PERF_PROFILE=PERF_ACCEPT",
            "PSP_PERF_PLAYER_SHOT=1",
            "PSP_PERF_DENSE_SLICE=1",
            "PSP_ME_ITEM_MOTION_UPDATE=1",
            "PSP_ME_RENDER_UV16=0",
            "PSP_ME_RENDER_XYZ16=0",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0",
            "PSP_ME_BULLET_OUTPUT_SLIM=0",
            "PSP_ME_BULLET_SEED_SLIM=0",
            "PSP_ME_ITEM_SEED_SLIM=0",
        ):
            with self.subTest(setting=setting):
                self.assertIn(setting, recipe)
        self.assertEqual(MAKEFILE.count("PSP_PERF_PLAYER_SHOT=1"), 1)

    def test_comparison_profiles_explicitly_block_parent_feature_leak(self) -> None:
        rid30 = target_body(MAKEFILE, "psp3000-a1-item-motion-build")
        self.assertIn("PSP_PERF_PLAYER_SHOT=0", rid30)

        for alias in (
            "psp3000-c1-uv16-m0-build",
            "psp3000-c1-xyz16-m0-build",
            "psp3000-c1-uvxyz16-m0-build",
        ):
            with self.subTest(alias=alias):
                self.assertIn(
                    f"{alias}: override PSP_PERF_PLAYER_SHOT=0", MAKEFILE
                )
        c1_generic = target_body(MAKEFILE, "psp3000-c1-vertex16-m0-build")
        self.assertIn("PSP_PERF_PLAYER_SHOT=0", c1_generic)

        for alias in (
            "c2a_output_slim",
            "c2b_bullet_seed_slim",
            "c2c_item_seed_slim",
            "c2abc_all_slim",
        ):
            with self.subTest(alias=alias):
                self.assertIn(
                    "PSP_PERF_PLAYER_SHOT=0", target_body(MAKEFILE, alias)
                )
        c2_generic = target_body(MAKEFILE, "psp3000-c2-slim-build")
        self.assertIn("PSP_PERF_PLAYER_SHOT=0", c2_generic)

    def test_draw_bullets_has_two_stamps_and_one_window_publish(self) -> None:
        draw = function_body(PLAYER, "void Player::DrawBullets()")
        self.assertEqual(draw.count("sceKernelGetSystemTimeWide()"), 2)
        self.assertEqual(
            draw.count("Th07PspPerfAddPlayerShotFrontendTime("), 1
        )
        active_guard = draw.index("if (bullet->bulletState != 1)")
        active_count = draw.index("++perfPlayerShotCount")
        anm_draw = draw.index("g_AnmManager->Draw(&bullet->vm)")
        self.assertLess(active_guard, active_count)
        self.assertLess(active_count, anm_draw)
        for forbidden in (
            "FlushDeferred",
            "sceGuFinish",
            "sceGuSync",
            "forceFlush",
            "ForceFlush",
        ):
            self.assertNotIn(forbidden, draw)

    def test_player_shot_window_is_an_observational_p06_subset(self) -> None:
        self.assertIn("strict subset", GRAPHICS)
        self.assertIn("P06", GRAPHICS)
        self.assertIn(
            "void Th07PspPerfAddPlayerShotFrontendTime", GRAPHICS_HEADER
        )
        publish = function_body(
            GRAPHICS, "void Th07PspPerfAddPlayerShotFrontendTime"
        )
        for counter in (
            "gPerfPlayerShotFrontendUs += elapsedUs",
            "gPerfPlayerShotActiveCount += activeShotCount",
            "++gPerfPlayerShotFrontendCalls",
        ):
            self.assertIn(counter, publish)
        for forbidden in (
            "Th07PspPerfAddDrawTime",
            "gPerfDrawJobUs",
            "gPerfDrawChainUs",
            "FlushDeferred",
            "sceGuFinish",
        ):
            self.assertNotIn(forbidden, publish)

    def test_accept_line_carries_m_and_player_shot_raw_window_totals(self) -> None:
        report = function_body(GRAPHICS, "void ReportPerfWindow")
        self.assertIn(
            "mMatrixSubmissions * 10u / mPerfFrames", report
        )
        self.assertIn('"AVGUS%u MAXUS%u P99US%u "', report)
        self.assertIn('"M%u.%u PSD%llu PSN%llu PSF%u "', report)
        observer = report[report.index("// This entire observer extension") :]
        observer = observer[: observer.index("#endif") + len("#endif")]
        self.assertIn("#if defined(TH07_PSP_PERF_PLAYER_SHOT)", observer)
        self.assertIn("mMatrixSubmissions * 10u / mPerfFrames", observer)
        self.assertRegex(
            report,
            r"#if defined\(TH07_PSP_PERF_PLAYER_SHOT\)\s+"
            r"char acceptMessage\[320\];\s+#else\s+"
            r"char acceptMessage\[256\];\s+#endif",
        )
        self.assertEqual(report.count('"PERF ACCEPT '), 1)
        self.assertEqual(report.count("th07_psp_perf_note(acceptMessage)"), 1)
        for counter in (
            "gPerfPlayerShotFrontendUs",
            "gPerfPlayerShotActiveCount",
            "gPerfPlayerShotFrontendCalls",
        ):
            self.assertIn(counter, report)

    def test_window_reset_clears_all_player_shot_counters_and_matrix_count(self) -> None:
        reset = function_body(GRAPHICS, "void ResetPerfWindowCounters()")
        for reset_line in (
            "mMatrixSubmissions = 0",
            "gPerfPlayerShotFrontendUs = 0",
            "gPerfPlayerShotActiveCount = 0",
            "gPerfPlayerShotFrontendCalls = 0",
        ):
            self.assertIn(reset_line, reset)


if __name__ == "__main__":
    unittest.main()

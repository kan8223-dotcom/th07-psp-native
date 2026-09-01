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


def assignments(body: str) -> dict[str, str]:
    return dict(
        re.findall(
            r"\b(PSP_[A-Z0-9_]+)=('[^']*'|[^\s\\]+)",
            body,
        )
    )


class PspPlayerShotPerfIndependentAudit(unittest.TestCase):
    def test_profile_is_opt_in_and_frozen_out_of_comparison_builds(self) -> None:
        self.assertIn("PSP_PERF_PLAYER_SHOT ?= 0", MAKEFILE)
        self.assertIn("CXXFLAGS += -DTH07_PSP_PERF_PLAYER_SHOT", MAKEFILE)
        stamp = next(
            line
            for line in MAKEFILE.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_PERF_PLAYER_SHOT)", stamp)
        for diagnostic in (
            "PSP_PERF_PLAYER_SHOT is PSP-2000+-only",
            "PSP_PERF_PLAYER_SHOT requires PSP_PERF_DIAG=1",
            "PSP_PERF_PLAYER_SHOT requires PSP_PERF_PROFILE=PERF_ACCEPT",
            "PSP_PERF_PLAYER_SHOT must be 0 or 1",
        ):
            self.assertIn(diagnostic, MAKEFILE)

        # These recipes are immutable comparison points.  Relying only on the
        # top-level default would let an inherited command-line assignment
        # silently turn the observer on in both sides of an A/B build.
        for target in (
            "psp3000-a1-item-motion-build",
            "psp3000-c1-vertex16-m0-build",
            "psp3000-c2-slim-build",
        ):
            with self.subTest(target=target):
                self.assertIn(
                    "PSP_PERF_PLAYER_SHOT=0", target_body(MAKEFILE, target)
                )

    def test_dedicated_profile_is_exactly_rid30_plus_the_observer(self) -> None:
        rid30 = assignments(target_body(MAKEFILE, "psp3000-a1-item-motion-build"))
        observer = assignments(
            target_body(MAKEFILE, "psp3000-player-shot-perf-build")
        )
        identity = {"PSP_AUDIO4M_BUILD_ID", "PSP_EBOOT_TITLE"}
        self.assertEqual(observer["PSP_PERF_PLAYER_SHOT"], "1")
        self.assertEqual(
            {key: value for key, value in observer.items() if key not in identity},
            {
                **{key: value for key, value in rid30.items() if key not in identity},
                "PSP_PERF_PLAYER_SHOT": "1",
            },
        )
        for feature in (
            "PSP_ME_RENDER_UV16",
            "PSP_ME_RENDER_XYZ16",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT",
            "PSP_ME_BULLET_OUTPUT_SLIM",
            "PSP_ME_BULLET_SEED_SLIM",
            "PSP_ME_ITEM_SEED_SLIM",
        ):
            self.assertEqual(observer[feature], "0")
        self.assertEqual(observer["PSP_1000"], "0")
        self.assertEqual(observer["PSP_PERF_PROFILE"], "PERF_ACCEPT")

    def test_draw_bullets_has_only_two_stamps_and_counts_state_one(self) -> None:
        draw = function_body(PLAYER, "void Player::DrawBullets()")
        self.assertEqual(draw.count("sceKernelGetSystemTimeWide()"), 2)
        self.assertEqual(draw.count("++perfPlayerShotCount"), 1)
        self.assertEqual(
            draw.count("Th07PspPerfAddPlayerShotFrontendTime("), 1
        )
        start = draw.index("perfPlayerShotStartUs")
        loop = draw.index("for (i = 0; i < 96; i++, bullet++)")
        state_one = draw.index("if (bullet->bulletState != 1)")
        count = draw.index("++perfPlayerShotCount")
        draw_call = draw.index("g_AnmManager->Draw(&bullet->vm)")
        end = draw.index("perfPlayerShotEndUs")
        publish = draw.index("Th07PspPerfAddPlayerShotFrontendTime(")
        self.assertLess(start, loop)
        self.assertLess(state_one, count)
        self.assertLess(count, draw_call)
        self.assertLess(draw_call, end)
        self.assertLess(end, publish)
        for forbidden in (
            "FlushDeferred",
            "sceGuFinish",
            "sceGuSync",
            "forceFlush",
            "ForceFlush",
        ):
            self.assertNotIn(forbidden, draw)

    def test_player_window_is_a_strict_priority_six_subset(self) -> None:
        register = function_body(PLAYER, "ZunResult Player::RegisterChain(")
        high = function_body(PLAYER, "u32 Player::OnDrawHighPrio(")
        publish = function_body(
            GRAPHICS, "void Th07PspPerfAddPlayerShotFrontendTime("
        )
        self.assertIn("g_Chain.AddToDrawChain(mgr->drawChain1, 6)", register)
        self.assertIn("arg->DrawBullets();", high)
        self.assertIn("strict subset", GRAPHICS_HEADER + GRAPHICS)
        self.assertIn("P06", GRAPHICS_HEADER + GRAPHICS)
        self.assertEqual(
            publish.count("gPerfPlayerShotFrontendUs += elapsedUs"), 1
        )
        self.assertEqual(
            publish.count("gPerfPlayerShotActiveCount += activeShotCount"), 1
        )
        self.assertEqual(
            publish.count("++gPerfPlayerShotFrontendCalls"), 1
        )
        for forbidden in (
            "Th07PspPerfAddDrawTime",
            "Th07PspPerfAddCalcTime",
            "gPerfDrawJobUs",
            "gPerfDrawChainUs",
            "mPerfCpuUs",
            "FlushDeferred",
            "sceGuFinish",
        ):
            self.assertNotIn(forbidden, publish)

    def test_accept_line_reports_raw_pl_and_matrix_without_accounting_them(self) -> None:
        report = function_body(GRAPHICS, "void ReportPerfWindow(")
        accept = report.split("#elif defined(TH07_PSP_PERF_ACCEPT)", 1)[1]
        self.assertEqual(
            accept.count("mMatrixSubmissions * 10u / mPerfFrames"), 1
        )
        self.assertIn('"AVGUS%u MAXUS%u P99US%u "', accept)
        self.assertIn('"M%u.%u PSD%llu PSN%llu PSF%u "', accept)
        # One legacy player-shot line plus one compact RID30 A/B hardware-FPS
        # line.  The raw PL/M counters below must still occur only in the
        # legacy branch.
        self.assertEqual(accept.count('"PERF ACCEPT '), 2)
        self.assertEqual(accept.count('"PERF ACCEPT S%d ST%d N%u HWFPS'), 1)
        self.assertEqual(accept.count('"PERF ACCEPT S%d ST%d N%u AVG'), 1)
        self.assertEqual(accept.count("th07_psp_perf_note(acceptMessage)"), 1)
        for counter in (
            "gPerfPlayerShotFrontendUs",
            "gPerfPlayerShotActiveCount",
            "gPerfPlayerShotFrontendCalls",
        ):
            # Raw window totals occur exactly once as snprintf arguments.  In
            # particular they are not divided per frame or added to CPU/R.
            self.assertEqual(accept.count(counter), 1, counter)
            self.assertNotIn(f"{counter} /", accept)
        accounted = accept[accept.index("accountedCpuUs") :]
        accounted = accounted[: accounted.index(";")]
        self.assertNotIn("gPerfPlayerShot", accounted)
        self.assertNotIn("mMatrixSubmissions", accounted)

    def test_matrix_counter_stays_post_cache_and_resets_per_window(self) -> None:
        apply = function_body(GRAPHICS, "void ApplyMatrices(bool screenSpace)")
        first_increment = min(
            position
            for token in ("++mMatrixSubmissions", "mMatrixSubmissions += 2")
            if (position := apply.find(token)) >= 0
        )
        for early_return in (
            "if (screenSpace && !modeChanged)",
            "if (!screenSpace && !modeChanged && mMatrixDirtyMask == 0)",
        ):
            self.assertLess(apply.index(early_return), first_increment)
        self.assertNotIn("mMatrixSubmissions", function_body(PLAYER, "void Player::DrawBullets()"))

        reset = function_body(GRAPHICS, "void ResetPerfWindowCounters()")
        self.assertEqual(reset.count("mMatrixSubmissions = 0"), 1)
        for counter in (
            "gPerfPlayerShotFrontendUs = 0",
            "gPerfPlayerShotActiveCount = 0",
            "gPerfPlayerShotFrontendCalls = 0",
        ):
            self.assertEqual(reset.count(counter), 1)


if __name__ == "__main__":
    unittest.main()

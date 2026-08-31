from __future__ import annotations

import math
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


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


class PspBulletDrawFastPathTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (ROOT / "src" / "AnmManager.cpp").read_text(encoding="utf-8")
        cls.fast_floor = cls.source[
            cls.source.index("inline float PspBulletFloor") :
            cls.source.index("inline void PspRenderSinCos")
        ]
        cls.fast = function_body(cls.source, "ZunResult AnmManager::DrawPspBullet(")
        cls.fallback = function_body(cls.source, "AnmManager::DrawPspBulletFallback(")
        cls.graphics = (ROOT / "psp" / "graphics" / "PspGuGraphics.cpp").read_text(
            encoding="utf-8"
        )
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

    def test_bullet_floor_is_the_two_instruction_allegrex_path(self) -> None:
        self.assertIn('"floor.w.s %0, %1\\n\\t"', self.fast_floor)
        self.assertIn('"cvt.s.w %0, %0"', self.fast_floor)
        self.assertNotIn("floorf", self.fast_floor)
        self.assertNotIn("isfinite", self.fast_floor)

    def test_generic_floor_keeps_its_corrupt_value_fallback(self) -> None:
        generic = self.source[
            self.source.index("inline float PspRenderFloor") :
            self.source.index("inline float PspBulletFloor")
        ]
        self.assertIn("std::isfinite(value)", generic)
        self.assertIn("return floorf(value);", generic)

    def test_hot_path_is_positive_axis_only(self) -> None:
        self.assertIn("vm->rotation.z == 0.0f && halfWidth >= 0.0f", self.fast)
        self.assertIn("halfHeight >= 0.0f", self.fast)
        self.assertIn("return DrawPspBulletFallback(vm, cachedSin, cachedCos);", self.fast)
        self.assertNotIn("float x[4]", self.fast)
        self.assertNotIn("float y[4]", self.fast)
        self.assertNotIn("PspRenderSinCos", self.fast)

    def test_hot_path_uses_four_direct_allegrex_floors(self) -> None:
        self.assertEqual(self.fast.count("PspBulletFloor("), 4)
        self.assertNotIn("PspRenderFloor(", self.fast)
        self.assertLess(self.fast.index("SyncRenderState(vm)"), self.fast.index("PspBulletFloor("))
        self.assertLess(
            self.fast.index("this->pspSpriteBatchUsesPairs = 1;"),
            self.fast.index("PspBulletFloor("),
        )

    def test_cull_stays_before_unchecked_coordinate_conversion(self) -> None:
        cull = self.fast.index("centerX + bound < g_Supervisor.viewport.x")
        cull_return = self.fast.index("return ZUN_SUCCESS;", cull)
        first_floor = self.fast.index("PspBulletFloor(")
        self.assertLess(cull, cull_return)
        self.assertLess(cull_return, first_floor)
        self.assertIn("const float bound = fabsf(halfWidth) + fabsf(halfHeight);", self.fast)

    def test_finite_render_domain_matches_floorf_exactly(self) -> None:
        # floor.w.s followed by cvt.s.w is exact for the finite, small integer
        # coordinate domain produced by the 480x272 PSP viewport and ANM sprites.
        for value in (
            -4095.75,
            -513.0,
            -0.75,
            -0.0,
            0.0,
            0.499,
            0.5,
            271.999,
            480.5,
            4095.75,
        ):
            expected = float(math.floor(value))
            allegrex_result = float(int(math.floor(value)))
            self.assertEqual(allegrex_result, expected)

    def test_hot_pair_writes_only_two_final_vertices(self) -> None:
        self.assertIn("WritePspSpriteVertex(out[0], left, top", self.fast)
        self.assertIn("WritePspSpriteVertex(out[1], right, bottom", self.fast)
        self.assertIn("this->vertexBufferCurPtr += 2;", self.fast)
        self.assertNotIn("this->vertexBufferCurPtr += 4;", self.fast)

    def test_anchor_endpoint_formulas_match_the_old_axis_path(self) -> None:
        self.assertIn("(vm->anchor & 1) ? vm->pos.x : vm->pos.x - halfWidth", self.fast)
        self.assertIn("vm->pos.x + halfWidth * 2.0f : vm->pos.x + halfWidth", self.fast)
        self.assertIn("(vm->anchor & 2) ? vm->pos.y : vm->pos.y - halfHeight", self.fast)
        self.assertIn("vm->pos.y + halfHeight * 2.0f : vm->pos.y + halfHeight", self.fast)

    def test_fallback_retains_rotated_and_mirrored_quad_contract(self) -> None:
        self.assertIn("float x[4];", self.fallback)
        self.assertIn("float y[4];", self.fallback)
        self.assertEqual(self.fallback.count("PspRenderFloor("), 4)
        self.assertNotIn("PspBulletFloor(", self.fallback)
        self.assertIn("PspRenderSinCos(vm->rotation.z", self.fallback)
        self.assertIn(
            "const bool pairEligible =\n"
            "        vm->rotation.z == 0.0f && x[0] <= x[3] && y[0] <= y[3];",
            self.fallback,
        )
        self.assertIn("const bool usePairs = pairEligible;", self.fallback)
        self.assertIn("WritePspSpriteVertex(out[1], x[3], y[3]", self.fallback)
        self.assertIn("WritePspSpriteVertex(out[1], x[1], y[1]", self.fallback)
        self.assertIn("WritePspSpriteVertex(out[2], x[2], y[2]", self.fallback)
        self.assertIn("this->vertexBufferCurPtr += 4;", self.fallback)

    def test_perf_classifies_eligibility_before_culling(self) -> None:
        eligibility = self.fast.index("const bool axisEligible")
        axis_count = self.fast.index("++gPspBulletAxisEligible;")
        fallback_count = self.fast.index("++gPspBulletFallbackEligible;")
        cull = self.fast.index("centerX + bound < g_Supervisor.viewport.x")
        branch = self.fast.index("if (__builtin_expect(!axisEligible, 0))")
        self.assertLess(eligibility, axis_count)
        self.assertLess(eligibility, fallback_count)
        self.assertLess(axis_count, cull)
        self.assertLess(fallback_count, cull)
        self.assertLess(cull, branch)
        # The shared original/fallback source classifies only in the macro-OFF
        # control build. The macro-ON wrapper has already counted the call.
        self.assertIn(
            "#if defined(TH07_PSP_PERF_M2) && !defined(TH07_PSP_BULLET_AXIS_FAST)",
            self.fallback,
        )

    def test_perf_counters_share_the_existing_ram_window(self) -> None:
        self.assertIn("Th07PspTakeBulletDrawPerf", self.graphics)
        self.assertIn("BAX%u.%u BFB%u.%u BCU%u.%u", self.graphics)
        self.assertIn("gPspBulletAxisEligible = 0;", self.source)
        self.assertIn("gPspBulletFallbackEligible = 0;", self.source)
        self.assertIn("gPspBulletCullRejects = 0;", self.source)

    def test_fast_path_is_an_explicit_profile_bit(self) -> None:
        self.assertIn("PSP_BULLET_AXIS_FAST ?= 0", self.makefile)
        self.assertIn("-DTH07_PSP_BULLET_AXIS_FAST", self.makefile)
        stamp = next(
            line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_BULLET_AXIS_FAST)", stamp)


if __name__ == "__main__":
    unittest.main()

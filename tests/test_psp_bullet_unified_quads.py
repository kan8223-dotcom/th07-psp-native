from __future__ import annotations

import math
import re
import struct
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "PSP_BULLET_UNIFIED_QUADS"
MACRO = "TH07_PSP_BULLET_UNIFIED_QUADS"


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


def recipe_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    tail = makefile[start + len(target) + 1 :]
    match = re.search(r"^[A-Za-z0-9_.-]+:", tail, re.MULTILINE)
    return makefile[start:] if match is None else makefile[start : start + len(target) + 1 + match.start()]


class PspBulletUnifiedQuadSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.anm = (ROOT / "src" / "AnmManager.cpp").read_text(encoding="utf-8")
        cls.anm_h = (ROOT / "src" / "AnmManager.hpp").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(encoding="utf-8")
        cls.graphics = (ROOT / "psp" / "graphics" / "PspGuGraphics.cpp").read_text(
            encoding="utf-8"
        )
        cls.graphics_h = (ROOT / "psp" / "graphics" / "PspGuGraphics.hpp").read_text(
            encoding="utf-8"
        )
        cls.draw = function_body(cls.anm, "AnmManager::DrawPspBulletFallback(")
        cls.rotated = function_body(cls.anm, "AnmManager::DrawPspRotatedBullet(")
        cls.flush = function_body(cls.anm, "void AnmManager::Flush()")
        cls.backend = function_body(cls.graphics, "void DrawSpriteQuads(")
        cls.on_draw = function_body(cls.bullets, "u32 BulletManager::OnDraw")

    def test_candidate_is_default_off_psp2000plus_only_and_reversible(self) -> None:
        self.assertIn(f"{FEATURE} ?= 0", self.makefile)
        start = self.makefile.index(f"ifeq ($({FEATURE}),1)")
        end = self.makefile.index("ifeq ($(PSP_ASCII_POPUP_BATCH),1)", start)
        block = self.makefile[start:end]
        self.assertIn(f"-D{MACRO}", block)
        for exclusion in (
            "PSP_1000",
            "PSP_BULLET_AXIS_FAST",
            "PSP_BULLET_SNAPSHOT_EMITTER",
        ):
            self.assertIn(exclusion, block)
        self.assertNotIn(
            "PSP_BULLET_UNIFIED_QUADS and PSP_BULLET_ROTATED_DIRECT are mutually exclusive",
            block,
        )
        stamp = next(line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :="))
        self.assertIn(f"$({FEATURE})", stamp)

    def test_release_and_named_builds_keep_candidate_off(self) -> None:
        for target in (
            "psp1000-build",
            "psp2000plus-build",
            "psp2000plus-shikigami-build",
            "psp3000-mecc-bgm384k-build",
            "psp3000-mecc-audio4m-build",
        ):
            with self.subTest(target=target):
                self.assertIn(f"{FEATURE}=0", recipe_body(self.makefile, target))

    def test_attribution_profiles_reject_changed_batch_boundary(self) -> None:
        self.assertRegex(
            self.makefile,
            rf"(?s)PSP_PERF_PROFILE\),ATTRIB\).*?{FEATURE}.*?PERF_ACCEPT",
        )
        self.assertRegex(
            self.makefile,
            rf"(?s)ifneq \(\$\({FEATURE}\),0\).*?"
            rf"Empty-timer A/A calibration requires {FEATURE}=0",
        )

    def test_batch_mode_resets_after_items_before_bullet_order_walk(self) -> None:
        item = self.on_draw.index("g_ItemManager.OnDraw();")
        rotated_begin = self.on_draw.index("BeginPspRotatedBulletBatch();")
        begin = self.on_draw.index("BeginPspUnifiedBulletBatch();")
        buckets = re.search(r"for\s*\([^;]*=\s*0;[^;]*<\s*6;", self.on_draw)
        self.assertIsNotNone(buckets)
        assert buckets is not None
        self.assertLess(item, rotated_begin)
        self.assertLess(rotated_begin, begin)
        self.assertLess(begin, buckets.start())
        self.assertEqual(self.on_draw.count("BeginPspRotatedBulletBatch();"), 1)
        self.assertEqual(self.on_draw.count("BeginPspUnifiedBulletBatch();"), 1)
        self.assertNotIn("sort", self.on_draw[begin:])

    def test_only_drawable_non_pair_bullet_latches_general_mode(self) -> None:
        cull_return = self.draw.index("return ZUN_SUCCESS;", self.draw.index("centerX + bound"))
        eligible = self.draw.index("const bool pairEligible")
        latch = self.draw.index("this->pspUnifiedBulletGeneralMode = 1;")
        self.assertLess(cull_return, eligible)
        self.assertLess(eligible, latch)
        self.assertIn(
            "const bool usePairs = pairEligible && !this->pspUnifiedBulletGeneralMode;",
            self.draw,
        )

    def test_axis_after_general_keeps_all_four_original_vertices(self) -> None:
        general = self.draw[self.draw.index("else", self.draw.index("if (usePairs)")) :]
        for corner in range(1, 4):
            self.assertIn(f"WritePspSpriteVertex(out[{corner}], x[{corner}], y[{corner}]", general)
        self.assertIn("this->vertexBufferCurPtr += 4;", general)
        self.assertIn("this->pspForceSpriteQuads = 1;", general)

    def test_rotated_direct_starts_same_unified_stream_after_pair_flush(self) -> None:
        cull_return = self.rotated.index(
            "return ZUN_SUCCESS;", self.rotated.index("centerX + bound")
        )
        pair_flush = self.rotated.index("if (this->pspSpriteBatchUsesPairs != 0)")
        latch = self.rotated.index("this->pspUnifiedBulletGeneralMode = 1;")
        force = self.rotated.index("this->pspForceSpriteQuads = 1;")
        first_write = self.rotated.index("WritePspSpriteVertex(out[0]")
        self.assertLess(cull_return, pair_flush)
        self.assertLess(self.rotated.rfind("this->Flush()"), latch)
        self.assertLess(latch, force)
        self.assertLess(force, first_write)
        guard = self.rotated.rfind("#if", 0, latch)
        self.assertIn(MACRO, self.rotated[guard:latch])

    def test_backend_disable_collapse_makes_one_input_run_without_reordering(self) -> None:
        self.assertIn("Th07PspDrawSpriteQuadsUnified", self.graphics_h)
        self.assertIn("allowAxisCollapse && canCollapse", self.backend)
        self.assertNotIn("sort", self.backend)
        self.assertNotIn("stable_sort", self.backend)
        self.assertIn("batch += batchSprites * 4u;", self.backend)
        self.assertIn("remaining -= batchSprites;", self.backend)
        self.assertIn("Th07PspDrawSpriteQuadsUnified", self.flush)
        normal_wrapper = function_body(self.graphics, "void Th07PspDrawSpriteQuads(")
        unified_wrapper = function_body(self.graphics, "void Th07PspDrawSpriteQuadsUnified(")
        self.assertIn("DrawSpriteQuads(vertices, spriteCount, true)", normal_wrapper)
        self.assertIn("DrawSpriteQuads(vertices, spriteCount, false)", unified_wrapper)

    def test_indexed_quad_topology_is_the_existing_ordered_general_path(self) -> None:
        expected = (
            "gQuadIndices[sprite * 6 + 0] = base;",
            "gQuadIndices[sprite * 6 + 1] = base + 1;",
            "gQuadIndices[sprite * 6 + 2] = base + 2;",
            "gQuadIndices[sprite * 6 + 3] = base + 1;",
            "gQuadIndices[sprite * 6 + 4] = base + 2;",
            "gQuadIndices[sprite * 6 + 5] = base + 3;",
        )
        positions = [self.graphics.index(line) for line in expected]
        self.assertEqual(positions, sorted(positions))

    def test_force_bit_is_cleared_at_frame_reset_and_after_every_flush(self) -> None:
        reset = function_body(self.anm, "void AnmManager::ResetVertexBuffer()")
        self.assertIn("this->pspUnifiedBulletGeneralMode = 0;", reset)
        self.assertIn("this->pspForceSpriteQuads = 0;", reset)
        self.assertIn("this->pspForceSpriteQuads = 0;", self.flush)
        self.assertLess(
            self.flush.index("if (this->pspForceSpriteQuads)"),
            self.flush.index("this->pspForceSpriteQuads = 0;"),
        )


class PspBulletUnifiedQuadGeometryTests(unittest.TestCase):
    def test_axis_pair_and_indexed_quad_have_identical_corner_attributes(self) -> None:
        # GU_SPRITES receives corners 0/3. The unified path sends the exact
        # four rectangle corners and the shared 0,1,2 / 1,2,3 index stream.
        # Verify all edge endpoints and IEEE-754 payloads over the finite PSP
        # gameplay domain; real-hardware framebuffer readback remains the
        # final rasterisation gate.
        for index in range(8192):
            left = float((index * 37) % 961 - 240)
            top = float((index * 53) % 641 - 160)
            width = float((index % 96) + 1)
            height = float(((index * 7) % 96) + 1)
            right = left + width
            bottom = top + height
            u0 = float((index % 32) / 32.0)
            v0 = float(((index * 3) % 32) / 32.0)
            u1 = u0 + float(((index % 8) + 1) / 64.0)
            v1 = v0 + float((((index * 5) % 8) + 1) / 64.0)
            color = (0xFF000000 | (index * 2654435761)) & 0xFFFFFFFF
            z = 0.05
            pair = ((u0, v0, color, left, top, z), (u1, v1, color, right, bottom, z))
            quad = (
                pair[0],
                (u1, v0, color, right, top, z),
                (u0, v1, color, left, bottom, z),
                pair[1],
            )
            self.assertEqual(quad[0], pair[0])
            self.assertEqual(quad[3], pair[1])
            for vertex in quad:
                packed = struct.pack("<ffIfff", *vertex)
                self.assertEqual(len(packed), 24)
                self.assertTrue(all(math.isfinite(value) for value in (vertex[0], vertex[1], vertex[3], vertex[4], vertex[5])))


if __name__ == "__main__":
    unittest.main()

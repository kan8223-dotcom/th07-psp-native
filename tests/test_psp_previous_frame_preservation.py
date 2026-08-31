from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text(encoding="utf-8")
ANM_SOURCE = (ROOT / "src/AnmManager.cpp").read_text(encoding="utf-8")


def function_body(signature: str) -> str:
    start = SOURCE.index(signature)
    opening = SOURCE.index("{", start)
    depth = 0
    for index in range(opening, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def anm_function_body(signature: str) -> str:
    start = ANM_SOURCE.index(signature)
    opening = ANM_SOURCE.index("{", start)
    depth = 0
    for index in range(opening, len(ANM_SOURCE)):
        if ANM_SOURCE[index] == "{":
            depth += 1
        elif ANM_SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return ANM_SOURCE[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class PspPreviousFramePreservationTests(unittest.TestCase):
    def test_swap_seeds_the_new_draw_buffer_before_other_frame_setup(self) -> None:
        swap = function_body("void SwapBuffers() override")
        swapped = swap.index("mCurrentDrawBuffer ^= 1;")
        started = swap.index("StartList();", swapped)
        preserved = swap.index("PreserveLatestPlayfield();", started)
        pillarboxes = swap.index("ClearPillarboxes();", preserved)
        self.assertLess(swapped, started)
        self.assertLess(started, preserved)
        self.assertLess(preserved, pillarboxes)

    def test_only_the_logical_playfield_is_copied_by_the_ge(self) -> None:
        preserve = function_body("void PreserveLatestPlayfield()")
        for boundary in (
            "32 * contentWidth / kLogicalWidth",
            "416 * contentWidth / kLogicalWidth",
            "16 * kScreenHeight / kLogicalHeight",
            "464 * kScreenHeight + kLogicalHeight - 1",
        ):
            self.assertIn(boundary, preserve)
        self.assertIn("sceGuCopyImage(GU_PSM_5650", preserve)
        self.assertIn("sceGuTexSync();", preserve)
        self.assertNotIn("memcpy", preserve)

    def test_copy_direction_tracks_the_post_swap_buffer_index(self) -> None:
        preserve = function_body("void PreserveLatestPlayfield()")
        self.assertIn(
            "mCurrentDrawBuffer ? 0u : kFrameBytes",
            preserve,
        )
        self.assertIn(
            "mCurrentDrawBuffer ? kFrameBytes : 0u",
            preserve,
        )
        copy = preserve.index("sceGuCopyImage")
        self.assertLess(preserve.index("displayBuffer"), copy)
        self.assertLess(preserve.index("drawBuffer"), copy)

    def test_draw3_preserves_vm_alpha_on_the_psp_fallback_path(self) -> None:
        self.assertIn("g_Supervisor.cfg.noVertexBuffers = true;", SOURCE)
        draw3 = anm_function_body("ZunResult AnmManager::Draw3(AnmVm *vm)")
        self.assertIn(
            "g_Supervisor.cfg.noVertexBuffers\n"
            "                                                  ? TEX_ARG_DIFFUSE\n"
            "                                                  : TEX_ARG_TFACTOR",
            draw3,
        )
        self.assertIn("DrawPrimitiveUP(PRIM_TRIANGLE_STRIP", draw3)


if __name__ == "__main__":
    unittest.main()

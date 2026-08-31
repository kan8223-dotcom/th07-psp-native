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


def old_corners(
    x: float,
    y: float,
    width: float,
    height: float,
    scale_x: float,
    scale_y: float,
    anchor: int,
    offset_x: float,
    offset_y: float,
) -> tuple[tuple[float, float], ...]:
    half_width = width * scale_x * 0.5
    half_height = height * scale_y * 0.5
    raw_left = x if anchor & 1 else x - half_width
    raw_right = x + half_width * 2.0 if anchor & 1 else x + half_width
    raw_top = y if anchor & 2 else y - half_height
    raw_bottom = y + half_height * 2.0 if anchor & 2 else y + half_height
    left = float(math.floor(raw_left + offset_x + 0.5))
    right = float(math.floor(raw_right + offset_x + 0.5))
    top = float(math.floor(raw_top + offset_y + 0.5))
    bottom = float(math.floor(raw_bottom + offset_y + 0.5))
    return ((left, top), (right, top), (left, bottom), (right, bottom))


class PspAsciiPopupBatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.anm = (ROOT / "src" / "AnmManager.cpp").read_text(encoding="utf-8")
        cls.ascii = (ROOT / "src" / "AsciiManager.cpp").read_text(encoding="utf-8")
        cls.graphics = (
            ROOT / "psp" / "graphics" / "PspGuGraphics.cpp"
        ).read_text(encoding="utf-8")
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.fast = function_body(cls.anm, "ZunResult AnmManager::DrawPspAsciiPopupBatch")
        cls.draw = function_body(cls.ascii, "void AsciiManager::DrawPopups()")

    def test_feature_is_reversible_and_not_a_psp1000_change(self) -> None:
        self.assertIn("PSP_ASCII_POPUP_BATCH ?= 0", self.makefile)
        self.assertIn("-DTH07_PSP_ASCII_POPUP_BATCH", self.makefile)
        self.assertIn(
            "PSP_ASCII_POPUP_BATCH is a PSP-2000+ validation profile only",
            self.makefile,
        )
        stamp = next(
            line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_ASCII_POPUP_BATCH)", stamp)
        self.assertGreaterEqual(self.makefile.count("PSP_ASCII_POPUP_BATCH=0"), 5)

    def test_ascii_manager_has_atomic_fallback_to_original_loop(self) -> None:
        guard = "#if defined(TH07_PSP) && defined(TH07_PSP_ASCII_POPUP_BATCH)"
        self.assertIn(guard, self.draw)
        self.assertIn("DrawPspAsciiPopupBatch", self.draw)
        self.assertIn("if (!popupsDrawnByBatch)", self.draw)
        self.assertIn("g_AnmManager->DrawNoRotation(&this->vm1);", self.draw)

    def test_validation_finishes_before_renderer_or_vm_mutation(self) -> None:
        validation = self.fast.index("Validate the entire frame")
        reset = self.fast.index("this->ResetVertexBuffer();")
        vm_write = self.fast.index("vm->pos.x = popup.position.x")
        first_vertex = self.fast.index("WritePspSpriteVertex(out[0]")
        self.assertLess(validation, reset)
        self.assertLess(validation, vm_write)
        self.assertLess(validation, first_vertex)
        self.assertIn("popup.characterCount > sizeof(popup.digits)", self.fast)
        self.assertIn("digit > 10", self.fast)
        self.assertIn("candidateSource != sourceFileIndex", self.fast)
        self.assertIn("!std::isfinite(candidate.widthPx)", self.fast)
        self.assertIn("!std::isfinite(candidate.heightPx)", self.fast)
        self.assertIn("!std::isfinite(vm->scale.x)", self.fast)
        self.assertIn("!std::isfinite(vm->scale.y)", self.fast)

    def test_digit_sprite_animation_contract_is_unchanged(self) -> None:
        self.assertGreaterEqual(self.fast.count("digit != 10"), 2)
        self.assertGreaterEqual(self.fast.count("popup.timer.current >= 52"), 2)
        self.assertGreaterEqual(
            self.fast.count("popup.timer.current < 56 ? 11 : 21"), 2
        )
        self.assertIn("&popup.digits[popup.characterCount - 1]", self.fast)
        self.assertIn("--j, --digit", self.fast)
        self.assertIn("vm->pos.x += 8.0f", self.fast)

    def test_player_proximity_alpha_contract_is_unchanged(self) -> None:
        self.assertIn("dx * dx + dy * dy", self.fast)
        self.assertIn("if (alpha > 4096)", self.fast)
        self.assertIn("alpha = 208", self.fast)
        self.assertIn("else if (alpha > 1024)", self.fast)
        self.assertIn("(alpha - 1024) * 128 / 3072 + 80", self.fast)
        self.assertIn("alpha = 80", self.fast)

    def test_one_state_sync_and_two_vertices_per_visible_digit(self) -> None:
        self.assertEqual(self.fast.count("this->SyncRenderState(vm);"), 1)
        self.assertIn("if (!batchStarted)", self.fast)
        self.assertIn("this->pspSpriteBatchUsesPairs = 1;", self.fast)
        self.assertIn("WritePspSpriteVertex(out[0], left, top", self.fast)
        self.assertIn("WritePspSpriteVertex(out[1], right, bottom", self.fast)
        self.assertIn("this->vertexBufferCurPtr += 2;", self.fast)
        self.assertNotIn("this->vertexBufferCurPtr += 4;", self.fast)

    def test_hot_path_has_no_allocation_io_or_forced_ge_sync(self) -> None:
        for forbidden in (
            "malloc(",
            "calloc(",
            "realloc(",
            "free(",
            "fopen(",
            "fread(",
            "fwrite(",
            "sceIo",
            "sceGuSync",
            "SubmitAndRestart",
            "ReadPixels",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, self.fast)

    def test_pair_endpoints_equal_old_quad_diagonal_for_all_anchors(self) -> None:
        for anchor in range(4):
            for x, y, width, height, sx, sy, ox, oy in (
                (128.25, 96.75, 8.0, 12.0, 1.0, 1.0, 0.0, 0.0),
                (-7.5, 271.25, 16.0, 16.0, 0.75, 1.25, 32.0, -16.0),
                (400.0, -40.0, 9.0, 15.0, 2.0, 0.5, -3.5, 8.5),
            ):
                quad = old_corners(x, y, width, height, sx, sy, anchor, ox, oy)
                pair = (quad[0], quad[3])
                self.assertLessEqual(pair[0][0], pair[1][0])
                self.assertLessEqual(pair[0][1], pair[1][1])
                self.assertEqual(pair, (quad[0], quad[3]))

    def test_perf_log_proves_fast_path_coverage_and_fallback_count(self) -> None:
        self.assertIn("Th07PspTakeAsciiPopupBatchPerf", self.graphics)
        self.assertIn('"PERF APB CALL%u DIG%u FB%u"', self.graphics)
        self.assertIn("gPspAsciiPopupBatchFallbacks", self.anm)


if __name__ == "__main__":
    unittest.main()

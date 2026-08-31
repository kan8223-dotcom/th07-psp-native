from __future__ import annotations

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
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class PspGuiTileBatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.anm = (ROOT / "src" / "AnmManager.cpp").read_text(encoding="utf-8")
        cls.gui = (ROOT / "src" / "Gui.cpp").read_text(encoding="utf-8")
        cls.grid = function_body(
            cls.anm, "ZunResult AnmManager::DrawPspNoRotationGrid("
        )
        cls.scene = function_body(cls.gui, "void Gui::DrawGameScene()")

    def test_feature_is_reversible_and_psp2000_plus_only(self) -> None:
        self.assertIn("PSP_GUI_TILE_BATCH ?= 0", self.makefile)
        self.assertIn("-DTH07_PSP_GUI_TILE_BATCH", self.makefile)
        self.assertIn("PSP_GUI_TILE_BATCH is PSP-2000+ only", self.makefile)
        stamp = next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_GUI_TILE_BATCH)", stamp)

    def test_three_grids_keep_canonical_iteration_order(self) -> None:
        self.assertEqual(self.scene.count("DrawPspNoRotationGrid("), 3)
        for call in (
            "0.0f, 1.0f, 1.0f, 0.0f, 464.0f, 32.0f, 0.49f",
            "416.0f, 624.0f, 32.0f, 16.0f, 464.0f, 32.0f, 0.49f",
            "0.0f, 624.0f, 128.0f, 0.0f, 480.0f, 464.0f, 0.49f",
        ):
            self.assertIn(call, self.scene)
        self.assertLess(
            self.grid.index("for (f32 x = xStart"),
            self.grid.index("for (f32 y = yStart"),
        )
        self.assertIn("vm->pos = ZunVec3(x, y, z);", self.grid)

    def test_vertex_math_and_primitive_match_draw_no_rotation(self) -> None:
        for token in (
            "vm->sprite->widthPx * vm->scale.x * 0.5f",
            "vm->sprite->heightPx * vm->scale.y * 0.5f",
            "PspRenderFloor(rawLeft + this->offset.x + 0.5f)",
            "PspRenderFloor(rawBottom + this->offset.y + 0.5f)",
            "vm->sprite->uvStart.x + vm->uvScrollPos.x",
            "vm->sprite->uvEnd.y + vm->uvScrollPos.y",
            "this->pspSpriteBatchUsesPairs",
            "this->vertexBufferCurPtr += 2",
            "this->vertexBufferCurPtr += 4",
        ):
            self.assertIn(token, self.grid)
        self.assertIn("WritePspSpriteVertex(out[0], left, top", self.grid)
        self.assertIn("WritePspSpriteVertex(out[3], right, bottom", self.grid)

    def test_renderer_state_is_synced_once_and_counter_is_preserved(self) -> None:
        self.assertEqual(self.grid.count("SyncRenderState(vm);"), 1)
        self.assertIn("if (!frontendReady)", self.grid)
        self.assertIn("visibleCopies - 1u", self.grid)
        self.assertIn("this->renderStateChangesThisFrame", self.grid)

    def test_invalid_vm_keeps_final_position_and_hot_path_has_no_io(self) -> None:
        invalid = self.grid[: self.grid.index("if (!this->vertexBufferCurPtr)")]
        self.assertIn("if (!drawable)", invalid)
        self.assertIn("vm->pos = ZunVec3(x, y, z);", invalid)
        for forbidden in (
            "malloc(",
            "calloc(",
            "free(",
            "fopen(",
            "fread(",
            "fwrite(",
            "sceIo",
            "sceGuSync",
            "SubmitAndRestart",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, self.grid)


if __name__ == "__main__":
    unittest.main()

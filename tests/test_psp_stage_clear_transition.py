from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GUI = (ROOT / "src" / "Gui.cpp").read_text(encoding="utf-8")
ANM = (ROOT / "src" / "AnmManager.cpp").read_text(encoding="utf-8")
ANM_HEADER = (ROOT / "src" / "AnmManager.hpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
GRAPHICS = (ROOT / "psp" / "graphics" / "PspGuGraphics.cpp").read_text(encoding="utf-8")


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


class PspStageClearTransitionTests(unittest.TestCase):
    def test_stage_results_starts_text_and_snapshot_before_immediate_capture(self) -> None:
        start = GUI.index("case MSG_STAGERESULTS:")
        stage_results = GUI[start : GUI.index("case MSG_FREEZE:", start)]

        text_vm = stage_results.index("&this->stageClearTextVm, 1566")
        snapshot_vm = stage_results.index("&this->stageTransitionSnapshotVm, 1829")
        request = stage_results.index("const i32 captureResult")
        capture = stage_results.index(
            "const bool captured = g_AnmManager->TakeScreenshotIfRequested()"
        )
        self.assertLess(text_vm, snapshot_vm)
        self.assertLess(snapshot_vm, request)
        self.assertLess(request, capture)
        self.assertNotIn("stage results PSP transition capture skipped", stage_results)

    def test_stage_results_disables_only_snapshot_when_capture_fails(self) -> None:
        start = GUI.index("case MSG_STAGERESULTS:")
        stage_results = GUI[start : GUI.index("case MSG_FREEZE:", start)]
        self.assertGreaterEqual(
            stage_results.count("this->stageTransitionSnapshotVm.activeSpriteIdx = -1;"),
            2,
        )
        self.assertNotIn("this->stageClearTextVm.activeSpriteIdx = -1;", stage_results)
        self.assertIn('th07_psp_boot_note("stage results snapshot unavailable")', stage_results)

    def test_inactive_snapshot_is_neither_updated_nor_drawn(self) -> None:
        update = function_body(GUI, "void Gui::UpdateGui()")
        draw = function_body(GUI, "void Gui::DrawStageElements()")
        self.assertIn(
            "if (this->impl->stageTransitionSnapshotVm.activeSpriteIdx >= 0 &&\n"
            "            g_AnmManager->ExecuteScript(&this->impl->stageTransitionSnapshotVm)",
            update,
        )
        snapshot_guard = "if (this->impl->stageTransitionSnapshotVm.activeSpriteIdx >= 0)"
        guard = draw.index(snapshot_guard)
        snapshot_draw = draw.index(
            "g_AnmManager->DrawNoRotation(&this->impl->stageTransitionSnapshotVm);",
            guard,
        )
        self.assertLess(guard, snapshot_draw)

    def test_next_stage_checkerboard_is_created_only_after_a_ready_capture(self) -> None:
        added = function_body(GUI, "ZunResult Gui::ActualAddedCallback()")
        ready = added.index("if (transitionCaptureReady)")
        row_loop = added.index("for (i = 0; i < 14; i++)", ready)
        column_loop = added.index("for (j = 0; j < 12; j++)", row_loop)
        enabled = added.index("this->impl->activeTransitionQuads = 168;", column_loop)

        self.assertLess(ready, row_loop)
        self.assertLess(row_loop, column_loop)
        self.assertLess(column_loop, enabled)
        self.assertIn("((i + j) & 1) + 1830", added)
        self.assertIn("transitionCaptureReady = g_AnmManager->TakeScreenshotIfRequested();", added)

    def test_screenshot_result_propagates_and_pending_request_is_always_cleared(self) -> None:
        self.assertIn("bool TakeScreenshot(", ANM_HEADER)
        self.assertIn("bool TakeScreenshotIfRequested();", ANM_HEADER)

        take = function_body(ANM, "bool AnmManager::TakeScreenshot(")
        requested = function_body(MAIN, "bool AnmManager::TakeScreenshotIfRequested()")
        self.assertIn("return captured;", take)
        self.assertIn("if (this->screenshotTextureId < 0)", requested)
        self.assertIn("this->screenshotTextureId = -1;", requested)
        self.assertLess(requested.index("TakeScreenshot("), requested.index("screenshotTextureId = -1"))
        self.assertLess(requested.index("screenshotTextureId = -1"), requested.index("return captured;"))

    def test_full_stage_capture_uses_proven_read_before_overwrite_guard(self) -> None:
        capture = function_body(GRAPHICS, "bool CaptureFramebufferToTexture(")
        self.assertNotIn("if (stagingTop < textureBottom)", capture)
        self.assertIn("const int overlapTop = std::max(textureTop, stagingTop);", capture)
        self.assertIn("const int sampledSourceRow = mappedSourceY(textureY)", capture)
        self.assertIn("if (overwrittenSourceRow >= sampledSourceRow)", capture)

    def test_capture_keeps_565_data_direct_and_ge_portrait_fallback_intact(self) -> None:
        capture = function_body(GRAPHICS, "bool CaptureFramebufferToTexture(")
        self.assertIn("if (texture.upperPortraitOwned)", capture)
        self.assertIn("MoveUpperPortraitToMain(texture)", capture)
        self.assertIn("short sourceColumnOffsets[kBufferWidth];", capture)
        self.assertIn(
            "capturePixels[textureY * texture.storageWidth + textureX] = color;",
            capture,
        )
        self.assertNotIn("WriteTexturePixel(texture, textureY", capture)


if __name__ == "__main__":
    unittest.main()

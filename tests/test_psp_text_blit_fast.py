from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
SOURCE = (ROOT / "src" / "TextHelper.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src" / "TextHelper.hpp").read_text(encoding="utf-8")
GUI = (ROOT / "src" / "Gui.cpp").read_text(encoding="utf-8")


class PspTextBlitFastContractTest(unittest.TestCase):
    def test_profile_is_opt_in_diagnostic_only_and_stamped(self) -> None:
        self.assertIn("PSP_TEXT_BLIT_FAST ?= 0", MAKEFILE)
        self.assertIn(
            "PSP_TEXT_BLIT_FAST is a PSP-2000+ validation profile only", MAKEFILE
        )
        self.assertIn("PSP_TEXT_BLIT_FAST requires PSP_PERF_DIAG=1", MAKEFILE)
        self.assertIn(
            "PSP_TEXT_BLIT_FAST requires PSP_TEXT_PREWARM_PROFILE=1", MAKEFILE
        )
        self.assertIn("-DTH07_PSP_TEXT_BLIT_FAST", MAKEFILE)
        stamp = MAKEFILE[
            MAKEFILE.index("PROFILE_STAMP :=") : MAKEFILE.index(".PHONY: FORCE_PROFILE")
        ]
        self.assertIn("$(PSP_TEXT_BLIT_FAST)", stamp)
        self.assertEqual(MAKEFILE.count("PSP_TEXT_BLIT_FAST=0"), 5)

    def test_fast_path_reproduces_sdl_slow_integer_steps(self) -> None:
        helper = SOURCE[
            SOURCE.index("inline u8 DivideTextChannelBy255") : SOURCE.index(
                "TextHelper::TextHelper()"
            )
        ]
        self.assertIn("const u32 biased = product + 1u", helper)
        self.assertIn("(biased + (biased >> 8)) >> 8", helper)
        self.assertIn("source->format->format != SDL_PIXELFORMAT_ARGB8888", helper)
        self.assertIn("destination->format->format != SDL_PIXELFORMAT_RGBA32", helper)
        self.assertIn("SDL_BYTEORDER != SDL_LIL_ENDIAN", helper)
        self.assertIn("activeWidth > destination->w", helper)
        self.assertIn("SDL_IntersectRect(&activeBounds, &destination->clip_rect", helper)
        self.assertIn("source->pitch", helper)
        self.assertIn("destination->pitch", helper)

    def test_white_and_unexpected_surface_states_fail_before_fallback(self) -> None:
        helper = SOURCE[
            SOURCE.index("bool CompositeBoldTextSurfaceExact") : SOURCE.index(
                "} // namespace", SOURCE.index("bool CompositeBoldTextSurfaceExact")
            )
        ]
        self.assertIn("(textColor & 0x00ffffffu) == 0x00ffffffu", helper)
        self.assertIn("SDL_GetSurfaceBlendMode", helper)
        self.assertIn("SDL_GetSurfaceAlphaMod", helper)
        self.assertIn("SDL_GetSurfaceColorMod", helper)
        self.assertIn("SDL_GetColorKey", helper)
        self.assertLess(helper.index("SDL_GetColorKey"), helper.index("SDL_LockSurface"))
        self.assertLess(helper.index("SDL_LockSurface"), helper.index("for (i32 layer"))

    def test_render_keeps_the_complete_legacy_fallback(self) -> None:
        render = SOURCE[
            SOURCE.index("void TextHelper::RenderTextToTextureBold(") : SOURCE.index(
                "i32 TextHelper::GetLogicalStringWidth("
            )
        ]
        self.assertIn("CompositeBoldTextSurfaceExact(", render)
        self.assertIn("if (!exactCompositeDone)", render)
        fallback = render[render.index("if (!exactCompositeDone)") :]
        self.assertEqual(fallback.count("SDL_BlitSurface("), 3)
        self.assertIn("i32 dx[4] = {4, 0, 2, 2}", fallback)
        self.assertIn("i32 dx[4] = {3, 1, 2, 2}", fallback)
        self.assertIn("dstRect = {xPos * 2 + 2, 2", fallback)

    def test_diagnostic_reports_fast_and_fallback_row_counts(self) -> None:
        self.assertIn("u32 fastBlitCount", HEADER)
        self.assertIn("u32 fastBlitFallbackCount", HEADER)
        self.assertIn("++g_StageTextPrewarmTiming.fastBlitCount", SOURCE)
        self.assertIn("++g_StageTextPrewarmTiming.fastBlitFallbackCount", SOURCE)
        self.assertIn("PO%u FA%u FF%u", GUI)


if __name__ == "__main__":
    unittest.main()

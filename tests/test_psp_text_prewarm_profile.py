from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "Makefile"
TEXT_SOURCE = ROOT / "src" / "TextHelper.cpp"
TEXT_HEADER = ROOT / "src" / "TextHelper.hpp"
GUI_SOURCE = ROOT / "src" / "Gui.cpp"


def function_body(source: str, signature: str, next_signature: str) -> str:
    return source[source.index(signature) : source.index(next_signature)]


class PspTextPrewarmProfileContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = MAKEFILE.read_text(encoding="utf-8")
        cls.text = TEXT_SOURCE.read_text(encoding="utf-8")
        cls.header = TEXT_HEADER.read_text(encoding="utf-8")
        cls.gui = GUI_SOURCE.read_text(encoding="utf-8")

    def test_profile_is_diagnostic_only_and_in_object_stamp(self) -> None:
        self.assertIn("PSP_TEXT_PREWARM_PROFILE ?= 0", self.makefile)
        self.assertIn(
            "PSP_TEXT_PREWARM_PROFILE requires PSP_PERF_DIAG=1", self.makefile
        )
        self.assertIn(
            "PSP_TEXT_PREWARM_PROFILE is a PSP-2000+ diagnostic profile only",
            self.makefile,
        )
        self.assertIn(
            "PSP_TEXT_PREWARM_PROFILE requires PSP_DIRECT_GAME=0", self.makefile
        )
        self.assertIn("-DTH07_PSP_TEXT_PREWARM_PROFILE", self.makefile)
        stamp = self.makefile[
            self.makefile.index("PROFILE_STAMP :=") : self.makefile.index(
                ".PHONY: FORCE_PROFILE"
            )
        ]
        self.assertIn("$(PSP_TEXT_PREWARM_PROFILE)", stamp)
        self.assertEqual(self.makefile.count("PSP_TEXT_PREWARM_PROFILE=0"), 5)

    def test_timing_state_and_api_are_compiled_only_for_profile(self) -> None:
        profile_block = self.header[
            self.header.index("#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)") :
            self.header.index("#endif", self.header.index(
                "#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)"
            ))
        ]
        self.assertIn("struct StageTextPrewarmTiming", profile_block)
        self.assertIn("static bool GetStageTextPrewarmTiming", self.header)
        self.assertIn(
            "StageTextPrewarmTiming g_StageTextPrewarmTiming = {};", self.text
        )
        detach = function_body(
            self.text,
            "void TextHelper::DetachStageTextCache()",
            "bool TextHelper::PreRenderTextToCacheBold(",
        )
        self.assertIn("g_StageTextPrewarmTiming = {};", detach)

    def test_duplicate_requests_return_before_unique_row_scope(self) -> None:
        render = function_body(
            self.text,
            "void TextHelper::RenderTextToTextureBold(",
            "i32 TextHelper::GetLogicalStringWidth(",
        )
        self.assertLess(render.index("if (cachedEntry)"), render.index("StageTextPrewarmRowScope"))
        self.assertIn("++g_StageTextPrewarmTiming.hitCount", render)
        self.assertIn("++g_StageTextPrewarmTiming.uniqueRowCount", self.text)

    def test_expensive_phases_are_aggregate_and_exclusive(self) -> None:
        render = function_body(
            self.text,
            "void TextHelper::RenderTextToTextureBold(",
            "i32 TextHelper::GetLogicalStringWidth(",
        )
        for field in (
            "fontUs",
            "conversionUs",
            "ttfUs",
            "clearUs",
            "blitUs",
            "invertUs",
            "filterUs",
            "storeUs",
        ):
            self.assertIn(f"g_StageTextPrewarmTiming.{field} +=", render)
        store = function_body(
            self.text,
            "bool StoreStageTextCache(",
            "} // namespace",
        )
        self.assertIn("rleMeasureUs +=", store)
        self.assertIn("rleEncodeUs +=", store)
        self.assertNotIn("StageTextPrewarmNowUs()", function_body(
            self.text,
            "u32 MeasureStageTextCachePayload(",
            "void EncodeStageTextCachePayload(",
        ))
        self.assertNotIn("StageTextPrewarmNowUs()", function_body(
            self.text,
            "void EncodeStageTextCachePayload(",
            "const u8 *DecodeStageTextCachePayload(",
        ))

    def test_report_has_two_bounded_aggregate_lines_and_closure_terms(self) -> None:
        prewarm = function_body(
            self.gui, "bool Gui::PreRenderStageText()", "void Gui::MsgRead("
        )
        self.assertIn('"textpw1 Q%u H%u U%u X%u FM%u SZ%u T%u LK%u FN%u CV%u TT%u"', prewarm)
        self.assertIn(
            '"textpw2 CL%u BL%u IV%u BF%u ST%u RM%u RE%u SO%u UO%u FL%u PO%u FA%u FF%u"',
            prewarm,
        )
        self.assertIn("timing.storeUs - storePartsUs", prewarm)
        self.assertIn("timing.uniqueTotalUs - uniquePartsUs", prewarm)
        self.assertIn("prewarmElapsedUs - prewarmPartsUs", prewarm)

    def test_direct_game_per_row_note_cannot_contaminate_profile(self) -> None:
        self.assertIn(
            "#if defined(TH07_PSP_DIRECT_GAME) && "
            "!defined(TH07_PSP_TEXT_PREWARM_PROFILE)",
            self.text,
        )


if __name__ == "__main__":
    unittest.main()

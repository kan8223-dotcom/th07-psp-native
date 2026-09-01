from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEXT_SOURCE = ROOT / "src" / "TextHelper.cpp"


def section(source: str, start: str, end: str) -> str:
    return source[source.index(start) : source.index(end, source.index(start))]


class PspLocalFontSubsetRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.text = TEXT_SOURCE.read_text(encoding="utf-8")

    def test_tracked_authority_header_and_exact_stock_count_are_required(self) -> None:
        self.assertIn("#if defined(TH07_PSP_LOCAL_FONT_SUBSET)", self.text)
        self.assertIn('#include "Th07FontCoverage.hpp"', self.text)
        self.assertIn("kExpectedStockFontCodepointCount = 1190u", self.text)
        self.assertIn("kExpectedNameEntryCodepointCount = 94u", self.text)
        self.assertIn(
            "kTh07PspStockFontCodepointCount == kExpectedStockFontCodepointCount",
            self.text,
        )
        self.assertIn(
            "kTh07PspNameEntryCodepointCount == kExpectedNameEntryCodepointCount",
            self.text,
        )
        self.assertIn("sizeof(kTh07PspStockFontCodepoints)", self.text)

    def test_candidate_order_is_subset_then_ttc_then_noto(self) -> None:
        candidates = section(
            self.text,
            "constexpr PspDefaultFontCandidate kPspDefaultFontCandidates[]",
            "long g_DefaultFontFaceIndex",
        )
        names = (
            candidates.index('"msgothic-subset.ttf"'),
            candidates.index('"msgothic.ttc"'),
            candidates.index('"NotoSansJP-Regular.ttf"'),
        )
        self.assertEqual(names, tuple(sorted(names)))
        self.assertEqual(candidates.count(", 0}"), 3)

    def test_every_candidate_checks_the_entire_pinned_union(self) -> None:
        coverage = section(
            self.text,
            "bool FontProvidesStockCoverage(",
            "TTF_Font *OpenCoverageCheckedFont(",
        )
        self.assertIn(
            "i < kExpectedStockFontCodepointCount; ++i", coverage
        )
        self.assertIn("kTh07PspStockFontCodepoints[i]", coverage)
        self.assertIn("TTF_GlyphIsProvided32", coverage)
        self.assertIn("++provided", coverage)
        self.assertIn(
            "return provided == kExpectedStockFontCodepointCount", coverage
        )

        candidate = section(
            self.text,
            "TTF_Font *OpenCoverageCheckedFont(",
            "} // namespace",
        )
        reject = candidate.index("if (!FontProvidesStockCoverage")
        close = candidate.index("TTF_CloseFont(font)", reject)
        accept = candidate.index("g_DefaultFontFaceIndex = candidate.faceIndex", close)
        self.assertEqual((reject, close, accept), tuple(sorted((reject, close, accept))))
        self.assertIn("missing=U+%04X", candidate)
        self.assertIn("missing=n/a", candidate)
        self.assertIn("missing=none", candidate)
        self.assertIn("result=ok", candidate)

    def test_selected_face_is_reused_by_ram_and_both_file_demotions(self) -> None:
        self.assertIn(
            "TTF_OpenFontIndex(resolvedPath, 10, candidate.faceIndex)", self.text
        )
        self.assertIn(
            "RememberDefaultFontSelection(fontPath, candidate.faceIndex)", self.text
        )
        self.assertIn(
            "TTF_OpenFontIndexRW(memoryStream, 0, 10, g_DefaultFontFaceIndex)",
            self.text,
        )
        self.assertEqual(
            self.text.count(
                "TTF_OpenFontIndex(g_DefaultFontPath, 10, g_DefaultFontFaceIndex)"
            ),
            2,
        )

    def test_frozen_path_retains_legacy_open_calls_behind_else_branches(self) -> None:
        open_default = section(
            self.text,
            "static TTF_Font *OpenDefaultFont()",
            "bool TextHelper::PromoteDefaultFontToMainRam()",
        )
        self.assertIn("#if defined(TH07_PSP_LOCAL_FONT_SUBSET)", open_default)
        self.assertIn("#else", open_default)
        self.assertIn("TTF_OpenFont(resolvedPath, 10)", open_default)

        promote = section(
            self.text,
            "bool TextHelper::PromoteDefaultFontToMainRam()",
            "bool TextHelper::DemoteDefaultFontToFile()",
        )
        self.assertIn("TTF_OpenFontIndexRW", promote)
        self.assertIn("TTF_OpenFontRW(memoryStream, 0, 10)", promote)

        render = section(
            self.text,
            "void TextHelper::RenderTextToTextureBold(",
            "i32 TextHelper::GetLogicalStringWidth(",
        )
        self.assertNotIn("TTF_GlyphIsProvided32", render)
        self.assertNotIn("OpenCoverageCheckedFont", render)


if __name__ == "__main__":
    unittest.main()

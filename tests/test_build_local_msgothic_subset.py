from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "build_local_msgothic_subset.py"


def load_tool():
    spec = importlib.util.spec_from_file_location("build_local_msgothic_subset", TOOL_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class LocalMsGothicSubsetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tool = load_tool()
        cls.source = TOOL_PATH.read_text(encoding="utf-8")

    def test_score_and_replay_name_entry_table_is_extracted_in_full(self) -> None:
        charset, source_hash = self.tool.load_name_entry_charset()
        expected = (
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,:;_@abcdefghijklmnopqrstuvwxyz+-/"
            "*=%0123456789#!?'\"$(){}[]<>&\\|~^ --"
        )
        self.assertEqual(charset, expected)
        self.assertEqual(len(charset), 96)
        self.assertEqual(len(source_hash), 64)
        extracted = self.tool.normalized_codepoints(charset)
        self.assertTrue(extracted <= self.tool.mandatory_codepoints())
        for required in "AZaz09@_\\|~":
            self.assertIn(ord(required), extracted)

    def test_base_mandatory_policy_keeps_all_printable_ascii(self) -> None:
        required = self.tool.mandatory_codepoints()
        for char in " 09AZaz~@_\\|":
            self.assertIn(ord(char), required)
        self.assertEqual(required, frozenset(range(0x20, 0x7F)))

    def test_stock_archive_profile_is_exact_and_keeps_fullwidth_symbols(self) -> None:
        profile_module = self.tool.load_stock_profile_module()
        archive = profile_module.DEFAULT_STOCK_ARCHIVE
        if not archive.is_file():
            self.skipTest("local stock TH07 analysis archive is absent")
        profile = profile_module.build_stock_profile(archive)
        profile_module.verify_authority_header(profile)
        self.assertEqual(len(profile.codepoints), 1190)
        self.assertEqual(
            profile.codepoint_sha256,
            "da81e0e1a2b8b5d44c135d2ac43f3f91a90ce684c62b985206992e3855a90aa4",
        )
        self.assertEqual(
            [(group.name, group.row_count, group.unique_row_count, len(group.codepoints))
             for group in profile.groups],
            [
                ("messages", 1109, 1016, 749),
                ("spell_names", 141, 141, 343),
                ("music_room", 141, 139, 482),
                ("endings", 280, 263, 447),
                ("static_ui", 69, 66, 243),
            ],
        )
        for char in "！０９ＡＭＷｏ「」、。～":
            self.assertIn(ord(char), profile.codepoints)

    def test_generated_paths_inside_repo_are_rejected(self) -> None:
        with self.assertRaisesRegex(self.tool.SubsetToolError, "inside repository"):
            self.tool.validate_generated_paths(
                (
                    ROOT / "msgothic-subset.ttf",
                    Path("/tmp/msgothic-subset.manifest.json"),
                    Path("/tmp/msgothic-subset.coverage.txt"),
                ),
                force=True,
            )

    def test_generated_paths_must_be_distinct(self) -> None:
        duplicate = Path("/tmp/th07-font-test-output")
        with self.assertRaisesRegex(self.tool.SubsetToolError, "must be distinct"):
            self.tool.validate_generated_paths((duplicate, duplicate, duplicate), force=True)

    def test_proprietary_source_font_inside_repo_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "subset.ttf"
            with self.assertRaisesRegex(
                self.tool.SubsetToolError, "source font inside repository"
            ):
                self.tool.build_subset(
                    TOOL_PATH,
                    [ROOT / "src" / "ResultScreen.cpp"],
                    output,
                    output.with_name("manifest.json"),
                    output.with_name("coverage.txt"),
                )

    def test_missing_fonttools_has_actionable_install_command(self) -> None:
        with mock.patch.object(
            self.tool.importlib,
            "import_module",
            side_effect=ModuleNotFoundError("fontTools"),
        ):
            with self.assertRaisesRegex(
                self.tool.SubsetToolError, r"pip install --user fonttools"
            ):
                self.tool.require_fonttools()

    def test_missing_pillow_has_actionable_install_command(self) -> None:
        with mock.patch.object(
            self.tool.importlib,
            "import_module",
            side_effect=ModuleNotFoundError("PIL"),
        ):
            with self.assertRaisesRegex(self.tool.SubsetToolError, r"pip install --user Pillow"):
                self.tool.require_pillow()

    def test_release_audit_allows_only_exact_formal_noto_compatibility_file(self) -> None:
        audit = (ROOT / "tools" / "release_audit.sh").read_text(encoding="utf-8")
        self.assertIn(r"msgothic[^/]*\.(ttc|ttf|otf)", audit)
        self.assertIn("approved_tracked_subset='psp/assets/msgothic-subset.ttf'", audit)
        self.assertIn("private/unapproved msgothic font", audit)
        self.assertIn("tools/build_release_noto_subset.py --check", audit)

    def _fonttools_or_skip(self):
        try:
            from fontTools.fontBuilder import FontBuilder
            from fontTools.pens.ttGlyphPen import TTGlyphPen
            from fontTools.ttLib import TTCollection, TTFont
        except ModuleNotFoundError:
            self.skipTest("fontTools not installed")
        return FontBuilder, TTGlyphPen, TTCollection, TTFont

    def _write_font(self, path: Path, family: str, codepoints: set[int]) -> None:
        FontBuilder, TTGlyphPen, _TTCollection, _TTFont = self._fonttools_or_skip()
        builder = FontBuilder(1000, isTTF=True)
        glyph_names = [".notdef"] + [f"uni{value:06X}" for value in sorted(codepoints)]
        builder.setupGlyphOrder(glyph_names)
        builder.setupCharacterMap(
            {value: f"uni{value:06X}" for value in sorted(codepoints)}
        )
        glyphs = {}
        metrics = {}
        for glyph_name in glyph_names:
            pen = TTGlyphPen(None)
            if glyph_name != ".notdef":
                pen.moveTo((80, 0))
                pen.lineTo((520, 0))
                pen.lineTo((520, 700))
                pen.lineTo((80, 700))
                pen.closePath()
            glyphs[glyph_name] = pen.glyph()
            metrics[glyph_name] = (600, 40)
        builder.setupGlyf(glyphs)
        builder.setupHorizontalMetrics(metrics)
        builder.setupHorizontalHeader(ascent=800, descent=-200)
        builder.setupNameTable(
            {
                "familyName": family,
                "styleName": "Regular",
                "uniqueFontIdentifier": f"TH07 test {family}",
                "fullName": family,
                "psName": family.replace(" ", "-"),
            }
        )
        builder.setupOS2(
            sTypoAscender=800,
            sTypoDescender=-200,
            usWinAscent=800,
            usWinDescent=200,
        )
        builder.setupPost()
        builder.setupMaxp()
        builder.font.recalcTimestamp = False
        builder.save(path)

    def _write_collection(self, path: Path, codepoints: set[int]) -> None:
        _FontBuilder, _TTGlyphPen, TTCollection, TTFont = self._fonttools_or_skip()
        first = path.with_name("other.ttf")
        second = path.with_name("msgothic.ttf")
        self._write_font(first, "Other Face", codepoints)
        self._write_font(second, "MS Gothic", codepoints)
        collection = TTCollection()
        collection.fonts = [
            TTFont(first, lazy=False, recalcTimestamp=False),
            TTFont(second, lazy=False, recalcTimestamp=False),
        ]
        collection.save(path)
        for font in collection.fonts:
            font.close()

    def test_deterministic_subset_manifest_and_coverage(self) -> None:
        _FontBuilder, _TTGlyphPen, _TTCollection, TTFont = self._fonttools_or_skip()
        with tempfile.TemporaryDirectory() as tmp:
            temporary = Path(tmp)
            chars = temporary / "used.txt"
            chars.write_text("妖々夢テスト\n", encoding="utf-8")
            needed = set(self.tool.mandatory_codepoints())
            needed.update(self.tool.normalized_codepoints(chars.read_text(encoding="utf-8")))
            source = temporary / "msgothic.ttc"
            self._write_collection(source, needed)

            outputs = []
            for directory_name in ("run-a", "run-b"):
                directory = temporary / directory_name
                output = directory / "msgothic-subset.ttf"
                manifest = directory / "manifest.json"
                coverage = directory / "coverage.txt"
                result = self.tool.build_subset(
                    source,
                    [chars],
                    output,
                    manifest,
                    coverage,
                    validate_raster=False,
                )
                outputs.append((output, manifest, coverage, result))

            first, second = outputs
            self.assertEqual(first[0].read_bytes(), second[0].read_bytes())
            self.assertEqual(first[1].read_bytes(), second[1].read_bytes())
            self.assertEqual(first[2].read_bytes(), second[2].read_bytes())
            manifest = json.loads(first[1].read_text(encoding="utf-8"))
            self.assertEqual(manifest["source_font"]["collection_index"], 1)
            self.assertEqual(manifest["coverage"]["source_missing_count"], 0)
            self.assertEqual(manifest["coverage"]["output_missing_count"], 0)
            self.assertNotIn(str(temporary), first[1].read_text(encoding="utf-8"))
            subset = TTFont(first[0], lazy=False, recalcTimestamp=False)
            cmap = subset.getBestCmap() or {}
            self.assertTrue(needed <= set(cmap))
            subset.close()

    def test_real_ms_gothic_stock_profile_passes_all_quality_gates(self) -> None:
        source = self.tool.discover_windows_font()
        profile_module = self.tool.load_stock_profile_module()
        archive = profile_module.DEFAULT_STOCK_ARCHIVE
        if source is None or not archive.is_file():
            self.skipTest("local Windows MS Gothic or stock TH07 archive is absent")
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "msgothic-subset.ttf"
            manifest = self.tool.build_subset(
                source,
                [],
                output,
                output.with_name("manifest.json"),
                output.with_name("coverage.txt"),
                stock_archive=archive,
            )
            self.assertEqual(manifest["coverage"]["requested_count"], 1190)
            self.assertEqual(manifest["coverage"]["source_missing_count"], 0)
            self.assertEqual(manifest["coverage"]["output_missing_count"], 0)
            self.assertEqual(manifest["validation"]["structural"]["status"], "passed")
            self.assertEqual(manifest["validation"]["raster"]["status"], "passed")
            self.assertEqual(
                manifest["validation"]["raster"]["checked_glyph_renders"], 3570
            )

    def test_missing_name_entry_character_fails_before_output(self) -> None:
        self._fonttools_or_skip()
        with tempfile.TemporaryDirectory() as tmp:
            temporary = Path(tmp)
            chars = temporary / "used.txt"
            chars.write_text("妖夢", encoding="utf-8")
            needed = set(self.tool.mandatory_codepoints())
            needed.update(self.tool.normalized_codepoints("妖夢"))
            needed.remove(ord("~"))
            source = temporary / "msgothic.ttc"
            self._write_collection(source, needed)
            output = temporary / "out" / "subset.ttf"
            with self.assertRaisesRegex(self.tool.CoverageError, r"U\+007E"):
                self.tool.build_subset(
                    source,
                    [chars],
                    output,
                    output.with_name("manifest.json"),
                    output.with_name("coverage.txt"),
                    validate_raster=False,
                )
            self.assertFalse(output.exists())

    def test_help_states_local_only_policy(self) -> None:
        completed = subprocess.run(
            ["python3", str(TOOL_PATH), "--help"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("local-use-only", completed.stdout)
        self.assertIn("--archive", completed.stdout)
        self.assertIn("--chars", completed.stdout)


if __name__ == "__main__":
    unittest.main()

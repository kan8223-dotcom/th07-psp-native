from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "build_release_noto_subset.py"
FONT_PATH = ROOT / "psp" / "assets" / "msgothic-subset.ttf"


def load_tool():
    spec = importlib.util.spec_from_file_location("build_release_noto_subset", TOOL_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class ReleaseNotoSubsetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tool = load_tool()

    def test_numeric_authority_is_exact(self) -> None:
        codepoints = self.tool.load_authority()
        self.assertEqual(len(codepoints), 1190)
        self.assertEqual(
            self.tool.codepoint_hash(codepoints),
            "da81e0e1a2b8b5d44c135d2ac43f3f91a90ce684c62b985206992e3855a90aa4",
        )
        self.assertEqual(tuple(sorted(set(codepoints))), codepoints)

    def test_checked_in_subset_is_exact_and_reproducible(self) -> None:
        approved = self.tool.build_and_verify(
            self.tool.DEFAULT_SOURCE, FONT_PATH, check=True
        )
        self.assertEqual(
            approved.sha256,
            "6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05",
        )
        self.assertEqual(FONT_PATH.stat().st_size, 264_288)
        self.assertEqual(
            self.tool.sha256_file(FONT_PATH),
            "c456df98197c895c2919a690c737ab3c4a2924799bb4d92fa3a53849c6b56dec",
        )

    def test_clean_build_has_required_filename_and_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "msgothic-subset.ttf"
            self.tool.build_and_verify(self.tool.DEFAULT_SOURCE, output, check=False)
            self.assertEqual(output.read_bytes(), FONT_PATH.read_bytes())

    def test_unapproved_source_and_output_name_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bad_source = root / "private-font.ttf"
            bad_source.write_bytes(b"not an approved font")
            with self.assertRaisesRegex(self.tool.ReleaseFontError, "unapproved source"):
                self.tool.build_and_verify(
                    bad_source, root / "msgothic-subset.ttf", check=False
                )
            with self.assertRaisesRegex(self.tool.ReleaseFontError, "filename must be"):
                self.tool.build_and_verify(
                    self.tool.DEFAULT_SOURCE, root / "other.ttf", check=False
                )

    def test_license_and_provenance_records_are_pinned(self) -> None:
        self.tool.audit_license()
        self.assertEqual(
            self.tool.sha256_file(self.tool.OFL_LICENSE),
            "babcfe66c8a098b2fa279bc724a3a342f8124f77ce18941fbcc1bbb39823cded",
        )
        fontlog = (
            ROOT / "licenses" / "NotoSansJP" / "FONTLOG-TH07PSP.txt"
        ).read_text(encoding="utf-8")
        flattened = fontlog.replace("\n", " ")
        for expected in (
            "68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5",
            "c456df98197c895c2919a690c737ab3c4a2924799bb4d92fa3a53849c6b56dec",
            "fontTools 4.62.1",
            "1,623 unique, non-empty",
            "did not capture the fontTools version",
            "not MS Gothic",
        ):
            self.assertIn(expected, flattened)

    def test_formal_tool_has_no_microsoft_or_original_data_input(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        self.assertNotIn("build_local_msgothic_subset", source)
        self.assertNotIn("th07.dat", source.casefold())
        self.assertNotIn("thbgm.dat", source.casefold())
        self.assertNotIn("/windows/fonts", source.casefold())


if __name__ == "__main__":
    unittest.main()

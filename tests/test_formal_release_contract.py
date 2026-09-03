from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FormalReleaseContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.readme = (ROOT / "README.md").read_text(encoding="utf-8")
        cls.readme_en = (ROOT / "README_EN.md").read_text(encoding="utf-8")
        cls.changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        cls.known = (ROOT / "docs" / "KNOWN_ISSUES.md").read_text(
            encoding="utf-8"
        )
        cls.ark_guide = (ROOT / "docs" / "ARK5_HIGH_MEMORY.md").read_text(
            encoding="utf-8"
        )
        cls.snippet = (ROOT / "ark" / "ARK5_HIGHMEM_SNIPPET.txt").read_text(
            encoding="utf-8"
        )
        cls.audit = (ROOT / "tools" / "release_audit.sh").read_text(
            encoding="utf-8"
        )
        cls.font_guide = (ROOT / "docs" / "PSP_RELEASE_FONTS.md").read_text(
            encoding="utf-8"
        )
        cls.fontlog = (
            ROOT / "licenses" / "NotoSansJP" / "FONTLOG-TH07PSP.txt"
        ).read_text(encoding="utf-8")

    def test_public_package_is_one_non_model_specific_zip(self) -> None:
        self.assertIn("PSP_RELEASE_ZIP := th07-psp-native-$(PSP_RELEASE_VERSION).zip", self.makefile)
        self.assertNotIn("PSP_RELEASE_1000_ZIP", self.makefile)
        self.assertNotIn("PSP_RELEASE_2000PLUS_ZIP", self.makefile)
        self.assertNotIn("release-psp1000:", self.makefile)
        self.assertNotIn("release-psp2000plus:", self.makefile)
        self.assertIn("release-build: psp-unified-build", self.makefile)

    def test_unified_build_uses_exact_accepted_anchors(self) -> None:
        for expected in (
            "d49f1683f370224e102b13c8a14a1d09d9bead77d55bff449ed26f0b65c08ef6",
            "356fbd32ee75dced8b1c9384b31a47613d1848ebd6a2af0b3b21cc92ba8e5a3d",
            "3dc5c753497349d6fb0ab5ae2a819b240cc51e8aa412ded10bb52daa540d841d",
            "--ge4wrap",
            "tools/pack_unified_pbp.py",
            "tools/audit_unified_pbp.py",
            "tools/check_no_original_assets.py",
        ):
            self.assertIn(expected, self.makefile)
        self.assertIn("東方妖々夢 ～ Perfect Cherry Blossom.", self.makefile)

    def test_current_readmes_do_not_repeat_obsolete_tester_status(self) -> None:
        stale_ja = (
            "現在はテスター向けのv0.1.7-beta",
            "PSP-1000専用32MB版を別々のZIP",
            "PSP-1000版は現在、リプレイの同期互換性を保証できません",
            "5.50用EBOOTは自己ビルド",
            "再現機種と32/64MBビルドは未確定",
            "Touhou 7 PSP-1000 Beta",
            "Touhou 7 PSP-2000+ Beta",
        )
        stale_en = (
            "The current release is v0.1.7-beta",
            "There are two separate downloads",
            "special 32 MiB tester build",
            "replay synchronization remains unverified",
            "fail to return to Replay Select",
            "Touhou 7 PSP-1000 Beta",
            "Touhou 7 PSP-2000+ Beta",
        )
        for text in stale_ja:
            self.assertNotIn(text, self.readme)
        for text in stale_en:
            self.assertNotIn(text, self.readme_en)

    def test_ark5_and_max_are_prerequisites_in_both_languages(self) -> None:
        self.assertIn("対応CFWはARK-5のみ", self.readme)
        self.assertIn("ARK-5 is the only supported CFW", self.readme_en)
        for document in (self.readme, self.readme_en, self.ark_guide):
            self.assertIn("ARK-5", document)
            self.assertIn("Use Extra Memory", document)
            self.assertIn("Max", document)
        active = [
            line.strip()
            for line in self.snippet.splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        self.assertEqual(active, ["homebrew, highmem, on"])
        self.assertIn("Use Extra Memory", self.snippet)
        self.assertIn("Max", self.snippet)

    def test_psp1000_acceptance_keeps_exact_scope_and_unknown_cfw(self) -> None:
        for document in (self.changelog, self.known):
            self.assertIn(
                "18CF0136DE1525EF6B0ECA4FCA5BC2415A0A65875D8C0D88D53A9A509A94C365",
                document,
            )
            self.assertIn(
                "D6B6634FB12DBA2DF5084D04DB05612FC681735DBC0D035A42A52143DFFB498F",
                document,
            )
            self.assertIn("Replay選択", document)
            self.assertRegex(document, re.compile(r"CFW.*未.*確定", re.DOTALL))

    def test_release_audit_understands_the_unified_container(self) -> None:
        self.assertIn("tools/audit_unified_pbp.py", self.audit)
        self.assertIn("tools/check_no_original_assets.py", self.audit)
        self.assertIn("exactly one TH07PSP/$required", self.audit)
        self.assertIn("Use Extra Memory = Max", self.audit)

    def test_formal_archive_has_two_exact_noto_fonts(self) -> None:
        for expected in (
            "PSP_RELEASE_FULL_FONT := psp/assets/NotoSansJP-Regular.ttf",
            "PSP_RELEASE_1000_FONT := psp/assets/msgothic-subset.ttf",
            "psp-release-font-audit",
            "tools/build_release_noto_subset.py --check",
            "docs/PSP_RELEASE_FONTS.md",
            "licenses/NotoSansJP/FONTLOG-TH07PSP.txt",
        ):
            self.assertIn(expected, self.makefile)
        for expected in (
            "msgothic-subset.ttf",
            "docs/PSP_RELEASE_FONTS.md",
            "licenses/NotoSansJP/FONTLOG-TH07PSP.txt",
        ):
            self.assertIn(expected, self.audit)
        for document in (self.readme, self.readme_en, self.font_guide):
            self.assertIn("NotoSansJP-Regular.ttf", document)
            self.assertIn("msgothic-subset.ttf", document)
            self.assertIn("264,288", document)
        self.assertIn(
            "c456df98197c895c2919a690c737ab3c4a2924799bb4d92fa3a53849c6b56dec",
            self.fontlog,
        )
        self.assertIn("not MS Gothic", self.fontlog)

    def test_initial_xmb_media_is_neutral_not_original_derived(self) -> None:
        self.assertIn("完全透明", self.readme)
        self.assertIn("fully transparent", self.readme_en)
        self.assertNotIn("初期EBOOTのXMB画像slotが空", self.readme)
        self.assertNotIn("initial XMB media slots are empty", self.readme_en)

    def test_readmes_warn_that_original_dats_must_be_direct_children(self) -> None:
        for document in (self.readme, self.readme_en):
            self.assertIn("TH07PSP/th7/東方妖々夢/th07.dat", document)
            self.assertIn("original TH07 1.00b data not found", document)
            self.assertIn("TH07PSP/th7/th07.dat", document)
        self.assertIn("必ず`TH07PSP/th7/`直下", self.readme)
        self.assertIn("directly in `TH07PSP/th7/`", self.readme_en)
        for document in (self.readme, self.readme_en):
            self.assertIn("23,829,135", document)
            self.assertIn("444,516,656", document)


if __name__ == "__main__":
    unittest.main()

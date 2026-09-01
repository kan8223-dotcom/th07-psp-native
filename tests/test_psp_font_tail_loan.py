from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "psp" / "optional_ram_budget.cpp"
HEADER = ROOT / "psp" / "optional_ram_budget.hpp"
HARNESS = ROOT / "tests" / "psp_font_tail_loan_harness.cpp"


class PspFontTailLoanTest(unittest.TestCase):
    def test_tail_path_is_strictly_a6v4_only(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        gate = (
            "#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) && \\\n"
            "    defined(TH07_PSP_TITLE_FONT_HOLE_SWAP) && \\\n"
            "    defined(TH07_PSP_LOCAL_FONT_SUBSET)"
        )
        self.assertGreaterEqual(source.count(gate), 2)
        self.assertIn("kFontTailAlignment = 64u", source)
        self.assertIn("textPoolBorrowsFontTail", source)
        self.assertIn("fontTailOffsetBytes", source)
        self.assertIn("A6V4 TEXT loan=font-tail", source)

    def test_owner_contract_refuses_overlapping_title_transition(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        header = HEADER.read_text(encoding="utf-8")
        release = source[
            source.index("void ReleaseFontBufferOwned") :
            source.index("void Th07PspOptionalRamReleaseFontBuffer(")
        ]
        self.assertIn("textPoolBorrowsFontTail", release)
        self.assertIn("transition=FONT->IDLE refused reason=text-tail-attached", release)
        self.assertLess(
            release.index("textPoolBorrowsFontTail"),
            release.index("archiveWorkspaceLease =\n                ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE"),
        )
        self.assertIn("owner refuses the transition", header)

    def test_tail_loan_lifecycle_and_malloc_fallback_under_asan(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "font_tail_loan_harness"
            command = [
                "g++",
                "-std=c++17",
                "-DTH07_PSP",
                "-DTH07_PSP_TITLE_ARCHIVE_WORKSPACE",
                "-DTH07_PSP_TITLE_FONT_HOLE_SWAP",
                "-DTH07_PSP_LOCAL_FONT_SUBSET",
                "-fsanitize=address",
                "-fno-omit-frame-pointer",
                "-I",
                str(ROOT / "src"),
                "-I",
                str(ROOT / "psp"),
                str(HARNESS),
                str(SOURCE),
                "-o",
                str(binary),
            ]
            subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
            subprocess.run(
                [str(binary)],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
                env={"ASAN_OPTIONS": "detect_leaks=0"},
            )


if __name__ == "__main__":
    unittest.main()

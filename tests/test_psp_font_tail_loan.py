from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
SOURCE = ROOT / "psp" / "optional_ram_budget.cpp"
HEADER = ROOT / "psp" / "optional_ram_budget.hpp"
FILE_SYSTEM = ROOT / "src" / "FileSystem.cpp"
HARNESS = ROOT / "tests" / "psp_font_tail_loan_harness.cpp"


class PspFontTailLoanTest(unittest.TestCase):
    def test_tail_path_is_opt_in_and_profile_stamped(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("PSP_FONT_TAIL_ARCHIVE ?= 0", MAKEFILE)
        self.assertIn("PSP_FONT_TAIL_ARCHIVE is PSP-2000+ only", MAKEFILE)
        self.assertIn(
            "PSP_FONT_TAIL_ARCHIVE requires PSP_TITLE_ARCHIVE_WORKSPACE=1",
            MAKEFILE,
        )
        self.assertIn(
            "PSP_FONT_TAIL_ARCHIVE requires PSP_TITLE_FONT_HOLE_SWAP=1",
            MAKEFILE,
        )
        self.assertIn(
            "PSP_FONT_TAIL_ARCHIVE requires PSP_LOCAL_FONT_SUBSET=1", MAKEFILE
        )
        stamp = next(
            line for line in MAKEFILE.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_FONT_TAIL_ARCHIVE)", stamp)
        self.assertGreaterEqual(source.count("#if defined(TH07_PSP_FONT_TAIL_ARCHIVE)"), 5)
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

    def test_face_archives_try_the_a6v4_font_tail_before_malloc(self) -> None:
        file_system = FILE_SYSTEM.read_text(encoding="utf-8")
        gate = (
            "defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT) || \\\n"
            "    defined(TH07_PSP_FONT_TAIL_ARCHIVE)"
        )
        self.assertGreaterEqual(file_system.count(gate), 1)
        self.assertIn('std::strncmp(filename, "face_", 5u)', file_system)
        transient = file_system.index(
            "Th07PspOptionalRamAcquireTransientArchive(fsize)"
        )
        fallback = file_system.index("buf = (u8 *)malloc(fsize);", transient)
        self.assertLess(transient, fallback)

        source = SOURCE.read_text(encoding="utf-8")
        acquire = source[
            source.index("void *Th07PspOptionalRamAcquireTransientArchive") :
            source.index("bool Th07PspOptionalRamIsTransientArchive")
        ]
        for contract in (
            "!g_ProcessOptionalRam.fontTailArchiveBorrowed",
            "!g_OptionalRamBudget.textPoolBorrowsTitleWorkspace",
            "GetFontTailRegion(&fontTail, &fontTailBytes)",
            "bytes <= fontTailBytes",
            "fontTailArchiveBorrowed = true",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, acquire)

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
                "-DTH07_PSP_FONT_TAIL_ARCHIVE",
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

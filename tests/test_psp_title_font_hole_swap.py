from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
MAIN_MENU = (ROOT / "src" / "MainMenu.cpp").read_text(encoding="utf-8")
SUPERVISOR = (ROOT / "src" / "Supervisor.cpp").read_text(encoding="utf-8")
SWAP_SOURCE = ROOT / "psp" / "title_font_hole_swap.cpp"
OWNER_TEXT = (ROOT / "psp" / "optional_ram_budget.cpp").read_text(encoding="utf-8")
TEXT_HELPER = (ROOT / "src" / "TextHelper.cpp").read_text(encoding="utf-8")
ANM_MANAGER = (ROOT / "src" / "AnmManager.cpp").read_text(encoding="utf-8")
HARNESS = ROOT / "tests" / "title_font_hole_swap_harness.cpp"
OWNER_HARNESS = ROOT / "tests" / "psp_title_font_workspace_harness.cpp"
OWNER_SOURCE = ROOT / "psp" / "optional_ram_budget.cpp"


class PspTitleFontHoleSwapTest(unittest.TestCase):
    def test_profile_is_default_off_psp2000plus_and_exclusive(self) -> None:
        self.assertIn("PSP_TITLE_FONT_HOLE_SWAP ?= 0", MAKEFILE)
        self.assertIn("PSP_TITLE_FONT_HOLE_SWAP is PSP-2000+ only", MAKEFILE)
        self.assertIn("PSP_TITLE_FONT_HOLE_SWAP requires PSP_FONT_MAIN_RAM=1", MAKEFILE)
        self.assertIn("PSP_TITLE_FONT_HOLE_SWAP requires PSP_TITLE_ARCHIVE_WORKSPACE=1", MAKEFILE)
        self.assertIn(
            "PSP_TITLE_FONT_HOLE_SWAP and PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT are mutually exclusive",
            MAKEFILE,
        )
        stamp = next(
            line for line in MAKEFILE.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_TITLE_FONT_HOLE_SWAP)", stamp)

    def test_a6v3_target_is_frozen_rid30_and_a6v2_stays_separate(self) -> None:
        self.assertIn("psp3000-rid30-a6v3-title-font-hole-swap-build:", MAKEFILE)
        target = MAKEFILE[MAKEFILE.index("psp3000-rid30-a6v3-title-font-hole-swap-build:") :
                          MAKEFILE.index("# SC member of the pair")]
        for token in (
            "PSP_RID30_AB_ME_TITLE_WORKSPACE=1",
            "PSP_RID30_AB_ME_TITLE_TRANSIENT=0",
            "PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1",
            "PSP_RID30_AB_ME_BUILD_ID=0x260901a8u",
            "psp3000-rid30-ab-me-build",
        ):
            self.assertIn(token, target)
        a6v2 = MAKEFILE[MAKEFILE.index("psp3000-rid30-a6v2-title-transient-workspace-build:") :
                        MAKEFILE.index("# A6v3 is an alternative")]
        self.assertIn("PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=0", a6v2)

    def test_music_room_font_fix_is_one_delta_from_a6v4w(self) -> None:
        target = MAKEFILE[
            MAKEFILE.index("psp3000-a6v4w-music-room-fontfix-build:") :
            MAKEFILE.index("# SC member of the pair")
        ]
        for token in (
            "PSP_RID30_AB_ME_UV16=0",
            "PSP_RID30_AB_ME_XYZ16=0",
            "PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0",
            "PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0",
            "PSP_RID30_AB_ME_SEED_SOA=0",
            "PSP_RID30_AB_ME_TITLE_WORKSPACE=1",
            "PSP_RID30_AB_ME_TITLE_TRANSIENT=0",
            "PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1",
            "PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1",
            "PSP_RID30_AB_ME_BUILD_ID=0x260901abu",
            "TH07 A6V4W MUSIC FONT FIX",
        ):
            self.assertIn(token, target)

    def test_swap_wraps_exact_title_load_and_restores_before_return(self) -> None:
        menu = MAIN_MENU[MAIN_MENU.index("ZunResult MainMenu::ActualAddedCallback()") :
                         MAIN_MENU.index("ZunResult MainMenu::AddedCallback(")]
        begin = menu.index("Th07PspBeginTitleFontHoleSwap()")
        load = menu.index('LoadAnms(ANM_FILE_TITLE, "data/title01.anm"', begin)
        end = menu.index("Th07PspEndTitleFontHoleSwap", load)
        failure = menu.index("if (titleLoadResult != ZUN_SUCCESS)", end)
        self.assertEqual((begin, load, end, failure), tuple(sorted((begin, load, end, failure))))

    def test_font_and_title_exchange_one_arena(self) -> None:
        create = TEXT_HELPER[TEXT_HELPER.index("ZunResult TextHelper::CreateTextBuffer()") :
                             TEXT_HELPER.index("void TextHelper::ReleaseTextBuffer()")]
        reserve = create.index("Th07PspOptionalRamReserveTitleFontWorkspace")
        promote = create.index("PromoteDefaultFontToMainRam()", reserve)
        self.assertLess(reserve, promote)
        for token in (
            "ARCHIVE_WORKSPACE_FONT",
            "fontBufferBorrowsTitleWorkspace",
            "transition=FONT->IDLE",
            "transition=IDLE->TITLE",
            "transition=TITLE->IDLE",
            "lease=FONT",
        ):
            self.assertIn(token, OWNER_TEXT)

        swap = SWAP_SOURCE.read_text(encoding="utf-8")
        self.assertIn("DemoteDefaultFontToFileForTitleLoad()", swap)
        self.assertIn("load=failed", swap)
        self.assertIn("promote=deferred", swap)
        self.assertIn("demote_us=%llu", swap)
        self.assertIn("promote_us=%llu", swap)
        self.assertIn("total_us=%llu", swap)

    def test_shared_arena_survives_stage_guard_and_promotion_failures(self) -> None:
        prepare = OWNER_TEXT[
            OWNER_TEXT.index("bool Th07PspOptionalRamPrepareStage()") :
            OWNER_TEXT.index("bool Th07PspOptionalRamEnterGameplay")
        ]
        self.assertIn("fontBufferBorrowsTitleWorkspace", prepare)
        self.assertIn("sharedArenaGuardPreserved = true", prepare)
        self.assertLess(
            prepare.index("fontBufferBorrowsTitleWorkspace"),
            prepare.index("TextHelper::DemoteDefaultFontToFile()"),
        )
        enter = OWNER_TEXT[
            OWNER_TEXT.index("bool Th07PspOptionalRamEnterGameplay") :
            OWNER_TEXT.index("void Th07PspOptionalRamEndStage")
        ]
        self.assertIn("action=text-off arena=preserved state=font", enter)
        promote = TEXT_HELPER[
            TEXT_HELPER.index("bool TextHelper::PromoteDefaultFontToMainRam()") :
            TEXT_HELPER.index("bool TextHelper::DemoteDefaultFontToFile()")
        ]
        self.assertEqual(promote.count("ReleaseDefaultFontPromotionBuffer(fontData)"), 3)
        release_helper = TEXT_HELPER[
            TEXT_HELPER.index("void ReleaseDefaultFontPromotionBuffer") :
            TEXT_HELPER.index("#endif", TEXT_HELPER.index("void ReleaseDefaultFontPromotionBuffer"))
        ]
        self.assertIn("Th07PspOptionalRamReleaseFontBufferPreserveWorkspace", release_helper)

    def test_font_checkout_refuses_attached_text_prefix(self) -> None:
        acquire = OWNER_TEXT[
            OWNER_TEXT.index("void *Th07PspOptionalRamAcquireFontBuffer") :
            OWNER_TEXT.index("namespace\n{", OWNER_TEXT.index("void *Th07PspOptionalRamAcquireFontBuffer"))
        ]
        self.assertIn("textPoolBorrowsTitleWorkspace", acquire)

    def test_title_workspace_compact_failure_is_fail_closed(self) -> None:
        load = ANM_MANAGER[ANM_MANAGER.index("i32 AnmManager::LoadAnms(") :
                           ANM_MANAGER.index("i32 AnmManager::LoadAnm(")]
        query = load.index("Th07PspOptionalRamIsArchiveWorkspace(entry)")
        reject = load.index("if (sourceUsesSynchronousWorkspace)", query)
        release = load.index("FileSystem::ReleaseFile(sourceBase)", reject)
        failure = load.index("return ZUN_ERROR", release)
        self.assertEqual((query, reject, release, failure),
                         tuple(sorted((query, reject, release, failure))))

    def test_existing_supervisor_retry_calls_register_chain_again(self) -> None:
        case_zero = SUPERVISOR[SUPERVISOR.index("case 0:") : SUPERVISOR.index("case 1:")]
        self.assertGreaterEqual(case_zero.count("MainMenu::RegisterChain()"), 2)
        self.assertIn("menu init failed; trim and retry", case_zero)
        self.assertIn("menu retry failed", case_zero)

    def test_lifecycle_harness_under_asan(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "title_font_hole_swap_harness"
            command = [
                "g++",
                "-std=c++17",
                "-fsanitize=address",
                "-fno-omit-frame-pointer",
                "-I",
                str(ROOT / "src"),
                "-I",
                str(ROOT / "psp"),
                str(HARNESS),
                str(SWAP_SOURCE),
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

    def test_shared_font_title_workspace_harness_under_asan(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "title_font_workspace_harness"
            command = [
                "g++",
                "-std=c++17",
                "-DTH07_PSP",
                "-DTH07_PSP_TITLE_ARCHIVE_WORKSPACE",
                "-DTH07_PSP_TITLE_FONT_HOLE_SWAP",
                "-fsanitize=address",
                "-fno-omit-frame-pointer",
                "-I",
                str(ROOT / "src"),
                "-I",
                str(ROOT / "psp"),
                str(OWNER_HARNESS),
                str(OWNER_SOURCE),
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

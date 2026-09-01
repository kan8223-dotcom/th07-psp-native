from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "Makefile"
TEXT_SOURCE = ROOT / "src" / "TextHelper.cpp"
TEXT_HEADER = ROOT / "src" / "TextHelper.hpp"
GUI_SOURCE = ROOT / "src" / "Gui.cpp"
OWNER_SOURCE = ROOT / "psp" / "optional_ram_budget.cpp"
OWNER_HEADER = ROOT / "psp" / "optional_ram_budget.hpp"
ASCII_SOURCE = ROOT / "src" / "AsciiManager.cpp"
ANM_SOURCE = ROOT / "src" / "AnmManager.cpp"


def function_body(source: str, signature: str, next_signature: str) -> str:
    return source[source.index(signature) : source.index(next_signature)]


class PspFontMainRamProfileTest(unittest.TestCase):
    def setUp(self) -> None:
        self.makefile = MAKEFILE.read_text(encoding="utf-8")

    def test_profile_is_opt_in_and_rejects_psp1000(self) -> None:
        self.assertIn("PSP_FONT_MAIN_RAM ?= 0", self.makefile)
        self.assertIn("ifeq ($(PSP_FONT_MAIN_RAM),1)", self.makefile)
        self.assertIn(
            "PSP_FONT_MAIN_RAM is a PSP-2000+ validation profile only",
            self.makefile,
        )
        self.assertIn("-DTH07_PSP_FONT_MAIN_RAM", self.makefile)

    def test_profile_participates_in_stamp_and_recursive_builds_stay_off(self) -> None:
        stamp = self.makefile[self.makefile.index("PROFILE_STAMP :=") :
                              self.makefile.index(".PHONY: FORCE_PROFILE")]
        self.assertIn("$(PSP_FONT_MAIN_RAM)", stamp)
        self.assertEqual(self.makefile.count("PSP_FONT_MAIN_RAM=0"), 5)

    def test_i3_source_and_flag_are_independent(self) -> None:
        self.assertNotIn("TH07_PSP_FONT_MAIN_RAM", ASCII_SOURCE.read_text(encoding="utf-8"))
        self.assertNotIn("TH07_PSP_FONT_MAIN_RAM", ANM_SOURCE.read_text(encoding="utf-8"))
        self.assertIn("PSP_ASCII_POPUP_BATCH ?= 0", self.makefile)
        self.assertIn("$(PSP_ASCII_POPUP_BATCH)", self.makefile)


class PspFontMainRamOwnershipTest(unittest.TestCase):
    def setUp(self) -> None:
        self.text = TEXT_SOURCE.read_text(encoding="utf-8")
        self.owner = OWNER_SOURCE.read_text(encoding="utf-8")

    def test_process_owner_allocates_and_text_helper_only_borrows(self) -> None:
        owner_header = OWNER_HEADER.read_text(encoding="utf-8")
        text_header = TEXT_HEADER.read_text(encoding="utf-8")
        self.assertIn("Th07PspOptionalRamAcquireFontBuffer(std::size_t bytes)", owner_header)
        self.assertIn("Th07PspOptionalRamReleaseFontBuffer(const void *borrowedBuffer)",
                      owner_header)
        self.assertIn("Process-lifetime", owner_header)
        self.assertIn("static bool PromoteDefaultFontToMainRam();", text_header)
        self.assertIn("static bool DemoteDefaultFontToFile();", text_header)

        acquire = function_body(
            self.owner,
            "void *Th07PspOptionalRamAcquireFontBuffer(std::size_t bytes)",
            "void Th07PspOptionalRamReleaseFontBuffer(",
        )
        self.assertIn("std::malloc(bytes)", acquire)
        promote = function_body(
            self.text,
            "bool TextHelper::PromoteDefaultFontToMainRam()",
            "bool TextHelper::DemoteDefaultFontToFile()",
        )
        self.assertIn("Th07PspOptionalRamAcquireFontBuffer", promote)
        self.assertNotIn("malloc(", promote)
        self.assertNotIn("SDL_LoadFile", promote)

    def test_stage_teardown_does_not_free_process_font(self) -> None:
        end_stage = function_body(
            self.owner,
            "void Th07PspOptionalRamEndStage()",
            "#else",
        )
        self.assertNotIn("ReleaseFontBuffer", end_stage)
        self.assertNotIn("g_ProcessOptionalRam", end_stage)

    def test_font_close_precedes_rw_close_and_owner_free(self) -> None:
        release = function_body(
            self.text,
            "void TextHelper::ReleaseTextBuffer()",
            "void TextHelper::RenderTextToTextureBold(",
        )
        self.assertLess(release.index("TTF_CloseFont(g_Font)"),
                        release.index("ReleaseDefaultFontMainRamBacking()"))
        self.assertLess(release.index("ReleaseDefaultFontMainRamBacking()"),
                        release.index("TTF_Quit()"))

        backing = function_body(
            self.text,
            "void ReleaseDefaultFontMainRamBacking()",
            "u32 AlignDownStageTextCache(",
        )
        self.assertLess(backing.index("SDL_RWclose(g_DefaultFontMainRamStream)"),
                        backing.index("Th07PspOptionalRamReleaseFontBuffer"))
        self.assertIn("g_DefaultFontMainRamData = nullptr", backing)
        self.assertIn("g_DefaultFontMainRamBytes = 0", backing)

    def test_font_replacement_invalidates_resettable_size_owner(self) -> None:
        self.assertIn("static i32 g_CurrentFontSize = 0", self.text)
        self.assertIn("static TTF_Font *g_CurrentFontSizeOwner = nullptr", self.text)
        reset = function_body(
            self.text,
            "void ResetDefaultFontRuntimeTracking()",
            "void ReportDefaultFontMainRamFailureOnce(",
        )
        self.assertIn("ResetFontSizeTracking()", reset)

        for signature, next_signature in (
            ("bool TextHelper::PromoteDefaultFontToMainRam()",
             "bool TextHelper::DemoteDefaultFontToFile()"),
            ("bool TextHelper::DemoteDefaultFontToFile()",
             "bool TextHelper::IsDefaultFontInMainRam()"),
            ("bool TextHelper::DemoteDefaultFontToFileForTitleLoad()",
             "// stolen from"),
        ):
            body = function_body(self.text, signature, next_signature)
            self.assertIn("ResetDefaultFontRuntimeTracking()", body)

        release = function_body(
            self.text,
            "void TextHelper::ReleaseTextBuffer()",
            "void TextHelper::RenderTextToTextureBold(",
        )
        self.assertIn("ResetFontSizeTracking()", release)


class PspFontMainRamPathTest(unittest.TestCase):
    def setUp(self) -> None:
        self.text = TEXT_SOURCE.read_text(encoding="utf-8")
        self.owner = OWNER_SOURCE.read_text(encoding="utf-8")

    def test_existing_font_preference_and_exact_path_fallback_are_preserved(self) -> None:
        open_font = function_body(
            self.text,
            "static TTF_Font *OpenDefaultFont()",
            "bool TextHelper::PromoteDefaultFontToMainRam()",
        )
        self.assertLess(open_font.index('"msgothic.ttc"'),
                        open_font.index('"NotoSansJP-Regular.ttf"'))
        self.assertIn("if (font)", open_font)
        self.assertIn("RememberDefaultFontPath(resolvedPath)", open_font)

        promote = function_body(
            self.text,
            "bool TextHelper::PromoteDefaultFontToMainRam()",
            "bool TextHelper::DemoteDefaultFontToFile()",
        )
        demote = function_body(
            self.text,
            "bool TextHelper::DemoteDefaultFontToFile()",
            "bool TextHelper::IsDefaultFontInMainRam()",
        )
        self.assertIn('SDL_RWFromFile(g_DefaultFontPath, "rb")', promote)
        self.assertIn("TTF_OpenFont(g_DefaultFontPath, 10)", demote)
        self.assertNotIn("NotoSansJP", promote + demote)

    def test_full_file_is_validated_before_memory_font_publish(self) -> None:
        promote = function_body(
            self.text,
            "bool TextHelper::PromoteDefaultFontToMainRam()",
            "bool TextHelper::DemoteDefaultFontToFile()",
        )
        for token in (
            "SDL_RWsize(fileStream)",
            "fileBytes <= 0",
            "SDL_RWseek(fileStream, 0, RW_SEEK_SET)",
            "SDL_RWread(fileStream, fontData, 1u",
            "bytesRead != static_cast<std::size_t>(fileBytes)",
            "SDL_RWFromConstMem(fontData, static_cast<int>(fileBytes))",
            "TTF_OpenFontRW(memoryStream, 0, 10)",
            "TTF_SetFontStyle(memoryFont, TTF_STYLE_BOLD)",
        ):
            self.assertIn(token, promote)
        self.assertIn("kDefaultFontMaxMainRamBytes = 8u * 1024u * 1024u", self.text)
        self.assertLess(promote.index("TTF_OpenFontRW(memoryStream, 0, 10)"),
                        promote.index("TTF_CloseFont(g_Font)"))

    def test_startup_only_promotion_and_no_gameplay_io(self) -> None:
        create = function_body(
            self.text,
            "ZunResult TextHelper::CreateTextBuffer()",
            "void TextHelper::ReleaseTextBuffer()",
        )
        positions = (
            create.index("TTF_Init()"),
            create.index("OpenDefaultFont()"),
            create.index("TTF_SetFontStyle(g_Font, TTF_STYLE_BOLD)"),
            create.index("g_TextWorkBuffer.AllocateBuffer(1024, 64)"),
            create.index("PromoteDefaultFontToMainRam()"),
            create.index("TTF_RenderUTF8_Blended"),
        )
        self.assertEqual(positions, tuple(sorted(positions)))

        render = function_body(
            self.text,
            "void TextHelper::RenderTextToTextureBold(",
            "i32 TextHelper::GetLogicalStringWidth(",
        )
        for forbidden in ("PromoteDefaultFontToMainRam", "SDL_RWread", "SDL_RWFromFile"):
            self.assertNotIn(forbidden, render)

    def test_stage_admission_can_demote_and_retry_once(self) -> None:
        prepare = function_body(
            self.owner,
            "bool Th07PspOptionalRamPrepareStage()",
            "bool Th07PspOptionalRamEnterGameplay(",
        )
        first_try = prepare.index("TryPrepareStageAllocations()")
        self.assertIn("firstAttempt == STAGE_ALLOCATION_GUARD_FAILED", prepare)
        in_ram = prepare.index("TextHelper::IsDefaultFontInMainRam()")
        release_guard = prepare.index("ReleaseGuard()", in_ram)
        demote = prepare.index("TextHelper::DemoteDefaultFontToFile()", release_guard)
        retry = prepare.index("TryPrepareStageAllocations()", demote)
        self.assertEqual((first_try, in_ram, release_guard, demote, retry),
                         tuple(sorted((first_try, in_ram, release_guard, demote, retry))))
        self.assertNotIn("sceKernelTotalFreeMemSize", prepare)
        self.assertNotIn("sceKernelMaxFreeMemSize", prepare)

    def test_psp1000_and_flag_off_paths_are_noops(self) -> None:
        for signature, next_signature in (
            ("bool TextHelper::PromoteDefaultFontToMainRam()",
             "bool TextHelper::DemoteDefaultFontToFile()"),
            ("bool TextHelper::DemoteDefaultFontToFile()",
             "bool TextHelper::IsDefaultFontInMainRam()"),
        ):
            body = function_body(self.text, signature, next_signature)
            self.assertIn(
                "#if defined(TH07_PSP) && defined(TH07_PSP_FONT_MAIN_RAM) && !defined(TH07_PSP_1000)",
                body,
            )
            self.assertIn("#else\n    return false;", body)

    def test_stage_prewarm_log_has_one_low_cost_total_timer(self) -> None:
        gui = GUI_SOURCE.read_text(encoding="utf-8")
        prewarm = function_body(gui, "bool Gui::PreRenderStageText()", "void Gui::MsgRead(")
        self.assertEqual(prewarm.count("sceKernelGetSystemTimeWide()"), 2)
        self.assertIn("prewarmStartUs", prewarm)
        self.assertIn("bomb %u ms %u", prewarm)


if __name__ == "__main__":
    unittest.main()

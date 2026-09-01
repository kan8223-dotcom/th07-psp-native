from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT / "psp" / "optional_ram_budget.cpp"
OWNER_HEADER = ROOT / "psp" / "optional_ram_budget.hpp"
FILESYSTEM = ROOT / "src" / "FileSystem.cpp"
ANM_SOURCE = ROOT / "src" / "AnmManager.cpp"
PBG4_SOURCE = ROOT / "src" / "pbg4" / "Pbg4Archive.cpp"
GAME_SOURCE = ROOT / "src" / "GameManager.cpp"
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
HARNESS = ROOT / "tests" / "psp_title_workspace_harness.cpp"
LZSS_HARNESS = ROOT / "tests" / "lzss_stream_harness.cpp"


class PspTitleWorkspaceContractTest(unittest.TestCase):
    def test_workspace_is_an_isolated_psp2000plus_profile(self) -> None:
        self.assertIn("PSP_TITLE_ARCHIVE_WORKSPACE ?= 0", MAKEFILE)
        self.assertIn("PSP_TITLE_ARCHIVE_WORKSPACE must be 0 or 1", MAKEFILE)
        self.assertIn("PSP_TITLE_ARCHIVE_WORKSPACE is PSP-2000+ only", MAKEFILE)
        self.assertIn("PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT ?= 0", MAKEFILE)
        self.assertIn(
            "PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT requires PSP_TITLE_ARCHIVE_WORKSPACE=1",
            MAKEFILE,
        )
        stamp = next(
            line for line in MAKEFILE.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_TITLE_ARCHIVE_WORKSPACE)", stamp)
        self.assertIn("$(PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT)", stamp)
        self.assertIn("psp3000-rid30-a6-title-workspace-build:", MAKEFILE)
        self.assertIn(
            "psp3000-rid30-a6v2-title-transient-workspace-build:", MAKEFILE
        )
        self.assertIn("PSP_RID30_AB_ME_TITLE_WORKSPACE=1", MAKEFILE)
        self.assertIn("PSP_RID30_AB_ME_TITLE_TRANSIENT=1", MAKEFILE)
        self.assertIn("PSP_RID30_AB_ME_BUILD_ID=0x260901a7u", MAKEFILE)

    def test_filesystem_routes_only_title_and_face_anms(self) -> None:
        source = FILESYSTEM.read_text(encoding="utf-8")
        self.assertIn("TH07_PSP_TITLE_ARCHIVE_WORKSPACE", source)
        self.assertIn('strcmp(filename, "title01.anm") == 0', source)
        acquire = source.index("Th07PspOptionalRamAcquireTitleArchive(fsize)")
        fallback = source.index("buf = (u8 *)malloc(fsize)", acquire)
        decode = source.index("g_Pbg4Archive.ReadDecompressEntry(filename, buf)", fallback)
        self.assertLess(acquire, fallback)
        self.assertLess(fallback, decode)
        release = source.index("Th07PspOptionalRamReleaseArchiveWorkspace(buffer)")
        libc_free = source.index("free(buffer)", release)
        self.assertLess(release, libc_free)
        self.assertIn('std::strncmp(filename, "face_", 5u) != 0', source)
        self.assertIn('std::strcmp(filename + length - 4u, ".anm") == 0', source)
        transient = source.index("Th07PspOptionalRamAcquireTransientArchive(fsize)")
        self.assertLess(transient, fallback)
        self.assertIn("Th07PspOptionalRamReleaseArchiveWorkspace(buffer)", source)

    def test_transient_source_cannot_escape_loadanms(self) -> None:
        source = ANM_SOURCE.read_text(encoding="utf-8")
        load = source[source.index("i32 AnmManager::LoadAnms(") :
                      source.index("i32 AnmManager::LoadAnm(")]
        query = load.index("Th07PspOptionalRamIsTransientArchive(entry)")
        compact = load.index("u8 *compactBase", query)
        release = load.index("FileSystem::ReleaseFile(sourceBase)", compact)
        reject = load.index("ANM WORKSPACE REJECT", release)
        reject_release = load.index("FileSystem::ReleaseFile(sourceBase)", reject)
        reject_return = load.index("return ZUN_ERROR", reject_release)
        self.assertEqual(
            (query, compact, release, reject, reject_release, reject_return),
            tuple(sorted((query, compact, release, reject, reject_release, reject_return))),
        )

    def test_workspace_is_process_lifetime_and_stage_cache_borrows_it(self) -> None:
        owner = OWNER.read_text(encoding="utf-8")
        header = OWNER_HEADER.read_text(encoding="utf-8")
        for token in (
            "Th07PspOptionalRamAcquireTitleArchive(std::size_t bytes)",
            "Th07PspOptionalRamAcquireTransientArchive(std::size_t bytes)",
            "Th07PspOptionalRamIsTransientArchive(const void *borrowedBuffer)",
            "Th07PspOptionalRamIsArchiveWorkspace(const void *borrowedBuffer)",
            "Th07PspOptionalRamReleaseArchiveWorkspace(const void *borrowedBuffer)",
        ):
            self.assertIn(token, header)
            self.assertIn(token, owner)
        self.assertIn("textPoolBorrowsTitleWorkspace", owner)
        self.assertIn("bytes <= g_ProcessOptionalRam.titleWorkspaceBytes", owner)
        self.assertIn(
            "TextHelper::AttachStageTextCache(g_ProcessOptionalRam.titleWorkspace, bytes)",
            owner,
        )
        self.assertIn("if (!g_OptionalRamBudget.textPoolBorrowsTitleWorkspace)", owner)
        self.assertIn("ARCHIVE_WORKSPACE_TRANSIENT", owner)
        self.assertIn("bytes > g_ProcessOptionalRam.titleWorkspaceBytes", owner)
        self.assertIn("return g_ProcessOptionalRam.titleWorkspace", owner)

    def test_face_loans_finish_before_stage_text_borrows_prefix(self) -> None:
        source = GAME_SOURCE.read_text(encoding="utf-8")
        added = source[source.index("ZunResult GameManager::AddedCallback(") :
                       source.index("ZunResult GameManager::RegisterChain(")]
        gui = added.index("Gui::RegisterChain()")
        prepare = added.index("Th07PspOptionalRamPrepareStage()")
        self.assertLess(gui, prepare)

    def test_workspace_output_uses_streaming_archive_decoder(self) -> None:
        source = PBG4_SOURCE.read_text(encoding="utf-8")
        query = source.index("Th07PspOptionalRamIsArchiveWorkspace(buf)")
        stream = source.index("Lzss::DecompressFile", query)
        compressed_malloc = source.index("srcBuf = (u8 *)malloc(dwBytes)", stream)
        self.assertLess(query, stream)
        self.assertLess(stream, compressed_malloc)

    def test_decode_and_noncompact_failures_release_the_workspace(self) -> None:
        filesystem = FILESYSTEM.read_text(encoding="utf-8")
        decode_failure = filesystem.index("if (!g_Pbg4Archive.ReadDecompressEntry")
        decode_release = filesystem.index("FileSystem::ReleaseFile(buf)", decode_failure)
        decode_return = filesystem.index("return NULL", decode_release)
        self.assertLess(decode_failure, decode_release)
        self.assertLess(decode_release, decode_return)

        source = ANM_SOURCE.read_text(encoding="utf-8")
        load = source[source.index("i32 AnmManager::LoadAnms(") :
                      source.index("i32 AnmManager::LoadAnm(")]
        reject = load.index("if (sourceUsesSynchronousWorkspace)")
        reject_release = load.index("FileSystem::ReleaseFile(sourceBase)", reject)
        reject_return = load.index("return ZUN_ERROR", reject_release)
        self.assertLess(reject, reject_release)
        self.assertLess(reject_release, reject_return)

    def test_psp1000_and_non_psp_keep_strict_fallback_stubs(self) -> None:
        owner = OWNER.read_text(encoding="utf-8")
        fallback = owner[owner.index("#else", owner.index("Th07PspOptionalRamEndStage")) :]
        self.assertIn(
            "void *Th07PspOptionalRamAcquireTitleArchive(std::size_t)\n{\n    return nullptr;",
            fallback,
        )
        self.assertIn(
            "bool Th07PspOptionalRamReleaseArchiveWorkspace(const void *)\n{\n    return false;",
            fallback,
        )
        self.assertIn(
            "void *Th07PspOptionalRamAcquireTransientArchive(std::size_t)\n{\n    return nullptr;",
            fallback,
        )
        self.assertIn(
            "bool Th07PspOptionalRamIsTransientArchive(const void *)\n{\n    return false;",
            fallback,
        )
        self.assertIn(
            "bool Th07PspOptionalRamIsArchiveWorkspace(const void *)\n{\n    return false;",
            fallback,
        )

    def test_owner_lifecycle_harness_under_asan(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "title_workspace_harness"
            command = [
                "g++",
                "-std=c++17",
                "-DTH07_PSP",
                "-DTH07_PSP_TITLE_ARCHIVE_WORKSPACE",
                "-DTH07_PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT",
                "-fsanitize=address",
                "-fno-omit-frame-pointer",
                "-I",
                str(ROOT / "src"),
                "-I",
                str(ROOT / "psp"),
                str(HARNESS),
                str(OWNER),
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

    def test_stream_decoder_matches_memory_decoder_and_rejects_truncation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "lzss_stream_harness"
            command = [
                "g++",
                "-std=c++17",
                "-fsanitize=address",
                "-fno-omit-frame-pointer",
                "-I",
                str(ROOT / "src"),
                str(LZSS_HARNESS),
                str(ROOT / "src" / "pbg4" / "Lzss.cpp"),
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

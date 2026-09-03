from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def target_recipe(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(r"\n(?=[A-Za-z0-9_.-]+(?:\s+[^\n:]*)?:)", makefile[start + 1 :])
    return makefile[start:] if match is None else makefile[start : start + 1 + match.start()]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise AssertionError(f"unterminated function: {signature}")


class PspGoBootJitterDiagnosticContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.fileio = (ROOT / "psp/fileio.cpp").read_text(encoding="utf-8")
        cls.fileio_h = (ROOT / "psp/fileio.hpp").read_text(encoding="utf-8")
        cls.supervisor = (ROOT / "src/Supervisor.cpp").read_text(encoding="utf-8")
        cls.sound = (ROOT / "psp/SoundPlayerPsp.cpp").read_text(encoding="utf-8")
        cls.text = (ROOT / "src/TextHelper.cpp").read_text(encoding="utf-8")
        cls.archive = (ROOT / "src/pbg4/Pbg4File.cpp").read_text(encoding="utf-8")

    def test_increment_is_default_off_profile_stamped_and_go_me1_only(self) -> None:
        self.assertIn("PSP_GO_BOOT_JITTER_DIAG ?= 0", self.makefile)
        self.assertIn("-DTH07_PSP_GO_BOOT_JITTER_DIAG", self.makefile)
        stamp = next(
            line for line in self.makefile.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_GO_BOOT_JITTER_DIAG)", stamp)
        for gate in (
            "requires PSP_SLIMPLUS_ME_GATE=1",
            "requires the GO-ME1 AUDIO4M contract",
            "requires PSP_LOCAL_FONT_SUBSET=1",
        ):
            self.assertIn(gate, self.makefile)

        normal = target_recipe(self.makefile, "pspgo-me1-slimplus-build")
        diag = target_recipe(self.makefile, "pspgo-me1-boot-jitter-diag-build")
        self.assertNotIn("PSP_GO_BOOT_JITTER_DIAG", normal)
        self.assertIn("PSP_GO_BOOT_JITTER_DIAG=1", diag)
        for contract in (
            "PSP_SLIMPLUS_ME_GATE=1",
            "PSP_1000=0",
            "PSP_SHIKIGAMI=1",
            "PSP_MECC_AUDIO_4M=1",
            "PSP_RID30_AB_ME_CLOCK_CALIBRATION=0",
            "PSP_RID30_AB_ME_BUILD_ID=0x260901adu",
            "PSP_RID30_AB_ME_TITLE='TH07 PSP v0.2.1-beta'",
            "psp3000-rid30-ab-me-build",
        ):
            self.assertIn(contract, diag)

    def test_exact_slow_boundary_is_bracketed_in_ram(self) -> None:
        added = function_body(
            self.supervisor, "ZunResult Supervisor::AddedCallback"
        )
        begin = added.index("th07_psp_boot_jitter_begin()")
        release = added.index("g_AnmManager->ReleaseSurface(0);", begin)
        input_phase = added.index("TH07_PSP_BOOT_JITTER_INPUT", release)
        sfx_phase = added.index("TH07_PSP_BOOT_JITTER_SFX", input_phase)
        sfx = added.index("g_SoundPlayer.InitSoundBuffers()", sfx_phase)
        anm_phase = added.index("TH07_PSP_BOOT_JITTER_ANM", sfx)
        text = added.index("TextHelper::CreateTextBuffer()", anm_phase)
        self.assertEqual(
            (begin, release, input_phase, sfx_phase, sfx, anm_phase, text),
            tuple(sorted((begin, release, input_phase, sfx_phase, sfx, anm_phase, text))),
        )

    def test_logger_reuse_and_fallback_io_are_measured_but_report_is_outside_sample(self) -> None:
        note = function_body(self.fileio, 'extern "C" void th07_psp_boot_note')
        self.assertIn("BootJitterLogCallScope bootJitterLogCall", note)
        for operation, marker in (
            ("LockBootLog()", "gBootJitter.logLock"),
            ("OpenBootLogAppend()", "gBootJitter.logOpen"),
            ("WriteAvailable(fd, message", "gBootJitter.logWrite"),
            ("CloseBootLog()", "gBootJitter.logClose"),
        ):
            self.assertIn(operation, note)
            self.assertIn(marker, note)
        self.assertIn("const bool bootLogOpenAttempt", note)
        self.assertIn("if (!writeOk)", note)
        self.assertNotIn("sceIoOpen(gBootLog", note)
        self.assertNotIn("sceIoClose(fd)", note)
        self.assertGreaterEqual(note.count("sceKernelGetSystemTimeWide()"), 6)

        finish = function_body(
            self.fileio, 'extern "C" void th07_psp_boot_jitter_finish'
        )
        stop = finish.index("__atomic_store_n(&gBootJitter.active, false")
        report = finish.index("th07_psp_boot_note(report)")
        self.assertLess(stop, report)
        self.assertEqual(finish.count("th07_psp_boot_note("), 1)
        self.assertIn('"BOOTJIT V1 US', finish)
        self.assertIn(
            '"LOG N%u T%u/%u@%c L%u/%u@%c O%u/%u@%c W%u/%u@%c C%u/%u@%c"',
            finish,
        )

    def test_sfx_archive_and_pcm_work_have_per_buffer_attribution(self) -> None:
        scope = self.sound[
            self.sound.index("struct BootJitterSfxScope") :
            self.sound.index("#endif", self.sound.index("struct BootJitterSfxScope"))
        ]
        self.assertIn("archiveEndUs - beginUs", scope)
        self.assertIn("endUs - archiveEndUs", scope)
        load = function_body(self.sound, "ZunResult SoundPlayer::LoadSound")
        self.assertLess(
            load.index("BootJitterSfxScope"),
            load.index("FileSystem::OpenFile(path, 0)"),
        )
        self.assertGreater(
            load.index("bootJitterSfx.ArchiveReady()"),
            load.index("FileSystem::OpenFile(path, 0)"),
        )

        for api, enum_name in (
            ("fopen(local_114", "ARCHIVE_OPEN"),
            ("fread(data, 1, len", "ARCHIVE_READ"),
            ("fseek(this->file, offset", "ARCHIVE_SEEK"),
            ("ftell(this->file)", "ARCHIVE_META"),
            ("fclose(this->file)", "ARCHIVE_CLOSE"),
        ):
            self.assertIn(api, self.archive)
            self.assertIn(enum_name, self.archive)
        get_size = function_body(self.archive, "u32 Pbg4File::GetSize")
        tell = function_body(self.archive, "u32 Pbg4File::Tell")
        self.assertIn("TH07_PSP_BOOT_JITTER_ARCHIVE_META", get_size)
        self.assertIn("TH07_PSP_BOOT_JITTER_ARCHIVE_META", tell)

    def test_font_candidate_open_read_and_coverage_are_separate(self) -> None:
        candidate = function_body(self.text, "TTF_Font *OpenCoverageCheckedFont")
        opened = candidate.index('SDL_RWFromFile(resolvedPath, "rb")')
        read = candidate.index("TTF_OpenFontIndexRW(fontStream, 1", opened)
        coverage = candidate.index("FontProvidesStockCoverage", read)
        record = candidate.index("th07_psp_boot_jitter_record_font", read)
        self.assertLess(opened, read)
        self.assertLess(read, coverage)
        self.assertIn("openUs", candidate)
        self.assertIn("readUs", candidate)
        self.assertIn("coverageUs", candidate)
        self.assertGreaterEqual(candidate.count("th07_psp_boot_jitter_record_font"), 3)
        self.assertGreater(record, read)
        self.assertIn("freesrc=1 preserves TTF_OpenFontIndex's ownership contract", candidate)

    def test_probe_does_not_invent_absent_wait_categories(self) -> None:
        measured = self.supervisor[
            self.supervisor.index("th07_psp_boot_jitter_begin()") :
            self.supervisor.index("g_SoundPlayer.LoadFmt", self.supervisor.index(
                "th07_psp_boot_jitter_begin()"
            ))
        ]
        diagnostic = "\n".join((
            function_body(self.fileio, 'extern "C" void th07_psp_boot_jitter_begin'),
            function_body(self.fileio, 'extern "C" void th07_psp_boot_jitter_advance'),
            function_body(self.fileio, 'extern "C" void th07_psp_boot_jitter_finish'),
            measured,
        ))
        for absent in ("sceDisplayWaitVblank", "scePower", "sceKernelDelayThread"):
            self.assertNotIn(absent, diagnostic)
        self.assertNotIn("MEWAIT", diagnostic)
        self.assertNotIn("PWRWAIT", diagnostic)


if __name__ == "__main__":
    unittest.main()

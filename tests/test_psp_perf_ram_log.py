import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class PspPerfRamLogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fileio = (ROOT / "psp/fileio.cpp").read_text()
        cls.fileio_header = (ROOT / "psp/fileio.hpp").read_text()
        cls.graphics = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text()
        cls.game = (ROOT / "src/GameManager.cpp").read_text()
        cls.main = (ROOT / "src/main.cpp").read_text()

    def test_perf_buffer_is_diagnostic_only_and_fixed_size(self):
        self.assertIn("#if defined(TH07_PSP_PERF_DIAG)", self.fileio)
        self.assertIn("kPerfLogBufferBytes = 128u * 1024u", self.fileio)
        self.assertIn("kPerfLogBufferBytes = 160u * 1024u", self.fileio)
        self.assertIn("kPerfLogBufferBytes = 512u * 1024u", self.fileio)
        self.assertIn("char gPerfLogBuffer[kPerfLogBufferBytes]", self.fileio)

    def test_bulk_flush_and_worker_notes_share_the_proven_semaphore(self):
        init = function_body(self.fileio, 'extern "C" void th07_psp_fileio_init')
        note = function_body(self.fileio, 'extern "C" void th07_psp_boot_note')
        flush = function_body(self.fileio, 'extern "C" void th07_psp_perf_log_flush')
        shutdown = function_body(self.fileio, 'extern "C" void th07_psp_fileio_shutdown')
        self.assertIn('sceKernelCreateSema("th07_boot_log"', init)
        self.assertIn("LockBootLog()", note)
        self.assertIn("UnlockBootLog(bootLogLocked)", note)
        self.assertIn("LockBootLog()", flush)
        self.assertIn("UnlockBootLog(bootLogLocked)", flush)
        self.assertIn("sceKernelDeleteSema(bootLogSema)", shutdown)

    def test_perf_note_only_appends_to_ram_in_diagnostic_build(self):
        body = function_body(self.fileio, 'extern "C" void th07_psp_perf_note')
        diagnostic = body.split("#else", 1)[0]
        self.assertIn("std::memcpy(gPerfLogBuffer", diagnostic)
        self.assertIn("LockBootLog()", diagnostic)
        self.assertIn("UnlockBootLog(perfLogLocked)", diagnostic)
        self.assertNotIn("sceIoOpen", diagnostic)
        self.assertNotIn("sceIoWrite", diagnostic)

    def test_gameplay_boot_notes_are_redirected_to_serialized_ram_log(self):
        body = function_body(self.fileio, 'extern "C" void th07_psp_boot_note')
        self.assertIn("__atomic_load_n(&gPerfGameplayActive", body)
        self.assertIn("th07_psp_perf_note(message);", body)
        self.assertLess(
            body.index("th07_psp_perf_note(message);"),
            body.index("SceUID fd = gBootLogFd;"),
        )
        self.assertIn("th07_psp_perf_set_gameplay_active(1)", self.graphics)
        self.assertIn("th07_psp_perf_set_gameplay_active(0)", self.graphics)

    def test_non_1000_diagnostic_stage_load_notes_use_the_same_ram_log(self):
        body = function_body(self.fileio, 'extern "C" void th07_psp_boot_note')
        self.assertIn("__atomic_load_n(&gPerfStageLoadActive", body)
        stage_redirect = body.index("__atomic_load_n(&gPerfStageLoadActive")
        synchronous_write = body.index("SceUID fd = gBootLogFd;")
        self.assertLess(stage_redirect, synchronous_write)
        self.assertIn(
            "th07_psp_perf_note(message);", body[stage_redirect:synchronous_write]
        )
        self.assertIn(
            "#if defined(TH07_PSP_PERF_DIAG) && !defined(TH07_PSP_1000)\n"
            'extern "C" void th07_psp_perf_set_stage_load_active(int active)',
            self.fileio,
        )
        self.assertIn(
            "#if defined(TH07_PSP_PERF_DIAG) && !defined(TH07_PSP_1000)\n"
            'extern "C" void th07_psp_perf_set_stage_load_active(int active);',
            self.fileio_header,
        )

    def test_stage_load_scope_is_all_exit_safe_and_does_not_move_bgm_gate(self):
        added = function_body(self.game, "ZunResult GameManager::AddedCallback")
        scope = self.game.index("class PspStageLoadRamLogScope")
        constructor = self.game.index("th07_psp_perf_set_stage_load_active(1)", scope)
        destructor = self.game.index("th07_psp_perf_set_stage_load_active(0)", constructor)
        self.assertLess(constructor, destructor)
        self.assertIn(
            "#if defined(TH07_PSP_PERF_DIAG) && !defined(TH07_PSP_1000)\n"
            "namespace",
            self.game,
        )
        self.assertIn("PspStageLoadRamLogScope stageLoadRamLogScope;", added)
        self.assertLess(
            added.index("PspStageLoadRamLogScope stageLoadRamLogScope;"),
            added.index('th07_psp_boot_note("game added begin")'),
        )
        self.assertNotIn("th07_psp_perf_log_flush()", added)
        self.assertEqual(added.count("SetBgmStageLoadBlocked("), 2)
        self.assertLess(
            added.index("SetBgmStageLoadBlocked(true)"),
            added.index("SetBgmStageLoadBlocked(false)"),
        )

    def test_perf_lines_have_profile_run_and_window_identity(self):
        body = function_body(self.fileio, 'extern "C" void th07_psp_perf_note')
        self.assertIn('"PERF PF%s RID%08X W%u %s"', body)
        self.assertIn("PerfProfileToken()", body)

    def test_renderer_routes_all_periodic_lines_to_ram(self):
        perf = function_body(self.graphics, "void ReportPerfWindow")
        self.assertGreaterEqual(perf.count("th07_psp_perf_note("), 1)
        self.assertIn("PERF ACCEPT", perf)
        self.assertIn("PERF DRAW P00MM", perf)
        self.assertIn("PERF M3 BU", perf)
        self.assertNotIn("th07_psp_boot_note(message)", perf)
        self.assertNotIn("th07_psp_boot_note(drawMessage)", perf)
        self.assertNotIn("th07_psp_boot_note(gpuMessage)", perf)

    def test_flush_occurs_after_gameplay_and_at_app_stop(self):
        deleted = function_body(self.game, "ZunResult GameManager::DeletedCallback")
        self.assertLess(deleted.index("th07_psp_perf_log_flush()"),
                        deleted.index('th07_psp_heap_note("game delete begin")'))
        stop = self.main.index("stop:")
        release = self.main.index("g_SoundPlayer.Release();", stop)
        flush = self.main.index("th07_psp_perf_log_flush();", stop)
        self.assertLess(flush, release)

    def test_flush_reuses_process_fd_and_retains_partial_data(self):
        body = function_body(self.fileio, 'extern "C" void th07_psp_perf_log_flush')
        self.assertNotIn("sceIoOpen(", body)
        self.assertNotIn("sceIoClose(", body)
        self.assertEqual(body.count("OpenBootLogAppend()"), 1)
        self.assertEqual(body.count("CloseBootLog()"), 1)
        self.assertIn("WriteAvailable(fd, gPerfLogBuffer, bufferedBytes)", body)
        self.assertIn("std::memmove(gPerfLogBuffer", body)
        self.assertIn("PERF PROFILE INVALID OVERFLOW %u LINES", body)
        self.assertIn("PERF END VALID=%u DROP=%u", body)


if __name__ == "__main__":
    unittest.main()

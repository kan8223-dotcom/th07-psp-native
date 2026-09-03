from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FILEIO = ROOT / "psp" / "fileio.cpp"
MAIN = ROOT / "src" / "main.cpp"


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
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def braced_statement(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated braced statement: {marker}")


def assert_ordered(
    test: unittest.TestCase, source: str, needles: tuple[str, ...]
) -> None:
    cursor = 0
    for needle in needles:
        position = source.find(needle, cursor)
        test.assertNotEqual(position, -1, f"missing or out of order: {needle}")
        cursor = position + len(needle)


class PspPersistentBootLogContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = FILEIO.read_text(encoding="utf-8")
        cls.main = MAIN.read_text(encoding="utf-8")
        cls.init = function_body(cls.source, 'extern "C" void th07_psp_fileio_init')
        cls.note = function_body(cls.source, 'extern "C" void th07_psp_boot_note')
        cls.flush = function_body(
            cls.source, 'extern "C" void th07_psp_perf_log_flush'
        )
        cls.open_append = function_body(cls.source, "SceUID OpenBootLogAppend")
        cls.close = function_body(cls.source, "void CloseBootLog")
        cls.shutdown = function_body(
            cls.source, 'extern "C" void th07_psp_fileio_shutdown'
        )

    def test_initial_truncate_descriptor_is_process_lifetime_state(self) -> None:
        self.assertRegex(
            self.source,
            r"(?m)^SceUID\s+gBootLogFd\s*=\s*-1\s*;$",
        )
        open_start = self.init.index("gBootLogFd = sceIoOpen(")
        open_end = self.init.index(");", open_start)
        initial_open = self.init[open_start : open_end + 2]
        for flag in (
            "PSP_O_WRONLY",
            "PSP_O_CREAT",
            "PSP_O_TRUNC",
            "PSP_O_APPEND",
        ):
            with self.subTest(flag=flag):
                self.assertIn(flag, initial_open)
        self.assertIn("gBootLog", initial_open)
        self.assertNotIn("sceIoClose", self.init)
        self.assertNotIn("CloseBootLog()", self.init)

        # Descriptor lifetime is a normal PSP file-I/O contract, not enabled
        # by the Go timing observer or PERF_DIAG.
        declarations = self.source[
            self.source.index("char gBootLog[") :
            self.source.index("#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)")
        ]
        self.assertIn("SceUID gBootLogFd = -1;", declarations)
        self.assertNotIn("#if", declarations)
        self.assertLess(open_end, self.init.index("#if defined(TH07_PSP_PERF_DIAG)"))

    def test_append_reopen_only_runs_after_invalid_or_missing_fd(self) -> None:
        assert_ordered(
            self,
            self.open_append,
            (
                "if (gBootLogFd >= 0 || gBootLogShutdown)",
                "return gBootLogFd;",
                "gBootLogFd =",
                "sceIoOpen(gBootLog, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND",
                "return gBootLogFd;",
            ),
        )
        self.assertNotIn("PSP_O_TRUNC", self.open_append)
        self.assertNotIn("#if", self.open_append)

        self.assertEqual(self.close.count("sceIoClose(gBootLogFd)"), 1)
        assert_ordered(
            self,
            self.close,
            (
                "if (gBootLogFd >= 0)",
                "sceIoClose(gBootLogFd);",
                "gBootLogFd = -1;",
            ),
        )
        self.assertNotIn("#if", self.close)

    def test_boot_note_reuses_fd_and_invalidates_only_on_write_failure(self) -> None:
        self.assertNotIn("sceIoOpen", self.note)
        self.assertNotIn("sceIoClose", self.note)
        self.assertEqual(self.note.count("OpenBootLogAppend()"), 1)
        self.assertEqual(self.note.count("CloseBootLog()"), 1)
        self.assertGreaterEqual(self.note.count("WriteAvailable(fd,"), 3)
        assert_ordered(
            self,
            self.note,
            (
                "SceUID fd = gBootLogFd;",
                "if (fd < 0)",
                "fd = OpenBootLogAppend();",
                "const bool writeOk =",
                "WriteAvailable(fd, stamp",
                "WriteAvailable(fd, message",
                'WriteAvailable(fd, "\\n"',
                "if (!writeOk)",
                "CloseBootLog();",
            ),
        )

    def test_perf_flush_reuses_fd_and_preserves_retryable_suffix(self) -> None:
        self.assertNotIn("sceIoOpen", self.flush)
        self.assertNotIn("sceIoClose", self.flush)
        self.assertEqual(self.flush.count("OpenBootLogAppend()"), 1)
        self.assertEqual(self.flush.count("CloseBootLog()"), 1)
        assert_ordered(
            self,
            self.flush,
            (
                "const SceUID fd = OpenBootLogAppend();",
                "const std::size_t bufferedBytes = gPerfLogUsed;",
                "const std::size_t written = WriteAvailable(",
                "bool logIoOk = written == bufferedBytes;",
                "std::memmove(gPerfLogBuffer",
                "gPerfLogUsed = remaining;",
                "if (!logIoOk)",
                "CloseBootLog();",
            ),
        )
        self.assertIn("logIoOk = false;", self.flush)

    def test_perf_diag_writes_hold_one_shared_semaphore(self) -> None:
        for name, writer, write_marker in (
            ("boot note", self.note, "SceUID fd = gBootLogFd;"),
            ("PERF flush", self.flush, "const SceUID fd = OpenBootLogAppend();"),
        ):
            with self.subTest(writer=name):
                lock = writer.index("LockBootLog()")
                write = writer.index(write_marker, lock)
                unlock = writer.rindex("UnlockBootLog(")
                self.assertLess(lock, write)
                self.assertLess(write, unlock)
                self.assertIn("gBootLogSema >= 0", writer[:write])

        declaration = self.source.index("SceUID gBootLogSema = -1;")
        prefix = self.source[
            self.source.index("unsigned int gDataCandidateCount;") : declaration
        ]
        self.assertNotIn("#if", prefix)
        self.assertIn('sceKernelCreateSema("th07_boot_log"', self.init)

    def test_shutdown_closes_once_while_locked_before_semaphore_delete(self) -> None:
        sema_live = braced_statement(self.shutdown, "if (gBootLogSema >= 0)")
        locked = braced_statement(sema_live, "if (LockBootLog())")
        self.assertEqual(locked.count("CloseBootLog()"), 1)
        assert_ordered(
            self,
            locked,
            (
                "gBootLogShutdown = true;",
                "CloseBootLog();",
                "const SceUID bootLogSema = gBootLogSema;",
                "gBootLogSema = -1;",
                "sceKernelDeleteSema(bootLogSema);",
            ),
        )
        self.assertEqual(locked.count("sceKernelDeleteSema("), 1)
        self.assertNotIn("sceIoClose", self.shutdown)
        self.assertIn('th07_psp_boot_note("main exited");', self.main)
        exit_note = self.main.index('th07_psp_boot_note("main exited");')
        shutdown = self.main.index("th07_psp_fileio_shutdown();", exit_note)
        psp_end = self.main.index("#endif", shutdown)
        self.assertLess(shutdown, psp_end)
        self.assertNotIn("TH07_PSP_PERF_DIAG", self.main[exit_note:shutdown])

        # The idempotent helper makes the non-PERF and missing-semaphore
        # branches close at most once as well.
        self.assertEqual(self.close.count("sceIoClose(gBootLogFd)"), 1)


if __name__ == "__main__":
    unittest.main()

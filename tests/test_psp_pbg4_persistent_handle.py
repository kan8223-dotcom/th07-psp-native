from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_SOURCE = ROOT / "src" / "pbg4" / "Pbg4Archive.cpp"
FILE_SOURCE = ROOT / "src" / "pbg4" / "Pbg4File.cpp"


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
    """Return the braced statement whose condition starts at ``marker``."""

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


def exact_ifdef_branches(source: str, macro: str) -> tuple[str, str]:
    """Extract the true/false arms of an exact ``#if defined(MACRO)``."""

    lines = source.splitlines(keepends=True)
    opener = re.compile(rf"^\s*#if\s+defined\({re.escape(macro)}\)\s*$")
    directive = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b")
    try:
        start = next(index for index, line in enumerate(lines) if opener.match(line))
    except StopIteration as error:
        raise AssertionError(f"missing #if defined({macro})") from error

    depth = 0
    alternative = None
    end = None
    for index in range(start, len(lines)):
        match = directive.match(lines[index])
        if match is None:
            continue
        kind = match.group(1)
        if kind in ("if", "ifdef", "ifndef"):
            depth += 1
        elif kind == "else" and depth == 1:
            alternative = index
        elif kind == "elif" and depth == 1:
            raise AssertionError(f"unexpected #elif in {macro} contract")
        elif kind == "endif":
            depth -= 1
            if depth == 0:
                end = index
                break

    if alternative is None or end is None:
        raise AssertionError(f"{macro} contract must have explicit #else/#endif")
    return (
        "".join(lines[start + 1 : alternative]),
        "".join(lines[alternative + 1 : end]),
    )


def assert_checked_absolute_seek(test: unittest.TestCase, source: str) -> int:
    seek = "this->fileAbstraction->Seek(entry->dataOffset, g_SeekModes[0])"
    position = source.find(seek)
    test.assertNotEqual(position, -1, "decode path lacks absolute entry seek")

    condition_start = source.rfind("if", 0, position)
    test.assertNotEqual(condition_start, -1, "entry seek is not failure-checked")
    condition = source[condition_start : position + len(seek) + 2]
    test.assertRegex(
        condition,
        r"if\s*\(\s*!\s*this->fileAbstraction->Seek\(\s*"
        r"entry->dataOffset\s*,\s*g_SeekModes\[0\]\s*\)\s*\)",
    )
    failure = braced_statement(source[condition_start:], "if")
    test.assertIn("goto err;", failure)
    return position


class PspPbg4PersistentHandleContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        archive = ARCHIVE_SOURCE.read_text(encoding="utf-8")
        cls.decode = function_body(
            archive, "u8 *Pbg4Archive::ReadDecompressEntry"
        )
        cls.file_source = FILE_SOURCE.read_text(encoding="utf-8")

    def test_psp_reuses_live_handle_and_opens_only_null_fallback(self) -> None:
        setup = self.decode[: self.decode.index("dwBytes =")]
        psp, non_psp = exact_ifdef_branches(setup, "TH07_PSP")
        open_call = (
            r"this->fileAbstraction->Open\(\s*this->filename\s*,\s*"
            r"g_AccessModes\[0\]\s*\)"
        )
        null_handle = (
            r"(?:!\s*this->fileAbstraction->file|"
            r"this->fileAbstraction->file\s*==\s*(?:NULL|nullptr))"
        )
        open_failed = rf"(?:!\s*{open_call}|{open_call}\s*==\s*(?:0|false))"

        self.assertEqual(len(re.findall(open_call, psp)), 1)
        self.assertRegex(
            psp,
            rf"if\s*\(\s*{null_handle}\s*&&\s*{open_failed}\s*\)",
        )

        # Desktop behavior deliberately stays stock: close/reopen through Open
        # on every extraction, with no live-handle shortcut.
        self.assertEqual(len(re.findall(open_call, non_psp)), 1)
        self.assertNotRegex(non_psp, null_handle)
        self.assertRegex(non_psp, rf"if\s*\(\s*{open_failed}\s*\)")
        # Both preprocessor-selected conditions own the same failure body.
        self.assertRegex(setup, r"#endif\s*\{\s*goto err;\s*\}")

    def test_reuse_is_default_on_for_all_psp_models(self) -> None:
        setup = self.decode[: self.decode.index("dwBytes =")]
        psp, _ = exact_ifdef_branches(setup, "TH07_PSP")
        directives = [
            line.strip()
            for line in setup.splitlines()
            if line.lstrip().startswith("#")
        ]
        self.assertEqual(directives, ["#if defined(TH07_PSP)", "#else", "#endif"])
        for forbidden in (
            "BOOT_JITTER",
            "PSP_GO",
            "SLIMPLUS",
            "PSP_MODEL",
            "MECC",
            "MERW",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, psp)

    def test_stream_and_general_decode_paths_reset_absolute_position(self) -> None:
        psp1000 = braced_statement(
            self.decode, "if (th07_psp_1000_arena_owns(buf))"
        )
        title_workspace = braced_statement(
            self.decode,
            "if (Th07PspOptionalRamIsArchiveWorkspace(buf))",
        )
        general_start = self.decode.index("srcBuf = (u8 *)malloc(dwBytes)")
        general_end = self.decode.index("return dstBuf;", general_start)
        general = self.decode[general_start : general_end + len("return dstBuf;")]

        for name, path in (
            ("PSP1000 arena stream", psp1000),
            ("title-workspace stream", title_workspace),
        ):
            with self.subTest(path=name):
                seek = assert_checked_absolute_seek(self, path)
                decompress = path.index("Lzss::DecompressFile(")
                self.assertLess(seek, decompress)

        seek = assert_checked_absolute_seek(self, general)
        read = general.index("this->fileAbstraction->Read(srcBuf, dwBytes)")
        decompress = general.index("Lzss::Decompress(srcBuf, dwBytes, buf, dstLen)")
        self.assertLess(seek, read)
        self.assertLess(read, decompress)

    def test_seek_mode_zero_is_absolute(self) -> None:
        declaration = re.search(
            r"const\s+u32\s+g_SeekModes\s*\[\s*3\s*\]\s*=\s*"
            r"\{(?P<values>[^}]*)\}\s*;",
            self.file_source,
        )
        self.assertIsNotNone(declaration)
        values = [value.strip() for value in declaration.group("values").split(",")]
        self.assertEqual(len(values), 3)
        self.assertRegex(values[0], r"^0(?:[uUlL]*)$")


if __name__ == "__main__":
    unittest.main()

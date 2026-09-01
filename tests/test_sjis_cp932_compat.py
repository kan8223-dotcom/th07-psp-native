from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "tests" / "sjis_cp932_compat_harness.cpp"
CONVERTER = ROOT / "src" / "thirdparty" / "sjis_converter.cpp"


class SjisCp932CompatTest(unittest.TestCase):
    def test_stock_wave_dash_uses_windows_cp932_mapping(self) -> None:
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("host C++ compiler is required")
        with tempfile.TemporaryDirectory(prefix="th07-cp932-") as temporary:
            executable = Path(temporary) / "sjis_cp932_compat"
            subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(HARNESS),
                    str(CONVERTER),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            completed = subprocess.run(
                [str(executable)],
                check=True,
                cwd=ROOT,
                text=True,
                capture_output=True,
            )
        self.assertIn("CP932 wave dash compatibility PASS", completed.stdout)

    def test_subset_authority_already_contains_fullwidth_tilde(self) -> None:
        authority = (ROOT / "src" / "Th07FontCoverage.hpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("0xFF5Eu", authority)


if __name__ == "__main__":
    unittest.main()

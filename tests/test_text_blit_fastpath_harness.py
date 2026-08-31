from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "tests" / "text_blit_fastpath_harness.cpp"


class TextBlitFastPathHarnessTest(unittest.TestCase):
    def test_reference_and_candidate_are_byte_exact(self) -> None:
        compiler = shutil.which("g++")
        sdl_config = shutil.which("sdl2-config")
        if not compiler or not sdl_config:
            self.skipTest("host C++ compiler and SDL2 development package are required")

        compile_flags = subprocess.check_output(
            [sdl_config, "--cflags"], text=True
        ).split()
        link_flags = subprocess.check_output(
            [sdl_config, "--libs"], text=True
        ).split()
        with tempfile.TemporaryDirectory(prefix="th07-text-blit-") as temporary:
            executable = Path(temporary) / "text_blit_fastpath_harness"
            subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    *compile_flags,
                    str(HARNESS),
                    "-o",
                    str(executable),
                    *link_flags,
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
        self.assertIn("110 cases byte-exact", completed.stdout)

    def test_harness_covers_the_production_formats_and_both_outputs(self) -> None:
        source = HARNESS.read_text(encoding="utf-8")
        for token in (
            "SDL_PIXELFORMAT_ARGB8888",
            "SDL_PIXELFORMAT_RGBA32",
            "SDL_BlitSurface",
            "CompositeBoldTextSurfaceExact",
            "RunFallbackCases",
            "RGBA-work",
            "512x16-upload",
            "glyphPitchPadding",
            "workPitchPadding",
            "SDL_SetClipRect",
            "0xffffffffu",
            "DivideBy255",
        ):
            self.assertIn(token, source)


if __name__ == "__main__":
    unittest.main()

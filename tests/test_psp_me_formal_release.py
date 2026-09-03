from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def target_recipe(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(r"\n(?=[A-Za-z0-9_.-]+(?:\s+[^\n:]*)?:)", makefile[start + 1 :])
    return makefile[start:] if match is None else makefile[start : start + 1 + match.start()]


class PspMeFormalReleaseContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.renderer = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text(
            encoding="utf-8"
        )
        cls.meter = (ROOT / "psp/usage_meter.c").read_text(encoding="utf-8")

    def test_formal_target_keeps_me_stack_without_observers(self) -> None:
        recipe = target_recipe(self.makefile, "psp2000plus-me1-formal-build")
        expected = {
            "PSP_ME_FORMAL_RELEASE": "1",
            "PSP_ME_BUSY_METER": "1",
            "PSP_MECC_AUDIO_4M": "1",
            "PSP_SLIMPLUS_ME_GATE": "1",
            "PSP_ME_RENDER_WORKER": "1",
            "PSP_ME_RENDER_GE_CONSUME": "1",
            "PSP_ME_RENDER_PERFORMANCE": "1",
            "PSP_ME_RENDER_DIRECT_LIST": "1",
            "PSP_ME_BULLET_COMPACT_UPDATE": "1",
            "PSP_ME_ITEM_RENDER_STREAM": "1",
            "PSP_ME_ITEM_MOTION_UPDATE": "1",
            "PSP_ME_ADAPTIVE_AUX_RENDER": "1",
            "PSP_ME_ITEM_PREFIX_SPLIT": "1",
            "PSP_PERF_DIAG": "0",
            "PSP_PERF_AB_COMPARE": "0",
            "PSP_PERF_DENSE_SLICE": "0",
            "PSP_SHIKIGAMI": "0",
            "PSP_USAGE_METER": "0",
            "PSP_ME_STARTUP_BREADCRUMBS": "0",
            "PSP_GO_BOOT_JITTER_DIAG": "0",
            "PSP_ME_CLOCK_CALIBRATION": "0",
            "PSP_BULLET_POSITION_SOA_SHADOW": "0",
            "PSP_BULLET_POSITION_SOA_READ": "0",
        }
        for variable, value in expected.items():
            with self.subTest(variable=variable):
                self.assertIn(f"{variable}={value}", recipe)
        for media in ("ICON", "ICON1", "PIC0", "PIC1", "SND0"):
            self.assertIn(f"PSP_EBOOT_{media}=NULL", recipe)

    def test_busy_backend_and_visible_overlay_are_separate(self) -> None:
        self.assertIn(
            "defined(TH07_PSP_ME_BUSY_METER) && !defined(TH07_PSP_PERF_DIAG)",
            self.renderer,
        )
        self.assertIn("th07_usage_meter_frame(0u);", self.renderer)
        self.assertIn(
            "defined(TH07_PSP_USAGE_METER) || defined(TH07_PSP_ME_BUSY_METER)",
            self.meter,
        )
        draw_start = self.renderer.index("th07_usage_meter_draw();")
        draw_guard = self.renderer.rfind("#if defined(TH07_PSP_USAGE_METER)", 0, draw_start)
        self.assertGreaterEqual(draw_guard, 0)

    def test_busy_backend_preserves_unclamped_veto_and_resets_per_frame(self) -> None:
        harness = r'''
#include <stdio.h>
#include "usage_meter.h"
int main(void) {
    th07_usage_meter_add_me_cycles(4170825u); /* 150% of 16.7 ms at 333 MHz. */
    th07_usage_meter_frame(0u);
    printf("%u ", th07_usage_meter_last_me_percent());
    th07_usage_meter_frame(0u);
    printf("%u\n", th07_usage_meter_last_me_percent());
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "meter.c"
            binary = Path(tmp) / "meter"
            source.write_text(harness, encoding="utf-8")
            subprocess.run(
                [
                    "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-DTH07_PSP_ME_BUSY_METER", "-I", str(ROOT / "psp"),
                    str(source), str(ROOT / "psp/usage_meter.c"), "-o", str(binary),
                ],
                check=True,
            )
            output = subprocess.run(
                [str(binary)], check=True, capture_output=True, text=True
            ).stdout.strip()
        first, second = map(int, output.split())
        self.assertGreater(first, 100)
        self.assertEqual(second, 0)


if __name__ == "__main__":
    unittest.main()

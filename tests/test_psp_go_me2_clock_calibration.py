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


def assignments(recipe: str) -> dict[str, str]:
    return dict(re.findall(r"\b([A-Z][A-Z0-9_]*)=([^ \\\n]+|'[^']*')", recipe))


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


class PspGoMe2ClockCalibrationContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")
        cls.header = (ROOT / "psp/audio_me.h").read_text(encoding="utf-8")
        cls.policy = (ROOT / "psp/me_clock_policy.h").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src/BulletManager.cpp").read_text(encoding="utf-8")

    def test_candidate_is_default_off_validated_and_profile_stamped(self) -> None:
        self.assertIn("PSP_ME_CLOCK_CALIBRATION ?= 0", self.makefile)
        self.assertIn("-DTH07_PSP_ME_CLOCK_CALIBRATION", self.makefile)
        stamp = next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_ME_CLOCK_CALIBRATION)", stamp)
        for required in (
            "requires the GO-ME1 Slim+ contract",
            "requires PSP_ME_RENDER_WORKER=1",
            "requires PSP_ME_ADAPTIVE_AUX_RENDER=1",
            "requires PSP_ME_ITEM_PREFIX_SPLIT=1",
            "requires PSP_ME_ITEM_MOTION_UPDATE=1",
            "requires PSP_USAGE_METER=1",
        ):
            self.assertIn(required, self.makefile)

    def test_candidate_preserves_go_me1_contract_except_delta_identity(self) -> None:
        me1 = assignments(target_recipe(self.makefile, "pspgo-me1-slimplus-build"))
        me2 = assignments(
            target_recipe(self.makefile, "pspgo-me2-clock-calibration-build")
        )
        self.assertEqual(me1["PSP_RID30_AB_ME_CLOCK_CALIBRATION"], "0")
        self.assertEqual(me2["PSP_RID30_AB_ME_CLOCK_CALIBRATION"], "1")
        self.assertEqual(me1["PSP_RID30_AB_ME_BUILD_ID"], "0x260901adu")
        self.assertEqual(me2["PSP_RID30_AB_ME_BUILD_ID"], "0x260902b2u")
        ignored = {
            "PSP_RID30_AB_ME_CLOCK_CALIBRATION",
            "PSP_RID30_AB_ME_BUILD_ID",
        }
        self.assertEqual(
            {key: value for key, value in me1.items() if key not in ignored},
            {key: value for key, value in me2.items() if key not in ignored},
        )
        self.assertIn("PSP_RID30_AB_ME_TITLE='TH07 PSP v0.2.1-beta'", target_recipe(
            self.makefile, "pspgo-me2-clock-calibration-build"
        ))

    def test_measurement_is_identical_kernel_wide_wall_and_no_power_api(self) -> None:
        start = self.audio.index("static int me_clock_wall_elapsed")
        end = self.audio.index("static int me_render_bench_dispatch", start)
        calibration = self.audio[start:end]
        self.assertGreaterEqual(calibration.count("sceKernelGetSystemTimeWide()"), 4)
        self.assertIn("me_render_expand_kernel(", calibration)
        self.assertIn("ME_CLOCK_CALIBRATION_REPEATS", calibration)
        self.assertIn("ME_RENDER_JOB_CLOCK_CALIBRATION", calibration)
        self.assertIn("ME_CLOCK_CALIBRATION_SAMPLES", calibration)
        self.assertIn("me_render_bench_compare", calibration)
        self.assertIn("me_render_bench_guards_ok", calibration)
        self.assertNotIn("scePower", calibration)
        self.assertNotIn("GetCpuClock", calibration)

    def test_boot_note_reports_raw_ratio_and_scaled_thresholds(self) -> None:
        self.assertIn("GO-ME2 CAL %s SC%lu/%lu/%lu MERT%lu/%lu/%lu", self.audio)
        self.assertIn("policy.rawRatioPermille", self.audio)
        self.assertIn("policy.meShareQ16", self.audio)
        self.assertIn("policy.admissionBudgetTicks", self.audio)
        self.assertIn("policy.vetoPercent", self.audio)
        self.assertIn("REJECT-UNITY", self.audio)

    def test_spread_reject_is_outlier_tolerant_and_preinit_is_legacy_safe(self) -> None:
        calibration = function_body(self.audio, "static int me_clock_calibrate")
        self.assertIn("scWallUs[ME_CLOCK_CALIBRATION_SAMPLES - 2u]", calibration)
        self.assertIn("scWallUs[1u]", calibration)
        self.assertIn("meWallUs[ME_CLOCK_CALIBRATION_SAMPLES - 2u]", calibration)
        self.assertIn("meWallUs[1u]", calibration)
        admission = function_body(
            self.audio,
            "unsigned int th07_psp_me_clock_admission_budget_ticks",
        )
        veto = function_body(
            self.audio, "unsigned int th07_psp_me_clock_veto_percent"
        )
        self.assertIn("TH07_PSP_ME_CLOCK_LEGACY_BUDGET_TICKS", admission)
        self.assertIn("TH07_PSP_ME_CLOCK_LEGACY_VETO_PERCENT", veto)

    def test_clean_output_mismatch_rejects_to_unity_without_poisoning_me(self) -> None:
        calibration = function_body(self.audio, "static int me_clock_calibrate")
        mismatch = calibration.index(
            "if (gMeClockCalibration.mismatchWords != 0u)"
        )
        reject = calibration[mismatch : calibration.index(
            "me_render_sort_samples", mismatch
        )]
        self.assertIn("result = 0;", reject)
        self.assertNotIn("poison_me();", reject)
        self.assertNotIn("gMeRenderBenchSummary.mismatchWords", reject)

        # Bounds/ownership damage and an in-flight timeout remain fatal; only
        # a completed, bounded calibration result is allowed to degrade.
        guard = calibration[calibration.index("if (!me_render_bench_guards_ok())") : mismatch]
        self.assertIn("poison_me();", guard)
        measure_me = function_body(self.audio, "static int me_clock_measure_me")
        self.assertIn("th07_psp_me_render_hard_fault();", measure_me)
        self.assertIn("return -1;", measure_me)

    def test_every_admission_consumer_uses_runtime_budget_and_veto(self) -> None:
        admission = function_body(
            self.bullets,
            "PspMeAdaptiveAuxAdmission PspMeAdaptiveAuxAdmissionFor",
        )
        prefix = function_body(
            self.bullets, "u32 PspMeAdaptiveItemPrefixCount"
        )
        motion = function_body(
            self.bullets, "u32 PspMeItemMotionCandidateLimitFor"
        )
        for body in (admission, prefix, motion):
            self.assertIn("PspMeAdaptiveBudgetTicksForRuntime()", body)
            self.assertIn("PspMeAdaptiveVetoPercentForRuntime()", body)
            self.assertIn("budgetTicks", body)
            self.assertIn("vetoPercent", body)
        self.assertIn("predicted > budgetTicks", admission)
        self.assertIn("base < budgetTicks", prefix)
        self.assertIn("base >= budgetTicks", motion)

    def test_policy_math_proves_3000_neutrality_and_go_scaling(self) -> None:
        source = r'''
#include <stdio.h>
#include "psp/me_clock_policy.h"
int main(void) {
    Th07PspMeClockPolicy equal = th07_psp_me_clock_make_policy(32000, 32000, 1);
    Th07PspMeClockPolicy low = th07_psp_me_clock_make_policy(32000, 32960, 1);
    Th07PspMeClockPolicy high = th07_psp_me_clock_make_policy(32000, 32992, 1);
    Th07PspMeClockPolicy go = th07_psp_me_clock_make_policy(32000, 36800, 1);
    Th07PspMeClockPolicy faster_me = th07_psp_me_clock_make_policy(36800, 32000, 1);
    Th07PspMeClockPolicy invalid = th07_psp_me_clock_make_policy(0, 0, 0);
    printf("%u %u %u %u %u\n", equal.meShareQ16,
           equal.admissionBudgetTicks, equal.vetoPercent,
           equal.snappedToUnity, equal.rawRatioPermille);
    printf("%u %u\n", low.meShareQ16, high.meShareQ16);
    printf("%u %u %u %u %u\n", go.meShareQ16,
           go.admissionBudgetTicks, go.vetoPercent,
           go.snappedToUnity, go.rawRatioPermille);
    printf("%u %u\n", faster_me.meShareQ16, invalid.meShareQ16);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "policy.c"
            exe = Path(tmp) / "policy"
            src.write_text(source, encoding="utf-8")
            subprocess.run(
                ["cc", "-std=c99", "-Wall", "-Werror", "-I", str(ROOT),
                 str(src), "-o", str(exe)],
                check=True,
                capture_output=True,
                text=True,
            )
            lines = subprocess.run(
                [str(exe)], check=True, capture_output=True, text=True
            ).stdout.splitlines()

        self.assertEqual(lines[0], "65536 2220044 85 1 1000")
        # 1030 permille is still exact unity; 1031 is the first scaled case.
        self.assertEqual(lines[1].split()[0], "65536")
        self.assertLess(int(lines[1].split()[1]), 65536)
        share, budget, veto, snapped, ratio = map(int, lines[2].split())
        self.assertEqual(share, (32000 * 65536) // 36800)
        self.assertEqual(budget, (2220044 * share) >> 16)
        self.assertEqual(veto, (85 * share + 32768) >> 16)
        self.assertEqual((snapped, ratio), (0, 1150))
        self.assertEqual(lines[3], "65536 65536")


if __name__ == "__main__":
    unittest.main()

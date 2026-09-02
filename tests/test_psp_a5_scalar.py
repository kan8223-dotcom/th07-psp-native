from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
AUDIO = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
HELPER = ROOT / "psp" / "sfx_div1_fast.c"
SOUND = (ROOT / "psp" / "SoundPlayerPsp.cpp").read_text(encoding="utf-8")
HARNESS = ROOT / "tests" / "sfx_div1_fast_harness.c"


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


def make_target_body(makefile: str, target: str) -> str:
    match = re.search(rf"(?m)^{re.escape(target)}:\s*.*$", makefile)
    if match is None:
        raise AssertionError(f"missing Make target: {target}")
    following = re.search(
        r"(?m)^[A-Za-z0-9_./%+-]+:\s*.*$", makefile[match.end() :]
    )
    end = len(makefile) if following is None else match.end() + following.start()
    return makefile[match.start() : end]


class PspA5ScalarTests(unittest.TestCase):
    def test_feature_is_default_off_stamped_and_observer_off_gated(self) -> None:
        self.assertRegex(MAKEFILE, r"(?m)^PSP_SFX_DIV1_FAST \?= 0$")
        gate_start = MAKEFILE.index("ifeq ($(PSP_SFX_DIV1_FAST),1)")
        gate_end = MAKEFILE.index(
            "else ifneq ($(PSP_SFX_DIV1_FAST),0)", gate_start
        )
        gate = MAKEFILE[gate_start:gate_end]
        for contract in (
            "PSP_SFX_DIV1_FAST is PSP-3000-only",
            "PSP_SFX_DIV1_FAST requires PSP_MECC_AUDIO_4M=1",
            "PSP_SFX_DIV1_FAST requires PSP_PERF_DIAG=1",
            "PSP_SFX_DIV1_FAST requires PSP_PERF_PROFILE=PERF_ACCEPT",
            "PSP_SFX_DIV1_FAST requires PSP_PERF_AB_COMPARE=1",
            "PSP_SFX_DIV1_FAST must be measured observer-off",
            "CFLAGS += -DTH07_PSP_SFX_DIV1_FAST",
            "CXXFLAGS += -DTH07_PSP_SFX_DIV1_FAST",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, gate)
        stamp = next(
            line for line in MAKEFILE.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_SFX_DIV1_FAST)", stamp)

    def test_control_and_candidate_share_a7_contract_and_one_switch(self) -> None:
        self.assertIn("PSP_RID30_AB_ME_SFX_DIV1_FAST ?= 0", MAKEFILE)
        recursive = make_target_body(MAKEFILE, "psp3000-rid30-ab-me-build")
        self.assertIn(
            "PSP_SFX_DIV1_FAST=$(PSP_RID30_AB_ME_SFX_DIV1_FAST)",
            recursive,
        )
        control = make_target_body(
            MAKEFILE, "psp3000-a7-a5-scalar-control-build"
        )
        candidate = make_target_body(MAKEFILE, "psp3000-a7-a5-scalar-build")
        self.assertIn("PSP_RID30_AB_ME_SFX_DIV1_FAST=0", control)
        self.assertIn("PSP_RID30_AB_ME_SFX_DIV1_FAST=1", candidate)
        for target in (control, candidate):
            self.assertIn("psp3000-a6v4w-stage6-font-tail-fix-build", target)
        self.assertRegex(
            MAKEFILE,
            r"(?s)\.PHONY:.*\bpsp3000-a7-a5-scalar-control-build\b"
            r".*\bpsp3000-a7-a5-scalar-build\b",
        )

    def test_runtime_split_preserves_the_generic_loop(self) -> None:
        body = function_body(AUDIO, "int th07_psp_sc_audio_mix_into(")
        ordered = (
            "const int divisor = job->mixDivisor ? (int)job->mixDivisor : 1;",
            "#if defined(TH07_PSP_SFX_DIV1_FAST)",
            "if (divisor == 1)",
            "th07_psp_sfx_compose_div1(gScWide, io, samples)",
            "return 1;",
            "#endif",
            "for (unsigned int sample = 0; sample < samples; ++sample)",
            "int effect = divisor == 1 ? gScWide[sample] : gScWide[sample] / divisor;",
        )
        cursor = 0
        for token in ordered:
            found = body.find(token, cursor)
            self.assertNotEqual(found, -1, f"missing or out of order: {token}")
            cursor = found + len(token)
        self.assertIn(
            'th07_psp_boot_note("A5 SCALAR DIV1 FAST ON / GENERIC DIV FALLBACK")',
            SOUND,
        )

    def test_actual_helper_is_pcm_exact_at_all_optimizations(self) -> None:
        compiler = shutil.which("gcc")
        if compiler is None:
            self.skipTest("host C compiler is unavailable")
        for optimization in ("-O0", "-O2", "-O3"):
            with self.subTest(optimization=optimization), tempfile.TemporaryDirectory(
                prefix="th07-a5-scalar-"
            ) as temporary:
                executable = Path(temporary) / "sfx-div1-fast"
                build = subprocess.run(
                    [
                        compiler,
                        "-std=c11",
                        optimization,
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-I",
                        str(ROOT),
                        str(HARNESS),
                        str(HELPER),
                        "-o",
                        str(executable),
                    ],
                    cwd=ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(build.returncode, 0, build.stderr)
                run = subprocess.run(
                    [str(executable)], cwd=ROOT, text=True, capture_output=True
                )
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertIn("A5 DIV1 PCM exact: 69634 guarded cases", run.stdout)

    def test_psp_helper_symbol_contains_no_divide_or_hilo_read(self) -> None:
        compiler = shutil.which("psp-gcc")
        objdump = shutil.which("psp-objdump")
        if compiler is None or objdump is None:
            self.skipTest("PSP cross compiler and objdump are unavailable")
        with tempfile.TemporaryDirectory(prefix="th07-a5-scalar-psp-") as temporary:
            object_file = Path(temporary) / "sfx_div1_fast.o"
            build = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-O2",
                    "-G0",
                    "-march=allegrex",
                    "-mtune=allegrex",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT),
                    "-c",
                    str(HELPER),
                    "-o",
                    str(object_file),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            disassembly = subprocess.check_output(
                [objdump, "-dr", str(object_file)], cwd=ROOT, text=True
            )
        match = re.search(
            r"<th07_psp_sfx_compose_div1>:(?P<body>.*?)(?=\n[0-9a-f]+ <|\Z)",
            disassembly,
            re.DOTALL,
        )
        self.assertIsNotNone(match, disassembly)
        helper_body = match.group("body")
        self.assertIsNone(
            re.search(r"\b(?:div|divu|mflo|mfhi)\b", helper_body), helper_body
        )


if __name__ == "__main__":
    unittest.main()

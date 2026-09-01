from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
HEADER = (ROOT / "psp" / "audio_me.h").read_text(encoding="utf-8")
WORKER = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
BULLETS = (ROOT / "src" / "BulletManager.cpp").read_text(encoding="utf-8")
HARNESS = ROOT / "tests" / "psp_bullet_seed_soa_d1_harness.c"

FEATURE = "PSP_ME_BULLET_SEED_SOA"
CPP_FEATURE = "TH07_PSP_ME_BULLET_SEED_SOA"
FIELD_MACRO = "TH07_PSP_ME_BULLET_SEED_FIELD"
IMPLEMENTED = (
    f"{FEATURE} ?=" in MAKEFILE
    and CPP_FEATURE in HEADER
    and FIELD_MACRO in HEADER
)

FIELDS = (
    "generation",
    "posXBits",
    "posYBits",
    "posZBits",
    "velocityXBits",
    "velocityYBits",
    "velocityZBits",
    "spriteWidthBits",
    "spriteHeightBits",
    "grazeSizeXBits",
    "grazeSizeYBits",
    "nextPosXBits",
    "nextPosYBits",
    "nextPosZBits",
)

BASE_DEFINES = (
    "TH07_PSP_ME_RENDER_WORKER",
    "TH07_PSP_ME_RENDER_CORRECTNESS",
    "TH07_PSP_ME_RENDER_RAW_LIVE",
    "TH07_PSP_ME_RENDER_DIRECT_LIST",
    "TH07_PSP_ME_RENDER_PERFORMANCE",
    "TH07_PSP_ME_BULLET_COMPACT_UPDATE",
    "TH07_PSP_ME_ITEM_RENDER_STREAM",
    "TH07_PSP_ME_ADAPTIVE_AUX_RENDER",
    "TH07_PSP_ME_ITEM_PREFIX_SPLIT",
    "TH07_PSP_ME_ITEM_MOTION_UPDATE",
)


def recipe_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    tail = makefile[start + len(target) + 1 :]
    match = re.search(r"^[A-Za-z0-9_.-]+:", tail, re.MULTILINE)
    return (
        makefile[start:]
        if match is None
        else makefile[start : start + len(target) + 1 + match.start()]
    )


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


def compile_and_run(
    source: Path | str, defines: tuple[str, ...], *, c_source: bool = True
) -> subprocess.CompletedProcess[str]:
    compiler = shutil.which("gcc" if c_source else "g++")
    if compiler is None:
        raise unittest.SkipTest("host compiler is unavailable")
    with tempfile.TemporaryDirectory() as temp:
        output = Path(temp) / "test"
        if isinstance(source, Path):
            source_path = source
        else:
            source_path = Path(temp) / ("test.c" if c_source else "test.cpp")
            source_path.write_text(textwrap.dedent(source), encoding="utf-8")
        build = subprocess.run(
            [
                compiler,
                "-std=c11" if c_source else "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                *(f"-D{name}" for name in (*BASE_DEFINES, *defines)),
                "-I",
                str(ROOT),
                str(source_path),
                "-o",
                str(output),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if build.returncode != 0:
            raise AssertionError(build.stderr)
        return subprocess.run(
            [str(output)], check=False, capture_output=True, text=True
        )


@unittest.skipUnless(IMPLEMENTED, "D1 Bullet seed SoA implementation has not landed")
class PspBulletSeedSoaD1ProfileTests(unittest.TestCase):
    def test_define_is_default_off_validated_stamped_and_compiled_for_both_cpus(self) -> None:
        self.assertIn(f"{FEATURE} ?= 0", MAKEFILE)
        stamp = next(
            line for line in MAKEFILE.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({FEATURE})", stamp)
        self.assertRegex(MAKEFILE, rf"filter-out 0 1,\$\({FEATURE}\)")
        self.assertGreaterEqual(MAKEFILE.count(f"-D{CPP_FEATURE}"), 2)

    def test_build_guards_are_compact_only_mutually_exclusive_and_non_1000(self) -> None:
        self.assertIn("D1 SoA", MAKEFILE)
        self.assertIn("requires PSP_ME_BULLET_COMPACT_UPDATE=1", MAKEFILE)
        self.assertIn("mutually exclusive", MAKEFILE)
        self.assertIn("PSP_ME_BULLET_SEED_SLIM", MAKEFILE)
        self.assertIn("PSP-2000+", MAKEFILE)
        self.assertIn("D1 SoA", HEADER)
        self.assertIn("TH07_PSP_ME_BULLET_COMPACT_UPDATE", HEADER)
        self.assertIn("TH07_PSP_ME_BULLET_SEED_SLIM", HEADER)
        self.assertIn("TH07_PSP_1000", HEADER)

    def test_d1a_and_d1b_are_isolated_rid30_me_profiles(self) -> None:
        targets = {
            "psp3000-rid30-ab-me-d1a-soa-build": "0",
            "psp3000-rid30-ab-me-d1b-soa-build": "1",
        }
        forced_off = (
            "PSP_ME_RENDER_UV16",
            "PSP_ME_RENDER_XYZ16",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT",
            "PSP_ME_BULLET_OUTPUT_SLIM",
            "PSP_ME_BULLET_SEED_SLIM",
            "PSP_ME_ITEM_SEED_SLIM",
            "PSP_ME_EFFECT_RENDER_STREAM",
        )
        for target, trusted in targets.items():
            with self.subTest(target=target):
                body = recipe_body(MAKEFILE, target)
                self.assertIn(f"{FEATURE}=1", body)
                self.assertIn("PSP_1000=0", body)
                self.assertIn(
                    f"PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY={trusted}", body
                )
                for feature in forced_off:
                    self.assertIn(f"{feature}=0", body)


@unittest.skipUnless(IMPLEMENTED, "D1 Bullet seed SoA implementation has not landed")
class PspBulletSeedSoaD1AbiTests(unittest.TestCase):
    def test_legacy_c2b_and_soa_versions_are_exact_and_unique(self) -> None:
        program = r"""
            #include <stdio.h>
            #include "psp/audio_me.h"
            int main(void) {
                printf("%08x %08x\n",
                       (unsigned)TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION,
                       (unsigned)TH07_PSP_ME_BULLET_COMPACT_VERSION);
                return 0;
            }
        """
        variants = {
            "legacy": (),
            "c2b": ("TH07_PSP_ME_BULLET_SEED_SLIM",),
            "soa": (CPP_FEATURE,),
        }
        observed: dict[str, tuple[int, int]] = {}
        for name, defines in variants.items():
            completed = compile_and_run(program, defines)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            observed[name] = tuple(
                int(word, 16) for word in completed.stdout.strip().split()
            )
        self.assertEqual(observed["legacy"][0], 0x42533131)
        self.assertEqual(observed["c2b"][0], 0x42533132)
        self.assertEqual(observed["soa"][0], 0x42533133)
        self.assertEqual(len(set(observed.values())), 3)
        self.assertEqual(
            observed["soa"][1] - observed["legacy"][1], 0x00000800
        )

    def test_soa_has_two_bitmaps_then_fourteen_full_u32_planes(self) -> None:
        offsets = ",\n                       ".join(
            "(size_t)((unsigned char *)&"
            f"{FIELD_MACRO}(&seed, 0u, {field}) - "
            "(unsigned char *)&seed)"
            for field in FIELDS
        )
        program = f"""
            #include <stddef.h>
            #include <stdio.h>
            #include "psp/audio_me.h"
            int main(void) {{
                Th07PspMeBulletCompactSeed seed;
                printf("%zu %zu %zu "
                       "{(' %zu' * len(FIELDS)).strip()}\\n",
                       sizeof(Th07PspMeBulletCompactSeed),
                       offsetof(Th07PspMeBulletCompactSeed, candidateBits),
                       offsetof(Th07PspMeBulletCompactSeed, inBoundsBits),
                       {offsets});
                return 0;
            }}
        """
        completed = compile_and_run(program, (CPP_FEATURE,))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        values = [int(value) for value in completed.stdout.split()]
        self.assertEqual(values[0], 58560)
        self.assertEqual(values[1:3], [64, 192])
        self.assertEqual(values[3:], [320 + 4160 * index for index in range(14)])
        self.assertTrue(
            all(
                (values[index + 1] - values[index]) == 4160
                for index in range(3, len(values) - 1)
            )
        )
        self.assertEqual(values[0] % 64, 0)

    def test_field_macro_is_an_lvalue_for_all_three_seed_layouts(self) -> None:
        program = r"""
            #include <stdlib.h>
            #include "psp/audio_me.h"
            int main(void) {
                Th07PspMeBulletCompactSeed *seed = calloc(1, sizeof(*seed));
                if (!seed) return 2;
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, 1023u, generation) =
                    0x12345678u;
                if (TH07_PSP_ME_BULLET_SEED_FIELD(
                        seed, 1023u, generation) != 0x12345678u)
                    return 3;
                free(seed);
                return 0;
            }
        """
        for defines in (
            (),
            ("TH07_PSP_ME_BULLET_SEED_SLIM",),
            (CPP_FEATURE,),
        ):
            with self.subTest(defines=defines):
                completed = compile_and_run(program, defines)
                self.assertEqual(completed.returncode, 0, completed.stderr)


@unittest.skipUnless(IMPLEMENTED, "D1 Bullet seed SoA implementation has not landed")
class PspBulletSeedSoaD1TransposeTests(unittest.TestCase):
    def test_transpose_is_bit_exact_at_all_required_densities_and_edges(self) -> None:
        completed = compile_and_run(HARNESS, (CPP_FEATURE,))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(
            completed.stdout.strip(),
            "D1 SoA transpose: 5 counts, sparse reuse, 14 planes, slot1023, finite edges",
        )

    def test_producer_worker_and_both_sc_consumers_use_the_shared_field_macro(self) -> None:
        bodies = (
            function_body(WORKER, "static void me_bullet_compact_capture_seed("),
            function_body(WORKER, "static uint32_t me_bullet_compact_update_kernel("),
            function_body(WORKER, "static int selftest_bullet_compact_update(void)"),
            function_body(BULLETS, "bool PspMeBulletCompactTryAdoptSeed("),
            function_body(BULLETS, "bool PspMeBulletCompactTryAdoptTrustedSeed("),
        )
        for body in bodies:
            with self.subTest(signature=body[:80]):
                self.assertIn(FIELD_MACRO, body)

    def test_soa_seed_clear_clears_both_bitmaps_and_all_fourteen_fields(self) -> None:
        body = function_body(
            WORKER, "static void me_bullet_compact_clear_seed_slot("
        )
        self.assertIn("candidateBits", body)
        self.assertIn("inBoundsBits", body)
        for field in FIELDS:
            with self.subTest(field=field):
                self.assertIn(f"{FIELD_MACRO}(seed, slot, {field})", body)


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import re
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
GRAPHICS = (ROOT / "psp" / "graphics" / "PspGuGraphics.cpp").read_text(
    encoding="utf-8"
)
GRAPHICS_H = (ROOT / "psp" / "graphics" / "PspGuGraphics.hpp").read_text(
    encoding="utf-8"
)
IMPLEMENTED = "PSP_ME_RENDER_UV16 ?=" in MAKEFILE


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


@unittest.skipUnless(IMPLEMENTED, "C1 implementation has not landed")
class PspMeVertex16BitProfileTests(unittest.TestCase):
    def test_independent_switches_are_default_off_and_in_profile_stamp(self) -> None:
        for feature in (
            "PSP_ME_RENDER_UV16",
            "PSP_ME_RENDER_XYZ16",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT",
        ):
            with self.subTest(feature=feature):
                self.assertIn(f"{feature} ?= 0", MAKEFILE)
                stamp = next(
                    line
                    for line in MAKEFILE.splitlines()
                    if line.startswith("PROFILE_STAMP :=")
                )
                self.assertIn(f"$({feature})", stamp)

        self.assertIn("-DTH07_PSP_ME_RENDER_UV16", MAKEFILE)
        self.assertIn("-DTH07_PSP_ME_RENDER_XYZ16", MAKEFILE)
        self.assertIn("-DTH07_PSP_ME_RENDER_16BIT_GE_EXPERIMENT", MAKEFILE)

    def test_packed_profiles_are_research_only_and_fail_closed_at_build_time(self) -> None:
        block_start = MAKEFILE.index("# C1 research profiles")
        block_end = MAKEFILE.index("# I-ME8 extends", block_start)
        block = MAKEFILE[block_start:block_end]
        for required in (
            "PSP_ME_RENDER_CORRECTNESS",
            "PSP_ME_RENDER_WORKER",
            "PSP_1000",
            "PSP_ME_RENDER_GE_CONSUME",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT",
        ):
            self.assertIn(required, block)

        self.assertIn("C1 packed vertices are PSP-2000+ research only", HEADER)
        self.assertIn(
            "C1 packed GE consumption requires the explicit readback experiment gate",
            HEADER,
        )

    def test_accepted_rid30_target_does_not_silently_enable_c1(self) -> None:
        target = "psp3000-a1-item-motion-build"
        if f"{target}:" not in MAKEFILE:
            self.fail(f"fixed RID30 target missing: {target}")
        recipe = recipe_body(MAKEFILE, target)
        for feature in (
            "PSP_ME_RENDER_UV16",
            "PSP_ME_RENDER_XYZ16",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT",
        ):
            self.assertIn(f"{feature}=0", recipe)

    def test_stream_versions_are_distinct_for_all_four_abis(self) -> None:
        for expected in ("0x00000100u", "0x00000200u", "0x00000300u"):
            self.assertIn(expected, HEADER)
        self.assertIn(
            "0x4d453131u + TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS",
            HEADER,
        )

    def test_three_m0_targets_are_distinct_and_share_the_rid30_profile(self) -> None:
        targets = {
            "psp3000-c1-uv16-m0-build": ("1", "0", "0x260831c1u"),
            "psp3000-c1-xyz16-m0-build": ("0", "1", "0x260831c2u"),
            "psp3000-c1-uvxyz16-m0-build": ("1", "1", "0x260831c3u"),
        }
        for target, (uv, xyz, build_id) in targets.items():
            with self.subTest(target=target):
                self.assertIn(f"{target}: PSP_C1_BUILD_UV16={uv}", MAKEFILE)
                self.assertIn(f"{target}: PSP_C1_BUILD_XYZ16={xyz}", MAKEFILE)
                self.assertIn(f"{target}: PSP_C1_BUILD_ID={build_id}", MAKEFILE)
                self.assertIn(
                    f"{target}: psp3000-c1-vertex16-m0-build", MAKEFILE
                )

        baseline = dict(
            re.findall(
                r"\b(PSP_[A-Z0-9_]+)=([^\s\\]+)",
                recipe_body(MAKEFILE, "psp3000-a1-item-motion-build"),
            )
        )
        candidate = dict(
            re.findall(
                r"\b(PSP_[A-Z0-9_]+)=([^\s\\]+)",
                recipe_body(MAKEFILE, "psp3000-c1-vertex16-m0-build"),
            )
        )
        allowed_differences = {
            "PSP_AUDIO4M_BUILD_ID",
            "PSP_EBOOT_TITLE",
            "PSP_ME_RENDER_UV16",
            "PSP_ME_RENDER_XYZ16",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT",
        }
        differences = {
            key
            for key in set(baseline) | set(candidate)
            if baseline.get(key) != candidate.get(key)
        }
        self.assertEqual(differences, allowed_differences)
        self.assertEqual(candidate["PSP_1000"], "0")

    def test_m0_is_explicitly_not_a_ge_readback_pass(self) -> None:
        self.assertIn("static const uint32_t counts[4] = {0u, 128u, 512u, 1024u}", WORKER)
        self.assertIn(
            '"MERW C1M0 PASS CASES4 GE0 READBACK-PENDING"', WORKER
        )


@unittest.skipUnless(IMPLEMENTED, "C1 implementation has not landed")
class PspMeVertex16BitAbiTests(unittest.TestCase):
    HARNESS = r"""
        #include <stddef.h>
        #include <stdio.h>
        #include "psp/audio_me.h"

        int main(void)
        {
        #if defined(TH07_PSP_ME_RENDER_UV16)
            _Static_assert(
                _Generic(((Th07PspMeRenderStreamVertex *)0)->u,
                         unsigned short: 1, default: 0),
                "GU_TEXTURE_16BIT payload must be unsigned u16");
            _Static_assert(
                _Generic(((Th07PspMeRenderStreamVertex *)0)->v,
                         unsigned short: 1, default: 0),
                "GU_TEXTURE_16BIT payload must be unsigned u16");
            const size_t u = offsetof(Th07PspMeRenderStreamVertex, u);
            const size_t v = offsetof(Th07PspMeRenderStreamVertex, v);
        #else
            const size_t u = offsetof(Th07PspMeRenderStreamVertex, uBits);
            const size_t v = offsetof(Th07PspMeRenderStreamVertex, vBits);
        #endif
        #if defined(TH07_PSP_ME_RENDER_XYZ16)
            _Static_assert(
                _Generic(((Th07PspMeRenderStreamVertex *)0)->x,
                         short: 1, default: 0),
                "GU_VERTEX_16BIT payload must be signed s16");
            _Static_assert(
                _Generic(((Th07PspMeRenderStreamVertex *)0)->z,
                         short: 1, default: 0),
                "GU_VERTEX_16BIT payload must be signed s16");
            const size_t x = offsetof(Th07PspMeRenderStreamVertex, x);
            const size_t y = offsetof(Th07PspMeRenderStreamVertex, y);
            const size_t z = offsetof(Th07PspMeRenderStreamVertex, z);
            const size_t tail = offsetof(Th07PspMeRenderStreamVertex, reserved);
        #else
            const size_t x = offsetof(Th07PspMeRenderStreamVertex, xBits);
            const size_t y = offsetof(Th07PspMeRenderStreamVertex, yBits);
            const size_t z = offsetof(Th07PspMeRenderStreamVertex, zBits);
            const size_t tail = sizeof(Th07PspMeRenderStreamVertex);
        #endif
            printf("%zu %d %zu %zu %zu %zu %zu %zu %zu\n",
                   sizeof(Th07PspMeRenderStreamVertex),
                   TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES,
                   u, v, offsetof(Th07PspMeRenderStreamVertex, color),
                   x, y, z, tail);
            return 0;
        }
    """

    def test_host_compiler_observes_exact_four_layouts(self) -> None:
        cases = {
            "off": ([], (24, 24, 0, 4, 8, 12, 16, 20, 24)),
            "uv16": (
                ["-DTH07_PSP_ME_RENDER_UV16"],
                (20, 20, 0, 2, 4, 8, 12, 16, 20),
            ),
            "xyz16": (
                ["-DTH07_PSP_ME_RENDER_XYZ16"],
                (20, 20, 0, 4, 8, 12, 14, 16, 18),
            ),
            "both": (
                ["-DTH07_PSP_ME_RENDER_UV16", "-DTH07_PSP_ME_RENDER_XYZ16"],
                (16, 16, 0, 2, 4, 8, 10, 12, 14),
            ),
        }
        with tempfile.TemporaryDirectory(prefix="th07-c1-abi-") as tmp:
            source = Path(tmp) / "abi.c"
            source.write_text(textwrap.dedent(self.HARNESS), encoding="utf-8")
            for name, (defines, expected) in cases.items():
                binary = Path(tmp) / name
                command = [
                    "gcc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DTH07_PSP_ME_RENDER_WORKER",
                    "-DTH07_PSP_ME_RENDER_CORRECTNESS",
                    *defines,
                    "-I",
                    str(ROOT),
                    str(source),
                    "-o",
                    str(binary),
                ]
                with self.subTest(profile=name):
                    subprocess.run(command, check=True, capture_output=True, text=True)
                    output = subprocess.run(
                        [str(binary)], check=True, capture_output=True, text=True
                    ).stdout
                    self.assertEqual(tuple(map(int, output.split())), expected)

    def test_color_stays_rgba8888_and_max_saving_is_one_third(self) -> None:
        self.assertRegex(
            HEADER,
            r"typedef struct Th07PspMeRenderStreamVertex\s*\{(?s:.*?)"
            r"unsigned int color;",
        )
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES = 16", HEADER)
        self.assertNotIn("TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES = 12", HEADER)

    def test_sc_and_me_translation_units_lock_four_byte_alignment(self) -> None:
        self.assertIn(
            "_Alignof(Th07PspMeRenderStreamVertex) == 4u", WORKER
        )
        self.assertIn(
            "alignof(Th07PspMeRenderStreamVertex) == 4u", BULLETS
        )


@unittest.skipUnless(IMPLEMENTED, "C1 implementation has not landed")
class PspMeVertex16BitPackingTests(unittest.TestCase):
    def test_me_and_independent_sc_packers_match_signed_and_unsigned_oracles(
        self,
    ) -> None:
        me_s16_body = function_body(WORKER, "me_render_stream_pack_s16(")
        me_u16_body = function_body(WORKER, "me_render_stream_pack_u16(")
        sc_s16_body = function_body(
            BULLETS, "bool PspMeRenderPackS16Reference("
        )
        sc_u16_body = function_body(
            BULLETS, "bool PspMeRenderPackU16Reference("
        )
        harness = f"""
            #include <cmath>
            #include <cstdint>
            #include <cstdio>
            #include <cstdlib>
            using i32 = std::int32_t;

            static int MePackS16(float value, float scale, std::int16_t *packed)
            {me_s16_body}

            static int MePackU16(float value, float scale, std::uint16_t *packed)
            {me_u16_body}

            static bool ScPackS16(float value, float scale, short *packed)
            {sc_s16_body}

            static bool ScPackU16(float value, float scale,
                                  unsigned short *packed)
            {sc_u16_body}

            struct Case {{ float value; float scale; int ok; int valueOut; }};

            int main()
            {{
                const float pInf = INFINITY;
                const float nInf = -INFINITY;
                const float nan = NAN;
                const Case signedCases[] = {{
                    {{0.0f, 32768.0f, 1, 0}},
                    {{-0.0f, 32768.0f, 1, 0}},
                    {{1.0f / 32768.0f, 32768.0f, 1, 1}},
                    {{-1.0f / 32768.0f, 32768.0f, 1, -1}},
                    {{0.5f / 32768.0f, 32768.0f, 1, 1}},
                    {{-0.5f / 32768.0f, 32768.0f, 1, -1}},
                    {{32767.0f / 32768.0f, 32768.0f, 1, 32767}},
                    {{1.0f, 32768.0f, 0, 0}},
                    {{-1.0f, 32768.0f, 1, -32768}},
                    {{1023.96875f, 32.0f, 1, 32767}},
                    {{1024.0f, 32.0f, 0, 0}},
                    {{-1024.0f, 32.0f, 1, -32768}},
                    {{pInf, 32.0f, 0, 0}},
                    {{nInf, 32.0f, 0, 0}},
                    {{nan, 32.0f, 0, 0}},
                }};
                for (unsigned int i = 0;
                     i < sizeof(signedCases) / sizeof(signedCases[0]); ++i)
                {{
                    std::int16_t me = 1234;
                    short sc = 1234;
                    const int meOk = MePackS16(
                        signedCases[i].value, signedCases[i].scale, &me);
                    const int scOk = ScPackS16(
                        signedCases[i].value, signedCases[i].scale, &sc) ? 1 : 0;
                    if (meOk != signedCases[i].ok ||
                        scOk != signedCases[i].ok || meOk != scOk)
                    {{
                        std::fprintf(stderr,
                                     "signed case %u status %d/%d expected %d\\n",
                                     i, meOk, scOk, signedCases[i].ok);
                        return 10 + static_cast<int>(i);
                    }}
                    if (meOk &&
                        (me != signedCases[i].valueOut ||
                         sc != signedCases[i].valueOut || me != sc))
                    {{
                        std::fprintf(stderr,
                                     "signed case %u value %d/%d expected %d\\n",
                                     i, static_cast<int>(me), static_cast<int>(sc),
                                     signedCases[i].valueOut);
                        return 40 + static_cast<int>(i);
                    }}
                }}

                const Case unsignedCases[] = {{
                    {{0.0f, 32768.0f, 1, 0}},
                    {{-0.0f, 32768.0f, 1, 0}},
                    {{1.0f / 32768.0f, 32768.0f, 1, 1}},
                    {{0.5f / 32768.0f, 32768.0f, 1, 1}},
                    {{1.0f, 32768.0f, 1, 32768}},
                    {{65535.0f / 32768.0f, 32768.0f, 1, 65535}},
                    {{2.0f, 32768.0f, 0, 0}},
                    {{-1.0f / 32768.0f, 32768.0f, 0, 0}},
                    {{pInf, 32768.0f, 0, 0}},
                    {{nInf, 32768.0f, 0, 0}},
                    {{nan, 32768.0f, 0, 0}},
                }};
                for (unsigned int i = 0;
                     i < sizeof(unsignedCases) / sizeof(unsignedCases[0]); ++i)
                {{
                    std::uint16_t me = 1234;
                    unsigned short sc = 1234;
                    const int meOk = MePackU16(
                        unsignedCases[i].value, unsignedCases[i].scale, &me);
                    const int scOk = ScPackU16(
                        unsignedCases[i].value, unsignedCases[i].scale, &sc)
                        ? 1 : 0;
                    if (meOk != unsignedCases[i].ok ||
                        scOk != unsignedCases[i].ok || meOk != scOk)
                    {{
                        std::fprintf(stderr,
                                     "unsigned case %u status %d/%d expected %d\\n",
                                     i, meOk, scOk, unsignedCases[i].ok);
                        return 70 + static_cast<int>(i);
                    }}
                    if (meOk &&
                        (me != unsignedCases[i].valueOut ||
                         sc != unsignedCases[i].valueOut || me != sc))
                    {{
                        std::fprintf(stderr,
                                     "unsigned case %u value %u/%u expected %d\\n",
                                     i, static_cast<unsigned int>(me),
                                     static_cast<unsigned int>(sc),
                                     unsignedCases[i].valueOut);
                        return 100 + static_cast<int>(i);
                    }}
                }}
                return 0;
            }}
        """
        with tempfile.TemporaryDirectory(prefix="th07-c1-pack-") as tmp:
            source = Path(tmp) / "pack.cpp"
            binary = Path(tmp) / "pack"
            source.write_text(textwrap.dedent(harness), encoding="utf-8")
            subprocess.run(
                [
                    "g++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_each_numeric_domain_rejects_instead_of_saturating(self) -> None:
        for source, signature in (
            (WORKER, "me_render_stream_pack_s16("),
            (BULLETS, "bool PspMeRenderPackS16Reference("),
        ):
            body = function_body(source, signature)
            self.assertIn("scaled >= -32768.0f && scaled < 32767.5f", body)

        for source, signature in (
            (WORKER, "me_render_stream_pack_u16("),
            (BULLETS, "bool PspMeRenderPackU16Reference("),
        ):
            body = function_body(source, signature)
            self.assertIn("scaled >= 0.0f && scaled < 65535.5f", body)

    def test_sc_reference_is_independent_and_me_reject_is_not_published(self) -> None:
        reference = function_body(
            BULLETS, "bool PspMeRenderPackReferenceVertex("
        )
        self.assertNotIn("me_render_stream_pack_s16", reference)
        self.assertNotIn("me_render_stream_pack_u16", reference)
        self.assertIn("PspMeRenderPackS16Reference", reference)
        self.assertIn("PspMeRenderPackU16Reference", reference)

        kernel = function_body(WORKER, "me_render_stream_expand_kernel(")
        pack = kernel.index("int packed = me_render_stream_write_vertex(")
        reject = kernel.index("return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;", pack)
        publish = kernel.index("vertexCount += verticesThisRecord;", pack)
        self.assertLess(reject, publish)

    def test_m0_reject_case_uses_real_command10_unsigned_and_signed_limits(
        self,
    ) -> None:
        m0 = function_body(WORKER, "static int selftest_render_stream_c1_m0(")
        self.assertIn('float_bits(2.0f)', m0)
        self.assertIn('float_bits(1027.5f)', m0)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD", m0)
        self.assertIn("rejectCompletion.outputBytes != 0u", m0)
        self.assertIn("rejectCompletion.vertexCount != 0u", m0)
        self.assertIn("rejectCompletion.runCount != 0u", m0)
        self.assertIn('"MERW C1M0 RANGE-REJECT PASS"', m0)


@unittest.skipUnless(IMPLEMENTED, "C1 implementation has not landed")
class PspMeVertex16BitGeStateTests(unittest.TestCase):
    def test_c1_api_uses_the_selected_stream_type_not_the_24_byte_alias(self) -> None:
        conditional = GRAPHICS_H[
            GRAPHICS_H.index("#if defined(TH07_PSP_ME_RENDER_UV16)") :
            GRAPHICS_H.index("void Th07PspEndMeRenderGeSubmission")
        ]
        self.assertIn("const Th07PspMeRenderStreamVertex *vertices", conditional)
        self.assertIn("const Th07PspSpriteVertex *vertices", conditional)

    def test_ge_declaration_keeps_color_and_transform_3d(self) -> None:
        body = function_body(GRAPHICS, "void DrawMeRenderStreamRun(")
        self.assertIn("GU_TEXTURE_16BIT", body)
        self.assertIn("GU_TEXTURE_32BITF", body)
        self.assertIn("GU_VERTEX_16BIT", body)
        self.assertIn("GU_VERTEX_32BITF", body)
        self.assertGreaterEqual(body.count("GU_COLOR_8888"), 2)
        self.assertGreaterEqual(body.count("GU_TRANSFORM_3D"), 2)
        self.assertNotIn("GU_TRANSFORM_2D", body)

    def test_xyz_nonuniform_scale_is_reapplied_after_list_space_check(self) -> None:
        matrix_start = GRAPHICS.index(
            "const ScePspFMatrix4 kMeRenderXyz16ModelMatrix"
        )
        matrix_end = GRAPHICS.index("};", matrix_start)
        matrix = GRAPHICS[matrix_start : matrix_end + 2]
        self.assertEqual(matrix.count("1024.0f"), 2)
        self.assertIn("{0.0f, 0.0f, 1.0f, 0.0f}", matrix)

        body = function_body(GRAPHICS, "void DrawMeRenderStreamRun(")
        ensure = body.index("EnsureListSpace(0)")
        apply = body.index("ApplyMatrices(true)")
        scale = body.index("sceGuSetMatrix(GU_MODEL, &kMeRenderXyz16ModelMatrix)")
        first_draw = body.index("sceGuDrawArray")
        restore = body.index("sceGuSetMatrix(GU_MODEL, &kIdentityMatrix)")
        last_draw = body.rindex("sceGuDrawArray")
        self.assertLess(ensure, apply)
        self.assertLess(apply, scale)
        self.assertLess(scale, first_draw)
        self.assertLess(last_draw, restore)

    def test_consumer_steps_by_selected_c_type(self) -> None:
        body = function_body(GRAPHICS, "void DrawMeRenderStreamRun(")
        self.assertIn("const auto *batch = vertices;", body)
        self.assertIn("batch += sprites * 4u;", body)
        self.assertNotIn("reinterpret_cast<const Th07PspSpriteVertex", body)


if __name__ == "__main__":
    unittest.main()

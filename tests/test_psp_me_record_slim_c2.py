from __future__ import annotations

import itertools
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
ITEMS = (ROOT / "src" / "ItemManager.cpp").read_text(encoding="utf-8")

HEADER_IMPLEMENTED = "TH07_PSP_ME_BULLET_OUTPUT_SLIM" in HEADER
PROFILE_IMPLEMENTED = "PSP_ME_BULLET_OUTPUT_SLIM ?=" in MAKEFILE

FEATURES = (
    "PSP_ME_BULLET_OUTPUT_SLIM",
    "PSP_ME_BULLET_SEED_SLIM",
    "PSP_ME_ITEM_SEED_SLIM",
)
CPP_FEATURES = tuple(f"TH07_{feature}" for feature in FEATURES)


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


def host_compile(source_text: str, defines: tuple[str, ...], output: Path) -> None:
    source = output.with_suffix(".c")
    source.write_text(textwrap.dedent(source_text), encoding="utf-8")
    base_defines = (
        "TH07_PSP_ME_RENDER_WORKER",
        "TH07_PSP_ME_RENDER_CORRECTNESS",
        "TH07_PSP_ME_RENDER_RAW_LIVE",
        "TH07_PSP_ME_RENDER_DIRECT_LIST",
        "TH07_PSP_ME_BULLET_COMPACT_UPDATE",
        "TH07_PSP_ME_ITEM_RENDER_STREAM",
        "TH07_PSP_ME_ITEM_MOTION_UPDATE",
    )
    completed = subprocess.run(
        [
            "gcc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            *(f"-D{name}" for name in (*base_defines, *defines)),
            "-I",
            str(ROOT),
            str(source),
            "-o",
            str(output),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise AssertionError(completed.stderr)


@unittest.skipUnless(PROFILE_IMPLEMENTED, "C2 Make profiles have not landed")
class PspMeRecordSlimC2ProfileTests(unittest.TestCase):
    def test_three_independent_switches_default_off_and_stamp_every_object(self) -> None:
        stamp = next(
            line
            for line in MAKEFILE.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        for make_feature, cpp_feature in zip(FEATURES, CPP_FEATURES):
            with self.subTest(feature=make_feature):
                self.assertIn(f"{make_feature} ?= 0", MAKEFILE)
                self.assertIn(f"$({make_feature})", stamp)
                self.assertIn(f"-D{cpp_feature}", MAKEFILE)
                self.assertRegex(
                    MAKEFILE,
                    rf"filter-out 0 1,\$\({make_feature}\)",
                )

    def test_rid30_is_explicit_c2_off_and_c1_off(self) -> None:
        recipe = recipe_body(MAKEFILE, "psp3000-a1-item-motion-build")
        for feature in (*FEATURES, "PSP_ME_RENDER_UV16", "PSP_ME_RENDER_XYZ16"):
            with self.subTest(feature=feature):
                self.assertIn(f"{feature}=0", recipe)

    def test_public_c2_targets_select_only_the_declared_components(self) -> None:
        targets = {
            "c2a_output_slim": (1, 0, 0),
            "c2b_bullet_seed_slim": (0, 1, 0),
            "c2c_item_seed_slim": (0, 0, 1),
            "c2abc_all_slim": (1, 1, 1),
        }
        for target, values in targets.items():
            with self.subTest(target=target):
                self.assertIn(f"{target}:", MAKEFILE)
                body = recipe_body(MAKEFILE, target)
                for feature, value in zip(FEATURES, values):
                    self.assertIn(f"{feature}={value}", body)
                self.assertIn("PSP_1000=0", body)
                self.assertIn("PSP_ME_RENDER_UV16=0", body)
                self.assertIn("PSP_ME_RENDER_XYZ16=0", body)

    def test_c2_build_guards_require_worker_item_sidecar_and_non_1000(self) -> None:
        self.assertIn("C2 compact arenas require the compact bullet worker", HEADER)
        self.assertIn("C2 Item seed packing requires the Item motion sidecar", HEADER)
        self.assertIn("C2 compact arenas are PSP-2000+ research only", HEADER)


@unittest.skipUnless(HEADER_IMPLEMENTED, "C2 public ABI has not landed")
class PspMeRecordSlimC2AbiTests(unittest.TestCase):
    HARNESS = r"""
        #include <stddef.h>
        #include <stdio.h>
        #include "psp/audio_me.h"

        int main(void)
        {
        #if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
            const size_t bulletInBounds =
                offsetof(Th07PspMeBulletCompactSeed, inBoundsBits);
            const size_t bulletStaticFlags = (size_t)-1;
            const size_t bulletReserved = (size_t)-1;
        #else
            const size_t bulletInBounds = (size_t)-1;
            const size_t bulletStaticFlags =
                offsetof(Th07PspMeBulletCompactSeedSlot, staticFlags);
            const size_t bulletReserved =
                offsetof(Th07PspMeBulletCompactSeedSlot, reserved);
        #endif
        #if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
            const size_t itemState0 =
                offsetof(Th07PspMeItemMotionSeed, stateBit0);
            const size_t itemState1 =
                offsetof(Th07PspMeItemMotionSeed, stateBit1);
            const size_t itemAuto =
                offsetof(Th07PspMeItemMotionSeed, autoCollectBits);
            const size_t itemPacked = (size_t)-1;
        #else
            const size_t itemState0 = (size_t)-1;
            const size_t itemState1 = (size_t)-1;
            const size_t itemAuto = (size_t)-1;
            const size_t itemPacked =
                offsetof(Th07PspMeItemMotionSeedSlot, stateAndFlags);
        #endif

            printf(
                "%zu %zu %zu %zu %zu %zu "
                "%zu %zu %zu %zu %zu %zu "
                "%zu %zu %zu %zu %zu %zu %zu %zu "
                "%zu %zu %zu %zu %zu %zu %zu %zu "
                "%zu %zu %zu %zu %zu %zu %zu %zu "
                "%zu %zu %zu %zu %zu %zu %zu %zu %zu %zu %zu %zu %zu\n",
                sizeof(Th07PspMeBulletCompactSlotResult),
                _Alignof(Th07PspMeBulletCompactSlotResult),
                offsetof(Th07PspMeBulletCompactSlotResult, generation),
                offsetof(Th07PspMeBulletCompactSlotResult, flags),
                sizeof(Th07PspMeBulletCompactOutput),
                offsetof(Th07PspMeBulletCompactOutput, slots),
                sizeof(Th07PspMeBulletCompactSeedSlot),
                _Alignof(Th07PspMeBulletCompactSeedSlot),
                sizeof(Th07PspMeBulletCompactSeed),
                offsetof(Th07PspMeBulletCompactSeed, candidateBits),
                bulletInBounds,
                offsetof(Th07PspMeBulletCompactSeed, slots),
                offsetof(Th07PspMeBulletCompactSeedSlot, generation),
                offsetof(Th07PspMeBulletCompactSeedSlot, posXBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, posYBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, posZBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, velocityXBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, velocityYBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, velocityZBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, spriteWidthBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, spriteHeightBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, grazeSizeXBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, grazeSizeYBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, nextPosXBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, nextPosYBits),
                offsetof(Th07PspMeBulletCompactSeedSlot, nextPosZBits),
                bulletStaticFlags, bulletReserved,
                sizeof(Th07PspMeItemMotionSeedSlot),
                _Alignof(Th07PspMeItemMotionSeedSlot),
                sizeof(Th07PspMeItemMotionSeed),
                offsetof(Th07PspMeItemMotionSeed, candidateBits),
                itemState0, itemState1, itemAuto,
                offsetof(Th07PspMeItemMotionSeed, slots),
                offsetof(Th07PspMeItemMotionSeedSlot, generation),
                offsetof(Th07PspMeItemMotionSeedSlot, posXBits),
                offsetof(Th07PspMeItemMotionSeedSlot, posYBits),
                offsetof(Th07PspMeItemMotionSeedSlot, posZBits),
                offsetof(Th07PspMeItemMotionSeedSlot, startXBits),
                offsetof(Th07PspMeItemMotionSeedSlot, startYBits),
                offsetof(Th07PspMeItemMotionSeedSlot, startZBits),
                offsetof(Th07PspMeItemMotionSeedSlot, targetXBits),
                offsetof(Th07PspMeItemMotionSeedSlot, targetYBits),
                offsetof(Th07PspMeItemMotionSeedSlot, targetZBits),
                offsetof(Th07PspMeItemMotionSeedSlot, timerCurrent),
                offsetof(Th07PspMeItemMotionSeedSlot, timerSubFrameBits),
                itemPacked);
            return 0;
        }
    """

    def test_host_compiler_observes_each_independent_and_cumulative_abi(self) -> None:
        max_size = (1 << 64) - 1
        output_off = (16, 4, 12, 14, 16512, 128)
        output_slim = (4, 2, 0, 2, 4224, 128)
        bullet_fields = tuple(range(0, 56, 4))
        bullet_off = (64, 4, 65728, 64, max_size, 192) + bullet_fields + (56, 60)
        bullet_slim = (56, 4, 57664, 64, 192, 320) + bullet_fields + (
            max_size,
            max_size,
        )
        item_fields = tuple(range(0, 48, 4))
        item_off = (64, 4, 70656, 64, max_size, max_size, max_size, 256) + item_fields + (48,)
        item_slim = (48, 4, 53632, 64, 256, 448, 640, 832) + item_fields + (max_size,)
        cases = {
            "off": ((), output_off + bullet_off + item_off),
            "output": ((CPP_FEATURES[0],), output_slim + bullet_off + item_off),
            "bullet": ((CPP_FEATURES[1],), output_off + bullet_slim + item_off),
            "item": ((CPP_FEATURES[2],), output_off + bullet_off + item_slim),
            "all": (CPP_FEATURES, output_slim + bullet_slim + item_slim),
        }
        with tempfile.TemporaryDirectory(prefix="th07-c2-abi-") as tmp:
            for name, (defines, expected) in cases.items():
                with self.subTest(profile=name):
                    binary = Path(tmp) / name
                    host_compile(self.HARNESS, defines, binary)
                    actual = tuple(
                        map(
                            int,
                            subprocess.run(
                                [str(binary)],
                                check=True,
                                capture_output=True,
                                text=True,
                            ).stdout.split(),
                        )
                    )
                    self.assertEqual(actual, expected)

    def test_every_selected_arena_is_a_whole_number_of_cache_lines(self) -> None:
        for size in (16512, 4224, 65728, 57664, 70656, 53632, 35456):
            with self.subTest(size=size):
                self.assertEqual(size % 64, 0)

    def test_all_eight_switch_combinations_have_unique_protocol_tuple(self) -> None:
        harness = r"""
            #include <stdio.h>
            #include "psp/audio_me.h"
            int main(void) {
                printf("%u %u %u\n",
                       TH07_PSP_ME_BULLET_COMPACT_VERSION,
                       TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION,
                       TH07_PSP_ME_ITEM_MOTION_VERSION);
                return 0;
            }
        """
        observed: set[tuple[int, int, int]] = set()
        with tempfile.TemporaryDirectory(prefix="th07-c2-version-") as tmp:
            for values in itertools.product((0, 1), repeat=3):
                defines = tuple(
                    feature for feature, enabled in zip(CPP_FEATURES, values) if enabled
                )
                binary = Path(tmp) / "".join(map(str, values))
                host_compile(harness, defines, binary)
                protocol = tuple(
                    map(
                        int,
                        subprocess.run(
                            [str(binary)],
                            check=True,
                            capture_output=True,
                            text=True,
                        ).stdout.split(),
                    )
                )
                self.assertNotIn(protocol, observed, values)
                observed.add(protocol)
        self.assertEqual(len(observed), 8)

    def test_old_redundant_members_are_absent_only_in_selected_layouts(self) -> None:
        self.assertIn("#if !defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)", HEADER)
        self.assertIn("#if !defined(TH07_PSP_ME_BULLET_SEED_SLIM)", HEADER)
        self.assertIn("#if !defined(TH07_PSP_ME_ITEM_SEED_SLIM)", HEADER)
        self.assertNotIn("inUseBits[TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS]", HEADER)


@unittest.skipUnless(PROFILE_IMPLEMENTED, "C2 implementation has not fully landed")
class PspMeRecordSlimC2ContractTests(unittest.TestCase):
    def test_exact_item_kernel_rejects_tail_or_orphan_planes_and_state_three(self) -> None:
        kernel = function_body(WORKER, "me_item_motion_update_kernel(")
        supported = function_body(WORKER, "me_item_motion_float_bits_supported(")
        harness = f"""
            #include <stddef.h>
            #include <stdint.h>
            #include <string.h>
            #include "psp/audio_me.h"

            static int me_item_motion_seed_header_valid(
                const Th07PspMeItemMotionSeed *seed, uint32_t bank)
            {{ (void)seed; (void)bank; return 1; }}

            static int me_item_motion_float_bits_supported(uint32_t bits)
            {supported}

            static float me_render_bits_float(uint32_t bits)
            {{ float value; memcpy(&value, &bits, 4); return value; }}
            static uint32_t me_render_float_bits(float value)
            {{ uint32_t bits; memcpy(&bits, &value, 4); return bits; }}
            static float me_item_motion_atan2(float y, float x)
            {{ (void)y; (void)x; return 0.0f; }}
            static float me_item_motion_cos(float angle)
            {{ (void)angle; return 1.0f; }}
            static float me_item_motion_sin(float angle)
            {{ (void)angle; return 0.0f; }}

            static uint32_t me_item_motion_update_kernel(
                const Th07PspMeBulletCompactJob *job,
                const Th07PspMeItemMotionSeed *seed,
                Th07PspMeItemMotionOutput *output,
                uint32_t *outCandidateCount, uint32_t *outProcessedCount,
                uint32_t *outFirstBadSlot)
            {kernel}

            static uint32_t run(Th07PspMeBulletCompactJob *job,
                                Th07PspMeItemMotionSeed *seed)
            {{
                Th07PspMeItemMotionOutput output;
                uint32_t candidates, processed, firstBad;
                return me_item_motion_update_kernel(
                    job, seed, &output, &candidates, &processed, &firstBad);
            }}

            int main(void)
            {{
                Th07PspMeBulletCompactJob job;
                Th07PspMeItemMotionSeed seed;
                memset(&job, 0, sizeof(job));
                job.flags = TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID;
                job.itemMotionCandidateLimit = 1100;

                memset(&seed, 0, sizeof(seed));
                seed.candidateBits[34] = 1u << 12;
                if (run(&job, &seed) != TH07_PSP_ME_ITEM_MOTION_RESULT_SEED)
                    return 10;

                memset(&seed, 0, sizeof(seed));
                seed.stateBit0[35] = 1u;
                if (run(&job, &seed) != TH07_PSP_ME_ITEM_MOTION_RESULT_SEED)
                    return 20;

                memset(&seed, 0, sizeof(seed));
                seed.stateBit0[0] = 1u;
                if (run(&job, &seed) != TH07_PSP_ME_ITEM_MOTION_RESULT_SEED)
                    return 30;

                memset(&seed, 0, sizeof(seed));
                seed.candidateBits[0] = 1u;
                seed.stateBit0[0] = 1u;
                seed.stateBit1[0] = 1u;
                seed.header.candidateCount = 1u;
                seed.slots[0].generation = 1u;
                if (run(&job, &seed) != TH07_PSP_ME_ITEM_MOTION_RESULT_RECORD)
                    return 40;
                return 0;
            }}
        """
        with tempfile.TemporaryDirectory(prefix="th07-c2-item-kernel-") as tmp:
            binary = Path(tmp) / "kernel"
            host_compile(harness, (CPP_FEATURES[2],), binary)
            subprocess.run(
                [str(binary)], check=True, capture_output=True, text=True
            )

    def test_exact_bullet_kernel_generates_slim_echo_and_rejects_bad_planes(self) -> None:
        kernel = function_body(WORKER, "me_bullet_compact_update_kernel(")
        harness = f"""
            #include <stdint.h>
            #include <string.h>
            #include "psp/audio_me.h"

            // The production target is 32-bit.  The collision-snapshot branch
            // is not entered by this host harness, but its exact extracted
            // source still contains the PSP's integer physical-address cast.
            #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"

            enum {{
                ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE = 20,
                ME_BULLET_COMPACT_BOMB_CLEAR_POS_X_OFFSET = 0,
                ME_BULLET_COMPACT_BOMB_CLEAR_POS_Y_OFFSET = 4,
                ME_BULLET_COMPACT_BOMB_CLEAR_POS_Z_OFFSET = 8,
                ME_BULLET_COMPACT_BOMB_CLEAR_SIZE_X_OFFSET = 12,
                ME_BULLET_COMPACT_BOMB_CLEAR_SIZE_Y_OFFSET = 16
            }};

            static int me_bullet_compact_seed_header_valid(
                const Th07PspMeBulletCompactSeed *seed, uint32_t bank)
            {{ (void)seed; (void)bank; return 1; }}

            static uint32_t me_render_stream_load_u32(
                const unsigned char *base, uint32_t offset)
            {{
                uint32_t value;
                memcpy(&value, base + offset, sizeof(value));
                return value;
            }}

            static int me_render_stream_float_bits_finite(uint32_t bits)
            {{ return (bits & 0x7f800000u) != 0x7f800000u; }}

            static float me_render_bits_float(uint32_t bits)
            {{
                float value;
                memcpy(&value, &bits, sizeof(value));
                return value;
            }}

            static int me_bullet_compact_no_collision(
                const Th07PspMeBulletCompactJob *job, float x, float y,
                float gx, float gy, int *valid)
            {{
                (void)job; (void)x; (void)y; (void)gx; (void)gy;
                *valid = 1;
                return 1;
            }}

            static uint32_t me_bullet_compact_update_kernel(
                const Th07PspMeBulletCompactJob *job,
                const Th07PspMeBulletCompactSeed *seed,
                Th07PspMeBulletCompactOutput *output,
                uint32_t *outCandidateCount, uint32_t *outInBoundsCount,
                uint32_t *outNoCollisionCount, uint32_t *outFirstBadSlot)
            {kernel}

            int main(void)
            {{
                Th07PspMeBulletCompactJob job;
                Th07PspMeBulletCompactSeed seed;
                Th07PspMeBulletCompactOutput output;
                uint32_t candidates, inBounds, noCollision, firstBad;
                memset(&job, 0, sizeof(job));
                memset(&seed, 0, sizeof(seed));
                seed.header.candidateCount = 2;
                seed.candidateBits[0] = 1u;
                seed.candidateBits[31] = 0x80000000u;
                seed.inBoundsBits[0] = 1u;
                seed.slots[0].generation = 0x10001u;
                seed.slots[1023].generation = 0x2ffffu;

                uint32_t result = me_bullet_compact_update_kernel(
                    &job, &seed, &output, &candidates, &inBounds,
                    &noCollision, &firstBad);
                if (result != TH07_PSP_ME_BULLET_COMPACT_RESULT_OK ||
                    candidates != 2u || inBounds != 1u || noCollision != 2u ||
                    firstBad != 0xffffffffu || output.candidateBits[0] != 1u ||
                    output.candidateBits[31] != 0x80000000u ||
                    output.slots[0].generation != 1u ||
                    output.slots[1023].generation != 0xffffu ||
                    output.slots[0].flags != 7u ||
                    output.slots[1023].flags != 5u)
                    return 10;

                seed.inBoundsBits[0] = 2u;
                result = me_bullet_compact_update_kernel(
                    &job, &seed, &output, &candidates, &inBounds,
                    &noCollision, &firstBad);
                if (result != TH07_PSP_ME_BULLET_COMPACT_RESULT_SEED ||
                    candidates != 0u)
                    return 20;

                seed.inBoundsBits[0] = 1u;
                seed.slots[0].generation = 0u;
                result = me_bullet_compact_update_kernel(
                    &job, &seed, &output, &candidates, &inBounds,
                    &noCollision, &firstBad);
                if (result != TH07_PSP_ME_BULLET_COMPACT_RESULT_RECORD ||
                    firstBad != 0u)
                    return 30;
                return 0;
            }}
        """
        with tempfile.TemporaryDirectory(prefix="th07-c2-bullet-kernel-") as tmp:
            binary = Path(tmp) / "kernel"
            host_compile(harness, CPP_FEATURES[:2], binary)
            subprocess.run(
                [str(binary)], check=True, capture_output=True, text=True
            )

    def test_exact_item_capture_code_packs_three_states_and_soft_rejects(self) -> None:
        supported = function_body(WORKER, "me_item_motion_float_bits_supported(")
        capture = function_body(WORKER, "me_item_motion_capture_seed(")
        clear = function_body(WORKER, "me_item_motion_clear_seed_slot(")
        harness = f"""
            #include <stdint.h>
            #include <stdio.h>
            #include <string.h>
            #include "psp/audio_me.h"

            enum {{
                ME_ITEM_MOTION_CURRENT_POS_OFFSET = 0,
                ME_ITEM_MOTION_START_POS_OFFSET = 12,
                ME_ITEM_MOTION_TARGET_POS_OFFSET = 24,
                ME_ITEM_MOTION_TIMER_CURRENT_OFFSET = 36,
                ME_ITEM_MOTION_TIMER_SUBFRAME_OFFSET = 40,
                ME_ITEM_MOTION_STATE_OFFSET = 44,
                ME_ITEM_MOTION_AUTOCOLLECT_OFFSET = 45,
                ME_RENDER_ITEM_IN_USE_OFFSET = 46
            }};

            static uint32_t me_render_stream_item_load_u8(
                const volatile unsigned char *base, uint32_t offset)
            {{ return base[offset]; }}

            static uint32_t me_render_stream_item_load_u32(
                const volatile unsigned char *base, uint32_t offset)
            {{
                uint32_t value;
                memcpy(&value, (const void *)(base + offset), sizeof(value));
                return value;
            }}

            static int me_item_motion_float_bits_supported(uint32_t bits)
            {supported}

            static void me_item_motion_capture_seed(
                Th07PspMeItemMotionSeed *seed,
                const volatile unsigned char *item, uint32_t slot,
                uint32_t generation)
            {capture}

            static void me_item_motion_clear_seed_slot(
                Th07PspMeItemMotionSeed *seed, uint32_t slot)
            {clear}

            static void set_item(unsigned char *item, uint32_t state,
                                 uint32_t autoCollect, uint32_t inUse)
            {{
                memset(item, 0, 64);
                item[ME_ITEM_MOTION_STATE_OFFSET] = (unsigned char)state;
                item[ME_ITEM_MOTION_AUTOCOLLECT_OFFSET] =
                    (unsigned char)autoCollect;
                item[ME_RENDER_ITEM_IN_USE_OFFSET] = (unsigned char)inUse;
            }}

            int main(void)
            {{
                Th07PspMeItemMotionSeed seed;
                unsigned char item[64];
                memset(&seed, 0, sizeof(seed));

                set_item(item, 0, 0, 1);
                me_item_motion_capture_seed(&seed, item, 0, 11);
                set_item(item, 1, 1, 1);
                me_item_motion_capture_seed(&seed, item, 1, 12);
                set_item(item, 2, 0, 1);
                me_item_motion_capture_seed(&seed, item, 2, 13);
                if (seed.candidateBits[0] != 7u ||
                    seed.stateBit0[0] != 2u || seed.stateBit1[0] != 4u ||
                    seed.autoCollectBits[0] != 2u ||
                    seed.header.candidateCount != 3u)
                    return 10;

                set_item(item, 3, 0, 1);
                me_item_motion_capture_seed(&seed, item, 3, 14);
                set_item(item, 0, 2, 1);
                me_item_motion_capture_seed(&seed, item, 4, 15);
                set_item(item, 0, 0, 0);
                me_item_motion_capture_seed(&seed, item, 5, 16);
                if ((seed.candidateBits[0] & 0x38u) != 0u ||
                    seed.header.candidateCount != 3u)
                    return 20;

                set_item(item, 2, 1, 1);
                me_item_motion_capture_seed(&seed, item, 1099, 17);
                if (seed.candidateBits[34] != (1u << 11) ||
                    seed.stateBit1[34] != (1u << 11) ||
                    seed.autoCollectBits[34] != (1u << 11))
                    return 30;
                me_item_motion_capture_seed(&seed, item, 1100, 18);
                for (uint32_t word = 35; word < 48; ++word)
                {{
                    if (seed.candidateBits[word] || seed.stateBit0[word] ||
                        seed.stateBit1[word] || seed.autoCollectBits[word])
                        return 40;
                }}

                me_item_motion_clear_seed_slot(&seed, 1);
                if ((seed.candidateBits[0] & 2u) ||
                    (seed.stateBit0[0] & 2u) ||
                    (seed.autoCollectBits[0] & 2u) ||
                    seed.slots[1].generation != 0u ||
                    seed.header.candidateCount != 3u)
                    return 50;
                return 0;
            }}
        """
        with tempfile.TemporaryDirectory(prefix="th07-c2-item-capture-") as tmp:
            binary = Path(tmp) / "capture"
            host_compile(harness, (CPP_FEATURES[2],), binary)
            subprocess.run(
                [str(binary)], check=True, capture_output=True, text=True
            )

    def test_bitplane_bounds_and_item_padding_contract_are_explicit(self) -> None:
        self.assertIn("TH07_PSP_ME_BULLET_COMPACT_ACTIVE_WORDS = 32", HEADER)
        self.assertIn("TH07_PSP_ME_ITEM_MOTION_ACTIVE_WORDS = 35", HEADER)
        self.assertIn("TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS = 48", HEADER)
        self.assertRegex(WORKER, r"slot\s*>=\s*TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS")
        self.assertRegex(WORKER, r"slot\s*>=\s*TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS")
        self.assertIn("TH07_PSP_ME_ITEM_MOTION_ACTIVE_WORDS", WORKER)

    def test_generation_race_clears_both_bullet_seed_planes_and_slot(self) -> None:
        gather = function_body(
            WORKER, "me_render_stream_reconstruct_list_record("
        )
        race = gather[gather.index("// Seed capture reads update-only fields") :]
        clear_candidate = race.index("*compactWord &= ~compactBit;")
        clear_bounds = race.index(
            "compactSeed->inBoundsBits[slot >> 5u] &= ~compactBit;"
        )
        clear_slot = race.index("memset(&compactSeed->slots[slot]")
        self.assertLess(clear_candidate, clear_bounds)
        self.assertLess(clear_bounds, clear_slot)

    def test_item_capture_rejects_unrepresentable_values_before_setting_candidate(self) -> None:
        capture = function_body(WORKER, "me_item_motion_capture_seed(")
        self.assertRegex(capture, r"state\s*>\s*2u")
        self.assertRegex(capture, r"autoCollect\s*>\s*1u")
        self.assertRegex(capture, r"inUse\s*!=\s*1u")
        guard = capture.index(
            "if (state > 2u || autoCollect > 1u || inUse != 1u)"
        )
        reject = capture.index("return;", guard)
        write_slot = capture.index("Th07PspMeItemMotionSeedSlot *out", guard)
        publish = capture.index("candidateBits", write_slot)
        self.assertLess(guard, reject)
        self.assertLess(reject, write_slot)
        self.assertLess(write_slot, publish)

    def test_output_slim_uses_seed_position_and_preserves_generation_and_flag_gates(self) -> None:
        adopt = function_body(BULLETS, "bool PspMeBulletCompactTryAdoptSeed(")
        self.assertIn("seedSlot.nextPosXBits", adopt)
        self.assertIn("seedSlot.nextPosYBits", adopt)
        self.assertIn("seedSlot.nextPosZBits", adopt)
        self.assertIn("static_cast<u16>(seedSlot.generation)", adopt)
        self.assertIn("allowedOutputFlags", adopt)
        self.assertIn("manager->pspMeRenderSlotGenerations[slot]", adopt)

    def test_selected_sizes_drive_capacity_cache_and_pointer_ranges(self) -> None:
        for type_name in (
            "Th07PspMeBulletCompactSeed",
            "Th07PspMeBulletCompactOutput",
            "Th07PspMeItemMotionSeed",
        ):
            with self.subTest(type_name=type_name):
                self.assertGreaterEqual(WORKER.count(f"sizeof({type_name})"), 2)
        for legacy in ("65728u", "16512u", "70656u"):
            # Legacy byte counts remain legal only in the explicit OFF ABI
            # assertions.  Runtime cache/capacity code must be type-derived.
            lines = [line for line in WORKER.splitlines() if legacy in line]
            self.assertTrue(lines)
            self.assertTrue(all("_Static_assert" in line or "ABI" in line for line in lines))

    def test_full_seed_identity_and_segment_local_item_fallback_remain_present(self) -> None:
        self.assertIn("header->seedBytes == sizeof(*seed)", WORKER)
        self.assertIn("header->version == TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION", WORKER)
        self.assertIn("TH07_PSP_ME_ITEM_MOTION_STATE_SAFE_FALLBACK", WORKER)
        self.assertIn("th07_psp_me_item_motion_available", ITEMS)


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
FEATURE = "TH07_PSP_ME_EDRAM_SEED_BENCH"


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


def make_target(makefile: str, name: str) -> str:
    start = makefile.index(f"{name}:")
    match = re.search(r"\n(?=[A-Za-z0-9_.-]+:)", makefile[start + 1 :])
    if match is None:
        return makefile[start:]
    return makefile[start : start + 1 + match.start()]


def without_feature(source: str) -> str:
    lines = source.splitlines()
    output: list[str] = []
    stack: list[tuple[bool, bool, bool]] = []
    enabled = True
    for line in lines:
        token = line.strip()
        if re.match(r"^#\s*(?:if|ifdef|ifndef)\b", token):
            targeted = FEATURE in token
            if targeted:
                negative = (
                    f"!defined({FEATURE})" in token
                    or re.match(rf"^#\s*ifndef\s+{FEATURE}\b", token)
                    is not None
                )
                stack.append((True, enabled, negative))
                enabled = enabled and negative
            else:
                stack.append((False, enabled, True))
                if enabled:
                    output.append(line)
            continue
        if re.match(r"^#\s*else\b", token) and stack:
            targeted, parent, branch = stack[-1]
            if targeted:
                branch = not branch
                stack[-1] = (targeted, parent, branch)
                enabled = parent and branch
            elif enabled:
                output.append(line)
            continue
        if re.match(r"^#\s*endif\b", token) and stack:
            targeted, parent, _ = stack.pop()
            if not targeted and enabled:
                output.append(line)
            enabled = parent
            continue
        if enabled:
            output.append(line)
    return "\n".join(output)


class MeEdramSeedBenchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")
        cls.audio_h = (ROOT / "psp/audio_me.h").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src/BulletManager.cpp").read_text(
            encoding="utf-8"
        )

    def test_make_profile_is_default_off_c_only_and_stamped(self) -> None:
        self.assertIn("PSP_ME_EDRAM_SEED_BENCH ?= 0", self.makefile)
        self.assertIn(
            "CFLAGS += -DTH07_PSP_ME_EDRAM_SEED_BENCH", self.makefile
        )
        self.assertNotIn(
            "CXXFLAGS += -DTH07_PSP_ME_EDRAM_SEED_BENCH", self.makefile
        )
        stamp = next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_ME_EDRAM_SEED_BENCH)", stamp)
        for requirement in (
            "requires PSP_ME_BULLET_COMPACT_UPDATE=1",
            "requires PSP_ME_RENDER_DIRECT_LIST=1",
            "requires PSP_MECC_AUDIO_4M=1",
            "is PSP-3000-only",
            "requires TRUSTED_SEED_AUTHORITY=0",
            "requires ITEM_RENDER_STREAM=0",
            "requires EFFECT_RENDER_STREAM=0",
            "requires ADAPTIVE_AUX_RENDER=0",
            "requires ITEM_PREFIX_SPLIT=0",
            "must be 0 or 1",
        ):
            self.assertIn(requirement, self.makefile)

    def test_rid25_is_exact_safe_runtime_plus_boot_bench(self) -> None:
        target = make_target(
            self.makefile, "psp3000-ime7-edram-seed-bench-build"
        )
        for setting in (
            "PSP_1000=0",
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_GE_CONSUME=1",
            "PSP_ME_RENDER_PERFORMANCE=1",
            "PSP_ME_RENDER_RAW_LIVE=1",
            "PSP_ME_RENDER_DIRECT_LIST=1",
            "PSP_ME_BULLET_FAST_UPDATE=0",
            "PSP_ME_BULLET_COMPACT_UPDATE=1",
            "PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0",
            "PSP_ME_ITEM_RENDER_STREAM=0",
            "PSP_ME_EFFECT_RENDER_STREAM=0",
            "PSP_ME_ADAPTIVE_AUX_RENDER=0",
            "PSP_ME_ITEM_PREFIX_SPLIT=0",
            "PSP_ME_EDRAM_SEED_BENCH=1",
            "PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0",
            "PSP_BULLET_COLLISION_BROADPHASE=1",
            "PSP_USAGE_METER=1",
            "PSP_AUDIO4M_BUILD_ID=0x26083125u",
            "PSP_EBOOT_TITLE='TH07 PSP ME EDRAM SEED BENCH'",
        ):
            self.assertIn(setting, target)
        self.assertEqual(self.makefile.count("PSP_ME_EDRAM_SEED_BENCH=1"), 1)
        self.assertNotIn("PSP_ME_EDRAM_SEED_BENCH=1", make_target(
            self.makefile, "psp1000-build"
        ))

    def test_feature_off_removes_all_bench_code(self) -> None:
        stripped = without_feature(self.audio)
        for token in (
            "ME_CMD_EDRAM_SEED_BENCH",
            "MeEdramSeedBenchMailbox",
            "process_edram_seed_bench_on_me",
            "dispatch_edram_seed_bench",
            "MEED ",
        ):
            self.assertNotIn(token, stripped)
        self.assertNotIn(FEATURE, self.audio_h)
        self.assertNotIn(FEATURE, self.bullets)

    def test_command13_is_boot_selftest_only(self) -> None:
        self.assertIn("ME_CMD_EDRAM_SEED_BENCH = 13", self.audio)
        # Skip the forward declaration and inspect the final worker definition.
        worker_source = self.audio[self.audio.rindex("void meLibOnProcess(void)") :]
        worker = function_body(worker_source, "void meLibOnProcess(void)")
        self.assertIn("process_edram_seed_bench_on_me(box)", worker)
        selftest = function_body(
            self.audio, "static int selftest_bullet_compact_update(void)"
        )
        self.assertIn("dispatch_edram_seed_bench(", selftest)
        self.assertIn("static const uint32_t counts[4]", selftest)
        self.assertIn("{0u, 128u, 512u, 1024u}", selftest)
        for production in (
            function_body(
                self.audio, "int th07_psp_me_bullet_compact_begin("
            ),
            function_body(
                self.audio, "int th07_psp_me_bullet_compact_poll("
            ),
            function_body(
                self.audio, "process_bullet_compact_update_on_me("
            ),
        ):
            self.assertNotIn("ME_CMD_EDRAM_SEED_BENCH", production)

    def test_extent_is_guarded_short_lived_and_never_authoritative(self) -> None:
        self.assertIn("ME_EDRAM_SEED_BENCH_AREA_BASE = 0x00300000", self.audio)
        self.assertIn("ME_EDRAM_SEED_BENCH_GUARD_BYTES = 64", self.audio)
        self.assertIn("sizeof(Th07PspMeBulletCompactSeed)", self.audio)
        self.assertIn("0x00400000u", self.audio)
        worker = function_body(
            self.audio, "static void process_edram_seed_bench_on_me("
        )
        self.assertIn("me_edram_seed_bench_fill_guards()", worker)
        self.assertIn("me_edram_seed_bench_guards_match()", worker)
        self.assertIn(
            "memset((void *)ME_EDRAM_SEED_BENCH_AREA_BASE, 0", worker
        )
        self.assertIn("No local byte survives command 13", worker)
        self.assertIn(
            "TH07_PSP_ME_BULLET_COMPACT_BACKEND_MAIN_RAM", self.bullets
        )
        self.assertNotIn(
            "TH07_PSP_ME_BULLET_COMPACT_BACKEND_ME_EDRAM", self.bullets
        )

    def test_logs_separate_correctness_from_performance_decision(self) -> None:
        selftest = function_body(
            self.audio, "static int selftest_bullet_compact_update(void)"
        )
        for marker in (
            "MEED SELFTEST BEGIN",
            "MEED SELFTEST PASS CASES4 MM0 GUARD0 HASH0 RUNTIME0",
            "MEED DECISION GO%d",
            "PATH=MAIN-vs-STAGE",
            "main1024 > stage1024",
            "stage1024 * 100u",
            "stage512 <= main512",
            "denseDelta >= minWinCycles",
            "denseTenPercent",
            "midNonRegress",
        ):
            self.assertIn(marker, selftest)
        dispatch = function_body(
            self.audio, "static int dispatch_edram_seed_bench("
        )
        self.assertIn("MEED A/B N%lu", dispatch)
        self.assertIn("L2MT%lu/%lu", dispatch)
        self.assertIn("result == ME_EDRAM_SEED_BENCH_RESULT_OK", dispatch)
        self.assertIn("sceKernelDcacheInvalidateRange(sourceArea", dispatch)
        self.assertIn("sceKernelDcacheInvalidateRange(mirrorArea", dispatch)
        self.assertIn("const int scMirrorValid", dispatch)
        self.assertIn(
            "memcmp(&sourceArea->seed, &mirrorArea->seed", dispatch
        )
        self.assertIn("scSourceHash == inputHash", dispatch)
        self.assertIn("scMirrorHash == inputHash", dispatch)
        self.assertNotIn("th07_psp_perf_note", dispatch)

    def test_bench_region_has_no_audio_game_or_ge_wiring(self) -> None:
        worker = function_body(
            self.audio, "static void process_edram_seed_bench_on_me("
        )
        dispatch = function_body(
            self.audio, "static int dispatch_edram_seed_bench("
        )
        combined = worker + dispatch
        for forbidden in (
            "ME_CMD_AUDIO_MIX",
            "ME_CMD_BGM_",
            "ME_CMD_SFX_",
            "sceAudio",
            "sceGu",
            "sceGe",
            "g_GameManager",
            "gBgmPaused",
        ):
            self.assertNotIn(forbidden, combined)


if __name__ == "__main__":
    unittest.main()

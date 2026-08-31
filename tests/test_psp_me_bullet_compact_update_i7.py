from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "TH07_PSP_ME_BULLET_COMPACT_UPDATE"
MAKE_FEATURE = "PSP_ME_BULLET_COMPACT_UPDATE"


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


def declaration(source: str, opening: str, closing: str) -> str:
    start = source.index(opening)
    end = source.index(closing, start) + len(closing)
    return source[start:end]


def make_target(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(
        r"\n(?=[A-Za-z0-9_.-]+(?:\s+[^\n:]*)?:)", makefile[start + 1 :]
    )
    return (
        makefile[start:]
        if match is None
        else makefile[start : start + match.start() + 1]
    )


def assert_order(test: unittest.TestCase, source: str, *needles: str) -> None:
    cursor = -1
    for needle in needles:
        found = source.find(needle, cursor + 1)
        test.assertNotEqual(found, -1, f"missing ordered token: {needle}")
        test.assertGreater(found, cursor, f"out-of-order token: {needle}")
        cursor = found


class PspMeBulletCompactUpdateI7Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "psp" / "audio_me.h").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.capture = function_body(
            cls.audio, "me_render_stream_reconstruct_list_record("
        )
        cls.render = function_body(cls.audio, "process_render_stream_on_me(")
        cls.worker = function_body(
            cls.audio, "process_bullet_compact_update_on_me("
        )
        cls.begin = function_body(
            cls.audio, "th07_psp_me_bullet_compact_begin("
        )
        cls.poll = function_body(
            cls.audio, "th07_psp_me_bullet_compact_poll("
        )
        cls.selftest = function_body(
            cls.audio, "selftest_bullet_compact_update("
        )

    def test_abi_is_fixed_contiguous_and_double_buffered(self) -> None:
        for evidence in (
            'TH07_PSP_ME_BULLET_COMPACT_VERSION =\n        0x4d453137u + TH07_PSP_ME_BULLET_OUTPUT_ABI_BIAS',
            'TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION = 0x42533131u',
            'TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS = 1024',
            'TH07_PSP_ME_BULLET_COMPACT_ACTIVE_WORDS = 32',
            'TH07_PSP_ME_BULLET_COMPACT_BANKS = 2',
            'sizeof(Th07PspMeBulletCompactSeedSlot) == 64u',
            'sizeof(Th07PspMeBulletCompactSeedHeader) == 64u',
            'sizeof(Th07PspMeBulletCompactSeed) == 65728u',
            'sizeof(Th07PspMeBulletCompactJob) == 92u',
            'sizeof(Th07PspMeBulletCompactSlotResult) == 16u',
            'sizeof(Th07PspMeBulletCompactOutput) == 16512u',
        ):
            self.assertIn(evidence, self.header + self.audio)

        slot = declaration(
            self.header,
            "typedef struct Th07PspMeBulletCompactSeedSlot",
            "} Th07PspMeBulletCompactSeedSlot;",
        )
        assert_order(
            self,
            slot,
            "generation",
            "posXBits",
            "velocityXBits",
            "spriteWidthBits",
            "grazeSizeXBits",
            "nextPosXBits",
            "nextPosYBits",
            "nextPosZBits",
            "staticFlags",
            "reserved",
        )
        self.assertIn(
            "gMeBulletCompactSeedAreas[TH07_PSP_ME_BULLET_COMPACT_BANKS]",
            self.audio,
        )

    def test_command_12_has_an_independent_owner_and_dispatch(self) -> None:
        for evidence in (
            "ME_CMD_BULLET_COMPACT_UPDATE = 12",
            "ME_OWNER_BULLET_COMPACT = 6",
            "else if (command == ME_CMD_BULLET_COMPACT_UPDATE)",
            "process_bullet_compact_update_on_me(box);",
        ):
            self.assertIn(evidence, self.audio)
        self.assertRegex(
            self.header,
            r"TH07_PSP_ME_BULLET_COMPACT_UPDATE\)\s*&&\s*\\\s*"
            r"!defined\(TH07_PSP_ME_RENDER_DIRECT_LIST\)",
        )

    def test_i5_sidecar_is_uncommitted_first_and_commits_only_on_success(self) -> None:
        assert_order(
            self,
            self.render,
            "compactSeed->header.committed = 0u;",
            "me_render_stream_expand_kernel(",
            "if (result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&",
            "compactSeed->header.payloadHash = 0u;",
            'compactSeed->header.committed =\n                TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED;',
            "compactSeedCommitted = 1u;",
            "else if (result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK)",
            "if (compactSeed && !compactSeedCommitted)",
            "meLibDcacheInvalidateRange((uint32_t)compactSeed",
        )
        success_start = self.render.index(
            "if (result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&"
        )
        success_end = self.render.index(
            "else if (result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK)",
            success_start,
        )
        commit = self.render.index(
            "TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED", success_start
        )
        self.assertLess(commit, success_end)

    def test_seed_capture_has_its_own_second_generation_bracket(self) -> None:
        assert_order(
            self,
            self.capture,
            "if (me_render_stream_load_u32(",
            "generation)\n        return 0;",
            "me_bullet_compact_capture_seed(compactSeed",
            "if (compactSeed &&",
            "ME_RENDER_LIST_GENERATION_STRIDE) !=",
            "*compactWord &= ~compactBit;",
            "--compactSeed->header.candidateCount;",
            "memset(&compactSeed->slots[slot], 0",
            "return 1;",
        )
        after_capture = self.capture.split(
            "me_bullet_compact_capture_seed(compactSeed", 1
        )[1]
        self.assertIn("ME_RENDER_LIST_GENERATION_STRIDE", after_capture)

    def test_replay_epoch_is_only_an_immutable_seed_echo(self) -> None:
        self.assertIn("Capture-time replay frame echo only", self.header)
        self.assertIn("must not be compared to current", self.header)
        self.assertIn(
            "seedArea->seed.header.replayEpoch != job->replayEpoch",
            self.begin,
        )
        self.assertIn(
            "replayEpoch is an immutable seed echo", self.begin
        )
        self.assertNotIn("ReplayManager", self.begin.replace(
            "ReplayManager::frameId", ""
        ))
        self.assertIn(
            "seed->header.replayEpoch = 0x3700u + caseIndex;",
            self.selftest,
        )

    def test_compact_command_never_pays_a_whole_cache_handoff(self) -> None:
        for body in (self.begin, self.worker):
            self.assertNotIn("sceKernelDcacheWritebackAll", body)
            self.assertNotIn("sceKernelDcacheWritebackInvalidateAll", body)
            self.assertNotIn("meLibDcacheWritebackInvalidateAll", body)
        self.assertIn("sceKernelDcacheWritebackRange(", self.begin)
        self.assertIn("meLibDcacheInvalidateRange((uint32_t)seed", self.worker)
        self.assertIn("meLibDcacheWritebackRange((uint32_t)output", self.worker)

    def test_begin_poll_api_is_nonblocking_and_fail_closed(self) -> None:
        for prototype in (
            "int th07_psp_me_bullet_compact_begin(",
            "int th07_psp_me_bullet_compact_poll(",
            "th07_psp_me_bullet_compact_seed_bank(unsigned int bank);",
        ):
            self.assertIn(prototype, self.header)
        self.assertNotIn("while (", self.begin)
        self.assertNotIn("sceKernelDelayThread", self.begin)
        assert_order(
            self,
            self.begin,
            "claim_me_for_bullet_compact()",
            "seedArea->seed.header.targetDrawSeq != job->seedTargetDrawSeq",
            "gMeBulletCompactPublishedJob = *job;",
            "box->command = ME_CMD_BULLET_COMPACT_UPDATE;",
            "return 1;",
        )
        for evidence in (
            "return 0;",
            "ME_BULLET_COMPACT_TIMEOUT_US",
            "timeout_me();",
            "return -2;",
            "memcmp(&echoedJob, &gMeBulletCompactPublishedJob",
            "local.candidateCount == seedValue->header.candidateCount",
            "release_me();",
            "return outputValid ? 1 : -1;",
        ):
            self.assertIn(evidence, self.poll)

    def test_full_generation_is_authority_and_output_u16_is_only_echo(self) -> None:
        self.assertIn("live full-u32 generation == seed full-u32 generation", self.header)
        self.assertIn("u16 generation in output is only a post-seed echo", self.header)
        self.assertIn("unsigned int generation;", self.header)
        self.assertIn("unsigned short generation;", self.header)
        self.assertIn("record->generation = 0x10000u + slot + 1u;", self.selftest)
        self.assertIn(
            "(uint16_t)publishedSeed->slots[slot].generation", self.selftest
        )

    def test_boot_selftest_covers_required_density_cases(self) -> None:
        self.assertIn(
            "static const uint32_t counts[4] = {0u, 128u, 512u, 1024u};",
            self.selftest,
        )
        for evidence in (
            "th07_psp_me_bullet_compact_begin(&job)",
            "th07_psp_me_bullet_compact_poll(",
            "completion.candidateCount != count",
            "completion.inBoundsCount != count",
            "completion.noCollisionCount != count",
            'ME17 SELFTEST PASS COMPACT C0/128/512/1024 MAINRAM',
            'ME17 SELFTEST NG -> COLD REBOOT',
        ):
            self.assertIn(evidence, self.audio)

    def test_makefile_feature_gates_and_profile_are_explicit(self) -> None:
        for evidence in (
            f"{MAKE_FEATURE} ?= 0",
            f"CXXFLAGS += -D{FEATURE}",
            f"CFLAGS += -D{FEATURE}",
            "$(error PSP_ME_BULLET_COMPACT_UPDATE requires "
            "PSP_ME_RENDER_DIRECT_LIST=1)",
            "$(error PSP_ME_BULLET_COMPACT_UPDATE and "
            "PSP_ME_BULLET_FAST_UPDATE are mutually exclusive)",
            "$(error PSP_ME_BULLET_COMPACT_UPDATE is PSP-2000+ only)",
            "$(PSP_ME_BULLET_COMPACT_UPDATE)-",
        ):
            self.assertIn(evidence, self.makefile)
        profile = make_target(
            self.makefile, "psp3000-me-render-i7-sc-relief-build"
        )
        for setting in (
            "PSP_1000=0",
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_PERFORMANCE=1",
            "PSP_ME_RENDER_RAW_LIVE=1",
            "PSP_ME_RENDER_DIRECT_LIST=1",
            "PSP_ME_BULLET_FAST_UPDATE=0",
            "PSP_ME_BULLET_COMPACT_UPDATE=1",
            "PSP_MECC_AUDIO_4M=1",
        ):
            self.assertIn(setting, profile)


if __name__ == "__main__":
    unittest.main()

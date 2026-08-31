from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "TH07_PSP_ME_BULLET_FAST_UPDATE"
HARNESS = ROOT / "tests" / "me_bullet_fast_update_harness.cpp"


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


class PspMeBulletFastUpdateI6Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "psp" / "audio_me.h").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.build = function_body(cls.bullets, "PspMeBulletFastBuildJob(")
        cls.preflight = function_body(
            cls.bullets, "PspMeBulletFastOutputMatches("
        )
        cls.runner = function_body(
            cls.bullets, "PspMeBulletFastRunSynchronous("
        )
        cls.update = function_body(cls.bullets, "BulletManager::OnUpdate(")
        cls.kernel = function_body(cls.audio, "me_bullet_fast_update_kernel(")
        cls.collision = function_body(cls.audio, "me_bullet_fast_no_collision(")
        cls.low_run = function_body(
            cls.audio, "th07_psp_me_bullet_fast_update_run("
        )

    def test_me16_abi_and_every_live_layout_field_are_frozen(self) -> None:
        for evidence in (
            "TH07_PSP_ME_BULLET_FAST_UPDATE_VERSION = 0x4d453136u",
            "TH07_PSP_ME_BULLET_FAST_LAYOUT_VERSION = 0x42463131u",
            "sizeof(Th07PspMeBulletFastLayout) == 152u",
            "sizeof(Th07PspMeBulletFastJob) == 216u",
            "sizeof(Th07PspMeBulletFastSlotResult) == 16u",
            "sizeof(Th07PspMeBulletFastOutput) == 16512u",
        ):
            self.assertIn(evidence, self.header + self.bullets)
        for field in (
            "bulletBasePhys",
            "generationBasePhys",
            "activeBitsPhys",
            "spriteBasePhys",
            "bulletPosXOffset",
            "bulletPosYOffset",
            "bulletPosZOffset",
            "bulletVelocityXOffset",
            "bulletVelocityYOffset",
            "bulletVelocityZOffset",
            "bulletCurrentCommandIndexOffset",
            "bulletCommandsOffset",
            "bulletCommandTypeOffset",
            "bulletGrazeSizeXOffset",
            "vmSpriteOffset",
            "bombClearStride",
            "bombClearSizeYOffset",
        ):
            self.assertIn(f"layout.{field} =", self.build)

    def test_submit_is_after_item_update_and_before_any_bullet_write(self) -> None:
        assert_order(
            self,
            self.update,
            "g_ItemManager.OnUpdate();",
            "PspMeBulletFastRunSynchronous(arg);",
            "arg->bulletCount = 0;",
            "for (i = 0; i < kBulletCapacity; i++)",
        )
        self.assertIn("synchronously retire the read-only ME traversal", self.update)

    def test_me_eligibility_reads_u16_exflags_and_exact_command_gate(self) -> None:
        self.assertIn(
            "layout->bulletExFlagsOffset,\n            sizeof(uint16_t)",
            self.audio,
        )
        for proof in (
            "state == ME_BULLET_FAST_STATE_NORMAL",
            "me_render_stream_load_u16(\n                    bullet, job->layout.bulletExFlagsOffset)",
            "spawnDelay == 0",
            "commandIndex >= ME_BULLET_FAST_COMMAND_COUNT",
            "job->layout.bulletCommandTypeOffset",
            "candidate = me_render_stream_load_u32(",
        ):
            self.assertIn(proof, self.kernel)

    def test_me_bounds_and_negative_collision_are_exact_and_conservative(self) -> None:
        for proof in (
            "halfWidth + newPosX < 0.0f",
            "newPosX - halfWidth > playfieldRight",
            "halfHeight + newPosY < 0.0f",
            "newPosY - halfHeight > playfieldBottom",
            "me_bullet_fast_no_collision(",
        ):
            self.assertIn(proof, self.kernel)
        for proof in (
            "job->playerState == ME_BULLET_FAST_PLAYER_STATE_BORDER",
            "job->bombClearHighWater",
            "if (bombZ != 0.0f)",
            "else if (bombSizeY != 0.0f)",
            "distanceSquared < radiusSquared",
            "playerHitboxLeftBits",
            "playerGrazeLeftBits",
            "bulletLeft - 20.0f",
            "*valid = 0",
        ):
            self.assertIn(proof, self.collision)

    def test_low_level_cache_timeout_and_output_contract_is_fail_closed(self) -> None:
        assert_order(
            self,
            self.low_run,
            "claim_me_for_bullet_fast()",
            "sceKernelDcacheWritebackAll();",
            "box->command = ME_CMD_BULLET_FAST_UPDATE;",
            "while (box->status != ME_STAT_DONE)",
            "timeout_me();",
            "return -1;",
            "sceKernelDcacheInvalidateRange(",
            "memcmp(&echoedJob, &gMeBulletFastPublishedJob",
            "me_bullet_fast_guards_match(",
            "me_bullet_fast_output_valid(",
            "release_me();",
        )
        timeout = self.low_run.index("return -1;")
        self.assertNotIn("release_me();", self.low_run[:timeout].split("timeout_me();")[-1])
        self.assertIn(
            "__atomic_store_n(&gMeBulletFastInFlight, 0u, __ATOMIC_RELEASE);",
            self.low_run[timeout:],
        )

    def test_synchronous_run_never_returns_to_live_mutation_after_timeout(self) -> None:
        assert_order(
            self,
            self.runner,
            "th07_psp_me_bullet_fast_update_run(",
            "if (result < 0)",
            "PspMeRenderRawFailStop(",
            "if (result != 1 ||",
            "!PspMeBulletFastOutputMatches(",
            "return output;",
        )
        self.assertIn("return nullptr;", self.runner)

    def test_preflight_is_all_or_nothing_and_revalidates_every_candidate(self) -> None:
        for proof in (
            "completion.firstBadSlot != 0xffffffffu",
            "completion.candidateCount > completion.activeCount",
            "for (u32 slot = 0u; slot < TH07_PSP_ME_BULLET_FAST_MAX_SLOTS;",
            "output->candidateBits[slot >> 5u]",
            "TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE",
            "manager->PspIsBulletSlotTracked",
            "manager->pspMeRenderSlotGenerations[slot]",
            "PspMeBulletFastIsEligible(bullet)",
            "g_Player.playerState == PLAYER_STATE_BORDER",
            "candidateCount == completion.candidateCount",
            "noCollisionCount == completion.noCollisionCount",
        ):
            self.assertIn(proof, self.preflight)
        self.assertLess(
            self.runner.index("!PspMeBulletFastOutputMatches("),
            self.runner.index("return output;"),
        )

    def test_slot_results_are_adopted_just_in_time_in_original_order(self) -> None:
        assert_order(
            self,
            self.update,
            "blockIdx = 0;",
            "bullet = arg->BulletAt(blockIdx);",
            "bullet->RunCommands();",
            "pspMeBulletFastOutput->candidateBits[slot >> 5u]",
            "std::memcpy(&bullet->pos.x",
            "std::memcpy(&bullet->pos.y",
            "std::memcpy(&bullet->pos.z",
            "bullet->pos += bullet->velocity;",
            "TH07_PSP_ME_BULLET_FAST_SLOT_IN_BOUNDS",
        )
        assert_order(
            self,
            self.update,
            "blockIdx--;",
            "if (blockIdx < 0)",
            "blockIdx = kBulletCapacity - 1;",
        )

    def test_negative_collision_replays_item_type_then_keeps_canonical_tail(self) -> None:
        assert_order(
            self,
            self.update,
            "do_collision:",
            "TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION",
            "g_Player.itemType = ITEM_POINT_BULLET;",
            "goto do_sprite_anim;",
            "do_sprite_anim:",
            "bullet->sprites.spriteBullet.currentInstruction",
            "g_AnmManager->ExecuteScript(&bullet->sprites.spriteBullet);",
            "update_timers:",
            "bullet->timer1++;",
            "bullet->timer2++;",
            "PspMeRenderCaptureFusedRecord(",
            "bullet->next = arg->bulletsPtrs",
            "PspMeRenderPublishFusedCapture(arg);",
        )

    def test_i5_profile_does_not_enable_i6(self) -> None:
        i5 = make_target(
            self.makefile, "psp3000-me-render-i5-direct-list-build"
        )
        self.assertIn("PSP_ME_RENDER_DIRECT_LIST=1", i5)
        self.assertNotIn("PSP_ME_BULLET_FAST_UPDATE=1", i5)
        self.assertIn(
            "TH07_PSP_ME_BULLET_FAST_UPDATE requires the I-ME5 direct-list owner",
            self.bullets,
        )

    def test_i6_is_an_explicit_i5_psp3000_profile(self) -> None:
        self.assertIn("PSP_ME_BULLET_FAST_UPDATE ?= 0", self.makefile)
        self.assertIn(f"CXXFLAGS += -D{FEATURE}", self.makefile)
        self.assertIn(f"CFLAGS += -D{FEATURE}", self.makefile)
        self.assertIn(
            "$(error PSP_ME_BULLET_FAST_UPDATE requires "
            "PSP_ME_RENDER_DIRECT_LIST=1)",
            self.makefile,
        )
        self.assertIn(
            "$(error PSP_ME_BULLET_FAST_UPDATE is PSP-2000+ only)",
            self.makefile,
        )
        i6 = make_target(
            self.makefile, "psp3000-me-render-i6-bullet-fast-build"
        )
        for setting in (
            "PSP_1000=0",
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_GE_CONSUME=1",
            "PSP_ME_RENDER_PERFORMANCE=1",
            "PSP_ME_RENDER_RAW_LIVE=1",
            "PSP_ME_RENDER_DIRECT_LIST=1",
            "PSP_ME_BULLET_FAST_UPDATE=1",
            "PSP_MECC_AUDIO_4M=1",
            "PSP_AUDIO4M_BUILD_ID=0x26083016u",
        ):
            self.assertIn(setting, i6)
        self.assertIn("$(PSP_ME_BULLET_FAST_UPDATE)-", self.makefile)

    def test_motion_bounds_and_tail_harness_is_bit_exact(self) -> None:
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("host C++ compiler is required")
        with tempfile.TemporaryDirectory(prefix="th07-me16-") as temporary:
            executable = Path(temporary) / "me16_harness"
            subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(HARNESS),
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
        self.assertIn("8 ME16 motion/bounds/tail cases bit-exact", completed.stdout)


if __name__ == "__main__":
    unittest.main()

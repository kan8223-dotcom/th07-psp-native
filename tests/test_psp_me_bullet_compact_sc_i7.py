from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, name: str) -> str:
    """Return a named C/C++ function body, rejecting a bare prototype."""
    match = re.search(
        rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*(?:const\s*)?\{{",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"missing function definition: {name}")
    opening = source.index("{", match.start())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise AssertionError(f"unterminated function: {name}")


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


class PspMeBulletCompactScI7Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.header = (ROOT / "psp" / "audio_me.h").read_text(
            encoding="utf-8"
        )
        cls.audio = (ROOT / "psp" / "audio_me.c").read_text(
            encoding="utf-8"
        )
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

    def test_priority_9_launcher_is_registered_and_never_waits(self) -> None:
        register = function_body(self.bullets, "BulletManager::RegisterChain")
        launch = function_body(self.bullets, "PspMeBulletCompactEarlyLaunch")

        for proof in (
            "g_PspMeBulletCompactLaunchChain.callback",
            "PspMeBulletCompactEarlyLaunch",
            "g_Chain.AddToCalcChain(&g_PspMeBulletCompactLaunchChain, 9)",
        ):
            self.assertIn(proof, register)
        self.assertIn("th07_psp_me_bullet_compact_begin(&job)", launch)
        for forbidden in (
            "while (",
            "sceKernelDelayThread",
            "sceKernelWait",
            "drain_live",
        ):
            self.assertNotIn(forbidden, launch)

    def test_launcher_retains_exact_seed_identity_and_rejects_restart(self) -> None:
        launch = function_body(self.bullets, "PspMeBulletCompactEarlyLaunch")

        # The retained I-ME5 sidecar belongs to the immediately following draw,
        # not merely to the same bank.  A replay/calc RESTART may invoke p9
        # again before that draw, and must not submit a second command.
        for proof in (
            "lastLaunchDrawSeq",
            "gPspMeRenderShadow.drawSeq",
            "th07_psp_me_bullet_compact_seed_bank(",
            "TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED",
            "TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION",
            "TH07_PSP_ME_BULLET_COMPACT_BACKEND_MAIN_RAM",
            "seed->header.frameSeq",
            "seed->header.targetDrawSeq",
            "seed->header.stageEpoch",
            "seed->header.managerEpoch",
            "gPspMeRenderShadow.stageEpoch",
            "gPspMeRenderShadow.managerEpoch",
            "g_BulletManager.updateCount",
            "managerUpdateCount",
        ):
            self.assertIn(proof, launch)
        self.assertRegex(
            launch,
            r"lastLaunchDrawSeq\s*==\s*gPspMeRenderShadow\.drawSeq",
        )
        self.assertRegex(
            launch,
            r"seed->header\.targetDrawSeq\s*!=\s*"
            r"gPspMeRenderShadow\.drawSeq",
        )
        self.assertRegex(
            launch,
            r"seed->header\.frameSeq\s*\+\s*1u\s*!=\s*"
            r"seed->header\.targetDrawSeq",
        )
        assert_order(
            self,
            launch,
            "lastLaunchDrawSeq",
            "th07_psp_me_bullet_compact_seed_bank(",
            "job.seedFrameSeq",
            "job.seedTargetDrawSeq",
            "managerUpdateCount",
            "th07_psp_me_bullet_compact_begin(&job)",
        )

    def test_priority_12_poll_is_single_probe_and_precedes_bullet_writes(self) -> None:
        poll = function_body(self.bullets, "PspMeBulletCompactPollForUpdate")
        update = function_body(self.bullets, "BulletManager::OnUpdate")

        self.assertEqual(
            poll.count("th07_psp_me_bullet_compact_poll("),
            1,
            "priority-12 may probe once but must never wait for ME",
        )
        for forbidden in (
            "while (",
            "sceKernelDelayThread",
            "sceKernelWait",
            "drain_live",
        ):
            self.assertNotIn(forbidden, poll)
        for proof in (
            "managerUpdateCount",
            "manager->updateCount",
            "seedTargetDrawSeq",
            "gPspMeRenderShadow.drawSeq",
            "stageEpoch",
            "managerEpoch",
        ):
            self.assertIn(proof, poll)
        assert_order(
            self,
            update,
            "g_ItemManager.OnUpdate();",
            "PspMeBulletCompactPollForUpdate(arg)",
            "arg->bulletCount = 0;",
            "for (i = 0; i < kBulletCapacity; i++)",
        )

    def test_jit_seed_adoption_checks_full_generation_and_all_ten_words(self) -> None:
        adopt = function_body(self.bullets, "PspMeBulletCompactTryAdoptSeed")

        self.assertIn("pspMeRenderSlotGenerations", adopt)
        self.assertIn("seedSlot.generation", adopt)
        self.assertNotRegex(
            adopt,
            r"static_cast\s*<\s*u16\s*>\s*\([^)]*"
            r"pspMeRenderSlotGenerations",
            "the output u16 echo must never replace full-u32 seed authority",
        )

        raw_fields = (
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
        )
        for field in raw_fields:
            self.assertIn(f"seedSlot.{field}", adopt)
        for live_value in (
            "bullet->pos.x",
            "bullet->pos.y",
            "bullet->pos.z",
            "bullet->velocity.x",
            "bullet->velocity.y",
            "bullet->velocity.z",
            "bullet->sprites.spriteBullet.sprite->widthPx",
            "bullet->sprites.spriteBullet.sprite->heightPx",
            "bullet->sprites.grazeSize.x",
            "bullet->sprites.grazeSize.y",
        ):
            self.assertIn(live_value, adopt)

        first_motion_write = min(
            position
            for position in (
                adopt.find("bullet->pos.x ="),
                adopt.find("std::memcpy(&bullet->pos.x"),
            )
            if position >= 0
        )
        self.assertLess(adopt.index("seedSlot.generation"), first_motion_write)
        for field in raw_fields:
            self.assertLess(adopt.index(f"seedSlot.{field}"), first_motion_write)
        self.assertIn("seedSlot.nextPosXBits", adopt[first_motion_write:])
        self.assertIn("seedSlot.nextPosYBits", adopt[first_motion_write:])
        self.assertIn("seedSlot.nextPosZBits", adopt[first_motion_write:])

    def test_no_collision_snapshot_is_rechecked_per_use_and_latches_off(self) -> None:
        snapshot = function_body(
            self.bullets, "PspMeBulletCompactPlayerSnapshotMatches"
        )
        update = function_body(self.bullets, "BulletManager::OnUpdate")
        collision_start = update.index("do_collision:")
        collision_end = update.index("g_Player.CheckGraze", collision_start)
        collision = update[collision_start:collision_end]

        for proof in (
            "g_Player.playerState",
            "g_Player.pspBombClearHighWater",
            "playerGrazeLeftBits",
            "playerGrazeTopBits",
            "playerGrazeRightBits",
            "playerGrazeBottomBits",
            "playerHitboxLeftBits",
            "playerHitboxTopBits",
            "playerHitboxRightBits",
            "playerHitboxBottomBits",
        ):
            self.assertIn(proof, snapshot)
        self.assertRegex(snapshot, r"g_Player\.pspBombClearHighWater\s*==\s*0")

        # This mutable latch is tested inside the loop, immediately before
        # every ME NO_COLLISION use.  Once any earlier bullet changes Player or
        # bomb state it may only move from true to false for the rest of p12.
        for proof in (
            "pspMeBulletCompactCollisionAllowed",
            "PspMeBulletCompactPlayerSnapshotMatches(",
            "pspMeBulletCompactCollisionAllowed = false;",
            "TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION",
            "g_Player.itemType = ITEM_POINT_BULLET;",
            "goto do_sprite_anim;",
        ):
            self.assertIn(proof, collision)
        self.assertNotIn("pspMeBulletCompactCollisionAllowed = true;", collision)
        assert_order(
            self,
            collision,
            "TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION",
            "PspMeBulletCompactPlayerSnapshotMatches(",
            "pspMeBulletCompactCollisionAllowed = false;",
            "g_Player.itemType = ITEM_POINT_BULLET;",
            "goto do_sprite_anim;",
        )

    def test_conservative_broadphase_keeps_canonical_item_type_side_effect(self) -> None:
        update = function_body(self.bullets, "BulletManager::OnUpdate")
        collision_start = update.index("do_collision:")
        collision_end = update.index("g_Player.CheckGraze", collision_start)
        collision = update[collision_start:collision_end]

        for proof in (
            "Th07PspBulletCollisionDefinitelyClear(",
            "g_Player.pspBombClearHighWater",
            "g_Player.hitboxTopLeft.x",
            "g_Player.hitboxTopLeft.y",
            "g_Player.hitboxBottomRight.x",
            "g_Player.hitboxBottomRight.y",
            "g_Player.grazeTopLeft.x",
            "g_Player.grazeTopLeft.y",
            "g_Player.grazeBottomRight.x",
            "g_Player.grazeBottomRight.y",
            "g_Player.itemType = ITEM_POINT_BULLET;",
        ):
            self.assertIn(proof, collision)
        for gate in (
            "!bullet->grazed",
            "bullet->timer2.GetCurrent() >= 16",
            "PLAYER_STATE_DEAD",
            "PLAYER_STATE_SPAWNING",
            "PLAYER_STATE_BORDER",
        ):
            self.assertIn(gate, collision)
        broadphase = collision.index("Th07PspBulletCollisionDefinitelyClear(")
        item_type = collision.index("g_Player.itemType = ITEM_POINT_BULLET;", broadphase)
        skip = collision.index("goto do_sprite_anim;", item_type)
        self.assertLess(broadphase, item_type)
        self.assertLess(item_type, skip)

    def test_loop_tail_and_priority_18_probe_without_blocking_render(self) -> None:
        update = function_body(self.bullets, "BulletManager::OnUpdate")
        finish = function_body(self.bullets, "PspMeBulletCompactFinishFrame")
        retire = function_body(
            self.bullets, "PspMeBulletCompactRetireBeforeRender"
        )
        sentinel = function_body(
            self.bullets, "PspMeRenderCalcCompleteSentinel"
        )

        loop_tail = update.rfind("PspMeBulletCompactFinishFrame(arg)")
        self.assertGreater(loop_tail, update.index("for (i = 0; i < kBulletCapacity; i++)"))
        for body in (finish, retire):
            for forbidden in (
                "while (",
                "sceKernelDelayThread",
                "sceKernelWait",
                "drain_live",
            ):
                self.assertNotIn(forbidden, body)
        self.assertLessEqual(
            finish.count("th07_psp_me_bullet_compact_poll(") +
            finish.count("PspMeBulletCompactPollForUpdate("),
            1,
        )
        self.assertLessEqual(
            retire.count("th07_psp_me_bullet_compact_poll(") +
            retire.count("PspMeBulletCompactPollForUpdate("),
            1,
        )
        assert_order(
            self,
            sentinel,
            "compactBlockedRender",
            "PspMeBulletCompactRetireBeforeRender(",
            "if (!compactBlockedRender",
            "PspMeRenderCorrectnessAfterCalc(",
        )

    def test_time_stop_still_polls_before_early_return(self) -> None:
        update = function_body(self.bullets, "BulletManager::OnUpdate")
        stop_start = update.index("if (g_GameManager.isTimeStopped)")
        stop_end = update.index("return CHAIN_CALLBACK_RESULT_CONTINUE;", stop_start)
        stop_path = update[stop_start:stop_end]
        self.assertIn("PspMeBulletCompactFinishFrame(arg)", stop_path)

    def test_teardown_drains_live_owner_and_poison_is_cold_reboot(self) -> None:
        self.assertIn(
            "int th07_psp_me_bullet_compact_drain_live(void);", self.header
        )
        drain = function_body(
            self.bullets, "PspMeBulletCompactDrainForManagerDelete"
        )
        deleted = function_body(self.bullets, "PspMeRenderManagerDeleted")
        poll = function_body(self.bullets, "PspMeBulletCompactPollForUpdate")

        assert_order(
            self,
            drain,
            "th07_psp_me_bullet_compact_drain_live()",
            "PspMeRenderRawFailStop(",
        )
        self.assertIn("COLD REBOOT", drain)
        self.assertLess(
            deleted.index("PspMeBulletCompactDrainForManagerDelete()"),
            deleted.index("gPspMeRenderShadow.managerActive = false"),
        )
        self.assertRegex(poll, r"(?:pollResult|result)\s*==\s*-2")
        self.assertIn("PspMeRenderRawFailStop(", poll)
        self.assertIn("COLD REBOOT", poll)

    def test_i7_profile_disables_old_scattered_me16_path(self) -> None:
        profile = make_target(
            self.makefile, "psp3000-me-render-i7-sc-relief-build"
        )
        for setting in (
            "PSP_1000=0",
            "PSP_ME_BULLET_FAST_UPDATE=0",
            "PSP_ME_BULLET_COMPACT_UPDATE=1",
            "PSP_ME_ITEM_RENDER_STREAM=1",
            "PSP_BULLET_COLLISION_BROADPHASE=1",
        ):
            self.assertIn(setting, profile)
        self.assertNotIn("PSP_ME_BULLET_FAST_UPDATE=1", profile)
        self.assertNotIn("PspMeBulletFastRunSynchronous(arg)", profile)


if __name__ == "__main__":
    unittest.main()

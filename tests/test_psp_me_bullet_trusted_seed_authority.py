from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY"
HARNESS = ROOT / "tests" / "me_bullet_trusted_seed_authority_harness.cpp"


def body(source: str, signature: str) -> str:
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


class PspMeBulletTrustedSeedAuthorityContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.ecl = (ROOT / "src" / "EnemyEclInstr.cpp").read_text(
            encoding="utf-8"
        )
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.adopt = body(
            cls.bullets, "bool PspMeBulletCompactTryAdoptTrustedSeed("
        )
        cls.update = body(cls.bullets, "u32 BulletManager::OnUpdate(")

    def test_feature_requires_compact_seed_and_mutation_epoch_owner(self) -> None:
        prefix = self.bullets[: self.bullets.index("namespace")]
        self.assertIn(
            f"defined({FEATURE}) && \\\n    !defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)",
            prefix,
        )
        self.assertIn(
            f"defined({FEATURE}) && \\\n    !defined(TH07_PSP_ME_RENDER_PERFORMANCE)",
            prefix,
        )
        for evidence in (
            "PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY ?= 0",
            "-DTH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY",
            "requires PSP_ME_BULLET_COMPACT_UPDATE=1",
            "requires PSP_ME_RENDER_PERFORMANCE=1",
            "PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY is PSP-2000+ only",
            "$(PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)",
        ):
            self.assertIn(evidence, self.makefile)

    def test_identity_binds_post_calc_mutation_epoch_end_to_end(self) -> None:
        identity = self.bullets[
            self.bullets.index("struct PspMeBulletCompactIdentity") :
            self.bullets.index("struct PspMeBulletCompactScState")
        ]
        self.assertIn("u32 managerMutationEpoch;", identity)
        self.assertIn(
            "left.managerMutationEpoch == right.managerMutationEpoch",
            self.bullets,
        )
        for evidence in (
            "gPspMeRenderCorrectness.managerMutationEpoch !=\n"
            "            manager->pspMeRenderMutationEpoch",
            "gPspMeRenderCorrectness.managerMutationEpoch,\n"
            "        gPspMeRenderCorrectness.managerUpdateCount",
            "manager->pspMeRenderMutationEpoch ==\n"
            "            completedIdentity.managerMutationEpoch",
            "pspMeBulletCompactIdentity.managerMutationEpoch ==\n"
            "            arg->pspMeRenderMutationEpoch",
        ):
            self.assertIn(evidence, self.bullets)

    def test_trusted_adoption_is_structural_not_scattered_aos_revalidation(self) -> None:
        for required in (
            "bullet->state != BULLET_NORMAL",
            "PspIsBulletSlotTracked",
            "candidateBits",
            "pspMeRenderSlotGenerations[slot] != seedSlot.generation",
            "seedSlot.reserved != 0u",
            "allowedSeedFlags",
            "result.posXBits == seedSlot.nextPosXBits",
            "std::memcpy(&bullet->pos.x",
        ):
            self.assertIn(required, self.adopt)

        for forbidden in (
            "PspMeBulletFastIsEligible",
            "bullet->velocity",
            "bullet->spawnDelay",
            "bullet->exFlags",
            "bullet->curCmdIdx",
            "spriteBullet.sprite",
            "grazeSize",
            "spriteWidthBits !=",
            "seedSlot.posXBits !=",
        ):
            self.assertNotIn(forbidden, self.adopt)

    def test_trusted_motion_precedes_and_bypasses_canonical_frontend(self) -> None:
        normal = self.update[
            self.update.index("case BULLET_NORMAL:") :
            self.update.index("do_collision:")
        ]
        trusted = normal.index("PspMeBulletCompactTryAdoptTrustedSeed(")
        fallback = normal.index("if (!pspMeBulletCompactTrustedMotion)")
        commands = normal.index("bullet->RunCommands();")
        motion = normal.index("bullet->pos += bullet->velocity;")
        self.assertLess(trusted, fallback)
        self.assertLess(fallback, commands)
        self.assertLess(commands, motion)
        self.assertIn(
            "pspMeBulletCompactTrustedMotion || bullet->spawnDelay == 0",
            normal,
        )

    def test_player_snapshot_is_latched_once_and_after_canonical_calls(self) -> None:
        loop = self.update.index("for (i = 0; i < kBulletCapacity; i++)")
        initial = self.update.index(
            "PspMeBulletCompactPlayerSnapshotMatches(\n"
            "                    gPspMeBulletCompactSc.job)"
        )
        self.assertLess(initial, loop)

        collision = self.update[
            self.update.index("do_collision:") :
            self.update.index("case BULLET_SPAWNING_FAST:")
        ]
        trusted_branch = collision[
            collision.index(f"#if defined({FEATURE})") :
            collision.index("#else")
        ]
        self.assertNotIn("PspMeBulletCompactPlayerSnapshotMatches", trusted_branch)
        self.assertEqual(collision.count("g_Player.CheckGraze("), 1)
        self.assertEqual(collision.count("g_Player.CalcKillboxCollision("), 1)
        self.assertGreaterEqual(
            collision.count("PspMeBulletCompactPlayerSnapshotMatches("), 3
        )

    def test_every_direct_enemy_bullet_writer_has_a_barrier(self) -> None:
        mutators = (
            "ExInsAliceCurveBullets",
            "ExInsTurnBulletsIntoOtherBullets",
            "ExInsDespawnLargeBulletAndSavePos",
            "ExInsSplitBulletsOrShootBackwards",
            "ExInsReflectBulletsFromLasers",
            "ExInsShootBulletsAlongLaser",
            "ExInsYoumuSetGameSpeed",
            "ExInsYoumuRestoreGameSpeed",
            "ExInsBurstLargeBullets",
            "ExInsYoumuCurveBulletsBelow",
            "ExInsYoumuRedirectBulletsToPlayer",
            "ExInsYuyukoButterflySpawnEnemy",
            "ExInsBurstLargeBullets2",
        )
        for mutator in mutators:
            fn = body(self.ecl, f"void EnemyEclInstr::{mutator}(")
            self.assertIn(
                "PSP_ECL_MARK_BULLET_MUTATION();", fn, mutator
            )

        readonly = body(
            self.ecl,
            "void EnemyEclInstr::ExInsYuyukoCountButterflyBullets(",
        )
        self.assertNotIn("PSP_ECL_MARK_BULLET_MUTATION", readonly)

    def test_host_harness_proves_bit_identity_and_fail_closed_authority(self) -> None:
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "trusted-seed-authority"
            build = subprocess.run(
                [
                    compiler,
                    "-std=gnu++17",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(HARNESS),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn(
                "trusted seed: 8 bit-exact, epoch/gen fail-closed",
                run.stdout,
            )


if __name__ == "__main__":
    unittest.main()

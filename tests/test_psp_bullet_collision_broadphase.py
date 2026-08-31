from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "tests" / "psp_bullet_collision_broadphase_harness.cpp"


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


class PspBulletCollisionBroadphaseContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "src" / "PspBulletCollisionBroadphase.hpp"
        ).read_text(encoding="utf-8")
        cls.player = (ROOT / "src" / "Player.cpp").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.gate = function_body(
            cls.header, "Th07PspBulletCollisionDefinitelyClear("
        )
        cls.bomb = function_body(cls.player, "Player::CheckBombGraze(")
        cls.killbox = function_body(cls.player, "Player::CalcKillboxCollision(")
        cls.graze = function_body(cls.player, "Player::CheckGraze(")
        cls.update = function_body(cls.bullets, "BulletManager::OnUpdate(")

    def test_invalid_dynamic_geometry_bomb_and_border_fail_closed(self) -> None:
        for evidence in (
            "borderActive",
            "bombClearHighWater != 0",
            "sizeX < 0.0f",
            "sizeY < 0.0f",
            "!std::isfinite(posX)",
            "!std::isfinite(posY)",
            "!std::isfinite(sizeX)",
            "!std::isfinite(sizeY)",
            "!std::isfinite(bulletLeft)",
            "!std::isfinite(bulletBottom)",
            "!std::isfinite(expandedLeft)",
            "!std::isfinite(expandedBottom)",
        ):
            with self.subTest(evidence=evidence):
                self.assertIn(evidence, self.gate)

    def test_hitbox_uses_the_canonical_strict_separation_boundaries(self) -> None:
        for broadphase_term, canonical_term in (
            ("hitLeft > bulletRight", "this->hitboxTopLeft.x > killboxBottomRight.x"),
            ("hitRight < bulletLeft", "this->hitboxBottomRight.x < killboxTopLeft.x"),
            ("hitTop > bulletBottom", "this->hitboxTopLeft.y > killboxBottomRight.y"),
            ("hitBottom < bulletTop", "this->hitboxBottomRight.y < killboxTopLeft.y"),
        ):
            with self.subTest(term=broadphase_term):
                self.assertIn(broadphase_term, self.gate)
                self.assertIn(canonical_term, self.killbox)
        self.assertNotIn(">=", self.gate)
        self.assertNotIn("<=", self.gate)

    def test_graze_uses_the_same_twenty_pixel_expansion_and_boundaries(self) -> None:
        for evidence in (
            "bulletLeft - 20.0f",
            "bulletTop - 20.0f",
            "bulletRight + 20.0f",
            "bulletBottom + 20.0f",
            "grazeLeft > expandedRight",
            "grazeRight < expandedLeft",
            "grazeTop > expandedBottom",
            "grazeBottom < expandedTop",
        ):
            self.assertIn(evidence, self.gate)
        for evidence in (
            "center->x - size->x / 2.0f - 20.0f",
            "center->y - size->y / 2.0f - 20.0f",
            "center->x + size->x / 2.0f + 20.0f",
            "center->y + size->y / 2.0f + 20.0f",
            "this->grazeTopLeft.x > bulletBottomRight.x",
            "this->grazeBottomRight.x < bulletTopLeft.x",
            "this->grazeTopLeft.y > bulletBottomRight.y",
            "this->grazeBottomRight.y < bulletTopLeft.y",
        ):
            self.assertIn(evidence, self.graze)

    def test_any_bomb_prefix_defers_both_bomb_shapes_to_canonical(self) -> None:
        self.assertIn("const i32 bombClearLimit = this->pspBombClearHighWater;", self.bomb)
        self.assertIn("bombProjectile->pos.z != 0.0f", self.bomb)
        self.assertIn("bombProjectile->size.y != 0.0", self.bomb)
        self.assertIn("bombX * bombX + bombY * bombY", self.bomb)
        self.assertIn("bombClearHighWater != 0", self.gate)

    def test_graze_gate_matches_timer_grazed_and_player_state_conditions(self) -> None:
        self.assertIn(
            "if (!bullet->grazed && bullet->timer2.GetCurrent() >= 16)",
            self.update,
        )
        self.assertIn("g_Player.CheckGraze(&bullet->pos", self.update)
        self.assertIn("PLAYER_STATE_DEAD || this->playerState == PLAYER_STATE_SPAWNING", self.graze)
        self.assertLess(
            self.graze.index("CheckBombGraze(center, size)"),
            self.graze.index("PLAYER_STATE_DEAD"),
        )
        self.assertIn("grazeCallCanObserve may be false only when", self.header)

    def test_border_and_negative_call_side_effects_remain_explicit_contracts(self) -> None:
        self.assertIn("this->playerState == PLAYER_STATE_BORDER", self.killbox)
        self.assertIn("g_Player.BreakBorder();", self.killbox)
        self.assertIn("this->itemType = ITEM_POINT_BULLET;", self.killbox)
        self.assertIn("this->itemType = ITEM_POINT_BULLET;", self.graze)
        self.assertIn("must still reproduce the canonical ITEM_POINT_BULLET", self.header)
        self.assertIn("PLAYER_STATE_BORDER", self.header)

    def test_host_harness_proves_true_implies_both_canonical_paths_are_clear(self) -> None:
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "psp-bullet-collision-broadphase"
            build = subprocess.run(
                [
                    compiler,
                    "-std=gnu++17",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT),
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
            self.assertIn("broadphase conservative: 21 directed, 1458 grid", run.stdout)


if __name__ == "__main__":
    unittest.main()

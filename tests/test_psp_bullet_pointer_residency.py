from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PspBulletPointerResidencyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (ROOT / "src" / "BulletManager.cpp").read_text(encoding="utf-8")
        update_start = cls.source.index("u32 BulletManager::OnUpdate")
        cls.update = cls.source[
            update_start : cls.source.index("void Bullet::Draw", update_start)
        ]

    def test_register_barrier_follows_the_single_bulletat_lookup(self) -> None:
        lookup = self.update.index("bullet = arg->BulletAt(blockIdx);")
        barrier = self.update.index('asm volatile("" : "+r"(bullet));', lookup)
        occupancy = self.update.index("PspIsBulletSlotTracked(blockIdx)", lookup)
        self.assertLess(lookup, barrier)
        self.assertLess(barrier, occupancy)

    def test_register_barrier_is_psp_only_and_has_no_memory_clobber(self) -> None:
        lookup = self.update.index("bullet = arg->BulletAt(blockIdx);")
        psp_guard = self.update.index("#if defined(TH07_PSP)", lookup)
        barrier = self.update.index('asm volatile("" : "+r"(bullet));', psp_guard)
        psp_end = self.update.index("#endif", barrier)
        self.assertLess(psp_guard, barrier)
        self.assertLess(barrier, psp_end)
        self.assertNotIn('"memory"', self.update[psp_guard:psp_end])

    def test_update_still_uses_the_pointer_for_all_state_work(self) -> None:
        self.assertIn("switch (bullet->state)", self.update)
        self.assertIn("bullet->RunCommands();", self.update)
        self.assertIn("bullet->sprites.spriteBullet.currentInstruction", self.update)
        self.assertIn("bullet->timer1++;", self.update)
        self.assertIn("bullet->timer2++;", self.update)


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def target_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(r"\n(?=[A-Za-z0-9_.-]+:)", makefile[start + 1 :])
    return makefile[start:] if match is None else makefile[start : start + match.start() + 1]


class PspMeI8r3CacheSafeProfile(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        cls.audio_me = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")
        cls.target = target_body(
            cls.makefile, "psp3000-me-render-i8r3-cache-safe-build"
        )

    def test_is_unique_psp3000_recovery_identity(self) -> None:
        self.assertIn("PSP_AUDIO4M_BUILD_ID=0x2608311bu", self.target)
        self.assertIn("TH07 PSP ME I-ME8R3 CACHE-SAFE", self.target)
        self.assertIn("PSP_1000=0", self.target)
        self.assertNotIn("psp1000-build", self.target)
        self.assertIn("MERW I-ME8R3 CACHE-SAFE", self.main)

    def test_keeps_i7_cache_contract_and_excludes_failed_features(self) -> None:
        self.assertIn("PSP_ME_EFFECT_RENDER_STREAM=0", self.target)
        self.assertIn("PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0", self.target)
        self.assertIn("PSP_ME_ITEM_RENDER_STREAM=1", self.target)
        self.assertIn("PSP_ME_BULLET_COMPACT_UPDATE=1", self.target)
        self.assertIn("PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=1", self.target)
        self.assertIn("PSP_ME_STARTUP_BREADCRUMBS=1", self.target)

    def test_cache_fences_are_unconditional(self) -> None:
        self.assertIn(
            "sceKernelDcacheWritebackInvalidateRange(outputArea->vertices",
            self.audio_me,
        )
        self.assertIn(
            "sceKernelDcacheWritebackInvalidateRange(runArea->runs",
            self.audio_me,
        )
        self.assertIn("hardware-proven I-ME7 fence", self.audio_me)


if __name__ == "__main__":
    unittest.main()

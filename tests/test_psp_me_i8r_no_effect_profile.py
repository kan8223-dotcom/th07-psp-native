from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def target_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(r"\n(?=[A-Za-z0-9_.-]+:)", makefile[start + 1 :])
    return makefile[start:] if match is None else makefile[start : start + match.start() + 1]


class PspMeI8rNoEffectProfile(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        cls.audio_me = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")
        cls.target = target_body(
            cls.makefile, "psp3000-me-render-i8r-no-effect-build"
        )

    def test_keeps_safe_i8_relief_and_compiles_effect_out(self) -> None:
        for setting in (
            "PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=1",
            "PSP_ME_ITEM_RENDER_STREAM=1",
            "PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=1",
            "PSP_GUI_TILE_BATCH=1",
        ):
            self.assertIn(setting, self.target)
        self.assertIn("PSP_ME_EFFECT_RENDER_STREAM=0", self.target)

    def test_has_unique_identity_and_no_psp1000_build(self) -> None:
        self.assertIn("PSP_AUDIO4M_BUILD_ID=0x26083119u", self.target)
        self.assertIn("TH07 PSP ME I-ME8R NO-EFFECT", self.target)
        self.assertIn("PSP_1000=0", self.target)
        self.assertNotIn("psp1000-build", self.target)

    def test_runtime_banner_cannot_claim_all_in(self) -> None:
        self.assertIn("MERW I-ME8R NO-EFFECT; TRUSTED-SEED=1", self.main)
        self.assertIn("#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)", self.main)

    def test_recovery_profile_has_startup_breadcrumbs(self) -> None:
        for note in (
            "MERW STREAM BEGIN E0 L1",
            "MERW STREAM BASE PASS",
            "MERW STREAM RAW PASS",
            "MERW STREAM DIRECT PASS",
        ):
            self.assertIn(note, self.audio_me)


if __name__ == "__main__":
    unittest.main()

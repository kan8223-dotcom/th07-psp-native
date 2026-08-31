import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def target_recipe(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(r"\n[^\t\n#][^\n]*:\s*\n", makefile[start + 1 :])
    end = len(makefile) if match is None else start + 1 + match.start()
    return makefile[start:end]


class PspMeI8AllInProfile(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        cls.recipe = target_recipe(
            cls.makefile, "psp3000-me-render-i8-allin-build"
        )

    def test_independent_feature_switches_are_profile_stamped(self):
        profile_stamp = self.makefile.split("\nPROFILE_STAMP :=", 1)[1].split(
            "\n", 1
        )[0]
        for variable in (
            "PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY",
            "PSP_ME_EFFECT_RENDER_STREAM",
            "PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP",
            "PSP_GUI_TILE_BATCH",
        ):
            self.assertIn(f"{variable} ?= 0", self.makefile)
            self.assertIn(f"$({variable})", profile_stamp)

    def test_allin_is_an_explicit_psp3000_superset_not_a_1000_build(self):
        expected = {
            "PSP_1000": "0",
            "PSP_ME_RENDER_WORKER": "1",
            "PSP_ME_RENDER_CORRECTNESS": "1",
            "PSP_ME_RENDER_GE_CONSUME": "1",
            "PSP_ME_RENDER_PERFORMANCE": "1",
            "PSP_ME_RENDER_RAW_LIVE": "1",
            "PSP_ME_RENDER_DIRECT_LIST": "1",
            "PSP_ME_BULLET_COMPACT_UPDATE": "1",
            "PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY": "1",
            "PSP_ME_ITEM_RENDER_STREAM": "1",
            "PSP_ME_EFFECT_RENDER_STREAM": "1",
            "PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP": "1",
            "PSP_BULLET_COLLISION_BROADPHASE": "1",
            "PSP_GUI_TILE_BATCH": "1",
        }
        for variable, value in expected.items():
            self.assertRegex(self.recipe, rf"\b{variable}={value}\b")
        self.assertNotIn("PSP_1000=1", self.recipe)

    def test_build_identity_is_unique_and_unambiguous(self):
        self.assertEqual(self.makefile.count("0x26083118u"), 1)
        self.assertIn("PSP_EBOOT_TITLE='TH07 PSP ME I-ME8 ALL-IN'", self.recipe)
        for marker in (
            "MERW I-ME8 ALL-IN",
            "TRUSTED-SEED=1",
            "EFFECT0/3-ME=1",
            "LEAN-CACHE=1",
            "GUI-TILE=1",
        ):
            self.assertIn(marker, self.main)


if __name__ == "__main__":
    unittest.main()

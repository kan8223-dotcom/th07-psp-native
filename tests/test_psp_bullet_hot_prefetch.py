from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "PSP_BULLET_HOT_PREFETCH"
MACRO = "TH07_PSP_BULLET_HOT_PREFETCH"


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


def recipe_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    tail = makefile[start + len(target) + 1 :]
    match = re.search(r"^[A-Za-z0-9_.-]+:", tail, re.MULTILINE)
    if match is None:
        return makefile[start:]
    return makefile[start : start + len(target) + 1 + match.start()]


class PspBulletHotPrefetchSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.source = (ROOT / "src" / "BulletManager.cpp").read_text(encoding="utf-8")
        cls.fileio = (ROOT / "psp" / "fileio.cpp").read_text(encoding="utf-8")
        cls.one_pass = function_body(
            cls.source, "PspDrawNormalAutoRotatedOnePass(Bullet *bullet"
        )
        cls.update = function_body(cls.source, "u32 BulletManager::OnUpdate")
        cls.draw = function_body(cls.source, "u32 BulletManager::OnDraw")

    def test_feature_is_default_off_psp2000plus_only_and_profile_stamped(self) -> None:
        self.assertIn(f"{FEATURE} ?= 0", self.makefile)
        start = self.makefile.index(f"ifeq ($({FEATURE}),1)")
        end = self.makefile.index("ifeq ($(PSP_BULLET_QUIESCENT_ANM),1)", start)
        block = self.makefile[start:end]
        self.assertIn(f"-D{MACRO}", block)
        self.assertIn("ifneq ($(PSP_1000),0)", block)
        self.assertIn("PSP-2000+", block)
        self.assertIn("else ifneq ($(PSP_BULLET_HOT_PREFETCH),0)", block)
        self.assertIn("must be 0 or 1", block)
        stamp = next(
            line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({FEATURE})", stamp)

    def test_feature_requires_the_entire_accepted_stack(self) -> None:
        start = self.makefile.index(f"ifeq ($({FEATURE}),1)")
        end = self.makefile.index("ifeq ($(PSP_BULLET_QUIESCENT_ANM),1)", start)
        block = self.makefile[start:end]
        for prerequisite in (
            "PSP_BULLET_ROTATED_DIRECT),1",
            "PSP_BULLET_UNIFIED_QUADS),1",
            "PSP_BULLET_ONEPASS_ROTATED),1",
        ):
            with self.subTest(prerequisite=prerequisite):
                self.assertIn(prerequisite, block)

    def test_named_and_release_build_roots_explicitly_keep_feature_off(self) -> None:
        for target in (
            "psp1000-build",
            "psp2000plus-build",
            "psp2000plus-shikigami-build",
            "psp3000-mecc-bgm384k-build",
            "psp3000-mecc-audio4m-build",
        ):
            with self.subTest(target=target):
                self.assertIn(f"{FEATURE}=0", recipe_body(self.makefile, target))
        self.assertIn("release-build: psp2000plus-build", self.makefile)

    def test_attribution_and_empty_timer_profiles_reject_prefetch(self) -> None:
        self.assertRegex(
            self.makefile,
            rf"(?s)PSP_PERF_PROFILE\),ATTRIB\).*?"
            rf"ifneq \(\$\({FEATURE}\),0\).*?PERF_ACCEPT",
        )
        self.assertIn(
            f"Empty-timer A/A calibration requires {FEATURE}=0", self.makefile
        )

    def test_exactly_one_compile_time_guarded_dcache_fill_exists(self) -> None:
        self.assertEqual(self.source.count("__builtin_allegrex_cache("), 1)
        cache = self.one_pass.index("__builtin_allegrex_cache(")
        guard = self.one_pass.rfind("#if", 0, cache)
        end = self.one_pass.index("#endif", cache)
        self.assertIn(MACRO, self.one_pass[guard:cache])
        self.assertLess(cache, end)
        call = self.one_pass[cache : self.one_pass.index(");", cache) + 2]
        self.assertIn("0x1e", call)
        self.assertIn(
            "(int)(uintptr_t)&nextBullet->sprites.spriteBullet.autoRotate", call
        )

    def test_only_non_null_linked_list_successor_is_prefetched(self) -> None:
        next_load = self.one_pass.index("Bullet *const nextBullet = bullet->next;")
        non_null = self.one_pass.index("nextBullet != NULL", next_load)
        cache = self.one_pass.index("__builtin_allegrex_cache(", non_null)
        self.assertLess(next_load, non_null)
        self.assertLess(non_null, cache)
        self.assertNotIn("BulletAt(", self.one_pass)
        self.assertNotIn("nextBullet->next", self.one_pass)

    def test_prefetch_follows_current_loads_and_precedes_corner_math_and_stores(self) -> None:
        out = self.one_pass.index("Th07PspSpriteVertex *out = manager->vertexBufferCurPtr;")
        cache = self.one_pass.index("__builtin_allegrex_cache(")
        corner_math = self.one_pass.index("const float localX0 = -halfWidth;")
        first_store = self.one_pass.index("PspBulletOnePassWriteVertex(out[0]")
        self.assertLess(out, cache)
        self.assertLess(cache, corner_math)
        self.assertLess(corner_math, first_store)

        between = self.one_pass[cache:first_store]
        for forbidden in (
            "PspBulletOnePassWriteVertex",
            "vertexBufferCurPtr +=",
            "bullet->Draw",
            "Flush(",
            "sceGu",
            "malloc(",
            "fwrite(",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, between)

    def test_update_and_outer_draw_walks_have_no_prefetch_instruction(self) -> None:
        self.assertNotIn("__builtin_allegrex_cache", self.update)
        self.assertNotIn("__builtin_allegrex_cache", self.draw)

    def test_perf_log_identity_cannot_be_confused_with_plain_accept(self) -> None:
        prefetch = self.fileio.index(
            "defined(TH07_PSP_PERF_ACCEPT) && "
            "defined(TH07_PSP_BULLET_HOT_PREFETCH)"
        )
        generic = self.fileio.index(
            "#elif defined(TH07_PSP_PERF_ACCEPT)", prefetch + 1
        )
        self.assertLess(prefetch, generic)
        self.assertIn('return "PREFETCH";', self.fileio[prefetch:generic])


if __name__ == "__main__":
    unittest.main()

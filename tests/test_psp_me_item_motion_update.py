from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def target(source: str, name: str) -> str:
    start = source.index(f"{name}:")
    return source[start:].split("\n\n", 1)[0]


def assignments(value: str) -> dict[str, str]:
    pairs = re.findall(r"\b(PSP_[A-Z0-9_]+)=('[^']*'|[^\s\\]+)", value)
    if len(pairs) != len(dict(pairs)):
        raise AssertionError("duplicate PSP assignment")
    return dict(pairs)


class PspMeItemMotionUpdateContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.header = (ROOT / "psp/audio_me.h").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src/BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.bullets_h = (ROOT / "src/BulletManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.items = (ROOT / "src/ItemManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.graphics = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text(
            encoding="utf-8"
        )

    def test_rid30_is_rid29_plus_one_reversible_feature(self) -> None:
        rid29 = assignments(target(
            self.makefile, "psp3000-ime7-adaptive-item-uncached-build"
        ))
        rid30 = assignments(target(
            self.makefile, "psp3000-a1-item-motion-build"
        ))
        identity = {"PSP_AUDIO4M_BUILD_ID", "PSP_EBOOT_TITLE"}
        expected = {
            key: value for key, value in rid29.items() if key not in identity
        }
        expected["PSP_ME_ITEM_MOTION_UPDATE"] = "1"
        self.assertEqual(
            {key: value for key, value in rid30.items() if key not in identity},
            expected,
        )
        self.assertEqual(rid30["PSP_1000"], "0")
        self.assertEqual(rid30["PSP_AUDIO4M_BUILD_ID"], "0x26083130u")
        self.assertEqual(
            rid30["PSP_EBOOT_TITLE"], "'TH07 PSP A1 ITEM MOTION'"
        )

    def test_sidecar_abi_and_live_item_offsets_are_compile_time_locked(self) -> None:
        for token in (
            "sizeof(Th07PspMeItemMotionSeedHeader) == 64u",
            "sizeof(Th07PspMeItemMotionSeedSlot) == 64u",
            "sizeof(Th07PspMeItemMotionSeed) == 70656u",
            "sizeof(Th07PspMeItemMotionOutputHeader) == 64u",
            "sizeof(Th07PspMeItemMotionSlotResult) == 32u",
            "sizeof(Th07PspMeItemMotionOutput) == 35456u",
            "__builtin_offsetof(Item, currentPosition.x) == 588u",
            "__builtin_offsetof(Item, timer.current) == 632u",
            "__builtin_offsetof(Item, autoCollect) == 640u",
        ):
            self.assertIn(token, self.bullets)

    def test_command10_capture_is_optional_and_generation_bracketed(self) -> None:
        reconstruct = body(
            self.audio, "me_render_stream_reconstruct_item_record("
        )
        self.assertIn("me_item_motion_capture_seed", reconstruct)
        self.assertIn("me_item_motion_clear_seed_slot", reconstruct)
        self.assertLess(
            reconstruct.index("me_item_motion_capture_seed"),
            reconstruct.index("me_item_motion_clear_seed_slot"),
        )
        build = body(self.bullets, "bool PspMeRenderBuildFusedSnapshot(")
        gate = build.index("th07_psp_me_item_motion_available()")
        flag = build.index("TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED")
        self.assertLess(gate, flag)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST", build)

    def test_command12_item_only_work_cannot_claim_bullet_collision(self) -> None:
        launch = body(self.bullets, "u32 PspMeBulletCompactEarlyLaunch(")
        self.assertIn("TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID", launch)
        self.assertIn("TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID", launch)
        adopt = body(self.bullets, "u32 BulletManager::OnUpdate(")
        self.assertIn("TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID", adopt)

    def test_unbanked_output_is_retired_before_a_new_me_writer(self) -> None:
        launch = body(self.bullets, "u32 PspMeBulletCompactEarlyLaunch(")
        clear = launch.index("state.currentItemMotionValid = false;")
        begin = launch.index("th07_psp_me_bullet_compact_begin(&job)")
        self.assertLess(clear, begin)
        self.assertIn("state.itemMotionSeed = nullptr;", launch[clear:begin])
        self.assertIn("state.itemMotionOutput = nullptr;", launch[clear:begin])

    def test_jit_adoption_keeps_lifetime_collision_and_rewards_on_sc(self) -> None:
        adopt = body(self.items, "PspMeItemMotionAdoptRoute PspTryAdoptMeItemMotion(")
        for exact in (
            "pspMeItemSlotGenerations[slot] == input.generation",
            "PspMeItemMotionGlobalsMatch(view.job)",
            "item->timer.current == input.timerCurrent",
            "TH07_PSP_ME_ITEM_MOTION_RESULT_CANDIDATE",
        ):
            self.assertIn(exact, adopt)
        update = body(self.items, "void ItemManager::OnUpdate()")
        self.assertLess(
            update.index("PspTryAdoptMeItemMotion"),
            update.index("g_Player.CalcItemBoxCollision"),
        )
        for authority in (
            "g_GameManager.AddScore",
            "g_SoundPlayer.PlaySoundByIdx",
            "item->isInUse = 0",
        ):
            self.assertIn(authority, update)

    def test_accept_log_has_complete_a1_adoption_accounting(self) -> None:
        for token in (
            "compactItemMotionCandidates",
            "compactItemMotionProcessed",
            "compactItemMotionJitCandidates",
            "compactItemMotionAdopted",
            "compactItemMotionSlotReject",
            "compactItemMotionGlobalReject",
            "compactItemMotionPendingAtItem",
        ):
            self.assertIn(token, self.bullets_h)
        self.assertIn(
            '"A1L%u A1RD%u A1C%llu A1P%llu A1J%llu A1A%llu "',
            self.graphics,
        )
        self.assertIn('"A1SR%u A1GR%u A1PN%u "', self.graphics)


if __name__ == "__main__":
    unittest.main()

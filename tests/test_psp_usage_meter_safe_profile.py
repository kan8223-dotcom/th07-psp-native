import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def target_body(makefile: str, name: str) -> str:
    match = re.search(
        rf"^{re.escape(name)}:\n(?P<body>(?:\t.*\n|#.*\n|\n)+)",
        makefile,
        re.MULTILINE,
    )
    if not match:
        raise AssertionError(f"missing target {name}")
    return match.group("body")


def function_body(source: str, name: str) -> str:
    start = source.index(name)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : pos + 1]
    raise AssertionError(f"unterminated function {name}")


class UsageMeterSafeProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.graphics = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text(
            encoding="utf-8"
        )
        cls.header = (ROOT / "psp/usage_meter.h").read_text(encoding="utf-8")
        cls.audio_me = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src/BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.target = target_body(
            cls.makefile, "psp3000-ime7-usage-meter-no-item-build"
        )
        cls.mefix_target = target_body(
            cls.makefile, "psp3000-ime7-usage-meter-mefix-build"
        )

    def test_profile_has_unique_identity_and_meter(self) -> None:
        self.assertIn("PSP_USAGE_METER=1", self.target)
        self.assertIn("PSP_AUDIO4M_BUILD_ID=0x26083121u", self.target)
        self.assertIn("TH07 PSP I-ME7 METER NO-ITEM", self.target)

    def test_mefix_profile_preserves_safe_path_with_new_identity(self) -> None:
        self.assertIn("PSP_USAGE_METER=1", self.mefix_target)
        self.assertIn("PSP_AUDIO4M_BUILD_ID=0x26083122u", self.mefix_target)
        self.assertIn("TH07 PSP I-ME7 METER MEFIX", self.mefix_target)
        for setting in (
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_GE_CONSUME=1",
            "PSP_ME_BULLET_COMPACT_UPDATE=1",
            "PSP_BULLET_COLLISION_BROADPHASE=1",
            "PSP_ME_ITEM_RENDER_STREAM=0",
            "PSP_ME_EFFECT_RENDER_STREAM=0",
            "PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0",
            "PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0",
            "PSP_GUI_TILE_BATCH=0",
        ):
            self.assertIn(setting, self.mefix_target)

    def test_rejected_startup_paths_are_compiled_out(self) -> None:
        for setting in (
            "PSP_ME_ITEM_RENDER_STREAM=0",
            "PSP_ME_EFFECT_RENDER_STREAM=0",
            "PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0",
            "PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0",
            "PSP_ME_STARTUP_BREADCRUMBS=0",
            "PSP_GUI_TILE_BATCH=0",
        ):
            self.assertIn(setting, self.target)

    def test_accepted_bullet_me_path_remains_enabled(self) -> None:
        for setting in (
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_DIRECT_LIST=1",
            "PSP_ME_BULLET_COMPACT_UPDATE=1",
            "PSP_BULLET_COLLISION_BROADPHASE=1",
        ):
            self.assertIn(setting, self.target)

    def test_overlay_reserves_list_space_and_restores_live_backend_state(self) -> None:
        self.assertIn(
            "#define TH07_PSP_USAGE_METER_VERTEX_BYTES 1776u", self.header
        )
        self.assertIn(
            "EnsureListSpace(TH07_PSP_USAGE_METER_VERTEX_BYTES)", self.graphics
        )
        for state in (
            "if (mBlendEnabled)",
            "if (mDepthTestEnabled)",
            "if (mFogEnabled)",
            "sceGuDepthMask(mDepthWrite ? GU_FALSE : GU_TRUE)",
            "if (mTextureEnableKnown && mTextureEnabled)",
        ):
            self.assertIn(state, self.graphics)

    def test_me_count_is_started_before_worker_publication(self) -> None:
        start_count = function_body(self.audio_me, "me_render_start_count")
        worker = function_body(self.audio_me, "\nvoid meLibOnProcess(void)\n")
        self.assertIn('mtc0 $0, $9', start_count)
        self.assertLess(
            worker.index("me_render_start_count();"),
            worker.index("box->workerState = ME_WORKER_READY"),
        )

    def test_compact_job_feeds_all_me_busy_phases_before_reject(self) -> None:
        poll = function_body(
            self.bullets, "int PspMeBulletCompactPollForUpdate"
        )
        hook = "th07_usage_meter_add_me_cycles("
        self.assertEqual(poll.count(hook), 1)
        self.assertLess(poll.index(hook), poll.index("if (pollResult == -1)"))
        call = poll[poll.index(hook) : poll.index(");", poll.index(hook))]
        for phase in (
            "completion.meInvalidateCycles",
            "completion.meKernelCycles",
            "completion.meWritebackCycles",
        ):
            self.assertIn(phase, call)

    def test_render_job_meter_counts_cache_work_as_busy_time(self) -> None:
        for function in (
            "int PspMeRenderRetirePending",
            "int PspMeRenderCorrectnessRetire",
        ):
            body = function_body(self.bullets, function)
            hook = body.index("th07_usage_meter_add_me_cycles(")
            call = body[hook : body.index(");", hook)]
            for phase in (
                "completion.meInvalidateCycles",
                "completion.meKernelCycles",
                "completion.meWritebackCycles",
            ):
                self.assertIn(phase, call)

    def test_fast_update_future_path_uses_the_same_busy_definition(self) -> None:
        body = function_body(
            self.bullets, "PspMeBulletFastRunSynchronous"
        )
        hook = body.index("th07_usage_meter_add_me_cycles(")
        call = body[hook : body.index(");", hook)]
        for phase in (
            "completion.meInvalidateCycles",
            "completion.meKernelCycles",
            "completion.meWritebackCycles",
        ):
            self.assertIn(phase, call)


if __name__ == "__main__":
    unittest.main()

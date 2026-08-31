from __future__ import annotations

import hashlib
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class GePortraitCachePolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.renderer = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text(
            encoding="utf-8"
        )
        cls.anm = (ROOT / "src/AnmManager.cpp").read_text(encoding="utf-8")
        cls.bridge = (ROOT / "psp/ge4_game_bridge.cpp").read_text(encoding="utf-8")
        cls.bridge_h = (ROOT / "psp/ge4_game_bridge.hpp").read_text(
            encoding="utf-8"
        )
        cls.portrait_h = (ROOT / "psp/ge_portrait_telemetry.h").read_text(
            encoding="utf-8"
        )
        cls.audio = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

    def test_upper_pool_staging_and_budget_are_exact(self) -> None:
        for invariant in (
            "kPortraitStagingOffset = 0x00180000u",
            "kPortraitStagingBytes = 512u * 1024u",
            "kUpperPortraitPoolOffset = 2u * 1024u * 1024u",
            "kUpperPortraitPoolBytes = 2u * 1024u * 1024u",
            "kUpperPortraitRawBase = 0x04200000u",
            "kPortraitBudgetBytes == 1536u * 1024u",
            "kPortraitStagingOffset + kPortraitStagingBytes == kLowerEdramBytes",
            "kEdramBytes + kSurfaceCacheMaxBytes <= kPortraitStagingOffset",
        ):
            self.assertIn(invariant, self.renderer)

    def test_six_roles_map_to_exact_anm_texture_slots(self) -> None:
        expected = (
            ("TH07_PSP_PORTRAIT_SELF", "ANM_FILE_FACE"),
            ("TH07_PSP_PORTRAIT_BOMB", "ANM_FILE_FACE + 1u"),
            ("TH07_PSP_PORTRAIT_STAGE_0", "ANM_FILE_FACE_STAGE"),
            ("TH07_PSP_PORTRAIT_STAGE_1", "ANM_FILE_FACE_STAGE + 1u"),
            ("TH07_PSP_PORTRAIT_STAGE_2", "ANM_FILE_FACE_STAGE + 2u"),
            ("TH07_PSP_PORTRAIT_STAGE_3", "ANM_FILE_FACE_STAGE + 3u"),
        )
        helper = self.anm[
            self.anm.index("void PreparePspPortraitTexture"):
            self.anm.index("SDL_Surface *LoadPspMusicRawSurface")
        ]
        for role, texture_slot in expected:
            pattern = (
                rf"case {re.escape(texture_slot)}:.*?"
                rf"Th07PspPrepareUpperPortraitTexture\({role}, textureIdx\);"
            )
            self.assertRegex(helper.replace("\n", " "), pattern)
        self.assertIn("IsStagePortraitRole(portraitRole)", self.renderer)
        self.assertIn("minifyStagePortrait ? 256u : 512u", self.renderer)

    def test_exact_frozen_prx_is_hash_gated_and_never_rebuilt(self) -> None:
        expected_hash = (
            "411e71b3ffb31bd91024cc0221481a787e693276c0899e05da08c3cd91dc1ab8"
        )
        self.assertIn("GE4_PROVEN_PRX_SIZE := 2150", self.makefile)
        self.assertIn(f"GE4_PROVEN_PRX_SHA256 := {expected_hash}", self.makefile)
        self.assertIn("GE4_PROVEN_PRX := ge4wrap_texv1.prx", self.makefile)
        self.assertIn('sha256sum "$(GE4_PROVEN_PRX_SOURCE)"', self.makefile)
        self.assertNotIn("GE4_GAME_PRX", self.makefile)
        self.assertNotIn("ge4_game_import.o", self.makefile)
        self.assertNotIn("ge4-game-clean", self.makefile)

        source_line = next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("GE4_PROVEN_PRX_SOURCE := ")
        )
        source = (ROOT / source_line.split(":=", 1)[1].strip()).resolve()
        blob = source.read_bytes()
        self.assertEqual(len(blob), 2150)
        self.assertEqual(hashlib.sha256(blob).hexdigest(), expected_hash)

    def test_bridge_has_no_wrapper_static_import_or_direct_kernel_call(self) -> None:
        for value in (
            './ge4wrap_texv1.prx',
            'th07_ge4_texbw_v1_wrap',
            'ge4wrap_texv1',
            '0x2ddac688u',
            '0xbb75238fu',
            '0x703b997bu',
            'sctrlHENFindFunction',
            'kuKernelCall(',
        ):
            self.assertIn(value, self.bridge)
        self.assertNotIn('extern "C" int ge4', self.bridge)
        self.assertNotIn("ge4Probe", self.bridge)
        self.assertEqual(self.bridge.count("kuKernelCall("), 1)
        self.assertIn("-lpspsystemctrl_user", self.makefile)
        self.assertIn("-lpspkubridge", self.makefile)

    def test_prepare_and_enable_are_split_around_initial_gu_idle(self) -> None:
        self.assertIn("th07_psp_ge4_prepare", self.bridge_h)
        self.assertIn("th07_psp_ge4_enable_after_gu_idle", self.bridge_h)
        # InitInterface constructs the GU backend whose first-GU-idle hook is
        # the only enable call site.  R6 hardware proved that preparing after
        # InitInterface silently skips enable, so prepare must run first.
        window = self.main.index('th07_psp_boot_note("window initialized")')
        prepare = self.main.index("th07_psp_ge4_prepare();")
        interface = self.main.index("GameWindow::InitInterface()")
        self.assertLess(window, prepare)
        self.assertLess(prepare, interface)

        initial_finish = self.renderer.index(
            "const int initialFinishResult = sceGuFinish();"
        )
        initial_sync = self.renderer.index("const int initialSyncResult = sceGuSync(")
        enable = self.renderer.index("th07_psp_ge4_enable_after_gu_idle()")
        pool = self.renderer.index("InitializeUpperPortraitPool();", enable)
        self.assertLess(initial_finish, initial_sync)
        self.assertLess(initial_sync, enable)
        self.assertLess(enable, pool)

    def test_bridge_uses_exact_canary_gates_and_bounded_idle_poll(self) -> None:
        for gate in (
            "kRequiredModel = 3",
            "kExpectedEdramBase = 0x04000000u",
            "hwSize != kFourMiB",
            "sizeBefore != kTwoMiB",
            "kGeTimeoutUs = 5000000ull",
            "state == PSP_GE_LIST_DONE",
            "pre-enable-sync",
            "post-Set4 sync",
            "sizeAfter != kFourMiB",
        ):
            self.assertIn(gate, self.bridge)
        enable = self.bridge[
            self.bridge.index('extern "C" int th07_psp_ge4_enable_after_gu_idle()'):
            self.bridge.index('extern "C" int th07_psp_ge4_active()')
        ]
        self.assertGreaterEqual(enable.count("WaitForGeIdle()"), 2)
        self.assertNotIn("sceGeDrawSync(0)", self.bridge)
        self.assertIn("CallKernelExport(gSetSizeAddress, kFourMiB)", enable)

    def test_power_lock_is_exact_zero_and_shared_with_mecc(self) -> None:
        lock_start = self.bridge.index("const int lockResult = scePowerLock(0);")
        lock_end = self.bridge.index("__atomic_store_n(&gPowerLocked", lock_start)
        lock = self.bridge[lock_start:lock_end]
        self.assertIn("if (lockResult < 0)", lock)
        self.assertIn("if (lockResult > 0)", lock)
        self.assertIn('ColdOffLoop("power-lock-uncertain"', lock)
        self.assertIn("th07_psp_ge4_power_lock_held()", self.audio)
        self.assertIn("POWER LOCK BORROWED FROM GE4", self.audio)
        self.assertIn("POWER LOCK RETURNED TO GE4", self.audio)

    def test_upload_uses_lower_staging_ge_copy_poison_and_hash_readback(self) -> None:
        upload = self.renderer[
            self.renderer.index("void CompleteUpperPortraitUpload"):
            self.renderer.index("void ValidateUpperPortraitAllocation")
        ]
        self.assertIn("UpperPortraitStagingCpuAddress()", upload)
        self.assertIn("UpperPortraitWordHash(stagingWords, bytes)", upload)
        self.assertIn("CopyUpperPortraitImageAndWait(stagingRaw, upperRaw", upload)
        self.assertIn("PoisonUpperPortraitStaging", upload)
        self.assertIn("CopyUpperPortraitImageAndWait(upperRaw, stagingRaw", upload)
        self.assertIn("sourceHash != readbackHash", upload)
        self.assertIn("upperPortraitVerified = true", upload)
        copy = self.renderer[
            self.renderer.index("void CopyUpperPortraitImageAndWait"):
            self.renderer.index("void InitializeUpperPortraitPool")
        ]
        self.assertIn("sceGuCopyImage(GU_PSM_4444", copy)
        self.assertIn("sceGuTexSync();", copy)
        self.assertIn("sceGeDrawSync(1) != PSP_GE_LIST_DONE", self.renderer)

    def test_no_cpu_copy_reads_or_writes_upper(self) -> None:
        upload = self.renderer[
            self.renderer.index("void CompleteUpperPortraitUpload"):
            self.renderer.index("void ValidateUpperPortraitAllocation")
        ]
        self.assertNotIn("std::memcpy(upperDestination", upload)
        self.assertNotIn("std::memset(upperDestination", upload)
        migration = self.renderer[
            self.renderer.index("bool MoveUpperPortraitToMain"):
            self.renderer.index("void NoteBoundUpperPortraitDraw")
        ]
        self.assertIn("CopyUpperPortraitImageAndWait(", migration)
        self.assertIn("UpperPortraitStagingRawAddress()", migration)
        self.assertIn(
            "std::memcpy(pixels, UpperPortraitStagingCpuAddress()", migration
        )
        self.assertNotIn("std::memcpy(pixels, texture.pixels", migration)

    def test_telemetry_separates_six_slot_capacity_from_dynamic_required_set(self) -> None:
        for token in (
            "TH07_PSP_PORTRAIT_SLOT_COUNT = 6",
            "TH07_PSP_PORTRAIT_CAPACITY_MASK",
            "TH07_PSP_PORTRAIT_PLAYER_MASK = 0x03u",
            "TH07_PSP_PORTRAIT_STAGE_MASK = 0x3cu",
            "this is the exact player + stage role mask",
        ):
            self.assertIn(token, self.portrait_h)
        self.assertNotIn("TH07_PSP_PORTRAIT_REQUIRED_MASK", self.portrait_h)

        for token in (
            "mPortraitTelemetry.owned_mask",
            "mPortraitTelemetry.verified_mask",
            "mPortraitTelemetry.sampled_mask",
            "telemetry.source_hash",
            "telemetry.readback_hash",
            "telemetry.upload_generation",
            "mUpperPortraitFallbacks",
            "mUpperPortraitMigrations",
            "mUpperPortraitAllocationFailures",
            "mUpperPortraitInvariantFailures",
            "PublishPortraitSnapshotSeqlock",
            "th07_psp_portrait_cache_snapshot",
        ):
            self.assertIn(token, self.renderer)
        self.assertIn("PortraitRoleMatchesSlot", self.renderer)
        self.assertIn("portrait-pool-not-empty-at-gu-term", self.renderer)

    def test_prewarm_commit_uses_exact_face_stage_child_prefix(self) -> None:
        initialize = self.renderer[
            self.renderer.index("void InitializeUpperPortraitTelemetry()") :
            self.renderer.index("void PublishUpperPortraitTelemetry()")
        ]
        self.assertIn("mPortraitTelemetry.required_mask = 0;", initialize)

        commit = self.renderer[
            self.renderer.index("void CompleteUpperPortraitPrewarm(") :
            self.renderer.index("bool GetTextureContentSize(")
        ]
        self.assertIn(
            "stagePortraitCount == 0 || stagePortraitCount > kMaxStagePortraitAtlases",
            commit,
        )
        self.assertIn("((1u << stagePortraitCount) - 1u)", commit)
        self.assertIn("mPortraitTelemetry.required_mask = expectedMask;", commit)
        self.assertIn("mPortraitTelemetry.owned_mask == expectedMask", commit)
        self.assertIn("mPortraitTelemetry.verified_mask == expectedMask", commit)
        self.assertIn("GE portrait PREWARM COMPLETE", commit)
        self.assertIn("GE portrait PREWARM FAIL", commit)

        release = self.renderer[
            self.renderer.index("void ReleaseUpperPortraitAllocation(") :
            self.renderer.index("bool MoveUpperPortraitToMain(")
        ]
        invalidate = release.index("mPortraitTelemetry.required_mask = 0;")
        clear_owned = release.index("mPortraitTelemetry.owned_mask &= ~slotMask;")
        self.assertLess(invalidate, clear_owned)

    def test_face_stage_success_commits_both_loadanms_paths_only(self) -> None:
        helper = self.anm[
            self.anm.index("void CompletePspPortraitPrewarm(") :
            self.anm.index("SDL_Surface *LoadPspMusicRawSurface")
        ]
        self.assertIn("anmIdx == ANM_FILE_FACE_STAGE", helper)
        self.assertIn(
            "Th07PspCompleteUpperPortraitPrewarm(childCount);", helper
        )

        load = self.anm[
            self.anm.index("i32 AnmManager::LoadAnms(") :
            self.anm.index("i32 AnmManager::LoadAnm(")
        ]
        self.assertEqual(load.count("CompletePspPortraitPrewarm("), 2)
        self.assertIn("CompletePspPortraitPrewarm(anmIdx, entryCount);", load)
        self.assertIn("CompletePspPortraitPrewarm(startIdx, childCount);", load)
        for failure_path in load.split("return res;")[:-1]:
            tail = failure_path.rsplit("if (res < 0)", 1)[-1]
            self.assertNotIn("CompletePspPortraitPrewarm(", tail)

    def test_shutdown_order_is_audio_renderer_set2_wrapper_unlock(self) -> None:
        release = self.main.index("g_SoundPlayer.Release();")
        renderer = self.main.index("SAFE_DELETE(g_Supervisor.gfxDevice);")
        shutdown = self.main.index("th07_psp_ge4_shutdown();")
        self.assertLess(release, renderer)
        self.assertLess(renderer, shutdown)

        shutdown_body = self.bridge[
            self.bridge.index('extern "C" void th07_psp_ge4_shutdown()'):
        ]
        restore = shutdown_body.index('RestoreTwoMiBOrCold("restore2")')
        unload = shutdown_body.index("StopUnloadWrapper")
        unlock = shutdown_body.index("UnlockPower();")
        self.assertLess(restore, unload)
        self.assertLess(unload, unlock)

    def test_vsync_wait_overlaps_the_final_ge_tail(self) -> None:
        swap = self.renderer[
            self.renderer.index("void SwapBuffers() override"):
            self.renderer.index("  private:", self.renderer.index("void SwapBuffers() override"))
        ]
        finish = swap.index("const int listBytes = sceGuFinish();")
        vblank = swap.index("sceDisplayWaitVblankStart();")
        sync = swap.index("const int syncResult = sceGuSync(")
        present = swap.index("sceGuSwapBuffers();")
        self.assertLess(finish, vblank)
        self.assertLess(vblank, sync)
        self.assertLess(sync, present)

    def test_main_ram_audio_identity_and_profile_invalidation_are_explicit(self) -> None:
        self.assertIn("PSP_AUDIO4M_BUILD_ID ?= 0x2608280bu", self.makefile)
        self.assertIn(
            "TH07 SHIKIGAMI MAIN RAM AUDIO GE", self.makefile
        )
        self.assertIn("$(PSP_AUDIO4M_BUILD_ID)-$(SHIKIGAMI_HOST_STAMP)", self.makefile)
        release_target = self.makefile[
            self.makefile.index("psp3000-mecc-audio4m-build:"):
            self.makefile.index("release-audit:")
        ]
        self.assertIn("PSP_PERF_DIAG=0", release_target)
        profile = self.makefile[
            self.makefile.index("ifeq ($(PSP_MECC_AUDIO_4M),1)", 1000):
            self.makefile.index("ifeq ($(PSP_DIRECT_GAME),1)")
        ]
        self.assertIn("-DTH07_PSP_GE_PORTRAIT_CACHE", profile)
        source_gate = re.search(
            r"ifeq \(\$\(PSP_MECC_AUDIO_4M\),1\)\n"
            r"SRCS \+= ([^\n]+)\nendif",
            self.makefile,
        )
        self.assertIsNotNone(source_gate)
        assert source_gate
        self.assertIn("psp/ge4_game_bridge.cpp", source_gate.group(1))


if __name__ == "__main__":
    unittest.main()

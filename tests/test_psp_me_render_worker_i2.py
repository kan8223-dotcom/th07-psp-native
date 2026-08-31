from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "TH07_PSP_ME_RENDER_GE_CONSUME"
MAKE_FEATURE = "PSP_ME_RENDER_GE_CONSUME"
PERFORMANCE_FEATURE = "TH07_PSP_ME_RENDER_PERFORMANCE"
MAKE_PERFORMANCE_FEATURE = "PSP_ME_RENDER_PERFORMANCE"


def function_body(source: str, signature: str) -> str:
    """Return a C/C++ function body without depending on its return type."""
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


def make_target(makefile: str, target: str) -> str:
    """Extract one explicit Make target and its recipe."""
    start = makefile.index(f"{target}:")
    match = re.search(
        r"\n(?=[A-Za-z0-9_.-]+(?:\s+[^\n:]*)?:)", makefile[start + 1 :]
    )
    if match is None:
        return makefile[start:]
    return makefile[start : start + match.start() + 1]


def assert_order(test: unittest.TestCase, source: str, *needles: str) -> None:
    cursor = -1
    for needle in needles:
        found = source.find(needle, cursor + 1)
        test.assertNotEqual(found, -1, f"missing ordered token: {needle}")
        test.assertGreater(found, cursor, f"out-of-order token: {needle}")
        cursor = found


class PspMeRenderWorkerI2Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        cls.audio_h = (ROOT / "psp" / "audio_me.h").read_text(
            encoding="utf-8"
        )
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.bullets_h = (ROOT / "src" / "BulletManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.graphics = (
            ROOT / "psp" / "graphics" / "PspGuGraphics.cpp"
        ).read_text(encoding="utf-8")
        cls.graphics_h = (
            ROOT / "psp" / "graphics" / "PspGuGraphics.hpp"
        ).read_text(encoding="utf-8")
        cls.main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
        cls.window = (ROOT / "src" / "GameWindow.cpp").read_text(
            encoding="utf-8"
        )
        cls.window_h = (ROOT / "src" / "GameWindow.hpp").read_text(
            encoding="utf-8"
        )
        cls.replay = (ROOT / "src" / "ReplayManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.replay_h = (ROOT / "src" / "ReplayManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.engine_sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted((ROOT / "src").glob("*.cpp"))
        )

    def test_dedicated_profile_enables_only_reviewed_i2_stack(self) -> None:
        self.assertIn(f"{MAKE_FEATURE} ?= 0", self.makefile)
        self.assertIn(f"CXXFLAGS += -D{FEATURE}", self.makefile)
        self.assertIn(f"CFLAGS += -D{FEATURE}", self.makefile)
        self.assertIn(
            "$(error PSP_ME_RENDER_GE_CONSUME requires "
            "PSP_ME_RENDER_CORRECTNESS=1)",
            self.makefile,
        )
        self.assertIn(
            "$(error PSP_ME_RENDER_GE_CONSUME requires "
            "PSP_ME_RENDER_WORKER=1)",
            self.makefile,
        )
        self.assertIn(
            "ME render GE consumption requires the correctness stream owner",
            self.audio_h,
        )
        self.assertIn(
            "ME render GE consumption requires the ME render worker",
            self.audio_h,
        )

        i2 = make_target(self.makefile, "psp3000-me-render-i2-ge-build")
        for setting in (
            "PSP_1000=0",
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_RETIRE_DIAG=1",
            "PSP_ME_RENDER_GE_CONSUME=1",
            "PSP_MECC_AUDIO_4M=1",
            "PSP_BULLET_ROTATED_DIRECT=1",
            "PSP_BULLET_UNIFIED_QUADS=1",
            "PSP_BULLET_ONEPASS_ROTATED=1",
            "PSP_AUDIO4M_BUILD_ID=0x26083010u",
        ):
            self.assertIn(setting, i2)
        for rejected in (
            "PSP_BULLET_AXIS_FAST=0",
            "PSP_BULLET_SNAPSHOT_EMITTER=0",
            "PSP_BULLET_HOT_PREFETCH=0",
            "PSP_BULLET_WARM_QUEUE=0",
            "PSP_BULLET_QUIESCENT_ANM=0",
        ):
            self.assertIn(rejected, i2)
        self.assertIn("TH07 PSP ME Render I-ME2 GE", self.makefile)

        # A profile-variable transition must invalidate every object, and the
        # already accepted M0/I1 artifacts must remain GE-consume-off builds.
        self.assertIn(
            "$(PSP_ME_RENDER_RETIRE_DIAG)-$(PSP_ME_RENDER_GE_CONSUME)-",
            self.makefile,
        )
        for target in (
            "psp3000-me-render-m0-build",
            "psp3000-me-render-i1-build",
            "psp3000-me-render-i1-retire-diag-build",
        ):
            self.assertIn("PSP_ME_RENDER_GE_CONSUME=0", make_target(self.makefile, target))

    def test_performance_profile_is_explicit_and_cannot_leak_to_old_targets(self) -> None:
        self.assertIn(f"{MAKE_PERFORMANCE_FEATURE} ?= 0", self.makefile)
        self.assertIn(
            f"CXXFLAGS += -D{PERFORMANCE_FEATURE}", self.makefile
        )
        self.assertIn(f"CFLAGS += -D{PERFORMANCE_FEATURE}", self.makefile)
        for dependency in (
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_GE_CONSUME=1",
            "PSP_ME_RENDER_RETIRE_DIAG=0",
        ):
            self.assertIn(
                f"$(error PSP_ME_RENDER_PERFORMANCE requires {dependency})",
                self.makefile,
            )
        self.assertIn("$(PSP_ME_RENDER_PERFORMANCE)-", self.makefile)

        performance = make_target(
            self.makefile, "psp3000-me-render-i3-performance-build"
        )
        self.assertNotIn("psp3000-me-render-i2-performance-build", self.makefile)
        for setting in (
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_RETIRE_DIAG=0",
            "PSP_ME_RENDER_GE_CONSUME=1",
            "PSP_ME_RENDER_PERFORMANCE=1",
            "PSP_PERF_DENSE_SLICE=1",
            "PSP_AUDIO4M_BUILD_ID=0x26083013u",
        ):
            self.assertIn(setting, performance)
        self.assertIn("TH07 PSP ME Render I-ME3 PERF", self.makefile)
        self.assertIn("TH07 PSP ME Render I-ME2 GE", self.makefile)
        self.assertIn("MERW I-ME3 FUSED PERFORMANCE", self.main)

        for target in (
            "psp3000-me-render-m0-build",
            "psp3000-me-render-i1-build",
            "psp3000-me-render-i1-retire-diag-build",
            "psp3000-me-render-i2-ge-build",
        ):
            self.assertIn(
                "PSP_ME_RENDER_PERFORMANCE=0",
                make_target(self.makefile, target),
            )

    def test_performance_profile_omits_stream_hashes_but_keeps_old_contract(self) -> None:
        snapshot = function_body(
            self.bullets, "PspMeRenderBuildCorrectnessSnapshot("
        )
        completion = function_body(
            self.bullets, "PspMeRenderCorrectnessCompletionMatches("
        )
        self.assertIn("#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)", snapshot)
        self.assertIn("job->flags = 0u;", snapshot)
        self.assertIn("job->payloadHash = 0u;", snapshot)
        self.assertIn(
            "job->flags = TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH",
            snapshot,
        )
        self.assertIn("th07_psp_me_render_stream_hash(", snapshot)
        self.assertIn(
            "#if !defined(TH07_PSP_ME_RENDER_PERFORMANCE)", completion
        )
        self.assertIn(
            "completion.payloadHash != state.job.payloadHash", completion
        )

        submit = function_body(
            self.audio, "th07_psp_me_render_stream_submit("
        )
        echo = function_body(
            self.audio, "me_render_stream_completion_echo_matches("
        )
        retire = function_body(
            self.audio, "th07_psp_me_render_stream_retire("
        )
        self.assertIn(
            "TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH", submit
        )
        self.assertIn("uint32_t payloadHash = 0u;", submit)
        for body in (echo, retire):
            self.assertIn(
                "TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH", body
            )

    def test_performance_authority_is_constant_time_and_live(self) -> None:
        authority = function_body(
            self.bullets, "PspMeRenderReadyAuthorityMatches(u32 expectedDrawSeq)"
        )
        for proof in (
            "state.job.targetDrawSeq != expectedDrawSeq",
            "state.job.stageEpoch != gPspMeRenderShadow.stageEpoch",
            "state.job.managerEpoch != gPspMeRenderShadow.managerEpoch",
            "g_ReplayManager ? g_ReplayManager->frameId : 0",
            "state.job.globalSignature != PspMeRenderGlobalSignature()",
            "g_BulletManager.bulletCount != state.managerBulletCount",
            "static_cast<u32>(state.managerBulletCount) < state.recordCount",
            "g_BulletManager.updateCount != state.managerUpdateCount",
            "g_BulletManager.time.previous != state.managerTimePrevious",
            "g_BulletManager.time.subFrame",
            "g_BulletManager.time.current != state.managerTimeCurrent",
            "g_BulletManager.pspMeRenderMutationEpoch",
            "state.managerMutationEpoch",
            "PspMeRenderRepresentativeSourceCacheMatches()",
            "constexpr u32 expectedFlags = 0u;",
            "state.job.flags != expectedFlags",
            "state.job.payloadHash != 0u",
            "g_BulletManager.bulletsPtrs[bucket]",
            "state.managerBucketHeads[bucket]",
            "end < previousEnd",
            "end > state.recordCount",
            "previousEnd == state.recordCount",
        ):
            self.assertIn(proof, authority)
        for forbidden in (
            "std::memcmp",
            "PspMeRenderBuildLiveRecord",
            "for (Bullet *",
            "while (bullet)",
            "static_cast<u32>(state.managerBulletCount) != state.recordCount",
        ):
            self.assertNotIn(forbidden, authority)

        snapshot_complete = function_body(
            self.bullets, "PspMeRenderCorrectnessAfterCalc("
        )
        for captured in (
            "state.managerBulletCount = g_BulletManager.bulletCount",
            "state.managerUpdateCount = g_BulletManager.updateCount",
            "state.managerTimePrevious = g_BulletManager.time.previous",
            "state.managerTimeSubFrameBits",
            "state.managerTimeCurrent = g_BulletManager.time.current",
            "state.managerMutationEpoch",
            "state.representativeSourceGeneration",
            "state.managerBucketHeads[bucket]",
        ):
            self.assertIn(captured, snapshot_complete)

    def test_performance_authority_accepts_same_update_despawns(self) -> None:
        authority = function_body(
            self.bullets, "PspMeRenderReadyAuthorityMatches(u32 expectedDrawSeq)"
        )

        # OnUpdate counts a live slot before it can Initialize() and skip list
        # insertion.  Therefore a dense frame with removals legitimately has
        # managerBulletCount > linked recordCount.  Only the impossible inverse
        # relation is a coverage failure; the captured count and all six linked
        # list heads remain live draw-time authority.
        self.assertIn(
            "static_cast<u32>(state.managerBulletCount) < state.recordCount",
            authority,
        )
        self.assertNotIn(
            "static_cast<u32>(state.managerBulletCount) != state.recordCount",
            authority,
        )
        self.assertIn(
            "g_BulletManager.bulletCount != state.managerBulletCount",
            authority,
        )
        self.assertEqual(authority.count("state.managerBucketHeads[bucket]"), 1)
        self.assertIn("end < previousEnd", authority)
        self.assertIn("end > state.recordCount", authority)
        self.assertIn("previousEnd == state.recordCount", authority)

    def test_i3_fuses_calc12_capture_into_six_reverse_staging_buckets(self) -> None:
        self.assertIn(
            "records[6][TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS]",
            self.bullets,
        )
        self.assertIn(
            "6u * TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS",
            self.bullets,
        )

        update = function_body(self.bullets, "BulletManager::OnUpdate(")
        assert_order(
            self,
            update,
            "PspMeRenderBeginFusedCapture(arg)",
            "PspMeRenderCaptureFusedRecord(",
            "bullet->next = arg->bulletsPtrs",
            "PspMeRenderPublishFusedCapture(arg)",
        )
        capture = function_body(
            self.bullets, "PspMeRenderCaptureFusedRecord("
        )
        self.assertIn(
            "TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS - 1u -",
            capture,
        )
        self.assertIn("capture.bucketCounts[bucket]", capture)

        fused = function_body(
            self.bullets, "PspMeRenderBuildFusedSnapshot("
        )
        self.assertIn("for (u32 bucket = 0u; bucket < 6u; ++bucket)", fused)
        self.assertIn("std::memcpy(build.records + count", fused)
        self.assertIn(
            "capture.records[bucket] + tail", fused
        )
        self.assertIn(
            "capture.bucketHeads[bucket] != manager->bulletsPtrs[bucket]",
            fused,
        )
        self.assertNotIn("while (bullet)", fused)
        self.assertNotIn("PspMeRenderSelectVm", fused)

        after_calc = function_body(
            self.bullets, "PspMeRenderCorrectnessAfterCalc("
        )
        self.assertIn("#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)", after_calc)
        self.assertIn("PspMeRenderBuildFusedSnapshot(", after_calc)
        self.assertIn("PspMeRenderBuildCorrectnessSnapshot(", after_calc)
        diagnostic = function_body(
            self.bullets, "PspMeRenderBuildCorrectnessSnapshot("
        )
        self.assertIn("while (bullet)", diagnostic)

    def test_i3_reverse_tail_matches_canonical_prepend_order(self) -> None:
        encounter_order = [0, *range(1023, 0, -1)]
        canonical = [[] for _ in range(6)]
        staging = [[None] * 1024 for _ in range(6)]
        counts = [0] * 6
        for slot in encounter_order:
            bucket = (slot * 5 + 1) % 6
            canonical[bucket].insert(0, slot)
            staging[bucket][1023 - counts[bucket]] = slot
            counts[bucket] += 1
        for bucket in range(6):
            self.assertEqual(
                staging[bucket][1024 - counts[bucket] :],
                canonical[bucket],
            )

    def test_i3_moves_exact_vm_side_effects_only_under_proven_draw_gate(self) -> None:
        begin = function_body(self.bullets, "PspMeRenderBeginFusedCapture(")
        self.assertIn("Th07PspCanCommitBulletWarmQueue()", begin)
        capture = function_body(
            self.bullets, "PspMeRenderCaptureFusedRecord("
        )
        for effect in (
            "vm->pos.x = capture.arcadeLeft + bullet->pos.x",
            "vm->pos.y = capture.arcadeTop + bullet->pos.y",
            "vm->pos.z = 0.05f",
            "vm->color.color",
            "vm->SetRotationZ(bullet->pspRenderAngle)",
            "vm->updateRotation = 1",
        ):
            self.assertIn(effect, capture)

        consume = function_body(self.bullets, "PspMeRenderTryGeConsume(")
        self.assertEqual(consume.count("PspMeRenderCommitVmSideEffects(manager)"), 2)
        self.assertGreaterEqual(
            consume.count("#if !defined(TH07_PSP_ME_RENDER_PERFORMANCE)"), 2
        )
        commit = function_body(
            self.bullets, "PspMeRenderCommitVmSideEffects("
        )
        self.assertIn("for (Bullet *bullet", commit)
        for source in (
            self.window,
            self.window_h,
            self.replay,
            self.replay_h,
        ):
            self.assertIn("defined(TH07_PSP_ME_RENDER_PERFORMANCE)", source)
        gate = function_body(
            self.window, "bool Th07PspCanCommitBulletWarmQueue()"
        )
        self.assertIn("!g_PspFixed30Fps", gate)
        self.assertIn("!ReplayManager::MayRestartCalcChainAfterBulletUpdate()", gate)

    def test_i3_mutation_epoch_allows_calc12_rebuild_but_rejects_late_clear(self) -> None:
        self.assertIn("u32 pspMeRenderMutationEpoch;", self.bullets_h)
        track = function_body(self.bullets_h, "void PspTrackBulletSlot(")
        forget = function_body(self.bullets_h, "void PspForgetBulletSlot(")
        for body in (track, forget):
            self.assertIn("PspMarkMeRenderMutation();", body)

        for signature in (
            "BulletManager::RemoveAllBullets(",
            "BulletManager::DespawnBullets(",
            "BulletManager::RemoveBulletsInRadius(",
            "BulletManager::StopBulletMovement(",
        ):
            self.assertIn(
                "PspMarkMeRenderMutation();",
                function_body(self.bullets, signature),
            )

        publish = function_body(
            self.bullets, "PspMeRenderPublishFusedCapture("
        )
        self.assertIn(
            "capture.mutationEpoch = manager->pspMeRenderMutationEpoch",
            publish,
        )
        fused = function_body(
            self.bullets, "PspMeRenderBuildFusedSnapshot("
        )
        self.assertIn(
            "capture.mutationEpoch != manager->pspMeRenderMutationEpoch",
            fused,
        )
        authority = function_body(
            self.bullets, "PspMeRenderReadyAuthorityMatches(u32 expectedDrawSeq)"
        )
        self.assertIn(
            "g_BulletManager.pspMeRenderMutationEpoch !=",
            authority,
        )

    def test_i3_representative_source_is_cached_and_texture_safe(self) -> None:
        self.assertIn("u16 representative[264];", self.bullets)
        self.assertIn("u32 textureIds[264];", self.bullets)
        build = function_body(
            self.bullets, "PspMeRenderBuildRepresentativeSourceCache()"
        )
        self.assertIn("cache.representative[source]", build)
        self.assertIn("cache.textureIds[source]", build)
        matches = function_body(
            self.bullets, "PspMeRenderRepresentativeSourceCacheMatches()"
        )
        self.assertIn("for (u32 source = 0u; source < 264u; ++source)", matches)
        self.assertIn("g_AnmManager->textures[source].id", matches)

        representative = function_body(
            self.bullets, "PspMeRenderRepresentativeSource("
        )
        perf_branch = representative.split("#else", 1)[0]
        self.assertIn("cache.representative[sourceFileIndex]", perf_branch)
        self.assertNotIn("for (u32 candidate", perf_branch)
        begin = function_body(self.bullets, "PspMeRenderBeginFusedCapture(")
        authority = function_body(
            self.bullets, "PspMeRenderReadyAuthorityMatches(u32 expectedDrawSeq)"
        )
        self.assertIn("PspMeRenderEnsureRepresentativeSourceCache()", begin)
        self.assertIn("PspMeRenderRepresentativeSourceCacheMatches()", authority)
        self.assertLess(
            begin.index("!g_GameManager.isTimeStopped"),
            begin.index("PspMeRenderEnsureRepresentativeSourceCache()"),
        )
        for signature in (
            "PspMeRenderPublishFusedCapture(",
            "PspMeRenderBuildFusedSnapshot(",
        ):
            phase = function_body(self.bullets, signature)
            self.assertIn(
                "PspMeRenderRepresentativeSourceCacheIdentityMatches(", phase
            )
            self.assertNotIn(
                "PspMeRenderRepresentativeSourceCacheMatches()", phase
            )

    def test_invalid_source_is_fail_closed_only_for_drawable_records(self) -> None:
        for signature in (
            "PspMeRenderCaptureFusedRecord(",
            "PspMeRenderBuildCorrectnessSnapshot(",
            "PspMeRenderBuildLiveRecord(",
        ):
            body = function_body(self.bullets, signature)
            drawable = body.index("const bool drawable")
            invalid = body.index("originalSource < 0", drawable)
            self.assertLess(drawable, invalid)
            guarded = body[drawable : invalid + 80]
            if signature == "PspMeRenderCaptureFusedRecord(":
                self.assertIn("if (drawable)", guarded)
                self.assertNotIn("if (vm->sprite)", guarded)
            else:
                self.assertIn("drawable &&", guarded)

    def test_performance_preflights_every_run_before_begin(self) -> None:
        runs = function_body(self.bullets, "PspMeRenderReadyRunsValid()")
        for proof in (
            "(ready.runCount == 0u) != (ready.vertexBytes == 0u)",
            "run.recordCount == 0u",
            "run.firstRecord >= state.recordCount",
            "run.firstRecord <= previousFirstRecord",
            "run.recordCount > state.recordCount - run.firstRecord",
            "run.firstRecord < previousRecordEnd",
            "run.recordCount > state.recordCount - totalRecords",
            "run.firstVertex != totalVertices",
            "run.sourceFileIndex >= 264u",
            "PspMeRenderRepresentativeSource(",
            "g_AnmManager->textures[run.sourceFileIndex].id == 0u",
            "run.logicalState >= static_cast<u32>(BULLET_END_ARRAY)",
            "run.renderStateFlags & ~allowedStateFlags",
            "if (generalMode)",
            "verticesPerRecord = 2u",
            "verticesPerRecord = 4u",
            "run.recordCount > vertexCapacity / verticesPerRecord",
            "run.vertexCount != expectedVertices",
            "run.vertexCount > vertexCapacity - totalVertices",
            "previousRecordEnd = run.firstRecord + run.recordCount",
            "totalVertices * sizeof(Th07PspMeRenderStreamVertex)",
            "ready.vertexBytes",
        ):
            self.assertIn(proof, runs)

        validate = function_body(
            self.bullets, "PspMeRenderValidateReadyStream("
        )
        light = validate.index("return PspMeRenderReadyRunsValid();")
        heavy = validate.index("std::memcmp(&live")
        self.assertLess(light, heavy)
        self.assertIn(
            "PspMeRenderReadyAuthorityMatches(expectedDrawSeq)",
            validate[:light],
        )

        consume = function_body(self.bullets, "PspMeRenderTryGeConsume(")
        assert_order(
            self,
            consume,
            "PspMeRenderValidateReadyStream(",
            "Th07PspBeginMeRenderGeSubmission(",
        )

    def test_priority_18_snapshot_sentinel_is_the_final_calc_owner(self) -> None:
        priorities = [
            int(priority)
            for priority in re.findall(
                r"AddToCalcChain\s*\([^,]+,\s*(\d+)\s*\)",
                self.engine_sources,
            )
        ]
        self.assertTrue(priorities)
        self.assertEqual(max(priorities), 18)
        self.assertEqual(priorities.count(18), 1)
        self.assertIn(
            "AddToCalcChain(&g_PspMeRenderCalcCompleteChain, 18)",
            self.bullets,
        )
        self.assertIn(
            "Priority 18 must remain the final calc callback", self.bullets
        )

    def test_low_level_ownership_is_ready_to_ge_to_free_only(self) -> None:
        mark = function_body(
            self.audio, "th07_psp_me_render_stream_mark_ge_in_flight("
        )
        release = function_body(
            self.audio, "th07_psp_me_render_stream_release_after_ge("
        )
        abort = function_body(
            self.audio, "th07_psp_me_render_stream_abort_ge_mark("
        )

        assert_order(
            self,
            mark,
            "TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC",
            "sceKernelDcacheWritebackInvalidateRange",
            "TH07_PSP_ME_RENDER_STREAM_STATE_GE_IN_FLIGHT",
        )
        assert_order(
            self,
            release,
            "TH07_PSP_ME_RENDER_STREAM_STATE_GE_IN_FLIGHT",
            "TH07_PSP_ME_RENDER_STREAM_STATE_FREE",
        )
        assert_order(
            self,
            abort,
            "TH07_PSP_ME_RENDER_STREAM_STATE_GE_IN_FLIGHT",
            "TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC",
        )
        self.assertNotIn("TH07_PSP_ME_RENDER_STREAM_STATE_FREE", abort)
        self.assertIn("no GE command may have been enqueued", abort)
        for body in (mark, release, abort):
            self.assertNotIn("sceGu", body)
            self.assertNotIn("sceGe", body)

    def test_low_level_interlock_closes_generation_aba_and_shutdown_reuse(self) -> None:
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_STATE_SC_TRANSITION = 6", self.audio_h)
        transition = function_body(
            self.audio, "me_render_stream_begin_sc_transition("
        )
        self.assertIn("__atomic_compare_exchange_n", transition)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_STATE_SC_TRANSITION", transition)
        self.assertGreaterEqual(transition.count("slot->generation"), 2)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED", transition)

        acquire = function_body(
            self.audio, "th07_psp_me_render_stream_acquire("
        )
        assert_order(
            self,
            acquire,
            "TH07_PSP_ME_RENDER_STREAM_STATE_FREE",
            "TH07_PSP_ME_RENDER_STREAM_STATE_SC_TRANSITION",
            "slot->generation",
            "TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD",
        )
        self.assertNotIn("TH07_PSP_ME_RENDER_STREAM_STATE_GE_IN_FLIGHT", acquire)

        drain = function_body(self.audio, "me_render_stream_drain_for_shutdown(")
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD", drain)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC", drain)
        self.assertIn("return 0;", drain)
        # Shutdown may cancel an SC build or release an unsubmitted READY slot,
        # but it has no path that turns a GE-owned slot into FREE.
        self.assertNotIn("th07_psp_me_render_stream_release_after_ge", drain)
        self.assertNotIn("th07_psp_me_render_stream_abort_ge_mark", drain)

    def test_complete_live_stream_and_all_runs_are_validated_before_begin(self) -> None:
        validate = function_body(self.bullets, "PspMeRenderValidateReadyStream(")
        consume = function_body(self.bullets, "PspMeRenderTryGeConsume(")

        for proof in (
            "ready.token.slot != state.token.slot",
            "ready.token.generation != state.token.generation",
            "for (u32 bucket = 0u; bucket < 6u; ++bucket)",
            "std::memcmp(&live, &state.records[recordIndex]",
            "state.job.bucketEnds[bucket] != recordIndex",
            "recordIndex != state.recordCount",
            "PspMeRenderRepresentativeSource(",
            "g_AnmManager->textures[expectedSource].id == 0u",
            "generalMode = true",
            "PspMeRenderRunMatches(",
            "runIndex != ready.runCount",
            "ready.vertexBytes",
        ):
            self.assertIn(proof, validate)
        for forbidden in (
            "Th07PspBeginMeRenderGeSubmission",
            "Th07PspDrawMeRenderStreamRun",
            "sceGu",
            "vm->pos",
            "SetRotationZ",
        ):
            self.assertNotIn(forbidden, validate)

        assert_order(
            self,
            consume,
            "PspMeRenderValidateReadyStream(",
            "Th07PspBeginMeRenderGeSubmission(",
            "PspMeRenderCommitVmSideEffects(manager)",
            "Th07PspDrawMeRenderStreamRun(",
            "Th07PspEndMeRenderGeSubmission()",
        )

    def test_direct_path_recreates_canonical_vm_and_renderer_state(self) -> None:
        commit = function_body(self.bullets, "PspMeRenderCommitVmSideEffects(")
        for side_effect in (
            "vm->pos.x",
            "vm->pos.y",
            "vm->pos.z = 0.05f",
            "vm->color.color",
            "vm->SetRotationZ(bullet->pspRenderAngle)",
            "vm->updateRotation = 1",
        ):
            self.assertIn(side_effect, commit)

        consume = function_body(self.bullets, "PspMeRenderTryGeConsume(")
        for state_step in (
            "g_AnmManager->Flush();",
            "SetTextureArg(TEX_ARG_DIFFUSE)",
            "SetColorOp(COMPONENT_ALPHA",
            "SetColorOp(COMPONENT_RGB",
            "g_AnmManager->currentTexture = texture",
            "BindTexture(texture)",
            "g_AnmManager->currentVertexShader = 1",
            "SetBlendMode(",
            "g_AnmManager->currentZWriteDisable",
            "SetDepthMask(!zWriteDisable)",
            "g_AnmManager->pspSpriteBatchUsesPairs = pairs",
            "g_AnmManager->pspUnifiedBulletGeneralMode = 1",
            "g_AnmManager->renderStateChangesThisFrame += run.recordCount",
            "g_AnmManager->pspForceSpriteQuads = 0",
        ):
            self.assertIn(state_step, consume)

    def test_external_stream_is_submitted_immediately_as_pairs_or_indexed_quads(self) -> None:
        draw = function_body(self.graphics, "DrawMeRenderStreamRun(")
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES", draw)
        self.assertIn("sceGuDrawArray(GU_SPRITES", draw)
        self.assertIn("nullptr, vertices", draw)
        self.assertIn("sceGuDrawArray(GU_TRIANGLES", draw)
        self.assertIn("GU_INDEX_16BIT", draw)
        self.assertIn("gQuadIndices", draw)
        self.assertIn("batch", draw)
        self.assertIn("FlushDeferredSpriteDraw();", draw)
        for deferred_or_blocking in (
            "mDeferredSpriteVertices = vertices",
            "DrawSpritePairs",
            "DrawSpriteQuads",
            "sceGuSync",
            "sceGeDrawSync",
            "sceKernelDcache",
        ):
            self.assertNotIn(deferred_or_blocking, draw)

    def test_open_submission_survives_internal_list_restart_until_real_fence(self) -> None:
        begin = function_body(self.graphics, "BeginMeRenderGeSubmission(")
        draw = function_body(self.graphics, "DrawMeRenderStreamRun(")
        end = function_body(self.graphics, "EndMeRenderGeSubmission(")
        release = function_body(
            self.graphics, "void ReleaseMeRenderGeTokenAfterSync()"
        )
        restart = function_body(self.graphics, "void SubmitAndRestart()")
        ensure = function_body(
            self.graphics, "bool EnsureListSpace(unsigned int vertexBytes)"
        )
        swap = function_body(self.graphics, "SwapBuffers() override")

        assert_order(
            self,
            begin,
            "mMeRenderGeTokenPending = true",
            "mMeRenderGeSubmissionOpen = true",
            "th07_psp_me_render_stream_mark_ge_in_flight(",
        )
        self.assertIn("mMeRenderGeSubmissionOpen = false", end)
        self.assertNotIn("release_after_ge", end)
        self.assertIn("EnsureListSpace(0)", draw)
        self.assertIn("SubmitAndRestart();", ensure)
        assert_order(
            self,
            restart,
            "sceGuFinish()",
            "sceGuSync(0, 0)",
            "ReleaseMeRenderGeTokenAfterSync()",
            "StartList()",
        )
        self.assertIn(
            "!mMeRenderGeTokenPending || mMeRenderGeSubmissionOpen", release
        )
        self.assertIn("th07_psp_me_render_stream_release_after_ge", release)
        assert_order(
            self,
            swap,
            "sceGuFinish()",
            "sceDisplayWaitVblankStart()",
            "sceGuSync",
            "ReleaseMeRenderGeTokenAfterSync()",
            "sceGuSwapBuffers()",
        )

    def test_failures_before_enqueue_take_same_frame_canonical_fallback(self) -> None:
        consume = function_body(self.bullets, "PspMeRenderTryGeConsume(")
        fallback = function_body(
            self.bullets,
            "void PspMeRenderReleaseReadyForFallback(bool identityFault)\n{",
        )
        deadline = function_body(
            self.bullets, "PspMeRenderCorrectnessDrawDeadline("
        )
        draw = function_body(self.bullets, "BulletManager::OnDraw(")

        self.assertIn("PspMeRenderReleaseReadyForFallback(true);", consume)
        self.assertIn("PspMeRenderReleaseReadyForFallback(false);", consume)
        self.assertIn("return false;", consume)
        self.assertIn("++gPspMeRenderShadowWindow.fallbackFrames", fallback)
        self.assertIn("th07_psp_me_render_stream_release_ready", fallback)
        self.assertNotIn("sceGu", consume)
        self.assertIn("retired == 0", deadline)
        self.assertIn("++gPspMeRenderShadowWindow.fallbackFrames", deadline)

        assert_order(
            self,
            draw,
            "PspMeRenderTryGeConsume(arg",
            "if (!pspMeGeConsumed)",
            "bullet->Draw();",
        )
        # Once Begin has succeeded, the path is deliberately infallible until
        # End; a late partial-frame fallback would duplicate or reorder bullets.
        last_commit = consume.rfind("PspMeRenderCommitVmSideEffects(manager)")
        self.assertGreater(last_commit, consume.index("Th07PspBeginMeRenderGeSubmission("))
        self.assertNotIn("return false", consume[last_commit:])

    def test_final_ge_fence_precedes_me_owner_shutdown(self) -> None:
        assert_order(
            self,
            self.main,
            "Th07PspFenceMeRenderBeforeMeShutdown();",
            "g_SoundPlayer.Release();",
            "SAFE_DELETE(g_Supervisor.gfxDevice);",
        )
        fence = function_body(self.graphics, "FenceMeRenderBeforeMeShutdown(")
        self.assertIn("mMeRenderGeTokenPending", fence)
        self.assertIn("mMeRenderGeSubmissionOpen || !mListOpen", fence)
        self.assertIn("Th07PspMeRenderGeReleaseFault();", fence)
        self.assertIn("SubmitAndRestart();", fence)
        self.assertIn("Th07PspFenceMeRenderBeforeMeShutdown", self.graphics_h)

    def test_render_worker_uses_main_ram_and_reports_me_edram_zero(self) -> None:
        kernel = function_body(self.audio, "me_render_stream_expand_kernel(")
        mark = function_body(
            self.audio, "th07_psp_me_render_stream_mark_ge_in_flight("
        )
        for pool in (
            "gMeRenderStreamInputAreas",
            "gMeRenderStreamOutputAreas",
            "gMeRenderStreamRunAreas",
        ):
            self.assertRegex(
                self.audio,
                rf"static\s+MeRenderStream\w+Area\s+\n?\s*{pool}",
            )
        for forbidden in (
            "ME_AUDIO_EDRAM",
            "meCoreEDRAMAlloc",
            "meCoreEDRAMFree",
        ):
            self.assertNotIn(forbidden, kernel + mark)
        self.assertIn("gMeRenderBenchSummary.meEdramBytes = 0u", self.audio)
        self.assertIn("ME12 SELFTEST PASS EDRAM0 GE-OWNER", self.audio)
        self.assertIn("ME_EDRAM=UNUSED 0/0", self.main)
        self.assertIn("summary.meEdramBytes == 0u", self.main)

    def test_i2_telemetry_counts_consumption_and_gates_fault_free_windows(self) -> None:
        for counter in (
            "streamGeFrames",
            "streamGeRuns",
            "streamGeVertices",
        ):
            self.assertIn(f"unsigned int {counter};", self.bullets_h)
            self.assertIn(f"gPspMeRenderShadowWindow.{counter}", self.bullets)
            self.assertIn(f"merw.{counter}", self.graphics)

        for label in (
            "PERF MERW I2 ",
            "PERF MERWT I2 ",
            "PERF MERW I2 OBS ",
            "PERF MERW I3 ",
            "PERF MERWT I3 ",
            "PERF MERW I3 OBS ",
            "GFR%u GSR%u GVX%u",
            "SC_DRAW=0",
            "SC_FALLBACK=1",
            "GE_CONSUME=1",
            "PERF MERW I2 FORMAT_OVERFLOW G0",
            "PERF MERW I2 OBS FORMAT_OVERFLOW G0",
            "PERF MERW I3 FORMAT_OVERFLOW G0",
            "PERF MERW I3 OBS FORMAT_OVERFLOW G0",
        ):
            self.assertIn(label, self.graphics)

        # A dense window is green only if every eligible frame was consumed,
        # no canonical fallback occurred, GE counters are non-empty, and all
        # correctness/audio-owner faults remain zero.
        for gate in (
            "merw.streamCompared == 0u",
            "merw.wouldConsume == mPerfFrames",
            "merw.fallbackFrames == 0u",
            "merw.streamGeFrames == mPerfFrames",
            "merw.streamGeRuns >= mPerfFrames",
            "merw.streamGeVertices != 0u",
            "merw.protocolFault == 0u",
            "merw.streamIdentityDrop == 0u",
            "merw.streamReleaseFault == 0u",
            "merw.scCopyUs == 0ull",
            "meAudioJobs == 0u",
            "meAudioFallbacks == 0u",
            "meAudioTimeouts == 0u",
        ):
            self.assertIn(gate, self.graphics)


if __name__ == "__main__":
    unittest.main()

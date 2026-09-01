from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


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


def preprocessor_block(source: str, directive: str) -> tuple[int, int, str]:
    """Return one complete, possibly nested, #if...#endif region."""
    start = source.index(directive)
    depth = 0
    offset = start
    for line in source[start:].splitlines(keepends=True):
        stripped = line.lstrip()
        if re.match(r"#\s*(?:if|ifdef|ifndef)\b", stripped):
            depth += 1
        elif re.match(r"#\s*endif\b", stripped):
            depth -= 1
            if depth == 0:
                end = offset + len(line)
                return start, end, source[start:end]
        offset += len(line)
    raise AssertionError(f"unterminated preprocessor block: {directive}")


def without_preprocessor_blocks(source: str, directive: str) -> str:
    """Remove every complete region beginning with the exact directive."""
    result = source
    while directive in result:
        start, end, _ = preprocessor_block(result, directive)
        result = result[:start] + result[end:]
    return result


class PspFinal60ProfilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.graphics = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text(
            encoding="utf-8"
        )
        cls.chain = (ROOT / "src/Chain.cpp").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src/BulletManager.cpp").read_text(encoding="utf-8")
        cls.anm = (ROOT / "src/AnmManager.cpp").read_text(encoding="utf-8")
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.fileio = (ROOT / "psp/fileio.cpp").read_text(encoding="utf-8")

    def test_profiles_are_explicit_and_part_of_the_object_stamp(self) -> None:
        self.assertIn("PSP_PERF_PROFILE ?= ATTRIB", self.makefile)
        self.assertIn("PSP_PERF_PROFILE ?= RELEASE", self.makefile)
        self.assertIn("-DTH07_PSP_PERF_ATTRIB", self.makefile)
        self.assertIn("-DTH07_PSP_PERF_ACCEPT", self.makefile)
        self.assertIn("PSP_PERF_ATTRIB_TARGET ?= M2", self.makefile)
        self.assertIn("-DTH07_PSP_PERF_M2", self.makefile)
        self.assertIn("-DTH07_PSP_PERF_M3", self.makefile)
        stamp = next(
            line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_PERF_PROFILE)", stamp)
        self.assertIn("$(PSP_PERF_ATTRIB_TARGET)", stamp)
        self.assertIn("$(PSP_PERF_GPU_ATTRIB)", stamp)
        self.assertIn("$(PSP_PERF_EMPTY_TIMERS)", stamp)
        self.assertIn("$(PSP_PERF_DENSE_SLICE)", stamp)
        self.assertIn("-DTH07_PSP_PERF_EMPTY_TIMERS", self.makefile)
        self.assertIn("-DTH07_PSP_PERF_DENSE_SLICE", self.makefile)

    def test_m2_frame_boundary_includes_finish_and_next_frame_setup(self) -> None:
        swap = function_body(self.graphics, "void SwapBuffers() override")
        self.assertLess(swap.index("sceGuFinish()"), swap.index("finishEndUs"))
        self.assertLess(swap.index("sceDisplayWaitVblankStart()"), swap.index("vblankEndUs"))
        self.assertLess(swap.index("geEndUs"), swap.index("PublishUpperPortraitTelemetry()"))
        self.assertLess(
            swap.index("nextFrameStartUs = sceKernelGetSystemTimeWide()"),
            swap.index("sceGuSwapBuffers()"),
        )
        self.assertLess(
            swap.index("AccumulateAndReportPerf"),
            swap.index("nextFrameStartUs = sceKernelGetSystemTimeWide()"),
        )
        self.assertLess(swap.index("sceGuSwapBuffers()"), swap.index("StartList()"))
        self.assertLess(swap.index("StartList()"), swap.index("PreserveLatestPlayfield()"))
        self.assertLess(swap.index("PreserveLatestPlayfield()"), swap.index("ClearPillarboxes()"))

    def test_gameplay_lifecycle_emits_the_partial_tail_before_flush(self) -> None:
        game = (ROOT / "src/GameManager.cpp").read_text(encoding="utf-8")
        added = function_body(game, "ZunResult GameManager::AddedCallback")
        deleted = function_body(game, "ZunResult GameManager::DeletedCallback")
        self.assertLess(added.index("arg->notInMenu = 1"),
                        added.index("Th07PspPerfBeginGameplayWindow(arg->currentStage)"))
        self.assertLess(deleted.index("Th07PspPerfFinalizeGameplayWindow()"),
                        deleted.index("th07_psp_perf_log_flush()"))
        finalize = function_body(self.graphics, "void PerfFinalizeGameplayWindow()")
        self.assertIn("mPerfFrames != 0u", finalize)
        self.assertIn("ReportPerfWindow", finalize)
        begin = function_body(self.graphics, "void PerfBeginGameplayWindow(int stage)")
        self.assertIn("mPerfGameplayPending = true", begin)
        self.assertIn("mPerfPendingStage = stage", begin)
        self.assertNotIn("mFrameStartUs", begin)
        swap = function_body(self.graphics, "void SwapBuffers() override")
        self.assertLess(swap.index("AccumulateAndReportPerf"),
                        swap.index("if (mPerfGameplayPending)"))
        self.assertLess(swap.index("ResetPerfWindowCounters();"),
                        swap.index("mFrameStartUs = nextFrameStartUs"))
        self.assertIn("mLastVblankCount = vblankCount", swap)
        report = function_body(self.graphics, "void ReportPerfWindow")
        self.assertIn("mPerfWindowState, mPerfWindowStage", report)
        self.assertNotIn("g_Supervisor.curState, g_GameManager.currentStage", report)
        reset = function_body(self.graphics, "void ResetPerfWindowCounters()")
        self.assertNotIn("mLastVblankCount = 0", reset)

    def test_blocking_ge_is_reference_only_and_critical_metric_is_cpu_plus_tail(self) -> None:
        perf = function_body(self.graphics, "void AccumulateAndReportPerf")
        self.assertIn("const unsigned long long criticalUs = cpuUs + geTailUs;", perf)
        self.assertIn("mPerfBlockingGeUs += mFrameBlockingGeUs;", perf)
        self.assertNotIn("mPerfGeUs += mFrameBlockingGeUs", perf)
        self.assertIn("mPerfCriticalSamplesUs[mPerfFrames]", perf)
        self.assertIn("mPerfOverBudgetFrames", perf)
        self.assertIn("mPerfVsyncMisses", perf)

    def test_m2_has_all_priorities_and_independent_chain_overhead(self) -> None:
        for priority in range(18):
            self.assertIn(f"drawJob10({priority})", self.graphics)
        self.assertIn("Th07PspPerfAddDrawChainOverheadTime", self.chain)
        self.assertIn("current->callback, jobStartUs", self.chain)
        self.assertIn("PERF OWNMAP", self.graphics)
        self.assertIn("ownerClosureErrorUs", self.graphics)
        self.assertIn(
            "accountedDrawUs = drawJobsUs + gPerfDrawChainOverheadUs", self.graphics
        )
        self.assertIn("200ull * mPerfFrames, gPerfDrawChainUs / 50ull", self.graphics)
        self.assertIn("gPerfDrawOutOfRange == 0u", self.graphics)
        self.assertIn("PERF M2I", self.graphics)
        for field in ("PKUS", "MXUS", "STUS", "FLUS", "DCUS", "OTUS"):
            self.assertIn(field, self.graphics)

    def test_empty_timer_profile_runs_probes_but_emits_perf_accept_only(self) -> None:
        self.assertIn(
            "defined(TH07_PSP_PERF_ATTRIB) || defined(TH07_PSP_PERF_EMPTY_TIMERS)",
            self.chain,
        )
        perf = function_body(self.graphics, "void ReportPerfWindow")
        self.assertIn("TH07_PSP_PERF_EMPTY_TIMERS", perf)
        self.assertIn("Th07PspTakeM3PerfWindow(&emptyM3)", perf)
        self.assertIn("emptyDrawClosureErrorUs <= emptyDrawClosureLimitUs", perf)
        self.assertIn("emptyOwnerClosureErrorUs == 0u", perf)
        self.assertIn("gPerfInternalMismatch == 0u", perf)
        self.assertIn("emptyClosureErrorUs <= emptyClosureLimitUs", perf)
        self.assertIn("emptyDetailErrorUs <= emptyDetailLimitUs", perf)
        self.assertIn("emptyTransferValid", perf)

    def test_m3_coarse_partition_keeps_laser_item_and_bullets_separate(self) -> None:
        draw = function_body(self.bullets, "u32 BulletManager::OnDraw")
        self.assertIn("laserEndUs", draw)
        self.assertIn("itemEndUs", draw)
        self.assertIn("gPspM3PerfWindow.laserUs", draw)
        self.assertIn("gPspM3PerfWindow.itemUs", draw)
        self.assertIn("gPspM3PerfWindow.bulletUs", draw)
        self.assertNotIn("g_AnmManager->Flush()", draw)

    def test_m3_samples_emitter_without_timing_every_bullet(self) -> None:
        self.assertIn("kPspM3EmitterSampleStride = 32u", self.anm)
        self.assertIn("PspM3EmitterSample m3Sample", self.anm)
        self.assertEqual(self.anm.count("m3Sample.Advance();"), 3)
        self.assertIn("PERF M3S RAWUS%llu BTXUS%llu LKUS%llu VMUS%llu", self.graphics)
        self.assertIn("RPUS%llu DCUS%llu SUMUS%llu", self.graphics)
        self.assertIn("CIUS%llu CIDCUS%llu COUS%llu CODCUS%llu", self.graphics)
        self.assertIn("MIX%u UNRES%u G%u", self.graphics)
        self.assertIn("PEND%uK", self.graphics)
        self.assertIn("PerfM3CalibrateTimerReadQ8", self.graphics)
        self.assertIn("TMRQ8%llu FRAWUS%llu POVUS%llu", self.graphics)
        self.assertIn("REC%u/%u/%u/%u", self.graphics)
        self.assertIn("emitter.phaseRecords[phase]", self.graphics)
        self.assertIn("Th07PspM3EmitterBackendBegin", self.graphics)
        self.assertIn("gPerfM3BulletLoopActive", self.graphics)
        population = function_body(
            self.anm, "bool Th07PspM3EmitterPopulationValid"
        )
        self.assertIn("window->emitterCalls / kPspM3EmitterSampleStride", population)
        self.assertIn("window->samples >= minimumSamples", population)
        self.assertIn("window->samples <= maximumSamples", population)
        self.assertIn("sampledBulletDraws == window->samples", population)
        self.assertIn("window->emitterCalls == bulletVisits", population)
        self.assertIn("window->sampledCulls <= window->samples", population)
        self.assertEqual(
            self.graphics.count("Th07PspM3EmitterPopulationValid("), 2
        )

    def test_m3_omits_unpresented_partial_tail(self) -> None:
        finalize = function_body(
            self.graphics, "void PerfFinalizeGameplayWindow"
        )
        self.assertIn("m3 partial omitted N%u", finalize)
        self.assertIn("ResetPerfWindowCounters();", finalize)
        m3_branch = finalize.split("#if defined(TH07_PSP_PERF_M3)", 1)[1].split(
            "#else", 1
        )[0]
        self.assertNotIn("ReportPerfWindow", m3_branch)

    def test_plain_accept_omits_detail_timer_probes(self) -> None:
        self.assertIn("-DTH07_PSP_PERF_DETAIL", self.makefile)
        self.assertIn("#if defined(TH07_PSP_PERF_DETAIL)\n    PspChainPerfScope", self.chain)
        stage = (ROOT / "src/Stage.cpp").read_text(encoding="utf-8")
        render = function_body(stage, "i32 Stage::RenderObjects")
        self.assertIn("#if defined(TH07_PSP_PERF_DETAIL)", render)
        accept = function_body(self.graphics, "void ReportPerfWindow").split(
            "#elif defined(TH07_PSP_PERF_ACCEPT)", 1
        )[1]
        self.assertIn("AVGUS%u MAXUS%u P99US%u", accept)

    def test_perf_accept_is_one_compact_line_per_window(self) -> None:
        perf = function_body(self.graphics, "void ReportPerfWindow")
        accept = perf.split("#elif defined(TH07_PSP_PERF_ACCEPT)", 1)[1].split(
            "        ResetPerfWindowCounters();", 1
        )[0]
        # Legacy detail profiles may emit their owned DENSE/MERW lines above,
        # while the final ACCEPT record itself remains exactly one line.  The
        # RID30 A/B branch deliberately drains those counters without logging.
        format_tail = accept[accept.index("const unsigned int critical10") :]
        self.assertEqual(format_tail.count("th07_psp_perf_note("), 1)
        self.assertIn("PERF ACCEPT", format_tail)
        self.assertIn("PERF DENSE", accept)
        self.assertIn("th07_psp_perf_note(merwMessage)", accept)
        self.assertIn("th07_psp_perf_note(merwTimingMessage)", accept)
        self.assertNotIn("PERF DRAW", format_tail)
        self.assertNotIn("PERF GPU", format_tail)

    def test_dense_slice_is_bounded_to_current_stack_and_stage6_windows(self) -> None:
        self.assertIn("PSP_PERF_DENSE_SLICE ?= 0", self.makefile)
        dense_make = self.makefile.split("ifeq ($(PSP_PERF_DENSE_SLICE),1)", 1)[1]
        for required in (
            "PSP_BULLET_ROTATED_DIRECT),1",
            "PSP_BULLET_UNIFIED_QUADS),1",
            "PSP_BULLET_ONEPASS_ROTATED),1",
            "PSP_BULLET_QUIESCENT_ANM),0",
            "-DTH07_PSP_PERF_DENSE_SLICE -DTH07_PSP_PERF_DETAIL",
        ):
            self.assertIn(required, dense_make)
        reset = function_body(self.graphics, "void ResetPerfWindowCounters()")
        self.assertIn("ConfigurePerfDenseSliceForNextWindow();", reset)
        latch = function_body(
            self.graphics, "void ConfigurePerfDenseSliceForNextWindow()"
        )
        self.assertIn("mPerfWindowStage == 6", latch)
        self.assertIn("nextWindow >= 12u", latch)
        self.assertIn("nextWindow <= 15u", latch)
        self.assertIn("PerfDenseCalibrateTimerReadQ8()", latch)

    def test_dense_slice_uses_shared_frame_boundaries_and_timer_free_counts(self) -> None:
        update = function_body(self.bullets, "u32 BulletManager::OnUpdate")
        draw = function_body(self.bullets, "u32 BulletManager::OnDraw")
        for boundary in (
            "pspDenseUpdateStartUs",
            "pspDenseItemEndUs",
            "pspDenseBulletEndUs",
            "pspDenseDrawStartUs",
            "pspDenseLaserEndUs",
            "pspDenseItemDrawEndUs",
        ):
            self.assertIn(boundary, update + draw)
        for field in (
            "updateItemUs",
            "updateBulletUs",
            "updateTailUs",
            "drawLaserUs",
            "drawItemUs",
            "drawBulletUs",
            "onePassAccepts",
            "onePassFallbacks",
            "canonicalDrawCalls",
        ):
            self.assertIn(field, update + draw)
        dense_blocks = re.findall(
            r"#if defined\(TH07_PSP_PERF_DENSE_SLICE\)(.*?)#endif",
            self.bullets,
            re.DOTALL,
        )
        hot_counter_blocks = [
            block for block in dense_blocks
            if "++pspDenseBulletVisits" in block or "++pspDenseOnePass" in block
        ]
        self.assertTrue(hot_counter_blocks)
        for block in hot_counter_blocks:
            self.assertNotIn("sceKernelGetSystemTimeWide()", block)

    def test_dense_slice_closure_and_probe_cost_are_hard_gates(self) -> None:
        report = function_body(self.graphics, "void ReportPerfWindow")
        for required in (
            "updateErrorUs == 0ull",
            "drawErrorUs == 0ull",
            "dense.bulletVisits == dense.onePassAccepts",
            "dense.onePassFallbacks == dense.canonicalDrawCalls",
            "kDenseProbeReadsPerFrame = 15ull",
            "kDenseProbeLimitQ8 = 50ull * 256ull",
            "PERF DENSE",
        ):
            self.assertIn(required, report)

    def test_rid30_ab_fps_is_psp_wall_clock_only(self) -> None:
        report = function_body(self.graphics, "void ReportPerfWindow")
        self.assertIn(
            "defined(TH07_PSP_PERF_ATTRIB) || defined(TH07_PSP_PERF_AB_COMPARE)",
            report,
        )
        self.assertIn("const unsigned long long elapsedUs = geEndUs - mPerfStartUs;", report)
        self.assertIn("mPerfFrames * 10000000ull /", report)
        self.assertIn("HWFPS%u.%u ELUS%llu", report)
        self.assertNotIn("curFps", report)
        self.assertIn('return "ABME";', self.fileio)
        self.assertIn('return "ABSC";', self.fileio)
        self.assertIn("psp3000-rid30-ab-me-build", self.makefile)
        self.assertIn("psp3000-rid30-ab-sc-build", self.makefile)
        self.assertIn("PSP_PERF_AB_COMPARE=1", self.makefile)

    def test_overflow_is_latched_and_release_perf_note_is_noop(self) -> None:
        note = function_body(self.fileio, 'extern "C" void th07_psp_perf_note')
        self.assertIn("gPerfLogInvalid = true;", note)
        self.assertIn("(void)message;", note)
        self.assertIn("PERF PROFILE INVALID OVERFLOW", self.fileio)
        self.assertIn("PERF END VALID=%u DROP=%u", self.fileio)


if __name__ == "__main__":
    unittest.main()

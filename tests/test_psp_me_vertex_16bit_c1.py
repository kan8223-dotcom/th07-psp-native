from __future__ import annotations

import re
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
HEADER = (ROOT / "psp" / "audio_me.h").read_text(encoding="utf-8")
WORKER = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
BULLETS = (ROOT / "src" / "BulletManager.cpp").read_text(encoding="utf-8")
GRAPHICS = (ROOT / "psp" / "graphics" / "PspGuGraphics.cpp").read_text(
    encoding="utf-8"
)
GRAPHICS_H = (ROOT / "psp" / "graphics" / "PspGuGraphics.hpp").read_text(
    encoding="utf-8"
)
IMPLEMENTED = "PSP_ME_RENDER_UV16 ?=" in MAKEFILE


def recipe_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    tail = makefile[start + len(target) + 1 :]
    match = re.search(r"^[A-Za-z0-9_.-]+:", tail, re.MULTILINE)
    return (
        makefile[start:]
        if match is None
        else makefile[start : start + len(target) + 1 + match.start()]
    )


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


@unittest.skipUnless(IMPLEMENTED, "C1 implementation has not landed")
class PspMeVertex16BitProfileTests(unittest.TestCase):
    def test_independent_switches_are_default_off_and_in_profile_stamp(self) -> None:
        for feature in (
            "PSP_ME_RENDER_UV16",
            "PSP_ME_RENDER_XYZ16",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT",
        ):
            with self.subTest(feature=feature):
                self.assertIn(f"{feature} ?= 0", MAKEFILE)
                stamp = next(
                    line
                    for line in MAKEFILE.splitlines()
                    if line.startswith("PROFILE_STAMP :=")
                )
                self.assertIn(f"$({feature})", stamp)

        self.assertIn("-DTH07_PSP_ME_RENDER_UV16", MAKEFILE)
        self.assertIn("-DTH07_PSP_ME_RENDER_XYZ16", MAKEFILE)
        self.assertIn("-DTH07_PSP_ME_RENDER_16BIT_GE_EXPERIMENT", MAKEFILE)

    def test_packed_profiles_are_research_only_and_fail_closed_at_build_time(self) -> None:
        block_start = MAKEFILE.index("# C1 research profiles")
        block_end = MAKEFILE.index("# I-ME8 extends", block_start)
        block = MAKEFILE[block_start:block_end]
        for required in (
            "PSP_ME_RENDER_CORRECTNESS",
            "PSP_ME_RENDER_WORKER",
            "PSP_1000",
            "PSP_ME_RENDER_GE_CONSUME",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT",
        ):
            self.assertIn(required, block)

        self.assertIn("C1 packed vertices are PSP-2000+ research only", HEADER)
        self.assertIn(
            "C1 packed GE consumption requires the explicit readback experiment gate",
            HEADER,
        )

    def test_accepted_rid30_target_does_not_silently_enable_c1(self) -> None:
        target = "psp3000-a1-item-motion-build"
        if f"{target}:" not in MAKEFILE:
            self.fail(f"fixed RID30 target missing: {target}")
        recipe = recipe_body(MAKEFILE, target)
        for feature in (
            "PSP_ME_RENDER_UV16",
            "PSP_ME_RENDER_XYZ16",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT",
        ):
            self.assertIn(f"{feature}=0", recipe)

    def test_stream_versions_are_distinct_for_all_four_abis(self) -> None:
        for expected in ("0x00000100u", "0x00000200u", "0x00000300u"):
            self.assertIn(expected, HEADER)
        self.assertIn(
            "0x4d453131u + TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS",
            HEADER,
        )

    def test_three_m0_targets_are_distinct_and_share_the_rid30_profile(self) -> None:
        targets = {
            "psp3000-c1-uv16-m0-build": ("1", "0", "0x260831c1u"),
            "psp3000-c1-xyz16-m0-build": ("0", "1", "0x260831c2u"),
            "psp3000-c1-uvxyz16-m0-build": ("1", "1", "0x260831c3u"),
        }
        for target, (uv, xyz, build_id) in targets.items():
            with self.subTest(target=target):
                self.assertIn(f"{target}: PSP_C1_BUILD_UV16={uv}", MAKEFILE)
                self.assertIn(f"{target}: PSP_C1_BUILD_XYZ16={xyz}", MAKEFILE)
                self.assertIn(f"{target}: PSP_C1_BUILD_ID={build_id}", MAKEFILE)
                self.assertIn(
                    f"{target}: psp3000-c1-vertex16-m0-build", MAKEFILE
                )

        baseline = dict(
            re.findall(
                r"\b(PSP_[A-Z0-9_]+)=([^\s\\]+)",
                recipe_body(MAKEFILE, "psp3000-a1-item-motion-build"),
            )
        )
        candidate = dict(
            re.findall(
                r"\b(PSP_[A-Z0-9_]+)=([^\s\\]+)",
                recipe_body(MAKEFILE, "psp3000-c1-vertex16-m0-build"),
            )
        )
        allowed_differences = {
            "PSP_AUDIO4M_BUILD_ID",
            "PSP_EBOOT_TITLE",
            "PSP_ME_RENDER_UV16",
            "PSP_ME_RENDER_XYZ16",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT",
        }
        differences = {
            key
            for key in set(baseline) | set(candidate)
            if baseline.get(key) != candidate.get(key)
        }
        self.assertEqual(differences, allowed_differences)
        self.assertEqual(candidate["PSP_1000"], "0")

    def test_current_ab_me_c1_target_preserves_hardware_fps_baseline(self) -> None:
        target = "psp3000-rid30-ab-me-c1-uv16-m0-build"
        expected_assignments = (
            f"{target}: PSP_RID30_AB_ME_UV16=1",
            f"{target}: PSP_RID30_AB_ME_XYZ16=0",
            f"{target}: PSP_RID30_AB_ME_C1_GE_EXPERIMENT=1",
            f"{target}: PSP_RID30_AB_ME_BUILD_ID=0x260901c7u",
            f"{target}: PSP_RID30_AB_ME_TITLE=TH07 RID30 AB ME C1 UV16 M0K",
            f"{target}: psp3000-rid30-ab-me-build",
        )
        for assignment in expected_assignments:
            self.assertIn(assignment, MAKEFILE)

        baseline = recipe_body(MAKEFILE, "psp3000-rid30-ab-me-build")
        for contract in (
            "PSP_PERF_AB_COMPARE=1",
            "PSP_SHIKIGAMI_HOST_IPV4=",
            "PSP_ME_RENDER_UV16=$(PSP_RID30_AB_ME_UV16)",
            "PSP_ME_RENDER_XYZ16=$(PSP_RID30_AB_ME_XYZ16)",
            "PSP_ME_RENDER_16BIT_GE_EXPERIMENT=$(PSP_RID30_AB_ME_C1_GE_EXPERIMENT)",
            "PSP_AUDIO4M_BUILD_ID=$(PSP_RID30_AB_ME_BUILD_ID)",
            "PSP_EBOOT_TITLE='$(PSP_RID30_AB_ME_TITLE)'",
        ):
            self.assertIn(contract, baseline)

        for default in (
            "PSP_RID30_AB_ME_UV16 ?= 0",
            "PSP_RID30_AB_ME_XYZ16 ?= 0",
            "PSP_RID30_AB_ME_C1_GE_EXPERIMENT ?= 0",
            "PSP_RID30_AB_ME_BUILD_ID ?= 0x260901a1u",
            "PSP_RID30_AB_ME_TITLE ?= TH07 RID30 AB ME",
        ):
            self.assertIn(default, MAKEFILE)

    def test_m0_is_explicitly_not_a_ge_readback_pass(self) -> None:
        self.assertIn("static const uint32_t counts[4] = {0u, 128u, 512u, 1024u}", WORKER)
        self.assertIn(
            '"MERW C1M0 PASS CASES4 GE0 READBACK-PENDING"', WORKER
        )

    def test_fcr31_write_drains_me_fpu_before_exact_restore(self) -> None:
        helper = function_body(
            WORKER,
            "static __attribute__((always_inline)) inline void "
            "me_render_write_fcr31(",
        )
        drain = helper.index('"cfc1 %0, $31')
        write = helper.index('"ctc1 %1, $31')
        self.assertLess(drain, write)
        self.assertIn(': "=&r"(drained)', helper)
        self.assertIn(': "r"(value)', helper)
        self.assertIn('"memory"', helper)
        reader = function_body(
            WORKER,
            "static __attribute__((always_inline)) inline uint32_t "
            "me_render_read_fcr31(",
        )
        self.assertIn(': "=r"(value) : : "memory"', reader)

        # C5 returned Allegrex FIR/FCR0 identity 0x3351 after a busy kernel.
        # The fix must preserve the strict effective/restore comparisons,
        # never whitelist that value or mask control/status bits.
        self.assertNotIn("0x3351", helper)
        stream = function_body(
            WORKER, "static void process_render_stream_on_me("
        )
        self.assertIn("renderStreamFcr31Effective == 0u", stream)
        self.assertIn("renderStreamFcr31After != originalFcr31", stream)
        self.assertIn(
            "result = TH07_PSP_ME_RENDER_STREAM_RESULT_PROTOCOL;", stream
        )

    def test_fcr31_restore_readback_is_late_and_sidecars_are_two_phase(self) -> None:
        stream = function_body(
            WORKER, "static void process_render_stream_on_me("
        )
        restore = stream.index("me_render_write_fcr31(originalFcr31);")
        vertex_writeback = stream.index(
            "meLibDcacheWritebackRange((uint32_t)vertices, outputBytes);",
            restore,
        )
        run_writeback = stream.index(
            "meLibDcacheWritebackRange((uint32_t)runs, runBytes);",
            vertex_writeback,
        )
        settle_sync = stream.index(
            '__asm__ volatile("sync" : : : "memory");', run_writeback
        )
        late_read = stream.index(
            "box->renderStreamFcr31After = me_render_read_fcr31();",
            settle_sync,
        )
        item_commit = stream.index(
            "itemMotionSeed->header.committed =\n"
            "                TH07_PSP_ME_ITEM_MOTION_COMMITTED;",
            late_read,
        )
        bullet_commit = stream.index(
            "compactSeed->header.committed =\n"
            "                TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED;",
            item_commit,
        )
        completion_publish = stream.index(
            "box->renderStreamOutputBytes = outputBytes;", bullet_commit
        )
        self.assertLess(restore, vertex_writeback)
        self.assertLess(vertex_writeback, run_writeback)
        self.assertLess(run_writeback, settle_sync)
        self.assertLess(settle_sync, late_read)
        self.assertLess(late_read, item_commit)
        self.assertLess(item_commit, bullet_commit)
        self.assertLess(bullet_commit, completion_publish)

        protocol = stream.index(
            "result = TH07_PSP_ME_RENDER_STREAM_RESULT_PROTOCOL;",
            late_read,
        )
        self.assertIn("outputBytes = 0u;", stream[late_read:protocol])
        self.assertIn("runBytes = 0u;", stream[late_read:protocol])
        self.assertIn("outputHash = 0u;", stream[late_read:protocol])
        self.assertIn("runHash = 0u;", stream[late_read:protocol])
        self.assertIn("vertexCount = 0u;", stream[late_read:protocol])
        self.assertIn("runCount = 0u;", stream[late_read:protocol])
        # RID30 keeps its proven eager sidecar publication in the feature-off
        # arm.  The packed C1 arm must keep both hashes local until after the
        # late FCR31 readback.  Inspect the two preprocessor arms separately
        # so this test does not require C1's rejected common-path changes to
        # leak into the accepted RID30 binary.
        hash_gate = stream.index(
            "if ((box->renderStreamFlags &\n"
            "             TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT) != 0u)",
            restore,
        )
        packed_hash_if = stream.index(
            "#if defined(TH07_PSP_ME_RENDER_UV16)", hash_gate
        )
        legacy_hash_else = stream.index("#else", packed_hash_if)
        hash_gate_end = stream.index("#endif", legacy_hash_else)
        self.assertIn(
            "outputHash = me_render_stream_hash_bytes(vertices, outputBytes);",
            stream[packed_hash_if:legacy_hash_else],
        )
        self.assertNotIn(
            "box->renderStreamOutputHash =\n",
            stream[packed_hash_if:legacy_hash_else],
        )
        self.assertIn(
            "box->renderStreamOutputHash =\n",
            stream[legacy_hash_else:hash_gate_end],
        )

    def test_pre_fcr_bounds_ordering_is_integer_only(self) -> None:
        bounds = function_body(WORKER, "static int me_render_stream_bounds_valid(")
        integer_compare = bounds.index("me_render_stream_finite_bits_le(")
        packed_if = bounds.rfind("#if defined(TH07_PSP_ME_RENDER_UV16)", 0,
                                 integer_compare)
        legacy_else = bounds.index("#else", integer_compare)
        compare_end = bounds.index("#endif", legacy_else)
        packed_arm = bounds[packed_if:legacy_else]
        legacy_arm = bounds[legacy_else:compare_end]
        self.assertIn("me_render_stream_finite_bits_le(", packed_arm)
        self.assertNotIn("me_render_bits_float(", packed_arm)
        self.assertNotIn("const float", packed_arm)
        self.assertIn("me_render_bits_float(", legacy_arm)

        ordered = function_body(
            WORKER,
            "static __attribute__((always_inline)) inline int\n"
            "me_render_stream_finite_bits_le(",
        )
        self.assertIn("me_render_stream_float_order_key(lhsBits)", ordered)
        self.assertIn("me_render_stream_float_order_key(rhsBits)", ordered)
        self.assertIn("(lhsBits | rhsBits) & 0x7fffffffu", ordered)

    def test_m0_matches_the_nohash_production_contract_with_exact_oracle(self) -> None:
        m0 = function_body(WORKER, "static int selftest_render_stream_c1_m0(")
        self.assertIn("job.flags = 0u;", m0)
        self.assertIn("job.payloadHash = 0u;", m0)
        self.assertIn("me_render_stream_c1_reference_vertex(", m0)
        self.assertIn("th07_psp_me_render_stream_compare(", m0)
        self.assertIn("gMeRenderStreamOutputAreas[build.token.slot].vertices", m0)
        self.assertIn("gMeRenderStreamRunAreas[build.token.slot].runs", m0)
        self.assertIn("H0 MI%lu", m0)
        self.assertNotIn(
            "job.flags = TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH",
            m0,
        )

    def test_m0_releases_each_ready_case_before_logging_or_next_acquire(
        self,
    ) -> None:
        m0 = function_body(WORKER, "static int selftest_render_stream_c1_m0(")
        loop_start = m0.index("for (uint32_t caseIndex = 0u;")
        loop_end = m0.index("// Exercise the real publish/worker/retire", loop_start)
        loop = m0[loop_start:loop_end]

        owned = loop.index("const int readyOwned =")
        guard = loop.index("const int guardsValid =")
        release = loop.index("th07_psp_me_render_stream_release_ready(")
        boot_note = loop.index('"MERW C1M0 F%lu N%lu VB%lu')
        failure = loop.index(
            "if (!compared || !readyOwned || !guardsValid || !released)"
        )
        self.assertLess(owned, guard)
        self.assertLess(guard, release)
        self.assertLess(release, boot_note)
        self.assertLess(boot_note, failure)
        self.assertIn("OK%d Q%d G%d R%d", loop)

        # Regression for the real failure sequence: the zero-record case must
        # release READY_SC before the loop can acquire the 128-record case.
        self.assertIn("{0u, 128u, 512u, 1024u}", m0)
        self.assertEqual(loop.count("th07_psp_me_render_stream_acquire(&build)"), 1)
        self.assertEqual(
            loop.count("th07_psp_me_render_stream_release_ready(&build.token)"),
            1,
        )

        # No guard read is legal after FREE has been published.  Saved scalar
        # results may be logged or tested, but the pool itself is no longer
        # owned by this iteration.
        after_release = loop[release + len("th07_psp_me_render_stream_release_ready(") :]
        self.assertNotIn("me_render_stream_guards_match_on_sc", after_release)

    def test_exact_vertex_or_run_mismatch_can_only_cleanly_disable_c1(
        self,
    ) -> None:
        m0 = function_body(WORKER, "static int selftest_render_stream_c1_m0(")
        loop_start = m0.index("for (uint32_t caseIndex = 0u;")
        loop_end = m0.index("// Exercise the real publish/worker/retire", loop_start)
        loop = m0[loop_start:loop_end]

        self.assertIn("const int shapeValid =", loop)
        self.assertIn("const int compareResult = shapeValid", loop)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_MISMATCH_VERTEX", loop)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_MISMATCH_RUN", loop)
        self.assertNotIn("TH07_PSP_ME_RENDER_STREAM_MISMATCH_SIZE", loop)
        self.assertNotIn("TH07_PSP_ME_RENDER_STREAM_MISMATCH_HASH", loop)
        self.assertIn("me_render_stream_c1_ready_fallback_safe(", loop)
        self.assertIn("(compared || safeExactMismatch) && guardsValid", loop)
        self.assertIn("if (safeExactMismatch && released)", loop)
        self.assertIn("gMeRenderC1M0SafeFailure = 1;", loop)

    def test_exact_mismatch_fallback_reproves_idle_ownership_and_bounds(
        self,
    ) -> None:
        safe = function_body(
            WORKER, "static int me_render_stream_c1_ready_fallback_safe("
        )
        for proof in (
            "TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC",
            "gMeMailboxUncached->command != ME_CMD_NONE",
            "gMeMailboxUncached->status != ME_STAT_DONE",
            "gMeMailboxUncached->workerState != ME_WORKER_READY",
            "gMeMailboxUncached->suspendRequested != 0u",
            "gMeRenderStreamInFlightSlot",
            "gMeOwner, __ATOMIC_ACQUIRE) != ME_OWNER_NONE",
            "gMePoisoned",
            "gMeUnsafe",
            "gMeActive",
            "stack_guards_match_on_sc()",
            "me_render_stream_completion_echo_matches(completion, job)",
            "completion->meFcr31Effective != 0u",
            "completion->meFcr31Before != completion->meFcr31After",
            "ME_RENDER_STREAM_POOL_MAX_VERTEX_BYTES",
            "ME_RENDER_STREAM_POOL_MAX_RUNS",
        ):
            self.assertIn(proof, safe)

        # Shape/identity/FCR/ownership faults never set the clean latch.  The
        # caller reaches this proof only for an exact vertex/run word mismatch.
        m0 = function_body(WORKER, "static int selftest_render_stream_c1_m0(")
        latch = m0.index("if (safeExactMismatch && released)")
        self.assertLess(m0.index("const int exactMismatch ="), latch)
        self.assertLess(m0.index("const int shapeValid ="), latch)

    def test_m0_retire_failure_is_diagnosed_only_after_quarantine_publish(
        self,
    ) -> None:
        m0 = function_body(WORKER, "static int selftest_render_stream_c1_m0(")
        retire = m0.index("const int retireResult =")
        diagnose = m0.index("me_render_stream_c1_diagnose_retire(", retire)
        quarantine = m0.index("TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED", diagnose)
        report = m0.index('"MERW C1M0 RETIRE-NG', quarantine)
        self.assertLess(retire, diagnose)
        self.assertLess(diagnose, quarantine)
        self.assertLess(quarantine, report)
        self.assertIn("M%08lx D%08lx", m0[report:])
        self.assertIn("E%08lx A%08lx", m0[report:])
        self.assertIn("Z%08lx FB%08lx FA%08lx SAFE%d Q%d", m0[report:])
        self.assertIn("completion.meFcr31Before", m0[report:])
        self.assertIn("completion.meFcr31After", m0[report:])
        self.assertNotIn("retireFaultMask", m0)

    def test_c1_retire_diagnostic_rechecks_safety_and_hashes_in_quarantine(
        self,
    ) -> None:
        diag = function_body(
            WORKER, "static int me_render_stream_c1_diagnose_retire("
        )
        ownership = diag.index("TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED")
        guard = diag.index("me_render_stream_c1_guard_fault_mask(")
        hash_gate = diag.index("outputExtentSafe && runExtentSafe")
        output_hash = diag.index("ME_RENDER_C1_DIAG_OUTPUT_HASH", hash_gate)
        run_hash = diag.index("ME_RENDER_C1_DIAG_RUN_HASH", hash_gate)
        self.assertLess(ownership, guard)
        self.assertLess(guard, hash_gate)
        self.assertLess(hash_gate, output_hash)
        self.assertLess(output_hash, run_hash)
        for fault in (
            "ME_RENDER_C1_DIAG_TOKEN",
            "ME_RENDER_C1_DIAG_FCR_EFFECTIVE",
            "ME_RENDER_C1_DIAG_STACK",
            "ME_RENDER_C1_DIAG_OUTPUT_BOUNDS",
            "ME_RENDER_C1_DIAG_RUN_BOUNDS",
            "ME_RENDER_C1_DIAG_GUARD_INPUT",
            "ME_RENDER_C1_DIAG_GUARD_OUTPUT",
            "ME_RENDER_C1_DIAG_GUARD_RUN",
            "ME_RENDER_C1_DIAG_ECHO_OTHER",
            "ME_RENDER_C1_DIAG_OWNERSHIP",
        ):
            self.assertIn(fault, diag)

        # A quarantined correctness-only mismatch is eligible for the clean
        # SC fallback; ownership/cache/bounds corruption is explicitly fatal.
        fatal_start = diag.index("const uint32_t fatalBits =")
        fatal = diag[fatal_start:]
        self.assertNotIn("ME_RENDER_C1_DIAG_OUTPUT_HASH", fatal)
        self.assertNotIn("ME_RENDER_C1_DIAG_RUN_HASH", fatal)
        self.assertNotIn("ME_RENDER_C1_DIAG_FCR_RESTORE", fatal)
        self.assertIn("ME_RENDER_C1_DIAG_FCR_EFFECTIVE", fatal)
        self.assertIn("diag->fatalMask = diag->mask & fatalBits", fatal)

    def test_c1_clean_failure_requires_idle_worker_and_owner_before_fallback(
        self,
    ) -> None:
        diag = function_body(
            WORKER, "static int me_render_stream_c1_diagnose_retire("
        )
        for proof in (
            "gMeMailboxUncached->status != ME_STAT_DONE",
            "gMeMailboxUncached->workerState != ME_WORKER_READY",
            "gMeMailboxUncached->suspendRequested != 0u",
            "gMeRenderStreamInFlightSlot",
            "gMeOwner, __ATOMIC_ACQUIRE) != ME_OWNER_NONE",
            "gMePoisoned",
            "gMeUnsafe",
            "gMeActive",
            "stack_guards_match_on_sc()",
        ):
            self.assertIn(proof, diag)
        self.assertIn("ME_RENDER_C1_DIAG_OWNERSHIP", diag)

    def test_c1_clean_failure_stops_me_and_returns_existing_sc_init_result(
        self,
    ) -> None:
        m0 = function_body(WORKER, "static int selftest_render_stream_c1_m0(")
        self.assertIn("gMeRenderC1M0SafeFailure = 0;", m0)
        safe = m0.index("if (safe)")
        latch = m0.index("gMeRenderC1M0SafeFailure = 1;", safe)
        report = m0.index('"MERW C1M0 RETIRE-NG', 0)
        self.assertLess(report, safe)
        self.assertLess(safe, latch)

        init = function_body(WORKER, "int th07_psp_me_audio_init(void)")
        branch = init.index("if (gMeRenderC1M0SafeFailure)")
        generic_fatal = init.index(
            "__atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);", branch
        )
        fallback = init[branch:generic_fatal]
        for action in (
            "__atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE)",
            "gMeItemRenderEnabled",
            "gMeItemMotionEnabled",
            "initialize_render_stream_slots();",
            "th07_psp_me_audio_shutdown();",
            "!__atomic_load_n(&gMeStarted",
            "!__atomic_load_n(&gMeUnsafe",
            "!__atomic_load_n(&gMePowerLocked",
            '"MERW C1 OFF; CANONICAL SC CONTINUE"',
            "return 0;",
        ):
            self.assertIn(action, fallback)

        main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
        main_init = main[main.index("meRenderInit = th07_psp_me_audio_init();") :]
        zero = main_init.index("if (meRenderInit == 0)")
        enable = main_init.index("Th07PspMeRenderSetAvailable(true);", zero)
        self.assertIn(
            "Th07PspMeRenderSetAvailable(false);", main_init[zero:enable]
        )


@unittest.skipUnless(IMPLEMENTED, "C1 implementation has not landed")
class PspMeVertex16BitAbiTests(unittest.TestCase):
    HARNESS = r"""
        #include <stddef.h>
        #include <stdio.h>
        #include "psp/audio_me.h"

        int main(void)
        {
        #if defined(TH07_PSP_ME_RENDER_UV16)
            _Static_assert(
                _Generic(((Th07PspMeRenderStreamVertex *)0)->u,
                         unsigned short: 1, default: 0),
                "GU_TEXTURE_16BIT payload must be unsigned u16");
            _Static_assert(
                _Generic(((Th07PspMeRenderStreamVertex *)0)->v,
                         unsigned short: 1, default: 0),
                "GU_TEXTURE_16BIT payload must be unsigned u16");
            const size_t u = offsetof(Th07PspMeRenderStreamVertex, u);
            const size_t v = offsetof(Th07PspMeRenderStreamVertex, v);
        #else
            const size_t u = offsetof(Th07PspMeRenderStreamVertex, uBits);
            const size_t v = offsetof(Th07PspMeRenderStreamVertex, vBits);
        #endif
        #if defined(TH07_PSP_ME_RENDER_XYZ16)
            _Static_assert(
                _Generic(((Th07PspMeRenderStreamVertex *)0)->x,
                         short: 1, default: 0),
                "GU_VERTEX_16BIT payload must be signed s16");
            _Static_assert(
                _Generic(((Th07PspMeRenderStreamVertex *)0)->z,
                         short: 1, default: 0),
                "GU_VERTEX_16BIT payload must be signed s16");
            const size_t x = offsetof(Th07PspMeRenderStreamVertex, x);
            const size_t y = offsetof(Th07PspMeRenderStreamVertex, y);
            const size_t z = offsetof(Th07PspMeRenderStreamVertex, z);
            const size_t tail = offsetof(Th07PspMeRenderStreamVertex, reserved);
        #else
            const size_t x = offsetof(Th07PspMeRenderStreamVertex, xBits);
            const size_t y = offsetof(Th07PspMeRenderStreamVertex, yBits);
            const size_t z = offsetof(Th07PspMeRenderStreamVertex, zBits);
            const size_t tail = sizeof(Th07PspMeRenderStreamVertex);
        #endif
            printf("%zu %d %zu %zu %zu %zu %zu %zu %zu\n",
                   sizeof(Th07PspMeRenderStreamVertex),
                   TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES,
                   u, v, offsetof(Th07PspMeRenderStreamVertex, color),
                   x, y, z, tail);
            return 0;
        }
    """

    def test_host_compiler_observes_exact_four_layouts(self) -> None:
        cases = {
            "off": ([], (24, 24, 0, 4, 8, 12, 16, 20, 24)),
            "uv16": (
                ["-DTH07_PSP_ME_RENDER_UV16"],
                (20, 20, 0, 2, 4, 8, 12, 16, 20),
            ),
            "xyz16": (
                ["-DTH07_PSP_ME_RENDER_XYZ16"],
                (20, 20, 0, 4, 8, 12, 14, 16, 18),
            ),
            "both": (
                ["-DTH07_PSP_ME_RENDER_UV16", "-DTH07_PSP_ME_RENDER_XYZ16"],
                (16, 16, 0, 2, 4, 8, 10, 12, 14),
            ),
        }
        with tempfile.TemporaryDirectory(prefix="th07-c1-abi-") as tmp:
            source = Path(tmp) / "abi.c"
            source.write_text(textwrap.dedent(self.HARNESS), encoding="utf-8")
            for name, (defines, expected) in cases.items():
                binary = Path(tmp) / name
                command = [
                    "gcc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DTH07_PSP_ME_RENDER_WORKER",
                    "-DTH07_PSP_ME_RENDER_CORRECTNESS",
                    *defines,
                    "-I",
                    str(ROOT),
                    str(source),
                    "-o",
                    str(binary),
                ]
                with self.subTest(profile=name):
                    subprocess.run(command, check=True, capture_output=True, text=True)
                    output = subprocess.run(
                        [str(binary)], check=True, capture_output=True, text=True
                    ).stdout
                    self.assertEqual(tuple(map(int, output.split())), expected)

    def test_color_stays_rgba8888_and_max_saving_is_one_third(self) -> None:
        self.assertRegex(
            HEADER,
            r"typedef struct Th07PspMeRenderStreamVertex\s*\{(?s:.*?)"
            r"unsigned int color;",
        )
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES = 16", HEADER)
        self.assertNotIn("TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES = 12", HEADER)

    def test_sc_and_me_translation_units_lock_four_byte_alignment(self) -> None:
        self.assertIn(
            "_Alignof(Th07PspMeRenderStreamVertex) == 4u", WORKER
        )
        self.assertIn(
            "alignof(Th07PspMeRenderStreamVertex) == 4u", BULLETS
        )


@unittest.skipUnless(IMPLEMENTED, "C1 implementation has not landed")
class PspMeVertex16BitPackingTests(unittest.TestCase):
    def test_me_and_independent_sc_packers_match_signed_and_unsigned_oracles(
        self,
    ) -> None:
        me_s16_body = function_body(WORKER, "me_render_stream_pack_s16(")
        me_u16_body = function_body(WORKER, "me_render_stream_pack_u16(")
        sc_s16_body = function_body(
            BULLETS, "bool PspMeRenderPackS16Reference("
        )
        sc_u16_body = function_body(
            BULLETS, "bool PspMeRenderPackU16Reference("
        )
        harness = f"""
            #include <cmath>
            #include <cstdint>
            #include <cstdio>
            #include <cstdlib>
            #include <cstring>
            using i32 = std::int32_t;

            static int MePackS16(float value, float scale, std::int16_t *packed)
            {me_s16_body}

            static int MePackU16(std::uint32_t bits, std::uint16_t *packed)
            {me_u16_body}

            static bool ScPackS16(float value, float scale, short *packed)
            {sc_s16_body}

            static bool ScPackU16(float value, float scale,
                                  unsigned short *packed)
            {sc_u16_body}

            struct Case {{ float value; float scale; int ok; int valueOut; }};

            static std::uint32_t FloatBits(float value)
            {{
                std::uint32_t bits;
                std::memcpy(&bits, &value, sizeof(bits));
                return bits;
            }}

            static float BitsFloat(std::uint32_t bits)
            {{
                float value;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            }}

            int main()
            {{
                const float pInf = INFINITY;
                const float nInf = -INFINITY;
                const float nan = NAN;
                const Case signedCases[] = {{
                    {{0.0f, 32768.0f, 1, 0}},
                    {{-0.0f, 32768.0f, 1, 0}},
                    {{1.0f / 32768.0f, 32768.0f, 1, 1}},
                    {{-1.0f / 32768.0f, 32768.0f, 1, -1}},
                    {{0.5f / 32768.0f, 32768.0f, 1, 1}},
                    {{-0.5f / 32768.0f, 32768.0f, 1, -1}},
                    {{32767.0f / 32768.0f, 32768.0f, 1, 32767}},
                    {{1.0f, 32768.0f, 0, 0}},
                    {{-1.0f, 32768.0f, 1, -32768}},
                    {{1023.96875f, 32.0f, 1, 32767}},
                    {{1024.0f, 32.0f, 0, 0}},
                    {{-1024.0f, 32.0f, 1, -32768}},
                    {{pInf, 32.0f, 0, 0}},
                    {{nInf, 32.0f, 0, 0}},
                    {{nan, 32.0f, 0, 0}},
                }};
                for (unsigned int i = 0;
                     i < sizeof(signedCases) / sizeof(signedCases[0]); ++i)
                {{
                    std::int16_t me = 1234;
                    short sc = 1234;
                    const int meOk = MePackS16(
                        signedCases[i].value, signedCases[i].scale, &me);
                    const int scOk = ScPackS16(
                        signedCases[i].value, signedCases[i].scale, &sc) ? 1 : 0;
                    if (meOk != signedCases[i].ok ||
                        scOk != signedCases[i].ok || meOk != scOk)
                    {{
                        std::fprintf(stderr,
                                     "signed case %u status %d/%d expected %d\\n",
                                     i, meOk, scOk, signedCases[i].ok);
                        return 10 + static_cast<int>(i);
                    }}
                    if (meOk &&
                        (me != signedCases[i].valueOut ||
                         sc != signedCases[i].valueOut || me != sc))
                    {{
                        std::fprintf(stderr,
                                     "signed case %u value %d/%d expected %d\\n",
                                     i, static_cast<int>(me), static_cast<int>(sc),
                                     signedCases[i].valueOut);
                        return 40 + static_cast<int>(i);
                    }}
                }}

                const Case unsignedCases[] = {{
                    {{0.0f, 32768.0f, 1, 0}},
                    {{-0.0f, 32768.0f, 1, 0}},
                    {{1.0f / 32768.0f, 32768.0f, 1, 1}},
                    {{0.5f / 32768.0f, 32768.0f, 1, 1}},
                    {{1.0f, 32768.0f, 1, 32768}},
                    {{65535.0f / 32768.0f, 32768.0f, 1, 65535}},
                    {{2.0f, 32768.0f, 0, 0}},
                    {{-1.0f / 32768.0f, 32768.0f, 0, 0}},
                    {{pInf, 32768.0f, 0, 0}},
                    {{nInf, 32768.0f, 0, 0}},
                    {{nan, 32768.0f, 0, 0}},
                }};
                for (unsigned int i = 0;
                     i < sizeof(unsignedCases) / sizeof(unsignedCases[0]); ++i)
                {{
                    std::uint16_t me = 1234;
                    unsigned short sc = 1234;
                    const int meOk = MePackU16(
                        FloatBits(unsignedCases[i].value), &me);
                    const int scOk = ScPackU16(
                        unsignedCases[i].value, unsignedCases[i].scale, &sc)
                        ? 1 : 0;
                    if (meOk != unsignedCases[i].ok ||
                        scOk != unsignedCases[i].ok || meOk != scOk)
                    {{
                        std::fprintf(stderr,
                                     "unsigned case %u status %d/%d expected %d\\n",
                                     i, meOk, scOk, unsignedCases[i].ok);
                        return 70 + static_cast<int>(i);
                    }}
                    if (meOk &&
                        (me != unsignedCases[i].valueOut ||
                         sc != unsignedCases[i].valueOut || me != sc))
                    {{
                        std::fprintf(stderr,
                                     "unsigned case %u value %u/%u expected %d\\n",
                                     i, static_cast<unsigned int>(me),
                                     static_cast<unsigned int>(sc),
                                     unsignedCases[i].valueOut);
                        return 100 + static_cast<int>(i);
                    }}
                }}

                // This predecessor of 0.5 Q15 is the one binary32
                // double-rounding exception: legacy (scaled + 0.5f) rounds
                // to exactly 1.0f under RN-even before the integer cast.
                std::uint16_t meBoundary = 0;
                unsigned short scBoundary = 0;
                const std::uint32_t boundaryBits = 0x377fffffu;
                const float boundaryValue = BitsFloat(boundaryBits);
                if (!MePackU16(boundaryBits, &meBoundary) ||
                    !ScPackU16(boundaryValue, 32768.0f, &scBoundary) ||
                    meBoundary != 1u || scBoundary != 1u)
                    return 130;

                // A deterministic raw-bit corpus checks exact compatibility
                // with the independent legacy SC oracle across normals,
                // subnormals, signed values, NaNs/infinities and both bounds.
                std::uint32_t state = 0x6d2b79f5u;
                for (unsigned int i = 0; i < 200000u; ++i)
                {{
                    state = state * 1664525u + 1013904223u;
                    const float value = BitsFloat(state);
                    std::uint16_t me = 0x55aau;
                    unsigned short sc = 0xaa55u;
                    const int meOk = MePackU16(state, &me);
                    const int scOk = ScPackU16(value, 32768.0f, &sc) ? 1 : 0;
                    if (meOk != scOk || (meOk && me != sc))
                    {{
                        std::fprintf(stderr,
                                     "random bits %08x status %d/%d value %u/%u\\n",
                                     state, meOk, scOk,
                                     static_cast<unsigned int>(me),
                                     static_cast<unsigned int>(sc));
                        return 131;
                    }}
                }}
                return 0;
            }}
        """
        with tempfile.TemporaryDirectory(prefix="th07-c1-pack-") as tmp:
            source = Path(tmp) / "pack.cpp"
            binary = Path(tmp) / "pack"
            source.write_text(textwrap.dedent(harness), encoding="utf-8")
            subprocess.run(
                [
                    "g++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_each_numeric_domain_rejects_instead_of_saturating(self) -> None:
        for source, signature in (
            (WORKER, "me_render_stream_pack_s16("),
            (BULLETS, "bool PspMeRenderPackS16Reference("),
        ):
            body = function_body(source, signature)
            self.assertIn("scaled >= -32768.0f && scaled < 32767.5f", body)

        me_u16 = function_body(WORKER, "me_render_stream_pack_u16(")
        self.assertIn("exponent == 0xffu", me_u16)
        self.assertIn("bits & 0x80000000u", me_u16)
        self.assertIn("roundedQ > 65535u", me_u16)
        self.assertIn("bits == 0x377fffffu", me_u16)
        sc_u16 = function_body(
            BULLETS, "bool PspMeRenderPackU16Reference("
        )
        self.assertIn("scaled >= 0.0f && scaled < 65535.5f", sc_u16)

    def test_me_uv_packer_and_m0_uv_oracle_do_not_execute_cop1(self) -> None:
        me_u16 = function_body(WORKER, "me_render_stream_pack_u16(")
        self.assertNotRegex(me_u16, r"\bfloat\b")
        self.assertNotIn("32768.0f", me_u16)

        writer = function_body(WORKER, "me_render_stream_write_vertex(")
        self.assertIn("me_render_stream_pack_u16(uBits, &packedU)", writer)
        self.assertIn("me_render_stream_pack_u16(vBits, &packedV)", writer)
        self.assertNotIn(
            "me_render_bits_float(uBits), 32768.0f", writer
        )

        oracle = function_body(
            WORKER, "static void me_render_stream_c1_reference_vertex("
        )
        self.assertIn("vertex->u = uQ;", oracle)
        self.assertIn("vertex->v = vQ;", oracle)
        self.assertNotIn("u * 32768.0f", oracle)
        self.assertNotIn("v * 32768.0f", oracle)

        m0 = function_body(WORKER, "static int selftest_render_stream_c1_m0(")
        self.assertIn("(uint16_t)u0Q, (uint16_t)v0Q", m0)
        self.assertIn("(uint16_t)u1Q, (uint16_t)v1Q", m0)

    def test_sc_reference_is_independent_and_me_reject_is_not_published(self) -> None:
        reference = function_body(
            BULLETS, "bool PspMeRenderPackReferenceVertex("
        )
        self.assertNotIn("me_render_stream_pack_s16", reference)
        self.assertNotIn("me_render_stream_pack_u16", reference)
        self.assertIn("PspMeRenderPackS16Reference", reference)
        self.assertIn("PspMeRenderPackU16Reference", reference)

        kernel = function_body(WORKER, "me_render_stream_expand_kernel(")
        pack = kernel.index("int packed = me_render_stream_write_vertex(")
        reject = kernel.index("return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;", pack)
        publish = kernel.index("vertexCount += verticesThisRecord;", pack)
        self.assertLess(reject, publish)

    def test_m0_reject_case_uses_real_command10_unsigned_and_signed_limits(
        self,
    ) -> None:
        m0 = function_body(WORKER, "static int selftest_render_stream_c1_m0(")
        self.assertIn('float_bits(2.0f)', m0)
        self.assertIn('float_bits(1027.5f)', m0)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD", m0)
        self.assertIn("rejectCompletion.outputBytes != 0u", m0)
        self.assertIn("rejectCompletion.vertexCount != 0u", m0)
        self.assertIn("rejectCompletion.runCount != 0u", m0)
        self.assertIn('"MERW C1M0 RANGE-REJECT PASS"', m0)


@unittest.skipUnless(IMPLEMENTED, "C1 implementation has not landed")
class PspMeVertex16BitGeStateTests(unittest.TestCase):
    def test_c1_api_uses_the_selected_stream_type_not_the_24_byte_alias(self) -> None:
        conditional = GRAPHICS_H[
            GRAPHICS_H.index("#if defined(TH07_PSP_ME_RENDER_UV16)") :
            GRAPHICS_H.index("void Th07PspEndMeRenderGeSubmission")
        ]
        self.assertIn("const Th07PspMeRenderStreamVertex *vertices", conditional)
        self.assertIn("const Th07PspSpriteVertex *vertices", conditional)

    def test_ge_declaration_keeps_color_and_transform_3d(self) -> None:
        body = function_body(GRAPHICS, "void DrawMeRenderStreamRun(")
        self.assertIn("GU_TEXTURE_16BIT", body)
        self.assertIn("GU_TEXTURE_32BITF", body)
        self.assertIn("GU_VERTEX_16BIT", body)
        self.assertIn("GU_VERTEX_32BITF", body)
        self.assertGreaterEqual(body.count("GU_COLOR_8888"), 2)
        self.assertGreaterEqual(body.count("GU_TRANSFORM_3D"), 2)
        self.assertNotIn("GU_TRANSFORM_2D", body)

    def test_xyz_nonuniform_scale_is_reapplied_after_list_space_check(self) -> None:
        matrix_start = GRAPHICS.index(
            "const ScePspFMatrix4 kMeRenderXyz16ModelMatrix"
        )
        matrix_end = GRAPHICS.index("};", matrix_start)
        matrix = GRAPHICS[matrix_start : matrix_end + 2]
        self.assertEqual(matrix.count("1024.0f"), 2)
        self.assertIn("{0.0f, 0.0f, 1.0f, 0.0f}", matrix)

        body = function_body(GRAPHICS, "void DrawMeRenderStreamRun(")
        ensure = body.index("EnsureListSpace(0)")
        apply = body.index("ApplyMatrices(true)")
        scale = body.index("sceGuSetMatrix(GU_MODEL, &kMeRenderXyz16ModelMatrix)")
        first_draw = body.index("sceGuDrawArray")
        restore = body.index("sceGuSetMatrix(GU_MODEL, &kIdentityMatrix)")
        last_draw = body.rindex("sceGuDrawArray")
        self.assertLess(ensure, apply)
        self.assertLess(apply, scale)
        self.assertLess(scale, first_draw)
        self.assertLess(last_draw, restore)

    def test_consumer_steps_by_selected_c_type(self) -> None:
        body = function_body(GRAPHICS, "void DrawMeRenderStreamRun(")
        self.assertIn("const auto *batch = vertices;", body)
        self.assertIn("batch += sprites * 4u;", body)
        self.assertNotIn("reinterpret_cast<const Th07PspSpriteVertex", body)


if __name__ == "__main__":
    unittest.main()

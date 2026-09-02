import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise AssertionError(f"unterminated function: {signature}")


def make_target_body(source: str, target: str) -> str:
    start = source.index(f"{target}:")
    return source[start:].split("\n\n", 1)[0]


def psp_target_assignments(body: str) -> dict[str, str]:
    pairs = re.findall(
        r"\b(PSP_[A-Z0-9_]+)=('[^']*'|[^\s\\]+)", body
    )
    if len(pairs) != len(dict(pairs)):
        raise AssertionError("duplicate PSP_* assignment in Make target")
    return dict(pairs)


class AdaptiveItemMeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src/BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.items = (ROOT / "src/ItemManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.audio = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")
        cls.meter_h = (ROOT / "psp/usage_meter.h").read_text(
            encoding="utf-8"
        )

    def test_rid23_is_item_only_adaptive_increment(self):
        target = self.makefile[
            self.makefile.index("psp3000-ime7-adaptive-item-build:") :
        ]
        target = target.split("\n\n", 1)[0]
        for setting in (
            "PSP_ME_ITEM_RENDER_STREAM=1",
            "PSP_ME_EFFECT_RENDER_STREAM=0",
            "PSP_ME_ADAPTIVE_AUX_RENDER=1",
            "PSP_ME_BULLET_COMPACT_UPDATE=1",
            "PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0",
            "PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0",
            "PSP_USAGE_METER=1",
            "PSP_AUDIO4M_BUILD_ID=0x26083123u",
        ):
            self.assertIn(setting, target)

    def test_current_record_model_is_positive_gate_and_meter_is_veto_only(self):
        gate = function_body(
            self.bullets, "PspMeAdaptiveAuxAdmission "
            "PspMeAdaptiveAuxAdmissionFor("
        )
        self.assertIn("bulletRecords", gate)
        self.assertIn("itemRecords", gate)
        self.assertIn("predicted > budgetTicks", gate)
        self.assertIn("PspMeAdaptiveBudgetTicksForRuntime()", gate)
        self.assertIn("PspMeAdaptiveVetoPercentForRuntime()", gate)
        self.assertIn("th07_usage_meter_last_me_percent() < vetoPercent", gate)
        self.assertLess(
            gate.index("predicted > budgetTicks"),
            gate.index("th07_usage_meter_last_me_percent()"),
        )
        self.assertIn("kPspMeAdaptiveBudgetTicks", self.bullets)
        self.assertIn("* 80u) / 100u", self.bullets)
        self.assertIn("th07_usage_meter_last_me_percent", self.meter_h)

    def test_budget_and_busy_rejections_are_distinct(self):
        publish = function_body(
            self.bullets, "void PspMeRenderPublishFusedCapture("
        )
        for token in (
            "streamItemCandidates",
            "streamItemBudgetReject",
            "streamItemBusyVeto",
            "streamItemCandidateRecords",
            "streamItemCandidateMax",
            "streamItemBudgetRejectMax",
            "streamItemPredictedTicksMax",
            "PSP_ME_ADAPTIVE_AUX_REJECT_BUDGET",
        ):
            self.assertIn(token, publish)
        graphics = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            '"CAND%u BR%u BV%u CREC%llu CMAX%u BRMAX%u PTMAX%llu "',
            graphics,
        )

    def test_rid24_enables_il02_prefix_split(self):
        for token in (
            "psp3000-ime7-adaptive-item-prefix-build:",
            "PSP_ME_ITEM_PREFIX_SPLIT=1",
            "PSP_AUDIO4M_BUILD_ID=0x26083124u",
            "authenticated suffix",
        ):
            self.assertIn(token, self.makefile)

    def test_rid26_keeps_rid24_gameplay_and_adds_only_startup_diagnostics(self):
        target = self.makefile[
            self.makefile.index("psp3000-ime7-item-selftest-diag-build:") :
        ]
        target = target.split("\n\n", 1)[0]
        for setting in (
            "PSP_ME_ITEM_RENDER_STREAM=1",
            "PSP_ME_ITEM_PREFIX_SPLIT=1",
            "PSP_ME_ADAPTIVE_AUX_RENDER=1",
            "PSP_ME_EFFECT_RENDER_STREAM=0",
            "PSP_ME_RENDER_RETIRE_DIAG=0",
            "PSP_ME_RENDER_PERFORMANCE=1",
            "PSP_ME_STARTUP_BREADCRUMBS=1",
            "PSP_AUDIO4M_BUILD_ID=0x26083126u",
        ):
            self.assertIn(setting, target)
        for marker in ("ME1A I0 W%d", "ME1A IA W%d", "ME1A IR W%d"):
            self.assertIn(marker, self.audio)

    def test_rid28_item_safe_has_exact_acceptance_feature_contract(self):
        target = make_target_body(
            self.makefile, "psp3000-ime7-adaptive-item-safe-build"
        )
        assignments = psp_target_assignments(target)
        identity_keys = {"PSP_AUDIO4M_BUILD_ID", "PSP_EBOOT_TITLE"}
        feature_assignments = {
            key: value
            for key, value in assignments.items()
            if key not in identity_keys
        }
        self.assertEqual(
            feature_assignments,
            {
                "PSP_1000": "0",
                "PSP_DIRECT_GAME": "0",
                "PSP_DIRECT_MUSIC": "0",
                "PSP_PERF_DIAG": "1",
                "PSP_PERF_PROFILE": "PERF_ACCEPT",
                "PSP_PERF_ATTRIB_TARGET": "M2",
                "PSP_PERF_GPU_ATTRIB": "0",
                "PSP_PERF_PLAYER_SHOT": "0",
                "PSP_PERF_EMPTY_TIMERS": "0",
                "PSP_PERF_DENSE_SLICE": "1",
                "PSP_ME_RENDER_WORKER": "1",
                "PSP_ME_RENDER_CORRECTNESS": "1",
                "PSP_ME_RENDER_RETIRE_DIAG": "0",
                "PSP_ME_RENDER_GE_CONSUME": "1",
                "PSP_ME_RENDER_PERFORMANCE": "1",
                "PSP_ME_RENDER_RAW_LIVE": "1",
                "PSP_ME_RENDER_DIRECT_LIST": "1",
                "PSP_ME_BULLET_FAST_UPDATE": "0",
                "PSP_ME_BULLET_COMPACT_UPDATE": "1",
                "PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY": "0",
                "PSP_ME_ITEM_RENDER_STREAM": "1",
                "PSP_ME_EFFECT_RENDER_STREAM": "0",
                "PSP_ME_RENDER_UV16": "0",
                "PSP_ME_RENDER_XYZ16": "0",
                "PSP_ME_RENDER_16BIT_GE_EXPERIMENT": "0",
                "PSP_ME_BULLET_OUTPUT_SLIM": "0",
                "PSP_ME_BULLET_SEED_SLIM": "0",
                "PSP_ME_ITEM_SEED_SLIM": "0",
                "PSP_ME_ADAPTIVE_AUX_RENDER": "1",
                "PSP_ME_ITEM_PREFIX_SPLIT": "1",
                "PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP": "0",
                "PSP_ME_STARTUP_BREADCRUMBS": "1",
                "PSP_BULLET_COLLISION_BROADPHASE": "1",
                "PSP_DIRECT_TRANSITION_TEST": "0",
                "PSP_SHIKIGAMI": "1",
                "PSP_MECC_BGM_384K": "0",
                "PSP_MECC_AUDIO_4M": "1",
                "PSP_BULLET_AXIS_FAST": "0",
                "PSP_BULLET_SNAPSHOT_EMITTER": "0",
                "PSP_BULLET_ROTATED_DIRECT": "1",
                "PSP_BULLET_UNIFIED_QUADS": "1",
                "PSP_BULLET_ONEPASS_ROTATED": "1",
                "PSP_BULLET_HOT_PREFETCH": "0",
                "PSP_BULLET_WARM_QUEUE": "0",
                "PSP_BULLET_STATIC_PROXY": "0",
                "PSP_ENEMY_P5_WARM_QUEUE": "0",
                "PSP_BULLET_QUIESCENT_ANM": "0",
                "PSP_ASCII_POPUP_BATCH": "1",
                "PSP_GUI_TILE_BATCH": "0",
                "PSP_FONT_MAIN_RAM": "1",
                "PSP_TEXT_BLIT_FAST": "1",
                "PSP_TEXT_PREWARM_PROFILE": "1",
                "PSP_USAGE_METER": "1",
                "PSP_USAGE_METER_TOGGLE": "0",
                "PSP_EASY_MIST_AUDIO": "0",
            },
        )

    def test_rid28_item_safe_has_unique_build_identity(self):
        target = make_target_body(
            self.makefile, "psp3000-ime7-adaptive-item-safe-build"
        )
        assignments = psp_target_assignments(target)
        self.assertEqual(assignments["PSP_AUDIO4M_BUILD_ID"], "0x26083128u")
        self.assertEqual(
            assignments["PSP_EBOOT_TITLE"],
            "'TH07 PSP I-ME7 ITEM SAFE'",
        )
        all_ids = re.findall(
            r"\bPSP_AUDIO4M_BUILD_ID=(0x[0-9A-Fa-f]+u)", self.makefile
        )
        self.assertEqual(all_ids.count("0x26083128u"), 1)

    def test_rid29_changes_identity_not_rid28_feature_contract(self):
        rid28 = psp_target_assignments(make_target_body(
            self.makefile, "psp3000-ime7-adaptive-item-safe-build"
        ))
        rid29 = psp_target_assignments(make_target_body(
            self.makefile, "psp3000-ime7-adaptive-item-uncached-build"
        ))
        identity = {"PSP_AUDIO4M_BUILD_ID", "PSP_EBOOT_TITLE"}
        self.assertEqual(
            {key: value for key, value in rid29.items() if key not in identity},
            {key: value for key, value in rid28.items() if key not in identity},
        )
        self.assertEqual(rid29["PSP_AUDIO4M_BUILD_ID"], "0x26083129u")
        self.assertEqual(
            rid29["PSP_EBOOT_TITLE"],
            "'TH07 PSP I-ME7 ITEM UNCACHED'",
        )
        all_ids = re.findall(
            r"\bPSP_AUDIO4M_BUILD_ID=(0x[0-9A-Fa-f]+u)", self.makefile
        )
        self.assertEqual(all_ids.count("0x26083129u"), 1)

    def test_rid27_is_marked_hardware_rejected_and_never_deploy(self):
        marker = self.makefile.index("# RID27 REJECTED ON HARDWARE")
        target = self.makefile.index(
            "psp3000-ime7-adaptive-item-cachefix-build:"
        )
        safe = self.makefile.index("psp3000-ime7-adaptive-item-safe-build:")
        rejected_text = self.makefile[marker:safe]
        self.assertLess(marker, target)
        self.assertIn("Never deploy this target", rejected_text)

    def test_dense_reject_pays_no_item_prepare_walk(self):
        publish = function_body(
            self.bullets, "void PspMeRenderPublishFusedCapture("
        )
        gate = publish.index("PspMeAdaptiveItemPrefixCount(")
        prepare = publish.index("PspPrepareMeItemRenderStream()")
        self.assertLess(gate, prepare)
        update = function_body(self.items, "void ItemManager::OnUpdate()")
        self.assertIn("pspMeItemListCount", update)
        self.assertRegex(
            update,
            r"TH07_PSP_ME_ADAPTIVE_AUX_RENDER[\s\S]*"
            r"PspPrepareMeItemRenderStream",
        )

    def test_item_live_inputs_use_uncached_volatile_me_reads(self):
        layout = function_body(
            self.audio, "static int me_render_stream_item_layout_valid("
        )
        self.assertNotIn("me_render_stream_load_u32(prepareSerial", layout)
        uncached = function_body(
            self.audio,
            "static const volatile unsigned char *me_render_stream_item_uncached(",
        )
        self.assertIn("0x40000000u | physical", uncached)
        load = function_body(
            self.audio, "static uint32_t me_render_stream_item_load_u32("
        )
        self.assertIn("const volatile uint32_t *source", load)

        reconstruct = function_body(
            self.audio,
            "static __attribute__((always_inline)) inline int\n"
            "me_render_stream_reconstruct_item_record(",
        )
        for token in (
            "itemLayout->activeBitsPhys",
            "itemLayout->generationBasePhys",
            "itemLayout->sinBasePhys",
            "itemLayout->cosBasePhys",
            "me_render_stream_item_uncached(itemPhys)",
            "me_render_stream_item_uncached(vmPhys)",
            "me_render_stream_item_load_u32",
            "me_render_stream_item_load_u8",
        ):
            self.assertIn(token, reconstruct)
        self.assertNotIn("0x80000000u | itemPhys", reconstruct)
        self.assertNotIn("me_render_stream_list_prefetch(vm", reconstruct)
        self.assertNotIn("0x80000000u | nextPhys", reconstruct)
        # Immutable sprite metadata and its representative table intentionally
        # retain the cached/prefetched path.
        self.assertIn("0x80000000u | spritePhys", reconstruct)
        self.assertIn("rawLayout->representativePhys", reconstruct)
        self.assertEqual(reconstruct.count("me_render_stream_list_prefetch"), 2)

        finish = function_body(
            self.audio, "static int me_render_stream_item_cursor_finish("
        )
        for token in (
            "prepareSerialPhys",
            "preparedSerialPhys",
            "preparedCountPhys",
            "expectedPrepareSerial",
        ):
            self.assertIn(token, finish)
        self.assertGreaterEqual(
            finish.count("me_render_stream_item_uncached"), 3
        )
        self.assertEqual(
            finish.count("me_render_stream_item_load_u32"), 3
        )
        worker = function_body(self.audio, "process_render_stream_on_me(")
        self.assertIn("meLibDcacheWritebackInvalidateAll()", worker)
        self.assertNotIn("me_render_stream_invalidate_item_inputs", worker)
        self.assertNotIn("meCoreDcacheWritebackInvalidateAll", worker)

        submit = function_body(
            self.audio, "int th07_psp_me_render_stream_submit("
        )
        self.assertIn("sceKernelDcacheWritebackAll()", submit)

    def test_item_selftest_failure_disables_only_item_and_reproves_bullet(self):
        init = function_body(self.audio, "int th07_psp_me_audio_init(void)")
        self.assertIn("me_render_stream_item_failure_recoverable()", init)
        self.assertIn("gMeItemRenderEnabled, 0u", init)
        self.assertIn("initialize_render_stream_slots()", init)
        self.assertEqual(init.count("selftest_render_stream()"), 2)
        self.assertIn(
            "ME1A SELFTEST NG -> ITEM OFF; RETRY BULLET ME", init
        )
        self.assertIn(
            "ME ITEM OFF; BULLET ME ACTIVE (SAFE FALLBACK)", init
        )

        submit = function_body(
            self.audio, "int th07_psp_me_render_stream_submit("
        )
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST", submit)
        self.assertIn("gMeItemRenderEnabled", submit)

        publish = function_body(
            self.bullets, "void PspMeRenderPublishFusedCapture("
        )
        build = function_body(
            self.bullets, "bool PspMeRenderBuildFusedSnapshot("
        )
        self.assertGreaterEqual(
            publish.count("th07_psp_me_item_render_available()"), 2
        )
        self.assertIn("th07_psp_me_item_render_available()", build)

    def test_item_decision_is_latched_for_shikigami_telemetry(self):
        direct = function_body(
            self.audio, "static int selftest_render_stream_direct_list("
        )
        for reason in (
            "TH07_PSP_ME_ITEM_REASON_LIVE_ACQUIRE",
            "TH07_PSP_ME_ITEM_REASON_LIVE_SUBMIT",
            "TH07_PSP_ME_ITEM_REASON_LIVE_CONTRACT",
            "TH07_PSP_ME_ITEM_REASON_AUTH_ACQUIRE",
            "TH07_PSP_ME_ITEM_REASON_AUTH_SUBMIT",
            "TH07_PSP_ME_ITEM_REASON_AUTH_CONTRACT",
            "TH07_PSP_ME_ITEM_REASON_REJECT_ACQUIRE",
            "TH07_PSP_ME_ITEM_REASON_REJECT_SUBMIT",
            "TH07_PSP_ME_ITEM_REASON_REJECT_CONTRACT",
        ):
            self.assertIn(reason, direct)
        self.assertIn("me_item_diag_begin()", direct)
        self.assertIn("me_item_diag_pass()", direct)

        init = function_body(self.audio, "int th07_psp_me_audio_init(void)")
        self.assertIn("gMeItemDiagBulletRetryRuns", init)
        self.assertIn("gMeItemDiagBulletRetryPasses", init)
        self.assertIn("TH07_PSP_ME_ITEM_STATE_SAFE_FALLBACK", init)
        self.assertIn("TH07_PSP_ME_ITEM_REASON_BULLET_RETRY_FAILED", init)
        fallback = init.index("me_render_stream_item_failure_recoverable()")
        self.assertLess(
            init.index("initialize_render_stream_slots();", fallback),
            init.index("TH07_PSP_ME_ITEM_STATE_SAFE_FALLBACK", fallback),
        )

        snapshot = function_body(
            self.audio, "void th07_psp_me_item_render_diag_snapshot("
        )
        for token in (
            "gMeItemDiagState",
            "gMeItemDiagReason",
            "gMeItemDiagSelftestRuns",
            "gMeItemDiagSelftestFailures",
            "gMeItemDiagBulletRetryRuns",
            "gMeItemDiagBulletRetryPasses",
            "gMeItemDiagLastWaitResult",
            "gMeItemDiagLastStreamResult",
            "gMeItemDiagLastItemResult",
        ):
            self.assertIn(token, snapshot)
        for forbidden in ("sceNet", "sceIo", "malloc"):
            self.assertNotIn(forbidden, snapshot)

    def test_boot_selftest_requires_authority_mismatch_fallback(self):
        direct = function_body(
            self.audio, "static int selftest_render_stream_direct_list("
        )
        for token in (
            "itemAuthorityBuild",
            "gMeRenderItemSelftestPreparedSerial",
            "TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD",
            "itemAuthorityCompletion.itemVertexCount != 0u",
            "itemAuthorityCompletion.outputBytes != expectedVertexBytes",
        ):
            self.assertIn(token, direct)

    def test_il02_me_prefix_stops_exactly_at_suffix_head(self):
        audio_h = (ROOT / "psp/audio_me.h").read_text(encoding="utf-8")
        self.assertIn("0x494c3032u", audio_h)
        self.assertIn("expectedTotalCount", audio_h)
        self.assertIn("suffixHeadPhys", audio_h)
        reconstruct = function_body(
            self.audio,
            "static __attribute__((always_inline)) inline int\n"
            "me_render_stream_reconstruct_item_record(",
        )
        self.assertIn("nextPhys != itemLayout->suffixHeadPhys", reconstruct)
        finish = function_body(
            self.audio, "static int me_render_stream_item_cursor_finish("
        )
        self.assertIn("cursor->nextPhys != itemLayout->suffixHeadPhys", finish)
        self.assertIn("itemLayout->expectedTotalCount", finish)

    def test_sc_revalidates_boundary_then_draws_only_suffix(self):
        authority = function_body(
            self.bullets, "bool PspMeRenderItemAuthorityMatches()"
        )
        for token in (
            "state.itemTail->next == state.itemSuffixHead",
            "expectedTotalCount",
            "suffixHeadPhys",
        ):
            self.assertIn(token, authority)
        consume = function_body(
            self.bullets, "bool PspMeRenderTryGeConsumeItem("
        )
        self.assertLess(
            consume.index("PspMeRenderSubmitRunRange("),
            consume.index("PspDrawCanonicalItemSuffix("),
        )

    def test_partial_prefix_still_obeys_busy_veto(self):
        prefix = function_body(
            self.bullets, "u32 PspMeAdaptiveItemPrefixCount("
        )
        self.assertIn("const u32 prefix =", prefix)
        self.assertIn(
            "th07_usage_meter_last_me_percent() >= vetoPercent", prefix
        )
        self.assertIn("PSP_ME_ADAPTIVE_AUX_REJECT_BUSY", prefix)
        self.assertLess(
            prefix.index("const u32 prefix ="),
            prefix.index("th07_usage_meter_last_me_percent() >= vetoPercent"),
        )


if __name__ == "__main__":
    unittest.main()

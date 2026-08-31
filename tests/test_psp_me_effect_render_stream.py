import pathlib
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


class PspMeEffectRenderStreamContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "psp/audio_me.h").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")
        cls.effects_h = (ROOT / "src/EffectManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.effects = (ROOT / "src/EffectManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.bullets = (ROOT / "src/BulletManager.cpp").read_text(
            encoding="utf-8"
        )

    def test_new_abi_is_independent_but_reuses_fixed_auxiliary_pool(self):
        self.assertIn("TH07_PSP_ME_EFFECT_RENDER_STREAM", self.header)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_EFFECT_VERSION", self.header)
        self.assertIn("0x4d453139u", self.header)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST", self.header)
        self.assertIn("effect render stream shares the fixed I-ME7 auxiliary pool", self.header)
        self.assertNotIn("TH07_PSP_ME_RENDER_STREAM_EFFECT_MAX_RECORDS +", self.header)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS = 1100", self.header)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS = 1024", self.header)
        bounds = body(self.audio, "static int me_render_stream_bounds_valid(")
        self.assertIn("if (effectList && !directList)", bounds)
        self.assertNotIn("effectList && (!directList || !itemList)", bounds)

    def test_effect_layout_brackets_live_lists_and_observable_presentation(self):
        for token in (
            "typedef struct Th07PspMeRenderEffectLayout",
            "generationBasePhys",
            "activeBitsPhys",
            "layer0HeadPhys",
            "layer0TailPhys",
            "layer3HeadPhys",
            "layer3TailPhys",
            "prepareSerialPhys",
            "preparedSerialPhys",
            "preparedCountsPhys",
        ):
            self.assertIn(token, self.header)
        for token in (
            "pspMeEffectSlotGenerations[kEffectCapacity]",
            "pspMeEffectRenderSin[kEffectCapacity]",
            "pspMeEffectRenderCos[kEffectCapacity]",
            "pspMeEffectPreparedCounts[2]",
        ):
            self.assertIn(token, self.effects_h)
        prepare = body(
            self.effects, "bool EffectManager::PspPrepareMeEffectRenderStream()"
        )
        self.assertIn("effect->vm.pos = effect->pos1", prepare)
        self.assertIn("PspMeEffectSinCos", prepare)
        self.assertIn("PspIsEffectSlotTracked", prepare)
        self.assertIn("pspMeEffectSlotGenerations[slot]", prepare)

    def test_worker_keeps_item_effect0_effect3_bullet_memory_order(self):
        worker = body(self.audio, "process_render_stream_on_me(")
        item = worker.index("itemResult = me_render_stream_expand_kernel")
        layer0 = worker.index("effectLayer0RecordCount")
        effect_kernel = worker.index(
            "effectResult = me_render_stream_expand_kernel", layer0
        )
        bullet = worker.index("uint32_t result = me_render_stream_expand_kernel")
        self.assertLess(item, effect_kernel)
        self.assertLess(effect_kernel, bullet)
        self.assertIn("effectLayer0VertexCount + effectLayer3VertexCount", worker)
        self.assertIn("effectLayer0RunCount + effectLayer3RunCount", worker)

    def test_both_effect_layers_fallback_atomically(self):
        worker = body(self.audio, "process_render_stream_on_me(")
        reject = worker[worker.index("if (effectResult !=") :]
        for token in (
            "effectLayer0VertexCount = 0u",
            "effectLayer0RunCount = 0u",
            "effectLayer3VertexCount = 0u",
            "effectLayer3RunCount = 0u",
            "TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_VERTEX_BYTES",
        ):
            self.assertIn(token, reject)
        self.assertIn("me_render_stream_effect_lists_prevalidate", worker)

    def test_ge_promotion_revalidates_effect_completion(self):
        mark = body(
            self.audio,
            "int th07_psp_me_render_stream_mark_ge_in_flight(",
        )
        self.assertIn(
            "me_render_stream_effect_completion_valid(completion, job)",
            mark,
        )

    def test_ready_validation_has_no_submission_or_draw_side_effect(self):
        validate = body(
            self.effects,
            "bool EffectManager::PspValidateMeEffectRenderStream(",
        )
        self.assertNotIn("submitRuns(", validate)
        self.assertNotIn("PspDrawCanonicalEffectLayer", validate)
        self.assertIn("PspMeEffectRenderAuthorityMatches", validate)
        consume = body(
            self.effects,
            "bool EffectManager::PspConsumeMeEffectRenderStream(",
        )
        self.assertIn("PspValidateMeEffectRenderStream", consume)
        self.assertLess(
            consume.index("submitRuns(context, layer0FirstRun"),
            consume.index("PspDrawCanonicalEffectLayer(2)"),
        )
        self.assertLess(
            consume.index("PspDrawCanonicalEffectLayer(2)"),
            consume.index("g_AnmManager->Flush()"),
        )
        self.assertLess(
            consume.index("g_AnmManager->Flush()"),
            consume.index("submitRuns(context, layer3FirstRun"),
        )

    def test_priority9_effect_and_priority10_suffix_share_one_ge_owner(self):
        effect_draw = body(self.effects, "u32 EffectManager::OnDraw(")
        self.assertIn("Th07PspTryConsumeMeEffectStream()", effect_draw)
        owner = body(self.bullets, "bool Th07PspTryConsumeMeEffectStream()")
        self.assertLess(
            owner.index("PspValidateMeEffectRenderStream"),
            owner.index("Th07PspBeginMeRenderGeSubmission"),
        )
        self.assertIn("state.prefixGeSubmissionOpen = true", owner)
        self.assertNotIn("Th07PspEndMeRenderGeSubmission();\n    return true", owner)

        item = body(self.bullets, "bool PspMeRenderTryGeConsumeItem(")
        self.assertIn("state.prefixGeSubmissionOpen", item)
        self.assertIn("PspMeRenderReusePrefixGeValidation", item)
        self.assertIn("!state.prefixGeSubmissionOpen &&", item)

        bullet = body(self.bullets, "bool PspMeRenderTryGeConsume(")
        for token in (
            "state.ready.effectLayer0RunCount",
            "state.ready.effectLayer3RunCount",
            "state.ready.effectLayer0VertexCount",
            "state.ready.effectLayer3VertexCount",
        ):
            self.assertIn(token, bullet)
        self.assertIn("PspMeRenderReusePrefixGeValidation", bullet)
        self.assertIn("Th07PspEndMeRenderGeSubmission()", bullet)


if __name__ == "__main__":
    unittest.main()
